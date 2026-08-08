#define TEST_ENV

#include "unity/src/unity.h"
#include "pebble.h"

// Include the implementation file directly so we can test its static functions
#include "../src/c/data.c"
#include "../src/c/theme.c"
#include "../src/c/drawing.c"
#include "../src/c/messaging.c"
#include "../src/c/main.c"

void setUp(void) {
  // Reset any global states if needed before each test
  s_settings_theme = 0;  // Auto
  s_settings_units = 0;  // Imperial
  s_settings_disconnect_vibe = 1;
  s_battery_level = 100;
  s_battery_charging = false;
  s_step_count = -1;
  s_sleep_seconds = -1;
  s_heart_rate = 0;
  s_weather_temp = -999;
  strcpy(s_weather_cond, "--");
  // Leak-proofing: these weather globals used to keep whatever the previous
  // test left in them.
  s_temp_high = -999;
  s_temp_low = -999;
  s_weather_pcp = -1;
  s_precip_now = -1;
  s_temp_low_tmrw = -999;
  s_temp_high_tmrw = -999;
  s_lo_hour_today = -1;
  s_hi_hour_today = -1;
  s_lo_hour_tmrw = -1;
  s_hi_hour_tmrw = -1;
  s_wall_hour = 8;  // morning: a neutral phase for tests that don't care
  s_connected = true;
  s_quiet_time_active = false;
  s_quick_view_active = false;
  mock_unobstructed_bounds = GRect(0, 0, 200, 228);
  mock_set_hidden_count = 0;
  mock_quiet_time_active = false;
  // Isolate the timestamp-based health throttle: zero both the mock clock
  // offset and the last-refresh stamp so no test inherits a window.
  mock_time_offset = 0;
  s_last_throttled_health_refresh = 0;
  s_weather_wind_direction = -1;
  s_weather_wind_speed = -1;
  // main_window_load() reads the theme like init() applies one first; the
  // render-gate tests load the window without going through init().
  s_active_theme = &s_theme_panel;

  // Slot contents gate health reads and the render snapshot, and the inbox
  // tests rewrite them; restore the shipped layout before every test.
  const ComplicationDataSource defaults[NUM_SLOTS] = {DATA_SOURCE_WEATHER,   DATA_SOURCE_SLEEP,
                                                      DATA_SOURCE_STEPS,     DATA_SOURCE_HEART_RATE,
                                                      DATA_SOURCE_BLUETOOTH, DATA_SOURCE_FULL_DATE};
  for (int i = 0; i < NUM_SLOTS; i++) {
    s_complication_slots[i].source = defaults[i];
  }
}

void tearDown(void) {}

// -----------------------------------------------------------------------------
// Tests
// -----------------------------------------------------------------------------

// apply_theme() takes a tm after the clock-consolidation refactor; keep the
// plumbing in one place.
static void test_apply_theme(void) {
  time_t now = time(NULL);
  apply_theme(localtime(&now));
}

void test_render_gate_should_go_silent_when_nothing_changes(void) {
  main_window_load(NULL);
  memset(&s_shown_ui, 0, sizeof(s_shown_ui));
  memset(s_slot_text, 0, sizeof(s_slot_text));
  mock_mark_dirty_count = 0;
  mock_set_text_count = 0;

  request_ui_redraw();  // cold: zeroed snapshot differs, applies once
  int marks = mock_mark_dirty_count;
  int texts = mock_set_text_count;

  request_ui_redraw();
  request_ui_redraw();
  TEST_ASSERT_EQUAL_INT(marks, mock_mark_dirty_count);
  TEST_ASSERT_EQUAL_INT(texts, mock_set_text_count);
}

void test_render_gate_should_ignore_changes_nobody_displays(void) {
  main_window_load(NULL);
  memset(&s_shown_ui, 0, sizeof(s_shown_ui));
  request_ui_redraw();
  int marks = mock_mark_dirty_count;

  s_battery_level = 5;  // the default layout shows no battery slot
  request_ui_redraw();
  TEST_ASSERT_EQUAL_INT(marks, mock_mark_dirty_count);
  s_battery_level = 100;
}

void test_render_gate_should_pass_displayed_changes_through(void) {
  main_window_load(NULL);
  memset(&s_shown_ui, 0, sizeof(s_shown_ui));
  request_ui_redraw();
  int marks = mock_mark_dirty_count;

  s_step_count = 4321;  // bottom-left slot shows STEPS by default
  request_ui_redraw();
  // STEPS paints on the canvas now: the value change marks the canvas layer.
  TEST_ASSERT_TRUE(mock_mark_dirty_count > marks);
  s_step_count = -1;
}

void test_render_gate_should_notice_bar_slot_changes(void) {
  // Regression: the bar sources used to have no data of their own, so a
  // snapshot keyed on slot->source recorded empty text forever and the bar
  // froze between minute ticks. The registry's .backs field re-reads the
  // plain source.
  main_window_load(NULL);
  s_complication_slots[2].source = DATA_SOURCE_STEPS_BAR;
  s_step_count = 1000;
  memset(&s_shown_ui, 0, sizeof(s_shown_ui));
  request_ui_redraw();
  int marks = mock_mark_dirty_count;

  s_step_count = 6000;  // same "--"/number shape, different fill and reading
  request_ui_redraw();
  TEST_ASSERT_TRUE(mock_mark_dirty_count > marks);

  s_step_count = -1;
  s_complication_slots[2].source = DATA_SOURCE_STEPS;
}

void test_render_gate_should_reapply_colors_on_theme_change(void) {
  main_window_load(NULL);
  s_settings_theme = 2;  // pin Panel so the Shadow swap below is unconditional
  memset(&s_shown_ui, 0, sizeof(s_shown_ui));
  test_apply_theme();
  request_ui_redraw();
  mock_set_text_color_count = 0;
  int texts = mock_set_text_count;
  int marks = mock_mark_dirty_count;

  s_active_theme = &s_theme_shadow;  // e.g. Auto crossing 22:00
  request_ui_redraw();
  TEST_ASSERT_TRUE(mock_mark_dirty_count > marks);    // canvas frames recolor
  TEST_ASSERT_TRUE(mock_set_text_color_count > 0);    // slots recolor…
  TEST_ASSERT_EQUAL_INT(texts, mock_set_text_count);  // …but no string changed
  test_apply_theme();
}

void test_quick_view_did_change_should_gate_and_restore(void) {
  main_window_load(NULL);
  request_ui_redraw();  // baseline full apply
  int marks = mock_mark_dirty_count;

  mock_unobstructed_bounds = GRect(0, 0, 200, 184);  // Quick View over the bottom row
  quick_view_did_change(NULL);
  TEST_ASSERT_TRUE(s_quick_view_active);
  // The render is scheduled purely off the obstruction change — no text moved.
  TEST_ASSERT_EQUAL_INT(marks + 1, mock_mark_dirty_count);

  quick_view_did_change(NULL);  // same unobstructed area: no-op
  TEST_ASSERT_TRUE(s_quick_view_active);
  TEST_ASSERT_EQUAL_INT(marks + 1, mock_mark_dirty_count);

  mock_unobstructed_bounds = GRect(0, 0, 200, 228);  // Quick View drops
  quick_view_did_change(NULL);
  TEST_ASSERT_FALSE(s_quick_view_active);
  TEST_ASSERT_EQUAL_INT(marks + 2, mock_mark_dirty_count);
}

void test_canvas_should_skip_the_bottom_row_while_quick_view_is_up(void) {
  test_apply_theme();
  s_quick_view_active = false;
  mock_fill_rect_reset();
  canvas_update_proc(NULL, NULL);
  int full = mock_fill_rect_count;

  s_quick_view_active = true;
  mock_fill_rect_reset();
  canvas_update_proc(NULL, NULL);
  int occluded = mock_fill_rect_count;

  TEST_ASSERT_TRUE(occluded < full);
  // Three bottom-row windows skipped; each frame is its five border rects.
  TEST_ASSERT_EQUAL_INT(full - 15, occluded);
  // Nothing still drawn lands in the covered row's band.
  for (int i = 0; i < mock_fill_rect_count; i++) {
    TEST_ASSERT_LESS_THAN_INT(184, mock_fill_rects[i].origin.y);
  }
}

void test_quick_view_should_hide_and_restore_bottom_row_text_layers(void) {
  main_window_load(NULL);
  request_ui_redraw();  // baseline: hidden flags recorded for slots 0..NUM_SLOTS-1
  mock_set_hidden_count = 0;

  mock_unobstructed_bounds = GRect(0, 0, 200, 184);
  quick_view_did_change(NULL);

  TEST_ASSERT_EQUAL_INT(NUM_SLOTS, mock_set_hidden_count);
  // The bottom row hides under the overlay. Slots 0-3 and 5 all paint onto
  // the canvas now, so their text layers stay hidden either way; only the
  // Bluetooth slot still has a text layer to cover.
  TEST_ASSERT_TRUE(mock_set_hidden_states[2]);
  TEST_ASSERT_TRUE(mock_set_hidden_states[3]);
  TEST_ASSERT_TRUE(mock_set_hidden_states[4]);
  TEST_ASSERT_TRUE(mock_set_hidden_states[1]);
  TEST_ASSERT_TRUE(mock_set_hidden_states[0]);
  TEST_ASSERT_TRUE(mock_set_hidden_states[5]);

  mock_set_hidden_count = 0;
  mock_unobstructed_bounds = GRect(0, 0, 200, 228);
  quick_view_did_change(NULL);

  TEST_ASSERT_EQUAL_INT(NUM_SLOTS, mock_set_hidden_count);
  // The Bluetooth slot's text layer returns; the canvas-painted slots
  // (0-3, 5) keep theirs hidden as always.
  TEST_ASSERT_FALSE(mock_set_hidden_states[4]);
  TEST_ASSERT_TRUE(mock_set_hidden_states[3]);
  TEST_ASSERT_FALSE(mock_set_hidden_states[4]);
}

// As delivered to the canvas, both checkboxes of the combined window must be
// drawn — a stray atomic-source read inside the drawer is exactly the bug
// where the QT box comes out empty.
void test_bt_qt_wide_should_draw_both_checkboxes(void) {
  test_apply_theme();
  s_complication_slots[0].source = DATA_SOURCE_BT_QT;  // top slot: wide form
  s_connected = true;
  s_quiet_time_active = true;
  mock_text_run_count = 0;
  canvas_update_proc(NULL, NULL);

  bool x_found = false, z_found = false;
  for (int i = 0; i < mock_text_run_count; i++) {
    if (strcmp(mock_text_runs[i], "[x]") == 0) x_found = true;
    if (strcmp(mock_text_runs[i], "[z]") == 0) z_found = true;
  }
  TEST_ASSERT_TRUE(x_found);
  TEST_ASSERT_TRUE(z_found);

  s_complication_slots[0].source = DATA_SOURCE_WEATHER;
}

void test_canvas_procs_should_never_word_wrap(void) {
  test_apply_theme();
  s_complication_slots[3].source = DATA_SOURCE_BATTERY_BAR;  // exercise the shade runs too
  mock_wordwrap_calls = 0;
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(0, mock_wordwrap_calls);
  s_complication_slots[3].source = DATA_SOURCE_HEART_RATE;
}

void test_hum_pcp_window_should_paint_its_halves(void) {
  test_apply_theme();
  s_complication_slots[0].source = DATA_SOURCE_HUM_PCP;
  s_weather_humidity = 61;
  s_weather_pcp = 12;
  mock_text_run_count = 0;
  canvas_update_proc(NULL, NULL);

  // The tight strip: an 8-cell block (3+2+3) centred in the 93px box at
  // x=8 places the fields at 22 and 62; content fills its cell-window.
  // Percent units are symbols, not shortkey letters: plain primary rows.
  bool hum_found = false, pcp_found = false;
  int value_y = s_complication_slots[0].box_rect.origin.y + VALUE_ROW_DY;
  for (int i = 0; i < mock_text_run_count; i++) {
    if (mock_text_run_boxes[i].origin.y == value_y &&
        gcolor_equal(mock_text_run_colors[i], s_active_theme->mark)) {
      TEST_FAIL_MESSAGE("a % half should never wear the mark");
    }
    if (strcmp(mock_text_runs[i], "61%") == 0) {
      hum_found = true;
      TEST_ASSERT_TRUE(gcolor_equal(mock_text_run_colors[i], s_active_theme->text_primary));
      TEST_ASSERT_EQUAL_INT(22, mock_text_run_boxes[i].origin.x);
    }
    if (strcmp(mock_text_runs[i], "12%") == 0) {
      pcp_found = true;
      TEST_ASSERT_TRUE(gcolor_equal(mock_text_run_colors[i], s_active_theme->text_primary));
      TEST_ASSERT_EQUAL_INT(62, mock_text_run_boxes[i].origin.x);
    }
  }
  TEST_ASSERT_TRUE(hum_found);
  TEST_ASSERT_TRUE(pcp_found);

  s_complication_slots[0].source = DATA_SOURCE_WEATHER;
}

void test_battery_bar_should_paint_its_fill_as_one_rect(void) {
  test_apply_theme();
  s_complication_slots[3].source = DATA_SOURCE_BATTERY_BAR;
  s_battery_level = 100;  // full bar
  mock_fill_rect_reset();
  mock_bar_glyph_calls = 0;

  canvas_update_proc(NULL, NULL);

  // Expected fill: every bar cell, one cell high, aligned with the band.
  GRect band = status_band_rect(s_complication_slots[3].box_rect);
  int cells = band.size.w / VGA16_CHAR_W;
  int bar_cells = cells - BAR_VALUE_CELLS - 1;
  GRect row = GRect(band.origin.x + (band.size.w - cells * VGA16_CHAR_W) / 2,
                    s_complication_slots[3].box_rect.origin.y + VALUE_ROW_DY, cells * VGA16_CHAR_W,
                    VALUE_ROW_H);
  GRect expected = GRect(row.origin.x, band.origin.y, bar_cells * VGA16_CHAR_W, VGA16_CELL_H);

  bool found = false;
  for (int i = 0; i < mock_fill_rect_count; i++) {
    if (mock_fill_rects[i].origin.x == expected.origin.x &&
        mock_fill_rects[i].origin.y == expected.origin.y &&
        mock_fill_rects[i].size.w == expected.size.w &&
        mock_fill_rects[i].size.h == expected.size.h) {
      found = true;
    }
  }
  TEST_ASSERT_TRUE(found);
  TEST_ASSERT_EQUAL_INT(0, mock_bar_glyph_calls);  // no block glyphs for the fill

  s_complication_slots[3].source = DATA_SOURCE_HEART_RATE;
}

void test_steps_bar_should_fill_with_the_plain_text_color(void) {
  // The steps bar encodes no status, so its fill is the plain text color —
  // the rule get_source_color gives STEPS.
  s_complication_slots[5].source = DATA_SOURCE_STEPS_BAR;
  s_step_count = 6000;  // 60% of the 10k goal
  mock_fill_rect_reset();

  canvas_update_proc(NULL, NULL);

  bool saw_bar_fill = false;
  for (int i = 0; i < mock_fill_rect_count; i++) {
    TEST_ASSERT_TRUE(mock_fill_rect_colors[i] != s_active_theme->status_green);
    if (mock_fill_rects[i].size.w > 0 && mock_fill_rect_colors[i] == s_active_theme->text_primary) {
      saw_bar_fill = true;
    }
  }
  TEST_ASSERT_TRUE(saw_bar_fill);

  s_complication_slots[5].source = DATA_SOURCE_FULL_DATE;
  s_step_count = -1;
}

void test_battery_bar_should_fill_with_the_status_color(void) {
  // The fill follows the battery ladder: plain text while healthy, then
  // yellow, then red — the chip's band and this fill paint from one reading.
  s_complication_slots[5].source = DATA_SOURCE_BATTERY_BAR;

  int levels[] = {100, 39, 19};
  GColor fills[] = {s_active_theme->text_primary, s_active_theme->status_yellow,
                    s_active_theme->status_red};

  for (int c = 0; c < 3; c++) {
    s_battery_level = levels[c];
    mock_fill_rect_reset();
    canvas_update_proc(NULL, NULL);

    bool saw_fill = false;
    for (int i = 0; i < mock_fill_rect_count; i++) {
      TEST_ASSERT_TRUE(mock_fill_rect_colors[i] != s_active_theme->status_green);
      if (mock_fill_rects[i].size.w > 0 && mock_fill_rect_colors[i] == fills[c]) {
        saw_fill = true;
      }
      for (int j = 0; j < 3; j++) {
        if (j != c) TEST_ASSERT_TRUE(mock_fill_rect_colors[i] != fills[j]);
      }
    }
    TEST_ASSERT_TRUE(saw_fill);
  }

  s_complication_slots[5].source = DATA_SOURCE_FULL_DATE;
  s_battery_level = 100;
}

void test_aqi_chip_should_band_only_on_an_attention_reading(void) {
  // Chip banding follows the strip's rule: a quiet reading draws plain text,
  // a flagged one fills — the thresholds stay in get_source_color alone.
  s_complication_slots[3].source = DATA_SOURCE_AQI;
  GRect band = status_band_rect(s_complication_slots[3].box_rect);

  s_weather_aqi = 30;  // clean air fills nothing
  mock_fill_rect_reset();
  canvas_update_proc(NULL, NULL);
  for (int i = 0; i < mock_fill_rect_count; i++) {
    bool at_band = mock_fill_rects[i].origin.x == band.origin.x &&
                   mock_fill_rects[i].origin.y == band.origin.y &&
                   mock_fill_rects[i].size.w == band.size.w;
    if (at_band) {
      // No status-colored fill: clean air used to wear a green band here.
      TEST_ASSERT_FALSE(gcolor_equal(mock_fill_rect_colors[i], s_active_theme->status_yellow) ||
                        gcolor_equal(mock_fill_rect_colors[i], s_active_theme->status_red) ||
                        gcolor_equal(mock_fill_rect_colors[i], s_active_theme->status_green));
    }
  }

  s_weather_aqi = 60;  // moderate: yellow band
  mock_fill_rect_reset();
  canvas_update_proc(NULL, NULL);
  bool saw_band = false;
  for (int i = 0; i < mock_fill_rect_count; i++) {
    if (mock_fill_rects[i].origin.x == band.origin.x &&
        mock_fill_rects[i].origin.y == band.origin.y && mock_fill_rects[i].size.w == band.size.w &&
        gcolor_equal(mock_fill_rect_colors[i], s_active_theme->status_yellow)) {
      saw_band = true;
    }
  }
  TEST_ASSERT_TRUE(saw_band);

  s_complication_slots[3].source = DATA_SOURCE_HEART_RATE;
}

