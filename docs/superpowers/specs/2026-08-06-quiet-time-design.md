# Quiet Time indicator — design

Date: 2026-08-06. Decided with bemyak in conversation (user feature request,
accepted). Revised same-day after the emulator pass: the combined window
proved too cramped at bottom-slot width, so it is a **new source offered only
in the top slots**; `DATA_SOURCE_BLUETOOTH = 9` keeps its released BT-only
semantics everywhere.

## Scope

Three sources; the stable-identifier hard rule is honored (nothing renumbered).

1. **New combined `DATA_SOURCE_BT_QT = 32`** — caption `BT/QT`, value
   `[x][z]`, `x` inked while the phone is connected, `z` while Quiet Time is
   active (four states, `[ ][ ]` … `[x][z]`). Offered **only** in the two top
   slots. At ≥93 px width the frame splits the title into `BT`/`QT` caption
   stubs with the border running through between them and the boxes gain an
   air cell (`[x] [z]`, 7-cell strip, both registered cell-for-cell). The
   compact 6-cell single-title form remains as the defensive render below
   that width.
2. **`DATA_SOURCE_BLUETOOTH = 9` unchanged** — caption `BT`, `[x]`/`[ ]`.
3. **New standalone `DATA_SOURCE_QUIET_TIME = 31`** — caption `QT`, value
   `[z]`/`[ ]`. Together with BT this is the entire bottom-slot phone-status
   offer.

## Geometry (verified against drawing.c constants)

These numbers are why the combined window went top-only:

- Value `[x][z]` = 6 cells / 48 px; fits the 93 px top windows (11-cell cap)
  with ~20 px of air and the 63 px bottom windows (7-cell cap). Caption
  `BT/QT` (5 chars) is under every window's title cap (bottom cap: 6).
- The rejected alternative — everything on the value row, `BT[x] QT[z]` — is
  exactly the 11-cell cap with zero margin; same crowding that killed the
  sunrise/sunset source.
- Per-checkbox caption stubs were declined in the abstract (per-box caption
  binding buys little with two fixed-order boxes) but adopted after seeing
  the rendered face: `draw_split_caption_window` reuses the captioned-bar
  stub math for exactly two captions without generalizing the weather-strip
  table.

## Data flow

- New global `bool s_quiet_time_active` (data.c/data.h), following the
  `s_connected` pattern.
- `main.c`: one-line refresh (`s_quiet_time_active = quiet_time_is_active();`)
  at init, next to the `s_connected` peek, and at the top of `tick_handler`
  before `update_time()`. The minute tick already guarantees a full-tree
  render and the snapshot gate sees the text change, so no extra redraw
  plumbing; worst-case staleness is one tick. `quiet_time_is_active()` is in
  the emery SDK (`_PBL_API_EXISTS_quiet_time_is_active`).
- Host tests: `test/pebble_mock.c` gains a `quiet_time_is_active()` stub
  following the `connection_service_peek_pebble_app_connection` pattern
  (test-settable global).

## Rendering / color

- Both sources stay in `theme.c`'s plain `text_primary` case family — the
  checkbox states it, no threshold colors (existing BT comment covers it;
  extend its case list with QT).
- `percent` out-param: combined keeps `connected ? 100 : 0` (unchanged);
  standalone QT sets `active ? 100 : 0` per the BT precedent. Bands are
  color-gated, so neither paints a band.

## Clay config (`src/pkjs/config.json`)

- Top slots (SLOT_1/2): one phone-status option —
  `{ "label": "Bluetooth + Quiet Time", "value": "32" }` — replacing the old
  `9` entry in place.
- Bottom slots (SLOT_3/4/5): `9` keeps `"Bluetooth Status"`, plus
  `{ "label": "Quiet Time", "value": "31" }` after it. `defaultValue: "9"` on
  SLOT_5 is untouched — the bottom-right default stays BT-only, matching
  every released face.
- Ride-along label polish (bemyak): `.beat (Swatch Internet Time)` →
  `.beat time`; `High / Low Temperature (rolls as extremes pass)` →
  `Next High / Low temperatures`. ASCII only.

## Tests (TDD)

- Labels: `"BT"` (9), `"BT/QT"` (32), `"QT"` (31).
- Values: BT on/off; combined four-state matrix; standalone on/off.
- Color: all three → `text_primary`; percent per the Rendering section.
- `bt_qt_split_captions(w)`: width-threshold test (62/63 → compact, 93/184 →
  split). The split frame/box pixels are screenshot-gated.

## Verification

`make test` (clang-format + unit, 96 green) and `pebble build` for all
targets pass. The emulator pass (freshly repaired on dalos) iterated the
layout with bemyak and produced today's top-only revision. The README
screenshot was **not** regenerated: the default layout is pixel-identical to
the released one, so the committed file stands. Committing is bemyak's (jj).
