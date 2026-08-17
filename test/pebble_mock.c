#include "pebble.h"
#include "unity.h"
#include <stdarg.h>

// Mock Data

// MESSAGE_KEY_* stand-ins, extern-linkage like the SDK's generated
// message_keys.auto.c (values are arbitrary but fixed so tests can key off
// them).
uint32_t MESSAGE_KEY_WEATHER_TEMP = 100;
uint32_t MESSAGE_KEY_WEATHER_COND = 101;
uint32_t MESSAGE_KEY_SETTINGS_THEME = 102;
uint32_t MESSAGE_KEY_SETTINGS_UNITS = 103;
uint32_t MESSAGE_KEY_SETTINGS_DATE_FORMAT = 104;
uint32_t MESSAGE_KEY_WEATHER_HIGH = 107;
uint32_t MESSAGE_KEY_WEATHER_LOW = 108;
uint32_t MESSAGE_KEY_WEATHER_AQI = 109;
uint32_t MESSAGE_KEY_WEATHER_UV = 110;
uint32_t MESSAGE_KEY_SLOT_1 = 112;
uint32_t MESSAGE_KEY_SLOT_2 = 113;
uint32_t MESSAGE_KEY_SLOT_3 = 114;
uint32_t MESSAGE_KEY_SLOT_4 = 115;
uint32_t MESSAGE_KEY_SLOT_5 = 116;
uint32_t MESSAGE_KEY_SLOT_6 = 119;
uint32_t MESSAGE_KEY_SETTINGS_DOW_POSITION = 120;
uint32_t MESSAGE_KEY_WEATHER_HUMIDITY = 121;
uint32_t MESSAGE_KEY_WEATHER_PCP = 122;
uint32_t MESSAGE_KEY_WEATHER_PRECIP_NOW = 123;
uint32_t MESSAGE_KEY_WEATHER_LOW_TOMORROW = 124;
uint32_t MESSAGE_KEY_WEATHER_TEMP_HIGH_TOMORROW = 125;
uint32_t MESSAGE_KEY_WEATHER_HI_HOUR_TODAY = 126;
uint32_t MESSAGE_KEY_WEATHER_LO_HOUR_TODAY = 127;
uint32_t MESSAGE_KEY_WEATHER_HI_HOUR_TOMORROW = 128;
uint32_t MESSAGE_KEY_WEATHER_LO_HOUR_TOMORROW = 129;
uint32_t MESSAGE_KEY_SETTINGS_SHORT_DATE_FORMAT = 118;
uint32_t MESSAGE_KEY_SETTINGS_DISCONNECT_VIBE = 130;
uint32_t MESSAGE_KEY_SETTINGS_WEATHER_WINDOW = 134;
uint32_t MESSAGE_KEY_WEATHER_WIND_DIRECTION = 131;
uint32_t MESSAGE_KEY_WEATHER_WIND_SPEED = 132;
uint32_t MESSAGE_KEY_WEATHER_REQUEST = 133;
uint32_t MESSAGE_KEY_SETTINGS_CRT = 135;
uint32_t MESSAGE_KEY_SETTINGS_CRT_SOUND = 136;

// Implementations
void app_event_loop(void) {}
AppMessageResult app_message_open(const uint32_t size_inbound, const uint32_t size_outbound) {
  (void)size_inbound;
  (void)size_outbound;
  return APP_MSG_OK;
}

bool mock_outbox_begin_ok = true;
int mock_outbox_sends = 0;
AppMessageResult app_message_outbox_begin(DictionaryIterator** iterator) {
  static int dummy;  // app code only checks the iterator for NULL
  if (!mock_outbox_begin_ok) {
    *iterator = NULL;
    return APP_MSG_INVALID_STATE;
  }
  *iterator = (DictionaryIterator*)&dummy;
  return APP_MSG_OK;
}
AppMessageResult app_message_outbox_send(void) {
  mock_outbox_sends++;
  return APP_MSG_OK;
}
int mock_inbox_dropped_count = 0;
AppMessageInboxDropped app_message_register_inbox_dropped(AppMessageInboxDropped dropped_callback) {
  mock_inbox_dropped_count++;
  return dropped_callback;
}
int mock_inbox_received_count = 0;
AppMessageInboxReceived app_message_register_inbox_received(
    AppMessageInboxReceived received_callback) {
  mock_inbox_received_count++;
  return received_callback;
}
int mock_outbox_sent_count = 0;
AppMessageOutboxSent app_message_register_outbox_sent(AppMessageOutboxSent sent_callback) {
  mock_outbox_sent_count++;
  return sent_callback;
}
int mock_outbox_failed_count = 0;
AppMessageOutboxFailed app_message_register_outbox_failed(AppMessageOutboxFailed failed_callback) {
  mock_outbox_failed_count++;
  return failed_callback;
}

