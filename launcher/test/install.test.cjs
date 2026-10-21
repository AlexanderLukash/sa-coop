const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { installArchive, safeEntry, latestRelease } = require('../src/core/install.cjs');
const { checkInstallAccess } = require('../src/core/install.cjs');

test('protected game folders produce actionable errors before downloading or modifying files', async t => {
  const denied = Object.assign(new Error('operation not permitted'), { code: 'EPERM' });
  t.mock.method(fs, 'realpath', async value => value);
  t.mock.method(fs, 'mkdtemp', async () => { throw denied; });
  await assert.rejects(checkInstallAccess('C:\\Program Files (x86)\\GTA'), error => {
    assert.equal(error.code, 'EPERM');
    assert.match(error.message, /адміністратора/);
    assert.match(error.message, /Games/);
    return true;
  });
});

// Build tiny stored ZIP fixtures, including adversarial paths, without an external zip utility.
function zip(entries) {
  let offset = 0;
  const chunks = [], directory = [];
  for (const [name, contents] of Object.entries(entries)) {
    const filename = Buffer.from(name), data = Buffer.from(contents);
    let crc = 0xffffffff;
    for (const byte of data) { crc ^= byte; for (let i = 0; i < 8; i++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0); }
    crc = (crc ^ 0xffffffff) >>> 0;
    const local = Buffer.alloc(30); local.writeUInt32LE(0x04034b50); local.writeUInt16LE(20, 4);
    local.writeUInt32LE(crc, 14); local.writeUInt32LE(data.length, 18); local.writeUInt32LE(data.length, 22); local.writeUInt16LE(filename.length, 26);
    chunks.push(local, filename, data);
    const central = Buffer.alloc(46); central.writeUInt32LE(0x02014b50); central.writeUInt16LE(20, 4); central.writeUInt16LE(20, 6);
    central.writeUInt32LE(crc, 16); central.writeUInt32LE(data.length, 20); central.writeUInt32LE(data.length, 24);
    central.writeUInt16LE(filename.length, 28); central.writeUInt32LE(offset, 42);
    directory.push(central, filename); offset += local.length + filename.length + data.length;
  }
  const end = Buffer.alloc(22), central = Buffer.concat(directory);
  end.writeUInt32LE(0x06054b50); end.writeUInt16LE(Object.keys(entries).length, 8); end.writeUInt16LE(Object.keys(entries).length, 10);
  end.writeUInt32LE(central.length, 12); end.writeUInt32LE(offset, 16);
  return Buffer.concat([...chunks, central, end]);
}
const files = { 'coop.asi': 'new-mod', 'coop/coop-server.exe': 'new-server', 'coop/loader/vorbisFile.dll': 'new-loader', 'coop/server.ini': 'new-settings' };
async function fixture(work) {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'sacoop-install-test-'));
  try {
    const game = path.join(dir, 'game'); await fs.mkdir(game);
    await fs.writeFile(path.join(game, 'gta_sa.exe'), require('../tools/game-fixture.cjs').compatibleFixture());
    const archive = path.join(dir, 'mod.zip');
    await fs.writeFile(archive, zip(files));
    await work({ dir, game, archive });
  } finally { await fs.rm(dir, { recursive: true, force: true }); }
}
test('safe archive paths reject traversal, absolute paths, alternate streams and aliases', () => {
  for (const name of ['../outside', '/absolute', 'C:/evil', 'coop/a:stream', 'coop/../outside', 'coop/file.']) assert.throws(() => safeEntry(name));
  assert.equal(safeEntry('SA-COOP/coop.asi'), 'coop.asi');
});
test('install preserves configuration, installs loader, and cleans staging', async () => fixture(async ({ game, archive }) => {
  await fs.mkdir(path.join(game, 'coop'));
  await fs.writeFile(path.join(game, 'coop/server.ini'), 'my-settings');
  await fs.writeFile(path.join(game, 'vorbisFile.dll'), 'original-loader');
  const result = await installArchive(archive, game, 'v-test');
  assert.equal(result.installed, true); assert.equal(result.installedVersion, 'v-test');
  assert.equal(await fs.readFile(path.join(game, 'vorbisHooked.dll'), 'utf8'), 'original-loader');
  assert.equal(await fs.readFile(path.join(game, 'vorbisFile.dll'), 'utf8'), 'new-loader');
  assert.equal(await fs.readFile(path.join(game, 'coop/server.ini'), 'utf8'), 'my-settings');
  assert.equal((await fs.readdir(game)).some(f => f.startsWith('.sacoop-install-')), false);
}));
test('failed install restores old mod and removes new files', async () => fixture(async ({ game, archive }) => {
  await fs.writeFile(path.join(game, 'coop.asi'), 'old-mod');
  // No original vorbisFile.dll causes a failure after mod files have been copied.
  await assert.rejects(installArchive(archive, game, 'bad'), /vorbisFile/);
  assert.equal(await fs.readFile(path.join(game, 'coop.asi'), 'utf8'), 'old-mod');
  await assert.rejects(fs.access(path.join(game, 'coop/coop-server.exe')));
}));

test('unrelated dinput8 DLL does not suppress loader installation; reinstall keeps original', async () => fixture(async ({ game, archive }) => {
  await fs.writeFile(path.join(game, 'dinput8.dll'), 'not-an-asi-loader');
  await fs.writeFile(path.join(game, 'vorbisFile.dll'), 'original-vorbis');
  await installArchive(archive, game, 'first');
  await installArchive(archive, game, 'second');
  assert.equal(await fs.readFile(path.join(game, 'vorbisFile.dll'), 'utf8'), 'new-loader');
  assert.equal(await fs.readFile(path.join(game, 'vorbisHooked.dll'), 'utf8'), 'original-vorbis');
  assert.equal(await fs.readFile(path.join(game, 'dinput8.dll'), 'utf8'), 'not-an-asi-loader');
}));
test('archive traversal cannot escape the game directory', async () => fixture(async ({ dir, game, archive }) => {
  await fs.writeFile(archive, zip({ ...files, '../outside.txt': 'escaped' }));
  await assert.rejects(installArchive(archive, game, 'bad'));
  await assert.rejects(fs.access(path.join(dir, 'outside.txt')));
}));
test('missing release gives actionable error instead of installing an unrelated version', async () => {
  await assert.rejects(latestRelease('owner/repo', async () => ({ ok: false, status: 404 })), /локальний/);
  const release = await latestRelease('owner/repo', async () => ({ ok: true, json: async () => ({ tag_name: 'v1', assets: [{ name: 'SA-COOP.zip', browser_download_url: 'https://github.com/owner/repo/releases/a.zip', size: 10 }] }) }));
  assert.equal(release.version, 'v1');
});
