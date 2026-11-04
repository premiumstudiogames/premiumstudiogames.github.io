'use strict';
// Runs inside a game window. Gives games the launcher hooks they already know (TV_LAUNCHER).
const { ipcRenderer } = require('electron');
window.TV_LAUNCHER = {
  version: ipcRenderer.sendSync('app:version'),
  exitGame() { ipcRenderer.send('game:exit'); },
  relaunch() { location.reload(); },
  nick() { return ipcRenderer.sendSync('game:nick') || 'Player'; }
};
window.PREMIUM_STUDIO = { platform: 'windows', version: window.TV_LAUNCHER.version };
