'use strict';

// Pins for the settings page skin (src/pkjs/skin.js): every theme offered by
// config.js has a palette block with the same role set, and colours live only
// in those blocks.

const {test} = require('node:test');
const assert = require('node:assert/strict');
const config = require('../../src/pkjs/config.js');
const skin = require('../../src/pkjs/skin.js');

const PALETTE = /body(?:,body)?\[data-theme="(\d+)"\]\{([^}]*)\}/g;

function palettes() {
  const found = {};
  for (const [, theme, body] of skin.matchAll(PALETTE)) {
    assert.ok(!(theme in found), `duplicate palette for theme ${theme}`);
    found[theme] = body.split(';').filter(Boolean).map(d => d.split(':'));
  }
  return found;
}

function themeValues() {
  for (const section of config) {
    for (const item of section.items || []) {
      if (item.messageKey === 'SETTINGS_THEME') return item.options.map(o => String(o.value));
    }
  }
  return [];
}

test('every theme option has a palette block and no palette is orphaned', () => {
  const values = themeValues();
  assert.ok(values.length > 0);
  assert.deepEqual(Object.keys(palettes()).sort(), values.slice().sort());
});

test('every palette defines the same roles, each as a hex colour', () => {
  const all = palettes();
  const roles = (all['2'] || []).map(([name]) => name);
  assert.ok(roles.length > 0);
  for (const [theme, decls] of Object.entries(all)) {
    assert.deepEqual(decls.map(([name]) => name), roles, `theme ${theme}`);
    for (const [name, value] of decls) {
      assert.match(name, /^--[a-z-]+$/);
      assert.match(value, /^#[0-9A-F]{6}$/, `${name} in theme ${theme}`);
    }
  }
});

test('Norton palette is also the fallback when no theme attribute is set', () => {
  assert.match(skin, /body,body\[data-theme="2"\]\{/);
});

test('no raw hex colour remains outside the palette blocks', () => {
  assert.doesNotMatch(skin.replace(PALETTE, ''), /#[0-9a-fA-F]{3,8}\b/);
});

test('every custom property used is defined by the palettes', () => {
  const defined = new Set((palettes()['2'] || []).map(([name]) => name));
  const used = new Set([...skin.matchAll(/var\((--[a-z-]+)/g)].map(m => m[1]));
  used.delete('--slack');
  for (const name of used) assert.ok(defined.has(name), `${name} is used but not defined`);
});
