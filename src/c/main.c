#include <pebble.h>
#include "main.h"
#include "data.h"
#include "theme.h"
#include "drawing.h"
#include "messaging.h"

// Two baked sizes of the same VGA 8x16 bitmap TTF, loaded once at init.
static GFont s_vga_16;
static GFont s_vga_64;

// What the clock layer currently says (guards the unconditional
// text_layer_set_text dirty-mark, below). Cleared on window load.
static char s_shown_time[8] = "";

GFont vga_font_16(void) {
  return s_vga_16;
}
GFont vga_font_64(void) {
  return s_vga_64;
}

// -----------------------------------------------------------------------------
// Data Updaters
// -----------------------------------------------------------------------------

static bool any_slot_is_one_of(const ComplicationDataSource* sources, int count) {
  for (int i = 0; i < NUM_SLOTS; i++) {
    for (int j = 0; j < count; j++) {
      if (s_complication_slots[i].source == sources[j]) return true;
    }
  }
  return false;
}

bool any_slot_needs_weather(void) {
  return any_slot_is_one_of(
      (ComplicationDataSource[]){DATA_SOURCE_WEATHER, DATA_SOURCE_WEATHER_TEMP,
                                 DATA_SOURCE_WEATHER_COND, DATA_SOURCE_AQI, DATA_SOURCE_UV,
                                 DATA_SOURCE_AQI_UV, DATA_SOURCE_HUMIDITY, DATA_SOURCE_WEATHER_FULL,
                                 DATA_SOURCE_WEATHER_PCP, DATA_SOURCE_TEMP_HIGH_LOW},
      10);
}

static void update_health_info() {
#if defined(PBL_HEALTH)
  time_t start = time_start_of_today();
  time_t end = time(NULL);

  // Each read is a real syscall; skip metrics nothing displays. Values fall
  // back to their sentinels so a later slot assignment never shows stale
  // data — the tick that follows the settings push refills them.
  if (any_slot_is_one_of((ComplicationDataSource[]){DATA_SOURCE_STEPS, DATA_SOURCE_STEPS_BAR}, 2)) {
    HealthServiceAccessibilityMask step_mask =
        health_service_metric_accessible(HealthMetricStepCount, start, end);
    if (step_mask & HealthServiceAccessibilityMaskAvailable) {
      s_step_count = (int)health_service_sum_today(HealthMetricStepCount);
      s_step_goal = 10000;
    } else {
      s_step_count = -1;
    }
  } else {
    s_step_count = -1;
  }

  if (any_slot_is_one_of((ComplicationDataSource[]){DATA_SOURCE_SLEEP}, 1)) {
    HealthServiceAccessibilityMask sleep_mask =
        health_service_metric_accessible(HealthMetricSleepSeconds, start, end);
    if (sleep_mask & HealthServiceAccessibilityMaskAvailable) {
      s_sleep_seconds = (int)health_service_sum_today(HealthMetricSleepSeconds);
    } else {
      s_sleep_seconds = -1;
    }
  } else {
    s_sleep_seconds = -1;
  }

  if (any_slot_is_one_of((ComplicationDataSource[]){DATA_SOURCE_ACTIVE_MINUTES}, 1)) {
    HealthServiceAccessibilityMask active_mask =
        health_service_metric_accessible(HealthMetricActiveSeconds, start, end);
    if (active_mask & HealthServiceAccessibilityMaskAvailable) {
      s_active_minutes = (int)(health_service_sum_today(HealthMetricActiveSeconds) / 60);
    } else {
      s_active_minutes = 0;
    }
  } else {
    s_active_minutes = 0;
  }

  // Heart rate accessibility must be checked at an instant, not over a day
  // range — the range form reports the metric unavailable and BPM never shows.
  if (any_slot_is_one_of((ComplicationDataSource[]){DATA_SOURCE_HEART_RATE}, 1)) {
    HealthServiceAccessibilityMask hr_mask =
        health_service_metric_accessible(HealthMetricHeartRateBPM, end, end);
    if (hr_mask & HealthServiceAccessibilityMaskAvailable) {
      HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
      s_heart_rate = (int)hr;
    } else {
      s_heart_rate = 0;
    }
  } else {
    s_heart_rate = 0;
  }
#else
  s_step_count = -1;
  s_sleep_seconds = -1;
  s_heart_rate = 0;
  s_active_minutes = 0;
#endif
}

