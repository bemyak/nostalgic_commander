#pragma once
#include <pebble.h>
#include "data.h"  // ComplicationDataSource for the drawer prototypes

// Frame stroke. Neighbours in a row overlap by this much so their borders
// coincide into a single shared divider instead of stacking into a double one.
#define WINDOW_BORDER_PX 2

// The title's 16px cell renders its ink in the lower 10 rows, so its centre
// line sits below the box origin. Dropping the top border by this much puts the
// line through the middle of the title instead of along its top edge.
#define TITLE_BORDER_DROP 2

// Metrics of the VGA 8x16 bitmap font at size 16.
#define VGA16_CHAR_W 8
#define VGA16_CELL_H 16

// Where a value sits inside its window, measured from the box origin. Shared
// by every slot's canvas value run; the big clock row is the lone TextLayer
// left.
#define VALUE_ROW_DY 10
#define VALUE_ROW_H 24

// Clearance between a status band and the frame it sits in. Matches the gap
// left below the band, so the fill is inset evenly on three sides — above it
// there is more room, because the value row sits low to clear the title.
#define STATUS_BAND_PAD 4

// The full-weather centre strip: four chips of 4/4/4/4 cells plus one-cell
// gaps, centred in the row. A test asserts the field table's cells plus gaps
// equal this, so a width edit can't silently desync the strip; captions and
// chip values share the same centring formula (drawing.c) so they stay
// registered to each other.
#define FULL_WEATHER_STRIP_CELLS 19

// The combined BT/QT window widens its layout at top-slot width: the single
// "BT/QT" title splits into one caption stub per checkbox (the frame runs
// through between them) and the boxes gain an air cell: two 3-cell boxes
// plus the gap. A test pins the threshold, so a layout edit can't silently
// retune the switch.
#define BT_QT_SPLIT_MIN_W 93
#define BT_QT_STRIP_CELLS 7

// The HI/LO window's value strip: two 4-cell halves with one air cell between.
#define HI_LO_STRIP_CELLS 9

// HUM/PCP two-field rhythm: 3-cell fields with a 2-cell gap, centred as one
// strip so the pair reads close together and off the frame.
#define HUM_PCP_FIELD_CELLS 3
#define HUM_PCP_GAP_CELLS 2

// The canvas main.c creates and everything draws on; request_ui_redraw()
// marks it dirty. s_main_window and the clock layer live over in main.h.
extern Layer* s_canvas_layer;

void draw_ascii_window(GContext* ctx, GRect rect, const char* title);

// Value painters. The three idioms cover the atomic sources; the rest are
// bespoke per source. These exist to populate ComplicationSpec.draw — not
// general API; the canvas reaches them through the registry
// (complication.c), never directly.
void draw_shortkey_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source);
void draw_plain_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source);
void draw_banded_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source);
void draw_battery_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source);
void draw_battery_bar_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source);
void draw_steps_bar_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source);
void draw_heart_rate_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source);
void draw_weather_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source);
void draw_hi_lo_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source);
void draw_weather_full_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source);
void draw_hum_pcp_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source);
void draw_aqi_uv_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source);
void draw_wind_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source);
void draw_bt_qt_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source);
void draw_beats_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source);
void draw_short_date_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source);
void draw_full_date_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source);

// Top-row-width slots lay composite windows out wide (split captions, units
// kept); narrow slots get the tight form. Threshold: BT_QT_SPLIT_MIN_W.
bool is_wide_slot(int width);
void canvas_update_proc(Layer* layer, GContext* ctx);
// Build a snapshot of all displayed state and schedule the one full-tree
// render iff it differs from what the last render drew. Safe to call from
// any event handler; no-ops when nothing visible changed.
void request_ui_redraw(void);

// Clear the render-gate snapshot. main_window_load calls this: a fresh layer
// tree must repaint unconditionally, even if it reuses a previous tree's
// state globals.
void reset_ui_snapshot(void);

// Two baked sizes of the same VGA 8x16 bitmap TTF, loaded once at init.
GFont vga_font_16(void);
GFont vga_font_64(void);
