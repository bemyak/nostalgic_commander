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
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// The centred rect a monospace value occupies inside its window: a whole number
// of glyph cells wide, sitting on the shared value row.
static GRect vga16_value_rect(GRect box_rect, const char* value) {
  int w = strlen(value) * VGA16_CHAR_W;
  return GRect(box_rect.origin.x + (box_rect.size.w - w) / 2, box_rect.origin.y + VALUE_ROW_DY, w,
               VALUE_ROW_H);
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
      GTextOverflowModeFill, GTextAlignmentLeft, NULL);
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

// A date with its weekday picked out. Shared by the DATE window and the
// year-less slot complication, which order their weekday the same way.
static void draw_date_text(GContext* ctx, GRect box_rect, const char* text) {
  if (!text[0]) return;

  int at = date_dow_offset(s_settings_dow_position, text);
  draw_accented_value(ctx, vga16_value_rect(box_rect, text), text, at, DOW_LEN,
                      s_active_theme->mark);
}

static void draw_full_date_complication(GContext* ctx, GRect box_rect) {
  draw_date_text(ctx, box_rect, s_date_display);
}

// CP437 shade characters, as UTF-8. FONT_VGA_16's characterRegex admits exactly
// these two; the 64pt clock never draws them.
#define BAR_TRACK "\xE2\x96\x91"  // U+2591 LIGHT SHADE
#define BAR_VALUE_CELLS 4         // three digits plus '%'
#define BAR_VALUE_MAX 999         // ...so this is the largest reading that fits

