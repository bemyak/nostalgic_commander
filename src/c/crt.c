#include <pebble.h>
#include "crt.h"
#include "main.h"
#include "theme.h"

// Everything runs on GColor8 bytes (0xAARRGGBB, 2 bits per channel) captured
// from the framebuffer. Emery-only build; the face targets no B&W platform.

#define GCOLOR8_ALPHA 0xC0
#define GCOLOR8_OPAQUE_BLACK 0xC0

// Current warm-up phase, or CRT_FLASH_IDLE. Advanced by the self-re-arming
// flash tick; written by crt_backlight_handler on backlight-on.
static int s_flash_phase = CRT_FLASH_IDLE;

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

int crt_warp_inset(int y, int h) {
  int dy = 2 * y - (h - 1);  // -(h-1)..h-1
  return (CRT_WARP_MAX_PX * dy * dy) / ((h - 1) * (h - 1));
}

int crt_ca_shift(int x, int y, int w, int h) {
  int dx = 2 * x - (w - 1);
  int dy = 2 * y - (h - 1);
  // Squared radial distance from the centre, Q8 (256 = corner).
  int q8 = ((dx * dx * 256) / ((w - 1) * (w - 1)) + (dy * dy * 256) / ((h - 1) * (h - 1))) / 2;
  int shift = (q8 * CRT_CA_MAX_SHIFT + 128) >> 8;
  return shift > CRT_CA_MAX_SHIFT ? CRT_CA_MAX_SHIFT : shift;
}

// Distance toward the nearest frame edge, counting around the corner arcs:
// inside the corner square the boundary is the arc, elsewhere the straight
// edge. 0 = boundary itself, growing inward.
static int crt_edge_distance(int x, int y, int w, int h) {
  int ex = x < w - 1 - x ? x : w - 1 - x;
  int ey = y < h - 1 - y ? y : h - 1 - y;
  if (ex < CRT_CORNER_RADIUS && ey < CRT_CORNER_RADIUS) {
    int rx = CRT_CORNER_RADIUS - ex;
    int ry = CRT_CORNER_RADIUS - ey;
    int d = CRT_CORNER_RADIUS - isqrt_floor(rx * rx + ry * ry);
    return d < 0 ? 0 : d;
  }
  return ex < ey ? ex : ey;
}

// The vignette falloff over edge-depth d, smoothstepped, Q8. A LUT: the pass
// does no per-pixel division, which is what kept the wake-up at 3–4 fps on
// hardware.
static const uint16_t s_vignette_q8[CRT_VIGNETTE_PX + 1] = {
    0, 1, 6, 15, 26, 40, 54, 71, 89, 108, 128, 145, 165, 183, 200, 216, 228, 240, 248, 254, 256};

