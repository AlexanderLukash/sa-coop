const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const { constants: fsConstants } = require('node:fs');
const { spawn } = require('node:child_process');
const { path7za } = require('7zip-bin');
const { ArchiveReader, libarchiveWasm } = require('libarchive-wasm');
const { download, exists, regularPath } = require('./install.cjs');

const GITHUB = 'https://api.github.com/repos';
async function githubAsset(repo, name) {
  const response = await fetch(`${GITHUB}/${repo}/releases/latest`, { headers: { Accept: 'application/vnd.github+json', 'User-Agent': 'SA-COOP-Launcher' }, signal: AbortSignal.timeout(20000) });
  if (!response.ok) throw new Error(`GitHub (${repo}): HTTP ${response.status}`);
  const release = await response.json();
  const asset = release.assets?.find(item => item.name === name);
  if (!asset || !asset.browser_download_url?.startsWith('https://github.com/')) throw new Error(`У релізі ${repo} немає ${name}.`);
  return { name, url: asset.browser_download_url, size: asset.size, digest: asset.digest || '', version: release.tag_name };
}
function executablePath() { return path7za.replace(/\.asar([\\/])/, '.asar.unpacked$1'); }
function run7z(args, signal, onProgress) {
  signal?.throwIfAborted();
  return new Promise((resolve, reject) => {
    const child = spawn(executablePath(), args, { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    let stdout = '', stderr = '', aborted = false, lastPercent = -1, outputTooLarge = false;
    const abort = () => { aborted = true; child.kill(); };
    signal?.addEventListener('abort', abort, { once: true });
    child.stdout.on('data', data => {
      const chunk = data.toString();
      if (stdout.length < 32 * 1024 * 1024) stdout += chunk;
      else if (!outputTooLarge) { outputTooLarge = true; child.kill(); }
      for (const m of chunk.matchAll(/(?:^|\s)(\d{1,3})%/g)) {
        const percent = Math.min(100, Number(m[1]));
        if (percent !== lastPercent) onProgress?.({ percent });
        lastPercent = percent;
      }
    });
    child.stderr.on('data', data => { if (stderr.length < 1024 * 1024) stderr += data.toString(); });
    child.on('error', error => { signal?.removeEventListener('abort', abort); reject(error); });
    child.on('close', code => {
      signal?.removeEventListener('abort', abort);
      if (aborted || signal?.aborted) return reject(Object.assign(new Error('Операцію скасовано.'), { name: 'AbortError' }));
      if (outputTooLarge) return reject(new Error('Архів містить надто багато записів.'));
      if (code !== 0) return reject(new Error(`7-Zip не зміг обробити мод${stderr.trim() ? `: ${stderr.trim().slice(-400)}` : '.'}`));
      resolve(stdout);
    });
  });
}
function runAudioInstaller(installer, gameDir, settings, signal) {
  signal?.throwIfAborted();
  const linux = process.platform === 'linux';
  if (process.platform !== 'win32' && !linux)
    return Promise.reject(new Error('Автоматичне встановлення озвучення підтримується у Windows та Linux з Wine.'));
  const env = { ...process.env };
  if (linux && settings.winePrefix) env.WINEPREFIX = path.resolve(settings.winePrefix);
  const command = linux ? (settings.wineCommand || 'wine') : installer;
  const target = linux ? `Z:${gameDir.replaceAll('/', '\\')}` : gameDir;
  const args = [
    ...(linux ? [installer] : []),
    '/s', '/nr', `/p=${target}`, '/l=Ukrainian',
  ];
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { cwd: gameDir, env, windowsHide: true, stdio: 'ignore' });
    let settled = false;
    const finish = (error, code) => {
      if (settled) return;
      settled = true; clearTimeout(timer); signal?.removeEventListener('abort', abort);
      if (error) reject(error);
      else if (signal?.aborted) reject(Object.assign(new Error('Операцію скасовано.'), { name: 'AbortError' }));
      else if (code !== 0) reject(new Error(`Інсталятор української озвучки завершився з кодом ${code}.`));
      else resolve();
    };
    const abort = () => child.kill();
    const timer = setTimeout(() => { child.kill(); finish(new Error('Інсталятор української озвучки перевищив час очікування.')); }, 30 * 60 * 1000);
    timer.unref?.();
    signal?.addEventListener('abort', abort, { once: true });
    child.once('error', error => finish(error));
    child.once('close', code => finish(null, code));
  });
}
function archiveEntries(listing) {
  const entries = [];
  const seen = new Set(); let unpacked = 0;
  for (const record of listing.split(/\r?\n\s*\r?\n/)) {
    const fields = Object.fromEntries(record.split(/\r?\n/).map(line => {
      const at = line.indexOf(' = '); return at < 0 ? [] : [line.slice(0, at), line.slice(at + 3)];
    }).filter(pair => pair.length));
    if (!fields.Path || fields.Folder === undefined) continue;
    const relative = fields.Path.replaceAll('\\', '/');
    if (!relative || relative.startsWith('/') || relative.includes(':') || path.win32.isAbsolute(fields.Path) ||
        relative.split('/').some(part => !part || part === '.' || part === '..' || /[. ]$/.test(part)) || /[\0-\x1f]/.test(relative))
      throw new Error('Архів мода містить небезпечний шлях.');
    if (fields['Symbolic Link'] || fields['Hard Link']) throw new Error('Посилання в архіві мода не підтримуються.');
    if (fields.Folder !== '+') {
      const key = relative.toLocaleLowerCase('en-US');
      if (seen.has(key)) throw new Error('Архів мода містить однакові шляхи.');
      seen.add(key); entries.push(relative);
      const size = Number(fields.Size || 0);
      if (!Number.isSafeInteger(size) || size < 0) throw new Error('Некоректний розмір файлу в архіві мода.');
      unpacked += size;
      if (unpacked > 16 * 1024 * 1024 * 1024) throw new Error('Розпакований мод перевищує 16 ГБ.');
    }
    if (entries.length > 200000) throw new Error('Архів мода містить надто багато файлів.');
  }
  return entries;
}

