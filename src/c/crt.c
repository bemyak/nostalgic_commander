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

static void crt_flash_start(void* data);
static void crt_flash_tick(void* data);

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

static int isqrt_floor(int v) {
  if (v <= 0) return 0;
  int r = 1;
  while ((r + 1) <= v / (r + 1)) r++;  // no overflow: r*r <= v
  return r;
}

// Parallel 4x4 ordered-dither threshold grid — the only way a 4-level
// channel ramps smoothly: the pass quantizes c*f/256 with this threshold, so
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
// resize. The two bands are not in the same units: the falloff sits in content
// space, ahead of the warp, so the side band is measured before the warp's
// ~1.09x rim compression while the top/bottom band — which the warp never
// touches — is measured on the glass.
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

// Strike amplitude per frame: sudden, then decaying to nothing. The pulse
// scales with it; at the first frame's 6 a corner moves CRT_STRIKE_REACH_PX
// rows, the most any pixel moves vertically, which sizes the gather's row
// ring.
static const int s_strike_amp_px[CRT_FLASH_PHASES] = {6, 4, 3, 2, 1, 1, 0, 0};
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
#define CRT_STRIKE_LOBE_STEP_X 16384
#define CRT_STRIKE_LOBE_STEP_Y 9830

static void crt_strike_pulse(int phase, int* zx, int* zy) {
  int amp = (phase & 1 ? -1 : 1) * s_strike_amp_px[phase];
  *zx = amp * CRT_STRIKE_PULSE_X_Q16;
  *zy = amp * CRT_STRIKE_PULSE_Y_Q16;
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
// bands at amplitude 6, a fraction of one cycle on the last frames. Q13.
#define CRT_STRIKE_STRIPES_Q13 11

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
  int amp = s_strike_amp_px[phase];
  return ((field * (amp * amp * CRT_STRIKE_STRIPES_Q13)) >> 13) % 6;
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
// four steps. The channels are dithered out of phase (stage 2), so on a lit
// field the three rarely reach 0 on the same pixel and pure black stays a small
// share of the band instead of a scatter.
static int vignette_level(int c, int f_q8, int t) {
  return dither_q4(gain_q4(c, f_q8), t);
}

// The span the FIELD's dither covers at this gain: (q4 + t) >> 4 over the
// Bayer t of 0..15, floored the same way vignette_level floors it.
static void ground_span(int bg_level, int f_q8, int* lo, int* hi) {
  int q4 = gain_q4(bg_level, f_q8);
  *lo = q4 >> 4;
  *hi = (q4 + 15) >> 4;
}

// Push a rendered level clear of EVERY level the field can take at this depth,
// on the source's side of it. A gain is the right curve for the field itself
// and for bright chrome, but on a 4-level panel it leaves ink and field
// sharing a level: at depth 6 Navigator's caption dithers {1,2} over a field
// dithering {0,1}, so a 1px stroke landing on a low Bayer phase renders the
// exact level its neighbouring field pixels do and the stroke breaks up.
// Matching the field pixel-for-pixel is not enough — a stroke is thinner than
// the 4x4 dither tile, so the phases it misses are the ones beside it.
// Disjoint spans are the guarantee that survives that, and they come out of a
// per-depth bound rather than a per-pixel compare, so ink renders solid where
// it used to speckle. A tube's luminance is continuous and never merges two
// distinct inputs; the merge is our quantization, not the optics.
// A channel the source had may not quantize to 0 while another channel of the
// same pixel survives — that renders one palette color as a different one.
// Dialog's yellow is brown (2,1,0), CGA having no dark yellow, and its red and
// green take different Q4 values under the one Bayer threshold: the phases
// that zero the green render (r,0,0), which is that theme's own alarm red, on
// the low-battery band. Holding the channel at 1 keeps a fill inside its hue
// family until the whole pixel reaches black, which it still does — every
// channel zeroes together at the rim. Light fields only; the dark themes' rim
// was tuned on hardware with the per-channel decay in place.
static int max2(int a, int b) {
  return a > b ? a : b;
}

static bool is_grey(uint8_t c) {
  return ((c >> 4) & 3) == ((c >> 2) & 3) && ((c >> 2) & 3) == (c & 3);
}

static int hold_hue(int v, int src, int other1, int other2) {
  return (src > 0 && v == 0 && (other1 > 0 || other2 > 0)) ? 1 : v;
}

static int clear_of_ground(int v, int src, int gnd_lo, int gnd_hi, int bg_level) {
  // Where the field's span already reaches the end of the range there is no
  // step left on that side, and the pixel is left alone rather than shoved
  // onto a level the field itself renders. That is not rare: a level-1 field
  // has gnd_lo == 0 at every depth below its plateau, so returning 0 here
  // would crush all of its darker ink to black for no separation at all.
  if (src < bg_level && v >= gnd_lo) return gnd_lo > 0 ? gnd_lo - 1 : v;
  if (src > bg_level && v <= gnd_hi) return gnd_hi < 3 ? gnd_hi + 1 : v;
  return v;
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

// A strike frame's last stage: every dest pixel gathered from the frame as
// stages 1 and 2 left it. The source is the curvature warp's position (the
// plain column without curvature) displaced by the strike's pincushion pulse;
// beyond the raster it is black. The nearest row, and the warp's column pair
// blend, as stage 3 below, then the purity stripes. The pulse's linear part
// advances along a row by one add in Q17 (the doubled coordinates make the
// centre exact); its lobes are one factor per row for the sideways pull and a
// table per column for the vertical.
//
// Dest rows are written top to bottom in place. A source row below the dest
// row is still untouched in the frame; one above it has been overwritten, but
// the ring holds each row as it was before its write, and the pulse reaches at
// most CRT_STRIKE_REACH_PX rows up.
static void crt_strike_gather(uint8_t* fb, int w, int h, int stride, int phase, bool curvature,
                              const uint16_t* xq) {
  static uint8_t ring[CRT_STRIKE_REACH_PX + 1][SCREEN_W];
  static int16_t mod_x[SCREEN_W];
  int zx, zy;
  crt_strike_pulse(phase, &zx, &zy);
  const int amp = s_strike_amp_px[phase];
  const int stripes = amp * amp * CRT_STRIKE_STRIPES_Q13;
  const int h1sq = (h - 1) * (h - 1);
  for (int x = 0; x < w; x++) mod_x[x] = strike_lobe_q8(x, w, phase, CRT_STRIKE_LOBE_STEP_Y);
  for (int y = 0; y < h; y++) {
    uint8_t* row = fb + (size_t)y * stride;
    memcpy(ring[y % (CRT_STRIKE_REACH_PX + 1)], row, w);
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
      const uint8_t* srow =
          sy > y ? fb + (size_t)sy * stride : ring[sy % (CRT_STRIKE_REACH_PX + 1)];
      uint8_t p0 = srow[sx], p1 = srow[fr ? sx + 1 : sx];
      uint8_t v = p0;
      if (p0 != p1) {
        int r = ((256 - fr) * ((p0 >> 4) & 3) + fr * ((p1 >> 4) & 3) + 128) >> 8;
        int g = ((256 - fr) * ((p0 >> 2) & 3) + fr * ((p1 >> 2) & 3) + 128) >> 8;
        int b = ((256 - fr) * (p0 & 3) + fr * (p1 & 3) + 128) >> 8;
        if (!is_grey(p0)) {
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
      const int band = (((stripe_y + xq[x]) * stripes) >> 13) % 6;
      row[x] = band && !is_grey(v) ? strike_hue(v, band) : v;
    }
  }
}

void crt_apply_framebuffer(uint8_t* fb, int w, int h, int stride, int flash_phase) {
  crt_apply_framebuffer_passes(fb, w, h, stride, flash_phase, true, true);
}

void crt_apply_framebuffer_passes(uint8_t* fb, int w, int h, int stride, int flash_phase,
                                  bool aberration, bool curvature) {
  static uint8_t row_ca[SCREEN_W];
  static uint16_t s_ca_xq[SCREEN_W];
  // Per-column side-axis vignette depth, and the corner arcs' depth by (ey, ex).
  static uint16_t s_col_depth[SCREEN_W];
  static uint8_t s_corner_d[CRT_CORNER_RADIUS][CRT_CORNER_RADIUS];
  // First column whose side depth reaches CRT_VIGNETTE_PX, or w if none.
  static int s_interior_x0 = 0;
  static int s_tables_w = 0;
  if (w > SCREEN_W || w <= 1 || h <= 1 || stride < w) return;
  if (s_tables_w != w) {
    for (int x = 0; x < w; x++) {
      int dx = 2 * x - (w - 1);
      s_ca_xq[x] = (uint16_t)((dx * dx * 256) / ((w - 1) * (w - 1)));
      s_col_depth[x] = (uint16_t)edge_depth_x(x < w - 1 - x ? x : w - 1 - x);
    }
    s_interior_x0 = 0;
    while (s_interior_x0 < w && s_col_depth[s_interior_x0] < CRT_VIGNETTE_PX) s_interior_x0++;
    for (int ey = 0; ey < CRT_CORNER_RADIUS; ey++) {
      for (int ex = 0; ex < CRT_CORNER_RADIUS; ex++) {
        s_corner_d[ey][ex] = (uint8_t)corner_depth(ex, ey);
      }
    }
    s_tables_w = w;
  }
  // CA boost during the strike: the separation balloons while the mask
  // degausses. Phase-indexed through the same decay table as the row offset.
  int ca_boost = (flash_phase >= 0 && flash_phase < CRT_FLASH_PHASES)
                     ? (s_strike_amp_px[flash_phase] + 1) / 2
                     : 0;
  // A strike always shows its fringing and wobble, whatever the settings say.
  const bool striking = flash_phase >= 0 && flash_phase < CRT_FLASH_PHASES;
  const bool do_ca = aberration || striking;
  if (!do_ca && !curvature) return;
  const int h1sq = (h - 1) * (h - 1);
  // The boxes are laid out for the face's own frame; any other buffer has none.
  const CrtTextBox* boxes = text_boxes();
  const int n_boxes = (w == SCREEN_W && h == SCREEN_H) ? CRT_TEXT_BOXES : 0;
  // The field's own grey level, or 0 for a theme without one. Stage 2 keys
  // both of its content rules off it: ink/field separation runs only when
  // there is a grey field to separate against, and it is the reference that
  // separation measures from.
  const int vig_ground = theme_ground_level(s_active_theme);
  // The falloff curve it selects, chosen once per frame: stage 2 runs 45k
  // times a frame and walks the LUT directly.
  const uint16_t* vig_lut = vignette_lut(vig_ground);

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
        int xy_sum = s_ca_xq[x] + yterm;
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

      // 2) Vignette + dither on the CA'd row — dimming in content space, so
      //    the curvature pass below bends the already-darkened rim with the
      //    image. The two axes are sized independently — see crt.h's
      //    CRT_VIGNETTE_SIDE_PX and _BAND_PX — because the warp resamples
      //    columns and leaves rows alone. Mirror-symmetric Bayer thresholds
      //    keep both rims identical.
      const int edge_px = y < h - 1 - y ? y : h - 1 - y;
      const int ey_depth = edge_depth_y(edge_px);
      const uint8_t* corner_row = ey_depth < CRT_CORNER_RADIUS ? s_corner_d[ey_depth] : NULL;
      const uint8_t* bayer_row = &s_bayer4[(edge_px & 3) * 4];
      // The row as drawn, before stage 1 scattered R and B across columns.
      // Which light-field rule a pixel takes is a property of the CONTENT —
      // grey ink wants separating from its field, a colored fill wants its
      // hue held — and CA makes every 1px stroke chromatic, so the CA'd pixel
      // cannot answer that. Classify on the raw pixel, act on the CA'd one.
      const uint8_t* raw_row = s_vraw_ring[y % 3];
      for (int x = 0; curvature && x < w; x++) {
        // At f = 256 every rule below is a no-op, so row_ca[x] is already final.
        if (x == s_interior_x0 && ey_depth >= CRT_VIGNETTE_PX) {
          x = w - s_interior_x0 - 1;
          continue;
        }
        int ex = x < w - 1 - x ? x : w - 1 - x;
        int ex_depth = s_col_depth[x];
        int d = (corner_row && ex_depth < CRT_CORNER_RADIUS)
                    ? corner_row[ex_depth]
                    : (ex_depth < ey_depth ? ex_depth : ey_depth);
        int f = d >= CRT_VIGNETTE_PX ? 256 : vig_lut[d];
        uint8_t p = row_ca[x];
        int t = bayer_row[ex & 3];
        int rs = (p >> 4) & 3, gs = (p >> 2) & 3, bs = p & 3;
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
        int r = vignette_level(rs, f, t);
        int g = vignette_level(gs, f, (t + 5) & 15);
        int b = vignette_level(bs, f, (t + 10) & 15);
        bool grey_src = is_grey(raw_row[x]);
        if (vig_ground > 0 && grey_src) {
          // Keep ink clear of the field's whole dither span — but only where
          // content is meant to be read. Below READ_Q8 the falloff IS the
          // bezel ring (light LUT depths 0-4), and separating ink there would
          // hold frames off the black the rim exists to render.
          if (f >= CRT_VIGNETTE_READ_Q8) {
            int gnd_lo, gnd_hi;
            ground_span(vig_ground, f, &gnd_lo, &gnd_hi);
            r = clear_of_ground(r, rs, gnd_lo, gnd_hi, vig_ground);
            g = clear_of_ground(g, gs, gnd_lo, gnd_hi, vig_ground);
            b = clear_of_ground(b, bs, gnd_lo, gnd_hi, vig_ground);
          }
        } else if (!grey_src) {
          // Colored content: hold the hue's channels together instead. Read
          // the pre-hold values so the three lifts don't feed each other.
          // NOT gated on vig_ground: whether a fill may decay into another
          // palette colour has nothing to do with whether the FIELD is grey,
          // and gating it there left the dark themes unprotected — panel's
          // SunsetOrange status fill shed its green near the rim and rendered
          // as flat red, on the one chip that means "worst reading".
          int r0 = r, g0 = g, b0 = b;
          r = hold_hue(r0, rs, g0, b0);
          g = hold_hue(g0, gs, r0, b0);
          b = hold_hue(b0, bs, r0, g0);
        }
        row_ca[x] = GCOLOR8_ALPHA | (uint8_t)((r << 4) | (g << 2) | b);
      }

      // 3) Curvature: dest (x,y) samples source column
      //    sx = cx + (x - cx)·M(x,y)/65536 with the radial M of warp_mul16 —
      //    recomposed here from the CA stage's cached terms. A strike frame
      //    stops at the row write and takes this from crt_strike_gather
      //    instead.
      //    M grows with x too, so integer
      //    quantization boundaries curve instead of aligning into seams.
      //    floor(S) + fraction IS the warp position, no rounding. Rim
      //    symmetry survives without rounding:
      //    floor+frac is pointwise symmetric in dx (the floor(−a) vs floor(a)
      //    ±1 phases sit under the vignette's black rim). The fraction blends
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
      for (int x = 0; x < w; x++) {
        int dx = 2 * x - (w - 1);
        int mul16 = warp_mul16(s_ca_xq[x] + yterm);
        int S = (w - 1) * 65536 + dx * mul16;  // exact warp position, Q17
        int sx = S >> 17;
        int fr = (S >> 9) & 0xFF;  // 0..255, weight toward sx+1
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
        uint8_t p0 = row_ca[sx], p1 = row_ca[sx1];
        if (p0 == p1) {
          row[x] = p0;
          continue;
        }
        int r = ((256 - fr) * ((p0 >> 4) & 3) + fr * ((p1 >> 4) & 3) + 128) >> 8;
        int g = ((256 - fr) * ((p0 >> 2) & 3) + fr * ((p1 >> 2) & 3) + 128) >> 8;
        int b = ((256 - fr) * (p0 & 3) + fr * (p1 & 3) + 128) >> 8;
        // The blend zeroes channels independently too: a black rim column
        // against a colored fill lands on (1,0,0) at some fractions — the
        // alarm red stage 2's hold_hue exists to prevent. Keyed on the LOWER
        // tap's content (usually the dominant one) so grey ink keeps the
        // separation stage 2 gave it instead of having a channel lifted back.
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
  }
  if (striking) crt_strike_gather(fb, w, h, stride, flash_phase, curvature, s_ca_xq);
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
    crt_apply_framebuffer_passes(gbitmap_get_data(fb), bounds.size.w, bounds.size.h,
                                 gbitmap_get_bytes_per_row(fb), s_flash_phase,
                                 s_settings_crt_aberration != 0, s_settings_crt_curvature != 0);
    graphics_release_frame_buffer(ctx, fb);
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
  }
  if (s_crt_layer) layer_mark_dirty(s_crt_layer);
  // One-shot timers free themselves on firing.
  if (s_flash_phase != CRT_FLASH_IDLE) {
    s_flash_timer = app_timer_register(CRT_FLASH_TICK_MS, crt_flash_tick, NULL);
  }
}

void crt_backlight_handler(bool on) {
  // Gating: the flash is the strike of a tube warming up. Backlight-off
  // transitions, a disabled effect, and a covered face start nothing — a
  // cover pumps the backlight too, and its flashes belong to the cover.
  if (!on) {
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
  // Prime, don't strike: the overlay proc starts sound + chain after the wake
  // frame. A mid-strike backlight-on cancels the old chain and re-primes.
  if (s_flash_timer) {
    app_timer_cancel(s_flash_timer);
    s_flash_timer = NULL;
  }
  s_flash_phase = CRT_FLASH_IDLE;
  s_strike_pending = true;
  if (s_crt_layer) layer_mark_dirty(s_crt_layer);
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
  // s_strike_amp_px values so the rumble tracks the wobble. A smoothstep fade
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
    int a0 = s_strike_amp_px[k];
    int a1 = k + 1 < CRT_FLASH_PHASES ? s_strike_amp_px[k + 1] : a0;
    int body =
        (a0 * ((int)per_phase - frac) + a1 * frac) * 32767 / (s_strike_amp_px[0] * (int)per_phase);
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
    s_flash_phase = CRT_FLASH_IDLE;
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
  s_strike_pending = false;
  s_cover_suppresses_strike = false;
  crt_cover_grace_cancel();
  s_app_in_focus = false;
  s_crt_layer = NULL;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
