'use strict';
// TV apps (YouTube) in console mode: read the controller and send each press to the main process,
// which turns it into a real key press for the TV site. Nothing is exposed to the page.
const { ipcRenderer } = require('electron');
const last = {}, held = {};
function poll() {
  const pads = navigator.getGamepads ? [...navigator.getGamepads()].filter(Boolean) : [];
  const now = performance.now();
  for (const p of pads) {
    const b = i => !!(p.buttons[i] && p.buttons[i].pressed), ax = p.axes || [];
    const st = { a: b(0), b: b(1), x: b(2), lb: b(4), rb: b(5), guide: b(16), up: b(12) || ax[1] < -0.55, down: b(13) || ax[1] > 0.55, left: b(14) || ax[0] < -0.55, right: b(15) || ax[0] > 0.55 };
    for (const [k, v] of Object.entries(st)) {
      const id = p.index + k, prev = last[id]; last[id] = v;
      if (!v) continue;
      const rep = k === 'up' || k === 'down' || k === 'left' || k === 'right';
      if (!prev) { held[id] = now + 300; ipcRenderer.send('tv:pad', k); }
      else if (rep && now > held[id]) { held[id] = now + 90; ipcRenderer.send('tv:pad', k); }
    }
  }
  requestAnimationFrame(poll);
}
addEventListener('DOMContentLoaded', () => requestAnimationFrame(poll));
