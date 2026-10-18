#pragma once
#include <pebble.h>

// Persistent storage keys. These are hand-assigned and deliberately
// independent of the auto-generated MESSAGE_KEY_* ids, so reordering the
// `messageKeys` array in package.json can never scramble saved data. Treat
// these numbers as a stable on-disk format: never reuse or renumber them.
#define PERSIST_KEY_WEATHER_TEMP 1000
// 1001 is retired (WEATHER_COND as a display string) — written to some
// watches already, never reused. The code form lives at
// PERSIST_KEY_WEATHER_COND_CODE.
#define PERSIST_KEY_WEATHER_AQI 1002
#define PERSIST_KEY_WEATHER_UV 1003
#define PERSIST_KEY_WEATHER_TIMESTAMP 1004
#define PERSIST_KEY_WEATHER_HUMIDITY 1005
#define PERSIST_KEY_WEATHER_PCP 1006
// 1007-1008 are retired (SUNRISE/SUNSET) — written to some watches already,
// never reused.
#define PERSIST_KEY_WEATHER_HIGH 1009
// 1010-1020 fall inside the settings block below; the weather cluster resumes
// after it.
#define PERSIST_KEY_WEATHER_LOW 1021
#define PERSIST_KEY_WEATHER_PRECIP_NOW 1022
#define PERSIST_KEY_WEATHER_LOW_TOMORROW 1023
#define PERSIST_KEY_WEATHER_HIGH_TOMORROW 1024
#define PERSIST_KEY_WEATHER_HI_HOUR_TODAY 1025
#define PERSIST_KEY_WEATHER_LO_HOUR_TODAY 1026
#define PERSIST_KEY_WEATHER_HI_HOUR_TOMORROW 1027
#define PERSIST_KEY_WEATHER_LO_HOUR_TOMORROW 1028
#define PERSIST_KEY_WEATHER_WIND_DIRECTION 1030
#define PERSIST_KEY_WEATHER_WIND_SPEED 1031
#define PERSIST_KEY_WEATHER_COND_CODE 1032

#define PERSIST_KEY_SETTINGS_THEME 1010
#define PERSIST_KEY_SETTINGS_UNITS 1011
#define PERSIST_KEY_SETTINGS_DATE_FORMAT 1012
#define PERSIST_KEY_SLOT_1 1013
#define PERSIST_KEY_SLOT_2 1014
#define PERSIST_KEY_SLOT_3 1015
#define PERSIST_KEY_SLOT_4 1016
#define PERSIST_KEY_SLOT_5 1017
#define PERSIST_KEY_SETTINGS_SHORT_DATE 1018
#define PERSIST_KEY_SLOT_6 1019
// 1029 is retired (SETTINGS_DISCONNECT_VIBE) — the OS owns phone-disconnect
// vibration now; written to some watches already, never reused.
#define PERSIST_KEY_SETTINGS_DOW 1020
#define PERSIST_KEY_SETTINGS_WEATHER_WINDOW 1033
#define PERSIST_KEY_SETTINGS_CRT 1034
#define PERSIST_KEY_SETTINGS_CRT_SOUND 1035
#define PERSIST_KEY_SETTINGS_HOURLY_CHIME 1036
#define PERSIST_KEY_SETTINGS_CHIME_VOLUME 1037
#define PERSIST_KEY_LONGITUDE 1038
#define PERSIST_KEY_LATITUDE 1039

#define WEATHER_CACHE_MAX_AGE_S (30 * 60)
// Weather older than this reads as no data.
#define WEATHER_STALE_AGE_S (2 * 60 * 60)
// How far ahead of the watch clock a payload's fetch time may claim to be.
#define WEATHER_STAMP_FUTURE_SLACK_S 60
// The weather fetch rides this minute edge of the hour clock.
#define WEATHER_FETCH_PERIOD_MIN 30
// A flapping link must not turn each reconnect into a request.
#define WEATHER_RECONNECT_MIN_GAP_S 60

#define APP_MESSAGE_INBOX_SIZE 256
#define APP_MESSAGE_OUTBOX_SIZE 64

typedef enum {
  WEATHER_AGE_FRESH,         // every reading usable
  WEATHER_AGE_PREVIOUS_DAY,  // fetched on an earlier local day: the day-bound
                             // HI/LO readings no longer describe today
  WEATHER_AGE_STALE,         // no reading usable (too old, or no/impossible stamp)
} WeatherAge;

// Persists the weather readings under s_weather_stamp.
void save_weather_cache(void);
bool load_weather_cache(void);
void load_settings(void);
void load_longitude(void);
void load_latitude(void);

// Pure decisions, kept apart from the state they steer.
WeatherAge weather_age(time_t stamp, time_t now);
// The time to stamp a payload with: the phone's fetch time when sane,
// otherwise `now`.
time_t weather_stamp_for(int32_t fetched_at, time_t now);
bool weather_fetch_due(int minute, bool needs_weather);
bool weather_fetch_after_settings(bool needs_weather, bool units_changed, bool window_changed,
                                  bool needed_weather, bool slots_changed);
// Whether a returning phone link should trigger a fetch: weather is shown
// and the held reading is older than the normal fetch cadence (or absent),
// unless the last reconnect-triggered request was under
// WEATHER_RECONNECT_MIN_GAP_S ago (`last_request` 0 = none yet).
bool weather_fetch_on_reconnect(bool needs_weather, time_t stamp, time_t now, time_t last_request);

// Applies weather_age() to the held readings.
void weather_expire(time_t now);
// Every weather reading back to its sentinel, and the cache marked unusable.
void weather_discard(void);

// A fresh request; the retry budget starts over.
void request_weather(void);
// A retry-timer send; spends from the budget main.c counts.
void request_weather_retry(void);
int tuple_get_int(Tuple* tuple);
void inbox_received_callback(DictionaryIterator* iterator, void* context);
void inbox_dropped_callback(AppMessageResult reason, void* context);
