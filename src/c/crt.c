#include <pebble.h>
#include "chime.h"
#include "crt.h"
#include "drawing.h"
#include "layout.h"
#include "main.h"
#include "theme.h"

// The pass runs per pixel on every CRT frame; the SDK's -Os trades its speed
// for code size the app can afford. Pragma, not a function attribute, so the
// inlined helpers below share the level; popped at the end of the file, where
// the host tests' single translation unit continues with other modules.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("O3")
#endif

// Everything runs on GColor8 bytes (0xAARRGGBB, 2 bits per channel) captured
// from the framebuffer. Emery-only build; the face targets no B&W platform.

#define GCOLOR8_ALPHA 0xC0

// Current warm-up phase, or CRT_FLASH_IDLE. Advanced by the self-re-arming
// flash tick; written by crt_backlight_handler on backlight-on.
static int s_flash_phase = CRT_FLASH_IDLE;
static AppTimer* s_flash_timer = NULL;  // latest tick arm — cancelled on retrigger
// Strike primed by the backlight handler, started by the overlay proc AFTER
// the wake frame lands: the wake burst (bg fill, canvas glyphs, clock, pass)
// blocks long enough to drain the audio ring once — an audible pop-gap-hum.
// Delaying sound+chain past that frame keeps the ring fed.
static bool s_strike_pending = false;
// Updated by the App Focus service (subscribed in main.c): a notification,
// alarm, modal or a launched app covering the face drops it (Quick View does
// not; the backlight handler gates it on s_quick_view_active). The
// face counts as covered until the compositor's first did_focus(true) after
// launch. Gates the strike sound and the backlight handler's priming — a
// covered tube starts nothing and stays silent.
static bool s_app_in_focus = false;
// A cover's close pumps the backlight once, in no guaranteed order against
// focus returning; that flash belongs to the cover. Armed when the cover
// appears over an enabled effect, eaten by the next in-focus backlight-on.
// The service reports edges only, so a backlight-off in view drops it: the
// next backlight-on after that is a fresh wake. A cover can also close with no
// backlight edge at all, so once focus returns the grace timer expires it.
static bool s_cover_suppresses_strike = false;
static AppTimer* s_cover_grace_timer = NULL;
// A button press lights the backlight while the face is still in focus, then
// the firmware closes the face for the menu — about 120-170 ms later, with no
// button or focus event the face could see. So only a wrist shake (the motion
// backlight) primes at once; any other backlight-on waits here, and
// crt_deinit or a cover cancels it before it strikes.
static AppTimer* s_button_wake_timer = NULL;
// The kernel forwards a shake to the app before the backlight-on it causes.
// The flag is consumed by the next backlight edge; the expiry timer clears it
// when the shake lit nothing (light already on, motion backlight disabled).
static bool s_shake_wake = false;
static AppTimer* s_shake_wake_timer = NULL;
// The raw composed frame (canvas and clock, no pass) a strike gathers from,
// borrowed from the canvas's scene cache on the strike's first frame. NULL
// outside a strike, and for one whose borrow found no cache (malloc failed):
// that strike runs the passes in place on the live frame.
static const uint8_t* s_strike_src = NULL;

static void crt_flash_start(void* data);
static void crt_flash_tick(void* data);

#ifndef TEST_ENV
static int timing_bake_ms;
static int timing_gather_ms;
static int timing_vig_ms;
static time_t timing_mark_s;
static uint16_t timing_mark_ms;
#define TIMING_MARK() time_ms(&timing_mark_s, &timing_mark_ms)
#else
#define TIMING_MARK()
#endif

static void crt_cover_grace_cancel(void) {
  if (s_cover_grace_timer) {
    app_timer_cancel(s_cover_grace_timer);
    s_cover_grace_timer = NULL;
  }
}

static void crt_cover_grace_expire(void* data) {
  (void)data;
  // One-shot timers free themselves on firing.
  s_cover_grace_timer = NULL;
  s_cover_suppresses_strike = false;
}

static void crt_button_wake_cancel(void) {
  if (s_button_wake_timer) {
    app_timer_cancel(s_button_wake_timer);
    s_button_wake_timer = NULL;
  }
}

static void crt_shake_wake_clear(void) {
  if (s_shake_wake_timer) {
    app_timer_cancel(s_shake_wake_timer);
    s_shake_wake_timer = NULL;
  }
  s_shake_wake = false;
}

static void crt_shake_wake_expire(void* data) {
  (void)data;
  s_shake_wake_timer = NULL;
  s_shake_wake = false;
}

void crt_tap_handler(AccelAxisType axis, int32_t direction) {
  (void)axis;
  (void)direction;
  if (s_shake_wake_timer) app_timer_cancel(s_shake_wake_timer);
  s_shake_wake = true;
  s_shake_wake_timer = app_timer_register(CRT_SHAKE_WAKE_WINDOW_MS, crt_shake_wake_expire, NULL);
}

static int isqrt_floor(int v) {
  if (v <= 0) return 0;
  int r = 1;
  while ((r + 1) <= v / (r + 1)) r++;  // no overflow: r*r <= v
  return r;
}

// Parallel 4x4 ordered-dither threshold grid — the only way a 4-level
// channel ramps smoothly: the pane quantizes c*f/256 with this threshold, so
// the vignette falloff shows as dither pattern, not banded steps.
static const uint8_t s_bayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

// Radial magnification of the curvature stage, Q16, from the elliptical
// squared radius xq + yq (Q8, the same formulation as the CA zone map).
// Identity at the centre, growing smoothly to 1.06x in the corners. Both axes
// feed it, so a curved surface bows its sides too, and the per-column boundary
// curve it produces keeps the outer columns from quantizing onto shared row
// boundaries (visible seams). CRT_WARP_R2_K sets its strength.
static inline int warp_mul16(int xq_plus_yq) {
  return 65536 + CRT_WARP_R2_K * xq_plus_yq;
}

// Edge distance per axis, on the LUTs' 0..CRT_VIGNETTE_PX domain: each axis is
// scaled by its own band constant, so the curves stay fixed while the rims
// resize. The two bands are not in the same units: the falloff is defined in
// content space and the pane pushes it through the warp, so the side band is
// measured before the warp's ~1.09x rim compression while the top/bottom band —
// which the warp never touches — is measured on the glass.
static inline int edge_depth_x(int edge_px) {
  return edge_px * CRT_VIGNETTE_PX / CRT_VIGNETTE_SIDE_PX;
}

// CRT_VIGNETTE_EDGE_PX rows of depth 0 first, standing in for the warp's
// clamped columns, then the falloff over what remains of the band.
static inline int edge_depth_y(int edge_px) {
  return edge_px < CRT_VIGNETTE_EDGE_PX ? 0
                                        : (edge_px - CRT_VIGNETTE_EDGE_PX + 1) * CRT_VIGNETTE_PX /
                                              (CRT_VIGNETTE_BAND_PX - CRT_VIGNETTE_EDGE_PX + 1);
}

// Inside the corner square the boundary is the arc, not the straight edge:
// depth below the arc through (ex, ey), both < CRT_CORNER_RADIUS.
static inline int corner_depth(int ex, int ey) {
  int rx = CRT_CORNER_RADIUS - ex;
  int ry = CRT_CORNER_RADIUS - ey;
  int d = CRT_CORNER_RADIUS - isqrt_floor(rx * rx + ry * ry);
  return d < 0 ? 0 : d;
}

// Horizontal CA from the squared Q8 radius (units: xq + yq), returning
// per-channel displacement in THIRDS of a pixel: 0 inside the dead zone, then
// ramping to 4 (1+1/3 px) over CRT_CA_RAMP_SHIFT — see crt.h for why it ramps
// rather than steps. Thirds because the pass samples channels fractionally
// (two weighted taps) instead of copying whole neighbours — see stage 1 in
// crt_apply_framebuffer. Zone boundary squared once: rq ≥ R ⇔ xq+yq ≥
// ceil(R²/256) — so no per-pixel sqrt is needed.
static inline int crt_ca_t3_h2(int xy_sum) {
  if (xy_sum <= CRT_CA_R2_X2Q8) return 0;
  int t3 = CRT_CA_RAMP_FLOOR + ((xy_sum - CRT_CA_R2_X2Q8) >> CRT_CA_RAMP_SHIFT);
  return t3 > 4 ? 4 : t3;
}
// Vertical wider zones, 1px max, same formulation.
static inline int crt_ca_shift_v2(int xy_sum) {
  return xy_sum < CRT_CA_R2V_X2Q8 ? 0 : 1;
}
// A text box spans its window between the side strokes and the bottom stroke,
// each cleared by one flat px so the whole-pixel taps never read a stroke, and
// from two flat rows above the title's ink (the 16px cell's ink sits in its
// lower 10 rows, so it starts 2 above the window's origin row) down to its
// bottom clearance. Neighbours' strokes (the stub above the next row's titles)
// stay outside by the same margins.
#define TEXT_BOX_PAD_X (WINDOW_BORDER_PX + 1)
#define TEXT_BOX_TOP_DY (-4)

static CrtTextBox make_text_box(GRect window, int y1) {
  CrtTextBox b = {.x0 = window.origin.x + TEXT_BOX_PAD_X,
                  .x1 = window.origin.x + window.size.w - TEXT_BOX_PAD_X,
                  .y0 = window.origin.y + TEXT_BOX_TOP_DY,
                  .y1 = y1};
  // Doubled offsets of the box centre from the screen's, the same units as
  // the pass's per-pixel ones, so the shift is the ramp evaluated there.
  int dx = b.x0 + b.x1 - 1 - (SCREEN_W - 1);
  int dy = b.y0 + b.y1 - 1 - (SCREEN_H - 1);
  int xy_sum = (dx * dx * 256) / ((SCREEN_W - 1) * (SCREEN_W - 1)) +
               (dy * dy * 256) / ((SCREEN_H - 1) * (SCREEN_H - 1));
  b.left = dx < 0;
  b.sh = (crt_ca_t3_h2(xy_sum) + 1) / 3;
  b.sv = crt_ca_shift_v2(xy_sum);
  return b;
}

// Built on first use: the layout is fixed (Quick View hides the bottom row, it
// does not move anything), so the table is too. Only the four corner slots are
// boxes; the middle slots, the TIME window and the clock keep the ramp. Boxes
// sharing a row band run left to right, which the pass relies on.
static const CrtTextBox* text_boxes(void) {
  static CrtTextBox boxes[CRT_TEXT_BOXES];
  static bool built = false;
  if (!built) {
    const GRect windows[CRT_TEXT_BOXES] = {SLOT_RECT_TOP_LEFT, SLOT_RECT_TOP_RIGHT,
                                           SLOT_RECT_BOTTOM_LEFT, SLOT_RECT_BOTTOM_RIGHT};
    for (int i = 0; i < CRT_TEXT_BOXES; i++) {
      boxes[i] =
          make_text_box(windows[i], windows[i].origin.y + windows[i].size.h - TEXT_BOX_PAD_X);
    }
    built = true;
  }
  return boxes;
}

// The vignette falloff over edge-depth d, Q8; smoothstep gamma-lifted by 0.7 —
// 4 levels quantize the raw smoothstep into a noticed black cliff near the rim
// (hardware A/B: the fade 'started too sharp'). Lifting the low half softens
// the rim start and raises the mid band ~15% without widening the fade.
// A LUT: the pass does no per-pixel division, which is what kept the wake-up
// at 3–4 fps on hardware.
static const uint16_t s_vignette_q8[CRT_VIGNETTE_PX + 1] = {
    0, 11, 28, 48, 70, 92, 114, 136, 158, 178, 196, 213, 227, 239, 248, 254, 256};

