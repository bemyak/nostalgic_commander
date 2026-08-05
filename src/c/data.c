#include <pebble.h>
#include "data.h"
#include "theme.h"

// Sensor & System Data Cache
int s_battery_level = 100;
// Charging speaks for itself in green; the level ladder is for draining.
bool s_battery_charging = false;
int s_step_count = -1;  // -1 indicates no data
int s_step_goal = 10000;
int s_sleep_seconds = -1;   // -1 indicates no data
int s_heart_rate = 0;       // Default to 0 (displays "--" if no HRM is present)
int s_weather_temp = -999;  // -999 indicates no data
char s_weather_cond[16] = "--";
int s_weather_aqi = -1;       // -1 indicates no data
int s_weather_uv = -1;        // -1 indicates no data
int s_weather_humidity = -1;  // -1 indicates no data
int s_weather_pcp = -1;       // -1 indicates no data
int s_precip_now = -1;        // tenths of mm over the past hour; -1 indicates no data
int s_temp_high = -999;       // -999 indicates no data
int s_temp_low = -999;        // -999 indicates no data
int s_temp_high_tmrw = -999;  // -999 indicates no data
int s_temp_low_tmrw = -999;   // -999 indicates no data
int s_hi_hour_today = -1;     // event hours 0-23; -1 unknown
int s_lo_hour_today = -1;
int s_hi_hour_tmrw = -1;
int s_lo_hour_tmrw = -1;
int s_wall_hour = 0;  // refreshed in update_time(); drives the rollover
int s_active_minutes = 0;
int s_active_minutes_goal = 30;
bool s_connected = true;
bool s_quick_view_active = false;
int s_date_day = 10;
int s_beats = 0;
char s_date_display[64] = "";
char s_short_date_display[16] = "";
int s_settings_theme = 2;        // 0 = Auto, 1 = Dialog, 2 = Panel, 3 = Shadow; default is Panel
int s_settings_units = 0;        // 0 = Imperial, 1 = Metric
int s_settings_date_format = 0;  // DateFormat: 0 = ISO, 1 = DOS, 2 = Text, 3 = Short
int s_settings_short_date_format = 0;  // 0 = Month-Day, 1 = Day-Month
int s_settings_dow_position = 0;       // 0 = Before, 1 = After, 2 = Hidden
int s_settings_disconnect_vibe = 1;    // 1 = buzz on phone disconnect (default), 0 = silenced

// Each row tiles LAYOUT_X..LAYOUT_X+LAYOUT_W-1, with neighbours overlapping by
// 2 columns — the frame border width — so their borders coincide into a single
// shared divider rather than stacking into a double-width one.
ComplicationSlot s_complication_slots[NUM_SLOTS] = {
    {.box_rect = {{LAYOUT_X, 8}, {93, 36}}, .source = DATA_SOURCE_WEATHER},  // Top Left
    {.box_rect = {{99, 8}, {93, 36}}, .source = DATA_SOURCE_SLEEP},          // Top Right
    {.box_rect = {{LAYOUT_X, 184}, {63, 36}}, .source = DATA_SOURCE_STEPS},  // Bottom Left
    {.box_rect = {{69, 184}, {62, 36}}, .source = DATA_SOURCE_HEART_RATE},   // Bottom Center
    {.box_rect = {{129, 184}, {63, 36}}, .source = DATA_SOURCE_BLUETOOTH},   // Bottom Right
    // The wide centre row. Indexed last so SLOT_1..5 keep their persisted
    // positions; its own setting is SLOT_6.
    {.box_rect = {{LAYOUT_X, 142}, {LAYOUT_W, 36}}, .source = DATA_SOURCE_FULL_DATE}};

