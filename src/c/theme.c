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
// palette's dark yellow (CGA has none), and status_ink flips to white to clear
// those fills. Brown's 2:1 red-to-green is why crt.c holds a hue's channels
// together through the falloff: dimmed per channel it lands on the alarm red.
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

// DOS Navigator 1.51's panels as they render: dark grey ground, white frames
// and entries, yellow column headers, the file info line in light cyan. Titles
// take the headers' yellow and marks the info line's cyan. Status stays on the
// bright variants to clear the grey, ink flips to black on fills; light red is
// the only red the 16 offer that reads on dark grey.
const WatchTheme s_theme_navigator = {.center_bg = GColorDarkGray,
                                      .accent_cold = GColorElectricBlue,
                                      .frame = GColorWhite,
                                      .text_primary = GColorWhite,
                                      .text_secondary = GColorIcterine,
                                      .mark = GColorElectricBlue,
                                      .status_ink = GColorBlack,
                                      .status_green = GColorScreaminGreen,
                                      .status_yellow = GColorIcterine,
                                      .status_red = GColorSunsetOrange};

// An amber monochrome display: the glass glows even where nothing is drawn, so
// the ground is the darkest warm tone the palette has. That is maroon, there being no darker warm
// one. Values are white and the amber carries titles and frames, for the same legibility, since
// amber-on-maroon is too faint to read at a glance. Status stays in color. Red is Melon because
// FF5555 is only about 4.8:1 on maroon and reads as the ground's own hue. Cold is ElectricBlue, the
// one blue clear of both the amber and the ground.
const WatchTheme s_theme_amber = {.center_bg = GColorBulgarianRose,
                                  .accent_cold = GColorElectricBlue,
                                  .frame = GColorChromeYellow,
                                  .text_primary = GColorWhite,
                                  .text_secondary = GColorChromeYellow,
                                  .mark = GColorIcterine,
                                  .status_ink = GColorBlack,
                                  .status_green = GColorScreaminGreen,
                                  .status_yellow = GColorIcterine,
                                  .status_red = GColorMelon};

uint8_t theme_ground_level(const WatchTheme* t) {
  if (gcolor_equal(t->center_bg, GColorLightGray)) return 2;
  if (gcolor_equal(t->center_bg, GColorDarkGray)) return 1;
  // Not grey, but a level-1 field all the same: (1,0,0) takes the same curve.
  if (gcolor_equal(t->center_bg, GColorBulgarianRose)) return 1;
  return 0;
}

const WatchTheme* determine_theme(int theme_setting) {
  switch (theme_setting) {
    case THEME_SETTING_DIALOG:
      return &s_theme_dialog;
    case THEME_SETTING_PANEL:
      return &s_theme_panel;
    case THEME_SETTING_SHADOW:
      return &s_theme_shadow;
    case THEME_SETTING_NAVIGATOR:
      return &s_theme_navigator;
    case THEME_SETTING_AMBER:
      return &s_theme_amber;
    default:  // anything unrecognized, a stale 0 included, falls back to Norton
      return &s_theme_panel;
  }
}

int pick_theme_setting(int day_setting, int night_setting, bool located, int elevation) {
  if (night_setting != 0 && located && elevation < SUNSET_ELEVATION) return night_setting;
  return day_setting;
}

// Palette selection only — main.c repaints the window background itself;
// a theme module has no business touching the window.
void apply_theme(void) {
  s_active_theme = determine_theme(pick_theme_setting(s_settings_theme, s_settings_night_theme,
                                                      location_known(), s_solar_elevation));
}
