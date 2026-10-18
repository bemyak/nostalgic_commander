'use strict';

// Pins for the generated settings page (src/pkjs/config.js): shapes, the
// curated per-slot value sets, and the shipped defaults.

const {test} = require('node:test');
const assert = require('node:assert/strict');
const config = require('../../src/pkjs/config.js');
const scrape = require('./contract-scrape.js');

function findItem(messageKey) {
  for (const section of config) {
    for (const item of section.items || []) {
      if (item.messageKey === messageKey) return item;
    }
  }
  return null;
}

test('config has three sections, inline save, submit, defaults, and quit in order', () => {
  assert.equal(config.length, 7);
  for (const i of [0, 1, 2]) assert.equal(config[i].type, 'section');
  assert.equal(config[4].type, 'submit');
  for (const i of [3, 5, 6]) {
    assert.equal(config[i].type, 'button');
    assert.equal(config[i].messageKey, undefined);
  }
  assert.equal(config[0].items.length, 7);  // heading + six settings
  assert.equal(config[1].items.length, 8);  // heading + five effect settings + test-sound pair
  assert.equal(config[2].items.length, 7);  // heading + six slot selects
});

test('every messageKey appears exactly once and every default is valid', () => {
  const seen = new Set();
  for (const section of config) {
    for (const item of section.items || []) {
      if (!item.messageKey) continue;
      assert.ok(!seen.has(item.messageKey), `duplicate ${item.messageKey}`);
      seen.add(item.messageKey);
      if (item.type === 'select' ||
          item.type === 'radiogroup') {  // toggles and the CHIME_TEST input have no options
        assert.ok(
            item.options.some(o => o.value === item.defaultValue),
            `${item.messageKey} default ${item.defaultValue} not among its options`);
        for (const o of item.options) assert.ok(o.label && o.value);
      }
    }
  }
  assert.equal(seen.size, 18);
});

test('shipped defaults match the C-side boots', () => {
  // Compared against the scraped C boots (data.c's s_settings_* initializers
  // and slot sources) rather than a third hand-copied map here.
  for (const [key, def] of scrape.cBootDefaults()) {
    const item = findItem(key);
    const shipped =
        String(item?.type === 'toggle' ? Number(item.defaultValue) : item?.defaultValue);
    assert.equal(shipped, def, key);
  }
});

test('the slot curation sets are exactly the pinned ones', () => {
  const values = key => findItem(key).options.map(o => o.value);
  const top = ['20', '0', '32', '22', '21', '1', '2', '6', '10', '5', '30', '34', '35', '18', '38'];
  const bottom = [
    '20', '0', '9', '31', '22', '21', '1', '2', '6', '10', '3', '28', '34', '26', '16', '17', '33',
    '36', '37'
  ];
  assert.deepEqual(values('SLOT_1'), top);
  assert.deepEqual(values('SLOT_2'), top);
  assert.deepEqual(values('SLOT_6'), ['23', '27', '24', '25']);
  for (const bottomKey of ['SLOT_3', 'SLOT_4', 'SLOT_5']) {
    assert.deepEqual(values(bottomKey), bottom);
  }
});

test('wind carries its units only in the narrow bottom slots', () => {
  const windLabel = key => findItem(key).options.find(o => o.value === '34')?.label;
  assert.equal(windLabel('SLOT_1'), 'Wind');
  assert.equal(windLabel('SLOT_6'), undefined);  // centre has no wind at all
  assert.equal(windLabel('SLOT_3'), 'Wind (m/s or mph)');
});

test('the theme radiogroup offers values 1-4 and 6, Norton first, Norton by default', () => {
  const theme = findItem('SETTINGS_THEME');
  assert.deepEqual(theme.options.map(o => String(o.value)).sort(), ['1', '2', '3', '4', '6']);
  assert.equal(String(theme.options[0].value), '2');
  assert.equal(theme.options[0].label, 'Norton (blue)');
  assert.equal(String(theme.defaultValue), '2');
});

test('the test-sound row is an action pair: hidden CHIME_TEST input + keyless button', () => {
  // The flag lives on a keyed input (input manipulator serializes .val) that
  // the page hides via customFn; the button must NOT carry a messageKey — the
  // button manipulator's value is its innerHTML, so a key would send the
  // caption string to the watch.
  const input = findItem('CHIME_TEST');
  assert.ok(input, 'CHIME_TEST input missing');
  assert.equal(input.type, 'input');
  assert.equal(String(input.defaultValue), '0');
  const button = config[1].items.find(item => item.id === 'test-sound');
  assert.ok(button, 'test-sound button missing');
  assert.equal(button.type, 'button');
  assert.ok(!('messageKey' in button), 'button carries a messageKey — caption would hit the wire');
});

test('the effects panel is headed Effects and holds the CRT effects and both volumes', () => {
  const [heading, ...rest] = config[1].items;
  assert.equal(heading.type, 'heading');
  assert.equal(heading.defaultValue, 'Effects');
  assert.deepEqual(rest.map(item => item.messageKey || item.id), [
    'SETTINGS_CRT_ABERRATION', 'SETTINGS_CRT_CURVATURE', 'SETTINGS_CRT_DEGAUSS',
    'SETTINGS_DEGAUSS_VOLUME', 'SETTINGS_CHIME_VOLUME', 'CHIME_TEST', 'test-sound'
  ]);
  const toggles = [
    ['SETTINGS_CRT_ABERRATION', 'Chromatic aberration'],
    ['SETTINGS_CRT_CURVATURE', 'Screen curvature'],
    ['SETTINGS_CRT_DEGAUSS', 'Degauss on wake'],
  ];
  for (const [key, label] of toggles) {
    assert.equal(findItem(key).type, 'toggle', key);
    assert.equal(findItem(key).label, label, key);
    assert.equal(findItem(key).defaultValue, false, key);
  }
  assert.equal(findItem('SETTINGS_DEGAUSS_VOLUME').label, 'Degauss volume');
  assert.equal(findItem('SETTINGS_CHIME_VOLUME').label, 'Hourly chime');
  for (const key of ['SETTINGS_DEGAUSS_VOLUME', 'SETTINGS_CHIME_VOLUME']) {
    const item = findItem(key);
    assert.equal(item.type, 'slider', key);
    assert.deepEqual([item.min, item.max, item.step], [0, 100, 1], key);
    assert.equal(item.options, undefined, key);
  }
  assert.equal(findItem('SETTINGS_DEGAUSS_VOLUME').defaultValue, 10);
  assert.equal(findItem('SETTINGS_CHIME_VOLUME').defaultValue, 0);
});

test('General no longer carries the CRT, sound or chime settings', () => {
  const general = config[0].items.map(item => item.messageKey).filter(Boolean);
  assert.deepEqual(general, [
    'SETTINGS_THEME', 'SETTINGS_UNITS', 'SETTINGS_WEATHER_WINDOW', 'SETTINGS_DATE_FORMAT',
    'SETTINGS_SHORT_DATE_FORMAT', 'SETTINGS_DOW_POSITION'
  ]);
});

test('every label in the master map is offered somewhere', () => {
  const offered = new Set();
  for (const key of ['SLOT_1', 'SLOT_2', 'SLOT_3', 'SLOT_4', 'SLOT_5', 'SLOT_6']) {
    for (const o of findItem(key).options) offered.add(o.value);
  }
  assert.equal(offered.size, 29);
});