const char* get_source_label(ComplicationDataSource source) {
  switch (source) {
    case DATA_SOURCE_BATTERY:
    case DATA_SOURCE_BATTERY_BAR:
      return "BATT";
    case DATA_SOURCE_STEPS:
    case DATA_SOURCE_STEPS_BAR:
      return "STEP";
    case DATA_SOURCE_SLEEP:
      return "SLEEP";
    case DATA_SOURCE_WEATHER_TEMP:
      return "TEMP";
    case DATA_SOURCE_WEATHER_COND:
      return "COND";
    case DATA_SOURCE_WEATHER:
      return "WEATHER";
    case DATA_SOURCE_HEART_RATE:
      return "BPM";
    case DATA_SOURCE_DATE:
    case DATA_SOURCE_SHORT_DATE:
    case DATA_SOURCE_FULL_DATE:
      return "DATE";
    case DATA_SOURCE_BLUETOOTH:
      return "BT";
    case DATA_SOURCE_ACTIVE_MINUTES:
      return "ACTV";
    case DATA_SOURCE_AQI:
      return "AQI";
    case DATA_SOURCE_UV:
      return "UV";
    case DATA_SOURCE_AQI_UV:
      return "AQI/UV";
    case DATA_SOURCE_HUMIDITY:
      return "HUM";
    case DATA_SOURCE_WEATHER_PCP:
      return "PCP";
    case DATA_SOURCE_TEMP_HIGH_LOW:
      // The cells swap sides as extremes roll; the caption must name the
      // current round, or "+20/+16" reads as "min 20, max 16".
      return high_low_hi_leads() ? "HI/LO" : "LO/HI";
    case DATA_SOURCE_WEATHER_FULL:
      // Caption tokens live in drawing.c's field table, centred per chip.
      return "";
    case DATA_SOURCE_BEATS:
      return "BEAT";
    case DATA_SOURCE_EMPTY:
      return "";
    default:
      return "???";
  }
}

// The face's temperature spelling, unit-aware by policy: imperial prints the
// unit letter and signs negatives only, metric always signs (Celsius crosses
// zero as a matter of course). with_unit=false is the centre strip's metric
// trade: the sign cell plus the letter would not fit the fixed chips.
static void format_temp(char* buf, size_t len, int temp, bool with_unit) {
  if (temp == -999) {
    snprintf(buf, len, "--");
  } else if (s_settings_units == 1) {
    snprintf(buf, len, with_unit ? "%+dC" : "%+d", temp);
  } else {
    snprintf(buf, len, with_unit ? "%dF" : "%d", temp);
  }
}

bool weather_shows_precip_amount(void) {
  if (s_settings_units != 1 || s_precip_now < 0) return false;
  return strcmp(s_weather_cond, "RAIN") == 0 || strcmp(s_weather_cond, "SNOW") == 0 ||
         strcmp(s_weather_cond, "TSTM") == 0;
}

void format_strip_temp(char* buf, int buf_size) {
  format_temp(buf, buf_size, s_weather_temp, s_settings_units != 1);
}

// An extreme "has passed" at the top of its own event hour — from then on
// the cell shows tomorrow's value, keeping the readout strictly about the
// next occurrence. Unknown hours (-1) count as not passed.
static bool extreme_passed(int event_hour) {
  return event_hour >= 0 && s_wall_hour >= event_hour;
}

// Which cell leads, hours only: the sooner event goes left. A tie, or
// unknown hours (nothing to sort by), keeps LO first — the usual shape of a
// day. Shared by the formatter and its label so they can never disagree.
bool high_low_hi_leads(void) {
  if (s_lo_hour_today < 0 || s_hi_hour_today < 0 || s_lo_hour_tmrw < 0 || s_hi_hour_tmrw < 0) {
    return false;
  }
  int lo_key = extreme_passed(s_lo_hour_today) ? 24 + s_lo_hour_tmrw : s_lo_hour_today;
  int hi_key = extreme_passed(s_hi_hour_today) ? 24 + s_hi_hour_tmrw : s_hi_hour_today;
  return hi_key < lo_key;
}

int high_low_displayed_high(void) {
  return extreme_passed(s_hi_hour_today) ? s_temp_high_tmrw : s_temp_high;
}

