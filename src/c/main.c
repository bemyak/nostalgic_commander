#include <pebble.h>
#include "main.h"
#include "data.h"
#include "complication.h"
#include "theme.h"
#include "drawing.h"
#include "messaging.h"
#include "crt.h"
#include "chime.h"

// The window, the canvas everything draws on, and the clock row — owned here,
// with lifecycle. drawing.c/the drawers read the handles via drawing.h/main.h.
Window* s_main_window = NULL;
Layer* s_canvas_layer = NULL;
TextLayer* s_time_layer = NULL;
Layer* s_crt_layer = NULL;

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

typedef enum {
  HEALTH_READ_RANGE_SUM,    // accessible + sum_today over [start of day, now]
  HEALTH_READ_INSTANT_PEEK  // HR is only accessible at an instant: the
                            // day-range form reports it unavailable and BPM
                            // never shows
} HealthReadMode;

// One row per health metric: how it is read and what stands in for no data
// — the divergences as data, and the sole runtime definition of the
// sentinels: every update pass starts from these empty values, health
// hardware or not. A metric is read while any visible slot's spec
// attributes it (complication.c's .health_metric); the watchful source list
// is the registry's, not this table's. `events` is the set of health events
// that refresh the metric: PebbleOS posts each kind on its own cadence, and
// the Sleep and ActiveSeconds reads walk 30 days of history, so an event
// pays only for the metrics it can have changed. SignificantUpdate and the
// minute tick refresh them all (HEALTH_REFRESH_ALL).
#define HEALTH_EVENT_BIT(event) (1u << (event))
#define HEALTH_REFRESH_ALL (~0u)

typedef struct {
  HealthMetric metric;
  int* target;
  int empty_value;
  HealthReadMode mode;
  int divisor;  // seconds → display unit (active minutes)
  unsigned events;
} HealthRead;

static const HealthRead s_health_reads[] = {
    {.metric = HealthMetricStepCount,
     .target = &s_step_count,
     .empty_value = -1,
     .mode = HEALTH_READ_RANGE_SUM,
     .divisor = 1,
     .events = HEALTH_EVENT_BIT(HealthEventMovementUpdate)},
    {.metric = HealthMetricSleepSeconds,
     .target = &s_sleep_seconds,
     .empty_value = -1,
     .mode = HEALTH_READ_RANGE_SUM,
     .divisor = 1,
     .events = HEALTH_EVENT_BIT(HealthEventSleepUpdate)},
    {.metric = HealthMetricActiveSeconds,
     .target = &s_active_minutes,
     .empty_value = -1,
     .mode = HEALTH_READ_RANGE_SUM,
     .divisor = 60,
     .events = HEALTH_EVENT_BIT(HealthEventMovementUpdate)},
    {.metric = HealthMetricHeartRateBPM,
     .target = &s_heart_rate,
     .empty_value = 0,
     .mode = HEALTH_READ_INSTANT_PEEK,
     .divisor = 1,
     .events = HEALTH_EVENT_BIT(HealthEventHeartRateUpdate)},
};

// Every update pass starts the metrics it refreshes from the table's empty
// values: a skipped or denied metric keeps its sentinel, a watched and
// accessible one gets overwritten. `trigger` is a HEALTH_EVENT_BIT or
// HEALTH_REFRESH_ALL.
static void reset_health_readings(unsigned trigger) {
  for (unsigned i = 0; i < sizeof(s_health_reads) / sizeof(s_health_reads[0]); i++) {
    if (s_health_reads[i].events & trigger) {
      *s_health_reads[i].target = s_health_reads[i].empty_value;
    }
  }
}

static void update_health_metrics(unsigned trigger) {
  reset_health_readings(trigger);
#if defined(PBL_HEALTH)
  time_t start = time_start_of_today();
  time_t now = time(NULL);

  // Each read is a real syscall; skip metrics nothing displays. Values fall
  // back to their sentinels so a later slot assignment never shows stale
  // data — the tick that follows the settings push refills them.
  for (unsigned i = 0; i < sizeof(s_health_reads) / sizeof(s_health_reads[0]); i++) {
    const HealthRead* read = &s_health_reads[i];
    if (!(read->events & trigger)) continue;
    if (!any_slot_monitors_health(read->metric)) continue;
    HealthServiceAccessibilityMask mask =
        read->mode == HEALTH_READ_INSTANT_PEEK
            ? health_service_metric_accessible(read->metric, now, now)
            : health_service_metric_accessible(read->metric, start, now);
    if (!(mask & HealthServiceAccessibilityMaskAvailable)) continue;
    int32_t value = read->mode == HEALTH_READ_INSTANT_PEEK
                        ? health_service_peek_current_value(read->metric)
                        : health_service_sum_today(read->metric);
    *read->target = (int)(value / read->divisor);
  }
#endif
}

static void update_health_info(void) {
  update_health_metrics(HEALTH_REFRESH_ALL);
}

static void update_solar_elevation(time_t now) {
  if (location_known()) s_solar_elevation = compute_solar_elevation(now, s_latitude, s_longitude);
}

