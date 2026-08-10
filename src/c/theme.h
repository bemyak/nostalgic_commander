#pragma once
#include <pebble.h>
#include "data.h"

typedef struct {
  GColor center_bg;
  GColor accent_cold;  // cold-temperature readings
  GColor frame;        // ASCII window borders and corner crosses
  GColor text_primary;
  GColor text_secondary;
  GColor mark;        // shortkey hints in the NC menu idiom: trailing units, the
                      // cond word, the date weekday, the beats "@", the bpm heart
  GColor status_ink;  // text drawn on top of a status-colored fill
  GColor status_green;
  GColor status_yellow;
  GColor status_red;
} WatchTheme;

extern const WatchTheme* s_active_theme;

// Every theme is a DOS one, built from the canonical CGA/EGA 16.
extern const WatchTheme s_theme_panel;      // Norton Commander's blue panel
extern const WatchTheme s_theme_shadow;     // the same panel, in shadow
extern const WatchTheme s_theme_dialog;     // the Turbo Vision dialog box
extern const WatchTheme s_theme_navigator;  // DOS Navigator's default screen

// Settings 1-4 pin one theme; 0 (retired Auto) and anything unrecognized
// fall back to Norton.
const WatchTheme* determine_theme(int theme_setting);
void apply_theme(void);

// Which reading earns which status color lives over in status.c/h.