void test_battery_complications_should_wear_green_while_charging(void) {
  // Charging speaks green on both forms: chip band and bar fill, level
  // notwithstanding — the quiet ladder is for when it is draining.
  s_complication_slots[3].source = DATA_SOURCE_BATTERY;
  GRect band = status_band_rect(s_complication_slots[3].box_rect);
  s_battery_level = 100;
  s_battery_charging = true;

  mock_fill_rect_reset();
  canvas_update_proc(NULL, NULL);
  bool saw_chip_band = false;
  for (int i = 0; i < mock_fill_rect_count; i++) {
    if (mock_fill_rects[i].origin.x == band.origin.x &&
        mock_fill_rects[i].origin.y == band.origin.y && mock_fill_rects[i].size.w == band.size.w &&
        gcolor_equal(mock_fill_rect_colors[i], s_active_theme->status_green)) {
      saw_chip_band = true;
    }
  }
  TEST_ASSERT_TRUE(saw_chip_band);

  s_complication_slots[3].source = DATA_SOURCE_HEART_RATE;
  s_complication_slots[5].source = DATA_SOURCE_BATTERY_BAR;
  mock_fill_rect_reset();
  canvas_update_proc(NULL, NULL);
  bool saw_bar_fill = false;
  for (int i = 0; i < mock_fill_rect_count; i++) {
    if (mock_fill_rects[i].size.w > 0 &&
        gcolor_equal(mock_fill_rect_colors[i], s_active_theme->status_green)) {
      saw_bar_fill = true;
    }
  }
  TEST_ASSERT_TRUE(saw_bar_fill);

  s_battery_charging = false;
  s_complication_slots[5].source = DATA_SOURCE_FULL_DATE;
}

// A PCP fill at the slot's band rect in the given color = the chip banded in
// that status colour.
static bool pcp_slot_banded_with(GRect band, GColor color) {
  for (int i = 0; i < mock_fill_rect_count; i++) {
    if (mock_fill_rects[i].origin.x == band.origin.x &&
        mock_fill_rects[i].origin.y == band.origin.y && mock_fill_rects[i].size.w == band.size.w &&
        gcolor_equal(mock_fill_rect_colors[i], color)) {
      return true;
    }
  }
  return false;
}

// A `mark`-coloured run spelling the tail unit (e.g. "mm") inside the slot's
// value row = the unit accent is being drawn there.
static bool pcp_slot_shows_unit_accent(GRect row) {
  for (int i = 0; i < mock_text_run_count; i++) {
    if (mock_text_run_boxes[i].origin.x >= row.origin.x &&
        mock_text_run_boxes[i].origin.x + mock_text_run_boxes[i].size.w <=
            row.origin.x + row.size.w &&
        mock_text_run_boxes[i].origin.y == row.origin.y &&
        gcolor_equal(mock_text_run_colors[i], s_active_theme->mark) &&
        strcmp(mock_text_runs[i], "mm") == 0) {
      return true;
    }
  }
  return false;
}

void test_pcp_chip_should_band_on_attention_probability(void) {
  // Parity with the strip chip: a quiet probability reads plain, past 50 the
  // reading bands, past 70 it bands red.
  s_complication_slots[3].source = DATA_SOURCE_WEATHER_PCP;
  GRect band = status_band_rect(s_complication_slots[3].box_rect);

  s_weather_pcp = 45;  // <= 50: nothing to plan around
  mock_fill_rect_reset();
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_FALSE(pcp_slot_banded_with(band, s_active_theme->status_yellow));
  TEST_ASSERT_FALSE(pcp_slot_banded_with(band, s_active_theme->status_red));

  s_weather_pcp = 60;  // 51-70: worth a thought — yellow band
  mock_fill_rect_reset();
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_TRUE(pcp_slot_banded_with(band, s_active_theme->status_yellow));

  s_weather_pcp = 75;  // past 70: plan around it — red band
  mock_fill_rect_reset();
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_TRUE(pcp_slot_banded_with(band, s_active_theme->status_red));

  s_weather_pcp = -1;  // no reading at all: quiet
  mock_fill_rect_reset();
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_FALSE(pcp_slot_banded_with(band, s_active_theme->status_yellow));
  TEST_ASSERT_FALSE(pcp_slot_banded_with(band, s_active_theme->status_red));

  s_complication_slots[3].source = DATA_SOURCE_HEART_RATE;
}

// A run inside `row` spelled exactly `text` and painted `color` — the generic
// form of pcp_slot_shows_unit_accent(), for chips that aren't bands.
static bool row_has_run(GRect row, const char* text, GColor color) {
  for (int i = 0; i < mock_text_run_count; i++) {
    if (mock_text_run_boxes[i].origin.x >= row.origin.x &&
        mock_text_run_boxes[i].origin.x + mock_text_run_boxes[i].size.w <=
            row.origin.x + row.size.w &&
        mock_text_run_boxes[i].origin.y == row.origin.y &&
        gcolor_equal(mock_text_run_colors[i], color) && strcmp(mock_text_runs[i], text) == 0) {
      return true;
    }
  }
  return false;
}

void test_weather_strip_should_draw_the_condition_in_mark(void) {
  // The strip's condition chip is the same hotkey word as the top chip's;
  // never banded — it carries no thresholds to encode.
  s_complication_slots[5].source = DATA_SOURCE_WEATHER_FULL;
  s_weather_temp = 72;
  strcpy(s_weather_cond, "SUN");
  s_weather_humidity = 55;
  s_weather_pcp = 10;
  mock_text_runs_reset();
  canvas_update_proc(NULL, NULL);
  GRect strip = s_complication_slots[5].box_rect;
  int strip_y = strip.origin.y + VALUE_ROW_DY;
  bool cond_marked = false;
  for (int i = 0; i < mock_text_run_count; i++) {
    if (mock_text_run_boxes[i].origin.y == strip_y &&
        mock_text_run_boxes[i].origin.x >= strip.origin.x &&
        mock_text_run_boxes[i].origin.x <= strip.origin.x + strip.size.w &&
        strcmp(mock_text_runs[i], "SUN") == 0 &&
        gcolor_equal(mock_text_run_colors[i], s_active_theme->mark)) {
      cond_marked = true;
    }
  }
  TEST_ASSERT_TRUE(cond_marked);
  s_complication_slots[5].source = DATA_SOURCE_FULL_DATE;
}

void test_heart_rate_chip_should_trail_the_heart(void) {
  // Digits, then the heart: the accent rides the tail like the units do.
  s_heart_rate = 75;
  mock_text_runs_reset();
  canvas_update_proc(NULL, NULL);
  GRect slot = s_complication_slots[3].box_rect;
  int heart_x = -1, digits_x = -1;
  for (int i = 0; i < mock_text_run_count; i++) {
    GRect b = mock_text_run_boxes[i];
    if (b.origin.y < slot.origin.y || b.origin.y > slot.origin.y + slot.size.h) continue;
    if (strcmp(mock_text_runs[i], "\xE2\x99\xA5") == 0) heart_x = b.origin.x;
    if (strcmp(mock_text_runs[i], "75") == 0) digits_x = b.origin.x;
  }
  TEST_ASSERT_TRUE(heart_x >= 0 && digits_x >= 0);
  TEST_ASSERT_TRUE(heart_x > digits_x);
  s_heart_rate = 0;
}

void test_weather_chip_should_hotkey_the_condition_and_the_unit(void) {
  // "CLD 72F": the condition word leads, the unit letter trails — both wear
  // the theme mark, the value between stays primary. NC menus hint the
  // shortkey; there is no one-accent-per-chip budget.
  s_weather_temp = 72;
  strcpy(s_weather_cond, "CLD");
  mock_text_runs_reset();
  canvas_update_proc(NULL, NULL);
  GRect row = vga16_value_rect(s_complication_slots[0].box_rect, "CLD 72F");
  TEST_ASSERT_TRUE(row_has_run(row, "CLD", s_active_theme->mark));
  TEST_ASSERT_TRUE(row_has_run(row, " 72", s_active_theme->text_primary));
  TEST_ASSERT_TRUE(row_has_run(row, "F", s_active_theme->mark));

  // No reading: the sentinel stays quiet on the ground.
  s_weather_temp = -999;
  strcpy(s_weather_cond, "--");
  mock_text_runs_reset();
  canvas_update_proc(NULL, NULL);
  GRect dash_row = vga16_value_rect(s_complication_slots[0].box_rect, "-- --");
  TEST_ASSERT_FALSE(row_has_run(dash_row, "--", s_active_theme->mark));
}

void test_sleep_chip_should_hint_only_the_trailing_unit(void) {
  // "7h 30m": the hint is the string's tail letters — the mid-string "h"
  // stays plain; the rail is the right edge.
  s_sleep_seconds = 7 * 3600 + 30 * 60;
  mock_text_runs_reset();
  canvas_update_proc(NULL, NULL);
  GRect row = vga16_value_rect(s_complication_slots[1].box_rect, "7h 30m");
  TEST_ASSERT_TRUE(row_has_run(row, "m", s_active_theme->mark));
  TEST_ASSERT_TRUE(row_has_run(row, "7h 30", s_active_theme->text_primary));
  TEST_ASSERT_FALSE(row_has_run(row, "h", s_active_theme->mark));
}

void test_steps_chip_should_hint_the_k(void) {
  s_complication_slots[1].source = DATA_SOURCE_STEPS;
  s_step_count = 12500;
  mock_text_runs_reset();
  canvas_update_proc(NULL, NULL);
  GRect row = vga16_value_rect(s_complication_slots[1].box_rect, "12.5k");
  TEST_ASSERT_TRUE(row_has_run(row, "k", s_active_theme->mark));
  TEST_ASSERT_TRUE(row_has_run(row, "12.5", s_active_theme->text_primary));
}

void test_battery_chip_should_band_without_hinting_the_percent(void) {
  // "%" is a symbol, not a shortkey letter: no hint — the battery speaks
  // through its band alone.
  s_complication_slots[3].source = DATA_SOURCE_BATTERY;
  s_battery_charging = false;
  s_battery_level = 87;
  mock_text_runs_reset();
  canvas_update_proc(NULL, NULL);
  GRect row = vga16_value_rect(s_complication_slots[3].box_rect, "87%");
  TEST_ASSERT_TRUE(row_has_run(row, "87%", s_active_theme->text_primary));
  TEST_ASSERT_FALSE(row_has_run(row, "%", s_active_theme->mark));

  s_battery_charging = true;  // on the band everything plays in ink
  mock_text_runs_reset();
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_FALSE(row_has_run(row, "%", s_active_theme->mark));
  s_battery_charging = false;
  s_battery_level = 100;
}

void test_humidity_chip_should_stay_plain(void) {
  s_complication_slots[3].source = DATA_SOURCE_HUMIDITY;
  s_weather_humidity = 62;
  mock_text_runs_reset();
  canvas_update_proc(NULL, NULL);
  GRect row = vga16_value_rect(s_complication_slots[3].box_rect, "62%");
  TEST_ASSERT_TRUE(row_has_run(row, "62%", s_active_theme->text_primary));
  TEST_ASSERT_FALSE(row_has_run(row, "%", s_active_theme->mark));
}

void test_active_chip_should_hint_minutes(void) {
  s_complication_slots[3].source = DATA_SOURCE_ACTIVE_MINUTES;
  s_active_minutes = 115;
  mock_text_runs_reset();
  canvas_update_proc(NULL, NULL);
  GRect row = vga16_value_rect(s_complication_slots[3].box_rect, "115m");
  TEST_ASSERT_TRUE(row_has_run(row, "m", s_active_theme->mark));
}

void test_temp_chip_should_color_shift_and_hint_the_unit(void) {
  // The solo temp reading color-shifts with the band (blue when cold, red
  // when hot) and its unit letter carries the shortkey accent on top.
  s_complication_slots[3].source = DATA_SOURCE_WEATHER_TEMP;
  s_settings_units = 1;
  s_weather_temp = -2;  // metric cold
  mock_text_runs_reset();
  canvas_update_proc(NULL, NULL);
  GRect row = vga16_value_rect(s_complication_slots[3].box_rect, "-2C");
  TEST_ASSERT_TRUE(row_has_run(row, "-2", s_active_theme->accent_cold));
  TEST_ASSERT_TRUE(row_has_run(row, "C", s_active_theme->mark));
  s_settings_units = 0;
}

void test_high_low_chip_should_hint_the_trailing_unit(void) {
  s_complication_slots[0].source = DATA_SOURCE_TEMP_HIGH_LOW;
  s_settings_units = 1;
  s_temp_low = 11;
  s_temp_high = 20;
  s_temp_low_tmrw = 7;
  s_temp_high_tmrw = 22;
  s_lo_hour_today = 5;
  s_hi_hour_today = 15;
  s_lo_hour_tmrw = 5;
  s_hi_hour_tmrw = 15;
  s_wall_hour = 4;  // nothing passed: "+11C +20C", LO leads
  mock_text_runs_reset();
  canvas_update_proc(NULL, NULL);
  GRect row = vga16_value_rect(s_complication_slots[0].box_rect, "+11C +20C");
  TEST_ASSERT_TRUE(row_has_run(row, "C", s_active_theme->mark));
  TEST_ASSERT_TRUE(row_has_run(row, "+11C +20", s_active_theme->text_primary));
  s_wall_hour = 12;
  s_settings_units = 0;
}

void test_wind_chip_should_hint_the_unit_until_gale(void) {
  // Wide form "↗ 12 mph": unit hinted, arrow and digits plain; at Bf 8 the
  // band inks everything.
  s_complication_slots[0].source = DATA_SOURCE_WIND;
  s_weather_wind_speed = 12;
  s_weather_wind_direction = 216;
  mock_text_runs_reset();
  canvas_update_proc(NULL, NULL);
  bool mph_marked = false;
  for (int i = 0; i < mock_text_run_count; i++) {
    if (strcmp(mock_text_runs[i], "mph") == 0 &&
        gcolor_equal(mock_text_run_colors[i], s_active_theme->mark)) {
      mph_marked = true;
    }
  }
  TEST_ASSERT_TRUE(mph_marked);

  s_weather_wind_speed = 45;  // gale: no hint survives the band
  mock_text_runs_reset();
  canvas_update_proc(NULL, NULL);
  mph_marked = false;
  for (int i = 0; i < mock_text_run_count; i++) {
    if (strcmp(mock_text_runs[i], "mph") == 0 &&
        gcolor_equal(mock_text_run_colors[i], s_active_theme->mark)) {
      mph_marked = true;
    }
  }
  TEST_ASSERT_FALSE(mph_marked);
  s_weather_wind_speed = -1;
  s_weather_wind_direction = -1;
}

void test_weather_strip_should_hint_quiet_units(void) {
  // The same rail under the strip: TMP's unit letter, HUM's "%".
  s_complication_slots[5].source = DATA_SOURCE_WEATHER_FULL;
  s_weather_temp = 72;
  strcpy(s_weather_cond, "SUN");
  s_weather_humidity = 55;
  s_weather_pcp = 10;
  mock_text_runs_reset();
  canvas_update_proc(NULL, NULL);
  GRect strip = s_complication_slots[5].box_rect;
  int strip_y = strip.origin.y + VALUE_ROW_DY;
  bool f_marked = false, pct_marked = false;
  for (int i = 0; i < mock_text_run_count; i++) {
    if (mock_text_run_boxes[i].origin.y != strip_y ||
        mock_text_run_boxes[i].origin.x < strip.origin.x ||
        mock_text_run_boxes[i].origin.x > strip.origin.x + strip.size.w) {
      continue;
    }
    if (strcmp(mock_text_runs[i], "F") == 0 &&
        gcolor_equal(mock_text_run_colors[i], s_active_theme->mark)) {
      f_marked = true;
    }
    if (strcmp(mock_text_runs[i], "%") == 0 &&
        gcolor_equal(mock_text_run_colors[i], s_active_theme->mark)) {
      pct_marked = true;
    }
  }
  TEST_ASSERT_TRUE(f_marked);
  TEST_ASSERT_FALSE(pct_marked);
  s_complication_slots[5].source = DATA_SOURCE_FULL_DATE;
}

void test_pcp_chip_should_band_by_wmo_intensity_and_keep_accent_when_calm(void) {
  // Amount mode: light rain keeps the "mm" unit accent; at 4 mm the chip
  // bands like the strip's and the accent would drown on the fill, so it goes.
  s_complication_slots[3].source = DATA_SOURCE_WEATHER_PCP;
  s_settings_units = 1;
  strcpy(s_weather_cond, "RAIN");
  GRect band = status_band_rect(s_complication_slots[3].box_rect);

  s_precip_now = 30;  // 3 mm: light
  mock_fill_rect_reset();
  mock_text_runs_reset();
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_FALSE(pcp_slot_banded_with(band, s_active_theme->status_yellow));
  TEST_ASSERT_FALSE(pcp_slot_banded_with(band, s_active_theme->status_red));
  TEST_ASSERT_TRUE(
      pcp_slot_shows_unit_accent(vga16_value_rect(s_complication_slots[3].box_rect, "3mm")));

  s_precip_now = 40;  // 4 mm: heavy — yellow band, no accent
  mock_fill_rect_reset();
  mock_text_runs_reset();
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_TRUE(pcp_slot_banded_with(band, s_active_theme->status_yellow));
  TEST_ASSERT_FALSE(
      pcp_slot_shows_unit_accent(vga16_value_rect(s_complication_slots[3].box_rect, "4mm")));

  s_precip_now = 80;  // 8 mm: violent — red band, no accent
  mock_fill_rect_reset();
  mock_text_runs_reset();
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_TRUE(pcp_slot_banded_with(band, s_active_theme->status_red));
  TEST_ASSERT_FALSE(
      pcp_slot_shows_unit_accent(vga16_value_rect(s_complication_slots[3].box_rect, "8mm")));

  s_precip_now = -1;
  s_settings_units = 0;
  strcpy(s_weather_cond, "--");
  s_complication_slots[3].source = DATA_SOURCE_HEART_RATE;
}

