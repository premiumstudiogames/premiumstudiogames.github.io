'use strict';
// Premium Studio desktop client: hosts the store, installs / launches / removes games.
const { app, BrowserWindow, ipcMain, shell, protocol, net, Menu, Tray, nativeImage } = require('electron');
const path = require('path');
const fs = require('fs');
const fsp = fs.promises;
const crypto = require('crypto');
const { pathToFileURL } = require('url');

const SITE = 'https://premiumstudiogames.github.io/';
const SITE_ORIGIN = new URL(SITE).origin;
const VERSION_LABEL = app.getVersion().split('.').map(n => n.padStart(2, '0')).join('.');
const ICON = path.join(__dirname, 'build', 'icon.ico');

protocol.registerSchemesAsPrivileged([{
  scheme: 'psgame',
  privileges: { standard: true, secure: true, supportFetchAPI: true, stream: true, corsEnabled: true }
}]);

/* ---------------- paths + registry ---------------- */
const LOCAL = process.env.LOCALAPPDATA || app.getPath('userData');
const GAMES_DIR = path.join(LOCAL, 'PremiumStudio', 'games');
const REG_FILE = path.join(GAMES_DIR, 'installed.json');
const validId = id => typeof id === 'string' && /^[a-z0-9-]{1,40}$/.test(id);
const EDITIONS = ['demo', 'std', 'gold'];

// Where each kind of game lives on disk.
// bf-launcher: BLOCKFALL's own Windows launcher, in the folder it expects (it updates itself and the game from its CDN).
const BF_DIR = path.join(LOCAL, 'Programs', 'TERRAVOLT');
const BF_EXE = path.join(BF_DIR, 'BLOCKFALL3001.exe');
const BF_DATA = path.join(app.getPath('appData'), '.terravolt');
function gameDir(id, m) { return m && m.type === 'bf-launcher' ? BF_DIR : path.join(GAMES_DIR, id); }
// Every owned edition installs as its own game. Install key = "<game>--<edition>" (older installs keep the plain game id).
const ED_LABEL = { demo: 'Demo', std: 'Standard', gold: 'Gold' };
const edTitle = (name, ed) => name + (ED_LABEL[ed] ? ' ' + ED_LABEL[ed] : '');
function keyInfo(key) {
  const r = readReg()[key];
  if (r) return { game: r.game || key.split('--')[0], edition: r.edition };
  const [game, edition] = key.split('--');
  return { game, edition: EDITIONS.includes(edition) ? edition : 'demo' };
}

function readReg() { try { return JSON.parse(fs.readFileSync(REG_FILE, 'utf8')); } catch (e) { return {}; } }
async function writeReg(reg) { await fsp.mkdir(GAMES_DIR, { recursive: true }); await fsp.writeFile(REG_FILE, JSON.stringify(reg, null, 2)); }
function installedNow() {
  const reg = readReg(), out = {};
  for (const [id, v] of Object.entries(reg)) {
    const ok = v.type === 'bf-launcher' ? fs.existsSync(BF_EXE) : fs.existsSync(path.join(gameDir(id, v), v.entry || 'index.html'));
    if (ok) out[id] = { ...v, game: v.game || id.split('--')[0] };
  }
  return out;
}

/* ---------------- the signed-in Premium Studio account (games play under it) ---------------- */
const USER_FILE = path.join(app.getPath('userData'), 'ps-user.json');
let currentUser = (() => { try { return JSON.parse(fs.readFileSync(USER_FILE, 'utf8')); } catch (e) { return null; } })();
async function setUser(u) {
  const clip = (v, n) => String(v || '').slice(0, n);
  const before = currentUser && currentUser.uid;
  const next = u && typeof u === 'object' ? { username: clip(u.username, 20), name: clip(u.name, 32), pid: clip(u.pid, 10), email: clip(u.email, 120), uid: clip(u.uid, 128), refreshToken: clip(u.refreshToken, 4096) } : null;
  // switched or signed out (like Steam): games running under the old account close first, saving to the old account
  if (before && before !== (next && next.uid)) await closeRunningGames();
  currentUser = next;
  try { if (currentUser) fs.writeFileSync(USER_FILE, JSON.stringify(currentUser)); else fs.rmSync(USER_FILE, { force: true }); } catch (e) {}
  writeBfAccount();
  updateTray();
}
function closeRunningGames() {
  if (process.platform === 'win32') { try { require('child_process').execSync('taskkill /IM BLOCKFALL3001.exe /F', { stdio: 'ignore' }); } catch (e) {} }
  for (const k of [...running.keys()]) if (!gameWins.has(k)) markStopped(k);
  return Promise.all([...gameWins.keys()].map(closeWebGame));
}
const playerName = () => (currentUser && (currentUser.username || currentUser.name)) || '';
// BLOCKFALL's launcher: the Premium Studio account it should play under
function writeBfAccount() {
  try {
    const f = path.join(BF_DATA, 'premium_account.json');
    if (!currentUser || !currentUser.username) { fs.rmSync(f, { force: true }); return; }
    if (!fs.existsSync(BF_DATA)) return;
    fs.writeFileSync(f, JSON.stringify({ ...currentUser, source: 'premium-studio', at: Date.now() }, null, 1));
  } catch (e) {}
}

/* ---------------- windows ---------------- */
let mainWin = null;
const gameWins = new Map();

function isTrusted(frameUrl) {
  if (!frameUrl) return false;
  if (frameUrl.startsWith('file://') && frameUrl.includes('offline.html')) return true;
  try { return new URL(frameUrl).origin === SITE_ORIGIN; } catch (e) { return false; }
}