// Light backgrounds swap the curve, not the depth: the dark smoothstep sweeps
// half its levels across a bright field — on a 4-level panel that reads as
// black pepper, not a falloff — so this one plateaus at depth 11 and carries
// the darkening as dot density, a bezel ring instead of dirt.
//
// The low end is spaced by where a LEVEL-2 field crosses its own quantization
// boundaries, which is what the eye reads as the shape of the rim. That field
// has two steps to make (2 -> 1 -> 0) against a level-1 field's one, and it
// hits DarkGray at f=128: a quartic ease-in put that at depth 3 of 11, so
// black sat four depths from a nearly-full field with almost no mid-tone
// between. Here f=132 lands at depth 5, splitting the band evenly — depths
// 1-4 ramp black to DarkGray, depth 5 is solid DarkGray, depths 6-10 ramp on
// to LightGray.
//
// The floor of 20 at depth 1 is set by the OTHER field level. A level-1 field
// (Navigator) has no shade between black and its ground, so its whole rim is
// dot density, and it lights no dot at all until f reaches 16 — f=12 there was
// a second solid-black depth, thickening the bar and punching a black column
// back through the middle of the ramp at wider side bands. Keep depth 1 above
// 16 for that reason; it is nearly free on a level-2 field (6% lit -> 12%).
static const uint16_t s_vignette_q8_light[CRT_VIGNETTE_PX + 1] = {
    0, 20, 40, 64, 96, 132, 172, 205, 228, 241, 249, 256, 256, 256, 256, 256, 256};

// The falloff curve a theme's field selects, chosen once per frame by the pass.
static inline const uint16_t* vignette_lut(int ground_level) {
  return ground_level > 0 ? s_vignette_q8_light : s_vignette_q8;
}

#ifdef TEST_ENV
// Test-only accessors: the text box table, and the falloff gain by depth (the
// vignette ramp tool reads it).
int crt_text_boxes(const CrtTextBox** boxes) {
  *boxes = text_boxes();
  return CRT_TEXT_BOXES;
}

int crt_vignette_q8_from_depth(int d) {
  if (d >= CRT_VIGNETTE_PX) return 256;
  return vignette_lut(theme_ground_level(s_active_theme))[d];
}
#endif

// Strike amplitude per frame, in sixteenths of a px: sudden, then decaying to
// nothing by 540 ms. The pulse scales with it; at the first frame's 6 px a
// corner moves CRT_STRIKE_REACH_PX rows, the most any pixel moves vertically,
// which sizes the gather's row ring. The sub-pixel steps keep consecutive
// frames from repeating.
#define CRT_STRIKE_AMP_SHIFT 4
static const int s_strike_amp_q4[CRT_FLASH_PHASES] = {96, 80, 64, 56, 48, 40, 32, 24,
                                                      16, 16, 16, 8,  0,  0,  0,  0};
#define CRT_STRIKE_REACH_PX 20

static int16_t crt_wave_q15(uint32_t phase_q16);

// The coil's field pulls the raster's edges in, in curves: a pincushion pulse
// that is nothing in the middle and climbs, per axis, with the squared
// distance from that axis's centre line (the warp's xq across, yq down, 256 at
// the rim), so a whole edge bows, flipping between pulled in and pushed out with
// the mains every frame and dying with the amplitude. The pull is not even
// along an edge: each axis's is modulated by lobes, one cycle per edge, the
// pull running from ~0.2x to ~1.8x of its base (CRT_STRIKE_LOBE_Q8 of 256
// either way), and the lobes drift a different fraction of a turn per frame on
// each axis, so the raster bows in curves that change every frame. Q16 per
// amplitude px, before the weight and the lobes, per axis: at amplitude 6 the
// pull reaches 20px sideways and CRT_STRIKE_REACH_PX (20) rows vertically,
// never more, in any frame.
#define CRT_STRIKE_PULSE_X_Q16 2500
#define CRT_STRIKE_PULSE_Y_Q16 2138
#define CRT_STRIKE_LOBE_Q8 200
// Turns per frame, Q16: the sideways pull's lobes run down the rows, the
// vertical pull's along the columns.
#define CRT_STRIKE_LOBE_STEP_X 8192
#define CRT_STRIKE_LOBE_STEP_Y 4915

static void crt_strike_pulse(int phase, int* zx, int* zy) {
  int amp = (phase & 1 ? -1 : 1) * s_strike_amp_q4[phase];
  *zx = amp * CRT_STRIKE_PULSE_X_Q16 / (1 << CRT_STRIKE_AMP_SHIFT);
  *zy = amp * CRT_STRIKE_PULSE_Y_Q16 / (1 << CRT_STRIKE_AMP_SHIFT);
}

// The CA separation the strike adds, in whole px: the channels balloon while the
// mask degausses. Phase-indexed through the same decay table as the pulse.
static int strike_ca_boost(int phase) {
  if (phase < 0 || phase >= CRT_FLASH_PHASES) return 0;
  int amp_px = (s_strike_amp_q4[phase] + (1 << (CRT_STRIKE_AMP_SHIFT - 1))) >> CRT_STRIKE_AMP_SHIFT;
  return (amp_px + 1) / 2;
}

// The Q8 factor of the lobes at position `pos` of `extent` along an edge.
static inline int strike_lobe_q8(int pos, int extent, int phase, int step) {
  uint32_t turn = (uint32_t)pos * 65536 / extent + (uint32_t)phase * step;
  return 256 + ((CRT_STRIKE_LOBE_Q8 * crt_wave_q15(turn)) >> 15);
}

// A Q17 displacement weighted by an axis's rim term (Q8, 256 at the rim), to
// whole px, nearest. Two bits are dropped first: the lobed displacement
// reaches ~21M, which times 256 would not fit.
static inline int strike_pulse_px(int lin_q17, int rq) {
  return (((lin_q17 >> 2) * rq) + (1 << 23)) >> 24;
}

// The mask demagnetising shows as purity stripes: every pixel's hue is rotated
// by an angle that is nothing in the middle and grows toward the rim (a field
// of 3 * yq + xq, 1024 in a corner), so the stripes are its iso-lines. The
// angle runs in sixths of a turn, `band` of them: the even bands are the
// channel permutations R->G->B->R and R->B->G->R (band 0 is the picture as it
// is), the odd ones the average of their neighbours, which is what makes the
// stripes soft. The count falls with the square of the amplitude: dozens of
// bands at amplitude 6, a fraction of one cycle on the last frames. A grey has
// no hue to rotate and takes a one-step tint of the band's hue instead; black
// stays black. Q13.
#define CRT_STRIKE_STRIPES_Q13 11

static int strike_stripes(int phase) {
  return s_strike_amp_q4[phase] * s_strike_amp_q4[phase] * CRT_STRIKE_STRIPES_Q13 >>
         (2 * CRT_STRIKE_AMP_SHIFT);
}

#ifdef TEST_ENV
// The displacement the gather applies at dest (x, y), from the same pulse
// terms, doubled-coordinate Q17 sums, lobes and weight.
void crt_strike_field(int x, int y, int phase, int* dx, int* dy) {
  int zx, zy;
  crt_strike_pulse(phase, &zx, &zy);
  int tdx = 2 * x - (SCREEN_W - 1);
  int tdy = 2 * y - (SCREEN_H - 1);
  int xq = tdx * tdx * 256 / ((SCREEN_W - 1) * (SCREEN_W - 1));
  int yq = tdy * tdy * 256 / ((SCREEN_H - 1) * (SCREEN_H - 1));
  int mod_y = strike_lobe_q8(y, SCREEN_H, phase, CRT_STRIKE_LOBE_STEP_X);
  int mod_x = strike_lobe_q8(x, SCREEN_W, phase, CRT_STRIKE_LOBE_STEP_Y);
  *dx = strike_pulse_px(((zx * tdx >> 4) * mod_y) >> 4, xq);
  *dy = strike_pulse_px(((zy * tdy >> 4) * mod_x) >> 4, yq);
}

// The hue band at dest (x, y) in frame `phase`.
int crt_strike_band(int x, int y, int phase) {
  int tdx = 2 * x - (SCREEN_W - 1);
  int tdy = 2 * y - (SCREEN_H - 1);
  int field = 3 * (tdy * tdy * 256 / ((SCREEN_H - 1) * (SCREEN_H - 1))) +
              tdx * tdx * 256 / ((SCREEN_W - 1) * (SCREEN_W - 1));
  return ((field * strike_stripes(phase)) >> 13) % 6;
}
#endif

// Quantize a Q4 channel level to the panel's four steps, the pixel's Bayer
// threshold breaking the rounding direction.
static int dither_q4(int v_q4, int t) {
  int v = (v_q4 + t) >> 4;
  return v > 3 ? 3 : v;
}

// The gain in Q4: c * f / 256 — shift-only (f <= 256 keeps c*16*f under 12k).
static int gain_q4(int c, int f_q8) {
  return c * 16 * f_q8 >> 8;
}

// One channel through the falloff: gain, then ordered-dither into the panel's
// four steps. The channels are dithered out of phase (the pane), so on a lit
// field the three rarely reach 0 on the same pixel and pure black stays a small
// share of the band instead of a scatter.
static int vignette_level(int c, int f_q8, int t) {
  return dither_q4(gain_q4(c, f_q8), t);
}

static int max2(int a, int b) {
  return a > b ? a : b;
}

static bool is_grey(uint8_t c) {
  return ((c >> 4) & 3) == ((c >> 2) & 3) && ((c >> 2) & 3) == (c & 3);
}

// A channel the source had may not blend to 0 while another channel of the
// same pixel survives — that renders one palette color as a different one.
// Dialog's yellow is brown (2,1,0), CGA having no dark yellow, and its red and
// green take different values under the blend: the weights that zero the green
// render (r,0,0), which is that theme's own alarm red. Holding the channel at 1
// keeps a fill inside its hue family until the whole pixel reaches black.
static int hold_hue(int v, int src, int other1, int other2) {
  return (src > 0 && v == 0 && (other1 > 0 || other2 > 0)) ? 1 : v;
}

// Where each channel of a one-level blend turns on, as coverage of the
// brighter tap in 1/256: the channels step in thirds of a level instead of all
// at one half, so a one-level edge (a dark-grey frame on black) moves in
// sub-pixel steps rather than whole-pixel jumps. B leads and R lags, as in the
// vignette's phases, so the thin edge decays cool.
static const uint8_t s_blend_on[3] = {213, 128, 43};  // R, G, B: 5/6, 1/2, 1/6

// The blend of two taps `fr` of 256 of the way across, per channel level.
static void blend_levels(uint8_t p0, uint8_t p1, int fr, int* r, int* g, int* b) {
  int* const out[3] = {r, g, b};
  int c0[3] = {(p0 >> 4) & 3, (p0 >> 2) & 3, p0 & 3};
  int c1[3] = {(p1 >> 4) & 3, (p1 >> 2) & 3, p1 & 3};
  const bool thirds = abs(c0[0] - c1[0]) <= 1 && abs(c0[1] - c1[1]) <= 1 && abs(c0[2] - c1[2]) <= 1;
  for (int i = 0; i < 3; i++) {
    if (thirds) {
      const int cover = c1[i] > c0[i] ? fr : 256 - fr;
      *out[i] = (c0[i] < c1[i] ? c0[i] : c1[i]) + (c0[i] != c1[i] && cover >= s_blend_on[i]);
    } else {
      *out[i] = ((256 - fr) * c0[i] + fr * c1[i] + 128) >> 8;
    }
  }
}

// Vertical CA samples neighbour rows; the row the pass writes into must stay
// raw for the reader. Ring of the last three raw rows, populated in the row's
// own turn, read for ±s_v offsets below/above.
static uint8_t s_vraw_ring[3][SCREEN_W];

// A text box's row of CA, from the raw source rows: R and B are exact copies
// `sh` whole px over (R from the right on the left half), G is the pixel's own.
static void ca_box_span(uint8_t* out, const uint8_t* row, const uint8_t* rrow, const uint8_t* brow,
                        const CrtTextBox* b, int boost) {
  int sh = b->sh + boost;
  int dr = b->left ? sh : -sh;
  for (int x = b->x0; x < b->x1; x++) {
    out[x] = GCOLOR8_ALPHA | (rrow[x + dr] & 0x30) | (row[x] & 0x0C) | (brow[x - dr] & 0x03);
  }
}

