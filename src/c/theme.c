#include <pebble.h>
#include "theme.h"
#include "drawing.h"  // For s_main_window

const WatchTheme* s_active_theme = NULL;

const WatchTheme s_theme_night = {.center_bg = GColorBlack,
                                  .sidebar_bg = GColorBlack,
                                  .steps_fill = GColorChromeYellow,
                                  .frame = GColorWhite,
                                  .text_primary = GColorWhite,
                                  .text_secondary = GColorLightGray,
                                  .status_green = GColorMintGreen,
                                  .status_yellow = GColorPastelYellow,
                                  .status_red = GColorSunsetOrange};

const WatchTheme s_theme_day = {.center_bg = GColorWhite,
                                .sidebar_bg = GColorWhite,
                                .steps_fill = GColorBlue,
                                .frame = GColorBlack,
                                .text_primary = GColorBlack,
                                .text_secondary = GColorDarkGray,
                                .status_green = GColorGreen,
                                .status_yellow = GColorLimerick,
                                .status_red = GColorRed};

// The amber monochrome terminal: black ground, dim amber frames, bright amber
// text. Status colors stay chromatic so battery and AQI still read as status.
const WatchTheme s_theme_commander_night = {.center_bg = GColorBlack,             // #000000
                                            .sidebar_bg = GColorDarkGray,         // #555555
                                            .steps_fill = GColorChromeYellow,     // #FFAA00
                                            .frame = GColorWindsorTan,            // #AA5500
                                            .text_primary = GColorChromeYellow,   // #FFAA00
                                            .text_secondary = GColorRajah,        // #FFAA55
                                            .status_green = GColorScreaminGreen,  // #55FF55
                                            .status_yellow = GColorChromeYellow,  // #FFAA00
                                            .status_red = GColorSunsetOrange};    // #FF5555

// Norton Commander's panel: EGA blue ground, cyan frames, white entries.
const WatchTheme s_theme_commander_day = {.center_bg = GColorDukeBlue,       // #0000AA
                                          .sidebar_bg = GColorOxfordBlue,    // #000055
                                          .steps_fill = GColorElectricBlue,  // #55FFFF
                                          .frame = GColorTiffanyBlue,        // #00AAAA
                                          .text_primary = GColorWhite,       // #FFFFFF
                                          .text_secondary = GColorElectricBlue,
                                          .status_green = GColorScreaminGreen,  // #55FF55
                                          .status_yellow = GColorChromeYellow,  // #FFAA00
                                          .status_red = GColorSunsetOrange};    // #FF5555

// Auto mode: Day = 6 AM to 5:59 PM
static bool is_daytime(int current_hour) {
  return current_hour >= 6 && current_hour < 18;
}

const WatchTheme* determine_theme(int theme_setting, int current_hour) {
  switch (theme_setting) {
    case 1:
      return &s_theme_day;
    case 2:
      return &s_theme_night;
    case 3:
      return is_daytime(current_hour) ? &s_theme_commander_day : &s_theme_commander_night;
    case 4:
      return &s_theme_commander_day;
    case 5:
      return &s_theme_commander_night;
    default:  // 0 = Auto
      return is_daytime(current_hour) ? &s_theme_day : &s_theme_night;
  }
}

void apply_theme() {
  time_t temp = time(NULL);
  struct tm* tick_time = localtime(&temp);

  s_active_theme = determine_theme(s_settings_theme, tick_time->tm_hour);

  if (s_main_window) {
    window_set_background_color(s_main_window, s_active_theme->center_bg);
  }
}

GColor get_source_color(ComplicationDataSource source) {
  if (!s_active_theme) return GColorWhite;

  switch (source) {
    case DATA_SOURCE_BATTERY:
      if (s_battery_level > 50) return s_active_theme->status_green;
      if (s_battery_level > 20) return s_active_theme->status_yellow;
      return s_active_theme->status_red;
    case DATA_SOURCE_STEPS:
    case DATA_SOURCE_ACTIVE_MINUTES:
      return s_active_theme->text_primary;
    case DATA_SOURCE_HEART_RATE:
      return s_active_theme->status_red;
    case DATA_SOURCE_WEATHER_TEMP:
    case DATA_SOURCE_WEATHER:
      if (s_settings_units == 1) {  // Metric (Celsius)
        if (s_weather_temp > 29) return s_active_theme->status_red;
        if (s_weather_temp < 4) return s_active_theme->steps_fill;  // Blue
      } else {                                                      // Imperial (Fahrenheit)
        if (s_weather_temp > 85) return s_active_theme->status_red;
        if (s_weather_temp < 40) return s_active_theme->steps_fill;  // Blue
      }
      return s_active_theme->text_primary;
    case DATA_SOURCE_BLUETOOTH:
      return s_connected ? s_active_theme->status_green : s_active_theme->status_red;
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
