#pragma once
#include <pebble.h>

// Owned by main.c, with lifecycle. Read by drawers/rendering as needed.
extern Window* s_main_window;

void refresh_state(void);
void weather_retries_reset(void);
// Re-derives which services are subscribed from the slots and settings;
// call after either changes.
void refresh_subscriptions(void);