async function sourceList(sourceConfig) {
  const [silent, ola, modloader] = await Promise.all([
    githubAsset('CookiePLMonster/SilentPatch', 'SilentPatchSA.zip'),
    githubAsset('GTAmodding/III.VC.SA.LimitAdjuster', 'III.VC.SA.LimitAdjuster.zip'),
    githubAsset('thelink2012/modloader', 'modloader.zip'),
  ]);
  return [
    { id: 'modloader', title: 'ModLoader', archive: modloader, rootPlugin: true },
    { id: 'silentpatch', title: 'SilentPatch', archive: silent },
    { id: 'widescreen-fix', title: 'Widescreen Fix', archive: { name: 'GTASA.WidescreenFix.zip', url: 'https://github.com/ThirteenAG/WidescreenFixesPack/releases/download/gtasa/GTASA.WidescreenFix.zip', digest: 'sha256:563078bfae5ad93be548d9378d4a97809762fb3a8dfdf0ef65e6023beadeb3ae' } },
    { id: 'skygfx', title: 'SkyGfx', archive: { name: 'SkyGfx_SA_4.2b.zip', url: 'https://github.com/aap/skygfx/releases/download/v4.2b/SkyGfx_SA_4.2b.zip' } },
    { id: 'open-limit-adjuster', title: 'Open Limit Adjuster', archive: ola },
    { id: 'ukrainian-audio', title: 'Українська озвучка', installer: true, archive: { name: 'GTA-SA-Ukrainian-Audio.zip', url: 'https://drive.usercontent.google.com/download?id=1e0pO_1e_kXLgRS5Zq26K8NVC3wQNTWzx&export=download&confirm=t', digest: 'sha256:a9bf782c1af0967c8851cf7236401919593e80196f2b6e40487e5b9a6e9c18a3' }, maxDownloadBytes: 3 * 1024 * 1024 * 1024 },
  ];
}

async function hasUkrainianAudio(gameDir) {
  for (const relative of [
    '.sacoop-ukrainian-audio.json',
    'modloader/SA-COOP ukrainian-audio/.installed.json',
  ]) {
    if (await exists(path.join(gameDir, relative))) return true;
  }
  return false;
}

let rarModulePromise;
async function rarEntries(archive) {
  const mod = await (rarModulePromise ||= libarchiveWasm());
  const data = await fs.readFile(archive);
  const reader = new ArchiveReader(mod, new Int8Array(data.buffer, data.byteOffset, data.byteLength));
  const entries = [];
  try {
    if (reader.hasEncryptedData()) throw new Error('Зашифровані архіви модів не підтримуються.');
    for (let entry; (entry = reader.nextEntry());) {
      const type = entry.getFiletype();
      const relative = entry.getPathname().replaceAll('\\', '/').replace(type === 'Directory' ? /\/+$/ : /$^/, '');
      if (!['File', 'Directory'].includes(type) || entry.getSymlinkTarget() || entry.getHardlinkTarget()) {
        entry.free(); throw new Error('Архів мода містить посилання або спеціальний файл.');
      }
      if (!relative || /[\0-\x1f]/.test(relative)) { entry.free(); throw new Error('Архів мода містить некоректний шлях.'); }
      if (entry.isEncrypted()) { entry.free(); throw new Error('Зашифровані архіви модів не підтримуються.'); }
      entries.push({ path: relative, type, size: entry.getSize() });
      entry.free();
      if (entries.length > 200000) throw new Error('Архів мода містить надто багато файлів.');
    }
  } finally { reader.free(); }
  if (!entries.length) throw new Error('Архів мода порожній.');
  const listing = entries.map(entry => `Path = ${entry.path}\nFolder = ${entry.type === 'Directory' ? '+' : '-'}\nSize = ${entry.size}`).join('\n\n');
  const files = archiveEntries(listing);
  if (!files.length) throw new Error('Архів мода не містить файлів.');
  return entries;
}

