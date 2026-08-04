#pragma once
#include <pebble.h>

// Persistent storage keys. These are hand-assigned and deliberately
// independent of the auto-generated MESSAGE_KEY_* ids, so reordering the
// `messageKeys` array in package.json can never scramble saved data. Treat
// these numbers as a stable on-disk format: never reuse or renumber them.
#define PERSIST_KEY_WEATHER_TEMP 1000
#define PERSIST_KEY_WEATHER_COND 1001
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
#define PERSIST_KEY_SETTINGS_DOW 1020
#define PERSIST_KEY_SETTINGS_DISCONNECT_VIBE 1029

#define WEATHER_CACHE_MAX_AGE_S (30 * 60)

void save_weather_cache(void);
bool load_weather_cache(void);
void load_settings(void);

void request_weather();
void inbox_received_callback(DictionaryIterator* iterator, void* context);
void inbox_dropped_callback(AppMessageResult reason, void* context);
