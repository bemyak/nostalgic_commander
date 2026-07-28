#include <pebble.h>
#include "data.h"
#include "theme.h"

// Sensor & System Data Cache
int s_battery_level = 100;
int s_step_count = -1;  // -1 indicates no data
int s_step_goal = 10000;
int s_sleep_seconds = -1;   // -1 indicates no data
int s_heart_rate = 0;       // Default to 0 (displays "--" if no HRM is present)
int s_weather_temp = -999;  // -999 indicates no data
char s_weather_cond[16] = "--";
int s_weather_aqi = -1;  // -1 indicates no data
int s_weather_uv = -1;   // -1 indicates no data
int s_active_minutes = 0;
int s_active_minutes_goal = 30;
bool s_connected = true;
int s_date_day = 10;
int s_beats = 0;
int s_settings_theme = 0;        // 0 = Auto, 1 = Day, 2 = Night
int s_settings_units = 0;        // 0 = Imperial, 1 = Metric
int s_settings_date_format = 0;  // 0 = Weekday + ISO, 1 = ISO + Weekday, 2 = Full Text

ComplicationDataSource s_left_sidebar_source = DATA_SOURCE_STEPS;
ComplicationDataSource s_right_sidebar_source = DATA_SOURCE_BATTERY;

ComplicationSlot s_complication_slots[NUM_SLOTS] = {
    {.box_rect = {{10, 8}, {90, 36}}, .source = DATA_SOURCE_WEATHER},       // Top Left
    {.box_rect = {{100, 8}, {90, 36}}, .source = DATA_SOURCE_SLEEP},        // Top Right
    {.box_rect = {{10, 184}, {60, 36}}, .source = DATA_SOURCE_STEPS},       // Bottom Left
    {.box_rect = {{70, 184}, {60, 36}}, .source = DATA_SOURCE_HEART_RATE},  // Bottom Center
    {.box_rect = {{130, 184}, {60, 36}}, .source = DATA_SOURCE_BLUETOOTH}   // Bottom Right
};

const char* get_source_label(ComplicationDataSource source) {
  switch (source) {
    case DATA_SOURCE_BATTERY:
      return "BATT";
    case DATA_SOURCE_STEPS:
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
    case DATA_SOURCE_BEATS:
      return "BEAT";
    case DATA_SOURCE_EMPTY:
      return "";
    default:
      return "???";
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
        *percent = s_step_count > 0 ? (s_step_count * 100) / s_step_goal : 0;
        if (*percent > 100) *percent = 100;
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
      if (s_weather_temp == -999) {
        snprintf(val_buf, val_len, "--");
      } else {
        snprintf(val_buf, val_len, "%d%s", s_weather_temp, (s_settings_units == 1) ? "C" : "F");
      }
      break;
    case DATA_SOURCE_WEATHER_COND:
      snprintf(val_buf, val_len, "%s", s_weather_cond);
      break;
    case DATA_SOURCE_WEATHER: {
      char t_buf[16];
      char c_buf[16];
      if (s_weather_temp == -999) {
        snprintf(t_buf, sizeof(t_buf), "--");
      } else {
        snprintf(t_buf, sizeof(t_buf), "%d%s", s_weather_temp, (s_settings_units == 1) ? "C" : "F");
      }
      snprintf(c_buf, sizeof(c_buf), "%s", s_weather_cond);
      snprintf(val_buf, val_len, "%s / %s", c_buf, t_buf);
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
    case DATA_SOURCE_BLUETOOTH:
      snprintf(val_buf, val_len, s_connected ? "OK" : "LOSS");
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

void format_date_string(int format, struct tm* tick_time, char* buffer, int buf_size) {
  if (format == 0) {
    // TUE 2026-06-09
    strftime(buffer, buf_size, "%a %Y-%m-%d", tick_time);
    to_upper_str(buffer);
  } else if (format == 1) {
    // 2026-06-09 TUE
    strftime(buffer, buf_size, "%Y-%m-%d %a", tick_time);
    to_upper_str(buffer);
  } else if (format == 2) {
    // TUE JUNE 9th, 2026
    char weekday_buf[8];
    char month_buf[16];
    char year_buf[8];

    strftime(weekday_buf, sizeof(weekday_buf), "%a", tick_time);
    strftime(month_buf, sizeof(month_buf), "%B", tick_time);
    strftime(year_buf, sizeof(year_buf), "%Y", tick_time);

    to_upper_str(weekday_buf);
    to_upper_str(month_buf);

    char suffix[3];
    ordinal_suffix(tick_time->tm_mday, suffix);

    snprintf(buffer, buf_size, "%s %s %d%s, %s", weekday_buf, month_buf, tick_time->tm_mday, suffix,
             year_buf);
  }
}
