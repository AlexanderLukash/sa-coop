const { test } = require('node:test');
const assert = require('node:assert/strict');
const { once } = require('node:events');
const { LanLobbyServer, LobbyClient, parseLobbyAddress, discover } = require('../src/core/lobby.cjs');
const settings = { name: 'Test crew', maxPlayers: 8, gamePort: 7788, loadSlot: -1, friendlyFire: false };
async function until(check, timeout = 3000) {
  const deadline = Date.now() + timeout;
  while (!check()) {
    if (Date.now() > deadline) throw new Error('Timed out');
    await new Promise(r => setTimeout(r, 10));
  }
}
test('lobby addresses reject arbitrary schemes and malformed room IDs', () => {
  assert.equal(parseLobbyAddress('192.168.1.2:7790/ABCDEF').host, '192.168.1.2');
  for (const address of ['file:///ABCDEF', 'https://host/ABCDEF', 'sacoop://a:b@host/ABCDEF', 'sacoop://host/ZZZZZZ'])
    assert.throws(() => parseLobbyAddress(address));
});
test('LAN discovery finds real UDP responder; mismatched mods are rejected', async () => {
  const server = new LanLobbyServer(settings, '', 'expected-build');
  const host = new LobbyClient(), guest = new LobbyClient();
  try {
    const info = await server.start(0);
    const address = `sacoop://127.0.0.1:${info.port}/${info.code}`;
    await host.connect(address, 'Host', '', info.hostToken, 'expected-build');
    await assert.rejects(guest.connect(address, 'Guest', '', undefined, 'other-build'), /Версія мода/);
    const found = await discover(300, ['127.0.0.1']);
    assert.ok(found.some(r => r.code === info.code && r.name === settings.name && r.count === 1));
  } finally { host.close(); guest.close(); await server.close(); }
});
test('running lobby admits late players, syncs skins/names and launches only the ready newcomer', async () => {
  const server = new LanLobbyServer(settings);
  const host = new LobbyClient(), first = new LobbyClient(), late = new LobbyClient();
  let lateLaunches = 0, otherLaunches = 0;
  late.on('message', m => { if (m.type === 'launch-guest') lateLaunches++; });
  first.on('message', m => { if (m.type === 'launch-guest') otherLaunches++; });
  try {
    const info = await server.start(0, '127.0.0.1', false);
    const address = `sacoop://127.0.0.1:${info.port}/${info.code}`;
    await host.connect(address, 'Host', '', info.hostToken);
    await first.connect(address, 'First');
    first.send({ type: 'ready', ready: true });
    await until(() => host.room?.members.every(m => m.ready));
    host.send({ type: 'start' }); await until(() => host.room.status === 'starting');
    host.send({ type: 'game-started' }); await until(() => first.room.status === 'running');
    first.send({ type: 'game-entered' }); await until(() => first.room.members.find(m => m.id === first.id).playing);
    await late.connect(address, 'Late', '', undefined, '', 270);
    await until(() => late.room?.status === 'running');
    assert.equal(lateLaunches, 0, 'joining the lobby must not auto-launch GTA');
    await late.updateProfile('NewName', 271);
    await until(() => host.room.members.some(m => m.name === 'NewName' && m.skin === 271));
    await assert.rejects(late.updateProfile('First', 269), /зайнятий/);
    await assert.rejects(late.updateProfile('NewName', 74), /скін/);
    late.send({ type: 'ready', ready: true }); await until(() => lateLaunches === 1);
    assert.equal(otherLaunches, 0, 'existing players must not launch again');
    late.send({ type: 'game-entered' }); await until(() => host.room.members.find(m => m.id === late.id).playing);
    await assert.rejects(late.updateProfile('ChangedInGame', 0), /до входу/);
    late.send({ type: 'game-left' }); await until(() => !host.room.members.find(m => m.id === late.id).playing);
    await late.updateProfile('BackAgain', 105);
    late.send({ type: 'ready', ready: true }); await until(() => lateLaunches === 2);
    host.send({ type: 'game-ended' }); await until(() => late.room.status === 'waiting');
    assert.ok(late.room.members.every(m => !m.playing && !m.launching));
  } finally { host.close(); first.close(); late.close(); await server.close(); }
});
test('server settings are acknowledged; password changes reach existing guests; invalid updates are atomic', async () => {
  const server = new LanLobbyServer(settings, 'old');
  const host = new LobbyClient(), guest = new LobbyClient(), outsider = new LobbyClient();
  try {
    const info = await server.start(0, '127.0.0.1', false);
    const address = `sacoop://127.0.0.1:${info.port}/${info.code}`;
    await host.connect(address, 'Host', 'old', info.hostToken);
    await guest.connect(address, 'Guest', 'old');
    await assert.rejects(guest.updateSettings(settings, 'hacked'), /лише хост/);
    const next = { ...settings, name: 'Changed', maxPlayers: 4, friendlyFire: true, gamePort: 7998 };
    await host.updateSettings(next, '"new\\"');
    assert.equal(host.room.settings.name, 'Changed');
    await until(() => guest.password === '"new\\"');
    await assert.rejects(outsider.connect(address, 'Outsider', 'old'), /пароль/);
    await assert.rejects(host.updateSettings({ ...next, name: 'MustNotChange' }, 'x'.repeat(65)), /текстове/);
    assert.equal(server.settings.name, 'Changed'); assert.equal(server.password, '"new\\"');
    await assert.rejects(host.updateSettings({ ...next, maxPlayers: 1 }, 'new'), /більше гравців/);
    assert.equal(server.settings.maxPlayers, 4);
  } finally { host.close(); guest.close(); outsider.close(); await server.close(); }
});
test('eight LAN players: password, uniqueness, readiness, host control, start, departure', async () => {
  const server = new LanLobbyServer(settings, 'secret');
  const clients = [];
  try {
    const info = await server.start(0, '127.0.0.1', false);
    const address = `sacoop://127.0.0.1:${info.port}/${info.code}`;
    async function client(name, password = 'secret', token) {
      const c = new LobbyClient(); clients.push(c); await c.connect(address, name, password, token); return c;
    }
    const host = await client('Host', 'secret', info.hostToken);
    await assert.rejects(client('Wrong', 'bad'), /пароль/);
    for (let i = 1; i < 8; i++) await client('Guest' + i);
    await until(() => host.room?.members.length === 8);
    await assert.rejects(client('Ninth'), /заповнене/);
    host.send({ type: 'start' });
    let [error] = await once(host, 'message');
    assert.equal(error.type, 'error'); assert.match(error.message, /готовності/);
    const guests = clients.filter(c => c.id && c !== host);
    guests[0].send({ type: 'start' });
    [error] = await once(guests[0], 'message'); assert.equal(error.type, 'error');
    for (const c of guests) c.send({ type: 'ready', ready: true });
    await until(() => host.room.members.every(m => m.ready));
    host.send({ type: 'settings', settings: { ...settings, name: 'Updated' } });
    await until(() => host.room.settings.name === 'Updated');
    assert.equal(host.room.members.filter(m => m.ready).length, 1, 'settings invalidate readiness');
    for (const c of guests) c.send({ type: 'ready', ready: true });
    await until(() => host.room.members.every(m => m.ready));
    host.send({ type: 'start' });
    await until(() => host.room.status === 'starting');
    host.send({ type: 'game-started' });
    await until(() => host.room.status === 'running');
    assert.equal(server.snapshot().settings.gamePort, 7788);
    assert.equal(JSON.stringify(server.snapshot()).includes('secret'), false);
    host.send({ type: 'game-ended' });
    await until(() => host.room.status === 'waiting');
    guests[0].close(); await until(() => host.room.members.length === 7);
    await assert.rejects(client('Guest2'), /зайнятий/);
    const rejoined = await client('NewGuest'); await until(() => rejoined.room?.members.length === 8);
    host.close(); await once(rejoined, 'closed');
  } finally { for (const c of clients) c.close(); await server.close(); }
});
