#include "unity/src/unity.h"
#include "pebble.h"

// Include the implementation file directly so we can test its static functions
#include "../src/c/data.c"
#include "../src/c/complication.c"
#include "../src/c/theme.c"
#include "../src/c/status.c"
#include "../src/c/drawing.c"
#include "../src/c/messaging.c"
#include "../src/c/crt.c"
#include "../src/c/chime.c"
#include "../src/c/main.c"

void tearDown(void) {}

// Slot layouts tests stand up against the shipped one; reset_all_state()
// restores boot before every test, so a test sets its layout and owes
// nothing afterwards.
static const ComplicationDataSource kSlotsBoot[NUM_SLOTS] = {
    DATA_SOURCE_WEATHER,    DATA_SOURCE_SLEEP,     DATA_SOURCE_STEPS,
    DATA_SOURCE_HEART_RATE, DATA_SOURCE_BLUETOOTH, DATA_SOURCE_FULL_DATE};
static const ComplicationDataSource kSlotsNoHealth[NUM_SLOTS] = {
    DATA_SOURCE_DATE,       DATA_SOURCE_BLUETOOTH, DATA_SOURCE_BEATS,
    DATA_SOURCE_SHORT_DATE, DATA_SOURCE_AQI_UV,    DATA_SOURCE_FULL_DATE};
static const ComplicationDataSource kSlotsOnlySteps[NUM_SLOTS] = {
    DATA_SOURCE_STEPS,      DATA_SOURCE_BLUETOOTH, DATA_SOURCE_BEATS,
    DATA_SOURCE_SHORT_DATE, DATA_SOURCE_DATE,      DATA_SOURCE_FULL_DATE};
static const ComplicationDataSource kSlotsNoWeather[NUM_SLOTS] = {
    DATA_SOURCE_DATE,       DATA_SOURCE_BLUETOOTH, DATA_SOURCE_STEPS,
    DATA_SOURCE_HEART_RATE, DATA_SOURCE_BEATS,     DATA_SOURCE_FULL_DATE};

static const ComplicationDataSource kSlotsAllHealth[NUM_SLOTS] = {
    DATA_SOURCE_STEPS,          DATA_SOURCE_SLEEP,     DATA_SOURCE_HEART_RATE,
    DATA_SOURCE_ACTIVE_MINUTES, DATA_SOURCE_BLUETOOTH, DATA_SOURCE_FULL_DATE};

static void set_slots(const ComplicationDataSource* sources) {
  for (int i = 0; i < NUM_SLOTS; i++) s_complication_slots[i].source = sources[i];
}

// Every global the single-TU build can see, back to its power-on value. New
// globals are reset here: added to data.c/main.c/drawing.c means added below,
// and the weather walk keeps itself in sync with messaging.c's wire table.
static void reset_all_state(void) {
  mock_reset();

  s_settings_theme = 2;  // Norton: apply_theme() stays palette-deterministic
  s_settings_night_theme = 0;
  s_settings_units = 0;
  s_settings_date_format = 0;
  s_settings_short_date_format = 0;
  s_settings_dow_position = 0;
  s_settings_weather_window = 12;
  s_settings_crt_aberration = 0;
  s_settings_crt_curvature = 0;
  s_settings_crt_degauss = 0;
  s_settings_degauss_volume = 10;
  s_settings_chime_volume = 0;

  s_battery_level = 100;
  s_battery_charging = false;
  s_connected = true;
  s_quiet_time_active = false;
  s_step_count = -1;
  s_sleep_seconds = -1;
  s_heart_rate = 0;
  s_active_minutes = -1;

  for (unsigned i = 0; i < sizeof(s_weather_fields) / sizeof(s_weather_fields[0]); i++) {
    *s_weather_fields[i].target = s_weather_fields[i].sentinel;
  }

  s_wall_hour = 8;  // morning: a neutral phase for tests that don't care
  s_date_day = 1;
  s_beats = 0;
  s_longitude = LONGITUDE_NONE;
  s_solar_hour_angle = 0;
  s_latitude = LATITUDE_NONE;
  s_solar_elevation = 0;
  s_week_number = 1;
  s_date_display[0] = '\0';
  s_short_date_display[0] = '\0';
  s_quick_view_active = false;
  set_slots(kSlotsBoot);

  // File-scope caches and counters (main.c / drawing.c).
  s_weather_request_retries = 0;
  s_weather_retry_timer = NULL;
  s_last_chimed_hour = -1;
  s_last_movement_refresh = 0;
  s_last_sleep_refresh = 0;
  s_last_reconnect_request = 0;
  s_subscribed = (SubscriptionSet){0};
  s_fmt_yday = -1;
  s_fmt_format = -1;
  s_fmt_dow = -1;
  s_fmt_short = -1;

  s_shown_time[0] = '\0';
  s_scene_cache_valid = false;
  s_scene_cache_borrowed = false;
  free(s_scene_cache);
  s_scene_cache = NULL;
  s_strike_src = NULL;
  s_strike_layers_skipped = false;
  s_flash_phase = CRT_FLASH_IDLE;
  s_flash_timer = NULL;
  s_cover_suppresses_strike = false;
  s_cover_grace_timer = NULL;
  s_button_wake_timer = NULL;
  s_shake_wake = false;
  s_shake_wake_timer = NULL;
  s_strike_pending = false;
  s_app_in_focus = false;
  s_crt_layer = NULL;
  s_canvas_layer = NULL;
  s_main_window = NULL;
  s_time_layer = NULL;
  s_vga_16 = NULL;
  s_vga_64 = NULL;
  reset_ui_snapshot();

  s_active_theme = &s_theme_panel;
}

// The globals' compiled-in boot values, captured before any test resets them.
static int s_boot_weather[sizeof(s_weather_fields) / sizeof(s_weather_fields[0])];
__attribute__((constructor)) static void snapshot_boot_weather(void) {
  for (unsigned i = 0; i < sizeof(s_boot_weather) / sizeof(s_boot_weather[0]); i++) {
    s_boot_weather[i] = *s_weather_fields[i].target;
  }
}

void setUp(void) {
  reset_all_state();
}

// -----------------------------------------------------------------------------
// Tests
// -----------------------------------------------------------------------------

// apply_theme() reads only s_settings_theme; keep the plumbing in one place.
static void test_apply_theme(void) {
  apply_theme();
}

void test_render_gate_should_go_silent_when_nothing_changes(void) {
  main_window_load(NULL);
  mock_mark_dirty_count = 0;
  mock_set_text_count = 0;

  request_ui_redraw();  // cold: cleared snapshot differs, applies once
  int marks = mock_mark_dirty_count;
  int texts = mock_set_text_count;

  request_ui_redraw();
  request_ui_redraw();
  TEST_ASSERT_EQUAL_INT(marks, mock_mark_dirty_count);
  TEST_ASSERT_EQUAL_INT(texts, mock_set_text_count);
}

void test_render_gate_should_ignore_changes_nobody_displays(void) {
  main_window_load(NULL);
  request_ui_redraw();
  int marks = mock_mark_dirty_count;

  s_battery_level = 5;  // the default layout shows no battery slot
  request_ui_redraw();
  TEST_ASSERT_EQUAL_INT(marks, mock_mark_dirty_count);
}

void test_render_gate_should_pass_displayed_changes_through(void) {
  main_window_load(NULL);
  request_ui_redraw();
  int marks = mock_mark_dirty_count;

  s_step_count = 4321;  // bottom-left slot shows STEPS by default
  request_ui_redraw();
  // STEPS paints on the canvas now: the value change marks the canvas layer.
  TEST_ASSERT_TRUE(mock_mark_dirty_count > marks);
}

void test_render_gate_should_notice_bar_slot_changes(void) {
  // Regression: the bar sources used to have no data of their own, so a
  // snapshot keyed on slot->source recorded empty text forever and the bar
  // froze between minute ticks. The registry's .backs field re-reads the
  // plain source.
  main_window_load(NULL);
  s_complication_slots[2].source = DATA_SOURCE_STEPS_BAR;
  s_step_count = 1000;
  request_ui_redraw();
  int marks = mock_mark_dirty_count;

  s_step_count = 6000;  // same "--"/number shape, different fill and reading
  request_ui_redraw();
  TEST_ASSERT_TRUE(mock_mark_dirty_count > marks);
}

void test_render_gate_should_notice_hi_lo_caption_swaps(void) {
  // An equal-value day prints identical HI/LO text under either lead, but the
  // frame's caption stubs swap sides — display state the value string can't
  // carry.
  main_window_load(NULL);
  s_complication_slots[SLOT_IDX_TOP_LEFT].source = DATA_SOURCE_TEMP_HIGH_LOW;
  s_wall_hour = 10;
  s_hi_hour_today = 8;  // both of today's extremes passed
  s_lo_hour_today = 6;
  s_hi_hour_tmrw = 15;  // tomorrow LO precedes HI → LO leads
  s_lo_hour_tmrw = 13;
  s_temp_high = s_temp_low = s_temp_high_tmrw = s_temp_low_tmrw = 8;
  request_ui_redraw();
  int marks = mock_mark_dirty_count;

  s_hi_hour_tmrw = 11;  // now HI leads; the printed values stay "8F 8F"
  request_ui_redraw();
  TEST_ASSERT_TRUE(mock_mark_dirty_count > marks);
}

void test_render_gate_should_reapply_colors_on_theme_change(void) {
  main_window_load(NULL);
  s_settings_theme = 2;  // pin Panel so the Shadow swap below is unconditional
  test_apply_theme();
  request_ui_redraw();
  mock_set_text_color_count = 0;
  int texts = mock_set_text_count;
  int marks = mock_mark_dirty_count;

  s_active_theme = &s_theme_shadow;  // e.g. a settings push swapped the palette
  request_ui_redraw();
  TEST_ASSERT_TRUE(mock_mark_dirty_count > marks);    // canvas recolors
  TEST_ASSERT_EQUAL_INT(texts, mock_set_text_count);  // no string changed
  // The clock row is the lone TextLayer left; refresh_state recolors it on a
  // theme swap even when the time string is unchanged.
  refresh_state();
  TEST_ASSERT_TRUE(mock_set_text_color_count > 0);
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

void test_canvas_should_blit_the_scene_cache_on_repeat_renders(void) {
  // Mock draw fns never paint pixels, so seed the glass with real bytes and
  // let the live draw's snapshot pick them up; the blit must restore them
  // byte-exactly after a corrupted second state.
  main_window_load(NULL);
  for (size_t i = 0; i < sizeof(mock_framebuffer); i++) mock_framebuffer[i] = (uint8_t)(i * 7 + 3);
  canvas_update_proc(NULL, NULL);  // live draw + snapshot
  int runs_after_first = mock_text_run_count;
  int fills_after_first = mock_fill_rect_count;
  TEST_ASSERT_TRUE(runs_after_first > 0);
  TEST_ASSERT_TRUE(fills_after_first > 0);

  memset(mock_framebuffer, 0x5A, sizeof(mock_framebuffer));
  canvas_update_proc(NULL, NULL);  // cache hit: blit, no drawing
  TEST_ASSERT_EQUAL_INT(runs_after_first, mock_text_run_count);
  TEST_ASSERT_EQUAL_INT(fills_after_first, mock_fill_rect_count);
  for (size_t i = 0; i < sizeof(mock_framebuffer); i++) {
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(i * 7 + 3), mock_framebuffer[i]);
  }

  // A content change invalidates THROUGH the gate: request_ui_redraw()'s
  // snapshot trip must fire canvas_invalidate_cache() — assert the wiring,
  // not just the outcome: the flag is false right after the gate runs.
  s_step_count = 4321;  // boot layout shows STEPS; value change trips the snapshot
  request_ui_redraw();
  TEST_ASSERT_FALSE(s_scene_cache_valid);
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_TRUE(mock_fill_rect_count > fills_after_first);
}

