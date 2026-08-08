# TODOs

Work that's been decided on but not yet built. Undecided brainstorm material
lives in [IDEAS.md](IDEAS.md) until it's approved.

- [ ] Hardware verification remainder of the perf pass. Done since: `make
  build` runs on every change (mock-vs-SDK drift now caught by tests — the
  mock matches SDK linkage), and full-face emulator screenshot pairs were
  AE-clean across the 2026-08-08 refactor stack (TextLayer removal included).
  Still open: weather logs showing exactly one fetch per request, and the
  launch-race (exactly one fetch per relaunch with a stale cache). Note:
  health/battery/BT/time injection IS scriptable via `pebble emu-steps`,
  `emu-sleep`, `emu-heart-rate`, `emu-set-time`, `emu-bt-connection` — only
  the Clay config page needs a phone (`emu-app-config` shows but can't
  submit). Weather payloads may be injectable via `pebble send-app-message`.
- [x] Performance & battery pass: killed the :00/:30 weather fetch loop,
  health reads and weather fetches are slot-gated, event-driven renders are
  snapshot-gated, persistence writes are compare-before-write, bar fills
  paint as rects, forecast+AQI fetch in parallel, AppMessage buffers
  shrunk. Plan: [docs/superpowers/plans/2026-07-30-perf-battery.md](docs/superpowers/plans/2026-07-30-perf-battery.md).
  Implemented 2026-07-30; host tests green, emulator gates pending above.
- [x] Handle Timeline Quick View (UnobstructedArea API): honest occlusion —
  the face subscribes to `.did_change` and simply omits the covered bottom
  slot row while a Quick View is up (frames skipped on the canvas, slot
  text layers hidden), restoring on dismissal. No reflow, by decision.
  Emulator check pending alongside the perf pass.

## Deferred

- Accent-span metadata (formatters declaring their accent offsets instead of
  `trailing_unit_span`'s lexical heuristic) — deferred 2026-08-08: the two
  existing idioms (trailing-letter heuristic, explicit-position e.g. beats'
  `@`) cover both accent shapes and the churn bought only unification.
  Revisit only if a complication needs an accent neither idiom expresses.

## Done

- [x] Quiet Time indicator: new combined phone-status window
  `DATA_SOURCE_BT_QT = 32` (caption `BT/QT`, `[x][z]`, one checkbox inked per
  state) — top slots only; at ≥93 px the title splits into `BT`/`QT` caption
  stubs with the frame running through between them and the boxes gain an air
  cell (`[x] [z]`). Bottom slots offer the unchanged BT checkbox (9) and the
  new standalone `QT` (`DATA_SOURCE_QUIET_TIME = 31`, `[z]`) — the combined
  read too cramped at 63 px (emulator pass). `s_quiet_time_active` polled
  from `quiet_time_is_active()` at init and atop the minute tick. Ride-along
  Clay label polish: `.beat time`, `Next High / Low temperatures`. Spec:
  [docs/superpowers/specs/2026-08-06-quiet-time-design.md](docs/superpowers/specs/2026-08-06-quiet-time-design.md).
- [x] Extended weather: precipitation probability (next-12h max) and daily
  high/low — top slots, still one Open-Meteo request. (sunrise/sunset was
  part of this batch, pulled after the emulator check: the 11-cell value
  crowded the 93px top slot's borders.) Spec:
  [docs/superpowers/specs/2026-07-31-extended-weather-design.md](docs/superpowers/specs/2026-07-31-extended-weather-design.md).
- [x] Full-weather centre complication: four captioned status chips. Spec:
  [docs/superpowers/specs/2026-07-31-full-weather-centre-design.md](docs/superpowers/specs/2026-07-31-full-weather-centre-design.md).
- [x] Humidity complication (plain readout — no color bands); config lists
  reordered into domain groups. Spec:
  [docs/superpowers/specs/2026-07-31-humidity-complication-design.md](docs/superpowers/specs/2026-07-31-humidity-complication-design.md).
- [x] Air Quality Index (AQI) / UV Index complications (separate color-coding
  for individual and combined views; UV shows the next-12h max)
