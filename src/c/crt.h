#pragma once
#include <pebble.h>
#include "data.h"

// The CRT effect lives entirely here: a full-screen overlay layer stacked
// above the clock whose update proc captures the framebuffer (the public
// graphics_capture_frame_buffer API) and runs a per-pixel pass — a software
// "shader". Stage order: 1) chromatic aberration, 2) vignette + dither (in
// content space, so the darkening bends WITH the curvature; light-background
// themes take a steeper ease-in LUT — dot density over shade, black only at
// the rim — and hold ink one step clear of the level their field renders as,
// which a plain gain quantizes away), 3) curvature — the pincushion warp.
// Each axis maps its edge distance onto the LUTs' 0..16 domain, so
// CRT_VIGNETTE_SIDE_PX and CRT_VIGNETTE_BAND_PX size the four rims on their
// own. The degauss strike gathers every pixel from the frame as stage 2 left it,
// pulling its edges in as a pincushion pulse, black beyond the raster, and
// rotating each pixel's hue into purity stripes. The
// toggle gates the pass; off means the proc returns before capturing, so the
// effect costs exactly nothing.
//
// main.c owns the layer's lifecycle (create/stack/destroy), like the canvas;
// the handle is mirrored here the way drawing.h mirrors s_canvas_layer.
extern Layer* s_crt_layer;

