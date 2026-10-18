'use strict';

// Host tests for the phone-side weather contract (src/pkjs/weather.js) —
// pure node, no Pebble runtime needed.

const {test} = require('node:test');
const assert = require('node:assert/strict');
const weather = require('../../src/pkjs/weather.js');

const NOW = Date.UTC(2026, 7, 8, 12, 30);

// parseForecast's third parameter is the forecast-window width in hours; the
// tests here pass it explicitly wherever the width itself is the point, and
// otherwise rely on the 12h default.

// Every WEATHER_FIELDS row present in the payload.
const isComplete = (payload) => weather.WEATHER_FIELDS.every(f => payload[f.key] !== undefined);

// Consecutive API-style hour stamps: location-local "YYYY-MM-DDTHH:MM".
function isoHours(startMs, count) {
  const out = [];
  for (let i = 0; i < count; i++)
    out.push(new Date(startMs + i * 3600000).toISOString().slice(0, 16));
  return out;
}

// A full, realistic two-day response; individual tests wiggle one field.
function fullResponse() {
  const times = isoHours(Date.UTC(2026, 7, 8, 0, 0), 48);
  const temps = times.map(() => 15);
  temps[5] = 8;    // today low at 05:00
  temps[15] = 27;  // today high at 15:00
  temps[27] = 4;   // tomorrow low at 03:00
  temps[45] = 22;  // tomorrow high at 21:00
  const uv = times.map(() => 0);
  uv[5] = 9;                          // 05:00 — outside the coming window (before now-1h), ignored
  uv[15] = 6.3;                       // 15:00 — in window, becomes the max
  const pcp = times.map(() => null);  // API nulls dry hours
  pcp[13] = 42.4;                     // in window
  return {
    current: {
      time: '2026-08-08T12:30',
      temperature_2m: 22.6,
      weather_code: 61,
      relative_humidity_2m: 54.4,
      precipitation: 0.35,
      wind_direction_10m: 269.6,
      wind_speed_10m: 12.4,
    },
    hourly: {
      time: times,
      temperature_2m: temps,
      uv_index: uv,
      precipitation_probability: pcp,
    },
    daily: {temperature_2m_max: [28.4, 26.4], temperature_2m_min: [11.6, 9.6]},
  };
}

test('field table declares every key once, with the contract sentinel', () => {
  const fields = weather.WEATHER_FIELDS;
  assert.equal(fields.length, 18);
  assert.equal(new Set(fields.map(f => f.key)).size, 18);
  const sentinel = Object.fromEntries(fields.map(f => [f.key, f.sentinel]));
  for (const k
           of ['WEATHER_TEMP', 'WEATHER_HIGH', 'WEATHER_LOW', 'WEATHER_LOW_TOMORROW',
               'WEATHER_TEMP_HIGH_TOMORROW'])
    assert.equal(sentinel[k], -999, k);
  for (const k
           of ['WEATHER_AQI', 'WEATHER_UV', 'WEATHER_HUMIDITY', 'WEATHER_PCP', 'WEATHER_COND',
               'WEATHER_PRECIP_NOW', 'WEATHER_WIND_DIRECTION', 'WEATHER_WIND_SPEED',
               'WEATHER_HI_HOUR_TODAY', 'WEATHER_LO_HOUR_TODAY', 'WEATHER_HI_HOUR_TOMORROW',
               'WEATHER_LO_HOUR_TOMORROW'])
    assert.equal(sentinel[k], -1, k);
  assert.equal(sentinel['WEATHER_FETCHED_AT'], 0);
  assert.ok(isComplete(weather.sentinelPayload()));
});

test('parseForecast maps and rounds the current block', () => {
  const out = weather.parseForecast(fullResponse(), NOW);
  assert.equal(out.WEATHER_TEMP, 23);
  assert.equal(out.WEATHER_COND, 61);  // the raw code; the watch maps the word
  assert.equal(out.WEATHER_HUMIDITY, 54);
  assert.equal(out.WEATHER_WIND_DIRECTION, 270);
  assert.equal(out.WEATHER_WIND_SPEED, 12);
  assert.equal(out.WEATHER_PRECIP_NOW, 4);  // 0.35mm → tenths
  assert.ok(isComplete(out));
});

