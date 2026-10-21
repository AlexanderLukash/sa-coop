const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const skins = require('../skins.json');
const { isIPv4 } = require('node:net');
const { randomUUID } = require('node:crypto');

const defaults = {
  nickname: 'Player', gameDir: '', releaseRepo: 'AlexanderLukash/sa-coop',
  wineCommand: 'wine', winePrefix: '', windowed: true, width: 1280, height: 720,
  voiceEnabled: true, voiceVolume: 80, micGain: 100,
  serverName: 'Grove Street crew', maxPlayers: 8, port: 7788, lobbyPort: 7790,
  friendlyFire: false, password: '', loadSlot: -1, skin: 0, networkAddress: '',
};

function cleanString(value, max = 128) {
  if (typeof value !== 'string' || value.length > max || /[\r\n\0]/.test(value))
    throw new Error('Некоректне текстове значення.');
  return value.trim();
}
function integer(value, min, max, label) {
  if (!Number.isInteger(value) || value < min || value > max)
    throw new Error(`${label}: допустимі значення ${min}–${max}.`);
  return value;
}
function validate(input) {
  const s = { ...defaults };
  for (const key of Object.keys(defaults)) if (Object.hasOwn(input, key)) s[key] = input[key];
  s.nickname = cleanString(s.nickname, 20);
  if (!/^[\p{L}\p{N}_\-.\[\]]{2,20}$/u.test(s.nickname))
    throw new Error('Нікнейм: 2–20 літер, цифр або символів _-.[]');
  for (const k of ['gameDir', 'winePrefix']) s[k] = cleanString(s[k], 4096);
  s.wineCommand = cleanString(s.wineCommand, 4096);
  if (!s.wineCommand) throw new Error('Вкажіть виконуваний файл Wine.');
  s.serverName = cleanString(s.serverName, 64);
  if (!s.serverName) throw new Error('Додайте назву лоббі.');
  s.password = cleanString(s.password, 64);
  if (Buffer.byteLength(s.password, 'utf8') > 64 || Buffer.byteLength(s.serverName, 'utf8') > 64)
    throw new Error('Пароль і назва сервера мають вміщатися в 64 байти UTF-8. Скоротіть текст.');
  s.networkAddress = cleanString(s.networkAddress, 64);
  if (s.networkAddress && !isIPv4(s.networkAddress)) throw new Error('Оберіть IPv4-адресу мережевого адаптера.');
  if (!skins.some(skin => skin.id === s.skin)) throw new Error('Невідомий скін.');
  if (!/^[A-Za-z0-9][A-Za-z0-9-]{0,38}\/[A-Za-z0-9][A-Za-z0-9_.-]{0,99}$/.test(s.releaseRepo)) throw new Error('Репозиторій: owner/repo.');
  integer(s.maxPlayers, 1, 8, 'Кількість гравців');
  for (const k of ['port', 'lobbyPort']) integer(s[k], 1024, 65535, 'Порт');
  integer(s.loadSlot, -1, 8, 'Збереження');
  integer(s.width, 640, 7680, 'Ширина'); integer(s.height, 480, 4320, 'Висота');
  integer(s.voiceVolume, 0, 100, 'Гучність'); integer(s.micGain, 0, 200, 'Мікрофон');
  for (const k of ['windowed', 'voiceEnabled', 'friendlyFire'])
    if (typeof s[k] !== 'boolean') throw new Error('Некоректний перемикач.');
  return s;
}

