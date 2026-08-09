#include <pebble.h>
#include "drawing.h"
#include "data.h"
#include "complication.h"
#include "status.h"
#include "theme.h"

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

// -------------------------------------------------------------------------
// The value row: the primitives every complication reads its pixels through.
//   layout   vga16_value_rect / status_band_rect — where a value sits
//   runs     draw_run / draw_accented_value / draw_shade_run — one color, or
//            one span picked out, or a multi-byte glyph repeated
//   idioms   draw_shortkey_complication / draw_plain_complication — quiet
//            readings, hint or none; draw_status_field / draw_banded_value /
//            draw_banded_complication — a severity band behind the text;
//            draw_progress_bar — goal fill plus a reading; draw_hinted_half
//            — one side of a two-field chip
// The bespoke painters below compose these; the registry's draw/frame fields
// in complication.c pick among them. canvas_update_proc() paints frames,
// captions, then values; request_ui_redraw() at the bottom is the change
// gate.
// -------------------------------------------------------------------------

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

// Draws a value with one run picked out in `accent` and the rest in `base`.
// Monospace, so splitting it is pure cell arithmetic and each run lands back
// on its own glyph column. `at < 0` accents nothing.
static void draw_accented_value(GContext* ctx, GRect row, const char* text, int at, int len,
                                GColor base, GColor accent) {
  int total = strlen(text);
  if (at < 0 || at >= total || len <= 0) {
    draw_run(ctx, row, 0, text, total, base);
    return;
  }
  if (at + len > total) len = total - at;

  draw_run(ctx, row, 0, text, at, base);
  draw_run(ctx, row, at, text + at, len, accent);
  draw_run(ctx, row, at + len, text + at + len, total - at - len, base);
}

// The trailing unit run of a reading — C/F, mm, mph, k, the final m of
// "7h 30m". NC menus dress a word's shortkey letter; "%" is a symbol, not a
// letter, so percent chips stay plain. Ends in a digit or bracket → no hint,
// sentinels like "--" included.
static bool trailing_unit_span(const char* text, int* at, int* len) {
  int total = strlen(text);
  int i = total;
  while (i > 0) {
    char c = text[i - 1];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) break;
    i--;
  }
  if (i == total) return false;
  *at = i;
  *len = total - i;
  return true;
}

// Plain rails: chips with no band of their own. The value's color still
// shifts with get_source_color (cold temp blues, a hot high reddens), and
// the trailing unit carries its shortkey accent.
void draw_shortkey_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source) {
  char buf[32];
  get_source_data(source, buf, sizeof(buf), NULL);
  int at = 0, len = 0;
  bool has_unit = trailing_unit_span(buf, &at, &len);
  draw_accented_value(ctx, vga16_value_rect(box_rect, buf), buf, has_unit ? at : -1, len,
                      get_source_color(source), s_active_theme->mark);
}

// A plain single-run value: no unit hint to find (the checkbox brackets are
// not letters, the day number is digits) or none wanted (a lone condition
// word would hint its whole self). Color still follows get_source_color.
void draw_plain_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source) {
  char buf[32];
  get_source_data(source, buf, sizeof(buf), NULL);
  draw_run(ctx, vga16_value_rect(box_rect, buf), 0, buf, strlen(buf), get_source_color(source));
}

void draw_weather_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source) {
  (void)source;  // bound to one source; see the registry row
  // The condition word leads, the temperature's unit trails — both wear the
  // theme mark, the value between stays primary. A missing reading stays
  // quiet on the ground.
  char buf[40];
  get_source_data(DATA_SOURCE_WEATHER, buf, sizeof(buf), NULL);

  int total = strlen(buf);
  bool has_reading = s_weather_temp != -999;
  const char* space = strchr(buf, ' ');
  int cond_len = (has_reading && space) ? (int)(space - buf) : 0;
  int unit_at = total, unit_len = 0;
  if (has_reading) trailing_unit_span(buf, &unit_at, &unit_len);

  GRect row = vga16_value_rect(box_rect, buf);
  draw_run(ctx, row, 0, buf, cond_len, s_active_theme->mark);
  draw_run(ctx, row, cond_len, buf + cond_len, unit_at - cond_len, s_active_theme->text_primary);
  draw_run(ctx, row, unit_at, buf + unit_at, total - unit_at, s_active_theme->mark);
}

// A date with its weekday picked out. Shared by the DATE window and the
// year-less slot complication, which order their weekday the same way.
static void draw_date_text(GContext* ctx, GRect box_rect, const char* text) {
  if (!text[0]) return;

  int at = date_dow_offset(s_settings_dow_position, text);
  draw_accented_value(ctx, vga16_value_rect(box_rect, text), text, at, DOW_LEN,
                      s_active_theme->text_primary, s_active_theme->mark);
}

