# Performance & Battery Pass Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate redundant renders, network round trips, persistence writes, and per-render drawing cost in the tuiface watchface. Every task is behavior-preserving except two, both reviewed and accepted and both called out at their task: Task 10 lets the reported location age to the weather cadence, and Task 11 skips weather fetches — watch- and phone-side — until a weather slot exists, with an immediate fetch when one first appears. Task 11 also removes a double fetch on relaunch, which is a straight bug fix.

**Architecture:** PebbleOS scheduling model (verified against `coredevices/pebbleos@main`, 2026-07-30): `layer_mark_dirty` carries no region (layer.c:105), it only sets `window->is_render_scheduled` (window.c:150); `window_render` then calls `update_proc` on every visible layer (window.c:113, layer.c:181). Marks within one event coalesce into one render. Consequences:

1. The minute clock's `text_layer_set_text` (src/c/main.c:98) schedules one full-tree render every tick, no matter what. **1440 renders/day is a floor; do not try to lower it.** Conditional-dirty schemes and "static frames layer" splits buy nothing and are explicitly out of scope.
2. The levers are: (a) don't schedule renders above the floor — gate health/battery/Bluetooth/inbox event handlers on an actual state change; (b) make each render cheaper — kill the duplicate full-screen fill, paint bar fills as rects, stop paying WordWrap layout.

**Tech Stack:** Pebble SDK C (emery target only), PebbleKit JS (Clay), Unity host tests (`test/`, hand-written mock), clang-format (CI-enforced), emulator screenshots for pixel-diff verification.

## Global Constraints

- VCS is `jj`, never `git`; never push. Each task ends with `jj desc -m "…"`; the next task begins with `jj new -m "…"`. Commit messages: `perf: <what>`.
- `make test` (top Makefile) = clang-format check + host unit tests, mirrors CI. Run it at every task boundary. `make format` fixes formatting.
- `ComplicationDataSource` values are stable persisted identifiers — never renumber, never add except before `DATA_SOURCE_EMPTY` (src/c/data.h, AGENTS.md hard rule).
- `PERSIST_KEY_*` are a stable on-disk format — never reuse or renumber (src/c/messaging.h, AGENTS.md hard rule). This rules out packing the weather cache into one blob; Task 8 uses compare-before-write instead.
- `connection_service_subscribe` stays unconditional: it drives the disconnect vibe (src/c/main.c `handle_bluetooth`), independent of any displayed slot.
- ISSUES.md documents as accepted design: default 10-minute HRM sampling is deliberate, and "health data is re-read every tick and on `HealthEventHeartRateUpdate`". This plan never reduces health read cadence for displayed metrics and never changes HRM sampling. Task 4 only skips reads for metrics **no slot displays**.
- The tick remains the 60-second coalescing backstop for all state. PebbleOS health events are droppable under queue pressure; nothing in this plan makes event delivery load-bearing.
- The emulator has no health data (steps/sleep/HR read as `--`) and no real battery drain source. Visual verification uses injected settings (`pebble emu-app-config`) to put deterministic, network-free complications on screen; see Task 5 for the shared setup.
- Pebble renders text-layer content only when a render is scheduled; `text_layer_set_text_color`/`layer_set_hidden`/`window_set_background_color` all early-return when the value is unchanged (verified in pebbleos source), so calling them unconditionally is free. `text_layer_set_text` marks dirty **unconditionally** (text_layer.c:121) — that asymmetry is what Task 3 exploits.
- Line numbers cited below are from the tree on 2026-07-30 and will drift; function names are the durable reference.

## Background: what this fixes

1. **Weather fetch feedback loop** (bug). `inbox_received_callback` ends with `update_time()` (src/c/messaging.c), and `update_time()` requests weather whenever `tm_min % 30 == 0` (src/c/main.c:111). Replies arriving during minute :00/:30 re-arm the request until the minute flips: ~20–60 fetches, 40–120 HTTPS requests, twice an hour, with `save_weather_cache()` amplifying to ~100–300 flash writes per episode. Task 1.
2. **Event-driven render storms.** Every `HealthEventMovementUpdate` (several/second while walking), battery, and Bluetooth event schedules a full-tree render even when nothing visible changed. Task 3 gates all of them on one snapshot comparison.
3. **Per-minute cost.** Three `localtime()` calls and ~5 `strftime` calls per tick for values that change daily (Task 2); health syscalls for undisplayed metrics (Task 4 — `health_service_metric_accessible` is a real syscall path, not a flag check).
4. **Per-render cost.** Duplicate full-screen fill (Task 5), `GTextOverflowModeWordWrap` layout passes on pre-clipped runs (Task 6), up to ~32 block glyphs for what is a solid bar (Task 7).
5. **Phone side.** Serial weather→AQI XHRs double radio-on time per fetch (Task 9); `maximumAge: 60000` forces a GPS wake every 30 min (Task 10, accepted trade).
6. **Weather fetched with nothing to show it, and fetched twice on relaunch.** The :00/:30 request fires regardless of slot contents, so a layout with no weather-derived slot still wakes the phone, takes a location fix, and makes two HTTPS round trips every 30 minutes for data nothing renders — the most expensive recurring operation in the system, and the only one that also spends the *phone's* battery. Separately, the JS `ready` listener fetches proactively on launch while the watch's `init()` independently requests a fetch for the same stale cache, so every relaunch with stale caches costs two full fetches. Task 11.

---

### Task 1: Edge-trigger weather requests

Kills the :00/:30 feedback loop. Requests happen on the tick edge only; `update_time()` (also called from `inbox_received_callback` and `init`) becomes fetch-free.

**Files:**
- Modify: `src/c/main.c` (`update_time`, `tick_handler`)
- Test: `test/test_watchface.c` (remove `expected_tick_fetches`, two new tests)

**Interfaces:**
- Consumes: `request_weather()` (src/c/messaging.h), `mock_outbox_sends` (test/pebble_mock.c)
- Produces: unchanged public API. `update_time()` must never call `request_weather()` after this task.

- [ ] **Step 1: Update the existing refetch test to assert the fixed behavior**

In `test/test_watchface.c`, delete the `expected_tick_fetches()` helper and its comment block, and in `test_inbox_units_change_should_trigger_weather_refetch` change:

```c
TEST_ASSERT_EQUAL_INT(before + 1 + expected_tick_fetches(), mock_outbox_sends);
```

```c
TEST_ASSERT_EQUAL_INT(before + expected_tick_fetches(), mock_outbox_sends);
```

to:

```c
TEST_ASSERT_EQUAL_INT(before + 1, mock_outbox_sends);
```

```c
TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);
```

- [ ] **Step 2: Write the failing tests**

Add after `test_inbox_units_change_should_trigger_weather_refetch`:

```c
void test_update_time_should_never_request_weather(void) {
  // Regression for the :00/:30 feedback loop: every weather reply ends in
  // update_time(); when the fetch trigger lived there, each reply re-armed
  // the request until the minute flipped — 20-60 fetches, twice an hour.
  int before = mock_outbox_sends;
  update_time();
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);
}

void test_tick_handler_should_request_weather_on_the_half_hour_edge(void) {
  struct tm t = {0};

  t.tm_min = 14;
  int before = mock_outbox_sends;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);

  t.tm_min = 30;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(before + 1, mock_outbox_sends);

  t.tm_min = 0;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(before + 2, mock_outbox_sends);
}
```

Register both in `main()` after `RUN_TEST(test_inbox_units_change_should_trigger_weather_refetch);`.

- [ ] **Step 3: Run to verify failure**

Run: `make test`
Expected: `test_tick_handler_should_request_weather_on_the_half_hour_edge` FAILS (no requests happen yet). `test_update_time_should_never_request_weather` may pass pre-fix outside :00/:30 wall-clock minutes — the tick test is the deterministic red.

- [ ] **Step 4: Move the trigger**

In `src/c/main.c`, delete from `update_time()`:

```c
  // Request weather update every 30 minutes
  if (tick_time->tm_min % 30 == 0) {
    request_weather();
  }
```

and change `tick_handler` to:

```c
static void tick_handler(struct tm* tick_time, TimeUnits units_changed) {
  update_time();
  // Fetch on the tick edge only. update_time() also runs from
  // inbox_received_callback; triggering there too re-armed the fetch on
  // every reply for the whole of minutes :00/:30.
  if (tick_time->tm_min % 30 == 0) {
    request_weather();
  }
}
```

The launch-time cache-miss fetch in `init()` stays. No JS changes: the `appmessage` listener keeps always-fetch semantics but is now only poked once per half hour.

- [ ] **Step 5: Run tests**

Run: `make test`
Expected: PASS, all tests.

- [ ] **Step 6: Commit**

```bash
jj desc -m "perf: edge-trigger weather requests to kill the :00/:30 fetch loop"
jj new -m "perf: read the clock once per tick, format dates on change"
```

---

### Task 2: One clock read per tick; date formatting on change only

`update_time()`, `apply_theme()`, and `update_date_info()` each call `localtime()`. The two date strings are reformatted every tick and change at midnight or on a settings push. Consolidate to one read; guard the formatters on `(tm_yday, date_format, short_date_format, dow_position)`.

**Files:**
- Modify: `src/c/theme.h:27`, `src/c/theme.c` (`apply_theme`)
- Modify: `src/c/main.c` (`update_time`, `init`; delete `update_date_info`)
- Test: `test/test_watchface.c`

**Interfaces:**
- Consumes: existing formatters in `src/c/data.h`.
- Produces: **`apply_theme(struct tm* tick_time)`** — new signature; every later task's test helper depends on it. `update_date_info()` no longer exists.

- [ ] **Step 1: Write the tests**

```c
void test_update_time_should_reformat_the_date_when_settings_change(void) {
  s_settings_dow_position = DOW_BEFORE;
  update_time();  // primes the format cache for (today, these settings)
  char with_dow[64];
  strcpy(with_dow, s_date_display);

  s_settings_dow_position = DOW_HIDDEN;
  update_time();
  TEST_ASSERT_TRUE(strcmp(with_dow, s_date_display) != 0);

  s_settings_dow_position = DOW_BEFORE;
  update_time();
  TEST_ASSERT_EQUAL_STRING(with_dow, s_date_display);
}

void test_update_time_should_keep_date_output_when_nothing_changes(void) {
  update_time();
  char once[64];
  char once_short[16];
  strcpy(once, s_date_display);
  strcpy(once_short, s_short_date_display);
  update_time();
  TEST_ASSERT_EQUAL_STRING(once, s_date_display);
  TEST_ASSERT_EQUAL_STRING(once_short, s_short_date_display);
}
```

