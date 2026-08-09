# AGENTS.md

Working notes for AI agents (and new contributors) on **Nostalgic
Commander**, an opinionated Pebble watchface. Read this first; deeper material is linked
throughout.

## Philosophy & goals

These are project values, not suggestions. When a change conflicts with them,
the change is wrong.

- **Nostalgic Commander has an opinion.** It cannot please everyone and we
  don't try.
  "Add an option for it" is not the default answer — prefer choosing the right
  behavior over making behavior configurable.
- **Complications are curated, hard.** Proposals are welcome — but bemyak
  approves every complication personally and plans to be *very* selective.
  Watchfaces offering hundreds of complications bury the three you actually
  care about; Nostalgic Commander won't become one. Propose first, don't
  implement speculatively, and don't take a "no" as a verdict on the idea —
  it's usually about protecting focus.
- **Forking is encouraged.** The strict curation above only works because
  forking is the sanctioned escape hatch. When someone wants a feature that
  doesn't fit Nostalgic Commander, pointing them at a fork (see
  [CONTRIBUTING.md](CONTRIBUTING.md)) is a good outcome, not a brush-off.
- **TUI-like, but legible.** The aesthetic is a terminal UI — framed windows,
  monospace-feeling layouts, text over icons. If a TUI flourish hurts
  legibility on the watch, legibility wins: the frames are solid 2px strokes
  rather than character-drawn rules, because box-drawing glyphs render thin and
  uneven at this size.
- **Everything is tested.** New logic comes with unit tests, expanded and run
  to prove it holds up. Pure logic (formatting, thresholds, theme selection)
  belongs in testable modules, not inline in UI code.
- **Configuration stays simple.** Every setting must justify its existence.
  Fewer, better defaults beat option sprawl.
- **Themes are high contrast.** Every palette must keep text sharply
  readable; muted/low-contrast color schemes are out of scope.
- **Utility over approachability.** When the two conflict, pick the version
  that's more useful to a committed user, even if it's less friendly at first
  glance.
- **Phone fetches and reduces; watch interprets and presents.** Values sent
  in the watch's configured units are the phone executing watch settings, not
  phone presentation. Wire vocabulary is API-native (WMO codes), never
  display strings. Bandwidth-forced reduction (AppMessage is small) stays
  phone-side.

## Build, run, test

The CLI comes from pip; the SDK and Clay install once per machine/clone:

```sh
pip install pebble-tool
pebble sdk install latest
npm ci
pebble build            # build for all targetPlatforms
pebble install --emulator emery
pebble install --phone <ip>
make test               # format check + host unit tests, no SDK needed
make format             # apply clang-format to C and JS sources
```

The README screenshot (`screenshot_current.png`) is a real capture, not a
mockup — regenerate it whenever the face's appearance changes with
`pebble screenshot --emulator emery screenshot_current.png` (`--phone <ip>`
captures from real hardware, which is what the README should ideally show).

Formatting is enforced: `.clang-format` at the repo root, applied to `src/c/`,
`src/pkjs/*.js`, and the hand-written test sources in `test/` (not the unity
submodule). CI (`.github/workflows/ci.yml`) runs the format check and the
test suite on every push and PR. Run `make test` after any change to `src/c/`.
The emulator has no real health data (steps/sleep/HR) and no phone weather
unless the JS side runs, so logic verification happens in the unit tests,
visual verification in the emulator.

When extending:
- Adding an SDK call? Extend `test/pebble_mock.c` to match.
- New glyph? Add it to the font's `characterRegex` in `package.json` or it
  won't render.
- After `messageKeys` edits, `pebble clean` — a stale `message_keys.auto.h`
  survives incremental builds.

## Architecture

The module map:

| Path | Role |
|------|------|
| `src/c/main.c` | Lifecycle: window, layers, service subscriptions, settings load |
| `src/c/layout.h` | All face geometry: margins, slot rects, TIME window, clock layer |
| `src/c/data.c`/`.h` | All state (globals), `ComplicationDataSource` enum, shared value helpers |
| `src/c/complication.c`/`.h` | The registry (`ComplicationSpec` table) and its per-source formatters |
| `src/c/theme.c`/`.h` | DOS/EGA palettes, theme selection |
| `src/c/status.c`/`.h` | Per-source severity policy (`get_source_color`), sole picker of status colors |
| `src/c/drawing.c`/`.h` | Canvas rendering: ASCII windows, slot refresh |
| `src/c/messaging.c`/`.h` | AppMessage: weather requests, inbox parsing, persistence |
| `src/pkjs/index.js` | Phone side: Clay config, geolocation, Open-Meteo fetches |
| `src/pkjs/config.js` | Clay settings page, as code (pinned by `test/pkjs/config.test.js`) |
| `src/pkjs/weather.js` | Fetch shaping, response parsing, cache policy — the pure, unit-tested JS half (`test/pkjs/weather.test.js`, `test/pkjs/wire-contract.test.js`) |
| `test/` | Unity-based host C suite with a hand-written SDK mock; `test/pkjs/` pins the JS halves (parser, config shape, cross-language wire contract) |

Data flows phone → watch over AppMessage: the watch sends a trigger message,
JS fetches weather/AQI/UV from Open-Meteo and replies with one dictionary;
`inbox_received_callback()` updates the `data.c` globals and redraws. Settings
from the Clay page travel the same path and are persisted on the watch.

## Conventions

Coding conventions (state globals, sentinels, theming, canvas refresh, test
layout) live in [CONTRIBUTING.md](CONTRIBUTING.md#conventions). They apply to
agent-written code too — follow them.

## Hard rules

- **`ComplicationDataSource` enum values are stable identifiers.** They are
  persisted and referenced as option values in `src/pkjs/config.js`.
  `DATA_SOURCE_EMPTY` is pinned at 20 (declared last in the enum); new
  sources get new numbers anywhere; never renumber or reuse.
- **Persistence keys are a stable on-disk format.** Settings and the weather
  cache are stored under the hand-assigned `PERSIST_KEY_*` constants in
  `src/c/messaging.h`, deliberately decoupled from the auto-numbered
  `messageKeys`. Never reuse or renumber a `PERSIST_KEY_*` value. (Because of
  this, `messageKeys` in `package.json` is *not* order-sensitive — reordering
  it is safe.)
- **New complications require bemyak's approval** (see Philosophy —
  proposals welcome, bar high).

## Adding a complication (once approved)

enum value in `data.h` → spec row in `complication.c`'s
`s_complication_specs[]` (`format` can be one of the generic idioms, `draw`
one of drawing.c's; health-backed sources set `.health_metric` — the metric
is read only while such a slot is visible — and weather-backed ones set
`.needs_weather`) → `get_source_color` case if it needs color logic → Clay
options in `config.js` → (if phone-sourced) `package.json` message key +
`weather.js` field row + `messaging.c` table row → unit tests.

## Project tracking

- [ISSUES.md](ISSUES.md) — known bugs and suspect behavior.
- [TODOs.md](TODOs.md) — approved work that hasn't been built yet.
- [IDEAS.md](IDEAS.md) — undecided brainstorm material. Nothing there is
  approved; don't implement from it (see the complication rule above).
