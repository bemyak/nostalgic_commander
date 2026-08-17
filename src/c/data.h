#pragma once
#include <pebble.h>
#include "layout.h"

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
  DATA_SOURCE_BEATS = 21,
  DATA_SOURCE_SHORT_DATE = 22,
  DATA_SOURCE_FULL_DATE = 23,
  DATA_SOURCE_STEPS_BAR = 24,
  DATA_SOURCE_BATTERY_BAR = 25,
  DATA_SOURCE_HUMIDITY = 26,
  DATA_SOURCE_WEATHER_FULL = 27,
  DATA_SOURCE_WEATHER_PCP = 28,
  DATA_SOURCE_TEMP_HIGH_LOW = 30,
  DATA_SOURCE_QUIET_TIME = 31,
  DATA_SOURCE_BT_QT = 32,
  DATA_SOURCE_WIND = 34,
  DATA_SOURCE_HUM_PCP = 35,
  DATA_SOURCE_WEEK_NUMBER = 33,
  // Retired ids: 19 (UTC_OFFSET), 29 (SUN_TIMES), 33 (ARROWS font test).
  DATA_SOURCE_EMPTY = 20
} ComplicationDataSource;

// Charge bands, as percentages. Above LOW the battery is healthy; at or below
// CRIT it is critical. Both the color logic and the decision to draw a status
// band read these, so a bar and a band can never disagree about one reading.
#define BATTERY_LOW_PCT 39
#define BATTERY_CRIT_PCT 19

// Goals are constants, not settings — the face has an opinion (AGENTS.md),
// and every progress readout shares these.
#define STEP_GOAL 10000
#define SLEEP_GOAL_S (8 * 3600)
#define ACTIVE_MINUTES_GOAL 30

// --- System state ---
extern int s_battery_level;
extern bool s_battery_charging;
extern bool s_connected;
extern bool s_quiet_time_active;

// --- Health readings (sentinels: steps/sleep/active minutes -1, heart rate 0) ---
extern int s_step_count;
extern int s_sleep_seconds;
extern int s_heart_rate;
extern int s_active_minutes;

// --- Weather readings; the wire contract is messaging.c's field table ---
extern int s_weather_temp;
extern int s_weather_cond_code;  // WMO weather code; -1 indicates no data
extern int s_weather_aqi;
extern int s_weather_uv;
extern int s_weather_humidity;
extern int s_weather_wind_direction;
extern int s_weather_wind_speed;
extern int s_weather_pcp;
extern int s_precip_now;

// While precipitating and metric, the PCP readout shows the observed rate
// instead of the forecast probability — the guess is settled.
bool weather_shows_precip_amount(void);
// The face's word for a WMO weather code ("--" when unmapped), and whether
// the code's family precipitates.
const char* weather_cond_word(int code);
bool weather_cond_precipitating(int code);
extern int s_temp_high;
extern int s_temp_low;
extern int s_temp_high_tmrw;  // tomorrow's daily max; -999 indicates no data
extern int s_temp_low_tmrw;   // tomorrow's daily min; -999 indicates no data
// Watch-local hours of each day's extremes, 0-23; -1 unknown.
extern int s_hi_hour_today;
extern int s_lo_hour_today;
extern int s_hi_hour_tmrw;
extern int s_lo_hour_tmrw;
// Watch-local wall hour 0-23, refreshed in refresh_state(); drives the rollover.
extern int s_wall_hour;
// Unit-aware temperature spelling (imperial prints the letter and signs
// negatives only; metric always signs and letters). The registry's temp
// formatters and format_strip_temp share it.
void format_temp(char* buf, size_t len, int temp, bool with_unit);

// The HI/LO pair as shown: each cell is the next occurrence of its kind,
// sooner event left. Colors and the frame stub swap read the same helpers
// below, so value, color, and captions never disagree.
void format_high_low(char* buf, size_t len);
int high_low_displayed_high(void);
bool high_low_hi_leads(void);
// Timeline Quick View: true while the system overlay covers the bottom slot
// row. Written only by the UnobstructedArea handler in main.c.
extern bool s_quick_view_active;

extern int s_date_day;
extern int s_beats;
extern int s_week_number;

// The date as last formatted by refresh_state(); drawn on the canvas so the
// weekday can carry its own color. The short form drops the year so it fits a
// top slot, and is the value behind DATA_SOURCE_SHORT_DATE.
extern char s_date_display[64];
extern char s_short_date_display[16];

// Units option values; Clay sends them as strings, messaging.c's
// tuple_get_int parses them to ints.
#define UNITS_IMPERIAL 0
#define UNITS_METRIC 1

extern int s_settings_theme;
extern int s_settings_units;
extern int s_settings_date_format;
extern int s_settings_short_date_format;
extern int s_settings_dow_position;
extern int s_settings_disconnect_vibe;
// Persisted like the other settings but read only phone-side: it sizes the
// reduction window for the UV/PCP maxima, which happens before the wire.
extern int s_settings_weather_window;

// Face geometry (margins, slot rects, TIME window, clock layer) lives in
// layout.h.

#define NUM_SLOTS 6
// Slot positions double as the persisted SLOT_* identities; never reorder.
#define SLOT_IDX_TOP_LEFT 0
#define SLOT_IDX_TOP_RIGHT 1
#define SLOT_IDX_BOTTOM_LEFT 2
#define SLOT_IDX_BOTTOM_CENTER 3
#define SLOT_IDX_BOTTOM_RIGHT 4
#define SLOT_IDX_CENTER 5
typedef struct {
  GRect box_rect;
  ComplicationDataSource source;
} ComplicationSlot;

extern ComplicationSlot s_complication_slots[NUM_SLOTS];

// One row per source: the registry lives in complication.c (see
// complication.h's ComplicationSpec).
// The single arrow for a wind blowing FROM `deg` (meteo bearing); the face
// points the way the wind goes. "--" on any negative bearing.
const char* wind_direction_arrow(int deg);
// The wind readout: "↗ 12 m/s" / "↗ 45 mph" wide; narrow windows pass
// with_unit=false and drop the unit. Either half may be absent; "--" when
// neither exists.
void format_wind(char* buf, size_t len, bool with_unit);
// Speed portion alone: "N", "N m/s", or "N mph"; "" when there is no
// reading. Owns the display clamp and the unit label for every consumer.
void format_wind_speed(char* buf, size_t len, bool with_unit);
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
int iso_week_number(int year, int yday, int wday);
void to_upper_str(char* str);
