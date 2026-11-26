const fs = require('node:fs/promises');
const path = require('node:path');
const { constants } = require('node:fs');
const { download, exists, extractZip, regularPath } = require('./install.cjs');
const { extractRarArchive } = require('./comfort-mods.cjs');

const title = 'Extended Vanilla Vehicles Pack 2.0';
const pack = {
  url: 'https://gta.com.ua/files/gta-san-andreas/13862?download',
  size: 136943825,
  digest: 'sha256:234175fde3e6678534b9aef589e053c2aec3311cee64e3505b426a0f40f9cc14',
};
const cleo = {
  url: 'https://github.com/cleolibrary/CLEO4/releases/download/v4.4.4/CLEO4.zip',
  size: 550990,
  digest: 'sha256:aca8a1ca9dfc85d6f886b5963388059b2580501219f1f6b891114560552a8560',
};
const fla = '$fastman92limitAdjuster.asi';
const ini = 'fastman92limitAdjuster_GTASA.ini';
const evvp = 'modloader/Extended Vanilla Vehicles Pack';
const marker = 'modloader/SA-COOP evvp/.installed.json';
const revision = 2;
const externalData = ['data/gtasa_trainTypeCarriages.dat', 'data/gtasa_vehicleAudioSettings.cfg'];
const ola = ['III.VC.SA.LimitAdjuster.asi', 'scripts/III.VC.SA.LimitAdjuster.asi',
  'modloader/SA-COOP open-limit-adjuster/III.VC.SA.LimitAdjuster.asi'];
const legacy = [`${evvp}/${fla}`, `${evvp}/${ini}`, ...ola];
const required = [fla, ini, 'CLEO.asi', 'bass.dll', `${evvp}/data/vehicles.ide`, ...externalData];

