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
  s_battery_level = 100;
  s_step_count = -1;
  s_sleep_seconds = -1;
  s_heart_rate = 0;
  s_weather_temp = -999;
  strcpy(s_weather_cond, "--");
  s_connected = true;
  s_quick_view_active = false;
  mock_unobstructed_bounds = GRect(0, 0, 200, 228);
  mock_set_hidden_count = 0;
  // Isolate the timestamp-based health throttle: zero both the mock clock
  // offset and the last-refresh stamp so no test inherits a window.
  mock_time_offset = 0;
  s_last_throttled_health_refresh = 0;
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
  int texts = mock_set_text_count;

  s_step_count = 4321;  // bottom-left slot shows STEPS by default
  request_ui_redraw();
  TEST_ASSERT_TRUE(mock_mark_dirty_count > marks);
  TEST_ASSERT_TRUE(mock_set_text_count > texts);
  s_step_count = -1;
}

void test_render_gate_should_notice_bar_slot_changes(void) {
  // Regression: the bar sources have no get_source_data case, so a snapshot
  // keyed on slot->source records empty text forever and the bar freezes
  // between minute ticks. snapshot_source() maps them to their plain source.
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
  // The bottom row hides under the overlay; the top text slot stays visible.
  // Slots 0 (WEATHER) and 5 (FULL_DATE) paint onto the canvas instead, so
  // their text layers are hidden either way.
  TEST_ASSERT_TRUE(mock_set_hidden_states[2]);
  TEST_ASSERT_TRUE(mock_set_hidden_states[3]);
  TEST_ASSERT_TRUE(mock_set_hidden_states[4]);
  TEST_ASSERT_FALSE(mock_set_hidden_states[1]);
  TEST_ASSERT_TRUE(mock_set_hidden_states[0]);
  TEST_ASSERT_TRUE(mock_set_hidden_states[5]);

  mock_set_hidden_count = 0;
  mock_unobstructed_bounds = GRect(0, 0, 200, 228);
  quick_view_did_change(NULL);

  TEST_ASSERT_EQUAL_INT(NUM_SLOTS, mock_set_hidden_count);
  TEST_ASSERT_FALSE(mock_set_hidden_states[2]);
  TEST_ASSERT_FALSE(mock_set_hidden_states[3]);
  TEST_ASSERT_FALSE(mock_set_hidden_states[4]);
}

