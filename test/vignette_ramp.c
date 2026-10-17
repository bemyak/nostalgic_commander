// Prints what the CRT vignette actually renders, per theme. Run it with
// `make vignette-ramp` from the repo root.
//
// The vignette has nine tunables in crt.h, two falloff LUTs and three
// per-theme fields, and they interact through a 2-bit-per-channel panel — so
// the same constant lands differently on each theme's ground and the source
// tells you almost nothing about the result. Every tuning bug found in this
// file's history was invisible in the code and obvious in this table.
//
// Columns are mean rendered level (0..3) averaged over a band of rows/columns,
// so ordered-dither phase averages out and what is left is the ramp a reader
// perceives. Read them for: how many cells sit at 0.00 (the solid black bar),
// how many carry an intermediate value (the shadow), and where it reaches the
// field's own level (the rim's total width).
//
// "first lit depth" is the trap this tool exists to catch. A field at grey
// level N renders q4 = N*f/16, so it lights no dot at all until f reaches
// 16/N. On a level-1 field that is f >= 16, which is why a LUT entry of 12 at
// depth 1 was a second solid-black depth on Navigator while being a harmless
// 6%-lit depth on Dialog. If that figure is not 1, the low end of the light
// LUT is being wasted on the darker field.

#include <stdio.h>
#include <string.h>
#include "pebble.h"

#include "../src/c/data.c"
#include "../src/c/complication.c"
#include "../src/c/theme.c"
#include "../src/c/status.c"
#include "../src/c/drawing.c"
#include "../src/c/messaging.c"
#include "../src/c/chime.c"
#include "../src/c/crt.c"
#include "../src/c/main.c"

#define RAMP_CELLS (CRT_VIGNETTE_BAND_PX + 1)

static double mean_level(uint8_t p) {
  return (((p >> 4) & 3) + ((p >> 2) & 3) + (p & 3)) / 3.0;
}

// Lowest LUT depth at which this field renders anything but black. Indexes the
// curve directly — going through crt_vignette_q8 would need an x coordinate,
// and the depth->x inverse rounds, which reports the first lit COLUMN instead.
static int first_lit_depth(int ground) {
  if (ground <= 0) return -1;
  for (int d = 1; d <= CRT_VIGNETTE_PX; d++) {
    if (gain_q4(ground, crt_vignette_q8_from_depth(d)) >= 1) return d;
  }
  return -1;
}

static void report(const char* name, const WatchTheme* theme, uint8_t fill) {
  s_settings_crt = 1;
  s_flash_phase = CRT_FLASH_IDLE;
  s_active_theme = theme;
  memset(mock_framebuffer, fill, sizeof(mock_framebuffer));
  crt_apply_framebuffer(mock_framebuffer, SCREEN_W, SCREEN_H, SCREEN_W, CRT_FLASH_IDLE);

  int ground = theme_ground_level(theme);
  printf("%-10s ground=%d curve=%-5s ", name, ground, ground > 0 ? "light" : "dark");
  int lit = first_lit_depth(ground);
  if (lit > 0) {
    printf("first-lit-depth=%d\n", lit);
  } else if ((fill & 0x3F) == 0) {
    printf("(field is black: the falloff is invisible on it, only ink dims)\n");
  } else {
    printf("(no grey ground; separation and the light curve are both off)\n");
  }

  printf("  side ");
  for (int x = 0; x <= CRT_VIGNETTE_SIDE_PX + 1; x++) {
    double s = 0;
    for (int y = 90; y < 138; y++) s += mean_level(mock_framebuffer[y * 200 + x]);
    printf("%.2f ", s / 48.0);
  }
  printf("\n  top  ");
  for (int y = 0; y <= CRT_VIGNETTE_BAND_PX; y++) {
    double s = 0;
    for (int x = 70; x < 130; x++) s += mean_level(mock_framebuffer[y * 200 + x]);
    printf("%.2f ", s / 60.0);
  }
  printf("\n\n");
}

// pebble_mock.c asserts through Unity, which links these in. Nothing here runs
// a Unity test, so they stay empty.
void setUp(void) {}
void tearDown(void) {}

int main(void) {
  printf("SIDE_PX=%d BAND_PX=%d EDGE_PX=%d READ_Q8=%d WARP_K=%d\n\n", CRT_VIGNETTE_SIDE_PX,
         CRT_VIGNETTE_BAND_PX, CRT_VIGNETTE_EDGE_PX, CRT_VIGNETTE_READ_Q8, CRT_WARP_R2_K);
  report("panel", &s_theme_panel, 0xC2);          // DukeBlue field
  report("shadow", &s_theme_shadow, 0xC0);        // Black field
  report("dialog", &s_theme_dialog, 0xEA);        // LightGray field
  report("navigator", &s_theme_navigator, 0xD5);  // DarkGray field
  report("amber", &s_theme_amber, 0xD0);          // BulgarianRose field
  return 0;
}
