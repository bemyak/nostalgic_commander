# Extended Weather Complications — Design

2026-08-01 · Status: approved (user picked items 1–3 from the ideation list,
top slots only)

## Summary

Add three weather complications, all sourced from fields added to the single
existing Open-Meteo forecast request (one `hourly=` entry, one `daily=` list) —
no new endpoints, no second radio wake. Offered in the top slots only.

## Sources

| Enum (new, appended) | Label | Value | Sentinel |
|----------------------|-------|-------|----------|
| `DATA_SOURCE_WEATHER_PCP = 28` | `PCP` | `45%`, `%` like battery/humidity; `*percent` passthrough | `-1` → `--`, percent 0 |
| `DATA_SOURCE_SUN_TIMES = 29` | `RISE/SET` | `05:00/21:52` (11 cells = 88px, fits the 89px top-slot interior) | `--:--` per side |
| `DATA_SOURCE_TEMP_HIGH_LOW = 30` | `HI/LO` | `82/61F` / `+28/+4C` (metric always signs) | `-999` → `-- / --` |

All three render through the plain slot text layer — no canvas drawers.
Labels fit the top-slot title budget (≤9 chars at 93px).

**PCP semantics**: max precipitation probability over the next 12h — the same
windowing philosophy as UV ("will I need an umbrella", not "is it raining").
Bands: `-1` primary · `≤30` neutral (revised after emulator review: a dry
timeline is unremarkable) · `31–60` yellow · `≥61` red.

**SUN_TIMES**: today's sunrise/sunset in local 24h time, from the API
(`timezone=auto` already). Fixed order rise/set, no severity color (always
`text_primary`).

**TEMP_HIGH_LOW**: day's max/min, colored by the **high** via the shared
temperature-band logic. Extracts `temp_band_color()` in `theme.c` from the
WEATHER_TEMP/WEATHER case so all three call sites share one authority; the
existing temp behavior (including the sentinel falling into the cold band)
is preserved bug-for-bug — the guard lives in the new case only.

## Phone (`src/pkjs/index.js`)

- URL gains `precipitation_probability` in the `hourly=` list and
  `daily=sunrise,sunset,temperature_2m_max,temperature_2m_min&forecast_days=1`.
- One hourly-window loop computes both UV and PCP maxima (typeof-number guard;
  probability is `null` in some regions/times). Missing → `-1`.
- `daily.sunrise[0]`/`sunset[0]` are local ISO strings — `substr(11, 5)` gives
  `HH:MM`; missing → `--:--`. High/low: `Math.round`, missing → `-999`.
- All five ride the existing `forecast` join object; retry/join semantics
  unchanged. Dict gains `WEATHER_PCP`, `WEATHER_SUNRISE`, `WEATHER_SUNSET`,
  `WEATHER_HIGH`, `WEATHER_LOW` (append-only in `package.json`).

## Watch

- State: `s_weather_pcp` (−1), `s_sunrise_time`/`s_sunset_time` (`char[8]`,
  `--:--`), `s_temp_high`/`s_temp_low` (−999). All follow existing
  declaration/init conventions.
- Persist keys (hand-assigned cluster): `1006` PCP, `1007` SUNRISE,
  `1008` SUNSET, `1009` HIGH, and — since `1010`–`1020` are the settings
  block — `1021` LOW, with a comment recording the jump. Never renumber.
- `messaging.c`: five tuple-guarded parses; save/load extended in the weather
  cache (compare-before-write as usual); old on-disk caches leave sentinels.
- `main.c`: all three sources join `any_slot_needs_weather()`.
- Mock: `MESSAGE_KEY_WEATHER_PCP 122`; `WEATHER_SUNRISE/SUNSET/HIGH/LOW`
  (105–108) already exist in the mock header.

## Config page

Top-slot (SLOT_1/2) weather group becomes, in order: Weather (5) ·
High / Low Temperature (30) · Precipitation (next 12h max) (28) ·
Humidity (26) · Sunrise & Sunset (29) · Air Quality (AQI) (16) ·
UV Index (17) · AQI / UV Index (18). Bottom slots and SLOT_6 untouched.

## Tests (registered in main())

- PCP: `--`/`45%`/`0%`/percent passthrough; color pins at `-1, 30, 31, 60, 61`.
  Later addition: in metric mode while the current condition precipitates
  (RAIN/SNOW/TSTM) the slot shows the live rate from `current.precipitation`
  (tenths of mm on the wire, `WEATHER_PRECIP_NOW`) — `<1mm`, `25mm`, `99mm`
  clamp — with WMO intensity bands (neutral <4mm/h, yellow 4–7, red ≥8);
  imperial always stays probability.
- SUN_TIMES: `--:--/--:--`, real join, total width ≤ 11 cells.
- TEMP_HIGH_LOW: `-- / --` when either side is the sentinel; `82/61F`,
  `28/4C`, width ≤ 11; color follows the high (cold high → `accent_cold`,
  hot high → `status_red`, mild → primary, sentinel → primary).
- Labels stay within the 9-char top-slot title budget.
- Weather-cache round trip and inbox payload tests extended to all five keys.
- Existing temp-color tests keep passing (proves the helper extraction is
  behavior-preserving).

## Housekeeping

- IDEAS.md: remove the graduated Sunrise/Sunset and High/Low entries.
- TODOs.md Done: add this batch (names the PCP newcomer PCP — probability of
  precipitation).
- ARCHITECTURE.md data-flow: the forecast fetch description gains the daily
  block and the PCP hourly field.

## Non-goals

No narrow-slot variants, no PCP in the full-weather centre strip, no
precipitation *amount*, no wind, no apparent temperature, no per-day
suntimes beyond today.

## Verification

`make test` green · `pebble build` green · reviewer pass on the diff.
Live API shape for all new fields was verified with `curl` before writing the
parser (2026-08-01).
