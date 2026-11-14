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
// as the game closes, hand its saved data to the app (it goes to your account)
const handOver = () => { try { ipcRenderer.sendSync('game:save', JSON.stringify(Object.assign({}, localStorage))); } catch (e) {} };
window.addEventListener('beforeunload', handOver); window.addEventListener('pagehide', handOver);
// the controller's Home / Guide button opens the overlay (like a console)
(function pad(){ let was = false;
  const tick = () => { try { const ps = navigator.getGamepads ? [...navigator.getGamepads()].filter(Boolean) : [];
      const now = ps.some(p => p.buttons[16] && p.buttons[16].pressed);
      if (now && !was) ipcRenderer.send('game:guide'); was = now; } catch (e) {}
    requestAnimationFrame(tick); };
  window.addEventListener('DOMContentLoaded', () => requestAnimationFrame(tick)); })();
