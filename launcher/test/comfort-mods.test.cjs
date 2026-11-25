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

test('existing Ukrainian audio files skip the optional audio download', async () => {
  const fs = require('node:fs/promises');
  const os = require('node:os');
  const path = require('node:path');
  const { hasUkrainianAudio } = require('../src/core/comfort-mods.cjs');
  const game = await fs.mkdtemp(path.join(os.tmpdir(), 'sacoop-audio-detect-'));
  try {
    assert.equal(await hasUkrainianAudio(game), false);
    const vanillaAudio = path.join(game, 'audio/streams');
    await fs.mkdir(vanillaAudio, { recursive: true });
    await fs.writeFile(path.join(vanillaAudio, 'CUTSCENE'), 'vanilla voice bank');
    assert.equal(await hasUkrainianAudio(game), false);
    await fs.writeFile(path.join(game, '.sacoop-ukrainian-audio.json'), '{}');
    assert.equal(await hasUkrainianAudio(game), true);
  } finally { await fs.rm(game, { recursive: true, force: true }); }
});
