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

#define MESSAGE_KEY_WEATHER_TEMP 100
#define MESSAGE_KEY_WEATHER_COND 101
#define MESSAGE_KEY_SETTINGS_THEME 102
#define MESSAGE_KEY_SETTINGS_UNITS 103
#define MESSAGE_KEY_SETTINGS_DATE_FORMAT 104
#define MESSAGE_KEY_WEATHER_SUNRISE 105
#define MESSAGE_KEY_WEATHER_SUNSET 106
#define MESSAGE_KEY_WEATHER_HIGH 107
#define MESSAGE_KEY_WEATHER_LOW 108
#define MESSAGE_KEY_WEATHER_AQI 109
#define MESSAGE_KEY_WEATHER_UV 110
#define MESSAGE_KEY_SLOT_1 112
#define MESSAGE_KEY_SLOT_2 113
#define MESSAGE_KEY_SLOT_3 114
#define MESSAGE_KEY_SLOT_4 115
#define MESSAGE_KEY_SLOT_5 116
#define MESSAGE_KEY_SLOT_6 119
#define MESSAGE_KEY_SETTINGS_DOW_POSITION 120
#define MESSAGE_KEY_UTC_OFFSET 117
#define MESSAGE_KEY_SETTINGS_SHORT_DATE_FORMAT 118

// --- Function Prototypes ---
void app_event_loop(void);
void app_message_open(uint32_t size_inbound, uint32_t size_outbound);
void app_message_outbox_begin(DictionaryIterator** iterator);
void app_message_outbox_send(void);
void app_message_register_inbox_dropped(void (*callback)(AppMessageResult reason, void* context));
void app_message_register_inbox_received(void (*callback)(DictionaryIterator* iterator,
                                                          void* context));
BatteryChargeState battery_state_service_peek(void);
void battery_state_service_subscribe(void (*handler)(BatteryChargeState charge));
bool clock_is_24h_style(void);
bool connection_service_peek_pebble_app_connection(void);
void connection_service_subscribe(ConnectionHandlers handlers);
Tuple* dict_find(const DictionaryIterator* iter, uint32_t key);
void dict_write_uint8(DictionaryIterator* iter, uint32_t key, uint8_t value);
GFont fonts_get_system_font(const char* font_key);
ResHandle resource_get_handle(uint32_t resource_id);
GFont fonts_load_custom_font(ResHandle handle);
void fonts_unload_custom_font(GFont font);
void graphics_context_set_fill_color(GContext* ctx, GColor color);
void graphics_context_set_stroke_color(GContext* ctx, GColor color);
void graphics_context_set_stroke_width(GContext* ctx, uint8_t stroke_width);
void graphics_context_set_text_color(GContext* ctx, GColor color);
void graphics_draw_line(GContext* ctx, GPoint p0, GPoint p1);
void graphics_draw_text(GContext* ctx, const char* text, GFont font, GRect box,
                        GTextOverflowMode overflow_mode, GTextAlignment alignment,
                        GContext* layout_cache);
void graphics_fill_rect(GContext* ctx, GRect rect, uint16_t corner_radius, GCornerMask corner_mask);
void health_service_events_subscribe(void (*handler)(HealthEventType event, void* context),
                                     void* context);
void health_service_events_unsubscribe(void);
HealthServiceAccessibilityMask health_service_metric_accessible(HealthMetric metric,
                                                                time_t time_start, time_t time_end);
HealthServiceAccessibilityMask health_service_metric_averaged_accessible(
    HealthMetric metric, time_t time_start, time_t time_end, HealthServiceTimeScope scope);
int32_t health_service_peek_current_value(HealthMetric metric);
int32_t health_service_sum_averaged(HealthMetric metric, time_t time_start, time_t time_end,
                                    HealthServiceTimeScope scope);
int32_t health_service_sum_today(HealthMetric metric);
void layer_add_child(Layer* parent, Layer* child);
Layer* layer_create(GRect frame);
void layer_destroy(Layer* layer);
GRect layer_get_bounds(Layer* layer);
void layer_mark_dirty(Layer* layer);
void layer_set_hidden(Layer* layer, bool hidden);
void layer_set_update_proc(Layer* layer, void (*update_proc)(Layer* layer, GContext* ctx));
bool persist_exists(const uint32_t key);
int32_t persist_read_int(const uint32_t key);
void persist_write_int(const uint32_t key, const int32_t value);
int persist_write_string(const uint32_t key, const char* cstring);
int persist_read_string(const uint32_t key, char* buffer, const size_t buffer_size);
// Test helpers/knobs below are not part of the real SDK
void mock_persist_reset(void);
extern int32_t mock_heart_rate;
extern int mock_health_accessible_count;
extern int mock_health_sum_today_count;
extern int mock_health_peek_count;
extern int mock_vibes_count;
extern int mock_outbox_sends;
extern int mock_mark_dirty_count;
extern int mock_set_text_count;
extern int mock_set_text_color_count;
extern int mock_wordwrap_calls;
extern int mock_bar_glyph_calls;
#define MOCK_MAX_FILL_RECTS 128
extern GRect mock_fill_rects[MOCK_MAX_FILL_RECTS];
extern int mock_fill_rect_count;
void mock_fill_rect_reset(void);
void mock_dict_reset(void);
void mock_dict_add_int(uint32_t key, int32_t value);
void mock_dict_add_cstring(uint32_t key, const char* str);
TextLayer* text_layer_create(GRect frame);
void text_layer_destroy(TextLayer* text_layer);
Layer* text_layer_get_layer(TextLayer* text_layer);
void text_layer_set_background_color(TextLayer* text_layer, GColor color);
void text_layer_set_font(TextLayer* text_layer, GFont font);
void text_layer_set_text(TextLayer* text_layer, const char* text);
void text_layer_set_text_alignment(TextLayer* text_layer, GTextAlignment text_alignment);
void text_layer_set_text_color(TextLayer* text_layer, GColor color);
void tick_timer_service_subscribe(TimeUnits tick_units,
                                  void (*handler)(struct tm* tick_time, TimeUnits units_changed));
time_t time_start_of_today(void);
void vibes_double_pulse(void);
Window* window_create(void);
void window_destroy(Window* window);
Layer* window_get_root_layer(Window* window);
void window_set_background_color(Window* window, GColor background_color);
void window_set_window_handlers(Window* window, WindowHandlers handlers);
void window_stack_push(Window* window, bool animated);
void APP_LOG(uint8_t level, const char* fmt, ...);