// Curvature geometry. The warp's strength grows with the radius but it moves
// pixels horizontally only — dest (x,y) samples source column
// cx + (x-cx)·M/65536 with M = 65536 + CRT_WARP_R2_K·r² (r² = the
// elliptical Q8 xq+yq the CA zones also use). K=8 ≈ 3.1px of pull at a
// mid-edge (r²=256), smoothly more toward the corners. The vignette darkens
// within a band of the nearest edge, counted around the corner arcs.
//
// K also sets how much solid black the sides carry, which is not obvious: the
// outermost dest columns' sources run off the frame and clamp to column 0,
// which the vignette has already taken to depth 0. At K=8 three columns clamp,
// and with the depth-0 and depth-1 columns behind them that is the black no
// vignette constant can touch. Lower K is a flatter tube, so this trades bow
// for rim; higher K clamps more columns.
//
// VIGNETTE_PX is the falloff LUTs' domain — 17 sample points, depth 0..16 —
// not a size. Both axes map their edge distance onto that domain, so the two
// BAND constants below set how far the rim actually reaches without touching
// the tuned curves. Shrink or grow the rims there and nowhere else.
#define CRT_VIGNETTE_PX 16
// Side band, in CONTENT px. The warp COMPRESSES the rim inward rather than
// stretching it — 13 source columns land in 12 dest — so this is very nearly
// its own width on the glass, sitting behind the clamp below.
//
// It sets the width of the RAMP, not of the black: the clamp's columns do not
// move with it at all. Judge it with the per-pixel view (test/vignette_ramp.c),
// never by mean level — a mean saturates once a column reaches the field, so
// it cannot show the ramp lengthening.
//
// On a level-2 field, 12 renders the ladder 1,3,4,5,6 — that jump from 1 to 3
// is the sharp edge coming out of black. 16 renders 1,2,3,4,5,6, every step
// present. The channels dither out of phase, so wider bands keep working
// instead of punching black columns back through the middle of the ramp.
#define CRT_VIGNETTE_SIDE_PX 16
// Top/bottom band, in DISPLAY px — the warp is horizontal-only, so nothing
// resamples this axis and it is the figure you see.
//
// Keep (BAND_PX - EDGE_PX + 1) equal to VIGNETTE_PX. The row-to-depth map is
// an integer divide, so any other ratio makes some rows advance two LUT
// depths while their neighbours advance one: one border row reads dimmer than
// its neighbour and glyphs crossing the seam look like they wobble. Equal
// gives exactly one depth per row.
#define CRT_VIGNETTE_BAND_PX 18
// Rows held at depth 0 before the falloff starts, standing in for the columns
// the warp's clamp renders solid on the sides. Nothing clamps vertically, so
// without it the top would carry less black than the sides and read as
// missing. Track it to the clamp: at K=8 that is three each way.
#define CRT_VIGNETTE_EDGE_PX 3
// Gain below which the falloff is bezel, not content — the light LUT's depths
// 0 to 4, where the field is crushed and nothing is meant to be read. Ink/field
// separation (crt.c's clear_of_ground) stops here so the rim can reach black.
// Compare it against the curve when either changes: depth 4 is 96, which is
// under this, so the boundary sits between depths 4 and 5.
#define CRT_VIGNETTE_READ_Q8 128
// Corner arc radius, in LUT depth units (the 0..CRT_VIGNETTE_PX domain both
// axes map onto), not pixels.
#define CRT_CORNER_RADIUS 14
#define CRT_WARP_R2_K 8
// CA has two regimes. The four corner slots (crt.c's text_boxes: each one's
// title and value rows) take ONE constant whole-pixel shift per box, evaluated
// at the box centre: R and B are exact copies of the source one or more whole
// px over, never a blend, so glyphs keep their edges. Everywhere else (the
// middle slots, the TIME window and its clock, frames, ground, rim) CA is the
// radial ramp below, fractional because a whole-pixel step there would put a
// hard oval on the glass.
//
// The ramp's trouble was where that oval crossed a complication's text: a
// 1+1/3px shift appearing between one pixel and the next cuts the word in
// half. Inside a box that cannot happen. The shift steps only at a box edge,
// and the boxes are inset to flat ground (a frame stroke and a pixel of air
// clear of every glyph), so the step lands where there is nothing to cut.
//
// The ramp: zones by elliptical radius, in the xq+yq units stage 1 already
// computes: 0 at the centre, 256 at a mid-edge, 512 in a corner. Inside R2
// the image is clean; past it each channel separates, up to 1+1/3px —
// expressed in THIRDS (0..4) because outside a box stage 1 samples
// fractionally, and a per-half ±1 third in the pass cancels the panel's
// built-in element offset (the stripe does not mirror: −1 left, +1 right) so
// the centre converges instead of carrying a 2/3px floor. A box shift is the
// ramp at its centre rounded to whole px, (t3 + 1) / 3: 0 or 1.
//
// Horizontal RAMPS from 0 to 4 thirds rather than stepping. RAMP_SHIFT sets
// the width: 4 thirds over (4 << SHIFT) units of r², so 5 spans 128 and
// saturates just inside a mid-edge. A shift, not a divide — stage 1 runs this
// per pixel.
//
// Vertical still steps: its shift is whole rows (the ring samples y±1, there
// is no fractional row), so there is nothing between 0 and 1 to ramp through.
// Its jump is 1px against the horizontal's 1+1/3, and it was not the visible
// edge. A box takes the vertical shift at its centre, too.
#define CRT_CA_R2_Q8 170
#define CRT_CA_R2V_Q8 170
#define CRT_CA_RAMP_SHIFT 5
// Lowest rung the ramp starts on, in thirds. It cannot start at 0: the pass
// adds a per-half ±1 third to cancel the panel's element offset, so a rung of
// 1 makes q exactly 0 on the left half — no displacement at all, which would
// stop the red fringe mid-character on the 64px clock digits. Starting at 2
// keeps q off zero everywhere past the dead zone, at the cost of a 0->2 step
// at the boundary.
#define CRT_CA_RAMP_FLOOR 2

// Squared thresholds for the zone compare — no per-pixel sqrt needed.
#define CRT_CA_R2_X2Q8 ((CRT_CA_R2_Q8 * CRT_CA_R2_Q8 + 255) >> 8)
#define CRT_CA_R2V_X2Q8 ((CRT_CA_R2V_Q8 * CRT_CA_R2V_Q8 + 255) >> 8)

