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

Before opening a PR, make sure `make test` passes (CI runs it; it includes
the format check). Two things will silently corrupt saved data or collide
installs if you get them wrong:

- Don't renumber `ComplicationDataSource` values or reuse a `PERSIST_KEY_*`
  constant — both are on-disk identifiers.
- If you're forking, change the `uuid` and `displayName` in `package.json` so
  your build doesn't collide with installed copies of Nostalgic Commander.

## Conventions

- State globals are `s_`-prefixed; most live in `data.c` (declared in
  `data.h`), with `theme.c` owning `s_active_theme` and `main.c` owning the
  window/layer handles. No accessor layer — that's idiomatic for Pebble C.
- "No data" sentinels vary by type and are declared where they're consumed:
  the weather wire fields in `messaging.c`'s field table, the rest beside
  their definitions in `data.c`. Formatters render them as `--`.
- Never hardcode colors in drawing code; read them from `s_active_theme`.
- State changes call `request_ui_redraw()`; the canvas redraws only when
  displayed state actually changed.
- Layout constants are hardcoded for emery (200×228), the only target
  platform.
- Tests `#include` the C sources directly (with `TEST_ENV` defined) so static
  functions are reachable. Add tests to `test/test_watchface.c` and register
  them with `RUN_TEST(...)`.

The [README](README.md#philosophy) covers the project's values;
[AGENTS.md](AGENTS.md) holds the hard rules and the module map.