Register both in `main()`. (These exercise observable behavior — the format calls themselves are libc and cannot be counted in the host harness; the existing formatter suite plus these cover regression.)

- [ ] **Step 2: Run to verify they pass as characterization baseline**

Run: `make test`
Expected: PASS both (they describe behavior, which the refactor must preserve).

- [ ] **Step 3: Change `apply_theme` signature**

In `src/c/theme.h`:

```c
void apply_theme(struct tm* tick_time);
```

In `src/c/theme.c`:

```c
void apply_theme(struct tm* tick_time) {
  s_active_theme = determine_theme(s_settings_theme, tick_time->tm_hour);

  if (s_main_window) {
    window_set_background_color(s_main_window, s_active_theme->center_bg);
  }
}
```

- [ ] **Step 4: Rewrite `update_time`'s head and drop `update_date_info`**

In `src/c/main.c`, delete the whole `update_date_info()` function and replace `update_time()` with:

```c
void update_time() {
  time_t temp = time(NULL);
  struct tm* tick_time = localtime(&temp);

  apply_theme(tick_time);

  // The theme can change while the face is open (Auto crossing 06:00/14:00/22:00,
  // or a settings push); the time layer keeps its load-time color unless
  // re-applied here. The date is canvas-drawn, so it follows on redraw.
  if (s_time_layer) text_layer_set_text_color(s_time_layer, s_active_theme->text_primary);

  s_beats = compute_beats(temp);

  static char s_time_buffer[8];
  strftime(s_time_buffer, sizeof(s_time_buffer), clock_is_24h_style() ? "%H:%M" : "%I:%M",
           tick_time);

  char* time_str = s_time_buffer;
  if (!clock_is_24h_style() && s_time_buffer[0] == '0') {
    time_str++;  // strip leading zero
  }
  text_layer_set_text(s_time_layer, time_str);

  // The date changes at midnight and on settings pushes only; reformat then,
  // not per tick. Consecutive days never share a tm_yday.
  static int s_fmt_yday = -1;
  static int s_fmt_format = -1;
  static int s_fmt_short = -1;
  static int s_fmt_dow = -1;
  if (tick_time->tm_yday != s_fmt_yday || s_settings_date_format != s_fmt_format ||
      s_settings_short_date_format != s_fmt_short || s_settings_dow_position != s_fmt_dow) {
    format_date_string(s_settings_date_format, s_settings_short_date_format,
                       s_settings_dow_position, tick_time, s_date_display, sizeof(s_date_display));
    format_short_date_string(s_settings_short_date_format, s_settings_dow_position, tick_time,
                             s_short_date_display, sizeof(s_short_date_display));
    s_date_day = tick_time->tm_mday;
    s_fmt_yday = tick_time->tm_yday;
    s_fmt_format = s_settings_date_format;
    s_fmt_short = s_settings_short_date_format;
    s_fmt_dow = s_settings_dow_position;
  }

  update_health_info();
  refresh_complications();
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}
```

(Task 3 rewrites the last three lines; keep them for now.)

In `init()`, replace `apply_theme();` with:

```c
  time_t now = time(NULL);
  apply_theme(localtime(&now));
```

- [ ] **Step 5: Fix remaining compile fallout**

Run: `make test`
The compiler will list every remaining `apply_theme()` no-arg call site (tests added by later tasks use the helper defined in Task 3). Fix each to:

```c
  time_t now = time(NULL);
  apply_theme(localtime(&now));
```

Expected after fixes: PASS.

- [ ] **Step 6: Commit**

```bash
jj desc -m "perf: read the clock once per tick, format dates on change"
jj new -m "perf: schedule renders only when displayed state changes"
```

---

### Task 3: Snapshot-gated renders; fold in `HealthEventSignificantUpdate`

All event handlers currently end in `refresh_complications()` + unconditional `layer_mark_dirty`. Replace with one gate: build a snapshot of everything on screen; if it matches what the last pass drew, schedule nothing. Also fixes a latent gap: `health_handler` ignores `HealthEventSignificantUpdate` ("applib health cache invalidated"); harmless only while every read is per-tick, so handle it here, before any later work leans on events.

**Files:**
- Modify: `src/c/drawing.c` (replace `refresh_complications` with `request_ui_redraw`; move slot text buffers to file scope)
- Modify: `src/c/drawing.h`
- Modify: `src/c/main.c` (`battery_callback`, `handle_bluetooth`, `health_handler`, `update_time` tail)
- Modify: `src/c/messaging.c` (`inbox_received_callback` tail)
- Modify: `test/pebble_mock.c`, `test/pebble.h`
- Test: `test/test_watchface.c`

**Interfaces:**
- Consumes: `get_source_data`, `get_source_color`, `canvas_drawer` (static, same file), `apply_theme(struct tm*)` from Task 2.
- Produces: **`void request_ui_redraw(void)`** in drawing.h — the only render-scheduling entry point. No other code calls `layer_mark_dirty` after this task except the tick's clock `text_layer_set_text` (which guarantees the floor). Also **`void reset_ui_snapshot(void)`** for the window-load path.

Mock changes (test/pebble_mock.c; declare all `extern` in test/pebble.h next to the existing mock helpers):

```c
int mock_mark_dirty_count = 0;
void layer_mark_dirty(Layer* layer) {
  mock_mark_dirty_count++;
}

int mock_set_text_count = 0;
void text_layer_set_text(TextLayer* text_layer, const char* text) {
  mock_set_text_count++;
}

int mock_set_text_color_count = 0;
void text_layer_set_text_color(TextLayer* text_layer, GColor color) {
  mock_set_text_color_count++;
}

// Return sentinels instead of NULL so layer-attached code paths are testable.
static char mock_layer_storage[8];
static int mock_layers_given = 0;
Layer* layer_create(GRect frame) {
  (void)frame;
  return (Layer*)&mock_layer_storage[mock_layers_given++ % 8];
}

static char mock_text_layer_storage[8];
static int mock_text_layers_given = 0;
TextLayer* text_layer_create(GRect frame) {
  (void)frame;
  return (TextLayer*)&mock_text_layer_storage[mock_text_layers_given++ % 8];
}
```

(These replace the existing `layer_create`/`text_layer_create`/`layer_mark_dirty`/`text_layer_set_text`/`text_layer_set_text_color` stubs.)

- [ ] **Step 1: Write the failing tests**

Add near the top of the test section:

```c
// apply_theme() takes a tm after the clock-consolidation refactor; keep the
// plumbing in one place.
static void test_apply_theme(void) {
  time_t now = time(NULL);
  apply_theme(localtime(&now));
}

void test_render_gate_should_go_silent_when_nothing_changes(void) {
  main_window_load(NULL);
  memset(&s_shown_ui, 0, sizeof(s_shown_ui));
  memset(s_slot_text, 0, sizeof(s_slot_text));
  mock_mark_dirty_count = 0;
  mock_set_text_count = 0;

  request_ui_redraw();  // cold: zeroed snapshot differs, applies once
  int marks = mock_mark_dirty_count;
  int texts = mock_set_text_count;

  request_ui_redraw();
  request_ui_redraw();
  TEST_ASSERT_EQUAL_INT(marks, mock_mark_dirty_count);
  TEST_ASSERT_EQUAL_INT(texts, mock_set_text_count);
}

void test_render_gate_should_ignore_changes_nobody_displays(void) {
  main_window_load(NULL);
  memset(&s_shown_ui, 0, sizeof(s_shown_ui));
  request_ui_redraw();
  int marks = mock_mark_dirty_count;

  s_battery_level = 5;  // the default layout shows no battery slot
  request_ui_redraw();
  TEST_ASSERT_EQUAL_INT(marks, mock_mark_dirty_count);
  s_battery_level = 100;
}

void test_render_gate_should_pass_displayed_changes_through(void) {
  main_window_load(NULL);
  memset(&s_shown_ui, 0, sizeof(s_shown_ui));
  request_ui_redraw();
  int marks = mock_mark_dirty_count;
  int texts = mock_set_text_count;

  s_step_count = 4321;  // bottom-left slot shows STEPS by default
  request_ui_redraw();
  TEST_ASSERT_TRUE(mock_mark_dirty_count > marks);
  TEST_ASSERT_TRUE(mock_set_text_count > texts);
  s_step_count = -1;
}

void test_render_gate_should_notice_bar_slot_changes(void) {
  // Regression: the bar sources have no get_source_data case, so a snapshot
  // keyed on slot->source records empty text forever and the bar freezes
  // between minute ticks. snapshot_source() maps them to their plain source.
  main_window_load(NULL);
  s_complication_slots[2].source = DATA_SOURCE_STEPS_BAR;
  s_step_count = 1000;
  memset(&s_shown_ui, 0, sizeof(s_shown_ui));
  request_ui_redraw();
  int marks = mock_mark_dirty_count;

  s_step_count = 6000;  // same "--"/number shape, different fill and reading
  request_ui_redraw();
  TEST_ASSERT_TRUE(mock_mark_dirty_count > marks);

  s_step_count = -1;
  s_complication_slots[2].source = DATA_SOURCE_STEPS;
}

void test_render_gate_should_reapply_colors_on_theme_change(void) {
  main_window_load(NULL);
  memset(&s_shown_ui, 0, sizeof(s_shown_ui));
  test_apply_theme();
  request_ui_redraw();
  mock_set_text_color_count = 0;
  int texts = mock_set_text_count;
  int marks = mock_mark_dirty_count;

  s_active_theme = &s_theme_shadow;  // e.g. Auto crossing 22:00
  request_ui_redraw();
  TEST_ASSERT_TRUE(mock_mark_dirty_count > marks);   // canvas frames recolor
  TEST_ASSERT_TRUE(mock_set_text_color_count > 0);   // slots recolor…
  TEST_ASSERT_EQUAL_INT(texts, mock_set_text_count); // …but no string changed
  test_apply_theme();
}

void test_battery_callback_should_coalesce_unchanged_levels(void) {
  main_window_load(NULL);
  memset(&s_shown_ui, 0, sizeof(s_shown_ui));
  battery_callback((BatteryChargeState){.charge_percent = 100});
  int marks = mock_mark_dirty_count;
  battery_callback((BatteryChargeState){.charge_percent = 100});
  TEST_ASSERT_EQUAL_INT(marks, mock_mark_dirty_count);
}
```

