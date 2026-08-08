#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#define PBL_COLOR 1
#define PBL_HEALTH 1

// --- Types ---
typedef struct Window Window;
typedef struct Layer Layer;
typedef struct TextLayer TextLayer;
typedef struct GContext GContext;
typedef struct DictionaryIterator DictionaryIterator;
typedef void AppTimer;

typedef struct {
  int x;
  int y;
} GPoint;
typedef struct {
  int w;
  int h;
} GSize;
typedef struct {
  GPoint origin;
  GSize size;
} GRect;

#define GRect(x, y, w, h) ((GRect){{(x), (y)}, {(w), (h)}})
#define GPoint(x, y) ((GPoint){(x), (y)})

typedef uint32_t GColor;
#define gcolor_equal(a, b) ((a) == (b))
#define GColorClear 0
#define GColorBlack 1
#define GColorWhite 2
#define GColorChromeYellow 3
#define GColorMintGreen 4
#define GColorPastelYellow 5
#define GColorSunsetOrange 6
#define GColorDarkGray 7
#define GColorLightGray 8
#define GColorOrange 9
#define GColorDarkGreen 10
#define GColorRed 11
#define GColorIslamicGreen 12
#define GColorBlue 13
#define GColorKellyGreen 14
#define GColorGreen 15
#define GColorYellow 16
#define GColorLimerick 17
#define GColorDukeBlue 18
#define GColorOxfordBlue 19
#define GColorTiffanyBlue 20
#define GColorElectricBlue 21
#define GColorScreaminGreen 22
#define GColorWindsorTan 23
#define GColorRajah 24
#define GColorIcterine 25
#define GColorDarkCandyAppleRed 26
static inline GColor GColorFromRGB(int r, int g, int b) {
  (void)r;
  (void)g;
  (void)b;
  return (GColor)0;
}

typedef enum { GCornerNone = 0 } GCornerMask;

typedef enum {
  GTextOverflowModeWordWrap,
  GTextOverflowModeTrailingEllipsis,
  GTextOverflowModeFill
} GTextOverflowMode;

typedef enum { GTextAlignmentLeft, GTextAlignmentCenter, GTextAlignmentRight } GTextAlignment;

typedef const void* GFont;
typedef void* ResHandle;
// Opaque; the SDK version exposes text-attribute fields the face never uses.
typedef struct GTextAttributes GTextAttributes;
typedef int32_t HealthValue;

#define FONT_KEY_GOTHIC_14_BOLD "FONT_KEY_GOTHIC_14_BOLD"
#define FONT_KEY_GOTHIC_18_BOLD "FONT_KEY_GOTHIC_18_BOLD"
#define FONT_KEY_ROBOTO_BOLD_SUBSET_49 "FONT_KEY_ROBOTO_BOLD_SUBSET_49"
#define FONT_KEY_LECO_60_NUMBERS_AM_PM "font"
#define RESOURCE_ID_FONT_VGA_16 1
#define RESOURCE_ID_FONT_VGA_64 2

typedef enum {
  APP_MSG_OK = 0,
  APP_MSG_SEND_TIMEOUT,
  APP_MSG_SEND_REJECTED,
  APP_MSG_NOT_CONNECTED,
  APP_MSG_APP_NOT_RUNNING,
  APP_MSG_INVALID_ARGS,
  APP_MSG_BUSY,
  APP_MSG_BUFFER_OVERFLOW,
  APP_MSG_ALREADY_RELEASED,
  APP_MSG_CALLBACK_ALREADY_REGISTERED,
  APP_MSG_CALLBACK_NOT_REGISTERED,
  APP_MSG_OUT_OF_MEMORY,
  APP_MSG_CLOSED,
  APP_MSG_INTERNAL_ERROR,
  APP_MSG_INVALID_STATE,
} AppMessageResult;

typedef enum {
  TUPLE_BYTE_ARRAY = 0,
  TUPLE_CSTRING = 1,
  TUPLE_UINT = 2,
  TUPLE_INT = 3,
} TupleType;

typedef struct {
  uint32_t key;
  TupleType type;
  uint16_t length;
  union {
    uint8_t data[1];
    char cstring[1];
    uint32_t uint32;
    int32_t int32;
    uint16_t uint16;
    int16_t int16;
    uint8_t uint8;
    int8_t int8;
  } value[];
} Tuple;

typedef struct {
  uint8_t charge_percent;
  bool is_charging;
  bool is_plugged;
} BatteryChargeState;

typedef struct {
  void (*pebble_app_connection_handler)(bool connected);
  void (*pebblekit_connection_handler)(bool connected);
} ConnectionHandlers;