// The pixel with its channels cycled by `band` (1..5), alpha kept: even bands
// take the channels rotated by band / 2 places, odd ones average that with one
// place further.
static uint8_t strike_hue(uint8_t v, int band) {
  const int c[6] = {(v >> 4) & 3, (v >> 2) & 3, v & 3, (v >> 4) & 3, (v >> 2) & 3, v & 3};
  const int k = 3 - (band >> 1);
  int o[3];
  for (int i = 0; i < 3; i++) o[i] = (band & 1) ? (c[i + k] + c[i + k - 1] + 1) >> 1 : c[i + k];
  return GCOLOR8_ALPHA | (uint8_t)((o[0] << 4) | (o[1] << 2) | o[2]);
}

#define STRIKE_TINT(r, g, b) ((uint8_t)(GCOLOR8_ALPHA | ((r) << 4) | ((g) << 2) | (b)))

// A grey's stripes by level and band: tinted toward the band's hue (yellow,
// green, cyan, blue, magenta) by raising its channels a step and lowering the
// complementary ones a step, so the stripe reads as colour, not as a lighter
// grey; white has no room to rise and lowers the complement two. Black is lit
// by nothing, so it stays black; band 0 is the grey as it is.
static const uint8_t s_strike_grey_tint[4][6] = {
    {STRIKE_TINT(0, 0, 0), STRIKE_TINT(0, 0, 0), STRIKE_TINT(0, 0, 0), STRIKE_TINT(0, 0, 0),
     STRIKE_TINT(0, 0, 0), STRIKE_TINT(0, 0, 0)},
    {STRIKE_TINT(1, 1, 1), STRIKE_TINT(2, 2, 0), STRIKE_TINT(0, 2, 0), STRIKE_TINT(0, 2, 2),
     STRIKE_TINT(0, 0, 2), STRIKE_TINT(2, 0, 2)},
    {STRIKE_TINT(2, 2, 2), STRIKE_TINT(3, 3, 1), STRIKE_TINT(1, 3, 1), STRIKE_TINT(1, 3, 3),
     STRIKE_TINT(1, 1, 3), STRIKE_TINT(3, 1, 3)},
    {STRIKE_TINT(3, 3, 3), STRIKE_TINT(3, 3, 1), STRIKE_TINT(1, 3, 1), STRIKE_TINT(1, 3, 3),
     STRIKE_TINT(1, 1, 3), STRIKE_TINT(3, 1, 3)},
};

// The stripes of every pixel value in every band, for the gather's one read per
// pixel: a grey takes s_strike_grey_tint's tint, anything else strike_hue's
// rotation. Indexed [band][pixel & 63]; band 0 is the pixel as it is. Written
// out from those two, which test_crt_strike_band_table_should_match_the_tint_and_hue_rules
// pins entry by entry.
static const uint8_t s_strike_band_px[6][64] = {
    {0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC,
     0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9,
     0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6,
     0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF2, 0xF3,
     0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF},
    {0xC0, 0xD1, 0xD1, 0xE2, 0xC5, 0xD5, 0xD6, 0xE6, 0xC5, 0xD6, 0xD6, 0xE7, 0xCA,
     0xDA, 0xDB, 0xEB, 0xD4, 0xD5, 0xE5, 0xE6, 0xD5, 0xE8, 0xE6, 0xE6, 0xD9, 0xDA,
     0xEA, 0xEB, 0xDA, 0xDA, 0xEB, 0xEB, 0xD4, 0xE5, 0xE5, 0xF6, 0xD9, 0xE9, 0xEA,
     0xFA, 0xD9, 0xEA, 0xFD, 0xFB, 0xDE, 0xEE, 0xEF, 0xFF, 0xE8, 0xE9, 0xF9, 0xFA,
     0xE9, 0xE9, 0xFA, 0xFA, 0xED, 0xEE, 0xFE, 0xFF, 0xEE, 0xEE, 0xFF, 0xFD},
    {0xC0, 0xD0, 0xE0, 0xF0, 0xC1, 0xD1, 0xE1, 0xF1, 0xC2, 0xD2, 0xE2, 0xF2, 0xC3,
     0xD3, 0xE3, 0xF3, 0xC4, 0xD4, 0xE4, 0xF4, 0xC5, 0xC8, 0xE5, 0xF5, 0xC6, 0xD6,
     0xE6, 0xF6, 0xC7, 0xD7, 0xE7, 0xF7, 0xC8, 0xD8, 0xE8, 0xF8, 0xC9, 0xD9, 0xE9,
     0xF9, 0xCA, 0xDA, 0xDD, 0xFA, 0xCB, 0xDB, 0xEB, 0xFB, 0xCC, 0xDC, 0xEC, 0xFC,
     0xCD, 0xDD, 0xED, 0xFD, 0xCE, 0xDE, 0xEE, 0xFE, 0xCF, 0xDF, 0xEF, 0xDD},
    {0xC0, 0xD4, 0xD4, 0xE8, 0xD1, 0xD5, 0xE5, 0xE9, 0xD1, 0xE5, 0xE5, 0xF9, 0xE2,
     0xE6, 0xF6, 0xFA, 0xC5, 0xD5, 0xD9, 0xE9, 0xD5, 0xCA, 0xE9, 0xE9, 0xD6, 0xE6,
     0xEA, 0xFA, 0xE6, 0xE6, 0xFA, 0xFA, 0xC5, 0xD9, 0xD9, 0xED, 0xD6, 0xDA, 0xEA,
     0xEE, 0xD6, 0xEA, 0xDF, 0xFE, 0xE7, 0xEB, 0xFB, 0xFF, 0xCA, 0xDA, 0xDE, 0xEE,
     0xDA, 0xDA, 0xEE, 0xEE, 0xDB, 0xEB, 0xEF, 0xFF, 0xEB, 0xEB, 0xFF, 0xDF},
    {0xC0, 0xC4, 0xC8, 0xCC, 0xD0, 0xD4, 0xD8, 0xDC, 0xE0, 0xE4, 0xE8, 0xEC, 0xF0,
     0xF4, 0xF8, 0xFC, 0xC1, 0xC5, 0xC9, 0xCD, 0xD1, 0xC2, 0xD9, 0xDD, 0xE1, 0xE5,
     0xE9, 0xED, 0xF1, 0xF5, 0xF9, 0xFD, 0xC2, 0xC6, 0xCA, 0xCE, 0xD2, 0xD6, 0xDA,
     0xDE, 0xE2, 0xE6, 0xD7, 0xEE, 0xF2, 0xF6, 0xFA, 0xFE, 0xC3, 0xC7, 0xCB, 0xCF,
     0xD3, 0xD7, 0xDB, 0xDF, 0xE3, 0xE7, 0xEB, 0xEF, 0xF3, 0xF7, 0xFB, 0xD7},
    {0xC0, 0xC5, 0xC5, 0xCA, 0xD4, 0xD5, 0xD9, 0xDA, 0xD4, 0xD9, 0xD9, 0xDE, 0xE8,
     0xE9, 0xED, 0xEE, 0xD1, 0xD5, 0xD6, 0xDA, 0xD5, 0xE2, 0xDA, 0xDA, 0xE5, 0xE9,
     0xEA, 0xEE, 0xE9, 0xE9, 0xEE, 0xEE, 0xD1, 0xD6, 0xD6, 0xDB, 0xE5, 0xE6, 0xEA,
     0xEB, 0xE5, 0xEA, 0xF7, 0xEF, 0xF9, 0xFA, 0xFE, 0xFF, 0xE2, 0xE6, 0xE7, 0xEB,
     0xE6, 0xE6, 0xEB, 0xEB, 0xF6, 0xFA, 0xFB, 0xFF, 0xFA, 0xFA, 0xFF, 0xF7}};

// The pixel as stage 1 would fringe it, from one source row: R taken `dr` px
// over, B the opposite way, G its own. Columns clamp at the frame's edges.
static inline uint8_t strike_ca_pixel(const uint8_t* srow, int x, int dr, int w) {
  if (!dr) return srow[x];
  int rx = x + dr, bx = x - dr;
  rx = rx < 0 ? 0 : rx >= w ? w - 1 : rx;
  bx = bx < 0 ? 0 : bx >= w ? w - 1 : bx;
  return GCOLOR8_ALPHA | (srow[rx] & 0x30) | (srow[x] & 0x0C) | (srow[bx] & 0x03);
}

// The pixel at column `sx` of a source row as stage 2 would blend it: the
// fringed pixel and its right neighbour, `fr` of 256 of the way across.
static inline uint8_t strike_blend(const uint8_t* srow, int sx, int dr, int w, int fr) {
  uint8_t p0 = strike_ca_pixel(srow, sx, dr, w);
  uint8_t p1 = fr ? strike_ca_pixel(srow, sx + 1, dr, w) : p0;
  uint8_t v = p0;
  if (p0 != p1) {
    int r, g, b;
    blend_levels(p0, p1, fr, &r, &g, &b);
    if (!is_grey(srow[sx])) {
      int sr = max2((p0 >> 4) & 3, (p1 >> 4) & 3);
      int sg = max2((p0 >> 2) & 3, (p1 >> 2) & 3);
      int sb = max2(p0 & 3, p1 & 3);
      int r0 = r, g0 = g, b0 = b;
      r = hold_hue(r0, sr, g0, b0);
      g = hold_hue(g0, sg, r0, b0);
      b = hold_hue(b0, sb, r0, g0);
    }
    v = GCOLOR8_ALPHA | (uint8_t)((r << 4) | (g << 2) | b);
  }
  return v;
}

// A strike frame's gather: every dest pixel taken from the source frame. The
// source is the curvature warp's position (the plain column without curvature)
// displaced by the strike's pincushion pulse; beyond the raster it is black.
// The nearest row, and the warp's column pair blend, as stage 2 below, then the
// purity stripes. A captured frame has the warp baked in (crt_strike_capture),
// so its gathers run with `curvature` off. The pulse's linear part advances along a row by one add
// in Q17 (the doubled coordinates make the centre exact); its lobes are one factor per row for the
// sideways pull and a table per column for the vertical.
//
// With `src`, the raw frame the strike captured, the gather also does stage 1's
// horizontal fringing: R and B come from whole px either side of the warped
// column, the separation the ramp's rung (as a text box rounds it) plus the
// strike's boost, signed by the dest's half like stage 1. There is no vertical
// fringe and no text box exemption, and the vignette pane follows as its own pass.
// Without it the frame is the passes' output, gathered in place: a source row
// below the dest row is still untouched in it; one above it has been
// overwritten, but the ring holds each row as it was before its write, and the
// pulse reaches at most CRT_STRIKE_REACH_PX rows up.
static void crt_strike_gather_scalar(uint8_t* fb, const uint8_t* src, int w, int h, int stride,
                                     int phase, bool curvature, const uint16_t* xq) {
  static uint8_t ring[CRT_STRIKE_REACH_PX + 1][SCREEN_W];
  static int16_t mod_x[SCREEN_W];
  int zx, zy;
  crt_strike_pulse(phase, &zx, &zy);
  const int stripes = strike_stripes(phase);
  const int boost = strike_ca_boost(phase);
  const int h1sq = (h - 1) * (h - 1);
  for (int x = 0; x < w; x++) mod_x[x] = strike_lobe_q8(x, w, phase, CRT_STRIKE_LOBE_STEP_Y);
  for (int y = 0; y < h; y++) {
    uint8_t* row = fb + (size_t)y * stride;
    if (!src) memcpy(ring[y % (CRT_STRIKE_REACH_PX + 1)], row, w);
    const int tdy = 2 * y - (h - 1);
    const int yterm = tdy * tdy * 256 / h1sq;
    const int stripe_y = 3 * yterm;
    const int lin_y = zy * tdy;
    const int mod_y = strike_lobe_q8(y, h, phase, CRT_STRIKE_LOBE_STEP_X);
    int lin_x = -zx * (w - 1);
    for (int x = 0; x < w; x++) {
      const int rq = xq[x] + yterm;
      int sy = y + strike_pulse_px(((lin_y >> 4) * mod_x[x]) >> 4, yterm);
      int sx = x + strike_pulse_px(((lin_x >> 4) * mod_y) >> 4, xq[x]);
      lin_x += 2 * zx;
      int fr = 0;
      if (curvature) {
        int mul16 = warp_mul16(rq);
        int S = (w - 1) * 65536 + (2 * x - (w - 1)) * mul16;
        sx += (S >> 17) - x;
        fr = (S >> 9) & 0xFF;
      }
      if (sx < 0 || sx >= w || sy < 0 || sy >= h) {
        row[x] = GCOLOR8_ALPHA;
        continue;
      }
      if (sx >= w - 1) fr = 0;  // w-1, not w: kill the fraction symmetrically
      const uint8_t* srow = src      ? src + (size_t)sy * w
                            : sy > y ? fb + (size_t)sy * stride
                                     : ring[sy % (CRT_STRIKE_REACH_PX + 1)];
      const int sep = src ? (crt_ca_t3_h2(rq) + 1) / 3 + boost : 0;
      const int dr = 2 * x < w - 1 ? sep : -sep;
      const uint8_t v = strike_blend(srow, sx, dr, w, fr);
      const int band = (((stripe_y + xq[x]) * stripes) >> 13) % 6;
      row[x] = is_grey(v) ? s_strike_grey_tint[v & 3][band] : band ? strike_hue(v, band) : v;
    }
  }
}

