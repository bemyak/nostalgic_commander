#pragma once
#include <pebble.h>

// All face geometry lives here: the margin columns every frame spans, the
// six slot rects, the TIME window, and the clock layer inside it. Tuned to
// the 200x228 emery display — nothing here is responsive.

// Every frame spans these columns: an 8px margin on each edge, matching the
// vertical margins. 184 is 23 whole character cells, so the frame lines up
// with the font's 8px column grid.
#define LAYOUT_X 8
#define LAYOUT_W 184

// Slot rows tile LAYOUT_X..LAYOUT_X+LAYOUT_W-1. Neighbours overlap by the 2px
// frame stroke (drawing.h's WINDOW_BORDER_PX) so their borders coincide into
// a single shared divider instead of stacking into a double-width one.
// Initializer form (nested braces): they feed s_complication_slots' static
// initializer in data.c, where the GRect() cast form is not constant.
#define SLOT_RECT_TOP_LEFT {{LAYOUT_X, 8}, {93, 36}}
#define SLOT_RECT_TOP_RIGHT {{99, 8}, {93, 36}}
#define SLOT_RECT_BOTTOM_LEFT {{LAYOUT_X, 184}, {63, 36}}
#define SLOT_RECT_BOTTOM_CENTER {{69, 184}, {62, 36}}
#define SLOT_RECT_BOTTOM_RIGHT {{129, 184}, {63, 36}}
// The wide centre row.
#define SLOT_RECT_CENTER {{LAYOUT_X, 142}, {LAYOUT_W, 36}}

// The fixed centre window holding the clock.
#define TIME_WINDOW_X LAYOUT_X
#define TIME_WINDOW_Y 50
#define TIME_WINDOW_W LAYOUT_W
#define TIME_WINDOW_H 86
#define TIME_WINDOW_RECT GRect(TIME_WINDOW_X, TIME_WINDOW_Y, TIME_WINDOW_W, TIME_WINDOW_H)

// The VGA 8x16 font baked at size 64: cells are 64px tall. Feeds the clock
// layer below (and nothing else — the registry-era geometry uses the 16px
// metrics from drawing.h).
#define VGA64_CELL_H 64

// The clock TextLayer: one 64pt cell high, inset 4px per side, and a
// deliberate 1px up-overlap past the window's top edge — the digits' ink
// renders low in the cell, so the overlap keeps the clock optically centred.
#define CLOCK_RECT GRect(TIME_WINDOW_X + 4, TIME_WINDOW_Y - 1, TIME_WINDOW_W - 8, VGA64_CELL_H)
