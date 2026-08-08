#include <pebble.h>
#include "theme.h"
#include "drawing.h"  // For s_main_window

const WatchTheme* s_active_theme = NULL;

// Norton Commander's panel: EGA blue ground, cyan frames, white entries. Status
// colors are the high-intensity variants, which is what reads on blue.
const WatchTheme s_theme_panel = {.center_bg = GColorDukeBlue,           // #0000AA
                                  .accent_cold = GColorElectricBlue,     // #55FFFF
                                  .frame = GColorTiffanyBlue,            // #00AAAA
                                  .text_primary = GColorWhite,           // #FFFFFF
                                  .text_secondary = GColorElectricBlue,  // #55FFFF
                                  .mark = GColorIcterine,                // #FFFF55
                                  .status_ink = GColorBlack,             // #000000
                                  .status_green = GColorScreaminGreen,   // #55FF55
                                  .status_yellow = GColorIcterine,       // #FFFF55
                                  .status_red = GColorSunsetOrange};     // #FF5555

// The same panel in shadow. With 16 colors and no way to darken one, DOS-era
// Turbo Vision faked a dimmed panel by drawing it grey-on-black. Three grey
// tiers (55 chrome, AA titles, FF values) keep the hierarchy intact.
const WatchTheme s_theme_shadow = {.center_bg = GColorBlack,             // #000000
                                   .accent_cold = GColorElectricBlue,    // #55FFFF
                                   .frame = GColorDarkGray,              // #555555
                                   .text_primary = GColorWhite,          // #FFFFFF
                                   .text_secondary = GColorLightGray,    // #AAAAAA
                                   .mark = GColorIcterine,               // #FFFF55
                                   .status_ink = GColorBlack,            // #000000
                                   .status_green = GColorScreaminGreen,  // #55FF55
                                   .status_yellow = GColorIcterine,      // #FFFF55
                                   .status_red = GColorSunsetOrange};    // #FF5555

// The Turbo Vision dialog box — text attribute 0x70, black on light grey, the
// palette NC used for its own menus. On a light ground everything drawn as text
// has to be a low-intensity color to stay legible, brown standing in as the
// palette's dark yellow, and status_ink flips to white to clear those fills.
// Turbo Vision highlighted hotkeys with 0x7E, yellow on grey — authentic, but
// far too faint to read on a watch, so marks take the dark yellow instead.
const WatchTheme s_theme_dialog = {.center_bg = GColorLightGray,            // #AAAAAA
                                   .accent_cold = GColorDukeBlue,           // #0000AA
                                   .frame = GColorDarkGray,                 // #0000AA
                                   .text_primary = GColorBlack,             // #000000
                                   .text_secondary = GColorDarkGray,        // #555555
                                   .mark = GColorDarkCandyAppleRed,         // #AA5500
                                   .status_ink = GColorWhite,               // #FFFFFF
                                   .status_green = GColorIslamicGreen,      // #00AA00
                                   .status_yellow = GColorWindsorTan,       // #AA5500
                                   .status_red = GColorDarkCandyAppleRed};  // #AA0000

// DOS Navigator's default screen as it actually renders: dark grey ground,
// light text and chrome, dim grey secondary readouts, yellow hotkey marks.
// Status stays on the bright variants to clear the grey, ink flips to black
// on fills.
const WatchTheme s_theme_navigator = {.center_bg = GColorDarkGray,          // #555555
                                      .accent_cold = GColorElectricBlue,    // #55FFFF
                                      .frame = GColorWhite,                 // #FFFFFF
                                      .text_primary = GColorWhite,          // #FFFFFF
                                      .text_secondary = GColorLightGray,    // #AAAAAA
                                      .mark = GColorIcterine,               // #FFFF55
                                      .status_ink = GColorBlack,            // #000000
                                      .status_green = GColorScreaminGreen,  // #55FF55
                                      .status_yellow = GColorIcterine,      // #FFFF55
                                      .status_red = GColorSunsetOrange};    // #FF5555

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

void apply_theme(struct tm* tick_time) {
  s_active_theme = determine_theme(s_settings_theme, tick_time->tm_hour);

  if (s_main_window) {
    window_set_background_color(s_main_window, s_active_theme->center_bg);
  }
}

// Hot/cold bands, shared by every temperature reading (temp alone, the
// combined weather value, and the day's high) so the thresholds live once.
static GColor temp_band_color(int temp) {
  if (s_settings_units == 1) {  // Metric (Celsius)
    if (temp > 29) return s_active_theme->status_red;
    if (temp < 4) return s_active_theme->accent_cold;  // Blue
  } else {                                             // Imperial (Fahrenheit)
    if (temp > 85) return s_active_theme->status_red;
    if (temp < 40) return s_active_theme->accent_cold;  // Blue
  }
  return s_active_theme->text_primary;
}

