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

## Build, run, test

The Pebble CLI lives in the project-local virtualenv:

```sh
source pebble-env/bin/activate
pebble build                          # build for all targetPlatforms
pebble install --emulator emery       # run on the emery emulator
pebble install --phone <ip>           # install to a paired phone
```

The README screenshot (`screenshot_current.png`) is a real capture, not a
mockup — regenerate it whenever the face's appearance changes:

```sh
pebble install --emulator emery       # get the current build running
pebble screenshot --emulator emery screenshot_current.png
```

(`--phone <ip>` works in place of `--emulator emery` to capture from real
hardware, which is what the README should ideally show.)

Unit tests run on the host — no SDK or emulator needed:

```sh
make test            # format check + unit tests (top-level Makefile)
make format          # apply clang-format to C and JS sources
```

Formatting is enforced: `.clang-format` at the repo root, applied to `src/c/`,
`src/pkjs/index.js`, and the test sources (not the unity submodule). CI
(`.github/workflows/ci.yml`) runs the format check and the test suite on every
push and PR. Run `make test` after any change to `src/c/`. The emulator has no real health
data (steps/sleep/HR) and no phone weather unless the JS side runs, so logic
verification happens in the unit tests, visual verification in the emulator.

## Architecture

Full tour: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). The short version:

| Path | Role |
|------|------|
| `src/c/main.c` | Lifecycle: window, layers, service subscriptions, settings load |
| `src/c/data.c`/`.h` | All state (globals), `ComplicationDataSource` enum, formatters |
| `src/c/theme.c`/`.h` | DOS/EGA palettes, theme selection |
| `src/c/status.c`/`.h` | Per-source severity policy (`get_source_color`), sole picker of status colors |
| `src/c/drawing.c`/`.h` | Canvas rendering: ASCII windows, slot refresh |
| `src/c/messaging.c`/`.h` | AppMessage: weather requests, inbox parsing, persistence |
| `src/pkjs/index.js` | Phone side: Clay config, geolocation, Open-Meteo fetches |
| `src/pkjs/config.json` | Clay settings page definition |
| `test/` | Unity-based host tests with a hand-written Pebble SDK mock |

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
  persisted and referenced as string values in `src/pkjs/config.json`. Append
  new sources before `DATA_SOURCE_EMPTY` only by adding new numbers; never
  renumber.
- **Persistence keys are a stable on-disk format.** Settings and the weather
  cache are stored under the hand-assigned `PERSIST_KEY_*` constants in
  `src/c/messaging.h`, deliberately decoupled from the auto-numbered
  `messageKeys`. Never reuse or renumber a `PERSIST_KEY_*` value. (Because of
  this, `messageKeys` in `package.json` is *not* order-sensitive — reordering
  it is safe.)
- **New complications require bemyak's approval** (see Philosophy —
  proposals welcome, bar high).

## Adding a complication (once approved)

The step-by-step recipe is in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#adding-a-new-complication-source):
enum value → label/data/color cases → Clay options → (if phone-sourced)
message key + JS fetch + inbox parsing → unit tests.

## Project tracking

- [ISSUES.md](ISSUES.md) — known bugs and suspect behavior.
- [TODOs.md](TODOs.md) — approved work that hasn't been built yet.
- [IDEAS.md](IDEAS.md) — undecided brainstorm material. Nothing there is
  approved; don't implement from it (see the complication rule above).
