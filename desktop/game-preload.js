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
  console: /[?&]console=1\b/.test(location.search),   // started from console mode: the console version (controller first, full screen)
  achieve(id) { ipcRenderer.send('game:achieve', String(id || '')); } };
// Cloud save: data from your account, put back before the game starts
try {
  const c = ipcRenderer.sendSync('game:cloud');
  if (c) { if (c.clear) localStorage.clear(); for (const [k, v] of Object.entries(c.data || {})) localStorage.setItem(k, String(v)); }
} catch (e) {}
// as the game closes, hand its saved data to the app (it goes to your account)
const handOver = () => { try { ipcRenderer.sendSync('game:save', JSON.stringify(Object.assign({}, localStorage))); } catch (e) {} };
window.addEventListener('beforeunload', handOver); window.addEventListener('pagehide', handOver);
// the controller's Home button (PS / guide, or View + Menu) opens the overlay; held, the console's home screen.
// While the overlay or the console is in front, the game doesn't see the controller (like a console pauses its input).
(function pad(){
  const real = navigator.getGamepads ? navigator.getGamepads.bind(navigator) : null; if (!real) return;
  let paused = false, down = 0, fired = false;
  ipcRenderer.on('game:paused', (e, on) => { paused = !!on; });
  const empty = () => { const n = (real() || []).length; return Array.from({ length: n }, () => null); };
  try { navigator.getGamepads = function () { return paused || !document.hasFocus() ? empty() : real(); }; } catch (e) {}
  const tick = () => { try {
      let home = false; for (const p of real()) { if (!p) continue; const b = i => !!(p.buttons[i] && p.buttons[i].pressed); if (b(16) || (b(8) && b(9))) home = true; }
      const now = performance.now();
      if (home && !down) { down = now; fired = false; }
      else if (home && !fired && now - down > 600) { fired = true; if (document.hasFocus()) ipcRenderer.send('game:guide', true); }
      else if (!home && down) { if (!fired && document.hasFocus()) ipcRenderer.send('game:guide', false); down = 0; }
    } catch (e) {}
    requestAnimationFrame(tick); };
  window.addEventListener('DOMContentLoaded', () => requestAnimationFrame(tick)); })();