void refresh_state(void) {
  time_t temp = time(NULL);
  struct tm* tick_time = localtime(&temp);

  s_wall_hour = tick_time->tm_hour;

  update_solar_elevation(temp);
  apply_theme();

  // A settings push can change the theme while the face is open; the window
  // and time layer keep their load-time colors unless re-applied here.
  // Everything else is canvas-drawn and follows on redraw.
  if (s_main_window) window_set_background_color(s_main_window, s_active_theme->center_bg);
  if (s_time_layer) text_layer_set_text_color(s_time_layer, s_active_theme->text_primary);

  s_beats = compute_beats(temp);
  s_solar_hour_angle = compute_solar_hour_angle(temp, s_longitude);
  s_week_number =
      iso_week_number(tick_time->tm_year + 1900, tick_time->tm_yday, tick_time->tm_wday);

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
  if (s_time_layer && strcmp(time_str, s_shown_time) != 0) {
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
  // Quiet Time has no OS event, so it is polled on the minute tick.
  s_quiet_time_active = quiet_time_is_active();
  weather_expire(time(NULL));
  refresh_state();
  // Fetch on the tick edge only. refresh_state() also runs from
  // inbox_received_callback; triggering there too re-armed the fetch on
  // every reply for the whole of minutes :00/:30.
  if (weather_fetch_due(tick_time->tm_min, any_slot_needs_weather())) {
    request_weather();
  }
  chime_on_tick(tick_time);
}

// A weather request that loses the race with the phone's JS runtime is
// dropped silently, and nothing on the phone side fetches on its own. Bounded
// retry: every fresh request and every successful send starts the budget
// over; only the retry timer spends from it. The outbox only ever carries the
// weather trigger, so no message discrimination is needed.
#define WEATHER_REQUEST_RETRY_MS 5000
#define WEATHER_REQUEST_MAX_RETRIES 2

static int s_weather_request_retries = 0;
static AppTimer* s_weather_retry_timer = NULL;

static void weather_retry_callback(void* data) {
  (void)data;
  s_weather_retry_timer = NULL;
  s_weather_request_retries++;
  request_weather_retry();
}

static void outbox_sent_callback(DictionaryIterator* iterator, void* context) {
  (void)iterator;
  (void)context;
  s_weather_request_retries = 0;
}

void weather_retries_reset(void) {
  s_weather_request_retries = 0;
  if (s_weather_retry_timer) {
    app_timer_cancel(s_weather_retry_timer);
    s_weather_retry_timer = NULL;
  }
}

static void outbox_failed_callback(DictionaryIterator* iterator, AppMessageResult reason,
                                   void* context) {
  (void)iterator;
  (void)context;
  if (s_weather_request_retries >= WEATHER_REQUEST_MAX_RETRIES) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Weather request failed (%d); retries exhausted", (int)reason);
    return;
  }
  // A second failure inside the window supersedes the armed retry. A NULL
  // return (timer pool exhausted) silently drops this retry — acceptable at
  // the weather-request failure rate.
  if (s_weather_retry_timer) app_timer_cancel(s_weather_retry_timer);
  s_weather_retry_timer =
      app_timer_register(WEATHER_REQUEST_RETRY_MS, weather_retry_callback, NULL);
}

static void battery_callback(BatteryChargeState state) {
  s_battery_level = state.charge_percent;
  s_battery_charging = state.is_charging;
  request_ui_redraw();
}

static time_t s_last_reconnect_request;

static void handle_bluetooth(bool connected) {
  s_connected = connected;
  request_ui_redraw();
  // Sends are skipped while the link is down, so a reconnect is the first
  // chance to catch up on a fetch that came due in the meantime.
  time_t now = time(NULL);
  if (connected && weather_fetch_on_reconnect(any_slot_needs_weather(), s_weather_stamp, now,
                                              s_last_reconnect_request)) {
    s_last_reconnect_request = now;
    request_weather();
  }
}

#if defined(PBL_HEALTH)
// PebbleOS posts MovementUpdate per accel batch, with no rate limit, for as
// long as the wearer keeps moving; SleepUpdate rides the same accel-driven
// cadence. With a step- or sleep-bearing slot visible every one of them
// changed the snapshot, so the face rendered several times a second while
// walking. Timestamp-based leading-edge throttle per event type (no
// app_timer): the 60 s tick stays the freshness floor, and a dropped event's
// data lands at the next in-window event or tick.
#define HEALTH_EVENT_THROTTLE_S 5
static time_t s_last_movement_refresh;
static time_t s_last_sleep_refresh;

static void health_handler(HealthEventType event, void* context) {
  (void)context;
  if (!any_slot_shows_health()) return;
  // SignificantUpdate = the applib health cache was invalidated (day
  // rollover, subscribe); rare, load-bearing, never throttled. HeartRateUpdate
  // bypasses the throttle too: a posted reading is already fresh, and stale
  // BPM is worse than a redraw (see ISSUES.md).
  if (event == HealthEventSignificantUpdate) {
    update_health_info();
    request_ui_redraw();
    return;
  }
  if (event == HealthEventHeartRateUpdate) {
    update_health_metrics(HEALTH_EVENT_BIT(event));
    request_ui_redraw();
    return;
  }
  if (event == HealthEventMovementUpdate || event == HealthEventSleepUpdate) {
    time_t now = time(NULL);
    time_t* last =
        event == HealthEventMovementUpdate ? &s_last_movement_refresh : &s_last_sleep_refresh;
    if (now - *last < HEALTH_EVENT_THROTTLE_S) return;
    *last = now;
    update_health_metrics(HEALTH_EVENT_BIT(event));
    request_ui_redraw();
  }
}
#endif

