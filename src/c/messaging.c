#include <pebble.h>
#include "messaging.h"
#include "data.h"
#include "complication.h"
#include "chime.h"
#include "main.h"
#include "crt.h"

// Data flow, phone → watch: the watch sends a trigger AppMessage (launch
// with a stale cache, the :00/:30 tick, a settings push that newly needs
// weather); PKJS fetches Open-Meteo (forecast and AQI, joined) and replies
// with one dictionary; inbox_received_callback lands values in data.c
// globals, persists the weather cache (30-minute TTL), and redraws. Settings
// from the Clay page travel the same path and persist as PERSIST_KEY_SETTINGS_*.

static void persist_write_int_if_changed(uint32_t key, int32_t value) {
  if (!persist_exists(key) || persist_read_int(key) != value) {
    persist_write_int(key, value);
  }
}

// Accepts whatever width the wire used, and the strings Clay sends.
// TUPLE_INT reads the signed members: a negative value keeps its sign at
// any width.
int tuple_get_int(Tuple* tuple) {
  if (!tuple) return 0;
  switch (tuple->type) {
    case TUPLE_INT:
      if (tuple->length == 1)
        return tuple->value->int8;
      else if (tuple->length == 2)
        return tuple->value->int16;
      else if (tuple->length == 4)
        return tuple->value->int32;
      return 0;
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

// One message-received reading: its MESSAGE_KEY_* by address (taking the
// address of an extern is a compile-time constant, so these tables stay
// static const even though the SDK bakes keys as extern variables), the
// PERSIST_KEY_* it persists under, the data.c global it lands in, and that
// global's no-data sentinel — declared here so the wire table and the
// formatters' fallback value meet on one row (settings have no sentinel;
// they carry 0).
typedef struct {
  const uint32_t* message_key;
  uint32_t persist_key;
  int* target;
  int sentinel;
} MessageField;

// Every int reading in a weather payload. Three walks share this table —
// inbox parse, cache save, cache load — which keeps those halves from
// drifting apart.
static const MessageField s_weather_fields[] = {
    {&MESSAGE_KEY_WEATHER_TEMP, PERSIST_KEY_WEATHER_TEMP, &s_weather_temp, WEATHER_TEMP_NONE},
    {&MESSAGE_KEY_WEATHER_COND, PERSIST_KEY_WEATHER_COND_CODE, &s_weather_cond_code, WEATHER_NONE},
    {&MESSAGE_KEY_WEATHER_AQI, PERSIST_KEY_WEATHER_AQI, &s_weather_aqi, WEATHER_NONE},
    {&MESSAGE_KEY_WEATHER_UV, PERSIST_KEY_WEATHER_UV, &s_weather_uv, WEATHER_NONE},
    {&MESSAGE_KEY_WEATHER_HUMIDITY, PERSIST_KEY_WEATHER_HUMIDITY, &s_weather_humidity,
     WEATHER_NONE},
    {&MESSAGE_KEY_WEATHER_WIND_DIRECTION, PERSIST_KEY_WEATHER_WIND_DIRECTION,
     &s_weather_wind_direction, WEATHER_NONE},
    {&MESSAGE_KEY_WEATHER_WIND_SPEED, PERSIST_KEY_WEATHER_WIND_SPEED, &s_weather_wind_speed,
     WEATHER_NONE},
    {&MESSAGE_KEY_WEATHER_PCP, PERSIST_KEY_WEATHER_PCP, &s_weather_pcp, WEATHER_NONE},
    {&MESSAGE_KEY_WEATHER_PRECIP_NOW, PERSIST_KEY_WEATHER_PRECIP_NOW, &s_precip_now, WEATHER_NONE},
    {&MESSAGE_KEY_WEATHER_HIGH, PERSIST_KEY_WEATHER_HIGH, &s_temp_high, WEATHER_TEMP_NONE},
    {&MESSAGE_KEY_WEATHER_LOW, PERSIST_KEY_WEATHER_LOW, &s_temp_low, WEATHER_TEMP_NONE},
    {&MESSAGE_KEY_WEATHER_LOW_TOMORROW, PERSIST_KEY_WEATHER_LOW_TOMORROW, &s_temp_low_tmrw,
     WEATHER_TEMP_NONE},
    {&MESSAGE_KEY_WEATHER_TEMP_HIGH_TOMORROW, PERSIST_KEY_WEATHER_HIGH_TOMORROW, &s_temp_high_tmrw,
     WEATHER_TEMP_NONE},
    {&MESSAGE_KEY_WEATHER_HI_HOUR_TODAY, PERSIST_KEY_WEATHER_HI_HOUR_TODAY, &s_hi_hour_today,
     WEATHER_NONE},
    {&MESSAGE_KEY_WEATHER_LO_HOUR_TODAY, PERSIST_KEY_WEATHER_LO_HOUR_TODAY, &s_lo_hour_today,
     WEATHER_NONE},
    {&MESSAGE_KEY_WEATHER_HI_HOUR_TOMORROW, PERSIST_KEY_WEATHER_HI_HOUR_TOMORROW, &s_hi_hour_tmrw,
     WEATHER_NONE},
    {&MESSAGE_KEY_WEATHER_LO_HOUR_TOMORROW, PERSIST_KEY_WEATHER_LO_HOUR_TOMORROW, &s_lo_hour_tmrw,
     WEATHER_NONE},
    // The phone's fetch time doubles as the cache's freshness marker.
    {&MESSAGE_KEY_WEATHER_FETCHED_AT, PERSIST_KEY_WEATHER_TIMESTAMP, &s_weather_stamp, 0},
};

// The settings Clay pushes. load_settings() restores from the same rows'
// persist keys, so the two halves of the settings format meet in one table.
static const MessageField s_settings_fields[] = {
    {&MESSAGE_KEY_SETTINGS_THEME, PERSIST_KEY_SETTINGS_THEME, &s_settings_theme, 0},
    {&MESSAGE_KEY_SETTINGS_UNITS, PERSIST_KEY_SETTINGS_UNITS, &s_settings_units, 0},
    {&MESSAGE_KEY_SETTINGS_DATE_FORMAT, PERSIST_KEY_SETTINGS_DATE_FORMAT, &s_settings_date_format,
     0},
    {&MESSAGE_KEY_SETTINGS_SHORT_DATE_FORMAT, PERSIST_KEY_SETTINGS_SHORT_DATE,
     &s_settings_short_date_format, 0},
    {&MESSAGE_KEY_SETTINGS_DOW_POSITION, PERSIST_KEY_SETTINGS_DOW, &s_settings_dow_position, 0},
    {&MESSAGE_KEY_SETTINGS_WEATHER_WINDOW, PERSIST_KEY_SETTINGS_WEATHER_WINDOW,
     &s_settings_weather_window, 0},
    {&MESSAGE_KEY_SETTINGS_CRT, PERSIST_KEY_SETTINGS_CRT, &s_settings_crt, 0},
    {&MESSAGE_KEY_SETTINGS_CRT_SOUND, PERSIST_KEY_SETTINGS_CRT_SOUND, &s_settings_crt_sound, 0},
    {&MESSAGE_KEY_SETTINGS_HOURLY_CHIME, PERSIST_KEY_SETTINGS_HOURLY_CHIME,
     &s_settings_hourly_chime, 0},
    {&MESSAGE_KEY_SETTINGS_CHIME_VOLUME, PERSIST_KEY_SETTINGS_CHIME_VOLUME,
     &s_settings_chime_volume, 0},
};

// The slot persist keys are deliberately not sequential (SLOT_6 landed after
// the settings block), so the pairing is tabulated, not computed. Row i pairs
// SLOT_{i+1}'s keys; index is the slot (SLOT_IDX_*), target unused.
static const MessageField s_slot_keys[NUM_SLOTS] = {
    {&MESSAGE_KEY_SLOT_1, PERSIST_KEY_SLOT_1, NULL, 0},
    {&MESSAGE_KEY_SLOT_2, PERSIST_KEY_SLOT_2, NULL, 0},
    {&MESSAGE_KEY_SLOT_3, PERSIST_KEY_SLOT_3, NULL, 0},
    {&MESSAGE_KEY_SLOT_4, PERSIST_KEY_SLOT_4, NULL, 0},
    {&MESSAGE_KEY_SLOT_5, PERSIST_KEY_SLOT_5, NULL, 0},
    {&MESSAGE_KEY_SLOT_6, PERSIST_KEY_SLOT_6, NULL, 0},
};

void save_weather_cache(void) {
  for (unsigned i = 0; i < sizeof(s_weather_fields) / sizeof(s_weather_fields[0]); i++) {
    persist_write_int_if_changed(s_weather_fields[i].persist_key, *s_weather_fields[i].target);
  }
}

// A torn or garbage wire int must neither pass for data nor paint past its
// slot: 2000000000 off the wire formats to ten digits and spills out of a
// 93px window into the TIME region. A reading outside its field's range is
// "no reading"; only the layout's display-width caps (UV, wind speed) clamp.
static void sanitize_int(int* v, int sentinel, int lo, int hi) {
  if (*v < lo || *v > hi) *v = sentinel;
}

static void cap_int(int* v, int hi) {
  if (*v > hi) *v = hi;
}

static void sanitize_weather_values(void) {
  sanitize_int(&s_weather_temp, WEATHER_TEMP_NONE, -99, 999);
  sanitize_int(&s_temp_high, WEATHER_TEMP_NONE, -99, 999);
  sanitize_int(&s_temp_low, WEATHER_TEMP_NONE, -99, 999);
  sanitize_int(&s_temp_low_tmrw, WEATHER_TEMP_NONE, -99, 999);
  sanitize_int(&s_temp_high_tmrw, WEATHER_TEMP_NONE, -99, 999);
  sanitize_int(&s_weather_cond_code, WEATHER_NONE, 0, 99);  // WMO table is < 100
  sanitize_int(&s_weather_aqi, WEATHER_NONE, 0, 500);
  cap_int(&s_weather_uv, 99);  // UV is open-ended (WHO top band is "11+")
  sanitize_int(&s_weather_uv, WEATHER_NONE, 0, 99);
  sanitize_int(&s_weather_humidity, WEATHER_NONE, 0, 100);
  sanitize_int(&s_weather_pcp, WEATHER_NONE, 0, 100);
  sanitize_int(&s_precip_now, WEATHER_NONE, 0, 999);
  sanitize_int(&s_weather_wind_direction, WEATHER_NONE, 0, 360);
  cap_int(&s_weather_wind_speed, 999);
  sanitize_int(&s_weather_wind_speed, WEATHER_NONE, 0, 999);
  sanitize_int(&s_hi_hour_today, WEATHER_NONE, 0, 23);
  sanitize_int(&s_lo_hour_today, WEATHER_NONE, 0, 23);
  sanitize_int(&s_hi_hour_tmrw, WEATHER_NONE, 0, 23);
  sanitize_int(&s_lo_hour_tmrw, WEATHER_NONE, 0, 23);
}

// East positive, hundredths of a degree.
static void sanitize_longitude(void) {
  sanitize_int(&s_longitude, LONGITUDE_NONE, -18000, 18000);
}

void load_longitude(void) {
  if (persist_exists(PERSIST_KEY_LONGITUDE)) {
    s_longitude = persist_read_int(PERSIST_KEY_LONGITUDE);
    sanitize_longitude();
  }
}

static int local_day_key(time_t t) {
  struct tm* local = localtime(&t);
  return local->tm_year * 366 + local->tm_yday;
}

WeatherAge weather_age(time_t stamp, time_t now) {
  if (stamp <= 0 || stamp > now + WEATHER_STAMP_FUTURE_SLACK_S) return WEATHER_AGE_STALE;
  if (now - stamp > WEATHER_STALE_AGE_S) return WEATHER_AGE_STALE;
  return local_day_key(stamp) != local_day_key(now) ? WEATHER_AGE_PREVIOUS_DAY : WEATHER_AGE_FRESH;
}

time_t weather_stamp_for(int32_t fetched_at, time_t now) {
  bool sane = fetched_at > 0 && fetched_at <= now + WEATHER_STAMP_FUTURE_SLACK_S;
  return sane ? fetched_at : now;
}

bool weather_fetch_due(int minute, bool needs_weather) {
  return needs_weather && minute % WEATHER_FETCH_PERIOD_MIN == 0;
}

bool weather_fetch_after_settings(bool needs_weather, bool units_changed, bool window_changed,
                                  bool needed_weather, bool slots_changed) {
  return needs_weather && (units_changed || window_changed || !needed_weather || slots_changed);
}

bool weather_fetch_on_reconnect(bool needs_weather, time_t stamp, time_t now, time_t last_request) {
  if (!needs_weather) return false;
  if (last_request > 0 && now >= last_request && now - last_request < WEATHER_RECONNECT_MIN_GAP_S) {
    return false;
  }
  return stamp <= 0 || stamp > now + WEATHER_STAMP_FUTURE_SLACK_S ||
         now - stamp > WEATHER_CACHE_MAX_AGE_S;
}

static void weather_clear(void) {
  for (unsigned i = 0; i < sizeof(s_weather_fields) / sizeof(s_weather_fields[0]); i++) {
    *s_weather_fields[i].target = s_weather_fields[i].sentinel;
  }
}

// The day-bound readings: today's and tomorrow's extremes and their hours.
static void weather_clear_extremes(void) {
  s_temp_high = s_temp_low = s_temp_high_tmrw = s_temp_low_tmrw = WEATHER_TEMP_NONE;
  s_hi_hour_today = s_lo_hour_today = s_hi_hour_tmrw = s_lo_hour_tmrw = WEATHER_NONE;
}

void weather_expire(time_t now) {
  switch (weather_age(s_weather_stamp, now)) {
    case WEATHER_AGE_FRESH:
      break;
    case WEATHER_AGE_PREVIOUS_DAY:
      weather_clear_extremes();
      break;
    case WEATHER_AGE_STALE:
      weather_clear();
      break;
  }
}

void weather_discard(void) {
  weather_clear();
  persist_write_int(PERSIST_KEY_WEATHER_TIMESTAMP, 0);
}

bool load_weather_cache(void) {
  if (!persist_exists(PERSIST_KEY_WEATHER_TIMESTAMP)) return false;

  time_t now = time(NULL);
  int32_t saved_at = persist_read_int(PERSIST_KEY_WEATHER_TIMESTAMP);
  int32_t age = (int32_t)now - saved_at;
  if (age < 0 || age > WEATHER_CACHE_MAX_AGE_S) return false;

  // Each field is individually optional: a cache written by an older build
  // lacks the newer keys, and those readings keep their sentinels.
  for (unsigned i = 0; i < sizeof(s_weather_fields) / sizeof(s_weather_fields[0]); i++) {
    if (persist_exists(s_weather_fields[i].persist_key)) {
      *s_weather_fields[i].target = persist_read_int(s_weather_fields[i].persist_key);
    }
  }
  weather_expire(now);
  return true;
}

// An id with no registry row — from a newer build's settings or a torn wire
// int — is an empty window, not an unnameable one.
static ComplicationDataSource known_source(int id) {
  return complication_spec((ComplicationDataSource)id) ? (ComplicationDataSource)id
                                                       : DATA_SOURCE_EMPTY;
}

void load_settings(void) {
  for (unsigned i = 0; i < sizeof(s_settings_fields) / sizeof(s_settings_fields[0]); i++) {
    if (persist_exists(s_settings_fields[i].persist_key)) {
      *s_settings_fields[i].target = persist_read_int(s_settings_fields[i].persist_key);
    }
  }
  for (unsigned i = 0; i < sizeof(s_slot_keys) / sizeof(s_slot_keys[0]); i++) {
    if (persist_exists(s_slot_keys[i].persist_key)) {
      s_complication_slots[i].source = known_source(persist_read_int(s_slot_keys[i].persist_key));
    }
  }
}

static void send_weather_request(void) {
  // With no phone link the outbox fails at once and would only arm the
  // retry timer; weather_fetch_on_reconnect() asks again when the link is back.
  if (!s_connected) return;
  DictionaryIterator* iter;
  app_message_outbox_begin(&iter);
  // No retry on begin failure (outbox_failed never fires when nothing was
  // sent, so main.c's bounded retry doesn't either); the next :00/:30 tick
  // re-asks.
  if (iter == NULL) return;

  dict_write_uint8(iter, MESSAGE_KEY_WEATHER_REQUEST, 0);
  app_message_outbox_send();
}

void request_weather(void) {
  weather_retries_reset();
  send_weather_request();
}

void request_weather_retry(void) {
  send_weather_request();
}

void inbox_received_callback(DictionaryIterator* iterator, void* context) {
  (void)context;
  // WEATHER_TEMP + WEATHER_COND together mark a real weather payload; a
  // settings-only message must not refresh the cache timestamp. The payload
  // check reads temp itself, so the table walk skips it; cond rides the
  // table like every other reading.
  Tuple* temp_tuple = dict_find(iterator, MESSAGE_KEY_WEATHER_TEMP);
  Tuple* cond_tuple = dict_find(iterator, MESSAGE_KEY_WEATHER_COND);
  if (temp_tuple && cond_tuple) {
    s_weather_temp = tuple_get_int(temp_tuple);
  }

  for (unsigned i = 0; i < sizeof(s_weather_fields) / sizeof(s_weather_fields[0]); i++) {
    if (s_weather_fields[i].message_key == &MESSAGE_KEY_WEATHER_TEMP) continue;
    Tuple* tuple = dict_find(iterator, *s_weather_fields[i].message_key);
    if (tuple) {
      *s_weather_fields[i].target = tuple_get_int(tuple);
    }
  }

  // The position rides the weather reply but outlives it: it is neither part
  // of the cache nor cleared by its expiry.
  Tuple* longitude_tuple = dict_find(iterator, MESSAGE_KEY_LONGITUDE);
  if (longitude_tuple) {
    s_longitude = tuple_get_int(longitude_tuple);
    sanitize_longitude();
    persist_write_int_if_changed(PERSIST_KEY_LONGITUDE, s_longitude);
  }

  // Persist the weather cache only for a real weather payload, so a
  // settings-only message can't refresh the timestamp. The payload's fetch
  // time stamps it, or the arrival time when that is missing or impossible.
  if (temp_tuple && cond_tuple) {
    sanitize_weather_values();
    time_t now = time(NULL);
    s_weather_stamp = (int)weather_stamp_for(
        tuple_get_int(dict_find(iterator, MESSAGE_KEY_WEATHER_FETCHED_AT)), now);
    weather_expire(now);
    save_weather_cache();
    weather_retries_reset();
  }

  // Settings: Clay sends strings; tuple_get_int() accepts those and ints.
  // Units and the forecast window are the receipts with an immediate
  // follow-up — both change what the phone's reduction computes — so they're
  // flagged on the way past.
  bool units_changed = false;
  bool window_changed = false;
  for (unsigned i = 0; i < sizeof(s_settings_fields) / sizeof(s_settings_fields[0]); i++) {
    Tuple* tuple = dict_find(iterator, *s_settings_fields[i].message_key);
    if (!tuple) continue;
    int old = *s_settings_fields[i].target;
    *s_settings_fields[i].target = tuple_get_int(tuple);
    if (s_settings_fields[i].target == &s_settings_units && *s_settings_fields[i].target != old) {
      units_changed = true;
    }
    if (s_settings_fields[i].target == &s_settings_weather_window &&
        *s_settings_fields[i].target != old) {
      window_changed = true;
    }
    // The CRT overlay repaints outside the canvas's snapshot gate, so a toggle
    // needs its own dirty-mark (units/window only schedule fetches). The sound
    // toggle goes through the same hook to release its PCM.
    if ((s_settings_fields[i].target == &s_settings_crt ||
         s_settings_fields[i].target == &s_settings_crt_sound) &&
        *s_settings_fields[i].target != old) {
      crt_apply_setting_change();
    }
    persist_write_int_if_changed(s_settings_fields[i].persist_key, *s_settings_fields[i].target);
  }

  // Action key: the settings page's test button. An action, not a setting —
  // no persist key, no s_settings_* global. Deliberately bypasses
  // speaker_is_muted(): the user asked for the beep explicitly, a silent test
  // reads as broken. After the settings walk so the volume shipped in the same
  // dict (the button submits the whole form) is already applied.
  Tuple* test_tuple = dict_find(iterator, MESSAGE_KEY_CHIME_TEST);
  if (test_tuple && tuple_get_int(test_tuple)) {
    chime_play();
  }

  // New units or a new window leave the held readings in the old terms; none
  // may sit beside the new unit letter while the refetch is in flight.
  if (units_changed || window_changed) weather_discard();

  // Assigning or rearranging slots has to fetch now, or a newly shown weather
  // reading sits at "--" until the next :00/:30 edge. Rearranging non-weather
  // slots also pays one fetch — a rare settings edit, not worth gating finer.
  bool needed_weather = any_slot_needs_weather();
  bool slots_changed = false;

  for (unsigned i = 0; i < sizeof(s_slot_keys) / sizeof(s_slot_keys[0]); i++) {
    Tuple* tuple = dict_find(iterator, *s_slot_keys[i].message_key);
    if (!tuple) continue;
    ComplicationDataSource source = known_source(tuple_get_int(tuple));
    if (s_complication_slots[i].source != source) slots_changed = true;
    s_complication_slots[i].source = source;
    persist_write_int_if_changed(s_slot_keys[i].persist_key, s_complication_slots[i].source);
  }

  refresh_subscriptions();

  // A weather reply carries no SLOT_*/SETTINGS_* keys and changes no
  // assignments, so it never re-arms a request.
  bool needs_weather = any_slot_needs_weather();
  if (weather_fetch_after_settings(needs_weather, units_changed, window_changed, needed_weather,
                                   slots_changed)) {
    request_weather();
  }

  refresh_state();
}

void inbox_dropped_callback(AppMessageResult reason, void* context) {
  (void)context;
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped (%d)", (int)reason);
}
