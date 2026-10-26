const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { defaults, validate, updateIni, launchSpec, writeGameConfig } = require('../src/core/config.cjs');

test('validate rejects config injection and out-of-range server options', () => {
  assert.equal(validate({ ...defaults }).allowCheats, true);
  assert.equal(validate({ ...defaults, allowCheats: false }).allowCheats, false);
  assert.throws(() => validate({ ...defaults, allowCheats: 'false' }));
  for (const change of [{ nickname: 'ab\n[game]' }, { serverName: 'x\npassword=y' }, { maxPlayers: 9 }, { port: 0 }, { password: 'x\r\ny' }, { releaseRepo: '../bad' }])
    assert.throws(() => validate({ ...defaults, ...change }));
  assert.equal(validate({ ...defaults, nickname: 'AlexanderLukash' }).maxPlayers, 8);
});
test('INI updates preserve unrelated settings and remove duplicate controlled keys', () => {
  const updated = updateIni('; keep\n[server]\nmax_players=2\nmax_players=4\nturn=custom\n[other]\nfoo=bar\n', {
    server: { max_players: 8, internet: 0 }, player: { name: 'Crew' },
  });
  assert.match(updated, /turn=custom/); assert.match(updated, /foo=bar/);
  assert.equal(updated.match(/max_players/g).length, 1);
  assert.match(updated, /max_players = 8/); assert.match(updated, /\[player\]\r\nname = Crew/);
  assert.equal(updateIni(updated, { server: { max_players: 8, internet: 0 }, player: { name: 'Crew' } }), updated);
});
test('Windows and Linux launch without shell interpolation and use the selected Wine prefix', () => {
  const s = { ...defaults, gameDir: '/games/GTA SA', wineCommand: '/usr/bin/wine', winePrefix: '/home/crew/Wine SA' };
  const linux = launchSpec(s, 'linux');
  assert.equal(linux.command, '/usr/bin/wine');
  assert.deepEqual(linux.args, [path.join(s.gameDir, 'gta_sa.exe')]);
  assert.equal(linux.options.env.SACOOP_CONFIG, 'launcher.ini');
  assert.equal(linux.options.env.WINEPREFIX, path.resolve(s.winePrefix));
  assert.match(linux.options.env.WINEDLLOVERRIDES, /^vorbisFile=n,b/);
  assert.match(linux.options.env.WINEDLLOVERRIDES, /dinput8=n,b/);
  assert.equal(linux.options.shell, undefined);
  assert.equal(linux.options.detached, true);
  assert.equal(launchSpec(s, 'win32').command, path.join(s.gameDir, 'gta_sa.exe'));
});
test('game integration writes LAN server and client configs without changing coop.ini', async () => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'sacoop-config-'));
  try {
    await fs.mkdir(path.join(dir, 'coop'));
    await fs.writeFile(path.join(dir, 'coop/coop.ini'), 'original-config');
    await fs.writeFile(path.join(dir, 'coop/server.ini'), '[server]\nturn = retained\nmax_players=2\n');
    await writeGameConfig({ ...defaults, gameDir: dir, nickname: 'Host', password: 'secret' }, { mode: 'host', password: 'secret' });
    const server = await fs.readFile(path.join(dir, 'coop/server.ini'), 'utf8');
    const client = await fs.readFile(path.join(dir, 'coop/launcher.ini'), 'utf8');
    assert.match(server, /max_players = 8/); assert.match(server, /internet = 0/); assert.match(server, /turn = retained/);
    assert.match(client, /mode = host/); assert.match(client, /auto_connect = 1/); assert.match(client, /load_slot = -1/);
    assert.match(client, /server_data = launcher-server-data/);
    assert.equal(await fs.readFile(path.join(dir, 'coop/coop.ini'), 'utf8'), 'original-config');
    await writeGameConfig({ ...defaults, gameDir: dir, skin: 271 }, { mode: 'join', address: '192.168.1.8', password: 'secret', port: 7799 });
    const joined = await fs.readFile(path.join(dir, 'coop/launcher.ini'), 'utf8');
    assert.match(joined, /host = 192.168.1.8/); assert.match(joined, /port = 7799/); assert.match(joined, /mode = join/);
    assert.match(joined, /skin = 271/);
    for (const loadSlot of [0, 1, 8, -1]) {
      await writeGameConfig({ ...defaults, gameDir: dir, loadSlot }, { mode: 'host' });
      assert.match(await fs.readFile(path.join(dir, 'coop/launcher.ini'), 'utf8'), new RegExp(`load_slot = ${loadSlot}\\r?\\n`));
    }
    for (const allowCheats of [false, true]) {
      await writeGameConfig({ ...defaults, gameDir: dir, allowCheats }, { mode: 'host' });
      for (const file of ['coop/launcher.ini', 'coop/server.ini'])
        assert.match(await fs.readFile(path.join(dir, file), 'utf8'), new RegExp(`allow_cheats = ${Number(allowCheats)}\\r?\\n`));
    }
  } finally { await fs.rm(dir, { recursive: true, force: true }); }
});