void test_battery_callback_should_coalesce_unchanged_levels(void) {
  main_window_load(NULL);
  memset(&s_shown_ui, 0, sizeof(s_shown_ui));
  battery_callback((BatteryChargeState){.charge_percent = 100});
  int marks = mock_mark_dirty_count;
  battery_callback((BatteryChargeState){.charge_percent = 100});
  TEST_ASSERT_EQUAL_INT(marks, mock_mark_dirty_count);

  // Charging flips the band while the level text stands still, so the flag
  // must reach the render gate too — else the chip redraws up to a minute late.
  s_complication_slots[3].source = DATA_SOURCE_BATTERY;
  battery_callback((BatteryChargeState){.charge_percent = 100, .is_charging = false});
  marks = mock_mark_dirty_count;
  battery_callback((BatteryChargeState){.charge_percent = 100, .is_charging = true});
  TEST_ASSERT_TRUE(mock_mark_dirty_count > marks);
  s_complication_slots[3].source = DATA_SOURCE_HEART_RATE;
}

void test_to_upper_str_should_convert_lowercase_to_uppercase(void) {
  char str1[] = "hello 123";
  to_upper_str(str1);
  TEST_ASSERT_EQUAL_STRING("HELLO 123", str1);

  char str2[] = "Mon";
  to_upper_str(str2);
  TEST_ASSERT_EQUAL_STRING("MON", str2);

  char str3[] = "ALREADY_UPPER";
  to_upper_str(str3);
  TEST_ASSERT_EQUAL_STRING("ALREADY_UPPER", str3);
}

void test_tuple_get_int_should_parse_strings_and_ints(void) {
  // We mock a tuple since we know its memory layout
  uint8_t buffer1[sizeof(Tuple) + 8];
  Tuple* t1 = (Tuple*)buffer1;
  t1->type = TUPLE_CSTRING;
  strcpy(t1->value->cstring, "42");
  TEST_ASSERT_EQUAL_INT(42, tuple_get_int(t1));

  uint8_t buffer2[sizeof(Tuple) + 4];
  Tuple* t2 = (Tuple*)buffer2;
  t2->type = TUPLE_INT;
  t2->length = 4;
  t2->value->int32 = 1234;
  TEST_ASSERT_EQUAL_INT(1234, tuple_get_int(t2));

  TEST_ASSERT_EQUAL_INT(0, tuple_get_int(NULL));
}

void test_get_source_label_should_return_correct_labels(void) {
  TEST_ASSERT_EQUAL_STRING("BATT", get_source_label(DATA_SOURCE_BATTERY));
  TEST_ASSERT_EQUAL_STRING("STEP", get_source_label(DATA_SOURCE_STEPS));
  TEST_ASSERT_EQUAL_STRING("WEATHER", get_source_label(DATA_SOURCE_WEATHER));
  TEST_ASSERT_EQUAL_STRING("AQI", get_source_label(DATA_SOURCE_AQI));
  TEST_ASSERT_EQUAL_STRING("UV", get_source_label(DATA_SOURCE_UV));
  TEST_ASSERT_EQUAL_STRING("HUM", get_source_label(DATA_SOURCE_HUMIDITY));
  TEST_ASSERT_EQUAL_STRING("PCP", get_source_label(DATA_SOURCE_WEATHER_PCP));
  TEST_ASSERT_EQUAL_STRING("BEAT", get_source_label(DATA_SOURCE_BEATS));
  // Both date sources title the same window; one shows the day, one the date.
  TEST_ASSERT_EQUAL_STRING("DATE", get_source_label(DATA_SOURCE_DATE));
  TEST_ASSERT_EQUAL_STRING("DATE", get_source_label(DATA_SOURCE_SHORT_DATE));
  TEST_ASSERT_EQUAL_STRING("BT", get_source_label(DATA_SOURCE_BLUETOOTH));
  // One window covering both phone states; the standalone keeps the short tag.
  TEST_ASSERT_EQUAL_STRING("BT/QT", get_source_label(DATA_SOURCE_BT_QT));
  TEST_ASSERT_EQUAL_STRING("QT", get_source_label(DATA_SOURCE_QUIET_TIME));
  TEST_ASSERT_EQUAL_STRING("WIND", get_source_label(DATA_SOURCE_WIND));
  TEST_ASSERT_EQUAL_STRING("", get_source_label(DATA_SOURCE_EMPTY));
}

void test_registry_rows_should_be_unique_and_resolve(void) {
  // One row per source, no shadowing duplicates, and every backing source
  // resolves to a row with a real formatter. EMPTY is the one source allowed
  // to format nothing at all.
  const int count = (int)(sizeof(s_complication_specs) / sizeof(s_complication_specs[0]));
  TEST_ASSERT_EQUAL_INT(27, count);  // one row per live enum value
  for (int i = 0; i < count; i++) {
    const ComplicationSpec* row = &s_complication_specs[i];
    TEST_ASSERT_NOT_NULL(row->label);
    TEST_ASSERT_EQUAL_INT(row->source, complication_spec(row->source)->source);
    for (int j = i + 1; j < count; j++) {
      TEST_ASSERT_NOT_EQUAL(row->source, s_complication_specs[j].source);
    }
    const ComplicationSpec* backing = complication_spec(row->backs);
    TEST_ASSERT_NOT_NULL(backing);
    ComplicationFormatFn format = row->format ? row->format : backing->format;
    if (row->source != DATA_SOURCE_EMPTY) TEST_ASSERT_NOT_NULL(format);
  }
}

void test_registry_should_pin_the_weather_backed_set(void) {
  // The fetch gate reads row flags now instead of a hand-maintained list; the
  // exact set drives the tick gate, the launch fetch, and the settings
  // refetch, so pin it.
  const ComplicationDataSource expected[] = {DATA_SOURCE_WEATHER,
                                             DATA_SOURCE_WEATHER_TEMP,
                                             DATA_SOURCE_WEATHER_COND,
                                             DATA_SOURCE_AQI,
                                             DATA_SOURCE_UV,
                                             DATA_SOURCE_AQI_UV,
                                             DATA_SOURCE_HUMIDITY,
                                             DATA_SOURCE_WIND,
                                             DATA_SOURCE_WEATHER_FULL,
                                             DATA_SOURCE_WEATHER_PCP,
                                             DATA_SOURCE_TEMP_HIGH_LOW,
                                             DATA_SOURCE_HUM_PCP};
  const int count = (int)(sizeof(s_complication_specs) / sizeof(s_complication_specs[0]));
  int found = 0;
  for (int i = 0; i < count; i++) {
    const ComplicationSpec* row = &s_complication_specs[i];
    bool want = false;
    for (unsigned j = 0; j < sizeof(expected) / sizeof(expected[0]); j++) {
      if (expected[j] == row->source) want = true;
    }
    if (row->needs_weather) {
      found++;
      TEST_ASSERT_TRUE_MESSAGE(want, "unexpected weather-backed source");
    } else {
      TEST_ASSERT_FALSE_MESSAGE(want, "weather-backed source lost its flag");
    }
  }
  TEST_ASSERT_EQUAL_INT((int)(sizeof(expected) / sizeof(expected[0])), found);
}

