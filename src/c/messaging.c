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

void save_weather_cache(void) {
  persist_write_int_if_changed(PERSIST_KEY_WEATHER_TEMP, s_weather_temp);
  persist_write_string_if_changed(PERSIST_KEY_WEATHER_COND, s_weather_cond);
  persist_write_int_if_changed(PERSIST_KEY_WEATHER_AQI, s_weather_aqi);
  persist_write_int_if_changed(PERSIST_KEY_WEATHER_UV, s_weather_uv);
  persist_write_int_if_changed(PERSIST_KEY_WEATHER_HUMIDITY, s_weather_humidity);
  persist_write_int_if_changed(PERSIST_KEY_WEATHER_PCP, s_weather_pcp);
  persist_write_int_if_changed(PERSIST_KEY_WEATHER_HIGH, s_temp_high);
  persist_write_int_if_changed(PERSIST_KEY_WEATHER_LOW, s_temp_low);
  // Always: the timestamp is the freshness marker; skipping it would age the
  // cache and cost a network fetch on next launch.
  persist_write_int(PERSIST_KEY_WEATHER_TIMESTAMP, (int32_t)time(NULL));
}

bool load_weather_cache(void) {
  if (!persist_exists(PERSIST_KEY_WEATHER_TIMESTAMP)) return false;

  int32_t saved_at = persist_read_int(PERSIST_KEY_WEATHER_TIMESTAMP);
  int32_t age = (int32_t)time(NULL) - saved_at;
  if (age < 0 || age > WEATHER_CACHE_MAX_AGE_S) return false;

  if (persist_exists(PERSIST_KEY_WEATHER_TEMP)) {
    s_weather_temp = persist_read_int(PERSIST_KEY_WEATHER_TEMP);
  }
  if (persist_exists(PERSIST_KEY_WEATHER_COND)) {
    persist_read_string(PERSIST_KEY_WEATHER_COND, s_weather_cond, sizeof(s_weather_cond));
  }
  if (persist_exists(PERSIST_KEY_WEATHER_AQI)) {
    s_weather_aqi = persist_read_int(PERSIST_KEY_WEATHER_AQI);
  }
  if (persist_exists(PERSIST_KEY_WEATHER_UV)) {
    s_weather_uv = persist_read_int(PERSIST_KEY_WEATHER_UV);
  }
  if (persist_exists(PERSIST_KEY_WEATHER_HUMIDITY)) {
    s_weather_humidity = persist_read_int(PERSIST_KEY_WEATHER_HUMIDITY);
  }
  if (persist_exists(PERSIST_KEY_WEATHER_PCP)) {
    s_weather_pcp = persist_read_int(PERSIST_KEY_WEATHER_PCP);
  }
  if (persist_exists(PERSIST_KEY_WEATHER_HIGH)) {
    s_temp_high = persist_read_int(PERSIST_KEY_WEATHER_HIGH);
  }
  if (persist_exists(PERSIST_KEY_WEATHER_LOW)) {
    s_temp_low = persist_read_int(PERSIST_KEY_WEATHER_LOW);
  }
  return true;
}

