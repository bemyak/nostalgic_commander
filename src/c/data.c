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

// Health readings; sentinels mark "no data" (steps/sleep/active minutes -1,
// heart rate 0).
int s_step_count = -1;
int s_sleep_seconds = -1;
int s_heart_rate = 0;
int s_active_minutes = -1;

// Weather readings — pushed by the phone; messaging.c's field table owns the
// wire contract for every one of them (keys, persistence, sentinels).
int s_weather_temp = WEATHER_TEMP_NONE;
int s_weather_cond_code = WEATHER_NONE;  // WMO weather code
int s_weather_aqi = WEATHER_NONE;
int s_weather_uv = WEATHER_NONE;
int s_weather_humidity = WEATHER_NONE;
int s_weather_wind_direction = WEATHER_NONE;  // meteo bearing, degrees FROM
int s_weather_wind_speed = WEATHER_NONE;      // in the settings unit (mph/m/s)
int s_weather_pcp = WEATHER_NONE;
int s_precip_now = WEATHER_NONE;  // tenths of mm over the past hour
int s_temp_high = WEATHER_TEMP_NONE;
int s_temp_low = WEATHER_TEMP_NONE;
int s_temp_high_tmrw = WEATHER_TEMP_NONE;
int s_temp_low_tmrw = WEATHER_TEMP_NONE;
int s_hi_hour_today = WEATHER_NONE;  // event hours 0-23
int s_lo_hour_today = WEATHER_NONE;
int s_hi_hour_tmrw = WEATHER_NONE;
int s_lo_hour_tmrw = WEATHER_NONE;
int s_weather_stamp = 0;

// Clock-derived state.
int s_wall_hour = 0;
int s_date_day = 1;
int s_beats = 0;
int s_longitude = LONGITUDE_NONE;
int s_solar_hour_angle = 0;
int s_latitude = LATITUDE_NONE;
int s_solar_elevation = 0;
int s_week_number = 1;
char s_date_display[64] = "";
char s_short_date_display[16] = "";
char s_time_text[8] = "";

// UI state.
bool s_quick_view_active = false;

// Settings — persisted under messaging.h's PERSIST_KEY_SETTINGS_*.
int s_settings_theme = 2;  // 1 = Turbo Vision, 2 = Norton, 3 = Dark, 4 = Navigator, 6 = Amber;
                           // default is Norton; 0 was Auto
