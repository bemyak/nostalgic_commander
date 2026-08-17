#pragma once
#include <pebble.h>
#include "data.h"

// The CRT effect lives entirely here: a full-screen overlay layer stacked
// above the clock whose update proc captures the framebuffer (the public
// graphics_capture_frame_buffer API) and runs a per-pixel pass — a software
// "shader": vignette curvature falloff with ordered dithering, chromatic
// aberration, a pincushion warp, and the backlight warm-up flash as a
// vignette modulation. The toggle gates the pass; off means the proc returns
// before capturing, so the effect costs exactly nothing.
//
// main.c owns the layer's lifecycle (create/stack/destroy), like the canvas;
// the handle is mirrored here the way drawing.h mirrors s_canvas_layer.
extern Layer* s_crt_layer;

// Curvature geometry. The vignette darkens within VIGNETTE_PX of the nearest
// edge (counted around the corner arcs); the warp pulls content inward by up
// to WARP_MAX px at the top/bottom rows; the CA samples channels up to
// CA_MAX_SHIFT px apart at the outermost pixels, none at the centre.
#define CRT_VIGNETTE_PX 20
#define CRT_CORNER_RADIUS 14
#define CRT_WARP_MAX_PX 5
#define CRT_CA_MAX_SHIFT 3

// The wake-up: a degauss strike. On backlight-on, FRAMES 50ms ticks of
// per-row horizontal jitter with decaying amplitude plus amplified channel
// separation — the CRT's shadow-mask demagnetization wobble.
#define CRT_FLASH_TICK_MS 90
#define CRT_FLASH_PHASES 8
#define CRT_FLASH_IDLE (-1)

// Pure pieces, unit-tested (all integer, fixed point):

// Q8 brightness factor at (x,y): 256 = untouched, 0 = black. Vignette
// falloff, smoothstepped, idle-only — the strike shakes geometry, not gain.
int crt_vignette_q8(int x, int y, int w, int h);
// Row x-offset during strike frame `flash_phase`: 0 when idle. Amplitude
// decays to 0 across phases; deterministic per (y, phase).
int crt_strike_offset(int y, int flash_phase);
// Horizontal source inset at row y: 0 at the middle row, growing to
// CRT_WARP_MAX_PX at the top/bottom rows (the screen "bends away").
int crt_warp_inset(int y, int h);
// CA channel-sample offset in px at (x,y): 0 at the centre, growing with the
// squared distance to CRT_CA_MAX_SHIFT at the extreme pixels.
int crt_ca_shift(int x, int y, int w, int h);

// The whole pass over an 8-bit GColor8 framebuffer (0xAARRGGBB packed bytes,
// row-major, w*h). Touch only via these: tests drive it with a mock buffer.
void crt_apply_framebuffer(uint8_t* fb, int w, int h, int flash_phase);

void crt_update_proc(Layer* layer, GContext* ctx);
void crt_backlight_handler(bool on);

// The strike's degauss crackle, as a note table played at backlight-on when
// the sound toggle is on: a static-tick opener (short square), then a falling
// triangle glissando. Nothing synthesized — the speaker API's own voices.
void crt_play_strike_sound(void);
// Settings push may have flipped the toggle: sync the flash state and mark
// the overlay dirty; turning off repaints — the OFF frame is painted by the
// canvas/clock underneath once the background re-dirties, the overlay then
// paints nothing over it.
void crt_apply_setting_change(void);
