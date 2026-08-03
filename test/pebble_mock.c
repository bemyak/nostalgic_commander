#include "pebble.h"
#include <assert.h>
#include <stdarg.h>

// Mock Data
int32_t mock_persist_storage[256];
bool mock_persist_exists[256];

// Implementations
void app_event_loop(void) {}
void app_message_open(uint32_t size_inbound, uint32_t size_outbound) {}

int mock_outbox_sends = 0;
void app_message_outbox_begin(DictionaryIterator** iterator) {
  static int dummy;  // app code only checks the iterator for NULL
  *iterator = (DictionaryIterator*)&dummy;
}
void app_message_outbox_send(void) {
  mock_outbox_sends++;
}
void app_message_register_inbox_dropped(void (*callback)(AppMessageResult reason, void* context)) {}
void app_message_register_inbox_received(void (*callback)(DictionaryIterator* iterator,
                                                          void* context)) {}
void app_message_register_outbox_sent(void (*callback)(DictionaryIterator* iterator,
                                                       void* context)) {}
void app_message_register_outbox_failed(void (*callback)(DictionaryIterator* iterator,
                                                         AppMessageResult reason, void* context)) {}

AppTimer* app_timer_register(uint32_t timeout_ms, void (*callback)(void* data), void* data) {
  return NULL;  // host tests drive retries by calling the callback directly
}
bool app_timer_reschedule(AppTimer* timer, uint32_t new_timeout_ms) {
  return false;
}
void app_timer_cancel(AppTimer* timer) {}

BatteryChargeState battery_state_service_peek(void) {
  BatteryChargeState state = {.charge_percent = 100, .is_charging = false, .is_plugged = false};
  return state;
}

void battery_state_service_subscribe(void (*handler)(BatteryChargeState charge)) {}
bool clock_is_24h_style(void) {
  return false;
}
bool connection_service_peek_pebble_app_connection(void) {
  return true;
}
void connection_service_subscribe(ConnectionHandlers handlers) {}

// Scriptable inbound dictionary: tests stage tuples with mock_dict_add_*()
// and dict_find() serves them back, so inbox_received_callback is testable.
#define MOCK_DICT_MAX 16
#define MOCK_DICT_TUPLE_BYTES 64
static uint8_t mock_dict_storage[MOCK_DICT_MAX][MOCK_DICT_TUPLE_BYTES];
static int mock_dict_count = 0;

void mock_dict_reset(void) {
  mock_dict_count = 0;
}

static Tuple* mock_dict_next_slot(uint32_t key) {
  assert(mock_dict_count < MOCK_DICT_MAX);
  Tuple* t = (Tuple*)mock_dict_storage[mock_dict_count++];
  t->key = key;
  return t;
}

void mock_dict_add_int(uint32_t key, int32_t value) {
  Tuple* t = mock_dict_next_slot(key);
  t->type = TUPLE_INT;
  t->length = 4;
  t->value->int32 = value;
}

void mock_dict_add_cstring(uint32_t key, const char* str) {
  Tuple* t = mock_dict_next_slot(key);
  assert(strlen(str) < MOCK_DICT_TUPLE_BYTES - sizeof(Tuple));
  t->type = TUPLE_CSTRING;
  t->length = strlen(str) + 1;
  strcpy(t->value->cstring, str);
}

Tuple* dict_find(const DictionaryIterator* iter, uint32_t key) {
  for (int i = 0; i < mock_dict_count; i++) {
    Tuple* t = (Tuple*)mock_dict_storage[i];
    if (t->key == key) return t;
  }
  return NULL;
}
void dict_write_uint8(DictionaryIterator* iter, uint32_t key, uint8_t value) {}

GFont fonts_get_system_font(const char* font_key) {
  return NULL;
}

ResHandle resource_get_handle(uint32_t resource_id) {
  return NULL;
}

GFont fonts_load_custom_font(ResHandle handle) {
  return NULL;
}

void fonts_unload_custom_font(GFont font) {}