int s_settings_night_theme = 0;        // 0 = off: the day theme all day
int s_settings_units = 0;              // UNITS_IMPERIAL; the JS defaults scrape parses this literal
int s_settings_date_format = 0;        // DateFormat: 0 = ISO, 1 = DOS, 2 = Text, 3 = Short
int s_settings_short_date_format = 0;  // 0 = Month-Day, 1 = Day-Month
int s_settings_dow_position = 0;       // 0 = Before, 1 = After, 2 = Hidden
int s_settings_weather_window = 12;    // hours ahead; 0 = in-progress hour only ("Now")
int s_settings_crt_aberration = 0;     // 1 = RGB fringing on (default off)
int s_settings_crt_curvature = 0;      // 1 = bent picture and dark corners on (default off)
int s_settings_crt_degauss = 0;        // 1 = degauss strike at backlight-on (default off)
int s_settings_degauss_volume = 10;    // 0-100, 0 = silent strike (default 10)
int s_settings_chime_volume = 0;       // 0-100, 0 = no POST beep on the hour

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
  if (temp == WEATHER_TEMP_NONE) {
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
    {0, 0, "SUN", false},   {1, 3, "CLDY", false},  {45, 48, "FOG", false},
    {51, 57, "RAIN", true}, {61, 67, "RAIN", true}, {80, 82, "RAIN", true},
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
  if (s_temp_high == WEATHER_TEMP_NONE || s_temp_low == WEATHER_TEMP_NONE ||
      s_temp_high_tmrw == WEATHER_TEMP_NONE || s_temp_low_tmrw == WEATHER_TEMP_NONE) {
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

static bool is_leap_year(int year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int utc_year_day(time_t utc) {
  int yday = (int)(utc / 86400);
  for (int year = 1970; yday >= (is_leap_year(year) ? 366 : 365); year++) {
    yday -= is_leap_year(year) ? 366 : 365;
  }
  return yday;
}

// sin_lookup only interpolates below ~45°; above it, it returns raw table
// steps (~0.35°). Up there the sine is derived from the interpolated range.
static int32_t sin_fine(int32_t angle) {
  angle = (angle % TRIG_MAX_ANGLE + TRIG_MAX_ANGLE) % TRIG_MAX_ANGLE;
  int32_t sign = 1;
  if (angle >= TRIG_MAX_ANGLE / 2) {
    sign = -1;
    angle -= TRIG_MAX_ANGLE / 2;
  }
  if (angle > TRIG_MAX_ANGLE / 4) angle = TRIG_MAX_ANGLE / 2 - angle;
  if (angle <= TRIG_MAX_ANGLE / 8 - 64) return sign * sin_lookup(angle);
  // sin x = 1 - 2 sin²((90° - x) / 2)
  uint32_t half_sin = sin_lookup((TRIG_MAX_ANGLE / 4 - angle) / 2);
  return sign * (TRIG_MAX_RATIO -
                 (int32_t)((2 * half_sin * half_sin + TRIG_MAX_RATIO / 2) / TRIG_MAX_RATIO));
}

static int32_t cos_fine(int32_t angle) {
  return sin_fine(angle + TRIG_MAX_ANGLE / 4);
}

// Apparent solar time past noon, unwrapped. Mean solar time is UTC plus 4
// minutes per degree of longitude; the equation of time (the three-term fit,
// day-of-year of the UTC date) turns that into apparent solar time.
// Everything is in hundredths of a minute: a degree is 400 of them, a full
// turn 144000.
static int solar_time_since_noon(time_t utc, int yday, int longitude_centi) {
  int minute_of_day = (int)(utc % 86400) / 60;
  int b = ((yday + 285) % 365) * TRIG_MAX_ANGLE / 365;  // 360° * (N - 81) / 365
  int b2 = (2 * b) % TRIG_MAX_ANGLE;
  int eot = (987 * sin_fine(b2) - 753 * cos_fine(b) - 150 * sin_fine(b)) / TRIG_MAX_RATIO;
  return minute_of_day * 100 + longitude_centi * 4 + eot - 12 * 60 * 100;
}

// The sun's local hour angle in whole degrees, (-180, 180], negative before
// solar noon. The date is worked out by hand: gmtime() shares its buffer with
// localtime(), which callers hold.
int compute_solar_hour_angle(time_t utc, int longitude_centi) {
  int since_noon = solar_time_since_noon(utc, utc_year_day(utc), longitude_centi);
  // Shifting by 179° and a half degree before the modulo rounds to the
  // nearest degree and lands the range on [-179, 180].
  int wrapped = ((since_noon + 200 + 179 * 400) % 144000 + 144000) % 144000;
  return wrapped / 400 - 179;
}

static int32_t ratio_mul(int32_t a, int32_t b) {
  return (int32_t)((int64_t)a * b / TRIG_MAX_RATIO);
}

static uint32_t isqrt_u32(uint32_t n) {
  uint32_t root = 0;
  for (uint32_t bit = 1u << 30; bit; bit >>= 2) {
    if (n >= root + bit) {
      n -= root + bit;
      root = (root >> 1) + bit;
    } else {
      root >>= 1;
    }
  }
  return root;
}

// The angle in [0, a quarter turn] whose sine is nearest `ratio`, bisected on
// sin_fine: the firmware's atan2_lookup is a coarse table (~0.2° steps).
static int32_t arcsin_fine(int32_t ratio) {
  int32_t lo = 0;
  int32_t hi = TRIG_MAX_ANGLE / 4;
  while (lo < hi) {
    int32_t mid = (lo + hi + 1) / 2;
    if (sin_fine(mid) <= ratio) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  if (lo < TRIG_MAX_ANGLE / 4 && sin_fine(lo + 1) - ratio < ratio - sin_fine(lo)) lo++;
  return lo;
}

bool location_known(void) {
  return s_latitude != LATITUDE_NONE && s_longitude != LONGITUDE_NONE;
}

// The sun's height above the geometric horizon, in hundredths of a degree
// (negative below), refraction ignored. The declination is Spencer's (1971)
// Fourier fit at the UTC hour; its coefficients are pre-divided by 2π so the
// sum lands straight in TRIG_MAX_ANGLE units. sin e is turned back into an
// angle by arcsin, or by 90° minus the arcsin of cos e near the zenith where
// the sine is too flat to resolve it.
int compute_solar_elevation(time_t utc, int latitude_centi, int longitude_centi) {
  int yday = utc_year_day(utc);
  int hour = (int)(utc % 86400) / 3600;
  int g = TRIG_MAX_ANGLE * (yday * 24 + hour - 12) / (365 * 24);
  int declination =
      (72 * TRIG_MAX_RATIO - 4171 * cos_fine(g) + 733 * sin_fine(g) - 70 * cos_fine(2 * g) +
       9 * sin_fine(2 * g) - 28 * cos_fine(3 * g) + 15 * sin_fine(3 * g)) /
      TRIG_MAX_RATIO;
  int latitude = latitude_centi * TRIG_MAX_ANGLE / 36000;
  int hour_angle = solar_time_since_noon(utc, yday, longitude_centi) % 144000 * 512 / 1125;

  int32_t sin_e =
      ratio_mul(sin_fine(latitude), sin_fine(declination)) +
      ratio_mul(ratio_mul(cos_fine(latitude), cos_fine(declination)), cos_fine(hour_angle));
  int32_t abs_sin_e = sin_e < 0 ? -sin_e : sin_e;
  if (abs_sin_e > TRIG_MAX_RATIO) abs_sin_e = TRIG_MAX_RATIO;
  int32_t angle;
  if (abs_sin_e < TRIG_MAX_RATIO * 7 / 10) {
    angle = arcsin_fine(abs_sin_e);
  } else {
    uint32_t cos_e = isqrt_u32((uint32_t)TRIG_MAX_RATIO * TRIG_MAX_RATIO -
                               (uint32_t)abs_sin_e * (uint32_t)abs_sin_e);
    angle = TRIG_MAX_ANGLE / 4 - arcsin_fine((int32_t)cos_e);
  }
  int centi = (angle * 36000 + TRIG_MAX_ANGLE / 2) / TRIG_MAX_ANGLE;
  return sin_e < 0 ? -centi : centi;
}

// 53 when Jan 1 is Thursday — or Wednesday in a leap year — (wday: Sunday = 0).
static int iso_week_count(int year, int jan1_wday) {
  if (jan1_wday == 4) return 53;
  if (is_leap_year(year) && jan1_wday == 3) return 53;
  return 52;
}

// ISO 8601 week number (Monday-start; W1 holds the year's first Thursday) —
// newlib's strftime has no %V, so it's computed. Arguments come straight from
// struct tm: yday 0-based, wday Sunday = 0, year the full year. The boundary
// cases are the definition working as specced: early January can read W52/W53
// (of the previous year), late December W01 (of the next).
int iso_week_number(int year, int yday, int wday) {
  int iso_wday = ((wday + 6) % 7) + 1;  // Monday = 1 .. Sunday = 7
  int week = (yday + 11 - iso_wday) / 7;
  int jan1_wday = (((wday - yday) % 7) + 7) % 7;
  if (week < 1) {
    // Step Jan 1 back over the previous year to learn its own Jan 1 weekday.
    int prev_jan1 = (((jan1_wday - (is_leap_year(year - 1) ? 2 : 1)) % 7) + 7) % 7;
    return iso_week_count(year - 1, prev_jan1);
  }
  if (week > iso_week_count(year, jan1_wday)) return 1;
  return week;
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

void date_strip_dow(int dow_position, const char* formatted, char* out, int out_size) {
  if (out_size <= 0) return;
  int at = date_dow_offset(dow_position, formatted);
  if (at < 0) {
    snprintf(out, out_size, "%s", formatted);
    return;
  }
  // The weekday and its separating space sit together at one end.
  int body_len = (int)strlen(formatted) - (DOW_LEN + 1);
  if (body_len < 0) body_len = 0;
  snprintf(out, out_size, "%.*s", body_len, formatted + (at == 0 ? DOW_LEN + 1 : 0));
}