// What the gather's row loop holds fixed while it works along one row.
typedef struct {
  const uint16_t* xq;
  const int16_t* mod_x;
  int w, h;
  int hb;  // first column of the right half
  bool curvature;
  int zx, mod_y, lin_y, yterm, stripe_y, stripes, boost;
} StrikeRow;

static inline int strike_dx_at(const StrikeRow* r, int x) {
  return strike_pulse_px((((r->zx * (2 * x - (r->w - 1))) >> 4) * r->mod_y) >> 4, r->xq[x]);
}

static inline int strike_warp_dx_at(const StrikeRow* r, int x) {
  return ((2 * x - (r->w - 1)) * (warp_mul16(r->xq[x] + r->yterm) - 65536)) >> 17;
}

static inline int strike_dy_at(const StrikeRow* r, int x) {
  return strike_pulse_px(((r->lin_y >> 4) * r->mod_x[x]) >> 4, r->yterm);
}

static inline int strike_sep_at(const StrikeRow* r, int x) {
  return (crt_ca_t3_h2(r->xq[x] + r->yterm) + 1) / 3 + r->boost;
}

static inline int strike_stripe_at(const StrikeRow* r, int x) {
  return ((r->stripe_y + r->xq[x]) * r->stripes) >> 13;
}

// The first column in (x, lim] where `at` leaves `v`, its value at x; lim if it
// never does. Each of the terms is monotone along the stretch it is searched
// over, so "still v" holds on a prefix of it and the search may gallop out and
// bisect back instead of stepping: a run costs its log, not its length.
#define STRIKE_RUN_END(name, at)                               \
  static int name(const StrikeRow* r, int x, int v, int lim) { \
    int lo = x, step = 1;                                      \
    while (lo + step < lim && at(r, lo + step) == v) {         \
      lo += step;                                              \
      step <<= 1;                                              \
    }                                                          \
    int hi = lo + step < lim ? lo + step : lim;                \
    while (hi - lo > 1) {                                      \
      int mid = (lo + hi) >> 1;                                \
      if (at(r, mid) == v) {                                   \
        lo = mid;                                              \
      } else {                                                 \
        hi = mid;                                              \
      }                                                        \
    }                                                          \
    return hi;                                                 \
  }

STRIKE_RUN_END(strike_dx_end, strike_dx_at)
STRIKE_RUN_END(strike_warp_dx_end, strike_warp_dx_at)
STRIKE_RUN_END(strike_dy_end, strike_dy_at)
STRIKE_RUN_END(strike_sep_end, strike_sep_at)
STRIKE_RUN_END(strike_stripe_end, strike_stripe_at)

// Where each column's monotone stretch of `m` ends (exclusive): the stretches
// are cut where the sequence turns round, which for the lobes' sine is a few
// times per row.
static void strike_mono_ends(const int16_t* m, int w, int16_t* end) {
  int start = 0, dir = 0;
  for (int i = 1; i <= w; i++) {
    int d = i < w ? (m[i] > m[i - 1]) - (m[i] < m[i - 1]) : 0;
    if (i == w || (d && dir && d != dir)) {
      for (int k = start; k < i; k++) end[k] = (int16_t)i;
      start = i;
      dir = 0;
    } else if (d) {
      dir = d;
    }
  }
}

static inline uint32_t strike_load4(const uint8_t* p) {
  uint32_t v;
  memcpy(&v, p, sizeof(v));
  return v;
}

static inline void strike_store4(uint8_t* p, uint32_t v) {
  memcpy(p, &v, sizeof(v));
}

// Dest columns [x0, x1) of one row, all under the same source offset (`dxo`
// columns, `dy` rows), fringe (`sep`, signed by the half) and band. Nothing
// here depends on the column but the source and, under curvature, the
// fraction. A run of whole pixels fringed 0 and unbanded is a copy; otherwise
// four pixels merge per step, the channels masked out of three unaligned
// loads, and the band, if any, is one table read per pixel.
static void strike_run(uint8_t* row, const uint8_t* src, const StrikeRow* r, int y, int x0, int x1,
                       int dxo, int dy, int sep, int band) {
  const int w = r->w;
  const int sy = y + dy;
  int xl = x0 > -dxo ? x0 : -dxo;
  int xr = x1 < w - dxo ? x1 : w - dxo;
  if (sy < 0 || sy >= r->h || xl >= xr) {
    memset(row + x0, GCOLOR8_ALPHA, (size_t)(x1 - x0));
    return;
  }
  if (xl > x0) memset(row + x0, GCOLOR8_ALPHA, (size_t)(xl - x0));
  if (xr < x1) memset(row + xr, GCOLOR8_ALPHA, (size_t)(x1 - xr));
  const uint8_t* srow = src + (size_t)sy * w;
  const int dr = x0 < r->hb ? sep : -sep;
  const uint8_t* lut = band ? s_strike_band_px[band] : NULL;
  int x = xl;
  if (r->curvature) {
    for (; x < xr; x++) {
      const int sx = x + dxo;
      int fr = (((2 * x - (w - 1)) * (warp_mul16(r->xq[x] + r->yterm) - 65536)) >> 9) & 0xFF;
      if (sx >= w - 1) fr = 0;
      const uint8_t v = strike_blend(srow, sx, dr, w, fr);
      row[x] = lut ? lut[v & 63] : v;
    }
    return;
  }
  if (!dr && !lut) {
    memcpy(row + xl, srow + xl + dxo, (size_t)(xr - xl));
    return;
  }
  // The columns whose R and B taps stay inside the row, clamping nowhere.
  const int adr = dr < 0 ? -dr : dr;
  int fast_l = adr - dxo > xl ? adr - dxo : xl;
  if (fast_l > xr) fast_l = xr;
  int fast_r = w - adr - dxo < xr ? w - adr - dxo : xr;
  if (fast_r < fast_l) fast_r = fast_l;
  for (; x < fast_l; x++) {
    const uint8_t v = strike_ca_pixel(srow, x + dxo, dr, w);
    row[x] = lut ? lut[v & 63] : v;
  }
  const uint8_t* g = srow + dxo;
  for (; x + 4 <= fast_r; x += 4) {
    uint32_t m = 0xC0C0C0C0u | (strike_load4(g + x + dr) & 0x30303030u) |
                 (strike_load4(g + x) & 0x0C0C0C0Cu) | (strike_load4(g + x - dr) & 0x03030303u);
    if (!lut) {
      strike_store4(row + x, m);
      continue;
    }
    uint8_t px[4];
    memcpy(px, &m, sizeof(px));
    for (int i = 0; i < 4; i++) row[x + i] = lut[px[i] & 63];
  }
  for (; x < xr; x++) {
    const uint8_t v = strike_ca_pixel(srow, x + dxo, dr, w);
    row[x] = lut ? lut[v & 63] : v;
  }
}

// A strike frame's gather from the captured frame, the same pixels as the
// per-pixel loop above, found run by run. Along a row the source offset, the
// fringe and the band are integer roundings of smooth terms, so each holds
// over long runs, and each is tracked on its own: its next change is searched
// for only when its run is spent, and a row is cut where any of them changes.
// Every cut is a strike_run: black beyond the raster, a straight copy where
// nothing displaces, fringes or stripes the pixels, four pixels at a time
// otherwise. The terms are searched over stretches where they are monotone: a
// half row for the pulse, the band and the fringe, the lobe table's rising or
// falling stretch for the vertical pull. The source pixels are the face's, all
// opaque.
static void crt_strike_gather_runs(uint8_t* fb, const uint8_t* src, int w, int h, int stride,
                                   int phase, bool curvature, const uint16_t* xq) {
  static int16_t mod_x[SCREEN_W];
  static int16_t mono_end[SCREEN_W];
  StrikeRow r = {.xq = xq, .mod_x = mod_x, .w = w, .h = h, .hb = w / 2, .curvature = curvature};
  int zy;
  crt_strike_pulse(phase, &r.zx, &zy);
  r.stripes = strike_stripes(phase);
  r.boost = strike_ca_boost(phase);
  const int h1sq = (h - 1) * (h - 1);
  for (int x = 0; x < w; x++) mod_x[x] = strike_lobe_q8(x, w, phase, CRT_STRIKE_LOBE_STEP_Y);
  strike_mono_ends(mod_x, w, mono_end);
  for (int y = 0; y < h; y++) {
    uint8_t* row = fb + (size_t)y * stride;
    const int tdy = 2 * y - (h - 1);
    r.yterm = tdy * tdy * 256 / h1sq;
    r.stripe_y = 3 * r.yterm;
    r.lin_y = zy * tdy;
    r.mod_y = strike_lobe_q8(y, h, phase, CRT_STRIKE_LOBE_STEP_X);
    int dx = 0, warp_dx = 0, dy = 0, sep = 0, stripe = 0;
    int dx_end = 0, warp_dx_end = curvature ? 0 : w, dy_end = 0, sep_end = 0, stripe_end = 0;
    for (int x = 0; x < w;) {
      const int half_end = x < r.hb ? r.hb : w;
      if (x >= dx_end) {
        dx = strike_dx_at(&r, x);
        dx_end = strike_dx_end(&r, x, dx, half_end);
      }
      if (x >= warp_dx_end) {
        warp_dx = strike_warp_dx_at(&r, x);
        warp_dx_end = strike_warp_dx_end(&r, x, warp_dx, w);
      }
      if (x >= dy_end) {
        dy = strike_dy_at(&r, x);
        dy_end = strike_dy_end(&r, x, dy, mono_end[x]);
      }
      if (x >= sep_end) {
        sep = strike_sep_at(&r, x);
        sep_end = strike_sep_end(&r, x, sep, half_end);
      }
      if (x >= stripe_end) {
        stripe = strike_stripe_at(&r, x);
        stripe_end = strike_stripe_end(&r, x, stripe, half_end);
      }
      int end = dx_end < warp_dx_end ? dx_end : warp_dx_end;
      if (dy_end < end) end = dy_end;
      if (sep_end < end) end = sep_end;
      if (stripe_end < end) end = stripe_end;
      strike_run(row, src, &r, y, x, end, dx + warp_dx, dy, sep, stripe % 6);
      x = end;
    }
  }
}

static void crt_strike_gather(uint8_t* fb, const uint8_t* src, int w, int h, int stride, int phase,
                              bool curvature, const uint16_t* xq) {
  if (src) {
    crt_strike_gather_runs(fb, src, w, h, stride, phase, curvature, xq);
  } else {
    crt_strike_gather_scalar(fb, src, w, h, stride, phase, curvature, xq);
  }
}

