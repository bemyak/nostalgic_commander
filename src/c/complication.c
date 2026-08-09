#include <pebble.h>
#include "complication.h"
#include "drawing.h"  // drawer prototypes the table references

// The registry, rows ordered by enum value (retired ids are documented in
// data.h's enum). A NULL format means "read through the `backs` source":
// the two progress bars mirror their plain counterpart's reading so the
// render gate's per-slot snapshot hears their changes. A "" label marks a
// frame-stub window — its captions come from the FRAME_* renderer in
// drawing.c, not the title slot.
// Per-source value formatters, wired into the registry table below. Each
// receives buf/len pre-cleared and percent pre-zeroed by get_source_data.
// Shared helpers (temperature, wind, HI/LO, dates, the WMO table) live back
// in data.c with the state they read.

static void fmt_battery(char* buf, int len, int* percent) {
  snprintf(buf, len, "%d%%", s_battery_level);
  if (percent) *percent = s_battery_level;
}

static void fmt_steps(char* buf, int len, int* percent) {
  if (s_step_count == -1) {
    snprintf(buf, len, "--");
  } else if (s_step_count >= 10000) {
    int whole = s_step_count / 1000;
    int tenth = (s_step_count % 1000) / 100;
    snprintf(buf, len, "%d.%dk", whole, tenth);
  } else {
    snprintf(buf, len, "%d", s_step_count);
  }
  if (percent) {
    // True progress, deliberately not clamped to 100: beating the goal is
    // worth seeing. Consumers clamp for their own needs — a progress bar
    // can only fill to its end, but the reading beside it keeps counting.
    *percent = s_step_count > 0 ? (s_step_count * 100) / STEP_GOAL : 0;
  }
}

static void fmt_sleep(char* buf, int len, int* percent) {
  if (s_sleep_seconds == -1) {
    snprintf(buf, len, "--");
  } else {
    int hrs = s_sleep_seconds / 3600;
    int mins = (s_sleep_seconds % 3600) / 60;
    snprintf(buf, len, "%dh %dm", hrs, mins);
  }
  if (percent) {
    *percent = s_sleep_seconds > 0 ? (s_sleep_seconds * 100) / SLEEP_GOAL_S : 0;
    if (*percent > 100) *percent = 100;
  }
}

static void fmt_weather_temp(char* buf, int len, int* percent) {
  (void)percent;
  format_temp(buf, len, s_weather_temp, true);
}

static void fmt_weather_cond(char* buf, int len, int* percent) {
  (void)percent;
  snprintf(buf, len, "%s", weather_cond_word(s_weather_cond_code));
}

static void fmt_weather(char* buf, int len, int* percent) {
  (void)percent;
  // A single space, not " / ": the slash would push 4-char conditions
  // plus signed temps past the 11-cell top-slot budget.
  char t_buf[16];
  format_temp(t_buf, sizeof(t_buf), s_weather_temp, true);
  snprintf(buf, len, "%s %s", weather_cond_word(s_weather_cond_code), t_buf);
}

static void fmt_heart_rate(char* buf, int len, int* percent) {
  (void)percent;
  if (s_heart_rate > 0) {
    snprintf(buf, len, "%d", s_heart_rate);
  } else {
    snprintf(buf, len, "--");
  }
}

static void fmt_date(char* buf, int len, int* percent) {
  (void)percent;
  snprintf(buf, len, "%d", s_date_day);
}

static void fmt_short_date(char* buf, int len, int* percent) {
  (void)percent;
  snprintf(buf, len, "%s", s_short_date_display);
}

static void fmt_full_date(char* buf, int len, int* percent) {
  (void)percent;
  snprintf(buf, len, "%s", s_date_display);
}

static void fmt_bluetooth(char* buf, int len, int* percent) {
  // A Turbo Vision checkbox: ticked while the phone is there.
  snprintf(buf, len, "%s", s_connected ? "[x]" : "[ ]");
  if (percent) *percent = s_connected ? 100 : 0;
}

static void fmt_bt_qt(char* buf, int len, int* percent) {
  // Turbo Vision checkboxes: ticked while the state holds — `x` for the
  // phone connection (which alone moves the band, per the BT precedent),
  // `z` for Quiet Time.
  snprintf(buf, len, "[%s][%s]", s_connected ? "x" : " ", s_quiet_time_active ? "z" : " ");
  if (percent) *percent = s_connected ? 100 : 0;
}

