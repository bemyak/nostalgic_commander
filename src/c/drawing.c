#include <pebble.h>
#include "drawing.h"
#include "data.h"
#include "theme.h"

Window* s_main_window = NULL;
Layer* s_canvas_layer = NULL;
TextLayer* s_time_layer = NULL;
TextLayer* s_date_iso_layer = NULL;

void draw_sidebar_complication(GContext* ctx, GRect bounds, int source, bool from_top) {
  if (source == DATA_SOURCE_EMPTY) return;

  int percent = 0;
  char val_str[16];
  get_source_data(source, val_str, sizeof(val_str), &percent);

  // Background track (fully black)
  graphics_context_set_fill_color(ctx, s_active_theme->sidebar_bg);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // Fill progress bar
  int fill_height = (bounds.size.h * percent) / 100;
  if (fill_height > bounds.size.h) fill_height = bounds.size.h;

  if (fill_height > 0) {
    GColor fill_color = get_source_color(source);
    if (source == DATA_SOURCE_STEPS || source == DATA_SOURCE_ACTIVE_MINUTES) {
      fill_color = s_active_theme->steps_fill;
    }

    graphics_context_set_fill_color(ctx, fill_color);
    int y_start = from_top ? bounds.origin.y : (bounds.origin.y + bounds.size.h - fill_height);
    graphics_fill_rect(ctx, GRect(bounds.origin.x, y_start, bounds.size.w, fill_height), 0,
                       GCornerNone);
  }
}

static void draw_plus(GContext* ctx, int x, int y) {
  graphics_draw_line(ctx, GPoint(x - 2, y), GPoint(x + 2, y));
  graphics_draw_line(ctx, GPoint(x, y - 2), GPoint(x, y + 2));
}

static void draw_dashed_vline(GContext* ctx, int x, int y1, int y2) {
  for (int y = y1; y < y2; y += 6) {
    int len = (y + 3 < y2) ? 3 : (y2 - y);
    graphics_draw_line(ctx, GPoint(x, y), GPoint(x, y + len));
  }
}

static void draw_dashed_hline(GContext* ctx, int x1, int x2, int y) {
  for (int x = x1; x < x2; x += 6) {
    int len = (x + 3 < x2) ? 3 : (x2 - x);
    graphics_draw_line(ctx, GPoint(x, y), GPoint(x + len, y));
  }
}

void draw_ascii_window(GContext* ctx, GRect rect, const char* title) {
  int x = rect.origin.x;
  int y = rect.origin.y;
  int w = rect.size.w;
  int h = rect.size.h;

  graphics_context_set_stroke_color(ctx, s_active_theme->frame);
  graphics_context_set_stroke_width(ctx, 1);

  // Draw vertical borders (dashed)
  draw_dashed_vline(ctx, x, y + 2, y + h - 2);
  draw_dashed_vline(ctx, x + w, y + 2, y + h - 2);

  // Draw bottom border (dashed)
  draw_dashed_hline(ctx, x + 2, x + w - 2, y + h);

  // Draw top border (dashed, with title gap)
  int title_len = strlen(title);
  int title_width = title_len * 8 + 4;  // approx 8px per character + padding
  if (title_width > w - 10) title_width = w - 10;

  int x_mid = x + w / 2;
  int gap_left = x_mid - title_width / 2 - 3;
  int gap_right = x_mid + title_width / 2 + 3;

  draw_dashed_hline(ctx, x + 2, gap_left, y);
  draw_dashed_hline(ctx, gap_right, x + w - 2, y);

  // Draw four corner crosses (+)
  draw_plus(ctx, x, y);
  draw_plus(ctx, x + w, y);
  draw_plus(ctx, x, y + h);
  draw_plus(ctx, x + w, y + h);

  // Draw title text
  graphics_context_set_text_color(ctx, s_active_theme->text_secondary);
  graphics_draw_text(ctx, title, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     GRect(gap_left, y - 7, gap_right - gap_left, 14), GTextOverflowModeWordWrap,
                     GTextAlignmentCenter, NULL);
}

