#include <pebble.h>
#include "main.h"
#include "data.h"
#include "theme.h"
#include "drawing.h"
#include "messaging.h"

// The window, the canvas everything draws on, and the clock row — owned here,
// with lifecycle. drawing.c/the drawers read the handles via drawing.h/main.h.
Window* s_main_window = NULL;
Layer* s_canvas_layer = NULL;
TextLayer* s_time_layer = NULL;

// Two baked sizes of the same VGA 8x16 bitmap TTF, loaded once at init.
static GFont s_vga_16;
static GFont s_vga_64;

// What the clock layer currently says (guards the unconditional
// text_layer_set_text dirty-mark, below). Cleared on window load.
static char s_shown_time[8] = "";

// The date changes at midnight and on settings pushes only; reformat then,
// not per tick. Consecutive days never share a tm_yday. File scope so tests
// can clear the cache (reset_all_state reaches every file-scope static).
static int s_fmt_yday = -1;
static int s_fmt_format = -1;
static int s_fmt_short = -1;
static int s_fmt_dow = -1;

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
  for (int i = 0; i < NUM_SLOTS; i++) {
    const ComplicationSpec* spec = complication_spec(s_complication_slots[i].source);
    if (spec && spec->needs_weather) return true;
  }
  return false;
}

#if defined(PBL_HEALTH)
typedef enum {
  HEALTH_READ_RANGE_SUM,    // accessible + sum_today over [start of day, now]
  HEALTH_READ_INSTANT_PEEK  // HR is only accessible at an instant: the
                            // day-range form reports it unavailable and BPM
                            // never shows
} HealthReadMode;

// One row per health metric: the slots that make its syscall worthwhile, how
// it is read, and what stands in for no data — the divergences as data.
typedef struct {
  ComplicationDataSource watched[2];
  int watched_count;
  HealthMetric metric;
  int* target;
  int empty_value;
  HealthReadMode mode;
  int divisor;  // seconds → display unit (active minutes)
} HealthRead;

static const HealthRead s_health_reads[] = {
    {.watched = {DATA_SOURCE_STEPS, DATA_SOURCE_STEPS_BAR},
     .watched_count = 2,
     .metric = HealthMetricStepCount,
     .target = &s_step_count,
     .empty_value = -1,
     .mode = HEALTH_READ_RANGE_SUM,
     .divisor = 1},
    {.watched = {DATA_SOURCE_SLEEP},
     .watched_count = 1,
     .metric = HealthMetricSleepSeconds,
     .target = &s_sleep_seconds,
     .empty_value = -1,
     .mode = HEALTH_READ_RANGE_SUM,
     .divisor = 1},
    {.watched = {DATA_SOURCE_ACTIVE_MINUTES},
     .watched_count = 1,
     .metric = HealthMetricActiveSeconds,
     .target = &s_active_minutes,
     .empty_value = 0,
     .mode = HEALTH_READ_RANGE_SUM,
     .divisor = 60},
    {.watched = {DATA_SOURCE_HEART_RATE},
     .watched_count = 1,
     .metric = HealthMetricHeartRateBPM,
     .target = &s_heart_rate,
     .empty_value = 0,
     .mode = HEALTH_READ_INSTANT_PEEK,
     .divisor = 1},
};
#endif

static void update_health_info() {
#if defined(PBL_HEALTH)
  time_t start = time_start_of_today();
  time_t now = time(NULL);

  // Each read is a real syscall; skip metrics nothing displays. Values fall
  // back to their sentinels so a later slot assignment never shows stale
  // data — the tick that follows the settings push refills them.
  for (unsigned i = 0; i < sizeof(s_health_reads) / sizeof(s_health_reads[0]); i++) {
    const HealthRead* read = &s_health_reads[i];
    if (!any_slot_is_one_of(read->watched, read->watched_count)) {
      *read->target = read->empty_value;
      continue;
    }
    HealthServiceAccessibilityMask mask =
        read->mode == HEALTH_READ_INSTANT_PEEK
            ? health_service_metric_accessible(read->metric, now, now)
            : health_service_metric_accessible(read->metric, start, now);
    if (!(mask & HealthServiceAccessibilityMaskAvailable)) {
      *read->target = read->empty_value;
      continue;
    }
    int32_t value = read->mode == HEALTH_READ_INSTANT_PEEK
                        ? health_service_peek_current_value(read->metric)
                        : health_service_sum_today(read->metric);
    *read->target = (int)(value / read->divisor);
  }
#else
  s_step_count = -1;
  s_sleep_seconds = -1;
  s_heart_rate = 0;
  s_active_minutes = 0;
#endif
}

