'use strict';

// The wire contract across the language boundary: the weather field table
// exists once in C (src/c/messaging.c, s_weather_fields) and once in JS
// (src/pkjs/weather.js, WEATHER_FIELDS), the cache TTL exists once in
// seconds (messaging.h) and once in milliseconds (weather.js), and package.json's messageKeys list
// the keys both halves speak. These tests parse the C side as text and demand equality, so a
// one-sided rename or sentinel drift fails here instead of surfacing as "--" on the wrist. The
// same scrape idiom pins the other cross-language seams: Clay's offered option ids and labels
// against the ComplicationDataSource enum, and the Clay shipped defaults against the C boots.

const {test} = require('node:test');
const assert = require('node:assert/strict');

const weather = require('../../src/pkjs/weather.js');
const config = require('../../src/pkjs/config.js');
const scrape = require('./contract-scrape.js');

const readRepoFile = scrape.readRepoFile;

// The rows of a MessageField table: {&MESSAGE_KEY_<NAME>, PERSIST_KEY_<NAME>,
// &target (or NULL for the slot table), <sentinel>[, wrapped across lines]}.
// Scoped to one table so identically-shaped rows elsewhere don't pollute it.
function parseCMessageRows(messagingC, arrayName) {
  const tableRe = new RegExp(arrayName + '\\[[^\\]]*\\]\\s*=\\s*\\{');
  const tableMatch = tableRe.exec(messagingC);
  assert.ok(tableMatch, `${arrayName} table not found in messaging.c`);
  const start = tableMatch.index;
  const table = messagingC.slice(start, messagingC.indexOf('};', start));
  const rowRe =
      /\{&MESSAGE_KEY_([A-Z0-9_]+),\s*(PERSIST_KEY_[A-Z0-9_]+),\s*(?:&[A-Za-z0-9_.]+|NULL),\s*(-?\d+)\s*\}/g;
  const rows = new Map();
  let m;
  while ((m = rowRe.exec(table)) !== null) {
    assert.ok(!rows.has(m[1]), `duplicate C row for ${m[1]}`);
    rows.set(m[1], {persist: m[2], sentinel: Number(m[3])});
  }
  assert.ok(rows.size > 0, `${arrayName}: row regex matched nothing — pattern drift?`);
  return rows;
}

function cWeatherRows() { return parseCMessageRows(messagingC, 's_weather_fields'); }

