#include <pebble.h>
#include "data.h"

// All state lives here, sectioned. Everything is extern'd via data.h; tests
// reset to these initials (the weather block via the messaging table).

// System state — what the watch knows about itself.
int s_battery_level = 100;
// Charging speaks for itself in green; the level ladder is for draining.
bool s_battery_charging = false;
bool s_connected = true;
bool s_quiet_time_active = false;

// Health readings; sentinels mark "no data" (steps/sleep -1, heart rate 0).
int s_step_count = -1;
int s_sleep_seconds = -1;
int s_heart_rate = 0;
int s_active_minutes = 0;

// Weather readings — pushed by the phone; messaging.c's field table owns the
// wire contract for every one of them (keys, persistence, sentinels).
int s_weather_temp = -999;
int s_weather_cond_code = -1;  // WMO weather code; -1 indicates no data
int s_weather_aqi = -1;
int s_weather_uv = -1;
int s_weather_humidity = -1;
int s_weather_wind_direction = -1;  // meteo bearing, degrees FROM; -1 indicates no data
int s_weather_wind_speed = -1;      // in the settings unit (mph/m/s); -1 indicates no data
int s_weather_pcp = -1;
int s_precip_now = -1;  // tenths of mm over the past hour; -1 indicates no data
int s_temp_high = -999;
int s_temp_low = -999;
int s_temp_high_tmrw = -999;
int s_temp_low_tmrw = -999;
int s_hi_hour_today = -1;  // event hours 0-23; -1 unknown
int s_lo_hour_today = -1;
int s_hi_hour_tmrw = -1;
int s_lo_hour_tmrw = -1;

// Clock-derived state.
int s_wall_hour = 0;
int s_date_day = 10;
int s_beats = 0;
char s_date_display[64] = "";
char s_short_date_display[16] = "";

// UI state.
bool s_quick_view_active = false;

// Settings — persisted under messaging.h's PERSIST_KEY_SETTINGS_*.
int s_settings_theme =
    2;  // 0 = Auto, 1 = Turbo Vision, 2 = Norton, 3 = Dark, 4 = Navigator; default is Norton
int s_settings_units = 0;              // UNITS_IMPERIAL; the JS defaults scrape parses this literal
int s_settings_date_format = 0;        // DateFormat: 0 = ISO, 1 = DOS, 2 = Text, 3 = Short
int s_settings_short_date_format = 0;  // 0 = Month-Day, 1 = Day-Month
int s_settings_dow_position = 0;       // 0 = Before, 1 = After, 2 = Hidden
int s_settings_disconnect_vibe = 1;    // 1 = buzz on phone disconnect (default), 0 = silenced

ComplicationSlot s_complication_slots[NUM_SLOTS] = {
    [SLOT_IDX_TOP_LEFT] = {.box_rect = SLOT_RECT_TOP_LEFT, .source = DATA_SOURCE_WEATHER},
    [SLOT_IDX_TOP_RIGHT] = {.box_rect = SLOT_RECT_TOP_RIGHT, .source = DATA_SOURCE_SLEEP},
    [SLOT_IDX_BOTTOM_LEFT] = {.box_rect = SLOT_RECT_BOTTOM_LEFT, .source = DATA_SOURCE_STEPS},
    [SLOT_IDX_BOTTOM_CENTER] = {.box_rect = SLOT_RECT_BOTTOM_CENTER,
                                .source = DATA_SOURCE_HEART_RATE},
    [SLOT_IDX_BOTTOM_RIGHT] = {.box_rect = SLOT_RECT_BOTTOM_RIGHT, .source = DATA_SOURCE_BLUETOOTH},
    [SLOT_IDX_CENTER] = {.box_rect = SLOT_RECT_CENTER, .source = DATA_SOURCE_FULL_DATE}};

// The face's temperature spelling, unit-aware by policy: imperial prints the
// unit letter and signs negatives only, metric always signs and letters
// (Celsius crosses zero as a matter of course).
void format_temp(char* buf, size_t len, int temp, bool with_unit) {
  if (temp == -999) {
    snprintf(buf, len, "--");
  } else if (s_settings_units == UNITS_METRIC) {
    snprintf(buf, len, with_unit ? "%+dC" : "%+d", temp);
  } else {
    snprintf(buf, len, with_unit ? "%dF" : "%d", temp);
  }
}

// WMO weather codes → the face's condition words, plus whether the family
// precipitates (that facet gates the live-rate PCP readout). Ranges don't
// overlap; anything unmapped reads "--" and counts as dry.
typedef struct {
  int from, to;
  const char* word;
  bool precipitating;
} WmoCond;