int mock_timer_register_count = 0;
uint32_t mock_timer_last_ms = 0;
AppTimerCallback mock_timer_callback = NULL;
AppTimer* app_timer_register(uint32_t timeout_ms, AppTimerCallback callback, void* callback_data) {
  (void)callback_data;
  // Recorded, not scheduled: host tests fire delayed callbacks by calling
  // mock_timer_callback(NULL) directly. The NULL return matches the SDK's
  // pool-exhausted case; callers tolerate it (see main.c's weather retry).
  mock_timer_register_count++;
  mock_timer_last_ms = timeout_ms;
  mock_timer_callback = callback;
  return NULL;
}
bool app_timer_reschedule(AppTimer* timer, uint32_t new_timeout_ms) {
  (void)timer;
  (void)new_timeout_ms;
  return false;
}
void app_timer_cancel(AppTimer* timer) {
  (void)timer;
}

int mock_backlight_subscribe_count = 0;
int mock_backlight_unsubscribe_count = 0;
BacklightHandler mock_backlight_handler = NULL;
void backlight_service_subscribe(BacklightHandler handler) {
  mock_backlight_subscribe_count++;
  mock_backlight_handler = handler;
}
void backlight_service_unsubscribe(void) {
  mock_backlight_unsubscribe_count++;
  mock_backlight_handler = NULL;
}

uint8_t mock_framebuffer[200 * 228];
int mock_fb_capture_count = 0;
int mock_fb_release_count = 0;
static GBitmap s_mock_fb = {GRect(0, 0, 200, 228), mock_framebuffer};
GBitmap* graphics_capture_frame_buffer(GContext* ctx) {
  (void)ctx;
  mock_fb_capture_count++;
  return &s_mock_fb;
}
bool graphics_release_frame_buffer(GContext* ctx, GBitmap* buffer) {
  (void)ctx;
  mock_fb_release_count++;
  return buffer == &s_mock_fb;
}
uint8_t* gbitmap_get_data(const GBitmap* bitmap) {
  return bitmap->data;
}
GRect gbitmap_get_bounds(const GBitmap* bitmap) {
  return bitmap->bounds;
}

bool mock_speaker_muted = false;
int mock_speaker_play_notes_count = 0;
uint32_t mock_speaker_last_num_notes = 0;
uint8_t mock_speaker_last_volume = 0;
bool speaker_play_notes(const SpeakerNote* notes, uint32_t num_notes, uint8_t volume) {
  (void)notes;
  mock_speaker_play_notes_count++;
  mock_speaker_last_num_notes = num_notes;
  mock_speaker_last_volume = volume;
  return true;
}
bool speaker_is_muted(void) {
  return mock_speaker_muted;
}

uint8_t mock_battery_percent = 100;
bool mock_battery_charging = false;
BatteryChargeState battery_state_service_peek(void) {
  BatteryChargeState state = {.charge_percent = mock_battery_percent,
                              .is_charging = mock_battery_charging};
  return state;
}

int mock_battery_subscribe_count = 0;
void battery_state_service_subscribe(BatteryStateHandler handler) {
  (void)handler;
  mock_battery_subscribe_count++;
}
bool mock_clock_24h = false;
bool clock_is_24h_style(void) {
  return mock_clock_24h;
}
bool mock_bt_connected = true;
bool connection_service_peek_pebble_app_connection(void) {
  return mock_bt_connected;
}
bool mock_quiet_time_active = false;
bool quiet_time_is_active(void) {
  return mock_quiet_time_active;
}
int mock_connection_subscribe_count = 0;
void connection_service_subscribe(ConnectionHandlers handlers) {
  (void)handlers;
  mock_connection_subscribe_count++;
}