void update_time() {
  time_t temp = time(NULL);
  struct tm* tick_time = localtime(&temp);

  apply_theme(tick_time);

  // The theme can change while the face is open (Auto crossing 06:00/14:00/22:00,
  // or a settings push); the time layer keeps its load-time color unless
  // re-applied here. The date is canvas-drawn, so it follows on redraw.
  if (s_time_layer) text_layer_set_text_color(s_time_layer, s_active_theme->text_primary);

  s_beats = compute_beats(temp);

  static char s_time_buffer[8];
  strftime(s_time_buffer, sizeof(s_time_buffer), clock_is_24h_style() ? "%H:%M" : "%I:%M",
           tick_time);

  char* time_str = s_time_buffer;
  if (!clock_is_24h_style() && s_time_buffer[0] == '0') {
    time_str++;  // strip leading zero
  }
  // Guards the unconditional dirty-mark in text_layer_set_text: an unchanged
  // string here would schedule a render and defeat the gate on every inbox
  // message. On a MINUTE_UNIT tick the string always differs — the 1440/day
  // floor is unaffected. s_shown_time is file-scope (see main_window_load).
  if (strcmp(time_str, s_shown_time) != 0) {
    strncpy(s_shown_time, time_str, sizeof(s_shown_time) - 1);
    text_layer_set_text(s_time_layer, time_str);
  }

  // The date changes at midnight and on settings pushes only; reformat then,
  // not per tick. Consecutive days never share a tm_yday.
  static int s_fmt_yday = -1;
  static int s_fmt_format = -1;
  static int s_fmt_short = -1;
  static int s_fmt_dow = -1;
  if (tick_time->tm_yday != s_fmt_yday || s_settings_date_format != s_fmt_format ||
      s_settings_short_date_format != s_fmt_short || s_settings_dow_position != s_fmt_dow) {
    format_date_string(s_settings_date_format, s_settings_short_date_format,
                       s_settings_dow_position, tick_time, s_date_display, sizeof(s_date_display));
    format_short_date_string(s_settings_short_date_format, s_settings_dow_position, tick_time,
                             s_short_date_display, sizeof(s_short_date_display));
    s_date_day = tick_time->tm_mday;
    s_fmt_yday = tick_time->tm_yday;
    s_fmt_format = s_settings_date_format;
    s_fmt_short = s_settings_short_date_format;
    s_fmt_dow = s_settings_dow_position;
  }

  update_health_info();
  request_ui_redraw();
}

static void tick_handler(struct tm* tick_time, TimeUnits units_changed) {
  update_time();
  // Fetch on the tick edge only. update_time() also runs from
  // inbox_received_callback; triggering there too re-armed the fetch on
  // every reply for the whole of minutes :00/:30.
  if (tick_time->tm_min % 30 == 0 && any_slot_needs_weather()) {
    request_weather();
  }
}

// A weather request that loses the race with the phone's JS runtime is
// dropped silently, and after Task 11 nothing on the phone side fetches on
// its own. Bounded retry, reset on the first successful send. The outbox only
// ever carries the weather trigger, so no message discrimination is needed.
#define WEATHER_REQUEST_RETRY_MS 5000
#define WEATHER_REQUEST_MAX_RETRIES 2

static int s_weather_request_retries = 0;

static void weather_retry_callback(void* data) {
  request_weather();
}

static void outbox_sent_callback(DictionaryIterator* iterator, void* context) {
  s_weather_request_retries = 0;
}

static void outbox_failed_callback(DictionaryIterator* iterator, AppMessageResult reason,
                                   void* context) {
  if (s_weather_request_retries >= WEATHER_REQUEST_MAX_RETRIES) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Weather request failed (%d); retries exhausted", (int)reason);
    return;
  }
  s_weather_request_retries++;
  // One-shot timers free themselves on firing, so the handle is deliberately
  // discarded. A second failure inside the window arms a second timer and
  // both fire — the counter bounds the total sends anyway. A NULL return
  // (timer pool exhausted) silently drops this retry — acceptable at the
  // weather-request failure rate.
  app_timer_register(WEATHER_REQUEST_RETRY_MS, weather_retry_callback, NULL);
}

static void battery_callback(BatteryChargeState state) {
  s_battery_level = state.charge_percent;
  request_ui_redraw();
}

static void handle_bluetooth(bool connected) {
  bool was_connected = s_connected;
  s_connected = connected;
  request_ui_redraw();
  // Buzz only on a genuine drop, not when launched already-disconnected —
  // otherwise every return to the watchface vibrates while the phone is away.
  if (was_connected && !connected) {
    vibes_double_pulse();
  }
}

#if defined(PBL_HEALTH)
// PebbleOS posts MovementUpdate per accel batch, with no rate limit, for as
// long as the wearer keeps moving; SleepUpdate rides the same accel-driven
// cadence. With a step- or sleep-bearing slot visible every one of them
// changed the snapshot, so the face rendered several times a second while
// walking. Timestamp-based leading-edge throttle (no app_timer): the 60 s
// tick stays the freshness floor, and a dropped event's data lands at the
// next in-window event or tick.
#define HEALTH_EVENT_THROTTLE_S 5
static time_t s_last_throttled_health_refresh;

