#include <pebble.h>
#include "chime.h"
#include "data.h"

bool chime_is_due(int minute, int enabled) {
  return minute == 0 && enabled;
}

int chime_effective_volume(int requested) {
  if (requested < 0) return 0;
  if (requested > 100) return 100;
  return requested;
}

void chime_play(void) {
  int volume = chime_effective_volume(s_settings_chime_volume);
  if (volume == 0) return;
  // Square — the PC speaker's 8253 never did anything else.
  speaker_play_tone(CHIME_FREQ_HZ, CHIME_DURATION_MS, volume, SpeakerWaveformSquare);
}

// Wall hour of the last chime. A DST fall-back replays a wall hour, and the
// replay must not chime again.
static int s_last_chimed_hour = -1;

void chime_on_tick(const struct tm* tick_time) {
  if (!chime_is_due(tick_time->tm_min, s_settings_hourly_chime)) return;
  int wall_hour = (tick_time->tm_year * 366 + tick_time->tm_yday) * 24 + tick_time->tm_hour;
  if (wall_hour == s_last_chimed_hour || speaker_is_muted()) return;
  s_last_chimed_hour = wall_hour;
  chime_play();
}
