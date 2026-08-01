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
  DATA_SOURCE_SHORT_DATE = 22,
  DATA_SOURCE_FULL_DATE = 23,
  DATA_SOURCE_STEPS_BAR = 24,
  DATA_SOURCE_BATTERY_BAR = 25,
  DATA_SOURCE_HUMIDITY = 26,
  DATA_SOURCE_WEATHER_FULL = 27,
  DATA_SOURCE_WEATHER_PCP = 28,
  DATA_SOURCE_TEMP_HIGH_LOW = 30,  // 29 is retired (SUN_TIMES), see 19
  DATA_SOURCE_EMPTY = 20
} ComplicationDataSource;

// Charge bands, as percentages. Above LOW the battery is healthy; at or below
// CRIT it is critical. Both the color logic and the decision to draw a status
// band read these, so a bar and a band can never disagree about one reading.
#define BATTERY_LOW_PCT 50
#define BATTERY_CRIT_PCT 20

extern int s_battery_level;
extern int s_step_count;
extern int s_step_goal;
extern int s_sleep_seconds;
extern int s_heart_rate;
extern int s_weather_temp;
extern char s_weather_cond[16];
extern int s_weather_aqi;
extern int s_weather_uv;
extern int s_weather_humidity;
extern int s_weather_pcp;
extern int s_precip_now;

// While precipitating and metric, the PCP readout shows the observed rate
// instead of the forecast probability — the guess is settled.
bool weather_shows_precip_amount(void);
extern int s_temp_high;
extern int s_temp_low;
extern int s_active_minutes;
extern int s_active_minutes_goal;
extern bool s_connected;
// Timeline Quick View: true while the system overlay covers the bottom slot
// row. Written only by the UnobstructedArea handler in main.c.
extern bool s_quick_view_active;

extern int s_date_day;
extern int s_beats;

// The date as last formatted by update_time(); drawn on the canvas so the
// weekday can carry its own color. The short form drops the year so it fits a
// top slot, and is the value behind DATA_SOURCE_SHORT_DATE.
extern char s_date_display[64];
extern char s_short_date_display[16];

extern int s_settings_theme;
extern int s_settings_units;
extern int s_settings_date_format;
extern int s_settings_short_date_format;
extern int s_settings_dow_position;

// Every frame spans these columns: an 8px margin on each edge, matching the
// vertical margins. 184 is 23 whole character cells, so the frame lines up with
// the font's 8px column grid. Lives here because the slot table below has to
// tile within it.
#define LAYOUT_X 8
#define LAYOUT_W 184

#define NUM_SLOTS 6
typedef struct {
  GRect box_rect;
  TextLayer* layer;
  ComplicationDataSource source;
} ComplicationSlot;

extern ComplicationSlot s_complication_slots[NUM_SLOTS];

void get_source_data(ComplicationDataSource source, char* val_buf, int val_len, int* percent);
const char* get_source_label(ComplicationDataSource source);
// One of the four DateFormat bodies with the weekday attached per
// `dow_position`. `short_format` only matters for DATE_FORMAT_SHORT.
void format_date_string(int format, int short_format, int dow_position, struct tm* tick_time,
                        char* buffer, int buf_size);

// Always the year-less form, whatever DATE_FORMAT is set to — this is what the
// short date complication renders.
void format_short_date_string(int short_format, int dow_position, struct tm* tick_time,
                              char* buffer, int buf_size);

// The centre strip's temperature chip spends its fixed cells differently per
// unit: metric always signs and drops the unit letter to fund the sign cell;
// imperial keeps the letter (below-zero F is rare, so its sign is no extra
// column in practice). Sentinel renders like the atomic source.
void format_strip_temp(char* buf, int buf_size);

// Weekdays are always the 3-letter abbreviation strftime's %a produces.
#define DOW_LEN 3

// SETTINGS_DATE_FORMAT — the date body, examples for Thursday 31 Dec 1970.
typedef enum {
  DATE_FORMAT_ISO = 0,    // 1970-12-31
  DATE_FORMAT_DOS = 1,    // 31-12-1970
  DATE_FORMAT_TEXT = 2,   // DECEMBER 31st, 1970
  DATE_FORMAT_SHORT = 3,  // the year-less short form
} DateFormat;

// SETTINGS_SHORT_DATE_FORMAT — the order of the year-less form.
typedef enum {
  SHORT_DATE_MONTH_DAY = 0,  // 12-31
  SHORT_DATE_DAY_MONTH = 1,  // 31-12
} ShortDateFormat;

// SETTINGS_DOW_POSITION — where the weekday goes, on every date the face draws.
typedef enum {
  DOW_BEFORE = 0,  // THU 1970-12-31
  DOW_AFTER = 1,   // 1970-12-31 THU
  DOW_HIDDEN = 2,  // 1970-12-31
} DowPosition;

// Where the weekday sits in a formatted date, or -1 when it is hidden — which
// is also the signal not to accent it.
int date_dow_offset(int dow_position, const char* formatted);
int compute_beats(time_t utc);
void to_upper_str(char* str);
int tuple_get_int(Tuple* tuple);