function createMain(hash, query) {
  const q = query ? '?' + query : '';
  if (mainWin && !mainWin.isDestroyed()) {
    if (hash) mainWin.loadURL(SITE + q + '#' + hash);
    if (mainWin.isMinimized()) mainWin.restore();
    mainWin.show(); mainWin.focus(); return mainWin;
  }
  // remember the window like Steam does (size, place, maximized); first time it opens big
  const WF = path.join(app.getPath('userData'), 'window.json');
  let ws = null; try { ws = JSON.parse(fs.readFileSync(WF, 'utf8')); } catch (e) {}
  const { screen } = require('electron'), wa = screen.getPrimaryDisplay().workAreaSize;
  mainWin = new BrowserWindow({
    width: ws && ws.w || Math.min(1600, wa.width), height: ws && ws.h || Math.min(980, wa.height), x: ws ? ws.x : undefined, y: ws ? ws.y : undefined, minWidth: 900, minHeight: 600,
    backgroundColor: '#0a0d14', title: 'Premium Studio', icon: ICON, autoHideMenuBar: true, show: false,
    webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, sandbox: true, nodeIntegration: false }
  });
  mainWin.once('ready-to-show', () => { if (!ws ? wa.width <= 1920 : ws.max) mainWin.maximize(); mainWin.show(); });
  const saveWin = () => { try { if (!mainWin || mainWin.isDestroyed() || mainWin.isMinimized()) return; const b = mainWin.getNormalBounds();
    fs.writeFileSync(WF, JSON.stringify({ x: b.x, y: b.y, w: b.width, h: b.height, max: mainWin.isMaximized() })); } catch (e) {} };
  mainWin.on('resize', saveWin); mainWin.on('move', saveWin); mainWin.on('maximize', saveWin); mainWin.on('unmaximize', saveWin);
  mainWin.webContents.setWindowOpenHandler(({ url }) => { openExternal(url); return { action: 'deny' }; });
  mainWin.webContents.on('will-navigate', (e, url) => {
    if (url.startsWith('premiumstudio://')) { e.preventDefault(); handleDeepLink(url); return; }
    if (!isTrusted(url) && !url.startsWith('https://premium-studio-f9bc9.firebaseapp.com')) { e.preventDefault(); openExternal(url); }
  });
  mainWin.webContents.on('did-fail-load', (e, code, desc, url, isMain) => {
    if (isMain && code !== -3) mainWin.loadFile(path.join(__dirname, 'offline.html'));
  });
  mainWin.loadURL(SITE + q + (hash ? '#' + hash : ''));
  mainWin.on('close', e => { if (!quitting && tray) { e.preventDefault(); mainWin.hide(); } });
  mainWin.on('closed', () => { mainWin = null; });
  mainWin.webContents.on('did-finish-load', () => mainWin && mainWin.webContents.send('ps:running', runningList()));
  return mainWin;
}
/* ---------------- running games (like Steam: the app knows what you're playing) ---------------- */
const running = new Map();   // install key -> {since, game, name}
const FB_KEY = 'AIzaSyBLZ4t7g5S03MFQS8ZF5jKDjKER-Te_Me0';
const FB_DB = 'projects/premium-studio-f9bc9/databases/(default)/documents';
let idTok = { t: '', exp: 0, uid: '' };
async function fbToken() {
  if (!currentUser || !currentUser.refreshToken || !currentUser.uid) return null;
  if (idTok.t && idTok.uid === currentUser.uid && Date.now() < idTok.exp - 60000) return idTok.t;
  const r = await net.fetch(`https://securetoken.googleapis.com/v1/token?key=${FB_KEY}`, { method: 'POST', headers: { 'content-type': 'application/x-www-form-urlencoded' }, body: 'grant_type=refresh_token&refresh_token=' + encodeURIComponent(currentUser.refreshToken) });
  if (!r.ok) return null;
  const j = await r.json();
  idTok = { t: j.id_token, exp: Date.now() + (+j.expires_in || 3600) * 1000, uid: currentUser.uid };
  return idTok.t;
}
async function fbCommit(writes) {
  try { const t = await fbToken(); if (!t) return;
    await net.fetch(`https://firestore.googleapis.com/v1/${FB_DB}:commit`, { method: 'POST', headers: { authorization: 'Bearer ' + t, 'content-type': 'application/json' }, body: JSON.stringify({ writes }) });
  } catch (e) {}
}
// what you're playing, for your friends' list (refreshed every few minutes while a game runs)
function pushPresence() {
  if (!currentUser || !currentUser.uid) return;
  const last = [...running.values()].pop();
  return fbCommit([{ update: { name: `${FB_DB}/presence/${currentUser.uid}`, fields: { game: { stringValue: last ? String(last.name).slice(0, 60) : '' }, gid: { stringValue: last ? String(last.game) : '' } } },
    updateTransforms: [{ fieldPath: 'at', setToServerValue: 'REQUEST_TIME' }] }]);
}
setInterval(() => { if (running.size) pushPresence(); }, 4 * 60000);
// play time per game, kept on the account
function addPlaytime(r, ms) {
  const min = Math.min(1440, Math.round(ms / 60000));
  if (!currentUser || !currentUser.uid || !r.game) return;
  return fbCommit([{ update: { name: `${FB_DB}/users/${currentUser.uid}/play/${r.game}`, fields: { game: { stringValue: r.game } } }, updateMask: { fieldPaths: ['game'] },
    updateTransforms: [{ fieldPath: 'minutes', increment: { integerValue: String(min) } }, { fieldPath: 'last', setToServerValue: 'REQUEST_TIME' }] }]);
}
const runningList = () => Object.fromEntries([...running].map(([k, v]) => [k, { since: v.since, game: v.game, closing: stopping.has(k) || !!v.closing }]));
function runningChanged() {
  if (mainWin && !mainWin.isDestroyed()) mainWin.webContents.send('ps:running', runningList());
  updateTray();
}
function markRunning(key) {
  if (running.has(key)) return;
  const g = readReg()[key] || {};
  running.set(key, { since: Date.now(), game: g.game || key.split('--')[0], name: g.name || key });
  pushPresence(); runningChanged();
  if (g.type === 'bf-launcher') watchBf();
}
function markStopped(key) {
  const r = running.get(key); if (!r) return;
  running.delete(key);
  addPlaytime(r, Date.now() - r.since); pushPresence(); runningChanged();
}
// BLOCKFALL runs as its own program: watch its process
let bfTimer = null;
function bfAlive() {
  // newer launchers say whether a window is open (it may stay in the tray after you close it)
  try { const st = JSON.parse(fs.readFileSync(path.join(BF_DATA, 'premium_state.json'), 'utf8'));
    if (Date.now() - st.at < 15000) return Promise.resolve(!!st.open);
    if (Date.now() - st.at < 600000) return Promise.resolve(false);   // it was reporting and stopped: closed
  } catch (e) {}
  return new Promise(res => {
    if (process.platform !== 'win32') return res(false);
    require('child_process').execFile('tasklist', ['/FI', 'IMAGENAME eq BLOCKFALL3001.exe', '/NH'], { windowsHide: true }, (e, out) => res(!e && /BLOCKFALL3001\.exe/i.test(out || '')));
  });
}
function watchBf() {
  if (bfTimer) return;
  let misses = 0;
  bfTimer = setInterval(async () => {
    const bfKeys = [...running.keys()].filter(k => (readReg()[k] || {}).type === 'bf-launcher');
    if (!bfKeys.length) { clearInterval(bfTimer); bfTimer = null; return; }
    if (bfKeys.some(k => stopping.has(k))) return;
    if (bfKeys.some(k => Date.now() - running.get(k).since < 45000) && !(await bfAlive())) return;   // still starting (or updating itself)
    if (await bfAlive()) { misses = 0; return; }
    if (++misses >= 3) { bfKeys.forEach(markStopped); clearInterval(bfTimer); bfTimer = null; }
  }, 5000);
}
/* ---------------- cloud saves (like Steam Cloud) ----------------
   A web game's saved data (its localStorage) goes to the player's account when the game closes,
   and comes back before it starts, on any PC. Kept at users/<uid>/cloud/<game>. */