typedef uint32_t AnimationProgress;
typedef struct {
  void (*will_change)(GRect final_unobstructed_pixel_area, void* context);
  void (*change)(AnimationProgress progress, void* context);
  void (*did_change)(void* context);
} UnobstructedAreaHandlers;

typedef enum {
  HealthEventSignificantUpdate,
  HealthEventMovementUpdate,
  HealthEventSleepUpdate,
  HealthEventMetricAlert,
  HealthEventHeartRateUpdate
} HealthEventType;

typedef enum {
  HealthMetricStepCount,
  HealthMetricActiveSeconds,
  HealthMetricWalkedDistanceMeters,
  HealthMetricSleepSeconds,
  HealthMetricSleepRestfulSeconds,
  HealthMetricHeartRateBPM,
  HealthMetricHeartRateRawBPM
} HealthMetric;

typedef enum {
  HealthServiceAccessibilityMaskAvailable = 1 << 0,
  HealthServiceAccessibilityMaskNoPermission = 1 << 1,
  HealthServiceAccessibilityMaskNotSupported = 1 << 2
} HealthServiceAccessibilityMask;

typedef enum {
  HealthServiceTimeScopeOnce,
  HealthServiceTimeScopeDaily,
  HealthServiceTimeScopeWeekly
} HealthServiceTimeScope;

typedef enum {
  SECONDS_UNIT = 1 << 0,
  MINUTE_UNIT = 1 << 1,
  HOUR_UNIT = 1 << 2,
  DAY_UNIT = 1 << 3,
  MONTH_UNIT = 1 << 4,
  YEAR_UNIT = 1 << 5
} TimeUnits;

typedef struct {
  void (*load)(Window* window);
  void (*appear)(Window* window);
  void (*disappear)(Window* window);
  void (*unload)(Window* window);
} WindowHandlers;

#define APP_LOG_LEVEL_ERROR 1
#define APP_LOG_LEVEL_WARNING 2
#define APP_LOG_LEVEL_INFO 3
#define APP_LOG_LEVEL_DEBUG 4
#define APP_LOG_LEVEL_DEBUG_VERBOSE 5

#define SECONDS_PER_DAY 86400

// The real SDK generates these as extern uint32_t variables (assigned from the
// messageKeys list at build time), not #define constants. The mock matches that
// linkage so address-taking compiles here exactly as it does in the target build.
extern uint32_t MESSAGE_KEY_WEATHER_TEMP;
extern uint32_t MESSAGE_KEY_WEATHER_COND;
extern uint32_t MESSAGE_KEY_SETTINGS_THEME;
extern uint32_t MESSAGE_KEY_SETTINGS_UNITS;
extern uint32_t MESSAGE_KEY_SETTINGS_DATE_FORMAT;
extern uint32_t MESSAGE_KEY_WEATHER_HIGH;
extern uint32_t MESSAGE_KEY_WEATHER_LOW;
extern uint32_t MESSAGE_KEY_WEATHER_AQI;
extern uint32_t MESSAGE_KEY_WEATHER_UV;
extern uint32_t MESSAGE_KEY_SLOT_1;
extern uint32_t MESSAGE_KEY_SLOT_2;
extern uint32_t MESSAGE_KEY_SLOT_3;
extern uint32_t MESSAGE_KEY_SLOT_4;
extern uint32_t MESSAGE_KEY_SLOT_5;
extern uint32_t MESSAGE_KEY_SLOT_6;
extern uint32_t MESSAGE_KEY_SETTINGS_DOW_POSITION;
extern uint32_t MESSAGE_KEY_WEATHER_HUMIDITY;
extern uint32_t MESSAGE_KEY_WEATHER_PCP;
extern uint32_t MESSAGE_KEY_WEATHER_PRECIP_NOW;
extern uint32_t MESSAGE_KEY_WEATHER_LOW_TOMORROW;
extern uint32_t MESSAGE_KEY_WEATHER_TEMP_HIGH_TOMORROW;
extern uint32_t MESSAGE_KEY_WEATHER_HI_HOUR_TODAY;
extern uint32_t MESSAGE_KEY_WEATHER_LO_HOUR_TODAY;
extern uint32_t MESSAGE_KEY_WEATHER_HI_HOUR_TOMORROW;
extern uint32_t MESSAGE_KEY_WEATHER_LO_HOUR_TOMORROW;
extern uint32_t MESSAGE_KEY_SETTINGS_SHORT_DATE_FORMAT;
extern uint32_t MESSAGE_KEY_SETTINGS_DISCONNECT_VIBE;
extern uint32_t MESSAGE_KEY_WEATHER_WIND_DIRECTION;
extern uint32_t MESSAGE_KEY_WEATHER_WIND_SPEED;
extern uint32_t MESSAGE_KEY_WEATHER_REQUEST;

