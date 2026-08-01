# Humidity Complication — Design

2026-07-31 · Status: approved (color option A; config order as specified below)

## Summary

Add relative humidity as a new curated complication, sourced from the same
Open-Meteo forecast request that already delivers temperature/condition/UV,
rendered with two-sided comfort-band coloring. In the same pass, reorder the
Clay config option lists — currently append-since-forever — into stable domain
groups: Empty, system, date, health, weather.

## Approved decisions

- **Color treatment: option A (two-sided comfort bands).** Consistent with the
  other phone-sourced readings (AQI, UV, temp), which all encode severity in
  color.
- **Config ordering: Empty → system → date → health → weather.** Weather goes
  last because it is the largest group, not by importance.

## Data & state

- `DATA_SOURCE_HUMIDITY = 26` in `data.h`, appended before `DATA_SOURCE_EMPTY`.
  All existing enum values keep their numbers (19 stays retired); values are
  persisted identifiers and appear as strings in `config.json`.
- `int s_weather_humidity = -1` in `data.c` (`extern` in `data.h`), following
  the AQI/UV pattern; `-1` is the no-data sentinel, rendered as `--`.
- `get_source_label()` → `"HUM"`; `get_source_data()` → `"65%"`, with
  `*percent = s_weather_humidity` — but only when data is present; the sentinel
  path leaves `*percent = 0` (the function's default), never a negative
  progress value.
- Plain single-color text source → renders through the slot's `TextLayer`; no
  `canvas_drawer()` entry.

## Colors (`get_source_color()` in `theme.c`)

| Reading | Color |
|---------|-------|
| `-1` (no data) | `text_primary` |
| `<30` (dry) | `accent_cold` — blue, rhyming with cold temps |
| `30–60` (comfort) | `text_primary` — revised after emulator review: comfort green read as noise; off-norm bands unchanged |
| `61–70` (sticky) | `status_yellow` |
| `>70` (oppressive / mold-risk) | `status_red` |

Boundaries are inclusive as shown: 29 blue, 30–60 neutral, 61–70 yellow,
71+ red. Bands follow the common 30–60% indoor-comfort consensus. Humidity is
unitless, so `SETTINGS_UNITS` does not affect formatting or colors.

## Phone (`src/pkjs/index.js`)

- The legacy `current_weather=true` block is replaced by the modern `current`
  block: `current=temperature_2m,weather_code,relative_humidity_2m`. Verified
  live (2026-07-31): the two are mutually exclusive — `current_weather=true`
  silently suppresses `current=` — so this is a migration, not an addition.
  Same XHR, no new request, no extra radio wake; `temperature_unit` and
  `hourly=uv_index&forecast_hours=12` both confirmed to keep working.
- Parser reads `json.current.temperature_2m` / `weather_code` /
  `relative_humidity_2m` (replacing `json.current_weather.temperature` /
  `weathercode`); humidity → `Math.round`, missing block or field → `-1` (the
  watch-side sentinel). Temp/cond semantics are unchanged — same values, new
  field names. Everything still rides on the existing `forecast` join object →
  parallel-fetch/retry semantics untouched.
- Reply dict gains `WEATHER_HUMIDITY`. The `localStorage` weather-cache replays
  the whole payload dict, so cached replay covers humidity with no changes.

## Watch plumbing

- `package.json` `messageKeys`: append `WEATHER_HUMIDITY` (append-only;
  ordering is safe because persist keys are hand-assigned).
- `messaging.c`: tuple-guarded inbox parse into `s_weather_humidity` (older JS
  payloads without the key leave the sentinel in place). New
  `PERSIST_KEY_WEATHER_HUMIDITY 1005`; saved in `save_weather_cache()` via
  `persist_write_int_if_changed` and restored in `load_weather_cache()` — it
  rides the existing timestamp/freshness logic, which stays conditioned on the
  temp+cond tuples.
- `main.c`: add `DATA_SOURCE_HUMIDITY` to the `any_slot_needs_weather()` list
  so launch-time stale-cache fetch, the :00/:30 refresh, and the
  assign-a-weather-slot-triggers-fetch behavior all cover humidity slots.

## Config page (`src/pkjs/config.json`)

New option `{ "label": "Humidity", "value": "26" }` in SLOT_1–5 (max 4 chars,
`100%`, fits the 63px bottom slots). Not offered in SLOT_6 — the centre row
stays curated to date and the two progress bars.

All option lists reordered into domain groups; only array order changes —
`value` strings and `defaultValue`s are untouched, so persisted settings and
enum identifiers are unaffected.

**Top slots (SLOT_1, SLOT_2), in order:**

Empty (20) · Battery (0) · Bluetooth Status (9) · Short Date (no year) (22) ·
.beat (Swatch Internet Time) (21) · Steps (1) · Sleep (2) · Heart Rate (6) ·
Active Minutes (10) · Weather (5) · Humidity (26) · Air Quality (AQI) (16) ·
UV Index (next 12h max) (17) · AQI / UV Index (next 12h max) (18)

**Bottom slots (SLOT_3–5)** — same sequence minus the wide-only entries
(Weather, AQI/UV, Short Date):

Empty (20) · Battery (0) · Bluetooth Status (9) · .beat (21) · Steps (1) ·
Sleep (2) · Heart Rate (6) · Active Minutes (10) · Humidity (26) ·
Air Quality (AQI) (16) · UV Index (next 12h max) (17)

**SLOT_6:** unchanged (Date, Steps Progress, Battery Progress).

## Tests

In `test/test_watchface.c`, registered in `main()`:

- Formatter: `65` → `"65%"` with `*percent == 65`; `-1` → `"--"`.
- Colors at every boundary: `-1` → `text_primary`; `29` → `accent_cold`;
  `30` and `60` → `status_green`; `61` and `70` → `status_yellow`; `71` →
  `status_red`.
- Weather-cache round trip: save + load restores humidity alongside temp/cond/
  AQI/UV; inbox parse of a humidity tuple updates the global.
- Mock `test/pebble.h` gains `#define MESSAGE_KEY_WEATHER_HUMIDITY 121`
  (mock keys are hand-assigned).

## Docs

`docs/ARCHITECTURE.md` data-flow step 2 names what the JS fetches
("weather, UV, AQI") — extend to include humidity.

## Non-goals

No combined weather+humidity source, no dew point, no humidity progress-bar
source, no SLOT_6 offering, no settings toggle.

## Verification

- `make test` — format check + full unit suite green.
- `pebble build` for all targets — guards mock/SDK signature drift.
- One live `curl` against Open-Meteo confirming the `current=` block.
- No screenshot regen: default slot layout is unchanged.