void draw_full_date_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source) {
  (void)source;  // bound to one source; see the registry row
  draw_date_text(ctx, box_rect, s_date_display);
}

// CP437 shade characters, as UTF-8. FONT_VGA_16's characterRegex admits
// these two plus U+2665 BLACK HEART SUIT (the heart rate window's chrome);
// the 64pt clock draws none of them.
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

void draw_steps_bar_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source) {
  (void)source;  // bound to one source; see the registry row
  char buf[16];
  int percent = 0;
  get_source_data(DATA_SOURCE_STEPS, buf, sizeof(buf), &percent);
  draw_progress_bar(ctx, box_rect, percent, s_step_count != -1, s_active_theme->text_primary);
}

void draw_battery_bar_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source) {
  (void)source;  // bound to one source; see the registry row
  char buf[8];
  int percent = 0;
  get_source_data(DATA_SOURCE_BATTERY, buf, sizeof(buf), &percent);
  draw_progress_bar(ctx, box_rect, percent, true, get_source_color(DATA_SOURCE_BATTERY_BAR));
}

void draw_short_date_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source) {
  (void)source;  // bound to one source; see the registry row
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
  // Quiet on the ground: the trailing unit keeps its shortkey accent. On a
  // band everything is ink — a mark run on its own fill is mud.
  int at = 0, len = 0;
  if (!banded && trailing_unit_span(text, &at, &len)) {
    draw_accented_value(ctx, vga16_value_rect(box_rect, text), text, at, len,
                        s_active_theme->text_primary, s_active_theme->mark);
    return;
  }
  GRect b = status_band_rect(box_rect);
  draw_status_field(ctx, box_rect, b.origin.x, b.size.w, text, banded, band);
}

// The band shows exactly when the reading earns an attention color — both
// come from get_source_color(), so thresholds live in one place. A quiet or
// missing reading (text_primary) draws plain text on the ground.
static bool reading_commands_attention(ComplicationDataSource source) {
  return !gcolor_equal(get_source_color(source), s_active_theme->text_primary);
}

// A lone status chip (AQI, UV, PCP): bands when the reading earns an
// attention color, and both band and thresholds come from get_source_color.
// A calm reading stays quiet on the ground, unit hint accented.
void draw_banded_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source) {
  char buf[8];
  get_source_data(source, buf, sizeof(buf), NULL);
  draw_banded_value(ctx, box_rect, buf, reading_commands_attention(source),
                    get_source_color(source));
}

// One half of a two-field chip: banded → fill and ink; quiet → the trailing
// unit's shortkey accent, the same rail as the solo chips.
static void draw_hinted_half(GContext* ctx, GRect box_rect, int x, int w, const char* text,
                             bool banded, GColor band) {
  int at = 0, ulen = 0;
  if (!banded && trailing_unit_span(text, &at, &ulen)) {
    int len = strlen(text);
    GRect row = GRect(x + (w - len * VGA16_CHAR_W) / 2, box_rect.origin.y + VALUE_ROW_DY,
                      len * VGA16_CHAR_W, VALUE_ROW_H);
    draw_accented_value(ctx, row, text, at, ulen, s_active_theme->text_primary,
                        s_active_theme->mark);
    return;
  }
  draw_status_field(ctx, box_rect, x, w, text, banded, band);
}

// The two fields of the HUM/PCP strip: equal cell-width boxes split by the
// gap cell, centred as one strip. The drawer paints its halves in them, the
// frame centres its caption stubs on them — one geometry, so a layout edit
// can't drift the two apart.
static void hum_pcp_field_boxes(GRect box_rect, GRect* left, GRect* right) {
  const int field_px = HUM_PCP_FIELD_CELLS * VGA16_CHAR_W;
  const int gap_px = HUM_PCP_GAP_CELLS * VGA16_CHAR_W;
  int left_x = box_rect.origin.x + (box_rect.size.w - (2 * field_px + gap_px)) / 2;
  *left = GRect(left_x, box_rect.origin.y, field_px, box_rect.size.h);
  *right = GRect(left_x + field_px + gap_px, box_rect.origin.y, field_px, box_rect.size.h);
}