const cloudRestore = new Map();   // install key -> {clear, data}
async function fbGet(docPath) {
  const t = await fbToken(); if (!t) return undefined;
  const r = await net.fetch(`https://firestore.googleapis.com/v1/${FB_DB}/${docPath}`, { headers: { authorization: 'Bearer ' + t } });
  if (r.status === 404) return null;
  if (!r.ok) return undefined;
  return r.json();
}
async function cloudPull(key) {
  cloudRestore.delete(key);
  const g = readReg()[key]; if (!g || g.type !== 'manifest' || !currentUser || !currentUser.uid) return;
  const game = g.game || key.split('--')[0], uid = currentUser.uid;
  let d; try { d = await fbGet(`users/${uid}/cloud/${game}`); } catch (e) { d = undefined; }
  if (d === undefined) return;   // offline: play with what's on this PC
  const at = d && d.fields && d.fields.at ? Date.parse(d.fields.at.timestampValue) : 0;
  const otherUser = g.cloudUid && g.cloudUid !== uid;
  if (d && (otherUser || !g.cloudAt || at > g.cloudAt)) {
    let data = {}; try { data = JSON.parse(d.fields.data.stringValue || '{}'); } catch (e) {}
    cloudRestore.set(key, { clear: true, data });
  } else if (!d && otherUser) cloudRestore.set(key, { clear: true, data: {} });   // another account played here before: start clean
  const reg = readReg(); if (reg[key]) { reg[key].cloudUid = uid; if (d) reg[key].cloudAt = at; await writeReg(reg); }
}
async function cloudPush(key, json) {
  const g = readReg()[key]; if (!g || !currentUser || !currentUser.uid || typeof json !== 'string' || json.length > 900000) return;
  const game = g.game || key.split('--')[0];
  await fbCommit([{ update: { name: `${FB_DB}/users/${currentUser.uid}/cloud/${game}`, fields: { data: { stringValue: json }, size: { integerValue: String(json.length) } } },
    updateTransforms: [{ fieldPath: 'at', setToServerValue: 'REQUEST_TIME' }] }]);
  const reg = readReg(); if (reg[key]) { reg[key].cloudUid = currentUser.uid; reg[key].cloudAt = Date.now() + 5000; await writeReg(reg); }
}
const lastSave = new Map();   // install key -> localStorage JSON from the game page (latest)
async function snapshot(key) {
  const w = gameWins.get(key); if (!w || w.isDestroyed()) return;
  try { const json = await Promise.race([w.webContents.executeJavaScript('JSON.stringify(Object.assign({}, localStorage))'), new Promise(r => setTimeout(() => r(null), 2000))]);
    if (typeof json === 'string') lastSave.set(key, json); } catch (e) {}
}
setInterval(() => { for (const k of gameWins.keys()) snapshot(k); }, 60000);
// close a web game: take its saves first, then close; resolves after it's closed and the saves went up
function closeWebGame(key) {
  const w = gameWins.get(key); if (!w || w.isDestroyed()) return Promise.resolve();
  return snapshot(key).then(() => new Promise(res => { const t = setTimeout(res, 6000); w.once('closed', () => { clearTimeout(t); res(); }); w.close(); }))
    .then(() => pushing.get(key)).catch(() => {});
}
const pushing = new Map();
ipcMain.on('game:save', (e, json) => {
  const k = [...gameWins].find(([, w]) => !w.isDestroyed() && w.webContents === e.sender);
  if (k && typeof json === 'string') lastSave.set(k[0], json);
  e.returnValue = true;
});
ipcMain.on('game:cloud', e => {
  const k = [...gameWins].find(([, w]) => !w.isDestroyed() && w.webContents === e.sender);
  const r = k && cloudRestore.get(k[0]); if (k) cloudRestore.delete(k[0]);
  e.returnValue = r || null;
});

const stopping = new Set();
async function stopGame(key) {
  const g = readReg()[key];
  if (g && g.type === 'bf-launcher') {
    if (stopping.has(key)) return; stopping.add(key); runningChanged();
    // ask the launcher to save and close; if it doesn't, close it
    try { fs.writeFileSync(path.join(BF_DATA, 'premium_cmd.json'), JSON.stringify({ cmd: 'close', at: Date.now() })); } catch (e) {}
    const t0 = Date.now();
    while (Date.now() - t0 < 25000) { await new Promise(r => setTimeout(r, 1000)); if (!(await bfAlive())) break; }
    if (process.platform === 'win32') { try { require('child_process').execSync('taskkill /IM BLOCKFALL3001.exe /F', { stdio: 'ignore' }); } catch (e) {} }
    try { fs.rmSync(path.join(BF_DATA, 'premium_cmd.json'), { force: true }); } catch (e) {}
    stopping.delete(key); markStopped(key); return;
  }
  const w = gameWins.get(key);
  if (w && !w.isDestroyed()) { const r = running.get(key); if (r) r.closing = true; runningChanged(); await closeWebGame(key); } else markStopped(key);
}