// Per-width tables, rebuilt when the width changes: the squared Q8 column
// offsets the CA zones and the strike share, and the pane's side depths.
typedef struct {
  uint16_t ca_xq[SCREEN_W];
  // Per-column side-axis vignette depth, and the corner arcs' depth by (ey, ex).
  uint16_t col_depth[SCREEN_W];
  uint8_t corner_d[CRT_CORNER_RADIUS][CRT_CORNER_RADIUS];
  int w;
} CrtTables;

static const CrtTables* crt_tables(int w) {
  static CrtTables t;
  if (t.w != w) {
    for (int x = 0; x < w; x++) {
      int dx = 2 * x - (w - 1);
      t.ca_xq[x] = (uint16_t)((dx * dx * 256) / ((w - 1) * (w - 1)));
      t.col_depth[x] = (uint16_t)edge_depth_x(x < w - 1 - x ? x : w - 1 - x);
    }
    for (int ey = 0; ey < CRT_CORNER_RADIUS; ey++) {
      for (int ex = 0; ex < CRT_CORNER_RADIUS; ex++) {
        t.corner_d[ey][ex] = (uint8_t)corner_depth(ex, ey);
      }
    }
    t.w = w;
  }
  return &t;
}

// The vertical frame strokes, each WINDOW_BORDER_PX wide from column x, over
// rows y0..y1-1: the sides of every window draw_ascii_window frames, where
// draw_frame_sides puts them. Neighbours share their divider, so a stroke is
// listed once; left to right, which the zone table relies on.
typedef struct {
  int x, y0, y1;
} CrtStroke;
#define CRT_STROKES 12

static const CrtStroke* frame_strokes(int* n) {
  static CrtStroke strokes[CRT_STROKES];
  static int count = 0;
  if (!count) {
    const GRect windows[] = {SLOT_RECT_TOP_LEFT,    SLOT_RECT_TOP_RIGHT,     SLOT_RECT_CENTER,
                             SLOT_RECT_BOTTOM_LEFT, SLOT_RECT_BOTTOM_CENTER, SLOT_RECT_BOTTOM_RIGHT,
                             TIME_WINDOW_RECT};
    for (size_t i = 0; i < sizeof(windows) / sizeof(windows[0]); i++) {
      const int xs[2] = {windows[i].origin.x,
                         windows[i].origin.x + windows[i].size.w - WINDOW_BORDER_PX};
      for (int side = 0; side < 2; side++) {
        const CrtStroke s = {xs[side], windows[i].origin.y + TITLE_BORDER_DROP,
                             windows[i].origin.y + windows[i].size.h};
        int at = 0;
        while (at < count &&
               (strokes[at].x < s.x || (strokes[at].x == s.x && strokes[at].y0 < s.y0))) {
          at++;
        }
        if (at < count && strokes[at].x == s.x && strokes[at].y0 == s.y0) continue;
        if (count == CRT_STROKES) continue;
        memmove(&strokes[at + 1], &strokes[at], (count - at) * sizeof(strokes[0]));
        strokes[at] = s;
        count++;
      }
    }
  }
  *n = count;
  return strokes;
}

// The curvature warp compresses content by up to ~9% toward the rim, so a 2px
// stroke is ~1.8px there and its two edges cross column boundaries on different
// rows. A light stroke blends through in-between levels and reads as soft
// edges, but on a level-1 line the blend can only round to 1 or 0: the line
// drops to 1px for several rows at each crossing, a bend the tube does not
// have. So in the rows of a stroke the glass pixels around it (its footprint
// and one pixel either side, so its blended edge is covered) sample by a pure
// translation, the warp's sub-pixel offset at the stroke's centre, and still
// blend by the fraction: the stroke keeps its width and moves smoothly. What
// the neighbouring pixels sample differs by a few tenths of a px from the true
// warp, where the content is the gap the layout leaves around a frame.
//
// One zone per stroke per row, built once for the face's size: the offset is
// Q8 px, so a zone's position is x * 256 + d, the unit the fraction is cut from.
typedef struct {
  uint8_t xa, xb;  // glass columns, inclusive
  int16_t d;
} CrtRigidZone;
#define CRT_RIGID_ZONES 480

static const CrtRigidZone* rigid_zones(int w, int h, int y, int* n) {
  static CrtRigidZone zones[CRT_RIGID_ZONES];
  static uint16_t first[SCREEN_H + 1];
  static bool built = false;
  *n = 0;
  if (w != SCREEN_W || h != SCREEN_H) return NULL;
  if (!built) {
    int ns;
    const CrtStroke* strokes = frame_strokes(&ns);
    const int h1sq = (h - 1) * (h - 1);
    int count = 0;
    for (int row = 0; row < h; row++) {
      first[row] = (uint16_t)count;
      const int dy = 2 * row - (h - 1);
      const int yterm = dy * dy * 256 / h1sq;
      for (int i = 0; i < ns && count < CRT_RIGID_ZONES; i++) {
        if (row < strokes[i].y0 || row >= strokes[i].y1) continue;
        // The glass column the stroke's centre lands on, Q8 in the doubled
        // coordinates of the warp: the fixed point of its own inverse.
        const int64_t dcc_q8 = (int64_t)(2 * strokes[i].x + WINDOW_BORDER_PX - w) * 256;
        int64_t dx_q8 = dcc_q8;
        for (int it = 0; it < 4; it++) {
          const int xq = (int)(dx_q8 * dx_q8 / (256LL * (w - 1) * (w - 1)));
          dx_q8 = dcc_q8 * 65536 / warp_mul16(xq + yterm);
        }
        // Footprint 3 content px (the stroke and the taps either side) over the
        // warp's >= 1x scale, so at most 1.5 px each way, plus the margin pixel.
        const int centre_q8 = (int)(((w - 1) * 256 + dx_q8) / 2);
        int xa = (centre_q8 - 640 + 255) >> 8;
        int xb = (centre_q8 + 640) >> 8;
        zones[count].xa = (uint8_t)(xa < 0 ? 0 : xa);
        zones[count].xb = (uint8_t)(xb > w - 1 ? w - 1 : xb);
        zones[count].d = (int16_t)((dcc_q8 - dx_q8) / 2);
        count++;
      }
    }
    first[h] = (uint16_t)count;
    built = true;
  }
  *n = first[y + 1] - first[y];
  return zones + first[y];
}

static inline const CrtRigidZone* rigid_at(const CrtRigidZone* z, int n, int x) {
  for (int i = 0; i < n; i++) {
    if (x >= z[i].xa && x <= z[i].xb) return &z[i];
  }
  return NULL;
}

// The vignette is a pane in front of the glass, not a stage of the picture: how
// much a glass pixel is darkened depends on where it is and on the theme's
// curve, never on what it shows, and it is the last thing applied to a frame.
// Its shape is the falloff of the content space (edge depths, corner arcs)
// pushed through the curvature warp: the pane at a glass pixel is the falloff at
// the content position the warp shows there, so the rim bends with the tube. The
// dither is the pane's own, on the glass's grid rather than the content's.
//
// Computed per rim pixel from the warp's own terms, not tabulated: a table of
// the rim band would take ~16 KB of a heap the scene cache already crowds, and
// the content column is one multiply away.
typedef struct {
  const CrtTables* tb;
  const uint16_t* lut;
  const uint8_t* corner_row;  // the arcs' depths for this row, or NULL outside the corner square
  const uint8_t* bayer_row;
  const CrtRigidZone* rz;  // this row's rigid frame zones: the pane follows the stroke
  int nrz;
  int w, yterm, ey_depth;
} PaneRow;

// The content column stage 3 samples for glass column x, floored, replicating
// the edge column where the warp runs off the raster, and the warp's fraction
// toward the next column (0..255, the curve's own blend weight).
static inline int pane_column(const PaneRow* r, int x, int* fr) {
  int S = (r->w - 1) * 65536 + (2 * x - (r->w - 1)) * warp_mul16(r->tb->ca_xq[x] + r->yterm);
  int sx = S >> 17;
  *fr = (S >> 9) & 0xFF;
  const CrtRigidZone* z = rigid_at(r->rz, r->nrz, x);
  if (z) {
    sx = (x * 256 + z->d) >> 8;
    *fr = (x * 256 + z->d) & 0xFF;
  }
  if (sx < 0) sx = *fr = 0;
  if (sx >= r->w - 1) {
    sx = r->w - 1;
    *fr = 0;
  }
  return sx;
}

// The falloff at content column sx of this row, Q8.
static inline int pane_falloff(const PaneRow* r, int sx) {
  const int ex_depth = r->tb->col_depth[sx];
  const int d = (r->corner_row && ex_depth < CRT_CORNER_RADIUS)
                    ? r->corner_row[ex_depth]
                    : (ex_depth < r->ey_depth ? ex_depth : r->ey_depth);
  return d >= CRT_VIGNETTE_PX ? 256 : r->lut[d];
}

// Whether the falloff is flat at both columns the warp blends: from here inward
// the pane has nothing to do on a row whose own depth is past the rim.
static inline bool pane_interior(const PaneRow* r, int sx) {
  return r->tb->col_depth[sx] >= CRT_VIGNETTE_PX &&
         r->tb->col_depth[sx < r->w - 1 ? sx + 1 : sx] >= CRT_VIGNETTE_PX;
}

// The falloff at the content position (sx + fr/256) the warp shows at a glass
// pixel. The warp's scale changes row by row, so that position jumps along a
// vertical stroke; blending the two columns it lies between keeps the darkening
// from stepping with it.
static inline int pane_gain(const PaneRow* r, int sx, int fr) {
  return (pane_falloff(r, sx) * (256 - fr) + pane_falloff(r, sx < r->w - 1 ? sx + 1 : sx) * fr) >>
         8;
}

static inline void pane_pixel(const PaneRow* r, uint8_t* row, int x, int sx, int fr) {
  const int f = pane_gain(r, sx, fr);
  const uint8_t p = row[x];
  const int t = r->bayer_row[(x < r->w - 1 - x ? x : r->w - 1 - x) & 3];
  // Ordered dither, one third of a cycle apart per channel. A grey run
  // dithered in lockstep can only be black, DarkGray, LightGray or white
  // — three stops from field to rim on a level-2 field, which is a sharp
  // step however wide the band is, and no amount of band tuning adds a
  // fifth grey to a 2-bit panel. Splitting the phase lets the channels
  // cross their quantization boundaries at different pixels, so the run
  // passes through (2,2,1), (2,1,1), (1,1,0) and so on: the same falloff
  // rendered in thirds of a level instead of whole ones. It costs a
  // colour cast at the rim, which is the CRT's own failure mode — the CA
  // stage already models the beam landing worst there.
  //
  // Only the falloff band sees it. At full gain q4 is an exact multiple
  // of 16, so every phase quantizes to the same level and the interior
  // is untouched. B leads and R lags, which decays cool.
  const int r_lv = vignette_level((p >> 4) & 3, f, t);
  const int g_lv = vignette_level((p >> 2) & 3, f, (t + 5) & 15);
  const int b_lv = vignette_level(p & 3, f, (t + 10) & 15);
  row[x] = GCOLOR8_ALPHA | (uint8_t)((r_lv << 4) | (g_lv << 2) | b_lv);
}

// One glass row of the pane. Past the rim the content column stays in the
// interior as long as the row's own depth does, and the warp is monotone along
// the row, so each half is walked in from its edge until it gets there.
static void vignette_pane_row(const PaneRow* r, uint8_t* row) {
  const int w = r->w;
  const bool sides_only = r->ey_depth >= CRT_VIGNETTE_PX;
  int xl = w;
  for (int x = 0; x < w; x++) {
    int fr;
    const int sx = pane_column(r, x, &fr);
    if (sides_only && pane_interior(r, sx)) {
      xl = x;
      break;
    }
    pane_pixel(r, row, x, sx, fr);
  }
  for (int x = w - 1; x >= xl; x--) {
    int fr;
    const int sx = pane_column(r, x, &fr);
    if (pane_interior(r, sx)) break;
    pane_pixel(r, row, x, sx, fr);
  }
}