// Same falloff without per-pixel arithmetic (the LUT above carries it).
static int crt_vignette_q8_from_depth(int d) {
  return d >= CRT_VIGNETTE_PX ? 256 : s_vignette_q8[d];
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

// c' = round(c * f / 256) with the pixel's Bayer threshold breaking the
// rounding direction — shift-only (f <= 256 keeps c*16*f under 12k).
static int dither_channel(int c, int f_q8, int t) {
  int v = ((c * 16 * f_q8 >> 8) + t) >> 4;
  return v > 3 ? 3 : v;
}

void crt_apply_framebuffer(uint8_t* fb, int w, int h, int flash_phase) {
  static uint8_t row_ca[200];
  // CA's x² column term: filled once for the actual width (constant on emery,
  // but the field keeps the host tests honest).
  static uint16_t s_ca_xterm[200];
  static int s_ca_xterm_w = 0;
  if (w > (int)sizeof(row_ca)) return;
  if (s_ca_xterm_w != w) {
    for (int x = 0; x < w; x++) {
      int dx = 2 * x - (w - 1);
      s_ca_xterm[x] = (uint16_t)((dx * dx * 256) / ((w - 1) * (w - 1)));
    }
    s_ca_xterm_w = w;
  }
  // CA boost during the strike: the separation balloons while the mask
  // degausses. Phase-indexed through the same decay table as the row jitter.
  int ca_boost = (flash_phase >= 0 && flash_phase < CRT_FLASH_PHASES)
                     ? (s_strike_amp_px[flash_phase] + 1) / 2
                     : 0;
  const int h1sq = (h - 1) * (h - 1);

  for (int y = 0; y < h; y++) {
    uint8_t* row = fb + (size_t)y * w;
    int dy = 2 * y - (h - 1);
    int yterm = dy * dy * 256 / h1sq;
    int row_off = crt_strike_offset(y, flash_phase);

    // 1) CA on the RAW row: the fringe samples clean pixels, so a uniform
    //    background stays uniform — neither side borrows vignette darkness.
    for (int x = 0; x < w; x++) {
      int q8 = (s_ca_xterm[x] + yterm) / 2;
      int s = ((q8 * CRT_CA_MAX_SHIFT + 128) >> 8) + ca_boost;
      if (s <= 0) {
        row_ca[x] = row[x];
      } else {
        int xl = x - s < 0 ? 0 : x - s;
        int xr = x + s >= w ? w - 1 : x + s;
        row_ca[x] = GCOLOR8_ALPHA | (row[xl] & 0x30) | (row[x] & 0x0C) | (row[xr] & 0x03);
      }
    }

    // 2) Dest write: warp the source column (+ strike jitter), dim with the
    //    vignette, dither with mirror-symmetric thresholds so left and right
    //    rims speckle identically.
    int inset = crt_warp_inset(y, h);
    int mul16 = (w << 16) / (w - 2 * inset);  // Q16 jacobian; ==65536 mid-rows
    int ey = y < h - 1 - y ? y : h - 1 - y;
    int ty = ey & 3;
    bool corner_row = ey < CRT_CORNER_RADIUS;
    for (int x = 0; x < w; x++) {
      int ex = x < w - 1 - x ? x : w - 1 - x;
      int d;
      if (corner_row && ex < CRT_CORNER_RADIUS) {
        int rx = CRT_CORNER_RADIUS - ex;
        int ry = CRT_CORNER_RADIUS - ey;
        d = CRT_CORNER_RADIUS - isqrt_floor(rx * rx + ry * ry);
        if (d < 0) d = 0;
      } else {
        d = ex < ey ? ex : ey;
      }
      int f = crt_vignette_q8_from_depth(d);

      // Source column: sx = cx + (x - cx) * (w / (w - 2*inset)), cx=(w-1)/2.
      // Rounded to nearest on BOTH signs — a >> on negative deltas floors
      // away from zero and clipped the left rim a quantum earlier than the
      // right (visible on hardware as a left-edge shift under each header).
      int dx = 2 * x - (w - 1);
      int sn = (w - 1) * 65536 + dx * mul16 + 65536;  // sx*131072 + half
      int sx = (sn >> 17) + row_off;                  // strike: the whole row slides sideways
      if (sx < 0 || sx >= w) {
        row[x] = GCOLOR8_OPAQUE_BLACK;
        continue;
      }
      uint8_t p = row_ca[sx];
      int t = s_bayer4[ty * 4 + (ex & 3)];
      int r = dither_channel((p >> 4) & 3, f, t);
      int g = dither_channel((p >> 2) & 3, f, t);
      int b = dither_channel(p & 3, f, t);
      row[x] = GCOLOR8_ALPHA | (uint8_t)((r << 4) | (g << 2) | b);
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
}

static void crt_flash_tick(void* data) {
  (void)data;
  s_flash_phase++;
  if (s_flash_phase >= CRT_FLASH_PHASES) s_flash_phase = CRT_FLASH_IDLE;
  if (s_crt_layer) layer_mark_dirty(s_crt_layer);
  // One-shot timers free themselves on firing. A NULL return (pool
  // exhausted) just ends the flash early — the next backlight-on restarts it
  // at the strike phase anyway.
  if (s_flash_phase != CRT_FLASH_IDLE) {
    app_timer_register(CRT_FLASH_TICK_MS, crt_flash_tick, NULL);
  }
}

void crt_backlight_handler(bool on) {
  // Backlight-only gating: the flash is the strike of a tube warming up;
  // backlight-off transitions and a disabled effect start nothing.
  if (!on || !s_settings_crt) return;
  s_flash_phase = 0;
  crt_play_strike_sound();
  if (s_crt_layer) layer_mark_dirty(s_crt_layer);
  app_timer_register(CRT_FLASH_TICK_MS, crt_flash_tick, NULL);
}

// speaker_play_notes over a static table: the degauss woomp — a short square
// thunk, then a falling triangle hum whose pitch AND velocity decay out.
// ≈300ms, matching the strike's visual decay.
static const SpeakerNote s_strike_notes[] = {
    {48, SpeakerWaveformSquare, 15, 127, 0},    // thunk: C2 spring hit
    {40, SpeakerWaveformTriangle, 60, 127, 0},  // E2, falling...
    {38, SpeakerWaveformTriangle, 60, 90, 0},  {36, SpeakerWaveformTriangle, 70, 60, 0},
    {35, SpeakerWaveformTriangle, 100, 35, 0},  // ...and fading out
};

void crt_play_strike_sound(void) {
  // speaker_is_muted covers both system mute and Quiet Time; honor it, and
  // the sound toggle.
  if (!s_settings_crt_sound || speaker_is_muted()) return;
  speaker_play_notes(s_strike_notes, sizeof(s_strike_notes) / sizeof(SpeakerNote), 70);
}

void crt_apply_setting_change(void) {
  if (s_settings_crt) {
    if (s_crt_layer) layer_mark_dirty(s_crt_layer);
    return;
  }
  s_flash_phase = CRT_FLASH_IDLE;
  // Off must ERASE, not just stop painting: the shader's pixels live in the
  // shared framebuffer and the next paint of anything underneath only happens
  // at the minute edge. Re-applying the window background dirties the whole
  // tree now — canvas and clock repaint, the overlay (disabled) adds nothing.
  if (s_main_window) window_set_background_color(s_main_window, s_active_theme->center_bg);
  if (s_crt_layer) layer_mark_dirty(s_crt_layer);
}