void load_settings(void) {
  if (persist_exists(PERSIST_KEY_SETTINGS_THEME))
    s_settings_theme = persist_read_int(PERSIST_KEY_SETTINGS_THEME);
  if (persist_exists(PERSIST_KEY_SETTINGS_UNITS))
    s_settings_units = persist_read_int(PERSIST_KEY_SETTINGS_UNITS);
  if (persist_exists(PERSIST_KEY_SETTINGS_DATE_FORMAT))
    s_settings_date_format = persist_read_int(PERSIST_KEY_SETTINGS_DATE_FORMAT);
  if (persist_exists(PERSIST_KEY_SETTINGS_SHORT_DATE))
    s_settings_short_date_format = persist_read_int(PERSIST_KEY_SETTINGS_SHORT_DATE);
  if (persist_exists(PERSIST_KEY_SETTINGS_DOW))
    s_settings_dow_position = persist_read_int(PERSIST_KEY_SETTINGS_DOW);
  if (persist_exists(PERSIST_KEY_SLOT_1))
    s_complication_slots[0].source = persist_read_int(PERSIST_KEY_SLOT_1);
  if (persist_exists(PERSIST_KEY_SLOT_2))
    s_complication_slots[1].source = persist_read_int(PERSIST_KEY_SLOT_2);
  if (persist_exists(PERSIST_KEY_SLOT_3))
    s_complication_slots[2].source = persist_read_int(PERSIST_KEY_SLOT_3);
  if (persist_exists(PERSIST_KEY_SLOT_4))
    s_complication_slots[3].source = persist_read_int(PERSIST_KEY_SLOT_4);
  if (persist_exists(PERSIST_KEY_SLOT_5))
    s_complication_slots[4].source = persist_read_int(PERSIST_KEY_SLOT_5);
  if (persist_exists(PERSIST_KEY_SLOT_6))
    s_complication_slots[5].source = persist_read_int(PERSIST_KEY_SLOT_6);
}

void request_weather() {
  DictionaryIterator* iter;
  app_message_outbox_begin(&iter);
  if (iter == NULL) return;

  dict_write_uint8(iter, MESSAGE_KEY_WEATHER_TEMP, 0);  // Trigger fetch
  app_message_outbox_send();
}

