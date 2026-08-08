#include <pebble.h>
#include "status.h"
#include "data.h"
#include "theme.h"

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
