#include <pebble.h>
#include "crt.h"
#include "main.h"
#include "theme.h"

// Everything runs on GColor8 bytes (0xAARRGGBB, 2 bits per channel) captured
// from the framebuffer. Emery-only build; the face targets no B&W platform.

#define GCOLOR8_ALPHA 0xC0

// Current warm-up phase, or CRT_FLASH_IDLE. Advanced by the self-re-arming
// flash tick; written by crt_backlight_handler on backlight-on.
static int s_flash_phase = CRT_FLASH_IDLE;
static AppTimer* s_flash_timer = NULL;  // latest tick arm — cancelled on retrigger
// Strike primed by the backlight handler, started by the overlay proc AFTER
// the wake frame lands: measured on hardware, the wake burst (bg fill, canvas
// glyphs, clock, pass) blocks long enough to drain the audio ring once — the
// audible pop-gap-hum. Delaying sound+chain past that frame keeps the ring fed.
static bool s_strike_pending = false;
// Updated by the App Focus service (subscribed in main.c): a notification,
// alarm, modal, quick view or a launched app covering the face drops it.
// Gates the strike sound and the backlight handler's priming — a covered
// tube starts nothing and stays silent.
static bool s_app_in_focus = true;
// A cover's close pumps the backlight once, in no guaranteed order against
// focus returning; that flash belongs to the cover. Armed when the cover
// appears, eaten by the next in-focus backlight-on.
static bool s_cover_suppresses_strike = false;

static void crt_flash_tick(void* data);

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

// Radial magnification of the curvature stage, Q16. Identity at the centre,
// K per unit of the elliptical squared radius (same xq/yq formulation as the
// CA zone map). K=12 gives ≈4.7px pull at a mid-edge, tuned on hardware
// against the 16px vignette (K=13's 5px bent the rim darkening over the
// bright side frames). Grows smoothly to 1.09x in the corners — a curved
// surface bows its sides too, so mid-height now warps where the old y-only
// gain was identity. Rank-1 quantized all outer columns on the same row
// boundaries (visible seams); the per-column boundary curve does not.
int crt_warp_q16(int x, int y, int w, int h) {
  int dx = 2 * x - (w - 1);
  int dy = 2 * y - (h - 1);
  int xq = (dx * dx * 256) / ((w - 1) * (w - 1));
  int yq = (dy * dy * 256) / ((h - 1) * (h - 1));
  return 65536 + CRT_WARP_R2_K * (xq + yq);
}

// Source column for dest (x,y) under the warp, without strike jitter —
// nearest-rounded. Pin target only: the pass works in floor+frac of the same
// S (for the blend), a half-px phase offset from this form.
int crt_warp_sx(int x, int y, int w, int h) {
  int dx = 2 * x - (w - 1);
  return ((w - 1) * 65536 + dx * crt_warp_q16(x, y, w, h) + 65536) >> 17;
}

// Horizontal CA from the squared Q8 radius (units: xq + yq from
// crt_ca_shift3), returning per-channel displacement in THIRDS of a pixel:
// 0 inside the dead zone, then ramping to 4 (1+1/3 px) over CRT_CA_RAMP_SHIFT
// — see crt.h for why it ramps rather than steps. Thirds because the pass
// samples channels fractionally (two weighted taps) instead of copying whole
// neighbours — see stage 1 in crt_apply_framebuffer. Zone boundary squared
// once: rq ≥ R ⇔ xq+yq ≥ ceil(R²/256) — so no per-pixel sqrt is needed.
static int crt_ca_t3_h2(int xy_sum) {
  if (xy_sum <= CRT_CA_R2_X2Q8) return 0;
  int t3 = CRT_CA_RAMP_FLOOR + ((xy_sum - CRT_CA_R2_X2Q8) >> CRT_CA_RAMP_SHIFT);
  return t3 > 4 ? 4 : t3;
}
// Vertical wider zones, 1px max, same formulation.
static int crt_ca_shift_v2(int xy_sum) {
  return xy_sum < CRT_CA_R2V_X2Q8 ? 0 : 1;
}

int crt_ca_shift3(int x, int y, int w, int h) {
  int dx = 2 * x - (w - 1);
  int dy = 2 * y - (h - 1);
  // Elliptical radius from the centre, Q8: corner ≈ 362, mid-edge = 256.
  int xq = (dx * dx * 256) / ((w - 1) * (w - 1));
  int yq = (dy * dy * 256) / ((h - 1) * (h - 1));
  return crt_ca_t3_h2(xq + yq);
}