void test_canvas_should_skip_the_bottom_row_while_quick_view_is_up(void) {
  test_apply_theme();
  s_quick_view_active = false;
  mock_fill_rect_reset();
  s_scene_cache_valid = false;
  canvas_update_proc(NULL, NULL);
  int full = mock_fill_rect_count;

  s_quick_view_active = true;
  mock_fill_rect_reset();
  s_scene_cache_valid = false;
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

// As delivered to the canvas, both checkboxes of the combined window must be
// drawn — a stray atomic-source read inside the drawer is exactly the bug
// where the QT box comes out empty.
void test_bt_qt_wide_should_draw_both_checkboxes(void) {
  test_apply_theme();
  s_complication_slots[0].source = DATA_SOURCE_BT_QT;  // top slot: wide form
  s_connected = true;
  s_quiet_time_active = true;
  mock_text_run_count = 0;
  s_scene_cache_valid = false;
  canvas_update_proc(NULL, NULL);

  bool x_found = false, z_found = false;
  for (int i = 0; i < mock_text_run_count; i++) {
    if (strcmp(mock_text_runs[i], "[x]") == 0) x_found = true;
    if (strcmp(mock_text_runs[i], "[z]") == 0) z_found = true;
  }
  TEST_ASSERT_TRUE(x_found);
  TEST_ASSERT_TRUE(z_found);
}

void test_canvas_procs_should_never_word_wrap(void) {
  test_apply_theme();
  s_complication_slots[3].source = DATA_SOURCE_BATTERY_BAR;  // exercise the shade runs too
  mock_wordwrap_calls = 0;
  s_scene_cache_valid = false;
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(0, mock_wordwrap_calls);
}

// One recorded fill pinned to its exact canvas pixels.
static bool fill_rect_at(GRect rect, GColor color) {
  for (int i = 0; i < mock_fill_rect_count; i++) {
    if (mock_fill_rects[i].origin.x == rect.origin.x &&
        mock_fill_rects[i].origin.y == rect.origin.y && mock_fill_rects[i].size.w == rect.size.w &&
        mock_fill_rects[i].size.h == rect.size.h && gcolor_equal(mock_fill_rect_colors[i], color)) {
      return true;
    }
  }
  return false;
}

static bool has_text_run(const char* text) {
  for (int i = 0; i < mock_text_run_count; i++) {
    if (strcmp(mock_text_runs[i], text) == 0) return true;
  }
  return false;
}

static void render_canvas(void) {
  mock_text_runs_reset();
  mock_fill_rect_reset();
  s_scene_cache_valid = false;
  canvas_update_proc(NULL, NULL);
}

void test_weather_strip_should_fill_only_the_chip_that_earns_a_band(void) {
  test_apply_theme();
  s_complication_slots[SLOT_IDX_CENTER].source = DATA_SOURCE_WEATHER_FULL;
  GRect box = s_complication_slots[SLOT_IDX_CENTER].box_rect;
  s_weather_temp = 72;
  s_weather_cond_code = 0;
  s_weather_humidity = 55;
  s_weather_pcp = 80;  // past 70: plan around it
  render_canvas();

  // The strip is 19 cells centred in the row; PCP is its last 4-cell chip.
  int strip_x = box.origin.x + (box.size.w - FULL_WEATHER_STRIP_CELLS * VGA16_CHAR_W) / 2;
  int pcp_x = strip_x + 15 * VGA16_CHAR_W;
  int hum_x = strip_x + 10 * VGA16_CHAR_W;
  GRect band = status_band_rect(box);
  TEST_ASSERT_TRUE(fill_rect_at(GRect(pcp_x, band.origin.y, 4 * VGA16_CHAR_W, band.size.h),
                                s_active_theme->status_red));
  TEST_ASSERT_FALSE(fill_rect_at(GRect(hum_x, band.origin.y, 4 * VGA16_CHAR_W, band.size.h),
                                 s_active_theme->status_red));
}

void test_wind_without_a_bearing_should_draw_the_speed_alone(void) {
  test_apply_theme();
  s_complication_slots[SLOT_IDX_TOP_LEFT].source = DATA_SOURCE_WIND;

  s_weather_wind_direction = -1;
  s_weather_wind_speed = 12;
  render_canvas();
  TEST_ASSERT_TRUE(has_text_run("12 mph"));
  for (int i = 0; i < mock_text_run_count; i++) {
    TEST_ASSERT_NULL(strstr(mock_text_runs[i], "\xE2\x86"));  // no arrow glyph anywhere
  }

  s_weather_wind_speed = -1;  // nothing at all: the sentinel dash
  render_canvas();
  TEST_ASSERT_TRUE(has_text_run("--"));
}

void test_bt_qt_narrow_should_draw_one_run_not_two_boxes(void) {
  test_apply_theme();
  s_complication_slots[SLOT_IDX_BOTTOM_CENTER].source = DATA_SOURCE_BT_QT;
  s_connected = true;
  s_quiet_time_active = true;
  render_canvas();

  // The default Bluetooth slot legitimately draws a lone "[x]"; a lone "[z]"
  // could only come from the combined window splitting into two boxes.
  bool found = false;
  for (int i = 0; i < mock_text_run_count; i++) {
    TEST_ASSERT_NOT_EQUAL(0, strcmp(mock_text_runs[i], "[z]"));
    if (strcmp(mock_text_runs[i], "[x][z]") == 0) {
      found = true;
      TEST_ASSERT_TRUE(
          gcolor_equal(mock_text_run_colors[i], get_source_color(DATA_SOURCE_BLUETOOTH)));
    }
  }
  TEST_ASSERT_TRUE(found);
}

void test_progress_bar_should_show_dashes_without_a_reading(void) {
  test_apply_theme();
  s_complication_slots[SLOT_IDX_BOTTOM_LEFT].source = DATA_SOURCE_STEPS_BAR;
  s_step_count = -1;
  GRect box = s_complication_slots[SLOT_IDX_BOTTOM_LEFT].box_rect;
  GRect band = status_band_rect(box);
  int cells = band.size.w / VGA16_CHAR_W;
  int row_x = band.origin.x + (band.size.w - cells * VGA16_CHAR_W) / 2;
  render_canvas();

  TEST_ASSERT_TRUE(has_text_run("  --"));
  // Nothing is filled: the whole bar is track.
  TEST_ASSERT_FALSE(fill_rect_at(GRect(row_x, band.origin.y, VGA16_CHAR_W, VGA16_CELL_H),
                                 s_active_theme->text_primary));
}

void test_steps_bar_should_fill_with_the_plain_text_color(void) {
  // The steps bar encodes no status, so its fill is the plain text color —
  // the rule get_source_color gives STEPS.
  s_complication_slots[5].source = DATA_SOURCE_STEPS_BAR;
  s_step_count = 6000;  // 60% of the 10k goal
  mock_fill_rect_reset();

  s_scene_cache_valid = false;
  canvas_update_proc(NULL, NULL);

  bool saw_bar_fill = false;
  for (int i = 0; i < mock_fill_rect_count; i++) {
    TEST_ASSERT_TRUE(mock_fill_rect_colors[i] != s_active_theme->status_green);
    if (mock_fill_rects[i].size.w > 0 && mock_fill_rect_colors[i] == s_active_theme->text_primary) {
      saw_bar_fill = true;
    }
  }
  TEST_ASSERT_TRUE(saw_bar_fill);
}

void test_battery_bar_should_fill_with_the_status_color(void) {
  // The fill follows the battery ladder: plain text while healthy, then
  // yellow, then red — the chip's band and this fill paint from one reading.
  s_complication_slots[5].source = DATA_SOURCE_BATTERY_BAR;

  int levels[] = {100, 24, 14};
  GColor fills[] = {s_active_theme->text_primary, s_active_theme->status_yellow,
                    s_active_theme->status_red};

  for (int c = 0; c < 3; c++) {
    s_battery_level = levels[c];
    mock_fill_rect_reset();
    s_scene_cache_valid = false;
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
}

void test_aqi_chip_should_band_only_on_an_attention_reading(void) {
  // Chip banding follows the strip's rule: a quiet reading draws plain text,
  // a flagged one fills — the thresholds stay in get_source_color alone.
  s_complication_slots[3].source = DATA_SOURCE_AQI;
  GRect band = status_band_rect(s_complication_slots[3].box_rect);

  s_weather_aqi = 30;  // clean air fills nothing
  mock_fill_rect_reset();
  s_scene_cache_valid = false;
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
  s_scene_cache_valid = false;
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
}

void test_battery_complications_should_wear_green_while_charging(void) {
  // Charging speaks green on both forms: chip band and bar fill, level
  // notwithstanding — the quiet ladder is for when it is draining.
  s_complication_slots[3].source = DATA_SOURCE_BATTERY;
  GRect band = status_band_rect(s_complication_slots[3].box_rect);
  s_battery_level = 100;
  s_battery_charging = true;

  mock_fill_rect_reset();
  s_scene_cache_valid = false;
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
  s_scene_cache_valid = false;
  canvas_update_proc(NULL, NULL);
  bool saw_bar_fill = false;
  for (int i = 0; i < mock_fill_rect_count; i++) {
    if (mock_fill_rects[i].size.w > 0 &&
        gcolor_equal(mock_fill_rect_colors[i], s_active_theme->status_green)) {
      saw_bar_fill = true;
    }
  }
  TEST_ASSERT_TRUE(saw_bar_fill);
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

void test_pcp_chip_should_band_on_attention_probability(void) {
  // Parity with the strip chip: a quiet probability reads plain, past 50 the
  // reading bands, past 70 it bands red.
  s_complication_slots[3].source = DATA_SOURCE_WEATHER_PCP;
  GRect band = status_band_rect(s_complication_slots[3].box_rect);

  s_weather_pcp = 45;  // <= 50: nothing to plan around
  mock_fill_rect_reset();
  s_scene_cache_valid = false;
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_FALSE(pcp_slot_banded_with(band, s_active_theme->status_yellow));
  TEST_ASSERT_FALSE(pcp_slot_banded_with(band, s_active_theme->status_red));

  s_weather_pcp = 60;  // 51-70: worth a thought — yellow band
  mock_fill_rect_reset();
  s_scene_cache_valid = false;
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_TRUE(pcp_slot_banded_with(band, s_active_theme->status_yellow));

  s_weather_pcp = 75;  // past 70: plan around it — red band
  mock_fill_rect_reset();
  s_scene_cache_valid = false;
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_TRUE(pcp_slot_banded_with(band, s_active_theme->status_red));

  s_weather_pcp = -1;  // no reading at all: quiet
  mock_fill_rect_reset();
  s_scene_cache_valid = false;
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_FALSE(pcp_slot_banded_with(band, s_active_theme->status_yellow));
  TEST_ASSERT_FALSE(pcp_slot_banded_with(band, s_active_theme->status_red));
}

void test_pcp_chip_should_band_by_wmo_intensity(void) {
  // Amount mode: light rain stays plain; at 4 mm the chip bands like the
  // strip's.
  s_complication_slots[3].source = DATA_SOURCE_WEATHER_PCP;
  s_settings_units = 1;
  s_weather_cond_code = 61;
  GRect band = status_band_rect(s_complication_slots[3].box_rect);

  s_precip_now = 30;  // 3 mm: light
  mock_fill_rect_reset();
  mock_text_runs_reset();
  s_scene_cache_valid = false;
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_FALSE(pcp_slot_banded_with(band, s_active_theme->status_yellow));
  TEST_ASSERT_FALSE(pcp_slot_banded_with(band, s_active_theme->status_red));

  s_precip_now = 40;  // 4 mm: heavy — yellow band
  mock_fill_rect_reset();
  mock_text_runs_reset();
  s_scene_cache_valid = false;
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_TRUE(pcp_slot_banded_with(band, s_active_theme->status_yellow));

  s_precip_now = 80;  // 8 mm: violent — red band
  mock_fill_rect_reset();
  mock_text_runs_reset();
  s_scene_cache_valid = false;
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_TRUE(pcp_slot_banded_with(band, s_active_theme->status_red));
}

void test_battery_callback_should_coalesce_unchanged_levels(void) {
  main_window_load(NULL);
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
  TEST_ASSERT_EQUAL_STRING("WEEK", get_source_label(DATA_SOURCE_WEEK_NUMBER));
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
  TEST_ASSERT_EQUAL_INT(31, count);  // one row per live enum value
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
    TEST_ASSERT_GREATER_OR_EQUAL(FRAME_PLAIN, row->frame);
    TEST_ASSERT_LESS_OR_EQUAL(FRAME_HUM_PCP, row->frame);
    // EMPTY is the one source allowed to draw nothing at all. Everything
    // else resolves a drawer directly off the row.
    if (row->source == DATA_SOURCE_EMPTY) {
      TEST_ASSERT_NULL(row->draw);
    } else {
      TEST_ASSERT_NOT_NULL(row->draw);
    }
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
                                             DATA_SOURCE_HUM_PCP,
                                             DATA_SOURCE_SOLAR_HOUR_ANGLE,
                                             DATA_SOURCE_SOLAR_ELEVATION,
                                             DATA_SOURCE_SOLAR_POSITION};
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

void test_registry_health_metrics_match_the_reads_table(void) {
  // A metric is read only while a visible slot's spec attributes it. The
  // exact attribution set is pinned (a lost field on STEPS itself must not
  // hide behind STEPS_BAR), and no row may attribute a metric the reads
  // table never reads — the reverse form of the silent seam.
  const struct {
    ComplicationDataSource source;
    int metric;
  } expected[] = {{DATA_SOURCE_STEPS, HealthMetricStepCount},
                  {DATA_SOURCE_STEPS_BAR, HealthMetricStepCount},
                  {DATA_SOURCE_SLEEP, HealthMetricSleepSeconds},
                  {DATA_SOURCE_ACTIVE_MINUTES, HealthMetricActiveSeconds},
                  {DATA_SOURCE_HEART_RATE, HealthMetricHeartRateBPM}};
  const int read_count = (int)(sizeof(s_health_reads) / sizeof(s_health_reads[0]));
  const int count = (int)(sizeof(s_complication_specs) / sizeof(s_complication_specs[0]));
  int found = 0;
  for (int i = 0; i < count; i++) {
    const ComplicationSpec* row = &s_complication_specs[i];
    int want = HEALTH_METRIC_NONE;
    for (unsigned j = 0; j < sizeof(expected) / sizeof(expected[0]); j++) {
      if (expected[j].source == row->source) want = expected[j].metric;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(want, row->health_metric,
                                  "health attribution differs from the expected set");
    if (row->health_metric == HEALTH_METRIC_NONE) continue;
    found++;
    bool in_reads = false;
    for (int r = 0; r < read_count; r++) {
      if ((int)s_health_reads[r].metric == row->health_metric) in_reads = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(in_reads, "spec attributes a metric the table never reads");
  }
  TEST_ASSERT_EQUAL_INT((int)(sizeof(expected) / sizeof(expected[0])), found);
  for (int r = 0; r < read_count; r++) {
    int attributers = 0;
    for (int i = 0; i < count; i++) {
      if (s_complication_specs[i].health_metric == (int)s_health_reads[r].metric) {
        attributers++;
      }
    }
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, attributers, "reads-table metric has no spec row");
  }
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
  s_weather_cond_code = -1;
  get_source_data(DATA_SOURCE_WEATHER, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("-- --", buf);

  // Imperial: no sign on positives (signed F is noise; negatives are rare)
  s_settings_units = 0;
  s_weather_temp = 72;
  s_weather_cond_code = 0;
  get_source_data(DATA_SOURCE_WEATHER, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("SUN 72F", buf);

  // Metric: always signed
  s_settings_units = 1;
  s_weather_temp = 22;
  s_weather_cond_code = 1;
  get_source_data(DATA_SOURCE_WEATHER, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("CLDY +22C", buf);

  // Widest realistic forms still fit the 11-cell top-slot budget
  s_weather_temp = -22;
  s_weather_cond_code = 95;
  get_source_data(DATA_SOURCE_WEATHER, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("TSTM -22C", buf);
  s_settings_units = 0;
  s_weather_temp = 103;
  s_weather_cond_code = 61;
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
  TEST_ASSERT_EQUAL_INT((s_sleep_seconds * 100) / SLEEP_GOAL_S, percent);

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
  s_weather_cond_code = 61;
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

  s_step_count = 100 * STEP_GOAL;  // 10000% of goal
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

  // The boundaries themselves: 25 still quiet, 24 yellow, 15 yellow, 14 red.
  s_battery_level = 25;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_BATTERY));
  s_battery_level = 24;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_BATTERY));
  s_battery_level = 15;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_BATTERY));
  s_battery_level = 14;
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
  s_weather_cond_code = 61;
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
  // the window is top-slot wide.
  TEST_ASSERT_FALSE(is_wide_slot(62));  // Bottom Center
  TEST_ASSERT_FALSE(is_wide_slot(63));  // Bottom Left/Right
  TEST_ASSERT_TRUE(is_wide_slot(93));   // Top row
  TEST_ASSERT_TRUE(is_wide_slot(184));  // Centre row
}

void test_get_source_data_should_format_active_minutes(void) {
  char buf[16];
  int percent = 0;

  // No data reads like steps and sleep: "--", never a fake "0m"
  s_active_minutes = -1;
  get_source_data(DATA_SOURCE_ACTIVE_MINUTES, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("--", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);

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
                                         &s_theme_navigator, &s_theme_amber};
#define NUM_THEMES (sizeof(all_themes) / sizeof(all_themes[0]))

void test_determine_theme_should_handle_all_configurations(void) {
  TEST_ASSERT_EQUAL_PTR(&s_theme_dialog, determine_theme(1));
  TEST_ASSERT_EQUAL_PTR(&s_theme_panel, determine_theme(2));
  TEST_ASSERT_EQUAL_PTR(&s_theme_shadow, determine_theme(3));
  TEST_ASSERT_EQUAL_PTR(&s_theme_navigator, determine_theme(4));
  TEST_ASSERT_EQUAL_PTR(&s_theme_amber, determine_theme(THEME_SETTING_AMBER));
  TEST_ASSERT_EQUAL_PTR(&s_theme_amber, determine_theme(6));

  // 0 (retired Auto) and anything unrecognized fall back to Norton, never
  // NULL — a stale persisted setting must still resolve to a palette.
  TEST_ASSERT_EQUAL_PTR(&s_theme_panel, determine_theme(0));
  TEST_ASSERT_EQUAL_PTR(&s_theme_panel, determine_theme(99));
  TEST_ASSERT_EQUAL_PTR(&s_theme_panel, determine_theme(-1));
}

void test_pick_theme_setting_should_go_night_only_below_the_horizon(void) {
  TEST_ASSERT_EQUAL_INT(3, pick_theme_setting(2, 3, true, -84));
  TEST_ASSERT_EQUAL_INT(3, pick_theme_setting(2, 3, true, -5000));
  // -0.833 degrees exactly is still day; the sun has to be below it.
  TEST_ASSERT_EQUAL_INT(2, pick_theme_setting(2, 3, true, -83));
  TEST_ASSERT_EQUAL_INT(2, pick_theme_setting(2, 3, true, 0));
  TEST_ASSERT_EQUAL_INT(2, pick_theme_setting(2, 3, true, 4500));
}

void test_pick_theme_setting_should_stay_day_without_a_location_or_a_night_theme(void) {
  TEST_ASSERT_EQUAL_INT(2, pick_theme_setting(2, 3, false, -5000));
  TEST_ASSERT_EQUAL_INT(2, pick_theme_setting(2, 0, true, -5000));
  // A night theme equal to the day one changes nothing.
  TEST_ASSERT_EQUAL_INT(2, pick_theme_setting(2, 2, true, -5000));
}

void test_apply_theme_should_follow_the_elevation_and_the_known_location(void) {
  s_settings_theme = 2;
  s_settings_night_theme = 3;
  s_latitude = 6017;
  s_longitude = 2494;
  s_solar_elevation = -2000;
  apply_theme();
  TEST_ASSERT_EQUAL_PTR(&s_theme_shadow, s_active_theme);
  s_solar_elevation = 2000;
  apply_theme();
  TEST_ASSERT_EQUAL_PTR(&s_theme_panel, s_active_theme);
  s_solar_elevation = -2000;
  s_latitude = LATITUDE_NONE;
  apply_theme();
  TEST_ASSERT_EQUAL_PTR(&s_theme_panel, s_active_theme);
  s_latitude = 6017;
  s_longitude = LONGITUDE_NONE;
  apply_theme();
  TEST_ASSERT_EQUAL_PTR(&s_theme_panel, s_active_theme);
}

void test_refresh_state_should_pick_the_theme_from_the_elevation_it_just_computed(void) {
  s_settings_theme = 2;
  s_settings_night_theme = 3;
  s_latitude = 6017;
  s_longitude = 2494;
  time_t now = time(NULL);
  // Stale in the opposite direction from the real one: the theme must not
  // trail the elevation by a refresh.
  s_solar_elevation = compute_solar_elevation(now, 6017, 2494) < SUNSET_ELEVATION ? 2000 : -2000;
  refresh_state();
  bool night = compute_solar_elevation(now, 6017, 2494) < SUNSET_ELEVATION;
  TEST_ASSERT_EQUAL_PTR(night ? &s_theme_shadow : &s_theme_panel, s_active_theme);
}

void test_refresh_state_should_not_compute_the_elevation_without_a_location(void) {
  s_solar_elevation = 1234;
  refresh_state();
  TEST_ASSERT_EQUAL_INT(1234, s_solar_elevation);
  s_latitude = 6017;  // half a position is still none
  refresh_state();
  TEST_ASSERT_EQUAL_INT(1234, s_solar_elevation);
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

    // A mark or status color that matches the primary text stops standing out.
    TEST_ASSERT_NOT_EQUAL(t->text_primary, t->mark);
    TEST_ASSERT_NOT_EQUAL(t->text_primary, t->status_green);
    TEST_ASSERT_NOT_EQUAL(t->text_primary, t->status_yellow);
    TEST_ASSERT_NOT_EQUAL(t->text_primary, t->status_red);
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
    if (t == &s_theme_amber) continue;  // a monochrome monitor's phosphor, not a CGA palette
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

void test_navigator_should_take_dos_navigators_header_and_info_colors(void) {
  const WatchTheme* t = &s_theme_navigator;
  TEST_ASSERT_EQUAL_HEX(GColorDarkGray, t->center_bg);
  TEST_ASSERT_EQUAL_HEX(GColorWhite, t->frame);
  TEST_ASSERT_EQUAL_HEX(GColorWhite, t->text_primary);
  // Titles are the column headers' yellow, marks the info line's cyan.
  TEST_ASSERT_EQUAL_HEX(GColorIcterine, t->text_secondary);
  TEST_ASSERT_EQUAL_HEX(GColorElectricBlue, t->mark);
}

void test_amber_should_keep_its_tiers_on_its_glowing_ground(void) {
  const WatchTheme* t = &s_theme_amber;
  TEST_ASSERT_EQUAL_HEX(GColorBulgarianRose, t->center_bg);
  // Values read white on the glow; the amber phosphor carries titles and chrome.
  TEST_ASSERT_EQUAL_HEX(GColorWhite, t->text_primary);
  TEST_ASSERT_EQUAL_HEX(GColorChromeYellow, t->text_secondary);
  TEST_ASSERT_EQUAL_HEX(t->text_secondary, t->frame);

  TEST_ASSERT_EQUAL_HEX(GColorScreaminGreen, t->status_green);
  TEST_ASSERT_EQUAL_HEX(GColorIcterine, t->status_yellow);
  TEST_ASSERT_EQUAL_HEX(GColorMelon, t->status_red);
  TEST_ASSERT_EQUAL_HEX(GColorElectricBlue, t->accent_cold);
  TEST_ASSERT_EQUAL_HEX(GColorBlack, t->status_ink);
}

void test_theme_ground_level_should_give_level_1_tinted_field(void) {
  TEST_ASSERT_EQUAL_UINT8(2, theme_ground_level(&s_theme_dialog));
  TEST_ASSERT_EQUAL_UINT8(1, theme_ground_level(&s_theme_navigator));
  TEST_ASSERT_EQUAL_UINT8(1, theme_ground_level(&s_theme_amber));
  // Not every dark ground with one lit channel: Norton's blue is (0,0,2).
  TEST_ASSERT_EQUAL_UINT8(0, theme_ground_level(&s_theme_panel));
  TEST_ASSERT_EQUAL_UINT8(0, theme_ground_level(&s_theme_shadow));
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
  s_weather_cond_code = -1;
  s_weather_humidity = -1;
  s_weather_pcp = -1;
  get_source_data(DATA_SOURCE_WEATHER_FULL, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("-- -- -- --", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);

  // A mid-fetch blip leaves one field at sentinel, the rest live
  s_settings_units = 0;
  s_weather_temp = 72;
  s_weather_cond_code = 0;
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
  s_weather_cond_code = 95;
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
  s_weather_cond_code = 0;
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
  TEST_ASSERT_NOT_NULL(complication_spec(DATA_SOURCE_WEATHER_PCP)->draw);

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
  s_weather_cond_code = 61;
  s_precip_now = 25;  // 2.5mm over the past hour
  get_source_data(DATA_SOURCE_WEATHER_PCP, buf, sizeof(buf), &percent);
  TEST_ASSERT_EQUAL_STRING("2mm", buf);
  TEST_ASSERT_EQUAL_INT(0, percent);  // amount tells no progress-story

  s_weather_cond_code = 95;
  s_precip_now = 1230;  // cloudburst clamps
  get_source_data(DATA_SOURCE_WEATHER_PCP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("99mm", buf);

  s_weather_cond_code = 71;
  s_precip_now = 4;  // trace drizzle
  get_source_data(DATA_SOURCE_WEATHER_PCP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("<1mm", buf);

  // Imperial never switches
  s_settings_units = 0;
  get_source_data(DATA_SOURCE_WEATHER_PCP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("45%", buf);

  // Settled sky or a missing live reading falls back to probability
  s_settings_units = 1;
  s_weather_cond_code = 0;
  get_source_data(DATA_SOURCE_WEATHER_PCP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("45%", buf);
  s_weather_cond_code = 61;
  s_precip_now = -1;
  get_source_data(DATA_SOURCE_WEATHER_PCP, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("45%", buf);
}

void test_wmo_cond_should_map_words_and_the_precipitation_facet(void) {
  // One table, both facets: the word the chips render and whether the family
  // flips PCP to the live rate. Edges and unmapped codes are the drift risks.
  struct {
    int code;
    const char* word;
    bool precipitating;
  } cases[] = {{0, "SUN", false},  {1, "CLDY", false}, {3, "CLDY", false}, {45, "FOG", false},
               {51, "RAIN", true}, {55, "RAIN", true}, {56, "RAIN", true}, {57, "RAIN", true},
               {58, "--", false},  {61, "RAIN", true}, {65, "RAIN", true}, {66, "RAIN", true},
               {67, "RAIN", true}, {68, "--", false},  {71, "SNOW", true}, {80, "RAIN", true},
               {82, "RAIN", true}, {85, "SNOW", true}, {95, "TSTM", true}, {99, "TSTM", true},
               {100, "--", false}, {-1, "--", false}};
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    TEST_ASSERT_EQUAL_STRING(cases[i].word, weather_cond_word(cases[i].code));
    TEST_ASSERT_EQUAL(cases[i].precipitating, weather_cond_precipitating(cases[i].code));
  }
}

void test_precip_amount_mode_should_gate_on_units_family_and_data(void) {
  s_settings_units = 1;
  s_precip_now = 25;

  s_weather_cond_code = 61;  // rain, tstm, snow: the live rate wins
  TEST_ASSERT_TRUE(weather_shows_precip_amount());
  s_weather_cond_code = 95;
  TEST_ASSERT_TRUE(weather_shows_precip_amount());
  s_weather_cond_code = 71;
  TEST_ASSERT_TRUE(weather_shows_precip_amount());

  s_weather_cond_code = 0;  // settled, fog, unmapped, missing: never
  TEST_ASSERT_FALSE(weather_shows_precip_amount());
  s_weather_cond_code = 45;
  TEST_ASSERT_FALSE(weather_shows_precip_amount());
  s_weather_cond_code = 100;
  TEST_ASSERT_FALSE(weather_shows_precip_amount());
  s_weather_cond_code = -1;
  TEST_ASSERT_FALSE(weather_shows_precip_amount());

  s_weather_cond_code = 61;
  s_settings_units = 0;  // imperial never switches to the rate
  TEST_ASSERT_FALSE(weather_shows_precip_amount());
  s_settings_units = 1;
  s_precip_now = -1;  // and a missing live reading falls back to probability
  TEST_ASSERT_FALSE(weather_shows_precip_amount());
}

void test_weather_cond_formatter_should_render_the_word_or_dashes(void) {
  char buf[8];

  s_weather_cond_code = 71;
  get_source_data(DATA_SOURCE_WEATHER_COND, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("SNOW", buf);

  s_weather_cond_code = -1;  // no data; an unmapped code reads the same
  get_source_data(DATA_SOURCE_WEATHER_COND, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("--", buf);
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

void test_iso_week_number_should_be_exact_iso_8601(void) {
  // Vectors verified against a reference calendar: year, 0-based yday, wday
  // (Sunday = 0) → week. The boundary rows are the ragged edge of ISO: early
  // January can be W52/W53 'of last year', late December W01 'of next year'.
  TEST_ASSERT_EQUAL_INT(53, iso_week_number(2021, 0, 5));    // 2021-01-01 Fri
  TEST_ASSERT_EQUAL_INT(53, iso_week_number(2020, 365, 4));  // 2020-12-31 Thu (leap, W53 year)
  TEST_ASSERT_EQUAL_INT(52, iso_week_number(2019, 362, 0));  // 2019-12-29 Sun
  TEST_ASSERT_EQUAL_INT(1, iso_week_number(2019, 363, 1));   // 2019-12-30 Mon → W01/2020
  TEST_ASSERT_EQUAL_INT(1, iso_week_number(2020, 0, 3));     // 2020-01-01 Wed
  TEST_ASSERT_EQUAL_INT(53, iso_week_number(2015, 364, 4));  // 2015-12-31 Thu
  TEST_ASSERT_EQUAL_INT(53, iso_week_number(2016, 0, 5));    // 2016-01-01 Fri → W53/2015
  TEST_ASSERT_EQUAL_INT(52, iso_week_number(2017, 0, 0));    // 2017-01-01 Sun → W52/2016
  TEST_ASSERT_EQUAL_INT(1, iso_week_number(2018, 364, 1));   // 2018-12-31 Mon → W01/2019
  TEST_ASSERT_EQUAL_INT(52, iso_week_number(2018, 363, 0));  // 2018-12-30 Sun
  TEST_ASSERT_EQUAL_INT(34, iso_week_number(2026, 228, 1));  // mid-year sanity
  TEST_ASSERT_EQUAL_INT(1, iso_week_number(2004, 0, 4));     // 2004-01-01 Thu: Jan 1 in W01
  TEST_ASSERT_EQUAL_INT(53, iso_week_number(2009, 361, 1));  // Thu-year, W53 though not Dec-31
}

void test_get_source_data_should_format_the_iso_week(void) {
  s_week_number = 7;
  char buf[8];
  get_source_data(DATA_SOURCE_WEEK_NUMBER, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("W07", buf);
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

static time_t utc_at(int year, int month, int day, int hour, int minute) {
  struct tm t = {.tm_year = year - 1900,
                 .tm_mon = month - 1,
                 .tm_mday = day,
                 .tm_hour = hour,
                 .tm_min = minute};
  return timegm(&t);
}

void test_compute_solar_hour_angle_should_match_the_reference_readings(void) {
  const int helsinki = 2494;
  TEST_ASSERT_EQUAL_INT(-16, compute_solar_hour_angle(utc_at(2026, 10, 18, 9, 0), helsinki));
  TEST_ASSERT_EQUAL_INT(-1, compute_solar_hour_angle(utc_at(2026, 11, 3, 10, 0), helsinki));
  TEST_ASSERT_EQUAL_INT(-9, compute_solar_hour_angle(utc_at(2026, 2, 11, 10, 0), helsinki));
  TEST_ASSERT_EQUAL_INT(-7, compute_solar_hour_angle(utc_at(2026, 7, 26, 10, 0), helsinki));
  TEST_ASSERT_EQUAL_INT(-5, compute_solar_hour_angle(utc_at(2026, 6, 13, 10, 0), helsinki));
}

void test_compute_solar_hour_angle_should_wrap_into_minus_179_to_180(void) {
  // Past solar midnight the raw angle exceeds 180 and comes back negative...
  TEST_ASSERT_EQUAL_INT(-100, compute_solar_hour_angle(utc_at(2026, 6, 13, 18, 0), 17000));
  // ...and before it, below -180 and comes back positive.
  TEST_ASSERT_EQUAL_INT(100, compute_solar_hour_angle(utc_at(2026, 6, 13, 6, 0), -17000));
  // 180 itself stays positive; -360 is a full turn back to noon.
  TEST_ASSERT_EQUAL_INT(180, compute_solar_hour_angle(utc_at(2026, 6, 13, 12, 0), 18000));
  TEST_ASSERT_EQUAL_INT(0, compute_solar_hour_angle(utc_at(2026, 6, 13, 0, 0), -18000));

  time_t day = utc_at(2026, 6, 13, 0, 0);
  const int longitudes[] = {-18000, -7398, 0, 2494, 18000};
  for (unsigned i = 0; i < sizeof(longitudes) / sizeof(longitudes[0]); i++) {
    for (int minute = 0; minute < 24 * 60; minute++) {
      int angle = compute_solar_hour_angle(day + minute * 60, longitudes[i]);
      TEST_ASSERT_TRUE(angle >= -179 && angle <= 180);
    }
  }
}

void test_compute_solar_hour_angle_should_round_west_longitudes_to_the_nearest_degree(void) {
  // -73.63 raw: nearest is -74, not the truncated -73.
  TEST_ASSERT_EQUAL_INT(-74, compute_solar_hour_angle(utc_at(2026, 12, 21, 12, 0), -7398));
  TEST_ASSERT_EQUAL_INT(-42, compute_solar_hour_angle(utc_at(2026, 1, 15, 17, 30), -12245));
  TEST_ASSERT_EQUAL_INT(-3, compute_solar_hour_angle(utc_at(2026, 3, 1, 12, 0), 0));
}

void test_get_source_data_should_format_the_solar_hour_angle(void) {
  char buf[16];
  s_longitude = 2494;

  s_solar_hour_angle = 0;
  get_source_data(DATA_SOURCE_SOLAR_HOUR_ANGLE, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("0\xC2\xB0", buf);

  s_solar_hour_angle = -16;
  get_source_data(DATA_SOURCE_SOLAR_HOUR_ANGLE, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("-16\xC2\xB0", buf);

  s_solar_hour_angle = 52;
  get_source_data(DATA_SOURCE_SOLAR_HOUR_ANGLE, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("+52\xC2\xB0", buf);

  // The widest readings still fit a 63px slot: five cells, the sign and the
  // degree sign included.
  s_solar_hour_angle = -179;
  get_source_data(DATA_SOURCE_SOLAR_HOUR_ANGLE, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("-179\xC2\xB0", buf);
  s_solar_hour_angle = 180;
  get_source_data(DATA_SOURCE_SOLAR_HOUR_ANGLE, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("+180\xC2\xB0", buf);

  s_longitude = LONGITUDE_NONE;
  get_source_data(DATA_SOURCE_SOLAR_HOUR_ANGLE, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("--", buf);
  TEST_ASSERT_EQUAL_STRING("SUN", get_source_label(DATA_SOURCE_SOLAR_HOUR_ANGLE));
}

void test_refresh_state_should_compute_the_solar_hour_angle(void) {
  s_longitude = 2494;
  refresh_state();
  TEST_ASSERT_EQUAL_INT(compute_solar_hour_angle(time(NULL), 2494), s_solar_hour_angle);
}

void test_the_solar_hour_angle_should_request_weather_to_learn_the_longitude(void) {
  set_slots(kSlotsOnlySteps);
  TEST_ASSERT_FALSE(any_slot_needs_weather());
  s_complication_slots[SLOT_IDX_BOTTOM_CENTER].source = DATA_SOURCE_SOLAR_HOUR_ANGLE;
  TEST_ASSERT_TRUE(any_slot_needs_weather());
}

void test_inbox_should_persist_the_longitude_and_load_it_back(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_LONGITUDE, -7398);
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(-7398, s_longitude);
  TEST_ASSERT_EQUAL_INT(-7398, persist_read_int(PERSIST_KEY_LONGITUDE));

  s_longitude = LONGITUDE_NONE;
  load_longitude();
  TEST_ASSERT_EQUAL_INT(-7398, s_longitude);
}

void test_inbox_should_drop_an_impossible_longitude(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_LONGITUDE, 18001);
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(LONGITUDE_NONE, s_longitude);
}

void test_load_longitude_should_keep_the_sentinel_when_nothing_was_stored(void) {
  mock_persist_reset();
  load_longitude();
  TEST_ASSERT_EQUAL_INT(LONGITUDE_NONE, s_longitude);
}

void test_the_longitude_should_outlive_stale_weather(void) {
  s_longitude = 2494;
  s_solar_hour_angle = -16;
  s_weather_temp = 72;
  s_weather_stamp = (int)(time(NULL) - WEATHER_STALE_AGE_S - 60);
  weather_expire(time(NULL));
  TEST_ASSERT_EQUAL_INT(WEATHER_TEMP_NONE, s_weather_temp);
  TEST_ASSERT_EQUAL_INT(2494, s_longitude);
  weather_discard();
  TEST_ASSERT_EQUAL_INT(2494, s_longitude);

  char buf[16];
  get_source_data(DATA_SOURCE_SOLAR_HOUR_ANGLE, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("-16\xC2\xB0", buf);
}

typedef struct {
  time_t utc;
  int latitude;
  int longitude;
  int elevation;
  const char* formatted;  // NULL where the value sits too near a rounding edge
} SolarReading;

static SolarReading solar_readings(int i) {
  const SolarReading readings[] = {
      {utc_at(2026, 10, 18, 9, 0), 6017, 2494, 1936,
       "\xE2\x86\x91"
       "19\xC2\xB0"},
      {utc_at(2026, 6, 21, 10, 20), 6017, 2494, 5328,
       "\xE2\x86\x91"
       "53\xC2\xB0"},
      {utc_at(2026, 12, 21, 10, 20), 6017, 2494, 641,
       "\xE2\x86\x91"
       "6\xC2\xB0"},
      {utc_at(2026, 12, 21, 22, 20), 6017, 2494, -5325,
       "\xE2\x86\x93"
       "53\xC2\xB0"},
      {utc_at(2026, 6, 21, 22, 20), 6017, 2494, -637,
       "\xE2\x86\x93"
       "6\xC2\xB0"},
      {utc_at(2026, 3, 20, 12, 7), 0, 0, 8946, NULL},
      {utc_at(2026, 10, 18, 15, 0), -3387, 15121, -4275,
       "\xE2\x86\x93"
       "43\xC2\xB0"},
      {utc_at(2026, 10, 18, 20, 0), 4071, -7401, 2203,
       "\xE2\x86\x91"
       "22\xC2\xB0"},
  };
  return readings[i];
}

void test_compute_solar_elevation_should_match_the_reference_readings(void) {
  for (int i = 0; i < 8; i++) {
    SolarReading r = solar_readings(i);
    int elevation = compute_solar_elevation(r.utc, r.latitude, r.longitude);
    TEST_ASSERT_INT_WITHIN_MESSAGE(15, r.elevation, elevation, "reading out of tolerance");

    if (!r.formatted) continue;
    char buf[16];
    s_latitude = r.latitude;
    s_longitude = r.longitude;
    s_solar_elevation = elevation;
    get_source_data(DATA_SOURCE_SOLAR_ELEVATION, buf, sizeof(buf), NULL);
    TEST_ASSERT_EQUAL_STRING(r.formatted, buf);
  }
}

void test_compute_solar_elevation_should_stay_within_bounds_all_year(void) {
  for (int day = 0; day < 366; day += 5) {
    for (int hour = 0; hour < 24; hour++) {
      time_t utc = utc_at(2026, 1, 1, hour, 0) + (time_t)day * 86400;
      const int latitudes[] = {-9000, -3387, 0, 6017, 9000};
      for (unsigned i = 0; i < sizeof(latitudes) / sizeof(latitudes[0]); i++) {
        int elevation = compute_solar_elevation(utc, latitudes[i], 2494);
        TEST_ASSERT_TRUE(elevation >= -9000 && elevation <= 9000);
      }
    }
  }
}

void test_get_source_data_should_format_the_solar_elevation_sign_by_the_unrounded_value(void) {
  char buf[16];
  s_latitude = 6017;
  s_longitude = 2494;

  s_solar_elevation = -30;
  get_source_data(DATA_SOURCE_SOLAR_ELEVATION, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING(
      "\xE2\x86\x93"
      "0\xC2\xB0",
      buf);
  s_solar_elevation = 30;
  get_source_data(DATA_SOURCE_SOLAR_ELEVATION, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING(
      "\xE2\x86\x91"
      "0\xC2\xB0",
      buf);
  s_solar_elevation = -9000;
  get_source_data(DATA_SOURCE_SOLAR_ELEVATION, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING(
      "\xE2\x86\x93"
      "90\xC2\xB0",
      buf);
  TEST_ASSERT_EQUAL_STRING("SUN", get_source_label(DATA_SOURCE_SOLAR_ELEVATION));
}

void test_get_source_data_should_format_the_solar_position(void) {
  char buf[16];
  s_latitude = 6017;
  s_longitude = 2494;

  s_solar_hour_angle = -16;
  s_solar_elevation = 1936;
  get_source_data(DATA_SOURCE_SOLAR_POSITION, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING(
      "-16\xC2\xB0 \xE2\x86\x91"
      "19\xC2\xB0",
      buf);

  s_solar_hour_angle = 180;
  s_solar_elevation = -5325;
  get_source_data(DATA_SOURCE_SOLAR_POSITION, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING(
      "+180\xC2\xB0 \xE2\x86\x93"
      "53\xC2\xB0",
      buf);
  TEST_ASSERT_EQUAL_INT(10, utf8_cells(buf));

  s_solar_hour_angle = 0;
  s_solar_elevation = 0;
  get_source_data(DATA_SOURCE_SOLAR_POSITION, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING(
      "0\xC2\xB0 \xE2\x86\x91"
      "0\xC2\xB0",
      buf);
  TEST_ASSERT_EQUAL_STRING("SUN", get_source_label(DATA_SOURCE_SOLAR_POSITION));
}

void test_the_solar_elevation_sources_should_read_no_data_until_both_coordinates_arrived(void) {
  const ComplicationDataSource sources[] = {DATA_SOURCE_SOLAR_ELEVATION,
                                            DATA_SOURCE_SOLAR_POSITION};
  for (unsigned i = 0; i < 2; i++) {
    char buf[16];
    s_longitude = 2494;
    s_latitude = LATITUDE_NONE;
    get_source_data(sources[i], buf, sizeof(buf), NULL);
    TEST_ASSERT_EQUAL_STRING("--", buf);

    s_longitude = LONGITUDE_NONE;
    s_latitude = 6017;
    get_source_data(sources[i], buf, sizeof(buf), NULL);
    TEST_ASSERT_EQUAL_STRING("--", buf);
  }
}

void test_utf8_cells_should_count_glyphs_not_bytes(void) {
  TEST_ASSERT_EQUAL_INT(0, utf8_cells(""));
  TEST_ASSERT_EQUAL_INT(2, utf8_cells("--"));
  TEST_ASSERT_EQUAL_INT(4, utf8_cells("\xE2\x86\x91"
                                      "12\xC2\xB0"));
  TEST_ASSERT_EQUAL_INT(9, utf8_cells("-16\xC2\xB0 \xE2\x86\x91"
                                      "19\xC2\xB0"));
}

void test_refresh_state_should_compute_the_solar_elevation(void) {
  s_latitude = 6017;
  s_longitude = 2494;
  refresh_state();
  TEST_ASSERT_EQUAL_INT(compute_solar_elevation(time(NULL), 6017, 2494), s_solar_elevation);
}

void test_the_solar_elevation_sources_should_request_weather_to_learn_the_position(void) {
  set_slots(kSlotsOnlySteps);
  s_complication_slots[SLOT_IDX_BOTTOM_CENTER].source = DATA_SOURCE_SOLAR_ELEVATION;
  TEST_ASSERT_TRUE(any_slot_needs_weather());
  s_complication_slots[SLOT_IDX_BOTTOM_CENTER].source = DATA_SOURCE_STEPS;
  s_complication_slots[SLOT_IDX_TOP_LEFT].source = DATA_SOURCE_SOLAR_POSITION;
  TEST_ASSERT_TRUE(any_slot_needs_weather());
}

void test_inbox_should_persist_the_latitude_and_load_it_back(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_LATITUDE, -3387);
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(-3387, s_latitude);
  TEST_ASSERT_EQUAL_INT(-3387, persist_read_int(PERSIST_KEY_LATITUDE));

  s_latitude = LATITUDE_NONE;
  load_latitude();
  TEST_ASSERT_EQUAL_INT(-3387, s_latitude);
}

void test_inbox_should_drop_an_impossible_latitude(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_LATITUDE, 9001);
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(LATITUDE_NONE, s_latitude);
}

void test_load_latitude_should_keep_the_sentinel_when_nothing_was_stored(void) {
  mock_persist_reset();
  load_latitude();
  TEST_ASSERT_EQUAL_INT(LATITUDE_NONE, s_latitude);
}

void test_the_latitude_should_outlive_stale_weather(void) {
  s_latitude = 6017;
  s_weather_stamp = (int)(time(NULL) - WEATHER_STALE_AGE_S - 60);
  weather_expire(time(NULL));
  TEST_ASSERT_EQUAL_INT(6017, s_latitude);
  weather_discard();
  TEST_ASSERT_EQUAL_INT(6017, s_latitude);
}

void test_get_source_color_should_return_appropriate_colors(void) {
  s_active_theme = &s_theme_panel;

  // A missing reading is neutral in either unit system — the hot/cold bands
  // are for real temperatures, "--" is not a cold one.
  s_weather_temp = -999;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_WEATHER_TEMP));

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
  s_weather_cond_code = 61;
  s_precip_now = 20;  // 2mm light — calm
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_WEATHER_PCP));
  s_precip_now = 50;  // 5mm heavy
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_WEATHER_PCP));
  s_precip_now = 90;  // 9mm violent
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_red, get_source_color(DATA_SOURCE_WEATHER_PCP));
  s_precip_now = -1;
  s_weather_cond_code = -1;
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
  // once the charge wants a charger (>=25 plain, 15-24 yellow, <=14 red)
  s_battery_level = 100;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_BATTERY));
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_BATTERY_BAR));

  s_battery_level = 25;  // boundary: still quiet
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_BATTERY));
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.text_primary, get_source_color(DATA_SOURCE_BATTERY_BAR));

  s_battery_level = 24;  // boundary: yellow
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_BATTERY));
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_BATTERY_BAR));

  s_battery_level = 15;
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_BATTERY));
  TEST_ASSERT_EQUAL_HEX(s_theme_panel.status_yellow, get_source_color(DATA_SOURCE_BATTERY_BAR));

  s_battery_level = 14;  // boundary: red
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

// Reference spellings for the sweep below, spelled out independently of the
// implementation under test so a typo on either side is caught, not shared.
static const char* kMonthAbbr[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                   "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
static const char* ref_ordinal_suffix(int day) {
  if (day >= 11 && day <= 13) return "th";
  switch (day % 10) {
    case 1:
      return "st";
    case 2:
      return "nd";
    case 3:
      return "rd";
    default:
      return "th";
  }
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
          // The TEXT body in full, suffix included, against the reference
          // spelling above — every day of the year, not just samples.
          if (fmt == DATE_FORMAT_TEXT && dow == DOW_HIDDEN) {
            char want[64];
            snprintf(want, sizeof(want), "%s %d%s, %d", kMonthAbbr[t.tm_mon], day,
                     ref_ordinal_suffix(day), t.tm_year + 1900);
            TEST_ASSERT_EQUAL_STRING(want, buf);
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

void test_date_strip_dow_should_drop_the_weekday_and_its_space(void) {
  char out[16];

  date_strip_dow(DOW_BEFORE, "THU 12-31", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("12-31", out);
  date_strip_dow(DOW_AFTER, "12-31 THU", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("12-31", out);
  date_strip_dow(DOW_HIDDEN, "12-31", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("12-31", out);

  // Degenerate input and tiny buffers stay in bounds.
  date_strip_dow(DOW_BEFORE, "", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("", out);
  date_strip_dow(DOW_AFTER, "TH", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("TH", out);
  char small[4] = "xxx";
  date_strip_dow(DOW_BEFORE, "THU 12-31", small, sizeof(small));
  TEST_ASSERT_EQUAL_STRING("12-", small);
  date_strip_dow(DOW_BEFORE, "THU 12-31", small, 0);
  TEST_ASSERT_EQUAL_STRING("12-", small);
}

void test_short_date_should_drop_the_weekday_only_in_a_narrow_slot(void) {
  test_apply_theme();
  s_settings_dow_position = DOW_BEFORE;
  snprintf(s_short_date_display, sizeof(s_short_date_display), "THU 12-31");

  mock_text_runs_reset();
  draw_short_date_complication(NULL, (GRect)SLOT_RECT_BOTTOM_LEFT, DATA_SOURCE_SHORT_DATE);
  TEST_ASSERT_EQUAL_INT(1, mock_text_run_count);
  TEST_ASSERT_EQUAL_STRING("12-31", mock_text_runs[0]);

  mock_text_runs_reset();
  draw_short_date_complication(NULL, (GRect)SLOT_RECT_TOP_LEFT, DATA_SOURCE_SHORT_DATE);
  TEST_ASSERT_EQUAL_INT(2, mock_text_run_count);
  TEST_ASSERT_EQUAL_STRING("THU", mock_text_runs[0]);
  TEST_ASSERT_EQUAL_STRING(" 12-31", mock_text_runs[1]);
}

void test_weather_field_table_should_pin_each_global_and_sentinel(void) {
  // The watch side of the contract: 18 int fields, each pointing at its
  // data.c global with the sentinel the JS field table and the formatters'
  // "--" fallbacks agree on. A row pointing at the wrong global or drifting
  // sentinel fails here.
  const struct {
    int* global;
    int sentinel;
  } want[] = {
      {&s_weather_temp, -999},     {&s_weather_cond_code, -1}, {&s_weather_aqi, -1},
      {&s_weather_uv, -1},         {&s_weather_humidity, -1},  {&s_weather_wind_direction, -1},
      {&s_weather_wind_speed, -1}, {&s_weather_pcp, -1},       {&s_precip_now, -1},
      {&s_temp_high, -999},        {&s_temp_low, -999},        {&s_temp_low_tmrw, -999},
      {&s_temp_high_tmrw, -999},   {&s_hi_hour_today, -1},     {&s_lo_hour_today, -1},
      {&s_hi_hour_tmrw, -1},       {&s_lo_hour_tmrw, -1},      {&s_weather_stamp, 0}};
  const unsigned rows = sizeof(s_weather_fields) / sizeof(s_weather_fields[0]);
  TEST_ASSERT_EQUAL_UINT(sizeof(want) / sizeof(want[0]), rows);
  for (unsigned w = 0; w < sizeof(want) / sizeof(want[0]); w++) {
    bool found = false;
    for (unsigned j = 0; j < rows; j++) {
      if (s_weather_fields[j].target == want[w].global) {
        TEST_ASSERT_FALSE(found);  // one row per global, no shadows
        found = true;
        TEST_ASSERT_EQUAL_INT(want[w].sentinel, s_weather_fields[j].sentinel);
      }
    }
    TEST_ASSERT_TRUE(found);
  }
}

void test_weather_cache_should_round_trip_when_fresh(void) {
  mock_persist_reset();

  s_weather_temp = 72;
  s_weather_cond_code = 0;
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
  s_weather_stamp = (int)time(NULL);
  save_weather_cache();

  // Simulate a relaunch: globals reset to sentinels
  s_weather_temp = -999;
  s_weather_cond_code = -1;
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
  TEST_ASSERT_EQUAL_INT(0, s_weather_cond_code);
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
  s_weather_cond_code = 0;
  save_weather_cache();
  persist_write_int(PERSIST_KEY_WEATHER_TIMESTAMP,
                    (int32_t)time(NULL) - (WEATHER_CACHE_MAX_AGE_S + 1));

  s_weather_temp = -999;
  s_weather_cond_code = -1;
  TEST_ASSERT_FALSE(load_weather_cache());
  TEST_ASSERT_EQUAL_INT(-999, s_weather_temp);
  TEST_ASSERT_EQUAL_INT(-1, s_weather_cond_code);

  // A timestamp from the future (clock change) is also rejected
  persist_write_int(PERSIST_KEY_WEATHER_TIMESTAMP, (int32_t)time(NULL) + 3600);
  TEST_ASSERT_FALSE(load_weather_cache());
}

void test_weather_cache_should_keep_values_at_edge_of_window(void) {
  mock_persist_reset();

  s_weather_temp = 18;
  s_weather_cond_code = 61;
  s_weather_aqi = 12;
  s_weather_uv = 2;
  save_weather_cache();
  // Just inside the freshness window
  persist_write_int(PERSIST_KEY_WEATHER_TIMESTAMP,
                    (int32_t)time(NULL) - (WEATHER_CACHE_MAX_AGE_S - 5));

  s_weather_temp = -999;
  TEST_ASSERT_TRUE(load_weather_cache());
  TEST_ASSERT_EQUAL_INT(18, s_weather_temp);
  TEST_ASSERT_EQUAL_INT(61, s_weather_cond_code);
}

void test_weather_cache_without_cond_code_should_degrade_to_dashes(void) {
  // A cache written before the wire switch has no code key: the per-key
  // optional load leaves the sentinel, and the word reads dashes until the
  // next fetch.
  mock_persist_reset();
  persist_write_int(PERSIST_KEY_WEATHER_TIMESTAMP, (int32_t)time(NULL));
  persist_write_int(PERSIST_KEY_WEATHER_TEMP, 18);

  s_weather_temp = -999;
  TEST_ASSERT_TRUE(load_weather_cache());
  TEST_ASSERT_EQUAL_INT(18, s_weather_temp);
  TEST_ASSERT_EQUAL_INT(-1, s_weather_cond_code);
  TEST_ASSERT_EQUAL_STRING("--", weather_cond_word(s_weather_cond_code));
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

void test_night_theme_should_round_trip_through_persistence_and_default_off(void) {
  mock_persist_reset();
  load_settings();
  TEST_ASSERT_EQUAL_INT(0, s_settings_night_theme);

  persist_write_int(PERSIST_KEY_SETTINGS_NIGHT_THEME, 3);
  s_settings_night_theme = 0;
  load_settings();
  TEST_ASSERT_EQUAL_INT(3, s_settings_night_theme);

  // Decoy under the message-key id: load_settings must ignore it.
  persist_write_int(MESSAGE_KEY_SETTINGS_NIGHT_THEME, 99);
  load_settings();
  TEST_ASSERT_EQUAL_INT(3, s_settings_night_theme);
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

void test_health_read_table_should_pin_each_metric_mode_and_sentinel(void) {
  const struct {
    int* global;
    HealthMetric metric;
    HealthReadMode mode;
  } want[] = {
      {&s_step_count, HealthMetricStepCount, HEALTH_READ_RANGE_SUM},
      {&s_sleep_seconds, HealthMetricSleepSeconds, HEALTH_READ_RANGE_SUM},
      {&s_active_minutes, HealthMetricActiveSeconds, HEALTH_READ_RANGE_SUM},
      {&s_heart_rate, HealthMetricHeartRateBPM, HEALTH_READ_INSTANT_PEEK},
  };
  const unsigned rows = sizeof(s_health_reads) / sizeof(s_health_reads[0]);
  TEST_ASSERT_EQUAL_UINT(sizeof(want) / sizeof(want[0]), rows);
  for (unsigned w = 0; w < sizeof(want) / sizeof(want[0]); w++) {
    bool found = false;
    for (unsigned j = 0; j < rows; j++) {
      if (s_health_reads[j].target == want[w].global) {
        TEST_ASSERT_FALSE(found);  // one row per global, no shadows
        found = true;
        TEST_ASSERT_EQUAL_INT(want[w].metric, s_health_reads[j].metric);
        TEST_ASSERT_EQUAL_INT(want[w].mode, s_health_reads[j].mode);
      }
    }
    TEST_ASSERT_TRUE(found);
  }
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

void test_update_health_info_should_do_nothing_with_no_health_slots(void) {
  set_slots(kSlotsNoHealth);

  update_health_info();
  TEST_ASSERT_EQUAL_INT(0, mock_health_accessible_count);
  TEST_ASSERT_EQUAL_INT(0, mock_health_sum_today_count);
  TEST_ASSERT_EQUAL_INT(0, mock_health_peek_count);
}

void test_update_health_info_should_read_only_displayed_metrics(void) {
  set_slots(kSlotsOnlySteps);

  update_health_info();
  // Steps alone: one accessibility check, one sum; no sleep/active/HR reads.
  TEST_ASSERT_EQUAL_INT(1, mock_health_accessible_count);
  TEST_ASSERT_EQUAL_INT(1, mock_health_sum_today_count);
  TEST_ASSERT_EQUAL_INT(0, mock_health_peek_count);
}

// PebbleOS posts MovementUpdate per accel batch at motion rate; a step- or
// sleep-bearing slot turns every one into a render. HEALTH_EVENT_THROTTLE_S
// bounds that to one refresh per window.
void test_health_handler_should_throttle_movement_updates(void) {
  set_slots(kSlotsOnlySteps);

  health_handler(HealthEventMovementUpdate, NULL);
  TEST_ASSERT_EQUAL_INT(1, mock_health_accessible_count);
  int dirty_after_first = mock_mark_dirty_count;

  mock_time_offset += 3;  // inside the window: dropped, no work, no redraw
  health_handler(HealthEventMovementUpdate, NULL);
  TEST_ASSERT_EQUAL_INT(1, mock_health_accessible_count);
  TEST_ASSERT_EQUAL_INT(dirty_after_first, mock_mark_dirty_count);
}

void test_health_handler_should_throttle_each_event_type_separately(void) {
  set_slots(kSlotsAllHealth);

  health_handler(HealthEventMovementUpdate, NULL);
  TEST_ASSERT_EQUAL_INT(2, mock_health_accessible_count);
  mock_time_offset += 1;
  health_handler(HealthEventSleepUpdate, NULL);
  TEST_ASSERT_EQUAL_INT(3, mock_health_accessible_count);
}

void test_health_handler_should_refresh_again_after_the_throttle_window(void) {
  set_slots(kSlotsOnlySteps);

  health_handler(HealthEventMovementUpdate, NULL);
  TEST_ASSERT_EQUAL_INT(1, mock_health_accessible_count);

  mock_time_offset += HEALTH_EVENT_THROTTLE_S + 1;
  health_handler(HealthEventMovementUpdate, NULL);
  TEST_ASSERT_EQUAL_INT(2, mock_health_accessible_count);
}

// A posted HR reading is already fresh — stale BPM is worse than a redraw
// (ISSUES.md), so heart-rate events bypass the throttle.
void test_health_handler_should_not_throttle_heart_rate_updates(void) {
  set_slots(kSlotsBoot);

  health_handler(HealthEventMovementUpdate, NULL);  // opens a throttle window
  TEST_ASSERT_EQUAL_INT(1, mock_health_accessible_count);
  health_handler(HealthEventHeartRateUpdate, NULL);
  health_handler(HealthEventHeartRateUpdate, NULL);
  TEST_ASSERT_EQUAL_INT(3, mock_health_accessible_count);
}

// SignificantUpdate is the applib cache-invalidated signal — rare and
// load-bearing, so it never waits out the throttle window.
void test_health_handler_should_not_throttle_significant_updates(void) {
  set_slots(kSlotsOnlySteps);

  health_handler(HealthEventMovementUpdate, NULL);  // opens a throttle window
  TEST_ASSERT_EQUAL_INT(1, mock_health_accessible_count);
  health_handler(HealthEventSignificantUpdate, NULL);
  TEST_ASSERT_EQUAL_INT(2, mock_health_accessible_count);
}

static void reset_health_read_counts(void) {
  mock_health_accessible_count = 0;
  mock_health_sum_today_count = 0;
  mock_health_peek_count = 0;
}

void test_health_events_should_read_only_their_own_metrics(void) {
  set_slots(kSlotsAllHealth);

  // Movement: steps and active minutes.
  s_sleep_seconds = 111;
  s_heart_rate = 77;
  health_handler(HealthEventMovementUpdate, NULL);
  TEST_ASSERT_EQUAL_INT(2, mock_health_accessible_count);
  TEST_ASSERT_EQUAL_INT(2, mock_health_sum_today_count);
  TEST_ASSERT_EQUAL_INT(0, mock_health_peek_count);
  TEST_ASSERT_EQUAL_INT((int)mock_health_sum_today_value[HealthMetricStepCount], s_step_count);
  TEST_ASSERT_EQUAL_INT(111, s_sleep_seconds);
  TEST_ASSERT_EQUAL_INT(77, s_heart_rate);

  // Sleep: sleep alone.
  reset_health_read_counts();
  mock_time_offset += HEALTH_EVENT_THROTTLE_S + 1;
  health_handler(HealthEventSleepUpdate, NULL);
  TEST_ASSERT_EQUAL_INT(1, mock_health_accessible_count);
  TEST_ASSERT_EQUAL_INT(1, mock_health_sum_today_count);
  TEST_ASSERT_EQUAL_INT(0, mock_health_peek_count);
  TEST_ASSERT_EQUAL_INT((int)mock_health_sum_today_value[HealthMetricSleepSeconds],
                        s_sleep_seconds);
  TEST_ASSERT_EQUAL_INT(77, s_heart_rate);

  // Heart rate: HR alone, through the instant peek.
  reset_health_read_counts();
  mock_heart_rate = 64;
  health_handler(HealthEventHeartRateUpdate, NULL);
  TEST_ASSERT_EQUAL_INT(1, mock_health_accessible_count);
  TEST_ASSERT_EQUAL_INT(0, mock_health_sum_today_count);
  TEST_ASSERT_EQUAL_INT(1, mock_health_peek_count);
  TEST_ASSERT_EQUAL_INT(64, s_heart_rate);
  TEST_ASSERT_EQUAL_INT((int)mock_health_sum_today_value[HealthMetricSleepSeconds],
                        s_sleep_seconds);

  // Significant: everything.
  reset_health_read_counts();
  health_handler(HealthEventSignificantUpdate, NULL);
  TEST_ASSERT_EQUAL_INT(4, mock_health_accessible_count);
  TEST_ASSERT_EQUAL_INT(3, mock_health_sum_today_count);
  TEST_ASSERT_EQUAL_INT(1, mock_health_peek_count);
}

void test_health_events_the_face_does_not_use_should_read_nothing(void) {
  set_slots(kSlotsAllHealth);

  health_handler(HealthEventMetricAlert, NULL);
  TEST_ASSERT_EQUAL_INT(0, mock_health_accessible_count);
}

void test_minute_tick_should_read_every_health_metric(void) {
  set_slots(kSlotsAllHealth);

  struct tm t = {0};
  t.tm_min = 14;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(4, mock_health_accessible_count);
  TEST_ASSERT_EQUAL_INT(3, mock_health_sum_today_count);
  TEST_ASSERT_EQUAL_INT(1, mock_health_peek_count);
}

void test_undisplayed_health_metrics_should_read_as_no_data(void) {
  set_slots(kSlotsNoHealth);

  s_step_count = 4321;
  update_health_info();
  TEST_ASSERT_EQUAL_INT(-1, s_step_count);
}

void test_inbox_should_parse_weather_payload_and_persist(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, 0);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_AQI, 42);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_UV, 7);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_HUMIDITY, 55);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_PCP, 35);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_PRECIP_NOW, 25);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_HIGH, 82);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_LOW, 61);

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(72, s_weather_temp);
  TEST_ASSERT_EQUAL_INT(0, s_weather_cond_code);
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

void test_inbox_should_sentinel_out_of_range_semantic_ints_and_cap_display_widths(void) {
  // A torn/garbage wire int must never read as data or paint past the slot.
  // A semantic field out of range is "no reading"; only the layout's width
  // caps (UV, wind speed) clamp.
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 2000000000);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, -5);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_AQI, -5);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_UV, 999);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_HUMIDITY, 150);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_PCP, 101);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_WIND_DIRECTION, 720);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_WIND_SPEED, 28123);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_HIGH, -500);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_HI_HOUR_TODAY, 25);

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(WEATHER_TEMP_NONE, s_weather_temp);
  TEST_ASSERT_EQUAL_INT(WEATHER_NONE, s_weather_cond_code);
  TEST_ASSERT_EQUAL_STRING("--", weather_cond_word(s_weather_cond_code));
  TEST_ASSERT_EQUAL_INT(WEATHER_NONE, s_weather_aqi);
  TEST_ASSERT_EQUAL_INT(99, s_weather_uv);
  TEST_ASSERT_EQUAL_INT(WEATHER_NONE, s_weather_humidity);
  TEST_ASSERT_EQUAL_INT(WEATHER_NONE, s_weather_pcp);
  TEST_ASSERT_EQUAL_INT(WEATHER_NONE, s_weather_wind_direction);
  TEST_ASSERT_EQUAL_INT(999, s_weather_wind_speed);
  TEST_ASSERT_EQUAL_INT(WEATHER_TEMP_NONE, s_temp_high);
  TEST_ASSERT_EQUAL_INT(WEATHER_NONE, s_hi_hour_today);
  // An absent field keeps its sentinel.
  TEST_ASSERT_EQUAL_INT(WEATHER_NONE, s_precip_now);
}

