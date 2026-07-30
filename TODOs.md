# TODOs

Work that's been decided on but not yet built. Undecided brainstorm material
lives in [IDEAS.md](IDEAS.md) until it's approved.

- [ ] Performance & battery pass: kill the :00/:30 weather fetch loop, stop
  fetching weather and reading health metrics nothing displays, gate
  event-driven renders on state changes, cut per-render drawing cost.
  Plan: [docs/superpowers/plans/2026-07-30-perf-battery.md](docs/superpowers/plans/2026-07-30-perf-battery.md).
- [ ] Handle Timeline Quick View (UnobstructedArea API): the overlay lands
  exactly on the bottom slot row on emery. Adapt the layout while a Quick
  View is up instead of letting it cover slots 3–5. Encouraged by the
  Spring 2026 SDK updates; instructions are in the official C tutorial.

## Done

- [x] Air Quality Index (AQI) / UV Index complications (separate color-coding
  for individual and combined views; UV shows the next-12h max)