void test_get_source_data_should_format_battery(void) {
  char buf[16];
  int percent = 0;

  s_battery_level = 85;
  get_source_data(DATA_SOURCE_BATTERY, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("85%", buf);
  TEST_ASSERT_EQUAL_INT(85, percent);

  // Edge values — format stays unpadded
  s_battery_level = 0;
  get_source_data(DATA_SOURCE_BATTERY, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("0%", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);

  s_battery_level = 100;
  get_source_data(DATA_SOURCE_BATTERY, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("100%", buf);
  TEST_ASSERT_EQUAL_INT(100, percent);
}

void test_get_source_data_should_format_steps(void) {
  char buf[16];
  int percent = 0;

  s_step_goal = 10000;

  // No data
  s_step_count = -1;
  get_source_data(DATA_SOURCE_STEPS, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("--", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);

  // Normal steps
  s_step_count = 5000;
  get_source_data(DATA_SOURCE_STEPS, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("5000", buf);
  TEST_ASSERT_EQUAL_INT(50, percent);

  // > 10k steps format. The percent is NOT clamped to 100 — beating the goal is
  // worth seeing, and the progress bar clamps its own fill separately.
  s_step_count = 12500;
  get_source_data(DATA_SOURCE_STEPS, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("12.5k", buf);
  TEST_ASSERT_EQUAL_INT(125, percent);
}

void test_get_source_data_should_format_weather(void) {
  char buf[32];

  // No data
  s_weather_temp = -999;
  strcpy(s_weather_cond, "--");
  get_source_data(DATA_SOURCE_WEATHER, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("-- --", buf);

  // Imperial: no sign on positives (signed F is noise; negatives are rare)
  s_settings_units = 0;
  s_weather_temp = 72;
  strcpy(s_weather_cond, "SUN");
  get_source_data(DATA_SOURCE_WEATHER, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("SUN 72F", buf);

  // Metric: always signed
  s_settings_units = 1;
  s_weather_temp = 22;
  strcpy(s_weather_cond, "CLD");
  get_source_data(DATA_SOURCE_WEATHER, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("CLD +22C", buf);

  // Widest realistic forms still fit the 11-cell top-slot budget
  s_weather_temp = -22;
  strcpy(s_weather_cond, "TSTM");
  get_source_data(DATA_SOURCE_WEATHER, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("TSTM -22C", buf);
  s_settings_units = 0;
  s_weather_temp = 103;
  strcpy(s_weather_cond, "RAIN");
  get_source_data(DATA_SOURCE_WEATHER, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("RAIN 103F", buf);
}

void test_get_source_data_should_format_sleep(void) {
  char buf[16];
  int percent = 0;

  // No data
  s_sleep_seconds = -1;
  get_source_data(DATA_SOURCE_SLEEP, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("--", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);

  // Normal sleep (7h 30m)
  s_sleep_seconds = (7 * 3600) + (30 * 60);
  get_source_data(DATA_SOURCE_SLEEP, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("7h 30m", buf);
  TEST_ASSERT_EQUAL_INT((s_sleep_seconds * 100) / 28800, percent);

  // Over goal
  s_sleep_seconds = 10 * 3600;  // 10 hours
  get_source_data(DATA_SOURCE_SLEEP, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_INT(100, percent);  // capped at 100%
}

void test_get_source_data_should_format_weather_temp_and_cond(void) {
  char buf[16];

  // Temp no data
  s_weather_temp = -999;
  get_source_data(DATA_SOURCE_WEATHER_TEMP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("--", buf);

  // Temp Imperial
  s_settings_units = 0;
  s_weather_temp = 68;
  get_source_data(DATA_SOURCE_WEATHER_TEMP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("68F", buf);

  // Temp negative imperial: minus prints, no plus
  s_weather_temp = -9;
  get_source_data(DATA_SOURCE_WEATHER_TEMP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("-9F", buf);

  // Temp Metric
  s_settings_units = 1;
  s_weather_temp = 20;
  get_source_data(DATA_SOURCE_WEATHER_TEMP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("+20C", buf);

  // …including freezing itself
  s_weather_temp = 0;
  get_source_data(DATA_SOURCE_WEATHER_TEMP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("+0C", buf);

  // Cond
  strcpy(s_weather_cond, "RAIN");
  get_source_data(DATA_SOURCE_WEATHER_COND, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("RAIN", buf);
}

void test_get_source_data_should_format_heart_rate(void) {
  char buf[16];

  s_heart_rate = 0;
  get_source_data(DATA_SOURCE_HEART_RATE, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("--", buf);

  s_heart_rate = 120;
  get_source_data(DATA_SOURCE_HEART_RATE, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("120", buf);
}

void test_progress_bar_sources_should_reuse_their_plain_counterparts(void) {
  char buf[16];
  int percent = 0;

  // The bars render from the plain sources' value and percent, so the percent
  // out-parameter has to stay correct — it is what sizes the fill.
  s_step_goal = 10000;
  s_step_count = 8200;
  get_source_data(DATA_SOURCE_STEPS, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_INT(82, percent);

  // Bars re-read their plain counterpart through the registry's .backs chain.
  get_source_data(DATA_SOURCE_STEPS_BAR, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("8200", buf);
  TEST_ASSERT_EQUAL_INT(82, percent);

  s_step_count = -1;
  get_source_data(DATA_SOURCE_STEPS, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_INT(0, percent);

  s_battery_level = 45;
  get_source_data(DATA_SOURCE_BATTERY, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_INT(45, percent);

  get_source_data(DATA_SOURCE_BATTERY_BAR, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("45%", buf);
  TEST_ASSERT_EQUAL_INT(45, percent);

  // Over-achievement reaches the bar intact, so the reading beside it can show
  // more than 100% while the fill stays full. The display caps at BAR_VALUE_MAX
  // so it still fits the 4-cell value field.
  s_step_count = 25000;
  get_source_data(DATA_SOURCE_STEPS, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_INT(250, percent);

  s_step_count = 100 * s_step_goal;  // 10000% of goal
  get_source_data(DATA_SOURCE_STEPS, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_INT(10000, percent);
  TEST_ASSERT_TRUE(percent > BAR_VALUE_MAX);  // the bar is what clamps it

  // Whatever the reading, the rendered value must fit BAR_VALUE_CELLS.
  char rendered[8];
  int shown[] = {0, 82, 100, 250, BAR_VALUE_MAX};
  for (unsigned i = 0; i < sizeof(shown) / sizeof(shown[0]); i++) {
    snprintf(rendered, sizeof(rendered), "%*d%%", BAR_VALUE_CELLS - 1, shown[i]);
    TEST_ASSERT_EQUAL_INT(BAR_VALUE_CELLS, (int)strlen(rendered));
  }

  // Both bars title their window like the plain reading they mirror.
  TEST_ASSERT_EQUAL_STRING("STEP", get_source_label(DATA_SOURCE_STEPS_BAR));
  TEST_ASSERT_EQUAL_STRING("BATT", get_source_label(DATA_SOURCE_BATTERY_BAR));
  TEST_ASSERT_EQUAL_STRING("DATE", get_source_label(DATA_SOURCE_FULL_DATE));
}

void test_battery_band_and_color_should_agree_at_every_level(void) {
  // The chip draws its band and the bar paints its fill from the same
  // BATTERY_LOW_PCT / BATTERY_CRIT_PCT pair — one reading can never wear two
  // colors.
  s_active_theme = &s_theme_panel;

  for (int level = 0; level <= 100; level++) {
    s_battery_level = level;
    GColor color = get_source_color(DATA_SOURCE_BATTERY);
    bool banded = level <= BATTERY_LOW_PCT;

    // A band appears exactly when a healthy charge stops being quiet — that
    // equivalence is the whole invariant.
    TEST_ASSERT_EQUAL_INT(banded, color != s_theme_panel.text_primary);

    if (level <= BATTERY_CRIT_PCT) {
      TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, color);
    } else if (level <= BATTERY_LOW_PCT) {
      TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, color);
    }
  }

  // The boundaries themselves: 40 still quiet, 39 yellow, 20 yellow, 19 red.
  s_battery_level = 40;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_BATTERY));
  s_battery_level = 39;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_BATTERY));
  s_battery_level = 20;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_BATTERY));
  s_battery_level = 19;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_BATTERY));

  // On the charger the ladder steps aside: chip band and bar fill read green
  // at every level — 5% on the wire is green, full stop.
  s_battery_charging = true;
  for (int level = 0; level <= 100; level++) {
    s_battery_level = level;
    TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_green, get_source_color(DATA_SOURCE_BATTERY));
    TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_green, get_source_color(DATA_SOURCE_BATTERY_BAR));
  }
}

void test_centre_slot_should_be_the_sixth_and_default_to_the_date(void) {
  // SLOT_1..5 keep their persisted indices, so the centre row must be last.
  TEST_ASSERT_EQUAL_INT(6, NUM_SLOTS);
  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_FULL_DATE, s_complication_slots[5].source);
  TEST_ASSERT_EQUAL_INT(LAYOUT_X, s_complication_slots[5].box_rect.origin.x);
  TEST_ASSERT_EQUAL_INT(LAYOUT_W, s_complication_slots[5].box_rect.size.w);
}

void test_get_source_data_should_format_date_and_day(void) {
  char buf[16];

  s_date_day = 15;
  get_source_data(DATA_SOURCE_DATE, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("15", buf);
}

void test_get_source_data_should_format_bluetooth(void) {
  char buf[16];
  int percent = 0;

  s_connected = true;
  get_source_data(DATA_SOURCE_BLUETOOTH, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("[x]", buf);
  TEST_ASSERT_EQUAL_INT(100, percent);

  s_connected = false;
  get_source_data(DATA_SOURCE_BLUETOOTH, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("[ ]", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);

  s_active_theme = &s_theme_panel;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_BLUETOOTH));
  s_connected = true;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_BLUETOOTH));
}

void test_get_source_data_should_format_bt_qt(void) {
  char buf[16];
  int percent = 0;

  // The combined phone-status window inks one checkbox per state: `x` while
  // the phone is connected, `z` while Quiet Time is active. The glyphs carry
  // the state on their own — no color needed. Quiet Time never feeds the
  // percent: the band belongs to the connection alone.
  s_connected = true;
  s_quiet_time_active = true;
  get_source_data(DATA_SOURCE_BT_QT, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("[x][z]", buf);
  TEST_ASSERT_EQUAL_INT(100, percent);

  s_quiet_time_active = false;
  get_source_data(DATA_SOURCE_BT_QT, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("[x][ ]", buf);
  TEST_ASSERT_EQUAL_INT(100, percent);

  s_connected = false;
  get_source_data(DATA_SOURCE_BT_QT, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("[ ][ ]", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);

  s_quiet_time_active = true;
  get_source_data(DATA_SOURCE_BT_QT, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("[ ][z]", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);

  s_active_theme = &s_theme_panel;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_BT_QT));
  s_quiet_time_active = false;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_BT_QT));
}

void test_wind_direction_arrow_should_point_where_the_wind_blows(void) {
  // Meteo reports the bearing the wind blows FROM; the face points the way
  // it goes — a northerly reads "down".
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x93", wind_direction_arrow(0));    // N wind blows S
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x99", wind_direction_arrow(45));   // NE blows SW
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x90", wind_direction_arrow(90));   // E blows W
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x96", wind_direction_arrow(135));  // SE blows NW
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x91", wind_direction_arrow(180));  // S blows N
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x97", wind_direction_arrow(225));  // SW blows NE
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x92", wind_direction_arrow(270));  // W blows E
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x98", wind_direction_arrow(315));  // NW blows SE
}

void test_wind_direction_arrow_should_take_the_clockwise_sector_on_boundaries(void) {
  // Sectors are 45 degrees wide, centered on their arrow's compass point;
  // the half-sector boundaries (x.5 degrees) fall between two ints, and the
  // first int on the clockwise side must flip the sector.
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x91", wind_direction_arrow(202));  // blows N
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x97", wind_direction_arrow(203));  // blows NE
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x97", wind_direction_arrow(247));  // blows NE
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x92", wind_direction_arrow(248));  // blows E
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x92", wind_direction_arrow(292));  // blows E
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x98", wind_direction_arrow(293));  // blows SE
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x98", wind_direction_arrow(337));  // blows SE
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x93", wind_direction_arrow(338));  // blows S
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x93", wind_direction_arrow(22));   // blows S
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x99", wind_direction_arrow(23));   // blows SW
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x99", wind_direction_arrow(67));   // blows SW
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x90", wind_direction_arrow(68));   // blows W
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x90", wind_direction_arrow(112));  // blows W
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x96", wind_direction_arrow(113));  // blows NW
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x96", wind_direction_arrow(157));  // blows NW
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x91", wind_direction_arrow(158));  // blows N
}

void test_wind_direction_arrow_should_normalize_or_reject_out_of_range_bearings(void) {
  // The N sector wraps the zero point in both bearing and result.
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x93", wind_direction_arrow(359));  // blows S
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x93", wind_direction_arrow(21));   // blows S
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x91", wind_direction_arrow(179));  // blows N
  // Wire values over 360 are junk, not a new kind of circle — clamp modulo.
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x91", wind_direction_arrow(540));  // 360+180
  // No data is the sentinel, and any negative is the sentinel's family.
  TEST_ASSERT_EQUAL_STRING("--", wind_direction_arrow(-1));
  TEST_ASSERT_EQUAL_STRING("--", wind_direction_arrow(-270));
}

void test_get_source_data_should_format_wind(void) {
  char buf[16];
  int percent = -1;

  s_weather_wind_direction = -1;
  get_source_data(DATA_SOURCE_WIND, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("--", buf);
  // A one-glyph arrow is a caption, not a progress bar.
  TEST_ASSERT_EQUAL_INT(0, percent);

  s_weather_wind_direction = 225;  // a southwesterly
  get_source_data(DATA_SOURCE_WIND, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x97", buf);

  // Speed rides with the arrow; the unit is m/s metric, mph imperial — spelled
  // out, since a lone letter read as knots.
  int saved_units = s_settings_units;
  s_settings_units = 1;  // metric
  s_weather_wind_speed = 12;
  get_source_data(DATA_SOURCE_WIND, buf, sizeof(buf), &percent);
  // JS hex escapes swallow trailing digits; split the literal.
  TEST_ASSERT_EQUAL_STRING(
      "\xE2\x86\x97"
      " 12 m/s",
      buf);

  s_settings_units = 0;  // imperial
  s_weather_wind_speed = 45;
  get_source_data(DATA_SOURCE_WIND, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING(
      "\xE2\x86\x97"
      " 45 mph",
      buf);

  // Speed alone, no bearing yet: still worth showing.
  s_weather_wind_direction = -1;
  s_settings_units = 1;
  s_weather_wind_speed = 12;
  get_source_data(DATA_SOURCE_WIND, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("12 m/s", buf);

  // Absurd readings clamp at the three digits the window can hold.
  s_weather_wind_direction = 225;
  s_settings_units = 0;
  s_weather_wind_speed = 1042;
  get_source_data(DATA_SOURCE_WIND, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING(
      "\xE2\x86\x97"
      " 999 mph",
      buf);
  s_settings_units = saved_units;

  s_weather_wind_speed = 5;  // calm, or the band ladder paints it
  s_active_theme = &s_theme_panel;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_WIND));
}

void test_get_source_data_should_format_hum_pcp(void) {
  char buf[16];
  int percent = -1;

  // Either half missing shows dashes in place; never a half-number.
  s_weather_humidity = -1;
  s_weather_pcp = -1;
  get_source_data(DATA_SOURCE_HUM_PCP, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("-- --", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);

  s_weather_humidity = 61;
  get_source_data(DATA_SOURCE_HUM_PCP, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("61% --", buf);

  s_weather_pcp = 12;
  get_source_data(DATA_SOURCE_HUM_PCP, buf, sizeof(buf), &percent);
  // Air joins the halves; the frame stubs (HUM/PCP) carry the naming.
  TEST_ASSERT_EQUAL_STRING("61% 12%", buf);

  // Metric rain swaps the PCP half to the amount spelling.
  s_settings_units = 1;
  s_precip_now = 34;
  strcpy(s_weather_cond, "RAIN");
  get_source_data(DATA_SOURCE_HUM_PCP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("61% 3mm", buf);

  s_active_theme = &s_theme_panel;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_HUM_PCP));
}

void test_get_source_data_should_format_quiet_time(void) {
  char buf[16];
  int percent = 0;

  // The standalone window shows one checkbox from the same state the
  // combined window inks in its second box.
  s_quiet_time_active = true;
  get_source_data(DATA_SOURCE_QUIET_TIME, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("[z]", buf);
  TEST_ASSERT_EQUAL_INT(100, percent);

  s_quiet_time_active = false;
  get_source_data(DATA_SOURCE_QUIET_TIME, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("[ ]", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);

  s_active_theme = &s_theme_panel;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_QUIET_TIME));
  s_quiet_time_active = true;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_QUIET_TIME));
}

void test_bt_qt_window_should_split_captions_only_at_top_width(void) {
  // The combined window trades its single "BT/QT" title for one frame-side
  // caption stub per checkbox (with an air cell joining the boxes) only once
  // the window is top-slot wide. The pixels are screenshot-gated; the width
  // switch is not.
  TEST_ASSERT_FALSE(bt_qt_split_captions(62));  // Bottom Center
  TEST_ASSERT_FALSE(bt_qt_split_captions(63));  // Bottom Left/Right
  TEST_ASSERT_TRUE(bt_qt_split_captions(93));   // Top row
  TEST_ASSERT_TRUE(bt_qt_split_captions(184));  // Centre row
}

void test_get_source_data_should_format_active_minutes(void) {
  char buf[16];
  int percent = 0;

  s_active_minutes_goal = 30;

  s_active_minutes = 15;
  get_source_data(DATA_SOURCE_ACTIVE_MINUTES, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("15m", buf);
  TEST_ASSERT_EQUAL_INT(50, percent);

  s_active_minutes = 45;
  get_source_data(DATA_SOURCE_ACTIVE_MINUTES, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("45m", buf);
  TEST_ASSERT_EQUAL_INT(100, percent);
}

// Every theme, so a new one cannot be added without inheriting the guarantees
// asserted below.
static const WatchTheme* all_themes[] = {&s_theme_panel, &s_theme_shadow, &s_theme_dialog,
                                         &s_theme_navigator};
#define NUM_THEMES (sizeof(all_themes) / sizeof(all_themes[0]))

void test_determine_theme_should_handle_all_configurations(void) {
  // Pinned choices ignore the clock entirely.
  for (int hour = 0; hour < 24; hour++) {
    TEST_ASSERT_EQUAL_PTR(&s_theme_dialog, determine_theme(1, hour));
    TEST_ASSERT_EQUAL_PTR(&s_theme_panel, determine_theme(2, hour));
    TEST_ASSERT_EQUAL_PTR(&s_theme_shadow, determine_theme(3, hour));
    TEST_ASSERT_EQUAL_PTR(&s_theme_navigator, determine_theme(4, hour));
  }

  // Auto: three 8-hour shifts, brightest first. Boundaries are the whole point.
  TEST_ASSERT_EQUAL_PTR(&s_theme_shadow, determine_theme(0, 5));  // 05:00 still night
  TEST_ASSERT_EQUAL_PTR(&s_theme_dialog, determine_theme(0, 6));  // 06:00 dialog starts
  TEST_ASSERT_EQUAL_PTR(&s_theme_dialog, determine_theme(0, 13));
  TEST_ASSERT_EQUAL_PTR(&s_theme_panel, determine_theme(0, 14));  // 14:00 panel starts
  TEST_ASSERT_EQUAL_PTR(&s_theme_panel, determine_theme(0, 21));
  TEST_ASSERT_EQUAL_PTR(&s_theme_shadow, determine_theme(0, 22));  // 22:00 shadow starts
  TEST_ASSERT_EQUAL_PTR(&s_theme_shadow, determine_theme(0, 23));
  TEST_ASSERT_EQUAL_PTR(&s_theme_shadow, determine_theme(0, 0));

  // Every hour must resolve to exactly one theme, and each shift must be 8h —
  // otherwise a gap or overlap would leave some hour unthemed or double-mapped.
  int dialog = 0, panel = 0, shadow = 0;
  for (int hour = 0; hour < 24; hour++) {
    const WatchTheme* t = determine_theme(0, hour);
    TEST_ASSERT_NOT_NULL(t);
    if (t == &s_theme_dialog)
      dialog++;
    else if (t == &s_theme_panel)
      panel++;
    else if (t == &s_theme_shadow)
      shadow++;
  }
  TEST_ASSERT_EQUAL_INT(8, dialog);
  TEST_ASSERT_EQUAL_INT(8, panel);
  TEST_ASSERT_EQUAL_INT(8, shadow);

  // Unknown values fall back to Auto rather than a null theme.
  TEST_ASSERT_EQUAL_PTR(determine_theme(0, 12), determine_theme(99, 12));
  TEST_ASSERT_EQUAL_PTR(determine_theme(0, 23), determine_theme(-1, 23));
}

void test_themes_should_keep_text_readable_on_their_ground(void) {
  for (unsigned i = 0; i < NUM_THEMES; i++) {
    const WatchTheme* t = all_themes[i];

    // AGENTS.md requires high contrast; the ground and its primary text must
    // never collapse into each other.
    TEST_ASSERT_NOT_EQUAL(t->center_bg, t->text_primary);

    // The dedicated `frame` field only earns its place if it clears the ground.
    TEST_ASSERT_NOT_EQUAL(t->center_bg, t->frame);

    // Cold readings and accent marks are drawn as text on the ground.
    TEST_ASSERT_NOT_EQUAL(t->center_bg, t->accent_cold);
    TEST_ASSERT_NOT_EQUAL(t->center_bg, t->mark);

    // Titles render in the top border's gap, on the ground itself — so the
    // label needs contrast with the ground, not with the frame.
    TEST_ASSERT_NOT_EQUAL(t->center_bg, t->text_secondary);

    // Status values are drawn as text on the ground too — this is what forces
    // the light theme to use the low-intensity variants.
    TEST_ASSERT_NOT_EQUAL(t->center_bg, t->status_green);
    TEST_ASSERT_NOT_EQUAL(t->center_bg, t->status_yellow);
    TEST_ASSERT_NOT_EQUAL(t->center_bg, t->status_red);
  }
}

void test_status_ink_should_clear_every_fill_it_is_drawn_on(void) {
  // Status fills write status_ink over themselves; if the ink matches its own
  // fill the reading vanishes — which is exactly what happened when the chip
  // used text_primary on a light ground. Charging keeps green among the fills.
  for (unsigned i = 0; i < NUM_THEMES; i++) {
    TEST_ASSERT_NOT_EQUAL(all_themes[i]->status_ink, all_themes[i]->status_red);
    TEST_ASSERT_NOT_EQUAL(all_themes[i]->status_ink, all_themes[i]->status_yellow);
    TEST_ASSERT_NOT_EQUAL(all_themes[i]->status_ink, all_themes[i]->status_green);
  }
}

static bool is_dos_palette_color(GColor c) {
  // The canonical CGA/EGA 16, minus the three the SDK mock has no name for
  // (#AA00AA, #5555FF, #FF55FF) — no theme uses them. The mock's GColor values
  // are opaque integers, so membership is asserted by symbol, not by hex.
  const GColor dos16[] = {
      GColorBlack,         GColorDukeBlue,     GColorIslamicGreen,      GColorTiffanyBlue,
      GColorWindsorTan,    GColorLightGray,    GColorDarkCandyAppleRed, GColorDarkGray,
      GColorScreaminGreen, GColorElectricBlue, GColorSunsetOrange,      GColorIcterine,
      GColorWhite};
  for (unsigned i = 0; i < sizeof(dos16) / sizeof(dos16[0]); i++) {
    if (c == dos16[i]) return true;
  }
  return false;
}

void test_every_theme_should_only_use_dos_palette_colors(void) {
  // The whole premise is that Pebble's channel steps match DOS's, so every
  // color must be one of the 16. Off-palette values (#FFAA00, #FFAA55) have
  // crept in before — this is the guard.
  for (unsigned i = 0; i < NUM_THEMES; i++) {
    const WatchTheme* t = all_themes[i];
    TEST_ASSERT_TRUE(is_dos_palette_color(t->center_bg));
    TEST_ASSERT_TRUE(is_dos_palette_color(t->accent_cold));
    TEST_ASSERT_TRUE(is_dos_palette_color(t->frame));
    TEST_ASSERT_TRUE(is_dos_palette_color(t->text_primary));
    TEST_ASSERT_TRUE(is_dos_palette_color(t->text_secondary));
    TEST_ASSERT_TRUE(is_dos_palette_color(t->mark));
    TEST_ASSERT_TRUE(is_dos_palette_color(t->status_ink));
    TEST_ASSERT_TRUE(is_dos_palette_color(t->status_green));
    TEST_ASSERT_TRUE(is_dos_palette_color(t->status_yellow));
    TEST_ASSERT_TRUE(is_dos_palette_color(t->status_red));
  }
}

void test_get_source_data_should_format_aqi_and_uv(void) {
  char buf[16];

  // AQI formatting
  s_weather_aqi = -1;
  get_source_data(DATA_SOURCE_AQI, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("--", buf);

  s_weather_aqi = 42;
  get_source_data(DATA_SOURCE_AQI, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("42", buf);

  // UV formatting
  s_weather_uv = -1;
  get_source_data(DATA_SOURCE_UV, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("--", buf);

  s_weather_uv = 5;
  get_source_data(DATA_SOURCE_UV, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("5", buf);

  // Combined AQI / UV formatting
  s_weather_aqi = -1;
  s_weather_uv = -1;
  get_source_data(DATA_SOURCE_AQI_UV, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("-- --", buf);

  s_weather_aqi = 42;
  s_weather_uv = 5;
  get_source_data(DATA_SOURCE_AQI_UV, buf, sizeof(buf), NULL);
  // Air joins the halves; the frame stubs (AQI/UV) carry the naming.
  TEST_ASSERT_EQUAL_STRING("42 5", buf);
}

void test_get_source_data_should_format_humidity(void) {
  char buf[16];
  int percent = -1;

  // No data — the sentinel must not leak a negative progress either
  s_weather_humidity = -1;
  get_source_data(DATA_SOURCE_HUMIDITY, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("--", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);

  s_weather_humidity = 65;
  get_source_data(DATA_SOURCE_HUMIDITY, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("65%", buf);
  TEST_ASSERT_EQUAL_INT(65, percent);

  // Hundred-percent edge: four chars, still fits the narrow bottom slots
  s_weather_humidity = 100;
  get_source_data(DATA_SOURCE_HUMIDITY, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("100%", buf);
  TEST_ASSERT_EQUAL_INT(100, percent);

  // A dry reading is still real data, not a sentinel
  s_weather_humidity = 0;
  get_source_data(DATA_SOURCE_HUMIDITY, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("0%", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);
}

void test_get_source_data_should_format_weather_full(void) {
  char buf[32];
  int percent = -1;

  // No data at all — every field's sentinel shows through
  s_weather_temp = -999;
  strcpy(s_weather_cond, "--");
  s_weather_humidity = -1;
  s_weather_pcp = -1;
  get_source_data(DATA_SOURCE_WEATHER_FULL, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("-- -- -- --", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);

  // A mid-fetch blip leaves one field at sentinel, the rest live
  s_settings_units = 0;
  s_weather_temp = 72;
  strcpy(s_weather_cond, "SUN");
  s_weather_humidity = -1;
  s_weather_pcp = 60;
  get_source_data(DATA_SOURCE_WEATHER_FULL, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("SUN 72F -- 60%", buf);

  // Typical imperial day
  s_weather_humidity = 45;
  get_source_data(DATA_SOURCE_WEATHER_FULL, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("SUN 72F 45% 60%", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);  // composite source, never a progress bar

  // Worst-case mix (metric): comfortably inside the strip budget
  s_settings_units = 1;
  s_weather_temp = -22;
  strcpy(s_weather_cond, "TSTM");
  s_weather_humidity = 100;
  s_weather_pcp = 100;
  get_source_data(DATA_SOURCE_WEATHER_FULL, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("TSTM -22C 100% 100%", buf);
  TEST_ASSERT_TRUE(strlen(buf) <= FULL_WEATHER_STRIP_CELLS);
}

void test_strip_temp_formatter_should_always_carry_the_unit_letter(void) {
  char buf[12];

  // Imperial: signs appear only on negatives
  s_settings_units = 0;
  s_weather_temp = 72;
  format_strip_temp(buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("72F", buf);
  s_weather_temp = -22;
  format_strip_temp(buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("-22F", buf);

  // Metric: always signed, always lettered — the letter is what tells a strip
  // reading from humidity or PCP at a glance
  s_settings_units = 1;
  s_weather_temp = 22;
  format_strip_temp(buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("+22C", buf);
  s_weather_temp = -9;
  format_strip_temp(buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("-9C", buf);
  s_weather_temp = -999;
  format_strip_temp(buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("--", buf);

  // Worst case is a winter extreme: four cells, the chip's accepted spill
  s_settings_units = 1;
  s_weather_temp = -22;
  format_strip_temp(buf, sizeof(buf));
  TEST_ASSERT_TRUE(strlen(buf) <= 4);
}

void test_full_weather_chips_should_fill_only_on_a_status_color(void) {
  const FullWeatherField* cond = &s_full_weather_fields[0];
  const FullWeatherField* temp = &s_full_weather_fields[1];
  const FullWeatherField* hum = &s_full_weather_fields[2];
  const FullWeatherField* pcp = &s_full_weather_fields[3];

  // Neutral weather (panel theme, imperial): everything plain text
  s_weather_temp = 72;
  strcpy(s_weather_cond, "SUN");
  s_weather_humidity = -1;
  s_weather_pcp = -1;
  TEST_ASSERT_FALSE(strip_field_is_banded(cond));
  TEST_ASSERT_FALSE(strip_field_is_banded(temp));
  TEST_ASSERT_FALSE(strip_field_is_banded(hum));
  TEST_ASSERT_FALSE(strip_field_is_banded(pcp));

  // Extremes and comfort statuses earn their fills
  s_weather_temp = 90;
  TEST_ASSERT_TRUE(strip_field_is_banded(temp));
  s_weather_humidity = 45;  // plain readout — never a fill
  TEST_ASSERT_FALSE(strip_field_is_banded(hum));
  s_weather_humidity = 65;
  TEST_ASSERT_FALSE(strip_field_is_banded(hum));
  s_weather_humidity = 95;  // however muggy, no band
  TEST_ASSERT_FALSE(strip_field_is_banded(hum));
  s_weather_pcp = 20;  // dry — neutral
  TEST_ASSERT_FALSE(strip_field_is_banded(pcp));
  s_weather_pcp = 50;  // 50 is now the neutral ceiling
  TEST_ASSERT_FALSE(strip_field_is_banded(pcp));
  s_weather_pcp = 70;  // pack the umbrella
  TEST_ASSERT_TRUE(strip_field_is_banded(pcp));
}

void test_full_weather_captions_should_align_with_the_strip(void) {
  // Captions come from the field table itself and are pixel-centred over
  // chips the same way values are, so the table is self-consistent by
  // construction — what can still break is sizing: every caption must fit
  // its chip, and the strip must fit between the borders.
  int cells = 0;
  for (size_t i = 0; i < FULL_WEATHER_NUM_FIELDS; i++) {
    TEST_ASSERT_TRUE(strlen(s_full_weather_fields[i].caption) > 0);
    TEST_ASSERT_TRUE((int)strlen(s_full_weather_fields[i].caption) <=
                     s_full_weather_fields[i].cells);
    cells += s_full_weather_fields[i].cells;
  }
  cells += FULL_WEATHER_NUM_FIELDS - 1;  // one-cell gaps between chips
  TEST_ASSERT_EQUAL_INT(FULL_WEATHER_STRIP_CELLS, cells);
  TEST_ASSERT_TRUE(FULL_WEATHER_STRIP_CELLS * VGA16_CHAR_W <= LAYOUT_W - 2 * WINDOW_BORDER_PX);
}

void test_get_source_data_should_format_pcp(void) {
  char buf[16];
  int percent = -1;

  // The unit accent needs canvas rendering; a text layer could not draw it
  TEST_ASSERT_NOT_NULL(canvas_drawer(DATA_SOURCE_WEATHER_PCP));

  s_weather_pcp = -1;
  get_source_data(DATA_SOURCE_WEATHER_PCP, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("--", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);

  s_weather_pcp = 45;
  get_source_data(DATA_SOURCE_WEATHER_PCP, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("45%", buf);
  TEST_ASSERT_EQUAL_INT(45, percent);

  s_weather_pcp = 0;
  get_source_data(DATA_SOURCE_WEATHER_PCP, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("0%", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);  // zero probability is real data

  // Metric and actively precipitating: the live rate replaces the guess
  s_settings_units = 1;
  s_weather_pcp = 45;
  strcpy(s_weather_cond, "RAIN");
  s_precip_now = 25;  // 2.5mm over the past hour
  get_source_data(DATA_SOURCE_WEATHER_PCP, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("2mm", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);  // amount tells no progress-story

  strcpy(s_weather_cond, "TSTM");
  s_precip_now = 1230;  // cloudburst clamps
  get_source_data(DATA_SOURCE_WEATHER_PCP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("99mm", buf);

  strcpy(s_weather_cond, "SNOW");
  s_precip_now = 4;  // trace drizzle
  get_source_data(DATA_SOURCE_WEATHER_PCP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("<1mm", buf);

  // Imperial never switches
  s_settings_units = 0;
  get_source_data(DATA_SOURCE_WEATHER_PCP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("45%", buf);

  // Settled sky or a missing live reading falls back to probability
  s_settings_units = 1;
  strcpy(s_weather_cond, "SUN");
  get_source_data(DATA_SOURCE_WEATHER_PCP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("45%", buf);
  strcpy(s_weather_cond, "RAIN");
  s_precip_now = -1;
  get_source_data(DATA_SOURCE_WEATHER_PCP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("45%", buf);
}

void test_get_source_data_should_format_high_low(void) {
  char buf[24];

  // Any missing extreme must not leak a half-number
  s_temp_high = -999;
  s_temp_low = 61;
  s_temp_low_tmrw = 55;
  s_temp_high_tmrw = 77;
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("-- --", buf);

  s_temp_high = 82;
  s_temp_low = -999;
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("-- --", buf);

  // …and tomorrow's extremes sink it too: partial data reads as data
  s_temp_low = 61;
  s_temp_low_tmrw = -999;
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("-- --", buf);

  s_temp_low_tmrw = 55;
  s_temp_high_tmrw = -999;
  s_wall_hour = 8;
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("-- --", buf);
  s_wall_hour = 21;
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("-- --", buf);

  // Values with unknown event hours still display; LO leads (see the layout
  // test below). hours are -1 here by the setUp reset.
  s_temp_high_tmrw = 77;
  s_settings_units = 0;
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("61F 82F", buf);

  s_settings_units = 1;
  s_temp_high = 28;
  s_temp_low = 4;
  s_temp_low_tmrw = 1;
  s_temp_high_tmrw = 26;
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("+4C +28C", buf);

  // Top-slot values cap at 11 cells even at winter extremes, any hour
  s_temp_high = 3;
  s_temp_low = -25;
  s_temp_low_tmrw = -25;
  s_temp_high_tmrw = -20;
  s_wall_hour = 8;
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_TRUE(strlen(buf) <= 11);
  s_wall_hour = 21;
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_TRUE(strlen(buf) <= 11);
}

void test_high_low_cells_should_roll_when_their_extreme_hour_ends(void) {
  char buf[24];
  s_settings_units = 1;
  // Typical day: this morning's low at 05:00, this afternoon's high at 15:00
  s_temp_low = 11;
  s_temp_high = 20;
  s_temp_low_tmrw = 7;
  s_temp_high_tmrw = 22;
  s_lo_hour_today = 5;
  s_hi_hour_today = 15;
  s_lo_hour_tmrw = 5;
  s_hi_hour_tmrw = 15;

  s_wall_hour = 4;  // nothing passed: today's pair, LO leads
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("+11C +20C", buf);

  s_wall_hour = 5;  // inside the low's own hour: it only rolls when the hour
                    // ends — today's low while it's the low
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("+11C +20C", buf);

  s_wall_hour = 6;  // the 05:00 hour is over: low rolled to tonight's; the
                    // 15:00 high leads as the next event
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("+20C +7C", buf);

  s_wall_hour = 15;  // inside the high's own hour — still today's high
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("+20C +7C", buf);

  s_wall_hour = 16;  // the 15:00 hour is over: tomorrow's pair, dawn LO leads
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("+7C +22C", buf);

  s_wall_hour = 23;
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("+7C +22C", buf);
}

void test_high_low_cells_should_stay_chronological_on_inversion_days(void) {
  char buf[24];
  s_settings_units = 1;
  // Front day: the day's low comes at 22:00, its high at 11:00
  s_temp_low = 11;
  s_temp_high = 20;
  s_temp_low_tmrw = 7;
  s_temp_high_tmrw = 22;
  s_lo_hour_today = 22;
  s_hi_hour_today = 11;
  s_lo_hour_tmrw = 5;
  s_hi_hour_tmrw = 15;

  s_wall_hour = 12;  // high passed: right cell is tomorrow's high; low leads
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("+11C +22C", buf);

  s_wall_hour = 21;  // the low's own event hasn't passed yet
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("+11C +22C", buf);

  s_wall_hour = 23;  // both passed: tomorrow's pair, LO sooner
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("+7C +22C", buf);
}

void test_high_low_layout_should_fall_back_to_lo_first_when_hours_unknown(void) {
  char buf[24];
  s_settings_units = 1;
  s_temp_low = 11;
  s_temp_high = 20;
  s_temp_low_tmrw = 7;
  s_temp_high_tmrw = 22;  // hours all -1 via setUp: no roll, no sort
  s_wall_hour = 21;
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("+11C +20C", buf);
}

void test_high_low_stub_order_should_follow_the_layout(void) {
  char buf[24];
  s_settings_units = 1;
  // Typical day: low at 02:00, high at 14:00 — the noon readout (+20/+16
  // under a MIN/MAX caption) that read as "min 20, max 16"
  s_temp_low = 11;
  s_temp_high = 20;
  s_temp_low_tmrw = 7;
  s_temp_high_tmrw = 22;
  s_lo_hour_today = 2;
  s_hi_hour_today = 14;
  s_lo_hour_tmrw = 4;
  s_hi_hour_tmrw = 16;

  s_wall_hour = 12;  // low rolled to tonight's; the 14:00 high leads
  TEST_ASSERT_TRUE(high_low_hi_leads());
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  // stubs and numbers share high_low_hi_leads(): the stub order below can't
  // drift from the value order here
  TEST_ASSERT_EQUAL_STRING("+20C +7C", buf);

  s_wall_hour = 1;  // before dawn: today's pair, LO leads
  TEST_ASSERT_FALSE(high_low_hi_leads());
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("+11C +20C", buf);

  s_wall_hour = 16;  // high passed too: tomorrow's pair, its dawn LO leads
  TEST_ASSERT_FALSE(high_low_hi_leads());
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("+7C +22C", buf);

  // Layout-only decision: missing values don't silence the stubs
  s_temp_high = -999;
  s_wall_hour = 12;
  TEST_ASSERT_TRUE(high_low_hi_leads());

  // Unknown hours own the fallback, values or not
  s_lo_hour_today = -1;
  s_hi_hour_today = -1;
  s_lo_hour_tmrw = -1;
  s_hi_hour_tmrw = -1;
  s_temp_low = -999;
  s_temp_low_tmrw = -999;
  s_temp_high_tmrw = -999;
  TEST_ASSERT_FALSE(high_low_hi_leads());
}

void test_compute_beats_should_map_the_bmt_day_to_0_999(void) {
  // BMT is UTC+1, so the beat day rolls over at 23:00 UTC.
  TEST_ASSERT_EQUAL_INT(0, compute_beats(82800));         // 23:00:00 UTC = @000
  TEST_ASSERT_EQUAL_INT(999, compute_beats(82799));       // 22:59:59 UTC = @999
  TEST_ASSERT_EQUAL_INT(41, compute_beats(0));            // epoch = 01:00 BMT
  TEST_ASSERT_EQUAL_INT(500, compute_beats(39600));       // 11:00:00 UTC = noon BMT
  TEST_ASSERT_EQUAL_INT(1, compute_beats(82887));         // one beat is 86.4s
  TEST_ASSERT_EQUAL_INT(763, compute_beats(1785000000));  // no overflow at modern timestamps
}

void test_get_source_data_should_format_beats(void) {
  char buf[16];
  int percent = -1;

  s_beats = 347;
  get_source_data(DATA_SOURCE_BEATS, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("@347", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);  // not a progress source

  s_beats = 0;
  get_source_data(DATA_SOURCE_BEATS, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("@000", buf);

  s_beats = 7;
  get_source_data(DATA_SOURCE_BEATS, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("@007", buf);

  s_beats = 999;
  get_source_data(DATA_SOURCE_BEATS, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("@999", buf);
}

void test_get_source_color_should_return_appropriate_colors(void) {
  s_active_theme = &s_theme_panel;

  // Weather Temp color severity (Imperial: >85 red, <40 blue)
  s_settings_units = 0;
  s_weather_temp = 70;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_WEATHER_TEMP));
  s_weather_temp = 90;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_WEATHER_TEMP));
  s_weather_temp = 35;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.accent_cold, get_source_color(DATA_SOURCE_WEATHER_TEMP));

  // Weather Temp color severity (Metric: >29 red, <4 blue)
  s_settings_units = 1;
  s_weather_temp = 20;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_WEATHER_TEMP));
  s_weather_temp = 30;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_WEATHER_TEMP));
  s_weather_temp = 2;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.accent_cold, get_source_color(DATA_SOURCE_WEATHER_TEMP));

  // AQI speaks up only outside the good range: clean air is unremarkable
  // and reads neutral; past 50 earns yellow, past 100 red
  s_weather_aqi = -1;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_AQI));

  s_weather_aqi = 34;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_AQI));

  s_weather_aqi = 50;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_AQI));

  s_weather_aqi = 51;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_AQI));

  s_weather_aqi = 100;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_AQI));

  s_weather_aqi = 101;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_AQI));

  s_weather_aqi = 150;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_AQI));

  // UV likewise: mild sun is quiet; from 3 a thought, from 6 a warning
  s_weather_uv = -1;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_UV));

  s_weather_uv = 2;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_UV));

  s_weather_uv = 3;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_UV));

  s_weather_uv = 5;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_UV));

  s_weather_uv = 6;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_UV));

  s_weather_uv = 8;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_UV));

  // AQI / UV combined: only a flagged half colors the pair
  s_weather_aqi = 34;
  s_weather_uv = 1;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_AQI_UV));

  s_weather_aqi = 65;  // yellow
  s_weather_uv = 1;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_AQI_UV));

  s_weather_aqi = 34;
  s_weather_uv = 8;  // red
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_AQI_UV));

  // Humidity is a plain readout like heart rate: outdoor RH has no
  // actionable threshold, so no value earns a color
  s_weather_humidity = -1;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_HUMIDITY));

  s_weather_humidity = 29;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_HUMIDITY));

  s_weather_humidity = 45;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_HUMIDITY));

  s_weather_humidity = 65;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_HUMIDITY));

  s_weather_humidity = 70;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_HUMIDITY));

  s_weather_humidity = 95;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_HUMIDITY));

  s_weather_humidity = 100;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_HUMIDITY));

  // Precipitation probability: at or under 50 the day is unremarkable and
  // reads neutral; only a likely chance of rain earns yellow, then red
  s_weather_pcp = -1;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_WEATHER_PCP));

  s_weather_pcp = 50;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_WEATHER_PCP));

  s_weather_pcp = 51;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_WEATHER_PCP));

  s_weather_pcp = 70;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_WEATHER_PCP));

  s_weather_pcp = 71;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_WEATHER_PCP));

  // In amount mode the bands are WMO intensities (mm over the past hour)
  s_settings_units = 1;
  strcpy(s_weather_cond, "RAIN");
  s_precip_now = 20;  // 2mm light — calm
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_WEATHER_PCP));
  s_precip_now = 50;  // 5mm heavy
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_WEATHER_PCP));
  s_precip_now = 90;  // 9mm violent
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_WEATHER_PCP));
  s_precip_now = -1;
  strcpy(s_weather_cond, "--");
  s_settings_units = 0;

  // High/low takes the SHARED temperature bands of the day's high; the low
  // never colors it, and missing data stays neutral
  s_temp_high = -999;
  s_temp_low = -999;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_TEMP_HIGH_LOW));

  s_settings_units = 1;
  s_temp_high = 30;
  s_temp_low = 18;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_TEMP_HIGH_LOW));

  s_temp_high = 3;
  s_temp_low = -10;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.accent_cold, get_source_color(DATA_SOURCE_TEMP_HIGH_LOW));

  s_temp_high = 20;
  s_temp_low = -10;  // a freezing low must not tint a mild day
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_TEMP_HIGH_LOW));

  // The color tracks the high on display: once today's high is an hour past,
  // tomorrow's takes over the headline
  s_temp_high = 30;
  s_temp_low = 18;
  s_temp_high_tmrw = 20;
  s_temp_low_tmrw = 12;
  s_hi_hour_today = 15;
  s_lo_hour_today = 5;
  s_hi_hour_tmrw = 15;
  s_lo_hour_tmrw = 5;
  s_wall_hour = 8;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_TEMP_HIGH_LOW));
  s_wall_hour = 21;  // today's 30C high passed; tomorrow's mild 20C headlines
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_TEMP_HIGH_LOW));
  s_hi_hour_today = -1;  // unknown timing: today's high stays the headline
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_TEMP_HIGH_LOW));

  // Battery: quiet while healthy — the chip bands and the bar paints only
  // once the charge wants a charger (>=40 plain, 20-39 yellow, <=19 red)
  s_battery_level = 100;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_BATTERY));
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_BATTERY_BAR));

  s_battery_level = 40;  // boundary: still quiet
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_BATTERY));
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_BATTERY_BAR));

  s_battery_level = 39;  // boundary: yellow
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_BATTERY));
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_BATTERY_BAR));

  s_battery_level = 20;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_BATTERY));
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_BATTERY_BAR));

  s_battery_level = 19;  // boundary: red
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_BATTERY));
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_BATTERY_BAR));

  s_battery_level = 0;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_BATTERY));
}

