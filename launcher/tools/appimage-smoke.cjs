const { spawn } = require('node:child_process');
const net = require('node:net');
const { setTimeout: delay } = require('node:timers/promises');

async function freePort() {
  const server = net.createServer();
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const port = server.address().port;
  await new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
  return port;
}

function stopGroup(child, signal) {
  try { process.kill(-child.pid, signal); }
  catch (error) { if (error.code !== 'ESRCH') throw error; }
}

async function main() {
  const appImage = process.argv[2];
  if (!appImage) throw new Error('Usage: node tools/appimage-smoke.cjs <AppImage>');
  const port = await freePort();
  const child = spawn(appImage, [
    '--appimage-extract-and-run', '--ozone-platform=x11', `--remote-debugging-port=${port}`,
  ], {
    detached: true,
    env: { ...process.env, APPIMAGE_EXTRACT_AND_RUN: '1' },
    stdio: 'ignore',
  });
  let ws;
  try {
    let pages;
    for (let i = 0; i < 60; i++) {
      if (child.exitCode !== null) throw new Error(`AppImage exited early (${child.exitCode}).`);
      try {
        const response = await fetch(`http://127.0.0.1:${port}/json/list`);
        if (response.ok) { pages = await response.json(); break; }
      } catch {}
      await delay(500);
    }
    const page = pages?.find(item => item.type === 'page' && item.url.startsWith('file:'));
    if (!page) throw new Error('The launcher renderer did not open.');
    ws = new WebSocket(page.webSocketDebuggerUrl);
    await Promise.race([
      new Promise((resolve, reject) => {
        ws.addEventListener('open', resolve, { once: true });
        ws.addEventListener('error', reject, { once: true });
      }),
      delay(5000).then(() => { throw new Error('Timed out connecting to the launcher renderer.'); }),
    ]);
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
    if (title !== 'SA COOP — Back to the Grove') throw new Error(`Unexpected renderer title: ${title}`);
    console.log(`AppImage renderer opened: ${title}`);
  } finally {
    if (ws && ws.readyState < WebSocket.CLOSING) ws.close();
    stopGroup(child, 'SIGTERM');
    await Promise.race([
      child.exitCode === null ? new Promise(resolve => child.once('exit', resolve)) : Promise.resolve(),
      delay(3000),
    ]);
    stopGroup(child, 'SIGKILL');
  }
}

main().catch(error => { console.error(error.message); process.exitCode = 1; });
