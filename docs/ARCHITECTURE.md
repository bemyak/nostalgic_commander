# Architecture Overview

**Nostalgic Commander** is a Pebble watchface (SDK 3, currently targeting the `emery`
platform / Pebble Time 2, 200x228 px) with a text-forward design: the time and
date sit inside "ASCII window" frames, surrounded by configurable
complication slots.

This document explains how the pieces fit together.

## High-level data flow

```
                 Phone (PebbleKit JS)                      Watch (C)
  ┌────────────────────────────────────────┐   ┌─────────────────────────────┐
  │ src/pkjs/index.js                      │   │ src/c/                      │
  │  - Clay settings page (config.json)    │   │  messaging.c  ← AppMessage  │
  │  - Geolocation → Open-Meteo APIs       │──▶│  data.c       (cache+format)│
  │    (weather, UV, AQI)                  │   │  theme.c      (colors)      │
  └────────────────────────────────────────┘   │  drawing.c    (rendering)   │
                                               │  main.c       (lifecycle)   │
                                               └─────────────────────────────┘
```

1. The watch requests weather by sending an AppMessage containing
   `WEATHER_TEMP` (`request_weather()` in `messaging.c`) — on launch when the
   persisted cache is stale (`init()` in `main.c`) and at every :00/:30
   minute tick (`update_time()` in `main.c`).
2. The JS side (`index.js`) receives any AppMessage, gets the phone's location,
   and calls two Open-Meteo endpoints: the forecast API (current temperature,
   weather code, and hourly UV — reduced to the peak over the next 12 hours)
   and the air-quality API (US AQI). It maps the
   WMO weather code to a short condition string (`SUN`, `CLD`, `FOG`, `RAIN`,
   `SNOW`, `TSTM`) and sends everything back in one dictionary.
3. `inbox_received_callback()` (`messaging.c`) writes the values into the
   global cache in `data.c`, persists any settings keys, and triggers a redraw.
4. Settings changes made on the Clay config page travel the same AppMessage
   path; the watch persists them with `persist_write_int` and re-reads them on
   the next launch (`init()` in `main.c`).

## C modules (`src/c/`)

| File | Role |
|------|------|
| `main.c` | App lifecycle: window setup, service subscriptions (tick, battery, bluetooth, health), settings load from persistent storage, text layer creation. |
| `data.c` / `data.h` | Single source of truth for state: sensor/weather caches, settings, the `ComplicationDataSource` enum, slot definitions, and the formatting functions. |
| `theme.c` / `theme.h` | `WatchTheme` color palettes, theme selection from `SETTINGS_THEME` and the hour (Auto mode), and per-source color logic (battery level, temperature bands, AQI/UV thresholds). |
| `drawing.c` / `drawing.h` | All custom rendering: the ASCII window frames, the split-color AQI/UV complication, and `refresh_complications()` which pushes formatted text into the slot text layers. |
| `messaging.c` / `messaging.h` | AppMessage in/out: weather requests, inbox parsing, settings persistence. |
| `main.h` | Exposes `update_time()` so messaging can trigger a full refresh. |

### Conventions

- State lives in globals (`s_`-prefixed) declared in `data.h` and defined in
  `data.c`; other modules reference them via `extern`. There is no
  encapsulation layer — this is idiomatic for Pebble's C SDK.
- "No data" sentinels: `-1` for steps/sleep/AQI/UV, `-999` for temperature,
  `0` for heart rate. Formatters render these as `--`.
- Everything that changes state calls `refresh_complications()` and marks the
  canvas layer dirty rather than redrawing directly.

## The complication system

A complication is identified by a `ComplicationDataSource` enum value
(`data.h`). Three functions define each source's behavior:

- `get_source_label()` (`data.c`) — the short title drawn in the window frame
  gap (e.g. `STEP`, `BPM`, `AQI/UV`).
- `get_source_data()` (`data.c`) — formats the value string and optionally the
  progress toward a goal, as a percentage. Steps are *not* clamped to 100:
  beating a goal is worth seeing, so consumers clamp for their own needs — a
  progress bar fills only to its end while the reading beside it keeps
  counting, up to the three digits the value field holds.
- `get_source_color()` (`theme.c`) — the value's color on color displays
  (e.g. battery green/yellow/red, AQI bands).

