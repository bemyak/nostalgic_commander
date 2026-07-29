#include <pebble.h>
#include "drawing.h"
#include "data.h"
#include "theme.h"

Window* s_main_window = NULL;
Layer* s_canvas_layer = NULL;
TextLayer* s_time_layer = NULL;

void draw_ascii_window(GContext* ctx, GRect rect, const char* title) {
  int x = rect.origin.x;
  int y = rect.origin.y;
  int w = rect.size.w;
  int h = rect.size.h;

  graphics_context_set_fill_color(ctx, s_active_theme->frame);

  // Borders as filled rects — graphics_draw_line is anti-aliased with round
  // caps, which softens the corners; fill rects stay pixel-sharp. Every border
  // is drawn inside the rect, so the frame stays within w x h.
  // The top line drops so the title centres on it; the verticals start there
  // too, or they would stick up past the corners.
  int top = y + TITLE_BORDER_DROP;
  int side_h = h - TITLE_BORDER_DROP;

  // Vertical borders
  graphics_fill_rect(ctx, GRect(x, top, WINDOW_BORDER_PX, side_h), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(x + w - WINDOW_BORDER_PX, top, WINDOW_BORDER_PX, side_h), 0,
                     GCornerNone);

  // Bottom border
  graphics_fill_rect(ctx, GRect(x, y + h - WINDOW_BORDER_PX, w, WINDOW_BORDER_PX), 0, GCornerNone);

  // Top border (solid, with title gap)
  int title_width = strlen(title) * VGA16_CHAR_W + 4;  // one cell per char + padding
  if (title_width > w - 10) title_width = w - 10;

  int x_mid = x + w / 2;
  int gap_left = x_mid - title_width / 2 - 3;
  int gap_right = x_mid + title_width / 2 + 3;

  graphics_fill_rect(ctx, GRect(x, top, gap_left - x + 1, WINDOW_BORDER_PX), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(gap_right, top, x + w - gap_right, WINDOW_BORDER_PX), 0,
                     GCornerNone);

  // Title cell straddles the top border
  graphics_context_set_text_color(ctx, s_active_theme->text_secondary);
  graphics_draw_text(ctx, title, vga_font_16(),
                     GRect(gap_left, y - VGA16_CELL_H / 2, gap_right - gap_left, VGA16_CELL_H),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

// The centred rect a monospace value occupies inside its window: a whole number
// of glyph cells wide, sitting on the shared value row.
static GRect vga16_value_rect(GRect box_rect, const char* value) {
  int w = strlen(value) * VGA16_CHAR_W;
  return GRect(box_rect.origin.x + (box_rect.size.w - w) / 2, box_rect.origin.y + VALUE_ROW_DY, w,
               VALUE_ROW_H);
}

// Draws `len` characters of `text` starting `cell` glyph cells into `row`.
static void draw_run(GContext* ctx, GRect row, int cell, const char* text, int len, GColor color) {
  if (len <= 0) return;

  char buf[32];
  if (len > (int)sizeof(buf) - 1) len = sizeof(buf) - 1;
  memcpy(buf, text, len);
  buf[len] = '\0';

  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(
      ctx, buf, vga_font_16(),
      GRect(row.origin.x + cell * VGA16_CHAR_W, row.origin.y, len * VGA16_CHAR_W, row.size.h),
      GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

// Draws a value with one run picked out in `accent` and the rest in the primary
// text color. Monospace, so splitting it is pure cell arithmetic and each run
// lands back on its own glyph column. `at < 0` accents nothing.
static void draw_accented_value(GContext* ctx, GRect row, const char* text, int at, int len,
                                GColor accent) {
  int total = strlen(text);
  if (at < 0 || at >= total || len <= 0) {
    draw_run(ctx, row, 0, text, total, s_active_theme->text_primary);
    return;
  }
  if (at + len > total) len = total - at;

  draw_run(ctx, row, 0, text, at, s_active_theme->text_primary);
  draw_run(ctx, row, at, text + at, len, accent);
  draw_run(ctx, row, at + len, text + at + len, total - at - len, s_active_theme->text_primary);
}

// Temperature readouts end in the unit letter; pick it out like the beats "@".
static void draw_unit_value(GContext* ctx, GRect box_rect, ComplicationDataSource source) {
  char buf[40];
  get_source_data(source, buf, sizeof(buf), NULL);

  int len = strlen(buf);
  int at = (len > 0 && (buf[len - 1] == 'C' || buf[len - 1] == 'F')) ? len - 1 : -1;
  draw_accented_value(ctx, vga16_value_rect(box_rect, buf), buf, at, 1, s_active_theme->mark);
}

static void draw_weather_complication(GContext* ctx, GRect box_rect) {
  draw_unit_value(ctx, box_rect, DATA_SOURCE_WEATHER);
}

static void draw_weather_temp_complication(GContext* ctx, GRect box_rect) {
  draw_unit_value(ctx, box_rect, DATA_SOURCE_WEATHER_TEMP);
}

// The DATE window's value, with the weekday picked out.
static void draw_date_value(GContext* ctx, GRect box_rect) {
  if (!s_date_display[0]) return;

  int at = date_dow_offset(s_settings_date_format, s_date_display);
  draw_accented_value(ctx, vga16_value_rect(box_rect, s_date_display), s_date_display, at, DOW_LEN,
                      s_active_theme->mark);
}

// A status band spans the whole cell, the way a DOS list highlights a row,
// inset so the fill never touches the frame. Because it is not tied to glyph
// cells, the font's spacing column stops showing as slack on one side.
static GRect status_band_rect(GRect box_rect) {
  int inset = WINDOW_BORDER_PX + STATUS_BAND_PAD;
  return GRect(box_rect.origin.x + inset,
               box_rect.origin.y + VALUE_ROW_DY + (VALUE_ROW_H - VGA16_CELL_H) / 2,
               box_rect.size.w - 2 * inset, VGA16_CELL_H);
}

// One field of the band: `w` pixels from `x`, filled when there is a reading,
// with the value centred inside it so the fill never sits lopsided around the
// text. The `--` sentinel draws plain rather than as a band of whatever "no
// data" happens to color as.
static void draw_status_field(GContext* ctx, GRect box_rect, int x, int w, const char* text,
                              bool banded, GColor band) {
  int len = strlen(text);
  GRect row = GRect(x + (w - len * VGA16_CHAR_W) / 2, box_rect.origin.y + VALUE_ROW_DY,
                    len * VGA16_CHAR_W, VALUE_ROW_H);

  if (banded) {
    GRect fill = status_band_rect(box_rect);
    fill.origin.x = x;
    fill.size.w = w;
    graphics_context_set_fill_color(ctx, band);
    graphics_fill_rect(ctx, fill, 0, GCornerNone);
  }
  draw_run(ctx, row, 0, text, len,
           banded ? s_active_theme->status_ink : s_active_theme->text_primary);
}

// A lone reading fills the whole band. Centring in the band is the same as
// centring in the box, since the band is itself centred.
static void draw_banded_value(GContext* ctx, GRect box_rect, const char* text, bool banded,
                              GColor band) {
  GRect b = status_band_rect(box_rect);
  draw_status_field(ctx, box_rect, b.origin.x, b.size.w, text, banded, band);
}

// The threshold logic stays in get_source_color(); only the sentinel check
// lives here.
static void draw_aqi_complication(GContext* ctx, GRect box_rect) {
  char buf[8];
  get_source_data(DATA_SOURCE_AQI, buf, sizeof(buf), NULL);
  draw_banded_value(ctx, box_rect, buf, s_weather_aqi != -1, get_source_color(DATA_SOURCE_AQI));
}

static void draw_uv_complication(GContext* ctx, GRect box_rect) {
  char buf[8];
  get_source_data(DATA_SOURCE_UV, buf, sizeof(buf), NULL);
  draw_banded_value(ctx, box_rect, buf, s_weather_uv != -1, get_source_color(DATA_SOURCE_UV));
}

// Both readings side by side, each banding its own half of the cell so a good
// AQI next to a high UV reads as two fields rather than one blended color. The
// separator sits on the ground between them.
static void draw_aqi_uv_complication(GContext* ctx, GRect box_rect) {
  char aqi_str[8];
  char uv_str[8];
  get_source_data(DATA_SOURCE_AQI, aqi_str, sizeof(aqi_str), NULL);
  get_source_data(DATA_SOURCE_UV, uv_str, sizeof(uv_str), NULL);

  GRect band = status_band_rect(box_rect);

  // Two equal fields with a cell of ground between them, each value centred in
  // its own field. Splitting by text width instead would leave each field's
  // slack on its outer edge, which reads as two lopsided blocks.
  int half = (band.size.w - VGA16_CHAR_W) / 2;
  int right_x = band.origin.x + band.size.w - half;

  draw_status_field(ctx, box_rect, band.origin.x, half, aqi_str, s_weather_aqi != -1,
                    get_source_color(DATA_SOURCE_AQI));
  draw_status_field(ctx, box_rect, right_x, half, uv_str, s_weather_uv != -1,
                    get_source_color(DATA_SOURCE_UV));

  // The separator sits on the ground in the gap, keeping the two fields legible
  // as separate readings even when both carry the same color.
  int gap_x = band.origin.x + half;
  draw_status_field(ctx, box_rect, gap_x, band.size.w - 2 * half, "/", false, GColorClear);
}

static void draw_beats_complication(GContext* ctx, GRect box_rect) {
  char buf[8];
  get_source_data(DATA_SOURCE_BEATS, buf, sizeof(buf), NULL);

  // "@" prefix in DOS yellow, beat count in primary text
  draw_accented_value(ctx, vga16_value_rect(box_rect, buf), buf, 0, 1, s_active_theme->mark);
}

static void draw_battery_complication(GContext* ctx, GRect box_rect) {
  char buf[8];
  get_source_data(DATA_SOURCE_BATTERY, buf, sizeof(buf), NULL);

  // Charge has its own thresholds, tighter than get_source_color's: at or above
  // half the reading just shows the ground, and only a low battery earns a
  // band.
  GColor band = s_battery_level < 25 ? s_active_theme->status_red : s_active_theme->status_yellow;
  draw_banded_value(ctx, box_rect, buf, s_battery_level < 50, band);
}

typedef void (*ComplicationDrawFn)(GContext*, GRect);

// Sources that paint their value straight onto the canvas instead of using the
// slot's TextLayer: AQI/UV so its halves can be coloured independently, BEATS
// and BATTERY so they land on whole glyph cells. Both canvas_update_proc and
// refresh_complications go through here, so the set is stated exactly once —
// returning NULL means "this source uses the generic text layer".
static ComplicationDrawFn canvas_drawer(ComplicationDataSource source) {
  switch (source) {
    case DATA_SOURCE_AQI_UV:
      return draw_aqi_uv_complication;
    case DATA_SOURCE_BEATS:
      return draw_beats_complication;
    case DATA_SOURCE_BATTERY:
      return draw_battery_complication;
    case DATA_SOURCE_WEATHER:
      return draw_weather_complication;
    case DATA_SOURCE_WEATHER_TEMP:
      return draw_weather_temp_complication;
    case DATA_SOURCE_AQI:
      return draw_aqi_complication;
    case DATA_SOURCE_UV:
      return draw_uv_complication;
    default:
      return NULL;
  }
}

void canvas_update_proc(Layer* layer, GContext* ctx) {
  GRect bounds = layer_get_bounds(layer);

  // Clean background
  graphics_context_set_fill_color(ctx, s_active_theme->center_bg);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // Draw TIME and DATE windows
  GRect date_box = GRect(LAYOUT_X, 142, LAYOUT_W, 36);
  draw_ascii_window(ctx, GRect(LAYOUT_X, 50, LAYOUT_W, 86), "TIME");
  draw_ascii_window(ctx, date_box, "DATE");
  draw_date_value(ctx, date_box);

  // Draw parameterized ASCII windows
  for (int i = 0; i < NUM_SLOTS; i++) {
    ComplicationSlot* slot = &s_complication_slots[i];
    if (slot->source != DATA_SOURCE_EMPTY) {
      draw_ascii_window(ctx, slot->box_rect, get_source_label(slot->source));
      ComplicationDrawFn draw = canvas_drawer(slot->source);
      if (draw) draw(ctx, slot->box_rect);
    }
  }
}

void refresh_complications() {
  static char s_slot_buffers[NUM_SLOTS][40];

  for (int i = 0; i < NUM_SLOTS; i++) {
    ComplicationSlot* slot = &s_complication_slots[i];
    if (slot->source != DATA_SOURCE_EMPTY && slot->layer) {
      if (canvas_drawer(slot->source)) {
        layer_set_hidden(text_layer_get_layer(slot->layer), true);
      } else {
        layer_set_hidden(text_layer_get_layer(slot->layer), false);
        get_source_data(slot->source, s_slot_buffers[i], sizeof(s_slot_buffers[i]), NULL);
        text_layer_set_text(slot->layer, s_slot_buffers[i]);

#if defined(PBL_COLOR)
        text_layer_set_text_color(slot->layer, get_source_color(slot->source));
#else
        text_layer_set_text_color(slot->layer, s_active_theme->text_primary);
#endif
      }
    } else if (slot->layer) {
      layer_set_hidden(text_layer_get_layer(slot->layer), true);
    }
  }
}
