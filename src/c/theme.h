#pragma once
#include <pebble.h>
#include "data.h"

typedef struct {
  GColor center_bg;
  GColor sidebar_bg;
  GColor steps_fill;
  GColor frame;  // ASCII window borders and corner crosses
  GColor text_primary;
  GColor text_secondary;
  GColor status_green;
  GColor status_yellow;
  GColor status_red;
} WatchTheme;

extern const WatchTheme* s_active_theme;

extern const WatchTheme s_theme_night;
extern const WatchTheme s_theme_day;
extern const WatchTheme s_theme_commander_night;
extern const WatchTheme s_theme_commander_day;

const WatchTheme* determine_theme(int theme_setting, int current_hour);
void apply_theme();
GColor get_source_color(ComplicationDataSource source);
