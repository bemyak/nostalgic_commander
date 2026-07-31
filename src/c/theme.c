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
                                   .frame = GColorDukeBlue,                 // #0000AA
                                   .text_primary = GColorBlack,             // #000000
                                   .text_secondary = GColorDarkGray,        // #555555
                                   .mark = GColorWindsorTan,                // #AA5500
                                   .status_ink = GColorWhite,               // #FFFFFF
                                   .status_green = GColorIslamicGreen,      // #00AA00
                                   .status_yellow = GColorWindsorTan,       // #AA5500
                                   .status_red = GColorDarkCandyAppleRed};  // #AA0000

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

GColor get_source_color(ComplicationDataSource source) {
  if (!s_active_theme) return GColorWhite;

  switch (source) {
    case DATA_SOURCE_BATTERY:
      if (s_battery_level > BATTERY_LOW_PCT) return s_active_theme->status_green;
      if (s_battery_level > BATTERY_CRIT_PCT) return s_active_theme->status_yellow;
      return s_active_theme->status_red;
    // Plain readouts, drawn in the primary text color. Heart rate belongs here,
    // not with the status colors: it has no thresholds to encode, so tinting it
    // only made it look like a warning. Bluetooth says it with a checkbox
    // glyph, so it needs no color either.
    case DATA_SOURCE_STEPS:
    case DATA_SOURCE_ACTIVE_MINUTES:
    case DATA_SOURCE_HEART_RATE:
    case DATA_SOURCE_BLUETOOTH:
      return s_active_theme->text_primary;
    case DATA_SOURCE_WEATHER_TEMP:
    case DATA_SOURCE_WEATHER:
      if (s_settings_units == 1) {  // Metric (Celsius)
        if (s_weather_temp > 29) return s_active_theme->status_red;
        if (s_weather_temp < 4) return s_active_theme->accent_cold;  // Blue
      } else {                                                       // Imperial (Fahrenheit)
        if (s_weather_temp > 85) return s_active_theme->status_red;
        if (s_weather_temp < 40) return s_active_theme->accent_cold;  // Blue
      }
      return s_active_theme->text_primary;
    case DATA_SOURCE_AQI:
      if (s_weather_aqi == -1) return s_active_theme->text_primary;
      if (s_weather_aqi > 100) return s_active_theme->status_red;
      if (s_weather_aqi > 50) return s_active_theme->status_yellow;
      return s_active_theme->status_green;
    case DATA_SOURCE_UV:
      if (s_weather_uv == -1) return s_active_theme->text_primary;
      if (s_weather_uv >= 6) return s_active_theme->status_red;
      if (s_weather_uv >= 3) return s_active_theme->status_yellow;
      return s_active_theme->status_green;
    case DATA_SOURCE_HUMIDITY:
      // Comfort bands: below 30 is dry (blue, rhyming with cold temps), 30-60
      // is comfortable, and past 60 the air turns sticky, then oppressive.
      if (s_weather_humidity == -1) return s_active_theme->text_primary;
      if (s_weather_humidity < 30) return s_active_theme->accent_cold;
      if (s_weather_humidity <= 60) return s_active_theme->status_green;
      if (s_weather_humidity <= 70) return s_active_theme->status_yellow;
      return s_active_theme->status_red;
    case DATA_SOURCE_AQI_UV: {
      if (s_weather_aqi == -1 && s_weather_uv == -1) return s_active_theme->text_primary;
      bool is_red = (s_weather_aqi > 100 || s_weather_uv >= 6);
      bool is_yellow = (s_weather_aqi > 50 || s_weather_uv >= 3);
      if (is_red) return s_active_theme->status_red;
      if (is_yellow) return s_active_theme->status_yellow;
      return s_active_theme->status_green;
    }
    default:
      return s_active_theme->text_primary;
  }
}
