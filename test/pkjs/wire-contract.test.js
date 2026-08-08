'use strict';

// The wire contract across the language boundary: the weather field table
// exists once in C (src/c/messaging.c, s_weather_fields) and once in JS
// (src/pkjs/weather.js, WEATHER_FIELDS), the cache TTL exists once in
// seconds and once in milliseconds, and package.json's messageKeys list the
// keys both halves speak. These tests parse the C side as text and demand
// equality, so a one-sided rename or sentinel drift fails here instead of
// surfacing as "--" on the wrist.

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');

const weather = require('../../src/pkjs/weather.js');

function readRepoFile(rel) {
  return fs.readFileSync(path.join(__dirname, '..', '..', rel), 'utf8');
}

// The rows of s_weather_fields: {&MESSAGE_KEY_<NAME>, PERSIST_KEY_*, &target,
// <sentinel>[, possibly wrapped across lines]}. Scoped to the table region so
// the identically-shaped settings rows don't pollute the result.
function parseCWeatherRows(messagingC) {
  const start = messagingC.indexOf('s_weather_fields[] = {');
  assert.notEqual(start, -1, 's_weather_fields table not found in messaging.c');
  const table = messagingC.slice(start, messagingC.indexOf('};', start));
  const rowRe =
      /\{&MESSAGE_KEY_([A-Z0-9_]+),\s*PERSIST_KEY_[A-Z0-9_]+,\s*&[A-Za-z0-9_.]+,\s*(-?\d+)\s*\}/g;
  const rows = new Map();
  let m;
  while ((m = rowRe.exec(table)) !== null) {
    assert.ok(!rows.has(m[1]), `duplicate C row for ${m[1]}`);
    rows.set(m[1], Number(m[2]));
  }
  assert.ok(rows.size > 0, 'weather row regex matched nothing — pattern drift?');
  return rows;
}

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
  const c = parseCWeatherRows(messagingC);
  const js = new Map(weather.WEATHER_FIELDS.map(f => [f.key, f.sentinel]));

  assert.deepEqual([...c.keys()].sort(), [...js.keys()].sort(), 'key sets differ');
  for (const [key, sentinel] of c) {
    assert.equal(js.get(key), sentinel, `sentinel differs for ${key}`);
  }
});

test('the weather cache TTL agrees in seconds (C) and milliseconds (JS)', () => {
  const sDef = readRepoFile('src/c/messaging.h')
                   .match(/#define WEATHER_CACHE_MAX_AGE_S \(?(\d+(?:\s*\*\s*\d+)*)\)?/);
  const msDef =
      readRepoFile('src/pkjs/index.js').match(/WEATHER_CACHE_MAX_AGE_MS = (\d+(?:\s*\*\s*\d+)*);/);
  assert.ok(sDef, 'WEATHER_CACHE_MAX_AGE_S not found/parseable in messaging.h');
  assert.ok(msDef, 'WEATHER_CACHE_MAX_AGE_MS not found/parseable in index.js');
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