Register all six in `main()`.

- [ ] **Step 2: Make the slot config a per-test precondition**

From this task on, `update_health_info()` (Task 4) and `request_ui_redraw()` both read `s_complication_slots`. The existing slot-mutating inbox tests restore the layout after themselves, but every new test here and in Task 4 would need the same save/restore dance — and one forgotten restore silently makes the whole suite order-dependent. Centralize the reset instead; add to `setUp()` in `test/test_watchface.c`, alongside the existing global resets:

```c
  // Slot contents gate health reads and the render snapshot, and the inbox
  // tests rewrite them; restore the shipped layout before every test.
  const ComplicationDataSource defaults[NUM_SLOTS] = {
      DATA_SOURCE_WEATHER, DATA_SOURCE_SLEEP,     DATA_SOURCE_STEPS,
      DATA_SOURCE_HEART_RATE, DATA_SOURCE_BLUETOOTH, DATA_SOURCE_FULL_DATE};
  for (int i = 0; i < NUM_SLOTS; i++) {
    s_complication_slots[i].source = defaults[i];
  }
```

(Must match the table in `src/c/data.c`. The inbox tests' own restores become redundant but harmless — leave them; minimal diff.)

- [ ] **Step 3: Run to verify failure**

Run: `make test`
Expected: FAIL — `request_ui_redraw` undefined (compile error is the red here; if you prefer a runtime red, stub it first, but the compile error suffices to anchor the task).

- [ ] **Step 4: Implement the gate**

In `src/c/drawing.c`, delete `refresh_complications()` and add:

```c
typedef struct {
  const WatchTheme* theme;
  ComplicationDataSource source[NUM_SLOTS];
  char text[NUM_SLOTS][40];
  int percent[NUM_SLOTS];
} UiSnapshot;

// What the last scheduled render will draw. Compared whole; build_snapshot
// memsets first so padding and string slack can't poison the memcmp.
static UiSnapshot s_shown_ui;

// Backing for the slot TextLayers: text_layer_set_text keeps the pointer, so
// the strings must outlive the call.
static char s_slot_text[NUM_SLOTS][40];

// The bar sources have no get_source_data case of their own — their drawers
// read the plain counterpart (draw_steps_bar_complication passes
// DATA_SOURCE_STEPS, draw_battery_bar_complication passes
// DATA_SOURCE_BATTERY). Snapshotting slot->source for a bar would record
// empty text and percent 0 forever, so a bar would never register a change.
static ComplicationDataSource snapshot_source(ComplicationDataSource source) {
  switch (source) {
    case DATA_SOURCE_STEPS_BAR:
      return DATA_SOURCE_STEPS;
    case DATA_SOURCE_BATTERY_BAR:
      return DATA_SOURCE_BATTERY;
    default:
      return source;
  }
}

// Everything on screen is derived from slot contents and the theme: values
// and bar fills come out of get_source_data's text and percent, bands and
// accents out of theme colors and the thresholds it reads. A change nothing
// displays (e.g. battery with no battery slot) never shows up here — which
// is exactly the gate.
static void build_snapshot(UiSnapshot* s) {
  memset(s, 0, sizeof(*s));
  s->theme = s_active_theme;
  for (int i = 0; i < NUM_SLOTS; i++) {
    ComplicationSlot* slot = &s_complication_slots[i];
    // The label and frame follow the configured source; the value follows
    // whatever the drawer actually reads.
    s->source[i] = slot->source;
    get_source_data(snapshot_source(slot->source), s->text[i], sizeof(s->text[i]),
                    &s->percent[i]);
  }
}

// Schedules a render only when what the screen shows has changed. The tick's
// clock set_text already guarantees one full-tree render a minute (PebbleOS
// marks carry no region and re-run every visible layer's update_proc), so
// this gate is about events: health, battery, Bluetooth, inbox.
void request_ui_redraw(void) {
  UiSnapshot now;
  build_snapshot(&now);
  if (memcmp(&now, &s_shown_ui, sizeof(now)) == 0) return;

  for (int i = 0; i < NUM_SLOTS; i++) {
    ComplicationSlot* slot = &s_complication_slots[i];
    if (!slot->layer) continue;
    bool text_backed = slot->source != DATA_SOURCE_EMPTY && !canvas_drawer(slot->source);
    layer_set_hidden(text_layer_get_layer(slot->layer), !text_backed);
    if (!text_backed) continue;
    if (strcmp(now.text[i], s_slot_text[i]) != 0 || now.source[i] != s_shown_ui.source[i]) {
      strcpy(s_slot_text[i], now.text[i]);
      text_layer_set_text(slot->layer, s_slot_text[i]);
    }
    // Colors re-apply even when the string is unchanged (theme rollover);
    // text_layer_set_text_color early-returns when nothing changed.
#if defined(PBL_COLOR)
    text_layer_set_text_color(slot->layer, get_source_color(slot->source));
#else
    text_layer_set_text_color(slot->layer, s_active_theme->text_primary);
#endif
  }

  s_shown_ui = now;
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}
```

In `src/c/drawing.h`, replace `void refresh_complications();` with:

```c
// Build a snapshot of all displayed state and schedule the one full-tree
// render iff it differs from what the last render drew. Safe to call from
// any event handler; no-ops when nothing visible changed.
void request_ui_redraw(void);

// Clear the snapshot and layer-text caches. main_window_load calls this:
// fresh layers hold no text, so the next request_ui_redraw() must apply
// unconditionally rather than match a previous layer tree's snapshot.
void reset_ui_snapshot(void);
```

In `src/c/main.c`:

```c
static void battery_callback(BatteryChargeState state) {
  s_battery_level = state.charge_percent;
  request_ui_redraw();
}
```

`handle_bluetooth`: replace `refresh_complications(); if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);` with `request_ui_redraw();` (vibe logic untouched).

```c
static void health_handler(HealthEventType event, void* context) {
  // SignificantUpdate = the applib health cache was invalidated (day
  // rollover, subscribe); harmless-while-ticking, and it costs one refresh.
  if (event == HealthEventMovementUpdate || event == HealthEventHeartRateUpdate ||
      event == HealthEventSleepUpdate || event == HealthEventSignificantUpdate) {
    update_health_info();
    request_ui_redraw();
  }
}
```

Invalidate the snapshot on window load. Fresh layers hold no text, and a matching stale snapshot would skip the `set_text` that fills them — and the clock guard below has the identical hazard with its own static. Watchface windows effectively never unload in-process, but the armor is three lines.

In `src/c/main.c`, promote the guard string out of `update_time()` to file scope so the load handler can reach it:

```c
// What the clock layer currently says (guards the unconditional
// text_layer_set_text dirty-mark, below). Cleared on window load.
static char s_shown_time[8] = "";
```

In `src/c/drawing.c`, after `build_snapshot`:

```c
void reset_ui_snapshot(void) {
  memset(&s_shown_ui, 0, sizeof(s_shown_ui));
  memset(s_slot_text, 0, sizeof(s_slot_text));
}
```

In `main_window_load`, after the slot loop:

```c
  // Fresh layers hold no text yet: the next request_ui_redraw()/update_time()
  // must apply unconditionally, not match a previous layer tree's snapshot.
  reset_ui_snapshot();
  s_shown_time[0] = '\0';
```

(The tests' own `memset`s on `s_shown_ui`/`s_slot_text` keep compiling — the test TU includes drawing.c — but become belt-and-braces rather than load-bearing.)

In `update_time()`, replace the last two lines (`refresh_complications(); if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);`) with:

```c
  request_ui_redraw();
```

and guard the clock's own `text_layer_set_text` so it is the *only* unconditional render source, and only on a real minute change:

```c
  // Guards the unconditional dirty-mark in text_layer_set_text: an unchanged
  // string here would schedule a render and defeat the gate on every inbox
  // message. On a MINUTE_UNIT tick the string always differs — the 1440/day
  // floor is unaffected. s_shown_time is file-scope (see main_window_load).
  if (strcmp(time_str, s_shown_time) != 0) {
    strncpy(s_shown_time, time_str, sizeof(s_shown_time) - 1);
    text_layer_set_text(s_time_layer, time_str);
  }
```

(Tick cost: one extra `get_source_data` × 6 + a ~300 B memcmp; the clock's set_text schedules the render either way. When a BEATS slot is configured the snapshot diffs every tick — the mark coalesces with the clock's, zero extra renders.)

In `src/c/messaging.c`, `inbox_received_callback`: delete the trailing `if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);` — the `update_time()` call immediately above routes through `request_ui_redraw()`.

- [ ] **Step 5: Run tests**

Run: `make test`
Expected: PASS, all tests (the six new ones green; no existing test touched `refresh_complications` directly, but if the compiler disagrees, reroute to `request_ui_redraw`).

- [ ] **Step 6: Commit**

```bash
jj desc -m "perf: schedule renders only when displayed state changes"
jj new -m "perf: skip health metrics no slot displays"
```

---

### Task 4: Skip health syscalls for metrics no slot displays

`update_health_info()` runs 4 accessibility checks + 3 sums per tick and per event. `health_service_metric_accessible` is a real syscall path (shares `prv_get_range_and_daily_history`), not a flag check. Gate each metric on slot usage. Masks stay live per tick for displayed metrics — caching them is unsafe (no event fires on permission toggles; verified in pebbleos) and ISSUES.md documents per-tick re-reads as the accepted design.

**Files:**
- Modify: `src/c/main.c` (`update_health_info`)
- Modify: `test/pebble_mock.c`, `test/pebble.h`
- Test: `test/test_watchface.c`

**Interfaces:**
- Consumes: `s_complication_slots` (src/c/data.h), mock counters below.
- Produces: `static bool any_slot_is_one_of(const ComplicationDataSource* sources, int count)` in main.c. Static, but reachable from tests (the test TU includes main.c).

Mock counters (test/pebble_mock.c; `extern` in test/pebble.h):

```c
int mock_health_accessible_count = 0;
int mock_health_sum_today_count = 0;
int mock_health_peek_count = 0;
```

Increment inside `health_service_metric_accessible`, `health_service_sum_today`, `health_service_peek_current_value` respectively.

- [ ] **Step 1: Write the failing tests**

```c
static void save_slots(ComplicationDataSource* saved) {
  for (int i = 0; i < NUM_SLOTS; i++) saved[i] = s_complication_slots[i].source;
}

static void restore_slots(const ComplicationDataSource* saved) {
  for (int i = 0; i < NUM_SLOTS; i++) s_complication_slots[i].source = saved[i];
}

void test_update_health_info_should_do_nothing_with_no_health_slots(void) {
  ComplicationDataSource saved[NUM_SLOTS];
  save_slots(saved);
  ComplicationDataSource weather_free[NUM_SLOTS] = {DATA_SOURCE_DATE, DATA_SOURCE_BLUETOOTH,
                                                    DATA_SOURCE_BEATS, DATA_SOURCE_SHORT_DATE,
                                                    DATA_SOURCE_AQI_UV, DATA_SOURCE_FULL_DATE};
  restore_slots(weather_free);

  mock_health_accessible_count = 0;
  mock_health_sum_today_count = 0;
  mock_health_peek_count = 0;
  update_health_info();
  TEST_ASSERT_EQUAL_INT(0, mock_health_accessible_count);
  TEST_ASSERT_EQUAL_INT(0, mock_health_sum_today_count);
  TEST_ASSERT_EQUAL_INT(0, mock_health_peek_count);

  restore_slots(saved);
}

void test_update_health_info_should_read_only_displayed_metrics(void) {
  ComplicationDataSource saved[NUM_SLOTS];
  save_slots(saved);
  ComplicationDataSource only_steps[NUM_SLOTS] = {DATA_SOURCE_STEPS, DATA_SOURCE_BLUETOOTH,
                                                  DATA_SOURCE_BEATS, DATA_SOURCE_SHORT_DATE,
                                                  DATA_SOURCE_DATE, DATA_SOURCE_FULL_DATE};
  restore_slots(only_steps);

  mock_health_accessible_count = 0;
  mock_health_sum_today_count = 0;
  mock_health_peek_count = 0;
  update_health_info();
  // Steps alone: one accessibility check, one sum; no sleep/active/HR reads.
  TEST_ASSERT_EQUAL_INT(1, mock_health_accessible_count);
  TEST_ASSERT_EQUAL_INT(1, mock_health_sum_today_count);
  TEST_ASSERT_EQUAL_INT(0, mock_health_peek_count);

  restore_slots(saved);
}

void test_undisplayed_health_metrics_should_read_as_no_data(void) {
  ComplicationDataSource saved[NUM_SLOTS];
  save_slots(saved);
  ComplicationDataSource weather_free[NUM_SLOTS] = {DATA_SOURCE_DATE, DATA_SOURCE_BLUETOOTH,
                                                    DATA_SOURCE_BEATS, DATA_SOURCE_SHORT_DATE,
                                                    DATA_SOURCE_AQI_UV, DATA_SOURCE_FULL_DATE};
  restore_slots(weather_free);

  s_step_count = 4321;
  update_health_info();
  TEST_ASSERT_EQUAL_INT(-1, s_step_count);

  restore_slots(saved);
}
```

Register all three in `main()`.

- [ ] **Step 2: Run to verify failure**

Run: `make test`
Expected: FAIL on counts (`update_health_info` reads everything today).

- [ ] **Step 3: Gate the metrics**

In `src/c/main.c`:

```c
static bool any_slot_is_one_of(const ComplicationDataSource* sources, int count) {
  for (int i = 0; i < NUM_SLOTS; i++) {
    for (int j = 0; j < count; j++) {
      if (s_complication_slots[i].source == sources[j]) return true;
    }
  }
  return false;
}
```

Then restructure `update_health_info()` (inside the existing `#if defined(PBL_HEALTH)`):

```c
static void update_health_info() {
#if defined(PBL_HEALTH)
  time_t start = time_start_of_today();
  time_t end = time(NULL);

  // Each read is a real syscall; skip metrics nothing displays. Values fall
  // back to their sentinels so a later slot assignment never shows stale
  // data — the tick that follows the settings push refills them.
  if (any_slot_is_one_of((ComplicationDataSource[]){DATA_SOURCE_STEPS, DATA_SOURCE_STEPS_BAR},
                         2)) {
    HealthServiceAccessibilityMask step_mask =
        health_service_metric_accessible(HealthMetricStepCount, start, end);
    if (step_mask & HealthServiceAccessibilityMaskAvailable) {
      s_step_count = (int)health_service_sum_today(HealthMetricStepCount);
      s_step_goal = 10000;
    } else {
      s_step_count = -1;
    }
  } else {
    s_step_count = -1;
  }

  if (any_slot_is_one_of((ComplicationDataSource[]){DATA_SOURCE_SLEEP}, 1)) {
    HealthServiceAccessibilityMask sleep_mask =
        health_service_metric_accessible(HealthMetricSleepSeconds, start, end);
    if (sleep_mask & HealthServiceAccessibilityMaskAvailable) {
      s_sleep_seconds = (int)health_service_sum_today(HealthMetricSleepSeconds);
    } else {
      s_sleep_seconds = -1;
    }
  } else {
    s_sleep_seconds = -1;
  }

  if (any_slot_is_one_of((ComplicationDataSource[]){DATA_SOURCE_ACTIVE_MINUTES}, 1)) {
    HealthServiceAccessibilityMask active_mask =
        health_service_metric_accessible(HealthMetricActiveSeconds, start, end);
    if (active_mask & HealthServiceAccessibilityMaskAvailable) {
      s_active_minutes = (int)(health_service_sum_today(HealthMetricActiveSeconds) / 60);
    } else {
      s_active_minutes = 0;
    }
  } else {
    s_active_minutes = 0;
  }

  // Heart rate accessibility must be checked at an instant, not over a day
  // range — the range form reports the metric unavailable and BPM never shows.
  if (any_slot_is_one_of((ComplicationDataSource[]){DATA_SOURCE_HEART_RATE}, 1)) {
    HealthServiceAccessibilityMask hr_mask =
        health_service_metric_accessible(HealthMetricHeartRateBPM, end, end);
    if (hr_mask & HealthServiceAccessibilityMaskAvailable) {
      HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
      s_heart_rate = (int)hr;
    } else {
      s_heart_rate = 0;
    }
  } else {
    s_heart_rate = 0;
  }
#else
  s_step_count = -1;
  s_sleep_seconds = -1;
  s_heart_rate = 0;
  s_active_minutes = 0;
#endif
}
```

Then correct `ISSUES.md`, whose BPM section asserts "health data is re-read every tick and on `HealthEventHeartRateUpdate`" — after this task that holds for displayed metrics only. Change that clause to "health data for every displayed metric is re-read every tick and on `HealthEventHeartRateUpdate`". The rest of the section (why the filtered value and the 10-minute sample period are deliberate) stands unchanged and is still a constraint on this plan.

- [ ] **Step 4: Run tests**

Run: `make test`
Expected: PASS. The pre-existing `test_update_health_info_should_read_heart_rate` still passes — the default layout includes an HR slot, and Task 3 Step 2 now guarantees that layout per test.

- [ ] **Step 5: Commit**

```bash
jj desc -m "perf: skip health metrics no slot displays"
jj new -m "perf: drop the duplicate full-screen fill"
```

---

### Task 5: Drop the duplicate full-screen fill; establish the screenshot harness

Every render pass paints the window twice: the window root layer fills its bounds with `window->background_color` (pebbleos `window_do_layer_update_proc`, window.c:37 — unconditional when the color isn't transparent), then `canvas_update_proc` fills the same bounds with the same color (`apply_theme` keeps the window background at `center_bg`). Delete the canvas fill.

**Files:**
- Modify: `src/c/drawing.c` (`canvas_update_proc`)
- Test: emulator pixel-diff (commands below); no unit test — the mock records no raster.

**Interfaces:**
- Consumes: nothing new.
- Produces: the deterministic slot config and crop-compare workflow that Tasks 6 and 7 reuse.

- [ ] **Step 1: Capture the before image deterministically**

The emulator has no health data and its JS side may or may not resolve weather; pin every slot to a source that is constant within a working session, including a battery bar (exercises the shade runs for later tasks):

```bash
cat > /tmp/perf-static-slots.json <<'EOF'
{"SLOT_1": 7, "SLOT_2": 9, "SLOT_3": 1, "SLOT_4": 25, "SLOT_5": 18, "SLOT_6": 23}
EOF
source pebble-env/bin/activate
pebble build && pebble install --emulator emery
pebble emu-app-config --emulator emery --file /tmp/perf-static-slots.json
sleep 3
pebble screenshot --emulator emery /tmp/perf-before.png
```

(SLOT_1=DATE, SLOT_2=BLUETOOTH, SLOT_3=STEPS, SLOT_4=BATTERY_BAR, SLOT_5=AQI_UV, SLOT_6=FULL_DATE — enum values from src/c/data.h. If the flag differs in this SDK build, `pebble emu-app-config --help`.)

Crop out the time window (y 50–136 — the clock digits validate nothing here and change every minute):

```bash
convert /tmp/perf-before.png -crop 200x50+0+0 +repage /tmp/before-top.png
convert /tmp/perf-before.png -crop 200x91+0+137 +repage /tmp/before-bot.png
```

(Requires ImageMagick `convert`/`compare`.)

- [ ] **Step 2: Remove the fill**

In `src/c/drawing.c`, `canvas_update_proc`, replace:

```c
void canvas_update_proc(Layer* layer, GContext* ctx) {
  GRect bounds = layer_get_bounds(layer);

  // Clean background
  graphics_context_set_fill_color(ctx, s_active_theme->center_bg);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
```

with:

```c
void canvas_update_proc(Layer* layer, GContext* ctx) {
  // No background fill: the window root layer fills the whole frame with
  // window->background_color on every render pass (PebbleOS
  // window_do_layer_update_proc), and apply_theme() keeps it at center_bg.
  (void)layer;
```

- [ ] **Step 3: Pixel-diff**

```bash
pebble build && pebble install --emulator emery
pebble emu-app-config --emulator emery --file /tmp/perf-static-slots.json
sleep 3
pebble screenshot --emulator emery /tmp/perf-after.png
convert /tmp/perf-after.png -crop 200x50+0+0 +repage /tmp/after-top.png
convert /tmp/perf-after.png -crop 200x91+0+137 +repage /tmp/after-bot.png
compare -metric AE /tmp/before-top.png /tmp/after-top.png null: ; echo "top AE=$?"
compare -metric AE /tmp/before-bot.png /tmp/after-bot.png null: ; echo "bot AE=$?"
```

Expected: both metrics print `0`. (Avoid crossing a wall-clock :00/:30 boundary between the two screenshots; AQI_UV shows `--` on the emulator so network state doesn't matter.)

- [ ] **Step 4: Host tests**

Run: `make test`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
jj desc -m "perf: drop the duplicate full-screen fill"
jj new -m "perf: draw runs with Fill overflow instead of WordWrap"
```

---

### Task 6: `GTextOverflowModeWordWrap` → `GTextOverflowModeFill`

Every `draw_run`, shade run, and window title pays a line-breaking layout pass for single-line runs drawn into rects sized to exact glyph cells. Worse, it's a latent bug: runs containing spaces ("CLD / 72F", the padded bar value) can wrap onto an invisible second line if a rect is ever 1 px short. Fill is single-line by construction.

**Files:**
- Modify: `src/c/drawing.c` (`draw_run`, `draw_shade_run`, `draw_ascii_window` title)
- Modify: `test/pebble_mock.c`, `test/pebble.h`
- Test: `test/test_watchface.c` + Task 5's crop-compare

**Interfaces:**
- Consumes: `test_apply_theme()` (Task 3), Task 5's snapshot harness.
- Produces: mock counter `mock_wordwrap_calls`.

Mock (test/pebble_mock.c; `extern` in test/pebble.h):

```c
int mock_wordwrap_calls = 0;
void graphics_draw_text(GContext* ctx, const char* text, GFont font, GRect box,
                        GTextOverflowMode overflow_mode, GTextAlignment alignment,
                        GContext* layout_cache) {
  if (overflow_mode == GTextOverflowModeWordWrap) mock_wordwrap_calls++;
}
```

- [ ] **Step 1: Write the failing test**

```c
void test_canvas_procs_should_never_word_wrap(void) {
  test_apply_theme();
  s_complication_slots[3].source = DATA_SOURCE_BATTERY_BAR;  // exercise the shade runs too
  mock_wordwrap_calls = 0;
  canvas_update_proc(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(0, mock_wordwrap_calls);
  s_complication_slots[3].source = DATA_SOURCE_HEART_RATE;
}
```

Register in `main()`.

- [ ] **Step 2: Run to verify failure**

Run: `make test`
Expected: FAIL — `mock_wordwrap_calls` > 0.

- [ ] **Step 3: Switch the overflow mode**

In `src/c/drawing.c`: in `draw_run`, `draw_shade_run`, and the title `graphics_draw_text` inside `draw_ascii_window`, change `GTextOverflowModeWordWrap` to `GTextOverflowModeFill`. Three call sites, no other changes.

- [ ] **Step 4: Run tests + pixel-diff**

Run: `make test` — Expected: PASS.
Then repeat Task 5 Step 1's before-image flow followed by Step 3's diff commands against a new before/after pair taken across this change (the static slot config persists in the emulator; re-apply `/tmp/perf-static-slots.json` after each install). Expected: both crops `AE=0`.

- [ ] **Step 5: Commit**

```bash
jj desc -m "perf: draw runs with Fill overflow instead of WordWrap"
jj new -m "perf: paint bar fills as rects, not glyph runs"
```

---

### Task 7: Paint the progress-bar fill as one `fill_rect`

`draw_shade_run` builds up to 32 UTF-8 block glyphs for what is visually a solid rectangle — per-glyph cache lookup and blit per cell, per bar, per render; two bar sources can be on screen at once. The track `░` is a dither pattern and stays a glyph run; only `█` becomes a rect.

**Where this task can fail.** The rect below is positioned at `band.origin.y` with height `VGA16_CELL_H`, i.e. it assumes the `█` ink box is the glyph cell centred in the value row. That is how `status_band_rect` was *designed* (see its comment — the band is a layout choice, inset from the frame), not something measured from the glyph: `graphics_draw_text` places glyphs by font ascent from the top of the text box, so the real ink box may sit a pixel or two off. Treat the geometry as a hypothesis that Step 5's pixel-diff tests. Horizontal identity is safe — `Px437_IBM_VGA_8x16` is a CP437 raster reproduction whose block glyphs tile seamlessly at the exact 8 px advance the layout already assumes (`LAYOUT_W` 184 = 23 cells).

**Files:**
- Modify: `src/c/drawing.c` (`draw_progress_bar`)
- Modify: `test/pebble_mock.c`, `test/pebble.h`
- Test: `test/test_watchface.c` + Task 5's crop-compare (the static config's battery bar is at 100%, fully filled)

**Interfaces:**
- Consumes: `status_band_rect` (static, same file), `test_apply_theme()`.
- Produces: `BAR_FILL`/`draw_shade_run`'s fill path disappears; `draw_shade_run` remains for the track.

Mock (test/pebble_mock.c; decls in test/pebble.h):

```c
#define MOCK_MAX_FILL_RECTS 128
GRect mock_fill_rects[MOCK_MAX_FILL_RECTS];
int mock_fill_rect_count = 0;
int mock_bar_glyph_calls = 0;

void mock_fill_rect_reset(void) {
  mock_fill_rect_count = 0;
}
```

In `graphics_fill_rect`, record:

```c
  if (mock_fill_rect_count < MOCK_MAX_FILL_RECTS) {
    mock_fill_rects[mock_fill_rect_count++] = rect;
  }
```

In `graphics_draw_text` (extended from Task 6), add:

```c
  if (strstr(text, "\xE2\x96\x88")) mock_bar_glyph_calls++;  // U+2588 FULL BLOCK
```

- [ ] **Step 1: Write the failing test**

```c
void test_battery_bar_should_paint_its_fill_as_one_rect(void) {
  test_apply_theme();
  s_complication_slots[3].source = DATA_SOURCE_BATTERY_BAR;
  s_battery_level = 100;  // full bar
  mock_fill_rect_reset();
  mock_bar_glyph_calls = 0;

  canvas_update_proc(NULL, NULL);

  // Expected fill: every bar cell, one cell high, aligned with the band.
  GRect band = status_band_rect(s_complication_slots[3].box_rect);
  int cells = band.size.w / VGA16_CHAR_W;
  int bar_cells = cells - BAR_VALUE_CELLS - 1;
  GRect row = GRect(band.origin.x + (band.size.w - cells * VGA16_CHAR_W) / 2,
                    s_complication_slots[3].box_rect.origin.y + VALUE_ROW_DY,
                    cells * VGA16_CHAR_W, VALUE_ROW_H);
  GRect expected = GRect(row.origin.x, band.origin.y, bar_cells * VGA16_CHAR_W, VGA16_CELL_H);

  bool found = false;
  for (int i = 0; i < mock_fill_rect_count; i++) {
    if (mock_fill_rects[i].origin.x == expected.origin.x &&
        mock_fill_rects[i].origin.y == expected.origin.y &&
        mock_fill_rects[i].size.w == expected.size.w &&
        mock_fill_rects[i].size.h == expected.size.h) {
      found = true;
    }
  }
  TEST_ASSERT_TRUE(found);
  TEST_ASSERT_EQUAL_INT(0, mock_bar_glyph_calls);  // no block glyphs for the fill

  s_complication_slots[3].source = DATA_SOURCE_HEART_RATE;
}
```

Register in `main()`.

- [ ] **Step 2: Run to verify failure**

Run: `make test`
Expected: FAIL — no such fill rect; `mock_bar_glyph_calls` > 0.

- [ ] **Step 3: Replace the fill path**

In `src/c/drawing.c`, `draw_progress_bar`, replace:

```c
  draw_shade_run(ctx, row, 0, filled, BAR_FILL, fill);
```

with:

```c
  // The fill is a solid block: one rect, not a glyph run. The track stays
  // glyphs — ░ is a dither pattern, not a color. Rect geometry is the ink box
  // the █ run occupied; pixel-identity is screenshot-gated, not assumed.
  if (filled > 0) {
    graphics_context_set_fill_color(ctx, fill);
    graphics_fill_rect(ctx, GRect(row.origin.x, band.origin.y, filled * VGA16_CHAR_W,
                                  VGA16_CELL_H),
                       0, GCornerNone);
  }
```

Leave the `draw_shade_run(ctx, row, filled, bar_cells - filled, BAR_TRACK, …)` call and the value run as they are. `BAR_FILL` becomes unused — delete the `#define BAR_FILL` line (keep the CP437 comment for `BAR_TRACK`).

- [ ] **Step 4: Run tests**

Run: `make test`
Expected: PASS.

- [ ] **Step 5: Pixel-diff**

Repeat the Task 5 before/after crop-compare across this change (battery bar pinned at 100% by the static config exercises the fill). Expected: both crops `AE=0`. If the bottom crop shows a non-zero AE, the rect doesn't match the █ ink box: localize with

```bash
compare -metric AE /tmp/before-bot.png /tmp/after-bot.png /tmp/bar-diff.png
convert /tmp/bar-diff.png -trim -format '%@\n' info:
```

and adjust the fill rect's origin/height to the inked box, recording the correction in the commit message. Do not accept a non-zero diff with a code tweak that changes appearance.

If two or three geometry corrections still leave `AE > 0`, **abandon this task** — revert to `draw_shade_run(ctx, row, 0, filled, BAR_FILL, fill)` and note in the plan that the fill is glyph-bound. It is the smallest saving in the pass (two bar slots at most, only while a bar is configured); the aesthetic is a stated project value and not worth trading for it.

- [ ] **Step 6: Commit**

```bash
jj desc -m "perf: paint bar fills as rects, not glyph runs"
jj new -m "perf: write persistence only when values change"
```

---

### Task 8: Compare-before-write for persistence

`save_weather_cache()` writes 5 keys per payload (~240/day; thousands during a pre-Task-1 loop episode — now capped by Task 1, still wasteful), and every settings tuple rewrites its key unconditionally. Skip writes whose value is unchanged. The timestamp key is always written: it's the freshness marker, and skipping it would age the cache and cause extra network fetches. No `PERSIST_KEY_*` changes.

**Files:**
- Modify: `src/c/messaging.c` (`save_weather_cache`, settings branches in `inbox_received_callback`)
- Modify: `test/pebble_mock.c`, `test/pebble.h`
- Test: `test/test_watchface.c`

**Interfaces:**
- Consumes: existing persist keys.
- Produces: `static void persist_write_int_if_changed(uint32_t key, int32_t value)` and `static void persist_write_string_if_changed(uint32_t key, const char* value)` in messaging.c.

Mock (test/pebble_mock.c; `extern` in test/pebble.h):

```c
int mock_persist_write_count = 0;
```

Increment at the top of `persist_write_int` and `persist_write_string`.

- [ ] **Step 1: Write the failing tests**

```c
void test_weather_cache_should_skip_rewrite_when_payload_is_unchanged(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_cstring(MESSAGE_KEY_WEATHER_COND, "SUN");
  inbox_received_callback(NULL, NULL);

  int writes = mock_persist_write_count;
  inbox_received_callback(NULL, NULL);  // identical payload
  // Only the timestamp is rewritten.
  TEST_ASSERT_EQUAL_INT(writes + 1, mock_persist_write_count);
}

void test_settings_message_should_not_rewrite_unchanged_keys(void) {
  mock_persist_reset();
  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SETTINGS_THEME, "2");
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(2, persist_read_int(PERSIST_KEY_SETTINGS_THEME));

  int writes = mock_persist_write_count;
  inbox_received_callback(NULL, NULL);  // same setting again
  TEST_ASSERT_EQUAL_INT(writes, mock_persist_write_count);
}
```

Register both in `main()`.

- [ ] **Step 2: Run to verify failure**

Run: `make test`
Expected: FAIL both (every key rewrites today).

- [ ] **Step 3: Implement the guards**

In `src/c/messaging.c`, add:

```c
static void persist_write_int_if_changed(uint32_t key, int32_t value) {
  if (!persist_exists(key) || persist_read_int(key) != value) {
    persist_write_int(key, value);
  }
}

static void persist_write_string_if_changed(uint32_t key, const char* value) {
  if (!persist_exists(key)) {
    persist_write_string(key, value);
    return;
  }
  char current[16];
  persist_read_string(key, current, sizeof(current));
  if (strcmp(current, value) != 0) {
    persist_write_string(key, value);
  }
}
```

In `save_weather_cache()`, use the two helpers for TEMP/COND/AQI/UV; keep the timestamp write unconditional:

```c
void save_weather_cache(void) {
  persist_write_int_if_changed(PERSIST_KEY_WEATHER_TEMP, s_weather_temp);
  persist_write_string_if_changed(PERSIST_KEY_WEATHER_COND, s_weather_cond);
  persist_write_int_if_changed(PERSIST_KEY_WEATHER_AQI, s_weather_aqi);
  persist_write_int_if_changed(PERSIST_KEY_WEATHER_UV, s_weather_uv);
  // Always: the timestamp is the freshness marker; skipping it would age the
  // cache and cost a network fetch on next launch.
  persist_write_int(PERSIST_KEY_WEATHER_TIMESTAMP, (int32_t)time(NULL));
}
```

In `inbox_received_callback`, wrap every settings `persist_write_int(...)` with `persist_write_int_if_changed(...)` (THEME, UNITS, DATE_FORMAT, SHORT_DATE, DOW, SLOT_1–SLOT_6).

- [ ] **Step 4: Run tests**

Run: `make test`
Expected: PASS, including the pre-existing `test_inbox_should_parse_weather_payload_and_persist` (first payload writes normally).

- [ ] **Step 5: Commit**

```bash
jj desc -m "perf: write persistence only when values change"
jj new -m "perf: fetch AQI and forecast in parallel"
```

---

### Task 9: Parallel weather + AQI requests

The AQI request is issued inside the forecast's `onload`; the two hosts are independent and both need only lat/lon. Fan out and join, roughly halving radio-on time per fetch. The join preserves today's serial semantics exactly: a failed forecast retries the whole fetch (an in-flight AQI result is discarded), a failed AQI sends with `AQI: -1`.

**Files:**
- Modify: `src/pkjs/index.js` (`getWeather`)
- Test: none host-side (no JS harness in this repo); verified via emulator JS logs.

**Interfaces:**
- Consumes: `sendWeatherDict`, `retryWeather`, `UV_WINDOW_HOURS` (all existing).
- Produces: unchanged message format; unchanged retry policy.

- [ ] **Step 1: Implement the fan-out**

Replace the entire `getWeather` function with (the condition-code mapping and UV windowing are carried over verbatim):

```js
function getWeather(attempt) {
  attempt = attempt || 0;
  navigator.geolocation.getCurrentPosition(
      function(position) {
        var lat = position.coords.latitude;
        var lon = position.coords.longitude;

        // Read units from Clay settings
        var settings = {};
        try {
          settings = JSON.parse(localStorage.getItem('clay-settings')) || {};
        } catch (e) {
          console.log('Error reading clay settings: ' + e);
        }
        var units = settings['SETTINGS_UNITS'] || '0';
        var tempUnit = (units === '1' || units === 1) ? 'celsius' : 'fahrenheit';

        var forecastUrl = 'https://api.open-meteo.com/v1/forecast?latitude=' + lat +
            '&longitude=' + lon +
            '&current_weather=true&timezone=auto&temperature_unit=' + tempUnit +
            '&hourly=uv_index&forecast_hours=' + UV_WINDOW_HOURS;
        var aqiUrl = 'https://air-quality-api.open-meteo.com/v1/air-quality?latitude=' + lat +
            '&longitude=' + lon + '&current=us_aqi';

        // The forecast and AQI backends are independent; fan out and join so
        // the phone radio is up once instead of twice. The join preserves the
        // old serial semantics: a failed forecast retries the whole fetch
        // (an in-flight AQI result is discarded); a failed AQI sends with -1.
        var forecast = null;
        var aqi = -1;
        var settled = 0;
        var failedReason = null;

        function join() {
          settled++;
          if (settled < 2) return;
          if (failedReason) {
            retryWeather(attempt, failedReason);
            return;
          }
          sendWeatherDict(
              {
                'WEATHER_TEMP': forecast.temp,
                'WEATHER_COND': forecast.cond,
                'WEATHER_AQI': aqi,
                'WEATHER_UV': forecast.uv
              },
              'Weather, AQI & UV');
        }

        var xhr = new XMLHttpRequest();
        xhr.onload = function() {
          if (xhr.status === 200) {
            try {
              var json = JSON.parse(this.responseText);
              var temp = Math.round(json.current_weather.temperature);
              var code = json.current_weather.weathercode;
              // -1 is the watch-side "no data" sentinel; forecast_hours
              // already windows the hourly data, the timestamp guard keeps us
              // honest if the API ever returns a wider range.
              var uv = -1;
              if (json.hourly && json.hourly.uv_index && json.hourly.time) {
                var windowStart = Date.now() - 3600 * 1000;  // include the in-progress hour
                var windowEnd = Date.now() + UV_WINDOW_HOURS * 3600 * 1000;
                for (var i = 0; i < json.hourly.uv_index.length; i++) {
                  var v = json.hourly.uv_index[i];
                  var t = new Date(json.hourly.time[i]).getTime();
                  if (typeof v === 'number' && t >= windowStart && t <= windowEnd && v > uv) {
                    uv = v;
                  }
                }
                if (uv >= 0) uv = Math.round(uv);
              }

              var cond = 'SUN';
              if (code === 0) {
                cond = 'SUN';
              } else if (code >= 1 && code <= 3) {
                cond = 'CLD';
              } else if (code === 45 || code === 48) {
                cond = 'FOG';
              } else if (
                  (code >= 51 && code <= 55) || (code >= 61 && code <= 65) ||
                  (code >= 80 && code <= 82)) {
                cond = 'RAIN';
              } else if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
                cond = 'SNOW';
              } else if (code >= 95) {
                cond = 'TSTM';
              } else {
                cond = 'CLD';
              }
              forecast = {temp: temp, cond: cond, uv: uv};
            } catch (e) {
              failedReason = 'parse error: ' + e;
            }
          } else {
            failedReason = 'HTTP status ' + xhr.status;
          }
          join();
        };
        xhr.onerror = function() { failedReason = 'network error'; join(); };
        xhr.ontimeout = function() { failedReason = 'timeout'; join(); };
        xhr.open('GET', forecastUrl);
        xhr.timeout = 10000;
        xhr.send();

        var aqiXhr = new XMLHttpRequest();
        aqiXhr.onload = function() {
          if (aqiXhr.status === 200) {
            try {
              var aqiJson = JSON.parse(this.responseText);
              if (aqiJson.current && aqiJson.current.us_aqi !== undefined) {
                aqi = Math.round(aqiJson.current.us_aqi);
              }
            } catch (e) {
              console.log('Error parsing AQI: ' + e);
            }
          }
          join();
        };
        aqiXhr.onerror = join;
        aqiXhr.ontimeout = join;
        aqiXhr.open('GET', aqiUrl);
        aqiXhr.timeout = 10000;
        aqiXhr.send();
      },
      function(err) { retryWeather(attempt, 'geolocation: ' + err.message); },
      {timeout: 15000, maximumAge: 60000});
}
```

- [ ] **Step 2: Run formatting + host tests**

Run: `make test`
Expected: PASS (index.js is clang-format-checked; make format if it complains).

- [ ] **Step 3: Verify on the emulator**

```bash
source pebble-env/bin/activate
pebble build && pebble install --emulator emery
pebble logs --emulator emery
```

Expected: exactly one `Weather, AQI & UV sent successfully!` per request (watch for two consecutive minutes of logs and note the single send line; no retry lines unless the network is actually down). Trigger one manual fetch by restarting the app on the emulator to see the round trip immediately.

- [ ] **Step 4: Commit**

```bash
jj desc -m "perf: fetch AQI and forecast in parallel"
jj new -m "perf: shrink AppMessage buffers and reuse cached locations"
```

---

### Task 10: AppMessage buffer sizes + geolocation reuse (accepted trade)

Two small comms trims. The outbox only ever carries one uint8 trigger; the largest inbound dict is a Clay settings snapshot (~11 small tuples, well under 200 B). Shrink 512/512 to 256/64 — RAM headroom, meaningful principally on aplite-class hardware but free everywhere. For geolocation, set `maximumAge` to the fetch cadence: the position can then be as stale as the weather data's own 30-minute freshness, never staler.

**Accepted trade (called out, per the review discussion):** the location can be up to 30 minutes old, so a weather reading can describe where the user was up to half an hour ago — the same bound the data's refresh cadence already imposes. This was reviewed and accepted; do not raise `maximumAge` beyond `WEATHER_CACHE_MAX_AGE_MS`.

**Files:**
- Modify: `src/c/main.c` (`app_message_open`)
- Modify: `src/pkjs/index.js` (`maximumAge`)

- [ ] **Step 1: Shrink the buffers**

In `src/c/main.c`, `init()`:

```c
  app_message_open(256, 64);
```

(large enough for the 11-tuple settings dict plus margin; the outbox only sends a one-byte trigger — `request_weather()` is the only outbound path in the codebase.)

- [ ] **Step 2: Reuse cached locations**

In `src/pkjs/index.js`, the geolocation options:

```js
      {timeout: 15000, maximumAge: WEATHER_CACHE_MAX_AGE_MS});
```

- [ ] **Step 3: Run tests + emulator round trip**

Run: `make test` — Expected: PASS (the mock ignores buffer sizes).

```bash
source pebble-env/bin/activate
pebble build && pebble install --emulator emery
pebble emu-app-config --emulator emery --file /tmp/perf-static-slots.json
pebble logs --emulator emery
```

Expected: weather dict still arrives and a settings push still applies (watch the log for both; the static-slots push itself is the settings round trip). Note the static config contains no weather source, but nothing is gated on that until Task 11 — expect the launch fetch here, and expect it twice (the `ready` listener and the watch's own launch request each trigger one; Task 11 Step 4 removes the duplicate).

- [ ] **Step 4: Commit**

```bash
jj desc -m "perf: shrink AppMessage buffers and reuse cached locations"
jj new -m "perf: fetch weather only when a slot shows it, and only once per launch"
```

---

### Task 11: Fetch weather only when a slot shows it, and only once per launch

Depends on Task 4 (`any_slot_is_one_of`) and its `save_slots`/`restore_slots` test helpers.

A layout with no weather-derived slot still pays the full cycle every 30 minutes: phone wake, location fix, two HTTPS round trips, an AppMessage round trip, five persist writes — for values nothing renders. Gate every request path on slot usage, and fetch immediately when a weather slot first appears so it fills in seconds instead of at the next :00/:30 edge.

The JS `ready` listener is a second door: on a relaunch with a stale cache it fetches whether or not the watch asked, which today means a *double* fetch on every such relaunch (Step 4). Rather than mirror the slot gate in JS, delete the proactive branch and let the watch — which holds the authoritative slot state — drive every fetch.

**Files:**
- Modify: `src/c/main.c` (`any_slot_needs_weather`, `tick_handler`, `init`, outbox retry handlers)
- Modify: `src/c/main.h`
- Modify: `src/c/messaging.c` (`inbox_received_callback`)
- Modify: `src/pkjs/index.js` (`ready` listener)
- Modify: `test/pebble_mock.c`, `test/pebble.h` (`AppTimer`, `app_timer_*`, outbox registrations — none exist yet)
- Test: `test/test_watchface.c`

**Interfaces:**
- Consumes: `any_slot_is_one_of` (Task 4, static in main.c), `request_weather()` (messaging.h).
- Produces: **`bool any_slot_needs_weather(void)`** — non-static, declared in `src/c/main.h`, because `messaging.c` needs it for the slot-appearance trigger and is compiled before `main.c` in the test TU (`messaging.c` already includes `main.h`).
- Produces: `outbox_sent_callback` / `outbox_failed_callback` / `weather_retry_callback` — static in main.c, reachable from tests via the include-the-`.c` harness. This is the first `app_timer_*` use in the project.

- [ ] **Step 1: Write the failing tests**

```c
void test_tick_handler_should_skip_weather_with_no_weather_slots(void) {
  ComplicationDataSource saved[NUM_SLOTS];
  save_slots(saved);
  ComplicationDataSource weather_free[NUM_SLOTS] = {DATA_SOURCE_DATE,        DATA_SOURCE_BLUETOOTH,
                                                   DATA_SOURCE_STEPS,       DATA_SOURCE_HEART_RATE,
                                                   DATA_SOURCE_BEATS,       DATA_SOURCE_FULL_DATE};
  restore_slots(weather_free);

  struct tm t = {0};
  t.tm_min = 30;
  int before = mock_outbox_sends;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);

  s_complication_slots[0].source = DATA_SOURCE_WEATHER;
  tick_handler(&t, MINUTE_UNIT);
  TEST_ASSERT_EQUAL_INT(before + 1, mock_outbox_sends);

  restore_slots(saved);
}

void test_inbox_should_fetch_when_a_weather_slot_first_appears(void) {
  ComplicationDataSource saved[NUM_SLOTS];
  save_slots(saved);
  ComplicationDataSource weather_free[NUM_SLOTS] = {DATA_SOURCE_DATE,        DATA_SOURCE_BLUETOOTH,
                                                   DATA_SOURCE_STEPS,       DATA_SOURCE_HEART_RATE,
                                                   DATA_SOURCE_BEATS,       DATA_SOURCE_FULL_DATE};
  restore_slots(weather_free);

  mock_dict_reset();
  mock_dict_add_cstring(MESSAGE_KEY_SLOT_1, "16");  // DATA_SOURCE_AQI
  int before = mock_outbox_sends;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(before + 1, mock_outbox_sends);

  // Already showing weather: the same push again must not refetch, and a
  // weather reply (no SLOT_* keys) must not either — that is the :00/:30 loop
  // reappearing by another route.
  before = mock_outbox_sends;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);

  mock_dict_reset();
  mock_dict_add_int(MESSAGE_KEY_WEATHER_TEMP, 72);
  mock_dict_add_cstring(MESSAGE_KEY_WEATHER_COND, "SUN");
  before = mock_outbox_sends;
  inbox_received_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(before, mock_outbox_sends);

  restore_slots(saved);
}
```

Register both in `main()`.

- [ ] **Step 2: Run to verify failure**

Run: `make test`
Expected: FAIL — the tick fetches unconditionally, and no slot-appearance trigger exists.

- [ ] **Step 3: Gate the request paths**

In `src/c/main.c`, after `any_slot_is_one_of`:

```c
bool any_slot_needs_weather(void) {
  return any_slot_is_one_of(
      (ComplicationDataSource[]){DATA_SOURCE_WEATHER, DATA_SOURCE_WEATHER_TEMP,
                                 DATA_SOURCE_WEATHER_COND, DATA_SOURCE_AQI, DATA_SOURCE_UV,
                                 DATA_SOURCE_AQI_UV},
      6);
}
```

Declare it in `src/c/main.h`. In `tick_handler`:

```c
  if (tick_time->tm_min % 30 == 0 && any_slot_needs_weather()) {
    request_weather();
  }
```

In `init()`, keep the cache load unconditional — it only reads persistence, and its side effect must happen before any later slot change makes weather visible:

```c
  // Restore cached weather; hit the network only if the cache is stale and
  // something actually shows weather. && short-circuits left to right, so the
  // cache load always runs.
  if (!load_weather_cache() && any_slot_needs_weather()) {
    request_weather();
  }
```

In `src/c/messaging.c`, `inbox_received_callback`: capture the state *before* the slot tuples land, immediately above the `slot1` block:

```c
  // Adding a weather slot has to fetch now, or the new slot reads "--" until
  // the next :00/:30 edge.
  bool needed_weather = any_slot_needs_weather();
```

and replace the trailing `if (units_changed) { request_weather(); }` with:

```c
  bool needs_weather = any_slot_needs_weather();
  if (needs_weather && (units_changed || !needed_weather)) {
    request_weather();
  }
```

A weather reply carries no `SLOT_*` or `SETTINGS_UNITS` keys, so `needed_weather == needs_weather` and `units_changed` is false — the reply cannot re-arm a request. That is the Task 1 invariant restated for this path; the third assertion in Step 1's second test pins it.

**Launch race — probably not residual, so Step 4 handles it first.** Until this task the proactive `ready` fetch doubled as accidental mitigation: if the watch's `init()` request loses the race with the phone's JS runtime coming up, today's `ready` fetch recovers within seconds. The reason to treat that as likely rather than rare: the watch boots the app locally while the phone has to be told over BLE and then spin up a JS VM, so the very first `app_message_outbox_send()` frequently has nobody listening — and **nothing in this codebase notices**. `request_weather()` swallows a NULL iterator (`if (iter == NULL) return;`), no `app_message_register_outbox_failed` or `_outbox_sent` handler is registered anywhere, and `inbox_dropped_callback` only logs. A dropped request is invisible and unretried on every path: launch, :00/:30, units change, slot appearance.

That gap is pre-existing, but deleting the JS proactive fetch makes it load-bearing. So Step 4 closes it *before* Step 5 removes the safety net, rather than leaving it as a contingency on an observation.

Note also that the emulator cannot settle this: `pypkjs` starts locally and is ready far sooner than a paired phone's JS VM over BLE. A clean "exactly one fetch" on emery therefore does not prove the phone case — which is the second reason to fix the race rather than measure it.

- [ ] **Step 4: Retry a dropped weather request**

Bound the retry like the JS side's policy (`WEATHER_MAX_RETRIES`/`WEATHER_RETRY_DELAY_MS` in `index.js`) — same shape, one deliberate difference: the JS budget resets per fetch cycle, while the watch counter resets only on a successful send. During extended phone-away the exhausted counter makes each :00/:30 send a single logged failure with no retry — quieter and cheaper than retrying forever — and it re-arms the moment any send succeeds. In `src/c/main.c`:

```c
// A weather request that loses the race with the phone's JS runtime is
// dropped silently, and after Task 11 nothing on the phone side fetches on
// its own. Bounded retry, reset on the first successful send. The outbox only
// ever carries the weather trigger, so no message discrimination is needed.
#define WEATHER_REQUEST_RETRY_MS 5000
#define WEATHER_REQUEST_MAX_RETRIES 2

static int s_weather_request_retries = 0;

static void weather_retry_callback(void* data) {
  request_weather();
}

static void outbox_sent_callback(DictionaryIterator* iterator, void* context) {
  s_weather_request_retries = 0;
}

static void outbox_failed_callback(DictionaryIterator* iterator, AppMessageResult reason,
                                   void* context) {
  if (s_weather_request_retries >= WEATHER_REQUEST_MAX_RETRIES) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Weather request failed (%d); retries exhausted", (int)reason);
    return;
  }
  s_weather_request_retries++;
  // One-shot timers free themselves on firing, so the handle is deliberately
  // discarded. A second failure inside the window arms a second timer and
  // both fire — the counter bounds the total sends anyway. A NULL return
  // (timer pool exhausted) silently drops this retry — acceptable at the
  // weather-request failure rate.
  app_timer_register(WEATHER_REQUEST_RETRY_MS, weather_retry_callback, NULL);
}
```

Register both in `init()` beside the inbox handlers, before `app_message_open`:

```c
  app_message_register_outbox_sent(outbox_sent_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
```

This cannot reintroduce the Task 1 loop: it fires on send *failure* only, is bounded at two attempts, and resets on the first success.

Mock additions (`test/pebble_mock.c`, declarations in `test/pebble.h`) — none of these exist yet:

```c
typedef void AppTimer;  // in pebble.h, beside the other opaque handles

AppTimer* app_timer_register(uint32_t timeout_ms, void (*callback)(void* data), void* data) {
  return NULL;  // host tests drive retries by calling the callback directly
}
bool app_timer_reschedule(AppTimer* timer, uint32_t new_timeout_ms) {
  return false;
}
void app_timer_cancel(AppTimer* timer) {}
void app_message_register_outbox_sent(void (*callback)(DictionaryIterator* iterator,
                                                      void* context)) {}
void app_message_register_outbox_failed(void (*callback)(DictionaryIterator* iterator,
                                                        AppMessageResult reason, void* context)) {}
```

Add a test asserting the bound, since an unbounded retry on a persistently unreachable phone is the one way this step could cost battery rather than save it:

```c
void test_dropped_weather_request_should_retry_a_bounded_number_of_times(void) {
  s_weather_request_retries = 0;
  int before = mock_outbox_sends;

  // Each failure schedules one retry; the mock timer never fires, so drive it.
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);
  weather_retry_callback(NULL);
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);
  weather_retry_callback(NULL);
  outbox_failed_callback(NULL, APP_MSG_SEND_TIMEOUT, NULL);  // exhausted: no third

  TEST_ASSERT_EQUAL_INT(WEATHER_REQUEST_MAX_RETRIES, s_weather_request_retries);
  TEST_ASSERT_EQUAL_INT(before + WEATHER_REQUEST_MAX_RETRIES, mock_outbox_sends);

  outbox_sent_callback(NULL, NULL);
  TEST_ASSERT_EQUAL_INT(0, s_weather_request_retries);
}
```

(`APP_MSG_SEND_TIMEOUT` must exist in the mock's `AppMessageResult` enum; add it if not.)

Register the test in `main()` next to the other weather-request tests.

- [ ] **Step 5: Let the watch drive — drop the JS proactive fetch**

The `ready` listener is the second door, and it is a door that shouldn't exist. Today, on any relaunch with both caches stale, weather is fetched **twice**: `ready` finds no fresh localStorage cache and calls `getWeather()`, while the watch's `init()` independently finds its persisted cache stale, calls `request_weather()`, and the always-fetch `appmessage` listener runs `getWeather()` again. Two location fixes, four XHRs, two payloads, two `save_weather_cache()` runs. Gating that branch on slot usage would not fix it — with a weather slot configured, which is the shipped default, both doors still open.

Delete the proactive branch instead. In `src/pkjs/index.js`, the `ready` listener becomes:

```js
Pebble.addEventListener('ready', function(e) {
  console.log('PebbleKit JS ready!');
  // Resend a fresh cached payload so a watch with cleared storage still gets
  // data — localStorage plus an AppMessage, no radio.
  var cached = readFreshWeatherCache();
  if (cached) {
    console.log('Weather cache fresh, resending cached payload');
    Pebble.sendAppMessage(
        cached, function(e) { console.log('Cached weather sent successfully!'); },
        function(e) { console.log('Error sending: ' + JSON.stringify(e)); });
  }
  // Nothing fresh cached: don't fetch proactively. The watch requests on
  // launch when its own cache is stale and a weather slot exists, retries a
  // dropped request (Step 4), and the appmessage listener always answers — the
  // watch holds the authoritative slot state, so mirroring it here would only
  // duplicate the request.
});
```

The `appmessage` listener stays always-fetch, unchanged.

Nothing is lost. The watch requests iff (its persisted cache is stale AND a weather slot exists), so the only situations the proactive fetch covered are "watch cache fresh" — it already has data to draw — and "no weather slot" — nothing renders it. Not fetching is correct in both. This also removes the need to reconstruct slot state in JS: no `WEATHER_SOURCES` table, no duplicated copy of the shipped layout, and no drift when the default layout is next reconsidered.

- [ ] **Step 6: Run tests**

Run: `make test`
Expected: PASS. `test_inbox_units_change_should_trigger_weather_refetch` still passes — the default layout's slot 1 is `DATA_SOURCE_WEATHER`, so `needs_weather` holds.

- [ ] **Step 7: Commit**

```bash
jj desc -m "perf: fetch weather only when a slot shows it, and only once per launch"
```

---

## Deferred (recorded, not in this pass)

- **Unsubscribing `health_service_events_subscribe` / `battery_state_service_subscribe` when no slot needs them.** Task 4 makes the *handler* nearly free for an unused metric, but the event still wakes the app task and consumes one of its 10 event-queue slots — and `MovementUpdate` arrives at accel-FIFO rate while walking. Unsubscribing removes the wake entirely. Left out because it needs subscribe/unsubscribe re-evaluation inside `inbox_received_callback` on every slot change, and `health_service_events_subscribe` posts a `HealthEventSignificantUpdate` on each subscribe — more moving parts than the remaining saving justifies once Tasks 3 and 4 have landed. Revisit only if a measurement shows the wakes matter.
- **Having the JS `appmessage` listener resend a fresh cache instead of always fetching.** After Task 11 the watch's :00/:30 request is a pure edge with no staleness check, so a launch-driven request can land on a JS cache only minutes old and refetch anyway; answering from localStorage would make that a radio-free round trip. Left out because it weakens the invariant Task 11 leans on — "a watch that asked is the authority on whether a fetch is wanted" — and the always-fetch listener is what makes deleting the JS proactive branch safe. Worth revisiting only if app-switching turns out to dominate the fetch count.
- **Any change to HRM sample period, health read cadence for displayed metrics, or the complication set.** Out of scope by project rule (AGENTS.md, ISSUES.md).

## Rejected approaches (do not resurrect)

- **Conditional canvas dirty / splitting static frames into their own layer.** Verified against pebbleos: marks carry no region, and every render re-runs every visible layer's `update_proc`. The clock's per-minute `text_layer_set_text` schedules a full-tree render regardless — the floor is 1440 renders/day and the only levers are event gating (Task 3) and cost-per-render (Tasks 5–7).
- **Standalone `strcmp` guard on `text_layer_set_text`.** Inert without the snapshot gate: on the tick path the clock already scheduled the render; on event paths the canvas mark did. It lives inside `request_ui_redraw` (Task 3), where it belongs.
- **Caching `health_service_metric_accessible` masks on `HealthEventSignificantUpdate` alone.** Unsafe: no event fires when health permissions or HRM tracking are toggled (the pebbleos activity paths emit nothing a `HealthEventHandler` receives), so a cached mask can go stale until day rollover or relaunch. Per-tick re-reads of displayed metrics are also the design ISSUES.md documents. Task 4's display-gating is the safe half.
- **Event-only health reads.** PebbleOS posts health events with no retry into a 10-slot queue; drops desync permanently. The tick stays the backstop everywhere.
- **Packing the weather cache into one `persist_write_data`.** Requires a new key + one-time cache loss, violating the `PERSIST_KEY_*` stability rule for a saving Task 8 already captures with compare-before-write.
- **Faster HRM sampling for a live BPM.** Explicitly rejected in ISSUES.md ("BPM trails the Health app — accepted, not a bug").
- **Gating `connection_service_subscribe` on slot contents.** Would silently kill the disconnect vibe, which is independent of the BT slot.
- **Mirroring the slot gate in JS to decide whether the `ready` listener should fetch.** Needs a `WEATHER_SOURCES` table plus a duplicate of the shipped default layout in `index.js` — a second source of truth for a design choice (the enum values are stable identifiers, the default layout is not), drifting silently the next time defaults change. It also leaves the relaunch double fetch in place whenever a weather slot *is* configured, which is the shipped default. Task 11 Step 4 deletes the proactive branch instead: the watch already holds the authoritative slot state and already requests on launch when its cache is stale.

## Verification summary

- Host: `make test` green after every task (format check + Unity suite; new tests per task).
- Visual: emulator crop-compare `AE=0` for Tasks 5, 6, 7 (static slot config `/tmp/perf-static-slots.json`; take both screenshots within the same wall-clock half hour).
- Weather path: emulator logs show exactly one weather send per request after Tasks 1, 9, 10. After Task 11: none at all under a weather-free layout, and **exactly one** fetch per launch under a weather layout with a stale cache — two is the old relaunch double fetch, zero means the launch request was dropped and Step 4's retry did not cover it. Treat the emulator's result as necessary but not sufficient: `pypkjs` is ready far sooner than a phone's JS VM, so it under-exercises the launch race Step 4 exists for. Confirming that on hardware needs `pebble logs --phone <ip>` across a few relaunches.
- **Not gated: on-watch battery life.** Every claim in this plan is structural — fewer scheduled renders, fewer health syscalls, fewer flash writes, fewer radio round trips — and none of it is measured in mAh. There is no battery instrumentation in this repo and the emulator has no drain model. Do not report a percentage improvement; report what was removed.
- Optional measurement (not a gate): an `APP_LOG` counter in `health_handler` during a walk quantifies the real `MovementUpdate` rate on hardware; the review's ~1/s estimate is source-derived, and the true rate was deemed not knowable from the open-sourced tree alone.
