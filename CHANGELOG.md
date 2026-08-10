# Changelog

All notable changes to Nostalgic Commander. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versioning is
[Semantic](https://semver.org/).

## [Unreleased]

## [1.5.0] - 2026-08-13

### Added

- New theme: Navigator. Dark grey background, white frames, yellow hotkey
  letters — the DOS Navigator screen.
- Menu icon for the watchapp list. App icon is also redrawn.
- Temperature reading is now available in bottom slots.
- Slot values draw the trailing unit (`24C`, `12 m/s`, `7h 30m`) in the
  accent color, same for the weather condition word. A value on a status
  band is drawn fully in ink, without accent.

### Changed

- Themes have new names: Dialog → Turbo Vision, Commander Panel → Norton,
  Shadowed Panel → Dark. A saved theme choice keeps working.
- Turbo Vision palette fixes: frames are dark grey (were blue), hotkey
  letter is dark red (was tan — hard to read on light grey at this size).
- Weather wire contract: the condition travels as the raw WMO code and the
  watch maps it to the display word. Unknown code shows `--` instead of an
  invented word.

### Fixed

- Active Minutes showed `0m` when there is no data. Now it shows `--`, like
  other short readings.
- Missing temperature was tinted cold-blue. Now `--` is neutral.
- Phone-side parsing: garbage AQI reported clean air, missing temperature
  showed `NaN`, malformed reply kept stale values on screen. All of these
  now show `--`. WMO fog codes 46/47 show `FOG`.

### Removed

- Auto theme switching. The theme is selected directly. A watch with Auto
  saved falls back to Norton.

## [1.4.1] - 2026-08-06

### Fixed

- **Combined BT/QT window in a top slot never painted its QT half** — the
  drawer read the atomic Bluetooth source after the source renumbering, so
  `[z]` never rendered.
- **Slot settings showed `[object Object]` where the wind option should be**:
  a botched options edit had nested the option object into its own label in
  config.json.
- **HI/LO rolled to tomorrow one hour early**: the roll fired at the start
  of an extreme's own hour, so a "now" reading inside that hour could
  disagree with the cell. Roll now happens when the hour ends.

## [1.4.0] - 2026-08-06

Quiet time, wind, and a caption grammar for two-field windows.

### Added

- **Quiet Time indicator.** A combined `BT/QT` window for top slots shows
  both phone states as Turbo Vision checkboxes (`[x] [z]`); bottom slots get
  a standalone `QT` window. State comes from `quiet_time_is_active()`.
- **Wind complication.** Direction as an 8-way arrow — including four custom
  diagonal glyphs patched into the bundled VGA font — plus sustained speed
  in your settings unit. Top slots show the unit (`↗ 12 m/s`), bottom slots
  save the space (`↗ 12`). The window bands yellow at strong breeze, red at
  gale (Beaufort rungs).
- **Combined Humidity + Precipitation** source for top slots (`HUM`/`PCP`
  stubs over two tight fields).
- **Heart glyph on the BPM reading** — `120♥`, drawn in the theme's accent
  color.

### Changed

- **Two-field windows drop the slashes.** HI/LO, AQI/UV, HUM/PCP and BT/QT
  now split their title into per-half frame stubs registered over the
  values. HI/LO's value spells each number with its unit letter:
  `+24C +18C`.
- **Settings slimmer on top slots**: individual AQI, UV, Humidity and
  Precipitation are gone there — the combined readouts are the option.
- **Centre weather strip**: the temperature chip always lettered now
  (`+22C`), chip widened to fit it.
- Rename some settings to improve readability.

## [1.3.1] - 2026-08-05

### Changed

- **Disconnect-buzz setting reads plainly** — `On`/`Off` options, with the
  battery-optimization warning moved to an explanatory note below the select.
- HI/LO swap logic simplified internally; same behavior, fewer moving parts.

## [1.3.0] - 2026-08-04

### Added

- **"Enable vibration on phone disconnect"** setting — a consent select that can
  silence the disconnect buzz (it doubles as the dead-phone detector; the default
  keeps it on, and "No" spells out what you'll lose).

## [1.2.0] - 2026-08-03

Color policy pass: the face only speaks up when something needs attention.

### Changed

- **High/low slot is now "next extremes"**: each cell rolls to the next day's
  value one hour after the day's own extreme passes, always ordered
  chronologically; the caption flips `HI/LO`/`LO/HI` to match the layout.
- **Battery shares one ladder across chip and central bar**: quiet above 39%,
  yellow 20–39%, red ≤19%; battery green is gone when discharging.
- **Charging shows green** at any level, on both the chip band and the
  progress bar.
- **Humidity is a plain readout** — the comfort-band coloring is removed.
- **PCP probability bands raised**: silent ≤50%, yellow 51–70%, red ≥71%.
- **Clean air reads neutral**: AQI/UV no longer carry a permanent green fill;
  they band yellow/red only when elevated.
- **Standalone PCP slot bands like the centre-strip chip** in attention states
  (probability >50%, or ≥4 mm/h in live-rate mode); calm live-rate still
  keeps its `mm` accent.
- **Default theme is Commander Panel (EGA blue)** instead of Auto; Auto
  remains an option for fresh installs only — existing settings are untouched.

## [1.1.0] - 2026-08-02

Complication expansion cycle.

### Added

- **Humidity complication** (HUM), with a comfort band: 30–60% reads neutral,
  off-norm air earns a fill.
- **Full-weather centre strip**: one row of captioned chips — condition,
  temperature, humidity, and precipitation probability.
- **New top/edge slot sources**: precipitation probability (PCP) and the day's
  high/low temperatures.
- **Live precipitation rate** (metric only): while it's raining, PCP shows the
  measured mm/h of the past hour instead of probability, banded by WMO
  intensity (≥4 mm/h yellow, ≥8 mm/h red).

### Changed

- Centre strip: high/low chips in place of AQI/UV, then PCP in place of
  high/low (the day's extremes live in the slot complications); chips fill
  only when their reading carries a status color; caption row replaces the top
  border; pixel-centred per-chip captions; 3-cell temperature chip (`CUR`).
- Temperatures: signed Celsius, single-space weather combo, unitless metric
  strip temps.
- Dry days read neutral — no PCP color ≤30%.
- Logical config ordering for the complications page.

### Removed

- **SUN_TIMES / rise-set complication** — the value crowded the top slot.
  (Added mid-cycle, dropped before this release; never shipped.)

## [1.0.0] - 2026-07-30

First release as **Nostalgic Commander** (forked from tuiface; new app name,
UUID and identity — no upgrade path).

### Added

- **DOS-style themes** (Norton Commander aesthetic): Commander Panel,
  Shadowed Panel, and Dialog, with DOS fonts and an Auto cycle — panel by
  day, terminal at night.
- **.beat (Swatch Internet Time) complication.**
- **More complications**: short date, full date, steps progress bar, battery
  progress bar.
- **Timeline Quick View** handled via honest occlusion.
- Settings page, README and docs re-voiced to the Commander look; refreshed
  screenshots.

### Performance

A full battery pass — the face:

- asks for weather only when a slot displays it, edge-triggered instead of a
  fixed fetch loop, once per launch;
- renders only when displayed state actually changed, reads the clock once
  per tick, and formats dates on change;
- skips health metrics no slot displays and throttles motion-driven refreshes;
- fetches AQI and forecast in parallel, shrinks AppMessage buffers, reuses
  cached locations, and persists only on change;
- draws cheaper: bar fills as rects not glyphs, runs with Fill overflow, no
  duplicate full-screen fill.

### Removed

- Old tuiface store thumbnail — `screenshots/` covers store imagery.

[Unreleased]: https://github.com/bemyak/nostalgic_commander/compare/v1.5.0...HEAD
[1.5.0]: https://github.com/bemyak/nostalgic_commander/compare/v1.4.1...v1.5.0
[1.4.1]: https://github.com/bemyak/nostalgic_commander/compare/v1.4.0...v1.4.1
[1.4.0]: https://github.com/bemyak/nostalgic_commander/compare/v1.3.1...v1.4.0
[1.3.1]: https://github.com/bemyak/nostalgic_commander/compare/v1.3.0...v1.3.1
[1.3.0]: https://github.com/bemyak/nostalgic_commander/compare/v1.2.0...v1.3.0
[1.2.0]: https://github.com/bemyak/nostalgic_commander/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/bemyak/nostalgic_commander/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/bemyak/nostalgic_commander/compare/c205236a...v1.0.0