async function extractRarArchive(archive, stage, signal, onProgress) {
  const entries = await rarEntries(archive);
  const mod = await (rarModulePromise ||= libarchiveWasm());
  const data = await fs.readFile(archive);
  const reader = new ArchiveReader(mod, new Int8Array(data.buffer, data.byteOffset, data.byteLength));
  try {
    for (let i = 0; i < entries.length; i++) {
      signal?.throwIfAborted();
      const entry = reader.nextEntry();
      if (!entry) throw new Error('Архів змінився під час розпакування.');
      const expected = entries[i];
      const relative = entry.getPathname().replaceAll('\\', '/').replace(expected.type === 'Directory' ? /\/+$/ : /$^/, '');
      if (relative !== expected.path || entry.getFiletype() !== expected.type || entry.getSize() !== expected.size || entry.getSymlinkTarget() || entry.getHardlinkTarget()) {
        entry.free(); throw new Error('Некоректний запис в архіві мода.');
      }
      const destination = path.join(stage, ...relative.split('/'));
      if (expected.type === 'Directory') {
        entry.free(); await fs.mkdir(destination, { recursive: true });
      } else {
        const bytes = entry.readData();
        if (!bytes || bytes.byteLength !== expected.size) throw new Error('Не вдалося розпакувати файл мода.');
        await fs.mkdir(path.dirname(destination), { recursive: true });
        await fs.writeFile(destination, bytes, { flag: 'wx' });
        entry.free();
      }
      onProgress?.({ percent: Math.round((i + 1) / entries.length * 100) });
    }
  } finally { reader.free(); }
}