// Repeats a shade glyph across `cells`. Byte length is not cell count here, so
// this cannot go through draw_run — the caller states the cell span instead.
static void draw_shade_run(GContext* ctx, GRect row, int cell, int cells, const char* glyph,
                           GColor color) {
  if (cells <= 0) return;

  char buf[3 * 32 + 1];
  int n = 0;
  for (int i = 0; i < cells && n + 3 < (int)sizeof(buf); i++, n += 3) {
    memcpy(buf + n, glyph, 3);
  }
  buf[n] = '\0';

  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(
      ctx, buf, vga_font_16(),
      GRect(row.origin.x + cell * VGA16_CHAR_W, row.origin.y, cells * VGA16_CHAR_W, row.size.h),
      GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

// A DOS progress bar: full blocks for the filled part, light shade for the
// track, and the percentage right-aligned after it. Keeping the reading outside
// the bar means nothing has to stay legible on top of a fill.
static void draw_progress_bar(GContext* ctx, GRect box_rect, int percent, bool has_reading,
                              GColor fill) {
  GRect band = status_band_rect(box_rect);
  int cells = band.size.w / VGA16_CHAR_W;
  int bar_cells = cells - BAR_VALUE_CELLS - 1;  // one cell of gap before the value
  if (bar_cells < 1) return;

  if (percent < 0) percent = 0;

  // The fill stops at the end of the bar, but the reading beside it does not —
  // 250% of a step goal is worth seeing. It caps at BAR_VALUE_MAX because that
  // is the widest number BAR_VALUE_CELLS can hold.
  int fill_percent = percent > 100 ? 100 : percent;
  if (percent > BAR_VALUE_MAX) percent = BAR_VALUE_MAX;

  int filled = has_reading ? (fill_percent * bar_cells) / 100 : 0;

  GRect row = GRect(band.origin.x + (band.size.w - cells * VGA16_CHAR_W) / 2,
                    box_rect.origin.y + VALUE_ROW_DY, cells * VGA16_CHAR_W, VALUE_ROW_H);

  // The fill is a solid block: one rect, not a glyph run. The track stays
  // glyphs — ░ is a dither pattern, not a color. Rect geometry is the ink box
  // the █ run occupied; pixel-identity is screenshot-gated, not assumed.
  if (filled > 0) {
    graphics_context_set_fill_color(ctx, fill);
    graphics_fill_rect(ctx, GRect(row.origin.x, band.origin.y, filled * VGA16_CHAR_W, VGA16_CELL_H),
                       0, GCornerNone);
  }
  draw_shade_run(ctx, row, filled, bar_cells - filled, BAR_TRACK, s_active_theme->frame);

  char value[8];
  if (has_reading) {
    snprintf(value, sizeof(value), "%*d%%", BAR_VALUE_CELLS - 1, percent);
  } else {
    snprintf(value, sizeof(value), "%*s", BAR_VALUE_CELLS, "--");
  }
  draw_run(ctx, row, bar_cells + 1, value, strlen(value), s_active_theme->text_primary);
}

static void draw_steps_bar_complication(GContext* ctx, GRect box_rect) {
  char buf[16];
  int percent = 0;
  get_source_data(DATA_SOURCE_STEPS, buf, sizeof(buf), &percent);
  draw_progress_bar(ctx, box_rect, percent, s_step_count != -1, s_active_theme->text_primary);
}

static void draw_battery_bar_complication(GContext* ctx, GRect box_rect) {
  char buf[8];
  int percent = 0;
  get_source_data(DATA_SOURCE_BATTERY, buf, sizeof(buf), &percent);
  draw_progress_bar(ctx, box_rect, percent, true, s_active_theme->text_primary);
}

static void draw_short_date_complication(GContext* ctx, GRect box_rect) {
  draw_date_text(ctx, box_rect, s_short_date_display);
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

// One chip per reading; one-cell gaps between chips. The fill comes from the
// atomic source's get_source_color, so severity thresholds stay
// single-authority — a chip with no data draws plain text, no band. The
// `reading` sentinel says when data is absent; condition shares temperature's
// because they arrive in the same AppMessage pair.
typedef struct {
  ComplicationDataSource source;
  int cells;
  const int* reading;
  int sentinel;
} FullWeatherField;

static const FullWeatherField s_full_weather_fields[] = {
    {DATA_SOURCE_WEATHER_COND, 4, &s_weather_temp, -999},
    {DATA_SOURCE_WEATHER_TEMP, 4, &s_weather_temp, -999},
    {DATA_SOURCE_HUMIDITY, 4, &s_weather_humidity, -1},
    {DATA_SOURCE_TEMP_HIGH_LOW, 7, &s_temp_high, -999},
};

#define FULL_WEATHER_NUM_FIELDS (sizeof(s_full_weather_fields) / sizeof(s_full_weather_fields[0]))

static void draw_weather_full_complication(GContext* ctx, GRect box_rect) {
  // Centred strip; the caption label above is the same width, also centred,
  // so its tokens land cell-for-cell on the chips drawn here.
  int x = box_rect.origin.x + (box_rect.size.w - FULL_WEATHER_STRIP_CELLS * VGA16_CHAR_W) / 2;
  for (size_t i = 0; i < FULL_WEATHER_NUM_FIELDS; i++) {
    const FullWeatherField* field = &s_full_weather_fields[i];
    int w = field->cells * VGA16_CHAR_W;
    // Sized past chip width: deep-winter hi/lo ("-22/-35C") intentionally
    // spills 4px into the flanking gap rather than silently clipping a digit.
    char buf[12];
    get_source_data(field->source, buf, sizeof(buf), NULL);
    draw_status_field(ctx, box_rect, x, w, buf, *field->reading != field->sentinel,
                      get_source_color(field->source));
    x += w + VGA16_CHAR_W;
  }
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

  // A healthy charge just shows the ground rather than sitting under a permanent
  // green block. Below that it wears exactly the color the bar would use, so the
  // two can never disagree about the same reading.
  draw_banded_value(ctx, box_rect, buf, s_battery_level <= BATTERY_LOW_PCT,
                    get_source_color(DATA_SOURCE_BATTERY));
}

typedef void (*ComplicationDrawFn)(GContext*, GRect);

// Sources that paint their value straight onto the canvas instead of using the
// slot's TextLayer: AQI/UV so its halves can be coloured independently, BEATS
// and BATTERY so they land on whole glyph cells. Both canvas_update_proc and
// request_ui_redraw go through here, so the set is stated exactly once —
// returning NULL means "this source uses the generic text layer".
static ComplicationDrawFn canvas_drawer(ComplicationDataSource source) {
  switch (source) {
    case DATA_SOURCE_AQI_UV:
      return draw_aqi_uv_complication;
    case DATA_SOURCE_WEATHER_FULL:
      return draw_weather_full_complication;
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
    case DATA_SOURCE_SHORT_DATE:
      return draw_short_date_complication;
    case DATA_SOURCE_FULL_DATE:
      return draw_full_date_complication;
    case DATA_SOURCE_STEPS_BAR:
      return draw_steps_bar_complication;
    case DATA_SOURCE_BATTERY_BAR:
      return draw_battery_bar_complication;
    default:
      return NULL;
  }
}

// The bottom slot row (indexes 2-4, y=184 and down) is what Timeline Quick
// View covers; while it is up these slots simply stop drawing — honest
// occlusion, no reflow.
static bool quick_view_covers_slot(int index) {
  return index >= 2 && index <= 4;
}

void canvas_update_proc(Layer* layer, GContext* ctx) {
  // No background fill: the window root layer fills the whole frame with
  // window->background_color on every render pass (PebbleOS
  // window_do_layer_update_proc), and apply_theme() keeps it at center_bg.
  (void)layer;

  // TIME is fixed; the centre row is the sixth slot, so the loop below draws
  // its frame and title from whatever source it holds.
  draw_ascii_window(ctx, GRect(LAYOUT_X, 50, LAYOUT_W, 86), "TIME");

  // Draw parameterized ASCII windows
  for (int i = 0; i < NUM_SLOTS; i++) {
    ComplicationSlot* slot = &s_complication_slots[i];
    if (s_quick_view_active && quick_view_covers_slot(i)) continue;
    if (slot->source != DATA_SOURCE_EMPTY) {
      draw_ascii_window(ctx, slot->box_rect, get_source_label(slot->source));
      ComplicationDrawFn draw = canvas_drawer(slot->source);
      if (draw) draw(ctx, slot->box_rect);
    }
  }
}

typedef struct {
  const WatchTheme* theme;
  ComplicationDataSource source[NUM_SLOTS];
  char text[NUM_SLOTS][40];
  int percent[NUM_SLOTS];
  // Obstruction is display state: the bottom row vanishing must pass the
  // memcmp gate even when no string or fill changed.
  bool quick_view_active;
} UiSnapshot;

// What the last scheduled render will draw. Compared whole; build_snapshot
// memsets first so padding and string slack can't poison the memcmp.
static UiSnapshot s_shown_ui;

// Backing for the slot TextLayers: text_layer_set_text keeps the pointer, so
// the strings must outlive the call.
static char s_slot_text[NUM_SLOTS][40];

// The bar sources have no get_source_data case of their own — their drawers
// read the plain counterpart (draw_steps_bar_complication passes
// DATA_SOURCE_STEPS, draw_battery_bar_complication passes
// DATA_SOURCE_BATTERY). Snapshotting slot->source for a bar would record
// empty text and percent 0 forever, so a bar would never register a change.
static ComplicationDataSource snapshot_source(ComplicationDataSource source) {
  switch (source) {
    case DATA_SOURCE_STEPS_BAR:
      return DATA_SOURCE_STEPS;
    case DATA_SOURCE_BATTERY_BAR:
      return DATA_SOURCE_BATTERY;
    default:
      return source;
  }
}

// Everything on screen is derived from slot contents and the theme: values
// and bar fills come out of get_source_data's text and percent, bands and
// accents out of theme colors and the thresholds it reads. A change nothing
// displays (e.g. battery with no battery slot) never shows up here — which
// is exactly the gate.
static void build_snapshot(UiSnapshot* s) {
  memset(s, 0, sizeof(*s));
  s->theme = s_active_theme;
  s->quick_view_active = s_quick_view_active;
  for (int i = 0; i < NUM_SLOTS; i++) {
    ComplicationSlot* slot = &s_complication_slots[i];
    // The label and frame follow the configured source; the value follows
    // whatever the drawer actually reads.
    s->source[i] = slot->source;
    get_source_data(snapshot_source(slot->source), s->text[i], sizeof(s->text[i]), &s->percent[i]);
  }
}

void reset_ui_snapshot(void) {
  memset(&s_shown_ui, 0, sizeof(s_shown_ui));
  memset(s_slot_text, 0, sizeof(s_slot_text));
}

// Schedules a render only when what the screen shows has changed. The tick's
// clock set_text already guarantees one full-tree render a minute (PebbleOS
// marks carry no region and re-run every visible layer's update_proc), so
// this gate is about events: health, battery, Bluetooth, inbox.
void request_ui_redraw(void) {
  UiSnapshot now;
  build_snapshot(&now);
  if (memcmp(&now, &s_shown_ui, sizeof(now)) == 0) return;

  for (int i = 0; i < NUM_SLOTS; i++) {
    ComplicationSlot* slot = &s_complication_slots[i];
    if (!slot->layer) continue;
    bool text_backed = slot->source != DATA_SOURCE_EMPTY && !canvas_drawer(slot->source);
    // A covered row's text layers hide while Quick View is over them; their
    // text keeps tracking underneath, so the restore shows current values.
    bool covered = now.quick_view_active && quick_view_covers_slot(i);
    layer_set_hidden(text_layer_get_layer(slot->layer), !text_backed || covered);
    if (!text_backed) continue;
    if (strcmp(now.text[i], s_slot_text[i]) != 0 || now.source[i] != s_shown_ui.source[i]) {
      strcpy(s_slot_text[i], now.text[i]);
      text_layer_set_text(slot->layer, s_slot_text[i]);
    }
    // Colors re-apply even when the string is unchanged (theme rollover);
    // text_layer_set_text_color early-returns when nothing changed.
#if defined(PBL_COLOR)
    text_layer_set_text_color(slot->layer, get_source_color(slot->source));
#else
    text_layer_set_text_color(slot->layer, s_active_theme->text_primary);
#endif
  }

  s_shown_ui = now;
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}