static const WmoCond s_wmo_conds[] = {
    {0, 0, "SUN", false},   {1, 3, "CLD", false},   {45, 48, "FOG", false},
    {51, 55, "RAIN", true}, {61, 65, "RAIN", true}, {80, 82, "RAIN", true},
    {71, 77, "SNOW", true}, {85, 86, "SNOW", true}, {95, 99, "TSTM", true},
};

static const WmoCond* wmo_cond(int code) {
  for (unsigned i = 0; i < sizeof(s_wmo_conds) / sizeof(s_wmo_conds[0]); i++) {
    if (code >= s_wmo_conds[i].from && code <= s_wmo_conds[i].to) return &s_wmo_conds[i];
  }
  return NULL;
}

const char* weather_cond_word(int code) {
  const WmoCond* cond = wmo_cond(code);
  return cond ? cond->word : "--";
}

bool weather_cond_precipitating(int code) {
  const WmoCond* cond = wmo_cond(code);
  return cond && cond->precipitating;
}

// While precipitating and metric, the PCP readout shows the observed rate
// instead of the forecast probability — the guess is settled.
bool weather_shows_precip_amount(void) {
  if (s_settings_units != UNITS_METRIC || s_precip_now < 0) return false;
  return weather_cond_precipitating(s_weather_cond_code);
}

// The eight arrows, clockwise from north. UTF-8 for U+2190..U+2199.
static const char* s_wind_arrows[8] = {"\xE2\x86\x91", "\xE2\x86\x97", "\xE2\x86\x92",
                                       "\xE2\x86\x98", "\xE2\x86\x93", "\xE2\x86\x99",
                                       "\xE2\x86\x90", "\xE2\x86\x96"};

const char* wind_direction_arrow(int deg) {
  if (deg < 0) return "--";
  // The meteo bearing is the direction the wind blows FROM; the face points
  // the way it goes, half a compass around.
  int toward = (deg % 360 + 180) % 360;
  return s_wind_arrows[(toward + 22) / 45 % 8];
}

void format_wind_speed(char* buf, size_t len, bool with_unit) {
  if (s_weather_wind_speed < 0) {
    buf[0] = '\0';
    return;
  }
  int speed = s_weather_wind_speed > 999 ? 999 : s_weather_wind_speed;
  if (with_unit) {
    snprintf(buf, len, "%d %s", speed, s_settings_units == UNITS_METRIC ? "m/s" : "mph");
  } else {
    snprintf(buf, len, "%d", speed);
  }
}

// Canonical wind readout: arrow, air, speed, air, unit. Narrow windows drop
// the unit (with_unit=false); the arrow alone, the number alone, and "--"
// for nothing are all legal.
void format_wind(char* buf, size_t len, bool with_unit) {
  const char* arrow =
      s_weather_wind_direction < 0 ? NULL : wind_direction_arrow(s_weather_wind_direction);
  char speed_buf[16];
  format_wind_speed(speed_buf, sizeof(speed_buf), with_unit);
  if (arrow && speed_buf[0]) {
    snprintf(buf, len, "%s %s", arrow, speed_buf);
  } else if (arrow || speed_buf[0]) {
    snprintf(buf, len, "%s", arrow ? arrow : speed_buf);
  } else {
    snprintf(buf, len, "--");
  }
}

void format_strip_temp(char* buf, int buf_size) {
  format_temp(buf, buf_size, s_weather_temp, true);
}

// An extreme "has passed" when its own event hour has ended — during that
// hour the face's now-reading can still equal it (a 14:00 high is true at
// 14:30), so the roll waits for the hour to be over. From then on the cell
// shows tomorrow's value, keeping the readout about the next occurrence.
// Unknown hours (-1) count as not passed.
static bool extreme_passed(int event_hour) {
  return event_hour >= 0 && s_wall_hour > event_hour;
}

// Which cell leads, hours only: the sooner event goes left. A tie, or
// unknown hours (nothing to sort by), keeps LO first — the usual shape of a
// day. Shared by the formatter and its label so they can never disagree.
bool high_low_hi_leads(void) {
  if (s_lo_hour_today < 0 || s_hi_hour_today < 0 || s_lo_hour_tmrw < 0 || s_hi_hour_tmrw < 0) {
    return false;
  }
  int lo_key = extreme_passed(s_lo_hour_today) ? 24 + s_lo_hour_tmrw : s_lo_hour_today;
  int hi_key = extreme_passed(s_hi_hour_today) ? 24 + s_hi_hour_tmrw : s_hi_hour_today;
  return hi_key < lo_key;
}

int high_low_displayed_high(void) {
  return extreme_passed(s_hi_hour_today) ? s_temp_high_tmrw : s_temp_high;
}

