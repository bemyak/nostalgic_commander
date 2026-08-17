'use strict';

// C-side (and config.js source-side) text scrapes shared by
// wire-contract.test.js and config.test.js. Kept out of a *.test.js file on
// purpose: `node --test` runs each of those, and requiring one test file from
// another would register its suite a second time. The idiom follows
// wire-contract.test.js: parse the sources as text so the tests pin the
// values the watch actually boots with, never a hand-copied map.

const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');

function readRepoFile(rel) {
  return fs.readFileSync(path.join(__dirname, '..', '..', rel), 'utf8');
}

// ComplicationDataSource, name → id. The enum's values are stable, persisted
// identifiers (AGENTS.md), so every id the settings page offers must resolve
// to a name here.
function dataSourceIds() {
  const header = readRepoFile('src/c/data.h');
  const start = header.indexOf('typedef enum {');
  const body = header.slice(start, header.indexOf('} ComplicationDataSource', start));
  const ids = new Map();
  for (const m of body.matchAll(/(DATA_SOURCE_[A-Z0-9_]+)\s*=\s*(\d+)/g)) {
    assert.ok(!ids.has(m[1]), `duplicate enum member ${m[1]}`);
    ids.set(m[1], Number(m[2]));
  }
  assert.ok(ids.size > 0, 'ComplicationDataSource enum not parsed — pattern drift?');
  return ids;
}

// config.js's OPTION_LABELS (id → label), the curation master list. Scraped
// as text because the object literal is not exported; labels hold no
// apostrophes today, which the parse relies on and asserts by coverage.
function clayOptionLabels() {
  const src = readRepoFile('src/pkjs/config.js');
  const start = src.indexOf('OPTION_LABELS = {');
  assert.ok(start >= 0, 'OPTION_LABELS not found in config.js');
  const body = src.slice(start, src.indexOf('};', start));
  const labels = new Map();
  for (const m of body.matchAll(/(\d+): '([^']+)'/g)) {
    labels.set(Number(m[1]), m[2]);
  }
  assert.ok(labels.size > 0, 'OPTION_LABELS rows not parsed — pattern drift?');
  return labels;
}

// Every id a slot select can offer: the TOP/CENTER/BOTTOM_VALUES arrays.
// (The settings selects' values are per-setting, not source ids, and are
// deliberately not scraped here.)
function claySlotOptionIds() {
  const src = readRepoFile('src/pkjs/config.js');
  const ids = [];
  for (const m of src.matchAll(/(?:TOP|CENTER|BOTTOM)_VALUES =\s*\[([^\]]*)\]/g)) {
    for (const v of m[1].matchAll(/'(\d+)'/g)) ids.push(Number(v[1]));
  }
  assert.ok(ids.length > 0, 'slot VALUES arrays not parsed — pattern drift?');
  return ids;
}

// The s_settings_* initializers in data.c, keyed like Clay messageKeys
// (s_settings_theme → SETTINGS_THEME). Nine keys: the phone-side weather
// window is persisted watch-side too, though only the phone reads it.
function cBootSettings() {
  const src = readRepoFile('src/c/data.c');
  const boots = new Map();
  for (const m of src.matchAll(/int (s_settings_[a-z_]+)\s*=\s*(\d+)\s*;/g)) {
    boots.set(m[1].slice('s_'.length).toUpperCase(), m[2]);
  }
  assert.equal(boots.size, 9, 's_settings_* initializers not parsed — pattern drift?');
  return boots;
}

// The boot slot layout. messaging.c's s_slot_keys pairs SLOT_{i+1} with
// s_complication_slots[i] (the table's index IS the slot), so SLOT_n's boot
// value is the .source initializer of the slot whose SLOT_IDX_* define is
// n-1, resolved to its enum id.
function cBootSlots() {
  const header = readRepoFile('src/c/data.h');
  const idxByName = new Map();
  for (const m of header.matchAll(/^#define (SLOT_IDX_[A-Z_]+) (\d+)$/gm)) {
    idxByName.set(m[1], Number(m[2]));
  }
  const ids = dataSourceIds();
  const rowRe =
      /\[(SLOT_IDX_[A-Z_]+)\]\s*=\s*\{\s*\.box_rect\s*=\s*[A-Z_]+,\s*\.source\s*=\s*(DATA_SOURCE_[A-Z_]+)\s*\}/g;
  const boots = new Map();
  for (const m of readRepoFile('src/c/data.c').matchAll(rowRe)) {
    boots.set(`SLOT_${idxByName.get(m[1]) + 1}`, String(ids.get(m[2])));
  }
  assert.equal(boots.size, 6, 'slot boot initializers not parsed — pattern drift?');
  return boots;
}

// The full messageKey → boot-default map the Clay shipped defaults must equal.
function cBootDefaults() { return new Map([...cBootSettings(), ...cBootSlots()]); }

module.exports = {
  readRepoFile,
  dataSourceIds,
  clayOptionLabels,
  claySlotOptionIds,
  cBootDefaults,
};
