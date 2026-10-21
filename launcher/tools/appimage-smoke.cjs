const { spawn } = require('node:child_process');
const net = require('node:net');
const { setTimeout: delay } = require('node:timers/promises');

async function main() {
  const appImage = process.argv[2];
  if (!appImage) throw new Error('Usage: node tools/appimage-smoke.cjs <AppImage>');
  const server = net.createServer();
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const port = server.address().port;
  await new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
  const child = spawn(appImage, [
    '--appimage-extract-and-run', '--ozone-platform=x11', `--remote-debugging-port=${port}`,
  ], { env: { ...process.env, APPIMAGE_EXTRACT_AND_RUN: '1' }, stdio: ['ignore', 'ignore', 'pipe'] });
  let logs = '';
  child.stderr.on('data', data => { logs = (logs + data.toString()).slice(-12000); });
  try {
    let pages;
    for (let i = 0; i < 60; i++) {
      if (child.exitCode !== null) throw new Error(`AppImage exited early (${child.exitCode}).\n${logs}`);
      try {
        const response = await fetch(`http://127.0.0.1:${port}/json/list`);
        if (response.ok) { pages = await response.json(); break; }
      } catch {}
      await delay(500);
    }
    const page = pages?.find(item => item.type === 'page' && item.url.startsWith('file:'));
    if (!page) throw new Error(`The launcher renderer did not open.\n${logs}`);
    const ws = new WebSocket(page.webSocketDebuggerUrl);
    await new Promise((resolve, reject) => {
      ws.addEventListener('open', resolve, { once: true });
      ws.addEventListener('error', reject, { once: true });
    });
    const title = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('Timed out reading the launcher title.')), 5000);
      ws.addEventListener('message', event => {
        const data = JSON.parse(event.data);
        if (data.id !== 1) return;
        clearTimeout(timer);
        resolve(data.result?.result?.value);
      });
      ws.send(JSON.stringify({ id: 1, method: 'Runtime.evaluate', params: { expression: 'document.title', returnByValue: true } }));
    });
    ws.close();
    if (title !== 'SA COOP — Back to the Grove') throw new Error(`Unexpected renderer title: ${title}.\n${logs}`);
    console.log(`AppImage renderer opened: ${title}`);
  } finally {
    child.kill('SIGTERM');
    await Promise.race([new Promise(resolve => child.once('exit', resolve)), delay(3000)]);
    if (child.exitCode === null) child.kill('SIGKILL');
  }
}

main().catch(error => { console.error(error.message); process.exitCode = 1; });
