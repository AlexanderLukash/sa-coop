const { test } = require('node:test');
const assert = require('node:assert/strict');
const { validateRarEntries } = require('../src/core/comfort-mods.cjs');

test('Ukrainian audio RAR accepts normal paths and rejects unsafe or oversized entries', () => {
  assert.deepEqual(validateRarEntries([
    { name: 'audio/', flags: { directory: true, encrypted: false } },
    { name: 'audio/Streams/CUTSCENE', flags: { directory: false, encrypted: false }, unpSize: 128 },
  ]), ['audio/Streams/CUTSCENE']);
  for (const name of ['../escape.txt', '/absolute.txt', 'C:\\outside.txt', 'audio/../escape']) {
    assert.throws(() => validateRarEntries([
      { name, flags: { directory: false, encrypted: false }, unpSize: 1 },
    ]), /небезпечний шлях/);
  }
  assert.throws(() => validateRarEntries([
    { name: 'same.bin', flags: { directory: false, encrypted: false }, unpSize: 1 },
    { name: 'same.bin', flags: { directory: false, encrypted: false }, unpSize: 1 },
  ]), /однакові шляхи/);
  assert.throws(() => validateRarEntries([
    { name: 'encrypted.bin', flags: { directory: false, encrypted: true }, unpSize: 1 },
  ]), /зашифрований/);
  assert.throws(() => validateRarEntries([
    { name: 'huge.bin', flags: { directory: false, encrypted: false }, unpSize: 3 * 1024 * 1024 * 1024 },
  ]), /2 ГБ/);
});