test('parseForecast falls back to sentinels per field', () => {
  const out = weather.parseForecast({current: {temperature_2m: 20}}, NOW);
  assert.equal(out.WEATHER_TEMP, 20);
  assert.equal(out.WEATHER_COND, -1);
  assert.equal(out.WEATHER_HUMIDITY, -1);
  assert.equal(out.WEATHER_UV, -1);
  assert.equal(out.WEATHER_HIGH, -999);
  assert.equal(out.WEATHER_HI_HOUR_TODAY, -1);
  assert.ok(isComplete(out));
});

test('a response with neither temperature nor weather code is a failure', () => {
  assert.throws(() => weather.parseForecast({}, NOW));
  assert.throws(() => weather.parseForecast({current: {}}, NOW));
  assert.throws(
      () => weather.parseForecast({current: {temperature_2m: null, weather_code: 'x'}}, NOW));
  assert.throws(() => weather.parseForecast({current: {relative_humidity_2m: 50}}, NOW));
  assert.doesNotThrow(() => weather.parseForecast({current: {weather_code: 3}}, NOW));
  assert.doesNotThrow(() => weather.parseForecast({current: {temperature_2m: 0}}, NOW));
});

test('WEATHER_FETCHED_AT is the fetch instant in UTC epoch seconds', () => {
  assert.equal(weather.parseForecast(fullResponse(), NOW + 999).WEATHER_FETCHED_AT, NOW / 1000);
});

test('the UV/PCP window follows the location clock whatever the process timezone', () => {
  // Location-local stamps; the same wall-clock hours must select the same
  // window under any TZ the test process (or the phone) runs in.
  const json = fullResponse();
  json.hourly.uv_index[12] = 8;
  const unrelatedNow = Date.UTC(2030, 0, 1);  // the fetch instant must not move the window
  const expected = weather.parseForecast(json, unrelatedNow, 12);
  assert.equal(expected.WEATHER_UV, 8);
  assert.equal(expected.WEATHER_PCP, 42);

  const saved = process.env.TZ;
  try {
    for (const tz of ['UTC', 'Pacific/Kiritimati', 'America/Los_Angeles', 'Asia/Kolkata']) {
      process.env.TZ = tz;
      assert.deepEqual(weather.parseForecast(json, unrelatedNow, 12), expected, tz);
    }
  } finally {
    if (saved === undefined)
      delete process.env.TZ;
    else
      process.env.TZ = saved;
  }
});

test('a response without current.time reads no UV/PCP window', () => {
  const json = fullResponse();
  delete json.current.time;
  const out = weather.parseForecast(json, NOW);
  assert.equal(out.WEATHER_UV, -1);
  assert.equal(out.WEATHER_PCP, -1);
});

test('UV and PCP are maxima over the coming window, including the in-progress hour', () => {
  const json = fullResponse();
  json.hourly.uv_index[12] = 8;  // 12:00 — the partly-elapsed hour counts
  const out = weather.parseForecast(json, NOW);
  assert.equal(out.WEATHER_UV, 8);    // 6.3 at 15:00 also in-window, but 8 wins
  assert.equal(out.WEATHER_PCP, 42);  // nulls ignored, 42.4 rounded

  // Explicit 12 must behave exactly like the default.
  assert.equal(weather.parseForecast(json, NOW, 12).WEATHER_UV, 8);
  // And the out-of-window spike at 05:00 was indeed excluded: without idx 12,
  // 6.3 stands, not 9.
  delete json.hourly.uv_index[12];
  assert.equal(weather.parseForecast(json, NOW).WEATHER_UV, 6);
});

