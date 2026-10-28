# Changelog

All notable changes to Nostalgic Commander. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versioning is
[Semantic](https://semver.org/).

## [Unreleased]

### Changed

- Degauss runs at 25 frames a second, the most the panel can show, every
  frame on time; it was 11. The face is drawn once per minute and the
  effects are baked onto that drawing, so a frame costs a copy and the
  strike's wobble.
- The vignette is a darkening of the glass applied after the curvature, so
  the rim bends with the tube; on light themes captions in the rim band are
  no longer held a shade clear of their field.
- Frame strokes keep their 2 px width under the curvature; the warp thinned
  them to 1 px for a few rows at each column boundary.
- A wrist shake degausses at once; any other wake waits 200 ms and strikes
  only if the face is still showing, so a button press that opens the menu
  no longer degausses behind it.

### Fixed

- Weather is fetched with the units and forecast window the watch holds: a
  Bluetooth drop while saving settings no longer leaves it in the old units
  until the next settings change.
- A failed air-quality fetch no longer blanks the last AQI reading, and an AQI
  above 500 shows as 500 instead of `--`.
- Launching with weather last fetched 30 minutes to 2 hours ago shows it while
  the fresh fetch is on its way, instead of `--`.
- Changing the forecast window blanks only the UV and rain-chance readings
  while the new maxima load; it used to blank all the weather.
- Step and sleep readouts keep updating after the watch clock is set back.
- The HUM/PCP window's fields are four cells wide, so a `100%` or `12mm`
  reading no longer spills past its red band.
- The weather strip's PCP chip bands on the live rain rate when the rain
  chance is missing, the way the lone PCP chip does.
- A rain rate of 3.9 mm is no longer rounded down below the 4 mm band.

## [1.13.0] - 2026-10-26

### Added

- Night theme: a second theme shown from sunset to sunrise at the weather
  location.

### Changed

- The settings page takes the colours of the selected theme.
- Degauss colour stripes show on grey and white: on the Turbo Vision,
  Console and Navigator themes the strike was a bare warp. Black stays black.

### Fixed

- Dismissing Quick View with Back no longer degausses.
- A notification or reminder that closed by itself no longer swallows the
  degauss on the next wake.
- Dragging a volume slider past its left end turns it off; it stopped at 10%.

## [1.12.0] - 2026-10-25

### Changed

- The CRT effect is split into chromatic aberration, screen curvature and
  degauss on wake, and the sound settings moved to a new Effects panel with a
  volume for the degauss and the hourly chime.
- The settings page is a Norton Commander window.
- Degauss animation redone from footage of a real tube: the raster bows in
  at the edges in curves that change every frame, black beyond the glass,
  with purity stripes that thin out as the field dies.
- Degauss sound redone: a thud, then the coil's mains buzz, tuned on the
  wrist against recordings.

### Fixed

- The degauss volume slider has an audible effect. The old sound sat below
  what the speaker can play.

## [1.11.0]

### Added

- Solar hour angle and elevation complications for the bottom slots.
- Combined solar hour angle and elevation for the top slots.

## [1.10.0]

### Added

- Amber theme.

### Changed

- Longer battery life.
- Crisp text in the corner slots with the CRT effect on.
- Navigator matches DOS Navigator: yellow titles, cyan marks.
- Weather older than 2 hours shows `--`.
- Changing units or forecast window refetches weather.
- Cloudy reads `CLDY`.
- Norton is first in the theme list.
- Clearer AQI/UV and humidity/precipitation labels.
- Fewer phone requests and location fixes.
- UV and precipitation follow the local time at your location.
- Coordinates sent to Open-Meteo are rounded.

### Fixed

- Yesterday's high/low no longer shows after midnight.
- A failed weather reply no longer blanks good data.
- Freezing rain reads `RAIN`.
- Degauss sound: no click at the end, first frame shown, no strike when
  returning from a menu.
- A wake after dismissing a notification strikes again.
- No double hourly chime when clocks fall back.
- UV above 11 shows its value.
- Bad values from the phone show `--`.

## [1.9.0] - 2026-08-30

### Added

- Hourly chime: a POST beep on the hour, off by default, with a volume
  select and a Test sound button on the settings page.

### Changed

- The CRT falloff dithers its three channels a third of a cycle apart. Run in
  lockstep on a four-level panel, a grey field can only be black, DarkGray,
  LightGray or white, so the rim stepped in whole levels however wide the band
  was — no amount of tuning adds a fifth grey. Splitting the phase carries the
  same falloff in thirds of a level, at the cost of a faint colour cast at the
  very edge of the glass.
- Light themes hold ink one step clear of the level their own field renders.
  A caption and its ground could quantize onto the same level partway through
  the falloff, and thin strokes dissolved into it.
- The CRT rims are sized per axis, and the top and bottom now carry the same
  solid black edge the sides get for free from the curvature clamp. The top
  had 2px against the sides' 6 and read as missing.
- CRT chromatic aberration fades in across a band instead of switching on at a
  hard oval. That oval crossed the top and bottom complication rows, so a
  1 1/3px channel shift appeared between one pixel and the next, mid-word. The
  fade starts two thirds of a pixel in rather than from nothing: the pass adds
  a per-half third to cancel the panel's element offset, and starting from zero
  cancelled to exactly no shift partway down the 64px clock digits, where the
  colour fringe stopped mid-character.
- The per-theme vignette dot floor is gone. It existed to stop the falloff
  scattering pure black over a lit field; out-of-phase dithering does that
  better, and the floor's own clamp flattened the ramp it was meant to protect.

### Fixed

- Status fills changed hue as the vignette darkened them, because the red and
  green of a fill cross their quantization steps at different pixels. Dialog's
  low-battery band is brown — the CGA palette has no dark yellow — and rendered
  as that theme's own alarm red; Panel's precipitation chip is SunsetOrange and
  shed its green near the rim to render flat red.
- The top and bottom rim drew a window's two border rows at different
  brightness, so the edge looked uneven and glyphs crossing it looked like they
  wobbled. The row-to-depth map was fitting 16 steps into 14 rows and skipping
  two of them.

## [1.8.0] - 2026-08-23

### Added

- The CRT aberration separates channels vertically as well as horizontally,
  one pixel at most, mirrored about the horizontal centre line.

### Changed

- CRT curvature is radial: all four edges bow. It was a vertical gain only,
  so the top and bottom rows pulled inward up to 5px while the sides stayed
  straight. Side content warps too now, and the outermost columns at
  mid-height clip to black, where the vignette is already dark.
- CRT chromatic aberration samples in thirds of a pixel, two weighted taps
  per channel. It was whole-pixel in three steps of 0/1/2px, so the fringe
  switched on at a visible ring; it now grows continuously, and the centre
  of the screen converges instead of carrying a 2/3px floor. Maximum channel
  separation is 5.3px (was 4px), and the widest rung starts past the slot
  text, so it lands on the frame strokes rather than on glyphs.
- CRT vignette fades over 16px (was 20px), and the low half of the falloff
  is lifted. At four grey levels the old curve started as a step.
- Degauss sound is synthesized PCM: a 110ms thunk at 190Hz with partials
  over a decaying envelope. It was a falling glissando played from a note
  table, gliding 90Hz down to 55Hz, which put all of its energy below 300Hz
  where the watch speaker produces nothing. 110ms also fits the firmware's
  128ms audio ring, so a rendered frame cannot interrupt playback.
- Weather readings are clamped to the range the layout budgets before they
  are drawn. A garbage value off the wire could format wider than its slot
  and paint over the clock.

### Fixed

- A second backlight-on during the degauss strike started a second timer
  chain, so the strike ran at double speed and then restarted. The pending
  tick is cancelled now.
- The 5-second weather retry stopped working for the rest of the session
  after two send failures in a row. The counter also clears when a payload
  arrives.
- The phone dropped a weather reply when the send itself failed, for example
  on a momentary Bluetooth drop, and the watch stayed stale until its next
  half-hour tick. The send is retried twice.
- Two weather fetches could run at once, each with its own location fix and
  requests, and the slower reply won the cache. One fetch runs at a time.
- A denied location permission was retried twice on every fetch cycle. It
  fails straight to `--` now.

### Removed

- Disconnect-vibration setting. The OS owns phone-disconnect vibration now;
  the select and its buzz path are gone.

## [1.7.0] - 2026-08-20

### Added

- **CRT effect** setting (off by default): the face is rendered as a tube
  screen. Corner arcs and a vignette falloff dim the edges (ordered dithering
  keeps the ramp smooth at 4 gray levels), chromatic aberration grows with
  the distance from the centre — the red channel reads from the left of each
  pixel, blue from the right — and the top and bottom rows warp inward up to
  5px. It runs as one per-pixel pass over the captured framebuffer in a
  topmost layer; off means the pass never runs.
- **Degauss strike** when the backlight turns on: the image wobbles sideways
  with decaying amplitude while the channel separation balloons, over eight
  frames (~700 ms). Timed in wide gaps so the sound pipeline keeps up.
- **CRT degauss sound** setting (off by default): a falling low woomp over
  the strike. Honors the system speaker-mute preference (Quiet Time included
  if so configured). Only plays with the CRT effect on.
## [1.6.0] - 2026-08-17

### Added

- Weather forecast window setting: Now, 2, 8, 12 (default), or 24 hours. It
  sizes the window the UV and precipitation probability maxima peak over.
  The slot labels read "(window max)" instead of "(next 12h max)", since
  12 hours is now a choice rather than a constant. Changing the setting
  refetches weather immediately instead of waiting out the cached values.
- Week Number complication in the bottom slots. ISO 8601 numbering, drawn
  as `W34`; newlib has no strftime `%V`, so the watch computes it. The
  year boundaries follow the spec: early January can read W52/W53 (last
  week's number), late December can read W01.

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

[Unreleased]: https://github.com/bemyak/nostalgic_commander/compare/v1.9.0...HEAD
[1.9.0]: https://github.com/bemyak/nostalgic_commander/compare/v1.8.0...v1.9.0
[1.8.0]: https://github.com/bemyak/nostalgic_commander/compare/v1.7.0...v1.8.0
[1.7.0]: https://github.com/bemyak/nostalgic_commander/compare/v1.6.0...v1.7.0
[1.6.0]: https://github.com/bemyak/nostalgic_commander/compare/v1.5.0...v1.6.0
[1.5.0]: https://github.com/bemyak/nostalgic_commander/compare/v1.4.1...v1.5.0
[1.4.1]: https://github.com/bemyak/nostalgic_commander/compare/v1.4.0...v1.4.1
[1.4.0]: https://github.com/bemyak/nostalgic_commander/compare/v1.3.1...v1.4.0
[1.3.1]: https://github.com/bemyak/nostalgic_commander/compare/v1.3.0...v1.3.1
[1.3.0]: https://github.com/bemyak/nostalgic_commander/compare/v1.2.0...v1.3.0
[1.2.0]: https://github.com/bemyak/nostalgic_commander/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/bemyak/nostalgic_commander/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/bemyak/nostalgic_commander/compare/c205236a...v1.0.0
