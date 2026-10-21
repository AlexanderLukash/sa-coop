const { contextBridge, ipcRenderer } = require('electron');
const methods = ['state', 'save', 'profile', 'choose-game', 'release', 'install', 'cancel-install', 'discover', 'host', 'join',
  'leave', 'ready', 'start', 'solo', 'copy-invite', 'open-folder', 'open-releases'];
const api = Object.fromEntries(methods.map(name => [name.replace(/-([a-z])/g, (_, c) => c.toUpperCase()),
  (...args) => ipcRenderer.invoke(name, ...args)]));
api.onEvent = callback => {
  const listener = (_event, message) => callback(message);
  ipcRenderer.on('launcher:event', listener);
  return () => ipcRenderer.removeListener('launcher:event', listener);
};
contextBridge.exposeInMainWorld('launcher', api);