void inbox_received_callback(DictionaryIterator* iterator, void* context) {
  // Weather
  Tuple* temp_tuple = dict_find(iterator, MESSAGE_KEY_WEATHER_TEMP);
  Tuple* cond_tuple = dict_find(iterator, MESSAGE_KEY_WEATHER_COND);
  if (temp_tuple && cond_tuple) {
    s_weather_temp = temp_tuple->value->int32;
    snprintf(s_weather_cond, sizeof(s_weather_cond), "%s", cond_tuple->value->cstring);
  }

  Tuple* aqi_tuple = dict_find(iterator, MESSAGE_KEY_WEATHER_AQI);
  if (aqi_tuple) {
    s_weather_aqi = aqi_tuple->value->int32;
  }

  Tuple* uv_tuple = dict_find(iterator, MESSAGE_KEY_WEATHER_UV);
  if (uv_tuple) {
    s_weather_uv = uv_tuple->value->int32;
  }

  Tuple* humidity_tuple = dict_find(iterator, MESSAGE_KEY_WEATHER_HUMIDITY);
  if (humidity_tuple) {
    s_weather_humidity = humidity_tuple->value->int32;
  }

  Tuple* pcp_tuple = dict_find(iterator, MESSAGE_KEY_WEATHER_PCP);
  if (pcp_tuple) {
    s_weather_pcp = pcp_tuple->value->int32;
  }

  Tuple* high_tuple = dict_find(iterator, MESSAGE_KEY_WEATHER_HIGH);
  if (high_tuple) {
    s_temp_high = high_tuple->value->int32;
  }

  Tuple* low_tuple = dict_find(iterator, MESSAGE_KEY_WEATHER_LOW);
  if (low_tuple) {
    s_temp_low = low_tuple->value->int32;
  }

  // Persist the weather cache only for a real weather payload, so a
  // settings-only message can't refresh the timestamp.
  if (temp_tuple && cond_tuple) {
    save_weather_cache();
  }

  // Settings: Theme
  Tuple* theme_tuple = dict_find(iterator, MESSAGE_KEY_SETTINGS_THEME);
  if (theme_tuple) {
    s_settings_theme = tuple_get_int(theme_tuple);
    persist_write_int_if_changed(PERSIST_KEY_SETTINGS_THEME, s_settings_theme);
  }

  Tuple* units_tuple = dict_find(iterator, MESSAGE_KEY_SETTINGS_UNITS);
  bool units_changed = false;
  if (units_tuple) {
    int val = tuple_get_int(units_tuple);
    if (s_settings_units != val) {
      s_settings_units = val;
      units_changed = true;
    }
    persist_write_int_if_changed(PERSIST_KEY_SETTINGS_UNITS, s_settings_units);
  }

  Tuple* date_format_tuple = dict_find(iterator, MESSAGE_KEY_SETTINGS_DATE_FORMAT);
  if (date_format_tuple) {
    s_settings_date_format = tuple_get_int(date_format_tuple);
    persist_write_int_if_changed(PERSIST_KEY_SETTINGS_DATE_FORMAT, s_settings_date_format);
  }

  Tuple* short_date_tuple = dict_find(iterator, MESSAGE_KEY_SETTINGS_SHORT_DATE_FORMAT);
  if (short_date_tuple) {
    s_settings_short_date_format = tuple_get_int(short_date_tuple);
    persist_write_int_if_changed(PERSIST_KEY_SETTINGS_SHORT_DATE, s_settings_short_date_format);
  }

  Tuple* dow_tuple = dict_find(iterator, MESSAGE_KEY_SETTINGS_DOW_POSITION);
  if (dow_tuple) {
    s_settings_dow_position = tuple_get_int(dow_tuple);
    persist_write_int_if_changed(PERSIST_KEY_SETTINGS_DOW, s_settings_dow_position);
  }

  // Assigning or rearranging slots has to fetch now, or a newly shown weather
  // reading sits at "--" until the next :00/:30 edge. Rearranging non-weather
  // slots also pays one fetch — a rare settings edit, not worth gating finer.
  bool needed_weather = any_slot_needs_weather();
  bool slots_changed = false;

  Tuple* slot1 = dict_find(iterator, MESSAGE_KEY_SLOT_1);
  if (slot1) {
    ComplicationDataSource source = tuple_get_int(slot1);
    if (s_complication_slots[0].source != source) slots_changed = true;
    s_complication_slots[0].source = source;
    persist_write_int_if_changed(PERSIST_KEY_SLOT_1, s_complication_slots[0].source);
  }

  Tuple* slot2 = dict_find(iterator, MESSAGE_KEY_SLOT_2);
  if (slot2) {
    ComplicationDataSource source = tuple_get_int(slot2);
    if (s_complication_slots[1].source != source) slots_changed = true;
    s_complication_slots[1].source = source;
    persist_write_int_if_changed(PERSIST_KEY_SLOT_2, s_complication_slots[1].source);
  }

  Tuple* slot3 = dict_find(iterator, MESSAGE_KEY_SLOT_3);
  if (slot3) {
    ComplicationDataSource source = tuple_get_int(slot3);
    if (s_complication_slots[2].source != source) slots_changed = true;
    s_complication_slots[2].source = source;
    persist_write_int_if_changed(PERSIST_KEY_SLOT_3, s_complication_slots[2].source);
  }

  Tuple* slot4 = dict_find(iterator, MESSAGE_KEY_SLOT_4);
  if (slot4) {
    ComplicationDataSource source = tuple_get_int(slot4);
    if (s_complication_slots[3].source != source) slots_changed = true;
    s_complication_slots[3].source = source;
    persist_write_int_if_changed(PERSIST_KEY_SLOT_4, s_complication_slots[3].source);
  }

  Tuple* slot5 = dict_find(iterator, MESSAGE_KEY_SLOT_5);
  if (slot5) {
    ComplicationDataSource source = tuple_get_int(slot5);
    if (s_complication_slots[4].source != source) slots_changed = true;
    s_complication_slots[4].source = source;
    persist_write_int_if_changed(PERSIST_KEY_SLOT_5, s_complication_slots[4].source);
  }

  Tuple* slot6 = dict_find(iterator, MESSAGE_KEY_SLOT_6);
  if (slot6) {
    ComplicationDataSource source = tuple_get_int(slot6);
    if (s_complication_slots[5].source != source) slots_changed = true;
    s_complication_slots[5].source = source;
    persist_write_int_if_changed(PERSIST_KEY_SLOT_6, s_complication_slots[5].source);
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
