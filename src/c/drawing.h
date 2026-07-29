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

extern Window* s_main_window;
extern Layer* s_canvas_layer;
extern TextLayer* s_time_layer;

void draw_ascii_window(GContext* ctx, GRect rect, const char* title);
void canvas_update_proc(Layer* layer, GContext* ctx);
void refresh_complications();

// Two baked sizes of the same VGA 8x16 bitmap TTF, loaded once at init.
GFont vga_font_16(void);
GFont vga_font_64(void);