static void format_high_low(char* buf, size_t len) {
  // Either pair incomplete sinks the readout: a half-number reads as data.
  if (s_temp_high == -999 || s_temp_low == -999 || s_temp_high_tmrw == -999 ||
      s_temp_low_tmrw == -999) {
    snprintf(buf, len, "-- / --");
    return;
  }
  // Each cell shows the next occurrence of its kind: today's value until
  // the extreme's own hour begins, then tomorrow's.
  int lo_val = extreme_passed(s_lo_hour_today) ? s_temp_low_tmrw : s_temp_low;
  int hi_val = extreme_passed(s_hi_hour_today) ? s_temp_high_tmrw : s_temp_high;
  // Chronological left to right — the sooner event leads.
  bool lo_left = !high_low_hi_leads();
  int left = lo_left ? lo_val : hi_val;
  int right = lo_left ? hi_val : lo_val;
  if (s_settings_units == 1) {
    snprintf(buf, len, "%+d/%+dC", left, right);
  } else {
    snprintf(buf, len, "%d/%dF", left, right);
  }
}

void get_source_data(ComplicationDataSource source, char* val_buf, int val_len, int* percent) {
  if (percent) *percent = 0;
  val_buf[0] = '\0';

  switch (source) {
    case DATA_SOURCE_BATTERY:
      snprintf(val_buf, val_len, "%d%%", s_battery_level);
      if (percent) *percent = s_battery_level;
      break;
    case DATA_SOURCE_STEPS:
      if (s_step_count == -1) {
        snprintf(val_buf, val_len, "--");
      } else if (s_step_count >= 10000) {
        int whole = s_step_count / 1000;
        int tenth = (s_step_count % 1000) / 100;
        snprintf(val_buf, val_len, "%d.%dk", whole, tenth);
      } else {
        snprintf(val_buf, val_len, "%d", s_step_count);
      }
      if (percent) {
        // True progress, deliberately not clamped to 100: beating the goal is
        // worth seeing. Consumers clamp for their own needs — a progress bar
        // can only fill to its end, but the reading beside it keeps counting.
        *percent = s_step_count > 0 ? (s_step_count * 100) / s_step_goal : 0;
      }
      break;
    case DATA_SOURCE_SLEEP: {
      if (s_sleep_seconds == -1) {
        snprintf(val_buf, val_len, "--");
      } else {
        int hrs = s_sleep_seconds / 3600;
        int mins = (s_sleep_seconds % 3600) / 60;
        snprintf(val_buf, val_len, "%dh %dm", hrs, mins);
      }
      if (percent) {
        *percent = s_sleep_seconds > 0 ? (s_sleep_seconds * 100) / 28800 : 0;  // 8-hour goal
        if (*percent > 100) *percent = 100;
      }
      break;
    }
    case DATA_SOURCE_WEATHER_TEMP:
      format_temp(val_buf, val_len, s_weather_temp, true);
      break;
    case DATA_SOURCE_WEATHER_COND:
      snprintf(val_buf, val_len, "%s", s_weather_cond);
      break;
    case DATA_SOURCE_WEATHER: {
      // A single space, not " / ": the slash would push 4-char conditions
      // plus signed temps past the 11-cell top-slot budget.
      char t_buf[16];
      format_temp(t_buf, sizeof(t_buf), s_weather_temp, true);
      snprintf(val_buf, val_len, "%s %s", s_weather_cond, t_buf);
      break;
    }
    case DATA_SOURCE_HEART_RATE:
      if (s_heart_rate > 0) {
        snprintf(val_buf, val_len, "%d", s_heart_rate);
      } else {
        snprintf(val_buf, val_len, "--");
      }
      break;
    case DATA_SOURCE_DATE:
      snprintf(val_buf, val_len, "%d", s_date_day);
      break;
    case DATA_SOURCE_SHORT_DATE:
      snprintf(val_buf, val_len, "%s", s_short_date_display);
      break;
    case DATA_SOURCE_FULL_DATE:
      snprintf(val_buf, val_len, "%s", s_date_display);
      break;
    case DATA_SOURCE_BLUETOOTH:
      // A Turbo Vision checkbox: ticked while the phone is there.
      snprintf(val_buf, val_len, "%s", s_connected ? "[x]" : "[ ]");
      if (percent) *percent = s_connected ? 100 : 0;
      break;
    case DATA_SOURCE_ACTIVE_MINUTES:
      snprintf(val_buf, val_len, "%dm", s_active_minutes);
      if (percent) {
        *percent = (s_active_minutes * 100) / s_active_minutes_goal;
        if (*percent > 100) *percent = 100;
      }
      break;
    case DATA_SOURCE_AQI:
      if (s_weather_aqi == -1) {
        snprintf(val_buf, val_len, "--");
      } else {
        snprintf(val_buf, val_len, "%d", s_weather_aqi);
      }
      break;
    case DATA_SOURCE_UV:
      if (s_weather_uv == -1) {
        snprintf(val_buf, val_len, "--");
      } else {
        snprintf(val_buf, val_len, "%d", s_weather_uv);
      }
      break;
    case DATA_SOURCE_AQI_UV: {
      char aqi_str[8];
      char uv_str[8];
      if (s_weather_aqi == -1) {
        snprintf(aqi_str, sizeof(aqi_str), "--");
      } else {
        snprintf(aqi_str, sizeof(aqi_str), "%d", s_weather_aqi);
      }
      if (s_weather_uv == -1) {
        snprintf(uv_str, sizeof(uv_str), "--");
      } else {
        snprintf(uv_str, sizeof(uv_str), "%d", s_weather_uv);
      }
      snprintf(val_buf, val_len, "%s / %s", aqi_str, uv_str);
      break;
    }
    case DATA_SOURCE_HUMIDITY:
      if (s_weather_humidity == -1) {
        snprintf(val_buf, val_len, "--");
      } else {
        snprintf(val_buf, val_len, "%d%%", s_weather_humidity);
        // The reading already is a percentage; hand it through like battery
        // does. The sentinel path keeps the function's default of 0.
        if (percent) *percent = s_weather_humidity;
      }
      break;
    case DATA_SOURCE_WEATHER_PCP:
      if (weather_shows_precip_amount()) {
        // Whole millimetres; trace drizzle reads "<1mm", a cloudburst clamps.
        // Four cells is always enough.
        if (s_precip_now < 10) {
          snprintf(val_buf, val_len, "<1mm");
        } else {
          int mm = s_precip_now / 10;
          snprintf(val_buf, val_len, "%dmm", mm > 99 ? 99 : mm);
        }
      } else if (s_weather_pcp == -1) {
        snprintf(val_buf, val_len, "--");
      } else {
        snprintf(val_buf, val_len, "%d%%", s_weather_pcp);
        if (percent) *percent = s_weather_pcp;
      }
      break;
    case DATA_SOURCE_TEMP_HIGH_LOW:
      format_high_low(val_buf, val_len);
      break;
    case DATA_SOURCE_WEATHER_FULL: {
      // Canvas-drawn; this text is the render-gate snapshot only. Joining the
      // four chip texts means any weather change reaches the memcmp.
      char cond[8], temp[8], hum[8], pcp[8];
      get_source_data(DATA_SOURCE_WEATHER_COND, cond, sizeof(cond), NULL);
      format_strip_temp(temp, sizeof(temp));
      get_source_data(DATA_SOURCE_HUMIDITY, hum, sizeof(hum), NULL);
      get_source_data(DATA_SOURCE_WEATHER_PCP, pcp, sizeof(pcp), NULL);
      snprintf(val_buf, val_len, "%s %s %s %s", cond, temp, hum, pcp);
      break;
    }
    case DATA_SOURCE_BEATS:
      snprintf(val_buf, val_len, "@%03d", s_beats);
      break;
    default:
      break;
  }
}