function updateIni(text, updates) {
  // Preserve unknown settings/comments; replace only keys owned by this launcher.
  const lines = text.replace(/^\uFEFF/, '').split(/\r?\n/);
  const pending = structuredClone(updates);
  let section = '';
  const output = [];
  function flush() {
    for (const [key, value] of Object.entries(pending[section] || {})) output.push(`${key} = ${iniValue(value)}`);
    delete pending[section];
  }
  for (const line of lines) {
    const header = line.match(/^\s*\[([^\]]+)\]/);
    if (header) { flush(); section = header[1].toLowerCase(); }
    const key = line.match(/^\s*([^;#=\s]+)\s*=/)?.[1]?.toLowerCase();
    if (key && Object.hasOwn(updates[section] || {}, key)) {
      if (Object.hasOwn(pending[section] || {}, key)) {
        output.push(`${key} = ${iniValue(pending[section][key])}`);
        delete pending[section][key];
      }
    } else output.push(line);
  }
  flush();
  for (const [name, values] of Object.entries(pending)) {
    output.push('', `[${name}]`);
    for (const [key, value] of Object.entries(values)) output.push(`${key} = ${iniValue(value)}`);
  }
  return output.join('\r\n').replace(/\s*$/, '') + '\r\n';
}
function iniValue(value) {
  const text = String(value);
  // The native parser strips one pair of enclosing quotes, without unescaping.
  return text.startsWith('"') && text.endsWith('"') ? `"${text}"` : text;
}

async function atomicWrite(file, contents) {
  await fs.mkdir(path.dirname(file), { recursive: true });
  const temp = `${file}.${randomUUID()}.tmp`;
  await fs.writeFile(temp, contents, { mode: 0o600 });
  await fs.rename(temp, file);
}
async function readText(file) {
  try { return await fs.readFile(file, 'utf8'); } catch (e) { if (e.code === 'ENOENT') return ''; throw e; }
}
async function writeGameConfig(settings, { mode = 'solo', address = '127.0.0.1', password = '', port, loadSlot, session = '' } = {}) {
  const s = validate(settings);
  address = cleanString(address, 256); password = cleanString(password, 64);
  if (mode !== 'solo' && !['host', 'join'].includes(mode)) throw new Error('Невідомий режим.');
  const coop = path.join(s.gameDir, 'coop');
  const file = path.join(coop, 'launcher.ini');
  const config = updateIni(await readText(file), {
    player: { name: s.nickname, language: 'en', skin: s.skin },
    connect: { auto_connect: Number(mode !== 'solo'), mode: mode === 'host' ? 'host' : 'join',
      host: address, port: port ?? s.port, password, server_name: s.serverName, load_slot: loadSlot ?? s.loadSlot,
      friendly_fire: Number(s.friendlyFire) },
    game: { windowed: Number(s.windowed), width: s.width, height: s.height,
      run_in_background: 1, no_pause_menu: 1, skip_intro: 1 },
    voice: { enabled: Number(s.voiceEnabled), volume: s.voiceVolume / 100, mic_gain: s.micGain / 100 },
    debug: { server_data: 'launcher-server-data', launcher_session: cleanString(session, 64) },
  });
  await atomicWrite(file, config);
  if (mode === 'host') {
    const serverFile = path.join(coop, 'server.ini');
    await atomicWrite(serverFile, updateIni(await readText(serverFile), {
      server: { name: s.serverName, max_players: s.maxPlayers, port: s.port,
        password: s.password, friendly_fire: Number(s.friendlyFire), internet: 0, host: s.nickname },
    }));
  }
  return file;
}

function launchSpec(settings, platform = process.platform) {
  const env = { ...process.env, SACOOP_CONFIG: 'launcher.ini' };
  const executable = path.join(settings.gameDir, 'gta_sa.exe');
  if (platform === 'win32') return { command: executable, args: [], options: { cwd: settings.gameDir, env, windowsHide: true, stdio: 'ignore' } };
  if (platform !== 'linux') throw new Error('Підтримуються Windows і Linux.');
  if (settings.winePrefix) env.WINEPREFIX = path.resolve(settings.winePrefix);
  // Force Wine to load the game's ASI loader rather than its builtin vorbis module.
  env.WINEDLLOVERRIDES = (env.WINEDLLOVERRIDES ? `${env.WINEDLLOVERRIDES};` : '') + 'vorbisFile=n,b;dinput8=n,b;dsound=n,b';
  return { command: settings.wineCommand, args: [executable], options: { cwd: settings.gameDir, env, stdio: 'ignore' } };
}
function lanAddresses() {
  return Object.values(os.networkInterfaces()).flat().filter(i => i && i.family === 'IPv4' && !i.internal)
    .map(i => ({ address: i.address, netmask: i.netmask }));
}
module.exports = { defaults, validate, integer, cleanString, updateIni, atomicWrite, readText, writeGameConfig, launchSpec, lanAddresses };