test('a "Now" window reads the in-progress hour only', () => {
  const json = fullResponse();
  json.hourly.uv_index[12] = 3;
  const out = weather.parseForecast(json, NOW, 0);
  assert.equal(out.WEATHER_UV, 3);    // the 12:00 bucket alone; 15:00's 6.3 excluded
  assert.equal(out.WEATHER_PCP, -1);  // the 42.4 sits at 13:00 — just out of reach
});

test('a "Now" window at exactly HH:00 reads that hour alone, not the one before', () => {
  const json = fullResponse();
  json.current.time = '2026-08-08T12:00';
  json.hourly.uv_index[11] = 9;  // the previous hour
  json.hourly.uv_index[12] = 3;
  json.hourly.uv_index[13] = 7;  // the next hour
  const out = weather.parseForecast(json, NOW, 0);
  assert.equal(out.WEATHER_UV, 3);
  assert.equal(out.WEATHER_PCP, -1);
});

test('short windows bound the maxima tightly', () => {
  const out2 = weather.parseForecast(fullResponse(), NOW, 2);
  assert.equal(out2.WEATHER_PCP, 42);  // 13:00 inside a 2h window
  assert.equal(out2.WEATHER_UV, 0);    // the in-window hours read UV 0; 15:00's 6.3 stays out
  const out24 = weather.parseForecast(fullResponse(), NOW, 24);
  assert.equal(out24.WEATHER_UV, 6);  // 15:00 well inside a 24h window
});

test('windowHoursFromClaySettings: 12h default in every no-settings shape', () => {
  assert.equal(weather.windowHoursFromClaySettings(undefined), 12);
  assert.equal(weather.windowHoursFromClaySettings({}), 12);  // no Clay save yet
  assert.equal(weather.windowHoursFromClaySettings({SETTINGS_WEATHER_WINDOW: 'bogus'}), 12);
});

test('windowHoursFromClaySettings: the select values read through, string or numeric', () => {
  assert.equal(weather.windowHoursFromClaySettings({SETTINGS_WEATHER_WINDOW: '2'}), 2);
  assert.equal(weather.windowHoursFromClaySettings({SETTINGS_WEATHER_WINDOW: 24}), 24);
  assert.equal(weather.windowHoursFromClaySettings({SETTINGS_WEATHER_WINDOW: '0'}), 0);  // Now
});

test('extremes sink together when any one is missing', () => {
  const json = fullResponse();
  json.daily.temperature_2m_max = [28.4];  // tomorrow's max absent
  const out = weather.parseForecast(json, NOW);
  for (const k
           of ['WEATHER_HIGH', 'WEATHER_LOW', 'WEATHER_LOW_TOMORROW', 'WEATHER_TEMP_HIGH_TOMORROW'])
    assert.equal(out[k], -999, k);
  // The rest of the payload still parses.
  assert.equal(out.WEATHER_TEMP, 23);
});

test('extreme event hours are per-day argmin/argmax', () => {
  const out = weather.parseForecast(fullResponse(), NOW);
  assert.equal(out.WEATHER_LO_HOUR_TODAY, 5);
  assert.equal(out.WEATHER_HI_HOUR_TODAY, 15);
  assert.equal(out.WEATHER_LO_HOUR_TOMORROW, 3);
  assert.equal(out.WEATHER_HI_HOUR_TOMORROW, 21);
});

test('a single-day series leaves tomorrow hours unknown', () => {
  const json = fullResponse();
  json.hourly.time = json.hourly.time.slice(0, 24);
  json.hourly.temperature_2m = json.hourly.temperature_2m.slice(0, 24);
  const out = weather.parseForecast(json, NOW);
  assert.equal(out.WEATHER_LO_HOUR_TODAY, 5);
  assert.equal(out.WEATHER_HI_HOUR_TODAY, 15);
  assert.equal(out.WEATHER_LO_HOUR_TOMORROW, -1);
  assert.equal(out.WEATHER_HI_HOUR_TOMORROW, -1);
});