void test_inbox_should_keep_weather_ints_at_the_edges_of_their_ranges(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, -99);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, 99);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_AQI, 500);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_HUMIDITY, 100);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_PCP, 0);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_WIND_DIRECTION, 360);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_HI_HOUR_TODAY, 23);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_LO_HOUR_TODAY, 0);

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(-99, s_weather_temp);
  TEST_ASSERT_EQUAL_INT(99, s_weather_cond_code);
  TEST_ASSERT_EQUAL_INT(500, s_weather_aqi);
  TEST_ASSERT_EQUAL_INT(100, s_weather_humidity);
  TEST_ASSERT_EQUAL_INT(0, s_weather_pcp);
  TEST_ASSERT_EQUAL_INT(360, s_weather_wind_direction);
  TEST_ASSERT_EQUAL_INT(23, s_hi_hour_today);
  TEST_ASSERT_EQUAL_INT(0, s_lo_hour_today);
}

void test_inbox_should_not_clamp_uv_above_11(void) {
  // The WHO scale's top band is literally "11+"; Open-Meteo returns 12-16 in
  // the tropics. The clamp guards layout, not the scale — a real 12 must
  // survive to the formatter and the cache.
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 32);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, 0);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_UV, 12);

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(12, s_weather_uv);
  TEST_ASSERT_EQUAL_INT(12, persist_read_int(PERSIST_KEY_WEATHER_UV));
}

void test_inbox_should_parse_narrow_width_weather_ints(void) {
  // The SDK sends 1-, 2- or 4-byte ints; the parse must not depend on the
  // sender picking the wide form.
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int_width(MESSAGE_KEY_WEATHER_TEMP, -5, 2);   // via the payload gate
  mock_dict_add_uint_width(MESSAGE_KEY_WEATHER_COND, 61, 1);  // gate's second half
  mock_dict_add_uint_width(MESSAGE_KEY_WEATHER_AQI, 42, 1);
  mock_dict_add_uint_width(MESSAGE_KEY_WEATHER_UV, 7, 2);
  mock_dict_add_int_width(MESSAGE_KEY_WEATHER_WIND_DIRECTION, 270, 2);

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(-5, s_weather_temp);
  TEST_ASSERT_EQUAL_INT(61, s_weather_cond_code);
  TEST_ASSERT_EQUAL_INT(42, s_weather_aqi);
  TEST_ASSERT_EQUAL_INT(7, s_weather_uv);
  TEST_ASSERT_EQUAL_INT(270, s_weather_wind_direction);
  TEST_ASSERT_TRUE(persist_exists(PERSIST_KEY_WEATHER_TIMESTAMP));
}

void test_inbox_should_parse_and_persist_tomorrow_low(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, 0);
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
  mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, 0);
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
  mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, 0);  // cache save needs a real payload
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
  mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, 0);
  s_weather_wind_direction = -1;

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(-1, s_weather_wind_direction);
}

void test_inbox_should_parse_and_persist_extreme_rollover_keys(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, 0);
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
  mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, 0);
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
  mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, 0);
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
}

void test_inbox_should_parse_and_persist_the_crt_effect_settings(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_CRT_ABERRATION, "1");  // Clay sends strings
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_CRT_CURVATURE, "1");
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_CRT_DEGAUSS, "1");
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_DEGAUSS_VOLUME, "25");
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(1, s_settings_crt_aberration);
  TEST_ASSERT_EQUAL_INT(1, s_settings_crt_curvature);
  TEST_ASSERT_EQUAL_INT(1, s_settings_crt_degauss);
  TEST_ASSERT_EQUAL_INT(25, s_settings_degauss_volume);
  TEST_ASSERT_EQUAL_INT(1, persist_read_int(PERSIST_KEY_SETTINGS_CRT_ABERRATION));
  TEST_ASSERT_EQUAL_INT(1, persist_read_int(PERSIST_KEY_SETTINGS_CRT_CURVATURE));
  TEST_ASSERT_EQUAL_INT(1, persist_read_int(PERSIST_KEY_SETTINGS_CRT_DEGAUSS));
  TEST_ASSERT_EQUAL_INT(25, persist_read_int(PERSIST_KEY_SETTINGS_DEGAUSS_VOLUME));

  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_SETTINGS_DEGAUSS_VOLUME, 3);  // a slider sends ints
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(3, s_settings_degauss_volume);
  TEST_ASSERT_EQUAL_INT(3, persist_read_int(PERSIST_KEY_SETTINGS_DEGAUSS_VOLUME));
  s_settings_degauss_volume = 25;
  persist_write_int(PERSIST_KEY_SETTINGS_DEGAUSS_VOLUME, 25);

  s_settings_crt_aberration = 0;
  s_settings_crt_curvature = 0;
  s_settings_crt_degauss = 0;
  s_settings_degauss_volume = 0;
  load_settings();
  TEST_ASSERT_EQUAL_INT(1, s_settings_crt_aberration);
  TEST_ASSERT_EQUAL_INT(1, s_settings_crt_curvature);
  TEST_ASSERT_EQUAL_INT(1, s_settings_crt_degauss);
  TEST_ASSERT_EQUAL_INT(25, s_settings_degauss_volume);
}

void test_load_settings_should_migrate_the_old_crt_switch_to_all_three_effects(void) {
  mock_persist_reset();
  persist_write_int(PERSIST_KEY_LEGACY_CRT, 1);
  load_settings();
  TEST_ASSERT_EQUAL_INT(1, s_settings_crt_aberration);
  TEST_ASSERT_EQUAL_INT(1, s_settings_crt_curvature);
  TEST_ASSERT_EQUAL_INT(1, s_settings_crt_degauss);
  TEST_ASSERT_EQUAL_INT(1, persist_read_int(PERSIST_KEY_SETTINGS_CRT_DEGAUSS));
  TEST_ASSERT_FALSE(persist_exists(PERSIST_KEY_LEGACY_CRT));

  // One-shot: a later launch finds nothing left to migrate.
  s_settings_crt_aberration = 0;
  load_settings();
  TEST_ASSERT_EQUAL_INT(1, s_settings_crt_aberration);
}

void test_load_settings_should_migrate_an_off_crt_switch_as_all_off(void) {
  mock_persist_reset();
  persist_write_int(PERSIST_KEY_LEGACY_CRT, 0);
  load_settings();
  TEST_ASSERT_EQUAL_INT(0, s_settings_crt_aberration);
  TEST_ASSERT_EQUAL_INT(0, s_settings_crt_curvature);
  TEST_ASSERT_EQUAL_INT(0, s_settings_crt_degauss);
  TEST_ASSERT_FALSE(persist_exists(PERSIST_KEY_LEGACY_CRT));
}

void test_load_settings_migration_should_not_override_the_new_effect_keys(void) {
  mock_persist_reset();
  persist_write_int(PERSIST_KEY_LEGACY_CRT, 1);
  persist_write_int(PERSIST_KEY_SETTINGS_CRT_CURVATURE, 0);
  persist_write_int(PERSIST_KEY_SETTINGS_DEGAUSS_VOLUME, 10);
  persist_write_int(PERSIST_KEY_LEGACY_CRT_SOUND, 1);
  load_settings();
  TEST_ASSERT_EQUAL_INT(0, s_settings_crt_aberration);
  TEST_ASSERT_EQUAL_INT(0, s_settings_crt_curvature);
  TEST_ASSERT_EQUAL_INT(0, s_settings_crt_degauss);
  TEST_ASSERT_EQUAL_INT(10, s_settings_degauss_volume);
  TEST_ASSERT_FALSE(persist_exists(PERSIST_KEY_LEGACY_CRT));
  TEST_ASSERT_FALSE(persist_exists(PERSIST_KEY_LEGACY_CRT_SOUND));
}

void test_load_settings_should_migrate_the_old_degauss_sound_to_a_volume(void) {
  mock_persist_reset();
  persist_write_int(PERSIST_KEY_LEGACY_CRT_SOUND, 1);
  load_settings();
  TEST_ASSERT_EQUAL_INT(10, s_settings_degauss_volume);
  TEST_ASSERT_EQUAL_INT(10, persist_read_int(PERSIST_KEY_SETTINGS_DEGAUSS_VOLUME));
  TEST_ASSERT_FALSE(persist_exists(PERSIST_KEY_LEGACY_CRT_SOUND));

  mock_persist_reset();
  s_settings_degauss_volume = 50;
  persist_write_int(PERSIST_KEY_LEGACY_CRT_SOUND, 0);
  load_settings();
  TEST_ASSERT_EQUAL_INT(0, s_settings_degauss_volume);
  TEST_ASSERT_FALSE(persist_exists(PERSIST_KEY_LEGACY_CRT_SOUND));
}

void test_load_settings_should_migrate_the_old_chime_switch_into_the_volume(void) {
  mock_persist_reset();
  persist_write_int(PERSIST_KEY_LEGACY_HOURLY_CHIME, 0);
  persist_write_int(PERSIST_KEY_SETTINGS_CHIME_VOLUME, 25);
  load_settings();
  TEST_ASSERT_EQUAL_INT(0, s_settings_chime_volume);
  TEST_ASSERT_EQUAL_INT(0, persist_read_int(PERSIST_KEY_SETTINGS_CHIME_VOLUME));
  TEST_ASSERT_FALSE(persist_exists(PERSIST_KEY_LEGACY_HOURLY_CHIME));

  mock_persist_reset();
  persist_write_int(PERSIST_KEY_LEGACY_HOURLY_CHIME, 1);
  persist_write_int(PERSIST_KEY_SETTINGS_CHIME_VOLUME, 25);
  load_settings();
  TEST_ASSERT_EQUAL_INT(25, s_settings_chime_volume);
  TEST_ASSERT_FALSE(persist_exists(PERSIST_KEY_LEGACY_HOURLY_CHIME));
}

void test_inbox_should_parse_and_persist_chime_volume_setting(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_CHIME_VOLUME, "75");  // Clay sends strings
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(75, s_settings_chime_volume);
  TEST_ASSERT_EQUAL_INT(75, persist_read_int(PERSIST_KEY_SETTINGS_CHIME_VOLUME));

  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_CHIME_VOLUME, "25");
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(25, s_settings_chime_volume);
  TEST_ASSERT_EQUAL_INT(25, persist_read_int(PERSIST_KEY_SETTINGS_CHIME_VOLUME));

  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_SETTINGS_CHIME_VOLUME, 3);  // a slider sends ints
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(3, s_settings_chime_volume);
  TEST_ASSERT_EQUAL_INT(3, persist_read_int(PERSIST_KEY_SETTINGS_CHIME_VOLUME));
  s_settings_chime_volume = 25;
  persist_write_int(PERSIST_KEY_SETTINGS_CHIME_VOLUME, 25);

  s_settings_chime_volume = 100;
  load_settings();
  TEST_ASSERT_EQUAL_INT(25, s_settings_chime_volume);
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
  TEST_ASSERT_TRUE(mock_outbox_has(MESSAGE_KEY_WEATHER_REQUEST, 0));

  // Same units again: no refetch
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_UNITS, "1");
  before = mock_outbox_sends;
  int writes = mock_outbox_write_count();
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);
  TEST_ASSERT_EQUAL_INT(writes, mock_outbox_write_count());
}

void test_inbox_weather_window_change_should_trigger_weather_refetch(void) {
  mock_persist_reset();

  // 12h -> 2h: the phone's reduction window changed, so the maxima must be
  // re-asked, not wait for the :00/:30 tick.
  s_settings_weather_window = 12;
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_WEATHER_WINDOW, "2");
  int before = mock_outbox_sends;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(2, s_settings_weather_window);
  TEST_ASSERT_EQUAL_INT(2, persist_read_int(PERSIST_KEY_SETTINGS_WEATHER_WINDOW));
  TEST_ASSERT_EQUAL_INT(before + 1, mock_outbox_sends);
  TEST_ASSERT_TRUE(mock_outbox_has(MESSAGE_KEY_WEATHER_REQUEST, 0));

  // Same window again: no refetch
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_WEATHER_WINDOW, "2");
  before = mock_outbox_sends;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);
}

void test_refresh_state_should_never_request_weather(void) {
  // Regression for the :00/:30 feedback loop: every weather reply ends in
  // refresh_state(); when the fetch trigger lived there, each reply re-armed
  // the request until the minute flipped — 20-60 fetches, twice an hour.
  int before = mock_outbox_sends;
  refresh_state();
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);
}

void test_request_weather_should_send_exactly_the_trigger_key(void) {
  // The outbox protocol is one content-free doorbell: key WEATHER_REQUEST,
  // value 0, nothing else. Guards against the key degenerating back into a
  // data-key overload — mock_outbox_sends alone can't see that.
  request_weather();
  TEST_ASSERT_EQUAL_INT(1, mock_outbox_write_count());
  TEST_ASSERT_TRUE(mock_outbox_has(MESSAGE_KEY_WEATHER_REQUEST, 0));
}

void test_refresh_state_should_refresh_the_hi_lo_phase_hour(void) {
  // The HI/LO rollover reads this global; refresh_state is its only writer.
  time_t now = time(NULL);
  int expected = localtime(&now)->tm_hour;
  s_wall_hour = (expected + 7) % 24;
  refresh_state();
  TEST_ASSERT_EQUAL_INT(expected, s_wall_hour);
}

void test_refresh_state_should_reformat_the_date_when_settings_change(void) {
  s_settings_dow_position = DOW_BEFORE;
  refresh_state();  // primes the format cache for (today, these settings)
  char with_dow[64];
  strcpy(with_dow, s_date_display);

  s_settings_dow_position = DOW_HIDDEN;
  refresh_state();
  TEST_ASSERT_TRUE(strcmp(with_dow, s_date_display) != 0);

  s_settings_dow_position = DOW_BEFORE;
  refresh_state();
  TEST_ASSERT_EQUAL_STRING(with_dow, s_date_display);
}

void test_refresh_state_should_keep_date_output_when_nothing_changes(void) {
  refresh_state();
  char once[64];
  char once_short[16];
  strcpy(once, s_date_display);
  strcpy(once_short, s_short_date_display);
  refresh_state();
  TEST_ASSERT_EQUAL_STRING(once, s_date_display);
  TEST_ASSERT_EQUAL_STRING(once_short, s_short_date_display);
}

void test_chime_gate_should_be_due_only_on_the_hour_when_enabled(void) {
  TEST_ASSERT_TRUE(chime_is_due(0, 5));
  TEST_ASSERT_FALSE(chime_is_due(0, 0));
  TEST_ASSERT_FALSE(chime_is_due(0, -10));
  TEST_ASSERT_FALSE(chime_is_due(29, 5));
}

void test_chime_should_beep_the_post_tone_once_at_the_full_hour(void) {
  s_settings_chime_volume = 5;

  struct tm t = {0};
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(1, mock_speaker_tone_count);
  TEST_ASSERT_EQUAL_UINT16(896, mock_speaker_tone_freq_hz);
  TEST_ASSERT_EQUAL_UINT32(500, mock_speaker_tone_duration_ms);
  TEST_ASSERT_EQUAL_UINT8(5, mock_speaker_tone_volume);
  TEST_ASSERT_EQUAL_INT(SpeakerWaveformSquare, mock_speaker_tone_waveform);
}

void test_chime_should_stay_silent_when_the_setting_is_off(void) {
  // The settings select ships '0' — no sound on the full hour by default.
  struct tm t = {0};
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(0, mock_speaker_tone_count);
}

void test_chime_should_stay_silent_off_the_full_hour(void) {
  s_settings_chime_volume = 5;

  struct tm t = {0};
  t.tm_min = 13;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(0, mock_speaker_tone_count);
}

void test_chime_should_stay_silent_when_the_speaker_is_muted(void) {
  s_settings_chime_volume = 5;
  mock_speaker_muted = true;

  struct tm t = {0};
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(0, mock_speaker_tone_count);
}

void test_chime_should_use_the_configured_volume(void) {
  s_settings_chime_volume = 75;

  struct tm t = {0};
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(1, mock_speaker_tone_count);
  TEST_ASSERT_EQUAL_UINT8(75, mock_speaker_tone_volume);
}

void test_chime_should_clamp_the_configured_volume_to_the_sdk_range(void) {
  struct tm t = {0};

  s_settings_chime_volume = 200;  // settings write arbitrary ints
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_UINT8(100, mock_speaker_tone_volume);

  s_settings_chime_volume = 150;
  t.tm_hour = 1;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(2, mock_speaker_tone_count);
  TEST_ASSERT_EQUAL_UINT8(100, mock_speaker_tone_volume);
}

void test_chime_effective_volume_matrix(void) {
  // Clamp-only: pass through in-range, cap at the SDK's 0..100.
  TEST_ASSERT_EQUAL_INT(0, chime_effective_volume(-10));
  TEST_ASSERT_EQUAL_INT(25, chime_effective_volume(25));
  TEST_ASSERT_EQUAL_INT(50, chime_effective_volume(50));
  TEST_ASSERT_EQUAL_INT(70, chime_effective_volume(70));
  TEST_ASSERT_EQUAL_INT(85, chime_effective_volume(85));
  TEST_ASSERT_EQUAL_INT(100, chime_effective_volume(200));
}

void test_inbox_chime_test_should_beep_once_at_the_volume_in_the_same_dict(void) {
  // The test button submits the whole Clay form, so CHIME_TEST arrives next to
  // the (possibly fresh) SETTINGS_CHIME_VOLUME. The watch must apply the volume
  // in the settings walk first, then beep at it — a branch placed before the
  // walk would beep at the stale volume.
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_CHIME_VOLUME, "75");  // Clay sends strings
  mock_dict_add_cstring(MESSAGE_KEY_CHIME_TEST, "1");
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(1, mock_speaker_tone_count);
  TEST_ASSERT_EQUAL_UINT8(75, mock_speaker_tone_volume);
  TEST_ASSERT_EQUAL_UINT16(896, mock_speaker_tone_freq_hz);
  TEST_ASSERT_EQUAL_UINT32(500, mock_speaker_tone_duration_ms);
  TEST_ASSERT_EQUAL_INT(SpeakerWaveformSquare, mock_speaker_tone_waveform);
  // CHIME_TEST is an action, not a setting: it must never persist.
  TEST_ASSERT_EQUAL_INT(1, mock_persist_write_count);  // the volume write, nothing else
}

void test_inbox_should_never_beep_on_a_plain_save(void) {
  // A settings-only dict without CHIME_TEST, and — the steady state after the
  // JS side stamps the flag back to '0' — CHIME_TEST explicitly '0': neither
  // may sound, or every ordinary Save Settings would beep.
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_CHIME_VOLUME, "75");
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(0, mock_speaker_tone_count);

  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_CHIME_VOLUME, "75");
  mock_dict_add_cstring(MESSAGE_KEY_CHIME_TEST, "0");
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(0, mock_speaker_tone_count);
}

void test_inbox_chime_test_should_bypass_mute(void) {
  // Owner decision: the test tone ignores speaker_is_muted() — the user asked
  // for the beep explicitly; a silent test reads as broken.
  mock_speaker_muted = true;
  s_settings_chime_volume = 5;
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_CHIME_TEST, "1");
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(1, mock_speaker_tone_count);
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
  TEST_ASSERT_TRUE(mock_outbox_has(MESSAGE_KEY_WEATHER_REQUEST, 0));

  t.tm_min = 0;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(before + 2, mock_outbox_sends);
}

void test_tick_handler_should_skip_weather_with_no_weather_slots(void) {
  set_slots(kSlotsNoWeather);

  struct tm t = {0};
  t.tm_min = 30;
  int before = mock_outbox_sends;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);

  s_complication_slots[0].source = DATA_SOURCE_WEATHER;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(before + 1, mock_outbox_sends);
}

void test_wind_slot_should_join_the_weather_fetch_gate(void) {
  // A wind-only layout must fetch on the :00/:30 edge like any other
  // weather reading, or the arrow never leaves "--".
  set_slots(kSlotsNoWeather);

  struct tm t = {0};
  t.tm_min = 30;
  int before = mock_outbox_sends;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);

  s_complication_slots[2].source = DATA_SOURCE_WIND;  // Bottom Left
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(before + 1, mock_outbox_sends);
}

void test_inbox_should_fetch_when_a_weather_slot_first_appears(void) {
  set_slots(kSlotsNoWeather);

  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SLOT_1, "16");  // DATA_SOURCE_AQI
  int before = mock_outbox_sends;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(before + 1, mock_outbox_sends);
  TEST_ASSERT_TRUE(mock_outbox_has(MESSAGE_KEY_WEATHER_REQUEST, 0));

  // Already showing weather: the same push again must not refetch, and a
  // weather reply (no SLOT_* keys) must not either — that is the :00/:30 loop
  // reappearing by another route.
  before = mock_outbox_sends;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);

  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, 0);
  before = mock_outbox_sends;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);
}

void test_inbox_should_fetch_when_a_slot_changes_with_weather_already_shown(void) {
  // Defaults show WEATHER in slot 0: weather is already needed, so the
  // first-appears trigger can't fire — an actual assignment change must.
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SLOT_2, "28");  // sleep -> PCP
  int before = mock_outbox_sends;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(before + 1, mock_outbox_sends);
  TEST_ASSERT_TRUE(mock_outbox_has(MESSAGE_KEY_WEATHER_REQUEST, 0));

  // Re-pushing an unchanged assignment stays silent
  before = mock_outbox_sends;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);
}

void test_dropped_weather_request_should_retry_a_bounded_number_of_times(void) {
  s_weather_request_retries = 0;
  int before = mock_outbox_sends;

  // Each failure arms a one-shot 5s retry; the mock timer never fires on its
  // own, so the test drives the armed callback.
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);
  TEST_ASSERT_EQUAL_UINT32(WEATHER_REQUEST_RETRY_MS, mock_timer_last_ms);
  TEST_ASSERT_EQUAL_PTR(weather_retry_callback, mock_timer_callback);
  mock_timer_callback(NULL);
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);
  TEST_ASSERT_EQUAL_PTR(weather_retry_callback, mock_timer_callback);
  mock_timer_callback(NULL);
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);  // exhausted: no third

  TEST_ASSERT_EQUAL_INT(WEATHER_REQUEST_MAX_RETRIES, s_weather_request_retries);
  TEST_ASSERT_EQUAL_INT(before + WEATHER_REQUEST_MAX_RETRIES, mock_outbox_sends);

  outbox_sent_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(0, s_weather_request_retries);
}

void test_answered_weather_request_should_rearm_the_retry_budget(void) {
  // Burn the budget: two retries fire, the next failure doesn't even arm.
  // Without the reset on a landed payload the 5s retry then stayed dead for
  // the rest of the app's life.
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);
  mock_timer_callback(NULL);
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);
  mock_timer_callback(NULL);
  int arms = mock_timer_register_count;
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);
  TEST_ASSERT_EQUAL_INT(WEATHER_REQUEST_MAX_RETRIES, s_weather_request_retries);
  TEST_ASSERT_EQUAL_INT(arms, mock_timer_register_count);

  // A landed payload proves the loop works — the budget must come back.
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, 0);
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(0, s_weather_request_retries);

  // A fresh failure arms the 5s retry again, and firing it re-sends.
  int sends = mock_outbox_sends;
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);
  TEST_ASSERT_EQUAL_INT(0, s_weather_request_retries);
  TEST_ASSERT_EQUAL_INT(arms + 1, mock_timer_register_count);
  TEST_ASSERT_EQUAL_UINT32(WEATHER_REQUEST_RETRY_MS, mock_timer_last_ms);
  TEST_ASSERT_EQUAL_PTR(weather_retry_callback, mock_timer_callback);
  mock_timer_callback(NULL);
  TEST_ASSERT_EQUAL_INT(sends + 1, mock_outbox_sends);
  TEST_ASSERT_EQUAL_INT(1, s_weather_request_retries);
}

void test_fresh_request_should_cancel_an_armed_retry_timer(void) {
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);
  TEST_ASSERT_NOT_NULL(s_weather_retry_timer);
  int cancels = mock_timer_cancel_count;
  request_weather();
  TEST_ASSERT_EQUAL_INT(cancels + 1, mock_timer_cancel_count);
  TEST_ASSERT_NULL(s_weather_retry_timer);
}

void test_landed_payload_should_cancel_an_armed_retry_timer(void) {
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);
  int cancels = mock_timer_cancel_count;
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, 0);
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(cancels + 1, mock_timer_cancel_count);
  TEST_ASSERT_NULL(s_weather_retry_timer);
}

void test_retry_reset_should_not_cancel_when_no_timer_is_armed(void) {
  weather_retries_reset();
  TEST_ASSERT_EQUAL_INT(0, mock_timer_cancel_count);
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);
  mock_timer_callback(NULL);
  TEST_ASSERT_NULL(s_weather_retry_timer);
  weather_retries_reset();
  TEST_ASSERT_EQUAL_INT(0, mock_timer_cancel_count);
}

void test_second_failure_should_supersede_the_armed_retry(void) {
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);
  int cancels = mock_timer_cancel_count;
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);
  TEST_ASSERT_EQUAL_INT(cancels + 1, mock_timer_cancel_count);
}

void test_weather_cache_should_skip_rewrite_when_payload_is_unchanged(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, 0);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_FETCHED_AT, (int32_t)(time(NULL) - 60));
  inbox_received_callback(NULL, NULL);

  int writes = mock_persist_write_count;
  inbox_received_callback(NULL, NULL);  // identical payload, same fetch time
  TEST_ASSERT_EQUAL_INT(writes, mock_persist_write_count);

  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, 0);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_FETCHED_AT, (int32_t)(time(NULL) - 30));
  inbox_received_callback(NULL, NULL);  // a newer fetch rewrites the stamp alone
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

// init() is safely callable under the mocks: every layer/window handle it
// touches is a nullable sentinel, and the persist mock starts empty.
void test_init_should_fetch_weather_when_the_cache_is_stale(void) {
  // Boot layout shows WEATHER; an absent cache must cost exactly one fetch.
  TEST_ASSERT_EQUAL_INT(0, mock_outbox_sends);
  init();
  TEST_ASSERT_EQUAL_INT(1, mock_outbox_sends);
}

void test_init_should_not_fetch_when_the_cache_is_fresh(void) {
  persist_write_int(PERSIST_KEY_WEATHER_TIMESTAMP, (int32_t)time(NULL));
  init();
  TEST_ASSERT_EQUAL_INT(0, mock_outbox_sends);
}

void test_init_should_not_fetch_without_a_weather_slot(void) {
  // load_settings() reads an empty persist mock, so the layout a test set
  // beforehand survives init().
  set_slots(kSlotsNoWeather);
  init();
  TEST_ASSERT_EQUAL_INT(0, mock_outbox_sends);
}

void test_request_weather_should_drop_quietly_when_the_outbox_is_unavailable(void) {
  // A NULL outbox iterator loses the race with the JS runtime; the bounded
  // retry in main.c owns recovery, request_weather itself just returns.
  mock_outbox_begin_ok = false;
  request_weather();
  TEST_ASSERT_EQUAL_INT(0, mock_outbox_sends);
}

void test_init_should_subscribe_only_what_the_default_layout_uses(void) {
  init();
  TEST_ASSERT_EQUAL_INT(1, mock_tick_subscribe_count);
  TEST_ASSERT_EQUAL_INT(MINUTE_UNIT, mock_tick_units);
  TEST_ASSERT_EQUAL_INT(1, mock_connection_subscribe_count);
  TEST_ASSERT_EQUAL_INT(1, mock_health_subscribe_count);
  TEST_ASSERT_EQUAL_INT(0, mock_battery_subscribe_count);
  TEST_ASSERT_EQUAL_INT(0, mock_backlight_subscribe_count);
  TEST_ASSERT_NULL(mock_app_focus_handlers.did_focus);
}

void test_init_should_subscribe_the_crt_services_when_degauss_is_on(void) {
  s_settings_crt_degauss = 1;
  init();
  TEST_ASSERT_EQUAL_INT(1, mock_backlight_subscribe_count);
  TEST_ASSERT_EQUAL_INT(1, mock_accel_tap_subscribe_count);
  TEST_ASSERT_NOT_NULL(mock_app_focus_handlers.did_focus);
  TEST_ASSERT_NOT_NULL(mock_app_focus_handlers.will_focus);
  // Launch focus still arrives through did_focus.
  TEST_ASSERT_FALSE(s_app_in_focus);
}