// Swatch Internet Time: the BMT (UTC+1, no DST) day split into 1000 beats of
// 86.4s. Ticks are per-minute, so the value is exact at each tick and lags by
// up to one beat before the next — a second-resolution tick isn't worth the
// battery.
int compute_beats(time_t utc) {
  int bmt_seconds = (int)((utc + 3600) % 86400);
  return (bmt_seconds * 1000) / 86400;
}

// Helper Functions
void to_upper_str(char* str) {
  for (int i = 0; str[i]; i++) {
    if (str[i] >= 'a' && str[i] <= 'z') {
      str[i] -= 32;
    }
  }
}

int tuple_get_int(Tuple* tuple) {
  if (!tuple) return 0;
  switch (tuple->type) {
    case TUPLE_INT:
    case TUPLE_UINT:
      if (tuple->length == 1)
        return tuple->value->uint8;
      else if (tuple->length == 2)
        return tuple->value->uint16;
      else if (tuple->length == 4)
        return tuple->value->uint32;
      return 0;
    case TUPLE_CSTRING:
      return atoi(tuple->value->cstring);
    default:
      return 0;
  }
}

static void ordinal_suffix(int day, char* buf) {
  if (day >= 11 && day <= 13) {
    strcpy(buf, "th");
    return;
  }
  switch (day % 10) {
    case 1:
      strcpy(buf, "st");
      break;
    case 2:
      strcpy(buf, "nd");
      break;
    case 3:
      strcpy(buf, "rd");
      break;
    default:
      strcpy(buf, "th");
      break;
  }
}