async function installComfortMods(gameDir, sourceConfig = {}, progress = () => {}, signal) {
  const root = await fs.realpath(gameDir);
  const game = path.join(root, 'modloader');
  const work = await fs.mkdtemp(path.join(os.tmpdir(), 'sacoop-comfort-'));
  const plan = [], skipped = [], installedSources = [];
  try {
    const sources = await sourceList(sourceConfig);
    const audio = sources.find(source => source.id === 'ukrainian-audio');
    if (audio && await hasUkrainianAudio(root)) {
      sources.splice(sources.indexOf(audio), 1);
      skipped.push('Українська озвучка: уже встановлена');
      progress({ stage: 'mod-skip', mod: audio.title, reason: 'Озвучка вже є у папці гри.' });
    }
    if (audio) {
      const oldAudioDir = path.join(game, 'SA-COOP ukrainian-audio');
      try {
        const marker = JSON.parse(await fs.readFile(path.join(oldAudioDir, '.installed.json'), 'utf8'));
        const expected = audio.archive.version || audio.archive.name;
        if (marker.id === 'ukrainian-audio' && marker.version !== expected)
          await fs.rm(oldAudioDir, { recursive: true, force: true });
      } catch (error) { if (error.code !== 'ENOENT' && !(error instanceof SyntaxError)) throw error; }
    }
    const missing = sources.filter(source => !source.archive);
    const pending = [];
    for (const source of sources)
      if (source.archive && !await exists(path.join(game, `SA-COOP ${source.id}`, '.installed.json'))) pending.push(source);
    if (missing.length) skipped.push(...missing.map(item => item.title));
    for (let i = 0; i < pending.length; i++) {
      signal?.throwIfAborted();
      const source = pending[i], archive = path.join(work, source.archive.name), stage = path.join(work, source.id);
      const modPlan = [];
      try {
      await fs.mkdir(stage);
      progress({ stage: 'mod-download', mod: source.title, index: i + 1, count: pending.length });
      await download(source.archive, archive, event => progress({ ...event, stage: 'mod-download', mod: source.title, index: i + 1, count: pending.length }), signal, source.maxDownloadBytes || 1024 * 1024 * 1024);
      if (/\.rar$/i.test(source.archive.name)) {
        const entries = await rarEntries(archive);
        if (!entries.length) throw new Error(`${source.title}: архів порожній.`);
        progress({ stage: 'mod-extract', mod: source.title, index: i + 1, count: pending.length, percent: 0 });
        await extractRarArchive(archive, stage, signal, event => progress({ stage: 'mod-extract', mod: source.title, index: i + 1, count: pending.length, ...event }));
      } else {
        const listing = await run7z(['l', '-slt', archive], signal);
        const entries = archiveEntries(listing);
        if (!entries.length) throw new Error(`${source.title}: архів порожній.`);
        progress({ stage: 'mod-extract', mod: source.title, index: i + 1, count: pending.length, percent: 0 });
        await run7z(['x', archive, `-o${stage}`, '-y', '-bsp1'], signal, event => progress({ stage: 'mod-extract', mod: source.title, index: i + 1, count: pending.length, ...event }));
      }
      const actual = await fs.readdir(stage, { recursive: true, withFileTypes: true });
      const files = actual.filter(entry => entry.isFile()).map(entry => path.relative(stage, path.join(entry.parentPath, entry.name)));
      if (!files.length) throw new Error(`${source.title}: немає файлів для встановлення.`);
      if (source.installer) {
        const installers = files.filter(file => path.extname(file).toLowerCase() === '.exe');
        if (installers.length !== 1) throw new Error(`${source.title}: очікувався один EXE-інсталятор.`);
        progress({ stage: 'mod-install', mod: source.title, index: i + 1, count: pending.length });
        await runAudioInstaller(path.join(stage, installers[0]), root, sourceConfig, signal);
        if (source.id === 'ukrainian-audio')
          await fs.writeFile(path.join(root, '.sacoop-ukrainian-audio.json'), JSON.stringify({ id: source.id, installedAt: new Date().toISOString() }));
        installedSources.push(source);
        continue;
      }
      if (source.id === 'skygfx') {
        const config = files.find(file => path.basename(file).toLowerCase() === 'skygfx2.ini');
        if (config) {
          const configPath = path.join(stage, config);
          let content = await fs.readFile(configPath, 'utf8');
          if (/^\s*buildingPipe\s*=/im.test(content)) content = content.replace(/^\s*buildingPipe\s*=.*$/im, 'buildingPipe=PS2');
          else content += `${content.endsWith('\n') ? '' : '\n'}buildingPipe=PS2\n`;
          await fs.writeFile(configPath, content);
        }
      }
      const pluginFiles = source.rootPlugin ? files.filter(file => file.toLowerCase().endsWith('.asi')) : [];
      if (source.rootPlugin && !pluginFiles.length) throw new Error(`${source.title}: пакет не містить ASI-плагіна.`);
      for (const file of files) {
        let relative;
        if (source.id === 'modloader') {
          if (pluginFiles.includes(file)) relative = path.basename(file);
          else if (file.replaceAll('\\', '/').toLowerCase().startsWith('modloader/')) relative = file.replaceAll('\\', '/');
          else continue;
        } else if (source.rootPlugin) {
          if (!pluginFiles.includes(file)) continue;
          relative = path.basename(file);
        } else relative = path.join('modloader', source.modFolder || `SA-COOP ${source.id}`, file);
        const destination = path.join(root, relative);
        const checked = await regularPath(root, relative);
        if (await exists(checked)) {
          if (!(await fs.readFile(checked)).equals(await fs.readFile(path.join(stage, file))))
            throw new Error(`Файл ${relative} уже існує та відрізняється. Його залишено без змін.`);
          continue;
        }
        modPlan.push({ destination: checked, source: path.join(stage, file), mod: source.title });
      }
      plan.push(...modPlan); installedSources.push(source);
      } catch (error) {
        if (signal?.aborted || error.name === 'AbortError') throw error;
        if (source.id === 'modloader') throw error;
        skipped.push(`${source.title}: ${error.message}`);
        progress({ stage: 'mod-skip', mod: source.title, reason: error.message, index: i + 1, count: pending.length });
      }
    }
    signal?.throwIfAborted();
    progress({ stage: 'mod-install', mod: 'Зберігаємо моди', index: 1, count: 1 });
    const written = [], markers = [];
    try {
      for (const item of plan) {
        signal?.throwIfAborted();
        await fs.mkdir(path.dirname(item.destination), { recursive: true });
        await fs.copyFile(item.source, item.destination, fsConstants.COPYFILE_EXCL);
        written.push(item.destination);
      }
      for (const source of installedSources) {
        const relative = path.join('modloader', `SA-COOP ${source.id}`, '.installed.json');
        const marker = await regularPath(root, relative);
        await fs.mkdir(path.dirname(marker), { recursive: true });
        markers.push(marker);
        await fs.writeFile(marker, JSON.stringify({ id: source.id, version: source.archive.version || source.archive.name, installedAt: new Date().toISOString() }));
      }
    } catch (error) {
      await Promise.all([...written, ...markers].map(file => fs.rm(file, { force: true })));
      throw error;
    }
    progress({ stage: 'mod-done', skipped });
    return { installed: installedSources.map(item => item.title), skipped };
  } finally { await fs.rm(work, { recursive: true, force: true }); }
}

module.exports = { installComfortMods, archiveEntries, hasUkrainianAudio, extractRarArchive };
