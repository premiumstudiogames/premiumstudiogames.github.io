'use strict';
// Bridge between the Premium Studio store page and the desktop client.
const { contextBridge, ipcRenderer } = require('electron');

const ok = (() => {
  try { const u = new URL(location.href); return u.origin === 'https://premiumstudiogames.github.io' || (u.protocol === 'file:' && u.pathname.endsWith('offline.html')); }
  catch (e) { return false; }
})();

if (ok) {
  contextBridge.exposeInMainWorld('premiumStudio', {
    isApp: true,
    multiEdition: true,
    version: ipcRenderer.sendSync('app:version'),
    getInstalled: () => ipcRenderer.invoke('ps:getInstalled'),
    install: (id, opts) => ipcRenderer.invoke('ps:install', id, opts),
    getLatest: () => ipcRenderer.invoke('ps:getLatest'),
    uninstall: id => ipcRenderer.invoke('ps:uninstall', id),
    launch: (id, edition) => ipcRenderer.invoke('ps:launch', id, edition),
    showFolder: id => ipcRenderer.invoke('ps:showFolder', id),
    setUser: u => ipcRenderer.invoke('ps:setUser', u),
    retryOnline: () => ipcRenderer.invoke('ps:retryOnline'),
    onProgress: cb => { const f = (e, p) => cb(p); ipcRenderer.on('ps:progress', f); return () => ipcRenderer.removeListener('ps:progress', f); },
    onInstalledChanged: cb => { const f = () => cb(); ipcRenderer.on('ps:installed-changed', f); return () => ipcRenderer.removeListener('ps:installed-changed', f); }
  });
}