void test_init_should_not_subscribe_the_crt_services_for_effects_without_a_strike(void) {
  s_settings_crt_aberration = 1;
  s_settings_crt_curvature = 1;
  init();
  TEST_ASSERT_EQUAL_INT(0, mock_backlight_subscribe_count);
  TEST_ASSERT_EQUAL_INT(0, mock_accel_tap_subscribe_count);
  TEST_ASSERT_NULL(mock_app_focus_handlers.did_focus);
}

void test_init_should_subscribe_the_battery_when_a_slot_shows_it(void) {
  mock_battery_percent = 37;
  s_complication_slots[1].source = DATA_SOURCE_BATTERY_BAR;
  init();
  TEST_ASSERT_EQUAL_INT(1, mock_battery_subscribe_count);
  TEST_ASSERT_EQUAL_INT(37, s_battery_level);
}

void test_turning_the_crt_on_mid_run_should_subscribe_and_let_the_next_wake_strike(void) {
  init();
  TEST_ASSERT_EQUAL_INT(0, mock_backlight_subscribe_count);

  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_CRT_DEGAUSS, "1");
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(1, mock_backlight_subscribe_count);
  TEST_ASSERT_NOT_NULL(mock_backlight_handler);
  TEST_ASSERT_NOT_NULL(mock_app_focus_handlers.did_focus);

  mock_accel_tap_handler(ACCEL_AXIS_X, 1);
  mock_backlight_handler(true);
  TEST_ASSERT_TRUE(s_strike_pending);

  // Unrelated pushes leave the subscription alone.
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_THEME, "1");
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(1, mock_backlight_subscribe_count);
  TEST_ASSERT_EQUAL_INT(0, mock_backlight_unsubscribe_count);
}

void test_turning_the_crt_off_should_unsubscribe_and_drop_a_pending_strike(void) {
  s_settings_crt_degauss = 1;
  init();
  mock_app_focus_handlers.did_focus(true);
  mock_accel_tap_handler(ACCEL_AXIS_X, 1);
  mock_backlight_handler(true);
  TEST_ASSERT_TRUE(s_strike_pending);

  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_CRT_DEGAUSS, "0");
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_FALSE(s_strike_pending);
  TEST_ASSERT_EQUAL_INT(1, mock_backlight_unsubscribe_count);
  TEST_ASSERT_EQUAL_INT(1, mock_accel_tap_unsubscribe_count);
  TEST_ASSERT_EQUAL_INT(1, mock_app_focus_unsubscribe_count);
  TEST_ASSERT_NULL(mock_backlight_handler);
}

void test_removing_every_health_slot_should_unsubscribe_health(void) {
  init();
  TEST_ASSERT_EQUAL_INT(1, mock_health_subscribe_count);

  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_SLOT_2, DATA_SOURCE_DATE);
  mock_dict_add_int(MESSAGE_KEY_SLOT_3, DATA_SOURCE_DATE);
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(0, mock_health_unsubscribe_count);  // HR still shown

  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_SLOT_4, DATA_SOURCE_DATE);
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(1, mock_health_unsubscribe_count);

  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_SLOT_4, DATA_SOURCE_STEPS);
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(2, mock_health_subscribe_count);
}

void test_adding_a_battery_slot_should_subscribe_and_show_the_current_level(void) {
  init();
  TEST_ASSERT_EQUAL_INT(0, mock_battery_subscribe_count);
  mock_battery_percent = 42;
  mock_battery_charging = true;

  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_SLOT_2, DATA_SOURCE_BATTERY);
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(1, mock_battery_subscribe_count);
  TEST_ASSERT_EQUAL_INT(42, s_battery_level);
  TEST_ASSERT_TRUE(s_battery_charging);

  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_SLOT_2, DATA_SOURCE_BATTERY_BAR);
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(1, mock_battery_subscribe_count);
  TEST_ASSERT_EQUAL_INT(0, mock_battery_unsubscribe_count);

  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_SLOT_2, DATA_SOURCE_SLEEP);
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(1, mock_battery_unsubscribe_count);
}

void test_wanted_subscriptions_should_follow_slots_and_settings(void) {
  set_slots(kSlotsNoHealth);
  SubscriptionSet want = wanted_subscriptions();
  TEST_ASSERT_FALSE(want.battery);
  TEST_ASSERT_FALSE(want.health);
  TEST_ASSERT_FALSE(want.crt);

  set_slots(kSlotsAllHealth);
  s_settings_crt_degauss = 1;
  s_complication_slots[4].source = DATA_SOURCE_BATTERY;
  want = wanted_subscriptions();
  TEST_ASSERT_TRUE(want.battery);
  TEST_ASSERT_TRUE(want.health);
  TEST_ASSERT_TRUE(want.crt);
}

void test_init_should_register_appmessage_handlers(void) {
  init();
  TEST_ASSERT_EQUAL_INT(1, mock_inbox_received_count);
  TEST_ASSERT_EQUAL_INT(1, mock_inbox_dropped_count);
  TEST_ASSERT_EQUAL_INT(1, mock_outbox_sent_count);
  TEST_ASSERT_EQUAL_INT(1, mock_outbox_failed_count);
}

void test_window_load_should_subscribe_unobstructed_area(void) {
  main_window_load(NULL);
  TEST_ASSERT_EQUAL_INT(1, mock_unobstructed_subscribe_count);
}

void test_tuple_get_int_should_parse_every_wire_width(void) {
  // Clay/AppMessage deliver whichever integer width fits the value; the
  // parser must honor 1-, 2- and 4-byte arms of both signednesses.
  mock_dict_reset();
  mock_dict_add_int_width(MESSAGE_KEY_SLOT_1, 5, 1);
  TEST_ASSERT_EQUAL_INT(5, tuple_get_int(dict_find(NULL, MESSAGE_KEY_SLOT_1)));

  mock_dict_reset();
  mock_dict_add_int_width(MESSAGE_KEY_SLOT_1, 2000, 2);
  TEST_ASSERT_EQUAL_INT(2000, tuple_get_int(dict_find(NULL, MESSAGE_KEY_SLOT_1)));

  mock_dict_reset();
  mock_dict_add_int_width(MESSAGE_KEY_SLOT_1, 70000, 4);
  TEST_ASSERT_EQUAL_INT(70000, tuple_get_int(dict_find(NULL, MESSAGE_KEY_SLOT_1)));

  mock_dict_reset();
  mock_dict_add_uint_width(MESSAGE_KEY_SLOT_1, 200, 1);
  TEST_ASSERT_EQUAL_INT(200, tuple_get_int(dict_find(NULL, MESSAGE_KEY_SLOT_1)));

  mock_dict_reset();
  mock_dict_add_uint_width(MESSAGE_KEY_SLOT_1, 600, 2);
  TEST_ASSERT_EQUAL_INT(600, tuple_get_int(dict_find(NULL, MESSAGE_KEY_SLOT_1)));

  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SLOT_1, "1234");
  TEST_ASSERT_EQUAL_INT(1234, tuple_get_int(dict_find(NULL, MESSAGE_KEY_SLOT_1)));

  // A width nobody sends reads as 0, not garbage.
  mock_dict_reset();
  mock_dict_add_int_width(MESSAGE_KEY_SLOT_1, 33, 8);
  TEST_ASSERT_EQUAL_INT(0, tuple_get_int(dict_find(NULL, MESSAGE_KEY_SLOT_1)));
}

void test_update_health_info_should_fall_back_to_sentinels_without_permission(void) {
  // Boot layout keeps STEPS in slot 2 and BPM in slot 3 throughout.
  mock_health_accessible[HealthMetricStepCount] = HealthServiceAccessibilityMaskNoPermission;
  s_step_count = 4321;  // stale value: a denied metric must clear it
  update_health_info();
  TEST_ASSERT_EQUAL_INT(-1, s_step_count);

  mock_health_accessible[HealthMetricSleepSeconds] = HealthServiceAccessibilityMaskNoPermission;
  s_complication_slots[1].source = DATA_SOURCE_SLEEP;
  s_sleep_seconds = 7 * 3600;
  update_health_info();
  TEST_ASSERT_EQUAL_INT(-1, s_sleep_seconds);

  // HR is the instant-read divergence: denied, the code must not even peek.
  mock_health_accessible[HealthMetricHeartRateBPM] = HealthServiceAccessibilityMaskNoPermission;
  s_heart_rate = 55;
  int peeks = mock_health_peek_count;
  update_health_info();
  TEST_ASSERT_EQUAL_INT(0, s_heart_rate);
  TEST_ASSERT_EQUAL_INT(peeks, mock_health_peek_count);

  mock_health_accessible[HealthMetricActiveSeconds] = HealthServiceAccessibilityMaskNoPermission;
  s_complication_slots[4].source = DATA_SOURCE_ACTIVE_MINUTES;
  s_active_minutes = 42;  // stale value: a denied metric must clear it
  update_health_info();
  TEST_ASSERT_EQUAL_INT(-1, s_active_minutes);
}

void test_clock_should_follow_the_12h_24h_settings(void) {
  main_window_load(NULL);
  mock_clock_24h = true;
  refresh_state();
  TEST_ASSERT_EQUAL_INT(5, (int)strlen(mock_last_text));
  TEST_ASSERT_EQUAL_INT(':', mock_last_text[2]);

  // 12-hour form drops the leading zero. Pin the wall clock at 09:05 local
  // so the strip branch cannot hide: "%I:%M" would give "09:05".
  mock_clock_24h = false;
  time_t now = time(NULL);
  struct tm* lt = localtime(&now);
  int now_min = lt->tm_hour * 60 + lt->tm_min;
  int want_min = 9 * 60 + 5;
  mock_time_offset += (time_t)((want_min - now_min + 24 * 60) % (24 * 60)) * 60 - lt->tm_sec;
  refresh_state();
  TEST_ASSERT_EQUAL_INT(4, (int)strlen(mock_last_text));
  TEST_ASSERT_EQUAL_INT('9', mock_last_text[0]);
  TEST_ASSERT_EQUAL_INT(':', mock_last_text[1]);
}

void test_empty_and_unknown_sources_should_draw_nothing(void) {
  char buf[16];
  get_source_data(DATA_SOURCE_EMPTY, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("", buf);
  get_source_data((ComplicationDataSource)42, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("", buf);

  TEST_ASSERT_NULL(complication_spec(DATA_SOURCE_EMPTY)->draw);
  TEST_ASSERT_NULL(complication_spec((ComplicationDataSource)42));

  // Canvas level: a vacated bottom-left draws neither frame title nor value —
  // no text run may land inside its rect.
  test_apply_theme();
  s_complication_slots[2].source = DATA_SOURCE_EMPTY;
  mock_text_runs_reset();
  s_scene_cache_valid = false;
  canvas_update_proc(NULL, NULL);
  GRect vacated = s_complication_slots[2].box_rect;
  for (int i = 0; i < mock_text_run_count; i++) {
    GRect b = mock_text_run_boxes[i];
    bool inside = b.origin.x >= vacated.origin.x &&
                  b.origin.x + b.size.w <= vacated.origin.x + vacated.size.w &&
                  b.origin.y >= vacated.origin.y &&
                  b.origin.y + b.size.h <= vacated.origin.y + vacated.size.h;
    TEST_ASSERT_FALSE(inside);
  }
}

void test_unknown_source_should_render_only_the_placeholder_frame(void) {
  // A persisted id this build doesn't know (downgrade, retired slot) still
  // gets its frame titled "???" — and nothing else: no value runs land in
  // its rect.
  test_apply_theme();
  s_complication_slots[2].source = (ComplicationDataSource)42;
  mock_text_runs_reset();
  s_scene_cache_valid = false;
  canvas_update_proc(NULL, NULL);
  GRect box = s_complication_slots[2].box_rect;
  int overlapping = 0;
  for (int i = 0; i < mock_text_run_count; i++) {
    GRect b = mock_text_run_boxes[i];
    bool touches = b.origin.x < box.origin.x + box.size.w && b.origin.x + b.size.w > box.origin.x &&
                   b.origin.y < box.origin.y + box.size.h && b.origin.y + b.size.h > box.origin.y;
    if (!touches) continue;
    TEST_ASSERT_EQUAL_STRING("???", mock_text_runs[i]);
    overlapping++;
  }
  TEST_ASSERT_EQUAL_INT(1, overlapping);
}

void test_update_health_info_should_land_each_metric_on_its_own_target(void) {
  // All four health-backed sources visible (boot slots hold SLEEP, STEPS,
  // HR; slot 0 takes ACTV). The mock's distinct per-metric defaults make
  // wrong-metric wiring land a tell-tale value, and the divisor in
  // update_health_info's table shows up as the seconds → minutes step.
  s_complication_slots[0].source = DATA_SOURCE_ACTIVE_MINUTES;
  mock_heart_rate = 72;
  update_health_info();
  TEST_ASSERT_EQUAL_INT((int)mock_health_sum_today_value[HealthMetricStepCount], s_step_count);
  TEST_ASSERT_EQUAL_INT((int)mock_health_sum_today_value[HealthMetricSleepSeconds],
                        s_sleep_seconds);
  TEST_ASSERT_EQUAL_INT((int)mock_health_sum_today_value[HealthMetricActiveSeconds] / 60,
                        s_active_minutes);
  TEST_ASSERT_EQUAL_INT(72, s_heart_rate);
}

void test_inbox_should_land_every_field_of_a_full_weather_payload(void) {
  // The exact key set weather.js WEATHER_FIELDS emits — including HI/LO
  // hours — in one message. MOCK_DICT_MAX covers it with headroom precisely
  // so this shape is stageable; distinct values catch cross-wired rows.
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, 61);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_AQI, 42);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_UV, 5);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_HUMIDITY, 55);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_WIND_DIRECTION, 270);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_WIND_SPEED, 12);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_PCP, 35);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_PRECIP_NOW, 25);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_HIGH, 82);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_LOW, 61);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_LOW_TOMORROW, 55);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP_HIGH_TOMORROW, 77);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_HI_HOUR_TODAY, 15);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_LO_HOUR_TODAY, 5);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_HI_HOUR_TOMORROW, 14);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_LO_HOUR_TOMORROW, 4);
  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(72, s_weather_temp);
  TEST_ASSERT_EQUAL_INT(61, s_weather_cond_code);
  TEST_ASSERT_EQUAL_INT(42, s_weather_aqi);
  TEST_ASSERT_EQUAL_INT(5, s_weather_uv);
  TEST_ASSERT_EQUAL_INT(55, s_weather_humidity);
  TEST_ASSERT_EQUAL_INT(270, s_weather_wind_direction);
  TEST_ASSERT_EQUAL_INT(12, s_weather_wind_speed);
  TEST_ASSERT_EQUAL_INT(35, s_weather_pcp);
  TEST_ASSERT_EQUAL_INT(25, s_precip_now);
  TEST_ASSERT_EQUAL_INT(82, s_temp_high);
  TEST_ASSERT_EQUAL_INT(61, s_temp_low);
  TEST_ASSERT_EQUAL_INT(55, s_temp_low_tmrw);
  TEST_ASSERT_EQUAL_INT(77, s_temp_high_tmrw);
  TEST_ASSERT_EQUAL_INT(15, s_hi_hour_today);
  TEST_ASSERT_EQUAL_INT(5, s_lo_hour_today);
  TEST_ASSERT_EQUAL_INT(14, s_hi_hour_tmrw);
  TEST_ASSERT_EQUAL_INT(4, s_lo_hour_tmrw);
}

// --- CRT shader (crt.c) ------------------------------------------------------

// The mock GContext is opaque by design; the capture mock ignores it, so the
// tests pass any stable pointer.
static char s_fake_ctx_storage;
static GContext* s_fake_ctx = (GContext*)&s_fake_ctx_storage;

void test_crt_should_not_capture_the_framebuffer_while_disabled(void) {
  s_settings_crt_degauss = 0;
  memset(mock_framebuffer, 0xFF, sizeof(mock_framebuffer));
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(0, mock_fb_capture_count);
  TEST_ASSERT_EQUAL_HEX8(0xFF, mock_framebuffer[0]);  // untouched
}

// Quick View hides the bottom slot row; nothing reflows and the framebuffer
// keeps its size, so the table (built from the layout alone) stays put.
void test_crt_text_boxes_should_not_move_with_quick_view(void) {
  const CrtTextBox* boxes;
  crt_text_boxes(&boxes);
  CrtTextBox before[CRT_TEXT_BOXES];
  memcpy(before, boxes, sizeof(before));

  s_quick_view_active = true;
  s_settings_crt_degauss = 1;
  s_active_theme = &s_theme_shadow;
  memset(mock_framebuffer, 0xC0, sizeof(mock_framebuffer));
  crt_update_proc(NULL, s_fake_ctx);

  crt_text_boxes(&boxes);
  for (int i = 0; i < CRT_TEXT_BOXES; i++) {
    TEST_ASSERT_EQUAL_INT(before[i].x0, boxes[i].x0);
    TEST_ASSERT_EQUAL_INT(before[i].x1, boxes[i].x1);
    TEST_ASSERT_EQUAL_INT(before[i].y0, boxes[i].y0);
    TEST_ASSERT_EQUAL_INT(before[i].y1, boxes[i].y1);
    TEST_ASSERT_EQUAL_INT(before[i].sh, boxes[i].sh);
    TEST_ASSERT_EQUAL_INT(before[i].sv, boxes[i].sv);
  }
}

// A wrist shake and the backlight-on it causes: the look that strikes at once.
static void crt_wake_by_shake(void) {
  crt_tap_handler(ACCEL_AXIS_X, 1);
  crt_backlight_handler(true);
}

void test_crt_sound_should_play_only_when_enabled_and_unmuted(void) {
  s_settings_crt_degauss = 1;
  s_settings_degauss_volume = 0;
  crt_app_focus_did_handler(true);

  crt_wake_by_shake();  // volume Off: nothing
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(0, mock_speaker_play_tracks_count);

  s_settings_degauss_volume = 90;
  mock_speaker_muted = true;  // system mute / Quiet Time wins
  crt_wake_by_shake();
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(0, mock_speaker_play_tracks_count);

  mock_speaker_muted = false;
  crt_wake_by_shake();
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(1, mock_speaker_play_tracks_count);
  TEST_ASSERT_EQUAL_UINT32(1, mock_speaker_last_num_tracks);
  TEST_ASSERT_EQUAL_UINT8(90, mock_speaker_last_volume);
}

void test_crt_strike_should_not_prime_before_the_first_did_focus(void) {
  s_settings_crt_degauss = 1;
  s_settings_degauss_volume = 90;

  crt_backlight_handler(true);
  TEST_ASSERT_FALSE(s_strike_pending);

  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(CRT_FLASH_IDLE, s_flash_phase);
  TEST_ASSERT_EQUAL_INT(0, mock_timer_register_count);
  TEST_ASSERT_EQUAL_INT(0, mock_speaker_play_tracks_count);
}

void test_crt_strike_should_prime_after_the_first_did_focus(void) {
  s_settings_crt_degauss = 1;
  s_settings_degauss_volume = 90;

  crt_app_focus_did_handler(true);
  crt_wake_by_shake();
  TEST_ASSERT_TRUE(s_strike_pending);

  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(0, s_flash_phase);
  TEST_ASSERT_EQUAL_INT(1, mock_speaker_play_tracks_count);
}

void test_crt_strike_should_ignore_backlight_while_the_face_is_covered(void) {
  s_settings_crt_degauss = 1;
  s_settings_degauss_volume = 90;

  // A notification covering the face fires will_focus(false); the cover's
  // backlight flash belongs to the cover: never primed, never started, and
  // silent.
  crt_app_focus_will_handler(false);
  crt_backlight_handler(true);
  TEST_ASSERT_FALSE(s_strike_pending);
  TEST_ASSERT_EQUAL_INT(CRT_FLASH_IDLE, s_flash_phase);
  TEST_ASSERT_EQUAL_INT(0, mock_timer_register_count);

  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(CRT_FLASH_IDLE, s_flash_phase);
  TEST_ASSERT_EQUAL_INT(0, mock_timer_register_count);
  TEST_ASSERT_EQUAL_INT(0, mock_speaker_play_tracks_count);
}

void test_crt_strike_should_eat_the_dismissals_backlight(void) {
  s_settings_crt_degauss = 1;
  s_settings_degauss_volume = 90;

  // Focus-first dismissal order: did_focus(true) lands before the cover's
  // closing backlight-on. That flash belongs to the cover — the one-shot the
  // cover armed eats it: consumed, never primed, never started, and silent.
  crt_app_focus_will_handler(false);
  crt_app_focus_did_handler(true);
  TEST_ASSERT_TRUE(s_app_in_focus);
  TEST_ASSERT_TRUE(s_cover_suppresses_strike);
  TEST_ASSERT_NOT_NULL(s_cover_grace_timer);

  // Consuming the one-shot retires its grace timer; only that one was armed.
  crt_backlight_handler(true);
  TEST_ASSERT_FALSE(s_cover_suppresses_strike);
  TEST_ASSERT_NULL(s_cover_grace_timer);
  TEST_ASSERT_EQUAL_INT(1, mock_timer_cancel_count);
  TEST_ASSERT_FALSE(s_strike_pending);
  TEST_ASSERT_EQUAL_INT(CRT_FLASH_IDLE, s_flash_phase);

  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(CRT_FLASH_IDLE, s_flash_phase);
  TEST_ASSERT_EQUAL_INT(1, mock_timer_register_count);
  TEST_ASSERT_EQUAL_INT(0, mock_speaker_play_tracks_count);
}

// Fires the zero-delay timer the wake frame armed: phase 0 gets its repaint
// and the tick chain is armed behind it.
static void crt_fire_strike_start(void) {
  TEST_ASSERT_EQUAL_INT(0, s_flash_phase);
  TEST_ASSERT_EQUAL_UINT32(0, mock_timer_last_ms);
  TEST_ASSERT_EQUAL_PTR(crt_flash_start, mock_timer_callback);
  int dirty = mock_mark_dirty_count;
  mock_timer_callback(NULL);
  TEST_ASSERT_EQUAL_INT(0, s_flash_phase);
  TEST_ASSERT_EQUAL_INT(dirty + 1, mock_mark_dirty_count);
  TEST_ASSERT_EQUAL_UINT32(CRT_FLASH_TICK_MS, mock_timer_last_ms);
  TEST_ASSERT_EQUAL_PTR(crt_flash_tick, mock_timer_callback);
}

void test_crt_strike_should_fire_on_the_backlight_after_the_dismissal(void) {
  s_settings_crt_degauss = 1;
  s_settings_degauss_volume = 90;
  s_crt_layer = layer_create(GRect(0, 0, 200, 228));

  // Guard against the one-shot latching shut: with the dismissal's flash
  // eaten, the next backlight-on primes and plays as always.
  crt_app_focus_will_handler(false);
  crt_app_focus_did_handler(true);
  crt_backlight_handler(true);  // the dismissal's: eaten
  TEST_ASSERT_FALSE(s_strike_pending);

  crt_backlight_handler(false);
  crt_wake_by_shake();
  TEST_ASSERT_TRUE(s_strike_pending);
  crt_update_proc(NULL, s_fake_ctx);
  crt_fire_strike_start();
  TEST_ASSERT_EQUAL_INT(1, mock_speaker_play_tracks_count);
}

void test_crt_strike_should_keep_the_one_shot_through_a_covered_backlight(void) {
  s_settings_crt_degauss = 1;
  s_settings_degauss_volume = 90;

  // The cover's own edges land while the face is still covered — gated, and
  // they must NOT consume the one-shot: the dismissal's backlight-on can still
  // follow focus returning. It is eaten; the wake after it strikes.
  crt_app_focus_will_handler(false);
  crt_backlight_handler(true);
  crt_backlight_handler(false);
  TEST_ASSERT_TRUE(s_cover_suppresses_strike);
  TEST_ASSERT_FALSE(s_strike_pending);

  crt_app_focus_did_handler(true);
  crt_backlight_handler(true);
  TEST_ASSERT_FALSE(s_cover_suppresses_strike);
  TEST_ASSERT_FALSE(s_strike_pending);

  crt_backlight_handler(false);
  crt_wake_by_shake();
  TEST_ASSERT_TRUE(s_strike_pending);
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(0, s_flash_phase);
  TEST_ASSERT_EQUAL_INT(1, mock_speaker_play_tracks_count);
}

void test_crt_strike_should_drop_the_one_shot_once_the_backlight_goes_off_in_view(void) {
  s_settings_crt_degauss = 1;
  s_settings_degauss_volume = 90;

  // Dismissed while the cover's light is still on: no on-edge follows, so
  // nothing would consume the one-shot. Going off in view drops it, and the
  // next wake strikes.
  crt_app_focus_will_handler(false);
  crt_backlight_handler(true);
  crt_app_focus_did_handler(true);
  TEST_ASSERT_TRUE(s_cover_suppresses_strike);

  crt_backlight_handler(false);
  TEST_ASSERT_FALSE(s_cover_suppresses_strike);

  crt_wake_by_shake();
  TEST_ASSERT_TRUE(s_strike_pending);
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(0, s_flash_phase);
  TEST_ASSERT_EQUAL_INT(1, mock_speaker_play_tracks_count);
}

void test_crt_strike_should_ignore_backlight_while_quick_view_is_up(void) {
  s_settings_crt_degauss = 1;
  s_settings_degauss_volume = 90;
  crt_app_focus_did_handler(true);

  // Quick View takes no focus, so Back's dismissal backlight-on arrives in
  // focus while the peek is still up: it primes nothing and plays nothing.
  s_quick_view_active = true;
  crt_backlight_handler(true);
  TEST_ASSERT_FALSE(s_strike_pending);
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(CRT_FLASH_IDLE, s_flash_phase);
  TEST_ASSERT_EQUAL_INT(0, mock_speaker_play_tracks_count);

  s_quick_view_active = false;
  crt_backlight_handler(false);
  crt_wake_by_shake();
  TEST_ASSERT_TRUE(s_strike_pending);
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(1, mock_speaker_play_tracks_count);
}

void test_crt_strike_should_not_spend_the_one_shot_on_a_quick_view_backlight(void) {
  s_settings_crt_degauss = 1;
  crt_app_focus_will_handler(false);
  crt_app_focus_did_handler(true);

  s_quick_view_active = true;
  crt_backlight_handler(true);
  TEST_ASSERT_TRUE(s_cover_suppresses_strike);
  s_quick_view_active = false;
}

void test_crt_strike_should_expire_the_one_shot_after_the_grace_window(void) {
  s_settings_crt_degauss = 1;
  s_settings_degauss_volume = 90;

  // A cover that closes with no backlight edge never consumes the one-shot;
  // the grace timer armed when focus returned expires it, so the next real
  // wake strikes.
  crt_app_focus_will_handler(false);
  crt_app_focus_did_handler(true);
  TEST_ASSERT_TRUE(s_cover_suppresses_strike);
  TEST_ASSERT_EQUAL_INT(1, mock_timer_register_count);
  TEST_ASSERT_EQUAL_UINT32(CRT_COVER_GRACE_MS, mock_timer_last_ms);
  TEST_ASSERT_EQUAL_PTR(crt_cover_grace_expire, mock_timer_callback);

  mock_timer_callback(NULL);
  TEST_ASSERT_FALSE(s_cover_suppresses_strike);
  TEST_ASSERT_NULL(s_cover_grace_timer);

  crt_wake_by_shake();
  TEST_ASSERT_TRUE(s_strike_pending);
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(1, mock_speaker_play_tracks_count);
}

void test_crt_strike_should_cancel_a_pending_grace_when_a_new_cover_arrives(void) {
  s_settings_crt_degauss = 1;
  crt_app_focus_will_handler(false);
  crt_app_focus_did_handler(true);
  TEST_ASSERT_NOT_NULL(s_cover_grace_timer);

  // The old cover's timer must not expire the new cover's one-shot.
  int cancelled = mock_timer_cancel_count;
  crt_app_focus_will_handler(false);
  TEST_ASSERT_EQUAL_INT(cancelled + 1, mock_timer_cancel_count);
  TEST_ASSERT_NULL(s_cover_grace_timer);
  TEST_ASSERT_TRUE(s_cover_suppresses_strike);
}

void test_crt_strike_synth_should_sound_and_end_in_exact_silence(void) {
  static int8_t pcm[CRT_STRIKE_PCM_SAMPLES];
  crt_strike_synth(pcm, CRT_STRIKE_PCM_SAMPLES);
  int peak = 0;
  for (int i = 0; i < CRT_STRIKE_PCM_SAMPLES; i++) peak = pcm[i] > peak ? pcm[i] : peak;
  TEST_ASSERT_GREATER_THAN_INT(19, peak);
  for (int i = CRT_STRIKE_PCM_SAMPLES - CRT_STRIKE_TAIL_SAMPLES; i < CRT_STRIKE_PCM_SAMPLES; i++) {
    TEST_ASSERT_EQUAL_INT8(0, pcm[i]);
  }
}

void test_crt_strike_synth_should_keep_its_waveform_whatever_the_frame_rate(void) {
  static int8_t pcm[CRT_STRIKE_PCM_SAMPLES];
  crt_strike_synth(pcm, CRT_STRIKE_PCM_SAMPLES);
  uint32_t hash = 2166136261u;
  for (int i = 0; i < CRT_STRIKE_PCM_SAMPLES; i++) hash = (hash ^ (uint8_t)pcm[i]) * 16777619u;
  // FNV-1a of the PCM the 8-frame strike shipped with: the envelope reads the
  // curve's knots, not the frames.
  TEST_ASSERT_EQUAL_HEX32(0x58D7C850u, hash);
}

void test_crt_strike_pcm_should_last_as_long_as_the_animation_within_the_speaker_budget(void) {
  TEST_ASSERT_EQUAL_INT(CRT_FLASH_PHASES * CRT_FLASH_TICK_MS, CRT_STRIKE_PCM_MS);
  TEST_ASSERT_TRUE(CRT_STRIKE_PCM_SAMPLES <= 16 * 1024);
}

void test_crt_deinit_should_cancel_the_strike_and_drop_the_layer(void) {
  main_window_load(NULL);
  s_settings_crt_degauss = 1;
  crt_app_focus_did_handler(true);
  crt_wake_by_shake();
  crt_update_proc(NULL, s_fake_ctx);  // strike running: chain armed
  int registered = mock_timer_register_count;
  int cancelled = mock_timer_cancel_count;
  TEST_ASSERT_NOT_NULL(s_flash_timer);

  crt_deinit();
  TEST_ASSERT_EQUAL_INT(cancelled + 1, mock_timer_cancel_count);
  TEST_ASSERT_NULL(s_flash_timer);
  TEST_ASSERT_NULL(s_crt_layer);
  TEST_ASSERT_EQUAL_INT(CRT_FLASH_IDLE, s_flash_phase);
  TEST_ASSERT_FALSE(s_strike_pending);

  // A tick or backlight event arriving after unload touches no layer and
  // re-arms nothing.
  mock_mark_dirty_count = 0;
  crt_flash_tick(NULL);
  crt_backlight_handler(true);
  TEST_ASSERT_EQUAL_INT(0, mock_mark_dirty_count);
  TEST_ASSERT_EQUAL_INT(registered, mock_timer_register_count);
}

void test_crt_cover_should_not_eat_a_strike_while_the_effect_is_off(void) {
  s_settings_crt_degauss = 0;
  s_settings_degauss_volume = 90;

  crt_app_focus_will_handler(false);
  TEST_ASSERT_FALSE(s_cover_suppresses_strike);
  crt_app_focus_did_handler(true);

  // The effect comes on later; the first real wake strikes.
  s_settings_crt_degauss = 1;
  crt_wake_by_shake();
  TEST_ASSERT_TRUE(s_strike_pending);
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(1, mock_speaker_play_tracks_count);
}

// A button or touch wake: the delay timer is armed, nothing is primed yet.
static void crt_wake_by_button(void) {
  crt_backlight_handler(true);
  TEST_ASSERT_FALSE(s_strike_pending);
  TEST_ASSERT_NOT_NULL(s_button_wake_timer);
  TEST_ASSERT_EQUAL_UINT32(CRT_BUTTON_WAKE_DELAY_MS, mock_timer_last_ms);
  TEST_ASSERT_EQUAL_PTR(crt_button_wake_fire, mock_timer_callback);
}

void test_crt_shake_should_prime_the_backlight_wake_at_once(void) {
  s_settings_crt_degauss = 1;
  s_settings_degauss_volume = 90;
  crt_app_focus_did_handler(true);

  crt_tap_handler(ACCEL_AXIS_X, 1);
  TEST_ASSERT_TRUE(s_shake_wake);
  TEST_ASSERT_EQUAL_UINT32(CRT_SHAKE_WAKE_WINDOW_MS, mock_timer_last_ms);
  TEST_ASSERT_EQUAL_PTR(crt_shake_wake_expire, mock_timer_callback);

  crt_backlight_handler(true);
  TEST_ASSERT_TRUE(s_strike_pending);
  TEST_ASSERT_FALSE(s_shake_wake);
  TEST_ASSERT_NULL(s_shake_wake_timer);
  TEST_ASSERT_NULL(s_button_wake_timer);
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(1, mock_speaker_play_tracks_count);
}

void test_crt_button_wake_should_strike_only_after_the_delay(void) {
  s_settings_crt_degauss = 1;
  s_settings_degauss_volume = 90;
  s_crt_layer = layer_create(GRect(0, 0, 200, 228));
  crt_app_focus_did_handler(true);

  crt_wake_by_button();
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(0, mock_speaker_play_tracks_count);

  mock_timer_callback(NULL);
  TEST_ASSERT_NULL(s_button_wake_timer);
  TEST_ASSERT_TRUE(s_strike_pending);
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(1, mock_speaker_play_tracks_count);
  crt_fire_strike_start();
}

void test_crt_button_wake_should_die_with_the_face(void) {
  s_settings_crt_degauss = 1;
  crt_app_focus_did_handler(true);
  crt_wake_by_button();

  int cancelled = mock_timer_cancel_count;
  crt_deinit();
  TEST_ASSERT_EQUAL_INT(cancelled + 1, mock_timer_cancel_count);
  TEST_ASSERT_NULL(s_button_wake_timer);
  TEST_ASSERT_FALSE(s_strike_pending);
}

void test_crt_button_wake_should_not_strike_once_a_cover_arrives(void) {
  s_settings_crt_degauss = 1;
  crt_app_focus_did_handler(true);
  crt_wake_by_button();
  AppTimerCallback fire = mock_timer_callback;

  int cancelled = mock_timer_cancel_count;
  crt_app_focus_will_handler(false);
  TEST_ASSERT_EQUAL_INT(cancelled + 1, mock_timer_cancel_count);
  TEST_ASSERT_NULL(s_button_wake_timer);
  fire(NULL);
  TEST_ASSERT_FALSE(s_strike_pending);
}

void test_crt_button_wake_should_not_strike_once_quick_view_is_up(void) {
  s_settings_crt_degauss = 1;
  crt_app_focus_did_handler(true);
  crt_wake_by_button();

  s_quick_view_active = true;
  mock_timer_callback(NULL);
  TEST_ASSERT_NULL(s_button_wake_timer);
  TEST_ASSERT_FALSE(s_strike_pending);
  s_quick_view_active = false;
}

