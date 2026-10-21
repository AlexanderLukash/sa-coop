const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { defaults } = require('../src/core/config.cjs');

test('story selection saves immediately and start waits for the saved selection', async () => {
  const elements = new Map();
  const element = selector => {
    if (!elements.has(selector)) elements.set(selector, {
      value: '', handlers: {}, classList: { toggle() {} }, dataset: {},
      addEventListener(event, callback) { this.handlers[event] = callback; },
    });
    return elements.get(selector);
  };
  const snapshot = { settings: { ...defaults }, game: {}, skins: [], addresses: [], room: null };
  let saved, finishSave, failSave, starts = 0;
  const api = {
    state: () => new Promise(() => {}), onEvent() {},
    save: settings => { saved = settings; return new Promise((resolve, reject) => { finishSave = resolve; failSave = reject; }); },
    start: async () => { starts++; assert.equal(saved.loadSlot, 3); },
  };
  const context = vm.createContext({
    window: { launcher: api },
    document: { querySelector: element, querySelectorAll: () => [] },
    setInterval() {}, clearTimeout() {}, setTimeout() {}, Intl, snapshot,
  });
  vm.runInContext(fs.readFileSync(path.join(__dirname, '../src/renderer/app.js'), 'utf8'), context);
  vm.runInContext('state = snapshot', context);
  const select = element('#settings-form [name="loadSlot"]');
  select.value = '3';
  select.handlers.change({ currentTarget: select });
  assert.equal(saved.loadSlot, 3);
  assert.equal(select.disabled, true);
  const start = element('#start-session');
  const pending = start.handlers.click({ currentTarget: start });
  await Promise.resolve();
  assert.equal(starts, 0);
  finishSave({ ...snapshot, settings: saved });
  await pending;
  assert.equal(starts, 1);
  assert.equal(select.disabled, false);
  select.value = '8';
  select.handlers.change({ currentTarget: select });
  const failedStart = start.handlers.click({ currentTarget: start });
  failSave(new Error('Server rejected settings'));
  await failedStart;
  assert.equal(starts, 1, 'failed settings must not launch the old story selection');
  assert.equal(Number(select.value), 3, 'failed selection restores the saved value');
});