static void fmt_quiet_time(char* buf, int len, int* percent) {
  snprintf(buf, len, "%s", s_quiet_time_active ? "[z]" : "[ ]");
  if (percent) *percent = s_quiet_time_active ? 100 : 0;
}

static void fmt_active_minutes(char* buf, int len, int* percent) {
  // No data reads like steps and sleep: "--", never a fake "0m".
  if (s_active_minutes == -1) {
    snprintf(buf, len, "--");
    if (percent) *percent = 0;
    return;
  }
  snprintf(buf, len, "%dm", s_active_minutes);
  if (percent) {
    *percent = (s_active_minutes * 100) / ACTIVE_MINUTES_GOAL;
    if (*percent > 100) *percent = 100;
  }
}

// A reading whose only states are "there" and "--"; AQI and UV share it.
static void fmt_sentinel_reading(char* buf, int len, int value) {
  if (value == -1) {
    snprintf(buf, len, "--");
  } else {
    snprintf(buf, len, "%d", value);
  }
}

static void fmt_aqi(char* buf, int len, int* percent) {
  (void)percent;
  fmt_sentinel_reading(buf, len, s_weather_aqi);
}

static void fmt_uv(char* buf, int len, int* percent) {
  (void)percent;
  fmt_sentinel_reading(buf, len, s_weather_uv);
}

static void fmt_aqi_uv(char* buf, int len, int* percent) {
  (void)percent;
  char aqi_str[8];
  char uv_str[8];
  fmt_sentinel_reading(aqi_str, sizeof(aqi_str), s_weather_aqi);
  fmt_sentinel_reading(uv_str, sizeof(uv_str), s_weather_uv);
  // Air joins the halves; the frame stubs carry the naming.
  snprintf(buf, len, "%s %s", aqi_str, uv_str);
}

static void fmt_humidity(char* buf, int len, int* percent) {
  if (s_weather_humidity == -1) {
    snprintf(buf, len, "--");
  } else {
    snprintf(buf, len, "%d%%", s_weather_humidity);
    // The reading already is a percentage; hand it through like battery
    // does. The sentinel path keeps the function's default of 0.
    if (percent) *percent = s_weather_humidity;
  }
}

static void fmt_wind(char* buf, int len, int* percent) {
  (void)percent;
  // Canonical (wide) form; narrow windows render format_wind(false)
  // from draw_wind_complication.
  format_wind(buf, len, true);
}

static void fmt_hum_pcp(char* buf, int len, int* percent) {
  (void)percent;
  char hum[8], pcp[8];
  get_source_data(DATA_SOURCE_HUMIDITY, hum, sizeof(hum), NULL);
  get_source_data(DATA_SOURCE_WEATHER_PCP, pcp, sizeof(pcp), NULL);
  snprintf(buf, len, "%s %s", hum, pcp);
}

static void fmt_weather_pcp(char* buf, int len, int* percent) {
  if (weather_shows_precip_amount()) {
    // Whole millimetres; trace drizzle reads "<1mm", a cloudburst clamps.
    // Four cells is always enough.
    if (s_precip_now < 10) {
      snprintf(buf, len, "<1mm");
    } else {
      int mm = s_precip_now / 10;
      snprintf(buf, len, "%dmm", mm > 99 ? 99 : mm);
    }
  } else if (s_weather_pcp == -1) {
    snprintf(buf, len, "--");
  } else {
    snprintf(buf, len, "%d%%", s_weather_pcp);
    if (percent) *percent = s_weather_pcp;
  }
}

static void fmt_temp_high_low(char* buf, int len, int* percent) {
  (void)percent;
  format_high_low(buf, len);
}

static void fmt_weather_full(char* buf, int len, int* percent) {
  (void)percent;
  // Canvas-drawn; this text is the render-gate snapshot only. Joining the
  // four chip texts means any weather change reaches the memcmp.
  char cond[8], temp[8], hum[8], pcp[8];
  get_source_data(DATA_SOURCE_WEATHER_COND, cond, sizeof(cond), NULL);
  format_strip_temp(temp, sizeof(temp));
  get_source_data(DATA_SOURCE_HUMIDITY, hum, sizeof(hum), NULL);
  get_source_data(DATA_SOURCE_WEATHER_PCP, pcp, sizeof(pcp), NULL);
  snprintf(buf, len, "%s %s %s %s", cond, temp, hum, pcp);
}

