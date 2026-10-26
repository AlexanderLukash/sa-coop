// Requires tools/build.py dist and tools/build_launcher_probe.py.
// Uses native mod config/command code and the real server, without GTA assets.
const { _electron: electron } = require('playwright');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const { promisify } = require('node:util');
const execFile = promisify(require('node:child_process').execFile);
const assert = require('node:assert/strict');
const net = require('node:net');
const { defaults, writeGameConfig, launchSpec } = require('../src/core/config.cjs');
const { installArchive } = require('../src/core/install.cjs');
const ROOT = path.resolve(__dirname, '../..');
const PROBE = path.join(ROOT, 'build/release/bin/launcher-native-probe.exe');
async function freePort() {
  const server = net.createServer(); await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const port = server.address().port; await new Promise(resolve => server.close(resolve)); return port;
}
async function until(check, description, timeout = 15000) {
  const end = Date.now() + timeout;
  while (!await check()) {
    if (Date.now() > end) throw new Error('Timed out: ' + description);
    await new Promise(resolve => setTimeout(resolve, 50));
  }
}
async function result(gameDir) {
  try { return JSON.parse(await fs.readFile(path.join(gameDir, 'coop/probe-result.json'), 'utf8')); } catch { return null; }
}
async function gameFixture(directory) {
  await fs.mkdir(directory, { recursive: true });
  await fs.writeFile(path.join(directory, 'gta_sa.exe'), require('./game-fixture.cjs').compatibleFixture(await fs.readFile(PROBE)));
  await fs.writeFile(path.join(directory, 'vorbisFile.dll'), 'test fixture original loader');
  // Exercise the actual newly built distribution archive, not a hand-written ZIP.
  await installArchive(path.join(ROOT, 'dist/SA-COOP.zip'), directory, 'native-integration');
}
async function main() {
  const base = await fs.mkdtemp(path.join(os.tmpdir(), 'sacoop-native-'));
  const apps = [];
  const hostGame = path.join(base, 'Гра хоста'), guestGame = path.join(base, 'Гра гостя');
  try {
    await gameFixture(hostGame); await gameFixture(guestGame);
    const port = await freePort(), lobbyPort = await freePort();
    const specialPassword = '"пароль "місто"\\"';
    const settings = { ...defaults, gameDir: hostGame, nickname: 'Їжак_Єнот', skin: 271,
      serverName: 'Ніч "Grove" \\', password: specialPassword, port, lobbyPort,
      friendlyFire: true, windowed: false, width: 1600, height: 900,
      voiceEnabled: false, voiceVolume: 23, micGain: 175 };
    await writeGameConfig(settings, { mode: 'host', password: specialPassword, session: 'native-check' });
    const inspected = JSON.parse((await execFile(PROBE, ['--inspect', path.join(hostGame, 'coop')], {
      env: launchSpec(settings).options.env, windowsHide: true,
    })).stdout);
    for (const key of ['nickname', 'skin', 'serverName', 'password', 'width', 'height', 'windowed']) assert.equal(inspected[key], settings[key], key);
    assert.equal(inspected.autoConnect, true); assert.equal(inspected.hostMode, true);
    assert.equal(inspected.voice, false); assert.equal(inspected.volume, .23); assert.equal(inspected.micGain, 1.75);
    const args = inspected.serverArgs;
    assert.equal(args[args.indexOf('--name') + 1], settings.serverName);
    assert.equal(args[args.indexOf('--password') + 1], specialPassword);
    assert.equal(args[args.indexOf('--host') + 1], settings.nickname);
    console.log('Native config: Unicode nick, skin, quoted password/name, video, voice and Windows argument parsing OK');
    async function launchApp(name, game, overrides) {
      const data = path.join(base, name); await fs.mkdir(data);
      await fs.writeFile(path.join(data, 'settings.json'), JSON.stringify({ ...defaults, gameDir: game, ...overrides }));
      const app = await electron.launch({ args: process.env.SACOOP_PACKAGED_EXE ? [] : [path.resolve(__dirname, '..')],
        executablePath: process.env.SACOOP_PACKAGED_EXE,
        env: { ...process.env, SACOOP_LAUNCHER_DATA: data } });
      apps.push(app);
      const page = await app.firstWindow();
      await page.waitForFunction(() => document.querySelector('#game-status').textContent === 'Готова до гри');
      return page;
    }
    const host = await launchApp('host-profile', hostGame, settings);
    const guest = await launchApp('guest-profile', guestGame, { nickname: 'Гість_Ї', skin: 270 });
    await host.evaluate(() => window.launcher.host());
    const hostState = await host.evaluate(() => window.launcher.state());
    await guest.evaluate(async address => window.launcher.join(address, '"пароль "місто"\\"'),
      `sacoop://127.0.0.1:${lobbyPort}/${hostState.room.code}`);
    // Change the password in the existing lobby; already-connected players must
    // receive it before their native clients are started.
    const updatedPassword = '"новий "Grove"\\"';
    await host.evaluate(async password => {
      const state = await window.launcher.state();
      await window.launcher.save({ ...state.settings, password, maxPlayers: 4 });
    }, updatedPassword);
    await guest.evaluate(() => window.launcher.profile({ name: 'Новий_Їжак', skin: 269, remember: false }));
    await guest.evaluate(() => window.launcher.ready(true));
    await until(async () => (await host.evaluate(() => window.launcher.state())).room.members.every(m => m.ready), 'ready');
    await host.evaluate(() => window.launcher.start());
    try {
      await until(async () => (await host.evaluate(() => window.launcher.state())).room.status === 'running', 'native host authentication', 45000);
    } catch (error) {
      console.error('Host launch diagnostics:', await host.evaluate(() => document.querySelector('#toast')?.textContent));
      console.error('Native status:', await fs.readFile(path.join(hostGame, 'coop/launcher.ini.status.json'), 'utf8').catch(() => 'missing'));
      throw error;
    }
    await until(async () => (await guest.evaluate(() => window.launcher.state())).room.members.every(m => m.playing), 'native guest authentication');
    const hostResult = await result(hostGame), guestResult = await result(guestGame);
    assert.equal(hostResult.serverName, settings.serverName); assert.equal(guestResult.serverName, settings.serverName);
    assert.equal(hostResult.friendlyFire, true); assert.equal(guestResult.friendlyFire, true);
    assert.equal(guestResult.nickname, 'Новий_Їжак'); assert.equal(guestResult.skin, 269);
    assert.equal(guestResult.port, port);
    assert.equal((await guest.evaluate(() => window.launcher.state())).settings.nickname, 'Гість_Ї');
    console.log('Real launcher -> native config -> actual ENet server -> both clients authenticated: OK');
    const rejectedGame = path.join(base, 'Відхилений гість');
    await gameFixture(rejectedGame);
    const rejectedSettings = { ...defaults, gameDir: rejectedGame, nickname: 'Rejected' };
    await writeGameConfig(rejectedSettings, { mode: 'join', address: '127.0.0.1', port, password: 'wrong-password', session: 'rejected-native' });
    const rejectedSpec = launchSpec(rejectedSettings);
    await assert.rejects(execFile(rejectedSpec.command, rejectedSpec.args, { ...rejectedSpec.options, timeout: 10000 }), error => error.code === 2);
    const rejectedStatus = JSON.parse(await fs.readFile(path.join(rejectedGame, 'coop/launcher.ini.status.json'), 'utf8'));
    assert.equal(rejectedStatus.state, 'failed'); assert.match(rejectedStatus.message, /wrong password/);
    console.log('Wrong password: real server rejects native client and reports failure to launcher status: OK');
    // Guest exits its process, changes its per-lobby identity and rejoins the live server.
    await fs.writeFile(path.join(guestGame, 'coop/probe-stop'), 'stop');
    try {
      await until(async () => !(await guest.evaluate(() => window.launcher.state())).playing, 'guest process exit');
    } catch (error) {
      console.error('Guest exit diagnostics:', await guest.evaluate(async () => {
        const state = await window.launcher.state();
        return { playing: state.playing, launching: state.launching, roomStatus: state.room?.status };
      }));
      console.error('Guest native status:', await fs.readFile(path.join(guestGame, 'coop/launcher.ini.status.json'), 'utf8').catch(() => 'missing'));
      console.error('Guest child handles:', await apps[1].evaluate(() => process._getActiveHandles()
        .filter(handle => handle.constructor.name === 'ChildProcess')
        .map(handle => ({ pid: handle.pid, exitCode: handle.exitCode, spawnfile: handle.spawnfile }))));
      throw error;
    }
    await fs.rm(path.join(guestGame, 'coop/probe-stop'));
    await guest.evaluate(() => window.launcher.profile({ name: 'Повернувся_Ї', skin: 105, remember: false }));
    await guest.evaluate(() => window.launcher.ready(true));
    try {
      await until(async () => (await result(guestGame))?.nickname === 'Повернувся_Ї', 'rejoin native handshake');
    } catch (error) {
      console.error('Rejoin state:', await guest.evaluate(async () => {
        const state = await window.launcher.state();
        return { playing: state.playing, launching: state.launching, members: state.room?.members, roomStatus: state.room?.status };
      }));
      console.error('Rejoin status:', await fs.readFile(path.join(guestGame, 'coop/launcher.ini.status.json'), 'utf8').catch(() => 'missing'));
      throw error;
    }
    assert.equal((await result(guestGame)).skin, 105);
    console.log('Exit, lobby nickname/skin change, and rejoin of running actual server: OK');
    await fs.writeFile(path.join(hostGame, 'coop/probe-stop'), 'stop');
    await fs.writeFile(path.join(guestGame, 'coop/probe-stop'), 'stop');
    await until(async () => {
      const h = await host.evaluate(() => window.launcher.state()), g = await guest.evaluate(() => window.launcher.state());
      return !h.playing && !g.playing && !h.launching && !g.launching;
    }, 'process and pending launch cleanup');
    console.log('Native integration passed. GTA rendering/SCM/skin application were not executed.');
  } finally {
    for (const game of [hostGame, guestGame]) await fs.writeFile(path.join(game, 'coop/probe-stop'), 'stop').catch(() => {});
    for (const app of apps.reverse()) {
      // On assertion failures, do not hang cleanup behind the production close
      // guard while a fixture launch is still being unwound.
      await app.evaluate(({ app }) => app.exit(0)).catch(() => {});
      await app.close().catch(() => {});
    }
    await fs.rm(base, { recursive: true, force: true });
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