// Distance toward the nearest frame edge, counting around the corner arcs:
// inside the corner square the boundary is the arc, elsewhere the straight
// edge. 0 = boundary itself, growing inward. Both axes are scaled onto the
// LUTs' 0..VIGNETTE_PX domain by their own band constant, so the curves stay
// fixed while the rims resize. The two bands are not in the same units: the
// falloff sits in content space, ahead of the warp, so the side band is
// measured before the warp's ~1.3x stretch while the top/bottom band — which
// the warp never touches — is measured on the glass.
static int crt_edge_distance(int x, int y, int w, int h) {
  int ex = (x < w - 1 - x ? x : w - 1 - x) * CRT_VIGNETTE_PX / CRT_VIGNETTE_SIDE_PX;
  int edge_px = y < h - 1 - y ? y : h - 1 - y;
  // EDGE_PX rows of depth 0 first, standing in for the warp's clamped columns,
  // then the falloff over what remains of the band.
  int ey = edge_px < CRT_VIGNETTE_EDGE_PX ? 0
                                          : (edge_px - CRT_VIGNETTE_EDGE_PX + 1) * CRT_VIGNETTE_PX /
                                                (CRT_VIGNETTE_BAND_PX - CRT_VIGNETTE_EDGE_PX + 1);
  if (ex < CRT_CORNER_RADIUS && ey < CRT_CORNER_RADIUS) {
    int rx = CRT_CORNER_RADIUS - ex;
    int ry = CRT_CORNER_RADIUS - ey;
    int d = CRT_CORNER_RADIUS - isqrt_floor(rx * rx + ry * ry);
    return d < 0 ? 0 : d;
  }
  return ex < ey ? ex : ey;
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

// Same falloff without per-pixel arithmetic (the LUTs above carry it); a
// theme with a grey field (theme_ground_level, theme.h) takes the light curve.
static int crt_vignette_q8_from_depth(int d) {
  if (d >= CRT_VIGNETTE_PX) return 256;
  return theme_ground_level(s_active_theme) > 0 ? s_vignette_q8_light[d] : s_vignette_q8[d];
}

int crt_vignette_q8(int x, int y, int w, int h) {
  return crt_vignette_q8_from_depth(crt_edge_distance(x, y, w, h));
}

// Strike amplitude per frame, px. Decays to zero; the strike reads as a
// wobble that settles, never a sequence of bands.
static const int s_strike_amp_px[CRT_FLASH_PHASES] = {6, 5, 4, 3, 2, 2, 1, 1};

// Per-row jitter: keyed hashing, deterministic, adjacent rows uncorrelated.
int crt_strike_offset(int y, int flash_phase) {
  if (flash_phase < 0 || flash_phase >= CRT_FLASH_PHASES) return 0;
  int amp = s_strike_amp_px[flash_phase];
  // Knuth multiplicative, cheap avalanche for row index × phase.
  uint32_t noise = (uint32_t)(y + 1) * 2654435761u + (uint32_t)flash_phase * 40503u;
  noise ^= noise >> 13;
  return (int)(noise % (uint32_t)(2 * amp + 1)) - amp;
}

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
// four steps.
//
// There used to be a per-theme "dot floor" here holding brighter-than-floor
// sources off black, because a falloff that dithers straight to black scatters
// pepper on a lit field. Dithering the channels out of phase (stage 2) solved
// that better: the three rarely reach 0 on the same pixel, so pure black is
// 0-3% of the band instead of a scatter. The floor's own cost was that it
// clamped every channel to the same level and flattened the ramp into a step,
// which is exactly what it was supposed to prevent.
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
static uint8_t s_vraw_ring[3][200];

void crt_apply_framebuffer(uint8_t* fb, int w, int h, int flash_phase) {
  static uint8_t row_ca[200];
  // Per-column: was the pixel grey as DRAWN? Stage 2 decides it from the raw
  // row; stage 3 needs the same answer for the column its blend leans on.
  static bool row_grey[200];
  static uint16_t s_ca_xq[200];
  static int s_ca_xq_w = 0;
  if (w > (int)sizeof(row_ca) || w <= 1 || h <= 1) return;
  if (s_ca_xq_w != w) {
    for (int x = 0; x < w; x++) {
      int dx = 2 * x - (w - 1);
      s_ca_xq[x] = (uint16_t)((dx * dx * 256) / ((w - 1) * (w - 1)));
    }
    s_ca_xq_w = w;
  }
  // CA boost during the strike: the separation balloons while the mask
  // degausses. Phase-indexed through the same decay table as the row jitter.
  int ca_boost = (flash_phase >= 0 && flash_phase < CRT_FLASH_PHASES)
                     ? (s_strike_amp_px[flash_phase] + 1) / 2
                     : 0;
  const int h1sq = (h - 1) * (h - 1);
  // The field's own grey level, or 0 for a theme without one. Stage 2 keys
  // both of its content rules off it: ink/field separation runs only when
  // there is a grey field to separate against, and it is the reference that
  // separation measures from.
  const int vig_ground = theme_ground_level(s_active_theme);
  // ...and the falloff curve it selects, chosen once per frame. crt_vignette_q8()
  // is the public form and re-picks it on every call — correct, and fine for
  // the tests and the ramp tool, but it is a cross-TU call and this loop runs
  // 45k times a frame. Stage 2 walks the LUT directly instead.
  const uint16_t* vig_lut = vig_ground > 0 ? s_vignette_q8_light : s_vignette_q8;

  // Per-row body, applied in two half-passes. Both passes read away from
  // the screen centreline: from captured (raw) ring rows only, which is the
  // pass-order invariant that lets dest rows be rewritten in place.
  for (int pass = 0; pass < 2; pass++) {
    // pass 0: 0..(h-1)/2 forward; pass 1: h-1..(h-1)/2+1 backward.
    for (int yi = (pass == 0 ? 0 : h - 1); pass == 0 ? yi <= (h - 1) / 2 : yi > (h - 1) / 2;
         yi += pass == 0 ? 1 : -1) {
      int y = yi;
      uint8_t* row = fb + (size_t)y * w;
      int dy = 2 * y - (h - 1);
      int yterm = dy * dy * 256 / h1sq;
      int row_off = crt_strike_offset(y, flash_phase);

      // Capture this row while it is still raw, and (if not done on this row)
      // one step ahead in traversal direction — the CA stage reads ±1 rows on
      // both halves and the ring can only hold what traversal has seen.
      memcpy(s_vraw_ring[y % 3], row, w);
      int y_next = pass == 0 ? y + 1 : y - 1;
      // Lookahead stays inside this pass's own half: the other half's rows may
      // already carry processed output by traversal time (ring reads raw).
      bool owns_next = pass == 0 ? y_next <= (h - 1) / 2 : y_next > (h - 1) / 2;
      if (owns_next && y_next >= 0 && y_next < h) {
        memcpy(s_vraw_ring[y_next % 3], fb + (size_t)y_next * w, w);
      }

      // 1) CA from raw sources: horizontal fringe takes a two-tap weighted
      //    sample per shifted channel (thirds-of-a-px pull); vertical
      //    fringe samples ring rows toward/away from the centreline.
      for (int x = 0; x < w; x++) {
        bool left = x * 2 < w - 1;
        // Horizontal pull in THIRDS: the rung (0/4), plus the element-offset
        // correction and the strike boost in whole pixels. The correction does
        // NOT mirror: the RGB stripe puts R 1/3px left (B 1/3px right) of its
        // pixel centre on BOTH halves, so the sign follows the half, not the
        // pull direction — folding a flat -1 into q before the tap mirror
        // (old form) carried a 2/3px residual splay through the right half,
        // measured on hardware. The cancellation makes the dead zone actually
        // converge: at the centre the rasters land on the content.
        int q = crt_ca_t3_h2(s_ca_xq[x] + yterm) + (left ? -1 : 1) + 3 * ca_boost;
        int j = (q - (q < 0 ? 2 : 0)) / 3;  // floor(q/3); q >= -1 by construction
        // 0..2, all three reachable. While crt_ca_t3_h2 returned only 0 or 4 the
        // ±1 correction kept q off 1 (mod 3) and f was never 1; the ramped onset
        // sweeps every rung, so it is now. The weighted tap handles all three.
        int f = q - 3 * j;  // blend weight toward the farther tap
        int s_v = crt_ca_shift_v2(s_ca_xq[x] + yterm);

        bool top = y * 2 < h - 1;
        // Sampling signs: left half pulls R from the right; top half pulls R
        // from below. Mirrors across both centrelines. Forward reads (below on
        // the top half, above on the bottom half) rely on the ring capture
        // being one row ahead in traversal direction.
        int rt0 = left ? x + j : x - j;
        int rt1 = left ? x + j + 1 : x - j - 1;
        int bt0 = left ? x - j : x + j;
        int bt1 = left ? x - j - 1 : x + j + 1;
        int ry = top ? y + s_v : y - s_v;
        int by = top ? y - s_v : y + s_v;
        // The ring only holds rows this pass has captured; at the half
        // boundary the forward tap's row belongs to the OTHER pass (its slot
        // carries a 3-rows-stale capture — pass 0 at y=113 would read row
        // 111). Fall back to the own row; only the forward read can cross.
        if (pass == 0 && ry > (h - 1) / 2) ry = y;
        if (pass == 1 && ry <= (h - 1) / 2) ry = y;
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
        // The ring holds rows y-2..y+2 at most; s_v stays within 1 today.
        // Both halves only read rows already captured into the ring.
        if (ry < 0) ry = 0;
        if (ry >= h) ry = h - 1;
        if (by < 0) by = 0;
        if (by >= h) by = h - 1;

        const uint8_t* rrow = s_vraw_ring[ry % 3];
        const uint8_t* brow = s_vraw_ring[by % 3];
        // Fractional channel sample: sum of both taps weighted (3-f, f),
        // rounded to nearest — plain truncation biased R/B down half a level
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
      int ey = y < h - 1 - y ? y : h - 1 - y;
      int ty = ey & 3;
      // The row as drawn, before stage 1 scattered R and B across columns.
      // Which light-field rule a pixel takes is a property of the CONTENT —
      // grey ink wants separating from its field, a colored fill wants its
      // hue held — and CA makes every 1px stroke chromatic, so the CA'd pixel
      // cannot answer that. Classify on the raw pixel, act on the CA'd one.
      const uint8_t* raw_row = s_vraw_ring[y % 3];
      for (int x = 0; x < w; x++) {
        int ex = x < w - 1 - x ? x : w - 1 - x;
        int d = crt_edge_distance(x, y, w, h);
        int f = d >= CRT_VIGNETTE_PX ? 256 : vig_lut[d];
        uint8_t p = row_ca[x];
        int t = s_bayer4[ty * 4 + (ex & 3)];
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
        uint8_t q = raw_row[x];
        bool grey_src = ((q >> 4) & 3) == ((q >> 2) & 3) && ((q >> 2) & 3) == (q & 3);
        if (vig_ground > 0 && grey_src) {
          // Keep ink clear of the field's whole dither span — but only where
          // content is meant to be read. Below READ_Q8 the falloff IS the
          // bezel ring (light LUT depths 0-3), and separating ink there would
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
        row_grey[x] = grey_src;
        row_ca[x] = GCOLOR8_ALPHA | (uint8_t)((r << 4) | (g << 2) | b);
      }

      // 3) Curvature (+ strike jitter): dest (x,y) samples source column
      //    sx = cx + (x - cx)·M(x,y)/65536 with the radial M of crt_warp_q16 —
      //    recomposed here from the CA stage's cached terms — then the strike
      //    slides the whole row sideways. M grows with x too, so integer
      //    quantization boundaries curve instead of aligning into seams.
      //    sn's +half cancels exactly in S below: stage 3 no longer rounds —
      //    floor(S) + fraction IS the warp position. Rim symmetry survives
      //    without rounding: floor+frac is pointwise symmetric in dx (the
      //    floor(−a) vs floor(a) ±1 phases sit under the vignette's black
      //    rim). The fraction then blends the column pair instead of picking
      //    one: magnification decimates source columns under nearest-
      //    neighbour (every damaged 8px letter cell measured 7px wide), and
      //    keeping their energy as fractional levels reads as tube edge
      //    defocus while letters keep their rhythm. Rounding (+128) avoids a
      //    truncation bias toward dark.
      for (int x = 0; x < w; x++) {
        int dx = 2 * x - (w - 1);
        int mul16 = 65536 + CRT_WARP_R2_K * (s_ca_xq[x] + yterm);
        int sn = (w - 1) * 65536 + dx * mul16 + 65536;  // sx*131072 + half
        int S = sn - 65536;                             // exact warp position, Q17
        int sx = (S >> 17) + row_off;                   // strike stays whole-pixel
        int fr = (S >> 9) & 0xFF;                       // 0..255, weight toward sx+1
        // Out-of-range replicates the nearest column WITHOUT the inward
        // blend: clamping sx alone left fr live, so the left rim blended the
        // edge column with the (still dithered) column 1 while the right rim
        // self-replicated — an asymmetric grey bleed at the corners.
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
        int r = ((256 - fr) * ((p0 >> 4) & 3) + fr * ((p1 >> 4) & 3) + 128) >> 8;
        int g = ((256 - fr) * ((p0 >> 2) & 3) + fr * ((p1 >> 2) & 3) + 128) >> 8;
        int b = ((256 - fr) * (p0 & 3) + fr * (p1 & 3) + 128) >> 8;
        // The blend zeroes channels independently too: a black rim column
        // against a colored fill lands on (1,0,0) at some fractions — the same
        // alarm red stage 2's hold_hue exists to prevent, arrived at one stage
        // later. Keyed on the LOWER tap's content — usually the dominant one,
        // and not worth carrying fr in to settle the fringe case — so grey ink
        // keeps the separation stage 2 gave it instead of having a channel
        // lifted back.
        if (!row_grey[sx]) {
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
}

void crt_update_proc(Layer* layer, GContext* ctx) {
  (void)layer;
  if (!s_settings_crt || !ctx) return;
  // The captured bitmap IS the framebuffer on a native-format platform;
  // release before returning or the layer never lands.
  GBitmap* fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  GRect bounds = gbitmap_get_bounds(fb);
  crt_apply_framebuffer(gbitmap_get_data(fb), bounds.size.w, bounds.size.h, s_flash_phase);
  graphics_release_frame_buffer(ctx, fb);
  // Strike trigger lives at the END of the wake frame (not the backlight
  // handler): the sound starts once the ring can survive what follows.
  if (s_strike_pending) {
    s_strike_pending = false;
    crt_play_strike_sound();
    s_flash_phase = 0;
    if (s_crt_layer) layer_mark_dirty(s_crt_layer);
    s_flash_timer = app_timer_register(CRT_FLASH_TICK_MS, crt_flash_tick, NULL);
  }
}

static void crt_flash_tick(void* data) {
  (void)data;
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
  if (!on || !s_settings_crt || !s_app_in_focus) return;
  // The dismissal's backlight-on may land after focus returns — it belongs
  // to the cover too: eat the one-shot a cover armed, prime nothing.
  if (s_cover_suppresses_strike) {
    s_cover_suppresses_strike = false;
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

// The degauss woomp as synthesized PCM: note-table playback clicks at every
// note boundary (pitch and velocity step), so the hum is generated instead —
// a pitch-gliding sine (≈90 → 55 Hz) under a swell-and-decay amplitude
// envelope. Continuous by construction. 320ms at 16 kHz 16-bit sits well
// under SPEAKER_MAX_SAMPLE_BYTES_TOTAL. Synthesized once on first use.
#define CRT_STRIKE_PCM_MS \
  110  // <= stock FW's 128ms ring: the sound is a thunk, frames can't starve it
#define CRT_STRIKE_PCM_RATE 16000
#define CRT_STRIKE_PCM_SAMPLES (CRT_STRIKE_PCM_MS * CRT_STRIKE_PCM_RATE / 1000)
static int16_t s_strike_pcm[CRT_STRIKE_PCM_SAMPLES];
static bool s_strike_pcm_ready = false;

// Q15 sine: |x| in [-1,1] shaped by the cubic smoothstep y = (3x − x³)/2 —
// within 1.1% of sin(πx/2), no libm. (A hard fault inside newlib's
// __ieee754_rem_pio2 took down the app on hardware — sin() has no business
// in a watchface sound anyway.)
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

void crt_strike_synth(int16_t* buf, size_t n) {
  // Thunk, not hum. Stock firmware's audio ring holds 128ms and every render
  // frame blocks the loop ~60ms (measured 2026-08-23; the refill callback is
  // droppable under KernelMain load), so the strike sound must fit inside one
  // ring: 110ms. It also wants the band the speaker reproduces — the old
  // 90→55Hz hum put 100% of its energy under 300Hz, mostly inaudible. The
  // coil's energisation IS the perceptible event: constant 190 Hz + partials
  // 380/570/760 at 4:3:2:1, ~5.5ms attack, exponential decay (thermistor
  // shape), last 64 samples silent so the codec never cuts mid-cycle.
  const size_t attack = n / 20;
  uint32_t phase = 0;
  int env = 32767;  // Q15, thermistor decay: env -= env>>10 per sample
  for (size_t i = 0; i < n; i++) {
    phase += (uint32_t)((190u * 65536) / CRT_STRIKE_PCM_RATE);
    int y = (4 * crt_wave_q15(phase) + 3 * crt_wave_q15(phase * 2) + 2 * crt_wave_q15(phase * 3) +
             crt_wave_q15(phase * 4)) /
            10;
    int gain = i < attack ? (int)(i * 256 / attack) : 256;
    if (i + 64 >= n) gain = 0;
    buf[i] = (int16_t)(((int64_t)y * gain * env * 30000) >> 38);
    if (env && gain && i >= attack) env -= env >> 10;
  }
}

#define CRT_STRIKE_VOLUME 90

void crt_play_strike_sound(void) {
  // speaker_is_muted covers the system mute preference (including Quiet
  // Time-mutes-speaker when the user set it); s_app_in_focus is the App
  // Focus service state — the strike sound belongs to a tube the user is
  // looking at. Honor those, and our own toggle.
  if (!s_settings_crt_sound || !s_app_in_focus || speaker_is_muted()) return;
  if (!s_strike_pcm_ready) {
    crt_strike_synth(s_strike_pcm, CRT_STRIKE_PCM_SAMPLES);
    s_strike_pcm_ready = true;
  }
  // Audio underrun notes (hardware-measured 2026-08-23): the driver ring is
  // 128ms and each strike frame blocks the main loop ~95ms, so refill
  // callbacks lose the race and the woomp gaps. The stream API that would
  // let the app top the ring itself only exists from FW 4.33.2 — older stock
  // firmware HARD-FAULTS on the missing syscall (fallback-by-return-false is
  // impossible), so the old track path stays until those builds die out.
  // One unshifted note over the whole sample plays it raw: the PCM IS the
  // sound.
  static const SpeakerNote through[1] = {
      {60, SpeakerWaveformSine, CRT_STRIKE_PCM_MS + 30, 127, 0},
  };
  static const SpeakerSample sample = {.data = s_strike_pcm,
                                       .num_bytes = CRT_STRIKE_PCM_SAMPLES * 2,
                                       .format = SpeakerPcmFormat_16kHz_16bit,
                                       .base_midi_note = 60,
                                       .loop = false};
  const SpeakerTrack track = {.notes = through, .num_notes = 1, .sample = &sample};
  speaker_play_tracks(&track, 1, CRT_STRIKE_VOLUME);
}

void crt_app_focus_will_handler(bool in_focus) {
  // A cover is ABOUT to appear: silence immediately, not after the animation,
  // and arm the one-shot that eats the dismissal's backlight-on.
  if (!in_focus) {
    s_app_in_focus = false;
    s_cover_suppresses_strike = true;
  }
}

void crt_app_focus_did_handler(bool in_focus) {
  // Focus reported while the cover closes is too early; enable only on the
  // completed transition.
  if (in_focus) s_app_in_focus = true;
}

void crt_apply_setting_change(void) {
  if (s_settings_crt) {
    if (s_crt_layer) layer_mark_dirty(s_crt_layer);
    return;
  }
  s_flash_phase = CRT_FLASH_IDLE;
  s_strike_pending = false;  // a primed-but-unstarted strike dies with the toggle
  // Mid-strike toggle-off: kill the armed chain, or the next tick re-arms
  // from IDLE into a full strike nobody asked for.
  if (s_flash_timer) {
    app_timer_cancel(s_flash_timer);
    s_flash_timer = NULL;
  }
  // Off must ERASE, not just stop painting: the shader's pixels live in the
  // shared framebuffer and the next paint of anything underneath only happens
  // at the minute edge. Re-applying the window background dirties the whole
  // tree now — canvas and clock repaint, the overlay (disabled) adds nothing.
  if (s_main_window) window_set_background_color(s_main_window, s_active_theme->center_bg);
  if (s_crt_layer) layer_mark_dirty(s_crt_layer);
}
