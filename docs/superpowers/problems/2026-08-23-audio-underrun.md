# Audio underruns during animated rendering (PT2 / emery / stock FW)

Date: 2026-08-23. Status: **diagnosed to firmware level, unresolved**. A one-page
problem description for independent review/verification. All measurements below
come from hardware (stock PT2 firmware) with the instrumented watchface.

## Symptom

A watchface plays a synthesized PCM hum (320 ms, 16 kHz, 16-bit, one `SpeakerTrack`
via `speaker_play_tracks`) at backlight-on while running an eight-frame animation
(8 ticks × 90 ms, each tick repaints the whole 200×228 framebuffer through the
window render path). The sound is continuous and clean **when nothing renders**;
**any** frame rendered during playback cracks the audio. Earliest form was a
single pop-gap-hum ("PI....UUUuum"); after cost reductions it presents as several
short gaps spread over the sound.

## Hard numbers

Instrumentation timestamps (ms) from a live strike, current build
(half-split pass ≈ 27 ms/frame):

```
tick 5 @ ...933.828      canvas enter 933.832    ovl enter 933.843
ovl exit 933.886         tick 6 @ 933.921
```

Per tick: ~4 ms to first layer paint, canvas (blit path) invisible in logs,
overlay 43 ms incl. capture/release overhead around a 27-35 ms shader pass,
~35 ms unlogged until the next tick. Frame-on-cpu ≈ 58 ms.

The crackle's invariant: **tick cadence ≈ 95 ms and crackling per-frame,
independent of the pass's compute cost**. Measured ladder, same watch, sound
playing:

| pass cost per frame | tick interval | audio |
| --- | --- | --- |
| (no frames at all) | 95 ms | **clean** |
| 67 ms (full pass) | 105–110 ms | cracked (one big gap early) |
| 54 ms (slimmed taps) | 93–98 ms | several short gaps |
| 27 ms (vertical half-split) | 94–98 ms | still cracking |

Compute is not the variable that moves the symptom. The presence of ANY display
update is.

## What stock firmware does with the sound

Refs from PebbleOS main (local tree):

1. `drivers/speaker/sf32lb52/audio.c: audio_start()` — PA enable pulse
   (GPIO high-low-high, 200 µs each). The audible "pop" prefix is likely this.
2. `include/pbl/drivers/speaker/sf32lb52/audio_definitions.h:17` —
   `CIRCULAR_BUF_SIZE_MS 128`: the app-writable playback ring holds **128 ms**.
3. `drivers/speaker/sf32lb52/audec.c` — codec playback via DMA with two
   half-buffers; each half-buffer IRQ calls, from ISR context,
   `system_task_add_callback_from_isr_droppable(...)` (**droppable** by name);
   its own comment: a dropped refill is only retried at the NEXT half-buffer IRQ.
4. `fw/services/speaker/speaker_service.c:302-306` — `prv_audio_trans_cb`
   then re-queues the real refill through `system_task_add_callback`
   (system task again).
5. Track mixing (`track_player.c`) runs in that refill chain; per-sample work
   there is small but non-zero (velocity scale, rate handling).

So the refill chain is: DMA half IRQ → droppable system-task callback → system
task → ring. Nothing in it runs on the app task — which is why reducing app
compute did not help.

## Established by experiment (and therefore not worth re-trying)

- Display-side: background layers can't be skipped (window engine bg-fills the
  dirty region; skipping a lower layer erases its region, verified by design).
- A canvas bitmap cache (scene blit instead of redrawn fonts) left tick
  intervals unchanged — font painting was never the cost.
- Deferred sound start (sound after the wake frame) did not move the gap.
- Halving and splitting the pass did not move the gap.
- The stream API that would let the app top the ring itself
  (`speaker_stream_open/write/close`) **first appears in FW 4.33.2**
  (commit 72b1c5329, fw/speaker app-facing API); anything older hard-faults on
  the missing syscall — probe by "returns false" fallback is impossible; the app
  crashed with `App fault! PC: 0x1242b0cb` exactly at that call.

## Leading hypothesis

Not CPU scheduling: DMA arbitration — the 45 KB display push per frame contends
with the codec's streaming DMA, and/or the droppable refill loses under load,
so only ~30 ms-worth of slack survives between frames against a 128 ms ring.
Consistent with every observation; not proven.

## Firmware-side candidates to evaluate (ordered)