// The pane over a whole frame, in place, on what the other passes left. The
// strike's frames and the idle ones share it, so a strike's last frame and the
// frame after it cannot disagree at the rim.
static void vignette_pane(uint8_t* fb, int w, int h, int stride) {
#ifndef TEST_ENV
  time_t v0s, v1s;
  uint16_t v0ms, v1ms;
  time_ms(&v0s, &v0ms);
#endif
  const CrtTables* tb = crt_tables(w);
  // The curve the theme's field selects, chosen once per frame: the pane runs
  // over thousands of pixels and walks the LUT directly.
  const uint16_t* lut = vignette_lut(theme_ground_level(s_active_theme));
  const int h1sq = (h - 1) * (h - 1);
  for (int y = 0; y < h; y++) {
    const int edge_px = y < h - 1 - y ? y : h - 1 - y;
    const int dy = 2 * y - (h - 1);
    PaneRow r = {.tb = tb,
                 .lut = lut,
                 .bayer_row = &s_bayer4[(edge_px & 3) * 4],
                 .w = w,
                 .yterm = dy * dy * 256 / h1sq,
                 .ey_depth = edge_depth_y(edge_px)};
    r.corner_row = r.ey_depth < CRT_CORNER_RADIUS ? tb->corner_d[r.ey_depth] : NULL;
    r.rz = rigid_zones(w, h, y, &r.nrz);
    vignette_pane_row(&r, fb + (size_t)y * stride);
  }
#ifndef TEST_ENV
  time_ms(&v1s, &v1ms);
  timing_vig_ms = (int)((v1s - v0s) * 1000 + v1ms - v0ms);
#endif
}

// Stage 2 on one row: `row` takes the warp of `src`, the row as stage 1 left
// it, with `raw_row` the row as drawn (what the hue rule classifies on).
// `src` and `row` must not overlap. `rz` are the row's rigid frame zones,
// left to right: inside one the position is the stroke's translation, not the warp.
static void crt_curve_row(uint8_t* row, const uint8_t* src, const uint8_t* raw_row, int w,
                          int yterm, const uint16_t* ca_xq, const CrtRigidZone* rz, int nrz) {
  const CrtRigidZone* const rz_end = rz + nrz;
  for (int x = 0; x < w; x++) {
    int dx = 2 * x - (w - 1);
    int mul16 = warp_mul16(ca_xq[x] + yterm);
    int S = (w - 1) * 65536 + dx * mul16;  // exact warp position, Q17
    int sx = S >> 17;
    int fr = (S >> 9) & 0xFF;  // 0..255, weight toward sx+1
    while (rz < rz_end && x > rz->xb) rz++;
    if (rz < rz_end && x >= rz->xa) {
      sx = (x * 256 + rz->d) >> 8;
      fr = (x * 256 + rz->d) & 0xFF;
    }
    // Out-of-range replicates the nearest column WITHOUT the inward
    // blend: clamping sx alone would leave fr live, so the left rim would
    // blend the edge column with the (still dithered) column 1 while the
    // right rim self-replicates — an asymmetric grey bleed at the corners.
    if (sx < 0) {
      sx = 0;
      fr = 0;
    }
    if (sx >= w - 1) {  // w-1, not w: kill the fraction symmetrically
      sx = w - 1;
      fr = 0;
    }
    int sx1 = sx + 1;
    if (sx1 >= w) sx1 = sx;
    uint8_t p0 = src[sx], p1 = src[sx1];
    if (p0 == p1) {
      row[x] = p0;
      continue;
    }
    int r, g, b;
    blend_levels(p0, p1, fr, &r, &g, &b);
    // The blend zeroes channels independently too: a black rim column
    // against a colored fill lands on (1,0,0) at some fractions — the
    // alarm red hold_hue exists to prevent. Keyed on the LOWER tap's content
    // (usually the dominant one) so grey ink is not given a channel back.
    if (!is_grey(raw_row[sx])) {
      int sr = max2((p0 >> 4) & 3, (p1 >> 4) & 3);
      int sg = max2((p0 >> 2) & 3, (p1 >> 2) & 3);
      int sb = max2(p0 & 3, p1 & 3);
      int r0 = r, g0 = g, b0 = b;
      r = hold_hue(r0, sr, g0, b0);
      g = hold_hue(g0, sg, r0, b0);
      b = hold_hue(b0, sb, r0, g0);
    }
    row[x] = GCOLOR8_ALPHA | (uint8_t)((r << 4) | (g << 2) | b);
  }
}

void crt_apply_framebuffer(uint8_t* fb, int w, int h, int stride, int flash_phase) {
  crt_apply_framebuffer_passes(fb, w, h, stride, flash_phase, true, true);
}

void crt_apply_framebuffer_passes(uint8_t* fb, int w, int h, int stride, int flash_phase,
                                  bool aberration, bool curvature) {
  static uint8_t row_ca[SCREEN_W];
  if (w > SCREEN_W || w <= 1 || h <= 1 || stride < w) return;
#ifndef TEST_ENV
  timing_gather_ms = 0;
  timing_vig_ms = 0;
#endif
  const CrtTables* tb = crt_tables(w);
  const uint16_t* ca_xq = tb->ca_xq;
  // CA boost during the strike: the separation balloons while the mask
  // degausses. Phase-indexed through the same decay table as the row offset.
  int ca_boost = strike_ca_boost(flash_phase);
  // A strike always shows its fringing and wobble, whatever the settings say.
  const bool striking = flash_phase >= 0 && flash_phase < CRT_FLASH_PHASES;
  const bool do_ca = aberration || striking;
  if (!do_ca && !curvature) return;
  const int h1sq = (h - 1) * (h - 1);
  // The boxes are laid out for the face's own frame; any other buffer has none.
  const CrtTextBox* boxes = text_boxes();
  const int n_boxes = (w == SCREEN_W && h == SCREEN_H) ? CRT_TEXT_BOXES : 0;

  // A strike with its frame captured never runs the passes: the gather takes
  // the capture, already curved, fringing included, and the pane follows on
  // the glass.
  if (striking && s_strike_src && w == SCREEN_W && h == SCREEN_H) {
#ifndef TEST_ENV
    time_t g0s, g1s;
    uint16_t g0ms, g1ms;
    time_ms(&g0s, &g0ms);
#endif
    crt_strike_gather(fb, s_strike_src, w, h, stride, flash_phase, false, ca_xq);
#ifndef TEST_ENV
    time_ms(&g1s, &g1ms);
    timing_gather_ms = (int)((g1s - g0s) * 1000 + g1ms - g0ms);
#endif
    if (curvature) vignette_pane(fb, w, h, stride);
    return;
  }

  // Per-row body, applied in two half-passes. Both passes read away from
  // the screen centreline: from captured (raw) ring rows only, which is the
  // pass-order invariant that lets dest rows be rewritten in place.
  for (int pass = 0; pass < 2; pass++) {
    // pass 0: 0..(h-1)/2 forward; pass 1: h-1..(h-1)/2+1 backward.
    for (int yi = (pass == 0 ? 0 : h - 1); pass == 0 ? yi <= (h - 1) / 2 : yi > (h - 1) / 2;
         yi += pass == 0 ? 1 : -1) {
      int y = yi;
      uint8_t* row = fb + (size_t)y * stride;
      int dy = 2 * y - (h - 1);
      int yterm = dy * dy * 256 / h1sq;

      // Capture this row while it is still raw, and (if not done on this row)
      // one step ahead in traversal direction — the CA stage reads ±1 rows on
      // both halves and the ring can only hold what traversal has seen.
      memcpy(s_vraw_ring[y % 3], row, w);
      int y_next = pass == 0 ? y + 1 : y - 1;
      // Lookahead stays inside this pass's own half: the other half's rows may
      // already carry processed output by traversal time (ring reads raw).
      bool owns_next = pass == 0 ? y_next <= (h - 1) / 2 : y_next > (h - 1) / 2;
      if (owns_next && y_next >= 0 && y_next < h) {
        memcpy(s_vraw_ring[y_next % 3], fb + (size_t)y_next * stride, w);
      }

      // 1) CA from raw sources: outside the text boxes the horizontal fringe
      //    takes a two-tap weighted sample per shifted channel (thirds-of-a-px
      //    pull); vertical fringe samples ring rows toward/away from the
      //    centreline.
      const bool top = y * 2 < h - 1;
      // Sampling signs: left half pulls R from the right; top half pulls R
      // from below. Mirrors across both centrelines. Forward reads (below on
      // the top half, above on the bottom half) rely on the ring capture
      // being one row ahead in traversal direction.
      int ry1 = top ? y + 1 : y - 1;
      int by1 = top ? y - 1 : y + 1;
      // The ring only holds rows this pass has captured; at the half
      // boundary the forward tap's row belongs to the OTHER pass (its slot
      // carries a 3-rows-stale capture — pass 0 at y=113 would read row
      // 111). Fall back to the own row; only the forward read can cross.
      if (pass == 0 && ry1 > (h - 1) / 2) ry1 = y;
      if (pass == 1 && ry1 <= (h - 1) / 2) ry1 = y;
      if (ry1 < 0) ry1 = 0;
      if (ry1 >= h) ry1 = h - 1;
      if (by1 < 0) by1 = 0;
      if (by1 >= h) by1 = h - 1;
      // Ring rows by vertical shift s_v (0 or 1): the own row, or the
      // neighbour. Both halves only read rows already captured into the ring.
      const uint8_t* const rrows[2] = {s_vraw_ring[y % 3], s_vraw_ring[ry1 % 3]};
      const uint8_t* const brows[2] = {s_vraw_ring[y % 3], s_vraw_ring[by1 % 3]};
      // The boxes this row crosses, left to right: inside one, CA is a single
      // whole-pixel shift for the span, so the per-pixel work is skipped.
      const CrtTextBox* row_boxes[CRT_TEXT_BOXES];
      int n_row_boxes = 0;
      for (int i = 0; i < n_boxes; i++) {
        if (y >= boxes[i].y0 && y < boxes[i].y1) row_boxes[n_row_boxes++] = &boxes[i];
      }
      int next_box = 0;
      int next_box_x = n_row_boxes ? row_boxes[0]->x0 : -1;
      if (!do_ca) memcpy(row_ca, row, w);
      for (int x = 0; do_ca && x < w; x++) {
        if (x == next_box_x) {
          const CrtTextBox* b = row_boxes[next_box++];
          ca_box_span(row_ca, row, rrows[b->sv], brows[b->sv], b, ca_boost);
          x = b->x1 - 1;
          next_box_x = next_box < n_row_boxes ? row_boxes[next_box]->x0 : -1;
          continue;
        }
        bool left = x * 2 < w - 1;
        int xy_sum = ca_xq[x] + yterm;
        // Horizontal pull in THIRDS: the rung (0/4), plus the element-offset
        // correction and the strike boost in whole pixels. The correction does
        // NOT mirror: the RGB stripe puts R 1/3px left (B 1/3px right) of its
        // pixel centre on BOTH halves, so the sign follows the half, not the
        // pull direction. Folding a flat -1 into q before the tap mirror would
        // carry a 2/3px residual splay through the right half. The
        // cancellation makes the dead zone actually converge: at the centre
        // the rasters land on the content.
        int q = crt_ca_t3_h2(xy_sum) + (left ? -1 : 1) + 3 * ca_boost;
        int j = (q - (q < 0 ? 2 : 0)) / 3;  // floor(q/3); q >= -1 by construction
        // f in 0..2, all three reachable: the ramped onset sweeps every rung.
        int f = q - 3 * j;  // blend weight toward the farther tap
        int s_v = crt_ca_shift_v2(xy_sum);

        int rt0 = left ? x + j : x - j;
        int rt1 = left ? x + j + 1 : x - j - 1;
        int bt0 = left ? x - j : x + j;
        int bt1 = left ? x - j - 1 : x + j + 1;
        // Both taps of a channel come from the same (valid) ring ROW; clamp
        // the COLUMNS — at max strike the right half reaches q = 4+1+9 = 14
        // (rung + sign fix + 3·boost) → j = 4, and the taps land on the same
        // edge column and the weighted sample degenerates to a plain copy.
        if (rt0 < 0) rt0 = 0;
        if (rt0 >= w) rt0 = w - 1;
        if (rt1 < 0) rt1 = 0;
        if (rt1 >= w) rt1 = w - 1;
        if (bt0 < 0) bt0 = 0;
        if (bt0 >= w) bt0 = w - 1;
        if (bt1 < 0) bt1 = 0;
        if (bt1 >= w) bt1 = w - 1;

        const uint8_t* rrow = rrows[s_v];
        const uint8_t* brow = brows[s_v];
        // Fractional channel sample: sum of both taps weighted (3-f, f),
        // rounded to nearest — plain truncation biases R/B down half a level
        // on average, a faint green cast on mid-tones (G is never blended).
        // A 0<->3 edge yields exactly 2 and 1; weights act on code values,
        // linear in light here (the panel makes levels by area fill) — no
        // gamma round trip.
        int r = ((3 - f) * ((rrow[rt0] >> 4) & 3) + f * ((rrow[rt1] >> 4) & 3) + 1) / 3;
        int b = ((3 - f) * (brow[bt0] & 3) + f * (brow[bt1] & 3) + 1) / 3;
        row_ca[x] = GCOLOR8_ALPHA | (uint8_t)(r << 4) | (row[x] & 0x0C) | (uint8_t)b;
      }

      // The row as drawn, before stage 1 scattered R and B across columns:
      // curvature's blend classifies on it.
      const uint8_t* raw_row = s_vraw_ring[y % 3];

      // 2) Curvature: dest (x,y) samples source column
      //    sx = cx + (x - cx)·M(x,y)/65536 with the radial M of warp_mul16 —
      //    recomposed here from the CA stage's cached terms. A strike frame
      //    stops at the row write; a captured one has had this baked into
      //    its capture.
      //    M grows with x too, so integer
      //    quantization boundaries curve instead of aligning into seams.
      //    floor(S) + fraction IS the warp position, no rounding. Rim
      //    symmetry survives without rounding:
      //    floor+frac is pointwise symmetric in dx (the floor(−a) vs floor(a)
      //    ±1 phases sit under the pane's black rim). The fraction blends
      //    the column pair instead of picking one: magnification decimates
      //    source columns under nearest-neighbour (a damaged 8px letter cell
      //    measures 7px wide), and keeping their energy as fractional levels
      //    reads as tube edge defocus while letters keep their rhythm.
      //    Rounding (+128) avoids a truncation bias toward dark.
      //    Without curvature the row is an exact copy.
      if (striking || !curvature) {
        memcpy(row, row_ca, w);
        continue;
      }
      int nrz;
      const CrtRigidZone* rz = rigid_zones(w, h, y, &nrz);
      crt_curve_row(row, row_ca, raw_row, w, yterm, ca_xq, rz, nrz);
    }
  }
#ifndef TEST_ENV
  time_t g0s, g1s;
  uint16_t g0ms, g1ms;
  time_ms(&g0s, &g0ms);
#endif
  if (striking) crt_strike_gather(fb, NULL, w, h, stride, flash_phase, curvature, ca_xq);
#ifndef TEST_ENV
  time_ms(&g1s, &g1ms);
  timing_gather_ms = (int)((g1s - g0s) * 1000 + g1ms - g0ms);
#endif
  // The vignette pane, last: over whatever the passes and the gather left.
  if (curvature) vignette_pane(fb, w, h, stride);
}

