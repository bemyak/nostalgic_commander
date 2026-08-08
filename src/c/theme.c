#include <pebble.h>
#include "theme.h"

const WatchTheme* s_active_theme = NULL;

// Norton Commander's panel: EGA blue ground, cyan frames, white entries. Status
// colors are the high-intensity variants, which is what reads on blue.
const WatchTheme s_theme_panel = {.center_bg = GColorDukeBlue,
                                  .accent_cold = GColorElectricBlue,
                                  .frame = GColorTiffanyBlue,
                                  .text_primary = GColorWhite,
                                  .text_secondary = GColorElectricBlue,
                                  .mark = GColorIcterine,
                                  .status_ink = GColorBlack,
                                  .status_green = GColorScreaminGreen,
                                  .status_yellow = GColorIcterine,
                                  .status_red = GColorSunsetOrange};

// The same panel in shadow. With 16 colors and no way to darken one, DOS-era
// Turbo Vision faked a dimmed panel by drawing it grey-on-black. Three grey
// tiers (55 chrome, AA titles, FF values) keep the hierarchy intact.
const WatchTheme s_theme_shadow = {.center_bg = GColorBlack,
                                   .accent_cold = GColorElectricBlue,
                                   .frame = GColorDarkGray,
                                   .text_primary = GColorWhite,
                                   .text_secondary = GColorLightGray,
                                   .mark = GColorIcterine,
                                   .status_ink = GColorBlack,
                                   .status_green = GColorScreaminGreen,
                                   .status_yellow = GColorIcterine,
                                   .status_red = GColorSunsetOrange};

// The Turbo Vision dialog box — text attribute 0x70, black on light grey, the
// palette NC used for its own menus. On a light ground everything drawn as text
// has to be a low-intensity color to stay legible, brown standing in as the
// palette's dark yellow, and status_ink flips to white to clear those fills.
// Turbo Vision highlighted hotkeys with 0x7E, yellow on grey — too faint to
// read on a watch, so the mark takes the dark red instead.
const WatchTheme s_theme_dialog = {.center_bg = GColorLightGray,
                                   .accent_cold = GColorDukeBlue,
                                   .frame = GColorDarkGray,
                                   .text_primary = GColorBlack,
                                   .text_secondary = GColorDarkGray,
                                   .mark = GColorDarkCandyAppleRed,
                                   .status_ink = GColorWhite,
                                   .status_green = GColorIslamicGreen,
                                   .status_yellow = GColorWindsorTan,
                                   .status_red = GColorDarkCandyAppleRed};

// DOS Navigator's default screen as it actually renders: dark grey ground,
// light text and chrome, dim grey secondary readouts, yellow hotkey marks.
// Status stays on the bright variants to clear the grey, ink flips to black
// on fills.
const WatchTheme s_theme_navigator = {.center_bg = GColorDarkGray,
                                      .accent_cold = GColorElectricBlue,
                                      .frame = GColorWhite,
                                      .text_primary = GColorWhite,
                                      .text_secondary = GColorLightGray,
                                      .mark = GColorIcterine,
                                      .status_ink = GColorBlack,
                                      .status_green = GColorScreaminGreen,
                                      .status_yellow = GColorIcterine,
                                      .status_red = GColorSunsetOrange};

// Auto walks the three themes on 8-hour shifts, brightest first: the light
// dialog through the morning, the blue panel through the afternoon, and the
// shadowed panel overnight.
static const WatchTheme* theme_for_hour(int current_hour) {
  if (current_hour >= 6 && current_hour < 14) return &s_theme_dialog;
  if (current_hour >= 14 && current_hour < 22) return &s_theme_panel;
  return &s_theme_shadow;  // 22:00 to 06:00
}

const WatchTheme* determine_theme(int theme_setting, int current_hour) {
  switch (theme_setting) {
    case 1:
      return &s_theme_dialog;
    case 2:
      return &s_theme_panel;
    case 3:
      return &s_theme_shadow;
    case 4:
      return &s_theme_navigator;
    default:  // 0 = Auto, and anything unrecognized
      return theme_for_hour(current_hour);
  }
}

// Palette selection only — main.c repaints the window background itself;
// a theme module has no business touching the window.
void apply_theme(struct tm* tick_time) {
  s_active_theme = determine_theme(s_settings_theme, tick_time->tm_hour);
}
