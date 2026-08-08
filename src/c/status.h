#pragma once
#include <pebble.h>
#include "data.h"

// The color a reading draws in right now, per source: the face's severity
// policy (battery ladder, Beaufort rungs, AQI/UV bands, WMO rates, hot/cold
// bands) all decided here, so a chip, its bar, and its band can never
// disagree about one reading. theme.c has the palettes; this file picks
// from them.
GColor get_source_color(ComplicationDataSource source);