// The message → persist pairing IS the on-disk format: a swapped PERSIST_KEY
// passes both per-side suites and corrupts the cache across versions. Written
// out, not derived: the exceptions (COND ↔ *_COND_CODE, *_TEMP_HIGH_TOMORROW
// ↔ *_HIGH_TOMORROW, *_SHORT_DATE_FORMAT ↔ *_SHORT_DATE, *_DOW_POSITION ↔
// *_DOW) are exactly the pairs name-mangling would get wrong.
const EXPECTED_PERSIST = new Map([
  ['WEATHER_TEMP', 'PERSIST_KEY_WEATHER_TEMP'],
  ['WEATHER_COND', 'PERSIST_KEY_WEATHER_COND_CODE'],
  ['WEATHER_AQI', 'PERSIST_KEY_WEATHER_AQI'],
  ['WEATHER_UV', 'PERSIST_KEY_WEATHER_UV'],
  ['WEATHER_UV_NOW', 'PERSIST_KEY_WEATHER_UV_NOW'],
  ['WEATHER_HUMIDITY', 'PERSIST_KEY_WEATHER_HUMIDITY'],
  ['WEATHER_WIND_DIRECTION', 'PERSIST_KEY_WEATHER_WIND_DIRECTION'],
  ['WEATHER_WIND_SPEED', 'PERSIST_KEY_WEATHER_WIND_SPEED'],
  ['WEATHER_PCP', 'PERSIST_KEY_WEATHER_PCP'],
  ['WEATHER_PRECIP_NOW', 'PERSIST_KEY_WEATHER_PRECIP_NOW'],
  ['WEATHER_HIGH', 'PERSIST_KEY_WEATHER_HIGH'],
  ['WEATHER_LOW', 'PERSIST_KEY_WEATHER_LOW'],
  ['WEATHER_LOW_TOMORROW', 'PERSIST_KEY_WEATHER_LOW_TOMORROW'],
  ['WEATHER_TEMP_HIGH_TOMORROW', 'PERSIST_KEY_WEATHER_HIGH_TOMORROW'],
  ['WEATHER_HI_HOUR_TODAY', 'PERSIST_KEY_WEATHER_HI_HOUR_TODAY'],
  ['WEATHER_LO_HOUR_TODAY', 'PERSIST_KEY_WEATHER_LO_HOUR_TODAY'],
  ['WEATHER_HI_HOUR_TOMORROW', 'PERSIST_KEY_WEATHER_HI_HOUR_TOMORROW'],
  ['WEATHER_LO_HOUR_TOMORROW', 'PERSIST_KEY_WEATHER_LO_HOUR_TOMORROW'],
  ['SETTINGS_THEME', 'PERSIST_KEY_SETTINGS_THEME'],
  ['SETTINGS_UNITS', 'PERSIST_KEY_SETTINGS_UNITS'],
  ['SETTINGS_DATE_FORMAT', 'PERSIST_KEY_SETTINGS_DATE_FORMAT'],
  ['SETTINGS_SHORT_DATE_FORMAT', 'PERSIST_KEY_SETTINGS_SHORT_DATE'],
  ['SETTINGS_DOW_POSITION', 'PERSIST_KEY_SETTINGS_DOW'],
  ['SETTINGS_DISCONNECT_VIBE', 'PERSIST_KEY_SETTINGS_DISCONNECT_VIBE'],
  ['SLOT_1', 'PERSIST_KEY_SLOT_1'],
  ['SLOT_2', 'PERSIST_KEY_SLOT_2'],
  ['SLOT_3', 'PERSIST_KEY_SLOT_3'],
  ['SLOT_4', 'PERSIST_KEY_SLOT_4'],
  ['SLOT_5', 'PERSIST_KEY_SLOT_5'],
  ['SLOT_6', 'PERSIST_KEY_SLOT_6'],
]);

// "30 * 60" → 1800; digits and '*' only.
function evalProduct(expr) {
  return expr.split('*')
      .map(p => {
        const n = Number(p.trim());
        assert.ok(Number.isSafeInteger(n), `not an int literal: "${p}"`);
        return n;
      })
      .reduce((a, b) => a * b, 1);
}

const messagingC = readRepoFile('src/c/messaging.c');

test('C and JS weather tables carry identical keys and sentinels', () => {
  const c = cWeatherRows();
  const js = new Map(weather.WEATHER_FIELDS.map(f => [f.key, f.sentinel]));

  assert.deepEqual([...c.keys()].sort(), [...js.keys()].sort(), 'key sets differ');
  for (const [key, row] of c) {
    assert.equal(js.get(key), row.sentinel, `sentinel differs for ${key}`);
  }
});

test('every weather/settings/slot row persists under exactly the pinned key', () => {
  const all = new Map([
    ...parseCMessageRows(messagingC, 's_weather_fields'),
    ...parseCMessageRows(messagingC, 's_settings_fields'),
    ...parseCMessageRows(messagingC, 's_slot_keys'),
  ]);
  const actual = new Map([...all].map(([key, row]) => [key, row.persist]));
  assert.deepEqual(actual, EXPECTED_PERSIST);
});