// Handler typedefs and result types, spelled as the SDK umbrella header has
// them so prototypes below can match the SDK verbatim.
typedef void (*AppMessageInboxReceived)(DictionaryIterator* iterator, void* context);
typedef void (*AppMessageInboxDropped)(AppMessageResult reason, void* context);
typedef void (*AppMessageOutboxSent)(DictionaryIterator* iterator, void* context);
typedef void (*AppMessageOutboxFailed)(DictionaryIterator* iterator, AppMessageResult reason,
                                       void* context);
typedef void (*AppTimerCallback)(void* data);
typedef void (*BatteryStateHandler)(BatteryChargeState charge);
typedef void (*HealthEventHandler)(HealthEventType event, void* context);
typedef void (*TickHandler)(struct tm* tick_time, TimeUnits units_changed);
typedef void (*LayerUpdateProc)(struct Layer* layer, GContext* ctx);

typedef enum { DICT_OK = 0, DICT_NOT_ENOUGH_STORAGE, DICT_INVALID_ARGS } DictionaryResult;

typedef enum { S_SUCCESS = 0, E_ERROR = -1 } StatusCode;
typedef int32_t status_t;

// --- Function Prototypes ---
void app_event_loop(void);
AppMessageResult app_message_open(const uint32_t size_inbound, const uint32_t size_outbound);
AppMessageResult app_message_outbox_begin(DictionaryIterator** iterator);
AppMessageResult app_message_outbox_send(void);
AppMessageInboxDropped app_message_register_inbox_dropped(AppMessageInboxDropped dropped_callback);
AppMessageInboxReceived app_message_register_inbox_received(
    AppMessageInboxReceived received_callback);
AppMessageOutboxSent app_message_register_outbox_sent(AppMessageOutboxSent sent_callback);
AppMessageOutboxFailed app_message_register_outbox_failed(AppMessageOutboxFailed failed_callback);
AppTimer* app_timer_register(uint32_t timeout_ms, AppTimerCallback callback, void* callback_data);
bool app_timer_reschedule(AppTimer* timer, uint32_t new_timeout_ms);
void app_timer_cancel(AppTimer* timer);
BatteryChargeState battery_state_service_peek(void);
void battery_state_service_subscribe(BatteryStateHandler handler);
bool clock_is_24h_style(void);
bool connection_service_peek_pebble_app_connection(void);
void connection_service_subscribe(ConnectionHandlers handlers);
bool quiet_time_is_active(void);
Tuple* dict_find(const DictionaryIterator* iter, const uint32_t key);
DictionaryResult dict_write_uint8(DictionaryIterator* iter, const uint32_t key,
                                  const uint8_t value);
GFont fonts_get_system_font(const char* font_key);
ResHandle resource_get_handle(uint32_t resource_id);
GFont fonts_load_custom_font(ResHandle handle);
void fonts_unload_custom_font(GFont font);
bool grect_equal(const GRect* const rect_a, const GRect* const rect_b);
void graphics_context_set_fill_color(GContext* ctx, GColor color);
void graphics_context_set_stroke_color(GContext* ctx, GColor color);
void graphics_context_set_stroke_width(GContext* ctx, uint8_t stroke_width);
void graphics_context_set_text_color(GContext* ctx, GColor color);
void graphics_draw_line(GContext* ctx, GPoint p0, GPoint p1);
void graphics_draw_text(GContext* ctx, const char* text, GFont const font, const GRect box,
                        const GTextOverflowMode overflow_mode, const GTextAlignment alignment,
                        GTextAttributes* text_attributes);
void graphics_fill_rect(GContext* ctx, GRect rect, uint16_t corner_radius, GCornerMask corner_mask);
bool health_service_events_subscribe(HealthEventHandler handler, void* context);
bool health_service_events_unsubscribe(void);
HealthServiceAccessibilityMask health_service_metric_accessible(HealthMetric metric,
                                                                time_t time_start, time_t time_end);
HealthServiceAccessibilityMask health_service_metric_averaged_accessible(
    HealthMetric metric, time_t time_start, time_t time_end, HealthServiceTimeScope scope);
HealthValue health_service_peek_current_value(HealthMetric metric);
HealthValue health_service_sum_averaged(HealthMetric metric, time_t time_start, time_t time_end,
                                        HealthServiceTimeScope scope);