static void fmt_beats(char* buf, int len, int* percent) {
  (void)percent;
  snprintf(buf, len, "@%03d", s_beats);
}

static const ComplicationSpec s_complication_specs[] = {
    {.source = DATA_SOURCE_BATTERY,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "BATT",
     .format = fmt_battery,
     .backs = DATA_SOURCE_BATTERY,
     .draw = draw_battery_complication},
    {.source = DATA_SOURCE_STEPS,
     .health_metric = HealthMetricStepCount,
     .label = "STEP",
     .format = fmt_steps,
     .backs = DATA_SOURCE_STEPS,
     .draw = draw_shortkey_complication},
    {.source = DATA_SOURCE_SLEEP,
     .health_metric = HealthMetricSleepSeconds,
     .label = "SLEEP",
     .format = fmt_sleep,
     .backs = DATA_SOURCE_SLEEP,
     .draw = draw_shortkey_complication},
    {.source = DATA_SOURCE_WEATHER_TEMP,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "TEMP",
     .format = fmt_weather_temp,
     .backs = DATA_SOURCE_WEATHER_TEMP,
     .draw = draw_shortkey_complication,
     .needs_weather = true},
    {.source = DATA_SOURCE_WEATHER_COND,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "COND",
     .format = fmt_weather_cond,
     .backs = DATA_SOURCE_WEATHER_COND,
     .draw = draw_plain_complication,
     .needs_weather = true},
    {.source = DATA_SOURCE_WEATHER,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "WEATHER",
     .format = fmt_weather,
     .backs = DATA_SOURCE_WEATHER,
     .draw = draw_weather_complication,
     .needs_weather = true},
    {.source = DATA_SOURCE_HEART_RATE,
     .health_metric = HealthMetricHeartRateBPM,
     .label = "BPM",
     .format = fmt_heart_rate,
     .backs = DATA_SOURCE_HEART_RATE,
     .draw = draw_heart_rate_complication},
    {.source = DATA_SOURCE_DATE,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "DATE",
     .format = fmt_date,
     .backs = DATA_SOURCE_DATE,
     .draw = draw_plain_complication},
    {.source = DATA_SOURCE_BLUETOOTH,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "BT",
     .format = fmt_bluetooth,
     .backs = DATA_SOURCE_BLUETOOTH,
     .draw = draw_plain_complication},
    {.source = DATA_SOURCE_ACTIVE_MINUTES,
     .health_metric = HealthMetricActiveSeconds,
     .label = "ACTV",
     .format = fmt_active_minutes,
     .backs = DATA_SOURCE_ACTIVE_MINUTES,
     .draw = draw_shortkey_complication},
    {.source = DATA_SOURCE_AQI,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "AQI",
     .format = fmt_aqi,
     .backs = DATA_SOURCE_AQI,
     .draw = draw_banded_complication,
     .needs_weather = true},
    {.source = DATA_SOURCE_UV,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "UV",
     .format = fmt_uv,
     .backs = DATA_SOURCE_UV,
     .draw = draw_banded_complication,
     .needs_weather = true},
    {.source = DATA_SOURCE_AQI_UV,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "",
     .format = fmt_aqi_uv,
     .backs = DATA_SOURCE_AQI_UV,
     .draw = draw_aqi_uv_complication,
     .frame = FRAME_AQI_UV,
     .needs_weather = true},
    {.source = DATA_SOURCE_EMPTY,
     .label = "",
     .backs = DATA_SOURCE_EMPTY,
     .health_metric = HEALTH_METRIC_NONE},
    {.source = DATA_SOURCE_BEATS,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "BEAT",
     .format = fmt_beats,
     .backs = DATA_SOURCE_BEATS,
     .draw = draw_beats_complication},
    {.source = DATA_SOURCE_SHORT_DATE,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "DATE",
     .format = fmt_short_date,
     .backs = DATA_SOURCE_SHORT_DATE,
     .draw = draw_short_date_complication},
    {.source = DATA_SOURCE_FULL_DATE,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "DATE",
     .format = fmt_full_date,
     .backs = DATA_SOURCE_FULL_DATE,
     .draw = draw_full_date_complication},
    {.source = DATA_SOURCE_STEPS_BAR,
     .health_metric = HealthMetricStepCount,
     .label = "STEP",
     .backs = DATA_SOURCE_STEPS,
     .draw = draw_steps_bar_complication},
    {.source = DATA_SOURCE_BATTERY_BAR,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "BATT",
     .backs = DATA_SOURCE_BATTERY,
     .draw = draw_battery_bar_complication},
    {.source = DATA_SOURCE_HUMIDITY,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "HUM",
     .format = fmt_humidity,
     .backs = DATA_SOURCE_HUMIDITY,
     .draw = draw_shortkey_complication,
     .needs_weather = true},
    // Caption tokens live in drawing.c's field table, centred per chip.
    {.source = DATA_SOURCE_WEATHER_FULL,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "",
     .format = fmt_weather_full,
     .backs = DATA_SOURCE_WEATHER_FULL,
     .draw = draw_weather_full_complication,
     .frame = FRAME_FULL_WEATHER,
     .needs_weather = true},
    {.source = DATA_SOURCE_WEATHER_PCP,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "PCP",
     .format = fmt_weather_pcp,
     .backs = DATA_SOURCE_WEATHER_PCP,
     .draw = draw_banded_complication,
     .needs_weather = true},
    {.source = DATA_SOURCE_TEMP_HIGH_LOW,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "",
     .format = fmt_temp_high_low,
     .backs = DATA_SOURCE_TEMP_HIGH_LOW,
     .draw = draw_shortkey_complication,
     .frame = FRAME_HI_LO,
     .needs_weather = true},
    {.source = DATA_SOURCE_QUIET_TIME,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "QT",
     .format = fmt_quiet_time,
     .backs = DATA_SOURCE_QUIET_TIME,
     .draw = draw_plain_complication},
    // One window covers both phone states.
    {.source = DATA_SOURCE_BT_QT,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "BT/QT",
     .format = fmt_bt_qt,
     .backs = DATA_SOURCE_BT_QT,
     .draw = draw_bt_qt_complication,
     .frame = FRAME_BT_QT},
    {.source = DATA_SOURCE_WIND,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "WIND",
     .format = fmt_wind,
     .backs = DATA_SOURCE_WIND,
     .draw = draw_wind_complication,
     .needs_weather = true},
    {.source = DATA_SOURCE_HUM_PCP,
     .health_metric = HEALTH_METRIC_NONE,
     .label = "",
     .format = fmt_hum_pcp,
     .backs = DATA_SOURCE_HUM_PCP,
     .draw = draw_hum_pcp_complication,
     .frame = FRAME_HUM_PCP,
     .needs_weather = true},
};