// The services whose events feed only some configurations; connection and
// the minute tick are always on. Each is held only while something consumes
// it: a subscription wakes the app for every event it delivers.
typedef struct {
  bool battery;  // a slot shows the battery
  bool health;   // a slot shows a health metric
  bool crt;      // the degauss strike's backlight and app focus gate
} SubscriptionSet;

static SubscriptionSet s_subscribed;

static SubscriptionSet wanted_subscriptions(void) {
  return (SubscriptionSet){.battery = any_slot_backed_by(DATA_SOURCE_BATTERY),
                           .health = any_slot_shows_health(),
                           .crt = s_settings_crt_degauss != 0};
}

// Subscribes and unsubscribes only the services whose want changed.
// `running` is false at launch, where focus still arrives through
// did_focus; a degauss turned on mid-run has no such event coming, since focus
// is long established.
static void apply_subscriptions(SubscriptionSet want, bool running) {
  if (want.battery != s_subscribed.battery) {
    if (want.battery) {
      battery_state_service_subscribe(battery_callback);
      // The level goes stale while unsubscribed.
      battery_callback(battery_state_service_peek());
    } else {
      battery_state_service_unsubscribe();
    }
  }
  if (want.crt != s_subscribed.crt) {
    if (want.crt) {
      backlight_service_subscribe(crt_backlight_handler);
      app_focus_service_subscribe_handlers((AppFocusHandlers){
          .will_focus = crt_app_focus_will_handler, .did_focus = crt_app_focus_did_handler});
      if (running) crt_set_in_focus(true);
    } else {
      backlight_service_unsubscribe();
      app_focus_service_unsubscribe();
    }
  }
#if defined(PBL_HEALTH)
  if (want.health != s_subscribed.health) {
    if (want.health) {
      health_service_events_subscribe(health_handler, NULL);
    } else {
      health_service_events_unsubscribe();
    }
  }
#endif
  s_subscribed = want;
}

void refresh_subscriptions(void) {
  apply_subscriptions(wanted_subscriptions(), true);
}

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

  s_time_layer = text_layer_create(CLOCK_RECT);
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, s_active_theme->text_primary);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  text_layer_set_font(s_time_layer, vga_font_64());
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

  // Topmost: the CRT overlay draws over the clock too (a tube has no
  // privileged pixels). It paints nothing while the setting is off.
  s_crt_layer = layer_create(bounds);
  layer_set_update_proc(s_crt_layer, crt_update_proc);
  layer_add_child(window_layer, s_crt_layer);

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
  unobstructed_area_service_unsubscribe();
  text_layer_destroy(s_time_layer);
  s_time_layer = NULL;
  Layer* crt_layer = s_crt_layer;
  Layer* canvas_layer = s_canvas_layer;
  crt_deinit();
  canvas_deinit();
  layer_destroy(crt_layer);
  layer_destroy(canvas_layer);
}

static void init(void) {
  load_settings();
  load_longitude();
  load_latitude();

  s_vga_16 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_VGA_16));
  s_vga_64 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_VGA_64));

  s_main_window = window_create();
  update_solar_elevation(time(NULL));
  apply_theme();
  window_set_background_color(s_main_window, s_active_theme->center_bg);
  window_set_window_handlers(s_main_window, (WindowHandlers){
                                                .load = main_window_load,
                                                .unload = main_window_unload,
                                            });
  window_stack_push(s_main_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  connection_service_subscribe(
      (ConnectionHandlers){.pebble_app_connection_handler = handle_bluetooth});
  // The battery level is seeded here through battery_callback when shown.
  apply_subscriptions(wanted_subscriptions(), false);

  // Peek the initial states — handle_bluetooth on launch would buzz while
  // the phone is merely away.
  s_connected = connection_service_peek_pebble_app_connection();
  s_quiet_time_active = quiet_time_is_active();

  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  AppMessageResult opened = app_message_open(APP_MESSAGE_INBOX_SIZE, APP_MESSAGE_OUTBOX_SIZE);
  if (opened != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "app_message_open failed (%d)", (int)opened);
  }

  // Restore cached weather; hit the network only if the cache is stale and
  // something actually shows weather. && short-circuits left to right, so the
  // cache load always runs.
  if (!load_weather_cache() && any_slot_needs_weather()) {
    request_weather();
  }
  refresh_state();
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  apply_subscriptions((SubscriptionSet){0}, true);
  connection_service_unsubscribe();
  crt_deinit();
  window_destroy(s_main_window);
  canvas_deinit();
  fonts_unload_custom_font(s_vga_16);
  fonts_unload_custom_font(s_vga_64);
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