void test_crt_button_wake_should_not_strike_once_degauss_is_off(void) {
  s_settings_crt_degauss = 1;
  crt_app_focus_did_handler(true);
  crt_wake_by_button();
  AppTimerCallback fire = mock_timer_callback;

  s_settings_crt_degauss = 0;
  int cancelled = mock_timer_cancel_count;
  crt_apply_setting_change();
  TEST_ASSERT_EQUAL_INT(cancelled + 1, mock_timer_cancel_count);
  TEST_ASSERT_NULL(s_button_wake_timer);
  fire(NULL);
  TEST_ASSERT_FALSE(s_strike_pending);
}

void test_crt_button_wake_should_end_with_the_backlight(void) {
  s_settings_crt_degauss = 1;
  crt_app_focus_did_handler(true);
  crt_wake_by_button();

  int cancelled = mock_timer_cancel_count;
  crt_backlight_handler(false);
  TEST_ASSERT_EQUAL_INT(cancelled + 1, mock_timer_cancel_count);
  TEST_ASSERT_NULL(s_button_wake_timer);
}

void test_crt_button_wake_should_restart_its_delay_on_a_new_wake(void) {
  s_settings_crt_degauss = 1;
  crt_app_focus_did_handler(true);
  crt_wake_by_button();

  int cancelled = mock_timer_cancel_count;
  crt_wake_by_button();
  TEST_ASSERT_EQUAL_INT(cancelled + 1, mock_timer_cancel_count);

  crt_wake_by_shake();
  TEST_ASSERT_NULL(s_button_wake_timer);
  TEST_ASSERT_TRUE(s_strike_pending);
}

void test_crt_shake_should_expire_so_a_later_button_wake_waits(void) {
  s_settings_crt_degauss = 1;
  crt_app_focus_did_handler(true);

  crt_tap_handler(ACCEL_AXIS_X, 1);
  mock_timer_callback(NULL);
  TEST_ASSERT_FALSE(s_shake_wake);
  TEST_ASSERT_NULL(s_shake_wake_timer);

  crt_wake_by_button();
}

void test_crt_backlight_off_should_clear_the_shake(void) {
  s_settings_crt_degauss = 1;
  crt_app_focus_did_handler(true);

  crt_tap_handler(ACCEL_AXIS_X, 1);
  crt_backlight_handler(false);
  TEST_ASSERT_FALSE(s_shake_wake);
  TEST_ASSERT_NULL(s_shake_wake_timer);

  crt_wake_by_button();
}

void test_crt_shake_should_rearm_its_expiry(void) {
  s_settings_crt_degauss = 1;
  crt_tap_handler(ACCEL_AXIS_X, 1);
  int cancelled = mock_timer_cancel_count;
  crt_tap_handler(ACCEL_AXIS_Y, -1);
  TEST_ASSERT_EQUAL_INT(cancelled + 1, mock_timer_cancel_count);
  TEST_ASSERT_TRUE(s_shake_wake);
}

void test_crt_deinit_should_clear_the_shake(void) {
  crt_tap_handler(ACCEL_AXIS_X, 1);
  crt_deinit();
  TEST_ASSERT_FALSE(s_shake_wake);
  TEST_ASSERT_NULL(s_shake_wake_timer);
}

void test_crt_setting_change_should_disarm_a_pending_cover_suppression(void) {
  s_settings_crt_degauss = 1;
  crt_app_focus_will_handler(false);
  crt_app_focus_did_handler(true);
  TEST_ASSERT_TRUE(s_cover_suppresses_strike);

  s_settings_crt_degauss = 0;
  crt_apply_setting_change();
  TEST_ASSERT_FALSE(s_cover_suppresses_strike);
  TEST_ASSERT_NULL(s_cover_grace_timer);
}

void test_crt_pass_should_follow_the_row_stride(void) {
  enum { PAD = 8 };
  static uint8_t packed[SCREEN_W * SCREEN_H];
  static uint8_t padded[(SCREEN_W + PAD) * SCREEN_H];
  uint32_t seed = 7;
  for (int y = 0; y < SCREEN_H; y++) {
    for (int x = 0; x < SCREEN_W; x++) {
      seed = seed * 1664525u + 1013904223u;
      packed[y * SCREEN_W + x] = padded[y * (SCREEN_W + PAD) + x] = 0xC0 | ((seed >> 16) & 0x3F);
    }
    memset(&padded[y * (SCREEN_W + PAD) + SCREEN_W], 0x5A, PAD);
  }

  crt_apply_framebuffer(packed, SCREEN_W, SCREEN_H, SCREEN_W, 3);
  crt_apply_framebuffer(padded, SCREEN_W, SCREEN_H, SCREEN_W + PAD, 3);
  for (int y = 0; y < SCREEN_H; y++) {
    TEST_ASSERT_EQUAL_MEMORY(&packed[y * SCREEN_W], &padded[y * (SCREEN_W + PAD)], SCREEN_W);
    for (int x = SCREEN_W; x < SCREEN_W + PAD; x++) {
      TEST_ASSERT_EQUAL_HEX8(0x5A, padded[y * (SCREEN_W + PAD) + x]);
    }
  }
}

void test_crt_flash_should_need_backlight_on_and_the_toggle(void) {
  mock_mark_dirty_count = 0;

  crt_backlight_handler(false);  // backlight turning off starts nothing
  TEST_ASSERT_EQUAL_INT(0, mock_timer_register_count);
  TEST_ASSERT_EQUAL_INT(CRT_FLASH_IDLE, s_flash_phase);

  crt_backlight_handler(true);  // toggle off starts nothing
  TEST_ASSERT_EQUAL_INT(0, mock_timer_register_count);

  s_settings_crt_degauss = 1;
  crt_app_focus_did_handler(true);
  s_crt_layer = layer_create(GRect(0, 0, 200, 228));
  crt_wake_by_shake();
  int armed = mock_timer_register_count;  // the shake's expiry
  // Deferred strike: the handler primes only; the wake frame's proc starts it.
  TEST_ASSERT_EQUAL_INT(CRT_FLASH_IDLE, s_flash_phase);
  TEST_ASSERT_TRUE(mock_mark_dirty_count > 0);
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(armed + 1, mock_timer_register_count);
  crt_fire_strike_start();
  TEST_ASSERT_EQUAL_INT(armed + 2, mock_timer_register_count);
  TEST_ASSERT_NOT_NULL(mock_timer_callback);

  // Drive the re-arming tick chain to completion; the chain must stop.
  int frames = 0;
  while (s_flash_phase != CRT_FLASH_IDLE) {
    mock_timer_callback(NULL);
    frames++;
    TEST_ASSERT_TRUE(frames <= CRT_FLASH_PHASES);  // no lost idle edge
  }
  TEST_ASSERT_EQUAL_INT(CRT_FLASH_PHASES, frames);
  TEST_ASSERT_EQUAL_INT(armed + CRT_FLASH_PHASES + 1, mock_timer_register_count);
}

void test_crt_strike_should_draw_phase_zero_after_the_wake_frame(void) {
  s_settings_crt_degauss = 1;
  s_active_theme = &s_theme_shadow;
  crt_app_focus_did_handler(true);
  s_crt_layer = layer_create(GRect(0, 0, 200, 228));

  static uint8_t idle_frame[SCREEN_W * SCREEN_H];
  s_flash_phase = CRT_FLASH_IDLE;
  memset(mock_framebuffer, 0xC0, sizeof(mock_framebuffer));
  for (int y = 0; y < SCREEN_H; y++) mock_framebuffer[y * SCREEN_W + 106] = 0xFF;
  crt_update_proc(NULL, s_fake_ctx);
  memcpy(idle_frame, mock_framebuffer, sizeof(idle_frame));

  crt_wake_by_shake();
  crt_update_proc(NULL, s_fake_ctx);  // wake frame: strike starts, not yet drawn
  crt_fire_strike_start();

  memset(mock_framebuffer, 0xC0, sizeof(mock_framebuffer));
  for (int y = 0; y < SCREEN_H; y++) mock_framebuffer[y * SCREEN_W + 106] = 0xFF;
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(0, s_flash_phase);
  TEST_ASSERT_TRUE(memcmp(idle_frame, mock_framebuffer, sizeof(idle_frame)) != 0);
}

// The gather alone, on the frame as staged: no fringing or vignette first.
static void strike_gather_frame(int phase, bool curvature) {
  static uint16_t xq[SCREEN_W];
  for (int x = 0; x < SCREEN_W; x++) {
    int dx = 2 * x - (SCREEN_W - 1);
    xq[x] = (uint16_t)(dx * dx * 256 / ((SCREEN_W - 1) * (SCREEN_W - 1)));
  }
  crt_strike_gather(mock_framebuffer, NULL, SCREEN_W, SCREEN_H, SCREEN_W, phase, curvature, xq);
}

// The gather from a captured frame: the frame as staged is the capture, and the
// framebuffer is overwritten from it, fringing included.
static void strike_gather_captured(int phase, bool curvature) {
  static uint8_t src[SCREEN_W * SCREEN_H];
  memcpy(src, mock_framebuffer, sizeof(src));
  crt_strike_gather(mock_framebuffer, src, SCREEN_W, SCREEN_H, SCREEN_W, phase, curvature,
                    crt_tables(SCREEN_W)->ca_xq);
}

// Either gather, for the tests whose frames fringing leaves as they are.
static void strike_gather_either(bool captured, int phase, bool curvature) {
  if (captured) {
    strike_gather_captured(phase, curvature);
  } else {
    strike_gather_frame(phase, curvature);
  }
}

// White as the strike leaves it at (x, y): tinted by that pixel's band.
static uint8_t strike_white(int x, int y) {
  return s_strike_grey_tint[3][crt_strike_band(x, y, 0)];
}

void test_crt_strike_field_should_stay_within_the_reach_in_every_frame(void) {
  for (int phase = 0; phase < CRT_FLASH_PHASES; phase++) {
    for (int y = 0; y < SCREEN_H; y++) {
      for (int x = 0; x < SCREEN_W; x++) {
        int dx, dy;
        crt_strike_field(x, y, phase, &dx, &dy);
        TEST_ASSERT_TRUE(abs(dx) <= 20);
        TEST_ASSERT_TRUE(abs(dy) <= CRT_STRIKE_REACH_PX);
        if (strike_amp_q4(phase) == 0) TEST_ASSERT_TRUE(dx == 0 && dy == 0);
      }
    }
  }
}

void test_crt_strike_field_should_pull_the_most_at_the_rim_and_leave_the_centre(void) {
  int max_dx = 0, max_dy = 0, dx, dy;
  for (int y = 0; y < SCREEN_H; y++) {
    for (int x = 0; x < SCREEN_W; x++) {
      crt_strike_field(x, y, 0, &dx, &dy);
      if (abs(dx) > max_dx) max_dx = abs(dx);
      if (abs(dy) > max_dy) max_dy = abs(dy);
    }
  }
  TEST_ASSERT_EQUAL_INT(20, max_dx);
  TEST_ASSERT_EQUAL_INT(CRT_STRIKE_REACH_PX, max_dy);
  // Halfway out to the rim up the vertical axis the pull is a fraction of the
  // rim's.
  int rim_dy, half_dy, unused;
  crt_strike_field(99, 0, 0, &unused, &rim_dy);
  crt_strike_field(99, 57, 0, &unused, &half_dy);
  TEST_ASSERT_TRUE(abs(rim_dy) >= 2);
  TEST_ASSERT_TRUE(4 * abs(half_dy) < abs(rim_dy));
  for (int phase = 0; phase < CRT_FLASH_PHASES; phase++) {
    for (int y = SCREEN_H / 2 - 1; y <= SCREEN_H / 2; y++) {
      for (int x = SCREEN_W / 2 - 1; x <= SCREEN_W / 2; x++) {
        crt_strike_field(x, y, phase, &dx, &dy);
        TEST_ASSERT_EQUAL_INT(0, dx);
        TEST_ASSERT_EQUAL_INT(0, dy);
      }
    }
  }
}

void test_crt_strike_field_should_flip_between_pulled_in_and_pushed_out_every_frame(void) {
  for (int phase = 0; phase < 3; phase++) {
    int pulled = 0;
    for (int y = 0; y < SCREEN_H; y++) {
      for (int x = 0; x < SCREEN_W; x++) {
        int dx0, dy0, dx1, dy1;
        crt_strike_field(x, y, phase, &dx0, &dy0);
        crt_strike_field(x, y, phase + 1, &dx1, &dy1);
        TEST_ASSERT_TRUE(dx0 * dx1 <= 0);
        TEST_ASSERT_TRUE(dy0 * dy1 <= 0);
        if (dx0 * dx1 < 0 && dy0 * dy1 < 0) pulled++;
      }
    }
    TEST_ASSERT_TRUE(pulled > 0);
  }
}

void test_crt_strike_decay_should_keep_its_curve_in_time(void) {
  // The curve's knots, in px every 90 ms and linear between; a frame is the
  // curve at its own time.
  static const int knots_px[8] = {6, 4, 3, 2, 1, 1, 0, 0};
  int prev = 1 << 20;
  for (int phase = 0; phase < CRT_FLASH_PHASES; phase++) {
    int t = phase * CRT_FLASH_TICK_MS;
    double px = 0;
    if (t < 630) {
      double f = (t % 90) / 90.0;
      px = knots_px[t / 90] * (1 - f) + knots_px[t / 90 + 1] * f;
    }
    int amp = (int)(px * (1 << CRT_STRIKE_AMP_SHIFT) + 0.5);
    TEST_ASSERT_EQUAL_INT(amp, strike_amp_q4(phase));
    TEST_ASSERT_EQUAL_INT(((amp + 8) / 16 + 1) / 2, strike_ca_boost(phase));
    TEST_ASSERT_EQUAL_INT(amp * amp * CRT_STRIKE_STRIPES_Q13 / 256, strike_stripes(phase));
    TEST_ASSERT_TRUE(strike_amp_q4(phase) <= prev);
    prev = strike_amp_q4(phase);
    if (t % 90 == 0) TEST_ASSERT_EQUAL_INT(knots_px[t / 90] << CRT_STRIKE_AMP_SHIFT, amp);
    if (t >= 540) TEST_ASSERT_EQUAL_INT(0, strike_amp_q4(phase));
  }
  TEST_ASSERT_EQUAL_INT(96, strike_amp_q4(0));
  TEST_ASSERT_TRUE(strike_amp_q4(520 / CRT_FLASH_TICK_MS) > 0);
}

void test_crt_strike_lobes_should_move_at_the_same_speed_in_time(void) {
  // 8192 and 4915 turns (Q16) per 45 ms, per 40-ms frame, to the nearest.
  TEST_ASSERT_INT_WITHIN(45 / 2, 8192 * CRT_FLASH_TICK_MS, CRT_STRIKE_LOBE_STEP_X * 45);
  TEST_ASSERT_INT_WITHIN(45 / 2, 4915 * CRT_FLASH_TICK_MS, CRT_STRIKE_LOBE_STEP_Y * 45);
  for (int k = 0; k < CRT_FLASH_PHASES; k++) {
    for (int pos = 0; pos < SCREEN_H; pos += 7) {
      uint32_t turn = (uint32_t)pos * 65536 / SCREEN_H + (uint32_t)k * 7282;
      TEST_ASSERT_EQUAL_INT(256 + ((CRT_STRIKE_LOBE_Q8 * crt_wave_q15(turn)) >> 15),
                            strike_lobe_q8(pos, SCREEN_H, k, CRT_STRIKE_LOBE_STEP_X));
      turn = (uint32_t)pos * 65536 / SCREEN_H + (uint32_t)k * 4369;
      TEST_ASSERT_EQUAL_INT(256 + ((CRT_STRIKE_LOBE_Q8 * crt_wave_q15(turn)) >> 15),
                            strike_lobe_q8(pos, SCREEN_H, k, CRT_STRIKE_LOBE_STEP_Y));
    }
  }
}

void test_crt_strike_should_last_720_ms_as_long_as_its_sound(void) {
  TEST_ASSERT_EQUAL_INT(720, CRT_FLASH_PHASES * CRT_FLASH_TICK_MS);
  TEST_ASSERT_EQUAL_INT(720, CRT_STRIKE_PCM_MS);
}

void test_crt_strike_field_should_pull_unevenly_along_an_edge(void) {
  int max_dx = 0, min_dx = 99, dx, dy;
  for (int y = 0; y < SCREEN_H; y++) {
    crt_strike_field(0, y, 0, &dx, &dy);
    if (abs(dx) > max_dx) max_dx = abs(dx);
    if (abs(dx) < min_dx) min_dx = abs(dx);
  }
  TEST_ASSERT_TRUE(max_dx >= 3 * min_dx);
  TEST_ASSERT_TRUE(max_dx > min_dx);
}

void test_crt_strike_field_should_move_the_lobes_between_frames(void) {
  int peak_y[5] = {0};
  for (int phase = 0; phase < 5; phase += 4) {
    int peak = 0, dx, dy;
    for (int y = 0; y < SCREEN_H; y++) {
      crt_strike_field(0, y, phase, &dx, &dy);
      if (abs(dx) > peak) {
        peak = abs(dx);
        peak_y[phase] = y;
      }
    }
  }
  TEST_ASSERT_TRUE(peak_y[0] != peak_y[4]);
}

void test_crt_strike_should_gather_pixels_where_the_field_says(void) {
  const int dst_x = 150, dst_y = 60;
  int dx, dy;
  crt_strike_field(dst_x, dst_y, 0, &dx, &dy);
  TEST_ASSERT_TRUE(dx != 0 || dy != 0);
  const int src_x = dst_x + dx, src_y = dst_y + dy;
  TEST_ASSERT_TRUE(src_x >= 0 && src_x < SCREEN_W && src_y >= 0 && src_y < SCREEN_H);
  memset(mock_framebuffer, 0xC0, sizeof(mock_framebuffer));
  mock_framebuffer[src_y * SCREEN_W + src_x] = 0xFF;
  strike_gather_frame(0, false);

  for (int y = 0; y < SCREEN_H; y++) {
    for (int x = 0; x < SCREEN_W; x++) {
      crt_strike_field(x, y, 0, &dx, &dy);
      bool lands = x + dx == src_x && y + dy == src_y;
      TEST_ASSERT_EQUAL_HEX8(lands ? strike_white(x, y) : 0xC0, mock_framebuffer[y * SCREEN_W + x]);
    }
  }
  TEST_ASSERT_EQUAL_HEX8(strike_white(dst_x, dst_y), mock_framebuffer[dst_y * SCREEN_W + dst_x]);
}

void test_crt_strike_should_leave_the_centre_pixel_where_it_is(void) {
  const int cx = SCREEN_W / 2 - 1, cy = SCREEN_H / 2 - 1;
  memset(mock_framebuffer, 0xC0, sizeof(mock_framebuffer));
  mock_framebuffer[cy * SCREEN_W + cx] = 0xFF;
  strike_gather_frame(0, false);

  for (int y = 0; y < SCREEN_H; y++) {
    for (int x = 0; x < SCREEN_W; x++) {
      TEST_ASSERT_EQUAL_HEX8(x == cx && y == cy ? strike_white(x, y) : 0xC0,
                             mock_framebuffer[y * SCREEN_W + x]);
    }
  }
}

void test_crt_strike_should_read_rows_above_as_they_were_before_the_warp(void) {
  // Phase 0 pulls the top right in from above: a dest row below its source
  // row, which has already been overwritten by the time it is read.
  const int dst_y = 30;
  int dst_x = SCREEN_W - 1, dx, dy;
  for (;; dst_x--) {
    crt_strike_field(dst_x, dst_y, 0, &dx, &dy);
    if (dst_x + dx < SCREEN_W) break;
  }
  TEST_ASSERT_TRUE(dy < 0);
  const int src_x = dst_x + dx, src_y = dst_y + dy;
  memset(mock_framebuffer, 0xC0, sizeof(mock_framebuffer));
  mock_framebuffer[src_y * SCREEN_W + src_x] = 0xFF;
  strike_gather_frame(0, false);

  for (int y = 0; y < SCREEN_H; y++) {
    for (int x = 0; x < SCREEN_W; x++) {
      crt_strike_field(x, y, 0, &dx, &dy);
      bool lands = x + dx == src_x && y + dy == src_y;
      TEST_ASSERT_EQUAL_HEX8(lands ? strike_white(x, y) : 0xC0, mock_framebuffer[y * SCREEN_W + x]);
    }
  }
  TEST_ASSERT_EQUAL_HEX8(strike_white(dst_x, dst_y), mock_framebuffer[dst_y * SCREEN_W + dst_x]);
}

void test_crt_strike_should_be_black_beyond_the_raster(void) {
  for (int captured = 0; captured < 2; captured++) {
    memset(mock_framebuffer, 0xFF, sizeof(mock_framebuffer));
    strike_gather_either(captured, 0, false);
    TEST_ASSERT_EQUAL_HEX8(0xC0, mock_framebuffer[0]);   // source left of the frame
    TEST_ASSERT_EQUAL_HEX8(0xC0, mock_framebuffer[99]);  // source above it
    TEST_ASSERT_EQUAL_HEX8(0xC0, mock_framebuffer[227 * SCREEN_W + 99]);  // source below it
    TEST_ASSERT_EQUAL_HEX8(strike_white(100, 113),
                           mock_framebuffer[113 * SCREEN_W + 100]);  // the picture stays
    TEST_ASSERT_EQUAL_HEX8(strike_white(60, 113), mock_framebuffer[113 * SCREEN_W + 60]);
  }
}

void test_crt_strike_should_stripe_colour_with_hue_and_tint_grey(void) {
  for (int captured = 0; captured < 2; captured++) {
    memset(mock_framebuffer, 0xC3, sizeof(mock_framebuffer));  // pure blue
    strike_gather_either(captured, 0, false);
    bool seen[256] = {false};
    for (int x = SCREEN_W / 2; x < 180; x++) {
      int dx, dy;
      crt_strike_field(x, 113, 0, &dx, &dy);
      if (x + dx >= SCREEN_W) break;  // beyond the raster: black from here out
      uint8_t v = mock_framebuffer[113 * SCREEN_W + x];
      TEST_ASSERT_EQUAL_HEX8(strike_hue(0xC3, crt_strike_band(x, 113, 0)), v);
      seen[v] = true;
    }
    TEST_ASSERT_TRUE(seen[0xC3]);  // identity: blue
    TEST_ASSERT_TRUE(seen[0xF0]);  // R->G->B->R: red
    TEST_ASSERT_TRUE(seen[0xCC]);  // R->B->G->R: green

    for (int level = 0; level < 4; level++) {
      const uint8_t grey = (uint8_t)(GCOLOR8_ALPHA | (level << 4) | (level << 2) | level);
      memset(mock_framebuffer, grey, sizeof(mock_framebuffer));
      strike_gather_either(captured, 0, false);
      bool tints[256] = {false};
      for (int x = SCREEN_W / 2; x < 180; x++) {
        int dx, dy;
        crt_strike_field(x, 113, 0, &dx, &dy);
        if (x + dx >= SCREEN_W) break;
        uint8_t v = mock_framebuffer[113 * SCREEN_W + x];
        int band = crt_strike_band(x, 113, 0);
        if (!band || level == 0) {
          TEST_ASSERT_EQUAL_HEX8(grey, v);
          continue;
        }
        TEST_ASSERT_FALSE(is_grey(v));
        TEST_ASSERT_TRUE(abs(((v >> 4) & 3) - level) <= 2);
        TEST_ASSERT_TRUE(abs(((v >> 2) & 3) - level) <= 2);
        TEST_ASSERT_TRUE(abs((v & 3) - level) <= 2);
        tints[v] = true;
      }
      int distinct = 0;
      for (int i = 0; i < 256; i++) distinct += tints[i];
      if (level == 2) TEST_ASSERT_TRUE(distinct >= 3);
    }
  }
}

void test_crt_strike_should_thin_the_stripes_as_the_amplitude_dies(void) {
  int bands0 = 0, bands_last = 0;
  for (int x = SCREEN_W / 2; x < SCREEN_W; x++) {
    if (crt_strike_band(x, 0, 0) != crt_strike_band(x - 1, 0, 0)) bands0++;
    if (crt_strike_band(x, 0, 11) != crt_strike_band(x - 1, 0, 11)) bands_last++;
  }
  TEST_ASSERT_TRUE(bands0 > 6 * bands_last);
  TEST_ASSERT_EQUAL_INT(0, crt_strike_band(SCREEN_W - 1, 0, 14));
}

void test_crt_strike_should_leave_the_frame_alone_at_zero_amplitude(void) {
  static uint8_t idle_frame[SCREEN_W * SCREEN_H];
  uint32_t seed = 11;
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
    seed = seed * 1664525u + 1013904223u;
    mock_framebuffer[i] = 0xC0 | ((seed >> 16) & 0x3F);
  }
  memcpy(idle_frame, mock_framebuffer, sizeof(idle_frame));
  crt_apply_framebuffer_passes(idle_frame, SCREEN_W, SCREEN_H, SCREEN_W, CRT_FLASH_IDLE, true,
                               false);
  crt_apply_framebuffer_passes(mock_framebuffer, SCREEN_W, SCREEN_H, SCREEN_W, 14, true, false);
  TEST_ASSERT_EQUAL_MEMORY(idle_frame, mock_framebuffer, sizeof(idle_frame));
}

void test_crt_strike_start_should_do_nothing_once_the_chain_is_killed(void) {
  s_settings_crt_degauss = 1;
  crt_app_focus_did_handler(true);
  s_crt_layer = layer_create(GRect(0, 0, 200, 228));

  // Setting off between the wake frame and the start timer.
  crt_wake_by_shake();
  crt_update_proc(NULL, s_fake_ctx);
  AppTimerCallback start = mock_timer_callback;
  s_settings_crt_degauss = 0;
  crt_apply_setting_change();
  int registered = mock_timer_register_count;
  mock_mark_dirty_count = 0;
  start(NULL);
  TEST_ASSERT_EQUAL_INT(CRT_FLASH_IDLE, s_flash_phase);
  TEST_ASSERT_EQUAL_INT(0, mock_mark_dirty_count);
  TEST_ASSERT_EQUAL_INT(registered, mock_timer_register_count);

  // A re-prime between the wake frame and the start timer.
  s_settings_crt_degauss = 1;
  crt_wake_by_shake();
  crt_update_proc(NULL, s_fake_ctx);
  start = mock_timer_callback;
  crt_wake_by_shake();
  registered = mock_timer_register_count;
  mock_mark_dirty_count = 0;
  start(NULL);
  TEST_ASSERT_EQUAL_INT(CRT_FLASH_IDLE, s_flash_phase);
  TEST_ASSERT_EQUAL_INT(0, mock_mark_dirty_count);
  TEST_ASSERT_EQUAL_INT(registered, mock_timer_register_count);
}

// A strike on a face whose canvas has drawn, so the scene cache exists to
// borrow; leaves phase 0 pending its repaint.
static void crt_strike_begin(void) {
  s_settings_crt_degauss = 1;
  s_active_theme = &s_theme_shadow;
  crt_app_focus_did_handler(true);
  s_crt_layer = layer_create(GRect(0, 0, 200, 228));
  canvas_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_NOT_NULL(s_scene_cache);
  crt_wake_by_shake();
  crt_update_proc(NULL, s_fake_ctx);
  crt_fire_strike_start();
}

static void crt_strike_advance_to(int phase) {
  while (s_flash_phase < phase) mock_timer_callback(NULL);
}

static void fill_test_frame(uint32_t seed) {
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
    seed = seed * 1664525u + 1013904223u;
    mock_framebuffer[i] = 0xC0 | ((seed >> 16) & 0x3F);
  }
}

void test_crt_strike_band_table_should_match_the_tint_and_hue_rules(void) {
  for (int band = 0; band < 6; band++) {
    for (int v = 0; v < 64; v++) {
      uint8_t px = (uint8_t)(GCOLOR8_ALPHA | v);
      uint8_t expected = is_grey(px) ? s_strike_grey_tint[v & 3][band] : strike_hue(px, band);
      TEST_ASSERT_EQUAL_HEX8(expected, s_strike_band_px[band][v]);
    }
  }
}

// Runs of flat colour and grey, which the random frame has none of: the blends
// of equal neighbours, the greys' tints and the copied spans all want them.
static void fill_flat_frame(uint32_t seed) {
  for (int y = 0; y < SCREEN_H; y++) {
    for (int x = 0; x < SCREEN_W; x++) {
      uint32_t h = ((uint32_t)(x / 7) * 73856093u) ^ ((uint32_t)(y / 5) * 19349663u) ^ seed;
      h = h * 1664525u + 1013904223u;
      uint8_t v = (uint8_t)((h >> 16) & 0x3F);
      if ((h >> 24) % 5 < 2) v = (uint8_t)(((v & 3) << 4) | ((v & 3) << 2) | (v & 3));
      mock_framebuffer[y * SCREEN_W + x] = GCOLOR8_ALPHA | v;
    }
  }
}

// The normal pipeline's stage 3 over a whole frame, one row helper call each.
static void curve_reference(uint8_t* dst, const uint8_t* raw) {
  const uint16_t* xq = crt_tables(SCREEN_W)->ca_xq;
  for (int y = 0; y < SCREEN_H; y++) {
    int dy = 2 * y - (SCREEN_H - 1);
    const uint8_t* r = raw + (size_t)y * SCREEN_W;
    int nrz;
    const CrtRigidZone* rz = rigid_zones(SCREEN_W, SCREEN_H, y, &nrz);
    crt_curve_row(dst + (size_t)y * SCREEN_W, r, r, SCREEN_W,
                  dy * dy * 256 / ((SCREEN_H - 1) * (SCREEN_H - 1)), xq, rz, nrz);
  }
}

// The windows the face draws, as drawing.c frames them: the six slots and TIME.
#define FACE_WINDOWS 7
static void face_windows(GRect* out) {
  const GRect windows[FACE_WINDOWS] = {
      SLOT_RECT_TOP_LEFT,      SLOT_RECT_TOP_RIGHT,    SLOT_RECT_CENTER, SLOT_RECT_BOTTOM_LEFT,
      SLOT_RECT_BOTTOM_CENTER, SLOT_RECT_BOTTOM_RIGHT, TIME_WINDOW_RECT};
  memcpy(out, windows, sizeof(windows));
}

// Every frame window through the real renderer, as `frame` on `field`.
static void draw_face_frames(uint8_t* img, uint8_t field, uint8_t frame) {
  GRect windows[FACE_WINDOWS];
  face_windows(windows);
  memset(img, field, SCREEN_W * SCREEN_H);
  for (int i = 0; i < FACE_WINDOWS; i++) {
    mock_fill_rect_reset();
    draw_ascii_window(NULL, windows[i], "AB");
    for (int k = 0; k < mock_fill_rect_count; k++) {
      const GRect r = mock_fill_rects[k];
      for (int y = r.origin.y; y < r.origin.y + r.size.h; y++) {
        memset(img + y * SCREEN_W + r.origin.x, frame, r.size.w);
      }
    }
  }
}

// The stroke table is the layout's: against what draw_ascii_window paints, on
// every row that is not a window's top or bottom line, the frame-coloured
// columns are exactly the listed strokes covering that row.
void test_crt_frame_strokes_should_match_the_drawn_frames(void) {
  static uint8_t img[SCREEN_W * SCREEN_H];
  s_active_theme = &s_theme_shadow;
  draw_face_frames(img, 0xC0, 0xD5);
  GRect windows[FACE_WINDOWS];
  face_windows(windows);
  int n;
  const CrtStroke* strokes = frame_strokes(&n);
  for (int y = 0; y < SCREEN_H; y++) {
    bool line_row = false;
    for (int i = 0; i < FACE_WINDOWS; i++) {
      const int top = windows[i].origin.y + TITLE_BORDER_DROP;
      const int bottom = windows[i].origin.y + windows[i].size.h - WINDOW_BORDER_PX;
      if ((y >= top && y < top + WINDOW_BORDER_PX) ||
          (y >= bottom && y < bottom + WINDOW_BORDER_PX)) {
        line_row = true;
      }
    }
    if (line_row) continue;
    for (int x = 0; x < SCREEN_W; x++) {
      bool listed = false;
      for (int i = 0; i < n; i++) {
        if (y >= strokes[i].y0 && y < strokes[i].y1 && x >= strokes[i].x &&
            x < strokes[i].x + WINDOW_BORDER_PX) {
          listed = true;
        }
      }
      TEST_ASSERT_EQUAL_INT_MESSAGE(listed, img[y * SCREEN_W + x] == 0xD5, "stroke table vs frame");
    }
  }
  for (int i = 1; i < n; i++) TEST_ASSERT_TRUE(strokes[i - 1].x <= strokes[i].x);
}

// Each stroke row's zone covers every glass pixel the true warp blends the
// stroke into, and a pixel past it on each side.
void test_crt_rigid_zones_should_cover_the_warped_stroke_and_a_margin(void) {
  const uint16_t* xq = crt_tables(SCREEN_W)->ca_xq;
  int ns;
  const CrtStroke* strokes = frame_strokes(&ns);
  for (int i = 0; i < ns; i++) {
    for (int y = strokes[i].y0; y < strokes[i].y1; y++) {
      int nrz;
      const CrtRigidZone* rz = rigid_zones(SCREEN_W, SCREEN_H, y, &nrz);
      const int dy = 2 * y - (SCREEN_H - 1);
      const int yterm = dy * dy * 256 / ((SCREEN_H - 1) * (SCREEN_H - 1));
      for (int x = 0; x < SCREEN_W; x++) {
        const int S = (SCREEN_W - 1) * 65536 + (2 * x - (SCREEN_W - 1)) * warp_mul16(xq[x] + yterm);
        const int sx = S >> 17;
        const int fr = (S >> 9) & 0xFF;
        const int c1 = strokes[i].x + WINDOW_BORDER_PX - 1;
        if ((sx == strokes[i].x - 1 && fr > 0) || (sx >= strokes[i].x && sx <= c1)) {
          TEST_ASSERT_NOT_NULL_MESSAGE(rigid_at(rz, nrz, x - 1), "left margin");
          TEST_ASSERT_NOT_NULL_MESSAGE(rigid_at(rz, nrz, x), "footprint");
          TEST_ASSERT_NOT_NULL_MESSAGE(rigid_at(rz, nrz, x + 1), "right margin");
        }
      }
    }
  }
}

// Zones never overlap, lie in the raster and fit the table.
void test_crt_rigid_zones_should_be_ordered_and_bounded(void) {
  int total = 0;
  for (int y = 0; y < SCREEN_H; y++) {
    int nrz;
    const CrtRigidZone* rz = rigid_zones(SCREEN_W, SCREEN_H, y, &nrz);
    total += nrz;
    for (int k = 0; k < nrz; k++) {
      TEST_ASSERT_TRUE(rz[k].xa <= rz[k].xb);
      TEST_ASSERT_TRUE(rz[k].xb < SCREEN_W);
      if (k) TEST_ASSERT_TRUE(rz[k - 1].xb < rz[k].xa);
    }
  }
  TEST_ASSERT_TRUE(total > 0 && total < CRT_RIGID_ZONES);
  int n;
  rigid_zones(SCREEN_W + 1, SCREEN_H, 0, &n);
  TEST_ASSERT_EQUAL_INT(0, n);
}

