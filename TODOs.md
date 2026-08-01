# TODOs

Work that's been decided on but not yet built. Undecided brainstorm material
lives in [IDEAS.md](IDEAS.md) until it's approved.

- [ ] Emulator/SDK verification pass for the perf work below: one `pebble
  build` (watch for mock-vs-SDK signature drift), crop-compare pixel diffs
  for Tasks 5–7 per the plan (revert the Task-7 bar fill rect to glyph runs
  if AE > 0), weather logs showing exactly one fetch per request, and
  hardware launch-race confirmation (exactly one fetch per relaunch with a
  stale cache).
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

## Done

- [x] Extended weather: precipitation probability (next-12h max) and daily
  high/low — top slots, still one Open-Meteo request. (sunrise/sunset was
  part of this batch, pulled after the emulator check: the 11-cell value
  crowded the 93px top slot's borders.) Spec:
  [docs/superpowers/specs/2026-07-31-extended-weather-design.md](docs/superpowers/specs/2026-07-31-extended-weather-design.md).
- [x] Full-weather centre complication: four captioned status chips. Spec:
  [docs/superpowers/specs/2026-07-31-full-weather-centre-design.md](docs/superpowers/specs/2026-07-31-full-weather-centre-design.md).
- [x] Humidity complication (comfort-band colors); config lists reordered
  into domain groups. Spec:
  [docs/superpowers/specs/2026-07-31-humidity-complication-design.md](docs/superpowers/specs/2026-07-31-humidity-complication-design.md).
- [x] Air Quality Index (AQI) / UV Index complications (separate color-coding
  for individual and combined views; UV shows the next-12h max)
