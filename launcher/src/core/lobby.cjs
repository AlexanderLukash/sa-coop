const { WebSocketServer, WebSocket } = require('ws');
const { EventEmitter } = require('node:events');
const { randomBytes, randomUUID } = require('node:crypto');
const http = require('node:http');
const dgram = require('node:dgram');
const { isIPv4 } = require('node:net');
const { cleanString, integer, lanAddresses } = require('./config.cjs');
const skins = require('../skins.json');
function profile(input) {
  const name = cleanString(input.name, 20), skin = input.skin ?? 0;
  if (!/^[\p{L}\p{N}_\-.\[\]]{2,20}$/u.test(name)) throw new Error('Некоректний нікнейм.');
  if (!skins.some(s => s.id === skin)) throw new Error('Невідомий скін.');
  return { name, skin };
}

const DISCOVERY_PORT = 7791;
function serverSettings(input) {
  const name = cleanString(input.name, 64);
  if (Buffer.byteLength(name, 'utf8') > 64) throw new Error('Скоротіть назву лобі до 64 байтів UTF-8.');
  if (!name) throw new Error('Вкажіть назву лоббі.');
  return { name, maxPlayers: integer(input.maxPlayers, 1, 8, 'Кількість гравців'),
    gamePort: integer(input.gamePort, 1024, 65535, 'Ігровий порт'),
    friendlyFire: !!input.friendlyFire, loadSlot: integer(input.loadSlot, -1, 8, 'Збереження') };
}
function parseLobbyAddress(input) {
  let text = cleanString(input, 256);
  if (!text.includes('://')) text = 'sacoop://' + text;
  const url = new URL(text);
  if (!['sacoop:', 'ws:'].includes(url.protocol) || url.username || url.password || url.search || url.hash || !url.hostname)
    throw new Error('Адреса лоббі: sacoоp://192.168.1.2:7790/КОД');
  const code = url.pathname.replace(/^\//, '').toUpperCase();
  if (!/^[A-F0-9]{6}$/.test(code)) throw new Error('Адреса має містити 6-символьний код лоббі.');
  const port = url.port ? integer(Number(url.port), 1024, 65535, 'Порт лоббі') : 7790;
  return { host: url.hostname, port, code, url: `ws://${url.hostname}:${port}/${code}` };
}

class LanLobbyServer {
  constructor(settings, password = '', buildHash = '') {
    this.settings = serverSettings(settings);
    this.password = cleanString(password, 64);
    if (Buffer.byteLength(this.password, 'utf8') > 64) throw new Error('Скоротіть пароль до 64 байтів UTF-8.');
    this.buildHash = buildHash;
    this.code = randomBytes(3).toString('hex').toUpperCase();
    this.hostToken = randomBytes(32).toString('hex');
    this.members = new Map(); this.status = 'waiting'; this.owner = null;
  }
  snapshot() {
    return { code: this.code, settings: this.settings, status: this.status, passwordProtected: !!this.password,
      members: [...this.members.values()].map(({ id, name, skin, ready, host, playing, launching }) => ({ id, name, skin, ready, host, playing, launching })) };
  }
  broadcast(message) {
    for (const ws of this.members.keys()) if (ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(message));
  }
  publish() { this.broadcast({ type: 'state', room: this.snapshot() }); }
  async start(port = 7790, bind = '0.0.0.0', discovery = true) {
    this.http = http.createServer((_req, res) => { res.writeHead(404); res.end(); });
    this.wss = new WebSocketServer({ server: this.http, maxPayload: 4096, perMessageDeflate: false });
    this.wss.on('connection', (ws, request) => {
      // Lobby APIs are used by the native main process, never a website.
      if (request.url !== '/' + this.code || request.headers.origin || this.wss.clients.size > 24) return ws.close(1008);
      let last = 0, rate = 0;
      ws.alive = true;
      ws.on('pong', () => { ws.alive = true; });
      const joinTimeout = setTimeout(() => { if (!this.members.has(ws)) ws.close(1008); }, 5000);
      ws.on('message', data => {
        try {
          const now = Date.now();
          if (now - last > 1000) { last = now; rate = 0; }
          if (++rate > 20) throw new Error('Забагато запитів.');
          const message = JSON.parse(data.toString());
          if (!this.members.has(ws)) {
            if (message.type !== 'join') throw new Error('Спочатку приєднайтесь до лоббі.');
            if (this.status === 'starting') throw new Error('Сесія запускається. Спробуйте за кілька секунд.');
            if (this.members.size >= this.settings.maxPlayers) throw new Error('Лоббі заповнене.');
            if (message.password !== this.password) throw new Error('Неправильний пароль.');
            if (this.buildHash && message.buildHash !== this.buildHash) throw new Error('Версія мода відрізняється від хоста. Встановіть однаковий реліз.');
            const { name, skin } = profile(message);
            if ([...this.members.values()].some(m => m.name.toLowerCase() === name.toLowerCase())) throw new Error('Цей нікнейм уже зайнятий.');
            const host = message.hostToken === this.hostToken;
            if (host && this.owner) throw new Error('Хост уже приєднався.');
            const member = { id: randomUUID(), name, skin, ready: host, host, playing: false, launching: false };
            this.members.set(ws, member);
            if (host) this.owner = ws;
            clearTimeout(joinTimeout);
            ws.send(JSON.stringify({ type: 'joined', id: member.id })); this.publish(); return;
          }
          const member = this.members.get(ws);
          if (message.type === 'profile') {
            if (this.status === 'starting' || member.playing || member.launching) throw new Error('Змінити персонажа можна до входу в гру.');
            const next = profile(message);
            if ([...this.members.values()].some(m => m !== member && m.name.toLowerCase() === next.name.toLowerCase())) throw new Error('Цей нікнейм уже зайнятий.');
            Object.assign(member, next, { ready: member.host });
            this.publish(); ws.send(JSON.stringify({ type: 'profile-updated', requestId: message.requestId, profile: next }));
          } else if (message.type === 'ready') {
            if (this.status === 'starting' || member.playing || member.launching) throw new Error('Зачекайте завершення запуску або закрийте GTA.');
            member.ready = member.host || !!message.ready;
            if (this.status === 'running' && member.ready && !member.host) {
              member.launching = true; this.publish(); ws.send(JSON.stringify({ type: 'launch-guest', port: this.settings.gamePort }));
            } else this.publish();
          } else if (message.type === 'settings') {
            if (!member.host || this.status !== 'waiting') throw new Error('Змінити параметри може лише хост до запуску.');
            const settings = serverSettings(message.settings);
            if (settings.maxPlayers < this.members.size) throw new Error('У лоббі вже більше гравців.');
            const password = Object.hasOwn(message, 'password') ? cleanString(message.password, 64) : this.password;
            if (Buffer.byteLength(password, 'utf8') > 64) throw new Error('Скоротіть пароль до 64 байтів UTF-8.');
            this.settings = settings;
            if (Object.hasOwn(message, 'password')) {
              this.password = password;
              this.broadcast({ type: 'password-changed', password: this.password });
            }
            for (const m of this.members.values()) m.ready = m.host;
            this.publish();
            ws.send(JSON.stringify({ type: 'settings-updated', requestId: message.requestId }));
          } else if (message.type === 'start') {
            if (!member.host || this.status !== 'waiting') throw new Error('Запуск доступний лише хосту.');
            if ([...this.members.values()].some(m => !m.ready)) throw new Error('Дочекайтесь готовності всіх гравців.');
            this.status = 'starting'; this.publish(); ws.send(JSON.stringify({ type: 'launch-host' }));
          } else if (message.type === 'game-started') {
            if (!member.host || this.status !== 'starting') throw new Error('Некоректний запуск.');
            member.playing = true;
            for (const m of this.members.values()) if (!m.host && m.ready) m.launching = true;
            this.status = 'running'; this.publish();
            this.broadcast({ type: 'game-started', port: this.settings.gamePort });
          } else if (message.type === 'game-entered' || message.type === 'game-left' || message.type === 'launch-failed') {
            if (member.host || (message.type !== 'game-left' && this.status !== 'running')) throw new Error('Некоректний стан гри.');
            if (message.type === 'game-entered' && !member.launching) throw new Error('Спочатку підтвердіть готовність.');
            member.playing = message.type === 'game-entered'; member.launching = false;
            member.ready = member.playing; this.publish();
          } else if (message.type === 'start-failed' || message.type === 'game-ended') {
            if (!member.host) throw new Error('Дія доступна лише хосту.');
            this.status = 'waiting';
            for (const m of this.members.values()) Object.assign(m, { ready: m.host, playing: false, launching: false });
            this.publish();
            this.broadcast({ type: 'notice', message: message.type === 'start-failed'
              ? `Кооп хоста не підключився до сервера.${typeof message.reason === 'string' ? ' ' + message.reason.slice(0, 500) : ''}`
              : 'Хост завершив сесію.' });
          } else throw new Error('Невідома дія.');
        } catch (error) { if (ws.readyState === WebSocket.OPEN) {
          let requestId; try { requestId = JSON.parse(data.toString()).requestId; } catch {}
          ws.send(JSON.stringify({ type: 'error', message: error.message, requestId }));
        } }
      });
      ws.on('error', () => {});
      ws.on('close', () => {
        clearTimeout(joinTimeout);
        const wasOwner = ws === this.owner;
        this.members.delete(ws);
        if (wasOwner) {
          this.owner = null;
          this.broadcast({ type: 'closed', message: 'Хост закрив лоббі.' });
          for (const peer of this.members.keys()) peer.close();
        } else this.publish();
      });
    });
    await new Promise((resolve, reject) => {
      this.http.once('error', reject); this.http.listen(port, bind, resolve);
    });
    this.port = this.http.address().port;
    this.heartbeat = setInterval(() => {
      for (const peer of this.wss.clients) {
        if (!peer.alive) peer.terminate();
        else { peer.alive = false; peer.ping(); }
      }
    }, 15000);
    this.heartbeat.unref();
    if (discovery) {
      this.udp = dgram.createSocket({ type: 'udp4', reuseAddr: true });
      this.udp.on('error', () => {});
      this.udp.on('message', (buffer, remote) => {
        if (buffer.toString() !== 'SACOOP_DISCOVER_V1' || !this.owner) return;
        const info = { protocol: 1, code: this.code, port: this.port, name: this.settings.name,
          count: this.members.size, maxPlayers: this.settings.maxPlayers, status: this.status, locked: !!this.password };
        this.udp.send(JSON.stringify(info), remote.port, remote.address, () => {});
      });
      this.udp.bind(DISCOVERY_PORT, bind);
    }
    return { port: this.port, code: this.code, hostToken: this.hostToken };
  }
  async close() {
    clearInterval(this.heartbeat);
    if (this.udp) { try { this.udp.close(); } catch {} }
    for (const peer of this.wss?.clients || []) peer.terminate();
    await new Promise(resolve => this.wss ? this.wss.close(resolve) : resolve());
    await new Promise(resolve => this.http?.listening ? this.http.close(resolve) : resolve());
  }
}

class LobbyClient extends EventEmitter {
  async connect(address, name, password = '', hostToken, buildHash = '', skin = 0) {
    this.endpoint = parseLobbyAddress(address);
    this.password = cleanString(password, 64);
    this.ws = new WebSocket(this.endpoint.url, { handshakeTimeout: 5000, maxPayload: 65536 });
    return new Promise((resolve, reject) => {
      let joined = false;
      const timeout = setTimeout(() => { this.ws.terminate(); reject(new Error('Лоббі не відповідає.')); }, 7000);
      this.ws.on('open', () => this.send({ type: 'join', name, password: this.password, hostToken, buildHash, skin }));
      this.ws.on('error', () => { if (!joined) reject(new Error('Не вдалося підключитися до лоббі. Перевірте адресу та мережу.')); });
      this.ws.on('close', () => { clearTimeout(timeout); this.emit('closed'); if (!joined) reject(new Error('Лоббі закрито.')); });
      this.ws.on('message', data => {
        try {
          const m = JSON.parse(data.toString());
          if (m.type === 'joined') this.id = m.id;
          if (m.type === 'state') {
            this.room = m.room;
            if (!joined && this.id && m.room.members.some(member => member.id === this.id)) {
              joined = true; clearTimeout(timeout); resolve(this.id);
            }
          }
          if (m.type === 'password-changed') this.password = m.password;
          if (m.type === 'error' && !joined) { clearTimeout(timeout); reject(new Error(m.message)); this.ws.close(); }
          this.emit('message', m);
        } catch { this.ws.close(); }
      });
    });
  }
  send(message) {
    if (this.ws?.readyState !== WebSocket.OPEN) throw new Error('Немає з’єднання з лоббі.');
    this.ws.send(JSON.stringify(message));
  }
  updateProfile(name, skin) {
    return this.request('profile', { name, skin });
  }
  updateSettings(settings, password) {
    return this.request('settings', { settings, password });
  }
  request(type, payload) {
    const requestId = randomUUID();
    return new Promise((resolve, reject) => {
      const cleanup = () => { clearTimeout(timer); this.off('message', listener); this.off('closed', closed); };
      const closed = () => { cleanup(); reject(new Error('Лобі закрито.')); };
      const listener = message => {
        if (message.requestId !== requestId) return;
        cleanup(); if (message.type === 'error') reject(new Error(message.message)); else resolve(message.profile);
      };
      const timer = setTimeout(() => { cleanup(); reject(new Error('Лобі не підтвердило зміну налаштувань.')); }, 5000);
      this.on('message', listener); this.once('closed', closed);
      try { this.send({ type, requestId, ...payload }); } catch (error) { cleanup(); reject(error); }
    });
  }
  close() { this.ws?.close(); }
}

async function discover(timeout = 1300, explicitTargets = []) {
  if (!Array.isArray(explicitTargets) || explicitTargets.length > 16 || explicitTargets.some(t => !isIPv4(t))) throw new Error('Введіть IPv4-адресу хоста або повне запрошення.');
  const socket = dgram.createSocket('udp4'), rooms = new Map();
  return new Promise(resolve => {
    const finish = () => { try { socket.close(); } catch {} resolve([...rooms.values()]); };
    const timer = setTimeout(finish, timeout);
    socket.on('error', () => { clearTimeout(timer); finish(); });
    socket.on('message', (buffer, remote) => {
      if (buffer.length > 2048) return;
      try {
        const r = JSON.parse(buffer.toString());
        if (r.protocol !== 1 || !/^[A-F0-9]{6}$/.test(r.code)) return;
        integer(r.port, 1024, 65535, 'Порт');
        r.name = cleanString(r.name, 64);
        r.address = `sacoop://${remote.address}:${r.port}/${r.code}`;
        rooms.set(r.address, r);
      } catch {}
    });
    socket.bind(0, '0.0.0.0', () => {
      socket.setBroadcast(true);
      const targets = new Set(explicitTargets.length ? explicitTargets : ['255.255.255.255', '127.0.0.1']);
      for (const n of lanAddresses()) {
        const ip = n.address.split('.').map(Number), mask = n.netmask.split('.').map(Number);
        targets.add(ip.map((v, i) => v | (~mask[i] & 255)).join('.'));
      }
      for (const target of targets) socket.send('SACOOP_DISCOVER_V1', DISCOVERY_PORT, target, () => {});
    });
  });
}
module.exports = { LanLobbyServer, LobbyClient, discover, parseLobbyAddress, serverSettings, DISCOVERY_PORT };
