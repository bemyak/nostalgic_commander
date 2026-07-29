#pragma once
#include <pebble.h>

typedef enum {
  DATA_SOURCE_BATTERY = 0,
  DATA_SOURCE_STEPS = 1,
  DATA_SOURCE_SLEEP = 2,
  DATA_SOURCE_WEATHER_TEMP = 3,
  DATA_SOURCE_WEATHER_COND = 4,
  DATA_SOURCE_WEATHER = 5,
  DATA_SOURCE_HEART_RATE = 6,
  DATA_SOURCE_DATE = 7,
  DATA_SOURCE_BLUETOOTH = 9,
  DATA_SOURCE_ACTIVE_MINUTES = 10,
  DATA_SOURCE_AQI = 16,
  DATA_SOURCE_UV = 17,
  DATA_SOURCE_AQI_UV = 18,
  DATA_SOURCE_BEATS = 21,  // 19 is retired (UTC_OFFSET), 20 is EMPTY
  DATA_SOURCE_EMPTY = 20
} ComplicationDataSource;

extern int s_battery_level;
extern int s_step_count;
extern int s_step_goal;
extern int s_sleep_seconds;
extern int s_heart_rate;
extern int s_weather_temp;
extern char s_weather_cond[16];
extern int s_weather_aqi;
extern int s_weather_uv;
extern int s_active_minutes;
extern int s_active_minutes_goal;
extern bool s_connected;

extern int s_date_day;
extern int s_beats;

// The date as last formatted by update_time(); drawn on the canvas so the
// weekday can carry its own color.
extern char s_date_display[64];

extern int s_settings_theme;
extern int s_settings_units;
extern int s_settings_date_format;

// Every frame spans these columns: an 8px margin on each edge, matching the
// vertical margins. 184 is 23 whole character cells, so the frame lines up with
// the font's 8px column grid. Lives here because the slot table below has to
// tile within it.
#define LAYOUT_X 8
#define LAYOUT_W 184

#define NUM_SLOTS 5
typedef struct {
  GRect box_rect;
  TextLayer* layer;
  ComplicationDataSource source;
} ComplicationSlot;

extern ComplicationSlot s_complication_slots[NUM_SLOTS];

void get_source_data(ComplicationDataSource source, char* val_buf, int val_len, int* percent);
const char* get_source_label(ComplicationDataSource source);
void format_date_string(int format, struct tm* tick_time, char* buffer, int buf_size);

// Weekdays are always the 3-letter abbreviation strftime's %a produces.
#define DOW_LEN 3
int date_dow_offset(int format, const char* formatted);
int compute_beats(time_t utc);
void to_upper_str(char* str);
int tuple_get_int(Tuple* tuple);