1. Raise `CIRCULAR_BUF_SIZE_MS` (sf32lb52) 128 → 400+. 45.6 KB × rate framing
   budget is mineable; ring memory cost is small.
2. Make the DMA-IRQ→refill path non-droppable (or count+retry-on-timer instead
   of waiting for the next half IRQ).
3. Examine DMA priorities/arbitration between the panel push and the codec.

## Reproducer (what the instrumented build does)

- Temp instrumentation (to be stripped on landing): `tick N @ t`, plus per-pass
  `crt pass <phase>: <ms>`, `ovl enter/exit`, `canvas enter/exit`.
- Strike triggers on backlight-on; expect the cadence ladder above.
- Control run (crackle-freeness oracle). Temporarily remove
  `layer_mark_dirty(s_crt_layer)` from `crt_flash_tick`: ticks advance, sound
  plays, nothing renders → sound is clean. Re-add → cracks return.

## Independent source review, 2026-08-23

Traced against the local PebbleOS tree. Two of the numbers above are the wrong
ones, and the mechanism is simpler than DMA arbitration.

**It is priority starvation.** Display work and the refill are on different
tasks, which is worse than sharing one:

- `display_update()` runs on **KernelMain**, priority `tskIDLE_PRIORITY + 3`
  (`fw/main.c:157-166`). Chain: `compositor_render_app()` →
  `prv_compositor_flush()` (asserts KernelMain, `compositor.c:202`) →
  `compositor_display_update()` (`compositor_display.c:157`) → `display_update()`
  (`drivers/display/sf32lb/display_jdi.c:418`).
- Both in-place conversion loops are KernelMain CPU work, neither in ISR: the
  forward 222→332 at `display_jdi.c:436-455`, and the back-conversion in
  `prv_display_update_terminate()` (`display_jdi.c:259-281`), which the DMA
  completion ISR merely *posts* to the KernelMain event queue
  (`display_jdi.c:334-343` → `events.c:254-257` → `event_loop.c:427-429`).
- The refill runs on **KernelBackground**, priority `tskIDLE_PRIORITY + 1`
  (`services/system_task/service.c:22,99`), via `system_task_add_callback`
  from `speaker_service.c:305`.

FreeRTOS is priority-preemptive, so prio-3 KernelMain starves prio-1 KernelBG
for as long as it is runnable. Nothing in the display path blocks on DMA —
`HAL_LCDC_SendLayerData_IT` returns immediately (`display_jdi.c:201`); only the
boot-splash path waits, and it yields on a semaphore (`display_jdi.c:513`). No
shared mutex, no critical section spans both paths (grepped: no matches). So the
arbitration hypothesis has no support in the source; the starvation one is
direct.

**The deadline is 32 ms, not 64 ms.** (An earlier draft of this section said
`CIRCULAR_BUF_SIZE_MS` was the wrong buffer to raise. That was too strong —
raising it is viable, see the priming question below. What is wrong is the
*deadline* figure, which comes from the other stage.) There are two stages:

| stage | size | interval |
| --- | --- | --- |
| hardware DMA double-buffer, `CFG_AUDIO_PLAYBACK_PIPE_SIZE` 1024 B ×2 (`audio_definitions.h:14`, `audec.c:323,349`) | 512 samples/half | **32 ms** — this is the refill deadline |
| software ring, `CIRCULAR_BUF_SIZE_MS` 128 → 2048 samples / 4096 B (`audio_definitions.h:17-19`) | what `prv_refill_bg` fills into | — |

The half/full-complete IRQs (`audec.c:505-519`) fire every 32 ms. The ~35 ms of
"unlogged" time in the trace above is the KernelMain conversion and flush work,
and **that alone exceeds the 32 ms deadline** — before the app's shader is
counted at all. That is exactly why the compute ladder never cleared the
symptom, and why it did *shift* it (67 → 27 ms moved one big gap to several
short ones: the app task contributes, but removing all of it still leaves
KernelMain over the line).

**The priming question decides several of the options.** The ISR needs
`CFG_AUDIO_PLAYBACK_PIPE_SIZE` bytes present in the software ring every 32 ms and
memsets silence when they are not (`audec.c:472-476`) — but the ring→DMA copy
itself is ISR-side, so it is *not* starved by KernelMain. Only the ring *top-up*
is. Therefore: if the initial fill primes the whole 4096-byte ring before
rendering starts, playback survives 128 ms of KernelBG starvation with no gap,
and a sound shorter than the ring never needs KernelBG at all. If the initial
fill only stages one pipe's worth, it does not. **Unverified — read
`speaker_play_tracks`' start path and whether `prv_refill_bg` fills to ring
capacity or to one pipe.** This is the cheapest question with the most leverage.

