const { _electron: electron } = require('playwright');
const path = require('node:path');
const fs = require('node:fs/promises');
async function main() {
  const assets = path.resolve(__dirname, '../assets');
  const data = await fs.mkdtemp(path.resolve(__dirname, '../test-results/icon-profile-'));
  const app = await electron.launch({ args: [path.resolve(__dirname, '..')], env: { ...process.env, SACOOP_LAUNCHER_DATA: data } });
  try {
    const page = await app.firstWindow();
    await page.setContent(await fs.readFile(path.join(assets, 'icon.svg'), 'utf8'));
    const png = await page.locator('svg').screenshot({ path: path.join(assets, 'icon.png'), omitBackground: true });
    const header = Buffer.alloc(22);
    header.writeUInt16LE(1, 2); header.writeUInt16LE(1, 4);
    header.writeUInt16LE(1, 10); header.writeUInt16LE(32, 12);
    header.writeUInt32LE(png.length, 14); header.writeUInt32LE(22, 18);
    await fs.writeFile(path.join(assets, 'icon.ico'), Buffer.concat([header, png]));
  } finally { await app.close(); await fs.rm(data, { recursive: true, force: true }); }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