// The canvas takes its cache back and repaints from scratch on its next
// render; idempotent, for every way a strike ends.
static void crt_strike_release(void) {
  if (!s_strike_src) return;
  s_strike_src = NULL;
  canvas_cache_return();
}

// The strike's first frame, the face as the layers drew it and before any pass,
// goes into the canvas's scene cache; the canvas stops repainting for the strike
// and every frame of it is gathered from here. With curvature on, the capture is
// warped once, in place, by stage 2's own row helper, so the gathers never warp
// per pixel and the pulse displaces the curved picture. A buffer that is not
// the face's own, or no cache to borrow, leaves the strike to run in place.
static void crt_strike_capture(const uint8_t* fb, int w, int h, int stride, bool curvature) {
  if (w != SCREEN_W || h != SCREEN_H) return;
  uint8_t* cache = canvas_cache_borrow();
  if (!cache) return;
  for (int y = 0; y < h; y++) memcpy(cache + (size_t)y * w, fb + (size_t)y * stride, w);
  s_strike_src = cache;
  if (!curvature) return;
#ifndef TEST_ENV
  time_t b0s, b1s;
  uint16_t b0ms, b1ms;
  time_ms(&b0s, &b0ms);
#endif
  static uint8_t tmp[SCREEN_W];
  const uint16_t* xq = crt_tables(w)->ca_xq;
  const int h1sq = (h - 1) * (h - 1);
  for (int y = 0; y < h; y++) {
    uint8_t* row = cache + (size_t)y * w;
    const int dy = 2 * y - (h - 1);
    memcpy(tmp, row, w);
    int nrz;
    const CrtRigidZone* rz = rigid_zones(w, h, y, &nrz);
    crt_curve_row(row, tmp, tmp, w, dy * dy * 256 / h1sq, xq, rz, nrz);
  }
#ifndef TEST_ENV
  time_ms(&b1s, &b1ms);
  timing_bake_ms = (int)((b1s - b0s) * 1000 + b1ms - b0ms);
#endif
#ifndef TEST_ENV
#endif
}

static bool crt_enabled(void) {
  return s_settings_crt_aberration || s_settings_crt_curvature || s_settings_crt_degauss;
}

void crt_update_proc(Layer* layer, GContext* ctx) {
  (void)layer;
  if (!crt_enabled() || !ctx) return;
  // A degauss-only face is untouched between strikes: nothing to capture.
  if (s_settings_crt_aberration || s_settings_crt_curvature || s_flash_phase != CRT_FLASH_IDLE) {
    // The captured bitmap IS the framebuffer on a native-format platform;
    // release before returning or the layer never lands.
    GBitmap* fb = graphics_capture_frame_buffer(ctx);
    if (!fb) return;
    GRect bounds = gbitmap_get_bounds(fb);
#ifndef TEST_ENV
    time_t t0s, t1s;
    uint16_t t0ms, t1ms;
    time_ms(&t0s, &t0ms);
#endif
    if (s_flash_phase != CRT_FLASH_IDLE && !s_strike_src) {
      crt_strike_capture(gbitmap_get_data(fb), bounds.size.w, bounds.size.h,
                         gbitmap_get_bytes_per_row(fb), s_settings_crt_curvature != 0);
    }
    crt_apply_framebuffer_passes(gbitmap_get_data(fb), bounds.size.w, bounds.size.h,
                                 gbitmap_get_bytes_per_row(fb), s_flash_phase,
                                 s_settings_crt_aberration != 0, s_settings_crt_curvature != 0);
    graphics_release_frame_buffer(ctx, fb);
#ifndef TEST_ENV
    time_ms(&t1s, &t1ms);
    APP_LOG(APP_LOG_LEVEL_INFO,
            "TIMING phase %d layers %ld ms pass %ld ms bake %d ms gather %d ms vig %d ms",
            s_flash_phase, (long)((t0s - timing_mark_s) * 1000 + t0ms - timing_mark_ms),
            (long)((t1s - t0s) * 1000 + t1ms - t0ms), timing_bake_ms, timing_gather_ms,
            timing_vig_ms);
    timing_bake_ms = 0;
    timing_vig_ms = 0;
#endif
  }
  // Strike trigger lives at the END of the wake frame (not the backlight
  // handler): the sound starts once the ring can survive what follows.
  if (s_strike_pending) {
    s_strike_pending = false;
    crt_play_strike_sound();
    s_flash_phase = 0;
    // A mark made during render is dropped when the render finishes, so
    // phase 0's repaint is scheduled from a timer.
    s_flash_timer = app_timer_register(0, crt_flash_start, NULL);
  }
}

static void crt_flash_start(void* data) {
  (void)data;
  // A killed chain (deinit, setting off, re-prime) leaves IDLE behind.
  if (s_flash_phase == CRT_FLASH_IDLE) return;
  TIMING_MARK();
  if (s_crt_layer) layer_mark_dirty(s_crt_layer);
  s_flash_timer = app_timer_register(CRT_FLASH_TICK_MS, crt_flash_tick, NULL);
}

static void crt_flash_tick(void* data) {
  (void)data;
  // A killed chain (deinit, setting off) leaves IDLE behind; a straggling tick
  // must not restart it.
  if (s_flash_phase == CRT_FLASH_IDLE) return;
  s_flash_phase++;
  if (s_flash_phase >= CRT_FLASH_PHASES) {
    s_flash_phase = CRT_FLASH_IDLE;
    s_flash_timer = NULL;
    crt_strike_release();
  }
  TIMING_MARK();
  if (s_crt_layer) layer_mark_dirty(s_crt_layer);
  // One-shot timers free themselves on firing.
  if (s_flash_phase != CRT_FLASH_IDLE) {
    s_flash_timer = app_timer_register(CRT_FLASH_TICK_MS, crt_flash_tick, NULL);
  }
}

// Prime, don't strike: the overlay proc starts sound + chain after the wake
// frame. A mid-strike prime cancels the old chain and re-primes.
static void crt_strike_prime(void) {
  if (s_flash_timer) {
    app_timer_cancel(s_flash_timer);
    s_flash_timer = NULL;
  }
  s_flash_phase = CRT_FLASH_IDLE;
  crt_strike_release();
  s_strike_pending = true;
  if (s_crt_layer) layer_mark_dirty(s_crt_layer);
}

static void crt_button_wake_fire(void* data) {
  (void)data;
  // One-shot timers free themselves on firing.
  s_button_wake_timer = NULL;
  if (!s_settings_crt_degauss || !s_app_in_focus || s_quick_view_active) return;
  crt_strike_prime();
}

void crt_backlight_handler(bool on) {
  // Gating: the flash is the strike of a tube warming up. Backlight-off
  // transitions, a disabled effect, and a covered face start nothing — a
  // cover pumps the backlight too, and its flashes belong to the cover.
  bool shake_wake = s_shake_wake;
  crt_shake_wake_clear();
  if (!on) {
    crt_button_wake_cancel();
    if (s_app_in_focus) s_cover_suppresses_strike = false;
    return;
  }
  // Quick View takes no focus, so its Back-dismissal backlight-on arrives in
  // focus: it primes nothing and leaves the one-shot alone.
  if (!s_settings_crt_degauss || !s_app_in_focus || s_quick_view_active) return;
  // The dismissal's backlight-on may land after focus returns — it belongs
  // to the cover too: eat the one-shot a cover armed, prime nothing.
  if (s_cover_suppresses_strike) {
    s_cover_suppresses_strike = false;
    crt_cover_grace_cancel();
    return;
  }
  // A shake is a look; anything else may be a menu press about to close the
  // face, so it waits out CRT_BUTTON_WAKE_DELAY_MS. A new wake restarts the wait.
  crt_button_wake_cancel();
  if (shake_wake) {
    crt_strike_prime();
    return;
  }
  s_button_wake_timer = app_timer_register(CRT_BUTTON_WAKE_DELAY_MS, crt_button_wake_fire, NULL);
}

// The degauss thunk as synthesized PCM: note-table playback clicks at every
// note boundary (pitch and velocity step), so the sound is generated instead.
// Synthesized per strike and freed right after the play call: the speaker
// service copies the sample into kernel memory, so the app's copy is dead
// weight once the call returns. The sound lasts as long as the animation.
// The one limit is the service's per-call sample budget (16 KB).
#define CRT_STRIKE_PCM_MS (CRT_FLASH_PHASES * CRT_FLASH_TICK_MS)
#define CRT_STRIKE_PCM_RATE 16000
#define CRT_STRIKE_PCM_SAMPLES (CRT_STRIKE_PCM_MS * CRT_STRIKE_PCM_RATE / 1000)
#define CRT_STRIKE_PCM_BUDGET_BYTES (16 * 1024)
_Static_assert(CRT_STRIKE_PCM_SAMPLES <= CRT_STRIKE_PCM_BUDGET_BYTES,
               "strike PCM exceeds the speaker service's per-call sample budget");