bool grect_equal(const GRect* const rect_a, const GRect* const rect_b) {
  return rect_a->origin.x == rect_b->origin.x && rect_a->origin.y == rect_b->origin.y &&
         rect_a->size.w == rect_b->size.w && rect_a->size.h == rect_b->size.h;
}

static GColor s_mock_fill_color = GColorClear;
void graphics_context_set_fill_color(GContext* ctx, GColor color) {
  s_mock_fill_color = color;
}
void graphics_context_set_stroke_color(GContext* ctx, GColor color) {}
void graphics_context_set_stroke_width(GContext* ctx, uint8_t stroke_width) {}
static GColor s_mock_text_color = GColorClear;
void graphics_context_set_text_color(GContext* ctx, GColor color) {
  s_mock_text_color = color;
}
void graphics_draw_line(GContext* ctx, GPoint p0, GPoint p1) {}
int mock_wordwrap_calls = 0;
int mock_bar_glyph_calls = 0;
char mock_text_runs[MOCK_MAX_TEXT_RUNS][32];
GColor mock_text_run_colors[MOCK_MAX_TEXT_RUNS];
GRect mock_text_run_boxes[MOCK_MAX_TEXT_RUNS];
int mock_text_run_count = 0;
void mock_text_runs_reset(void) {
  mock_text_run_count = 0;
}
void graphics_draw_text(GContext* ctx, const char* text, GFont font, GRect box,
                        GTextOverflowMode overflow_mode, GTextAlignment alignment,
                        GContext* layout_cache) {
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
  if (mock_fill_rect_count < MOCK_MAX_FILL_RECTS) {
    mock_fill_rects[mock_fill_rect_count] = rect;
    mock_fill_rect_colors[mock_fill_rect_count++] = s_mock_fill_color;
  }
}

void health_service_events_subscribe(void (*handler)(HealthEventType event, void* context),
                                     void* context) {}
void health_service_events_unsubscribe(void) {}
int32_t mock_heart_rate = 0;
int mock_health_accessible_count = 0;
int mock_health_sum_today_count = 0;
int mock_health_peek_count = 0;

HealthServiceAccessibilityMask health_service_metric_accessible(HealthMetric metric,
                                                                time_t time_start,
                                                                time_t time_end) {
  mock_health_accessible_count++;
  // Mirror real firmware: heart-rate accessibility is only reported for an
  // instant query; a time-range query comes back unsupported.
  if (metric == HealthMetricHeartRateBPM && time_start != time_end) {
    return HealthServiceAccessibilityMaskNotSupported;
  }
  return HealthServiceAccessibilityMaskAvailable;
}
HealthServiceAccessibilityMask health_service_metric_averaged_accessible(
    HealthMetric metric, time_t time_start, time_t time_end, HealthServiceTimeScope scope) {
  return HealthServiceAccessibilityMaskAvailable;
}
int32_t health_service_peek_current_value(HealthMetric metric) {
  mock_health_peek_count++;
  if (metric == HealthMetricHeartRateBPM) return mock_heart_rate;
  return 0;
}
int32_t health_service_sum_averaged(HealthMetric metric, time_t time_start, time_t time_end,
                                    HealthServiceTimeScope scope) {
  return 10000;
}
int32_t health_service_sum_today(HealthMetric metric) {
  mock_health_sum_today_count++;
  return 5000;
}