Revised candidates, ordered:

1. **Prove it first, one build:** `audec.c:472-476` already logs
   `"audio data not enough remain:%..."` precisely when the ring was not refilled
   in time, gated on `CONFIG_DRIVER_SPEAKER_LOG_LEVEL` (`audec.c:13`). Count
   those per strike. The droppable-coalescing path (`audec.c:489-502`) has no
   counter, and `speaker_stream_underrun_count`
   (`speaker_service.c:431`) is a source-side metric, not this.
2. **Buffer sizes, either or both.** `CIRCULAR_BUF_SIZE_MS` 128 → 512 (+12 KB)
   buys tolerance to a late KernelBG — worth only as much as the priming answer
   above allows. `CFG_AUDIO_PLAYBACK_PIPE_SIZE` 1024 → 4096 B per half stretches
   the deadline itself from 32 ms to 128 ms for 6 KB more DMA buffer, and does not
   depend on priming at all. Neither touches scheduling.
3. **`system_task_enable_raised_priority()` already exists** — `service.c:220-224`,
   raises KernelBG to `+3`, "Same as KernelMain" — and has **zero callers in the
   tree**. Someone built this lever for this class of problem and never wired it
   up. Raise on playback start, release on stop. Ranked *below* the buffer changes
   despite being the smaller patch: at equal priority KernelBG takes CPU from
   KernelMain, and with ~62 ms of work in a 93 ms tick that risks trading audio
   crackle for frame stutter. Also check `configUSE_TIME_SLICING` before assuming
   equal priorities share fairly.

**8 kHz buys nothing here.** Mixing is fixed at `SPEAKER_SAMPLE_RATE 16000`
(`speaker_service.c:29`) and an 8 kHz source is upsampled with a 4-tap cubic
before queueing (`speaker_service.c:376-406`), so the codec's DMA byte rate is
unchanged.

**App-side, the only remaining lever is not rendering during playback** — which
the control run already proves clean. But see step 3 below: if the ring primes,
there is a second lever, and it points the opposite way from
`plans/2026-08-23-crt-degauss-field.md`, which lengthens the sound to 700 ms so
it ends with the motion. On stock firmware that widens the overlap from 320 ms to
700 ms and makes this strictly worse.

## Proposed way forward

**Severity, first:** `s_settings_crt_sound` defaults to 0 (`src/c/data.c:61`), so
the crackle only reaches users who opt in. This is not a ship blocker, and the
sequence below is ordered accordingly.

1. **Land the motion.** Tasks 1, 2 and 4 of
   `plans/2026-08-23-crt-degauss-field.md`. The field and the purity band are
   untouched by this bug and are the part of the effect that carries it. Task 3
   (sound) waits for step 2.
2. **One instrumented firmware build, two answers.** The underrun count per
   strike (candidate 1 above), and the priming question — does the playback start
   path fill the ring to capacity or to one pipe's worth. Everything below
   branches on the second answer.
3. **If the ring primes: shorten the sound, do not lengthen it.** A sound that
   fits inside the ring needs no KernelBG run during playback, so it cannot be
   starved — a percussive thunk of ≤128 ms is immune on stock firmware. This costs
   almost nothing perceptually: 100% of the current sample's energy sits below
   300 Hz (measured), so the sustained hum is already inaudible on this speaker
   and what a listener actually hears is the onset. Retuning that onset to sit at
   200-500 Hz makes it *more* audible than today, not less. This supersedes the
   degauss plan's Task 3.
4. **Firmware fix, measured, upstream.** Candidate 2, then 3 if needed. Submit to
   coredevices/PebbleOS with the counter numbers from step 2 — the diagnosis above
   is the useful half of that patch.
5. **Only if 3 and 4 both fail:** serialise — sound over a still frame, then
   motion — gated on a firmware version check so it disappears when the fix ships.

## Loose end inside the app

None blocking: after the report-backed fixes, watchface logic is
green (177/177 host tests, emulator visual gate clean). The remaining crackle
is judged by these numbers to be outside app reach.
