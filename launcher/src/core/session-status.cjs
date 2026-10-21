const fs = require('node:fs/promises');
async function readStatus(file, session) {
  try {
    const status = JSON.parse(await fs.readFile(file, 'utf8'));
    return status.session === session && typeof status.state === 'string' ? status : null;
  } catch (error) {
    if (error.code === 'ENOENT' || error instanceof SyntaxError) return null;
    throw error;
  }
}
async function waitForStatus(file, session, { isAlive, timeout = 90000, interval = 150 } = {}) {
  const end = Date.now() + timeout;
  while (Date.now() < end) {
    if (!isAlive()) throw new Error('GTA завершилась до підключення. Перевірте coop/launcher.log та launcher-crash.log.');
    const status = await readStatus(file, session);
    if (status && ['online', 'playing'].includes(status.state)) return status;
    if (status && ['failed', 'offline'].includes(status.state)) throw new Error(status.message || 'Мод не зміг підключитися до сервера.');
    await new Promise(resolve => setTimeout(resolve, interval));
  }
  throw new Error('Мод не підтвердив підключення. Встановіть актуальний ZIP і перевірте coop/launcher.log та ASI loader.');
}
module.exports = { readStatus, waitForStatus };