/* ---------------- tray: the app keeps running in the background, like Steam ---------------- */
let tray = null, quitting = false;
function updateTray() {
  if (!tray) return;
  const games = Object.entries(installedNow());
  const items = [
    { label: 'المتجر', click: () => createMain('store') },
    { label: 'المكتبة', click: () => createMain('library') },
    { label: 'الأصدقاء', click: () => createMain('friends') },
    { type: 'separator' }
  ];
  if (running.size) for (const [k, r] of running) items.push({ label: `■ إغلاق ${r.name}`, click: () => stopGame(k) });
  else for (const [k, v] of games.slice(0, 8)) items.push({ label: v.name, click: () => playFromOutside(k) });
  items.push({ type: 'separator' }, { label: 'خروج من Premium Studio', click: () => { quitting = true; app.quit(); } });
  tray.setContextMenu(Menu.buildFromTemplate(items));
  tray.setToolTip(running.size ? 'Premium Studio — ' + [...running.values()].map(r => r.name).join('، ') : 'Premium Studio');
}
function makeTray() {
  try {
    tray = new Tray(nativeImage.createFromPath(ICON));
    tray.on('click', () => createMain());
    tray.on('double-click', () => createMain());
    updateTray();
  } catch (e) { tray = null; }
}
// started from a desktop shortcut or the tray: play only if signed in, otherwise open the sign-in
function playFromOutside(key) {
  if (!currentUser || !currentUser.uid) { createMain(); return; }
  launchWithUpdate(key).catch(() => createMain());
}

