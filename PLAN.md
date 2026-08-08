# PLAN — architecture fixes for template-readiness

Origin: architectural review 2026-08-08. Scope: structure, wire contract, test
floor, docs. No new complications. Repo hard rules apply throughout (enum ids
and persist keys are stable formats; `make test` green after every step).

## Principles (agreed)

- **Docs-lite.** Code self-documents. AGENTS.md is the one sanctioned doc;
  everything else is enforcement (tests, types, table structure) or a short
  AGENTS.md bullet. Comments: keep only SDK/hardware constraints and
  deliberate-acceptance notes that invite wrong "fixes"; delete narration,
  color annotations, journal history, duplicated notes, anything a rename or
  type could carry.
- **Phone fetches and reduces; watch interprets and presents.** Values in the
  watch's configured units are the phone executing watch settings, not phone
  presentation. Wire vocabulary is API-native (WMO codes), never display
  strings. Bandwidth-forced reduction (256-byte AppMessage) stays phone-side.

## Decisions (settled)

- **D1** — dialog theme colors: keep shipped pixels; the contradicting
  comments are deleted by the cull, not rewritten.
- **D3** — precip-mode ownership: resolved by the phone/watch principle. Not
  "JS sends -1 unless precipitating"; instead the raw WMO code crosses the
  wire and the watch decides (Phase 2). Units stay phone-converted (phone
  executes the watch's own setting).
- **D2** — delete `docs/ARCHITECTURE.md`. Residue placed point-of-use, not
  into AGENTS.md: dataflow → header comment in `messaging.c` (the pipe
  module); render-gate invariant → `UiSnapshot` (3.6); theme-schedule
  rationale → `theme_for_hour`. AGENTS.md gains only the phone/watch
  principle (a value) and, post-fold, the 4-line complication recipe.
  Rationale: the repo's evidence is that why-comments at point of use stay
  accurate while restating-docs drift (this file misdirected three ways on
  review day).

## Phase 0 — docs/repo de-rot (~30 min; no code risk)

- [x] AGENTS.md: `config.json` → `config.js`; enum rule reworded ("new ids =
      new numbers; `DATA_SOURCE_EMPTY` is pinned at 20; never renumber");
      format-scope list matches Makefile; one commands block.
- [x] README.md: feature list → pointer to `config.js OPTION_LABELS`; test
      command is top-level `make test` only.
- [x] Delete `docs/ARCHITECTURE.md` (D2). Point-of-use residue:
      phone→watch dataflow header in `messaging.c`, theme-schedule rationale
      at `theme_for_hour`. AGENTS.md gains only the principle bullets. Slim
      CONTRIBUTING.md to deltas.
- [x] Duties as AGENTS.md bullets (not comments): extend the SDK mock when
      adding SDK calls; new glyphs need `package.json` characterRegex;
      `pebble clean` after `messageKeys` edits. (setUp duty becomes
      `data_reset()` in Phase 1.)
- [ ] Comment cull scope: delete narration, color annotations, journal
      history, duplicated notes. Keep (short, point-of-use): SDK/hardware
      constraints (HR instant-vs-range, fill-rects-not-draw_line AA,
      cache-after-parse ordering), sentinel meanings, deliberate-acceptance
      notes that invite wrong fixes.
- [x] CHANGELOG.md: retro-fill 1.3.0/1.3.1, add `[Unreleased]`, fix compare
      links. Delete `resources/icon.png~`.

Verify: `make test` green; no `config.json` references outside CHANGELOG.

## Phase 1 — test floor hardening (~½–1 day; 1.1 is the swing factor)

- [x] 1.1 `test/Makefile`: `-fsanitize=address,undefined -fno-omit-frame-pointer
      -Werror`; fix fallout. (Zero sanitizer findings; unity needed no
      exclusions.)
- [x] 1.2 Mocks: `MOCK_DICT_MAX` 16→24; `assert()` overflow →
      `TEST_FAIL_MESSAGE`; `graphics_draw_text` last param →
      `const GTextAttributes*` (stub type in `pebble.h`); fonts return
      distinct non-NULL sentinels.
- [x] 1.3 `reset_all_state()` in test_watchface.c (single-TU reaches
      everything, incl. main.c's moved-to-file-scope `s_fmt_*` cache);
      `setUp` calls it. Leaks killed; 3 named slot fixtures + `set_slots`
      replaced 10+ copy-pasted save/restore blocks; dead tail restores
      deleted. (`data_reset()` in data.c rejected: the reset walks
      messaging.c's static table, only reachable from the test TU.)
- [x] 1.4 Mock knobs: record subscriptions; peek values (battery, BT, 24h),
      `outbox_begin` failure, health permission denied, real
      `time_start_of_today`. (Incl. per-metric `mock_health_accessible[]`
      masks, `mock_dict_add_*_width` staging, `mock_last_text` capture.)
- [x] 1.5 New tests: `init()` stale-cache launch-fetch gate; four init
      subscriptions registered; ordinal suffixes asserted in the 372-day
      sweep; `tuple_get_int` width-1/2/uint arms; EMPTY/unknown draws nothing;
      permission-denied → sentinel paths; 24h clock + leading-zero strip via
      captured `set_text`; a real 17-key weather payload parses. (12 new
      tests; ordinal sweep extended in place, all 372 days vs an independent
      reference.)
- [x] 1.6 Runner-drift guard: `make test` fails when defined ≠ registered
      test count. (Caught a dropped registration on its first run.)
- [x] 1.7 CI runs `make test` (single source) instead of reimplemented steps.

Verify: suite green incl. sanitizers; sabotage one subscription and one
ordinal → confirm red → revert.

## Phase 2 — condition code on the wire (~2–3 h)

- [x] `WEATHER_COND` cstring → raw WMO `weather_code` int. `WMO_COND` table
      moves `weather.js` → `data.c`, one row per range with both facets:
      display word + is-precipitating.
- [x] `weather_shows_precip_amount()` becomes a table lookup; strcmp of
      display words dies. Drizzle `<1mm` behavior preserved exactly.
- [x] Unknown/absent code → `--` (was invented `CLD`). Sole intentional
      behavior change.
- [x] Persist: `PERSIST_KEY_WEATHER_COND` (1001, string) retired (1007-1008
      precedent); `PERSIST_KEY_WEATHER_COND_CODE` added at the next free id.
      Old caches lack the key → existing optional-field load path degrades to
      `--` until next fetch; no version key needed.
- [x] `inbox_received_callback`: pair-gate works with cond-as-int; cond joins
      `s_weather_fields`, its special-case `continue` dies.
- [x] Stays phone-side: units conversion, dual unit ladders in `status.c`,
      `units_changed` refetch, extremes/UV/PCP reduction, precip mm×10.

Verify: JS suite (code passthrough, no WMO table); C tests per code family
(word + precip facet), precip-mode per code, missing-key cache degrades;
`make test` + `pebble build`; emulator rain → `Xmm`, both unit systems.

## Phase 3 — C refactors + comment cull (~1 day + 1–2 h sweep)

Each item independent: ends with `make test` + `pebble build` green and
emulator screenshot A/B pixel-identical (excluding clock digits). Comment
cull rides each touched file (scope: Phase 0 checkbox), then one final
sweep over the rest. Phase estimate excludes 3.7.

- [x] 3.1 Rename `bt_qt_split_captions` → `is_wide_slot` (wind uses it too).
- [x] 3.2 Wind dedup: one shared assembly in `data.c` owns the 999-clamp and
      unit label; `draw_wind_complication` stops re-deriving.
- [x] 3.3 `update_health_info` → one loop over a local table
      `{watched sources, metric, target, empty_value, read_mode, divisor}`.
      `read_mode ∈ {RANGE_SUM, INSTANT_PEEK}` carries the HR instant-vs-range
      divergence as data, not a hidden special case; `divisor` carries
      active-minutes' `/60`. Merge the two any-slot-need mechanisms.
- [x] 3.4 `tuple_get_int` → `messaging.c` (declaration `data.h` →
      `messaging.h`); `update_time` → `refresh_state`; named slot indexes
      replace hardcoded 2–4 in `quick_view_covers_slot`.
- [x] 3.5 Registry fold: `ComplicationSpec` gains `draw` fn + `frame` enum
      (`FRAME_PLAIN`, `FRAME_AQI_UV`, `FRAME_HUM_PCP`, `FRAME_FULL_WEATHER`, `FRAME_HI_LO`,
      `BT_QT`). Named boundary crossing: the ~26 static drawers are exported
      through `drawing.h` so the table in `data.c` can reference them
      (vtable idiom; rejected alternative — parallel draw-table in drawing.c
      — keeps the second touch point the fold exists to kill). Frame drawers
      stay private: `canvas_update_proc`'s branch chain becomes a static
      frame-enum→fn table inside drawing.c. `canvas_drawer` switch dies.
      `status.c` color switch stays — single-authority beats declarative
      there. After this, adding a complication = spec row + color case +
      Clay entry (+ wire rows).
- [ ] 3.6 Render-gate: two-line invariant contract at `UiSnapshot` ("every
      drawn pixel derives from these fields; new visual state → new field").
      Kept comment — invisible, load-bearing.
- [ ] 3.7 Accent-span metadata — CONDITIONAL, LAST, default skip. The
      trailing-letter heuristic plus the explicit-position idiom (beats' `@`)
      already cover both accent shapes; signature churn across ~24 formatters
      buys only unification. Revive only if a complication needs an accent
      neither idiom expresses. If done: `ComplicationFormatFn` gains
      `accent_at/accent_len` out-params, `trailing_unit_span` and
      `strchr`-parsing die, AE-0 emulator sweep as acceptance.

## Phase 4 — closeout (~30 min)

- [ ] `make test`, `pebble build`, emulator pass over each slot type.
- [ ] CHANGELOG `[Unreleased]` finalized; README screenshot regen only if any
      phase changed pixels (none should).
- [ ] Delete this file (TODOs.md/ISSUES.md remain the trackers).

## Battery

No change: fetch cadence, retry policy, render gating, and subscription set
are untouched in every phase; payload loses a cstring, gains an int.
Transient on upgrade: condition reads `--` until the next fetch (≤30 min).

## Explicitly not doing

Message-type wire key; phone→watch timestamp plumbing; test auto-discovery
beyond the count guard; any replacement architecture doc; new complications.
