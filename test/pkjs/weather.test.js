'use strict';

// Host tests for the phone-side weather contract (src/pkjs/weather.js) —
// pure node, no Pebble runtime needed.

const {test} = require('node:test');
const assert = require('node:assert/strict');
const weather = require('../../src/pkjs/weather.js');

const NOW = Date.UTC(2026, 7, 8, 12, 30);

// Consecutive API-style hour stamps; ISO strings positionally match the
// "YYYY-MM-DDTHH:MM" shape the parser slices.
function isoHours(startMs, count) {
  const out = [];
  for (let i = 0; i < count; i++) out.push(new Date(startMs + i * 3600000).toISOString());
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
  assert.equal(fields.length, 17);
  assert.equal(new Set(fields.map(f => f.key)).size, 17);
  const sentinel = Object.fromEntries(fields.map(f => [f.key, f.sentinel]));
  for (const k
           of ['WEATHER_TEMP', 'WEATHER_HIGH', 'WEATHER_LOW', 'WEATHER_LOW_TOMORROW',
               'WEATHER_TEMP_HIGH_TOMORROW'])
    assert.equal(sentinel[k], -999, k);
  assert.equal(sentinel.WEATHER_COND, '--');
  for (const k
           of ['WEATHER_AQI', 'WEATHER_UV', 'WEATHER_HUMIDITY', 'WEATHER_PCP', 'WEATHER_PRECIP_NOW',
               'WEATHER_WIND_DIRECTION', 'WEATHER_WIND_SPEED', 'WEATHER_HI_AT_TODAY',
               'WEATHER_LO_AT_TODAY', 'WEATHER_HI_AT_TOMORROW', 'WEATHER_LO_AT_TOMORROW'])
    assert.equal(sentinel[k], -1, k);
  // The sentinel payload is complete by construction.
  assert.ok(weather.isCompleteWeatherPayload(weather.sentinelPayload()));
});

test('parseForecast maps and rounds the current block', () => {
  const out = weather.parseForecast(fullResponse(), NOW);
  assert.equal(out.WEATHER_TEMP, 23);
  assert.equal(out.WEATHER_COND, 'RAIN');
  assert.equal(out.WEATHER_HUMIDITY, 54);
  assert.equal(out.WEATHER_WIND_DIRECTION, 270);
  assert.equal(out.WEATHER_WIND_SPEED, 12);
  assert.equal(out.WEATHER_PRECIP_NOW, 4);  // 0.35mm → tenths
  assert.ok(weather.isCompleteWeatherPayload(out));
});

test('parseForecast falls back to sentinels per field', () => {
  const out = weather.parseForecast({}, NOW);
  assert.equal(out.WEATHER_TEMP, -999);
  // A missing weather code reads as CLD, not the sentinel — the face always
  // shows a condition word for a parsed forecast.
  assert.equal(out.WEATHER_COND, 'CLD');
  assert.equal(out.WEATHER_HUMIDITY, -1);
  assert.equal(out.WEATHER_UV, -1);
  assert.equal(out.WEATHER_HIGH, -999);
  assert.equal(out.WEATHER_HI_AT_TODAY, -1);
  assert.ok(weather.isCompleteWeatherPayload(out));
});

test('UV and PCP are maxima over the coming window, including the in-progress hour', () => {
  const json = fullResponse();
  json.hourly.uv_index[12] = 8;  // 12:00 — the partly-elapsed hour counts
  const out = weather.parseForecast(json, NOW);
  assert.equal(out.WEATHER_UV, 8);    // 6.3 at 15:00 also in-window, but 8 wins
  assert.equal(out.WEATHER_PCP, 42);  // nulls ignored, 42.4 rounded
  // And the out-of-window spike at 05:00 was indeed excluded: without idx 12,
  // 6.3 stands, not 9.
  delete json.hourly.uv_index[12];
  assert.equal(weather.parseForecast(json, NOW).WEATHER_UV, 6);
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

test('extreme events travel as per-day argmin/argmax instants', () => {
  const out = weather.parseForecast(fullResponse(), NOW);
  const epoch = idx => Math.round(new Date(fullResponse().hourly.time[idx]).getTime() / 1000);
  assert.equal(out.WEATHER_LO_AT_TODAY, epoch(5));
  assert.equal(out.WEATHER_HI_AT_TODAY, epoch(15));
  assert.equal(out.WEATHER_LO_AT_TOMORROW, epoch(27));
  assert.equal(out.WEATHER_HI_AT_TOMORROW, epoch(45));
});

test('a single-day series leaves tomorrow hours unknown', () => {
  const json = fullResponse();
  json.hourly.time = json.hourly.time.slice(0, 24);
  json.hourly.temperature_2m = json.hourly.temperature_2m.slice(0, 24);
  const out = weather.parseForecast(json, NOW);
  const epoch = idx => Math.round(new Date(json.hourly.time[idx]).getTime() / 1000);
  assert.equal(out.WEATHER_LO_AT_TODAY, epoch(5));
  assert.equal(out.WEATHER_HI_AT_TODAY, epoch(15));
  assert.equal(out.WEATHER_LO_AT_TOMORROW, -1);
  assert.equal(out.WEATHER_HI_AT_TOMORROW, -1);
});

test('wmoCondition maps ranges and boundaries', () => {
  const cases = [
    [0, 'SUN'],
    [3, 'CLD'],
    [45, 'FOG'],
    [48, 'FOG'],
    [51, 'RAIN'],
    [55, 'RAIN'],
    [66, 'CLD'],
    [71, 'SNOW'],
    [82, 'RAIN'],
    [86, 'SNOW'],
    [95, 'TSTM'],
    [99, 'TSTM'],
  ];
  for (const [code, word] of cases) assert.equal(weather.wmoCondition(code), word, `${code}`);
  assert.equal(weather.wmoCondition(undefined), 'CLD');
  assert.equal(weather.wmoCondition(NaN), 'CLD');
  assert.equal(weather.wmoCondition(4), 'CLD');
});

test('parseAqi rounds real values and reads junk as no-data', () => {
  assert.equal(weather.parseAqi({current: {us_aqi: 42.6}}), 43);
  assert.equal(weather.parseAqi({current: {us_aqi: null}}), -1);  // null is not clean air
  assert.equal(weather.parseAqi({}), -1);
});

test('a cache from an older build fails completeness', () => {
  const full = weather.parseForecast(fullResponse(), NOW);
  assert.ok(weather.isCompleteWeatherPayload(full));
  delete full.WEATHER_UV;
  assert.ok(!weather.isCompleteWeatherPayload(full));
});
