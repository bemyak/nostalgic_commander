'use strict';

// The UV and PCP complications show the peak over the coming window, not a
// calendar day max (which is mostly about the past by evening) and not the
// instant value (which reads 0 whenever the sun is low). How wide the window
// is, is a setting; this is its shipped default.
var DEFAULT_WINDOW_HOURS = 12;

// One row per value the watch consumes. The AppMessage dict and the sentinel
// fallback for anything unparsable are projections of this table. Adding a
// field is one row here, its parse code in parseForecast below, and the C-side
// walk row. AQI is filled from the second (air-quality) response, not the
// forecast JSON.
var WEATHER_FIELDS = [
  {key: 'WEATHER_TEMP', sentinel: -999},
  {key: 'WEATHER_COND', sentinel: -1},  // raw WMO weather code
  {key: 'WEATHER_AQI', sentinel: -1},
  {key: 'WEATHER_UV', sentinel: -1},
  {key: 'WEATHER_HUMIDITY', sentinel: -1},
  {key: 'WEATHER_WIND_DIRECTION', sentinel: -1},
  {key: 'WEATHER_WIND_SPEED', sentinel: -1},
  {key: 'WEATHER_PCP', sentinel: -1},
  {key: 'WEATHER_PRECIP_NOW', sentinel: -1},
  {key: 'WEATHER_HIGH', sentinel: -999},
  {key: 'WEATHER_LOW', sentinel: -999},
  {key: 'WEATHER_LOW_TOMORROW', sentinel: -999},
  {key: 'WEATHER_TEMP_HIGH_TOMORROW', sentinel: -999},
  {key: 'WEATHER_HI_HOUR_TODAY', sentinel: -1},
  {key: 'WEATHER_LO_HOUR_TODAY', sentinel: -1},
  {key: 'WEATHER_HI_HOUR_TOMORROW', sentinel: -1},
  {key: 'WEATHER_LO_HOUR_TOMORROW', sentinel: -1},
  {key: 'WEATHER_FETCHED_AT', sentinel: 0},  // UTC epoch seconds of the phone's fetch
];

// Every row at its sentinel — what the watch reads when the value can't be
// parsed out of the response.
function sentinelPayload() {
  var out = {};
  for (var i = 0; i < WEATHER_FIELDS.length; i++) {
    out[WEATHER_FIELDS[i].key] = WEATHER_FIELDS[i].sentinel;
  }
  return out;
}

// Position staleness is its own budget. The reuse window is just over one
// 30-minute fetch cadence, so every other fetch reliably reuses the previous
// fix instead of depending on timing jitter.
var GEOLOCATION_MAX_AGE_MS = 31 * 60 * 1000;

// The settings select stores raw option values as strings ('1' = metric,
// matching data.h's UNITS_METRIC — wire-contract.test.js pins that join).
// Before the first settings save the key is absent entirely, so this is the
// operative default: imperial. Numeric 1 is accepted for belt and braces.
function unitsFromClaySettings(settings) {
  var raw = settings ? settings['SETTINGS_UNITS'] : undefined;
  var metric = raw === '1' || raw === 1;
  return {tempUnit: metric ? 'celsius' : 'fahrenheit', windSpeedUnit: metric ? 'ms' : 'mph'};
}

// Finite numbers only: missing keys, nulls, and NaN all count as "no data".
// (The API's null probability-rows and the occasional absent field both land
// here instead of becoming a bogus reading.)
function num(value) { return typeof value === 'number' && isFinite(value) ? value : undefined; }

// The forecast-window select ships hours as strings; 0 is the page's "Now".
// Before the first settings save the key is absent, so: shipped default. A
// value off the select (a hand-edited store) parses or falls back.
function windowHoursFromClaySettings(settings) {
  var raw = settings ? settings['SETTINGS_WEATHER_WINDOW'] : undefined;
  if (raw === undefined) return DEFAULT_WINDOW_HOURS;
  var hours = parseInt(raw, 10);
  return isNaN(hours) ? DEFAULT_WINDOW_HOURS : hours;
}

// ComplicationDataSource ids (data.h) that read WEATHER_AQI: AQI and AQI + UV.
var AQI_SOURCES = ['16', '18'];
var SLOT_KEYS = ['SLOT_1', 'SLOT_2', 'SLOT_3', 'SLOT_4', 'SLOT_5', 'SLOT_6'];

// Whether any slot shows an AQI-bearing source. The slot selects store raw
// option values as strings. A slot missing from the settings (nothing saved
// yet) is unknown, so it counts as needing AQI.
function needsAqi(settings) {
  return SLOT_KEYS.some(function(key) {
    var raw = settings ? settings[key] : undefined;
    return raw === undefined || raw === null || AQI_SOURCES.indexOf(String(raw)) !== -1;
  });
}

