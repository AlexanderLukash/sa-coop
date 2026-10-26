const { app, BrowserWindow, ipcMain, dialog, shell, clipboard } = require('electron');
const path = require('node:path');
const fs = require('node:fs/promises');
const os = require('node:os');
const { spawn } = require('node:child_process');
const { pathToFileURL } = require('node:url');
const { randomUUID } = require('node:crypto');
const { readStatus, waitForStatus } = require('./core/session-status.cjs');
const { defaults, validate, atomicWrite, readText, writeGameConfig, launchSpec, lanAddresses } = require('./core/config.cjs');
const { inspectGame, installedBuild, latestRelease, download, installArchive, checkInstallAccess } = require('./core/install.cjs');
const { installComfortMods } = require('./core/comfort-mods.cjs');
if (process.platform === 'linux') app.disableHardwareAcceleration();
const { LanLobbyServer, LobbyClient, discover, serverSettings } = require('./core/lobby.cjs');
const skins = require('./skins.json');

if (process.env.SACOOP_LAUNCHER_DATA) app.setPath('userData', process.env.SACOOP_LAUNCHER_DATA);
const locked = app.requestSingleInstanceLock();
if (!locked) app.quit();
let window, settings = { ...defaults }, gameProcess, lobbyServer, lobbyClient, release, installation;
let room = null, selfId = null, busy = false, operation = null, launching = false, connectionBusy = false;
const rendererURL = pathToFileURL(path.join(__dirname, 'renderer/index.html')).href;
const notify = (type, data) => { if (window && !window.isDestroyed()) window.webContents.send('launcher:event', { type, ...data }); };
const configPath = () => path.join(app.getPath('userData'), 'settings.json');
function requireIdle() {
  if (gameProcess) throw new Error('Спочатку закрийте GTA.');
  if (launching) throw new Error('Гра вже запускається.');
  if (busy) throw new Error('Дочекайтесь завершення поточної операції.');
  if (connectionBusy) throw new Error('Дочекайтесь підключення до лобі.');
}
async function snapshot() {
  return { settings, platform: process.platform, version: app.getVersion(), game: await inspectGame(settings.gameDir),
    addresses: lanAddresses().map(a => a.address), skins, room, selfId, playing: !!gameProcess, launching, busy, operation, release };
}
async function save(input) {
  if (connectionBusy) throw new Error('Дочекайтесь підключення до лобі.');
  const next = validate(input);
  if (gameProcess || launching) throw new Error('Налаштування можна змінити після закриття GTA.');
  if (busy) throw new Error('Триває інсталяція.');
  if (lobbyClient) {
    const current = room?.members?.find(m => m.id === selfId);
    if (next.gameDir !== settings.gameDir || next.lobbyPort !== settings.lobbyPort)
      throw new Error('Вийдіть із лоббі, щоб змінити профіль, папку, порт лоббі або пароль.');
    const changedServer = JSON.stringify(roomSettings(next)) !== JSON.stringify(roomSettings(settings)) || next.password !== settings.password;
    if (!current?.host && changedServer) throw new Error('Налаштування сервера доступні лише хосту.');
    if (current?.host && changedServer) {
      if (room.status !== 'waiting') throw new Error('Дочекайтесь завершення сесії.');
      if (next.maxPlayers < room.members.length) throw new Error('У лоббі вже більше гравців.');
      await lobbyClient.updateSettings(roomSettings(next), next.password);
    }
    if (next.nickname !== settings.nickname || next.skin !== settings.skin) await lobbyClient.updateProfile(next.nickname, next.skin);
  }
  await atomicWrite(configPath(), JSON.stringify(next, null, 2)); settings = next;
  return snapshot();
}
function roomSettings(s) {
  return { name: s.serverName, maxPlayers: s.maxPlayers, gamePort: s.port, friendlyFire: s.friendlyFire, allowCheats: s.allowCheats, loadSlot: s.loadSlot };
}
async function leave() {
  lobbyClient?.removeAllListeners(); lobbyClient?.close(); lobbyClient = null;
  if (lobbyServer) await lobbyServer.close();
  lobbyServer = null; room = null; selfId = null;
  notify('lobby', { room: null, selfId: null });
}
async function join(address, password, hostToken) {
  const client = new LobbyClient(); lobbyClient = client;
  client.on('closed', () => {
    if (lobbyClient !== client) return;
    room = null; selfId = null; notify('lobby', { room: null, selfId: null });
    notify('notice', { message: 'З’єднання з лоббі завершено.' });
    lobbyClient = null;
  });
  client.on('message', message => {
    if (message.type === 'state') { room = message.room; notify('lobby', { room, selfId: client.id }); }
    else if (message.type === 'error' || message.type === 'notice' || message.type === 'closed') notify('notice', { message: message.message });
    else if (message.type === 'launch-host') {
      launch('host').catch(error => {
        notify('notice', { message: error.message });
        if (client.ws?.readyState === 1) client.send({ type: 'start-failed', reason: error.message });
      });
    } else if (['game-started', 'launch-guest'].includes(message.type) && room?.status === 'running' &&
      room.members.some(m => m.id === client.id && !m.host && m.ready) && message.port === room.settings.gamePort) {
      launch('join', { address: client.endpoint.host, port: message.port, password: client.password })
        .catch(error => {
          notify('notice', { message: error.message });
          if (client.ws?.readyState === 1) client.send({ type: 'launch-failed' });
        });
    }
  });
  try { selfId = await client.connect(address, settings.nickname, password, hostToken, await installedBuild(settings.gameDir), settings.skin); }
  catch (error) { client.removeAllListeners(); client.close(); if (lobbyClient === client) lobbyClient = null; throw error; }
  return snapshot();
}
async function launch(mode, connection = {}) {
  requireIdle();
  launching = true;
  try {
  const game = await inspectGame(settings.gameDir);
  if (!game.supported || !game.installed) throw new Error('Оберіть GTA 1.0 US та встановіть мод у розділі «Файли гри».');
  let active = settings;
  const member = room?.members.find(m => m.id === selfId);
  if (member) active = { ...active, nickname: member.name, skin: member.skin };
  if (mode === 'host' && room) {
    const r = serverSettings(room.settings);
    active = { ...active, serverName: r.name, maxPlayers: r.maxPlayers, port: r.gamePort,
      friendlyFire: r.friendlyFire, allowCheats: r.allowCheats, loadSlot: r.loadSlot };
  }
  const session = randomUUID(), ownerClient = lobbyClient;
  const configFile = await writeGameConfig(active, { mode, password: mode === 'host' ? settings.password : '', ...connection, session });
  const statusFile = configFile + '.status.json';
  const spec = launchSpec(active);
  if (process.platform === 'linux' && window && !window.isDestroyed()) window.minimize();
  const child = spawn(spec.command, spec.args, spec.options);
  try {
    await new Promise((resolve, reject) => { child.once('spawn', resolve); child.once('error', reject); });
  } catch (error) {
    if (window && !window.isDestroyed()) window.restore();
    throw error;
  }
  gameProcess = child; notify('game', { playing: true });
  let monitor, reportedEnd = false;
  const reportGameEnd = () => {
    if (reportedEnd) return;
    reportedEnd = true;
    if (ownerClient === lobbyClient && ownerClient?.ws?.readyState === 1)
      ownerClient.send({ type: mode === 'host' ? 'game-ended' : 'game-left' });
  };
  child.on('error', error => notify('notice', { message: error.message }));
  child.once('exit', () => {
    clearInterval(monitor);
    if (gameProcess === child) gameProcess = null;
    if (process.platform === 'linux' && window && !window.isDestroyed()) { window.restore(); window.focus(); }
    notify('game', { playing: false });
    reportGameEnd();
  });
  if (mode !== 'solo') {
    await waitForStatus(statusFile, session, { isAlive: () => gameProcess === child && child.exitCode === null });
    if (ownerClient !== lobbyClient || ownerClient?.ws?.readyState !== 1) throw new Error('Підключення до лобі втрачено.');
    ownerClient.send({ type: mode === 'host' ? 'game-started' : 'game-entered' });
    let checking = false, disconnected = false;
    monitor = setInterval(async () => {
      if (checking || gameProcess !== child) return;
      checking = true;
      try {
        const status = await readStatus(statusFile, session);
        if (!disconnected && status && ['failed', 'offline'].includes(status.state)) {
          disconnected = true;
          notify('notice', { message: status.message || 'Мод від’єднався. Закрийте GTA, щоб підключитися повторно.' });
          reportGameEnd();
        }
      } catch (error) { notify('notice', { message: error.message }); }
      finally { checking = false; }
    }, 1000);
    // Keep the game monitor referenced until the child exits. Packaged Electron
    // must keep pumping Node events even while its renderer is in the background;
    // an offline status may arrive just before the process actually terminates.
  }
  return snapshot();
  } finally { launching = false; }
}
async function install() {
  requireIdle();
  if (lobbyClient) throw new Error('Вийдіть із лоббі перед оновленням мода.');
  requireIdle();
  busy = true; installation = new AbortController(); notify('busy', { busy });
  let temp;
  try {
    await checkInstallAccess(settings.gameDir);
    release = await latestRelease(settings.releaseRepo);
    temp = await fs.mkdtemp(path.join(os.tmpdir(), 'sacoop-download-'));
    const archive = path.join(temp, 'SA-COOP.zip');
    await download(release, archive, progress => notify('progress', progress), installation.signal);
    await installArchive(archive, settings.gameDir, release.version, progress => notify('progress', progress), installation.signal);
  } finally {
    try { if (temp) await fs.rm(temp, { recursive: true, force: true }); }
    finally { busy = false; installation = null; notify('busy', { busy }); }
  }
  return snapshot();
}
function handle(name, fn) {
  ipcMain.handle(name, async (event, ...args) => {
    if (event.senderFrame !== window.webContents.mainFrame || event.senderFrame.url !== rendererURL)
      throw new Error('Невідоме джерело запиту.');
    return fn(...args);
  });
}
async function createWindow() {
  window = new BrowserWindow({ width: 1280, height: 840, minWidth: 1020, minHeight: 720,
    backgroundColor: '#0c0b14', autoHideMenuBar: true, title: 'SA COOP — Back to the Grove',
    icon: path.join(__dirname, '../assets/icon.png'),
    webPreferences: { preload: path.join(__dirname, 'preload.cjs'), contextIsolation: true, sandbox: process.platform !== 'linux', nodeIntegration: false } });
  window.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  window.webContents.on('will-navigate', event => event.preventDefault());
  window.webContents.session.setPermissionRequestHandler((_webContents, _permission, callback) => callback(false));
  window.on('close', event => {
    if (gameProcess || busy || launching) { event.preventDefault(); notify('notice', { message: busy ? 'Дочекайтесь завершення інсталяції або скасуйте її.' : 'Дочекайтесь запуску та закрийте GTA перед виходом із лаунчера.' }); }
  });
  await window.loadFile(path.join(__dirname, 'renderer/index.html'));
}