// Humidity and precipitation side by side, same two-field shape as
// AQI/UV: both halves carry the quiet-state unit hint; only the PCP half
// bands.
void draw_hum_pcp_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source) {
  (void)source;  // bound to one source; see the registry row
  GRect left, right;
  hum_pcp_field_boxes(box_rect, &left, &right);

  char hum_str[8];
  get_source_data(DATA_SOURCE_HUMIDITY, hum_str, sizeof(hum_str), NULL);
  draw_hinted_half(ctx, box_rect, left.origin.x, left.size.w, hum_str, false, GColorClear);

  char pcp_str[8];
  get_source_data(DATA_SOURCE_WEATHER_PCP, pcp_str, sizeof(pcp_str), NULL);
  draw_hinted_half(ctx, box_rect, right.origin.x, right.size.w, pcp_str,
                   reading_commands_attention(DATA_SOURCE_WEATHER_PCP),
                   get_source_color(DATA_SOURCE_WEATHER_PCP));
}

// Both readings side by side, each banding its own half of the cell so a good
// AQI next to a high UV reads as two fields rather than one blended color. The
// separator sits on the ground between them.
void draw_aqi_uv_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source) {
  (void)source;  // bound to one source; see the registry row
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

  // No separator glyph: the frame stubs above (AQI/UV) name the halves, a
  // cell of ground separates them, like the centre strip's chips.
  draw_status_field(ctx, box_rect, band.origin.x, half, aqi_str,
                    reading_commands_attention(DATA_SOURCE_AQI), get_source_color(DATA_SOURCE_AQI));
  draw_status_field(ctx, box_rect, right_x, half, uv_str,
                    reading_commands_attention(DATA_SOURCE_UV), get_source_color(DATA_SOURCE_UV));
}

// One chip per reading; one-cell gaps between chips. The fill comes from the
// atomic source's get_source_color, so severity thresholds stay
// single-authority — a chip with no data draws plain text, no band. The
// `reading` sentinel says when data is absent; condition shares temperature's
// because they arrive in the same AppMessage pair.
typedef struct {
  ComplicationDataSource source;
  int cells;
  const char* caption;
  const int* reading;
  int sentinel;
} FullWeatherField;

static const FullWeatherField s_full_weather_fields[] = {
    {DATA_SOURCE_WEATHER_COND, 4, "COND", &s_weather_temp, -999},
    // 4 cells since the unit letter always shows: "+22C"/"103F" fit whole.
    {DATA_SOURCE_WEATHER_TEMP, 4, "TMP", &s_weather_temp, -999},
    {DATA_SOURCE_HUMIDITY, 4, "HUM", &s_weather_humidity, -1},
    {DATA_SOURCE_WEATHER_PCP, 4, "PCP", &s_weather_pcp, -1},
};

#define FULL_WEATHER_NUM_FIELDS (sizeof(s_full_weather_fields) / sizeof(s_full_weather_fields[0]))

// Chips earn their fill from a severity color only; a neutral reading would
// paint its chip in the plain text color, which reads as a permanent
// highlight rather than status.
static bool strip_field_is_banded(const FullWeatherField* field) {
  if (*field->reading == field->sentinel) return false;
  return !gcolor_equal(get_source_color(field->source), s_active_theme->text_primary);
}

void draw_weather_full_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source) {
  (void)source;  // bound to one source; see the registry row
  // Centred strip; the caption label above is the same width, also centred,
  // so its tokens land cell-for-cell on the chips drawn here.
  int x = box_rect.origin.x + (box_rect.size.w - FULL_WEATHER_STRIP_CELLS * VGA16_CHAR_W) / 2;
  for (size_t i = 0; i < FULL_WEATHER_NUM_FIELDS; i++) {
    const FullWeatherField* field = &s_full_weather_fields[i];
    int w = field->cells * VGA16_CHAR_W;
    // Sized past chip width: a value wider than its chip centres in place
    // rather than clipping (no current reading exceeds its chip).
    char buf[12];
    if (field->source == DATA_SOURCE_WEATHER_TEMP) {
      format_strip_temp(buf, sizeof(buf));
    } else {
      get_source_data(field->source, buf, sizeof(buf), NULL);
    }
    if (field->source == DATA_SOURCE_WEATHER_COND) {
      // The strip's hotkey word, like the top chip's: the theme mark, never
      // a band — draw_status_field's color is fill-only, so the chip draws
      // its run directly. A missing reading stays quiet on the ground.
      int len = strlen(buf);
      GRect row = GRect(x + (w - len * VGA16_CHAR_W) / 2, box_rect.origin.y + VALUE_ROW_DY,
                        len * VGA16_CHAR_W, VALUE_ROW_H);
      draw_run(
          ctx, row, 0, buf, len,
          *field->reading != field->sentinel ? s_active_theme->mark : s_active_theme->text_primary);
    } else if (strip_field_is_banded(field)) {
      draw_status_field(ctx, box_rect, x, w, buf, true, get_source_color(field->source));
    } else {
      // A calm chip keeps its trailing unit's shortkey accent over the same
      // ground row.
      int len = strlen(buf);
      GRect row = GRect(x + (w - len * VGA16_CHAR_W) / 2, box_rect.origin.y + VALUE_ROW_DY,
                        len * VGA16_CHAR_W, VALUE_ROW_H);
      int unit_at = 0, unit_len = 0;
      bool has_unit = trailing_unit_span(buf, &unit_at, &unit_len);
      draw_accented_value(ctx, row, buf, has_unit ? unit_at : -1, unit_len,
                          s_active_theme->text_primary, s_active_theme->mark);
    }
    x += w + VGA16_CHAR_W;
  }
}