test('the WMO code crosses the wire untouched, rounded when fractional', () => {
  const json = fullResponse();
  json.current.weather_code = 61.6;
  assert.equal(weather.parseForecast(json, NOW).WEATHER_COND, 62);
  json.current.weather_code = 96;
  assert.equal(weather.parseForecast(json, NOW).WEATHER_COND, 96);

  // Missing, null, or non-finite codes are the sentinel, not an invented word
  for (const junk of [undefined, null, NaN, '3']) {
    json.current.weather_code = junk;
    assert.equal(weather.parseForecast(json, NOW).WEATHER_COND, -1, `${junk}`);
  }
});

test('parseAqi rounds real values and reads junk as no-data', () => {
  assert.equal(weather.parseAqi({current: {us_aqi: 42.6}}), 43);
  assert.equal(weather.parseAqi({current: {us_aqi: null}}), -1);  // null is not clean air
  assert.equal(weather.parseAqi({}), -1);
});

test('unitsFromClaySettings: imperial is the operative default, in every no-settings shape', () => {
  const imperial = {tempUnit: 'fahrenheit', windSpeedUnit: 'mph'};
  assert.deepEqual(weather.unitsFromClaySettings(undefined), imperial);
  assert.deepEqual(weather.unitsFromClaySettings({}), imperial);  // no Clay save yet
  assert.deepEqual(weather.unitsFromClaySettings({SETTINGS_UNITS: '0'}), imperial);
  assert.deepEqual(weather.unitsFromClaySettings({SETTINGS_UNITS: 'bogus'}), imperial);
});

test('unitsFromClaySettings: metric exactly on the select\'s 1, string or numeric', () => {
  const metric = {tempUnit: 'celsius', windSpeedUnit: 'ms'};
  assert.deepEqual(weather.unitsFromClaySettings({SETTINGS_UNITS: '1'}), metric);
  assert.deepEqual(weather.unitsFromClaySettings({SETTINGS_UNITS: 1}), metric);
});

const METRIC = {
  tempUnit: 'celsius',
  windSpeedUnit: 'ms'
};

const IMPERIAL = {
  tempUnit: 'fahrenheit',
  windSpeedUnit: 'mph'
};

test('forecast URL rounds coordinates and carries units', () => {
  const url = new URL(weather.buildForecastUrl(48.858844, -2.294351, METRIC));
  assert.equal(url.searchParams.get('latitude'), '48.86');
  assert.equal(url.searchParams.get('longitude'), '-2.29');
  assert.equal(url.searchParams.get('temperature_unit'), 'celsius');
  assert.equal(url.searchParams.get('wind_speed_unit'), 'ms');
  assert.equal(url.searchParams.get('timezone'), 'auto');
  const aqi = new URL(weather.buildAqiUrl(48.858844, -2.294351));
  assert.equal(aqi.searchParams.get('latitude'), '48.86');
  assert.equal(aqi.searchParams.get('longitude'), '-2.29');
  assert.equal(aqi.searchParams.get('current'), 'us_aqi');
});

test('longitude goes to the watch in hundredths of a degree, west negative', () => {
  assert.equal(weather.longitudeCenti(24.9384), 2494);
  assert.equal(weather.longitudeCenti(-2.294351), -229);
  assert.equal(weather.longitudeCenti(-73.9856), -7399);
  assert.equal(weather.longitudeCenti(-179.999), -18000);
  assert.equal(weather.longitudeCenti(180), 18000);
});

test('every field parseForecast reads is requested from the API', () => {
  const read = {current: new Set(), hourly: new Set(), daily: new Set()};
  const json = fullResponse();
  for (const block of Object.keys(read)) {
    json[block] = new Proxy(json[block], {
      get(target, prop) {
        if (typeof prop === 'string') read[block].add(prop);
        return target[prop];
      },
    });
  }
  weather.parseForecast(json, NOW);
  const url = new URL(weather.buildForecastUrl(1, 2, IMPERIAL));
  for (const block of Object.keys(read)) {
    const requested = url.searchParams.get(block).split(',');
    read[block].delete('time');  // always returned, never requested
    assert.ok(read[block].size > 0, block);
    for (const field of read[block]) assert.ok(requested.includes(field), `${block}.${field}`);
  }
});