app.on('second-instance', () => { window?.restore(); window?.focus(); });
app.whenReady().then(async () => {
  try { const content = await readText(configPath()); if (content) settings = validate(JSON.parse(content)); }
  catch { dialog.showErrorBox('Налаштування лаунчера', 'Не вдалося прочитати налаштування. Використовуємо стандартні.'); }
  handle('state', snapshot); handle('save', save);
  handle('profile', async (input) => {
    requireIdle();
    const next = validate({ ...settings, nickname: input.name, skin: input.skin });
    if (lobbyClient) await lobbyClient.updateProfile(next.nickname, next.skin);
    if (input.remember || !lobbyClient) {
      await atomicWrite(configPath(), JSON.stringify(next, null, 2)); settings = next;
    }
    return snapshot();
  });
  handle('choose-game', async () => {
    requireIdle();
    const result = await dialog.showOpenDialog(window, { title: 'Папка GTA San Andreas', properties: ['openDirectory'] });
    if (!result.canceled) {
      const game = await inspectGame(result.filePaths[0]);
      if (!game.found) throw new Error('У цій папці немає gta_sa.exe.');
      await save({ ...settings, gameDir: result.filePaths[0] });
      if (game.supported) {
        busy = true; operation = 'comfort-mods'; installation = new AbortController();
        notify('busy', { busy, operation });
        try {
          const result = await installComfortMods(settings.gameDir, settings,
            progress => notify('progress', progress), installation.signal);
          const skipped = [...result.skipped];
          try {
            const localization = require('./core/localization.cjs');
            const localized = await localization.installUkrainian(settings.gameDir, settings,
              progress => notify('progress', progress), installation.signal);
            skipped.push(...localized.skipped);
          } catch (error) { if (installation.signal?.aborted) throw error; skipped.push(`Українська локалізація: ${error.message}`); }
          notify('notice', { message: skipped.length
            ? `Налаштування завершено. Пропущено: ${skipped.join('; ')}`
            : 'Гру налаштовано: моди та українська локалізація встановлені.' });
        } catch (error) { notify('notice', { message: `Папку GTA збережено, але встановлення модів не завершилось: ${error.message}` }); }
        finally { busy = false; operation = null; installation = null; notify('busy', { busy, operation }); }
      }
    }
    return snapshot();
  });
  handle('release', async () => { release = await latestRelease(settings.releaseRepo); return release; });
  handle('install', install); handle('cancel-install', () => installation?.abort());
  handle('discover', target => discover(1300, target ? [target] : []));
  handle('host', async () => {
    requireIdle(); if (lobbyClient || lobbyServer) throw new Error('Вийдіть із поточного лоббі.');
    connectionBusy = true;
    try {
    const game = await inspectGame(settings.gameDir);
    if (!game.supported || !game.installed) throw new Error('Спочатку встановіть мод у сумісну GTA.');
    lobbyServer = new LanLobbyServer(roomSettings(settings), settings.password, await installedBuild(settings.gameDir));
    try {
      const info = await lobbyServer.start(settings.lobbyPort);
      return await join(`sacoop://127.0.0.1:${info.port}/${info.code}`, settings.password, info.hostToken);
    } catch (error) { await leave(); throw error; }
    } finally { connectionBusy = false; }
  });
  handle('join', async (address, password) => {
    requireIdle(); if (lobbyClient || lobbyServer) throw new Error('Вийдіть із поточного лоббі.');
    connectionBusy = true;
    try {
    const game = await inspectGame(settings.gameDir);
    if (!game.supported || !game.installed) throw new Error('Спочатку встановіть мод у сумісну GTA.');
    return await join(address, password);
    } finally { connectionBusy = false; }
  });
  handle('leave', async () => { requireIdle(); await leave(); return snapshot(); });
  handle('ready', ready => { lobbyClient?.send({ type: 'ready', ready: !!ready }); });
  handle('start', () => { requireIdle(); if (!lobbyClient) throw new Error('Спочатку створіть лоббі.'); lobbyClient.send({ type: 'start' }); });
  handle('solo', () => launch('solo'));
  handle('copy-invite', () => {
    if (!room || !lobbyClient) throw new Error('Спочатку приєднайтесь до лоббі.');
    const host = lobbyServer ? settings.networkAddress || lanAddresses()[0]?.address : lobbyClient.endpoint.host;
    if (!host) throw new Error('Локальну IPv4-адресу не знайдено.');
    const invite = `sacoop://${host}:${lobbyClient.endpoint.port}/${room.code}`;
    clipboard.writeText(invite); return invite;
  });
  handle('open-folder', async () => {
    if (!settings.gameDir) throw new Error('Оберіть папку GTA.');
    const error = await shell.openPath(settings.gameDir); if (error) throw new Error(error);
  });
  handle('open-releases', () => shell.openExternal(`https://github.com/${settings.releaseRepo}/releases`));
  await createWindow();
});
app.on('window-all-closed', () => { leave().finally(() => app.quit()); });