void test_canvas_procs_should_never_word_wrap(void) {
  test_apply_theme();
  s_complication_slots[3].source = DATA_SOURCE_BATTERY_BAR;  // exercise the shade runs too
  mock_wordwrap_calls = 0;
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(0, mock_wordwrap_calls);
  s_complication_slots[3].source = DATA_SOURCE_HEART_RATE;
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

void test_battery_bar_should_fill_with_the_plain_text_color(void) {
  // The bar fill matches the steps bar: plain text color at any level. The
  // green/yellow/red thresholds belong to the plain battery slot only.
  s_complication_slots[5].source = DATA_SOURCE_BATTERY_BAR;
  s_battery_level = 60;  // mid-level: get_source_color would give green
  mock_fill_rect_reset();

  canvas_update_proc(NULL, NULL);

  bool saw_bar_fill = false;
  for (int i = 0; i < mock_fill_rect_count; i++) {
    TEST_ASSERT_TRUE(mock_fill_rect_colors[i] != s_active_theme->status_green);
    TEST_ASSERT_TRUE(mock_fill_rect_colors[i] != s_active_theme->status_yellow);
    TEST_ASSERT_TRUE(mock_fill_rect_colors[i] != s_active_theme->status_red);
    if (mock_fill_rects[i].size.w > 0 && mock_fill_rect_colors[i] == s_active_theme->text_primary) {
      saw_bar_fill = true;
    }
  }
  TEST_ASSERT_TRUE(saw_bar_fill);

  s_complication_slots[5].source = DATA_SOURCE_FULL_DATE;
  s_battery_level = 100;
}

void test_battery_callback_should_coalesce_unchanged_levels(void) {
  main_window_load(NULL);
  memset(&s_shown_ui, 0, sizeof(s_shown_ui));
  battery_callback((BatteryChargeState){.charge_percent = 100});
  int marks = mock_mark_dirty_count;
  battery_callback((BatteryChargeState){.charge_percent = 100});
  TEST_ASSERT_EQUAL_INT(marks, mock_mark_dirty_count);
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
  TEST_ASSERT_EQUAL_STRING("AQI/UV", get_source_label(DATA_SOURCE_AQI_UV));
  TEST_ASSERT_EQUAL_STRING("HUM", get_source_label(DATA_SOURCE_HUMIDITY));
  TEST_ASSERT_EQUAL_STRING("PCP", get_source_label(DATA_SOURCE_WEATHER_PCP));
  TEST_ASSERT_EQUAL_STRING("HI/LO", get_source_label(DATA_SOURCE_TEMP_HIGH_LOW));
  // Top-slot titles cap at 9 characters (93px window); RISE/SET is the widest
  TEST_ASSERT_EQUAL_STRING("RISE/SET", get_source_label(DATA_SOURCE_SUN_TIMES));
  TEST_ASSERT_TRUE(strlen(get_source_label(DATA_SOURCE_SUN_TIMES)) <= 9);
  TEST_ASSERT_EQUAL_STRING("BEAT", get_source_label(DATA_SOURCE_BEATS));
  // Both date sources title the same window; one shows the day, one the date.
  TEST_ASSERT_EQUAL_STRING("DATE", get_source_label(DATA_SOURCE_DATE));
  TEST_ASSERT_EQUAL_STRING("DATE", get_source_label(DATA_SOURCE_SHORT_DATE));
  TEST_ASSERT_EQUAL_STRING("", get_source_label(DATA_SOURCE_EMPTY));
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
  TEST_ASSERT_EQUAL_STRING("-- / --", buf);

  // Imperial
  s_settings_units = 0;
  s_weather_temp = 72;
  strcpy(s_weather_cond, "SUN");
  get_source_data(DATA_SOURCE_WEATHER, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("SUN / 72F", buf);

  // Metric
  s_settings_units = 1;
  s_weather_temp = 22;
  strcpy(s_weather_cond, "CLD");
  get_source_data(DATA_SOURCE_WEATHER, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("CLD / 22C", buf);
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

  // Temp Metric
  s_settings_units = 1;
  s_weather_temp = 20;
  get_source_data(DATA_SOURCE_WEATHER_TEMP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("20C", buf);

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

  s_step_count = -1;
  get_source_data(DATA_SOURCE_STEPS, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_INT(0, percent);

  s_battery_level = 45;
  get_source_data(DATA_SOURCE_BATTERY, buf, sizeof(buf), &percent);
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
  // The progress bar colors its fill from get_source_color while the bottom
  // complication decides whether to draw a band. Those used to be two separate
  // threshold sets that disagreed between 21% and 24% — one yellow, one red.
  s_active_theme = &s_theme_panel;

  for (int level = 0; level <= 100; level++) {
    s_battery_level = level;
    GColor color = get_source_color(DATA_SOURCE_BATTERY);
    bool banded = level <= BATTERY_LOW_PCT;

    // A band appears exactly when the charge is not healthy, and never in the
    // healthy color — that equivalence is the whole invariant.
    TEST_ASSERT_EQUAL_INT(banded, color != s_theme_panel.status_green);

    if (level <= BATTERY_CRIT_PCT) {
      TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, color);
    } else if (level <= BATTERY_LOW_PCT) {
      TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, color);
    }
  }

  // The boundary that was wrong: 21% is yellow, not red.
  s_battery_level = 21;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_BATTERY));
  s_battery_level = 20;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_BATTERY));
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

  // A checkbox, so the glyph carries the state on its own — no color needed.
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
static const WatchTheme* all_themes[] = {&s_theme_panel, &s_theme_shadow, &s_theme_dialog};
#define NUM_THEMES (sizeof(all_themes) / sizeof(all_themes[0]))

void test_determine_theme_should_handle_all_configurations(void) {
  // Pinned choices ignore the clock entirely.
  for (int hour = 0; hour < 24; hour++) {
    TEST_ASSERT_EQUAL_PTR(&s_theme_dialog, determine_theme(1, hour));
    TEST_ASSERT_EQUAL_PTR(&s_theme_panel, determine_theme(2, hour));
    TEST_ASSERT_EQUAL_PTR(&s_theme_shadow, determine_theme(3, hour));
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

    // Titles must stay distinguishable from the frame they sit in, or the
    // window label disappears into its own border.
    TEST_ASSERT_NOT_EQUAL(t->frame, t->text_secondary);

    // Status values are drawn as text on the ground too — this is what forces
    // the light theme to use the low-intensity variants.
    TEST_ASSERT_NOT_EQUAL(t->center_bg, t->status_green);
    TEST_ASSERT_NOT_EQUAL(t->center_bg, t->status_yellow);
    TEST_ASSERT_NOT_EQUAL(t->center_bg, t->status_red);
  }
}