// The envelope eases to zero over the last CRT_STRIKE_FADE_SAMPLES (250ms at
// 16 kHz), ahead of CRT_STRIKE_TAIL_SAMPLES of exact silence, so the codec
// never cuts a nonzero signal.
#define CRT_STRIKE_FADE_SAMPLES 4000
#define CRT_STRIKE_TAIL_SAMPLES 16

// Q15 sine: |x| in [-1,1] shaped by the cubic smoothstep y = (3x − x³)/2 —
// within 1.1% of sin(πx/2), no libm: newlib's __ieee754_rem_pio2 hard-faults
// on the watch.
static int16_t crt_wave_q15(uint32_t phase_q16) {
  // Triangle coordinate over each half-cycle: x sweeps -1..+1 per half.
  int32_t half = (int32_t)((phase_q16 >> 15) & 1);
  int32_t x = (int32_t)((phase_q16 & 32767) * 2) - 32767;
  int64_t x2 = (int64_t)x * x;
  int y = (int)((3 * (int64_t)x - (x2 * x >> 30)) >> 1);
  // The cubic kisses ±(32767+2) near |x|=1 — cast to int16_t would wrap the
  // sign at the peaks (heard as clicks). Clamp before casting.
  if (y > 32767) y = 32767;
  if (y < -32767) y = -32767;
  return (int16_t)(half ? -y : y);
}

// Q8 gain of the closing fade for the sample `left` samples before the tail:
// a smoothstep from 256 down to 0, exactly 0 once the tail starts.
static int strike_fade_q8(int left) {
  if (left >= CRT_STRIKE_FADE_SAMPLES) return 256;
  if (left <= 0) return 0;
  int u = left * 1024 / CRT_STRIKE_FADE_SAMPLES;  // Q10, 0..1024
  int uu = u * u >> 10;
  int s = 3 * uu - (2 * uu * u >> 10);  // Q10 smoothstep, 0..1024
  return s >> 2;
}

// One strike voice. Counts are in samples at CRT_STRIKE_PCM_RATE; the tone
// sweeps exponentially from from_hz to to_hz (time constant sweep_tau) and
// dies with tone_tau; sub_q8 is the Q8 weight of a copy an octave below; the
// rumble is white noise through two one-poles of 1/lp_div, at rumble_gain/2^17
// of full scale, dying with rumble_tau; attack ramps the whole mix in; len ends
// the envelope early (0 = the full length); sync_hz (0 = free-running)
// restarts the tone's phase at that rate and rings each restart down with
// ring_tau, so the tone is heard as the colour of a sync_hz buzz rather than
// as a note; out_gain scales the mix to the peak.
typedef struct {
  int from_hz, to_hz, sweep_tau, tone_tau, attack, sub_q8, rumble_gain, lp_div, rumble_tau, len,
      sync_hz, ring_tau, out_gain;
} CrtStrikeVoice;

// A thud, then the coil's mains buzz. The watch speaker reproduces almost
// nothing below ~500 Hz and gains towards 2 kHz, so there is no note to hear:
// the tone restarts 100 times a second and rings down inside each period, a
// buzz whose colour falls from 1.6 kHz to 600 Hz over the first 60 ms and
// dies over ~300 ms. The rumble is a 50 ms noise thud under the onset. Judged
// on the wrist against recordings; the speaker turns broadband noise into
// hiss, so the buzz carries the body.
static const CrtStrikeVoice s_strike_voice = {.from_hz = 1600,
                                              .to_hz = 600,
                                              .sweep_tau = 960,
                                              .tone_tau = 5000,
                                              .attack = 120,
                                              .sub_q8 = 0,
                                              .rumble_gain = 60,
                                              .lp_div = 6,
                                              .rumble_tau = 800,
                                              .len = 0,
                                              .sync_hz = 100,
                                              .ring_tau = 60,
                                              .out_gain = 9102};

static void crt_strike_synth_voice(int8_t* buf, size_t n, const CrtStrikeVoice* v) {
  // A buzz with a falling colour and a rumble under it. The tone's partials
  // are 1:2 at 4:1. The body is broadband: white noise through two
  // one-pole low-passes, decaying and scaled by
  // the strike's visual amplitude, interpolated between the per-phase
  // s_strike_amp_q4 values so the rumble tracks the wobble. A smoothstep fade
  // to zero over the last CRT_STRIKE_FADE_SAMPLES and CRT_STRIKE_TAIL_SAMPLES
  // of silence closes it.
  const size_t per_phase = n / CRT_FLASH_PHASES;
  const int end = v->len ? v->len : (int)n;
  uint32_t phase = 0;
  int ring = 32767;
  int sync_left = 0;
  uint32_t seed = 1;
  int fq = v->from_hz * 256;  // Q8 Hz
  int tone = 32767;           // Q15
  int rumble = 32767;         // Q15
  int lp1 = 0, lp2 = 0;
  for (size_t i = 0; i < n; i++) {
    if (v->sync_hz && sync_left-- == 0) {
      phase = 0;
      ring = 32767;
      sync_left = CRT_STRIKE_PCM_RATE / v->sync_hz - 1;
    }
    phase += (uint32_t)(((int64_t)fq << 16) / (256 * CRT_STRIKE_PCM_RATE));
    fq -= (fq - v->to_hz * 256) / v->sweep_tau;
    int y = (4 * crt_wave_q15(phase) + crt_wave_q15(phase * 2)) / 5;
    y += (int)(((int64_t)crt_wave_q15(phase >> 1) * v->sub_q8) >> 8);
    if (v->sync_hz) {
      y = (int)(((int64_t)y * ring) >> 15);
      ring -= ring / v->ring_tau;
    }
    seed = seed * 1664525u + 1013904223u;
    lp1 += (((int)(seed >> 16) - 32768) - lp1) / v->lp_div;
    lp2 += (lp1 - lp2) / v->lp_div;

    size_t k = i / per_phase;
    if (k >= CRT_FLASH_PHASES) k = CRT_FLASH_PHASES - 1;
    int frac = (int)(i - k * per_phase);
    int a0 = s_strike_amp_q4[k];
    int a1 = k + 1 < CRT_FLASH_PHASES ? s_strike_amp_q4[k + 1] : a0;
    int body =
        (a0 * ((int)per_phase - frac) + a1 * frac) * 32767 / (s_strike_amp_q4[0] * (int)per_phase);
    int64_t tonal = (int64_t)y * tone >> 15;
    int64_t noisy = (int64_t)lp2 * v->rumble_gain * ((int64_t)rumble * body >> 15) >> 17;

    int gain = i < (size_t)v->attack ? (int)(i * 256 / v->attack) : 256;
    gain = gain * strike_fade_q8(end - CRT_STRIKE_TAIL_SAMPLES - (int)i) >> 8;
    int64_t out = (tonal + noisy) * gain * v->out_gain >> 23;
    buf[i] = (int8_t)((out > 32767 ? 32767 : (out < -32767 ? -32767 : out)) >> 8);
    if (i >= (size_t)v->attack) {
      tone -= tone / v->tone_tau;
      rumble -= (rumble + v->rumble_tau - 1) / v->rumble_tau;
    }
  }
}

void crt_strike_synth(int8_t* buf, size_t n) {
  crt_strike_synth_voice(buf, n, &s_strike_voice);
}

void crt_play_strike_sound(void) {
  // speaker_is_muted covers the system mute preference (including Quiet
  // Time-mutes-speaker when the user set it); s_app_in_focus is the App
  // Focus service state — the strike sound belongs to a tube the user is
  // looking at. Honor those, and our own volume.
  int volume = chime_effective_volume(s_settings_degauss_volume);
  if (!s_settings_crt_degauss || volume == 0 || !s_app_in_focus || speaker_is_muted()) return;
  int8_t* pcm = malloc(CRT_STRIKE_PCM_SAMPLES);
  if (!pcm) return;
  crt_strike_synth(pcm, CRT_STRIKE_PCM_SAMPLES);
  // The whole sample goes in as one track: the speaker service copies it into
  // kernel memory and refills its ring from an interrupt, so playback does not
  // depend on how long the app's frames block. The stream API only exists
  // from FW 4.33.2 — older stock firmware HARD-FAULTS on the missing syscall
  // (fallback-by-return-false is impossible), so the track path stays until
  // those builds die out.
  // One unshifted note over the whole sample plays it raw: the PCM IS the
  // sound.
  static const SpeakerNote through[1] = {
      {60, SpeakerWaveformSine, CRT_STRIKE_PCM_MS + 30, 127, 0},
  };
  const SpeakerSample sample = {.data = pcm,
                                .num_bytes = CRT_STRIKE_PCM_SAMPLES,
                                .format = SpeakerPcmFormat_16kHz_8bit,
                                .base_midi_note = 60,
                                .loop = false};
  const SpeakerTrack track = {.notes = through, .num_notes = 1, .sample = &sample};
  speaker_play_tracks(&track, 1, volume);
  free(pcm);
}

void crt_app_focus_will_handler(bool in_focus) {
  // A cover is ABOUT to appear: silence immediately, not after the animation.
  // With the effect on, arm the one-shot that eats the dismissal's
  // backlight-on; with it off nothing eats it, or the next real wake would
  // lose its strike.
  if (!in_focus) {
    crt_cover_grace_cancel();
    crt_button_wake_cancel();
    s_app_in_focus = false;
    s_cover_suppresses_strike = s_settings_crt_degauss;
  }
}

void crt_app_focus_did_handler(bool in_focus) {
  // Focus reported while the cover closes is too early; enable only on the
  // completed transition.
  if (!in_focus) return;
  s_app_in_focus = true;
  // The dismissal's backlight-on, if any, follows within the grace window.
  if (s_cover_suppresses_strike) {
    crt_cover_grace_cancel();
    s_cover_grace_timer = app_timer_register(CRT_COVER_GRACE_MS, crt_cover_grace_expire, NULL);
  }
}

void crt_set_in_focus(bool in_focus) {
  s_app_in_focus = in_focus;
}

void crt_apply_setting_change(void) {
  if (!s_settings_crt_degauss) {
    s_cover_suppresses_strike = false;
    crt_cover_grace_cancel();
    crt_button_wake_cancel();
    s_flash_phase = CRT_FLASH_IDLE;
    crt_strike_release();
    s_strike_pending = false;  // a primed-but-unstarted strike dies with the toggle
    // Mid-strike toggle-off: kill the armed chain, or the next tick re-arms
    // from IDLE into a full strike nobody asked for.
    if (s_flash_timer) {
      app_timer_cancel(s_flash_timer);
      s_flash_timer = NULL;
    }
  }
  if (crt_enabled()) {
    if (s_crt_layer) layer_mark_dirty(s_crt_layer);
    return;
  }
  // Off must ERASE, not just stop painting: the shader's pixels live in the
  // shared framebuffer and the next paint of anything underneath only happens
  // at the minute edge. Re-applying the window background dirties the whole
  // tree now — canvas and clock repaint, the overlay (disabled) adds nothing.
  if (s_main_window) window_set_background_color(s_main_window, s_active_theme->center_bg);
  if (s_crt_layer) layer_mark_dirty(s_crt_layer);
}

void crt_deinit(void) {
  if (s_flash_timer) {
    app_timer_cancel(s_flash_timer);
    s_flash_timer = NULL;
  }
  s_flash_phase = CRT_FLASH_IDLE;
  crt_strike_release();
  s_strike_pending = false;
  s_cover_suppresses_strike = false;
  crt_cover_grace_cancel();
  crt_button_wake_cancel();
  crt_shake_wake_clear();
  s_app_in_focus = false;
  s_crt_layer = NULL;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
