#include <pebble.h>
#include "messaging.h"
#include "data.h"
#include "main.h"
#include "drawing.h"

static void persist_write_int_if_changed(uint32_t key, int32_t value) {
  if (!persist_exists(key) || persist_read_int(key) != value) {
    persist_write_int(key, value);
  }
}

static void persist_write_string_if_changed(uint32_t key, const char* value) {
  if (!persist_exists(key)) {
    persist_write_string(key, value);
    return;
  }
  char current[16];
  persist_read_string(key, current, sizeof(current));
  if (strcmp(current, value) != 0) {
    persist_write_string(key, value);
  }
}

// One message-received reading: its MESSAGE_KEY_*, the PERSIST_KEY_* it
// persists under, and the data.c global it lands in.
typedef struct {
  uint32_t message_key;
  uint32_t persist_key;
  int* target;
} MessageField;

// The real SDK build bakes MESSAGE_KEY_* as extern variables, not
// compile-time constants, so the tables are filled once at first use instead
// of in their initializers. Three walks share the weather table — inbox
// parse, cache save, cache load — and two share the settings table (inbox,
// load_settings), which is what keeps those halves from drifting apart.
static MessageField s_weather_fields[16];
static MessageField s_settings_fields[6];
// The slot persist keys are deliberately not sequential (SLOT_6 landed after
// the settings block), so the pairing is tabulated, not computed.
static MessageField s_slot_keys[NUM_SLOTS];  // target unused; index is the slot
static bool s_tables_initialized;