void refresh_state() {
  time_t temp = time(NULL);
  struct tm* tick_time = localtime(&temp);

  s_wall_hour = tick_time->tm_hour;

  apply_theme(tick_time);

  // The theme can change while the face is open (Auto crossing 06:00/14:00/22:00,
  // or a settings push); the window and time layer keep their load-time colors
  // unless re-applied here. Everything else is canvas-drawn and follows on redraw.
  if (s_main_window) window_set_background_color(s_main_window, s_active_theme->center_bg);
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
  (void)units_changed;
  s_quiet_time_active = quiet_time_is_active();
  refresh_state();
  // Fetch on the tick edge only. refresh_state() also runs from
  // inbox_received_callback; triggering there too re-armed the fetch on
  // every reply for the whole of minutes :00/:30.
  if (tick_time->tm_min % 30 == 0 && any_slot_needs_weather()) {
    request_weather();
  }
}

// A weather request that loses the race with the phone's JS runtime is
// dropped silently, and nothing on the phone side fetches on its own. Bounded
// retry, reset on the first successful send. The outbox only ever carries the
// weather trigger, so no message discrimination is needed.
#define WEATHER_REQUEST_RETRY_MS 5000
#define WEATHER_REQUEST_MAX_RETRIES 2

static int s_weather_request_retries = 0;

static void weather_retry_callback(void* data) {
  (void)data;
  request_weather();
}

static void outbox_sent_callback(DictionaryIterator* iterator, void* context) {
  (void)iterator;
  (void)context;
  s_weather_request_retries = 0;
}

static void outbox_failed_callback(DictionaryIterator* iterator, AppMessageResult reason,
                                   void* context) {
  (void)iterator;
  (void)context;
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
  s_battery_charging = state.is_charging;
  request_ui_redraw();
}

static void handle_bluetooth(bool connected) {
  bool was_connected = s_connected;
  s_connected = connected;
  request_ui_redraw();
  // Buzz only on a genuine drop, not when launched already-disconnected —
  // otherwise every return to the watchface vibrates while the phone is away.
  if (was_connected && !connected) {
    // The buzz is the dead-phone detector; the settings select is the consent.
    if (s_settings_disconnect_vibe) vibes_double_pulse();
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
  (void)context;
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
  (void)context;
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

  // Fresh layer tree: the next request_ui_redraw()/refresh_state() must apply
  // unconditionally, not match a previous layer tree's snapshot.
  reset_ui_snapshot();
  s_shown_time[0] = '\0';

  // Layers exist now: subscribe, and evaluate once so the face comes up
  // correct if Quick View is already up (e.g. a relaunch with the peek out).
  UnobstructedAreaHandlers handlers = {.did_change = quick_view_did_change};
  unobstructed_area_service_subscribe(handlers, NULL);
  quick_view_did_change(NULL);
}

static void main_window_unload(Window* window) {
  (void)window;
  text_layer_destroy(s_time_layer);
  layer_destroy(s_canvas_layer);
}

static void init(void) {
  load_settings();

  s_vga_16 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_VGA_16));
  s_vga_64 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_VGA_64));

  s_main_window = window_create();
  time_t now = time(NULL);
  apply_theme(localtime(&now));
  window_set_background_color(s_main_window, s_active_theme->center_bg);
  window_set_window_handlers(s_main_window, (WindowHandlers){
                                                .load = main_window_load,
                                                .unload = main_window_unload,
                                            });
  window_stack_push(s_main_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_callback);
  connection_service_subscribe(
      (ConnectionHandlers){.pebble_app_connection_handler = handle_bluetooth});
#if defined(PBL_HEALTH)
  health_service_events_subscribe(health_handler, NULL);
#endif

  // Peek the initial states — seeding through battery_callback is fine, but
  // handle_bluetooth on launch would buzz while the phone is merely away.
  battery_callback(battery_state_service_peek());
  s_connected = connection_service_peek_pebble_app_connection();
  s_quiet_time_active = quiet_time_is_active();

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
  refresh_state();
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
#else
// Single-TU tests compile main() out; init() is driven directly instead.
void (*const test_env_unused_lifecycle[])(void) = {init, deinit};
#endif