test('persist values are unique and every pinned key is defined', () => {
  const header = readRepoFile('src/c/messaging.h');
  const defs = new Map();
  for (const m of header.matchAll(/^#define (PERSIST_KEY_[A-Z0-9_]+) (\d+)$/gm)) {
    assert.ok(!defs.has(m[1]), `duplicate define of ${m[1]}`);
    defs.set(m[1], Number(m[2]));
  }
  const values = [...defs.values()];
  assert.equal(new Set(values).size, values.length, 'PERSIST_KEY values must never collide');
  for (const name of EXPECTED_PERSIST.values()) {
    assert.ok(defs.has(name), `${name} is pinned but never defined`);
  }
});

test('the weather cache TTL agrees in seconds (C) and milliseconds (JS)', () => {
  const sDef = readRepoFile('src/c/messaging.h')
                   .match(/#define WEATHER_CACHE_MAX_AGE_S \(?(\d+(?:\s*\*\s*\d+)*)\)?/);
  const msDef = readRepoFile('src/pkjs/weather.js')
                    .match(/WEATHER_CACHE_MAX_AGE_MS = (\d+(?:\s*\*\s*\d+)*);/);
  assert.ok(sDef, 'WEATHER_CACHE_MAX_AGE_S not found/parseable in messaging.h');
  assert.ok(msDef, 'WEATHER_CACHE_MAX_AGE_MS not found/parseable in weather.js');
  assert.equal(evalProduct(msDef[1]), evalProduct(sDef[1]) * 1000);
});

test('package.json messageKeys are exactly the keys messaging.c speaks', () => {
  const pkg = JSON.parse(readRepoFile('package.json'));
  const declared = pkg.pebble.messageKeys;
  assert.equal(new Set(declared).size, declared.length, 'duplicate messageKeys entries');
  const referenced = new Set([...messagingC.matchAll(/MESSAGE_KEY_([A-Z0-9_]+)/g)].map(m => m[1]));

  const undeclared = [...referenced].filter(k => !declared.includes(k));
  const unreferenced = declared.filter(k => !referenced.has(k));
  assert.deepEqual(undeclared, [], 'keys used in messaging.c but absent from messageKeys');
  assert.deepEqual(unreferenced, [], 'messageKeys entries messaging.c never references');
});

// The one sanctioned semantic join between the settings page and the C enum:
// which ComplicationDataSource each curated label stands for. Written out so
// a renumbered id on either side fails here naming the drifted pair; kept in
// OPTION_LABELS source order. (Option ids and labels are scraped on the
// config.js side — the semantic join itself can only be pinned by hand.)
const EXPECTED_SOURCE_LABELS = [
  ['DATA_SOURCE_EMPTY', 'Empty'],
  ['DATA_SOURCE_BATTERY', 'Battery'],
  ['DATA_SOURCE_BT_QT', 'Bluetooth + Quiet Time'],
  ['DATA_SOURCE_SHORT_DATE', 'Short Date (no year)'],
  ['DATA_SOURCE_BEATS', '.beat time'],
  ['DATA_SOURCE_STEPS', 'Steps'],
  ['DATA_SOURCE_SLEEP', 'Sleep'],
  ['DATA_SOURCE_HEART_RATE', 'Heart Rate'],
  ['DATA_SOURCE_ACTIVE_MINUTES', 'Active Minutes'],
  ['DATA_SOURCE_WEATHER', 'Weather'],
  ['DATA_SOURCE_TEMP_HIGH_LOW', 'Next High / Low temperatures'],
  ['DATA_SOURCE_WIND', 'Wind'],
  ['DATA_SOURCE_HUM_PCP', 'Humidity + Precipitation (next 12h max)'],
  ['DATA_SOURCE_AQI_UV', 'AQI + UV Index (current)'],
  ['DATA_SOURCE_FULL_DATE', 'Date'],
  ['DATA_SOURCE_WEATHER_FULL', 'Full Weather'],
  ['DATA_SOURCE_STEPS_BAR', 'Steps Progress'],
  ['DATA_SOURCE_BATTERY_BAR', 'Battery Progress'],
  ['DATA_SOURCE_BLUETOOTH', 'Bluetooth Status'],
  ['DATA_SOURCE_QUIET_TIME', 'Quiet Time'],
  ['DATA_SOURCE_WEATHER_TEMP', 'Temperature'],
  ['DATA_SOURCE_WEATHER_PCP', 'Precipitation (next 12h max)'],
  ['DATA_SOURCE_HUMIDITY', 'Humidity'],
  ['DATA_SOURCE_AQI', 'Air Quality (AQI)'],
  ['DATA_SOURCE_UV', 'UV Index (next 12h max)'],
];

test('every slot option id Clay offers exists in ComplicationDataSource', () => {
  const ids = new Set(scrape.dataSourceIds().values());
  for (const id of scrape.claySlotOptionIds()) {
    assert.ok(ids.has(id), `Clay offers source id ${id}, absent from the enum`);
  }
});

test('the Clay label ↔ enum name join holds on both sides', () => {
  const ids = scrape.dataSourceIds();
  const joined = new Map();
  for (const [name, label] of EXPECTED_SOURCE_LABELS) {
    assert.ok(ids.has(name), `${name} no longer exists in the enum`);
    joined.set(ids.get(name), label);
  }
  assert.deepEqual(scrape.clayOptionLabels(), joined);
});

// The settings selects' value↔label pairs are the second hand-written
// semantic join: permuting 'Imperial' ↔ 'Metric' (or DOS ↔ ISO) passes every
// other test while users get swapped behavior, so the pairs are pinned here
// verbatim. The theme SELECT's id↔name mirroring into theme.c's switch stays
// hand-maintained — the (value, label) pin plus the boot-defaults pin below
// is the agreed coverage.
const EXPECTED_SETTINGS_OPTIONS = {
  SETTINGS_THEME: [
    ['1', 'Turbo Vision (light grey)'],
    ['2', 'Norton (EGA blue)'],
    ['3', 'Dark (black)'],
    ['4', 'Navigator (dark grey)'],
  ],
  SETTINGS_UNITS: [['0', 'Imperial'], ['1', 'Metric']],
  SETTINGS_DATE_FORMAT: [
    ['0', 'ISO (1970-12-31)'],
    ['1', 'DOS (31-12-1970)'],
    ['2', 'Text (DEC 31st, 1970)'],
    ['3', 'Short (no year)'],
  ],
  SETTINGS_SHORT_DATE_FORMAT: [['0', 'Month-Day (12-31)'], ['1', 'Day-Month (31-12)']],
  SETTINGS_DOW_POSITION: [
    ['0', 'Before date (THU 1970-12-31)'],
    ['1', 'After date (1970-12-31 THU)'],
    ['2', 'Hidden (1970-12-31)'],
  ],
  SETTINGS_DISCONNECT_VIBE: [['1', 'On'], ['0', 'Off']],
};

test('every settings select offers exactly the pinned value ↔ label pairs', () => {
  const actual = {};
  for (const section of config) {
    for (const item of section.items || []) {
      if (item.type === 'select' && /^SETTINGS_/.test(item.messageKey)) {
        assert.ok(!actual[item.messageKey], `duplicate select for ${item.messageKey}`);
        actual[item.messageKey] = item.options.map(o => [String(o.value), o.label]);
      }
    }
  }
  assert.deepEqual(
      Object.keys(actual).sort(), Object.keys(EXPECTED_SETTINGS_OPTIONS).sort(),
      'the six settings selects drifted');
  for (const [key, expected] of Object.entries(EXPECTED_SETTINGS_OPTIONS)) {
    assert.deepEqual(actual[key], expected, `option pair drift in ${key}`);
  }
});

test('the units select\'s values carry the same semantics as data.h\'s defines', () => {
  // The join that actually decides C behavior: the option value labeled
  // 'Metric' must be data.h's UNITS_METRIC, likewise Imperial.
  const dataH = readRepoFile('src/c/data.h');
  const defines = new Map();
  for (const m of dataH.matchAll(/^#define (UNITS_(?:IMPERIAL|METRIC)) (\d+)$/gm)) {
    defines.set(m[1], m[2]);
  }
  assert.equal(defines.size, 2, 'UNITS_* defines not parsed — pattern drift?');
  const options = new Map(EXPECTED_SETTINGS_OPTIONS.SETTINGS_UNITS.map(([v, l]) => [l, v]));
  assert.equal(defines.get('UNITS_IMPERIAL'), options.get('Imperial'));
  assert.equal(defines.get('UNITS_METRIC'), options.get('Metric'));
});

test('the C boot defaults equal the Clay shipped defaults', () => {
  const boots = scrape.cBootDefaults();  // 12 entries; the scraper asserts its own coverage
  const shipped = new Map();
  for (const section of config) {
    for (const item of section.items || []) {
      if (item.messageKey) shipped.set(item.messageKey, String(item.defaultValue));
    }
  }
  assert.deepEqual(shipped, boots);
});