// Thursday 31 December 1970 — the same date the settings page uses as its
// example, so the UI, the docs and the tests all say the same thing.
static struct tm epoch_new_years_eve(void) {
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_mday = 31;
  t.tm_mon = 11;   // December
  t.tm_year = 70;  // 1970
  t.tm_wday = 4;   // Thursday
  t.tm_yday = 364;
  return t;
}

void test_format_date_string_should_render_every_body(void) {
  char buf[64];
  struct tm t = epoch_new_years_eve();

  // Bodies, with the weekday hidden so each is seen on its own.
  format_date_string(DATE_FORMAT_ISO, SHORT_DATE_MONTH_DAY, DOW_HIDDEN, &t, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("1970-12-31", buf);

  format_date_string(DATE_FORMAT_DOS, SHORT_DATE_MONTH_DAY, DOW_HIDDEN, &t, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("31-12-1970", buf);

  format_date_string(DATE_FORMAT_TEXT, SHORT_DATE_MONTH_DAY, DOW_HIDDEN, &t, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("DEC 31st, 1970", buf);

  // Short defers to the short-date setting for its order.
  format_date_string(DATE_FORMAT_SHORT, SHORT_DATE_MONTH_DAY, DOW_HIDDEN, &t, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("12-31", buf);
  format_date_string(DATE_FORMAT_SHORT, SHORT_DATE_DAY_MONTH, DOW_HIDDEN, &t, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("31-12", buf);

  // An unrecognized format falls back to ISO rather than emptying the window.
  format_date_string(99, SHORT_DATE_MONTH_DAY, DOW_HIDDEN, &t, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("1970-12-31", buf);
}

void test_every_date_combination_should_fit_its_window(void) {
  // The centre DATE window is LAYOUT_W wide, less its two 2px borders, so it
  // holds 22 glyph cells. Nothing clips overlong text — vga16_value_rect sizes
  // the draw rect to the string, not the box — so an overlong date spills over
  // the frame. The full month name used to do exactly that for about a quarter
  // of the year, which is why the text format abbreviates.
  const int cap = (LAYOUT_W - 2 * WINDOW_BORDER_PX) / VGA16_CHAR_W;
  const int month_days[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  char buf[64];
  struct tm t = epoch_new_years_eve();

  for (int month = 0; month < 12; month++) {
    t.tm_mon = month;
    for (int day = 1; day <= month_days[month]; day++) {
      t.tm_mday = day;
      for (int fmt = 0; fmt <= 3; fmt++) {
        for (int dow = 0; dow <= 2; dow++) {
          format_date_string(fmt, SHORT_DATE_MONTH_DAY, dow, &t, buf, sizeof(buf));
          if ((int)strlen(buf) > cap) {
            char msg[96];
            snprintf(msg, sizeof(msg), "%s = %d cells, over %d", buf, (int)strlen(buf), cap);
            TEST_FAIL_MESSAGE(msg);
          }
        }
      }
    }
  }
}

void test_weekday_position_should_be_independent_of_the_body(void) {
  char buf[64];
  struct tm t = epoch_new_years_eve();

  // The weekday setting applies to every body, which is the whole point of
  // splitting it out of the format.
  int bodies[] = {DATE_FORMAT_ISO, DATE_FORMAT_DOS, DATE_FORMAT_TEXT, DATE_FORMAT_SHORT};
  for (unsigned i = 0; i < sizeof(bodies) / sizeof(bodies[0]); i++) {
    format_date_string(bodies[i], SHORT_DATE_MONTH_DAY, DOW_BEFORE, &t, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING_LEN("THU ", buf, 4);
    TEST_ASSERT_EQUAL_INT(0, date_dow_offset(DOW_BEFORE, buf));

    format_date_string(bodies[i], SHORT_DATE_MONTH_DAY, DOW_AFTER, &t, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("THU", buf + strlen(buf) - DOW_LEN);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf) - DOW_LEN, date_dow_offset(DOW_AFTER, buf));

    format_date_string(bodies[i], SHORT_DATE_MONTH_DAY, DOW_HIDDEN, &t, buf, sizeof(buf));
    TEST_ASSERT_NULL(strstr(buf, "THU"));
    // Hidden means there is nothing to accent.
    TEST_ASSERT_EQUAL_INT(-1, date_dow_offset(DOW_HIDDEN, buf));
  }

  format_date_string(DATE_FORMAT_ISO, SHORT_DATE_MONTH_DAY, DOW_BEFORE, &t, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("THU 1970-12-31", buf);
  format_date_string(DATE_FORMAT_ISO, SHORT_DATE_MONTH_DAY, DOW_AFTER, &t, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("1970-12-31 THU", buf);

  // Degenerate input must stay in bounds rather than index before the string.
  TEST_ASSERT_EQUAL_INT(-1, date_dow_offset(DOW_AFTER, ""));
  TEST_ASSERT_EQUAL_INT(0, date_dow_offset(DOW_BEFORE, ""));
}

void test_short_date_should_stay_short_whatever_the_date_format(void) {
  char buf[16];
  struct tm t = epoch_new_years_eve();

  // The complication always renders the year-less form, and must fit the
  // 11-character top slot in every combination.
  for (int shortfmt = 0; shortfmt <= 1; shortfmt++) {
    for (int dow = 0; dow <= 2; dow++) {
      format_short_date_string(shortfmt, dow, &t, buf, sizeof(buf));
      TEST_ASSERT_TRUE(strlen(buf) <= 11);
      TEST_ASSERT_NULL(strstr(buf, "1970"));
    }
  }

  format_short_date_string(SHORT_DATE_MONTH_DAY, DOW_BEFORE, &t, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("THU 12-31", buf);
  format_short_date_string(SHORT_DATE_DAY_MONTH, DOW_AFTER, &t, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("31-12 THU", buf);
  format_short_date_string(SHORT_DATE_DAY_MONTH, DOW_HIDDEN, &t, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("31-12", buf);
}

void test_weather_cache_should_round_trip_when_fresh(void) {
  mock_persist_reset();

  s_weather_temp = 72;
  strcpy(s_weather_cond, "SUN");
  s_weather_aqi = 42;
  s_weather_uv = 5;
  s_weather_humidity = 55;
  s_weather_pcp = 35;
  s_precip_now = 25;
  s_temp_high = 82;
  s_temp_low = 61;
  s_temp_low_tmrw = 55;
  s_temp_high_tmrw = 77;
  s_lo_hour_today = 5;
  s_hi_hour_today = 15;
  s_lo_hour_tmrw = 4;
  s_hi_hour_tmrw = 14;
  s_weather_wind_direction = 270;
  save_weather_cache();

  // Simulate a relaunch: globals reset to sentinels
  s_weather_temp = -999;
  strcpy(s_weather_cond, "--");
  s_weather_aqi = -1;
  s_weather_uv = -1;
  s_weather_humidity = -1;
  s_weather_pcp = -1;
  s_precip_now = -1;
  s_temp_high = -999;
  s_temp_low = -999;
  s_temp_low_tmrw = -999;
  s_temp_high_tmrw = -999;
  s_lo_hour_today = -1;
  s_hi_hour_today = -1;
  s_lo_hour_tmrw = -1;
  s_hi_hour_tmrw = -1;
  s_weather_wind_direction = -1;

  TEST_ASSERT_TRUE(load_weather_cache());
  TEST_ASSERT_EQUAL_INT(72, s_weather_temp);
  TEST_ASSERT_EQUAL_STRING("SUN", s_weather_cond);
  TEST_ASSERT_EQUAL_INT(42, s_weather_aqi);
  TEST_ASSERT_EQUAL_INT(5, s_weather_uv);
  TEST_ASSERT_EQUAL_INT(55, s_weather_humidity);
  TEST_ASSERT_EQUAL_INT(35, s_weather_pcp);
  TEST_ASSERT_EQUAL_INT(25, s_precip_now);
  TEST_ASSERT_EQUAL_INT(82, s_temp_high);
  TEST_ASSERT_EQUAL_INT(61, s_temp_low);
  TEST_ASSERT_EQUAL_INT(55, s_temp_low_tmrw);
  TEST_ASSERT_EQUAL_INT(77, s_temp_high_tmrw);
  TEST_ASSERT_EQUAL_INT(5, s_lo_hour_today);
  TEST_ASSERT_EQUAL_INT(15, s_hi_hour_today);
  TEST_ASSERT_EQUAL_INT(4, s_lo_hour_tmrw);
  TEST_ASSERT_EQUAL_INT(14, s_hi_hour_tmrw);
  TEST_ASSERT_EQUAL_INT(270, s_weather_wind_direction);
}

void test_weather_cache_should_leave_extreme_timing_at_sentinel_in_old_caches(void) {
  // A cache written by an older build has neither LOW_TOMORROW nor any of the
  // rollover keys; loading it must not invent values — the formatter sinks
  // the pair instead.
  mock_persist_reset();
  persist_write_int(PERSIST_KEY_WEATHER_TIMESTAMP, (int32_t)time(NULL));
  persist_write_int(PERSIST_KEY_WEATHER_TEMP, 72);
  persist_write_int(PERSIST_KEY_WEATHER_HIGH, 82);
  persist_write_int(PERSIST_KEY_WEATHER_LOW, 61);
  s_temp_low_tmrw = -999;
  s_temp_high_tmrw = -999;
  s_lo_hour_today = -1;
  s_hi_hour_today = -1;
  s_lo_hour_tmrw = -1;
  s_hi_hour_tmrw = -1;
  s_weather_wind_direction = -1;
  s_weather_wind_speed = -1;

  TEST_ASSERT_TRUE(load_weather_cache());
  TEST_ASSERT_EQUAL_INT(82, s_temp_high);
  TEST_ASSERT_EQUAL_INT(61, s_temp_low);
  TEST_ASSERT_EQUAL_INT(-999, s_temp_low_tmrw);
  TEST_ASSERT_EQUAL_INT(-999, s_temp_high_tmrw);
  TEST_ASSERT_EQUAL_INT(-1, s_lo_hour_today);
  TEST_ASSERT_EQUAL_INT(-1, s_hi_hour_today);
  TEST_ASSERT_EQUAL_INT(-1, s_lo_hour_tmrw);
  TEST_ASSERT_EQUAL_INT(-1, s_hi_hour_tmrw);
  TEST_ASSERT_EQUAL_INT(-1, s_weather_wind_direction);
}

void test_weather_cache_should_reject_missing_or_stale_data(void) {
  mock_persist_reset();

  // Nothing persisted yet
  TEST_ASSERT_FALSE(load_weather_cache());
  TEST_ASSERT_EQUAL_INT(-999, s_weather_temp);

  // Persist, then age the timestamp past the 30-minute window
  s_weather_temp = 72;
  strcpy(s_weather_cond, "SUN");
  save_weather_cache();
  persist_write_int(PERSIST_KEY_WEATHER_TIMESTAMP,
                    (int32_t)time(NULL) - (WEATHER_CACHE_MAX_AGE_S + 1));

  s_weather_temp = -999;
  strcpy(s_weather_cond, "--");
  TEST_ASSERT_FALSE(load_weather_cache());
  TEST_ASSERT_EQUAL_INT(-999, s_weather_temp);
  TEST_ASSERT_EQUAL_STRING("--", s_weather_cond);

  // A timestamp from the future (clock change) is also rejected
  persist_write_int(PERSIST_KEY_WEATHER_TIMESTAMP, (int32_t)time(NULL) + 3600);
  TEST_ASSERT_FALSE(load_weather_cache());
}

void test_weather_cache_should_keep_values_at_edge_of_window(void) {
  mock_persist_reset();

  s_weather_temp = 18;
  strcpy(s_weather_cond, "RAIN");
  s_weather_aqi = 12;
  s_weather_uv = 2;
  save_weather_cache();
  // Just inside the freshness window
  persist_write_int(PERSIST_KEY_WEATHER_TIMESTAMP,
                    (int32_t)time(NULL) - (WEATHER_CACHE_MAX_AGE_S - 5));

  s_weather_temp = -999;
  TEST_ASSERT_TRUE(load_weather_cache());
  TEST_ASSERT_EQUAL_INT(18, s_weather_temp);
  TEST_ASSERT_EQUAL_STRING("RAIN", s_weather_cond);
}

void test_settings_should_round_trip_through_persistence(void) {
  mock_persist_reset();

  s_settings_theme = 2;        // Night
  s_settings_units = 1;        // Metric
  s_settings_date_format = 2;  // Full text
  s_complication_slots[0].source = DATA_SOURCE_AQI;
  s_complication_slots[4].source = DATA_SOURCE_UV;

  // Persist exactly as inbox_received_callback does, via the dedicated keys.
  persist_write_int(PERSIST_KEY_SETTINGS_THEME, s_settings_theme);
  persist_write_int(PERSIST_KEY_SETTINGS_UNITS, s_settings_units);
  persist_write_int(PERSIST_KEY_SETTINGS_DATE_FORMAT, s_settings_date_format);
  persist_write_int(PERSIST_KEY_SLOT_1, s_complication_slots[0].source);
  persist_write_int(PERSIST_KEY_SLOT_5, s_complication_slots[4].source);

  // Simulate a relaunch: globals reset to defaults
  s_settings_theme = 0;
  s_settings_units = 0;
  s_settings_date_format = 0;
  s_complication_slots[0].source = DATA_SOURCE_EMPTY;
  s_complication_slots[4].source = DATA_SOURCE_EMPTY;

  load_settings();
  TEST_ASSERT_EQUAL_INT(2, s_settings_theme);
  TEST_ASSERT_EQUAL_INT(1, s_settings_units);
  TEST_ASSERT_EQUAL_INT(2, s_settings_date_format);
  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_AQI, s_complication_slots[0].source);
  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_UV, s_complication_slots[4].source);
}

void test_settings_persistence_is_decoupled_from_message_key_ids(void) {
  // Settings must load from their own PERSIST_KEY_* constants, never from the
  // auto-generated MESSAGE_KEY_* ids. This guards against the old shortcut
  // where reordering package.json's messageKeys would scramble saved data.
  mock_persist_reset();

  // Real saved values, under the dedicated persist keys.
  persist_write_int(PERSIST_KEY_SETTINGS_THEME, 2);
  persist_write_int(PERSIST_KEY_SLOT_1, DATA_SOURCE_AQI);

  // Decoys under the message-key ids — load_settings must ignore these.
  persist_write_int(MESSAGE_KEY_SETTINGS_THEME, 99);
  persist_write_int(MESSAGE_KEY_SLOT_1, DATA_SOURCE_BATTERY);

  s_settings_theme = 0;
  s_complication_slots[0].source = DATA_SOURCE_EMPTY;

  load_settings();
  TEST_ASSERT_EQUAL_INT(2, s_settings_theme);
  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_AQI, s_complication_slots[0].source);
}

void test_update_health_info_should_read_heart_rate(void) {
  // The mock reports HR inaccessible for range queries (like real firmware),
  // so this passing proves update_health_info uses an instant query.
  mock_heart_rate = 72;
  update_health_info();
  TEST_ASSERT_EQUAL_INT(72, s_heart_rate);

  char buf[16];
  get_source_data(DATA_SOURCE_HEART_RATE, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("72", buf);

  // No recent reading: 0 renders as the no-data state
  mock_heart_rate = 0;
  update_health_info();
  TEST_ASSERT_EQUAL_INT(0, s_heart_rate);
  get_source_data(DATA_SOURCE_HEART_RATE, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("--", buf);
}

static void save_slots(ComplicationDataSource* saved) {
  for (int i = 0; i < NUM_SLOTS; i++) saved[i] = s_complication_slots[i].source;
}

static void restore_slots(const ComplicationDataSource* saved) {
  for (int i = 0; i < NUM_SLOTS; i++) s_complication_slots[i].source = saved[i];
}

void test_update_health_info_should_do_nothing_with_no_health_slots(void) {
  ComplicationDataSource saved[NUM_SLOTS];
  save_slots(saved);
  ComplicationDataSource weather_free[NUM_SLOTS] = {DATA_SOURCE_DATE,   DATA_SOURCE_BLUETOOTH,
                                                    DATA_SOURCE_BEATS,  DATA_SOURCE_SHORT_DATE,
                                                    DATA_SOURCE_AQI_UV, DATA_SOURCE_FULL_DATE};
  restore_slots(weather_free);

  mock_health_accessible_count = 0;
  mock_health_sum_today_count = 0;
  mock_health_peek_count = 0;
  update_health_info();
  TEST_ASSERT_EQUAL_INT(0, mock_health_accessible_count);
  TEST_ASSERT_EQUAL_INT(0, mock_health_sum_today_count);
  TEST_ASSERT_EQUAL_INT(0, mock_health_peek_count);

  restore_slots(saved);
}

void test_update_health_info_should_read_only_displayed_metrics(void) {
  ComplicationDataSource saved[NUM_SLOTS];
  save_slots(saved);
  ComplicationDataSource only_steps[NUM_SLOTS] = {DATA_SOURCE_STEPS, DATA_SOURCE_BLUETOOTH,
                                                  DATA_SOURCE_BEATS, DATA_SOURCE_SHORT_DATE,
                                                  DATA_SOURCE_DATE,  DATA_SOURCE_FULL_DATE};
  restore_slots(only_steps);

  mock_health_accessible_count = 0;
  mock_health_sum_today_count = 0;
  mock_health_peek_count = 0;
  update_health_info();
  // Steps alone: one accessibility check, one sum; no sleep/active/HR reads.
  TEST_ASSERT_EQUAL_INT(1, mock_health_accessible_count);
  TEST_ASSERT_EQUAL_INT(1, mock_health_sum_today_count);
  TEST_ASSERT_EQUAL_INT(0, mock_health_peek_count);

  restore_slots(saved);
}

// PebbleOS posts MovementUpdate per accel batch at motion rate; a step- or
// sleep-bearing slot turns every one into a render. HEALTH_EVENT_THROTTLE_S
// bounds that to one refresh per window.
void test_health_handler_should_throttle_movement_updates(void) {
  ComplicationDataSource saved[NUM_SLOTS];
  save_slots(saved);
  ComplicationDataSource only_steps[NUM_SLOTS] = {DATA_SOURCE_STEPS, DATA_SOURCE_BLUETOOTH,
                                                  DATA_SOURCE_BEATS, DATA_SOURCE_SHORT_DATE,
                                                  DATA_SOURCE_DATE,  DATA_SOURCE_FULL_DATE};
  restore_slots(only_steps);

  mock_health_accessible_count = 0;
  health_handler(HealthEventMovementUpdate, NULL);
  TEST_ASSERT_EQUAL_INT(1, mock_health_accessible_count);
  int dirty_after_first = mock_mark_dirty_count;

  mock_time_offset += 3;  // inside the window: dropped, no work, no redraw
  health_handler(HealthEventMovementUpdate, NULL);
  TEST_ASSERT_EQUAL_INT(1, mock_health_accessible_count);
  TEST_ASSERT_EQUAL_INT(dirty_after_first, mock_mark_dirty_count);

  restore_slots(saved);
}

void test_health_handler_should_refresh_again_after_the_throttle_window(void) {
  ComplicationDataSource saved[NUM_SLOTS];
  save_slots(saved);
  ComplicationDataSource only_steps[NUM_SLOTS] = {DATA_SOURCE_STEPS, DATA_SOURCE_BLUETOOTH,
                                                  DATA_SOURCE_BEATS, DATA_SOURCE_SHORT_DATE,
                                                  DATA_SOURCE_DATE,  DATA_SOURCE_FULL_DATE};
  restore_slots(only_steps);

  mock_health_accessible_count = 0;
  health_handler(HealthEventMovementUpdate, NULL);
  TEST_ASSERT_EQUAL_INT(1, mock_health_accessible_count);

  mock_time_offset += HEALTH_EVENT_THROTTLE_S + 1;
  health_handler(HealthEventMovementUpdate, NULL);
  TEST_ASSERT_EQUAL_INT(2, mock_health_accessible_count);

  restore_slots(saved);
}

// A posted HR reading is already fresh — stale BPM is worse than a redraw
// (ISSUES.md), so heart-rate events bypass the throttle.
void test_health_handler_should_not_throttle_heart_rate_updates(void) {
  ComplicationDataSource saved[NUM_SLOTS];
  save_slots(saved);
  ComplicationDataSource only_steps[NUM_SLOTS] = {DATA_SOURCE_STEPS, DATA_SOURCE_BLUETOOTH,
                                                  DATA_SOURCE_BEATS, DATA_SOURCE_SHORT_DATE,
                                                  DATA_SOURCE_DATE,  DATA_SOURCE_FULL_DATE};
  restore_slots(only_steps);

  mock_health_accessible_count = 0;
  health_handler(HealthEventMovementUpdate, NULL);  // opens a throttle window
  TEST_ASSERT_EQUAL_INT(1, mock_health_accessible_count);
  health_handler(HealthEventHeartRateUpdate, NULL);
  health_handler(HealthEventHeartRateUpdate, NULL);
  TEST_ASSERT_EQUAL_INT(3, mock_health_accessible_count);

  restore_slots(saved);
}

// SignificantUpdate is the applib cache-invalidated signal — rare and
// load-bearing, so it never waits out the throttle window.
void test_health_handler_should_not_throttle_significant_updates(void) {
  ComplicationDataSource saved[NUM_SLOTS];
  save_slots(saved);
  ComplicationDataSource only_steps[NUM_SLOTS] = {DATA_SOURCE_STEPS, DATA_SOURCE_BLUETOOTH,
                                                  DATA_SOURCE_BEATS, DATA_SOURCE_SHORT_DATE,
                                                  DATA_SOURCE_DATE,  DATA_SOURCE_FULL_DATE};
  restore_slots(only_steps);

  mock_health_accessible_count = 0;
  health_handler(HealthEventMovementUpdate, NULL);  // opens a throttle window
  TEST_ASSERT_EQUAL_INT(1, mock_health_accessible_count);
  health_handler(HealthEventSignificantUpdate, NULL);
  TEST_ASSERT_EQUAL_INT(2, mock_health_accessible_count);

  restore_slots(saved);
}

void test_undisplayed_health_metrics_should_read_as_no_data(void) {
  ComplicationDataSource saved[NUM_SLOTS];
  save_slots(saved);
  ComplicationDataSource weather_free[NUM_SLOTS] = {DATA_SOURCE_DATE,   DATA_SOURCE_BLUETOOTH,
                                                    DATA_SOURCE_BEATS,  DATA_SOURCE_SHORT_DATE,
                                                    DATA_SOURCE_AQI_UV, DATA_SOURCE_FULL_DATE};
  restore_slots(weather_free);

  s_step_count = 4321;
  update_health_info();
  TEST_ASSERT_EQUAL_INT(-1, s_step_count);

  restore_slots(saved);
}

void test_handle_bluetooth_should_vibrate_only_on_disconnect_transition(void) {
  mock_vibes_count = 0;

  s_connected = true;
  handle_bluetooth(false);  // genuine drop: buzz
  TEST_ASSERT_EQUAL_INT(1, mock_vibes_count);
  TEST_ASSERT_FALSE(s_connected);

  handle_bluetooth(false);  // still disconnected (relaunch-while-away): silent
  TEST_ASSERT_EQUAL_INT(1, mock_vibes_count);

  handle_bluetooth(true);  // reconnect: silent
  TEST_ASSERT_EQUAL_INT(1, mock_vibes_count);
  TEST_ASSERT_TRUE(s_connected);

  handle_bluetooth(false);  // second genuine drop: buzz again
  TEST_ASSERT_EQUAL_INT(2, mock_vibes_count);
}

void test_handle_bluetooth_should_stay_silent_on_drops_when_the_buzz_is_disabled(void) {
  mock_vibes_count = 0;

  s_settings_disconnect_vibe = 0;  // consent select: silenced
  s_connected = true;
  handle_bluetooth(false);  // genuine drop, but silenced
  TEST_ASSERT_EQUAL_INT(0, mock_vibes_count);
  TEST_ASSERT_FALSE(s_connected);

  handle_bluetooth(true);
  handle_bluetooth(false);  // drop again: still silent
  TEST_ASSERT_EQUAL_INT(0, mock_vibes_count);
}

void test_inbox_should_parse_weather_payload_and_persist(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_cstring(MESSAGE_KEY_WEATHER_COND, "SUN");
  mock_dict_add_int(MESSAGE_KEY_WEATHER_AQI, 42);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_UV, 7);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_HUMIDITY, 55);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_PCP, 35);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_PRECIP_NOW, 25);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_HIGH, 82);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_LOW, 61);

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(72, s_weather_temp);
  TEST_ASSERT_EQUAL_STRING("SUN", s_weather_cond);
  TEST_ASSERT_EQUAL_INT(42, s_weather_aqi);
  TEST_ASSERT_EQUAL_INT(7, s_weather_uv);
  TEST_ASSERT_EQUAL_INT(55, s_weather_humidity);
  TEST_ASSERT_EQUAL_INT(35, s_weather_pcp);
  TEST_ASSERT_EQUAL_INT(25, s_precip_now);
  TEST_ASSERT_EQUAL_INT(82, s_temp_high);
  TEST_ASSERT_EQUAL_INT(61, s_temp_low);

  // A weather payload must persist the cache
  TEST_ASSERT_TRUE(persist_exists(PERSIST_KEY_WEATHER_TIMESTAMP));
  s_weather_temp = -999;
  s_weather_aqi = -1;
  TEST_ASSERT_TRUE(load_weather_cache());
  TEST_ASSERT_EQUAL_INT(72, s_weather_temp);
  TEST_ASSERT_EQUAL_INT(42, s_weather_aqi);
}

void test_inbox_should_parse_and_persist_tomorrow_low(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_cstring(MESSAGE_KEY_WEATHER_COND, "SUN");
  mock_dict_add_int(MESSAGE_KEY_WEATHER_LOW_TOMORROW, 55);

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(55, s_temp_low_tmrw);
  TEST_ASSERT_TRUE(persist_exists(PERSIST_KEY_WEATHER_LOW_TOMORROW));
  TEST_ASSERT_EQUAL_INT(55, persist_read_int(PERSIST_KEY_WEATHER_LOW_TOMORROW));
}

void test_inbox_should_parse_and_persist_wind_direction(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_cstring(MESSAGE_KEY_WEATHER_COND, "SUN");
  mock_dict_add_int(MESSAGE_KEY_WEATHER_WIND_DIRECTION, 270);

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(270, s_weather_wind_direction);
  TEST_ASSERT_TRUE(persist_exists(PERSIST_KEY_WEATHER_WIND_DIRECTION));
  TEST_ASSERT_EQUAL_INT(270, persist_read_int(PERSIST_KEY_WEATHER_WIND_DIRECTION));

  s_weather_wind_direction = -1;
  TEST_ASSERT_TRUE(load_weather_cache());
  TEST_ASSERT_EQUAL_INT(270, s_weather_wind_direction);
}

void test_wind_color_should_follow_the_beaufort_rungs(void) {
  s_active_theme = &s_theme_panel;

  // Sentinel: no band on an empty readout
  s_weather_wind_speed = -1;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_WIND));

  // Metric (m/s): strong breeze (Bf 6, 10.8 m/s) is yellow, gale (Bf 8,
  // 17.2 m/s) is red — rungs rounded to whole units
  s_settings_units = 1;
  s_weather_wind_speed = 10;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_WIND));
  s_weather_wind_speed = 11;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_WIND));
  s_weather_wind_speed = 16;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_WIND));
  s_weather_wind_speed = 17;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_WIND));

  // Imperial (mph): same Beaufort rungs, 24/39
  s_settings_units = 0;
  s_weather_wind_speed = 24;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_WIND));
  s_weather_wind_speed = 25;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_WIND));
  s_weather_wind_speed = 38;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_WIND));
  s_weather_wind_speed = 39;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_WIND));
}