function changeIni(text, section, key, value, minimum = false) {
  const lines = text.split(/\r?\n/);
  const start = lines.findIndex(line => line.trim().toLowerCase() === `[${section.toLowerCase()}]`);
  if (start < 0) throw new Error(`У конфігурації fastman92 немає секції [${section}].`);
  let active = false, commented = -1;
  let end = start + 1;
  for (; end < lines.length && !/^\s*\[/.test(lines[end]); ++end) {
    const setting = lines[end].match(/^\s*([#;]?)\s*([^=]+?)\s*=\s*(.*)$/);
    if (!setting || setting[2].toLowerCase() !== key.toLowerCase()) continue;
    if (setting[1]) { commented = end; continue; }
    if (!minimum || !(Number.parseInt(setting[3], 10) >= value)) lines[end] = `${key} = ${value}`;
    active = true;
  }
  if (!active) {
    if (commented >= 0) lines[commented] = `${key} = ${value}`;
    else lines.splice(end, 0, `${key} = ${value}`);
  }
  return lines.join(text.includes('\r\n') ? '\r\n' : '\n');
}

async function vehiclePackInstalled(gameDir) {
  if (!gameDir) return false;
  try {
    const info = JSON.parse(await fs.readFile(await regularPath(gameDir, marker), 'utf8'));
    if (info.revision !== revision) return false;
    for (const file of required) if (!await exists(await regularPath(gameDir, file))) return false;
    for (const file of legacy) if (await exists(await regularPath(gameDir, file))) return false;
    return true;
  } catch { return false; }
}

async function installVehiclePack(gameDir, progress = () => {}, signal) {
  const root = await fs.realpath(gameDir);
  if (await vehiclePackInstalled(root)) return;
  if (!await exists(await regularPath(root, 'modloader.asi')))
    throw new Error('Потрібен Mod Loader. Виберіть папку GTA в лаунчері, щоб його встановити.');
  const work = await fs.mkdtemp(path.join(root, '.sacoop-evvp-'));
  const report = (stage, details) => progress({ ...details, stage, mod: details?.mod || title });
  let backup;
  const added = [], moved = [];
  try {
    const archive = path.join(work, 'EVVP.rar'), stage = path.join(work, 'evvp'), runtime = path.join(work, 'cleo');
    await download(pack, archive, event => report('mod-download', event), signal, pack.size);
    report('mod-extract', { percent: 0 });
    await extractRarArchive(archive, stage, signal, event => report('mod-extract', event));

    const cleoArchive = path.join(work, 'CLEO4.zip');
    await download(cleo, cleoArchive, event => report('mod-download', { ...event, mod: 'CLEO 4.4.4' }), signal, cleo.size);
    await extractZip(cleoArchive, runtime, signal);

    const files = new Map();
    for (const entry of await fs.readdir(stage, { recursive: true, withFileTypes: true })) {
      if (!entry.isFile()) continue;
      const source = path.join(entry.parentPath, entry.name);
      const parts = path.relative(stage, source).split(path.sep);
      const folder = parts.indexOf('EVVP_2.0');
      if (folder < 0 || /\.log$/i.test(entry.name)) continue;
      const relative = parts.slice(folder + 1).join('/');
      if (ola.includes(relative)) continue;
      if (relative === `${evvp}/${fla}` || relative === `${evvp}/${ini}`) files.set(entry.name, source);
      else {
        files.set(relative, source);
        for (const file of externalData) if (relative === `${evvp}/${file}`) files.set(file, source);
      }
    }
    for (const file of ['CLEO.asi', 'bass.dll', 'cleo/FileSystemOperations.cleo', 'cleo/IniFiles.cleo', 'cleo/IntOperations.cleo'])
      files.set(file, path.join(runtime, file));
    for (const file of required) if (!files.has(file)) throw new Error(`У пакеті немає ${file}.`);

    const disabled = [];
    for (const file of legacy) if (await exists(await regularPath(root, file))) disabled.push(file);
    const oldFla = await regularPath(root, `${evvp}/${fla}`);
    if (disabled.includes(`${evvp}/${fla}`) && !(await fs.readFile(oldFla)).equals(await fs.readFile(files.get(fla))))
      throw new Error('У папці EVVP є інша версія fastman92 Limit Adjuster.');

    const rootConfig = await regularPath(root, ini);
    const oldConfig = await regularPath(root, `${evvp}/${ini}`);
    const existingConfig = await exists(rootConfig) ? rootConfig : await exists(oldConfig) ? oldConfig : null;
    // FLA 4.3 extends stores by changing their constructors. Load it from the game root
    // before GTA runs those constructors, then let Mod Loader provide the IDE/model files.
    let config = await fs.readFile(existingConfig || files.get(ini), 'latin1');
    config = changeIni(config, 'DYNAMIC LIMITS', 'VehicleStructs', 128, true);
    config = changeIni(config, 'IDE LIMITS', 'Vehicle Models', 512, true);
    config = changeIni(config, 'IDE LIMITS', 'IDE Objects Type 1', 18000, true);
    config = changeIni(config, 'IDE LIMITS', 'IDE Objects Type 2', 256, true);
    const stagedConfig = path.join(work, ini);
    await fs.writeFile(stagedConfig, config, 'latin1');
    files.set(ini, stagedConfig);

    const next = [];
    for (const [relative, source] of files) {
      signal?.throwIfAborted();
      const target = await regularPath(root, relative);
      if (await exists(target)) {
        if ((await fs.readFile(target)).equals(await fs.readFile(source))) continue;
        if (relative !== ini) throw new Error(`Файл ${relative} уже існує та відрізняється.`);
        disabled.push(relative);
      }
      next.push([target, source]);
    }
    if (await exists(await regularPath(root, marker))) disabled.push(marker);
    const stagedMarker = path.join(work, 'installed.json');
    await fs.writeFile(stagedMarker, JSON.stringify({ id: 'evvp', version: '2.0', revision }));
    next.push([await regularPath(root, marker), stagedMarker]);

    report('mod-install');
    if (disabled.length) {
      const parent = await regularPath(root, 'coop');
      await fs.mkdir(parent, { recursive: true });
      backup = await fs.mkdtemp(path.join(parent, 'evvp-backup-'));
    }
    for (const relative of disabled) {
      signal?.throwIfAborted();
      const from = await regularPath(root, relative), to = path.join(backup, relative);
      await fs.mkdir(path.dirname(to), { recursive: true });
      await fs.rename(from, to);
      moved.push([from, to]);
    }
    for (const [target, source] of next) {
      signal?.throwIfAborted();
      await fs.mkdir(path.dirname(target), { recursive: true });
      await fs.copyFile(source, target, constants.COPYFILE_EXCL);
      added.push(target);
    }
    report('mod-done');
  } catch (error) {
    for (const file of added.reverse()) await fs.rm(file, { force: true });
    for (const [from, to] of moved.reverse()) await fs.rename(to, from);
    if (backup) await fs.rm(backup, { recursive: true, force: true });
    throw error;
  } finally {
    await fs.rm(work, { recursive: true, force: true });
  }
}

async function saveDonationCode(gameDir, code) {
  const normalized = String(code).trim().toUpperCase();
  if (!/^[0-9A-F]{5}(?:-[0-9A-F]{5}){4}$/.test(normalized))
    throw new Error('Введіть код fastman92 з п’яти груп по п’ять символів.');
  const root = await fs.realpath(gameDir);
  const file = await regularPath(root, ini);
  const source = await fs.readFile(file, 'latin1');
  const work = await fs.mkdtemp(path.join(root, '.sacoop-fla-'));
  try {
    const staged = path.join(work, ini);
    await fs.writeFile(staged, changeIni(source, 'MAIN', 'donation window disable code', normalized), 'latin1');
    await fs.rename(staged, file);
  } finally { await fs.rm(work, { recursive: true, force: true }); }
}

module.exports = { installVehiclePack, vehiclePackInstalled, saveDonationCode };