void test_status_ink_should_clear_every_fill_it_is_drawn_on(void) {
  // The battery chip fills with status_red/status_yellow and writes status_ink
  // over it. If the ink matches its own fill the reading vanishes — which is
  // exactly what happened when the chip used text_primary on a light ground.
  for (unsigned i = 0; i < NUM_THEMES; i++) {
    TEST_ASSERT_NOT_EQUAL(all_themes[i]->status_ink, all_themes[i]->status_red);
    TEST_ASSERT_NOT_EQUAL(all_themes[i]->status_ink, all_themes[i]->status_yellow);
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
  TEST_ASSERT_EQUAL_STRING("-- / --", buf);

  s_weather_aqi = 42;
  s_weather_uv = 5;
  get_source_data(DATA_SOURCE_AQI_UV, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("42 / 5", buf);
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

  // No data at all — five sentinels, and no fake progress
  s_weather_temp = -999;
  strcpy(s_weather_cond, "--");
  s_weather_humidity = -1;
  s_weather_aqi = -1;
  s_weather_uv = -1;
  get_source_data(DATA_SOURCE_WEATHER_FULL, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("-- -- -- -- --", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);

  // Typical imperial day with the AQI fetch failed: it keeps its own sentinel
  s_settings_units = 0;
  s_weather_temp = 72;
  strcpy(s_weather_cond, "SUN");
  s_weather_humidity = 45;
  s_weather_aqi = -1;
  s_weather_uv = 3;
  get_source_data(DATA_SOURCE_WEATHER_FULL, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("SUN 72F 45% -- 3", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);  // composite source, never a progress bar

  // Widest realistic mix (metric): exactly the strip's cell budget
  s_settings_units = 1;
  s_weather_temp = -22;
  strcpy(s_weather_cond, "TSTM");
  s_weather_humidity = 100;
  s_weather_aqi = 150;
  s_weather_uv = 11;
  get_source_data(DATA_SOURCE_WEATHER_FULL, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("TSTM -22C 100% 150 11", buf);
  TEST_ASSERT_TRUE(strlen(buf) <= FULL_WEATHER_STRIP_CELLS);
}

void test_full_weather_captions_should_align_with_the_strip(void) {
  // Captions live in the (centred) title gap, chips in the (centred) strip;
  // both are FULL_WEATHER_STRIP_CELLS wide, so tokens land on their chips.
  // The field table, the constant and the label must all agree.
  int strip_cells = 0;
  for (size_t i = 0; i < FULL_WEATHER_NUM_FIELDS; i++) {
    strip_cells += s_full_weather_fields[i].cells;
  }
  strip_cells += FULL_WEATHER_NUM_FIELDS - 1;  // one-cell gaps between chips
  TEST_ASSERT_EQUAL_INT(FULL_WEATHER_STRIP_CELLS, strip_cells);
  TEST_ASSERT_EQUAL_INT(FULL_WEATHER_STRIP_CELLS,
                        (int)strlen(get_source_label(DATA_SOURCE_WEATHER_FULL)));
  // …and inside the title gap a 21-cell title still leaves border stubs.
  TEST_ASSERT_TRUE(FULL_WEATHER_STRIP_CELLS * VGA16_CHAR_W + 4 <= LAYOUT_W - 10);
}

void test_get_source_data_should_format_pcp(void) {
  char buf[16];
  int percent = -1;

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
}

void test_get_source_data_should_format_sun_times(void) {
  char buf[24];

  strcpy(s_sunrise_time, "--:--");
  strcpy(s_sunset_time, "--:--");
  get_source_data(DATA_SOURCE_SUN_TIMES, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("--:--/--:--", buf);

  strcpy(s_sunrise_time, "05:00");
  strcpy(s_sunset_time, "21:52");
  get_source_data(DATA_SOURCE_SUN_TIMES, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("05:00/21:52", buf);
}

void test_get_source_data_should_format_high_low(void) {
  char buf[24];

  // Either side missing must not leak a half-number
  s_temp_high = -999;
  s_temp_low = 61;
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("-- / --", buf);

  // …and the other half too
  s_temp_high = 82;
  s_temp_low = -999;
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("-- / --", buf);

  s_settings_units = 0;
  s_temp_high = 82;
  s_temp_low = 61;
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("82/61F", buf);

  s_settings_units = 1;
  s_temp_high = 28;
  s_temp_low = 4;
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("28/4C", buf);

  // Top-slot values cap at 11 cells even at winter extremes
  s_temp_high = 3;
  s_temp_low = -25;
  get_source_data(DATA_SOURCE_TEMP_HIGH_LOW, buf, sizeof(buf), NULL);
  TEST_ASSERT_TRUE(strlen(buf) <= 11);
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

  // AQI color severity levels (0-50 green, 51-100 yellow, >100 red)
  s_weather_aqi = -1;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_AQI));

  s_weather_aqi = 34;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_green, get_source_color(DATA_SOURCE_AQI));

  s_weather_aqi = 65;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_AQI));

  s_weather_aqi = 150;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_AQI));

  // UV color severity levels (0-2 green, 3-5 yellow, >=6 red)
  s_weather_uv = -1;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_UV));

  s_weather_uv = 1;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_green, get_source_color(DATA_SOURCE_UV));

  s_weather_uv = 4;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_UV));

  s_weather_uv = 8;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_UV));

  // AQI / UV Combined severity
  s_weather_aqi = 34;
  s_weather_uv = 1;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_green, get_source_color(DATA_SOURCE_AQI_UV));

  s_weather_aqi = 65;  // yellow
  s_weather_uv = 1;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_AQI_UV));

  s_weather_aqi = 34;
  s_weather_uv = 8;  // red
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_AQI_UV));

  // Humidity comfort bands (<30 dry blue, 30-60 green, 61-70 yellow, >70 red)
  s_weather_humidity = -1;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_HUMIDITY));

  s_weather_humidity = 29;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.accent_cold, get_source_color(DATA_SOURCE_HUMIDITY));

  s_weather_humidity = 30;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_green, get_source_color(DATA_SOURCE_HUMIDITY));

  s_weather_humidity = 60;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_green, get_source_color(DATA_SOURCE_HUMIDITY));

  s_weather_humidity = 61;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_HUMIDITY));

  s_weather_humidity = 70;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_HUMIDITY));

  s_weather_humidity = 71;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_HUMIDITY));

  // Precipitation probability bands (<=30 green, 31-60 yellow, >60 red)
  s_weather_pcp = -1;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_WEATHER_PCP));

  s_weather_pcp = 30;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_green, get_source_color(DATA_SOURCE_WEATHER_PCP));

  s_weather_pcp = 31;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_WEATHER_PCP));

  s_weather_pcp = 60;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_WEATHER_PCP));

  s_weather_pcp = 61;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_WEATHER_PCP));

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

  // Battery color thresholds (>50 green, >20 yellow, <=20 red)
  s_battery_level = 100;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_green, get_source_color(DATA_SOURCE_BATTERY));

  s_battery_level = 51;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_green, get_source_color(DATA_SOURCE_BATTERY));

  s_battery_level = 50;  // boundary: yellow
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_BATTERY));

  s_battery_level = 21;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_BATTERY));

  s_battery_level = 20;  // boundary: red
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_BATTERY));

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
  strcpy(s_sunrise_time, "05:00");
  strcpy(s_sunset_time, "21:52");
  s_temp_high = 82;
  s_temp_low = 61;
  save_weather_cache();

  // Simulate a relaunch: globals reset to sentinels
  s_weather_temp = -999;
  strcpy(s_weather_cond, "--");
  s_weather_aqi = -1;
  s_weather_uv = -1;
  s_weather_humidity = -1;
  s_weather_pcp = -1;
  strcpy(s_sunrise_time, "--:--");
  strcpy(s_sunset_time, "--:--");
  s_temp_high = -999;
  s_temp_low = -999;

  TEST_ASSERT_TRUE(load_weather_cache());
  TEST_ASSERT_EQUAL_INT(72, s_weather_temp);
  TEST_ASSERT_EQUAL_STRING("SUN", s_weather_cond);
  TEST_ASSERT_EQUAL_INT(42, s_weather_aqi);
  TEST_ASSERT_EQUAL_INT(5, s_weather_uv);
  TEST_ASSERT_EQUAL_INT(55, s_weather_humidity);
  TEST_ASSERT_EQUAL_INT(35, s_weather_pcp);
  TEST_ASSERT_EQUAL_STRING("05:00", s_sunrise_time);
  TEST_ASSERT_EQUAL_STRING("21:52", s_sunset_time);
  TEST_ASSERT_EQUAL_INT(82, s_temp_high);
  TEST_ASSERT_EQUAL_INT(61, s_temp_low);
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