void test_format_wind_narrow_should_drop_the_unit(void) {
  char buf[16];

  s_weather_wind_direction = 225;
  s_weather_wind_speed = 12;
  s_settings_units = 1;
  format_wind(buf, sizeof(buf), false);
  TEST_ASSERT_EQUAL_STRING(
      "\xE2\x86\x97"
      " 12",
      buf);

  // Narrow never prints the unit, whatever the toggle state
  s_settings_units = 0;
  format_wind(buf, sizeof(buf), false);
  TEST_ASSERT_EQUAL_STRING(
      "\xE2\x86\x97"
      " 12",
      buf);

  s_weather_wind_direction = -1;
  format_wind(buf, sizeof(buf), false);
  TEST_ASSERT_EQUAL_STRING("12", buf);

  s_weather_wind_speed = -1;
  s_weather_wind_direction = 225;
  format_wind(buf, sizeof(buf), false);
  TEST_ASSERT_EQUAL_STRING("\xE2\x86\x97", buf);
}

void test_inbox_should_parse_and_persist_wind_speed(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_cstring(MESSAGE_KEY_WEATHER_COND, "SUN");  // cache save needs a real payload
  mock_dict_add_int(MESSAGE_KEY_WEATHER_WIND_SPEED, 23);

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(23, s_weather_wind_speed);
  TEST_ASSERT_TRUE(persist_exists(PERSIST_KEY_WEATHER_WIND_SPEED));

  s_weather_wind_speed = -1;
  TEST_ASSERT_TRUE(load_weather_cache());
  TEST_ASSERT_EQUAL_INT(23, s_weather_wind_speed);
}