function openExternal(url) { if (/^https?:\/\//.test(url)) shell.openExternal(url); }

// Before starting a game that updates from its own manifest, pull the newest version (like the game's standalone app does).
/* ---------------- "Starting game" window (like Steam's) ---------------- */
const COVERS = path.join(app.getPath('userData'), 'covers');
let splash = null, splashKey = '';
function splashHTML(name, cover) {
  const esc = t => String(t).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  return `<!doctype html><html dir="rtl"><head><meta charset="utf-8"><style>
  *{box-sizing:border-box;margin:0}body{font-family:"Segoe UI",Tahoma,sans-serif;background:#1b2130;color:#e8ecf3;height:100vh;display:flex;gap:20px;padding:18px;border:1px solid #2c3446;-webkit-app-region:drag;user-select:none;overflow:hidden}
  .art{width:150px;flex:none;background:#0b0f16 center/cover no-repeat;border:1px solid #2c3446;${cover ? `background-image:url("${cover}")` : 'background-image:linear-gradient(135deg,#ffcc1f,#ff7a1f)'}}
  .side{flex:1;display:flex;flex-direction:column;min-width:0}
  small{color:#9aa4b8;font-size:14px}h1{font-size:26px;margin:6px 0 16px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
  #st{color:#9aa4b8;letter-spacing:.12em;font-size:13px;font-weight:600}
  .bar{height:4px;background:#0b0f16;margin-top:10px;overflow:hidden}.bar i{display:block;height:100%;width:30%;background:#ffcc1f;animation:a 1.2s infinite ease-in-out}
  @keyframes a{0%{transform:translateX(120%)}100%{transform:translateX(-340%)}}
  a{-webkit-app-region:no-drag;cursor:pointer;font:inherit;text-decoration:none;text-align:center}
  .open{margin-top:auto;background:#2c3650;color:#e8ecf3;border:0;padding:11px;font-size:14px}.open:hover{background:#38446a}
  .x{position:absolute;top:8px;left:10px;background:none;border:0;color:#9aa4b8;font-size:18px}.x:hover{color:#fff}
  .ps{position:absolute;bottom:8px;left:12px;font:700 11px sans-serif;color:#5c667a}.ps b{color:#ffcc1f}
  </style></head><body><a class="x" href="#close">✕</a><div class="art"></div>
  <div class="side"><small>جاري تشغيل اللعبة</small><h1>${esc(name)}</h1><div id="st">يتحقق من التحديثات…</div><div class="bar"><i></i></div>
  <a class="open" href="#open">افتح Premium Studio</a></div></body></html>`;
}
function showSplash(key) {
  closeSplash();
  const g = readReg()[key]; if (!g) return;
  let cover = ''; try { cover = 'data:image/png;base64,' + fs.readFileSync(path.join(COVERS, (g.game || key.split('--')[0]) + '.png')).toString('base64'); } catch (e) {}
  splashKey = key;
  splash = new BrowserWindow({ width: 560, height: 240, frame: false, resizable: false, alwaysOnTop: true, skipTaskbar: false, show: false, backgroundColor: '#1b2130', title: g.name, icon: ICON,
    webPreferences: { sandbox: true, contextIsolation: true } });
  splash.loadURL('data:text/html;charset=utf-8,' + encodeURIComponent(splashHTML(g.name, cover)));
  splash.once('ready-to-show', () => splash && splash.show());
  splash.webContents.on('did-navigate-in-page', (e, url) => { const h = url.split('#')[1]; if (h === 'open') { closeSplash(); createMain('library'); } else if (h === 'close') closeSplash(); });
  splash.on('closed', () => { splash = null; });
  setTimeout(() => { if (splashKey === key) closeSplash(); }, 45000);   // never hang around
}
function splashStatus(t) { if (splash && !splash.isDestroyed()) splash.webContents.executeJavaScript(`document.getElementById('st').textContent=${JSON.stringify(t)}`).catch(() => {}); }
function closeSplash() { splashKey = ''; if (splash && !splash.isDestroyed()) splash.destroy(); splash = null; }
ipcMain.handle('ps:setCover', guard(async (id, dataUrl) => {
  if (!validId(id) || typeof dataUrl !== 'string' || !dataUrl.startsWith('data:image/png;base64,') || dataUrl.length > 3e6) return false;
  await fsp.mkdir(COVERS, { recursive: true }); await fsp.writeFile(path.join(COVERS, id + '.png'), Buffer.from(dataUrl.split(',')[1], 'base64')); return true;
}));

async function launchWithUpdate(id, edition) {
  const g = installedNow()[id];
  if (g && !running.has(id)) showSplash(id);
  if (g && g.type === 'manifest' && !busy.has(id) && !running.has(id)) {
    try {
      const m = ((await fetchManifest()).games || {})[g.game];
      if (m && JSON.stringify(m.account || null) !== JSON.stringify(readReg()[id].account || null)) { const reg = readReg(); reg[id].account = m.account || undefined; await writeReg(reg); }
      if (m) { const l = await latestOf(m); if (l.version && l.version !== g.version) { splashStatus('يحمّل التحديث ' + l.version + '…'); await install(id); } }
    } catch (e) { /* offline: play the copy we have */ }
  }
  if (g && g.type === 'manifest' && !running.has(id)) { splashStatus('يجيب حفظك من السحابة…'); try { await cloudPull(id); } catch (e) {} }
  splashStatus('جاري التشغيل');
  try { launchGame(id, edition); } catch (e) { closeSplash(); throw e; }
}
function launchGame(id, edition) {
  const g = installedNow()[id];
  if (!g) throw new Error('اللعبة مو مثبتة');
  if (g.type === 'bf-launcher') {
    // One launcher on disk serves every owned BLOCKFALL edition; the edition comes from the license written just before it starts.
    let cur = null; try { cur = JSON.parse(fs.readFileSync(path.join(BF_DATA, 'license.json'), 'utf8')); } catch (e) {}
    const curEd = cur && cur.ps ? cur.ed : (cur && cur.key ? 'own' : 'demo');
    if (curEd !== 'own' && curEd !== g.edition && process.platform === 'win32') { try { require('child_process').execSync('taskkill /IM BLOCKFALL3001.exe /F', { stdio: 'ignore' }); } catch (e) {} }
    writeBfLicense(g.edition, g.unlock);
    writeBfAccount();
    try { fs.rmSync(path.join(BF_DATA, 'premium_cmd.json'), { force: true }); } catch (e) {}
    const { spawn } = require('child_process');
    const child = spawn(BF_EXE, ['--premium'], { cwd: BF_DIR, detached: true, stdio: 'ignore' });
    // the starting window stays until BLOCKFALL shows up
    (async () => { const t0 = Date.now(); while (splashKey === id && Date.now() - t0 < 30000) { await new Promise(r => setTimeout(r, 700)); if (await bfAlive()) break; } if (splashKey === id) closeSplash(); })();
    child.unref();
    for (const k of [...running.keys()]) if ((readReg()[k] || {}).type === 'bf-launcher' && k !== id) markStopped(k);
    markRunning(id);
    // the launcher re-adds its own "BLOCKFALL 3001" shortcuts each start; put ours (one per edition) back
    setTimeout(fixBfShortcuts, 1500); setTimeout(fixBfShortcuts, 6000);
    return;
  }
  if (gameWins.has(id) && !gameWins.get(id).isDestroyed()) { const w = gameWins.get(id); w.show(); w.focus(); return; }
  const w = new BrowserWindow({
    width: 1600, height: 900, backgroundColor: '#000000', title: g.name, icon: path.join(gameDir(id, g), g.icon || 'icon.ico'),
    autoHideMenuBar: true, show: false,
    webPreferences: { preload: path.join(__dirname, 'game-preload.js'), contextIsolation: false, sandbox: false, nodeIntegration: false, backgroundThrottling: false }
  });
  w.setMenu(null);
  w.once('ready-to-show', () => { w.maximize(); w.show(); if (splashKey === id) closeSplash(); });
  w.webContents.setWindowOpenHandler(({ url }) => { openExternal(url); return { action: 'deny' }; });
  w.webContents.on('before-input-event', (e, input) => {
    if (input.type === 'keyDown' && input.key === 'F11') { w.setFullScreen(!w.isFullScreen()); e.preventDefault(); }
  });
  const q = g.edition === 'demo' ? '?demo=1' : '';
  w.loadURL(`psgame://${id}/${g.entry}${q}`);
  // the game page hands over its saved data as it unloads (game-preload.js); it goes to the account after the window closes
  w.on('closed', () => { gameWins.delete(id); const snap = lastSave.get(id); lastSave.delete(id);
    if (snap) { const p = cloudPush(id, snap).catch(() => {}); pushing.set(id, p); p.finally(() => pushing.delete(id)); }
    markStopped(id); });
  gameWins.set(id, w);
  markRunning(id);
}

// Installs from before editions were separate: give them their game id and an edition in the name and shortcut
function migrateReg() {
  try {
    const reg = readReg(); let changed = false;
    for (const [k, v] of Object.entries(reg)) {
      const base = v.base || (v.game ? String(v.name).replace(/ (Demo|Standard|Gold)$/, '') : v.name);
      const want = edTitle(base, v.edition);
      if (v.game && v.base && v.name === want) continue;
      removeShortcuts(v);
      v.game = v.game || k.split('--')[0]; v.base = base; v.name = want; changed = true;
      makeShortcuts(k, v);
    }
    if (changed) writeReg(reg).catch(() => {});
  } catch (e) {}
}
function fixBfShortcuts() {
  const own = Object.entries(installedNow()).filter(([, v]) => v.type === 'bf-launcher');
  try { fs.rmSync(path.join(app.getPath('appData'), 'Microsoft', 'Windows', 'Start Menu', 'Programs', 'BLOCKFALL 3001.lnk'), { force: true }); } catch (e) {}
  try { fs.rmSync(path.join(app.getPath('desktop'), 'BLOCKFALL 3001.lnk'), { force: true }); } catch (e) {}
  for (const [k, v] of own) makeShortcuts(k, v);
}

// BLOCKFALL's launcher reads its edition from %APPDATA%\.terravolt\license.json and keeps a local unlock while its
// fingerprint is listed in the launcher's shop.json. Premium Studio owners of Standard or Gold open it already unlocked.
function writeBfLicense(edition, unlock) {
  try {
    const file = path.join(BF_DATA, 'license.json');
    let cur = null; try { cur = JSON.parse(fs.readFileSync(file, 'utf8')); } catch (e) {}
    if (cur && cur.key && !cur.ps) return;                           // the player's own key or code: leave it alone
    const code = unlock && unlock[edition];
    if (!code || edition === 'demo') { if (cur && cur.ps) fs.rmSync(file, { force: true }); return; }
    fs.mkdirSync(BF_DATA, { recursive: true });
    fs.writeFileSync(file, JSON.stringify({ key: 'PREMIUM-STUDIO', code, local: true, ed: edition, checked: Date.now(), ps: true }, null, 1));
  } catch (e) {}
}

/* ---------------- install / uninstall ---------------- */
const busy = new Set();
function send(ch, payload) { if (mainWin && !mainWin.isDestroyed()) mainWin.webContents.send(ch, payload); }

async function fetchJSON(url) {
  const r = await net.fetch(url + (url.includes('?') ? '&' : '?') + 't=' + Date.now(), { cache: 'no-store' });
  if (!r.ok) throw new Error('HTTP ' + r.status + ' · ' + url);
  return r.json();
}
const fetchManifest = () => fetchJSON(SITE + 'games.json');

// Latest version each game's own update source offers right now.
async function latestOf(m) {
  if (m.type === 'bf-launcher') { const p = await fetchJSON(m.parts); let game = ''; try { game = (await fetchJSON(m.gameManifest)).latest.release; } catch (e) {} return { version: p.version, game, size: p.size }; }
  if (m.type === 'manifest') { const j = await fetchJSON(m.base + 'manifest.json'); return { version: j.version, files: j.files }; }
  return { version: m.version };
}
async function getLatest() {
  const man = await fetchManifest(), out = {};
  await Promise.all(Object.entries(man.games || {}).map(async ([id, m]) => { try { const l = await latestOf(m); out[id] = { version: l.version, game: l.game || '' }; } catch (e) {} }));
  return out;
}

// Stream a URL to disk (or memory) with progress.
async function download(url, onBytes, toFile) {
  const res = await net.fetch(url, { cache: 'no-store' });
  if (!res.ok || !res.body) throw new Error('فشل التحميل (' + res.status + ')');
  const reader = res.body.getReader(), parts = [];
  const out = toFile ? fs.createWriteStream(toFile) : null;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    onBytes(value.length);
    if (out) { if (!out.write(Buffer.from(value))) await new Promise(r => out.once('drain', r)); } else parts.push(Buffer.from(value));
  }
  if (out) { await new Promise((r, j) => out.end(err => err ? j(err) : r())); return null; }
  return Buffer.concat(parts);
}

async function install(id, opts) {
  opts = opts || {};
  if (!validId(id)) throw new Error('لعبة غير معروفة');
  if (busy.has(id)) return;
  busy.add(id);
  let stage = null, tmpZip = null;
  try {
    const info = keyInfo(id), man = await fetchManifest(), m = man.games && man.games[info.game];
    if (!m) throw new Error('اللعبة مو موجودة في القائمة');
    const edition = info.edition;
    let received = 0, last = 0, total = 0;
    const tick = n => { received += n; const now = Date.now(); if (now - last > 150) { last = now; send('ps:progress', { id, phase: 'download', received, total: total || received }); } };
    let entry = m.entry, version;

    if (m.type === 'bf-launcher' && fs.existsSync(BF_EXE) && !readReg()[id] && !opts.force) {
      // another BLOCKFALL edition already put the launcher on this PC: this edition uses the same one
      version = (Object.values(installedNow()).find(v => v.type === 'bf-launcher') || {}).version;
      entry = 'BLOCKFALL3001.exe';
    } else if (m.type === 'bf-launcher') {
      // BLOCKFALL's official launcher package: encrypted parts on its CDN, unlocked with the studio's access code.
      const P = await fetchJSON(m.parts);
      if (P.enc && !opts.code) throw new Error('تحميل اللانشر يحتاج كود، تواصل مع الاستوديو'); total = P.zipSize || P.size; version = P.version;
      send('ps:progress', { id, phase: 'download', received: 0, total });
      const base = m.site || new URL('..', m.parts).toString();   // part URLs are relative to the game's site root
      const chunks = [];
      for (const part of P.parts) chunks.push(await download(new URL(part.url, base).toString(), tick));
      send('ps:progress', { id, phase: 'extract', received, total: received });
      const zip = P.enc ? decryptBf(Buffer.concat(chunks), P, opts.code) : Buffer.concat(chunks);
      if (P.sha1 && crypto.createHash('sha1').update(zip).digest('hex') !== P.sha1) throw new Error('الملف وصل ناقص، جرّب مرة ثانية');
      tmpZip = path.join(app.getPath('temp'), `ps-${id}-${Date.now()}.zip`);
      await fsp.writeFile(tmpZip, zip);
      stage = path.join(LOCAL, 'Programs', 'TERRAVOLT.new');
      await fsp.rm(stage, { recursive: true, force: true });
      await require('extract-zip')(tmpZip, { dir: stage });
      const inner = path.join(stage, m.zipRoot || '');
      await fsp.rm(BF_DIR, { recursive: true, force: true });
      await fsp.rename(inner, BF_DIR);
      await fsp.rm(stage, { recursive: true, force: true }); stage = null;
      entry = 'BLOCKFALL3001.exe';
    } else if (m.type === 'manifest') {
      // The game's own update channel: a manifest plus loose files (same source the game's standalone app uses).
      const j = await fetchJSON(m.base + 'manifest.json'); version = j.version; total = m.installSize || 0;
      send('ps:progress', { id, phase: 'download', received: 0, total });
      stage = gameDir(id) + '.new';
      await fsp.rm(stage, { recursive: true, force: true });
      for (const f of j.files) {
        if (typeof f !== 'string' || f.includes('..') || path.isAbsolute(f)) throw new Error('ملف غير صالح في التحديث');
        const dest = path.join(stage, f);
        await fsp.mkdir(path.dirname(dest), { recursive: true });
        await download(m.base + f.split('/').map(encodeURIComponent).join('/'), tick, dest);
      }
      if (m.icon) await download(new URL(m.icon, SITE).toString(), () => {}, path.join(stage, 'icon.ico')).catch(() => {});
      send('ps:progress', { id, phase: 'extract', received, total: received });
      await fsp.rm(gameDir(id), { recursive: true, force: true });
      await fsp.rename(stage, gameDir(id)); stage = null;
    } else {
      throw new Error('نوع اللعبة غير معروف');
    }

    const reg = readReg();
    if (m.type === 'bf-launcher') for (const v of Object.values(reg)) if (v.type === 'bf-launcher') v.version = version;
    reg[id] = { type: m.type, game: info.game, base: m.name, name: edTitle(m.name, edition), version, entry, demoPatch: Array.isArray(m.demoPatch) ? m.demoPatch : undefined, account: m.account || undefined, icon: m.type === 'bf-launcher' ? 'resources/app/icon.ico' : 'icon.ico', edition,
      unlock: m.type === 'bf-launcher' ? (m.editionUnlock || {}) : undefined, size: await dirSize(gameDir(id, m)), installedAt: Date.now() };
    await writeReg(reg);
    makeShortcuts(id, reg[id]);
    send('ps:progress', { id, phase: 'done' });
    send('ps:installed-changed', {}); updateTray();
  } catch (e) {
    if (stage) await fsp.rm(stage, { recursive: true, force: true }).catch(() => {});
    send('ps:progress', { id, phase: 'error', error: e.message || String(e) });
    throw e;
  } finally {
    busy.delete(id);
    if (tmpZip) fsp.rm(tmpZip, { force: true }).catch(() => {});
  }
}

function decryptBf(all, P, code) {
  const b64 = s => Buffer.from(s, 'base64');
  let key = null;
  for (const k of P.enc.keys) {
    try {
      const kek = crypto.pbkdf2Sync(Buffer.from(String(code)), b64(k.salt), P.enc.iter, 32, 'sha256');
      const wk = b64(k.wk), d = crypto.createDecipheriv('aes-256-gcm', kek, b64(k.iv));
      d.setAuthTag(wk.subarray(wk.length - 16));
      key = Buffer.concat([d.update(wk.subarray(0, wk.length - 16)), d.final()]); break;
    } catch (e) {}
  }
  if (!key) throw new Error('كود التحميل غير صحيح');
  const d = crypto.createDecipheriv('aes-256-gcm', key, b64(P.enc.iv));
  d.setAuthTag(all.subarray(all.length - 16));
  return Buffer.concat([d.update(all.subarray(0, all.length - 16)), d.final()]);
}

async function uninstall(id) {
  if (!validId(id)) throw new Error('لعبة غير معروفة');
  const w = gameWins.get(id); if (w && !w.isDestroyed()) w.close();
  const reg = readReg(), g = reg[id];
  const otherBf = g && g.type === 'bf-launcher' && Object.entries(reg).some(([k, v]) => k !== id && v.type === 'bf-launcher');
  if (otherBf) { /* the launcher stays for the other BLOCKFALL edition(s) */ }
  else if (g && g.type === 'bf-launcher') {
    try { require('child_process').execSync('taskkill /IM BLOCKFALL3001.exe /F', { stdio: 'ignore' }); } catch (e) {}
    await fsp.rm(BF_DIR, { recursive: true, force: true });
    for (const p of [path.join(app.getPath('desktop'), 'BLOCKFALL 3001.lnk'), path.join(app.getPath('appData'), 'Microsoft', 'Windows', 'Start Menu', 'Programs', 'BLOCKFALL 3001.lnk')]) { try { fs.rmSync(p, { force: true }); } catch (e) {} }
    try { const f = path.join(BF_DATA, 'license.json'); const L = JSON.parse(fs.readFileSync(f, 'utf8')); if (L.ps) fs.rmSync(f, { force: true }); } catch (e) {}
  } else {
    await fsp.rm(gameDir(id), { recursive: true, force: true });
  }
  if (g) removeShortcuts(g);
  delete reg[id]; await writeReg(reg);
  send('ps:installed-changed', {}); updateTray();
}

async function dirSize(dir) {
  let total = 0;
  for (const ent of await fsp.readdir(dir, { withFileTypes: true })) {
    const p = path.join(dir, ent.name);
    total += ent.isDirectory() ? await dirSize(p) : (await fsp.stat(p)).size;
  }
  return total;
}

function shortcutPaths(g) {
  const safe = String(g.name).replace(/[\\/:*?"<>|]/g, '').trim() || 'Game';
  return [
    path.join(app.getPath('desktop'), safe + '.lnk'),
    path.join(app.getPath('appData'), 'Microsoft', 'Windows', 'Start Menu', 'Programs', 'Premium Studio', safe + '.lnk')
  ];
}
function makeShortcuts(id, g) {
  if (process.platform !== 'win32') return;
  for (const p of shortcutPaths(g)) {
    try {
      fs.mkdirSync(path.dirname(p), { recursive: true });
      shell.writeShortcutLink(p, 'create', {
        target: process.execPath, args: `--launch=${id}`, cwd: path.dirname(process.execPath),
        icon: path.join(gameDir(id, g), g.icon || 'icon.ico'), iconIndex: 0, description: g.name + ' — Premium Studio'
      });
    } catch (e) {}
  }
}
function removeShortcuts(g) { for (const p of shortcutPaths(g)) { try { fs.rmSync(p, { force: true }); } catch (e) {} } }

/* ---------------- game file protocol ---------------- */
function registerGameProtocol() {
  protocol.handle('psgame', req => {
    const u = new URL(req.url), id = u.hostname;
    if (!validId(id)) return new Response('bad game', { status: 400 });
    const root = gameDir(id);
    const rel = decodeURIComponent(u.pathname).replace(/^\/+/, '') || 'index.html';
    const file = path.normalize(path.join(root, rel));
    if (!file.startsWith(root + path.sep)) return new Response('forbidden', { status: 403 });
    const g = readReg()[id];
    if (g && rel === g.entry && ((u.searchParams.get('demo') === '1' && g.demoPatch) || g.account)) {
      let html = fs.readFileSync(file, 'utf8');
      // Demo owners: the game's own demo switch, applied to the entry page as it's served
      if (u.searchParams.get('demo') === '1' && g.demoPatch) html = html.split(g.demoPatch[0]).join(g.demoPatch[1]);
      // Play under the Premium Studio account: the game's saved player name is the username, and its name box is locked
      if (g.account) html = html.replace(/<head[^>]*>/i, m => m + accountScript(g.account));
      return new Response(html, { headers: { 'content-type': 'text/html; charset=utf-8' } });
    }
    return net.fetch(pathToFileURL(file).toString());
  });
}

function accountScript(a) {
  const J = v => JSON.stringify(String(v || ''));
  return `<script>(function(){var u=window.PREMIUM_STUDIO&&window.PREMIUM_STUDIO.user;if(!u||!u.username)return;
try{var k=${J(a.store)},o=JSON.parse(localStorage.getItem(k)||'{}');o[${J(a.nickKey || 'nick')}]=u.username;localStorage.setItem(k,JSON.stringify(o));}catch(e){}
var f=${J(a.nickField)};if(!f)return;var lock=function(){var i=document.getElementById(f);if(i&&!i.readOnly){i.readOnly=true;i.value=u.username;i.title='Premium Studio';i.style.opacity='.75';}};
new MutationObserver(lock).observe(document.documentElement,{childList:true,subtree:true});})();</script>`;
}

/* ---------------- IPC ---------------- */
function guard(fn) {
  return async (e, ...args) => {
    if (!isTrusted(e.senderFrame && e.senderFrame.url)) throw new Error('غير مسموح');
    return fn(...args);
  };
}
ipcMain.handle('ps:getInstalled', guard(async () => installedNow()));
ipcMain.handle('ps:install', guard(async (id, opts) => { await install(id, opts); return true; }));
ipcMain.handle('ps:getLatest', guard(async () => getLatest()));
ipcMain.handle('ps:uninstall', guard(async id => { await uninstall(id); return true; }));
ipcMain.handle('ps:launch', guard(async id => { await launchWithUpdate(id); return true; }));
ipcMain.handle('ps:features', guard(async () => ({ editions: true, running: true })));
ipcMain.handle('ps:getRunning', guard(async () => runningList()));
ipcMain.handle('ps:stop', guard(async id => { if (!validId(id)) throw new Error('bad id'); stopGame(id); return true; }));
ipcMain.handle('ps:showFolder', guard(async id => { if (validId(id)) shell.openPath(gameDir(id, readReg()[id])); return true; }));
ipcMain.handle('ps:setUser', guard(async u => { await setUser(u); return true; }));
ipcMain.handle('ps:retryOnline', guard(async () => { if (mainWin) mainWin.loadURL(SITE); return true; }));
// Game achievements: a game calls PREMIUM_STUDIO.achieve('id'); it's kept on the player's account
ipcMain.on('game:achieve', (e, aid) => {
  const key = [...gameWins].find(([, w]) => !w.isDestroyed() && w.webContents === e.sender);
  if (!key || !currentUser || !currentUser.uid || typeof aid !== 'string' || !/^[a-z0-9_-]{1,40}$/i.test(aid)) return;
  const game = (readReg()[key[0]] || {}).game || key[0].split('--')[0];
  fbCommit([{ update: { name: `${FB_DB}/users/${currentUser.uid}/achievements/${game}__${aid}`, fields: { game: { stringValue: game }, id: { stringValue: aid } } },
    currentDocument: { exists: false }, updateTransforms: [{ fieldPath: 'at', setToServerValue: 'REQUEST_TIME' }] }]);
});
ipcMain.on('game:exit', e => { const w = BrowserWindow.fromWebContents(e.sender); if (w) w.close(); });
ipcMain.on('game:nick', e => { e.returnValue = playerName(); });
ipcMain.on('game:user', e => { e.returnValue = currentUser && currentUser.username ? { username: currentUser.username, name: currentUser.name, pid: currentUser.pid, email: currentUser.email } : null; });
ipcMain.on('app:version', e => { e.returnValue = VERSION_LABEL; });

/* ---------------- deep links + startup ---------------- */
function argValue(argv, name) { const a = argv.find(x => x.startsWith(`--${name}=`)); return a ? a.split('=')[1] : null; }
function handleDeepLink(url) {
  // premiumstudio://game/<id>  → open store page;  premiumstudio://launch/<id> → start the game
  try {
    const u = new URL(url), kind = u.hostname, id = u.pathname.replace(/^\/+/, '').split('/')[0];
    if (kind === 'launch' && validId(id) && installedNow()[id]) return playFromOutside(id);
    if (kind === 'install' && validId(id)) return createMain(id.split('--')[0], 'install=' + encodeURIComponent(id));
    createMain(validId(id) ? id : (kind === 'library' ? 'library' : ''));
  } catch (e) { createMain(); }
}
function handleArgv(argv, firstRun) {
  const launch = argValue(argv, 'launch');
  const link = argv.find(a => a.startsWith('premiumstudio://'));
  if (launch && validId(launch) && installedNow()[launch]) { playFromOutside(launch); return true; }
  if (link) { handleDeepLink(link); return true; }
  if (firstRun && argv.includes('--hidden')) return true;   // started with Windows: stay in the tray
  createMain(); return true;
}

if (!app.requestSingleInstanceLock()) {
  app.quit();
} else {
  if (process.defaultApp && process.argv.length >= 2) app.setAsDefaultProtocolClient('premiumstudio', process.execPath, [path.resolve(process.argv[1])]);
  else app.setAsDefaultProtocolClient('premiumstudio');
  app.setAppUserModelId('com.premiumstudio.client');
  app.on('second-instance', (e, argv) => handleArgv(argv, false));
  app.whenReady().then(() => {
    Menu.setApplicationMenu(null);
    registerGameProtocol();
    migrateReg();
    makeTray();
    handleArgv(process.argv, true);
    // start with Windows, quietly in the tray (like Steam)
    if (app.isPackaged && process.platform === 'win32') { try { app.setLoginItemSettings({ openAtLogin: true, args: ['--hidden'] }); } catch (e) {} }
    if (app.isPackaged) {
      try {
        const { autoUpdater } = require('electron-updater');
        autoUpdater.autoDownload = true;
        autoUpdater.checkForUpdatesAndNotify().catch(() => {});
      } catch (e) {}
    }
  });
  app.on('window-all-closed', () => { if (!tray) app.quit(); });   // otherwise keep running in the tray
  app.on('before-quit', () => { quitting = true; for (const k of [...running.keys()]) if (!gameWins.has(k)) { const r = running.get(k); addPlaytime(r, Date.now() - r.since); } });
}
