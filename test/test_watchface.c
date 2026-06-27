#define TEST_ENV

#include "unity/src/unity.h"
#include "pebble.h"

// Include the implementation file directly so we can test its static functions
#include "../src/c/data.c"
#include "../src/c/theme.c"
#include "../src/c/drawing.c"
#include "../src/c/messaging.c"
#include "../src/c/main.c"

void setUp(void) {
  // Reset any global states if needed before each test
  s_settings_theme = 0;  // Auto
  s_settings_units = 0;  // Imperial
  s_battery_level = 100;
  s_step_count = -1;
  s_sleep_seconds = -1;
  s_heart_rate = 0;
  s_weather_temp = -999;
  strcpy(s_weather_cond, "--");
  s_connected = true;
}

void tearDown(void) {}

// -----------------------------------------------------------------------------
// Tests
// -----------------------------------------------------------------------------

void test_to_upper_str_should_convert_lowercase_to_uppercase(void) {
  char str1[] = "hello 123";
  to_upper_str(str1);
  TEST_ASSERT_EQUAL_STRING("HELLO 123", str1);

  char str2[] = "Mon";
  to_upper_str(str2);
  TEST_ASSERT_EQUAL_STRING("MON", str2);

  char str3[] = "ALREADY_UPPER";
  to_upper_str(str3);
  TEST_ASSERT_EQUAL_STRING("ALREADY_UPPER", str3);
}

void test_tuple_get_int_should_parse_strings_and_ints(void) {
  // We mock a tuple since we know its memory layout
  uint8_t buffer1[sizeof(Tuple) + 8];
  Tuple* t1 = (Tuple*)buffer1;
  t1->type = TUPLE_CSTRING;
  strcpy(t1->value->cstring, "42");
  TEST_ASSERT_EQUAL_INT(42, tuple_get_int(t1));

  uint8_t buffer2[sizeof(Tuple) + 4];
  Tuple* t2 = (Tuple*)buffer2;
  t2->type = TUPLE_INT;
  t2->length = 4;
  t2->value->int32 = 1234;
  TEST_ASSERT_EQUAL_INT(1234, tuple_get_int(t2));

  TEST_ASSERT_EQUAL_INT(0, tuple_get_int(NULL));
}

void test_get_source_label_should_return_correct_labels(void) {
  TEST_ASSERT_EQUAL_STRING("BATT", get_source_label(DATA_SOURCE_BATTERY));
  TEST_ASSERT_EQUAL_STRING("STEP", get_source_label(DATA_SOURCE_STEPS));
  TEST_ASSERT_EQUAL_STRING("WEATHER", get_source_label(DATA_SOURCE_WEATHER));
  TEST_ASSERT_EQUAL_STRING("AQI", get_source_label(DATA_SOURCE_AQI));
  TEST_ASSERT_EQUAL_STRING("UV", get_source_label(DATA_SOURCE_UV));
  TEST_ASSERT_EQUAL_STRING("AQI/UV", get_source_label(DATA_SOURCE_AQI_UV));
  TEST_ASSERT_EQUAL_STRING("", get_source_label(DATA_SOURCE_EMPTY));
}