void test_inbox_without_wind_should_leave_the_sentinel(void) {
  // An old phone-side build sends no wind key; nothing is invented.
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_cstring(MESSAGE_KEY_WEATHER_COND, "SUN");
  s_weather_wind_direction = -1;

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(-1, s_weather_wind_direction);
}

void test_inbox_should_parse_and_persist_extreme_rollover_keys(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_cstring(MESSAGE_KEY_WEATHER_COND, "SUN");
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP_HIGH_TOMORROW, 77);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_HI_HOUR_TODAY, 15);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_LO_HOUR_TODAY, 5);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_HI_HOUR_TOMORROW, 14);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_LO_HOUR_TOMORROW, 4);

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(77, s_temp_high_tmrw);
  TEST_ASSERT_EQUAL_INT(15, s_hi_hour_today);
  TEST_ASSERT_EQUAL_INT(5, s_lo_hour_today);
  TEST_ASSERT_EQUAL_INT(14, s_hi_hour_tmrw);
  TEST_ASSERT_EQUAL_INT(4, s_lo_hour_tmrw);
  TEST_ASSERT_TRUE(persist_exists(PERSIST_KEY_WEATHER_HIGH_TOMORROW));
  TEST_ASSERT_EQUAL_INT(77, persist_read_int(PERSIST_KEY_WEATHER_HIGH_TOMORROW));
  TEST_ASSERT_EQUAL_INT(15, persist_read_int(PERSIST_KEY_WEATHER_HI_HOUR_TODAY));
  TEST_ASSERT_EQUAL_INT(5, persist_read_int(PERSIST_KEY_WEATHER_LO_HOUR_TODAY));
  TEST_ASSERT_EQUAL_INT(14, persist_read_int(PERSIST_KEY_WEATHER_HI_HOUR_TOMORROW));
  TEST_ASSERT_EQUAL_INT(4, persist_read_int(PERSIST_KEY_WEATHER_LO_HOUR_TOMORROW));
}

void test_inbox_without_extreme_timing_should_leave_the_sentinels(void) {
  // An old phone-side build sends no rollover keys; they must not be invented.
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_cstring(MESSAGE_KEY_WEATHER_COND, "SUN");
  s_temp_high_tmrw = -999;
  s_hi_hour_today = -1;
  s_lo_hour_today = -1;
  s_hi_hour_tmrw = -1;
  s_lo_hour_tmrw = -1;

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(-999, s_temp_high_tmrw);
  TEST_ASSERT_EQUAL_INT(-1, s_hi_hour_today);
  TEST_ASSERT_EQUAL_INT(-1, s_lo_hour_today);
  TEST_ASSERT_EQUAL_INT(-1, s_hi_hour_tmrw);
  TEST_ASSERT_EQUAL_INT(-1, s_lo_hour_tmrw);
}

void test_inbox_without_tomorrow_low_should_leave_the_sentinel(void) {
  // An old phone-side build sends no LOW_TOMORROW; the pair must sink, not
  // show yesterday's leftover.
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_cstring(MESSAGE_KEY_WEATHER_COND, "SUN");
  s_temp_low_tmrw = -999;

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(-999, s_temp_low_tmrw);
}

void test_inbox_settings_only_message_should_not_stamp_weather_cache(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_THEME, "2");

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(2, s_settings_theme);
  TEST_ASSERT_EQUAL_INT(2, persist_read_int(PERSIST_KEY_SETTINGS_THEME));
  TEST_ASSERT_FALSE(persist_exists(PERSIST_KEY_WEATHER_TIMESTAMP));
}

void test_inbox_should_parse_slot_assignments(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SLOT_1, "16");  // Clay sends strings
  mock_dict_add_int(MESSAGE_KEY_SLOT_5, 17);

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_AQI, s_complication_slots[0].source);
  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_UV, s_complication_slots[4].source);
  TEST_ASSERT_EQUAL_INT(16, persist_read_int(PERSIST_KEY_SLOT_1));
  TEST_ASSERT_EQUAL_INT(17, persist_read_int(PERSIST_KEY_SLOT_5));

  // Restore defaults so later tests see the boot layout
  s_complication_slots[0].source = DATA_SOURCE_WEATHER;
  s_complication_slots[4].source = DATA_SOURCE_BLUETOOTH;
}

void test_inbox_should_parse_the_newer_settings_and_centre_slot(void) {
  // Every other setting has a round-trip test; these three were added without
  // one, and a wiring gap in exactly this plumbing has broken a real build
  // before — a message key referenced in C but missing from package.json.
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_SHORT_DATE_FORMAT, "1");  // Clay sends strings
  mock_dict_add_int(MESSAGE_KEY_SETTINGS_DOW_POSITION, DOW_HIDDEN);
  mock_dict_add_cstring(MESSAGE_KEY_SLOT_6, "24");

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(SHORT_DATE_DAY_MONTH, s_settings_short_date_format);
  TEST_ASSERT_EQUAL_INT(DOW_HIDDEN, s_settings_dow_position);
  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_STEPS_BAR, s_complication_slots[5].source);

  TEST_ASSERT_EQUAL_INT(1, persist_read_int(PERSIST_KEY_SETTINGS_SHORT_DATE));
  TEST_ASSERT_EQUAL_INT(DOW_HIDDEN, persist_read_int(PERSIST_KEY_SETTINGS_DOW));
  TEST_ASSERT_EQUAL_INT(24, persist_read_int(PERSIST_KEY_SLOT_6));

  // ...and load_settings() must restore what the inbox persisted, or the choice
  // silently reverts on the next launch.
  s_settings_short_date_format = 0;
  s_settings_dow_position = 0;
  s_complication_slots[5].source = DATA_SOURCE_FULL_DATE;

  load_settings();

  TEST_ASSERT_EQUAL_INT(SHORT_DATE_DAY_MONTH, s_settings_short_date_format);
  TEST_ASSERT_EQUAL_INT(DOW_HIDDEN, s_settings_dow_position);
  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_STEPS_BAR, s_complication_slots[5].source);

  // Restore the boot layout for later tests.
  s_settings_short_date_format = 0;
  s_settings_dow_position = 0;
  s_complication_slots[5].source = DATA_SOURCE_FULL_DATE;
}