// Outside the zones the warp is the plain one, bit for bit.
void test_crt_curve_should_only_differ_from_the_plain_warp_inside_the_zones(void) {
  static uint8_t src[SCREEN_W * SCREEN_H];
  static uint8_t rigid[SCREEN_W];
  static uint8_t plain[SCREEN_W];
  fill_test_frame(11);
  memcpy(src, mock_framebuffer, sizeof(src));
  const uint16_t* xq = crt_tables(SCREEN_W)->ca_xq;
  bool differs = false;
  for (int y = 0; y < SCREEN_H; y++) {
    const int dy = 2 * y - (SCREEN_H - 1);
    const int yterm = dy * dy * 256 / ((SCREEN_H - 1) * (SCREEN_H - 1));
    const uint8_t* r = src + y * SCREEN_W;
    int nrz;
    const CrtRigidZone* rz = rigid_zones(SCREEN_W, SCREEN_H, y, &nrz);
    crt_curve_row(rigid, r, r, SCREEN_W, yterm, xq, rz, nrz);
    crt_curve_row(plain, r, r, SCREEN_W, yterm, xq, NULL, 0);
    for (int x = 0; x < SCREEN_W; x++) {
      if (rigid_at(rz, nrz, x)) {
        differs |= rigid[x] != plain[x];
      } else {
        TEST_ASSERT_EQUAL_HEX8(plain[x], rigid[x]);
      }
    }
  }
  TEST_ASSERT_TRUE(differs);
}

// Every stroke row of the face's frames, curved through the pass on `field`:
// the stroke never narrows below the width it was drawn at, with no notch,
// and its intensity against the field, summed over the channels, is constant
// within one level: one pixel's coverage is quantised to thirds of a level.
// The windows around a stroke hold nothing else in these rows.
static void check_curved_strokes(uint8_t field, uint8_t frame) {
  static uint8_t img[SCREEN_W * SCREEN_H];
  static uint8_t out[SCREEN_W];
  draw_face_frames(img, field, frame);
  const uint16_t* xq = crt_tables(SCREEN_W)->ca_xq;
  int ns;
  const CrtStroke* strokes = frame_strokes(&ns);
  int lo = 1 << 20, hi = 0;
  for (int i = 0; i < ns; i++) {
    for (int y = strokes[i].y0 + WINDOW_BORDER_PX; y < strokes[i].y1 - WINDOW_BORDER_PX; y++) {
      const int dy = 2 * y - (SCREEN_H - 1);
      const int yterm = dy * dy * 256 / ((SCREEN_H - 1) * (SCREEN_H - 1));
      const uint8_t* r = img + y * SCREEN_W;
      int nrz;
      const CrtRigidZone* rz = rigid_zones(SCREEN_W, SCREEN_H, y, &nrz);
      crt_curve_row(out, r, r, SCREEN_W, yterm, xq, rz, nrz);
      int lit = 0, intensity = 0;
      for (int x = strokes[i].x - 12; x < strokes[i].x + 14; x++) {
        if (x < 0 || x >= SCREEN_W) continue;
        lit += out[x] != field;
        for (int shift = 0; shift < 6; shift += 2) {
          intensity += abs(((field >> shift) & 3) - ((out[x] >> shift) & 3));
        }
      }
      TEST_ASSERT_TRUE_MESSAGE(lit >= WINDOW_BORDER_PX, "stroke width");
      lo = intensity < lo ? intensity : lo;
      hi = intensity > hi ? intensity : hi;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(hi - lo <= 1, "stroke intensity");
}

void test_crt_curve_should_keep_a_level_one_stroke_at_least_two_pixels_wide(void) {
  check_curved_strokes(0xC0, 0xD5);  // Console: dark grey on black
}

void test_crt_curve_should_keep_a_light_theme_stroke_intensity_constant(void) {
  check_curved_strokes(0xEA, 0xD5);  // Turbo Vision: dark grey on light grey
}

static int blend_sum(uint8_t p0, uint8_t p1, int fr) {
  int r, g, b;
  blend_levels(p0, p1, fr, &r, &g, &b);
  return r + g + b;
}

// A one-level blend steps one channel at a time, B then G then R, so the
// pixel's channel sum rises through every value from 0 to 3 as the brighter
// tap's coverage grows.
void test_blend_levels_should_turn_one_level_channels_on_one_at_a_time(void) {
  int prev = 0;
  bool seen[4] = {false};
  for (int fr = 0; fr < 256; fr++) {
    int r, g, b;
    blend_levels(0xC0, 0xD5, fr, &r, &g, &b);
    TEST_ASSERT_EQUAL_INT(fr >= 213, r);
    TEST_ASSERT_EQUAL_INT(fr >= 128, g);
    TEST_ASSERT_EQUAL_INT(fr >= 43, b);
    TEST_ASSERT_TRUE(r + g + b >= prev);
    prev = r + g + b;
    seen[prev] = true;
  }
  for (int v = 0; v < 4; v++) TEST_ASSERT_TRUE(seen[v]);
}

void test_blend_levels_should_mirror_a_one_level_blend_with_the_taps_swapped(void) {
  for (int fr = 0; fr < 256; fr++) {
    int r, g, b, r2, g2, b2;
    blend_levels(0xC0, 0xD5, fr, &r, &g, &b);
    blend_levels(0xD5, 0xC0, 256 - fr, &r2, &g2, &b2);
    TEST_ASSERT_EQUAL_INT(r, r2);
    TEST_ASSERT_EQUAL_INT(g, g2);
    TEST_ASSERT_EQUAL_INT(b, b2);
  }
}

void test_blend_levels_should_step_light_greys_the_same_way(void) {
  const uint8_t pairs[2][2] = {{0xD5, 0xEA}, {0xEA, 0xFF}};  // 1 to 2, 2 to 3
  for (int i = 0; i < 2; i++) {
    int prev = 0;
    bool seen[4] = {false};
    for (int fr = 0; fr < 256; fr++) {
      const int sum = blend_sum(pairs[i][0], pairs[i][1], fr) - 3 * (pairs[i][0] & 3);
      TEST_ASSERT_TRUE(sum >= prev);
      prev = sum;
      seen[sum] = true;
    }
    for (int v = 0; v < 4; v++) TEST_ASSERT_TRUE(seen[v]);
  }
}

// Only blends within one level are stepped in thirds; every wider one keeps the
// plain rounded mix.
void test_blend_levels_should_keep_the_rounded_mix_for_wider_blends(void) {
  for (int p0 = 0; p0 < 64; p0++) {
    for (int p1 = 0; p1 < 64; p1++) {
      const int c0[3] = {(p0 >> 4) & 3, (p0 >> 2) & 3, p0 & 3};
      const int c1[3] = {(p1 >> 4) & 3, (p1 >> 2) & 3, p1 & 3};
      if (abs(c0[0] - c1[0]) <= 1 && abs(c0[1] - c1[1]) <= 1 && abs(c0[2] - c1[2]) <= 1) continue;
      for (int fr = 0; fr < 256; fr++) {
        int got[3];
        blend_levels(GCOLOR8_ALPHA | p0, GCOLOR8_ALPHA | p1, fr, &got[0], &got[1], &got[2]);
        for (int i = 0; i < 3; i++) {
          TEST_ASSERT_EQUAL_INT(((256 - fr) * c0[i] + fr * c1[i] + 128) >> 8, got[i]);
        }
      }
    }
  }
}

void test_crt_strike_gather_should_match_the_per_pixel_loop_in_every_frame(void) {
  static uint8_t src[SCREEN_W * SCREEN_H];
  static uint8_t expected[SCREEN_W * SCREEN_H];
  static uint8_t baked[SCREEN_W * SCREEN_H];
  const CrtTables* tb = crt_tables(SCREEN_W);
  const uint16_t* xq = tb->ca_xq;
  for (int source = 0; source < 3; source++) {
    if (source == 0) {
      fill_test_frame(5);
    } else if (source == 1) {
      fill_flat_frame(9);
    } else {
      memset(mock_framebuffer, 0xFF, sizeof(src));
    }
    memcpy(src, mock_framebuffer, sizeof(src));
    for (int phase = 0; phase < CRT_FLASH_PHASES; phase++) {
      for (int curvature = 0; curvature < 2; curvature++) {
        memset(expected, 0x5A, sizeof(expected));
        memset(mock_framebuffer, 0xA5, sizeof(src));
        if (curvature) {
          // A curved strike gathers from the baked capture with curvature off,
          // then lays the pane over the glass.
          curve_reference(baked, src);
          s_strike_src = baked;
          s_active_theme = &s_theme_shadow;
          crt_strike_gather_scalar(expected, baked, SCREEN_W, SCREEN_H, SCREEN_W, phase, false, xq);
          vignette_pane(expected, SCREEN_W, SCREEN_H, SCREEN_W);
          crt_apply_framebuffer_passes(mock_framebuffer, SCREEN_W, SCREEN_H, SCREEN_W, phase, false,
                                       true);
          s_strike_src = NULL;
        } else {
          crt_strike_gather_scalar(expected, src, SCREEN_W, SCREEN_H, SCREEN_W, phase, false, xq);
          crt_strike_gather(mock_framebuffer, src, SCREEN_W, SCREEN_H, SCREEN_W, phase, false, xq);
        }
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
          if (expected[i] != mock_framebuffer[i]) {
            char msg[96];
            snprintf(msg, sizeof(msg), "source %d phase %d curvature %d x %d y %d", source, phase,
                     curvature, i % SCREEN_W, i / SCREEN_W);
            TEST_FAIL_MESSAGE(msg);
          }
        }
      }
    }
  }
}

void test_crt_strike_should_capture_the_raw_frame_on_phase_zero_and_gather_later_frames_from_it(
    void) {
  static uint8_t raw[SCREEN_W * SCREEN_H];
  static uint8_t expected[SCREEN_W * SCREEN_H];
  crt_strike_begin();
  fill_test_frame(3);
  memcpy(raw, mock_framebuffer, sizeof(raw));

  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_PTR(s_scene_cache, s_strike_src);
  TEST_ASSERT_EQUAL_MEMORY(raw, s_scene_cache, sizeof(raw));
  TEST_ASSERT_TRUE(memcmp(raw, mock_framebuffer, sizeof(raw)) != 0);

  // The live frame is no longer read: whatever the layers leave there, the
  // frame is the gather of the capture.
  memset(mock_framebuffer, 0xFF, sizeof(mock_framebuffer));
  crt_strike_advance_to(2);
  crt_update_proc(NULL, s_fake_ctx);
  memcpy(expected, raw, sizeof(expected));
  crt_strike_gather(expected, raw, SCREEN_W, SCREEN_H, SCREEN_W, 2, false,
                    crt_tables(SCREEN_W)->ca_xq);
  TEST_ASSERT_EQUAL_MEMORY(expected, mock_framebuffer, sizeof(expected));
  TEST_ASSERT_EQUAL_MEMORY(raw, s_scene_cache, sizeof(raw));
}

void test_crt_strike_should_write_every_pixel_from_the_capture(void) {
  crt_strike_begin();
  memset(mock_framebuffer, 0xC3, sizeof(mock_framebuffer));  // blue everywhere
  crt_update_proc(NULL, s_fake_ctx);
  // Nothing of the live frame may survive: a stale 0x5A byte would be read as
  // content were any pixel skipped.
  memset(mock_framebuffer, 0x5A, sizeof(mock_framebuffer));
  crt_strike_advance_to(14);
  crt_update_proc(NULL, s_fake_ctx);
  for (int y = 0; y < SCREEN_H; y++) {
    for (int x = 0; x < SCREEN_W; x++) {
      TEST_ASSERT_EQUAL_HEX8(0xC3, mock_framebuffer[y * SCREEN_W + x]);
    }
  }
}

// One column of white on black; the lit pixels the gather leaves on row 113 say
// where each channel was sampled: R, G and B each light on their own.
static void strike_lit_columns(int phase, int* r, int* g, int* b) {
  memset(mock_framebuffer, 0xC0, sizeof(mock_framebuffer));
  for (int y = 0; y < SCREEN_H; y++) mock_framebuffer[y * SCREEN_W + 120] = 0xFF;
  strike_gather_captured(phase, false);
  *r = *g = *b = -1;
  for (int x = 100; x < 150; x++) {
    uint8_t v = mock_framebuffer[113 * SCREEN_W + x];
    if (v == 0xF0) *r = x;
    if (v == 0xCC) *g = x;
    if (v == 0xC3) *b = x;
  }
}

void test_crt_strike_should_separate_the_channels_further_in_the_early_frames(void) {
  int r0, g0, b0, r5, g5, b5;
  strike_lit_columns(0, &r0, &g0, &b0);
  strike_lit_columns(10, &r5, &g5, &b5);
  TEST_ASSERT_TRUE(r0 >= 0 && g0 >= 0 && b0 >= 0);
  TEST_ASSERT_TRUE(r5 >= 0 && g5 >= 0 && b5 >= 0);
  // Right half: R is sampled from the left of G, B from the right, so on the
  // glass R lands right of B.
  TEST_ASSERT_TRUE(r0 > b0 && r5 > b5);
  TEST_ASSERT_TRUE(r0 - b0 >= r5 - b5 + 3);
  // The channels straddle the green one, each as far as the other.
  TEST_ASSERT_INT_WITHIN(1, g0 - b0, r0 - g0);
  TEST_ASSERT_INT_WITHIN(1, g5 - b5, r5 - g5);
}

void test_crt_strike_should_mirror_the_fringing_across_the_centreline(void) {
  memset(mock_framebuffer, 0xC0, sizeof(mock_framebuffer));
  for (int y = 0; y < SCREEN_H; y++) mock_framebuffer[y * SCREEN_W + 79] = 0xFF;
  strike_gather_captured(10, false);
  int r = -1, b = -1;
  for (int x = 50; x < 100; x++) {
    uint8_t v = mock_framebuffer[113 * SCREEN_W + x];
    if (v == 0xF0) r = x;
    if (v == 0xC3) b = x;
  }
  TEST_ASSERT_TRUE(r >= 0 && b >= 0);
  TEST_ASSERT_TRUE(r < b);  // left half: R from the right of G
}

void test_crt_strike_should_leave_the_dead_zone_alone_at_zero_amplitude(void) {
  fill_test_frame(11);
  static uint8_t raw[SCREEN_W * SCREEN_H];
  memcpy(raw, mock_framebuffer, sizeof(raw));
  strike_gather_captured(14, false);
  // Inside the CA's dead zone, past the fringing's reach: the frame as it was.
  for (int y = 100; y < 128; y++) {
    for (int x = 60; x < 140; x++) {
      TEST_ASSERT_EQUAL_HEX8(raw[y * SCREEN_W + x], mock_framebuffer[y * SCREEN_W + x]);
    }
  }
}

void test_crt_strike_should_vignette_the_glass_after_the_warp(void) {
  s_settings_crt_curvature = 1;
  crt_strike_begin();
  memset(mock_framebuffer, 0xFF, sizeof(mock_framebuffer));  // white, bright to the rim
  crt_update_proc(NULL, s_fake_ctx);
  crt_strike_advance_to(14);
  crt_update_proc(NULL, s_fake_ctx);
  // The warp pulls bright interior content to the rim columns, and the rim is
  // dark on the glass all the same.
  for (int x = 3; x < 6; x++) {
    uint8_t v = mock_framebuffer[113 * SCREEN_W + x];
    TEST_ASSERT_TRUE(((v >> 4) & 3) <= 1 && ((v >> 2) & 3) <= 1 && (v & 3) <= 1);
  }
  TEST_ASSERT_EQUAL_HEX8(0xFF, mock_framebuffer[113 * SCREEN_W + 100]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, mock_framebuffer[SCREEN_W / 2 + 113 * SCREEN_W - 1]);
}

void test_crt_strike_should_not_vignette_without_curvature(void) {
  s_settings_crt_curvature = 0;
  crt_strike_begin();
  memset(mock_framebuffer, 0xFF, sizeof(mock_framebuffer));
  crt_update_proc(NULL, s_fake_ctx);
  crt_strike_advance_to(14);
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_HEX8(0xFF, mock_framebuffer[113 * SCREEN_W + 4]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, mock_framebuffer[4 * SCREEN_W + 100]);
}

void test_crt_strike_should_keep_the_canvas_out_of_the_frame_while_it_runs(void) {
  crt_strike_begin();
  fill_test_frame(5);
  crt_update_proc(NULL, s_fake_ctx);
  int fills = mock_fill_rect_count;
  int captures = mock_fb_capture_count;
  memset(mock_framebuffer, 0x5A, sizeof(mock_framebuffer));
  canvas_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(fills, mock_fill_rect_count);
  TEST_ASSERT_EQUAL_INT(captures, mock_fb_capture_count);
  TEST_ASSERT_EQUAL_HEX8(0x5A, mock_framebuffer[0]);
}

// The canvas repaints from scratch: it draws, not blits a stale scene.
static void assert_canvas_repaints(void) {
  int fills = mock_fill_rect_count;
  canvas_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_TRUE(mock_fill_rect_count > fills);
}

void test_crt_strike_should_hand_the_canvas_cache_back_when_it_ends(void) {
  crt_strike_begin();
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_NOT_NULL(s_strike_src);
  while (s_flash_phase != CRT_FLASH_IDLE) mock_timer_callback(NULL);
  TEST_ASSERT_NULL(s_strike_src);
  TEST_ASSERT_FALSE(s_scene_cache_borrowed);
  TEST_ASSERT_FALSE(s_scene_cache_valid);
  assert_canvas_repaints();
  TEST_ASSERT_TRUE(s_scene_cache_valid);
}

void test_crt_strike_should_hand_the_canvas_cache_back_when_cancelled(void) {
  crt_strike_begin();
  crt_update_proc(NULL, s_fake_ctx);
  s_settings_crt_degauss = 0;
  crt_apply_setting_change();
  TEST_ASSERT_NULL(s_strike_src);
  TEST_ASSERT_FALSE(s_scene_cache_borrowed);
  assert_canvas_repaints();

  s_settings_crt_degauss = 1;
  s_scene_cache_valid = false;
  crt_wake_by_shake();
  crt_update_proc(NULL, s_fake_ctx);
  crt_fire_strike_start();
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_NOT_NULL(s_strike_src);
  crt_deinit();
  TEST_ASSERT_NULL(s_strike_src);
  TEST_ASSERT_FALSE(s_scene_cache_borrowed);
  assert_canvas_repaints();
}

void test_crt_strike_should_recapture_after_a_reprime(void) {
  static uint8_t second[SCREEN_W * SCREEN_H];
  crt_strike_begin();
  fill_test_frame(1);
  crt_update_proc(NULL, s_fake_ctx);
  crt_strike_advance_to(4);

  crt_wake_by_shake();  // re-prime mid-strike
  TEST_ASSERT_NULL(s_strike_src);
  TEST_ASSERT_FALSE(s_scene_cache_borrowed);
  crt_update_proc(NULL, s_fake_ctx);  // the wake frame
  crt_fire_strike_start();
  fill_test_frame(2);
  memcpy(second, mock_framebuffer, sizeof(second));
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_PTR(s_scene_cache, s_strike_src);
  TEST_ASSERT_EQUAL_MEMORY(second, s_scene_cache, sizeof(second));
}

// The face's layers as main.c would have created them, for the strike's skip.
static char s_fake_window_storage;
static void crt_stage_face_layers(void) {
  s_main_window = (Window*)&s_fake_window_storage;
  s_canvas_layer = layer_create(GRect(0, 0, 200, 228));
  s_time_layer = text_layer_create(GRect(0, 0, 200, 64));
}

static void assert_face_layers_drawn(void) {
  TEST_ASSERT_FALSE(s_strike_layers_skipped);
  TEST_ASSERT_FALSE(layer_get_hidden(s_canvas_layer));
  TEST_ASSERT_FALSE(layer_get_hidden(text_layer_get_layer(s_time_layer)));
}

static void assert_face_layers_skipped(void) {
  TEST_ASSERT_TRUE(s_strike_layers_skipped);
  TEST_ASSERT_TRUE(layer_get_hidden(s_canvas_layer));
  TEST_ASSERT_TRUE(layer_get_hidden(text_layer_get_layer(s_time_layer)));
  TEST_ASSERT_EQUAL_UINT32(GColorClear, mock_window_last_bg);
}

static void assert_face_layers_restored(void) {
  assert_face_layers_drawn();
  TEST_ASSERT_EQUAL_UINT32(s_active_theme->center_bg, mock_window_last_bg);
}

void test_crt_strike_should_skip_the_face_layers_from_the_frame_after_the_capture(void) {
  crt_stage_face_layers();
  crt_strike_begin();
  assert_face_layers_drawn();
  TEST_ASSERT_EQUAL_INT(0, mock_window_set_bg_count);

  crt_update_proc(NULL, s_fake_ctx);  // phase 0: the capture needs the layers drawn
  TEST_ASSERT_NOT_NULL(s_strike_src);
  assert_face_layers_drawn();

  mock_timer_callback(NULL);
  TEST_ASSERT_EQUAL_INT(1, s_flash_phase);
  assert_face_layers_skipped();
  int bg_sets = mock_window_set_bg_count;
  crt_strike_advance_to(CRT_FLASH_PHASES - 1);
  assert_face_layers_skipped();
  TEST_ASSERT_EQUAL_INT(bg_sets, mock_window_set_bg_count);

  mock_timer_callback(NULL);  // the last tick releases the capture
  TEST_ASSERT_EQUAL_INT(CRT_FLASH_IDLE, s_flash_phase);
  assert_face_layers_restored();
}

void test_crt_strike_should_leave_the_window_background_to_itself_across_a_refresh(void) {
  crt_stage_face_layers();
  crt_strike_begin();
  crt_update_proc(NULL, s_fake_ctx);
  mock_timer_callback(NULL);
  assert_face_layers_skipped();

  s_active_theme = &s_theme_panel;
  refresh_state();
  assert_face_layers_skipped();

  while (s_flash_phase != CRT_FLASH_IDLE) mock_timer_callback(NULL);
  assert_face_layers_restored();
  TEST_ASSERT_EQUAL_UINT32(s_active_theme->center_bg, mock_window_last_bg);
}

void test_crt_strike_should_restore_the_face_layers_when_degauss_goes_off(void) {
  crt_stage_face_layers();
  crt_strike_begin();
  crt_update_proc(NULL, s_fake_ctx);
  mock_timer_callback(NULL);
  assert_face_layers_skipped();

  s_settings_crt_degauss = 0;
  crt_apply_setting_change();
  assert_face_layers_restored();
}

void test_crt_strike_should_restore_the_face_layers_when_re_primed(void) {
  crt_stage_face_layers();
  crt_strike_begin();
  crt_update_proc(NULL, s_fake_ctx);
  mock_timer_callback(NULL);
  assert_face_layers_skipped();

  crt_wake_by_shake();
  assert_face_layers_restored();
  crt_update_proc(NULL, s_fake_ctx);  // the wake frame
  crt_fire_strike_start();
  crt_update_proc(NULL, s_fake_ctx);  // the new capture frame is drawn whole
  assert_face_layers_drawn();
  mock_timer_callback(NULL);
  assert_face_layers_skipped();
}

void test_crt_strike_should_restore_the_face_layers_on_deinit(void) {
  crt_stage_face_layers();
  crt_strike_begin();
  crt_update_proc(NULL, s_fake_ctx);
  mock_timer_callback(NULL);
  assert_face_layers_skipped();

  crt_deinit();
  assert_face_layers_restored();
}

void test_crt_strike_should_not_skip_the_face_layers_without_a_capture(void) {
  crt_stage_face_layers();
  s_settings_crt_degauss = 1;
  s_active_theme = &s_theme_shadow;
  crt_app_focus_did_handler(true);
  s_crt_layer = layer_create(GRect(0, 0, 200, 228));
  crt_wake_by_shake();
  crt_update_proc(NULL, s_fake_ctx);
  crt_fire_strike_start();
  crt_update_proc(NULL, s_fake_ctx);  // no scene cache to borrow
  TEST_ASSERT_NULL(s_strike_src);
  mock_timer_callback(NULL);
  assert_face_layers_drawn();
  TEST_ASSERT_EQUAL_INT(0, mock_window_set_bg_count);
}

void test_crt_strike_should_bake_the_curvature_into_its_capture_once(void) {
  static uint8_t raw[SCREEN_W * SCREEN_H];
  static uint8_t curved[SCREEN_W * SCREEN_H];
  s_settings_crt_curvature = 1;
  crt_strike_begin();
  fill_test_frame(3);
  memcpy(raw, mock_framebuffer, sizeof(raw));
  curve_reference(curved, raw);
  TEST_ASSERT_TRUE(memcmp(raw, curved, sizeof(raw)) != 0);

  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_MEMORY(curved, s_scene_cache, sizeof(curved));

  // Later frames gather from the capture as it is: a changed live frame is
  // neither read nor warped into it.
  fill_test_frame(4);
  crt_strike_advance_to(2);
  crt_update_proc(NULL, s_fake_ctx);
  crt_strike_advance_to(11);
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_MEMORY(curved, s_scene_cache, sizeof(curved));
}

void test_crt_strike_should_not_bake_without_curvature(void) {
  static uint8_t raw[SCREEN_W * SCREEN_H];
  s_settings_crt_curvature = 0;
  crt_strike_begin();
  fill_test_frame(3);
  memcpy(raw, mock_framebuffer, sizeof(raw));
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_MEMORY(raw, s_scene_cache, sizeof(raw));
}

void test_crt_strike_should_bake_again_after_a_reprime(void) {
  static uint8_t second[SCREEN_W * SCREEN_H];
  static uint8_t curved[SCREEN_W * SCREEN_H];
  s_settings_crt_curvature = 1;
  crt_strike_begin();
  fill_test_frame(1);
  crt_update_proc(NULL, s_fake_ctx);
  crt_strike_advance_to(4);

  crt_wake_by_shake();
  crt_update_proc(NULL, s_fake_ctx);
  crt_fire_strike_start();
  fill_test_frame(2);
  memcpy(second, mock_framebuffer, sizeof(second));
  curve_reference(curved, second);
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_MEMORY(curved, s_scene_cache, sizeof(curved));
}

void test_crt_curvature_pass_should_warp_each_row_then_lay_the_pane(void) {
  static uint8_t raw[SCREEN_W * SCREEN_H];
  static uint8_t expected[SCREEN_W * SCREEN_H];
  s_active_theme = &s_theme_shadow;
  fill_test_frame(6);
  memcpy(raw, mock_framebuffer, sizeof(raw));
  curve_reference(expected, raw);
  vignette_pane(expected, SCREEN_W, SCREEN_H, SCREEN_W);
  crt_apply_framebuffer_passes(mock_framebuffer, SCREEN_W, SCREEN_H, SCREEN_W, CRT_FLASH_IDLE,
                               false, true);
  TEST_ASSERT_EQUAL_MEMORY(expected, mock_framebuffer, sizeof(expected));
}

// The pane's darkening of `in` at glass (x, y), worked out from the definition
// alone: the content position the warp shows there, the falloff blended between
// the two columns it lies between, the Bayer phase of the glass pixel, the
// channels dithered a third of a cycle apart.
static int pane_expected_falloff(int sx, int y) {
  const int w = SCREEN_W, h = SCREEN_H;
  const int ex = sx < w - 1 - sx ? sx : w - 1 - sx;
  const int ey = y < h - 1 - y ? y : h - 1 - y;
  const int dxd = edge_depth_x(ex), dyd = edge_depth_y(ey);
  const int d = (dxd < CRT_CORNER_RADIUS && dyd < CRT_CORNER_RADIUS) ? corner_depth(dxd, dyd)
                : dxd < dyd                                          ? dxd
                                                                     : dyd;
  return crt_vignette_q8_from_depth(d);
}

static uint8_t pane_expected(uint8_t in, int x, int y) {
  const int w = SCREEN_W, h = SCREEN_H;
  const int dx = 2 * x - (w - 1), dy = 2 * y - (h - 1);
  const int xq = dx * dx * 256 / ((w - 1) * (w - 1));
  const int yq = dy * dy * 256 / ((h - 1) * (h - 1));
  const int S = (w - 1) * 65536 + dx * (65536 + CRT_WARP_R2_K * (xq + yq));
  int sx = S >> 17, fr = (S >> 9) & 0xFF;
  // Inside a frame stroke's rigid zone the glass shows the stroke translated,
  // and the pane follows what is shown.
  int nrz;
  const CrtRigidZone* rz = rigid_zones(w, h, y, &nrz);
  for (int k = 0; k < nrz; k++) {
    if (x >= rz[k].xa && x <= rz[k].xb) {
      sx = (x * 256 + rz[k].d) >> 8;
      fr = (x * 256 + rz[k].d) & 0xFF;
    }
  }
  if (sx < 0) sx = fr = 0;
  if (sx >= w - 1) sx = w - 1, fr = 0;
  const int f = (pane_expected_falloff(sx, y) * (256 - fr) +
                 pane_expected_falloff(sx < w - 1 ? sx + 1 : sx, y) * fr) >>
                8;
  const int gx = x < w - 1 - x ? x : w - 1 - x;
  const int ey = y < h - 1 - y ? y : h - 1 - y;
  const int t = s_bayer4[(ey & 3) * 4 + (gx & 3)];
  return GCOLOR8_ALPHA | (vignette_level((in >> 4) & 3, f, t) << 4) |
         (vignette_level((in >> 2) & 3, f, (t + 5) & 15) << 2) |
         vignette_level(in & 3, f, (t + 10) & 15);
}

static const WatchTheme* const s_pane_themes[] = {&s_theme_shadow, &s_theme_dialog,
                                                  &s_theme_navigator, &s_theme_panel};

void test_crt_pane_should_darken_each_pixel_by_the_falloff_at_the_content_position_the_warp_shows(
    void) {
  static uint8_t in[SCREEN_W * SCREEN_H];
  for (size_t th = 0; th < sizeof(s_pane_themes) / sizeof(s_pane_themes[0]); th++) {
    s_active_theme = s_pane_themes[th];
    fill_test_frame(21 + th);
    memcpy(in, mock_framebuffer, sizeof(in));
    vignette_pane(mock_framebuffer, SCREEN_W, SCREEN_H, SCREEN_W);
    for (int y = 0; y < SCREEN_H; y++) {
      for (int x = 0; x < SCREEN_W; x++) {
        uint8_t want = pane_expected(in[y * SCREEN_W + x], x, y);
        if (mock_framebuffer[y * SCREEN_W + x] != want) {
          char msg[64];
          snprintf(msg, sizeof(msg), "theme %d x %d y %d", (int)th, x, y);
          TEST_ASSERT_EQUAL_HEX8_MESSAGE(want, mock_framebuffer[y * SCREEN_W + x], msg);
        }
      }
    }
  }
}

void test_crt_pane_should_bend_with_the_warp_not_follow_the_glass_edge(void) {
  // Without the warp the glass column 5 would sit at side depth 5; the warp
  // shows content column 0 there, which is black, and column 19 sits where the
  // content's own column 20 does.
  s_active_theme = &s_theme_shadow;
  memset(mock_framebuffer, 0xFF, sizeof(mock_framebuffer));
  vignette_pane(mock_framebuffer, SCREEN_W, SCREEN_H, SCREEN_W);
  int black_run = 0;
  while (mock_framebuffer[113 * SCREEN_W + black_run] == GCOLOR8_ALPHA) black_run++;
  TEST_ASSERT_TRUE(black_run >= 3);
  const PaneRow r = {.tb = crt_tables(SCREEN_W),
                     .w = SCREEN_W,
                     .yterm = (2 * 113 - (SCREEN_H - 1)) * (2 * 113 - (SCREEN_H - 1)) * 256 /
                              ((SCREEN_H - 1) * (SCREEN_H - 1))};
  int fr;
  for (int x = 0; x < SCREEN_W / 2; x += 7) {
    TEST_ASSERT_TRUE(pane_column(&r, x, &fr) <= x);
  }
  TEST_ASSERT_TRUE(pane_column(&r, 10, &fr) < 10);
}

void test_crt_pane_should_not_depend_on_the_content_it_darkens(void) {
  static uint8_t flat[SCREEN_W * SCREEN_H];
  static uint8_t noise[SCREEN_W * SCREEN_H];
  static uint8_t out[SCREEN_W * SCREEN_H];
  for (size_t th = 0; th < sizeof(s_pane_themes) / sizeof(s_pane_themes[0]); th++) {
    s_active_theme = s_pane_themes[th];
    fill_test_frame(33 + th);
    memcpy(noise, mock_framebuffer, sizeof(noise));
    memcpy(out, noise, sizeof(out));
    vignette_pane(out, SCREEN_W, SCREEN_H, SCREEN_W);
    // Every pixel value, grey or coloured, on its own flat frame: wherever the
    // noisy frame carries that value, the pane gave it the same result.
    for (int v = 0; v < 64; v++) {
      memset(flat, GCOLOR8_ALPHA | v, sizeof(flat));
      vignette_pane(flat, SCREEN_W, SCREEN_H, SCREEN_W);
      for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        if ((noise[i] & 63) == v) TEST_ASSERT_EQUAL_HEX8(flat[i], out[i]);
      }
    }
  }
}

void test_crt_pane_falloff_should_rise_toward_the_interior_and_not_step_between_rows(void) {
  // Along a vertical stroke the pane's gain at a glass column may not jump
  // from one row to the next: the warp's scale changes per row, and a falloff
  // read at the floored content column would step by a whole LUT interval
  // (up to 22/256) each time. Within a row it only ever rises inward.
  s_active_theme = &s_theme_shadow;
  const CrtTables* tb = crt_tables(SCREEN_W);
  const uint16_t* lut = vignette_lut(theme_ground_level(s_active_theme));
  static int gain[SCREEN_H][SCREEN_W / 2];
  for (int y = 0; y < SCREEN_H; y++) {
    const int edge_px = y < SCREEN_H - 1 - y ? y : SCREEN_H - 1 - y;
    const int dy = 2 * y - (SCREEN_H - 1);
    PaneRow r = {.tb = tb,
                 .lut = lut,
                 .w = SCREEN_W,
                 .yterm = dy * dy * 256 / ((SCREEN_H - 1) * (SCREEN_H - 1)),
                 .ey_depth = edge_depth_y(edge_px)};
    r.corner_row = r.ey_depth < CRT_CORNER_RADIUS ? tb->corner_d[r.ey_depth] : NULL;
    for (int x = 0; x < SCREEN_W / 2; x++) {
      int fr;
      const int sx = pane_column(&r, x, &fr);
      gain[y][x] = pane_gain(&r, sx, fr);
      if (x > 0) TEST_ASSERT_TRUE(gain[y][x] >= gain[y][x - 1]);
    }
  }
  for (int y = CRT_VIGNETTE_BAND_PX + 2; y < SCREEN_H - CRT_VIGNETTE_BAND_PX - 3; y++) {
    for (int x = 0; x < SCREEN_W / 2; x++) {
      TEST_ASSERT_TRUE(abs(gain[y + 1][x] - gain[y][x]) <= 2);
    }
  }
}

void test_crt_pane_should_leave_the_interior_alone(void) {
  static uint8_t in[SCREEN_W * SCREEN_H];
  for (size_t th = 0; th < sizeof(s_pane_themes) / sizeof(s_pane_themes[0]); th++) {
    s_active_theme = s_pane_themes[th];
    fill_test_frame(45 + th);
    memcpy(in, mock_framebuffer, sizeof(in));
    vignette_pane(mock_framebuffer, SCREEN_W, SCREEN_H, SCREEN_W);
    for (int y = CRT_VIGNETTE_BAND_PX + 2; y < SCREEN_H - CRT_VIGNETTE_BAND_PX - 2; y++) {
      for (int x = CRT_VIGNETTE_SIDE_PX + 6; x < SCREEN_W - CRT_VIGNETTE_SIDE_PX - 6; x++) {
        TEST_ASSERT_EQUAL_HEX8(in[y * SCREEN_W + x], mock_framebuffer[y * SCREEN_W + x]);
      }
    }
  }
}