static void draw_aqi_uv_complication(GContext* ctx, GRect box_rect) {
  char aqi_str[8];
  char uv_str[8];

  if (s_weather_aqi == -1) {
    snprintf(aqi_str, sizeof(aqi_str), "--");
  } else {
    snprintf(aqi_str, sizeof(aqi_str), "%d", s_weather_aqi);
  }

  if (s_weather_uv == -1) {
    snprintf(uv_str, sizeof(uv_str), "--");
  } else {
    snprintf(uv_str, sizeof(uv_str), "%d", s_weather_uv);
  }

  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

  // AQI color logic (0-50 green, 51-100 yellow, >100 red)
  GColor aqi_color = s_active_theme->text_primary;
  if (s_weather_aqi != -1) {
    if (s_weather_aqi > 100) {
      aqi_color = s_active_theme->status_red;
    } else if (s_weather_aqi > 50) {
      aqi_color = s_active_theme->status_yellow;
    } else {
      aqi_color = s_active_theme->status_green;
    }
  }

  // UV color logic (0-2 green, 3-5 yellow, >=6 red)
  GColor uv_color = s_active_theme->text_primary;
  if (s_weather_uv != -1) {
    if (s_weather_uv >= 6) {
      uv_color = s_active_theme->status_red;
    } else if (s_weather_uv >= 3) {
      uv_color = s_active_theme->status_yellow;
    } else {
      uv_color = s_active_theme->status_green;
    }
  }

  int half_width = box_rect.size.w / 2;
  int center_x = box_rect.origin.x + half_width;
  int y_offset = box_rect.origin.y + 11;

  // Draw AQI (right-aligned in left half)
  GRect aqi_rect = GRect(box_rect.origin.x, y_offset, half_width - 5, 20);
  graphics_context_set_text_color(ctx, aqi_color);
  graphics_draw_text(ctx, aqi_str, font, aqi_rect, GTextOverflowModeWordWrap, GTextAlignmentRight,
                     NULL);

  // Draw Separator (centered)
  graphics_context_set_text_color(ctx, s_active_theme->text_primary);
  graphics_draw_text(ctx, " / ", font, GRect(box_rect.origin.x, y_offset, box_rect.size.w, 20),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

  // Draw UV (left-aligned in right half)
  GRect uv_rect = GRect(center_x + 5, y_offset, half_width - 5, 20);
  graphics_context_set_text_color(ctx, uv_color);
  graphics_draw_text(ctx, uv_str, font, uv_rect, GTextOverflowModeWordWrap, GTextAlignmentLeft,
                     NULL);
}

void canvas_update_proc(Layer* layer, GContext* ctx) {
  GRect bounds = layer_get_bounds(layer);

  // Clean background of center region
  graphics_context_set_fill_color(ctx, s_active_theme->center_bg);
  graphics_fill_rect(ctx, GRect(4, 0, bounds.size.w - 8, bounds.size.h), 0, GCornerNone);

  // Draw Sidebars
  draw_sidebar_complication(ctx, GRect(0, 0, 4, bounds.size.h), s_left_sidebar_source, true);
  draw_sidebar_complication(ctx, GRect(bounds.size.w - 4, 0, 4, bounds.size.h),
                            s_right_sidebar_source, false);

  // Draw TIME and DATE windows
  draw_ascii_window(ctx, GRect(10, 50, 180, 86), "TIME");
  draw_ascii_window(ctx, GRect(10, 142, 180, 36), "DATE");

  // Draw parameterized ASCII windows
  for (int i = 0; i < NUM_SLOTS; i++) {
    ComplicationSlot* slot = &s_complication_slots[i];
    if (slot->source != DATA_SOURCE_EMPTY) {
      draw_ascii_window(ctx, slot->box_rect, get_source_label(slot->source));
      if (slot->source == DATA_SOURCE_AQI_UV) {
        draw_aqi_uv_complication(ctx, slot->box_rect);
      }
    }
  }
}

void refresh_complications() {
  static char s_slot_buffers[NUM_SLOTS][40];

  for (int i = 0; i < NUM_SLOTS; i++) {
    ComplicationSlot* slot = &s_complication_slots[i];
    if (slot->source != DATA_SOURCE_EMPTY && slot->layer) {
      if (slot->source == DATA_SOURCE_AQI_UV) {
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
