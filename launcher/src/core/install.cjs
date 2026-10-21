const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const { Readable, Transform } = require('node:stream');
const { pipeline } = require('node:stream/promises');
const { createWriteStream } = require('node:fs');
const yauzl = require('yauzl');
const { inspectExecutable } = require('./game-version.cjs');
const { atomicWrite, readText } = require('./config.cjs');

async function exists(file) { try { await fs.access(file); return true; } catch { return false; } }
function installationError(error, directory) {
  if (['EPERM', 'EACCES', 'EROFS'].includes(error.code)) {
    const result = new Error(`Немає доступу для запису в папку GTA: ${directory}. Перенесіть гру в папку користувача (наприклад, Games у вашій домашній папці) та виберіть її в лаунчері. У Windows також можна закрити лаунчер і запустити його через «Запуск від імені адміністратора». Перевірте, що GTA закрита й антивірус не блокує файли.`);
    result.code = error.code;
    result.cause = error;
    return result;
  }
  return error;
}
async function installationWorkspace(root) {
  try { return await fs.mkdtemp(path.join(root, '.sacoop-install-')); }
  catch (error) { throw installationError(error, root); }
}
async function checkInstallAccess(directory) {
  const root = await fs.realpath(directory);
  const work = await installationWorkspace(root);
  try { await fs.rm(work, { recursive: true, force: true }); }
  catch (error) { throw installationError(error, root); }
}
async function installedBuild(directory) {
  return crypto.createHash('sha256').update(await fs.readFile(path.join(directory, 'coop.asi'))).digest('hex');
}
async function inspectGame(directory) {
  if (!directory) return { found: false, installed: false, supported: false };
  const exe = path.join(directory, 'gta_sa.exe');
  let stat;
  try { stat = await fs.stat(exe); } catch { return { found: false, installed: false, supported: false }; }
  let installedVersion = '';
  try { installedVersion = JSON.parse(await readText(path.join(directory, 'coop/launcher-install.json'))).version; } catch {}
  const compatibility = stat.isFile() && stat.size <= 128 * 1024 * 1024 ? inspectExecutable(await fs.readFile(exe)) : { supported: false, reason: 'Некоректний виконуваний файл.' };
  return { found: true, ...compatibility,
    installed: await exists(path.join(directory, 'coop.asi')) && await exists(path.join(directory, 'coop/coop-server.exe')),
    installedVersion, exeSize: stat.size };
}

async function latestRelease(repo, fetcher = fetch) {
  const response = await fetcher(`https://api.github.com/repos/${repo}/releases/latest`, {
    headers: { Accept: 'application/vnd.github+json', 'User-Agent': 'SA-COOP-Launcher' }, signal: AbortSignal.timeout(15000),
  });
  if (!response.ok) {
    if (response.status === 404) throw new Error('У репозиторії ще немає релізу. Оберіть локальний SA-COOP.zip або опублікуйте GitHub Release.');
    throw new Error(`GitHub: HTTP ${response.status}. Спробуйте пізніше.`);
  }
  const release = await response.json();
  const asset = release.assets?.find(a => a.name === 'SA-COOP.zip');
  if (!asset || !asset.browser_download_url?.startsWith('https://github.com/'))
    throw new Error('Реліз не містить SA-COOP.zip.');
  return { version: release.tag_name, name: release.name || release.tag_name, url: asset.browser_download_url,
    size: asset.size, digest: asset.digest || '', publishedAt: release.published_at };
}

async function download(release, file, progress, signal) {
  const response = await fetch(release.url, { signal });
  if (!response.ok || !response.body) throw new Error(`Не вдалося завантажити архів: HTTP ${response.status}`);
  const total = Number(response.headers.get('content-length')) || release.size;
  const hash = crypto.createHash('sha256');
  let received = 0;
  const counter = new Transform({ transform(chunk, _encoding, callback) {
    received += chunk.length;
    if (received > 512 * 1024 * 1024) return callback(new Error('Архів перевищує 512 МБ.'));
    hash.update(chunk); progress({ stage: 'download', received, total }); callback(null, chunk);
  } });
  await pipeline(Readable.fromWeb(response.body), counter, createWriteStream(file), { signal });
  const digest = 'sha256:' + hash.digest('hex');
  if (release.digest && release.digest !== digest) throw new Error('Контрольна сума архіву не збігається.');
  return digest;
}

