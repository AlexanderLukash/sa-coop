const { spawn } = require('node:child_process');
const { setTimeout: delay } = require('node:timers/promises');

async function main() {
  const appImage = process.argv[2];
  if (!appImage) throw new Error('Usage: node tools/appimage-smoke.cjs <AppImage>');
  const child = spawn(appImage, ['--appimage-extract-and-run', '--ozone-platform=x11'], {
    env: { ...process.env, APPIMAGE_EXTRACT_AND_RUN: '1' },
    stdio: ['ignore', 'ignore', 'pipe'],
  });
  let logs = '';
  child.stderr.on('data', data => { logs = (logs + data.toString()).slice(-12000); });
  try {
    await Promise.race([
      delay(20000),
      new Promise((_, reject) => {
        child.once('error', reject);
        child.once('exit', code => reject(new Error(`AppImage exited early (${code}).\n${logs}`)));
      }),
    ]);
    console.log('AppImage stayed open for 20 seconds without FUSE.');
  } finally {
    child.kill('SIGTERM');
    await Promise.race([new Promise(resolve => child.once('exit', resolve)), delay(3000)]);
    if (child.exitCode === null) child.kill('SIGKILL');
  }
}

main().catch(error => { console.error(error.message); process.exitCode = 1; });
