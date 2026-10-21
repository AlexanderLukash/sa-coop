const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const { spawn } = require('node:child_process');
const { download } = require('./install.cjs');

const UKRAINIAN_TEXT = {
  name: 'GTA-SA-Ukrainian-Text.exe',
  url: 'https://drive.usercontent.google.com/download?id=1Z9aN14O5ID1AS0XWDS4DRPSSpAw38oZh&export=download&confirm=t',
  digest: 'sha256:65f76ec8f89cda6b828555493cd0f8e803a391b99340a7398a3a6b284c744a81',
};

function runInstaller(command, args, options, signal) {
  signal?.throwIfAborted();
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { ...options, windowsHide: true, stdio: 'ignore' });
    let settled = false;
    const finish = (error, code) => {
      if (settled) return;
      settled = true; clearTimeout(timer); signal?.removeEventListener('abort', abort);
      if (error) reject(error);
      else if (signal?.aborted) reject(Object.assign(new Error('Операцію скасовано.'), { name: 'AbortError' }));
      else if (code !== 0) reject(new Error(`Інсталятор українського тексту завершився з кодом ${code}.`));
      else resolve();
    };
    const abort = () => { child.kill(); };
    const timer = setTimeout(() => { child.kill(); finish(new Error('Інсталятор українського тексту перевищив час очікування.')); }, 15 * 60 * 1000);
    timer.unref?.();
    signal?.addEventListener('abort', abort, { once: true });
    child.once('error', error => finish(error));
    child.once('close', code => finish(null, code));
  });
}

async function installUkrainianText(gameDir, settings, progress = () => {}, signal) {
  const root = await fs.realpath(gameDir);
  const marker = path.join(root, '.sacoop-ukrainian-text.json');
  try { await fs.access(marker); return { installed: false, skipped: [] }; } catch {}
  const work = await fs.mkdtemp(path.join(os.tmpdir(), 'sacoop-ukrainian-text-'));
  try {
    const installer = path.join(work, UKRAINIAN_TEXT.name);
    progress({ stage: 'translation-download', mod: 'Український текст', percent: 0 });
    await download(UKRAINIAN_TEXT, installer,
      event => progress({ ...event, stage: 'translation-download', mod: 'Український текст' }), signal, 16 * 1024 * 1024);
    signal?.throwIfAborted();
    let command = installer;
    let destination = root;
    if (process.platform === 'linux') {
      command = settings.wineCommand || 'wine';
      destination = `Z:${root.replaceAll('/', '\\')}`;
    } else if (process.platform !== 'win32') {
      throw new Error('Автоматичне встановлення українського тексту підтримується у Windows та Linux з Wine.');
    }
    progress({ stage: 'translation-install', mod: 'Український текст', percent: 0 });
    const env = { ...process.env };
    if (process.platform === 'linux' && settings.winePrefix) env.WINEPREFIX = path.resolve(settings.winePrefix);
    await runInstaller(command, [
      ...(process.platform === 'linux' ? [installer] : []),
      '/s', '/nr', `/p=${destination}`, '/l=Ukrainian',
    ], { cwd: root, env }, signal);
    await fs.writeFile(marker, JSON.stringify({ id: 'ukrainian-text', digest: UKRAINIAN_TEXT.digest, installedAt: new Date().toISOString() }));
    progress({ stage: 'translation-done', mod: 'Український текст', percent: 100 });
    return { installed: true, skipped: [] };
  } finally { await fs.rm(work, { recursive: true, force: true }); }
}

async function installUkrainian(gameDir, settings, progress, signal) {
  const skipped = [];
  try {
    const result = await installUkrainianText(gameDir, settings, progress, signal);
    return { ...result, skipped };
  } catch (error) {
    if (signal?.aborted || error.name === 'AbortError') throw error;
    skipped.push(`Український текст: ${error.message}`);
    progress({ stage: 'translation-skip', mod: 'Український текст', reason: error.message });
    return { installed: false, skipped };
  }
}

module.exports = { installUkrainian, installUkrainianText, UKRAINIAN_TEXT };
