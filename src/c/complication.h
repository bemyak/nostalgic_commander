#pragma once
#include <pebble.h>
#include "data.h"

typedef void (*ComplicationFormatFn)(char* buf, int len, int* percent);
typedef void (*ComplicationDrawFn)(GContext*, GRect, ComplicationDataSource);

// How a slot's window is framed; the renderer per kind lives in drawing.c.
typedef enum {
  FRAME_PLAIN,         // solid frame, spec label as title
  FRAME_FULL_WEATHER,  // captioned four-chip bar
  FRAME_BT_QT,         // split BT/QT captions at top-slot width, plain title otherwise
  FRAME_HI_LO,         // HI/LO stub captions following the value's swap
  FRAME_AQI_UV,        // AQI/UV stubs over the band halves
  FRAME_HUM_PCP,       // HUM/PCP stubs over the two fields
} ComplicationFrame;

// What a complication source *is*, one row per source in complication.c's
// s_complication_specs[]: window title, value formatter, the source whose
// reading backs it (the progress bars mirror their plain counterpart — the
// render gate's snapshot follows this too), the value drawer and frame kind
// (renderers in drawing.c), and whether a slot showing it needs the weather
// feed. Adding a complication: one row here, a color case in status.c if it
// needs one, a Clay entry in config.js, and wire rows if phone-sourced.
// HealthMetric as a plain int (mock/SDK-proof); -1 = not health-backed.
// Explicit on every spec row: designated-init omission would read as 0, which
// *is* HealthMetricStepCount in the SDK — a forgotten field would silently
// attribute a new source to steps instead of reading as unset.
#define HEALTH_METRIC_NONE (-1)

typedef struct {
  ComplicationDataSource source;
  const char* label;
  ComplicationFormatFn format;  // NULL: format through `backs` (only EMPTY
                                // legitimately has neither)
  ComplicationDataSource backs;
  ComplicationDrawFn draw;  // NULL draws no value (EMPTY)
  ComplicationFrame frame;
  bool needs_weather;
  int health_metric;
} ComplicationSpec;

const ComplicationSpec* complication_spec(ComplicationDataSource source);

void get_source_data(ComplicationDataSource source, char* val_buf, int val_len, int* percent);
const char* get_source_label(ComplicationDataSource source);
bool any_slot_needs_weather(void);
bool any_slot_monitors_health(int metric);