void layer_add_child(Layer* parent, Layer* child) {}
// Return sentinels instead of NULL so layer-attached code paths are testable.
static char mock_layer_storage[8];
static int mock_layers_given = 0;
Layer* layer_create(GRect frame) {
  (void)frame;
  return (Layer*)&mock_layer_storage[mock_layers_given++ % 8];
}
void layer_destroy(Layer* layer) {}
GRect layer_get_bounds(Layer* layer) {
  // The face targets emery, so the root layer spans the 200x228 screen.
  return GRect(0, 0, 200, 228);
}
// Tests shrink this from the bottom to stand in for a Quick View overlay.
GRect mock_unobstructed_bounds = GRect(0, 0, 200, 228);
GRect layer_get_unobstructed_bounds(Layer* layer) {
  return mock_unobstructed_bounds;
}
int mock_mark_dirty_count = 0;
void layer_mark_dirty(Layer* layer) {
  mock_mark_dirty_count++;
}
// Records each call's hidden flag in order; request_ui_redraw applies slots
// 0..NUM_SLOTS-1 in order, so buffer indices are slot indices within one call of it.
bool mock_set_hidden_states[MOCK_MAX_SET_HIDDEN];
int mock_set_hidden_count = 0;
void layer_set_hidden(Layer* layer, bool hidden) {
  if (mock_set_hidden_count < MOCK_MAX_SET_HIDDEN) {
    mock_set_hidden_states[mock_set_hidden_count++] = hidden;
  }
}
void layer_set_update_proc(Layer* layer, void (*update_proc)(Layer* layer, GContext* ctx)) {}

char mock_persist_strings[256][64];
int mock_persist_write_count = 0;

bool persist_exists(const uint32_t key) {
  return mock_persist_exists[key % 256];
}
int32_t persist_read_int(const uint32_t key) {
  return mock_persist_storage[key % 256];
}
void persist_write_int(const uint32_t key, const int32_t value) {
  mock_persist_write_count++;
  mock_persist_storage[key % 256] = value;
  mock_persist_exists[key % 256] = true;
}
int persist_write_string(const uint32_t key, const char* cstring) {
  mock_persist_write_count++;
  strncpy(mock_persist_strings[key % 256], cstring, sizeof(mock_persist_strings[0]) - 1);
  mock_persist_strings[key % 256][sizeof(mock_persist_strings[0]) - 1] = '\0';
  mock_persist_exists[key % 256] = true;
  return strlen(mock_persist_strings[key % 256]) + 1;
}
int persist_read_string(const uint32_t key, char* buffer, const size_t buffer_size) {
  strncpy(buffer, mock_persist_strings[key % 256], buffer_size - 1);
  buffer[buffer_size - 1] = '\0';
  return strlen(buffer) + 1;
}
void mock_persist_reset(void) {
  memset(mock_persist_storage, 0, sizeof(mock_persist_storage));
  memset(mock_persist_exists, 0, sizeof(mock_persist_exists));
  memset(mock_persist_strings, 0, sizeof(mock_persist_strings));
}

static char mock_text_layer_storage[8];
static int mock_text_layers_given = 0;
TextLayer* text_layer_create(GRect frame) {
  (void)frame;
  return (TextLayer*)&mock_text_layer_storage[mock_text_layers_given++ % 8];
}
void text_layer_destroy(TextLayer* text_layer) {}
Layer* text_layer_get_layer(TextLayer* text_layer) {
  return NULL;
}
void text_layer_set_background_color(TextLayer* text_layer, GColor color) {}
void text_layer_set_font(TextLayer* text_layer, GFont font) {}
int mock_set_text_count = 0;
void text_layer_set_text(TextLayer* text_layer, const char* text) {
  mock_set_text_count++;
}
void text_layer_set_text_alignment(TextLayer* text_layer, GTextAlignment text_alignment) {}
int mock_set_text_color_count = 0;
void text_layer_set_text_color(TextLayer* text_layer, GColor color) {
  mock_set_text_color_count++;
}

void tick_timer_service_subscribe(TimeUnits tick_units,
                                  void (*handler)(struct tm* tick_time, TimeUnits units_changed)) {}
void unobstructed_area_service_subscribe(UnobstructedAreaHandlers handlers, void* context) {}
time_t time_start_of_today(void) {
  return 0;
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
int mock_vibes_count = 0;
void vibes_double_pulse(void) {
  mock_vibes_count++;
}

Window* window_create(void) {
  return NULL;
}
void window_destroy(Window* window) {}
Layer* window_get_root_layer(Window* window) {
  return NULL;
}
void window_set_background_color(Window* window, GColor background_color) {}
void window_set_window_handlers(Window* window, WindowHandlers handlers) {}
void window_stack_push(Window* window, bool animated) {}

void APP_LOG(uint8_t level, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  printf("\n");
  va_end(args);
}
