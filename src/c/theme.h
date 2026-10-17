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

// The field's own 2-bit grey level, and the CRT vignette's master switch for a
// theme. Nonzero means "a grey field the falloff can key off": it selects the
// ease-in curve (a smoothstep sweep across a bright field reads as pepper) and
// turns on ink/field separation — crt.c's clear_of_ground keeps ink one step
// clear of whatever the field renders as, without which the falloff quantizes
// a caption and its ground onto the same level.
//
// Derived from center_bg rather than stored on the theme: there is exactly one
// right answer per theme, so a stored copy is a second place to keep correct
// and a way for the pass to separate ink against a level the screen is not
// showing. 0 covers Black and every ground without a level (Norton's blue)
// alike; nothing needs to tell those apart, since both mean "no separation,
// take the dark curve".
uint8_t theme_ground_level(const WatchTheme* t);

// The DOS themes keep to the canonical CGA/EGA 16; Phosphor and Amber are a
// monitor's single phosphor.
extern const WatchTheme s_theme_panel;      // Norton Commander's blue panel
extern const WatchTheme s_theme_shadow;     // the same panel, in shadow
extern const WatchTheme s_theme_dialog;     // the Turbo Vision dialog box
extern const WatchTheme s_theme_navigator;  // DOS Navigator's default screen
extern const WatchTheme s_theme_phosphor;   // the IBM 5151's green phosphor
extern const WatchTheme s_theme_amber;      // the amber monitors' phosphor

// The theme setting's values, equal to the option values of the theme select in
// src/pkjs/config.js. Anything else falls back to Norton.
enum {
  THEME_SETTING_DIALOG = 1,
  THEME_SETTING_PANEL = 2,
  THEME_SETTING_SHADOW = 3,
  THEME_SETTING_NAVIGATOR = 4,
  THEME_SETTING_PHOSPHOR = 5,
  THEME_SETTING_AMBER = 6,
};
const WatchTheme* determine_theme(int theme_setting);
void apply_theme(void);

// Which reading earns which status color lives over in status.c/h.
