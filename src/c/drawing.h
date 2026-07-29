#pragma once
#include <pebble.h>

extern Window* s_main_window;
extern Layer* s_canvas_layer;
extern TextLayer* s_time_layer;
extern TextLayer* s_date_iso_layer;

void draw_sidebar_complication(GContext* ctx, GRect bounds, int source, bool from_top);
void draw_ascii_window(GContext* ctx, GRect rect, const char* title);
void canvas_update_proc(Layer* layer, GContext* ctx);
void refresh_complications();

// Two baked sizes of the same VGA 8x16 bitmap TTF, loaded once at init.
GFont vga_font_16(void);
GFont vga_font_64(void);
