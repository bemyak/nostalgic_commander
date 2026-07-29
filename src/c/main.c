#include <pebble.h>
#include "main.h"
#include "data.h"
#include "theme.h"
#include "drawing.h"
#include "messaging.h"

// Two baked sizes of the same VGA 8x16 bitmap TTF, loaded once at init.
static GFont s_vga_16;
static GFont s_vga_64;

GFont vga_font_16(void) {
  return s_vga_16;
}
GFont vga_font_64(void) {
  return s_vga_64;
}

// -----------------------------------------------------------------------------
// Data Updaters
// -----------------------------------------------------------------------------

static void update_date_info() {
  time_t temp = time(NULL);
  struct tm* tick_time = localtime(&temp);

  s_date_day = tick_time->tm_mday;
}

static void update_health_info() {
#if defined(PBL_HEALTH)
  time_t start = time_start_of_today();
  time_t end = time(NULL);

  HealthServiceAccessibilityMask step_mask =
      health_service_metric_accessible(HealthMetricStepCount, start, end);
  if (step_mask & HealthServiceAccessibilityMaskAvailable) {
    s_step_count = (int)health_service_sum_today(HealthMetricStepCount);
    s_step_goal = 10000;
  } else {
    s_step_count = -1;
  }

  HealthServiceAccessibilityMask sleep_mask =
      health_service_metric_accessible(HealthMetricSleepSeconds, start, end);
  if (sleep_mask & HealthServiceAccessibilityMaskAvailable) {
    s_sleep_seconds = (int)health_service_sum_today(HealthMetricSleepSeconds);
  } else {
    s_sleep_seconds = -1;
  }

  HealthServiceAccessibilityMask active_mask =
      health_service_metric_accessible(HealthMetricActiveSeconds, start, end);
  if (active_mask & HealthServiceAccessibilityMaskAvailable) {
    s_active_minutes = (int)(health_service_sum_today(HealthMetricActiveSeconds) / 60);
  } else {
    s_active_minutes = 0;
  }

  // Heart rate accessibility must be checked at an instant, not over a day
  // range — the range form reports the metric unavailable and BPM never shows.
  HealthServiceAccessibilityMask hr_mask =
      health_service_metric_accessible(HealthMetricHeartRateBPM, end, end);
  if (hr_mask & HealthServiceAccessibilityMaskAvailable) {
    HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
    s_heart_rate = (int)hr;
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
  apply_theme();

  // The theme can flip while the face is open (Auto mode at 06:00/18:00, or a
  // settings push); the time layer keeps its load-time color unless re-applied
  // here. The date is canvas-drawn, so it picks the new theme up on redraw.
  if (s_time_layer) text_layer_set_text_color(s_time_layer, s_active_theme->text_primary);

  time_t temp = time(NULL);
  struct tm* tick_time = localtime(&temp);
  s_beats = compute_beats(temp);

  static char s_time_buffer[8];
  strftime(s_time_buffer, sizeof(s_time_buffer), clock_is_24h_style() ? "%H:%M" : "%I:%M",
           tick_time);

  char* time_str = s_time_buffer;
  if (!clock_is_24h_style() && s_time_buffer[0] == '0') {
    time_str++;  // strip leading zero
  }
  text_layer_set_text(s_time_layer, time_str);

  format_date_string(s_settings_date_format, tick_time, s_date_display, sizeof(s_date_display));

  update_date_info();
  update_health_info();
  refresh_complications();
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);

  // Request weather update every 30 minutes
  if (tick_time->tm_min % 30 == 0) {
    request_weather();
  }
}

static void tick_handler(struct tm* tick_time, TimeUnits units_changed) {
  update_time();
}

static void battery_callback(BatteryChargeState state) {
  s_battery_level = state.charge_percent;
  refresh_complications();
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

static void handle_bluetooth(bool connected) {
  bool was_connected = s_connected;
  s_connected = connected;
  refresh_complications();
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
  // Buzz only on a genuine drop, not when launched already-disconnected —
  // otherwise every return to the watchface vibrates while the phone is away.
  if (was_connected && !connected) {
    vibes_double_pulse();
  }
}

#if defined(PBL_HEALTH)
static void health_handler(HealthEventType event, void* context) {
  if (event == HealthEventMovementUpdate || event == HealthEventHeartRateUpdate ||
      event == HealthEventSleepUpdate) {
    update_health_info();
    refresh_complications();
    if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
  }
}
#endif

// -----------------------------------------------------------------------------
// Window Management
// -----------------------------------------------------------------------------

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
  apply_theme();
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
  app_message_open(512, 512);

  // Restore cached weather; only hit the network if the cache is stale
  if (!load_weather_cache()) {
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
