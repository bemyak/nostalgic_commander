#pragma once
#include <pebble.h>

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

// Where a value sits inside its window, measured from the box origin. Shared by
// the canvas-drawn complications and the TextLayer path in main.c, so a slot
// lands in the same place whichever of the two renders it.
#define VALUE_ROW_DY 10
#define VALUE_ROW_H 24

// Clearance between a status band and the frame it sits in. Matches the gap
// left below the band, so the fill is inset evenly on three sides — above it
// there is more room, because the value row sits low to clear the title.
#define STATUS_BAND_PAD 4

// The full-weather centre strip: four chips of 4/3/4/4 cells plus one-cell
// gaps, centred in the row. A test asserts the field table's cells plus gaps
// equal this, so a width edit can't silently desync the strip; captions and
// chip values share the same centring formula (drawing.c) so they stay
// registered to each other.
#define FULL_WEATHER_STRIP_CELLS 18

// The combined BT/QT window widens its layout at top-slot width: the single
// "BT/QT" title splits into one caption stub per checkbox (the frame runs
// through between them) and the boxes gain an air cell: two 3-cell boxes
// plus the gap. A test pins the threshold, so a layout edit can't silently
// retune the switch.
#define BT_QT_SPLIT_MIN_W 93
#define BT_QT_STRIP_CELLS 7

extern Window* s_main_window;
extern Layer* s_canvas_layer;
extern TextLayer* s_time_layer;

void draw_ascii_window(GContext* ctx, GRect rect, const char* title);

// Width switch for the combined BT/QT window (see BT_QT_SPLIT_MIN_W).
bool bt_qt_split_captions(int width);
void canvas_update_proc(Layer* layer, GContext* ctx);
// Build a snapshot of all displayed state and schedule the one full-tree
// render iff it differs from what the last render drew. Safe to call from
// any event handler; no-ops when nothing visible changed.
void request_ui_redraw(void);

// Clear the snapshot and layer-text caches. main_window_load calls this:
// fresh layers hold no text, so the next request_ui_redraw() must apply
// unconditionally rather than match a previous layer tree's snapshot.
void reset_ui_snapshot(void);

// Two baked sizes of the same VGA 8x16 bitmap TTF, loaded once at init.
GFont vga_font_16(void);
GFont vga_font_64(void);
