'use strict';

// The UV complication shows the peak over the coming window, not a calendar
// day max (which is mostly about the past by evening) and not the instant
// value (which reads 0 whenever the sun is low). PCP rides the same window.
var UV_WINDOW_HOURS = 12;

// One row per value the watch consumes. The AppMessage dict, the sentinel
// fallback for anything unparsable, and the cached-payload completeness check
// are all projections of this table. Adding a field is one row here, its
// parse code in parseForecast below, and the C-side walk row. AQI is filled
// from the second (air-quality) response, not the forecast JSON.
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

// The completeness check for a cached payload: a cache written by an older
// build lacks the newer keys and fails this, triggering one completing fetch.
function isCompleteWeatherPayload(payload) {
  return WEATHER_FIELDS.every(function(f) { return payload[f.key] !== undefined; });
}

// The phone-side weather cache's freshness window. The watch keeps the same
// figure in seconds (messaging.h's WEATHER_CACHE_MAX_AGE_S) —
// wire-contract.test.js pins the two in agreement.
var WEATHER_CACHE_MAX_AGE_MS = 30 * 60 * 1000;

// Position staleness is its own budget, not the payload cache's: a cached fix
// plus a cached payload can describe where you were up to ~1 h ago, so raising
// the TTL must not silently widen the accepted GPS age.
var GEOLOCATION_MAX_AGE_MS = 30 * 60 * 1000;

// Fresh = fetched within the window. Garbage timestamps (NaN, undefined) are
// never fresh, nor is a future one; at exactly the window edge the cache is
// already stale.
function isFreshWeatherCache(fetchedAtMs, nowMs) {
  if (typeof fetchedAtMs !== 'number' || !isFinite(fetchedAtMs) || typeof nowMs !== 'number' ||
      !isFinite(nowMs)) {
    return false;
  }
  var age = nowMs - fetchedAtMs;
  return age >= 0 && age < WEATHER_CACHE_MAX_AGE_MS;
}

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

// Parse one Open-Meteo forecast response into a complete payload: every
// WEATHER_FIELDS key present, unparsable values at their sentinel. `nowMs`
// pins "now" for the UV/PCP window (tests pass a fixed instant).
function parseForecast(json, nowMs) {
  var out = sentinelPayload();
  var current = json.current || {};

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
    var windowStart = nowMs - 3600 * 1000;
    var windowEnd = nowMs + UV_WINDOW_HOURS * 3600 * 1000;
    var uvArr = hourly.uv_index || [];
    var pcpArr = hourly.precipitation_probability || [];
    for (var i = 0; i < hourly.time.length; i++) {
      var ts = new Date(hourly.time[i]).getTime();
      if (!(ts >= windowStart && ts <= windowEnd)) continue;  // NaN-safe
      v = num(uvArr[i]);
      if (v !== undefined && v > out.WEATHER_UV) out.WEATHER_UV = v;
      v = num(pcpArr[i]);
      if (v !== undefined && v > out.WEATHER_PCP) out.WEATHER_PCP = v;
    }
    if (out.WEATHER_UV >= 0) out.WEATHER_UV = Math.round(out.WEATHER_UV);
    if (out.WEATHER_PCP >= 0) out.WEATHER_PCP = Math.round(out.WEATHER_PCP);

    // Event hours of the four extremes: each day's argmin/argmax over the
    // hourly curve, grouped by phone-local date (timezone=auto). Unknown —
    // or absent — days stay at the sentinel and the watch falls back to a
    // plain LO/HI order.
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

// The air-quality response is a separate backend; a parse failure means "no
// data", never "perfectly clean air".
function parseAqi(json) {
  var v = json.current ? num(json.current.us_aqi) : undefined;
  return v === undefined ? -1 : Math.round(v);
}

module.exports = {
  WEATHER_FIELDS : WEATHER_FIELDS,
  UV_WINDOW_HOURS : UV_WINDOW_HOURS,
  sentinelPayload : sentinelPayload,
  isCompleteWeatherPayload : isCompleteWeatherPayload,
  WEATHER_CACHE_MAX_AGE_MS : WEATHER_CACHE_MAX_AGE_MS,
  GEOLOCATION_MAX_AGE_MS : GEOLOCATION_MAX_AGE_MS,
  isFreshWeatherCache : isFreshWeatherCache,
  unitsFromClaySettings : unitsFromClaySettings,
  parseForecast : parseForecast,
  parseAqi : parseAqi,
};
