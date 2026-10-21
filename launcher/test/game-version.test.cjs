const { test } = require('node:test');
const assert = require('node:assert/strict');
const { inspectExecutable } = require('../src/core/game-version.cjs');
const { compatibleFixture } = require('../tools/game-fixture.cjs');
test('compatible signatures work independently of file length', () => {
  const bytes = compatibleFixture();
  assert.equal(inspectExecutable(bytes).supported, true);
  assert.equal(inspectExecutable(Buffer.concat([bytes, Buffer.alloc(12345)])).supported, true);
});
test('wrong signatures, invalid PE and old size-only fixtures are rejected', () => {
  const bytes = compatibleFixture(); bytes.writeUInt32LE(0, 512 + 0x57c);
  assert.equal(inspectExecutable(bytes).supported, false);
  for (const b of [Buffer.alloc(14383616), Buffer.from('MZ'), Buffer.alloc(0)]) assert.equal(inspectExecutable(b).supported, false);
});
