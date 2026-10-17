'use strict';

// Pins for the generated settings page (src/pkjs/config.js): shapes, the
// curated per-slot value sets, and the shipped defaults.

const {test} = require('node:test');
const assert = require('node:assert/strict');
const config = require('../../src/pkjs/config.js');
const scrape = require('./contract-scrape.js');

function findSelect(messageKey) {
  for (const section of config) {
    for (const item of section.items || []) {
      if (item.messageKey === messageKey) return item;
    }
  }
  return null;
}

test('config has the heading, two sections, and submit in order', () => {
  assert.equal(config.length, 4);
  assert.equal(config[0].type, 'heading');
  assert.equal(config[1].type, 'section');
  assert.equal(config[2].type, 'section');
  assert.equal(config[3].type, 'submit');
  assert.equal(config[1].items.length, 12);  // eight settings + test-sound pair
  assert.equal(config[2].items.length, 7);   // heading + six slot selects
});

test('every messageKey appears exactly once and every default is valid', () => {
  const seen = new Set();
  for (const section of config) {
    for (const item of section.items || []) {
      if (!item.messageKey) continue;
      assert.ok(!seen.has(item.messageKey), `duplicate ${item.messageKey}`);
      seen.add(item.messageKey);
      if (item.type === 'select') {  // non-select keyed items (CHIME_TEST input) have no options
        assert.ok(
            item.options.some(o => o.value === item.defaultValue),
            `${item.messageKey} default ${item.defaultValue} not among its options`);
        for (const o of item.options) assert.ok(o.label && o.value);
      }
    }
  }
  assert.equal(seen.size, 17);
});

test('shipped defaults match the C-side boots', () => {
  // Compared against the scraped C boots (data.c's s_settings_* initializers
  // and slot sources) rather than a third hand-copied map here.
  for (const [key, def] of scrape.cBootDefaults()) {
    assert.equal(findSelect(key)?.defaultValue, def, key);
  }
});

test('the slot curation sets are exactly the pinned ones', () => {
  const values = key => findSelect(key).options.map(o => o.value);
  const top = ['20', '0', '32', '22', '21', '1', '2', '6', '10', '5', '30', '34', '35', '18'];
  const bottom =
      ['20', '0', '9', '31', '21', '1', '2', '6', '10', '3', '28', '34', '26', '16', '17', '33'];
  assert.deepEqual(values('SLOT_1'), top);
  assert.deepEqual(values('SLOT_2'), top);
  assert.deepEqual(values('SLOT_6'), ['23', '27', '24', '25']);
  for (const bottomKey of ['SLOT_3', 'SLOT_4', 'SLOT_5']) {
    assert.deepEqual(values(bottomKey), bottom);
  }
});

test('wind carries its units only in the narrow bottom slots', () => {
  const windLabel = key => findSelect(key).options.find(o => o.value === '34')?.label;
  assert.equal(windLabel('SLOT_1'), 'Wind');
  assert.equal(windLabel('SLOT_6'), undefined);  // centre has no wind at all
  assert.equal(windLabel('SLOT_3'), 'Wind (m/s or mph)');
});

test('the theme select offers values 1-4 and 6, Norton first, Norton by default', () => {
  const theme = findSelect('SETTINGS_THEME');
  assert.deepEqual(theme.options.map(o => String(o.value)).sort(), ['1', '2', '3', '4', '6']);
  assert.equal(String(theme.options[0].value), '2');
  assert.equal(theme.options[0].label, 'Norton (EGA blue)');
  assert.equal(String(theme.defaultValue), '2');
});

test('the test-sound row is an action pair: hidden CHIME_TEST input + keyless button', () => {
  // The flag lives on a keyed input (input manipulator serializes .val) that
  // the page hides via customFn; the button must NOT carry a messageKey — the
  // button manipulator's value is its innerHTML, so a key would send the
  // caption string to the watch.
  const input = findSelect('CHIME_TEST');
  assert.ok(input, 'CHIME_TEST input missing');
  assert.equal(input.type, 'input');
  assert.equal(String(input.defaultValue), '0');
  const button = config[1].items.find(item => item.id === 'test-sound');
  assert.ok(button, 'test-sound button missing');
  assert.equal(button.type, 'button');
  assert.ok(!('messageKey' in button), 'button carries a messageKey — caption would hit the wire');
});

test('every label in the master map is offered somewhere', () => {
  const offered = new Set();
  for (const key of ['SLOT_1', 'SLOT_2', 'SLOT_3', 'SLOT_4', 'SLOT_5', 'SLOT_6']) {
    for (const o of findSelect(key).options) offered.add(o.value);
  }
  assert.equal(offered.size, 26);
});
