#pragma once
#include <pebble.h>

// The POST success beep, on the hour, when the volume select is above Off (the
// default). Numbers are the BIOS beep: 880 Hz (A5) as the PIT quantizes it
// (divisor 1331 → ~896 Hz measured on real hardware), for the IBM BIOS's half
// second, square as the 8253 always was.
#define CHIME_FREQ_HZ 896
#define CHIME_DURATION_MS 500

// The gate, kept pure so the host suite can assert every combination without
// a speaker: full-hour minute and an audible volume. Mute (system mute and Quiet
// Time) is asked of speaker_is_muted() only once this passes.
bool chime_is_due(int minute, int volume);

// Settings arrive as arbitrary ints; clamp to [0, 100] so an off-wire value
// never reaches the SDK out of range.
int chime_effective_volume(int requested);

// One POST beep at the configured (clamped) volume, ungated — called by
// chime_on_tick at the full hour and by messaging.c for the settings page's
// test button. A volume of 0 plays nothing.
void chime_play(void);

// Tick hook; main.c's tick_handler calls this on every MINUTE_UNIT tick.
void chime_on_tick(const struct tm* tick_time);