const ComplicationSpec* complication_spec(ComplicationDataSource source) {
  for (size_t i = 0; i < sizeof(s_complication_specs) / sizeof(s_complication_specs[0]); i++) {
    if (s_complication_specs[i].source == source) return &s_complication_specs[i];
  }
  return NULL;
}

const char* get_source_label(ComplicationDataSource source) {
  const ComplicationSpec* spec = complication_spec(source);
  return spec ? spec->label : "???";
}

void get_source_data(ComplicationDataSource source, char* val_buf, int val_len, int* percent) {
  if (percent) *percent = 0;
  val_buf[0] = '\0';

  const ComplicationSpec* spec = complication_spec(source);
  if (!spec) return;
  ComplicationFormatFn format = spec->format;
  if (!format) {
    // An unresolvable `backs` reads as no data.
    const ComplicationSpec* backing = complication_spec(spec->backs);
    if (backing) format = backing->format;
  }
  if (format) format(val_buf, val_len, percent);
}

bool any_slot_needs_weather(void) {
  for (int i = 0; i < NUM_SLOTS; i++) {
    const ComplicationSpec* spec = complication_spec(s_complication_slots[i].source);
    if (spec && spec->needs_weather) return true;
  }
  return false;
}

bool any_slot_monitors_health(int metric) {
  for (int i = 0; i < NUM_SLOTS; i++) {
    const ComplicationSpec* spec = complication_spec(s_complication_slots[i].source);
    if (spec && spec->health_metric == metric) return true;
  }
  return false;
}
