#pragma once
#include <pebble.h>

// Owned by main.c, with lifecycle. Read by drawers/rendering as needed.
extern Window* s_main_window;
extern TextLayer* s_time_layer;

void refresh_state(void);