// The date itself, without the weekday.
static void format_date_body(int format, int short_format, struct tm* tick_time, char* buffer,
                             int buf_size) {
  switch (format) {
    case DATE_FORMAT_DOS:
      strftime(buffer, buf_size, "%d-%m-%Y", tick_time);
      break;
    case DATE_FORMAT_TEXT: {
      char month_buf[16];
      char year_buf[8];
      char suffix[3];

      strftime(month_buf, sizeof(month_buf), "%b", tick_time);
      strftime(year_buf, sizeof(year_buf), "%Y", tick_time);
      to_upper_str(month_buf);
      ordinal_suffix(tick_time->tm_mday, suffix);

      snprintf(buffer, buf_size, "%s %d%s, %s", month_buf, tick_time->tm_mday, suffix, year_buf);
      break;
    }
    case DATE_FORMAT_SHORT:
      strftime(buffer, buf_size, short_format == SHORT_DATE_DAY_MONTH ? "%d-%m" : "%m-%d",
               tick_time);
      break;
    default:
      strftime(buffer, buf_size, "%Y-%m-%d", tick_time);
      break;
  }
}

// Attaches the weekday where the Day of week setting wants it. Every date the
// face draws goes through here, so the position never depends on the format.
static void format_with_weekday(int dow_position, struct tm* tick_time, const char* body,
                                char* buffer, int buf_size) {
  if (dow_position == DOW_HIDDEN) {
    snprintf(buffer, buf_size, "%s", body);
    return;
  }

  char weekday_buf[8];
  strftime(weekday_buf, sizeof(weekday_buf), "%a", tick_time);
  to_upper_str(weekday_buf);

  if (dow_position == DOW_AFTER) {
    snprintf(buffer, buf_size, "%s %s", body, weekday_buf);
  } else {
    snprintf(buffer, buf_size, "%s %s", weekday_buf, body);
  }
}

void format_date_string(int format, int short_format, int dow_position, struct tm* tick_time,
                        char* buffer, int buf_size) {
  char body[32];
  format_date_body(format, short_format, tick_time, body, sizeof(body));
  format_with_weekday(dow_position, tick_time, body, buffer, buf_size);
}

void format_short_date_string(int short_format, int dow_position, struct tm* tick_time,
                              char* buffer, int buf_size) {
  format_date_string(DATE_FORMAT_SHORT, short_format, dow_position, tick_time, buffer, buf_size);
}

// Kept beside the formatters so the two can't drift apart.
int date_dow_offset(int dow_position, const char* formatted) {
  if (dow_position == DOW_HIDDEN) return -1;
  if (dow_position == DOW_AFTER) {
    int len = strlen(formatted);
    return len >= DOW_LEN ? len - DOW_LEN : -1;
  }
  return 0;
}