HealthValue health_service_sum_today(HealthMetric metric);
void layer_add_child(Layer* parent, Layer* child);
Layer* layer_create(GRect frame);
void layer_destroy(Layer* layer);
GRect layer_get_bounds(const Layer* layer);
GRect layer_get_unobstructed_bounds(const Layer* layer);
void layer_mark_dirty(Layer* layer);
void layer_set_update_proc(Layer* layer, LayerUpdateProc update_proc);
bool persist_exists(const uint32_t key);
int32_t persist_read_int(const uint32_t key);
status_t persist_write_int(const uint32_t key, const int32_t value);
int persist_write_string(const uint32_t key, const char* cstring);
int persist_read_string(const uint32_t key, char* buffer, const size_t buffer_size);
// Test helpers/knobs below are not part of the real SDK
void mock_persist_reset(void);
void mock_reset(void);
extern int mock_persist_write_count;
extern int32_t mock_heart_rate;
extern int mock_health_accessible_count;
extern int mock_health_sum_today_count;
extern int mock_health_peek_count;
// Per-metric permission knob, indexed by HealthMetric; a value without the
// Available bit routes update_health_info() down its sentinel branch.
#define MOCK_HEALTH_METRIC_COUNT 7
extern HealthServiceAccessibilityMask mock_health_accessible[MOCK_HEALTH_METRIC_COUNT];
extern time_t mock_time_offset;
extern bool mock_quiet_time_active;
extern uint8_t mock_battery_percent;
extern bool mock_battery_charging;
extern bool mock_bt_connected;
extern bool mock_clock_24h;
extern bool mock_outbox_begin_ok;
extern int mock_vibes_count;
extern int mock_outbox_sends;
extern int mock_mark_dirty_count;
extern int mock_set_text_count;
extern int mock_set_text_color_count;
extern char mock_last_text[32];  // most recent text_layer_set_text payload
// Subscription recording: init()'s wiring is asserted, not assumed.
extern int mock_tick_subscribe_count;
extern TimeUnits mock_tick_units;
extern int mock_battery_subscribe_count;
extern int mock_connection_subscribe_count;
extern int mock_health_subscribe_count;
extern int mock_unobstructed_subscribe_count;
extern int mock_inbox_received_count;
extern int mock_inbox_dropped_count;
extern int mock_outbox_sent_count;
extern int mock_outbox_failed_count;
extern int mock_wordwrap_calls;
extern int mock_bar_glyph_calls;
#define MOCK_MAX_FILL_RECTS 128
extern GRect mock_fill_rects[MOCK_MAX_FILL_RECTS];
extern GColor mock_fill_rect_colors[MOCK_MAX_FILL_RECTS];
extern int mock_fill_rect_count;
void mock_fill_rect_reset(void);

// Canvas text runs, recorded per draw_run with their active text color so
// tests can see accents (e.g. a `mark` unit run) the way mock_fill_rects
// lets them see bands.
#define MOCK_MAX_TEXT_RUNS 256
extern char mock_text_runs[MOCK_MAX_TEXT_RUNS][32];
extern GColor mock_text_run_colors[MOCK_MAX_TEXT_RUNS];
extern GRect mock_text_run_boxes[MOCK_MAX_TEXT_RUNS];
extern int mock_text_run_count;
void mock_text_runs_reset(void);
extern GRect mock_unobstructed_bounds;
void mock_dict_reset(void);
void mock_dict_add_int(uint32_t key, int32_t value);
void mock_dict_add_int_width(uint32_t key, int32_t value, uint16_t width);
void mock_dict_add_uint_width(uint32_t key, uint32_t value, uint16_t width);
void mock_dict_add_cstring(uint32_t key, const char* str);
TextLayer* text_layer_create(GRect frame);
void text_layer_destroy(TextLayer* text_layer);
Layer* text_layer_get_layer(TextLayer* text_layer);
void text_layer_set_background_color(TextLayer* text_layer, GColor color);
void text_layer_set_font(TextLayer* text_layer, GFont font);
void text_layer_set_text(TextLayer* text_layer, const char* text);
void text_layer_set_text_alignment(TextLayer* text_layer, GTextAlignment text_alignment);
void text_layer_set_text_color(TextLayer* text_layer, GColor color);
void tick_timer_service_subscribe(TimeUnits tick_units, TickHandler handler);
void unobstructed_area_service_subscribe(UnobstructedAreaHandlers handlers, void* context);
time_t time_start_of_today(void);
void vibes_double_pulse(void);
Window* window_create(void);
void window_destroy(Window* window);
Layer* window_get_root_layer(const Window* window);
void window_set_background_color(Window* window, GColor background_color);
void window_set_window_handlers(Window* window, WindowHandlers handlers);
void window_stack_push(Window* window, bool animated);
void APP_LOG(uint8_t level, const char* fmt, ...);
