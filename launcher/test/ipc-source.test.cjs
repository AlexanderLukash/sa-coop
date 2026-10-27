const { test } = require('node:test');
const assert = require('node:assert/strict');
const { trustedRenderer } = require('../src/core/ipc-source.cjs');
const expected = 'file:///C:/Users/Player/SA%20COOP/resources/app.asar/src/renderer/index.html';
function fixture(url = expected) {
  const mainFrame = { processId: 11, routingId: 3, url: expected };
  const webContents = { mainFrame, isDestroyed: () => false };
  const window = { webContents, isDestroyed: () => false };
  const event = { sender: webContents, senderFrame: { ...mainFrame, url } };
  return { event, window };
}
function allowed(f, platform = 'win32') { return trustedRenderer(f.event, f.window, expected, platform); }
test('accept the same main frame exposed through a different Electron JS wrapper', () => {
  const f = fixture();
  assert.notEqual(f.event.senderFrame, f.window.webContents.mainFrame);
  assert.equal(allowed(f), true);
});
test('Windows renderer URL drive/path case, escapes and anchors refer to the same file', () => {
  for (const url of [expected, expected.replace('/C:/', '/c:/'), expected.replace('/Users/Player/', '/users/player/'),
    expected.replace('index.html', '%69ndex.html'), expected + '#home']) assert.equal(allowed(fixture(url)), true, url);
  const url = 'file:///C:/Users/%D0%9B%D1%83%D0%BA%D0%B0%D1%88/SA%20COOP/index.html';
  const f = fixture(url.replace('/C:/', '/c:/'));
  assert.equal(trustedRenderer(f.event, f.window, url, 'win32'), true);
});
test('reject other windows, frames, files, remote URLs and malformed senders', () => {
  for (const url of ['https://example.com/index.html', 'file:///C:/Users/Player/other.html',
    expected + '?other', 'file://remote/share/index.html', 'not a URL', 'about:blank'])
    assert.equal(allowed(fixture(url)), false, url);
  for (const change of [f => { f.event.sender = {}; }, f => { f.event.senderFrame.routingId++; },
    f => { f.event.senderFrame.processId++; }, f => { f.event.senderFrame = null; },
    f => { f.event.senderFrame.detached = true; }, f => { f.window.isDestroyed = () => true; },
    f => { f.window.webContents.isDestroyed = () => true; }]) {
    const f = fixture(); change(f); assert.equal(allowed(f), false);
  }
});
test('POSIX paths stay case sensitive', () => {
  const url = 'file:///opt/SA%20COOP/index.html';
  const f = fixture(url);
  assert.equal(trustedRenderer(f.event, f.window, url, 'linux'), true);
  f.event.senderFrame.url = url.replace('SA%20COOP', 'sa%20coop');
  assert.equal(trustedRenderer(f.event, f.window, url, 'linux'), false);
});