// Scriptable inbound dictionary: tests stage tuples with mock_dict_add_*()
// and dict_find() serves them back, so inbox_received_callback is testable.
// Capacity covers the real weather payload (weather.js WEATHER_FIELDS) with
// headroom, so tests can stage the message shape the watch actually gets.
#define MOCK_DICT_MAX 24
#define MOCK_DICT_TUPLE_BYTES 64
static uint8_t mock_dict_storage[MOCK_DICT_MAX][MOCK_DICT_TUPLE_BYTES];
static int mock_dict_count = 0;

void mock_dict_reset(void) {
  mock_dict_count = 0;
}

static Tuple* mock_dict_new_slot(uint32_t key, uint16_t width, TupleType type) {
  if (mock_dict_count >= MOCK_DICT_MAX) {
    TEST_FAIL_MESSAGE("mock dict capacity exceeded; raise MOCK_DICT_MAX");
  }
  if (width >= MOCK_DICT_TUPLE_BYTES - sizeof(Tuple)) {
    TEST_FAIL_MESSAGE("mock dict tuple too wide; raise MOCK_DICT_TUPLE_BYTES");
  }
  Tuple* t = (Tuple*)mock_dict_storage[mock_dict_count++];
  t->key = key;
  t->type = type;
  t->length = width;
  return t;
}

void mock_dict_add_int(uint32_t key, int32_t value) {
  mock_dict_add_int_width(key, value, 4);
}

// Staging for tuple_get_int's wire-width arms: the SDK sends 1-, 2- or 4-byte
// ints; the mock has to be able to stage them all.
void mock_dict_add_int_width(uint32_t key, int32_t value, uint16_t width) {
  Tuple* t = mock_dict_new_slot(key, width, TUPLE_INT);
  if (width == 1) {
    t->value->uint8 = (uint8_t)value;
  } else if (width == 2) {
    t->value->uint16 = (uint16_t)value;
  } else {
    t->value->int32 = value;
  }
}

void mock_dict_add_uint_width(uint32_t key, uint32_t value, uint16_t width) {
  Tuple* t = mock_dict_new_slot(key, width, TUPLE_UINT);
  if (width == 1) {
    t->value->uint8 = (uint8_t)value;
  } else if (width == 2) {
    t->value->uint16 = (uint16_t)value;
  } else {
    t->value->uint32 = value;
  }
}

void mock_dict_add_cstring(uint32_t key, const char* str) {
  size_t len = strlen(str) + 1;
  Tuple* t = mock_dict_new_slot(key, (uint16_t)len, TUPLE_CSTRING);
  strcpy(t->value->cstring, str);
}

Tuple* dict_find(const DictionaryIterator* iter, uint32_t key) {
  (void)iter;
  for (int i = 0; i < mock_dict_count; i++) {
    Tuple* t = (Tuple*)mock_dict_storage[i];
    if (t->key == key) return t;
  }
  return NULL;
}
// Outbound capture: the only outbox payload is the weather-request trigger,
// whose key *is* the message kind — tests must see the key, not just count
// sends. Assertions go through the accessors, not this storage.
#define MOCK_OUTBOX_WRITES_MAX 8
static struct {
  uint32_t key;
  uint8_t value;
} s_mock_outbox_writes[MOCK_OUTBOX_WRITES_MAX];
static int s_mock_outbox_write_count = 0;

int mock_outbox_write_count(void) {
  return s_mock_outbox_write_count;
}

bool mock_outbox_has(uint32_t key, uint8_t value) {
  for (int i = 0; i < s_mock_outbox_write_count; i++) {
    if (s_mock_outbox_writes[i].key == key && s_mock_outbox_writes[i].value == value) return true;
  }
  return false;
}

