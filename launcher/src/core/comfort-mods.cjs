const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const { constants: fsConstants } = require('node:fs');
const { spawn } = require('node:child_process');
const { path7za } = require('7zip-bin');
const { createExtractorFromFile } = require('node-unrar-js');
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
    { id: 'ukrainian-audio', title: 'Українська озвучка', archive: { name: 'GTA-SA-Ukrainian-Audio.rar', url: 'https://drive.usercontent.google.com/download?id=180iMsjuemneiSiJD3Teouoqjjq-fj9AG&export=download&confirm=t', digest: 'sha256:c60102632f16384df04fa6e00b17331ee03c2e6123453cd26c070edaaf90d222' }, rar: true },
  ];
}

function validateRarEntries(headers) {
  const files = [], seen = new Set(); let total = 0;
  for (const header of headers) {
    const relative = header.name.replaceAll('\\', '/').replace(/\/$/, '');
    if (!relative || relative.startsWith('/') || relative.includes(':') || path.win32.isAbsolute(header.name) ||
        relative.split('/').some(part => !part || part === '.' || part === '..' || /[. ]$/.test(part)) || /[\0-\x1f]/.test(relative))
      throw new Error('Архів озвучення містить небезпечний шлях.');
    if (header.flags.encrypted) throw new Error('Архів озвучення зашифрований.');
    const key = relative.toLocaleLowerCase('en-US');
    if (seen.has(key)) throw new Error('Архів озвучення містить однакові шляхи.');
    seen.add(key);
    if (!header.flags.directory) {
      if (!Number.isSafeInteger(header.unpSize) || header.unpSize < 0) throw new Error('Некоректний розмір файлу в архіві озвучення.');
      total += header.unpSize;
      if (total > 2 * 1024 * 1024 * 1024) throw new Error('Озвучення перевищує обмеження 2 ГБ.');
      files.push(relative);
    }
    if (seen.size > 10000) throw new Error('Архів озвучення містить надто багато записів.');
  }
  if (!files.length) throw new Error('Архів озвучення порожній.');
  return files;
}
async function extractRar(archive, stage, signal, progress) {
  signal?.throwIfAborted();
  const extractor = await createExtractorFromFile({ filepath: archive, targetPath: stage,
    filenameTransform: name => name.replaceAll('\\', '/') });
  const headers = [...extractor.getFileList().fileHeaders];
  const files = validateRarEntries(headers);
  signal?.throwIfAborted();
  progress?.({ percent: 0 });
  const allowed = new Set(files);
  const result = extractor.extract({ files: header => !header.flags.directory &&
    allowed.has(header.name.replaceAll('\\', '/').replace(/\/$/, '')) });
  let extracted = 0;
  for (const item of result.files) {
    signal?.throwIfAborted();
    if (item.fileHeader.flags.encrypted || item.fileHeader.unpSize > 0 && !item.fileHeader.name)
      throw new Error('Не вдалося повністю розпакувати архів озвучення.');
    progress?.({ percent: Math.round(++extracted / files.length * 100) });
  }
  if (extracted !== files.length) throw new Error('Не вдалося повністю розпакувати архів озвучення.');
  signal?.throwIfAborted();
}

async function installComfortMods(gameDir, sourceConfig = {}, progress = () => {}, signal) {
  const root = await fs.realpath(gameDir);
  const game = path.join(root, 'modloader');
  const work = await fs.mkdtemp(path.join(os.tmpdir(), 'sacoop-comfort-'));
    const plan = [], skipped = [], installedSources = [];
  try {
    const sources = await sourceList(sourceConfig);
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
      await download(source.archive, archive, event => progress({ ...event, stage: 'mod-download', mod: source.title, index: i + 1, count: pending.length }), signal, 1024 * 1024 * 1024);
      if (source.rar) {
        await extractRar(archive, stage, signal, event => progress({ stage: 'mod-extract', mod: source.title, index: i + 1, count: pending.length, ...event }));
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
        } else relative = path.join('modloader', `SA-COOP ${source.id}`, file);
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

module.exports = { installComfortMods, validateRarEntries, extractRar };
