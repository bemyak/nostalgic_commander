# Full-Weather Centre Complication — Design

2026-07-31 · Status: implemented; revised across three same-day emulator
reviews. This document describes the final design; git history carries the
iterations (AQI/UV chips replaced by hi/lo; banding and caption fixes).

## Summary

`DATA_SOURCE_WEATHER_FULL` (27), offered only in the centre slot (SLOT_6),
renders four readings — condition, temperature, humidity, high/low — as a row
of fixed-width status chips, each captioned in the frame's header position.
Norton Commander menu-bar style: the caption row replaces the top border
outright (verticals and bottom border only).

## Source behavior

- Enum appended before `DATA_SOURCE_EMPTY`; no renumbering.
- No new phone data: rides values already fetched and cached (temp/cond/
  humidity/high/low). No new message keys, no JS semantics changes, no new
  persist keys. `any_slot_needs_weather()` includes the source.
- `get_source_data()` returns the four chip texts space-joined — never
  displayed; it feeds the `UiSnapshot` render gate so any weather change
  passes the memcmp. `*percent` stays 0.

## Layout (all widths in 8px cells; centre slot is 23 columns of 184px)

Chips: `COND` 4 · `TMP` 3 · `HUM` 4 · `HI/LO` 7, one-cell gaps, 21 cells
total = 168px, centred (8px margins). Each caption token lives in the field
table and is drawn pixel-centred over its chip with the same centring math
the value gets — caption and value can't drift half a cell apart.
`get_source_label()` returns `""`; frame and captions are `draw_captioned_bar()`'s job:

```
─COND─TMP─HUM──HI/LO─       ← top border stubs flank the caption block
▓▓▓▓ ▓▓▓ ▓▓▓▓ ▓▓▓▓▓▓▓        ← fills only when severity-colored
```

Width policies: imperial keeps unit letters (`72F`, `82/61F`); metric always
signs and drops the letter to fund the sign cell (`+22`, `+28/+4`). Accepted
spills into the 1-cell gaps: imperial extremes (`103F`, `-22F`, and the polar
pair `-22/-35F`) run 4px over a chip edge; metric never exceeds its chips.
Chip texts come from `format_strip_temp()` / `format_strip_high_low()` in
data.c, **not** the atomic `get_source_data()` formatters, so the strip can
spell temps differently from the standalone complications.

## Rendering

- Static field table `(source, cells, reading, sentinel)` in drawing.c is the
  single authority for composition and width; condition shares temperature's
  sentinel (they arrive paired).
- Chip fill = `get_source_color()` of the atomic source (severity thresholds
  stay single-authority), **but a chip fills only when that color differs
  from `text_primary`** — a neutral reading is plain text, not a band
  (`strip_field_is_banded()`). Sentinels render plain `--` / `-- / --`.
- Values issue through `draw_status_field()` (banded or not), text centred
  within the chip.

## Config page

SLOT_6 options: Date (23) · **Full Weather (27)** · Steps Progress (24) ·
Battery Progress (25). No other slot offers it.

## Tests

- Formatters: atomic and strip spelling per unit, sign/unit trade, sentinels,
  acknowledged imperial spill.
- Composite snapshot join: all-sentinel, mixed-sentinel, typical, cold snap.
- Alignment: a test parses the caption string and asserts each token lands
  floor-centred over its chip, derived purely from the field table, and that
  the strip fits between the borders.
- Banding: `strip_field_is_banded()` pins neutral/plain, status/fill, and
  sentinel behavior per field.

## Non-goals

No narrow-slot variants, no caption color per field, no animation.

## Verification

`make test` green · `pebble build` green · reviewer passes on each diff ·
three emulator visual passes.
