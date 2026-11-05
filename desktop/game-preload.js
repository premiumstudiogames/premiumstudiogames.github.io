'use strict';
// Runs inside a game window. Gives games the launcher hooks they already know (TV_LAUNCHER).
const { ipcRenderer } = require('electron');
window.TV_LAUNCHER = {
  version: ipcRenderer.sendSync('app:version'),
  exitGame() { ipcRenderer.send('game:exit'); },
  relaunch() { location.reload(); },
  nick() { return ipcRenderer.sendSync('game:nick') || 'Player'; }
};
window.PREMIUM_STUDIO = { platform: 'windows', version: window.TV_LAUNCHER.version, user: ipcRenderer.sendSync('game:user'),
  achieve(id) { ipcRenderer.send('game:achieve', String(id || '')); } };
// Cloud save: data from your account, put back before the game starts
try {
  const c = ipcRenderer.sendSync('game:cloud');
  if (c) { if (c.clear) localStorage.clear(); for (const [k, v] of Object.entries(c.data || {})) localStorage.setItem(k, String(v)); }
} catch (e) {}
