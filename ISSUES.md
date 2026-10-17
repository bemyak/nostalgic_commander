# Known Issues

Bugs and suspect behavior, ordered roughly by user impact. Approved upcoming
work lives in [TODOs.md](TODOs.md); unvetted ideas in [IDEAS.md](IDEAS.md).

## Chime arrives as a click plus the tone below 100% volume — stock PebbleOS bug

On volumes under 100 the beep comes out as a loud click followed by the
quieter tone ("BE-BOOP"). Root cause is `audec_start()` in PebbleOS
(`drivers/speaker/sf32lb52/audec.c`): the requested digital volume is
applied only after the DAC path is already un-muted, so the codec power-up
transients always leave at full gain; only at 100 does the tone itself mask
them. Fix prepared in the firmware tree (`drivers/speaker: apply volume
before un-muting the DAC path`, in flight upstream). The face applies no
workaround — on stock firmware, 100% volume is clean.

## BPM trails the Health app — accepted, not a bug

`update_health_info()` reads `HealthMetricHeartRateBPM`, which the SDK defines as
"a filtered value that is at most 15 minutes old", and the system's default HRM
sample period is 10 minutes. The Health app looks live because a foreground app
can request 1-second sampling via `health_service_set_heart_rate_sample_period()`.

Deliberately not fixed. The filtered value is the right glanceable number — it
drops bad readings from hand movement and poor sensor contact — and a watchface
is displayed all day, so requesting faster sampling would run the HRM
continuously and cost battery. The refresh path is not at fault: health data for
every displayed metric is re-read every tick and on `HealthEventHeartRateUpdate`.

## CRT degauss sound crackles on hardware during the strike — firmware

On stock PT2 firmware, animating frames while the strike sound plays starves
the audio refill path (128 ms ring, droppable system-task refills,
KernelMain/KernelBG priority inversion). The watchface side mitigates: the
sound is a 110 ms thunk that fits the ring, starts after the wake frame, and
fades out over its last samples.
Full analysis and firmware fix candidates:
[2026-08-23-audio-underrun](docs/superpowers/problems/2026-08-23-audio-underrun.md).

A pop at stop can remain after the fade. It comes from the firmware stop path:
`audec_stop` (`drivers/speaker/sf32lb52/audec.c`, ~409-435) and `audio_stop`
(`drivers/speaker/sf32lb52/audio.c`, ~40-47) turn the PLL off first, then close
the analog DAC and VCM while the PA is still on, with no ramp. `audec_start`
clears `RAMP_EN`. Suggested firmware fix: drop `pa_ctrl` before `audec_stop`,
mute the DAC path before the DMA stop, and move the PLL off to the end.
Unverified on hardware.