void test_inbox_should_parse_and_persist_disconnect_vibe_setting(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_SETTINGS_DISCONNECT_VIBE, 1);
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(1, s_settings_disconnect_vibe);
  TEST_ASSERT_EQUAL_INT(1, persist_read_int(PERSIST_KEY_SETTINGS_DISCONNECT_VIBE));

  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_SETTINGS_DISCONNECT_VIBE, 0);
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(0, s_settings_disconnect_vibe);
  TEST_ASSERT_EQUAL_INT(0, persist_read_int(PERSIST_KEY_SETTINGS_DISCONNECT_VIBE));

  // load_settings() must restore what the inbox persisted, or the choice
  // silently reverts on the next launch.
  s_settings_disconnect_vibe = 1;
  load_settings();
  TEST_ASSERT_EQUAL_INT(0, s_settings_disconnect_vibe);

  s_settings_disconnect_vibe = 0;
}

void test_inbox_units_change_should_trigger_weather_refetch(void) {
  mock_persist_reset();

  // Imperial -> Metric: expect one refetch so temps arrive in the new unit
  s_settings_units = 0;
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_UNITS, "1");
  int before = mock_outbox_sends;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(1, s_settings_units);
  TEST_ASSERT_EQUAL_INT(before + 1, mock_outbox_sends);

  // Same units again: no refetch
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_UNITS, "1");
  before = mock_outbox_sends;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);
}

void test_update_time_should_never_request_weather(void) {
  // Regression for the :00/:30 feedback loop: every weather reply ends in
  // update_time(); when the fetch trigger lived there, each reply re-armed
  // the request until the minute flipped — 20-60 fetches, twice an hour.
  int before = mock_outbox_sends;
  update_time();
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);
}

void test_update_time_should_refresh_the_hi_lo_phase_hour(void) {
  // The HI/LO rollover reads this global; update_time is its only writer.
  time_t now = time(NULL);
  int expected = localtime(&now)->tm_hour;
  s_wall_hour = (expected + 7) % 24;
  update_time();
  TEST_ASSERT_EQUAL_INT(expected, s_wall_hour);
}

void test_update_time_should_reformat_the_date_when_settings_change(void) {
  s_settings_dow_position = DOW_BEFORE;
  update_time();  // primes the format cache for (today, these settings)
  char with_dow[64];
  strcpy(with_dow, s_date_display);

  s_settings_dow_position = DOW_HIDDEN;
  update_time();
  TEST_ASSERT_TRUE(strcmp(with_dow, s_date_display) != 0);

  s_settings_dow_position = DOW_BEFORE;
  update_time();
  TEST_ASSERT_EQUAL_STRING(with_dow, s_date_display);
}

void test_update_time_should_keep_date_output_when_nothing_changes(void) {
  update_time();
  char once[64];
  char once_short[16];
  strcpy(once, s_date_display);
  strcpy(once_short, s_short_date_display);
  update_time();
  TEST_ASSERT_EQUAL_STRING(once, s_date_display);
  TEST_ASSERT_EQUAL_STRING(once_short, s_short_date_display);
}

void test_tick_handler_should_refresh_quiet_time_state(void) {
  struct tm t = {0};

  mock_quiet_time_active = true;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_TRUE(s_quiet_time_active);

  mock_quiet_time_active = false;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_FALSE(s_quiet_time_active);
}

void test_tick_handler_should_request_weather_on_the_half_hour_edge(void) {
  struct tm t = {0};

  t.tm_min = 14;
  int before = mock_outbox_sends;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);

  t.tm_min = 30;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(before + 1, mock_outbox_sends);

  t.tm_min = 0;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(before + 2, mock_outbox_sends);
}

void test_tick_handler_should_skip_weather_with_no_weather_slots(void) {
  ComplicationDataSource saved[NUM_SLOTS];
  save_slots(saved);
  ComplicationDataSource weather_free[NUM_SLOTS] = {DATA_SOURCE_DATE,  DATA_SOURCE_BLUETOOTH,
                                                    DATA_SOURCE_STEPS, DATA_SOURCE_HEART_RATE,
                                                    DATA_SOURCE_BEATS, DATA_SOURCE_FULL_DATE};
  restore_slots(weather_free);

  struct tm t = {0};
  t.tm_min = 30;
  int before = mock_outbox_sends;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);

  s_complication_slots[0].source = DATA_SOURCE_WEATHER;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(before + 1, mock_outbox_sends);

  restore_slots(saved);
}

void test_wind_slot_should_join_the_weather_fetch_gate(void) {
  // A wind-only layout must fetch on the :00/:30 edge like any other
  // weather reading, or the arrow never leaves "--".
  ComplicationDataSource saved[NUM_SLOTS];
  save_slots(saved);
  ComplicationDataSource weather_free[NUM_SLOTS] = {DATA_SOURCE_DATE,  DATA_SOURCE_BLUETOOTH,
                                                    DATA_SOURCE_STEPS, DATA_SOURCE_HEART_RATE,
                                                    DATA_SOURCE_BEATS, DATA_SOURCE_FULL_DATE};
  restore_slots(weather_free);

  struct tm t = {0};
  t.tm_min = 30;
  int before = mock_outbox_sends;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);

  s_complication_slots[2].source = DATA_SOURCE_WIND;  // Bottom Left
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(before + 1, mock_outbox_sends);

  restore_slots(saved);
}

void test_inbox_should_fetch_when_a_weather_slot_first_appears(void) {
  ComplicationDataSource saved[NUM_SLOTS];
  save_slots(saved);
  ComplicationDataSource weather_free[NUM_SLOTS] = {DATA_SOURCE_DATE,  DATA_SOURCE_BLUETOOTH,
                                                    DATA_SOURCE_STEPS, DATA_SOURCE_HEART_RATE,
                                                    DATA_SOURCE_BEATS, DATA_SOURCE_FULL_DATE};
  restore_slots(weather_free);

  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SLOT_1, "16");  // DATA_SOURCE_AQI
  int before = mock_outbox_sends;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(before + 1, mock_outbox_sends);

  // Already showing weather: the same push again must not refetch, and a
  // weather reply (no SLOT_* keys) must not either — that is the :00/:30 loop
  // reappearing by another route.
  before = mock_outbox_sends;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);

  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_cstring(MESSAGE_KEY_WEATHER_COND, "SUN");
  before = mock_outbox_sends;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);

  restore_slots(saved);
}

void test_inbox_should_fetch_when_a_slot_changes_with_weather_already_shown(void) {
  // Defaults show WEATHER in slot 0: weather is already needed, so the
  // first-appears trigger can't fire — an actual assignment change must.
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SLOT_2, "28");  // sleep -> PCP
  int before = mock_outbox_sends;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(before + 1, mock_outbox_sends);

  // Re-pushing an unchanged assignment stays silent
  before = mock_outbox_sends;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);
}

void test_dropped_weather_request_should_retry_a_bounded_number_of_times(void) {
  s_weather_request_retries = 0;
  int before = mock_outbox_sends;

  // Each failure schedules one retry; the mock timer never fires, so drive it.
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);
  weather_retry_callback(NULL);
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);
  weather_retry_callback(NULL);
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);  // exhausted: no third

  TEST_ASSERT_EQUAL_INT(WEATHER_REQUEST_MAX_RETRIES, s_weather_request_retries);
  TEST_ASSERT_EQUAL_INT(before + WEATHER_REQUEST_MAX_RETRIES, mock_outbox_sends);

  outbox_sent_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(0, s_weather_request_retries);
}

void test_weather_cache_should_skip_rewrite_when_payload_is_unchanged(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_cstring(MESSAGE_KEY_WEATHER_COND, "SUN");
  inbox_received_callback(NULL, NULL);

  int writes = mock_persist_write_count;
  inbox_received_callback(NULL, NULL);  // identical payload
  // Only the timestamp is rewritten.
  TEST_ASSERT_EQUAL_INT(writes + 1, mock_persist_write_count);
}

void test_settings_message_should_not_rewrite_unchanged_keys(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_THEME, "2");
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(2, persist_read_int(PERSIST_KEY_SETTINGS_THEME));

  int writes = mock_persist_write_count;
  inbox_received_callback(NULL, NULL);  // same setting again
  TEST_ASSERT_EQUAL_INT(writes, mock_persist_write_count);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_render_gate_should_go_silent_when_nothing_changes);
  RUN_TEST(test_render_gate_should_ignore_changes_nobody_displays);
  RUN_TEST(test_render_gate_should_pass_displayed_changes_through);
  RUN_TEST(test_render_gate_should_notice_bar_slot_changes);
  RUN_TEST(test_render_gate_should_reapply_colors_on_theme_change);
  RUN_TEST(test_quick_view_did_change_should_gate_and_restore);
  RUN_TEST(test_canvas_should_skip_the_bottom_row_while_quick_view_is_up);
  RUN_TEST(test_quick_view_should_hide_and_restore_bottom_row_text_layers);
  RUN_TEST(test_canvas_procs_should_never_word_wrap);
  RUN_TEST(test_hum_pcp_window_should_paint_its_halves);
  RUN_TEST(test_bt_qt_wide_should_draw_both_checkboxes);
  RUN_TEST(test_battery_bar_should_paint_its_fill_as_one_rect);
  RUN_TEST(test_steps_bar_should_fill_with_the_plain_text_color);
  RUN_TEST(test_battery_bar_should_fill_with_the_status_color);
  RUN_TEST(test_aqi_chip_should_band_only_on_an_attention_reading);
  RUN_TEST(test_battery_complications_should_wear_green_while_charging);
  RUN_TEST(test_weather_chip_should_hotkey_the_condition_and_the_unit);
  RUN_TEST(test_sleep_chip_should_hint_only_the_trailing_unit);
  RUN_TEST(test_steps_chip_should_hint_the_k);
  RUN_TEST(test_battery_chip_should_band_without_hinting_the_percent);
  RUN_TEST(test_humidity_chip_should_stay_plain);
  RUN_TEST(test_active_chip_should_hint_minutes);
  RUN_TEST(test_temp_chip_should_color_shift_and_hint_the_unit);
  RUN_TEST(test_high_low_chip_should_hint_the_trailing_unit);
  RUN_TEST(test_wind_chip_should_hint_the_unit_until_gale);
  RUN_TEST(test_weather_strip_should_hint_quiet_units);
  RUN_TEST(test_weather_strip_should_draw_the_condition_in_mark);
  RUN_TEST(test_heart_rate_chip_should_trail_the_heart);
  RUN_TEST(test_pcp_chip_should_band_on_attention_probability);
  RUN_TEST(test_pcp_chip_should_band_by_wmo_intensity_and_keep_accent_when_calm);
  RUN_TEST(test_battery_callback_should_coalesce_unchanged_levels);
  RUN_TEST(test_to_upper_str_should_convert_lowercase_to_uppercase);
  RUN_TEST(test_tuple_get_int_should_parse_strings_and_ints);
  RUN_TEST(test_get_source_label_should_return_correct_labels);
  RUN_TEST(test_registry_rows_should_be_unique_and_resolve);
  RUN_TEST(test_registry_should_pin_the_weather_backed_set);
  RUN_TEST(test_get_source_data_should_format_battery);
  RUN_TEST(test_get_source_data_should_format_steps);
  RUN_TEST(test_get_source_data_should_format_weather);

  RUN_TEST(test_get_source_data_should_format_sleep);
  RUN_TEST(test_get_source_data_should_format_weather_temp_and_cond);
  RUN_TEST(test_get_source_data_should_format_heart_rate);
  RUN_TEST(test_get_source_data_should_format_date_and_day);
  RUN_TEST(test_progress_bar_sources_should_reuse_their_plain_counterparts);
  RUN_TEST(test_battery_band_and_color_should_agree_at_every_level);
  RUN_TEST(test_centre_slot_should_be_the_sixth_and_default_to_the_date);
  RUN_TEST(test_get_source_data_should_format_bluetooth);
  RUN_TEST(test_get_source_data_should_format_bt_qt);
  RUN_TEST(test_get_source_data_should_format_hum_pcp);
  RUN_TEST(test_get_source_data_should_format_quiet_time);
  RUN_TEST(test_get_source_data_should_format_wind);
  RUN_TEST(test_wind_color_should_follow_the_beaufort_rungs);
  RUN_TEST(test_format_wind_narrow_should_drop_the_unit);
  RUN_TEST(test_wind_direction_arrow_should_point_where_the_wind_blows);
  RUN_TEST(test_wind_direction_arrow_should_take_the_clockwise_sector_on_boundaries);
  RUN_TEST(test_wind_direction_arrow_should_normalize_or_reject_out_of_range_bearings);
  RUN_TEST(test_bt_qt_window_should_split_captions_only_at_top_width);
  RUN_TEST(test_get_source_data_should_format_active_minutes);
  RUN_TEST(test_get_source_data_should_format_aqi_and_uv);
  RUN_TEST(test_get_source_data_should_format_humidity);
  RUN_TEST(test_get_source_data_should_format_weather_full);
  RUN_TEST(test_full_weather_captions_should_align_with_the_strip);
  RUN_TEST(test_full_weather_chips_should_fill_only_on_a_status_color);
  RUN_TEST(test_strip_temp_formatter_should_always_carry_the_unit_letter);
  RUN_TEST(test_get_source_data_should_format_pcp);
  RUN_TEST(test_get_source_data_should_format_high_low);
  RUN_TEST(test_high_low_cells_should_roll_when_their_extreme_hour_ends);
  RUN_TEST(test_high_low_cells_should_stay_chronological_on_inversion_days);
  RUN_TEST(test_high_low_layout_should_fall_back_to_lo_first_when_hours_unknown);
  RUN_TEST(test_high_low_stub_order_should_follow_the_layout);
  RUN_TEST(test_compute_beats_should_map_the_bmt_day_to_0_999);
  RUN_TEST(test_get_source_data_should_format_beats);
  RUN_TEST(test_get_source_color_should_return_appropriate_colors);
  RUN_TEST(test_determine_theme_should_handle_all_configurations);
  RUN_TEST(test_themes_should_keep_text_readable_on_their_ground);
  RUN_TEST(test_status_ink_should_clear_every_fill_it_is_drawn_on);
  RUN_TEST(test_every_theme_should_only_use_dos_palette_colors);
  RUN_TEST(test_format_date_string_should_render_every_body);
  RUN_TEST(test_every_date_combination_should_fit_its_window);
  RUN_TEST(test_weekday_position_should_be_independent_of_the_body);
  RUN_TEST(test_short_date_should_stay_short_whatever_the_date_format);
  RUN_TEST(test_weather_cache_should_round_trip_when_fresh);
  RUN_TEST(test_weather_cache_should_leave_extreme_timing_at_sentinel_in_old_caches);
  RUN_TEST(test_weather_cache_should_reject_missing_or_stale_data);
  RUN_TEST(test_weather_cache_should_keep_values_at_edge_of_window);
  RUN_TEST(test_settings_should_round_trip_through_persistence);
  RUN_TEST(test_settings_persistence_is_decoupled_from_message_key_ids);
  RUN_TEST(test_update_health_info_should_read_heart_rate);
  RUN_TEST(test_update_health_info_should_do_nothing_with_no_health_slots);
  RUN_TEST(test_update_health_info_should_read_only_displayed_metrics);
  RUN_TEST(test_health_handler_should_throttle_movement_updates);
  RUN_TEST(test_health_handler_should_refresh_again_after_the_throttle_window);
  RUN_TEST(test_health_handler_should_not_throttle_heart_rate_updates);
  RUN_TEST(test_health_handler_should_not_throttle_significant_updates);
  RUN_TEST(test_undisplayed_health_metrics_should_read_as_no_data);
  RUN_TEST(test_handle_bluetooth_should_vibrate_only_on_disconnect_transition);
  RUN_TEST(test_handle_bluetooth_should_stay_silent_on_drops_when_the_buzz_is_disabled);
  RUN_TEST(test_inbox_should_parse_weather_payload_and_persist);
  RUN_TEST(test_inbox_should_parse_and_persist_tomorrow_low);
  RUN_TEST(test_inbox_should_parse_and_persist_wind_direction);
  RUN_TEST(test_inbox_should_parse_and_persist_wind_speed);
  RUN_TEST(test_inbox_without_wind_should_leave_the_sentinel);
  RUN_TEST(test_inbox_should_parse_and_persist_extreme_rollover_keys);
  RUN_TEST(test_inbox_without_tomorrow_low_should_leave_the_sentinel);
  RUN_TEST(test_inbox_without_extreme_timing_should_leave_the_sentinels);
  RUN_TEST(test_inbox_settings_only_message_should_not_stamp_weather_cache);
  RUN_TEST(test_inbox_should_parse_slot_assignments);
  RUN_TEST(test_inbox_should_parse_the_newer_settings_and_centre_slot);
  RUN_TEST(test_inbox_should_parse_and_persist_disconnect_vibe_setting);
  RUN_TEST(test_inbox_units_change_should_trigger_weather_refetch);
  RUN_TEST(test_update_time_should_never_request_weather);
  RUN_TEST(test_update_time_should_refresh_the_hi_lo_phase_hour);
  RUN_TEST(test_update_time_should_reformat_the_date_when_settings_change);
  RUN_TEST(test_update_time_should_keep_date_output_when_nothing_changes);
  RUN_TEST(test_tick_handler_should_refresh_quiet_time_state);
  RUN_TEST(test_tick_handler_should_request_weather_on_the_half_hour_edge);
  RUN_TEST(test_tick_handler_should_skip_weather_with_no_weather_slots);
  RUN_TEST(test_wind_slot_should_join_the_weather_fetch_gate);
  RUN_TEST(test_inbox_should_fetch_when_a_weather_slot_first_appears);
  RUN_TEST(test_inbox_should_fetch_when_a_slot_changes_with_weather_already_shown);
  RUN_TEST(test_dropped_weather_request_should_retry_a_bounded_number_of_times);
  RUN_TEST(test_weather_cache_should_skip_rewrite_when_payload_is_unchanged);
  RUN_TEST(test_settings_message_should_not_rewrite_unchanged_keys);
  return UNITY_END();
}