static void init_message_tables(void) {
  if (s_tables_initialized) return;
  s_tables_initialized = true;
  unsigned i = 0;
  s_weather_fields[i++] =
      (MessageField){MESSAGE_KEY_WEATHER_TEMP, PERSIST_KEY_WEATHER_TEMP, &s_weather_temp};
  s_weather_fields[i++] =
      (MessageField){MESSAGE_KEY_WEATHER_AQI, PERSIST_KEY_WEATHER_AQI, &s_weather_aqi};
  s_weather_fields[i++] =
      (MessageField){MESSAGE_KEY_WEATHER_UV, PERSIST_KEY_WEATHER_UV, &s_weather_uv};
  s_weather_fields[i++] = (MessageField){MESSAGE_KEY_WEATHER_HUMIDITY, PERSIST_KEY_WEATHER_HUMIDITY,
                                         &s_weather_humidity};
  s_weather_fields[i++] =
      (MessageField){MESSAGE_KEY_WEATHER_WIND_DIRECTION, PERSIST_KEY_WEATHER_WIND_DIRECTION,
                     &s_weather_wind_direction};
  s_weather_fields[i++] = (MessageField){MESSAGE_KEY_WEATHER_WIND_SPEED,
                                         PERSIST_KEY_WEATHER_WIND_SPEED, &s_weather_wind_speed};
  s_weather_fields[i++] =
      (MessageField){MESSAGE_KEY_WEATHER_PCP, PERSIST_KEY_WEATHER_PCP, &s_weather_pcp};
  s_weather_fields[i++] =
      (MessageField){MESSAGE_KEY_WEATHER_PRECIP_NOW, PERSIST_KEY_WEATHER_PRECIP_NOW, &s_precip_now};
  s_weather_fields[i++] =
      (MessageField){MESSAGE_KEY_WEATHER_HIGH, PERSIST_KEY_WEATHER_HIGH, &s_temp_high};
  s_weather_fields[i++] =
      (MessageField){MESSAGE_KEY_WEATHER_LOW, PERSIST_KEY_WEATHER_LOW, &s_temp_low};
  s_weather_fields[i++] = (MessageField){MESSAGE_KEY_WEATHER_LOW_TOMORROW,
                                         PERSIST_KEY_WEATHER_LOW_TOMORROW, &s_temp_low_tmrw};
  s_weather_fields[i++] = (MessageField){MESSAGE_KEY_WEATHER_TEMP_HIGH_TOMORROW,
                                         PERSIST_KEY_WEATHER_HIGH_TOMORROW, &s_temp_high_tmrw};
  s_weather_fields[i++] = (MessageField){MESSAGE_KEY_WEATHER_HI_HOUR_TODAY,
                                         PERSIST_KEY_WEATHER_HI_HOUR_TODAY, &s_hi_hour_today};
  s_weather_fields[i++] = (MessageField){MESSAGE_KEY_WEATHER_LO_HOUR_TODAY,
                                         PERSIST_KEY_WEATHER_LO_HOUR_TODAY, &s_lo_hour_today};
  s_weather_fields[i++] = (MessageField){MESSAGE_KEY_WEATHER_HI_HOUR_TOMORROW,
                                         PERSIST_KEY_WEATHER_HI_HOUR_TOMORROW, &s_hi_hour_tmrw};
  s_weather_fields[i++] = (MessageField){MESSAGE_KEY_WEATHER_LO_HOUR_TOMORROW,
                                         PERSIST_KEY_WEATHER_LO_HOUR_TOMORROW, &s_lo_hour_tmrw};
  // A row added without widening the array overflows silently; trip on it.
  if (i != sizeof(s_weather_fields) / sizeof(s_weather_fields[0])) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "weather field table overfilled: %u rows", i);
  }
  i = 0;
  s_settings_fields[i++] =
      (MessageField){MESSAGE_KEY_SETTINGS_THEME, PERSIST_KEY_SETTINGS_THEME, &s_settings_theme};
  s_settings_fields[i++] =
      (MessageField){MESSAGE_KEY_SETTINGS_UNITS, PERSIST_KEY_SETTINGS_UNITS, &s_settings_units};
  s_settings_fields[i++] = (MessageField){
      MESSAGE_KEY_SETTINGS_DATE_FORMAT, PERSIST_KEY_SETTINGS_DATE_FORMAT, &s_settings_date_format};
  s_settings_fields[i++] =
      (MessageField){MESSAGE_KEY_SETTINGS_SHORT_DATE_FORMAT, PERSIST_KEY_SETTINGS_SHORT_DATE,
                     &s_settings_short_date_format};
  s_settings_fields[i++] = (MessageField){MESSAGE_KEY_SETTINGS_DOW_POSITION,
                                          PERSIST_KEY_SETTINGS_DOW, &s_settings_dow_position};
  s_settings_fields[i++] =
      (MessageField){MESSAGE_KEY_SETTINGS_DISCONNECT_VIBE, PERSIST_KEY_SETTINGS_DISCONNECT_VIBE,
                     &s_settings_disconnect_vibe};
  if (i != sizeof(s_settings_fields) / sizeof(s_settings_fields[0])) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "settings field table overfilled: %u rows", i);
  }
  s_slot_keys[0] = (MessageField){MESSAGE_KEY_SLOT_1, PERSIST_KEY_SLOT_1, NULL};
  s_slot_keys[1] = (MessageField){MESSAGE_KEY_SLOT_2, PERSIST_KEY_SLOT_2, NULL};
  s_slot_keys[2] = (MessageField){MESSAGE_KEY_SLOT_3, PERSIST_KEY_SLOT_3, NULL};
  s_slot_keys[3] = (MessageField){MESSAGE_KEY_SLOT_4, PERSIST_KEY_SLOT_4, NULL};
  s_slot_keys[4] = (MessageField){MESSAGE_KEY_SLOT_5, PERSIST_KEY_SLOT_5, NULL};
  s_slot_keys[5] = (MessageField){MESSAGE_KEY_SLOT_6, PERSIST_KEY_SLOT_6, NULL};
}

void save_weather_cache(void) {
  init_message_tables();
  for (unsigned i = 0; i < sizeof(s_weather_fields) / sizeof(s_weather_fields[0]); i++) {
    persist_write_int_if_changed(s_weather_fields[i].persist_key, *s_weather_fields[i].target);
  }
  persist_write_string_if_changed(PERSIST_KEY_WEATHER_COND, s_weather_cond);
  // Always: the timestamp is the freshness marker; skipping it would age the
  // cache and cost a network fetch on next launch.
  persist_write_int(PERSIST_KEY_WEATHER_TIMESTAMP, (int32_t)time(NULL));
}

bool load_weather_cache(void) {
  init_message_tables();
  if (!persist_exists(PERSIST_KEY_WEATHER_TIMESTAMP)) return false;

  int32_t saved_at = persist_read_int(PERSIST_KEY_WEATHER_TIMESTAMP);
  int32_t age = (int32_t)time(NULL) - saved_at;
  if (age < 0 || age > WEATHER_CACHE_MAX_AGE_S) return false;

  // Each field is individually optional: a cache written by an older build
  // lacks the newer keys, and those readings keep their sentinels.
  for (unsigned i = 0; i < sizeof(s_weather_fields) / sizeof(s_weather_fields[0]); i++) {
    if (persist_exists(s_weather_fields[i].persist_key)) {
      *s_weather_fields[i].target = persist_read_int(s_weather_fields[i].persist_key);
    }
  }
  if (persist_exists(PERSIST_KEY_WEATHER_COND)) {
    persist_read_string(PERSIST_KEY_WEATHER_COND, s_weather_cond, sizeof(s_weather_cond));
  }
  return true;
}