test('a trigger mid-flight queues exactly one rerun however many arrive', () => {
  const guard = weather.createFetchGuard();
  assert.equal(guard.begin(), true);
  assert.equal(guard.begin(), false);
  assert.equal(guard.begin(), false);
  assert.equal(guard.end(), true);   // the single rerun, guard held for it
  assert.equal(guard.end(), false);  // and the rerun's own end owes nothing
  assert.equal(guard.begin(), true);
});

test('end without a queued trigger releases the guard and owes no rerun', () => {
  const guard = weather.createFetchGuard();
  guard.begin();
  assert.equal(guard.end(), false);
  assert.equal(guard.state.inFlight, false);
  assert.equal(guard.begin(), true);
});

test('a result is not sent while a rerun is pending', () => {
  const guard = weather.createFetchGuard();
  guard.begin();
  assert.equal(guard.shouldSendResult(), true);
  guard.begin();
  assert.equal(guard.shouldSendResult(), false);
  assert.equal(guard.end(), true);
  assert.equal(guard.shouldSendResult(), true);  // the rerun's result is current
});

test('a failure with budget left arms a retry, and a fresh trigger supersedes it', () => {
  const guard = weather.createFetchGuard();
  guard.begin();
  assert.equal(guard.retry(), true);
  assert.equal(guard.state.retryScheduled, true);
  assert.equal(guard.state.inFlight, false);
  assert.equal(guard.begin(), true);  // not queued: the armed retry is replaced
  assert.equal(guard.state.retryScheduled, false);
  assert.equal(guard.state.pending, false);
});

test('a retry timer firing holds the guard again', () => {
  const guard = weather.createFetchGuard();
  guard.begin();
  guard.retry();
  guard.resume();
  assert.equal(guard.state.retryScheduled, false);
  assert.equal(guard.begin(), false);
});

test('a failure with a trigger queued hands over to the rerun instead of a retry', () => {
  const guard = weather.createFetchGuard();
  guard.begin();
  guard.begin();
  assert.equal(guard.retry(), false);
  assert.equal(guard.state.retryScheduled, false);
  assert.equal(guard.state.inFlight, true);
  assert.equal(guard.state.pending, false);
});

test('needsAqi: AQI and AQI + UV slots need the request, string or int', () => {
  const slots = (...v) => Object.fromEntries(v.map((x, i) => ['SLOT_' + (i + 1), x]));
  assert.equal(weather.needsAqi(slots('5', '2', '1', '6', '9', '23')), false);
  assert.equal(weather.needsAqi(slots('16', '2', '1', '6', '9', '23')), true);
  assert.equal(weather.needsAqi(slots('5', '2', '1', '6', '9', '18')), true);
  assert.equal(weather.needsAqi(slots(5, 2, 1, 6, 9, 16)), true);
  assert.equal(weather.needsAqi(slots(5, 2, 1, 6, 9, 18)), true);
  assert.equal(weather.needsAqi(slots(5, 2, 1, 6, 9, 17)), false);  // UV alone: forecast data
  assert.equal(weather.needsAqi(slots('5', '2', '1', '6', '9', '160')), false);
});

test('needsAqi: unknown slots default to needing AQI', () => {
  for (const missing of [undefined, null, {}, {SETTINGS_UNITS: '1'}])
    assert.equal(weather.needsAqi(missing), true);
  const partial = {SLOT_1: '5', SLOT_2: '2', SLOT_3: '1', SLOT_4: '6', SLOT_5: '9'};
  assert.equal(weather.needsAqi(partial), true);
});

test('a skipped AQI request leaves the field at its no-data sentinel', () => {
  const out = weather.parseForecast(fullResponse(), NOW);
  assert.equal(out.WEATHER_AQI, -1);
  assert.equal(weather.sentinelPayload().WEATHER_AQI, -1);
});

test('the geolocation fix is reusable for just over one 30-minute cadence', () => {
  assert.equal(weather.GEOLOCATION_MAX_AGE_MS, 31 * 60 * 1000);
});
