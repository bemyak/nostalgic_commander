'use strict';

// The wire contract across the language boundary: the weather field table
// exists once in C (src/c/messaging.c, s_weather_fields) and once in JS
// (src/pkjs/weather.js, WEATHER_FIELDS), the cache TTL exists once in
// seconds (messaging.h) and once in milliseconds (weather.js), and package.json's messageKeys list
// the keys both halves speak. These tests parse the C side as text and demand equality, so a
// one-sided rename or sentinel drift fails here instead of surfacing as "--" on the wrist.

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');

const weather = require('../../src/pkjs/weather.js');

function readRepoFile(rel) {
  return fs.readFileSync(path.join(__dirname, '..', '..', rel), 'utf8');
}

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
