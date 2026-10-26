const { _electron: electron } = require('playwright');
const path = require('node:path');
const fs = require('node:fs/promises');
const assert = require('node:assert/strict');
const { LobbyClient, LanLobbyServer } = require('../src/core/lobby.cjs');
const { installedBuild } = require('../src/core/install.cjs');
async function main() {
  const output = path.resolve(process.env.SACOOP_SCREENSHOTS || path.resolve(__dirname, '../test-results'));
  await fs.mkdir(output, { recursive: true });
  const data = await fs.mkdtemp(path.join(output, 'profile-'));
  const args = process.env.SACOOP_PACKAGED_EXE ? [] : [path.resolve(__dirname, '..')];
  if (process.platform === 'linux' && process.env.CI) args.unshift('--no-sandbox');
  const application = await electron.launch({ args, executablePath: process.env.SACOOP_PACKAGED_EXE,
    env: { ...process.env, SACOOP_LAUNCHER_DATA: data }, timeout: 30000 });
  const errors = [];
  const guests = [];
  let runningServer, runningHost;
  try {
    const page = await application.firstWindow();
    page.on('pageerror', e => errors.push(e.message));
    await page.waitForSelector('#profile-name');
    await page.waitForFunction(() => document.querySelector('#platform-label').textContent !== 'WINDOWS / LINUX');
    await page.screenshot({ path: path.join(output, 'home.png') });
    await page.click('[data-page="settings"]');
    await page.fill('[name="nickname"]', 'GroveCrew');
    await page.fill('[name="serverName"]', 'Our LAN crew');
    await page.selectOption('[name="maxPlayers"]', '8');
    await page.uncheck('[name="allowCheats"]');
    await page.click('[form="settings-form"]');
    await page.waitForFunction(() => document.querySelector('#profile-name').textContent === 'GroveCrew');
    const settings = JSON.parse(await fs.readFile(path.join(data, 'settings.json'), 'utf8'));
    assert.equal(settings.nickname, 'GroveCrew'); assert.equal(settings.maxPlayers, 8);
    assert.equal(settings.allowCheats, false);
    await page.screenshot({ path: path.join(output, 'settings.png') });
    await page.click('[data-page="files"]');
    assert.equal(await page.isDisabled('#install-mod'), true);
    await page.screenshot({ path: path.join(output, 'files.png') });
    await page.click('[data-page="lobby"]');
    await page.waitForSelector('#refresh-lobbies:not([disabled])');
    await page.screenshot({ path: path.join(output, 'lobby.png') });
    await page.click('#create-lobby');
    await page.waitForFunction(() => !document.querySelector('#toast').hidden);
    assert.equal(await page.isVisible('#page-files'), true);
    assert.equal(await page.isVisible('#active-room'), false);
    const overflow = await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth);
    assert.equal(overflow, false, 'page must not overflow horizontally');
    // Real LAN service and IPC with isolated fixture files; never launch the fixture executable.
    const fixture = path.join(data, 'game');
    await fs.mkdir(path.join(fixture, 'coop'), { recursive: true });
    await fs.writeFile(path.join(fixture, 'gta_sa.exe'), require('./game-fixture.cjs').compatibleFixture());
    await fs.writeFile(path.join(fixture, 'coop.asi'), 'ui-test-build');
    await fs.writeFile(path.join(fixture, 'coop/coop-server.exe'), 'ui-test-server');
    await page.evaluate(async gameDir => {
      const s = await window.launcher.state();
      await window.launcher.save({ ...s.settings, gameDir, lobbyPort: 17990 });
    }, fixture);
    await page.reload();
    await page.waitForFunction(() => document.querySelector('#game-status').textContent === 'Готова до гри');
    await page.click('[data-page="lobby"]');
    await page.click('#create-lobby');
    await page.waitForSelector('#page-host.active');
    await page.fill('#host-name', 'Вечір на Grove Street');
    await page.click('#host-form [type="submit"]');
    await page.waitForSelector('#active-room:not([hidden])');
    await page.screenshot({ path: path.join(output, 'lobby-1.png') });
    await page.click('.edit-character');
    await page.waitForSelector('#profile-dialog[open]');
    await page.fill('#player-name', 'HostCrew');
    await page.fill('#skin-search', 'Ryder');
    await page.click('[data-skin="271"]');
    await page.uncheck('#remember-profile');
    await page.screenshot({ path: path.join(output, 'skin-picker.png') });
    await page.click('#profile-form [type="submit"]');
    await page.waitForFunction(() => !document.querySelector('#profile-dialog').open);
    const current = await page.evaluate(() => window.launcher.state());
    const hash = await installedBuild(fixture);
    for (let i = 1; i < 8; i++) {
      const guest = new LobbyClient(); guests.push(guest);
      await guest.connect(`sacoop://127.0.0.1:17990/${current.room.code}`, 'Crew' + i, '', undefined, hash);
      guest.send({ type: 'ready', ready: true });
      if (i === 1) {
        await page.waitForFunction(() => document.querySelectorAll('.member').length === 2);
        await page.screenshot({ path: path.join(output, 'lobby-2.png') });
      }
    }
    await page.waitForFunction(() => document.querySelectorAll('.member.ready').length === 8);
    assert.equal(await page.isDisabled('#start-session'), false);
    assert.equal(await page.locator('.member').count(), 8);
    assert.equal(await page.locator('.member.player-self .member-name').textContent(), 'HostCrew');
    await page.screenshot({ path: path.join(output, 'crew.png') });
    await application.evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows()[0].setSize(1020, 720));
    await page.waitForTimeout(150);
    const startFits = await page.locator('#start-session').evaluate(button => button.getBoundingClientRect().bottom <= window.innerHeight);
    assert.equal(startFits, true, 'eight player controls must fit the minimum window size');
    const allCardsFit = await page.locator('#members').evaluate(grid => grid.getBoundingClientRect().bottom <= window.innerHeight);
    assert.equal(allCardsFit, true, 'all eight cards must fit the minimum window size');
    await page.screenshot({ path: path.join(output, 'crew-small.png') });
    await page.click('#leave-room');
    await page.waitForSelector('#lobby-browser:not([hidden])');
    // A separate real lobby advertises an already-running session; the UI must
    // let the newcomer customize first, with no automatic executable launch.
    runningServer = new LanLobbyServer({ name: 'Сесія вже триває', maxPlayers: 8, gamePort: 7788, loadSlot: -1 }, '', hash);
    const runningInfo = await runningServer.start(0, '127.0.0.1', false);
    const runningAddress = `sacoop://127.0.0.1:${runningInfo.port}/${runningInfo.code}`;
    runningHost = new LobbyClient();
    await runningHost.connect(runningAddress, 'OtherHost', '', runningInfo.hostToken, hash, 270);
    runningHost.send({ type: 'start' });
    while (runningServer.status !== 'starting') await new Promise(resolve => setTimeout(resolve, 10));
    runningHost.send({ type: 'game-started' });
    while (runningServer.status !== 'running') await new Promise(resolve => setTimeout(resolve, 10));
    await page.fill('#join-address', runningAddress);
    await page.click('#join-form [type="submit"]');
    await page.waitForSelector('#active-room:not([hidden])');
    await page.waitForFunction(() => document.querySelector('#ready-button').textContent === 'Приєднатися до гри ↗');
    assert.equal(await page.isDisabled('#ready-button'), false);
    assert.equal(await page.isVisible('#room-server-settings'), false);
    assert.equal(await page.evaluate(async () => (await window.launcher.state()).playing), false);
    await page.screenshot({ path: path.join(output, 'late-join.png') });
    await page.click('[data-page="settings"]');
    assert.equal(await page.locator('#server-settings').evaluate(el => el.disabled), true);
    await page.click('[data-page="lobby"]');
    await page.click('#leave-room');
    assert.deepEqual(errors, []);
    console.log('Electron UI smoke: navigation, settings persistence, install/launch guards, real eight-player lobby and screenshots OK');
  } finally {
    for (const guest of guests) guest.close();
    runningHost?.close();
    if (runningServer) await runningServer.close();
    await application.close();
    await fs.rm(data, { recursive: true, force: true });
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
