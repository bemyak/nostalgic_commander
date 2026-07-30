#pragma once
#include <pebble.h>
#include "data.h"

typedef struct {
  GColor center_bg;
  GColor accent_cold;  // cold-temperature readings
  GColor frame;        // ASCII window borders and corner crosses
  GColor text_primary;
  GColor text_secondary;
  GColor mark;        // accented characters: unit letter, weekday, the beats "@"
  GColor status_ink;  // text drawn on top of a status-colored fill
  GColor status_green;
  GColor status_yellow;
  GColor status_red;
} WatchTheme;

extern const WatchTheme* s_active_theme;

// Every theme is a DOS one, built from the canonical CGA/EGA 16.
extern const WatchTheme s_theme_panel;   // Norton Commander's blue panel
extern const WatchTheme s_theme_shadow;  // the same panel, in shadow
extern const WatchTheme s_theme_dialog;  // the Turbo Vision dialog box

// Setting 0 is Auto, which picks by the hour; 1-3 pin one theme.
const WatchTheme* determine_theme(int theme_setting, int current_hour);
void apply_theme(struct tm* tick_time);
GColor get_source_color(ComplicationDataSource source);