DictionaryResult dict_write_uint8(DictionaryIterator* iter, const uint32_t key,
                                  const uint8_t value) {
  (void)iter;
  if (s_mock_outbox_write_count >= MOCK_OUTBOX_WRITES_MAX) {
    TEST_FAIL_MESSAGE("mock outbox capture full; raise MOCK_OUTBOX_WRITES_MAX");
  }
  s_mock_outbox_writes[s_mock_outbox_write_count].key = key;
  s_mock_outbox_writes[s_mock_outbox_write_count].value = value;
  s_mock_outbox_write_count++;
  return DICT_OK;
}

GFont fonts_get_system_font(const char* font_key) {
  return (GFont)font_key;  // pointer identity is the distinct sentinel
}

ResHandle resource_get_handle(uint32_t resource_id) {
  return (ResHandle)(uintptr_t)resource_id;
}

// Stable one-to-one with the handle, like the SDK's font cache, so tests can
// tell the two baked sizes apart.
GFont fonts_load_custom_font(ResHandle handle) {
  static char s_mock_fonts[8];
  return (GFont)&s_mock_fonts[(uintptr_t)handle % 8];
}

void fonts_unload_custom_font(GFont font) {
  (void)font;
}

bool grect_equal(const GRect* const rect_a, const GRect* const rect_b) {
  return rect_a->origin.x == rect_b->origin.x && rect_a->origin.y == rect_b->origin.y &&
         rect_a->size.w == rect_b->size.w && rect_a->size.h == rect_b->size.h;
}

static GColor s_mock_fill_color = GColorClear;
void graphics_context_set_fill_color(GContext* ctx, GColor color) {
  (void)ctx;
  s_mock_fill_color = color;
}
void graphics_context_set_stroke_color(GContext* ctx, GColor color) {
  (void)ctx;
  (void)color;
}
void graphics_context_set_stroke_width(GContext* ctx, uint8_t stroke_width) {
  (void)ctx;
  (void)stroke_width;
}
static GColor s_mock_text_color = GColorClear;
void graphics_context_set_text_color(GContext* ctx, GColor color) {
  (void)ctx;
  s_mock_text_color = color;
}
void graphics_draw_line(GContext* ctx, GPoint p0, GPoint p1) {
  (void)ctx;
  (void)p0;
  (void)p1;
}
int mock_wordwrap_calls = 0;
int mock_bar_glyph_calls = 0;
char mock_text_runs[MOCK_MAX_TEXT_RUNS][32];
GColor mock_text_run_colors[MOCK_MAX_TEXT_RUNS];
GRect mock_text_run_boxes[MOCK_MAX_TEXT_RUNS];
int mock_text_run_count = 0;
void mock_text_runs_reset(void) {
  mock_text_run_count = 0;
}
void graphics_draw_text(GContext* ctx, const char* text, GFont const font, const GRect box,
                        const GTextOverflowMode overflow_mode, const GTextAlignment alignment,
                        GTextAttributes* text_attributes) {
  (void)ctx;
  (void)font;
  (void)alignment;
  (void)text_attributes;
  if (overflow_mode == GTextOverflowModeWordWrap) mock_wordwrap_calls++;
  if (strstr(text, "\xE2\x96\x88")) mock_bar_glyph_calls++;  // U+2588 FULL BLOCK
  if (mock_text_run_count < MOCK_MAX_TEXT_RUNS) {
    snprintf(mock_text_runs[mock_text_run_count], sizeof(mock_text_runs[0]), "%s", text);
    mock_text_run_colors[mock_text_run_count] = s_mock_text_color;
    mock_text_run_boxes[mock_text_run_count] = box;
    mock_text_run_count++;
  }
}
GRect mock_fill_rects[MOCK_MAX_FILL_RECTS];
GColor mock_fill_rect_colors[MOCK_MAX_FILL_RECTS];
int mock_fill_rect_count = 0;
void mock_fill_rect_reset(void) {
  mock_fill_rect_count = 0;
}
void graphics_fill_rect(GContext* ctx, GRect rect, uint16_t corner_radius,
                        GCornerMask corner_mask) {
  (void)ctx;
  (void)corner_radius;
  (void)corner_mask;
  if (mock_fill_rect_count < MOCK_MAX_FILL_RECTS) {
    mock_fill_rects[mock_fill_rect_count] = rect;
    mock_fill_rect_colors[mock_fill_rect_count++] = s_mock_fill_color;
  }
}