static void health_handler(HealthEventType event, void* context) {
  // SignificantUpdate = the applib health cache was invalidated (day
  // rollover, subscribe); rare, load-bearing, never throttled. HeartRateUpdate
  // bypasses the throttle too: a posted reading is already fresh, and stale
  // BPM is worse than a redraw (see ISSUES.md).
  if (event == HealthEventHeartRateUpdate || event == HealthEventSignificantUpdate) {
    update_health_info();
    request_ui_redraw();
    return;
  }
  if (event == HealthEventMovementUpdate || event == HealthEventSleepUpdate) {
    time_t now = time(NULL);
    if (now - s_last_throttled_health_refresh < HEALTH_EVENT_THROTTLE_S) return;
    s_last_throttled_health_refresh = now;
    update_health_info();
    request_ui_redraw();
  }
}
#endif

// -----------------------------------------------------------------------------
// Window Management
// -----------------------------------------------------------------------------

// Timeline Quick View: honest occlusion. While the system overlay is up the
// face stops drawing what it covers (the bottom slot row; drawing.c) instead
// of reflowing the layout. will_change fires too early for show/hide
// decisions, so only did_change is wired.
static void quick_view_did_change(void* context) {
  Layer* root_layer = window_get_root_layer(s_main_window);
  GRect full = layer_get_bounds(root_layer);
  GRect unobstructed = layer_get_unobstructed_bounds(root_layer);
  bool obstructed = !grect_equal(&full, &unobstructed);
  if (obstructed == s_quick_view_active) return;
  s_quick_view_active = obstructed;
  request_ui_redraw();
}

static void main_window_load(Window* window) {
  Layer* window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);

  s_time_layer = text_layer_create(GRect(LAYOUT_X + 4, 49, LAYOUT_W - 8, 64));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, s_active_theme->text_primary);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  text_layer_set_font(s_time_layer, vga_font_64());
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

  // Init text layers for slots
  for (int i = 0; i < NUM_SLOTS; i++) {
    ComplicationSlot* slot = &s_complication_slots[i];
    GRect text_rect = GRect(slot->box_rect.origin.x, slot->box_rect.origin.y + VALUE_ROW_DY,
                            slot->box_rect.size.w, VALUE_ROW_H);
    slot->layer = text_layer_create(text_rect);
    text_layer_set_background_color(slot->layer, GColorClear);
    text_layer_set_text_color(slot->layer, s_active_theme->text_primary);
    text_layer_set_text_alignment(slot->layer, GTextAlignmentCenter);
    text_layer_set_font(slot->layer, vga_font_16());
    layer_add_child(window_layer, text_layer_get_layer(slot->layer));
  }

  // Fresh layers hold no text yet: the next request_ui_redraw()/update_time()
  // must apply unconditionally, not match a previous layer tree's snapshot.
  reset_ui_snapshot();
  s_shown_time[0] = '\0';

  // Layers exist now: subscribe, and evaluate once so the face comes up
  // correct if Quick View is already up (e.g. a relaunch with the peek out).
  UnobstructedAreaHandlers handlers = {.did_change = quick_view_did_change};
  unobstructed_area_service_subscribe(handlers, NULL);
  quick_view_did_change(NULL);
}

static void main_window_unload(Window* window) {
  text_layer_destroy(s_time_layer);
  layer_destroy(s_canvas_layer);
  for (int i = 0; i < NUM_SLOTS; i++) {
    if (s_complication_slots[i].layer) {
      text_layer_destroy(s_complication_slots[i].layer);
    }
  }
}

static void init(void) {
  // Load settings from persistent storage
  load_settings();

  s_vga_16 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_VGA_16));
  s_vga_64 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_VGA_64));

  s_main_window = window_create();
  time_t now = time(NULL);
  apply_theme(localtime(&now));
  window_set_window_handlers(s_main_window, (WindowHandlers){
                                                .load = main_window_load,
                                                .unload = main_window_unload,
                                            });
  window_stack_push(s_main_window, true);

  // Subscriptions
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_callback);
  connection_service_subscribe(
      (ConnectionHandlers){.pebble_app_connection_handler = handle_bluetooth});
#if defined(PBL_HEALTH)
  health_service_events_subscribe(health_handler, NULL);
#endif

  // Initial states (seed connection state directly — no vibe on launch)
  battery_callback(battery_state_service_peek());
  s_connected = connection_service_peek_pebble_app_connection();

  // AppMessage setup
  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_open(256, 64);

  // Restore cached weather; hit the network only if the cache is stale and
  // something actually shows weather. && short-circuits left to right, so the
  // cache load always runs.
  if (!load_weather_cache() && any_slot_needs_weather()) {
    request_weather();
  }
  update_time();
}

static void deinit(void) {
  fonts_unload_custom_font(s_vga_16);
  fonts_unload_custom_font(s_vga_64);
  window_destroy(s_main_window);
}

#ifndef TEST_ENV
int main(void) {
  init();
  app_event_loop();
  deinit();
}
#endif
