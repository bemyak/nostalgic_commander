# Changelog

All notable changes to Nostalgic Commander. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versioning is
[Semantic](https://semver.org/).

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

[1.2.0]: https://github.com/bemyak/nostalgic_commander/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/bemyak/nostalgic_commander/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/bemyak/nostalgic_commander/compare/c205236a...v1.0.0