int mock_health_subscribe_count = 0;
bool health_service_events_subscribe(HealthEventHandler handler, void* context) {
  (void)handler;
  (void)context;
  mock_health_subscribe_count++;
  return true;
}
bool health_service_events_unsubscribe(void) {
  return true;
}
int32_t mock_heart_rate = 0;
int mock_health_accessible_count = 0;
int mock_health_sum_today_count = 0;
int mock_health_peek_count = 0;
// Per-metric permission, indexed by HealthMetric; default Available.
HealthServiceAccessibilityMask mock_health_accessible[MOCK_HEALTH_METRIC_COUNT];

HealthServiceAccessibilityMask health_service_metric_accessible(HealthMetric metric,
                                                                time_t time_start,
                                                                time_t time_end) {
  mock_health_accessible_count++;
  // An explicit denial wins over every other rule.
  if (mock_health_accessible[metric] != HealthServiceAccessibilityMaskAvailable) {
    return mock_health_accessible[metric];
  }
  // Mirror real firmware: heart-rate accessibility is only reported for an
  // instant query; a time-range query comes back unsupported.
  if (metric == HealthMetricHeartRateBPM && time_start != time_end) {
    return HealthServiceAccessibilityMaskNotSupported;
  }
  return HealthServiceAccessibilityMaskAvailable;
}
HealthServiceAccessibilityMask health_service_metric_averaged_accessible(
    HealthMetric metric, time_t time_start, time_t time_end, HealthServiceTimeScope scope) {
  (void)metric;
  (void)time_start;
  (void)time_end;
  (void)scope;
  return HealthServiceAccessibilityMaskAvailable;
}
HealthValue health_service_peek_current_value(HealthMetric metric) {
  mock_health_peek_count++;
  if (metric == HealthMetricHeartRateBPM) return mock_heart_rate;
  return 0;
}
// Per-metric sum values, indexed by HealthMetric — distinct defaults, so a
// formatter wired to the wrong metric shows a tell-tale number instead of a
// plausible one. Never staged per-test today (nothing consumes averages), but
// knobbed for parity with sum_today.
int32_t mock_health_sum_today_value[MOCK_HEALTH_METRIC_COUNT];
int32_t mock_health_sum_averaged_value[MOCK_HEALTH_METRIC_COUNT];

// Power-on defaults; mock_reset restores these. HR rows are 0: HR is an
// instant read (mock_heart_rate), never summed.
static void mock_health_values_reset(void) {
  static const int32_t defaults[MOCK_HEALTH_METRIC_COUNT] = {
      5678,   // StepCount
      2700,   // ActiveSeconds (-> 45 display minutes)
      4321,   // WalkedDistanceMeters
      26100,  // SleepSeconds (7h 15m)
      19000,  // SleepRestfulSeconds
      0,      // HeartRateBPM
      0,      // HeartRateRawBPM
  };
  for (int i = 0; i < MOCK_HEALTH_METRIC_COUNT; i++) {
    mock_health_sum_today_value[i] = defaults[i];
    // Distinct from the today-sums on purpose: cross-wiring sum_today
    // against sum_averaged must not accidentally read plausible.
    mock_health_sum_averaged_value[i] = defaults[i] * 2;
  }
}

HealthValue health_service_sum_averaged(HealthMetric metric, time_t time_start, time_t time_end,
                                        HealthServiceTimeScope scope) {
  (void)time_start;
  (void)time_end;
  (void)scope;
  return mock_health_sum_averaged_value[metric];
}
HealthValue health_service_sum_today(HealthMetric metric) {
  mock_health_sum_today_count++;
  return mock_health_sum_today_value[metric];
}