void format_high_low(char* buf, size_t len) {
  // Either pair incomplete sinks the readout: a half-number reads as data.
  if (s_temp_high == -999 || s_temp_low == -999 || s_temp_high_tmrw == -999 ||
      s_temp_low_tmrw == -999) {
    snprintf(buf, len, "-- --");
    return;
  }
  // Each cell shows the next occurrence of its kind: today's value until
  // the extreme's own hour begins, then tomorrow's.
  int lo_val = extreme_passed(s_lo_hour_today) ? s_temp_low_tmrw : s_temp_low;
  int hi_val = extreme_passed(s_hi_hour_today) ? s_temp_high_tmrw : s_temp_high;
  // Chronological left to right — the sooner event leads. Every number
  // carries its unit letter; the air between halves is what the frame-stub
  // captions above register to.
  bool lo_left = !high_low_hi_leads();
  int left = lo_left ? lo_val : hi_val;
  int right = lo_left ? hi_val : lo_val;
  if (s_settings_units == UNITS_METRIC) {
    snprintf(buf, len, "%+dC %+dC", left, right);
  } else {
    snprintf(buf, len, "%dF %dF", left, right);
  }
}

// Swatch Internet Time: the BMT (UTC+1, no DST) day split into 1000 beats of
// 86.4s. Ticks are per-minute, so the value is exact at each tick and lags by
// up to one beat before the next — a second-resolution tick isn't worth the
// battery.
int compute_beats(time_t utc) {
  int bmt_seconds = (int)((utc + 3600) % 86400);
  return (bmt_seconds * 1000) / 86400;
}

void to_upper_str(char* str) {
  for (int i = 0; str[i]; i++) {
    if (str[i] >= 'a' && str[i] <= 'z') {
      str[i] -= 32;
    }
  }
}

static void ordinal_suffix(int day, char* buf) {
  if (day >= 11 && day <= 13) {
    strcpy(buf, "th");
    return;
  }
  switch (day % 10) {
    case 1:
      strcpy(buf, "st");
      break;
    case 2:
      strcpy(buf, "nd");
      break;
    case 3:
      strcpy(buf, "rd");
      break;
    default:
      strcpy(buf, "th");
      break;
  }
}

// The date itself, without the weekday.
static void format_date_body(int format, int short_format, struct tm* tick_time, char* buffer,
                             int buf_size) {
  switch (format) {
    case DATE_FORMAT_DOS:
      strftime(buffer, buf_size, "%d-%m-%Y", tick_time);
      break;
    case DATE_FORMAT_TEXT: {
      char month_buf[16];
      char year_buf[8];
      char suffix[3];

      strftime(month_buf, sizeof(month_buf), "%b", tick_time);
      strftime(year_buf, sizeof(year_buf), "%Y", tick_time);
      to_upper_str(month_buf);
      ordinal_suffix(tick_time->tm_mday, suffix);

      snprintf(buffer, buf_size, "%s %d%s, %s", month_buf, tick_time->tm_mday, suffix, year_buf);
      break;
    }
    case DATE_FORMAT_SHORT:
      strftime(buffer, buf_size, short_format == SHORT_DATE_DAY_MONTH ? "%d-%m" : "%m-%d",
               tick_time);
      break;
    default:
      strftime(buffer, buf_size, "%Y-%m-%d", tick_time);
      break;
  }
}

// Attaches the weekday where the Day of week setting wants it. Every date the
// face draws goes through here, so the position never depends on the format.
static void format_with_weekday(int dow_position, struct tm* tick_time, const char* body,
                                char* buffer, int buf_size) {
  if (dow_position == DOW_HIDDEN) {
    snprintf(buffer, buf_size, "%s", body);
    return;
  }

  char weekday_buf[8];
  strftime(weekday_buf, sizeof(weekday_buf), "%a", tick_time);
  to_upper_str(weekday_buf);

  if (dow_position == DOW_AFTER) {
    snprintf(buffer, buf_size, "%s %s", body, weekday_buf);
  } else {
    snprintf(buffer, buf_size, "%s %s", weekday_buf, body);
  }
}

void format_date_string(int format, int short_format, int dow_position, struct tm* tick_time,
                        char* buffer, int buf_size) {
  char body[32];
  format_date_body(format, short_format, tick_time, body, sizeof(body));
  format_with_weekday(dow_position, tick_time, body, buffer, buf_size);
}

void format_short_date_string(int short_format, int dow_position, struct tm* tick_time,
                              char* buffer, int buf_size) {
  format_date_string(DATE_FORMAT_SHORT, short_format, dow_position, tick_time, buffer, buf_size);
}

// Kept beside the formatters so the two can't drift apart.
int date_dow_offset(int dow_position, const char* formatted) {
  if (dow_position == DOW_HIDDEN) return -1;
  if (dow_position == DOW_AFTER) {
    int len = strlen(formatted);
    return len >= DOW_LEN ? len - DOW_LEN : -1;
  }
  return 0;
}
