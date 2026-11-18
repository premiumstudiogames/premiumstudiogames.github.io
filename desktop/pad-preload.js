'use strict';
// Console mode: reads the controller's Home button while a game runs (also over games in their own program) and tells
// the app: a short press opens the Control Center, held it goes to the console's home screen.
// Home = the PS / guide button, or View + Menu together (Windows keeps the Xbox button for its Game Bar).
const { ipcRenderer } = require('electron');
let down = 0, fired = false;
setInterval(() => {
  let home = false;
  try { for (const p of navigator.getGamepads ? navigator.getGamepads() : []) { if (!p) continue; const b = i => !!(p.buttons[i] && p.buttons[i].pressed); if (b(16) || (b(8) && b(9))) home = true; } } catch (e) {}
  const now = performance.now();
  if (home && !down) { down = now; fired = false; }
  else if (home && !fired && now - down > 600) { fired = true; ipcRenderer.send('pad:home', true); }
  else if (!home && down) { if (!fired) ipcRenderer.send('pad:home', false); down = 0; }
}, 16);
