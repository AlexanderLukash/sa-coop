const { test } = require('node:test');
const assert = require('node:assert/strict');
const { archiveEntries } = require('../src/core/comfort-mods.cjs');

function listing(...entries) {
  return entries.map(({ path, folder = '-', ...fields }) =>
    [`Path = ${path}`, `Folder = ${folder}`, ...Object.entries(fields).map(([key, value]) => `${key} = ${value}`)].join('\n')
  ).join('\n\n');
}

test('Ukrainian audio ZIP accepts normal paths and rejects unsafe archive entries', () => {
  assert.deepEqual(archiveEntries(listing(
    { path: 'audio', folder: '+' },
    { path: 'audio/Streams/CUTSCENE', Size: 128 },
  )), ['audio/Streams/CUTSCENE']);
  for (const path of ['../escape.txt', '/absolute.txt', 'C:\\outside.txt', 'audio/../escape'])
    assert.throws(() => archiveEntries(listing({ path, Size: 1 })), /небезпечний шлях/);
  assert.throws(() => archiveEntries(listing(
    { path: 'same.bin', Size: 1 }, { path: 'SAME.bin', Size: 1 },
  )), /однакові шляхи/);
  assert.throws(() => archiveEntries(listing({ path: 'link', 'Symbolic Link': 'target' })), /Посилання в архіві/);
});
