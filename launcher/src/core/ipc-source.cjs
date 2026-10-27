const path = require('node:path');
const { fileURLToPath } = require('node:url');

function rendererFile(url, platform) {
  const parsed = new URL(url);
  if (parsed.protocol !== 'file:' || parsed.search) return null;
  const windows = platform === 'win32';
  const filename = fileURLToPath(parsed, { windows });
  const normalized = (windows ? path.win32 : path.posix).normalize(filename);
  return windows ? normalized.toLowerCase() : normalized;
}

function trustedRenderer(event, window, rendererURL, platform = process.platform) {
  try {
    if (!window || window.isDestroyed() || window.webContents.isDestroyed() || event.sender !== window.webContents)
      return false;
    const frame = event.senderFrame;
    const main = window.webContents.mainFrame;
    // Electron can expose different JS wrappers for the same Chromium frame.
    if (!frame || !main || frame.detached || !Number.isInteger(frame.processId) || !Number.isInteger(frame.routingId) ||
        frame.processId !== main.processId || frame.routingId !== main.routingId) return false;
    // Compare the actual file, rather than its URL spelling (Windows drive/path
    // case, URL escapes and in-page anchors can differ without changing the file).
    const actual = rendererFile(frame.url, platform);
    return actual !== null && actual === rendererFile(rendererURL, platform);
  } catch { return false; }
}

module.exports = { trustedRenderer };