There are two placement types:

1. **Slots** — six configurable boxes in `s_complication_slots[]`
   (`data.c`): two wide ones on top (93px), three narrow on the bottom
   (63/62/63px), and the full-width centre row (184px). Neighbours in a row
   overlap by 2 columns so their 2px borders merge into a single shared
   divider. The centre row is index 5 so `SLOT_1`–`SLOT_5` keep their persisted
   positions; it offers the date plus `DATA_SOURCE_STEPS_BAR` and
   `DATA_SOURCE_BATTERY_BAR`, which need the width for their progress bars.
   Each slot has a fixed `box_rect`, a `TextLayer`, and a user-chosen source.
   The slot's frame and label are drawn on the canvas
   (`canvas_update_proc`); the value text lives in the slot's text layer,
   updated by `refresh_complications()`. Source `DATA_SOURCE_EMPTY` (20) hides
   a slot entirely.
   - Most sources are canvas-drawn instead of using that text layer — anything
     needing more than one color inside a value (the accent marks, the split
     AQI/UV, a status band) or a non-text rendering (the progress bars).
     `canvas_drawer()` in `drawing.c` is the single list of which sources those
     are, returning a draw function or `NULL`; `refresh_complications()`
     consults the same function to decide whether to hide the text layer, so
     the two can't disagree about a source.
   - `DATA_SOURCE_SHORT_DATE` (22) is offered in the top slots only: it needs
     9 characters and the bottom slots hold 7.
2. **Fixed element** — the time, drawn by the one remaining `TextLayer` in the
   64pt VGA font inside the TIME window. Everything else on the face is a slot.

### Adding a new complication source

1. Add an enum value in `data.h` (before `DATA_SOURCE_EMPTY`, keeping numeric
   values stable — they are persisted and used in `config.json`).
2. Add cases to `get_source_label()` and `get_source_data()` in `data.c`
   (and `get_source_color()` in `theme.c` if it needs color logic).
3. Add the option (label + numeric value as a string) to the relevant `SLOT_*`
   selects in `src/pkjs/config.json`. Top slots are wider — wide formats like
   combined Weather belong only there.
4. If the data comes from the phone, add a message key in `package.json`,
   populate it in `index.js`, and parse it in `inbox_received_callback()`.
5. Add unit tests in `test/test_watchface.c` (formatting, colors, edge cases —
   see AGENTS.md's exhaustive-testing directive).

## Theming

Three `WatchTheme` palettes (`theme.c`), all DOS/EGA:

| Theme | Ground | Look |
|-------|--------|------|
| `s_theme_dialog` | `#AAAAAA` light grey | Turbo Vision/NC dialog box, attribute `0x70` (black on light grey); blue frames, dark-grey titles, black values |
| `s_theme_panel` | `#0000AA` EGA blue | Norton Commander panel, cyan frames, white entries |
| `s_theme_shadow` | `#000000` black | the same panel in shadow — Turbo Vision faked a dimmed panel as grey-on-black, so dark-grey frames, light-grey titles, white entries |

These are exact EGA 16-color values — Pebble's 64-color display uses the
same `0x00/0x55/0xAA/0xFF` channel steps DOS did, so no approximation is
needed.

Auto (`SETTINGS_THEME` 0, the default) cycles these three on an 8-hour
schedule, brightest to darkest as the day goes on: `s_theme_dialog`
06:00–14:00, `s_theme_panel` 14:00–22:00, `s_theme_shadow` 22:00–06:00.
`determine_theme(int theme_setting, int current_hour)` maps setting +
hour to a `WatchTheme` pointer: Auto (0) checks `current_hour` against
that schedule; 1–3 pick `s_theme_dialog`/`s_theme_panel`/`s_theme_shadow`
directly. Unrecognized settings fall back to Auto. `apply_theme()` reads
the clock and re-runs this; since `update_time()` calls `apply_theme()`
every minute, Auto's theme is re-evaluated every minute. All drawing code
reads colors from `s_active_theme`, never hardcoded colors.

`WatchTheme` carries ten colors. `frame` is the ASCII window border stroke,
kept separate from `text_primary` so the panel themes can draw cyan frames
around white text. `mark` is the accent highlight (unit letter, date
weekday, `.beat` `@`) — Turbo Vision's `0x7E` hotkey color, yellow in all
three themes, deliberately separate from `status_yellow` since the dialog
theme's `status_yellow` is brown. `status_ink` is the text color drawn over
a status-colored fill (the battery chip): black on the two dark themes,
white on the light dialog theme.

## Rendering

`canvas_update_proc()` (`drawing.c`) draws, in order: the center background,
the TIME and DATE windows, then each non-empty slot's window.
The "ASCII window" look (`draw_ascii_window`) is a solid `WINDOW_BORDER_PX`
stroke with a gap in the top border where the title sits. The stroke is filled
rects rather than lines or box-drawing glyphs: `graphics_draw_line` is
anti-aliased with round caps, which softens the corners, and character-drawn
rules render thin and uneven at 8x16. The top line drops `TITLE_BORDER_DROP` so
it runs through the middle of the title rather than along its top edge.

Layout constants are hardcoded for emery's 200x228 screen (slot rects in
`data.c`, time/date rects in `main.c` and `drawing.c`). Supporting other
platforms would require deriving these from `layer_get_bounds()`.