void test_get_source_data_should_format_battery(void) {
  char buf[16];
  int percent = 0;

  s_battery_level = 85;
  get_source_data(DATA_SOURCE_BATTERY, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("85%", buf);
  TEST_ASSERT_EQUAL_INT(85, percent);

  // Edge values — format stays unpadded
  s_battery_level = 0;
  get_source_data(DATA_SOURCE_BATTERY, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("0%", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);

  s_battery_level = 100;
  get_source_data(DATA_SOURCE_BATTERY, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("100%", buf);
  TEST_ASSERT_EQUAL_INT(100, percent);
}

void test_get_source_data_should_format_steps(void) {
  char buf[16];
  int percent = 0;

  s_step_goal = 10000;

  // No data
  s_step_count = -1;
  get_source_data(DATA_SOURCE_STEPS, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("--", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);

  // Normal steps
  s_step_count = 5000;
  get_source_data(DATA_SOURCE_STEPS, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("5000", buf);
  TEST_ASSERT_EQUAL_INT(50, percent);

  // > 10k steps format
  s_step_count = 12500;
  get_source_data(DATA_SOURCE_STEPS, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("12.5k", buf);
  TEST_ASSERT_EQUAL_INT(100, percent);  // capped at 100%
}

void test_get_source_data_should_format_weather(void) {
  char buf[32];

  // No data
  s_weather_temp = -999;
  strcpy(s_weather_cond, "--");
  get_source_data(DATA_SOURCE_WEATHER, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("-- / --", buf);

  // Imperial
  s_settings_units = 0;
  s_weather_temp = 72;
  strcpy(s_weather_cond, "SUN");
  get_source_data(DATA_SOURCE_WEATHER, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("SUN / 72F", buf);

  // Metric
  s_settings_units = 1;
  s_weather_temp = 22;
  strcpy(s_weather_cond, "CLD");
  get_source_data(DATA_SOURCE_WEATHER, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("CLD / 22C", buf);
}

void test_get_source_data_should_format_sleep(void) {
  char buf[16];
  int percent = 0;

  // No data
  s_sleep_seconds = -1;
  get_source_data(DATA_SOURCE_SLEEP, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("--", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);

  // Normal sleep (7h 30m)
  s_sleep_seconds = (7 * 3600) + (30 * 60);
  get_source_data(DATA_SOURCE_SLEEP, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("7h 30m", buf);
  TEST_ASSERT_EQUAL_INT((s_sleep_seconds * 100) / 28800, percent);

  // Over goal
  s_sleep_seconds = 10 * 3600;  // 10 hours
  get_source_data(DATA_SOURCE_SLEEP, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_INT(100, percent);  // capped at 100%
}

void test_get_source_data_should_format_weather_temp_and_cond(void) {
  char buf[16];

  // Temp no data
  s_weather_temp = -999;
  get_source_data(DATA_SOURCE_WEATHER_TEMP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("--", buf);

  // Temp Imperial
  s_settings_units = 0;
  s_weather_temp = 68;
  get_source_data(DATA_SOURCE_WEATHER_TEMP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("68F", buf);

  // Temp Metric
  s_settings_units = 1;
  s_weather_temp = 20;
  get_source_data(DATA_SOURCE_WEATHER_TEMP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("20C", buf);

  // Cond
  strcpy(s_weather_cond, "RAIN");
  get_source_data(DATA_SOURCE_WEATHER_COND, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("RAIN", buf);
}

void test_get_source_data_should_format_heart_rate(void) {
  char buf[16];

  s_heart_rate = 0;
  get_source_data(DATA_SOURCE_HEART_RATE, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("--", buf);

  s_heart_rate = 120;
  get_source_data(DATA_SOURCE_HEART_RATE, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("120", buf);
}

void test_get_source_data_should_format_date_and_day(void) {
  char buf[16];

  s_date_day = 15;
  get_source_data(DATA_SOURCE_DATE, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("15", buf);
}

void test_get_source_data_should_format_bluetooth(void) {
  char buf[16];
  int percent = 0;

  s_connected = true;
  get_source_data(DATA_SOURCE_BLUETOOTH, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("OK", buf);
  TEST_ASSERT_EQUAL_INT(100, percent);

  s_connected = false;
  get_source_data(DATA_SOURCE_BLUETOOTH, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("LOSS", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);
}

void test_get_source_data_should_format_active_minutes(void) {
  char buf[16];
  int percent = 0;

  s_active_minutes_goal = 30;

  s_active_minutes = 15;
  get_source_data(DATA_SOURCE_ACTIVE_MINUTES, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("15m", buf);
  TEST_ASSERT_EQUAL_INT(50, percent);

  s_active_minutes = 45;
  get_source_data(DATA_SOURCE_ACTIVE_MINUTES, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("45m", buf);
  TEST_ASSERT_EQUAL_INT(100, percent);
}

void test_determine_theme_should_handle_all_configurations(void) {
  // Theme 1 = Day
  TEST_ASSERT_EQUAL_PTR(&s_theme_day, determine_theme(1, 0));
  TEST_ASSERT_EQUAL_PTR(&s_theme_day, determine_theme(1, 12));

  // Theme 2 = Night
  TEST_ASSERT_EQUAL_PTR(&s_theme_night, determine_theme(2, 0));
  TEST_ASSERT_EQUAL_PTR(&s_theme_night, determine_theme(2, 12));

  // Theme 0 = Auto
  TEST_ASSERT_EQUAL_PTR(&s_theme_night, determine_theme(0, 0));   // Midnight
  TEST_ASSERT_EQUAL_PTR(&s_theme_night, determine_theme(0, 5));   // 5 AM
  TEST_ASSERT_EQUAL_PTR(&s_theme_day, determine_theme(0, 6));     // 6 AM
  TEST_ASSERT_EQUAL_PTR(&s_theme_day, determine_theme(0, 12));    // Noon
  TEST_ASSERT_EQUAL_PTR(&s_theme_day, determine_theme(0, 17));    // 5 PM
  TEST_ASSERT_EQUAL_PTR(&s_theme_night, determine_theme(0, 18));  // 6 PM
  TEST_ASSERT_EQUAL_PTR(&s_theme_night, determine_theme(0, 23));  // 11 PM
}

void test_format_date_string_should_handle_all_configurations(void) {
  char buf[64];
  struct tm tick_time;
  memset(&tick_time, 0, sizeof(tick_time));
  tick_time.tm_mday = 9;
  tick_time.tm_mon = 5;     // June (0-indexed)
  tick_time.tm_year = 126;  // 2026
  tick_time.tm_wday = 2;    // Tuesday

  // Format 0: Weekday + ISO
  format_date_string(0, &tick_time, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("TUE 2026-06-09", buf);

  // Format 1: ISO + Weekday
  format_date_string(1, &tick_time, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("2026-06-09 TUE", buf);

  // Format 2: Full Text
  format_date_string(2, &tick_time, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("TUE JUNE 9th, 2026", buf);
}

void test_get_source_data_should_format_aqi_and_uv(void) {
  char buf[16];

  // AQI formatting
  s_weather_aqi = -1;
  get_source_data(DATA_SOURCE_AQI, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("--", buf);

  s_weather_aqi = 42;
  get_source_data(DATA_SOURCE_AQI, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("42", buf);

  // UV formatting
  s_weather_uv = -1;
  get_source_data(DATA_SOURCE_UV, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("--", buf);

  s_weather_uv = 5;
  get_source_data(DATA_SOURCE_UV, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("5", buf);

  // Combined AQI / UV formatting
  s_weather_aqi = -1;
  s_weather_uv = -1;
  get_source_data(DATA_SOURCE_AQI_UV, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("-- / --", buf);

  s_weather_aqi = 42;
  s_weather_uv = 5;
  get_source_data(DATA_SOURCE_AQI_UV, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("42 / 5", buf);
}

void test_get_source_color_should_return_appropriate_colors(void) {
  s_active_theme = &s_theme_day;

  // Weather Temp color severity (Imperial: >85 red, <40 blue)
  s_settings_units = 0;
  s_weather_temp = 70;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.text_primary, get_source_color(DATA_SOURCE_WEATHER_TEMP));
  s_weather_temp = 90;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.status_red, get_source_color(DATA_SOURCE_WEATHER_TEMP));
  s_weather_temp = 35;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.steps_fill, get_source_color(DATA_SOURCE_WEATHER_TEMP));

  // Weather Temp color severity (Metric: >29 red, <4 blue)
  s_settings_units = 1;
  s_weather_temp = 20;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.text_primary, get_source_color(DATA_SOURCE_WEATHER_TEMP));
  s_weather_temp = 30;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.status_red, get_source_color(DATA_SOURCE_WEATHER_TEMP));
  s_weather_temp = 2;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.steps_fill, get_source_color(DATA_SOURCE_WEATHER_TEMP));

  // AQI color severity levels (0-50 green, 51-100 yellow, >100 red)
  s_weather_aqi = -1;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.text_primary, get_source_color(DATA_SOURCE_AQI));

  s_weather_aqi = 34;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.status_green, get_source_color(DATA_SOURCE_AQI));

  s_weather_aqi = 65;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.status_yellow, get_source_color(DATA_SOURCE_AQI));

  s_weather_aqi = 150;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.status_red, get_source_color(DATA_SOURCE_AQI));

  // UV color severity levels (0-2 green, 3-5 yellow, >=6 red)
  s_weather_uv = -1;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.text_primary, get_source_color(DATA_SOURCE_UV));

  s_weather_uv = 1;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.status_green, get_source_color(DATA_SOURCE_UV));

  s_weather_uv = 4;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.status_yellow, get_source_color(DATA_SOURCE_UV));

  s_weather_uv = 8;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.status_red, get_source_color(DATA_SOURCE_UV));

  // AQI / UV Combined severity
  s_weather_aqi = 34;
  s_weather_uv = 1;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.status_green, get_source_color(DATA_SOURCE_AQI_UV));

  s_weather_aqi = 65;  // yellow
  s_weather_uv = 1;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.status_yellow, get_source_color(DATA_SOURCE_AQI_UV));

  s_weather_aqi = 34;
  s_weather_uv = 8;  // red
  TEST_ASSERT_EQUAL_HEX(s_theme_day.status_red, get_source_color(DATA_SOURCE_AQI_UV));

  // Battery color thresholds (>50 green, >20 yellow, <=20 red)
  s_battery_level = 100;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.status_green, get_source_color(DATA_SOURCE_BATTERY));

  s_battery_level = 51;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.status_green, get_source_color(DATA_SOURCE_BATTERY));

  s_battery_level = 50;  // boundary: yellow
  TEST_ASSERT_EQUAL_HEX(s_theme_day.status_yellow, get_source_color(DATA_SOURCE_BATTERY));

  s_battery_level = 21;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.status_yellow, get_source_color(DATA_SOURCE_BATTERY));

  s_battery_level = 20;  // boundary: red
  TEST_ASSERT_EQUAL_HEX(s_theme_day.status_red, get_source_color(DATA_SOURCE_BATTERY));

  s_battery_level = 0;
  TEST_ASSERT_EQUAL_HEX(s_theme_day.status_red, get_source_color(DATA_SOURCE_BATTERY));
}

void test_weather_cache_should_round_trip_when_fresh(void) {
  mock_persist_reset();

  s_weather_temp = 72;
  strcpy(s_weather_cond, "SUN");
  s_weather_aqi = 42;
  s_weather_uv = 5;
  save_weather_cache();

  // Simulate a relaunch: globals reset to sentinels
  s_weather_temp = -999;
  strcpy(s_weather_cond, "--");
  s_weather_aqi = -1;
  s_weather_uv = -1;

  TEST_ASSERT_TRUE(load_weather_cache());
  TEST_ASSERT_EQUAL_INT(72, s_weather_temp);
  TEST_ASSERT_EQUAL_STRING("SUN", s_weather_cond);
  TEST_ASSERT_EQUAL_INT(42, s_weather_aqi);
  TEST_ASSERT_EQUAL_INT(5, s_weather_uv);
}

void test_weather_cache_should_reject_missing_or_stale_data(void) {
  mock_persist_reset();

  // Nothing persisted yet
  TEST_ASSERT_FALSE(load_weather_cache());
  TEST_ASSERT_EQUAL_INT(-999, s_weather_temp);

  // Persist, then age the timestamp past the 30-minute window
  s_weather_temp = 72;
  strcpy(s_weather_cond, "SUN");
  save_weather_cache();
  persist_write_int(PERSIST_KEY_WEATHER_TIMESTAMP,
                    (int32_t)time(NULL) - (WEATHER_CACHE_MAX_AGE_S + 1));

  s_weather_temp = -999;
  strcpy(s_weather_cond, "--");
  TEST_ASSERT_FALSE(load_weather_cache());
  TEST_ASSERT_EQUAL_INT(-999, s_weather_temp);
  TEST_ASSERT_EQUAL_STRING("--", s_weather_cond);

  // A timestamp from the future (clock change) is also rejected
  persist_write_int(PERSIST_KEY_WEATHER_TIMESTAMP, (int32_t)time(NULL) + 3600);
  TEST_ASSERT_FALSE(load_weather_cache());
}

void test_weather_cache_should_keep_values_at_edge_of_window(void) {
  mock_persist_reset();

  s_weather_temp = 18;
  strcpy(s_weather_cond, "RAIN");
  s_weather_aqi = 12;
  s_weather_uv = 2;
  save_weather_cache();
  // Just inside the freshness window
  persist_write_int(PERSIST_KEY_WEATHER_TIMESTAMP,
                    (int32_t)time(NULL) - (WEATHER_CACHE_MAX_AGE_S - 5));

  s_weather_temp = -999;
  TEST_ASSERT_TRUE(load_weather_cache());
  TEST_ASSERT_EQUAL_INT(18, s_weather_temp);
  TEST_ASSERT_EQUAL_STRING("RAIN", s_weather_cond);
}

void test_settings_should_round_trip_through_persistence(void) {
  mock_persist_reset();

  s_settings_theme = 2;        // Night
  s_settings_units = 1;        // Metric
  s_settings_date_format = 2;  // Full text
  s_complication_slots[0].source = DATA_SOURCE_AQI;
  s_complication_slots[4].source = DATA_SOURCE_UV;

  // Persist exactly as inbox_received_callback does, via the dedicated keys.
  persist_write_int(PERSIST_KEY_SETTINGS_THEME, s_settings_theme);
  persist_write_int(PERSIST_KEY_SETTINGS_UNITS, s_settings_units);
  persist_write_int(PERSIST_KEY_SETTINGS_DATE_FORMAT, s_settings_date_format);
  persist_write_int(PERSIST_KEY_SLOT_1, s_complication_slots[0].source);
  persist_write_int(PERSIST_KEY_SLOT_5, s_complication_slots[4].source);

  // Simulate a relaunch: globals reset to defaults
  s_settings_theme = 0;
  s_settings_units = 0;
  s_settings_date_format = 0;
  s_complication_slots[0].source = DATA_SOURCE_EMPTY;
  s_complication_slots[4].source = DATA_SOURCE_EMPTY;

  load_settings();
  TEST_ASSERT_EQUAL_INT(2, s_settings_theme);
  TEST_ASSERT_EQUAL_INT(1, s_settings_units);
  TEST_ASSERT_EQUAL_INT(2, s_settings_date_format);
  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_AQI, s_complication_slots[0].source);
  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_UV, s_complication_slots[4].source);
}

void test_settings_persistence_is_decoupled_from_message_key_ids(void) {
  // Settings must load from their own PERSIST_KEY_* constants, never from the
  // auto-generated MESSAGE_KEY_* ids. This guards against the old shortcut
  // where reordering package.json's messageKeys would scramble saved data.
  mock_persist_reset();

  // Real saved values, under the dedicated persist keys.
  persist_write_int(PERSIST_KEY_SETTINGS_THEME, 2);
  persist_write_int(PERSIST_KEY_SLOT_1, DATA_SOURCE_AQI);

  // Decoys under the message-key ids — load_settings must ignore these.
  persist_write_int(MESSAGE_KEY_SETTINGS_THEME, 99);
  persist_write_int(MESSAGE_KEY_SLOT_1, DATA_SOURCE_BATTERY);

  s_settings_theme = 0;
  s_complication_slots[0].source = DATA_SOURCE_EMPTY;

  load_settings();
  TEST_ASSERT_EQUAL_INT(2, s_settings_theme);
  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_AQI, s_complication_slots[0].source);
}

void test_update_health_info_should_read_heart_rate(void) {
  // The mock reports HR inaccessible for range queries (like real firmware),
  // so this passing proves update_health_info uses an instant query.
  mock_heart_rate = 72;
  update_health_info();
  TEST_ASSERT_EQUAL_INT(72, s_heart_rate);

  char buf[16];
  get_source_data(DATA_SOURCE_HEART_RATE, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("72", buf);

  // No recent reading: 0 renders as the no-data state
  mock_heart_rate = 0;
  update_health_info();
  TEST_ASSERT_EQUAL_INT(0, s_heart_rate);
  get_source_data(DATA_SOURCE_HEART_RATE, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("--", buf);
}

void test_handle_bluetooth_should_vibrate_only_on_disconnect_transition(void) {
  mock_vibes_count = 0;

  s_connected = true;
  handle_bluetooth(false);  // genuine drop: buzz
  TEST_ASSERT_EQUAL_INT(1, mock_vibes_count);
  TEST_ASSERT_FALSE(s_connected);

  handle_bluetooth(false);  // still disconnected (relaunch-while-away): silent
  TEST_ASSERT_EQUAL_INT(1, mock_vibes_count);

  handle_bluetooth(true);  // reconnect: silent
  TEST_ASSERT_EQUAL_INT(1, mock_vibes_count);
  TEST_ASSERT_TRUE(s_connected);

  handle_bluetooth(false);  // second genuine drop: buzz again
  TEST_ASSERT_EQUAL_INT(2, mock_vibes_count);
}

// update_time() (called at the end of inbox_received_callback) requests
// weather itself when the wall-clock minute is :00 or :30; account for that
// so the outbox assertions don't flake twice an hour.
static int expected_tick_fetches(void) {
  time_t now = time(NULL);
  struct tm* t = localtime(&now);
  return (t->tm_min % 30 == 0) ? 1 : 0;
}

void test_inbox_should_parse_weather_payload_and_persist(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_cstring(MESSAGE_KEY_WEATHER_COND, "SUN");
  mock_dict_add_int(MESSAGE_KEY_WEATHER_AQI, 42);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_UV, 7);

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(72, s_weather_temp);
  TEST_ASSERT_EQUAL_STRING("SUN", s_weather_cond);
  TEST_ASSERT_EQUAL_INT(42, s_weather_aqi);
  TEST_ASSERT_EQUAL_INT(7, s_weather_uv);

  // A weather payload must persist the cache
  TEST_ASSERT_TRUE(persist_exists(PERSIST_KEY_WEATHER_TIMESTAMP));
  s_weather_temp = -999;
  s_weather_aqi = -1;
  TEST_ASSERT_TRUE(load_weather_cache());
  TEST_ASSERT_EQUAL_INT(72, s_weather_temp);
  TEST_ASSERT_EQUAL_INT(42, s_weather_aqi);
}

void test_inbox_settings_only_message_should_not_stamp_weather_cache(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_THEME, "2");

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(2, s_settings_theme);
  TEST_ASSERT_EQUAL_INT(2, persist_read_int(PERSIST_KEY_SETTINGS_THEME));
  TEST_ASSERT_FALSE(persist_exists(PERSIST_KEY_WEATHER_TIMESTAMP));
}

void test_inbox_should_parse_slot_assignments(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SLOT_1, "16");  // Clay sends strings
  mock_dict_add_int(MESSAGE_KEY_SLOT_5, 17);

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_AQI, s_complication_slots[0].source);
  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_UV, s_complication_slots[4].source);
  TEST_ASSERT_EQUAL_INT(16, persist_read_int(PERSIST_KEY_SLOT_1));
  TEST_ASSERT_EQUAL_INT(17, persist_read_int(PERSIST_KEY_SLOT_5));

  // Restore defaults so later tests see the boot layout
  s_complication_slots[0].source = DATA_SOURCE_WEATHER;
  s_complication_slots[4].source = DATA_SOURCE_BLUETOOTH;
}

void test_inbox_units_change_should_trigger_weather_refetch(void) {
  mock_persist_reset();

  // Imperial -> Metric: expect one refetch so temps arrive in the new unit
  s_settings_units = 0;
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_UNITS, "1");
  int before = mock_outbox_sends;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(1, s_settings_units);
  TEST_ASSERT_EQUAL_INT(before + 1 + expected_tick_fetches(), mock_outbox_sends);

  // Same units again: no refetch
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_UNITS, "1");
  before = mock_outbox_sends;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(before + expected_tick_fetches(), mock_outbox_sends);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_to_upper_str_should_convert_lowercase_to_uppercase);
  RUN_TEST(test_tuple_get_int_should_parse_strings_and_ints);
  RUN_TEST(test_get_source_label_should_return_correct_labels);
  RUN_TEST(test_get_source_data_should_format_battery);
  RUN_TEST(test_get_source_data_should_format_steps);
  RUN_TEST(test_get_source_data_should_format_weather);

  RUN_TEST(test_get_source_data_should_format_sleep);
  RUN_TEST(test_get_source_data_should_format_weather_temp_and_cond);
  RUN_TEST(test_get_source_data_should_format_heart_rate);
  RUN_TEST(test_get_source_data_should_format_date_and_day);
  RUN_TEST(test_get_source_data_should_format_bluetooth);
  RUN_TEST(test_get_source_data_should_format_active_minutes);
  RUN_TEST(test_get_source_data_should_format_aqi_and_uv);
  RUN_TEST(test_get_source_color_should_return_appropriate_colors);
  RUN_TEST(test_determine_theme_should_handle_all_configurations);
  RUN_TEST(test_format_date_string_should_handle_all_configurations);
  RUN_TEST(test_weather_cache_should_round_trip_when_fresh);
  RUN_TEST(test_weather_cache_should_reject_missing_or_stale_data);
  RUN_TEST(test_weather_cache_should_keep_values_at_edge_of_window);
  RUN_TEST(test_settings_should_round_trip_through_persistence);
  RUN_TEST(test_settings_persistence_is_decoupled_from_message_key_ids);
  RUN_TEST(test_update_health_info_should_read_heart_rate);
  RUN_TEST(test_handle_bluetooth_should_vibrate_only_on_disconnect_transition);
  RUN_TEST(test_inbox_should_parse_weather_payload_and_persist);
  RUN_TEST(test_inbox_settings_only_message_should_not_stamp_weather_cache);
  RUN_TEST(test_inbox_should_parse_slot_assignments);
  RUN_TEST(test_inbox_units_change_should_trigger_weather_refetch);
  return UNITY_END();
}
