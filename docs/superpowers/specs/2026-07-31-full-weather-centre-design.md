# Full-Weather Centre Complication — Design

2026-07-31 · Status: approved (layout B + caption line, per user review)

## Summary

Add `DATA_SOURCE_WEATHER_FULL`, offered only in the centre slot (SLOT_6), which
shows all five weather readings — condition, temperature, humidity, AQI, UV —
as a row of five fixed-width status chips, captioned per field in the frame
title: Norton Commander bottom-bar style.

## Approved decisions

- **Layout B (status strip)**, not a tinted text line: each reading pinned to a
  fixed column; a plain text line was rejected because tokens slide as value
  widths change.
- **Per-field captions in the title gap** instead of the `WEATHER` title.
  Captions only align with layout B's fixed columns.

## Source behavior

- `DATA_SOURCE_WEATHER_FULL = 27` in `data.h`, appended before
  `DATA_SOURCE_EMPTY` (nothing renumbered).
- No new phone data: rides the five values already fetched and cached
  (temp/cond/humidity/AQI/UV). No new message keys, no JS changes, no new
  persist keys. `any_slot_needs_weather()` gains the source.
- Label (frame title) is the caption string `COND TEMP  HUM AQI UV` — exactly
  21 cells, aligned token-over-chip (see Layout). `HUM` is centred in its
  4-cell chip; the other captions exactly fill theirs.
- `get_source_data()` returns the readings space-joined (`SUN 72F 45% 12 3`,
  sentinels as `--`, worst case `TSTM -22C 100% 150 11` = 21 chars); never
  displayed — it feeds the `UiSnapshot` render gate so weather changes pass
  the memcmp like every other canvas-drawn source. `*percent` stays 0.

## Layout (all widths in 8px cells; centre slot interior is 23 columns)

Chip cells: condition 4, temp 4, humidity 4, AQI 3, UV 2, with 1-cell gaps.
Total 21 cells = 168px, centred in the 184px box → strip starts at box x+8.
The caption string is also 21 cells and is drawn centred in the title gap, so
its tokens land on the chips cell-for-cell. Single source of truth:
`FULL_WEATHER_STRIP_CELLS` (21) in `drawing.h`, used by the renderer and
asserted against `strlen(get_source_label(...))` in tests so label and strip
can never drift.

```
┌COND TEMP  HUM AQI UV┐     ← captions in text_secondary, chips' columns
█▓▓▓▓ ▓▓▓▓ ▓▓▓▓ ▓▓▓ ▓▓█     ← fills: each reading's severity color
█TSTM -22C 100% 150 11█     ← status_ink text, centred within its chip
█▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄█
```

## Rendering (drawing.c)

Reuse the existing `draw_status_field()` vocabulary (as the split AQI/UV
source does), extended from 2 fields to 5 via a static field table of
`(source, cells)` pairs:

| Field | Text from | Fill color from | `banded` when |
|-------|-----------|-----------------|----------------|
| Condition | `DATA_SOURCE_WEATHER_COND` | `text_primary` (default case) | `s_weather_temp != -999` |
| Temperature | `DATA_SOURCE_WEATHER_TEMP` | existing temp bands | `s_weather_temp != -999` |
| Humidity | `DATA_SOURCE_HUMIDITY` | new humidity bands | `!= -1` |
| AQI | `DATA_SOURCE_AQI` | existing AQI bands | `!= -1` |
| UV | `DATA_SOURCE_UV` | existing UV bands | `!= -1` |

Severity colors therefore stay single-authority: `get_source_color()` of the
atomic source. A sentinel field draws plain `--` on the ground, no chip —
matching split AQI/UV. Condition and temperature share the temp sentinel
because they arrive in the same AppMessage pair.

A 21-cell title leaves only corner stubs of the top border — intentional:
the caption row reads as the bar's header.

## Config page

SLOT_6 options become: Date (23) · **Full Weather (27)** · Steps Progress (24) ·
Battery Progress (25). No other slot offers it (only the centre row is wide
enough).

## Tests

- `get_source_data` formatting: all sentinels → `-- -- -- -- --`; typical and
  worst-case mixes; metric/imperial unit switch on the temp field.
- Strip invariants: `strlen(get_source_label(DATA_SOURCE_WEATHER_FULL))` equals
  the field table's cells+gaps, equals `FULL_WEATHER_STRIP_CELLS`, and fits the
  22.5-cell interior (the test file `#include`s the sources, so the static
  field table is directly inspectable).

No new fetch/cache/JS logic to test — already covered by the humidity work.

## Docs

ARCHITECTURE.md centre-row description gains the new source.

## Non-goals

No reordering of fields, no caption color per field (captions render in
`text_secondary` like every title), no offering in other slots, no animation
on value change.

## Verification

`make test` green, `pebble build` green, reviewer pass on the diff.