function safeEntry(name) {
  const normalized = name.replaceAll('\\', '/');
  if (!normalized || normalized.startsWith('/') || normalized.includes(':') ||
      normalized.split('/').some(part => part === '..' || /[. ]$/.test(part)) || /[\0-\x1f]/.test(normalized))
    throw new Error('Архів містить небезпечний шлях.');
  // Releases may wrap their files in a single SA-COOP directory.
  return normalized.replace(/^SA-COOP\//i, '');
}
async function extractZip(file, target, signal) {
  const zip = await new Promise((resolve, reject) => yauzl.open(file, { lazyEntries: true }, (e, z) => e ? reject(e) : resolve(z)));
  let size = 0, count = 0;
  const seen = new Set();
  await new Promise((resolve, reject) => {
    let stopped = false;
    const fail = error => { if (!stopped) { stopped = true; zip.close(); reject(error); } };
    zip.on('error', fail);
    zip.on('end', resolve);
    zip.on('entry', entry => { (async () => {
      signal?.throwIfAborted();
      const mode = (entry.externalFileAttributes >>> 16) & 0xf000;
      if (mode === 0xa000) throw new Error('Символічні посилання в архіві заборонені.');
      const relative = safeEntry(entry.fileName);
      if (!relative) { zip.readEntry(); return; }
      if (seen.has(relative.toLowerCase())) throw new Error('Дублікати файлів в архіві.');
      seen.add(relative.toLowerCase());
      size += entry.uncompressedSize;
      if (++count > 10000 || size > 1024 * 1024 * 1024) throw new Error('Архів перевищує обмеження розпакування.');
      const destination = path.join(target, relative);
      if (relative.endsWith('/')) await fs.mkdir(destination, { recursive: true });
      else {
        await fs.mkdir(path.dirname(destination), { recursive: true });
        const stream = await new Promise((res, rej) => zip.openReadStream(entry, (e, s) => e ? rej(e) : res(s)));
        await pipeline(stream, createWriteStream(destination), { signal });
      }
      zip.readEntry();
    })().catch(fail); });
    zip.readEntry();
  });
}

async function regularPath(root, relative) {
  // Reject installed symlinks/junctions as well as malicious archive entries.
  let current = root;
  for (const part of relative.split(/[\\/]/)) {
    current = path.join(current, part);
    try { if ((await fs.lstat(current)).isSymbolicLink()) throw new Error('Шлях інсталяції містить символічне посилання.'); }
    catch (e) { if (e.code !== 'ENOENT') throw e; }
  }
  return current;
}
async function installArchive(archive, gameDir, version, progress = () => {}, signal) {
  const game = await inspectGame(gameDir);
  if (!game.supported) throw new Error(game.reason || 'Потрібен виконуваний файл, сумісний із GTA San Andreas 1.0 US.');
  const root = await fs.realpath(gameDir);
  // Staging/backups stay on the game's volume; failures roll back overwritten files.
  const work = await installationWorkspace(root);
  const stage = path.join(work, 'stage'), backups = path.join(work, 'backup');
  const changes = [];
  let keepBackup = false;
  try {
    await fs.mkdir(stage); await fs.mkdir(backups);
    progress({ stage: 'extract' });
    await extractZip(archive, stage, signal);
    for (const required of ['coop.asi', 'coop/coop-server.exe', 'coop/loader/vorbisFile.dll'])
      if (!await exists(path.join(stage, required))) throw new Error(`В архіві немає ${required}.`);
    const entries = await fs.readdir(stage, { recursive: true, withFileTypes: true });
    const files = entries.filter(e => e.isFile()).map(e => path.relative(stage, path.join(e.parentPath, e.name)));
    for (const relative of files) {
      const unix = relative.replaceAll('\\', '/');
      if (!(unix === 'coop.asi' || unix.startsWith('coop/') || unix === 'install.bat' || /\.(txt|md)$/i.test(unix)))
        throw new Error(`Неочікуваний файл у моді: ${unix}`);
    }
    async function replace(relative, source) {
      const destination = await regularPath(root, relative);
      const backup = path.join(backups, String(changes.length));
      const existed = await exists(destination);
      if (existed) await fs.copyFile(destination, backup);
      changes.push({ destination, backup, existed });
      await fs.mkdir(path.dirname(destination), { recursive: true });
      await fs.copyFile(source, destination);
    }
    progress({ stage: 'install' });
    for (const relative of files) {
      signal?.throwIfAborted();
      if (/(^|[\\/])(server\.ini|coop\.ini|launcher\.ini|launcher-install\.json)$/i.test(relative) && await exists(path.join(root, relative))) continue;
      await replace(relative, path.join(stage, relative));
    }
    // A DLL named dinput8/dsound is not proof that it loads ASI plugins.
    // Install our known loader and retain the original forwarding target.
    const loader = path.join(stage, 'coop/loader/vorbisFile.dll');
    const currentLoader = path.join(root, 'vorbisFile.dll');
    const loaderInstalled = await exists(currentLoader) && (await fs.readFile(currentLoader)).equals(await fs.readFile(loader));
    if (!loaderInstalled) {
      if (!await exists(path.join(root, 'vorbisFile.dll'))) throw new Error('Немає оригінального vorbisFile.dll. Відновіть файли GTA.');
      if (!await exists(path.join(root, 'vorbisHooked.dll'))) await replace('vorbisHooked.dll', currentLoader);
      await replace('vorbisFile.dll', loader);
    }
    const metadata = path.join(work, 'metadata.json');
    await atomicWrite(metadata, JSON.stringify({ version, installedAt: new Date().toISOString() }));
    await replace('coop/launcher-install.json', metadata);
    progress({ stage: 'done' });
    return await inspectGame(root);
  } catch (error) {
    error = installationError(error, root);
    const failures = [];
    for (const change of changes.reverse()) {
      try {
        if (change.existed) await fs.copyFile(change.backup, change.destination);
        else await fs.rm(change.destination, { force: true });
      } catch { failures.push(change.destination); }
    }
    if (failures.length) {
      error.message += ` Резервні копії для ручного відновлення: ${work}`;
      keepBackup = true;
    }
    throw error;
  } finally {
    if (!keepBackup) await fs.rm(work, { recursive: true, force: true });
  }
}
module.exports = { exists, inspectGame, installedBuild, latestRelease, download, safeEntry, extractZip, regularPath, installArchive, checkInstallAccess };
