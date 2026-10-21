const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { waitForStatus, readStatus } = require('../src/core/session-status.cjs');
test('game status ignores stale launches and reports native connection failures', async () => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'sacoop-status-'));
  const file = path.join(dir, 'status.json');
  try {
    await fs.writeFile(file, JSON.stringify({ session: 'previous', state: 'online' }));
    assert.equal(await readStatus(file, 'new'), null);
    await assert.rejects(waitForStatus(file, 'new', { isAlive: () => true, timeout: 25, interval: 5 }), /не підтвердив/);
    await fs.writeFile(file, JSON.stringify({ session: 'new', state: 'failed', message: 'wrong password' }));
    await assert.rejects(waitForStatus(file, 'new', { isAlive: () => true }), /wrong password/);
    await fs.writeFile(file, '{incomplete'); assert.equal(await readStatus(file, 'new'), null);
    await fs.writeFile(file, JSON.stringify({ session: 'new', state: 'online' }));
    assert.equal((await waitForStatus(file, 'new', { isAlive: () => true })).state, 'online');
    await assert.rejects(waitForStatus(file, 'new', { isAlive: () => false }), /GTA завершилась/);
  } finally { await fs.rm(dir, { recursive: true, force: true }); }
});