void layer_add_child(Layer* parent, Layer* child) {
  (void)parent;
  (void)child;
}
// Return sentinels instead of NULL so layer-attached code paths are testable.
static char mock_layer_storage[8];
static int mock_layers_given = 0;
Layer* layer_create(GRect frame) {
  (void)frame;
  return (Layer*)&mock_layer_storage[mock_layers_given++ % 8];
}
void layer_destroy(Layer* layer) {
  (void)layer;
}
GRect layer_get_bounds(const Layer* layer) {
  (void)layer;
  // The face targets emery, so the root layer spans the 200x228 screen.
  return GRect(0, 0, 200, 228);
}
// Tests shrink this from the bottom to stand in for a Quick View overlay.
GRect mock_unobstructed_bounds = GRect(0, 0, 200, 228);
GRect layer_get_unobstructed_bounds(const Layer* layer) {
  (void)layer;
  return mock_unobstructed_bounds;
}
int mock_mark_dirty_count = 0;
void layer_mark_dirty(Layer* layer) {
  (void)layer;
  mock_mark_dirty_count++;
}
void layer_set_update_proc(Layer* layer, LayerUpdateProc update_proc) {
  (void)layer;
  (void)update_proc;
}

// Key-exact store: persist keys are sparse (1000-1032 today), so an indexed
// array would need a modulus — and silently alias keys a multiple of it
// apart (test_mock_persist_should_keep_distant_keys_independent pins the
// bug class). Capacity overflow fails loud, like the dict/outbox mocks.
#define MOCK_PERSIST_MAX_KEYS 64
static struct {
  uint32_t key;
  int32_t value;
} s_mock_persist[MOCK_PERSIST_MAX_KEYS];
static int s_mock_persist_count = 0;

static int mock_persist_slot(uint32_t key) {
  for (int i = 0; i < s_mock_persist_count; i++) {
    if (s_mock_persist[i].key == key) return i;
  }
  return -1;
}

int mock_persist_write_count = 0;

bool persist_exists(const uint32_t key) {
  return mock_persist_slot(key) >= 0;
}
int32_t persist_read_int(const uint32_t key) {
  int slot = mock_persist_slot(key);
  return slot >= 0 ? s_mock_persist[slot].value : 0;  // a missing key reads 0
}
status_t persist_write_int(const uint32_t key, const int32_t value) {
  mock_persist_write_count++;
  int slot = mock_persist_slot(key);
  if (slot < 0) {
    if (s_mock_persist_count >= MOCK_PERSIST_MAX_KEYS) {
      TEST_FAIL_MESSAGE("mock persist capacity exceeded; raise MOCK_PERSIST_MAX_KEYS");
    }
    slot = s_mock_persist_count++;
    s_mock_persist[slot].key = key;
  }
  s_mock_persist[slot].value = value;
  return S_SUCCESS;
}
void mock_persist_reset(void) {
  s_mock_persist_count = 0;
}

static char mock_text_layer_storage[8];
static int mock_text_layers_given = 0;
TextLayer* text_layer_create(GRect frame) {
  (void)frame;
  return (TextLayer*)&mock_text_layer_storage[mock_text_layers_given++ % 8];
}
void text_layer_destroy(TextLayer* text_layer) {
  (void)text_layer;
}
Layer* text_layer_get_layer(TextLayer* text_layer) {
  (void)text_layer;
  return NULL;
}
void text_layer_set_background_color(TextLayer* text_layer, GColor color) {
  (void)text_layer;
  (void)color;
}
void text_layer_set_font(TextLayer* text_layer, GFont font) {
  (void)text_layer;
  (void)font;
}
int mock_set_text_count = 0;
char mock_last_text[32] = "";
void text_layer_set_text(TextLayer* text_layer, const char* text) {
  (void)text_layer;
  mock_set_text_count++;
  snprintf(mock_last_text, sizeof(mock_last_text), "%s", text);
}
void text_layer_set_text_alignment(TextLayer* text_layer, GTextAlignment text_alignment) {
  (void)text_layer;
  (void)text_alignment;
}
int mock_set_text_color_count = 0;
void text_layer_set_text_color(TextLayer* text_layer, GColor color) {
  (void)text_layer;
  (void)color;
  mock_set_text_color_count++;
}

