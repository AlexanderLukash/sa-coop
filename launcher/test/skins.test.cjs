const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const skins = require('../src/skins.json');
const { defaults, validate } = require('../src/core/config.cjs');
test('complete stock skin catalog accepts story and civilian choices and excludes invalid/extra models', () => {
  assert.equal(skins.length, 299);
  assert.equal(new Set(skins.map(s => s.id)).size, 299);
  for (const id of [0, 1, 65, 105, 190, 265, 288, 299]) assert.equal(validate({ ...defaults, skin: id }).skin, id);
  for (const id of [-1, 74, 300, 311, 400]) assert.throws(() => validate({ ...defaults, skin: id }));
});
test('every selectable skin has a valid offline PNG thumbnail', () => {
  for (const skin of skins) {
    const bytes = fs.readFileSync(path.join(__dirname, '../assets/skins', `${skin.id}.png`));
    assert.equal(bytes.subarray(0, 8).toString('hex'), '89504e470d0a1a0a', `skin ${skin.id}`);
  }
});