void test_inbox_should_parse_weather_payload_and_persist(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_cstring(MESSAGE_KEY_WEATHER_COND, "SUN");
  mock_dict_add_int(MESSAGE_KEY_WEATHER_AQI, 42);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_UV, 7);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_HUMIDITY, 55);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_PCP, 35);
  mock_dict_add_cstring(MESSAGE_KEY_WEATHER_SUNRISE, "05:00");
  mock_dict_add_cstring(MESSAGE_KEY_WEATHER_SUNSET, "21:52");
  mock_dict_add_int(MESSAGE_KEY_WEATHER_HIGH, 82);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_LOW, 61);

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(72, s_weather_temp);
  TEST_ASSERT_EQUAL_STRING("SUN", s_weather_cond);
  TEST_ASSERT_EQUAL_INT(42, s_weather_aqi);
  TEST_ASSERT_EQUAL_INT(7, s_weather_uv);
  TEST_ASSERT_EQUAL_INT(55, s_weather_humidity);
  TEST_ASSERT_EQUAL_INT(35, s_weather_pcp);
  TEST_ASSERT_EQUAL_STRING("05:00", s_sunrise_time);
  TEST_ASSERT_EQUAL_STRING("21:52", s_sunset_time);
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
  RUN_TEST(test_battery_bar_should_paint_its_fill_as_one_rect);
  RUN_TEST(test_steps_bar_should_fill_with_the_plain_text_color);
  RUN_TEST(test_battery_bar_should_fill_with_the_plain_text_color);
  RUN_TEST(test_battery_callback_should_coalesce_unchanged_levels);
  RUN_TEST(test_to_upper_str_should_convert_lowercase_to_uppercase);
  RUN_TEST(test_tuple_get_int_should_parse_strings_and_ints);
  RUN_TEST(test_get_source_label_should_return_correct_labels);
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
  RUN_TEST(test_get_source_data_should_format_active_minutes);
  RUN_TEST(test_get_source_data_should_format_aqi_and_uv);
  RUN_TEST(test_get_source_data_should_format_humidity);
  RUN_TEST(test_get_source_data_should_format_weather_full);
  RUN_TEST(test_full_weather_captions_should_align_with_the_strip);
  RUN_TEST(test_get_source_data_should_format_pcp);
  RUN_TEST(test_get_source_data_should_format_sun_times);
  RUN_TEST(test_get_source_data_should_format_high_low);
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
  RUN_TEST(test_inbox_should_parse_weather_payload_and_persist);
  RUN_TEST(test_inbox_settings_only_message_should_not_stamp_weather_cache);
  RUN_TEST(test_inbox_should_parse_slot_assignments);
  RUN_TEST(test_inbox_should_parse_the_newer_settings_and_centre_slot);
  RUN_TEST(test_inbox_units_change_should_trigger_weather_refetch);
  RUN_TEST(test_update_time_should_never_request_weather);
  RUN_TEST(test_update_time_should_reformat_the_date_when_settings_change);
  RUN_TEST(test_update_time_should_keep_date_output_when_nothing_changes);
  RUN_TEST(test_tick_handler_should_request_weather_on_the_half_hour_edge);
  RUN_TEST(test_tick_handler_should_skip_weather_with_no_weather_slots);
  RUN_TEST(test_inbox_should_fetch_when_a_weather_slot_first_appears);
  RUN_TEST(test_inbox_should_fetch_when_a_slot_changes_with_weather_already_shown);
  RUN_TEST(test_dropped_weather_request_should_retry_a_bounded_number_of_times);
  RUN_TEST(test_weather_cache_should_skip_rewrite_when_payload_is_unchanged);
  RUN_TEST(test_settings_message_should_not_rewrite_unchanged_keys);
  return UNITY_END();
}