// The full-weather bar: a complete frame whose top line is mostly the
// per-chip captions — corner stubs, then short continuations in the
// inter-caption gaps, so the row reads as a window whose title is the header.
static void draw_captioned_bar(GContext* ctx, GRect rect, ComplicationDataSource source) {
  (void)source;
  int x = rect.origin.x;
  int y = rect.origin.y;
  int w = rect.size.w;
  int h = rect.size.h;
  int top = y + TITLE_BORDER_DROP;
  int strip_x = x + (w - FULL_WEATHER_STRIP_CELLS * VGA16_CHAR_W) / 2;

  graphics_context_set_fill_color(ctx, s_active_theme->frame);

  // Full frame: the top border's stubs flank the caption block, which takes
  // the rest of the top line — a window whose title is the whole header row.
  int side_h = h - TITLE_BORDER_DROP;
  graphics_fill_rect(ctx, GRect(x, top, WINDOW_BORDER_PX, side_h), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(x + w - WINDOW_BORDER_PX, top, WINDOW_BORDER_PX, side_h), 0,
                     GCornerNone);
  graphics_fill_rect(ctx, GRect(x, y + h - WINDOW_BORDER_PX, w, WINDOW_BORDER_PX), 0, GCornerNone);
  // Half a cell of air between each stub and the nearest caption *ink* — not
  // the chip edge: PCP's caption is narrower than its chip, so measuring
  // from the block edge parked the right stub noticeably far.
  const int pad = VGA16_CHAR_W / 2;
  const FullWeatherField* last = &s_full_weather_fields[FULL_WEATHER_NUM_FIELDS - 1];
  int last_caption_end = strip_x + (FULL_WEATHER_STRIP_CELLS - last->cells) * VGA16_CHAR_W +
                         (last->cells + (int)strlen(last->caption)) * VGA16_CHAR_W / 2;
  graphics_fill_rect(ctx, GRect(x, top, strip_x - x - pad, WINDOW_BORDER_PX), 0, GCornerNone);
  graphics_fill_rect(
      ctx, GRect(last_caption_end + pad, top, x + w - last_caption_end - pad, WINDOW_BORDER_PX), 0,
      GCornerNone);

  // One caption per chip, pixel-centred over it — the same centring math the
  // values get, so captions can't drift half a cell off their readings.
  graphics_context_set_text_color(ctx, s_active_theme->text_secondary);
  int chip_x = strip_x;
  for (size_t i = 0; i < FULL_WEATHER_NUM_FIELDS; i++) {
    const FullWeatherField* field = &s_full_weather_fields[i];
    int w_px = field->cells * VGA16_CHAR_W;
    int len = strlen(field->caption);
    graphics_draw_text(ctx, field->caption, vga_font_16(),
                       GRect(chip_x + (w_px - len * VGA16_CHAR_W) / 2, y - VGA16_CELL_H / 2,
                             len * VGA16_CHAR_W, VGA16_CELL_H),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    chip_x += w_px;

    // Frame continuation between neighbouring captions, 2px of air from each
    // ink run. Dash widths are uneven (4/8/16px) because the caption air is
    // uneven — that's the intended look, warts included.
    if (i + 1 < FULL_WEATHER_NUM_FIELDS) {
      const FullWeatherField* next = &s_full_weather_fields[i + 1];
      // chip_x already advanced past this chip, so ink ends where its
      // pixel-centred caption does.
      int ink_end = chip_x - (w_px - len * VGA16_CHAR_W) / 2;
      int next_chip_x = chip_x + VGA16_CHAR_W;
      int next_ink_start =
          next_chip_x + (next->cells - (int)strlen(next->caption)) * VGA16_CHAR_W / 2;
      int dash_w = next_ink_start - ink_end - 4;
      if (dash_w > 0) {
        graphics_fill_rect(ctx, GRect(ink_end + 2, top, dash_w, WINDOW_BORDER_PX), 0, GCornerNone);
      }
      chip_x = next_chip_x;
    }
  }
}

bool is_wide_slot(int width) {
  return width >= BT_QT_SPLIT_MIN_W;
}

// Two-stub frame: one caption straddling the top line per value half, each
// ink-centred over an anchor offset (px from the box's left edge); the runs
// before, between, and after carry the frame itself.
static void draw_split_caption_window(GContext* ctx, GRect rect, const char* left, int left_cx,
                                      const char* right, int right_cx) {
  int x = rect.origin.x;
  int y = rect.origin.y;
  int w = rect.size.w;
  int h = rect.size.h;
  int top = y + TITLE_BORDER_DROP;

  graphics_context_set_fill_color(ctx, s_active_theme->frame);

  graphics_fill_rect(ctx, GRect(x, top, WINDOW_BORDER_PX, h - TITLE_BORDER_DROP), 0, GCornerNone);
  graphics_fill_rect(ctx,
                     GRect(x + w - WINDOW_BORDER_PX, top, WINDOW_BORDER_PX, h - TITLE_BORDER_DROP),
                     0, GCornerNone);
  graphics_fill_rect(ctx, GRect(x, y + h - WINDOW_BORDER_PX, w, WINDOW_BORDER_PX), 0, GCornerNone);

  const int pad = VGA16_CHAR_W / 2;
  int left_x = x + left_cx - (int)strlen(left) * VGA16_CHAR_W / 2;
  int right_x = x + right_cx - (int)strlen(right) * VGA16_CHAR_W / 2;
  int left_end = left_x + (int)strlen(left) * VGA16_CHAR_W;
  int right_end = right_x + (int)strlen(right) * VGA16_CHAR_W;

  graphics_fill_rect(ctx, GRect(x, top, left_x - pad - x, WINDOW_BORDER_PX), 0, GCornerNone);
  graphics_fill_rect(ctx,
                     GRect(left_end + pad, top, right_x - pad - left_end - pad, WINDOW_BORDER_PX),
                     0, GCornerNone);
  graphics_fill_rect(ctx, GRect(right_end + pad, top, x + w - right_end - pad, WINDOW_BORDER_PX), 0,
                     GCornerNone);

  graphics_context_set_text_color(ctx, s_active_theme->text_secondary);
  graphics_draw_text(ctx, left, vga_font_16(),
                     GRect(left_x, y - VGA16_CELL_H / 2, strlen(left) * VGA16_CHAR_W, VGA16_CELL_H),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  graphics_draw_text(
      ctx, right, vga_font_16(),
      GRect(right_x, y - VGA16_CELL_H / 2, strlen(right) * VGA16_CHAR_W, VGA16_CELL_H),
      GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

void draw_wind_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source) {
  (void)source;  // bound to one source; see the registry row
  // Narrow slots drop the unit — the "↗ 12" form; wide ones carry the
  // canonical speed text. The arrow is multi-byte, so the strip is laid out
  // cell-by-cell (the heart drawer's precedent), never by
  // strlen-centred text.
  bool wide = is_wide_slot(box_rect.size.w);
  const char* arrow =
      s_weather_wind_direction < 0 ? NULL : wind_direction_arrow(s_weather_wind_direction);
  char speed[16];
  format_wind_speed(speed, sizeof(speed), wide);
  // Extreme wind takes the status band: severity color fill, ink flips — the
  // lone-reading convention from draw_status_field. rungs live in
  // get_source_color, so nothing here compares speeds.
  GColor band = get_source_color(DATA_SOURCE_WIND);
  bool banded = !gcolor_equal(band, s_active_theme->text_primary);
  GColor ink = banded ? s_active_theme->status_ink : band;
  if (banded) {
    graphics_context_set_fill_color(ctx, band);
    graphics_fill_rect(ctx, status_band_rect(box_rect), 0, GCornerNone);
  }
  if (!arrow) {
    // No bearing: the speed (or "--"), ASCII-only, centres by strlen.
    const char* buf = speed[0] ? speed : "--";
    draw_run(ctx, vga16_value_rect(box_rect, buf), 0, buf, strlen(buf), ink);
    return;
  }
  int cells = 1 + (speed[0] ? 1 + (int)strlen(speed) : 0);
  GRect row = GRect(box_rect.origin.x + (box_rect.size.w - cells * VGA16_CHAR_W) / 2,
                    box_rect.origin.y + VALUE_ROW_DY, cells * VGA16_CHAR_W, VALUE_ROW_H);
  graphics_context_set_text_color(ctx, ink);
  graphics_draw_text(ctx, arrow, vga_font_16(),
                     GRect(row.origin.x, row.origin.y, VGA16_CHAR_W, row.size.h),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  if (speed[0]) {
    int at = 0, len = 0;
    if (!banded && trailing_unit_span(speed, &at, &len)) {
      draw_run(ctx, row, 2, speed, at, ink);
      draw_run(ctx, row, 2 + at, speed + at, len, s_active_theme->mark);
    } else {
      draw_run(ctx, row, 2, speed, strlen(speed), ink);
    }
  }
}

// The wide form's boxes: 3 cells each, BT on strip cell 0, QT an air cell
// later. The drawer's runs and the frame's caption stubs anchor to the same
// boxes, so the split can't drift half a cell.
#define BT_QT_BOX_CELLS 3
#define BT_QT_QT_CELL (BT_QT_BOX_CELLS + 1)

static GRect bt_qt_strip_box(GRect box_rect, int strip_cell) {
  int strip_x = box_rect.origin.x + (box_rect.size.w - BT_QT_STRIP_CELLS * VGA16_CHAR_W) / 2;
  return GRect(strip_x + strip_cell * VGA16_CHAR_W, box_rect.origin.y + VALUE_ROW_DY,
               BT_QT_BOX_CELLS * VGA16_CHAR_W, VALUE_ROW_H);
}

void draw_bt_qt_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source) {
  (void)source;  // bound to one source; see the registry row
  char buf[8];
  // Reads its own combined source — the atomic Bluetooth row has no QT half.
  get_source_data(DATA_SOURCE_BT_QT, buf, sizeof(buf), NULL);
  GRect row = vga16_value_rect(box_rect, buf);
  GColor color = get_source_color(DATA_SOURCE_BLUETOOTH);
  if (!is_wide_slot(box_rect.size.w)) {
    // Narrow: the centred pair, both boxes tight together.
    draw_run(ctx, row, 0, buf, strlen(buf), color);
    return;
  }
  // Wide: the boxes hold the strip's centre but gain an air cell; the split
  // captions above stay registered to them.
  GRect bt = bt_qt_strip_box(box_rect, 0);
  GRect qt = bt_qt_strip_box(box_rect, BT_QT_QT_CELL);
  draw_run(ctx, bt, 0, buf, BT_QT_BOX_CELLS, color);
  draw_run(ctx, qt, 0, buf + BT_QT_BOX_CELLS, BT_QT_BOX_CELLS, color);
}

void draw_heart_rate_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source) {
  (void)source;  // bound to one source; see the registry row
  char buf[8];
  get_source_data(DATA_SOURCE_HEART_RATE, buf, sizeof(buf), NULL);
  if (s_heart_rate <= 0) {
    // No reading: plain dashed value, no heart — the icon only decorates a
    // live number.
    draw_run(ctx, vga16_value_rect(box_rect, buf), 0, buf, strlen(buf),
             get_source_color(DATA_SOURCE_HEART_RATE));
    return;
  }
  // A mark heart cell fused to the right of the digits — the accent rides
  // the tail like the units do; only words lead. The heart is multi-byte,
  // so — like the bar's shade runs — it takes an explicit one-cell span
  // instead of draw_run.
  int cells = (int)strlen(buf) + 1;
  GRect row = GRect(box_rect.origin.x + (box_rect.size.w - cells * VGA16_CHAR_W) / 2,
                    box_rect.origin.y + VALUE_ROW_DY, cells * VGA16_CHAR_W, VALUE_ROW_H);
  draw_run(ctx, row, 0, buf, strlen(buf), get_source_color(DATA_SOURCE_HEART_RATE));
  graphics_context_set_text_color(ctx, s_active_theme->mark);
  graphics_draw_text(
      ctx, "\xE2\x99\xA5" /* U+2665 BLACK HEART SUIT */, vga_font_16(),
      GRect(row.origin.x + (int)strlen(buf) * VGA16_CHAR_W, row.origin.y, VGA16_CHAR_W, row.size.h),
      GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

void draw_beats_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source) {
  (void)source;  // bound to one source; see the registry row
  char buf[8];
  get_source_data(DATA_SOURCE_BEATS, buf, sizeof(buf), NULL);

  // "@" prefix in DOS yellow, beat count in primary text
  draw_accented_value(ctx, vga16_value_rect(box_rect, buf), buf, 0, 1, s_active_theme->text_primary,
                      s_active_theme->mark);
}

void draw_battery_complication(GContext* ctx, GRect box_rect, ComplicationDataSource source) {
  (void)source;  // bound to one source; see the registry row
  char buf[8];
  get_source_data(DATA_SOURCE_BATTERY, buf, sizeof(buf), NULL);

  // A healthy charge just shows the ground. Below that — or whenever the
  // charger is in — it wears exactly the color the bar paints, so the two can
  // never disagree about the same reading.
  draw_banded_value(ctx, box_rect, buf, reading_commands_attention(DATA_SOURCE_BATTERY),
                    get_source_color(DATA_SOURCE_BATTERY));
}

// The frame renderer per kind; the per-source caption anchors live here, in
// the renderer, next to the split-window primitive they feed.
static void render_bt_qt_frame(GContext* ctx, GRect rect, ComplicationDataSource source) {
  if (!is_wide_slot(rect.size.w)) {
    draw_ascii_window(ctx, rect, get_source_label(source));
    return;
  }
  // Captions centre over the same strip boxes the checkboxes paint.
  GRect bt = bt_qt_strip_box(rect, 0);
  GRect qt = bt_qt_strip_box(rect, BT_QT_QT_CELL);
  draw_split_caption_window(ctx, rect, "BT", bt.origin.x + bt.size.w / 2 - rect.origin.x, "QT",
                            qt.origin.x + qt.size.w / 2 - rect.origin.x);
}

// The strip's two halves: 4 cells each around one air cell, on the value
// row. The stub captions centre on them — the centred value run lands its
// temperatures on the same halves.
#define HI_LO_HALF_CELLS 4

static GRect hi_lo_half_box(GRect box_rect, int half) {
  int strip_x = box_rect.origin.x + (box_rect.size.w - HI_LO_STRIP_CELLS * VGA16_CHAR_W) / 2;
  return GRect(strip_x + half * (HI_LO_HALF_CELLS + 1) * VGA16_CHAR_W,
               box_rect.origin.y + VALUE_ROW_DY, HI_LO_HALF_CELLS * VGA16_CHAR_W, VALUE_ROW_H);
}

// Never a plain-title window: the settings offer HI/LO for top slots only,
// but even off-list it gets its structured frame rather than a clipped
// title. Stubs name the current round and follow the same swap the value
// performs.
static void render_hi_lo_frame(GContext* ctx, GRect rect, ComplicationDataSource source) {
  (void)source;
  bool hi_leads = high_low_hi_leads();
  GRect left = hi_lo_half_box(rect, 0);
  GRect right = hi_lo_half_box(rect, 1);
  draw_split_caption_window(ctx, rect, hi_leads ? "HI" : "LO",
                            left.origin.x + left.size.w / 2 - rect.origin.x, hi_leads ? "LO" : "HI",
                            right.origin.x + right.size.w / 2 - rect.origin.x);
}

// Two-field windows: captions centre over the fields the value drawers paint
// below — band halves for AQI/UV, the tight strip for HUM/PCP.
static void render_aqi_uv_frame(GContext* ctx, GRect rect, ComplicationDataSource source) {
  (void)source;
  GRect band = status_band_rect(rect);
  int half = (band.size.w - VGA16_CHAR_W) / 2;
  draw_split_caption_window(ctx, rect, "AQI", band.origin.x + half / 2 - rect.origin.x, "UV",
                            band.origin.x + band.size.w - half / 2 - rect.origin.x);
}

static void render_hum_pcp_frame(GContext* ctx, GRect rect, ComplicationDataSource source) {
  (void)source;
  GRect left, right;
  hum_pcp_field_boxes(rect, &left, &right);
  draw_split_caption_window(ctx, rect, "HUM", left.origin.x + left.size.w / 2 - rect.origin.x,
                            "PCP", right.origin.x + right.size.w / 2 - rect.origin.x);
}

typedef void (*FrameRenderFn)(GContext*, GRect, ComplicationDataSource);
static const struct {
  ComplicationFrame frame;
  FrameRenderFn render;
} s_frame_renderers[] = {
    {FRAME_FULL_WEATHER, draw_captioned_bar}, {FRAME_BT_QT, render_bt_qt_frame},
    {FRAME_HI_LO, render_hi_lo_frame},        {FRAME_AQI_UV, render_aqi_uv_frame},
    {FRAME_HUM_PCP, render_hum_pcp_frame},
};

static FrameRenderFn frame_renderer(ComplicationFrame frame) {
  for (unsigned i = 0; i < sizeof(s_frame_renderers) / sizeof(s_frame_renderers[0]); i++) {
    if (s_frame_renderers[i].frame == frame) return s_frame_renderers[i].render;
  }
  return NULL;
}

// The bottom slot row is what Timeline Quick View covers; while it is up
// these slots simply stop drawing — honest occlusion, no reflow.
static bool quick_view_covers_slot(int index) {
  return index >= SLOT_IDX_BOTTOM_LEFT && index <= SLOT_IDX_BOTTOM_RIGHT;
}

void canvas_update_proc(Layer* layer, GContext* ctx) {
  // No background fill: the window root layer fills the whole frame with
  // window->background_color on every render pass (PebbleOS
  // window_do_layer_update_proc), and apply_theme() keeps it at center_bg.
  (void)layer;

  // TIME is fixed; the centre row is the sixth slot, so the loop below draws
  // its frame and title from whatever source it holds.
  draw_ascii_window(ctx, TIME_WINDOW_RECT, "TIME");

  // Draw parameterized ASCII windows
  for (int i = 0; i < NUM_SLOTS; i++) {
    ComplicationSlot* slot = &s_complication_slots[i];
    if (s_quick_view_active && quick_view_covers_slot(i)) continue;
    if (slot->source != DATA_SOURCE_EMPTY) {
      const ComplicationSpec* spec = complication_spec(slot->source);
      FrameRenderFn frame = spec ? frame_renderer(spec->frame) : NULL;
      if (frame) {
        frame(ctx, slot->box_rect, slot->source);
      } else {
        // A spec-less source (stale persisted id) gets the "???" title frame
        // and no value.
        draw_ascii_window(ctx, slot->box_rect, get_source_label(slot->source));
      }
      if (spec && spec->draw) spec->draw(ctx, slot->box_rect, slot->source);
    }
  }
}

// The output-purity contract: every drawn pixel derives from these fields;
// visual state this struct can't carry needs a field added here.
typedef struct {
  const WatchTheme* theme;
  ComplicationDataSource source[NUM_SLOTS];
  char text[NUM_SLOTS][40];
  int percent[NUM_SLOTS];
  // Charging flips the battery band without touching its text or fill ratio;
  // per battery slot so the gate hears charge-state changes on their own.
  bool battery_charging[NUM_SLOTS];
  // Obstruction is display state: the bottom row vanishing must pass the
  // memcmp gate even when no string or fill changed.
  bool quick_view_active;
  // The HI/LO frame's caption stubs swap sides with the lead, outside any
  // snapshotted text — an equal-value day flips the frame alone, so the
  // lead itself is display state (one derivation shared by all HI/LO slots).
  bool hi_lo_hi_leads;
} UiSnapshot;

// What the last scheduled render will draw. Compared whole; build_snapshot
// memsets first so padding and string slack can't poison the memcmp.
static UiSnapshot s_shown_ui;

// Everything on screen is derived from slot contents and the theme: values
// and bar fills come out of get_source_data's text and percent, bands and
// accents out of theme colors and the thresholds it reads. A change nothing
// displays (e.g. battery with no battery slot) never shows up here — which
// is exactly the gate.
static void build_snapshot(UiSnapshot* s) {
  memset(s, 0, sizeof(*s));
  s->theme = s_active_theme;
  s->quick_view_active = s_quick_view_active;
  s->hi_lo_hi_leads = high_low_hi_leads();
  for (int i = 0; i < NUM_SLOTS; i++) {
    ComplicationSlot* slot = &s_complication_slots[i];
    // The label and frame follow the configured source; the value follows
    // whatever the drawer actually reads (for the bars, through the
    // registry's .backs chain to the plain counterpart).
    s->source[i] = slot->source;
    get_source_data(slot->source, s->text[i], sizeof(s->text[i]), &s->percent[i]);
    const ComplicationSpec* spec = complication_spec(slot->source);
    s->battery_charging[i] = spec && spec->backs == DATA_SOURCE_BATTERY && s_battery_charging;
  }
}

void reset_ui_snapshot(void) {
  memset(&s_shown_ui, 0, sizeof(s_shown_ui));
}

// Schedules a render only when what the screen shows has changed. The tick's
// clock set_text already guarantees one full-tree render a minute (PebbleOS
// marks carry no region and re-run every visible layer's update_proc), so
// this gate is about events: health, battery, Bluetooth, inbox.
void request_ui_redraw(void) {
  UiSnapshot now;
  build_snapshot(&now);
  if (memcmp(&now, &s_shown_ui, sizeof(now)) == 0) return;

  s_shown_ui = now;
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}