// The wake-up: a degauss strike. On backlight-on, FRAMES 90ms ticks of
// the raster's edges pulled in and pushed out in curves with the mains, black
// where it leaves the glass, and the picture striped in hues that thin to
// nothing as the amplitude decays, plus amplified channel separation — the
// CRT's shadow-mask demagnetization.
#define CRT_FLASH_TICK_MS 90
#define CRT_FLASH_PHASES 8
#define CRT_FLASH_IDLE (-1)

// One text box: columns x0..x1-1 and rows y0..y1-1 of the face, taking a
// constant CA of `sh` px across and `sv` rows down (before the strike's boost,
// which adds whole px to `sh`). `left` picks the half whose pull direction it
// uses: R from the right on the left half, mirrored on the right.
typedef struct {
  int x0, x1, y0, y1;
  int sh, sv;
  bool left;
} CrtTextBox;
#define CRT_TEXT_BOXES 4

// Pure pieces, unit-tested (all integer, fixed point):

#ifdef TEST_ENV
// The text box table of the face, left to right within each row band; returns
// its length.
int crt_text_boxes(const CrtTextBox** boxes);
// The strike gather's displacement at dest (x, y) in frame `phase`, px: the
// source sits at (x + dx, y + dy) before the curvature warp's own position.
void crt_strike_field(int x, int y, int phase, int* dx, int* dy);
// The hue band (0..5, sixths of a turn) the gather rotates dest (x, y) by.
int crt_strike_band(int x, int y, int phase);
// Q8 brightness factor at falloff depth `d` for the active theme's ground;
// 256 = untouched. The vignette ramp tool reads it.
int crt_vignette_q8_from_depth(int d);
#endif

// The whole pass over an 8-bit GColor8 framebuffer (0xAARRGGBB packed bytes,
// row-major, `stride` bytes per row, w*h visible). Touch only via these:
// tests drive it with a mock buffer.
void crt_apply_framebuffer(uint8_t* fb, int w, int h, int stride, int flash_phase);
// The same pass with its stages chosen: `aberration` is stage 1 (RGB fringing),
// `curvature` stages 2 and 3 (vignette, warp). With curvature off the sampling
// is an exact copy. A strike (flash_phase in range) runs the fringing and the
// pulse and stripes regardless, as the strike always shows both.
void crt_apply_framebuffer_passes(uint8_t* fb, int w, int h, int stride, int flash_phase,
                                  bool aberration, bool curvature);

void crt_update_proc(Layer* layer, GContext* ctx);
void crt_backlight_handler(bool on);

// Synthesises the degauss woomp to PCM, plays it via the speaker API and
// frees the buffer (the service keeps its own copy). Silent when the speaker is muted by system
// preference (including Quiet Time when the user set it to mute), or while the face is covered by
// a notification/alarm/modal/quick view — see the crt_app_focus_* handlers.
void crt_play_strike_sound(void);
// App Focus service callbacks (subscribed in main.c): track whether
// anything covers the face. While covered, the backlight handler primes no
// strike and the sound gate stays shut; a cover arms a one-shot that eats
// the next in-focus backlight-on, so the dismissal's flash cannot strike.
void crt_app_focus_will_handler(bool in_focus);
void crt_app_focus_did_handler(bool in_focus);
// Seeds the focus state for a subscription taken after launch, when the
// did_focus that would establish it has already come and gone.
void crt_set_in_focus(bool in_focus);
// Re-applies the effect settings: with any effect on it only marks the overlay
// dirty; turning the last off repaints — the OFF frame is painted by the
// canvas/clock underneath once the background re-dirties, the overlay then
// paints nothing over it. Turning degauss off ends any strike.
void crt_apply_setting_change(void);

// Window-unload teardown, called before main.c destroys the overlay layer:
// cancels the strike timer, resets the strike state, frees the strike PCM and
// clears s_crt_layer, so a late tick or backlight event cannot touch a freed
// layer. Idempotent.
void crt_deinit(void);