var FORECAST_CURRENT_FIELDS = [
  'temperature_2m', 'weather_code', 'relative_humidity_2m', 'precipitation', 'wind_direction_10m',
  'wind_speed_10m'
];
var FORECAST_HOURLY_FIELDS = ['uv_index', 'precipitation_probability', 'temperature_2m'];
var FORECAST_DAILY_FIELDS = ['temperature_2m_max', 'temperature_2m_min'];

// Coordinates go out at ~1 km resolution; the weather doesn't need more.
function coordinate(value) { return Number(value).toFixed(2); }

function buildForecastUrl(lat, lon, units) {
  return 'https://api.open-meteo.com/v1/forecast?latitude=' + coordinate(lat) +
      '&longitude=' + coordinate(lon) + '&current=' + FORECAST_CURRENT_FIELDS.join(',') +
      '&timezone=auto&temperature_unit=' + units.tempUnit +
      '&wind_speed_unit=' + units.windSpeedUnit + '&hourly=' + FORECAST_HOURLY_FIELDS.join(',') +
      '&forecast_days=2&daily=' + FORECAST_DAILY_FIELDS.join(',');
}

function buildAqiUrl(lat, lon) {
  return 'https://air-quality-api.open-meteo.com/v1/air-quality?latitude=' + coordinate(lat) +
      '&longitude=' + coordinate(lon) + '&current=us_aqi';
}

// "YYYY-MM-DDTHH:MM" location-local wall-clock stamp as minutes on a
// timezone-free axis; NaN when it doesn't have that shape. Only differences
// between stamps from the same response are meaningful.
function localStampMinutes(stamp) {
  var m = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})/.exec(String(stamp));
  if (!m) return NaN;
  return Date.UTC(+m[1], +m[2] - 1, +m[3], +m[4], +m[5]) / 60000;
}

// Parse one Open-Meteo forecast response into a complete payload: every
// WEATHER_FIELDS key present, unparsable values at their sentinel. A response
// with neither temperature nor weather code is rejected. `nowMs` is the fetch
// instant reported as WEATHER_FETCHED_AT; the UV/PCP window is anchored on the
// response's own `current.time`. `windowHours` sizes it, 0 reading the
// in-progress hour alone.
function parseForecast(json, nowMs, windowHours) {
  if (windowHours === undefined) windowHours = DEFAULT_WINDOW_HOURS;
  var out = sentinelPayload();
  var current = json.current || {};
  if (num(current.temperature_2m) === undefined && num(current.weather_code) === undefined) {
    throw new Error('no usable current conditions');
  }
  out.WEATHER_FETCHED_AT = Math.floor(nowMs / 1000);

  var v = num(current.temperature_2m);
  if (v !== undefined) out.WEATHER_TEMP = Math.round(v);
  // The raw WMO code; the watch owns the word and precipitating facets.
  v = num(current.weather_code);
  if (v !== undefined) out.WEATHER_COND = Math.round(v);
  v = num(current.relative_humidity_2m);
  if (v !== undefined) out.WEATHER_HUMIDITY = Math.round(v);
  // Meteo FROM bearing, whole degrees; the watch flips it to the direction
  // the wind blows.
  v = num(current.wind_direction_10m);
  if (v !== undefined) out.WEATHER_WIND_DIRECTION = Math.round(v);
  v = num(current.wind_speed_10m);
  if (v !== undefined) out.WEATHER_WIND_SPEED = Math.round(v);
  // Tenths of mm over the past hour. Always mm — the watch displays the live
  // rate only in metric mode, so no unit param.
  v = num(current.precipitation);
  if (v !== undefined) out.WEATHER_PRECIP_NOW = Math.round(v * 10);

  var hourly = json.hourly || {};
  if (hourly.time) {
    // UV and PCP share the coming-window max; the in-progress hour counts.
    // The API nulls probability where no precip is forecast at all, so a
    // window of nulls still reads "no data".
    var nowMinutes = localStampMinutes(current.time);
    var windowStart = Math.floor(nowMinutes / 60) * 60;
    var windowEnd = nowMinutes + windowHours * 60;
    var uvArr = hourly.uv_index || [];
    var pcpArr = hourly.precipitation_probability || [];
    for (var i = 0; i < hourly.time.length; i++) {
      var ts = localStampMinutes(hourly.time[i]);
      if (!(ts >= windowStart && ts <= windowEnd)) continue;  // NaN-safe
      v = num(uvArr[i]);
      if (v !== undefined && v > out.WEATHER_UV) out.WEATHER_UV = v;
      v = num(pcpArr[i]);
      if (v !== undefined && v > out.WEATHER_PCP) out.WEATHER_PCP = v;
    }
    if (out.WEATHER_UV >= 0) out.WEATHER_UV = Math.round(out.WEATHER_UV);
    if (out.WEATHER_PCP >= 0) out.WEATHER_PCP = Math.round(out.WEATHER_PCP);

    // Event hours of the four extremes: each day's argmin/argmax over the
    // hourly curve, grouped by location-local date. Days without data stay at
    // the sentinel and the watch falls back to a plain LO/HI order.
    if (hourly.temperature_2m) {
      var dayOrder = [];
      var dayExtremes = {};
      for (var h = 0; h < hourly.time.length; h++) {
        var hourTemp = num(hourly.temperature_2m[h]);
        if (hourTemp === undefined) continue;
        var timestamp = String(hourly.time[h]);  // "YYYY-MM-DDTHH:MM"
        var dayKey = timestamp.substring(0, 10);
        if (!dayExtremes[dayKey]) {
          if (dayOrder.length === 2) continue;
          dayExtremes[dayKey] = {min: Infinity, minH: -1, max: -Infinity, maxH: -1};
          dayOrder.push(dayKey);
        }
        var hour = parseInt(timestamp.substring(11, 13), 10);
        if (isNaN(hour)) continue;
        var day = dayExtremes[dayKey];
        if (hourTemp < day.min) {
          day.min = hourTemp;
          day.minH = hour;
        }
        if (hourTemp > day.max) {
          day.max = hourTemp;
          day.maxH = hour;
        }
      }
      if (dayOrder.length > 0) {
        out.WEATHER_LO_HOUR_TODAY = dayExtremes[dayOrder[0]].minH;
        out.WEATHER_HI_HOUR_TODAY = dayExtremes[dayOrder[0]].maxH;
      }
      if (dayOrder.length > 1) {
        out.WEATHER_LO_HOUR_TOMORROW = dayExtremes[dayOrder[1]].minH;
        out.WEATHER_HI_HOUR_TOMORROW = dayExtremes[dayOrder[1]].maxH;
      }
    }
  }

  // The watch formatter sinks the whole HI/LO readout when any extreme is
  // missing — keep its sentinel predictable by sinking all four together.
  var daily = json.daily || {};
  var maxes = daily.temperature_2m_max || [];
  var mins = daily.temperature_2m_min || [];
  var high = num(maxes[0]);
  var low = num(mins[0]);
  var lowTmrw = num(mins[1]);
  var highTmrw = num(maxes[1]);
  if (high !== undefined && low !== undefined && lowTmrw !== undefined && highTmrw !== undefined) {
    out.WEATHER_HIGH = Math.round(high);
    out.WEATHER_LOW = Math.round(low);
    out.WEATHER_LOW_TOMORROW = Math.round(lowTmrw);
    out.WEATHER_TEMP_HIGH_TOMORROW = Math.round(highTmrw);
  }

  return out;
}