GColor get_source_color(ComplicationDataSource source) {
  if (!s_active_theme) return GColorWhite;

  switch (source) {
    case DATA_SOURCE_BATTERY:
    case DATA_SOURCE_BATTERY_BAR:
      // On the charger it says so in green, level notwithstanding; off it,
      // quiet while healthy — the battery only speaks once it wants a
      // charger. Chip and bar share this ladder, so one reading never wears
      // two colors.
      if (s_battery_charging) return s_active_theme->status_green;
      if (s_battery_level > BATTERY_LOW_PCT) return s_active_theme->text_primary;
      if (s_battery_level > BATTERY_CRIT_PCT) return s_active_theme->status_yellow;
      return s_active_theme->status_red;
    // Plain readouts, drawn in the primary text color. Heart rate belongs here,
    // not with the status colors: it has no thresholds to encode, so tinting it
    // only made it look like a warning. Bluetooth and Quiet Time say it with
    // checkbox glyphs, so they need no color either. Same for humidity:
    // outdoor RH has no actionable threshold, its diurnal swing makes bands
    // noise.
    case DATA_SOURCE_STEPS:
    case DATA_SOURCE_ACTIVE_MINUTES:
    case DATA_SOURCE_HEART_RATE:
    case DATA_SOURCE_BLUETOOTH:
    case DATA_SOURCE_BT_QT:
    case DATA_SOURCE_QUIET_TIME:
    case DATA_SOURCE_HUMIDITY:
    case DATA_SOURCE_HUM_PCP:  // halves band on their own in the drawer
      return s_active_theme->text_primary;
    // Wind shows sustained speed (the consumer convention — Yle/FMI family);
    // the band follows Beaufort rungs on it, unit-rounded: strong breeze
    // (Bf 6) yellow, gale (Bf 8) red. The speed is stored in the settings
    // unit, so rungs follow it.
    case DATA_SOURCE_WIND:
      if (s_weather_wind_speed < 0) return s_active_theme->text_primary;
      if (s_weather_wind_speed >= (s_settings_units == 1 ? 17 : 39))
        return s_active_theme->status_red;
      if (s_weather_wind_speed >= (s_settings_units == 1 ? 11 : 25))
        return s_active_theme->status_yellow;
      return s_active_theme->text_primary;
    case DATA_SOURCE_WEATHER_TEMP:
    case DATA_SOURCE_WEATHER:
      return temp_band_color(s_weather_temp);
    // Clean air and mild sun are unremarkable: only a flagged reading
    // earns a color, and the band follows it. (AQI >50 / >100, UV >=3 / >=6.)
    case DATA_SOURCE_AQI:
      if (s_weather_aqi == -1) return s_active_theme->text_primary;
      if (s_weather_aqi > 100) return s_active_theme->status_red;
      if (s_weather_aqi > 50) return s_active_theme->status_yellow;
      return s_active_theme->text_primary;
    case DATA_SOURCE_UV:
      if (s_weather_uv == -1) return s_active_theme->text_primary;
      if (s_weather_uv >= 6) return s_active_theme->status_red;
      if (s_weather_uv >= 3) return s_active_theme->status_yellow;
      return s_active_theme->text_primary;
    case DATA_SOURCE_WEATHER_PCP:
      if (weather_shows_precip_amount()) {
        // WMO intensity bands (mm over the past hour): light rain is calm;
        // heavy is worth a thought, violent is a warning.
        int mm = s_precip_now / 10;
        if (mm >= 8) return s_active_theme->status_red;
        if (mm >= 4) return s_active_theme->status_yellow;
        return s_active_theme->text_primary;
      }
      // A dry timeline is unremarkable — at or under 50 there is nothing to
      // plan around. Past 50: worth a thought; past 70: plan around it.
      if (s_weather_pcp == -1 || s_weather_pcp <= 50) return s_active_theme->text_primary;
      if (s_weather_pcp > 70) return s_active_theme->status_red;
      return s_active_theme->status_yellow;
    case DATA_SOURCE_TEMP_HIGH_LOW: {
      // The high on display is the day's headline and alone carries the color
      // — a freezing low under a mild high stays neutral. Once today's peak
      // passes, tomorrow's high takes the headline.
      int hi = high_low_displayed_high();
      if (hi == -999) return s_active_theme->text_primary;
      return temp_band_color(hi);
    }
    case DATA_SOURCE_AQI_UV: {
      if (s_weather_aqi == -1 && s_weather_uv == -1) return s_active_theme->text_primary;
      bool is_red = (s_weather_aqi > 100 || s_weather_uv >= 6);
      bool is_yellow = (s_weather_aqi > 50 || s_weather_uv >= 3);
      if (is_red) return s_active_theme->status_red;
      if (is_yellow) return s_active_theme->status_yellow;
      return s_active_theme->text_primary;
    }
    default:
      return s_active_theme->text_primary;
  }
}