void test_crt_pane_should_follow_the_row_stride(void) {
  enum { PAD = 8 };
  static uint8_t packed[SCREEN_W * SCREEN_H];
  static uint8_t padded[(SCREEN_W + PAD) * SCREEN_H];
  s_active_theme = &s_theme_dialog;
  fill_test_frame(8);
  memcpy(packed, mock_framebuffer, sizeof(packed));
  for (int y = 0; y < SCREEN_H; y++) {
    memcpy(&padded[y * (SCREEN_W + PAD)], &packed[y * SCREEN_W], SCREEN_W);
    memset(&padded[y * (SCREEN_W + PAD) + SCREEN_W], 0x5A, PAD);
  }
  vignette_pane(packed, SCREEN_W, SCREEN_H, SCREEN_W);
  vignette_pane(padded, SCREEN_W, SCREEN_H, SCREEN_W + PAD);
  for (int y = 0; y < SCREEN_H; y++) {
    TEST_ASSERT_EQUAL_MEMORY(&packed[y * SCREEN_W], &padded[y * (SCREEN_W + PAD)], SCREEN_W);
  }
}

void test_crt_strike_last_frame_should_meet_the_next_normal_frame_at_the_rim(void) {
  // Rows of flat colour: no displacement, fringe or stripe changes a pixel in
  // the strike's last frame, so what is left to differ is the pane.
  static uint8_t src[SCREEN_W * SCREEN_H];
  static uint8_t baked[SCREEN_W * SCREEN_H];
  static uint8_t normal[SCREEN_W * SCREEN_H];
  for (size_t th = 0; th < sizeof(s_pane_themes) / sizeof(s_pane_themes[0]); th++) {
    s_active_theme = s_pane_themes[th];
    uint32_t seed = 17 + th;
    for (int y = 0; y < SCREEN_H; y++) {
      seed = seed * 1664525u + 1013904223u;
      memset(src + y * SCREEN_W, GCOLOR8_ALPHA | ((seed >> 16) & 0x3F), SCREEN_W);
    }
    memcpy(normal, src, sizeof(normal));
    crt_apply_framebuffer_passes(normal, SCREEN_W, SCREEN_H, SCREEN_W, CRT_FLASH_IDLE, false, true);

    curve_reference(baked, src);
    s_strike_src = baked;
    memset(mock_framebuffer, 0xA5, sizeof(mock_framebuffer));
    crt_apply_framebuffer_passes(mock_framebuffer, SCREEN_W, SCREEN_H, SCREEN_W,
                                 CRT_FLASH_PHASES - 1, false, true);
    s_strike_src = NULL;
    TEST_ASSERT_EQUAL_MEMORY(normal, mock_framebuffer, sizeof(normal));
  }
}

void test_crt_pane_should_not_darken_without_curvature(void) {
  s_settings_crt_curvature = 0;
  memset(mock_framebuffer, 0xFF, sizeof(mock_framebuffer));
  crt_apply_framebuffer_passes(mock_framebuffer, SCREEN_W, SCREEN_H, SCREEN_W, CRT_FLASH_IDLE, true,
                               false);
  TEST_ASSERT_EQUAL_HEX8(0xFF, mock_framebuffer[113 * SCREEN_W + 4]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, mock_framebuffer[4 * SCREEN_W + 100]);
}

void test_crt_strike_without_a_scene_cache_should_run_the_passes_in_place(void) {
  static uint8_t expected[SCREEN_W * SCREEN_H];
  s_settings_crt_degauss = 1;
  s_active_theme = &s_theme_shadow;
  crt_app_focus_did_handler(true);
  s_crt_layer = layer_create(GRect(0, 0, 200, 228));
  TEST_ASSERT_NULL(s_scene_cache);
  crt_wake_by_shake();
  crt_update_proc(NULL, s_fake_ctx);
  crt_fire_strike_start();

  fill_test_frame(9);
  memcpy(expected, mock_framebuffer, sizeof(expected));
  crt_apply_framebuffer_passes(expected, SCREEN_W, SCREEN_H, SCREEN_W, 0, false, false);
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_NULL(s_strike_src);
  TEST_ASSERT_EQUAL_MEMORY(expected, mock_framebuffer, sizeof(expected));
  while (s_flash_phase != CRT_FLASH_IDLE) mock_timer_callback(NULL);
  TEST_ASSERT_FALSE(s_scene_cache_borrowed);
}

void test_canvas_cache_borrow_should_need_a_cache_and_force_a_repaint_on_return(void) {
  TEST_ASSERT_NULL(canvas_cache_borrow());
  TEST_ASSERT_FALSE(s_scene_cache_borrowed);

  canvas_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_TRUE(s_scene_cache_valid);
  TEST_ASSERT_EQUAL_PTR(s_scene_cache, canvas_cache_borrow());
  TEST_ASSERT_FALSE(s_scene_cache_valid);
  canvas_cache_return();
  TEST_ASSERT_FALSE(s_scene_cache_borrowed);
  assert_canvas_repaints();
}

void test_crt_setting_push_should_redraw_the_overlay(void) {
  s_crt_layer = layer_create(GRect(0, 0, 200, 228));
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_SETTINGS_CRT_DEGAUSS, 1);
  mock_mark_dirty_count = 0;

  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(1, s_settings_crt_degauss);
  TEST_ASSERT_EQUAL_INT(1, persist_read_int(PERSIST_KEY_SETTINGS_CRT_DEGAUSS));
  TEST_ASSERT_TRUE(mock_mark_dirty_count > 0);

  // A repeat push with the same value keeps the overlay paint budget flat.
  mock_mark_dirty_count = 0;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(0, mock_mark_dirty_count);
}

void test_crt_sound_should_play_at_the_configured_volume_and_stay_silent_at_zero(void) {
  s_settings_crt_degauss = 1;
  crt_app_focus_did_handler(true);

  s_settings_degauss_volume = 25;
  crt_play_strike_sound();
  TEST_ASSERT_EQUAL_INT(1, mock_speaker_play_tracks_count);
  TEST_ASSERT_EQUAL_UINT8(25, mock_speaker_last_volume);

  s_settings_degauss_volume = 250;  // settings write arbitrary ints
  crt_play_strike_sound();
  TEST_ASSERT_EQUAL_UINT8(100, mock_speaker_last_volume);

  s_settings_degauss_volume = 0;
  crt_play_strike_sound();
  TEST_ASSERT_EQUAL_INT(2, mock_speaker_play_tracks_count);

  s_settings_degauss_volume = 50;
  s_settings_crt_degauss = 0;  // the volume is moot without the strike
  crt_play_strike_sound();
  TEST_ASSERT_EQUAL_INT(2, mock_speaker_play_tracks_count);
}

void test_crt_degauss_off_should_end_a_strike_but_keep_the_other_effects(void) {
  s_settings_crt_curvature = 1;
  s_settings_crt_degauss = 1;
  crt_app_focus_did_handler(true);
  s_crt_layer = layer_create(GRect(0, 0, 200, 228));
  crt_wake_by_shake();
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_NOT_EQUAL(CRT_FLASH_IDLE, s_flash_phase);

  s_settings_crt_degauss = 0;
  static char fake_window;
  s_main_window = (Window*)&fake_window;
  mock_window_set_bg_count = 0;
  crt_apply_setting_change();
  TEST_ASSERT_EQUAL_INT(CRT_FLASH_IDLE, s_flash_phase);
  TEST_ASSERT_NULL(s_flash_timer);
  TEST_ASSERT_EQUAL_INT(0, mock_window_set_bg_count);  // curvature still paints

  s_settings_crt_curvature = 0;
  crt_apply_setting_change();
  TEST_ASSERT_TRUE(mock_window_set_bg_count > 0);  // the last effect off erases
}

void test_crt_overlay_should_capture_only_while_something_paints(void) {
  s_active_theme = &s_theme_shadow;
  crt_app_focus_did_handler(true);

  s_settings_crt_degauss = 1;  // between strikes a degauss-only face is left alone
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(0, mock_fb_capture_count);

  crt_wake_by_shake();  // the strike starts after the wake frame...
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(0, mock_fb_capture_count);
  TEST_ASSERT_EQUAL_INT(0, s_flash_phase);
  crt_update_proc(NULL, s_fake_ctx);  // ...and every frame of it paints
  TEST_ASSERT_EQUAL_INT(1, mock_fb_capture_count);

  s_settings_crt_degauss = 0;
  crt_apply_setting_change();
  s_settings_crt_aberration = 1;
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(2, mock_fb_capture_count);

  s_settings_crt_aberration = 0;
  s_settings_crt_curvature = 1;
  crt_update_proc(NULL, s_fake_ctx);
  TEST_ASSERT_EQUAL_INT(3, mock_fb_capture_count);
}

void test_crt_backlight_should_strike_only_with_the_degauss_effect(void) {
  s_settings_crt_aberration = 1;
  s_settings_crt_curvature = 1;
  crt_app_focus_did_handler(true);
  crt_wake_by_shake();
  TEST_ASSERT_FALSE(s_strike_pending);

  s_settings_crt_degauss = 1;
  crt_wake_by_shake();
  TEST_ASSERT_TRUE(s_strike_pending);
}

void test_crt_toggle_off_should_force_a_full_repaint(void) {
  // The shader's pixels live in the shared framebuffer; marking only the
  // overlay dirty would leave them standing until the next minute render.
  // Toggling off re-applies the window background, dirtying the whole tree.
  // The mock's window_create returns NULL; the branch under test needs a
  // non-null window.
  static char fake_window;
  s_main_window = (Window*)&fake_window;
  s_settings_crt_degauss = 1;
  mock_window_set_bg_count = 0;
  crt_apply_setting_change();
  TEST_ASSERT_EQUAL_INT(0, mock_window_set_bg_count);  // on: overlay mark only

  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_SETTINGS_CRT_DEGAUSS, 0);
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(0, s_settings_crt_degauss);
  TEST_ASSERT_EQUAL_INT(CRT_FLASH_IDLE, s_flash_phase);
  TEST_ASSERT_TRUE(mock_window_set_bg_count > 0);
}

void test_crt_toggle_off_mid_strike_should_cancel_the_chain(void) {
  // Toggle off mid-strike: the pending tick must be cancelled — otherwise the
  // chain re-arms from IDLE (phase −1 → 0) and runs a full strike
  // nobody triggered, dirtying the layer each frame.
  s_settings_crt_degauss = 1;
  crt_app_focus_did_handler(true);
  s_crt_layer = layer_create(GRect(0, 0, 200, 228));
  crt_wake_by_shake();                // primes the strike
  crt_update_proc(NULL, s_fake_ctx);  // wake frame lands → chain armed
  crt_fire_strike_start();
  mock_timer_callback(NULL);  // advance one tick for realism
  int cancel_before = mock_timer_cancel_count;

  s_settings_crt_degauss = 0;
  crt_apply_setting_change();

  TEST_ASSERT_EQUAL_INT(CRT_FLASH_IDLE, s_flash_phase);
  TEST_ASSERT_EQUAL_INT(cancel_before + 1, mock_timer_cancel_count);
  TEST_ASSERT_NULL(s_flash_timer);
}

// -----------------------------------------------------------------------------
// Weather age, stamping, and settings-change discards
// -----------------------------------------------------------------------------

static time_t local_time_at(int month, int day, int hour, int min) {
  struct tm tm = {.tm_year = 126,
                  .tm_mon = month - 1,
                  .tm_mday = day,
                  .tm_hour = hour,
                  .tm_min = min,
                  .tm_isdst = -1};
  return mktime(&tm);
}

// Moves the mock clock to `target`.
static void mock_clock_to(time_t target) {
  mock_time_offset = target - (time(NULL) - mock_time_offset);
}

static void set_held_weather(void) {
  s_weather_temp = 72;
  s_weather_cond_code = 0;
  s_weather_humidity = 40;
  s_temp_high = 80;
  s_temp_low = 60;
  s_temp_high_tmrw = 82;
  s_temp_low_tmrw = 62;
  s_hi_hour_today = 15;
  s_lo_hour_today = 5;
  s_hi_hour_tmrw = 14;
  s_lo_hour_tmrw = 6;
}

void test_weather_age_should_judge_by_age_and_local_day(void) {
  time_t now = local_time_at(7, 1, 15, 0);
  TEST_ASSERT_EQUAL_INT(WEATHER_AGE_FRESH, weather_age(now - 600, now));
  TEST_ASSERT_EQUAL_INT(WEATHER_AGE_FRESH, weather_age(now, now));
  TEST_ASSERT_EQUAL_INT(WEATHER_AGE_FRESH, weather_age(now - WEATHER_STALE_AGE_S, now));
  TEST_ASSERT_EQUAL_INT(WEATHER_AGE_STALE, weather_age(now - WEATHER_STALE_AGE_S - 1, now));
  TEST_ASSERT_EQUAL_INT(WEATHER_AGE_STALE, weather_age(0, now));
  TEST_ASSERT_EQUAL_INT(WEATHER_AGE_FRESH, weather_age(now + WEATHER_STAMP_FUTURE_SLACK_S, now));
  TEST_ASSERT_EQUAL_INT(WEATHER_AGE_STALE,
                        weather_age(now + WEATHER_STAMP_FUTURE_SLACK_S + 1, now));

  time_t after_midnight = local_time_at(7, 2, 0, 30);
  TEST_ASSERT_EQUAL_INT(WEATHER_AGE_PREVIOUS_DAY,
                        weather_age(after_midnight - 3600, after_midnight));
  TEST_ASSERT_EQUAL_INT(WEATHER_AGE_FRESH, weather_age(after_midnight - 600, after_midnight));
  // The two-hour cut outranks the day rollover.
  TEST_ASSERT_EQUAL_INT(WEATHER_AGE_STALE,
                        weather_age(after_midnight - WEATHER_STALE_AGE_S - 1, after_midnight));
}

void test_weather_stamp_for_should_trust_only_sane_fetch_times(void) {
  time_t now = local_time_at(7, 1, 15, 0);
  TEST_ASSERT_EQUAL_INT(now - 900, weather_stamp_for((int32_t)(now - 900), now));
  TEST_ASSERT_EQUAL_INT(now + 60, weather_stamp_for((int32_t)(now + 60), now));
  TEST_ASSERT_EQUAL_INT(now, weather_stamp_for((int32_t)(now + 61), now));
  TEST_ASSERT_EQUAL_INT(now, weather_stamp_for(0, now));
  TEST_ASSERT_EQUAL_INT(now, weather_stamp_for(-5, now));
}

void test_weather_expire_should_keep_everything_while_fresh(void) {
  set_held_weather();
  time_t now = local_time_at(7, 1, 15, 0);
  s_weather_stamp = (int)(now - 600);
  weather_expire(now);
  TEST_ASSERT_EQUAL_INT(72, s_weather_temp);
  TEST_ASSERT_EQUAL_INT(80, s_temp_high);
  TEST_ASSERT_EQUAL_INT(14, s_hi_hour_tmrw);
}

void test_weather_expire_should_blank_every_reading_when_stale(void) {
  set_held_weather();
  time_t now = local_time_at(7, 1, 15, 0);
  s_weather_stamp = (int)(now - WEATHER_STALE_AGE_S - 1);
  weather_expire(now);
  for (unsigned i = 0; i < sizeof(s_weather_fields) / sizeof(s_weather_fields[0]); i++) {
    TEST_ASSERT_EQUAL_INT(s_weather_fields[i].sentinel, *s_weather_fields[i].target);
  }
  char buf[16];
  format_temp(buf, sizeof(buf), s_weather_temp, true);
  TEST_ASSERT_EQUAL_STRING("--", buf);
  TEST_ASSERT_EQUAL_STRING("--", weather_cond_word(s_weather_cond_code));
}

void test_weather_expire_should_drop_only_the_day_bound_readings_after_midnight(void) {
  set_held_weather();
  time_t now = local_time_at(7, 2, 0, 30);
  s_weather_stamp = (int)(now - 3600);
  weather_expire(now);
  TEST_ASSERT_EQUAL_INT(72, s_weather_temp);
  TEST_ASSERT_EQUAL_INT(40, s_weather_humidity);
  TEST_ASSERT_EQUAL_INT(WEATHER_TEMP_NONE, s_temp_high);
  TEST_ASSERT_EQUAL_INT(WEATHER_TEMP_NONE, s_temp_low);
  TEST_ASSERT_EQUAL_INT(WEATHER_TEMP_NONE, s_temp_high_tmrw);
  TEST_ASSERT_EQUAL_INT(WEATHER_TEMP_NONE, s_temp_low_tmrw);
  TEST_ASSERT_EQUAL_INT(WEATHER_NONE, s_hi_hour_today);
  TEST_ASSERT_EQUAL_INT(WEATHER_NONE, s_lo_hour_tmrw);
  char buf[16];
  format_high_low(buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("-- --", buf);
}

void test_tick_handler_should_show_dashes_for_weather_older_than_two_hours(void) {
  set_held_weather();
  s_weather_stamp = (int)(time(NULL) - WEATHER_STALE_AGE_S - 1);
  struct tm t = {0};
  t.tm_min = 14;
  tick_handler(&t, MINUTE_UNIT);
  char buf[40];
  get_source_data(DATA_SOURCE_WEATHER, buf, sizeof(buf), NULL);
  TEST_ASSERT_EQUAL_STRING("-- --", buf);
  TEST_ASSERT_EQUAL_INT(WEATHER_NONE, s_weather_humidity);
}

void test_tick_handler_should_not_present_yesterdays_hi_lo_as_todays(void) {
  struct tm tomorrow = *localtime(&(time_t){time(NULL)});
  tomorrow.tm_mday += 1;
  tomorrow.tm_hour = 0;
  tomorrow.tm_min = 5;
  tomorrow.tm_sec = 0;
  tomorrow.tm_isdst = -1;
  time_t just_after_midnight = mktime(&tomorrow);
  mock_clock_to(just_after_midnight);

  set_held_weather();
  s_weather_stamp = (int)(just_after_midnight - 1800);
  struct tm t = {0};
  t.tm_min = 5;
  tick_handler(&t, MINUTE_UNIT);

  char buf[24];
  format_high_low(buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("-- --", buf);
  TEST_ASSERT_EQUAL_INT(72, s_weather_temp);
}

void test_weather_cache_should_drop_extremes_cached_on_an_earlier_day(void) {
  mock_persist_reset();
  struct tm tomorrow = *localtime(&(time_t){time(NULL)});
  tomorrow.tm_mday += 1;
  tomorrow.tm_hour = 0;
  tomorrow.tm_min = 10;
  tomorrow.tm_sec = 0;
  tomorrow.tm_isdst = -1;
  time_t just_after_midnight = mktime(&tomorrow);
  mock_clock_to(just_after_midnight);

  set_held_weather();
  s_weather_stamp = (int)(just_after_midnight - 900);
  save_weather_cache();
  weather_clear();

  TEST_ASSERT_TRUE(load_weather_cache());
  TEST_ASSERT_EQUAL_INT(72, s_weather_temp);
  TEST_ASSERT_EQUAL_INT(WEATHER_TEMP_NONE, s_temp_high);
  TEST_ASSERT_EQUAL_INT(WEATHER_TEMP_NONE, s_temp_low_tmrw);
}

void test_weather_cache_should_restore_the_stamp_and_reject_old_data_by_it(void) {
  mock_persist_reset();
  set_held_weather();
  time_t now = time(NULL);
  s_weather_stamp = (int)(now - 120);
  save_weather_cache();
  weather_clear();
  TEST_ASSERT_TRUE(load_weather_cache());
  TEST_ASSERT_EQUAL_INT((int)(now - 120), s_weather_stamp);
}

void test_inbox_should_stamp_the_cache_with_the_phones_fetch_time(void) {
  mock_persist_reset();
  time_t now = time(NULL);
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, 0);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_FETCHED_AT, (int32_t)(now - 900));
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT((int)(now - 900), s_weather_stamp);
  TEST_ASSERT_EQUAL_INT((int)(now - 900), persist_read_int(PERSIST_KEY_WEATHER_TIMESTAMP));
  TEST_ASSERT_EQUAL_INT(72, s_weather_temp);
}

void test_inbox_should_stamp_with_arrival_time_when_the_fetch_time_is_missing_or_impossible(void) {
  time_t now = time(NULL);
  for (int pass = 0; pass < 3; pass++) {
    mock_persist_reset();
    s_weather_stamp = 0;
    mock_dict_reset();
    mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
    mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, 0);
    if (pass == 1) mock_dict_add_int(MESSAGE_KEY_WEATHER_FETCHED_AT, 0);
    if (pass == 2) mock_dict_add_int(MESSAGE_KEY_WEATHER_FETCHED_AT, (int32_t)(now + 3600));
    inbox_received_callback(NULL, NULL);
    TEST_ASSERT_TRUE(s_weather_stamp >= (int)now && s_weather_stamp <= (int)now + 5);
    TEST_ASSERT_EQUAL_INT(72, s_weather_temp);
  }
}

void test_inbox_should_treat_a_replayed_payload_older_than_two_hours_as_no_data(void) {
  mock_persist_reset();
  time_t now = time(NULL);
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_COND, 0);
  mock_dict_add_int(MESSAGE_KEY_WEATHER_FETCHED_AT, (int32_t)(now - WEATHER_STALE_AGE_S - 60));
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(WEATHER_TEMP_NONE, s_weather_temp);
  TEST_ASSERT_EQUAL_INT(WEATHER_NONE, s_weather_cond_code);
}

void test_inbox_settings_only_message_should_not_stamp_weather(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_THEME, "2");
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(0, s_weather_stamp);
}

void test_inbox_units_change_should_discard_held_weather_and_the_cache_stamp(void) {
  mock_persist_reset();
  set_held_weather();
  s_weather_stamp = (int)(time(NULL) - 60);
  save_weather_cache();

  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_UNITS, "1");
  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(WEATHER_TEMP_NONE, s_weather_temp);
  TEST_ASSERT_EQUAL_INT(WEATHER_NONE, s_weather_cond_code);
  TEST_ASSERT_EQUAL_INT(WEATHER_TEMP_NONE, s_temp_high);
  TEST_ASSERT_EQUAL_INT(0, s_weather_stamp);
  TEST_ASSERT_EQUAL_INT(0, persist_read_int(PERSIST_KEY_WEATHER_TIMESTAMP));
  TEST_ASSERT_FALSE(load_weather_cache());
  TEST_ASSERT_TRUE(mock_outbox_has(MESSAGE_KEY_WEATHER_REQUEST, 0));
}

void test_inbox_window_change_should_discard_held_weather(void) {
  mock_persist_reset();
  set_held_weather();
  s_weather_stamp = (int)(time(NULL) - 60);
  save_weather_cache();

  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_WEATHER_WINDOW, "24");
  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(WEATHER_TEMP_NONE, s_weather_temp);
  TEST_ASSERT_EQUAL_INT(0, persist_read_int(PERSIST_KEY_WEATHER_TIMESTAMP));
}

void test_inbox_unchanged_units_should_keep_held_weather(void) {
  mock_persist_reset();
  set_held_weather();
  s_weather_stamp = (int)(time(NULL) - 60);

  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_UNITS, "0");
  inbox_received_callback(NULL, NULL);

  TEST_ASSERT_EQUAL_INT(72, s_weather_temp);
  TEST_ASSERT_TRUE(s_weather_stamp != 0);
}

void test_weather_table_sentinels_should_equal_the_globals_boot_values(void) {
  for (unsigned i = 0; i < sizeof(s_weather_fields) / sizeof(s_weather_fields[0]); i++) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(s_boot_weather[i], s_weather_fields[i].sentinel,
                                  "wire-table sentinel differs from the global's boot value");
  }
}

// -----------------------------------------------------------------------------
// Fetch decisions
// -----------------------------------------------------------------------------

void test_weather_fetch_due_should_need_the_half_hour_edge_and_a_weather_slot(void) {
  TEST_ASSERT_TRUE(weather_fetch_due(0, true));
  TEST_ASSERT_TRUE(weather_fetch_due(30, true));
  TEST_ASSERT_FALSE(weather_fetch_due(15, true));
  TEST_ASSERT_FALSE(weather_fetch_due(59, true));
  TEST_ASSERT_FALSE(weather_fetch_due(0, false));
  TEST_ASSERT_FALSE(weather_fetch_due(30, false));
}

void test_weather_fetch_after_settings_should_follow_what_changed_and_what_is_shown(void) {
  //                                       needs units  window needed slots
  TEST_ASSERT_FALSE(weather_fetch_after_settings(false, true, true, false, true));
  TEST_ASSERT_FALSE(weather_fetch_after_settings(true, false, false, true, false));
  TEST_ASSERT_TRUE(weather_fetch_after_settings(true, true, false, true, false));
  TEST_ASSERT_TRUE(weather_fetch_after_settings(true, false, true, true, false));
  TEST_ASSERT_TRUE(weather_fetch_after_settings(true, false, false, false, false));
  TEST_ASSERT_TRUE(weather_fetch_after_settings(true, false, false, true, true));
}

// -----------------------------------------------------------------------------
// Retry budget
// -----------------------------------------------------------------------------

static void exhaust_retry_budget(void) {
  for (int i = 0; i < WEATHER_REQUEST_MAX_RETRIES; i++) {
    outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);
    mock_timer_callback(NULL);
  }
  TEST_ASSERT_EQUAL_INT(WEATHER_REQUEST_MAX_RETRIES, s_weather_request_retries);
}

void test_a_failed_send_should_not_spend_the_budget_until_its_retry_fires(void) {
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);
  TEST_ASSERT_EQUAL_INT(0, s_weather_request_retries);
  mock_timer_callback(NULL);
  TEST_ASSERT_EQUAL_INT(1, s_weather_request_retries);
}

void test_a_tick_request_should_start_the_retry_budget_over(void) {
  exhaust_retry_budget();
  struct tm t = {0};
  t.tm_min = 30;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(0, s_weather_request_retries);

  int arms = mock_timer_register_count;
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);
  TEST_ASSERT_EQUAL_INT(arms + 1, mock_timer_register_count);
}

void test_a_settings_request_should_start_the_retry_budget_over(void) {
  mock_persist_reset();
  exhaust_retry_budget();
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_UNITS, "1");
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(0, s_weather_request_retries);
}

void test_a_launch_request_should_start_the_retry_budget_over(void) {
  mock_persist_reset();
  exhaust_retry_budget();
  init();
  TEST_ASSERT_EQUAL_INT(0, s_weather_request_retries);
}

void test_weather_requests_should_not_send_while_the_phone_is_disconnected(void) {
  s_connected = false;

  request_weather();
  request_weather_retry();
  weather_retry_callback(NULL);
  TEST_ASSERT_EQUAL_INT(0, mock_outbox_sends);
  TEST_ASSERT_EQUAL_INT(0, mock_timer_register_count);

  struct tm t = {0};
  t.tm_min = 30;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(0, mock_outbox_sends);
  TEST_ASSERT_EQUAL_INT(0, mock_timer_register_count);
}

void test_init_should_not_fetch_weather_while_the_phone_is_disconnected(void) {
  mock_bt_connected = false;
  init();
  TEST_ASSERT_EQUAL_INT(0, mock_outbox_sends);
  TEST_ASSERT_EQUAL_INT(0, mock_timer_register_count);
}

void test_weather_fetch_on_reconnect_should_follow_the_stamp_age(void) {
  time_t now = 1000000;
  TEST_ASSERT_TRUE(weather_fetch_on_reconnect(true, 0, now, 0));
  TEST_ASSERT_TRUE(weather_fetch_on_reconnect(true, now - WEATHER_CACHE_MAX_AGE_S - 1, now, 0));
  TEST_ASSERT_TRUE(
      weather_fetch_on_reconnect(true, now + WEATHER_STAMP_FUTURE_SLACK_S + 1, now, 0));
  TEST_ASSERT_FALSE(weather_fetch_on_reconnect(true, now - WEATHER_CACHE_MAX_AGE_S, now, 0));
  TEST_ASSERT_FALSE(weather_fetch_on_reconnect(true, now, now, 0));
  TEST_ASSERT_FALSE(weather_fetch_on_reconnect(false, 0, now, 0));
}

void test_weather_fetch_on_reconnect_should_hold_the_minimum_gap(void) {
  time_t now = 1000000;
  time_t stale = now - WEATHER_CACHE_MAX_AGE_S - 1;
  TEST_ASSERT_FALSE(weather_fetch_on_reconnect(true, stale, now, now - 10));
  TEST_ASSERT_FALSE(
      weather_fetch_on_reconnect(true, stale, now, now - WEATHER_RECONNECT_MIN_GAP_S + 1));
  TEST_ASSERT_TRUE(weather_fetch_on_reconnect(true, stale, now, now - WEATHER_RECONNECT_MIN_GAP_S));
  TEST_ASSERT_TRUE(weather_fetch_on_reconnect(true, stale, now, now + 10));
}

void test_reconnect_should_request_weather_once_when_the_data_is_stale(void) {
  s_weather_stamp = (int)time(NULL) - WEATHER_CACHE_MAX_AGE_S - 60;
  handle_bluetooth(false);
  TEST_ASSERT_EQUAL_INT(0, mock_outbox_sends);

  handle_bluetooth(true);
  TEST_ASSERT_EQUAL_INT(1, mock_outbox_sends);
  TEST_ASSERT_TRUE(mock_outbox_has(MESSAGE_KEY_WEATHER_REQUEST, 0));
}

void test_reconnect_should_not_request_again_within_the_minimum_gap(void) {
  s_weather_stamp = (int)time(NULL) - WEATHER_CACHE_MAX_AGE_S - 60;
  handle_bluetooth(true);
  TEST_ASSERT_EQUAL_INT(1, mock_outbox_sends);

  mock_time_offset += 10;
  handle_bluetooth(false);
  handle_bluetooth(true);
  TEST_ASSERT_EQUAL_INT(1, mock_outbox_sends);

  mock_time_offset += 51;
  handle_bluetooth(false);
  handle_bluetooth(true);
  TEST_ASSERT_EQUAL_INT(2, mock_outbox_sends);
}

void test_reconnect_should_not_request_weather_when_the_data_is_fresh(void) {
  s_weather_stamp = (int)time(NULL) - 60;
  handle_bluetooth(false);
  handle_bluetooth(true);
  TEST_ASSERT_EQUAL_INT(0, mock_outbox_sends);
}

void test_reconnect_should_not_request_weather_without_a_weather_slot(void) {
  set_slots(kSlotsNoWeather);
  s_weather_stamp = 0;
  handle_bluetooth(false);
  handle_bluetooth(true);
  TEST_ASSERT_EQUAL_INT(0, mock_outbox_sends);
}

// -----------------------------------------------------------------------------
// Slots, health events, app message, wire widths, bluetooth
// -----------------------------------------------------------------------------

void test_inbox_should_empty_a_slot_assigned_a_source_without_a_spec(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_SLOT_1, 99);
  mock_dict_add_int(MESSAGE_KEY_SLOT_2, 19);  // retired id
  mock_dict_add_int(MESSAGE_KEY_SLOT_3, -4);
  mock_dict_add_int(MESSAGE_KEY_SLOT_4, DATA_SOURCE_BATTERY);
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_EMPTY, s_complication_slots[0].source);
  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_EMPTY, s_complication_slots[1].source);
  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_EMPTY, s_complication_slots[2].source);
  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_BATTERY, s_complication_slots[3].source);
  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_EMPTY, persist_read_int(PERSIST_KEY_SLOT_1));
  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_EMPTY, persist_read_int(PERSIST_KEY_SLOT_2));
}

void test_load_settings_should_empty_a_persisted_source_without_a_spec(void) {
  mock_persist_reset();
  persist_write_int(PERSIST_KEY_SLOT_1, 77);
  persist_write_int(PERSIST_KEY_SLOT_2, DATA_SOURCE_STEPS);
  load_settings();
  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_EMPTY, s_complication_slots[0].source);
  TEST_ASSERT_EQUAL_INT(DATA_SOURCE_STEPS, s_complication_slots[1].source);
}

void test_health_handler_should_do_nothing_when_no_visible_slot_shows_health(void) {
  main_window_load(NULL);
  set_slots(kSlotsNoHealth);
  request_ui_redraw();
  int marks = mock_mark_dirty_count;

  health_handler(HealthEventSignificantUpdate, NULL);
  health_handler(HealthEventHeartRateUpdate, NULL);
  health_handler(HealthEventMovementUpdate, NULL);
  health_handler(HealthEventSleepUpdate, NULL);

  TEST_ASSERT_EQUAL_INT(0, mock_health_accessible_count);
  TEST_ASSERT_EQUAL_INT(0, mock_health_sum_today_count);
  TEST_ASSERT_EQUAL_INT(marks, mock_mark_dirty_count);
}

void test_health_handler_should_still_work_when_a_health_slot_is_visible(void) {
  set_slots(kSlotsOnlySteps);
  health_handler(HealthEventSignificantUpdate, NULL);
  TEST_ASSERT_TRUE(mock_health_accessible_count > 0);
}

void test_full_weather_and_settings_dicts_should_fit_the_inbox(void) {
  // Dictionary header 1 byte; every tuple a 7-byte header plus its value. The
  // weather table (stamp row included) is all 4-byte ints; Clay's settings and
  // slots arrive as short strings, bounded by the same 4 bytes.
  size_t tuple = 7 + 4;
  size_t weather = 1 + (sizeof(s_weather_fields) / sizeof(s_weather_fields[0])) * tuple;
  size_t settings = 1 + (sizeof(s_settings_fields) / sizeof(s_settings_fields[0]) +
                         sizeof(s_slot_keys) / sizeof(s_slot_keys[0]) + 1) *
                            tuple;
  TEST_ASSERT_TRUE(weather <= APP_MESSAGE_INBOX_SIZE);
  TEST_ASSERT_TRUE(settings <= APP_MESSAGE_INBOX_SIZE);
  TEST_ASSERT_TRUE(1 + 7 + 1 <= APP_MESSAGE_OUTBOX_SIZE);
}

void test_init_should_open_app_message_with_the_named_sizes(void) {
  init();
  TEST_ASSERT_EQUAL_UINT32(APP_MESSAGE_INBOX_SIZE, mock_app_message_open_inbound);
  TEST_ASSERT_EQUAL_UINT32(APP_MESSAGE_OUTBOX_SIZE, mock_app_message_open_outbound);
  TEST_ASSERT_EQUAL_INT(0, mock_log_count);
}

void test_init_should_log_when_app_message_open_fails(void) {
  mock_app_message_open_result = APP_MSG_OUT_OF_MEMORY;
  init();
  TEST_ASSERT_EQUAL_INT(1, mock_log_count);
  char want[16];
  snprintf(want, sizeof(want), "(%d)", (int)APP_MSG_OUT_OF_MEMORY);
  TEST_ASSERT_NOT_NULL(strstr(mock_last_log, want));
}