int mock_tick_subscribe_count = 0;
TimeUnits mock_tick_units = 0;
void tick_timer_service_subscribe(TimeUnits tick_units, TickHandler handler) {
  (void)handler;
  mock_tick_subscribe_count++;
  mock_tick_units = tick_units;
}
int mock_unobstructed_subscribe_count = 0;
void unobstructed_area_service_subscribe(UnobstructedAreaHandlers handlers, void* context) {
  (void)handlers;
  (void)context;
  mock_unobstructed_subscribe_count++;
}
// Mock wall clock: the health-event throttle is timestamp-based, so tests jump
// seconds through this offset instead of sleeping.
time_t mock_time_offset = 0;
time_t time(time_t* t) {
  struct timespec ts;
  timespec_get(&ts, TIME_UTC);
  time_t now = (time_t)ts.tv_sec + mock_time_offset;
  if (t) *t = now;
  return now;
}
// Today's midnight on the mock clock, like the SDK — health range queries get
// a real day boundary instead of the epoch.
time_t time_start_of_today(void) {
  time_t now = time(NULL);
  struct tm* lt = localtime(&now);
  return now - (time_t)(lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec);
}
int mock_vibes_count = 0;
void vibes_double_pulse(void) {
  mock_vibes_count++;
}

Window* window_create(void) {
  return NULL;
}
void window_destroy(Window* window) {
  (void)window;
}
Layer* window_get_root_layer(const Window* window) {
  (void)window;
  return NULL;
}
int mock_window_set_bg_count = 0;
void window_set_background_color(Window* window, GColor background_color) {
  (void)window;
  (void)background_color;
  mock_window_set_bg_count++;
}
void window_set_window_handlers(Window* window, WindowHandlers handlers) {
  (void)window;
  (void)handlers;
}
void window_stack_push(Window* window, bool animated) {
  (void)window;
  (void)animated;
}

void APP_LOG(uint8_t level, const char* fmt, ...) {
  (void)level;
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  printf("\n");
  va_end(args);
}

// All knobs and counters back to their power-on values. setUp calls this via
// reset_all_state(); new mock state belongs here.
void mock_reset(void) {
  mock_time_offset = 0;
  mock_quiet_time_active = false;
  mock_battery_percent = 100;
  mock_battery_charging = false;
  mock_bt_connected = true;
  mock_clock_24h = false;
  mock_outbox_begin_ok = true;
  mock_outbox_sends = 0;
  s_mock_outbox_write_count = 0;
  mock_health_accessible_count = 0;
  for (int i = 0; i < MOCK_HEALTH_METRIC_COUNT; i++) {
    mock_health_accessible[i] = HealthServiceAccessibilityMaskAvailable;
  }
  mock_heart_rate = 0;

  mock_health_sum_today_count = 0;
  mock_health_peek_count = 0;
  mock_health_values_reset();
  mock_vibes_count = 0;
  mock_mark_dirty_count = 0;
  mock_set_text_count = 0;
  mock_set_text_color_count = 0;
  mock_last_text[0] = '\0';
  mock_tick_subscribe_count = 0;
  mock_tick_units = 0;
  mock_battery_subscribe_count = 0;
  mock_connection_subscribe_count = 0;
  mock_backlight_subscribe_count = 0;
  mock_backlight_unsubscribe_count = 0;
  mock_backlight_handler = NULL;
  mock_timer_register_count = 0;
  mock_timer_last_ms = 0;
  mock_timer_callback = NULL;
  mock_fb_capture_count = 0;
  mock_fb_release_count = 0;
  mock_window_set_bg_count = 0;
  mock_speaker_muted = false;
  mock_speaker_play_notes_count = 0;
  mock_speaker_last_num_notes = 0;
  mock_speaker_last_volume = 0;
  memset(mock_framebuffer, 0xC0, sizeof(mock_framebuffer));  // opaque black glass
  mock_health_subscribe_count = 0;
  mock_unobstructed_subscribe_count = 0;
  mock_inbox_received_count = 0;
  mock_inbox_dropped_count = 0;
  mock_outbox_sent_count = 0;
  mock_outbox_failed_count = 0;
  mock_wordwrap_calls = 0;
  mock_bar_glyph_calls = 0;
  mock_persist_write_count = 0;
  mock_layers_given = 0;
  mock_text_layers_given = 0;
  mock_unobstructed_bounds = GRect(0, 0, 200, 228);
  s_mock_fill_color = GColorClear;
  s_mock_text_color = GColorClear;
  mock_dict_reset();
  mock_persist_reset();
  mock_text_runs_reset();
  mock_fill_rect_reset();
}