## Settings

| Key | Values | Where used |
|-----|--------|-----------|
| `SETTINGS_THEME` | 0 Auto, 1 Dialog, 2 Commander Panel, 3 Shadowed Panel | `determine_theme()` |
| `SETTINGS_UNITS` | 0 Imperial, 1 Metric | Temp formatting/colors; JS picks the API unit, and a unit change triggers an immediate re-fetch |
| `SETTINGS_DATE_FORMAT` | 0 ISO (`1970-12-31`), 1 DOS (`31-12-1970`), 2 full text (`DEC 31st, 1970`), 3 short (per `SETTINGS_SHORT_DATE_FORMAT`) | `format_date_string()` |
| `SETTINGS_SHORT_DATE_FORMAT` | 0 Month-Day (`12-31`), 1 Day-Month (`31-12`) | `DATA_SOURCE_SHORT_DATE` slot rendering; also the short choice (3) of `SETTINGS_DATE_FORMAT` |
| `SETTINGS_DOW_POSITION` | 0 before (`THU 1970-12-31`), 1 after (`1970-12-31 THU`), 2 hidden (`1970-12-31`) | `date_dow_offset()`; applied to every rendered date, not just the DATE window |
| `SLOT_1`–`SLOT_5` | `ComplicationDataSource` value | Slot sources (1=top-left, 2=top-right, 3=bottom-left, 4=bottom-center, 5=bottom-right) |

Clay sends values as strings; `tuple_get_int()` (`data.c`) normalizes string
and integer tuples. On receipt, `inbox_received_callback()` (`messaging.c`)
persists each setting under a dedicated `PERSIST_KEY_*` constant
(`messaging.h`), and `load_settings()` restores them on launch. These persist
keys are hand-assigned and independent of `messageKeys` ordering.

## Testing

Unit tests live in `test/` and run on the host (no emulator needed):

```sh
cd test && make test
```

- `test_watchface.c` `#include`s the C source files directly (so static
  functions are testable) with `TEST_ENV` defined, which compiles out the real
  `main()`.
- `test/pebble.h` + `pebble_mock.c` are a minimal hand-written mock of the
  Pebble SDK — enough types and no-op functions to link the app code.
- The framework is [Unity](https://github.com/ThrowTheSwitch/Unity), vendored
  as a git submodule at `test/unity`.
- Coverage focuses on the pure logic: formatters in `data.c`, theme selection,
  and color thresholds. UI code (layers, graphics calls) is linked against
  no-op mocks, not asserted on.

When adding a test, write the `test_*` function and register it with
`RUN_TEST(...)` in `main()` at the bottom of `test_watchface.c`.

## Building & installing

```sh
pebble build                    # uses wscript (standard SDK build)
pebble install --emulator emery
```

`pebble-env/` is a local Python virtualenv for the Pebble tool; `build/` is
generated output. Neither is hand-edited.

## Miscellaneous

- `TODOs.md` tracks approved-but-unbuilt work, `IDEAS.md` holds undecided
  brainstorm material, and `ISSUES.md` tracks known bugs.
- `AGENTS.md` holds the project values, conventions, and hard rules for
  contributors and AI agents.