void test_init_should_open_on_the_theme_the_sun_calls_for(void) {
  mock_persist_reset();
  persist_write_int(PERSIST_KEY_SETTINGS_THEME, 2);
  persist_write_int(PERSIST_KEY_SETTINGS_NIGHT_THEME, 3);
  persist_write_int(PERSIST_KEY_LATITUDE, 6017);
  persist_write_int(PERSIST_KEY_LONGITUDE, 2494);
  bool night = compute_solar_elevation(time(NULL), 6017, 2494) < SUNSET_ELEVATION;
  init();
  // The very first background paint already wears the right palette.
  const WatchTheme* want = night ? &s_theme_shadow : &s_theme_panel;
  TEST_ASSERT_EQUAL_UINT8(want->center_bg, mock_window_first_bg);
}

void test_inbox_dropped_should_log_the_reason(void) {
  inbox_dropped_callback(APP_MSG_BUFFER_OVERFLOW, NULL);
  TEST_ASSERT_EQUAL_INT(1, mock_log_count);
  char want[16];
  snprintf(want, sizeof(want), "(%d)", (int)APP_MSG_BUFFER_OVERFLOW);
  TEST_ASSERT_NOT_NULL(strstr(mock_last_log, want));
}

void test_tuple_get_int_should_read_a_four_byte_unsigned(void) {
  mock_dict_reset();
  mock_dict_add_uint_width(MESSAGE_KEY_SLOT_1, 70000, 4);
  TEST_ASSERT_EQUAL_INT(70000, tuple_get_int(dict_find(NULL, MESSAGE_KEY_SLOT_1)));
  mock_dict_reset();
  mock_dict_add_int_width(MESSAGE_KEY_SLOT_1, -70000, 4);
  TEST_ASSERT_EQUAL_INT(-70000, tuple_get_int(dict_find(NULL, MESSAGE_KEY_SLOT_1)));
}

void test_handle_bluetooth_should_record_the_link_and_redraw(void) {
  main_window_load(NULL);
  request_ui_redraw();
  int marks = mock_mark_dirty_count;

  handle_bluetooth(false);
  TEST_ASSERT_FALSE(s_connected);
  TEST_ASSERT_TRUE(mock_mark_dirty_count > marks);

  marks = mock_mark_dirty_count;
  handle_bluetooth(true);
  TEST_ASSERT_TRUE(s_connected);
  TEST_ASSERT_TRUE(mock_mark_dirty_count > marks);
}

// -----------------------------------------------------------------------------
// Chime
// -----------------------------------------------------------------------------

void test_chime_should_not_ask_the_speaker_off_the_full_hour_or_when_disabled(void) {
  struct tm t = {0};
  t.tm_min = 13;
  s_settings_chime_volume = 5;
  chime_on_tick(&t);
  t.tm_min = 0;
  s_settings_chime_volume = 0;
  chime_on_tick(&t);
  TEST_ASSERT_EQUAL_INT(0, mock_speaker_muted_query_count);

  s_settings_chime_volume = 5;
  chime_on_tick(&t);
  TEST_ASSERT_EQUAL_INT(1, mock_speaker_muted_query_count);
}

void test_chime_should_stay_silent_at_volume_zero(void) {
  s_settings_chime_volume = 0;
  struct tm t = {0};
  chime_on_tick(&t);
  TEST_ASSERT_EQUAL_INT(0, mock_speaker_tone_count);

  s_settings_chime_volume = -10;
  chime_play();
  TEST_ASSERT_EQUAL_INT(0, mock_speaker_tone_count);
}

void test_chime_should_not_repeat_in_a_replayed_wall_hour(void) {
  s_settings_chime_volume = 5;
  struct tm t = {0};
  t.tm_hour = 1;
  chime_on_tick(&t);
  chime_on_tick(&t);  // DST fall-back: 01:00 happens twice
  TEST_ASSERT_EQUAL_INT(1, mock_speaker_tone_count);

  t.tm_hour = 2;
  chime_on_tick(&t);
  TEST_ASSERT_EQUAL_INT(2, mock_speaker_tone_count);

  t.tm_hour = 2;
  t.tm_yday = 1;  // the same hour the next day is a new chime
  chime_on_tick(&t);
  TEST_ASSERT_EQUAL_INT(3, mock_speaker_tone_count);
}

void test_chime_muted_hour_should_still_chime_if_unmuted_within_the_hour(void) {
  s_settings_chime_volume = 5;
  struct tm t = {0};
  mock_speaker_muted = true;
  chime_on_tick(&t);
  TEST_ASSERT_EQUAL_INT(0, mock_speaker_tone_count);
  mock_speaker_muted = false;
  chime_on_tick(&t);
  TEST_ASSERT_EQUAL_INT(1, mock_speaker_tone_count);
}

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

void test_refresh_state_should_tolerate_a_missing_time_layer(void) {
  main_window_load(NULL);
  main_window_unload(NULL);
  int texts = mock_set_text_count;
  int colors = mock_set_text_color_count;
  refresh_state();
  TEST_ASSERT_EQUAL_INT(texts, mock_set_text_count);
  TEST_ASSERT_EQUAL_INT(colors, mock_set_text_color_count);
}

void test_window_unload_should_drop_the_layer_handles_and_the_unobstructed_subscription(void) {
  main_window_load(NULL);
  TEST_ASSERT_NOT_NULL(s_canvas_layer);
  TEST_ASSERT_NOT_NULL(s_time_layer);
  TEST_ASSERT_NOT_NULL(s_crt_layer);
  Layer* canvas_layer = s_canvas_layer;
  Layer* crt_layer = s_crt_layer;
  main_window_unload(NULL);
  TEST_ASSERT_EQUAL_INT(2, mock_layer_destroy_count);
  TEST_ASSERT_EQUAL_PTR(crt_layer, mock_layer_destroyed[0]);
  TEST_ASSERT_EQUAL_PTR(canvas_layer, mock_layer_destroyed[1]);
  TEST_ASSERT_NULL(s_canvas_layer);
  TEST_ASSERT_NULL(s_time_layer);
  TEST_ASSERT_NULL(s_crt_layer);
  TEST_ASSERT_EQUAL_INT(1, mock_unobstructed_unsubscribe_count);
}

void test_deinit_should_release_every_service_init_subscribed(void) {
  s_settings_crt_degauss = 1;
  s_complication_slots[1].source = DATA_SOURCE_BATTERY;
  init();
  TEST_ASSERT_EQUAL_INT(1, mock_battery_subscribe_count);
  TEST_ASSERT_EQUAL_INT(1, mock_backlight_subscribe_count);
  TEST_ASSERT_EQUAL_INT(1, mock_accel_tap_subscribe_count);
  deinit();
  TEST_ASSERT_EQUAL_INT(mock_tick_subscribe_count, mock_tick_unsubscribe_count);
  TEST_ASSERT_EQUAL_INT(mock_battery_subscribe_count, mock_battery_unsubscribe_count);
  TEST_ASSERT_EQUAL_INT(mock_connection_subscribe_count, mock_connection_unsubscribe_count);
  TEST_ASSERT_EQUAL_INT(mock_health_subscribe_count, mock_health_unsubscribe_count);
  TEST_ASSERT_EQUAL_INT(mock_backlight_subscribe_count, mock_backlight_unsubscribe_count);
  TEST_ASSERT_EQUAL_INT(mock_accel_tap_subscribe_count, mock_accel_tap_unsubscribe_count);
  TEST_ASSERT_EQUAL_INT(1, mock_app_focus_unsubscribe_count);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_render_gate_should_go_silent_when_nothing_changes);
  RUN_TEST(test_render_gate_should_ignore_changes_nobody_displays);
  RUN_TEST(test_render_gate_should_pass_displayed_changes_through);
  RUN_TEST(test_render_gate_should_notice_bar_slot_changes);
  RUN_TEST(test_render_gate_should_notice_hi_lo_caption_swaps);
  RUN_TEST(test_render_gate_should_reapply_colors_on_theme_change);
  RUN_TEST(test_quick_view_did_change_should_gate_and_restore);
  RUN_TEST(test_canvas_should_blit_the_scene_cache_on_repeat_renders);
  RUN_TEST(test_canvas_should_skip_the_bottom_row_while_quick_view_is_up);
  RUN_TEST(test_canvas_procs_should_never_word_wrap);
  RUN_TEST(test_weather_strip_should_fill_only_the_chip_that_earns_a_band);
  RUN_TEST(test_wind_without_a_bearing_should_draw_the_speed_alone);
  RUN_TEST(test_bt_qt_narrow_should_draw_one_run_not_two_boxes);
  RUN_TEST(test_progress_bar_should_show_dashes_without_a_reading);
  RUN_TEST(test_bt_qt_wide_should_draw_both_checkboxes);
  RUN_TEST(test_steps_bar_should_fill_with_the_plain_text_color);
  RUN_TEST(test_battery_bar_should_fill_with_the_status_color);
  RUN_TEST(test_aqi_chip_should_band_only_on_an_attention_reading);
  RUN_TEST(test_battery_complications_should_wear_green_while_charging);
  RUN_TEST(test_pcp_chip_should_band_on_attention_probability);
  RUN_TEST(test_pcp_chip_should_band_by_wmo_intensity);
  RUN_TEST(test_battery_callback_should_coalesce_unchanged_levels);
  RUN_TEST(test_to_upper_str_should_convert_lowercase_to_uppercase);
  RUN_TEST(test_tuple_get_int_should_parse_strings_and_ints);
  RUN_TEST(test_get_source_label_should_return_correct_labels);
  RUN_TEST(test_registry_rows_should_be_unique_and_resolve);
  RUN_TEST(test_registry_should_pin_the_weather_backed_set);
  RUN_TEST(test_registry_health_metrics_match_the_reads_table);
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
  RUN_TEST(test_wmo_cond_should_map_words_and_the_precipitation_facet);
  RUN_TEST(test_precip_amount_mode_should_gate_on_units_family_and_data);
  RUN_TEST(test_weather_cond_formatter_should_render_the_word_or_dashes);
  RUN_TEST(test_get_source_data_should_format_high_low);
  RUN_TEST(test_high_low_cells_should_roll_when_their_extreme_hour_ends);
  RUN_TEST(test_high_low_cells_should_stay_chronological_on_inversion_days);
  RUN_TEST(test_high_low_layout_should_fall_back_to_lo_first_when_hours_unknown);
  RUN_TEST(test_high_low_stub_order_should_follow_the_layout);
  RUN_TEST(test_compute_beats_should_map_the_bmt_day_to_0_999);
  RUN_TEST(test_iso_week_number_should_be_exact_iso_8601);
  RUN_TEST(test_get_source_data_should_format_the_iso_week);
  RUN_TEST(test_get_source_data_should_format_beats);
  RUN_TEST(test_compute_solar_hour_angle_should_match_the_reference_readings);
  RUN_TEST(test_compute_solar_hour_angle_should_wrap_into_minus_179_to_180);
  RUN_TEST(test_compute_solar_hour_angle_should_round_west_longitudes_to_the_nearest_degree);
  RUN_TEST(test_get_source_data_should_format_the_solar_hour_angle);
  RUN_TEST(test_refresh_state_should_compute_the_solar_hour_angle);
  RUN_TEST(test_the_solar_hour_angle_should_request_weather_to_learn_the_longitude);
  RUN_TEST(test_inbox_should_persist_the_longitude_and_load_it_back);
  RUN_TEST(test_inbox_should_drop_an_impossible_longitude);
  RUN_TEST(test_load_longitude_should_keep_the_sentinel_when_nothing_was_stored);
  RUN_TEST(test_the_longitude_should_outlive_stale_weather);
  RUN_TEST(test_compute_solar_elevation_should_match_the_reference_readings);
  RUN_TEST(test_compute_solar_elevation_should_stay_within_bounds_all_year);
  RUN_TEST(test_get_source_data_should_format_the_solar_elevation_sign_by_the_unrounded_value);
  RUN_TEST(test_get_source_data_should_format_the_solar_position);
  RUN_TEST(test_the_solar_elevation_sources_should_read_no_data_until_both_coordinates_arrived);
  RUN_TEST(test_utf8_cells_should_count_glyphs_not_bytes);
  RUN_TEST(test_refresh_state_should_compute_the_solar_elevation);
  RUN_TEST(test_the_solar_elevation_sources_should_request_weather_to_learn_the_position);
  RUN_TEST(test_inbox_should_persist_the_latitude_and_load_it_back);
  RUN_TEST(test_inbox_should_drop_an_impossible_latitude);
  RUN_TEST(test_load_latitude_should_keep_the_sentinel_when_nothing_was_stored);
  RUN_TEST(test_the_latitude_should_outlive_stale_weather);
  RUN_TEST(test_get_source_color_should_return_appropriate_colors);
  RUN_TEST(test_determine_theme_should_handle_all_configurations);
  RUN_TEST(test_pick_theme_setting_should_go_night_only_below_the_horizon);
  RUN_TEST(test_pick_theme_setting_should_stay_day_without_a_location_or_a_night_theme);
  RUN_TEST(test_apply_theme_should_follow_the_elevation_and_the_known_location);
  RUN_TEST(test_refresh_state_should_pick_the_theme_from_the_elevation_it_just_computed);
  RUN_TEST(test_refresh_state_should_not_compute_the_elevation_without_a_location);
  RUN_TEST(test_themes_should_keep_text_readable_on_their_ground);
  RUN_TEST(test_status_ink_should_clear_every_fill_it_is_drawn_on);
  RUN_TEST(test_every_theme_should_only_use_dos_palette_colors);
  RUN_TEST(test_navigator_should_take_dos_navigators_header_and_info_colors);
  RUN_TEST(test_amber_should_keep_its_tiers_on_its_glowing_ground);
  RUN_TEST(test_theme_ground_level_should_give_level_1_tinted_field);
  RUN_TEST(test_format_date_string_should_render_every_body);
  RUN_TEST(test_every_date_combination_should_fit_its_window);
  RUN_TEST(test_date_strip_dow_should_drop_the_weekday_and_its_space);
  RUN_TEST(test_short_date_should_drop_the_weekday_only_in_a_narrow_slot);
  RUN_TEST(test_weekday_position_should_be_independent_of_the_body);
  RUN_TEST(test_short_date_should_stay_short_whatever_the_date_format);
  RUN_TEST(test_weather_field_table_should_pin_each_global_and_sentinel);
  RUN_TEST(test_weather_cache_should_round_trip_when_fresh);
  RUN_TEST(test_weather_cache_should_leave_extreme_timing_at_sentinel_in_old_caches);
  RUN_TEST(test_weather_cache_should_reject_missing_or_stale_data);
  RUN_TEST(test_weather_cache_should_keep_values_at_edge_of_window);
  RUN_TEST(test_weather_cache_without_cond_code_should_degrade_to_dashes);
  RUN_TEST(test_settings_should_round_trip_through_persistence);
  RUN_TEST(test_night_theme_should_round_trip_through_persistence_and_default_off);
  RUN_TEST(test_settings_persistence_is_decoupled_from_message_key_ids);
  RUN_TEST(test_health_read_table_should_pin_each_metric_mode_and_sentinel);
  RUN_TEST(test_update_health_info_should_read_heart_rate);
  RUN_TEST(test_update_health_info_should_do_nothing_with_no_health_slots);
  RUN_TEST(test_update_health_info_should_read_only_displayed_metrics);
  RUN_TEST(test_update_health_info_should_land_each_metric_on_its_own_target);
  RUN_TEST(test_health_handler_should_throttle_movement_updates);
  RUN_TEST(test_health_handler_should_throttle_each_event_type_separately);
  RUN_TEST(test_health_handler_should_refresh_again_after_the_throttle_window);
  RUN_TEST(test_health_handler_should_not_throttle_heart_rate_updates);
  RUN_TEST(test_health_handler_should_not_throttle_significant_updates);
  RUN_TEST(test_health_events_should_read_only_their_own_metrics);
  RUN_TEST(test_health_events_the_face_does_not_use_should_read_nothing);
  RUN_TEST(test_minute_tick_should_read_every_health_metric);
  RUN_TEST(test_undisplayed_health_metrics_should_read_as_no_data);
  RUN_TEST(test_inbox_should_parse_weather_payload_and_persist);
  RUN_TEST(test_inbox_should_sentinel_out_of_range_semantic_ints_and_cap_display_widths);
  RUN_TEST(test_inbox_should_keep_weather_ints_at_the_edges_of_their_ranges);
  RUN_TEST(test_inbox_should_not_clamp_uv_above_11);
  RUN_TEST(test_inbox_should_parse_narrow_width_weather_ints);
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
  RUN_TEST(test_inbox_should_parse_and_persist_the_crt_effect_settings);
  RUN_TEST(test_load_settings_should_migrate_the_old_crt_switch_to_all_three_effects);
  RUN_TEST(test_load_settings_should_migrate_an_off_crt_switch_as_all_off);
  RUN_TEST(test_load_settings_migration_should_not_override_the_new_effect_keys);
  RUN_TEST(test_load_settings_should_migrate_the_old_degauss_sound_to_a_volume);
  RUN_TEST(test_load_settings_should_migrate_the_old_chime_switch_into_the_volume);
  RUN_TEST(test_inbox_should_parse_and_persist_chime_volume_setting);
  RUN_TEST(test_inbox_units_change_should_trigger_weather_refetch);
  RUN_TEST(test_inbox_weather_window_change_should_trigger_weather_refetch);
  RUN_TEST(test_refresh_state_should_never_request_weather);
  RUN_TEST(test_request_weather_should_send_exactly_the_trigger_key);
  RUN_TEST(test_refresh_state_should_refresh_the_hi_lo_phase_hour);
  RUN_TEST(test_refresh_state_should_reformat_the_date_when_settings_change);
  RUN_TEST(test_refresh_state_should_keep_date_output_when_nothing_changes);
  RUN_TEST(test_chime_gate_should_be_due_only_on_the_hour_when_enabled);
  RUN_TEST(test_chime_should_beep_the_post_tone_once_at_the_full_hour);
  RUN_TEST(test_chime_should_stay_silent_when_the_setting_is_off);
  RUN_TEST(test_chime_should_stay_silent_off_the_full_hour);
  RUN_TEST(test_chime_should_stay_silent_when_the_speaker_is_muted);
  RUN_TEST(test_chime_should_use_the_configured_volume);
  RUN_TEST(test_chime_should_clamp_the_configured_volume_to_the_sdk_range);
  RUN_TEST(test_chime_effective_volume_matrix);
  RUN_TEST(test_inbox_chime_test_should_beep_once_at_the_volume_in_the_same_dict);
  RUN_TEST(test_inbox_should_never_beep_on_a_plain_save);
  RUN_TEST(test_inbox_chime_test_should_bypass_mute);
  RUN_TEST(test_tick_handler_should_refresh_quiet_time_state);
  RUN_TEST(test_tick_handler_should_request_weather_on_the_half_hour_edge);
  RUN_TEST(test_tick_handler_should_skip_weather_with_no_weather_slots);
  RUN_TEST(test_wind_slot_should_join_the_weather_fetch_gate);
  RUN_TEST(test_inbox_should_fetch_when_a_weather_slot_first_appears);
  RUN_TEST(test_inbox_should_fetch_when_a_slot_changes_with_weather_already_shown);
  RUN_TEST(test_dropped_weather_request_should_retry_a_bounded_number_of_times);
  RUN_TEST(test_fresh_request_should_cancel_an_armed_retry_timer);
  RUN_TEST(test_landed_payload_should_cancel_an_armed_retry_timer);
  RUN_TEST(test_retry_reset_should_not_cancel_when_no_timer_is_armed);
  RUN_TEST(test_second_failure_should_supersede_the_armed_retry);
  RUN_TEST(test_answered_weather_request_should_rearm_the_retry_budget);
  RUN_TEST(test_weather_cache_should_skip_rewrite_when_payload_is_unchanged);
  RUN_TEST(test_settings_message_should_not_rewrite_unchanged_keys);
  RUN_TEST(test_init_should_fetch_weather_when_the_cache_is_stale);
  RUN_TEST(test_init_should_not_fetch_when_the_cache_is_fresh);
  RUN_TEST(test_init_should_not_fetch_without_a_weather_slot);
  RUN_TEST(test_request_weather_should_drop_quietly_when_the_outbox_is_unavailable);
  RUN_TEST(test_init_should_subscribe_only_what_the_default_layout_uses);
  RUN_TEST(test_init_should_subscribe_the_crt_services_when_degauss_is_on);
  RUN_TEST(test_init_should_not_subscribe_the_crt_services_for_effects_without_a_strike);
  RUN_TEST(test_init_should_subscribe_the_battery_when_a_slot_shows_it);
  RUN_TEST(test_turning_the_crt_on_mid_run_should_subscribe_and_let_the_next_wake_strike);
  RUN_TEST(test_turning_the_crt_off_should_unsubscribe_and_drop_a_pending_strike);
  RUN_TEST(test_removing_every_health_slot_should_unsubscribe_health);
  RUN_TEST(test_adding_a_battery_slot_should_subscribe_and_show_the_current_level);
  RUN_TEST(test_wanted_subscriptions_should_follow_slots_and_settings);
  RUN_TEST(test_weather_requests_should_not_send_while_the_phone_is_disconnected);
  RUN_TEST(test_init_should_not_fetch_weather_while_the_phone_is_disconnected);
  RUN_TEST(test_weather_fetch_on_reconnect_should_follow_the_stamp_age);
  RUN_TEST(test_weather_fetch_on_reconnect_should_hold_the_minimum_gap);
  RUN_TEST(test_reconnect_should_not_request_again_within_the_minimum_gap);
  RUN_TEST(test_reconnect_should_request_weather_once_when_the_data_is_stale);
  RUN_TEST(test_reconnect_should_not_request_weather_when_the_data_is_fresh);
  RUN_TEST(test_reconnect_should_not_request_weather_without_a_weather_slot);
  RUN_TEST(test_init_should_register_appmessage_handlers);
  RUN_TEST(test_window_load_should_subscribe_unobstructed_area);
  RUN_TEST(test_tuple_get_int_should_parse_every_wire_width);
  RUN_TEST(test_update_health_info_should_fall_back_to_sentinels_without_permission);
  RUN_TEST(test_clock_should_follow_the_12h_24h_settings);
  RUN_TEST(test_empty_and_unknown_sources_should_draw_nothing);
  RUN_TEST(test_unknown_source_should_render_only_the_placeholder_frame);
  RUN_TEST(test_inbox_should_land_every_field_of_a_full_weather_payload);
  RUN_TEST(test_crt_should_not_capture_the_framebuffer_while_disabled);
  RUN_TEST(test_crt_text_boxes_should_not_move_with_quick_view);
  RUN_TEST(test_crt_sound_should_play_only_when_enabled_and_unmuted);
  RUN_TEST(test_crt_strike_should_not_prime_before_the_first_did_focus);
  RUN_TEST(test_crt_strike_should_prime_after_the_first_did_focus);
  RUN_TEST(test_crt_strike_should_ignore_backlight_while_the_face_is_covered);
  RUN_TEST(test_crt_strike_should_eat_the_dismissals_backlight);
  RUN_TEST(test_crt_strike_should_fire_on_the_backlight_after_the_dismissal);
  RUN_TEST(test_crt_strike_should_keep_the_one_shot_through_a_covered_backlight);
  RUN_TEST(test_crt_strike_should_drop_the_one_shot_once_the_backlight_goes_off_in_view);
  RUN_TEST(test_crt_strike_should_ignore_backlight_while_quick_view_is_up);
  RUN_TEST(test_crt_strike_should_not_spend_the_one_shot_on_a_quick_view_backlight);
  RUN_TEST(test_crt_strike_should_expire_the_one_shot_after_the_grace_window);
  RUN_TEST(test_crt_strike_should_cancel_a_pending_grace_when_a_new_cover_arrives);
  RUN_TEST(test_crt_strike_synth_should_sound_and_end_in_exact_silence);
  RUN_TEST(test_crt_strike_synth_should_keep_its_waveform_whatever_the_frame_rate);
  RUN_TEST(test_crt_strike_pcm_should_last_as_long_as_the_animation_within_the_speaker_budget);
  RUN_TEST(test_crt_deinit_should_cancel_the_strike_and_drop_the_layer);
  RUN_TEST(test_crt_cover_should_not_eat_a_strike_while_the_effect_is_off);
  RUN_TEST(test_crt_shake_should_prime_the_backlight_wake_at_once);
  RUN_TEST(test_crt_button_wake_should_strike_only_after_the_delay);
  RUN_TEST(test_crt_button_wake_should_die_with_the_face);
  RUN_TEST(test_crt_button_wake_should_not_strike_once_a_cover_arrives);
  RUN_TEST(test_crt_button_wake_should_not_strike_once_quick_view_is_up);
  RUN_TEST(test_crt_button_wake_should_not_strike_once_degauss_is_off);
  RUN_TEST(test_crt_button_wake_should_end_with_the_backlight);
  RUN_TEST(test_crt_button_wake_should_restart_its_delay_on_a_new_wake);
  RUN_TEST(test_crt_shake_should_expire_so_a_later_button_wake_waits);
  RUN_TEST(test_crt_backlight_off_should_clear_the_shake);
  RUN_TEST(test_crt_shake_should_rearm_its_expiry);
  RUN_TEST(test_crt_deinit_should_clear_the_shake);
  RUN_TEST(test_crt_setting_change_should_disarm_a_pending_cover_suppression);
  RUN_TEST(test_crt_pass_should_follow_the_row_stride);
  RUN_TEST(test_crt_flash_should_need_backlight_on_and_the_toggle);
  RUN_TEST(test_crt_setting_push_should_redraw_the_overlay);
  RUN_TEST(test_crt_sound_should_play_at_the_configured_volume_and_stay_silent_at_zero);
  RUN_TEST(test_crt_degauss_off_should_end_a_strike_but_keep_the_other_effects);
  RUN_TEST(test_crt_overlay_should_capture_only_while_something_paints);
  RUN_TEST(test_crt_backlight_should_strike_only_with_the_degauss_effect);
  RUN_TEST(test_crt_toggle_off_should_force_a_full_repaint);
  RUN_TEST(test_crt_toggle_off_mid_strike_should_cancel_the_chain);
  RUN_TEST(test_crt_strike_should_draw_phase_zero_after_the_wake_frame);
  RUN_TEST(test_crt_strike_field_should_stay_within_the_reach_in_every_frame);
  RUN_TEST(test_crt_strike_field_should_pull_the_most_at_the_rim_and_leave_the_centre);
  RUN_TEST(test_crt_strike_field_should_flip_between_pulled_in_and_pushed_out_every_frame);
  RUN_TEST(test_crt_strike_decay_should_keep_its_curve_in_time);
  RUN_TEST(test_crt_strike_lobes_should_move_at_the_same_speed_in_time);
  RUN_TEST(test_crt_strike_should_last_720_ms_as_long_as_its_sound);
  RUN_TEST(test_crt_strike_field_should_pull_unevenly_along_an_edge);
  RUN_TEST(test_crt_strike_field_should_move_the_lobes_between_frames);
  RUN_TEST(test_crt_strike_should_gather_pixels_where_the_field_says);
  RUN_TEST(test_crt_strike_should_leave_the_centre_pixel_where_it_is);
  RUN_TEST(test_crt_strike_should_read_rows_above_as_they_were_before_the_warp);
  RUN_TEST(test_crt_strike_should_be_black_beyond_the_raster);
  RUN_TEST(test_crt_strike_should_stripe_colour_with_hue_and_tint_grey);
  RUN_TEST(test_crt_strike_should_thin_the_stripes_as_the_amplitude_dies);
  RUN_TEST(test_crt_strike_should_leave_the_frame_alone_at_zero_amplitude);
  RUN_TEST(test_crt_strike_start_should_do_nothing_once_the_chain_is_killed);
  RUN_TEST(
      test_crt_strike_should_capture_the_raw_frame_on_phase_zero_and_gather_later_frames_from_it);
  RUN_TEST(test_crt_strike_should_write_every_pixel_from_the_capture);
  RUN_TEST(test_crt_strike_band_table_should_match_the_tint_and_hue_rules);
  RUN_TEST(test_crt_frame_strokes_should_match_the_drawn_frames);
  RUN_TEST(test_crt_rigid_zones_should_cover_the_warped_stroke_and_a_margin);
  RUN_TEST(test_crt_rigid_zones_should_be_ordered_and_bounded);
  RUN_TEST(test_crt_curve_should_only_differ_from_the_plain_warp_inside_the_zones);
  RUN_TEST(test_crt_curve_should_keep_a_level_one_stroke_at_least_two_pixels_wide);
  RUN_TEST(test_crt_curve_should_keep_a_light_theme_stroke_intensity_constant);
  RUN_TEST(test_blend_levels_should_turn_one_level_channels_on_one_at_a_time);
  RUN_TEST(test_blend_levels_should_mirror_a_one_level_blend_with_the_taps_swapped);
  RUN_TEST(test_blend_levels_should_step_light_greys_the_same_way);
  RUN_TEST(test_blend_levels_should_keep_the_rounded_mix_for_wider_blends);
  RUN_TEST(test_crt_strike_gather_should_match_the_per_pixel_loop_in_every_frame);
  RUN_TEST(test_crt_strike_should_skip_the_face_layers_from_the_frame_after_the_capture);
  RUN_TEST(test_crt_strike_should_leave_the_window_background_to_itself_across_a_refresh);
  RUN_TEST(test_crt_strike_should_restore_the_face_layers_when_degauss_goes_off);
  RUN_TEST(test_crt_strike_should_restore_the_face_layers_when_re_primed);
  RUN_TEST(test_crt_strike_should_restore_the_face_layers_on_deinit);
  RUN_TEST(test_crt_strike_should_not_skip_the_face_layers_without_a_capture);
  RUN_TEST(test_crt_strike_should_bake_the_curvature_into_its_capture_once);
  RUN_TEST(test_crt_strike_should_not_bake_without_curvature);
  RUN_TEST(test_crt_strike_should_bake_again_after_a_reprime);
  RUN_TEST(test_crt_curvature_pass_should_warp_each_row_then_lay_the_pane);
  RUN_TEST(
      test_crt_pane_should_darken_each_pixel_by_the_falloff_at_the_content_position_the_warp_shows);
  RUN_TEST(test_crt_pane_should_bend_with_the_warp_not_follow_the_glass_edge);
  RUN_TEST(test_crt_pane_should_not_depend_on_the_content_it_darkens);
  RUN_TEST(test_crt_pane_falloff_should_rise_toward_the_interior_and_not_step_between_rows);
  RUN_TEST(test_crt_pane_should_leave_the_interior_alone);
  RUN_TEST(test_crt_pane_should_follow_the_row_stride);
  RUN_TEST(test_crt_strike_last_frame_should_meet_the_next_normal_frame_at_the_rim);
  RUN_TEST(test_crt_pane_should_not_darken_without_curvature);
  RUN_TEST(test_crt_strike_should_separate_the_channels_further_in_the_early_frames);
  RUN_TEST(test_crt_strike_should_mirror_the_fringing_across_the_centreline);
  RUN_TEST(test_crt_strike_should_leave_the_dead_zone_alone_at_zero_amplitude);
  RUN_TEST(test_crt_strike_should_vignette_the_glass_after_the_warp);
  RUN_TEST(test_crt_strike_should_not_vignette_without_curvature);
  RUN_TEST(test_crt_strike_should_keep_the_canvas_out_of_the_frame_while_it_runs);
  RUN_TEST(test_crt_strike_should_hand_the_canvas_cache_back_when_it_ends);
  RUN_TEST(test_crt_strike_should_hand_the_canvas_cache_back_when_cancelled);
  RUN_TEST(test_crt_strike_should_recapture_after_a_reprime);
  RUN_TEST(test_crt_strike_without_a_scene_cache_should_run_the_passes_in_place);
  RUN_TEST(test_canvas_cache_borrow_should_need_a_cache_and_force_a_repaint_on_return);
  RUN_TEST(test_weather_age_should_judge_by_age_and_local_day);
  RUN_TEST(test_weather_stamp_for_should_trust_only_sane_fetch_times);
  RUN_TEST(test_weather_expire_should_keep_everything_while_fresh);
  RUN_TEST(test_weather_expire_should_blank_every_reading_when_stale);
  RUN_TEST(test_weather_expire_should_drop_only_the_day_bound_readings_after_midnight);
  RUN_TEST(test_tick_handler_should_show_dashes_for_weather_older_than_two_hours);
  RUN_TEST(test_tick_handler_should_not_present_yesterdays_hi_lo_as_todays);
  RUN_TEST(test_weather_cache_should_drop_extremes_cached_on_an_earlier_day);
  RUN_TEST(test_weather_cache_should_restore_the_stamp_and_reject_old_data_by_it);
  RUN_TEST(test_inbox_should_stamp_the_cache_with_the_phones_fetch_time);
  RUN_TEST(test_inbox_should_stamp_with_arrival_time_when_the_fetch_time_is_missing_or_impossible);
  RUN_TEST(test_inbox_should_treat_a_replayed_payload_older_than_two_hours_as_no_data);
  RUN_TEST(test_inbox_settings_only_message_should_not_stamp_weather);
  RUN_TEST(test_inbox_units_change_should_discard_held_weather_and_the_cache_stamp);
  RUN_TEST(test_inbox_window_change_should_discard_held_weather);
  RUN_TEST(test_inbox_unchanged_units_should_keep_held_weather);
  RUN_TEST(test_weather_table_sentinels_should_equal_the_globals_boot_values);
  RUN_TEST(test_weather_fetch_due_should_need_the_half_hour_edge_and_a_weather_slot);
  RUN_TEST(test_weather_fetch_after_settings_should_follow_what_changed_and_what_is_shown);
  RUN_TEST(test_a_failed_send_should_not_spend_the_budget_until_its_retry_fires);
  RUN_TEST(test_a_tick_request_should_start_the_retry_budget_over);
  RUN_TEST(test_a_settings_request_should_start_the_retry_budget_over);
  RUN_TEST(test_a_launch_request_should_start_the_retry_budget_over);
  RUN_TEST(test_inbox_should_empty_a_slot_assigned_a_source_without_a_spec);
  RUN_TEST(test_load_settings_should_empty_a_persisted_source_without_a_spec);
  RUN_TEST(test_health_handler_should_do_nothing_when_no_visible_slot_shows_health);
  RUN_TEST(test_health_handler_should_still_work_when_a_health_slot_is_visible);
  RUN_TEST(test_full_weather_and_settings_dicts_should_fit_the_inbox);
  RUN_TEST(test_init_should_open_app_message_with_the_named_sizes);
  RUN_TEST(test_init_should_log_when_app_message_open_fails);
  RUN_TEST(test_init_should_open_on_the_theme_the_sun_calls_for);
  RUN_TEST(test_inbox_dropped_should_log_the_reason);
  RUN_TEST(test_tuple_get_int_should_read_a_four_byte_unsigned);
  RUN_TEST(test_handle_bluetooth_should_record_the_link_and_redraw);
  RUN_TEST(test_chime_should_not_ask_the_speaker_off_the_full_hour_or_when_disabled);
  RUN_TEST(test_chime_should_stay_silent_at_volume_zero);
  RUN_TEST(test_chime_should_not_repeat_in_a_replayed_wall_hour);
  RUN_TEST(test_chime_muted_hour_should_still_chime_if_unmuted_within_the_hour);
  RUN_TEST(test_refresh_state_should_tolerate_a_missing_time_layer);
  RUN_TEST(test_window_unload_should_drop_the_layer_handles_and_the_unobstructed_subscription);
  RUN_TEST(test_deinit_should_release_every_service_init_subscribed);
  return UNITY_END();
}