// The fetch chain's bookkeeping, as pure state: index.js keeps only the timer
// and XHR wiring and asks this what to do. `inFlight` runs from a fetch's
// start to its final send or failure, except while a retry delay is armed
// (`retryScheduled`); `pending` is a trigger that arrived mid-flight, owed
// exactly one fresh fetch.
function createFetchGuard() {
  var state = {inFlight: false, pending: false, retryScheduled: false};
  return {
    state: state,
    // A trigger: true to start a fetch now (superseding any armed retry),
    // false when one is in flight and the trigger is queued.
    begin: function() {
      if (state.inFlight) {
        state.pending = true;
        return false;
      }
      state.inFlight = true;
      state.retryScheduled = false;
      return true;
    },
    // The armed retry timer fired.
    resume: function() {
      state.inFlight = true;
      state.retryScheduled = false;
    },
    // The chain is over; true when a queued trigger's fetch must start now
    // (the guard stays held for it).
    end: function() {
      state.inFlight = false;
      if (!state.pending) return false;
      state.pending = false;
      state.inFlight = true;
      return true;
    },
    // A failed attempt with budget left: true when a retry delay should be
    // armed, false when a queued trigger took over instead.
    retry: function() {
      if (this.end()) return false;
      state.retryScheduled = true;
      return true;
    },
    // A result fetched while a trigger is queued predates it (settings may
    // have changed); it is neither sent nor cached.
    shouldSendResult: function() { return !state.pending; },
  };
}

// The air-quality response is a separate backend; a parse failure means "no
// data", never "perfectly clean air".
function parseAqi(json) {
  var v = json.current ? num(json.current.us_aqi) : undefined;
  return v === undefined ? -1 : Math.round(v);
}

module.exports = {
  WEATHER_FIELDS : WEATHER_FIELDS,
  DEFAULT_WINDOW_HOURS : DEFAULT_WINDOW_HOURS,
  windowHoursFromClaySettings : windowHoursFromClaySettings,
  sentinelPayload : sentinelPayload,
  buildForecastUrl : buildForecastUrl,
  buildAqiUrl : buildAqiUrl,
  GEOLOCATION_MAX_AGE_MS : GEOLOCATION_MAX_AGE_MS,
  needsAqi : needsAqi,
  AQI_SOURCES : AQI_SOURCES,
  unitsFromClaySettings : unitsFromClaySettings,
  parseForecast : parseForecast,
  parseAqi : parseAqi,
  createFetchGuard : createFetchGuard,
};
