# Nostalgic Commander

A Norton Commander-styled watchface for Pebble: time, date, and the data you
care about, in exact EGA colors — cyan frames over panel blue, the dimmed
shadow of it after dark. Text over icons, contrast over decoration, utility
over hand-holding.

![Nostalgic Commander](screenshot_current.png)

Built for the modern Pebble lineup; currently targets **emery**
(Pebble Time 2).

Nostalgic Commander is forked from
[tuiface](https://github.com/lizwinn/tuiface) by Elizardbeth and reworked
hard toward the DOS aesthetic: VGA bitmap font, EGA palettes, block-glyph
progress bars, .beat time. The backbone — the complication system, weather
pipeline, test harness, much of the runtime — is upstream's work. See
[License](#license); the upstream copyright notice ships unchanged.

## Gallery

| Commander Panel | Dialog | Shadowed Panel |
|---|---|---|
| ![Commander Panel](screenshots/01_theme_ega.png) | ![Dialog](screenshots/02_theme_dialog.png) | ![Shadowed Panel](screenshots/03_theme_shadow.png) |

| Complication combo | | Minimal layout |
|---|---|---|
| ![Complication layout A](screenshots/04_config1.png) | ![Complication layout B](screenshots/05_config2.png) | ![Minimal](screenshots/06_minimal.png) |

## Features

- **Big, legible time** in an IBM VGA 8x16 bitmap font at 4x, with your choice
  of date formats (`1970-12-31`, `31-12-1970`, `DEC 31st, 1970`, or the
  year-less short form), and the weekday before, after, or hidden.
- **Six complication slots** (two wide on top, three below, one wide in the
  middle) you can fill from: weather (condition + temperature), humidity,
  precipitation probability (peak over the next 12 hours; shows the live
  rate in mm while it actually rains, metric only), the day's
  high/low temperatures (rolling to tomorrow's as each extreme passes),
  steps, sleep, heart rate, active minutes, Bluetooth
  status, air quality (US AQI), UV index (the peak over the next 12 hours —
  what's coming, not what already happened), a combined AQI/UV view, a short
  date (`THU 12-31`, top slots only), or .beat (Swatch Internet Time) — or
  leave empty.
- **The middle slot** holds the date, a full-weather strip (condition,
  current temp, humidity and precipitation as captioned status chips), or a
  DOS progress bar for steps or battery — `█` blocks against a `░` track,
  with the percentage after it.
- **Four DOS/EGA themes, Commander Panel by default.** Auto cycles the other three on
  8-hour shifts, brightest to darkest as the day goes on: Dialog (Turbo
  Vision dialog box, blue frames, black text on light grey) 06:00–14:00,
  Commander Panel (Norton Commander panel, cyan frames, white entries over
  EGA blue) 14:00–22:00, Shadowed Panel (the same panel dimmed to grey
  chrome on black, the way Turbo Vision faked it) 22:00–06:00. Pick a theme
  directly to lock it in. Exact EGA colors, since Pebble's display uses the
  same channel steps.
- **Colors only when something needs attention**: temperature runs
  red-hot / blue-cold, AQI and UV go yellow then red past their thresholds,
  battery goes yellow then red as it drains — and green on the charger.
- **Weather without an API key** — data comes from
  [Open-Meteo](https://open-meteo.com) via your phone's location, refreshed
  every 30 minutes.

## Configuration

Open the watchface settings in the Pebble mobile app. Settings are
deliberately few:

| Setting | Options |
|---------|---------|
| Theme | Auto, Dialog, Commander Panel, Shadowed Panel |
| Units | Imperial, Metric |
| Date format | ISO, DOS, full text, short |
| Short date format | Month-Day, Day-Month |
| Day of week | Before date, after date, hidden |
| Enable vibration on phone disconnect | On (default), off — the buzz doubles as the dead-phone detector |
| Slots 1–6 | Data source per slot, or Empty |

That's the whole surface. Good defaults over knobs; if a behavior isn't
configurable, that's a decision, not an oversight.

## Philosophy

Inherited from upstream, held to more strictly, not less:

- **TUI-like, but legible.** The terminal aesthetic serves readability on a
  small e-paper-style screen; where the two conflict, legibility wins.
- **High-contrast themes.** Every palette keeps text sharply readable; muted,
  low-contrast color schemes are out of scope.
- **Curated complications.** Ever scrolled a settings page with a hundred
  complications trying to find the three you actually care about? Data sources
  are added deliberately and selectively.
- **Utility first.** When usefulness and approachability pull in different
  directions, useful wins.
- **Minimal configuration.** Every setting has to earn its place.
- **Fork-friendly.** This face only exists because upstream lives by that
  value. It applies here too: fork it, make it yours.

## Building from source

Requires the [Pebble SDK](https://developer.repebble.com). The CLI is set up
in a project-local virtualenv:

```sh
source pebble-env/bin/activate
pebble build                          # build for all targetPlatforms
pebble install --emulator emery       # run on the emery emulator
pebble install --phone <ip>           # install to a paired phone
```

Run the unit tests (host-only, no SDK needed):

```sh
cd test && make test
```

## Development

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — how it all works: data flow,
  modules, the complication system, theming, testing
- [AGENTS.md](AGENTS.md) / [CONTRIBUTING.md](CONTRIBUTING.md) — upstream's
  notes for agents and contributors; conventions here also apply
- [ISSUES.md](ISSUES.md) — known bugs · [TODOs.md](TODOs.md) — planned work ·
  [IDEAS.md](IDEAS.md) — undecided ideas

Full SDK docs, tutorials, and API reference: <https://developer.repebble.com>

## AI disclosure

Upstream tuiface was developed with assistance from AI coding agents —
Google's **Gemini** and Anthropic's **Claude** — and this fork continues the
same practice, under human direction and review. This includes code, tests,
and documentation.

If you'd rather not use a watchface built with AI assistance, that's
completely fair — no hard feelings.

## License

This project is licensed, like upstream, under the
[PolyForm Noncommercial License 1.0.0](LICENSE.md). In short: you may fork it,
modify it, and redistribute your own versions freely — for any
**noncommercial** purpose. Selling this watchface or a derivative of it is
not permitted. Upstream's required copyright notice ("Copyright Elizardbeth")
is retained in [LICENSE.md](LICENSE.md).

The bundled font is [Px437 IBM VGA 8x16](https://int10h.org/oldschool-pc-fonts/)
by VileR, with four diagonal arrows added, under
[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) — see
[docs/LICENSES.md](docs/LICENSES.md) for the full dependency audit.

Weather, UV, and air-quality data is provided by
[Open-Meteo.com](https://open-meteo.com/) (CC BY 4.0, free for non-commercial
use). See [docs/LICENSES.md](docs/LICENSES.md) for a full audit of upstream
dependency licenses.
