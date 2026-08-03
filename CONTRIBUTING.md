# Contributing to Nostalgic Commander

To be honest, I didn't think anyone was going to be interested in downloading
this watchface, let alone want to contribute to it. It seemed a bit silly to
write a file that I fully expect nobody to read.

That said, I think it is fairly safe to say that you are in fact reading this
file right now, and I was wrong. So, I will operate under the assumption that
I am communicating to someone interested in contributing to this watchface. If
this is not the case, I can only offer my sincere apologies.

So, since you *are* reading this, I suppose I should set some expectations and
conventions about how I am wanting to maintain this project. (This page is for
humans. If you're driving an AI agent, point it at [AGENTS.md](AGENTS.md) as
well.)

## The basics

- Bug fixes and test or legibility improvements are welcome.
- For a new complication or any larger change, I recommend opening an issue
  first — I'm selective about what I merge, and forking is always a fine
  option if we don't see eye to eye.

## Dev setup

You'll need the [Pebble SDK](https://developer.repebble.com); the `pebble` CLI
lives in a project-local virtualenv.

```sh
git clone --recurse-submodules <your-fork>   # test/unity is a submodule
source pebble-env/bin/activate
pebble build
pebble install --emulator emery
```

Before opening a PR, run `make format` and make sure `make test` passes (CI
checks both). Two things will silently corrupt saved data or collide installs
if you get them wrong:

- Don't renumber `ComplicationDataSource` values or reuse a `PERSIST_KEY_*`
  constant — both are on-disk identifiers.
- If you're forking, change the `uuid` and `displayName` in `package.json` so
  your build doesn't collide with installed copies of Nostalgic Commander.

## Conventions

- State lives in `s_`-prefixed globals declared in `data.h`, defined in
  `data.c`. There's no accessor layer — that's idiomatic for Pebble C.
- "No data" sentinels: `-1` (steps, sleep, AQI, UV), `-999` (temperature),
  `0` (heart rate). Formatters render these as `--`.
- Never hardcode colors in drawing code; read them from `s_active_theme`.
- State changes call `request_ui_redraw()` and mark the canvas dirty
  rather than drawing directly.
- Layout constants are hardcoded for emery (200×228), the only current target
  platform.
- Tests `#include` the C sources directly (with `TEST_ENV` defined) so static
  functions are reachable. Add tests to `test/test_watchface.c` and register
  them with `RUN_TEST(...)`.

The [README](README.md#philosophy) covers the project's values, and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) is a full tour of how it works.