void load_settings(void) {
  init_message_tables();
  for (unsigned i = 0; i < sizeof(s_settings_fields) / sizeof(s_settings_fields[0]); i++) {
    if (persist_exists(s_settings_fields[i].persist_key)) {
      *s_settings_fields[i].target = persist_read_int(s_settings_fields[i].persist_key);
    }
  }
  for (unsigned i = 0; i < sizeof(s_slot_keys) / sizeof(s_slot_keys[0]); i++) {
    if (persist_exists(s_slot_keys[i].persist_key)) {
      s_complication_slots[i].source = persist_read_int(s_slot_keys[i].persist_key);
    }
  }
}

void request_weather() {
  DictionaryIterator* iter;
  app_message_outbox_begin(&iter);
  if (iter == NULL) return;

  dict_write_uint8(iter, MESSAGE_KEY_WEATHER_TEMP, 0);  // Trigger fetch
  app_message_outbox_send();
}

void inbox_received_callback(DictionaryIterator* iterator, void* context) {
  init_message_tables();
  // WEATHER_TEMP + WEATHER_COND together mark a real weather payload: a
  // settings-only message must not refresh the cache timestamp, so the temp
  // is parsed with the condition word and both stay out of the generic walk.
  Tuple* temp_tuple = dict_find(iterator, MESSAGE_KEY_WEATHER_TEMP);
  Tuple* cond_tuple = dict_find(iterator, MESSAGE_KEY_WEATHER_COND);
  if (temp_tuple && cond_tuple) {
    s_weather_temp = temp_tuple->value->int32;
    snprintf(s_weather_cond, sizeof(s_weather_cond), "%s", cond_tuple->value->cstring);
  }

  for (unsigned i = 0; i < sizeof(s_weather_fields) / sizeof(s_weather_fields[0]); i++) {
    if (s_weather_fields[i].message_key == MESSAGE_KEY_WEATHER_TEMP) continue;
    Tuple* tuple = dict_find(iterator, s_weather_fields[i].message_key);
    if (tuple) {
      *s_weather_fields[i].target = tuple->value->int32;
    }
  }

  // Persist the weather cache only for a real weather payload, so a
  // settings-only message can't refresh the timestamp.
  if (temp_tuple && cond_tuple) {
    save_weather_cache();
  }

  // Settings: Clay sends strings; tuple_get_int() accepts those and ints.
  // Units is the one receipt with an immediate follow-up — the weather feed
  // has to be re-asked in the new unit — so it's flagged on the way past.
  bool units_changed = false;
  for (unsigned i = 0; i < sizeof(s_settings_fields) / sizeof(s_settings_fields[0]); i++) {
    Tuple* tuple = dict_find(iterator, s_settings_fields[i].message_key);
    if (!tuple) continue;
    int old = *s_settings_fields[i].target;
    *s_settings_fields[i].target = tuple_get_int(tuple);
    if (s_settings_fields[i].target == &s_settings_units && *s_settings_fields[i].target != old) {
      units_changed = true;
    }
    persist_write_int_if_changed(s_settings_fields[i].persist_key, *s_settings_fields[i].target);
  }

  // Assigning or rearranging slots has to fetch now, or a newly shown weather
  // reading sits at "--" until the next :00/:30 edge. Rearranging non-weather
  // slots also pays one fetch — a rare settings edit, not worth gating finer.
  bool needed_weather = any_slot_needs_weather();
  bool slots_changed = false;

  for (unsigned i = 0; i < sizeof(s_slot_keys) / sizeof(s_slot_keys[0]); i++) {
    Tuple* tuple = dict_find(iterator, s_slot_keys[i].message_key);
    if (!tuple) continue;
    ComplicationDataSource source = tuple_get_int(tuple);
    if (s_complication_slots[i].source != source) slots_changed = true;
    s_complication_slots[i].source = source;
    persist_write_int_if_changed(s_slot_keys[i].persist_key, s_complication_slots[i].source);
  }

  // A weather reply carries no SLOT_*/UNITS keys and changes no assignments,
  // so it never re-arms a request.
  bool needs_weather = any_slot_needs_weather();
  if (needs_weather && (units_changed || !needed_weather || slots_changed)) {
    request_weather();
  }

  // Redraw UI with new settings/weather
  update_time();
}

void inbox_dropped_callback(AppMessageResult reason, void* context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}
