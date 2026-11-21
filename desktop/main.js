'use strict';
// Premium Games desktop client: hosts the store, installs / launches / removes games.
const { app, BrowserWindow, ipcMain, shell, protocol, net, Menu, Tray, nativeImage } = require('electron');
const path = require('path');
const fs = require('fs');
const fsp = fs.promises;
const crypto = require('crypto');
const zlib = require('zlib');
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
// Console mode is its own system, like a PS5: what is installed from it lives in its own folder and list, shows only in
// console mode, runs only from it (no desktop or Start-menu icons, nothing in the PC library or the tray) and is removed
// only from it. Inside the app a console install's key ends in "--console"; the store page always sees plain keys,
// for the system that is open (fromPage / plainKey).
const CON_DIR = path.join(LOCAL, 'PremiumStudio', 'console', 'games');
const CON_REG = path.join(CON_DIR, 'installed.json');
const CON = '--console';
const isCon = k => typeof k === 'string' && k.endsWith(CON);
const plainKey = k => isCon(k) ? k.slice(0, -CON.length) : k;
const EDITIONS = ['demo', 'std', 'gold', 'dev'];

// Where each kind of game lives on disk.
// bf-launcher: BLOCKFALL's own Windows launcher, in the folder it expects (it updates itself and the game from its CDN).
// BLOCKFALL is one game: Demo, Standard and Gold share one install and the player picks the edition inside its launcher.
// The Developer edition is its own program (its own folder, data and icon), only for players who own it.
const BFV = {
  blockfall: { dir: 'TERRAVOLT', exe: 'BLOCKFALL3001.exe', data: '.terravolt', name: 'BLOCKFALL 3001' },
  'blockfall-dev': { dir: 'BLOCKFALL3001-Dev', exe: 'BLOCKFALL3001-Dev.exe', data: '.terravolt-dev', name: 'BLOCKFALL 3001 المطور' }
};
function bfv(game) { const v = BFV[game] || BFV.blockfall, dir = path.join(LOCAL, 'Programs', v.dir);
  return { dir, exe: path.join(dir, v.exe), image: v.exe, data: path.join(app.getPath('appData'), v.data), name: v.name }; }
// install keys from older versions ("blockfall--gold", "blockfall--dev") → the one install key
function bfKey(k) { if (isCon(k)) return k; const [g, e] = String(k || '').split('--'); if (!BFV[g]) return k; return g === 'blockfall' && e === 'dev' ? 'blockfall-dev' : g; }
const BF_DIR = bfv('blockfall').dir, BF_EXE = bfv('blockfall').exe, BF_DATA = bfv('blockfall').data;
function gameDir(id, m) { if (isCon(id)) return path.join(CON_DIR, plainKey(id)); return m && m.type === 'bf-launcher' ? bfv(m.game || bfKey(id)).dir : path.join(GAMES_DIR, id); }
// Every owned edition installs as its own game. Install key = "<game>--<edition>" (older installs keep the plain game id).
const ED_LABEL = { demo: 'Demo', std: 'Standard', gold: 'Gold', dev: 'المبرمج' };
const edTitle = (name, ed) => name + (ED_LABEL[ed] ? ' ' + ED_LABEL[ed] : '');
function keyInfo(key) {
  const r = readReg()[key];
  if (r) return { game: r.game || key.split('--')[0], edition: r.edition };
  const [game, edition] = plainKey(key).split('--');
  return { game, edition: EDITIONS.includes(edition) ? edition : 'demo' };
}

// one list for the app: the PC's installs, and the console's with "--console" keys (each system keeps its own file)
const readJSON = f => { try { const o = JSON.parse(fs.readFileSync(f, 'utf8')); return o && typeof o === 'object' ? o : {}; } catch (e) { return {}; } };
function readReg() { const reg = readJSON(REG_FILE); for (const [k, v] of Object.entries(readJSON(CON_REG))) reg[k + CON] = v; return reg; }
function splitReg(reg) { const pc = {}, con = {}; for (const [k, v] of Object.entries(reg)) (isCon(k) ? con : pc)[plainKey(k)] = v; return { pc, con }; }
async function writeReg(reg) { const { pc, con } = splitReg(reg);
  await fsp.mkdir(GAMES_DIR, { recursive: true }); await fsp.writeFile(REG_FILE, JSON.stringify(pc, null, 2));
  if (Object.keys(con).length || fs.existsSync(CON_REG)) { await fsp.mkdir(CON_DIR, { recursive: true }); await fsp.writeFile(CON_REG, JSON.stringify(con, null, 2)); } }
function writeRegSync(reg) { const { pc, con } = splitReg(reg);
  fs.mkdirSync(GAMES_DIR, { recursive: true }); fs.writeFileSync(REG_FILE, JSON.stringify(pc, null, 2));
  if (Object.keys(con).length || fs.existsSync(CON_REG)) { fs.mkdirSync(CON_DIR, { recursive: true }); fs.writeFileSync(CON_REG, JSON.stringify(con, null, 2)); } }
function installedNow() {
  const reg = readReg(), out = {};
  for (const [id, v] of Object.entries(reg)) {
    const ok = v.type === 'bf-launcher' ? fs.existsSync(bfv(v.game || id).exe) : fs.existsSync(path.join(gameDir(id, v), v.entry || 'index.html'));
    if (ok) out[id] = { ...v, game: v.game || id.split('--')[0] };
  }
  return out;
}
// what one system has installed, with the keys its page uses
function installedFor(con) { const out = {}; for (const [k, v] of Object.entries(installedNow())) if (isCon(k) === !!con) out[plainKey(k)] = v; return out; }
// a key from the store page → the install key in the system that is open (the console's own, or the PC's)
const fromPage = id => { if (typeof id !== 'string') return id; const k = bfKey(plainKey(id)); return consoleOn ? k + CON : k; };

/* ---------------- the signed-in Premium Games account (games play under it) ---------------- */
const USER_FILE = path.join(app.getPath('userData'), 'ps-user.json');
let currentUser = (() => { try { return JSON.parse(fs.readFileSync(USER_FILE, 'utf8')); } catch (e) { return null; } })();
async function setUser(u) {
  const clip = (v, n) => String(v || '').slice(0, n);
  const before = currentUser && currentUser.uid;
  const next = u && typeof u === 'object' ? { username: clip(u.username, 20), name: clip(u.name, 32), pid: clip(u.pid, 10), email: clip(u.email, 120), uid: clip(u.uid, 128), refreshToken: clip(u.refreshToken, 4096) } : null;
  // switched or signed out (like Steam): games running under the old account close first, saving to the old account
  if (before && before !== (next && next.uid)) await closeRunningGames();
  if (next && currentUser && currentUser.uid === next.uid && currentUser.owned) next.owned = currentUser.owned;   // keep what they own until the store sends it again
  currentUser = next;
  try { if (currentUser) fs.writeFileSync(USER_FILE, JSON.stringify(currentUser)); else fs.rmSync(USER_FILE, { force: true }); } catch (e) {}
  writeBfAccount();
  updateTray();
}
function closeRunningGames() {
  if (process.platform === 'win32') for (const g of Object.keys(BFV)) { try { require('child_process').execSync(`taskkill /IM ${bfv(g).image} /F`, { stdio: 'ignore' }); } catch (e) {} }
  for (const k of [...running.keys()]) if (!gameWins.has(k)) markStopped(k);
  return Promise.all([...gameWins.keys()].map(closeWebGame));
}
const playerName = () => (currentUser && (currentUser.username || currentUser.name)) || '';
// BLOCKFALL's launcher: the Premium Games account it should play under
// owned.blockfall = the editions this launcher may open (Demo/Standard/Gold for BLOCKFALL, Developer for its own launcher)
function writeBfAccount() {
  const reg = readReg();
  for (const g of Object.keys(BFV)) {
    try {
      const V = bfv(g), f = path.join(V.data, 'premium_account.json');
      if (!currentUser || !currentUser.username) { fs.rmSync(f, { force: true }); continue; }
      const o = currentUser.owned || {}, main = (o.blockfall || []).filter(e => e !== 'dev');
      const eds = g === 'blockfall' ? main : (((o['blockfall-dev'] || []).length || (o.blockfall || []).includes('dev')) ? ['dev'] : []);
      if (!fs.existsSync(V.data)) { if (!reg[g]) continue; fs.mkdirSync(V.data, { recursive: true }); }
      fs.writeFileSync(f, JSON.stringify({ ...currentUser, owned: { ...o, blockfall: eds }, source: 'premium-studio', at: Date.now() }, null, 1));
    } catch (e) {}
  }
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
    backgroundColor: '#0a0d14', title: 'Premium Games', icon: ICON, autoHideMenuBar: true, show: false,
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
  mainWin.on('closed', () => { mainWin = null; consoleOn = false; syncPadWatch(); });
  mainWin.webContents.on('did-navigate', () => { consoleOn = false; syncPadWatch(); });   // a new page: the console screen says again when it opens
  mainWin.webContents.on('did-finish-load', () => mainWin && mainWin.webContents.send('ps:running', runningList()));
  return mainWin;
}
/* ---------------- the app used to be called "Premium Studio": carry everything over to "Premium Games" ----------------
   Same install, account and games; only the name changes. The old desktop / Start-menu icons are renamed (never two icons),
   and the screenshot, clip and game-shortcut folders keep their files. */
function renameLegacy() {
  if (process.platform !== 'win32') return;
  const H = app.getPath('home'), AD = app.getPath('appData');
  const desk = [app.getPath('desktop'), path.join(process.env.PUBLIC || 'C:\\Users\\Public', 'Desktop')];
  const menu = path.join(AD, 'Microsoft', 'Windows', 'Start Menu', 'Programs');
  const pairs = desk.map(d => [path.join(d, 'Premium Studio.lnk'), path.join(d, 'Premium Games.lnk')]).concat([[path.join(menu, 'Premium Studio.lnk'), path.join(menu, 'Premium Games.lnk')]]);
  for (const [o, n] of pairs) { try { if (!fs.existsSync(o)) continue;
      if (fs.existsSync(n)) fs.rmSync(o, { force: true });
      else { const d = shell.readShortcutLink(o); shell.writeShortcutLink(n, 'create', Object.assign({}, d, { description: 'Premium Games' })); fs.rmSync(o, { force: true }); } } catch (e) {} }
  for (const [o, n] of [[path.join(app.getPath('pictures'), 'Premium Studio'), path.join(app.getPath('pictures'), 'Premium Games')],
                        [path.join(app.getPath('videos'), 'Premium Studio'), path.join(app.getPath('videos'), 'Premium Games')],
                        [path.join(menu, 'Premium Studio'), path.join(menu, 'Premium Games')]]) {
    try { if (!fs.existsSync(o)) continue; if (!fs.existsSync(n)) { fs.renameSync(o, n); continue; }
      for (const f of fs.readdirSync(o)) { const a = path.join(o, f), b = path.join(n, f); if (!fs.existsSync(b)) fs.renameSync(a, b); }
      fs.rmSync(o, { recursive: true, force: true }); } catch (e) {} }
  void H;
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
    updateMask: { fieldPaths: ['game', 'gid'] }, updateTransforms: [{ fieldPath: 'at', setToServerValue: 'REQUEST_TIME' }] }]);
}
setInterval(() => { if (running.size) pushPresence(); }, 4 * 60000);
// play time per game, kept on the account
function addPlaytime(r, ms) {
  const min = Math.min(1440, Math.round(ms / 60000));
  if (!currentUser || !currentUser.uid || !r.game) return;
  return fbCommit([{ update: { name: `${FB_DB}/users/${currentUser.uid}/play/${r.game}`, fields: { game: { stringValue: r.game } } }, updateMask: { fieldPaths: ['game'] },
    updateTransforms: [{ fieldPath: 'minutes', increment: { integerValue: String(min) } }, { fieldPath: 'last', setToServerValue: 'REQUEST_TIME' }] }]);
}
const runningList = () => Object.fromEntries([...running].filter(([k]) => isCon(k) === consoleOn).map(([k, v]) => [plainKey(k), { since: v.since, game: v.game, closing: stopping.has(k) || !!v.closing }]));
function runningChanged() {
  syncPadWatch();
  if (mainWin && !mainWin.isDestroyed()) mainWin.webContents.send('ps:running', runningList());
  updateTray();
}
function markRunning(key) {
  if (running.has(key)) return;
  const g = readReg()[key] || {};
  running.set(key, { since: Date.now(), game: g.game || key.split('--')[0], name: g.name || key, console: isCon(key) });
  pushPresence(); runningChanged();
  if (g.type === 'bf-launcher') { watchBf(); gameKeys(true); }
}
function markStopped(key) {
  const r = running.get(key); if (!r) return;
  if (r.console) { const mins = Math.round((Date.now() - r.since) / 60000); csLog('game-stop', { game: r.game, key, minutes: mins }); csGame(r.game, x => ({ minutes: x.minutes + mins })); }
  running.delete(key);
  addPlaytime(r, Date.now() - r.since); pushPresence(); runningChanged();
  if (ovKey === key) closeOverlay();
  if (recKey === key && recWin && !recWin.isDestroyed()) recWin.webContents.send('rec:stop');
  if (![...running.keys()].some(k => ['bf-launcher', 'pge'].includes((readReg()[k] || {}).type))) gameKeys(false);
}
// BLOCKFALL runs as its own program: watch its process
let bfTimer = null;
function bfAlive(game) {
  const V = bfv(game || 'blockfall');
  // newer launchers say whether a window is open (it may stay in the tray after you close it)
  try { const st = JSON.parse(fs.readFileSync(path.join(V.data, 'premium_state.json'), 'utf8'));
    if (Date.now() - st.at < 15000) return Promise.resolve(!!st.open);
    if (Date.now() - st.at < 600000) return Promise.resolve(false);   // it was reporting and stopped: closed
  } catch (e) {}
  return new Promise(res => {
    if (process.platform !== 'win32') return res(false);
    require('child_process').execFile('tasklist', ['/FI', 'IMAGENAME eq ' + V.image, '/NH'], { windowsHide: true }, (e, out) => res(!e && String(out || '').toLowerCase().includes(V.image.toLowerCase())));
  });
}
function watchBf() {
  if (bfTimer) return;
  const misses = {};
  bfTimer = setInterval(async () => {
    const bfKeys = [...running.keys()].filter(k => (readReg()[k] || {}).type === 'bf-launcher');
    if (!bfKeys.length) { clearInterval(bfTimer); bfTimer = null; return; }
    for (const k of bfKeys) {
      if (stopping.has(k)) continue; const game = (readReg()[k] || {}).game || k, alive = await bfAlive(game);
      if (alive) { misses[k] = 0; continue; }
      if (Date.now() - running.get(k).since < 45000) continue;   // still starting (or updating itself)
      if ((misses[k] = (misses[k] || 0) + 1) >= 3) { markStopped(k); delete misses[k]; }
    }
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
    const V = bfv(g.game || key);
    try { fs.writeFileSync(path.join(V.data, 'premium_cmd.json'), JSON.stringify({ cmd: 'close', at: Date.now() })); } catch (e) {}
    const t0 = Date.now();
    while (Date.now() - t0 < 25000) { await new Promise(r => setTimeout(r, 1000)); if (!(await bfAlive(g.game || key))) break; }
    if (process.platform === 'win32') { try { require('child_process').execSync(`taskkill /IM ${V.image} /F`, { stdio: 'ignore' }); } catch (e) {} }
    try { fs.rmSync(path.join(V.data, 'premium_cmd.json'), { force: true }); } catch (e) {}
    stopping.delete(key); markStopped(key); return;
  }
  const c = pgeProcs.get(key);
  if (c && c.exitCode === null) {   // asked to close (the game saves as it quits); after 15 s it's closed
    if (stopping.has(key)) return; stopping.add(key); runningChanged();
    const done = new Promise(r => c.once('exit', r));
    if (process.platform === 'win32') require('child_process').execFile('taskkill', ['/PID', String(c.pid)], { windowsHide: true }, () => {}); else c.kill('SIGTERM');
    if (await Promise.race([done.then(() => true), new Promise(r => setTimeout(() => r(false), 15000))]) === false) { try { c.kill('SIGKILL'); } catch (e) {} await Promise.race([done, new Promise(r => setTimeout(r, 3000))]); }
    stopping.delete(key); markStopped(key); return;
  }
  const w = gameWins.get(key);
  if (w && !w.isDestroyed()) { const r = running.get(key); if (r) r.closing = true; runningChanged(); await closeWebGame(key); } else markStopped(key);
}

async function quitApp() {
  if (running.size) {
    const { dialog } = require('electron');
    const r = await dialog.showMessageBox({ type: 'question', title: 'Premium Games', icon: ICON, message: 'فيه لعبة شغالة', detail: [...running.values()].map(x => x.name).join('، ') + '\nتنقفل اللعبة (بعد ما تحفظ) ويطلع Premium Games.', buttons: ['أقفلها واطلع', 'إلغاء'], defaultId: 0, cancelId: 1 });
    if (r.response !== 0) return;
    await Promise.all([...running.keys()].map(k => stopGame(k)));
  }
  quitting = true; app.quit();
}
/* ---------------- tray: the app keeps running in the background, like Steam ---------------- */
let tray = null, quitting = false;
function updateTray() {
  if (!tray) return;
  const games = Object.entries(installedFor(false));
  const items = [
    { label: 'المتجر', click: () => createMain('store') },
    { label: 'المكتبة', click: () => createMain('library') },
    { label: 'الأصدقاء', click: () => createMain('friends') },
    { label: 'الشاشة الكبيرة', click: () => createMain('bigpicture') },
    { type: 'separator' }
  ];
  if (running.size) for (const [k, r] of [...running].filter(([k]) => !isCon(k))) items.push({ label: `■ إغلاق ${r.name}`, click: () => stopGame(k) });
  else for (const [k, v] of games.slice(0, 8)) items.push({ label: v.name, click: () => playFromOutside(k) });
  items.push({ type: 'separator' }, { label: 'خروج من Premium Games', click: () => quitApp() });
  tray.setContextMenu(Menu.buildFromTemplate(items));
  tray.setToolTip(running.size ? 'Premium Games — ' + [...running.values()].map(r => r.name).join('، ') : 'Premium Games');
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
  if (isCon(key)) { createMain('bigpicture'); return; }   // the console's games start only from console mode
  if (!currentUser || !currentUser.uid) { createMain(); return; }
  launchWithUpdate(key).catch(() => createMain());
}

function openExternal(url) { if (/^https?:\/\//.test(url)) shell.openExternal(url); }

// Before starting a game that updates from its own manifest, pull the newest version (like the game's standalone app does).
/* ---------------- screenshots (F12) and the in-game overlay (Shift+Tab), like Steam ---------------- */
const SHOTS = () => path.join(app.getPath('pictures'), 'Premium Games');
const gameFolder = key => { const g = readReg()[key] || {}; return String(g.base || g.name || key).replace(/[\\/:*?"<>|]/g, '').trim() || 'Game'; };
async function screenshot(key) {
  try {
    let img = null;
    const w = gameWins.get(key);
    if (w && !w.isDestroyed()) img = await w.webContents.capturePage();
    else {   // a game in its own program (BLOCKFALL): the screen it's on
      const { desktopCapturer, screen } = require('electron'), d = screen.getPrimaryDisplay();
      const src = (await desktopCapturer.getSources({ types: ['screen'], thumbnailSize: { width: d.size.width * d.scaleFactor, height: d.size.height * d.scaleFactor } }))[0];
      img = src && src.thumbnail;
    }
    if (!img || img.isEmpty()) return null;
    const dir = path.join(SHOTS(), gameFolder(key)); await fsp.mkdir(dir, { recursive: true });
    const t = new Date(), pad = n => String(n).padStart(2, '0');
    const f = path.join(dir, `${t.getFullYear()}-${pad(t.getMonth() + 1)}-${pad(t.getDate())} ${pad(t.getHours())}.${pad(t.getMinutes())}.${pad(t.getSeconds())}.png`);
    await fsp.writeFile(f, img.toPNG());
    flash('📸 انحفظت اللقطة'); if (consoleOn) csLog('screenshot', { game: (running.get(key) || {}).game || key, file: path.basename(f) });
    return f;
  } catch (e) { return null; }
}
ipcMain.handle('ps:screenshots', guard(async gid => {
  const out = [];
  for (const [k, v] of Object.entries(installedNow())) { if ((v.game || k.split('--')[0]) !== gid) continue;
    const dir = path.join(SHOTS(), gameFolder(k)); let files = []; try { files = (await fsp.readdir(dir)).filter(f => f.endsWith('.png')); } catch (e) {}
    for (const f of files) out.push(path.join(dir, f)); }
  const uniq = [...new Set(out)].sort().reverse().slice(0, 24);
  const { nativeImage } = require('electron');
  return uniq.map(f => { try { return { file: f, name: path.basename(f, '.png'), thumb: nativeImage.createFromPath(f).resize({ width: 360 }).toDataURL() }; } catch (e) { return null; } }).filter(Boolean);
}));
ipcMain.handle('ps:openShots', guard(async file => { if (typeof file === 'string' && path.resolve(file).startsWith(SHOTS())) shell.showItemInFolder(file); else { await fsp.mkdir(SHOTS(), { recursive: true }); shell.openPath(SHOTS()); } return true; }));

// game recording (F9 starts, F9 again saves), like Steam's — saved to Videos\Premium Games\<game>
let recWin = null, recKey = '', recT = null, recLive = false, recStopAsked = false;
const CLIPS = () => path.join(app.getPath('videos'), 'Premium Games');
async function toggleRecord(key) {
  // F9 while the recorder is still starting must stop it right after it starts (not start a second one)
  if (recKey) { if (recLive) { if (recWin && !recWin.isDestroyed()) recWin.webContents.send('rec:stop'); } else recStopAsked = true; return; }
  recKey = key; recLive = false; recStopAsked = false;
  try {
    let sourceId = '';
    const w = gameWins.get(key);
    if (w && !w.isDestroyed()) sourceId = w.getMediaSourceId();
    else { const { desktopCapturer } = require('electron'); const src = (await desktopCapturer.getSources({ types: ['screen'] }))[0]; sourceId = src && src.id; }
    if (!sourceId) { recKey = ''; return; }
    if (!recWin || recWin.isDestroyed()) {
      recWin = new BrowserWindow({ show: false, width: 200, height: 100, webPreferences: { preload: path.join(__dirname, 'recorder-preload.js'), contextIsolation: false, sandbox: false, backgroundThrottling: false } });
      await recWin.loadFile(path.join(__dirname, 'recorder.html'));   // a file page is a secure context, so it can record (a data: page can't)
    }
    recWin.webContents.send('rec:start', sourceId);
    clearTimeout(recT); recT = setTimeout(() => { if (recKey && recWin && !recWin.isDestroyed()) recWin.webContents.send('rec:stop'); }, 10 * 60000);   // 10 minutes at most
  } catch (e) { console.log('recording failed:', e && e.message); recKey = ''; }
}
ipcMain.on('rec:started', () => { recLive = true; flash('⏺ يسجّل… اضغط F9 عشان توقف');
  if (recStopAsked) { recStopAsked = false; setTimeout(() => { if (recWin && !recWin.isDestroyed()) recWin.webContents.send('rec:stop'); }, 1500); } });
ipcMain.on('rec:error', (e, m) => { console.log('recording failed:', m); recKey = ''; recLive = false; flash('ما قدرت أسجّل'); });
ipcMain.on('rec:done', async (e, bytes) => {
  const key = recKey; recKey = ''; recLive = false; clearTimeout(recT);
  if (!bytes || !bytes.length) { flash('ما انحفظ المقطع: ما وصلت صورة من اللعبة'); return; }   // never save an empty video
  try { const dir = path.join(CLIPS(), gameFolder(key)); await fsp.mkdir(dir, { recursive: true });
    const t = new Date(), pad = n => String(n).padStart(2, '0');
    await fsp.writeFile(path.join(dir, `${t.getFullYear()}-${pad(t.getMonth() + 1)}-${pad(t.getDate())} ${pad(t.getHours())}.${pad(t.getMinutes())}.${pad(t.getSeconds())}.webm`), Buffer.from(bytes));
    flash('🎬 انحفظ المقطع'); } catch (er) { flash('ما انحفظ المقطع'); }
});
ipcMain.handle('ps:openClips', guard(async () => { await fsp.mkdir(CLIPS(), { recursive: true }); shell.openPath(CLIPS()); return true; }));

// small notice at the bottom corner, above any game
let flashWin = null, flashT = null;
function flash(text) {
  try {
    const { screen } = require('electron'), wa = screen.getPrimaryDisplay().workArea;
    if (!flashWin || flashWin.isDestroyed()) {
      flashWin = new BrowserWindow({ width: 300, height: 64, x: wa.x + wa.width - 320, y: wa.y + wa.height - 84, frame: false, transparent: true, alwaysOnTop: true, focusable: false, skipTaskbar: true, show: false, resizable: false, webPreferences: { sandbox: true, contextIsolation: true } });
      flashWin.setAlwaysOnTop(true, 'screen-saver'); flashWin.setIgnoreMouseEvents(true);
    }
    flashWin.loadURL('data:text/html;charset=utf-8,' + encodeURIComponent(`<html dir="rtl"><body style="margin:0;font:600 15px 'Segoe UI',Tahoma,sans-serif;color:#fff"><div style="margin:6px;padding:14px 16px;background:rgba(17,21,31,.94);border:1px solid #2c3446;border-inline-start:4px solid #ffcc1f">${text.replace(/</g, '&lt;')}</div></body></html>`));
    flashWin.showInactive(); clearTimeout(flashT); flashT = setTimeout(() => { if (flashWin && !flashWin.isDestroyed()) flashWin.hide(); }, 2600);
  } catch (e) {}
}

// the overlay: a see-through layer over the game with what Steam's has — time played, achievements, screenshot, friends, close the game
let ovWin = null, ovKey = '';
async function achievementsFor(game) {
  try { const t = await fbToken(); if (!t || !currentUser) return [];
    const r = await net.fetch(`https://firestore.googleapis.com/v1/${FB_DB}/users/${currentUser.uid}/achievements?pageSize=300`, { headers: { authorization: 'Bearer ' + t } });
    const j = await r.json(); return (j.documents || []).map(d => d.fields).filter(f => f.game && f.game.stringValue === game).map(f => f.id.stringValue);
  } catch (e) { return []; }
}
function ovHTML(d) {
  const esc = t => String(t).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const all = Object.entries(d.defs || {}), got = new Set(d.got), O = { platinum: 0, gold: 1, silver: 2, bronze: 3 };
  const CUP = '<svg viewBox="0 0 24 24" width="16" height="16" style="vertical-align:-3px"><path fill="currentColor" d="M7 2h10v2h3.5v3.2A4.3 4.3 0 0 1 16.4 11.5 5.1 5.1 0 0 1 13 14.4V17h3.2v3H7.8v-3H11v-2.6a5.1 5.1 0 0 1-3.4-2.9A4.3 4.3 0 0 1 3.5 7.2V4H7zm0 4H5.5v1.2A2.3 2.3 0 0 0 7 9.4zm10 0v3.4a2.3 2.3 0 0 0 1.5-2.2V6z"/></svg>';
  return `<!doctype html><html dir="rtl"><head><meta charset="utf-8"><style>
  *{box-sizing:border-box;margin:0}body{font-family:"Segoe UI",Tahoma,sans-serif;color:#eef2f8;background:rgba(5,8,14,.78);height:100vh;display:flex;flex-direction:column;user-select:none}
  header{display:flex;align-items:center;gap:22px;padding:22px 36px;background:rgba(10,14,22,.9);border-bottom:1px solid #2c3446}
  .ps{background:#000;border:1px solid #2c3446;padding:3px 9px;font:800 20px sans-serif;direction:ltr}.ps i{font-style:normal}.ps i+i{color:#ffcc1f}
  h1{font-size:22px;flex:1}.t{color:#9aa4b8;font-size:15px}
  main{flex:1;display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1.3fr);gap:24px;padding:30px 36px;min-height:0}
  .card{background:rgba(17,22,34,.95);border:1px solid #2c3446;padding:20px;overflow:auto}
  .card h2{font-size:16px;margin-bottom:14px;color:#c9d2e3}
  a{display:block;text-decoration:none;color:#eef2f8;background:#1d2638;padding:15px 18px;margin-bottom:10px;font-size:16px;border:1px solid #2c3446}
  a:hover,a:focus{border-color:#ffcc1f;outline:none}a.red{background:#5a1d22;border-color:#7a2a30}
  .ach{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}
  .a{padding:12px;background:#1a2232;border:1px solid #2c3446;opacity:.35;color:#c98a55}.a.t-silver{color:#cfd6e0}.a.t-gold{color:#f2c94c}.a.t-platinum{color:#a9e4ff}.a b{color:inherit}.a.got{opacity:1;border-color:#6b5512}.a b span{color:#eef2f8}.a b{display:block;font-size:14px}.a small{font-size:12px;color:#9aa4b8}
  footer{padding:14px;text-align:center;color:#8c96aa;font-size:14px}
  </style></head><body><header><span class="ps"><i>P</i><i>G</i></span><h1>${esc(d.name)}</h1><span class="t">تلعب من ${d.mins} دقيقة · ${esc(d.clock)}</span></header>
  <main><div class="card"><h2>اختصارات</h2><a href="#shot">📸 لقطة شاشة (F12)</a><a href="#rec">🎬 سجّل مقطع (F9)</a><a href="#shots">🖼 لقطاتي</a><a href="#friends">👥 الأصدقاء والدردشة</a><a href="#back">↩ ارجع للعبة</a><a class="red" href="#close">■ إغلاق اللعبة</a></div>
  <div class="card"><h2>التروفيات ${all.length ? `(${[...got].filter(id => d.defs[id]).length}/${all.length})` : ''}</h2>${all.length ? `<div class="ach">${all.map(([id, v], i) => [id, v, i]).sort((a, b) => (got.has(b[0]) - got.has(a[0])) || ((O[a[1].tier] ?? 3) - (O[b[1].tier] ?? 3)) || a[2] - b[2]).map(([id, v]) => `<div class="a${got.has(id) ? ' got' : ''} t-${O[v.tier] != null ? v.tier : 'bronze'}"><b>${CUP} ${esc(v.name)}</b><small>${esc(v.desc || '')}</small></div>`).join('')}</div>` : '<p class="t">هذي اللعبة ما فيها تروفيات للحين.</p>'}</div></main>
  <footer>Shift+Tab أو Esc يرجعك للعبة</footer></body></html>`;
}
async function toggleOverlay(key) {
  if (ovWin && !ovWin.isDestroyed()) { closeOverlay(); return; }
  const r = running.get(key); if (!r) return;
  ovKey = key;
  const { screen } = require('electron'), w = gameWins.get(key);
  const b = w && !w.isDestroyed() ? w.getBounds() : screen.getPrimaryDisplay().bounds;
  let defs = {}; try { defs = (((await fetchManifest()).games || {})[r.game] || {}).achievements || {}; } catch (e) {}
  const got = await achievementsFor(r.game);
  ovWin = new BrowserWindow({ ...b, frame: false, transparent: true, alwaysOnTop: true, skipTaskbar: true, resizable: false, show: false, parent: w && !w.isDestroyed() ? w : undefined, webPreferences: { sandbox: true, contextIsolation: true } });
  ovWin.setAlwaysOnTop(true, 'screen-saver');
  ovWin.loadURL('data:text/html;charset=utf-8,' + encodeURIComponent(ovHTML({ name: r.name, mins: Math.floor((Date.now() - r.since) / 60000), clock: new Date().toLocaleTimeString('ar-SA-u-nu-latn', { hour: 'numeric', minute: '2-digit' }), defs, got })));
  ovWin.once('ready-to-show', () => { if (ovWin) { ovWin.show(); ovWin.focus(); } });
  ovWin.webContents.on('before-input-event', (e, i) => { if (i.type === 'keyDown' && (i.key === 'Escape' || (i.key === 'Tab' && i.shift))) { e.preventDefault(); closeOverlay(); } });
  ovWin.webContents.on('did-navigate-in-page', async (e, url) => {
    const h = url.split('#')[1], k = ovKey;
    if (h === 'shot') { closeOverlay(); setTimeout(() => screenshot(k), 250); }
    else if (h === 'rec') { closeOverlay(); setTimeout(() => toggleRecord(k), 250); }
    else if (h === 'shots') { closeOverlay(); shell.openPath(path.join(SHOTS(), gameFolder(k))).catch(() => {}); }
    else if (h === 'friends') { closeOverlay(); createMain('friends'); }
    else if (h === 'close') { closeOverlay(); stopGame(k); }
    else if (h === 'back') closeOverlay();
  });
  ovWin.on('closed', () => { ovWin = null; });
}
function closeOverlay() { if (ovWin && !ovWin.isDestroyed()) { const w = gameWins.get(ovKey); ovWin.destroy(); if (w && !w.isDestroyed()) w.focus(); } ovWin = null; }
/* ---------------- the console's own files (like a console's system storage) ----------------
   %LOCALAPPDATA%\PremiumStudio\console\
     system.json                 the console screen's settings (wallpaper, sounds, button names, sorting)
     activity.log                everything that happens, one JSON line each (console opened, game started / closed, Home, ...)
     users\<uid>\lists.json      your game lists
     users\<uid>\hq.json         per game: the edition and version you picked in its console menu
     users\<uid>\games.json      per game on the console: sessions, minutes, last played, edition, version
     users\<uid>\activity.json   your last 500 events */
const CS_DIR = path.join(LOCAL, 'PremiumStudio', 'console');
const csUser = () => String((currentUser && currentUser.uid) || 'guest').replace(/[^\w-]/g, '').slice(0, 64) || 'guest';
const CS_FILES = { system: () => path.join(CS_DIR, 'system.json'), lists: () => path.join(CS_DIR, 'users', csUser(), 'lists.json'), hq: () => path.join(CS_DIR, 'users', csUser(), 'hq.json'),
  games: () => path.join(CS_DIR, 'users', csUser(), 'games.json'), activity: () => path.join(CS_DIR, 'users', csUser(), 'activity.json') };
function csRead(name, def) { try { return JSON.parse(fs.readFileSync(CS_FILES[name](), 'utf8')); } catch (e) { return def; } }
function csWrite(name, data) { const f = CS_FILES[name](); try { fs.mkdirSync(path.dirname(f), { recursive: true }); fs.writeFileSync(f + '.tmp', JSON.stringify(data, null, 1)); fs.renameSync(f + '.tmp', f); } catch (e) {} }
function csLog(type, data) {
  const ev = Object.assign({ t: new Date().toISOString(), type, user: csUser() }, data || {});
  try { fs.mkdirSync(CS_DIR, { recursive: true }); fs.appendFileSync(path.join(CS_DIR, 'activity.log'), JSON.stringify(ev) + '\n'); } catch (e) {}
  try { const st = fs.statSync(path.join(CS_DIR, 'activity.log')); if (st.size > 4e6) fs.renameSync(path.join(CS_DIR, 'activity.log'), path.join(CS_DIR, 'activity.old.log')); } catch (e) {}
  const a = csRead('activity', []); a.unshift(ev); csWrite('activity', a.slice(0, 500));
}
// a game started / stopped from the console: its console record
function csGame(game, upd) { const g = csRead('games', {}); g[game] = Object.assign(g[game] || { sessions: 0, minutes: 0 }, upd(g[game] || { sessions: 0, minutes: 0 })); csWrite('games', g); }
ipcMain.handle('cs:get', guard(async name => { if (!CS_FILES[name]) throw new Error('bad file'); return csRead(name, name === 'activity' || name === 'lists' ? [] : {}); }));
ipcMain.handle('cs:set', guard(async (name, data) => { if (!['system', 'lists', 'hq'].includes(name)) throw new Error('read only');
  if (JSON.stringify(data || null).length > 200000) throw new Error('too big'); csWrite(name, data); return true; }));
ipcMain.handle('cs:log', guard(async (type, data) => { if (typeof type !== 'string' || !/^[a-z-]{2,24}$/.test(type)) throw new Error('bad event');
  const d = data && typeof data === 'object' && JSON.stringify(data).length <= 2000 ? data : {}; csLog(type, d); return true; }));

/* ---- the versions of a game that has them (BLOCKFALL: its own version list, like its launcher shows) */
async function gameVersions(gid) {
  const m = ((await fetchManifest()).games || {})[gid]; if (!m || !m.gameManifest) return null;
  const vdir = path.join(bfv(gid).data, 'versions');
  const have = new Set((() => { try { return fs.readdirSync(vdir).filter(id => fs.existsSync(path.join(vdir, id, id + '.json'))); } catch (e) { return []; } })());
  let man = null; try { man = await fetchJSON(m.gameManifest); } catch (e) {}
  if (!man) { try { man = JSON.parse(fs.readFileSync(path.join(vdir, 'version_manifest_v2.json'), 'utf8')); } catch (e) { man = { latest: {}, versions: [] }; } }
  const list = (man.versions || []).map(v => ({ id: v.id, type: v.type || 'release', title: v.title || '', notes: (v.notes || []).slice(0, 8), time: v.releaseTime || v.time || '', size: v.size || 0, installed: have.has(v.id) }));
  for (const id of have) if (!list.some(v => v.id === id)) list.push({ id, type: 'release', title: '', notes: [], time: '', size: 0, installed: true, offline: true });
  return { latest: (man.latest && man.latest.release) || (list[0] || {}).id || '', versions: list };
}
ipcMain.handle('ps:gameVersions', guard(async gid => { if (!validId(gid)) throw new Error('bad id'); return gameVersions(gid); }));

/* ---------------- console mode while a game runs (like a PS5) ----------------
   The console screen tells us when it's open. Games started from it run full screen without a launcher. The controller's
   Home button (PS button; on Xbox pads View + Menu together, as Windows keeps the Xbox button for itself) opens the
   Control Center over the game; held, it goes to the console's home screen while the game keeps running.
   A tiny always-on-top window reads the controller, so Home works over games in their own program too. */
let consoleOn = false, ccWin = null, ccKey = '', padWin = null, lastHome = 0;
ipcMain.handle('ps:setConsole', guard(async on => { const was = consoleOn; if (was !== !!on) csLog(on ? 'console-open' : 'console-close', {}); consoleOn = !!on; syncPadWatch(); if (!consoleOn) closeCC();
  if (was !== consoleOn) systemSwitched(); return true; }));
// the page now shows the other system: its installs, its running games and its downloads
function systemSwitched() {
  for (const [k, p] of progNow) { if (isCon(k) !== consoleOn) send('ps:progress', { id: plainKey(k), phase: 'clear' }); else send('ps:progress', { ...p, id: plainKey(k) }); }
  send('ps:installed-changed', {}); runningChanged();
}
function gameWindowOf(key) { const w = gameWins.get(key); return w && !w.isDestroyed() ? w : null; }
// the game the player is in: the focused game window, else the newest running game
function currentGame() {
  for (const [k, w] of gameWins) if (w && !w.isDestroyed() && w.isFocused()) return k;
  const ks = [...running.keys()].filter(k => !stopping.has(k) && isCon(k) === consoleOn); return ks[ks.length - 1] || '';
}
function homePressed(long, native) {
  const now = Date.now(); if (now - lastHome < 450) return; lastHome = now;
  if (ccWin && !ccWin.isDestroyed()) { const k = ccKey; closeCC(); if (long) consoleHome(k); csLog('home', { long: !!long, to: long ? 'home' : 'game' }); return; }
  const fw = BrowserWindow.getFocusedWindow();
  if (fw && fw === tvWin) { if (native) closeTv(); return; }
  if (fw && fw === mainWin) { if (native) mainWin.webContents.send('bp:guide', { long: !!long }); return; }   // the console screen: its own Control Center
  const k = currentGame(); if (!k) return;
  csLog('home', { long: !!long, game: (running.get(k) || {}).game || k });
  if (long) consoleHome(k); else openCC(k);
}
function focusGame(key) {
  const w = gameWindowOf(key);
  if (w) { if (w.isMinimized()) w.restore(); w.show(); if (w.consoleMode) w.setFullScreen(true); w.focus(); return; }
  const g = readReg()[key];   // a game in its own program: ask it to come to the front
  if (g && g.type === 'bf-launcher') { try { fs.writeFileSync(path.join(bfv(g.game || key).data, 'premium_cmd.json'), JSON.stringify({ cmd: 'focus', at: Date.now() })); } catch (e) {} }
  const c = pgeProcs.get(key); if (c && c.exitCode === null) focusPid(c.pid);
}
// the console's home screen over the running game (the game keeps running behind it)
function consoleHome(key, page) {
  const w = mainWin && !mainWin.isDestroyed() ? mainWin : createMain('bigpicture');
  if (w.isMinimized()) w.restore(); w.show(); w.setFullScreen(true); w.focus();
  w.webContents.send('bp:home', { key: plainKey(key || ''), page: page || '' });
}
// the controller watcher: PremiumPad.exe (SDL, reads every controller in the background, also the real Xbox button),
// or - when it's missing (development) - a 1 x 1 px see-through window that reads the controller through Chromium
let lastFocused = '', padProc = null, padDown = 0, padLongT = null, padLong = false, padKind = '';
const PAD_EXE = () => app.isPackaged ? path.join(process.resourcesPath, 'PremiumPad.exe') : path.join(__dirname, 'build', 'PremiumPad.exe');
function startPadProc() {
  if (padProc || process.platform !== 'win32' || !fs.existsSync(PAD_EXE())) return !!padProc;
  let p; try { p = padProc = require('child_process').spawn(PAD_EXE(), [], { stdio: ['pipe', 'pipe', 'ignore'], windowsHide: true }); } catch (e) { padProc = null; return false; }
  let buf = '';
  p.stdin.on('error', () => {});
  p.stdout.on('data', d => { if (p !== padProc) return; buf += d; let i; while ((i = buf.indexOf('\n')) >= 0) { const line = buf.slice(0, i).trim(); buf = buf.slice(i + 1); padLine(line); } });
  p.on('error', () => {});
  p.on('exit', () => { if (p !== padProc) return; padProc = null; padDown = 0; clearTimeout(padLongT); if (mainWin && !mainWin.isDestroyed()) mainWin.webContents.send('bp:nativePad', false); syncPadWatch(); });
  if (mainWin && !mainWin.isDestroyed()) mainWin.webContents.send('bp:nativePad', true);
  return true;
}
function padLine(line) {
  if (line === 'ready') { if (mainWin && !mainWin.isDestroyed()) mainWin.webContents.send('bp:nativePad', true); return; }
  if (line.startsWith('pad ')) { padKind = line.split(' ')[1]; return; }
  if (line.startsWith('focused ')) { lastFocused = line; return; }
  if (line === 'home 1' && !padDown) { padDown = Date.now(); padLong = false; clearTimeout(padLongT); padLongT = setTimeout(() => { if (padDown) { padLong = true; homePressed(true, true); } }, 600); }
  else if (line === 'home 0' && padDown) { padDown = 0; clearTimeout(padLongT); if (!padLong) homePressed(false, true); }
}
const nativePad = () => !!padProc;
ipcMain.handle('ps:nativePad', guard(async () => nativePad()));
global.__psTest = Object.assign(global.__psTest || {}, { padProc: () => padProc, padPress: () => { if (padProc) padProc.stdin.write('test\n'); }, padLine, pgeProc: k => pgeProcs.get(k), granted: () => [...granted], lastFocused: () => lastFocused });
function syncPadWatch() {
  if (consoleOn) startPadProc(); else if (padProc) { try { padProc.stdin.end(); padProc.kill(); } catch (e) {} padProc = null; }
  const need = consoleOn && running.size > 0 && !padProc;
  if (!need) { if (padWin && !padWin.isDestroyed()) padWin.destroy(); padWin = null; return; }
  if (padWin && !padWin.isDestroyed()) return;
  padWin = new BrowserWindow({ width: 1, height: 1, x: 0, y: 0, frame: false, transparent: true, focusable: false, skipTaskbar: true, alwaysOnTop: true, resizable: false, show: false, hasShadow: false,
    webPreferences: { preload: path.join(__dirname, 'pad-preload.js'), contextIsolation: true, sandbox: true, backgroundThrottling: false } });
  padWin.setAlwaysOnTop(true, 'screen-saver'); padWin.setIgnoreMouseEvents(true);
  padWin.loadURL('data:text/html;charset=utf-8,<html><body style="margin:0;background:transparent"></body></html>');
  padWin.once('ready-to-show', () => padWin && !padWin.isDestroyed() && padWin.showInactive());
  padWin.on('closed', () => { padWin = null; });
}
ipcMain.on('pad:home', (e, long) => { if (padWin && e.sender === padWin.webContents && !nativePad()) homePressed(!!long); });

/* the Control Center over a game */
const coverOf = gid => { try { return 'data:image/png;base64,' + fs.readFileSync(path.join(COVERS, gid + '.png')).toString('base64'); } catch (e) { return ''; } };
async function ccData(key) {
  const r = running.get(key) || {}, reg = readReg(), g = reg[key] || {}, w = gameWindowOf(key);
  let defs = {}; try { defs = (((await fetchManifest()).games || {})[r.game] || {}).achievements || {}; } catch (e) {}
  const got = Object.keys(defs).length ? await achievementsFor(r.game) : [];
  let con = null;   // notifications, friends and profile come from the console screen
  try { if (mainWin && !mainWin.isDestroyed()) con = await Promise.race([mainWin.webContents.executeJavaScript('window.__bpCC ? window.__bpCC() : null'), new Promise(res => setTimeout(() => res(null), 1200))]); } catch (e) {}
  const games = [...running].filter(([k]) => isCon(k) === isCon(key)).map(([k, v]) => ({ key: k, name: v.name || k, cover: coverOf(v.game), since: v.since }));
  return { key, name: r.name || g.name || key, game: r.game, cover: coverOf(r.game), since: r.since || Date.now(),
    trophies: { all: Object.keys(defs).length, got: got.filter(id => defs[id]).length, list: Object.entries(defs).map(([id, v]) => ({ name: v.name, tier: v.tier || 'bronze', got: got.includes(id) })).sort((a, b) => b.got - a.got).slice(0, 6) },
    running: games, muted: w ? w.webContents.isAudioMuted() : false, canMute: !!w, recording: recLive && recKey === key, con: con || {} };
}
async function openCC(key) {
  if (ccWin && !ccWin.isDestroyed()) return;
  ccKey = key;
  const { screen } = require('electron'), w = gameWindowOf(key);
  const b = w ? w.getBounds() : screen.getPrimaryDisplay().bounds;
  ccWin = new BrowserWindow({ ...b, frame: false, transparent: true, alwaysOnTop: true, skipTaskbar: true, resizable: false, show: false, hasShadow: false, backgroundColor: '#00000000',
    webPreferences: { preload: path.join(__dirname, 'cc-preload.js'), contextIsolation: true, sandbox: true, backgroundThrottling: false } });
  ccWin.setAlwaysOnTop(true, 'screen-saver');
  ccWin.loadFile(path.join(__dirname, 'cc.html'));
  ccWin.once('ready-to-show', () => { if (ccWin && !ccWin.isDestroyed()) { ccWin.show(); ccWin.focus(); } });
  ccWin.on('closed', () => { ccWin = null; });
  if (w) w.webContents.send('game:paused', true);
}
function closeCC() {
  if (!ccWin || ccWin.isDestroyed()) { ccWin = null; return; }
  const k = ccKey; ccWin.destroy(); ccWin = null;
  const w = gameWindowOf(k); if (w) { w.webContents.send('game:paused', false); w.focus(); } else if (k) focusGame(k);
}
ipcMain.handle('cc:data', guard(async () => ccKey ? Object.assign(await ccData(ccKey), { nativePad: nativePad(), padKind }) : null));
ipcMain.handle('cc:act', guard(async (what, arg) => {
  const k = ccKey, w = gameWindowOf(k); csLog('cc', { what, arg: typeof arg === 'string' ? arg : '', game: (running.get(k) || {}).game || k });
  if (what === 'close') { closeCC(); return true; }
  if (what === 'home') { ccWin && ccWin.destroy(); ccWin = null; consoleHome(k, typeof arg === 'string' ? arg : ''); return true; }
  if (what === 'switch') { ccWin && ccWin.destroy(); ccWin = null; if (arg === 'home') consoleHome(k); else if (running.has(arg)) focusGame(arg); return true; }
  if (what === 'stop') { ccWin && ccWin.destroy(); ccWin = null; consoleHome(k); stopGame(k); return true; }
  if (what === 'shot') { closeCC(); setTimeout(() => screenshot(k), 300); return true; }
  if (what === 'rec') { closeCC(); setTimeout(() => toggleRecord(k), 300); return true; }
  if (what === 'mute') { if (w) w.webContents.setAudioMuted(!w.webContents.isAudioMuted()); return w ? w.webContents.isAudioMuted() : false; }
  if (what === 'power') { if (['sleep', 'restart', 'shutdown', 'quit'].includes(arg)) { closeCC(); await power(arg); } return true; }
  throw new Error('bad action');
}));
// games in their own program (BLOCKFALL) get F12 and Shift+Tab while they run
function gameKeys(on) {
  const { globalShortcut } = require('electron');
  try {
    if (on) {
      const bf = () => [...running.keys()].find(k => ['bf-launcher', 'pge'].includes((readReg()[k] || {}).type));
      if (!globalShortcut.isRegistered('F12')) globalShortcut.register('F12', () => { const k = bf(); if (k) screenshot(k); });
      if (!globalShortcut.isRegistered('Shift+Tab')) globalShortcut.register('Shift+Tab', () => { const k = bf(); if (k) { if (consoleOn) homePressed(false); else toggleOverlay(k); } });
      if (!globalShortcut.isRegistered('F9')) globalShortcut.register('F9', () => { const k = bf(); if (k) toggleRecord(k); });
    } else { globalShortcut.unregister('F12'); globalShortcut.unregister('Shift+Tab'); globalShortcut.unregister('F9'); }
  } catch (e) {}
}

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
  <a class="open" href="#open">افتح Premium Games</a></div></body></html>`;
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

async function launchWithUpdate(id, edition, opts) {
  const g = installedNow()[id];
  const pb = g && parentalBlocks(g.game || id.split('--')[0]); if (pb) { flash('🔒 ' + pb); throw new Error(pb); }
  if (g && !running.has(id) && !isCon(id)) showSplash(id);
  if (g && (g.type === 'manifest' || g.type === 'pge') && !busy.has(id) && !running.has(id)) {
    try {
      const m = ((await fetchManifest()).games || {})[g.game];
      if (m && JSON.stringify(m.account || null) !== JSON.stringify(readReg()[id].account || null)) { const reg = readReg(); reg[id].account = m.account || undefined; await writeReg(reg); }
      if (m) { const l = await latestOf(m); if (l.version && (!g.version || newerVersion(l.version, g.version))) { splashStatus('يحمّل التحديث ' + l.version + '…'); await install(id); } }
    } catch (e) { /* offline: play the copy we have */ }
  }
  if (g && g.type === 'bf-launcher' && !running.has(id)) { splashStatus('يتحقق من التحديثات…'); try { await Promise.race([bfAppUpdate(((await fetchManifest()).games || {})[g.game] || ((await fetchManifest()).games || {}).blockfall, g.game), new Promise(r => setTimeout(r, 15000))]); } catch (e) {} }
  if (g && g.type === 'manifest' && !running.has(id)) { splashStatus('يجيب حفظك من السحابة…'); try { await cloudPull(id); } catch (e) {} }
  splashStatus('جاري التشغيل');
  try { const reg = readReg(); if (reg[id]) { reg[id].lastPlayed = Date.now(); await writeReg(reg); } } catch (e) {}
  try { launchGame(id, edition, opts); } catch (e) { closeSplash(); throw e; }
}
function launchGame(id, edition, opts) {
  const ver = (opts && opts.version) || '';
  const g = installedNow()[id];
  if (!g) throw new Error('اللعبة مو مثبتة');
  if (g.type === 'bf-launcher') {
    if (running.has(id)) { focusGame(id); return; }   // already playing: back to it
    // One install serves every owned edition: the launcher reads what you own from premium_account.json and you pick inside it.
    // From console mode the launcher is skipped: the game starts straight away, full screen, in the edition picked on the console.
    const V = bfv(g.game || id);
    writeBfAccount();
    try { fs.rmSync(path.join(V.data, 'premium_cmd.json'), { force: true }); } catch (e) {}
    const ed = EDITIONS.includes(edition) ? edition : '';
    try { fs.mkdirSync(V.data, { recursive: true }); fs.writeFileSync(path.join(V.data, 'premium_launch.json'), JSON.stringify({ console: consoleOn, edition: ed, version: ver, at: Date.now() })); } catch (e) {}
    const { spawn } = require('child_process');
    // console mode: the console version - no launcher window, straight into the game in the edition and version picked on the console
    const child = spawn(V.exe, ['--premium', ...(consoleOn ? ['--console'] : []), ...(consoleOn && ed ? ['--edition=' + ed] : []), ...(consoleOn && ver ? ['--version=' + ver] : [])], { cwd: V.dir, detached: true, stdio: 'ignore' });
    if (consoleOn) { const game = g.game || id; csLog('game-start', { game, key: id, edition: ed, version: ver, from: 'console' }); csGame(game, r => ({ sessions: r.sessions + 1, last: Date.now(), edition: ed || r.edition, version: ver || r.version })); }
    // the starting window stays until BLOCKFALL shows up
    (async () => { const t0 = Date.now(); while (splashKey === id && Date.now() - t0 < 30000) { await new Promise(r => setTimeout(r, 700)); if (await bfAlive(g.game || id)) break; } if (splashKey === id) closeSplash(); })();
    child.unref();
    markRunning(id);
    return;
  }
  if (g.type === 'pge') { launchPge(id, g); return; }
  if (gameWins.has(id) && !gameWins.get(id).isDestroyed()) { focusGame(id); return; }
  const cm = isCon(id);   // the console's own install: full screen like a console game, no window frame
  const w = new BrowserWindow({
    width: 1600, height: 900, backgroundColor: '#000000', title: g.name, icon: path.join(gameDir(id, g), g.icon || 'icon.ico'),
    autoHideMenuBar: true, show: false, frame: !cm, fullscreen: cm,
    webPreferences: { preload: path.join(__dirname, 'game-preload.js'), contextIsolation: false, sandbox: false, nodeIntegration: false, backgroundThrottling: false }
  });
  w.setMenu(null); w.consoleMode = cm;
  w.once('ready-to-show', () => { if (cm) { w.setFullScreen(true); w.show(); w.focus(); } else { w.maximize(); w.show(); } if (splashKey === id) closeSplash(); });
  w.webContents.setWindowOpenHandler(({ url }) => { openExternal(url); return { action: 'deny' }; });
  w.webContents.on('before-input-event', (e, input) => {
    if (input.type !== 'keyDown') return;
    if (input.key === 'F11') { if (!w.consoleMode) w.setFullScreen(!w.isFullScreen()); e.preventDefault(); }
    else if (input.key === 'F12') { e.preventDefault(); screenshot(id); }
    else if (input.key === 'F9') { e.preventDefault(); toggleRecord(id); }
    else if (input.key === 'Tab' && input.shift) { e.preventDefault(); if (w.consoleMode) homePressed(false); else toggleOverlay(id); }
  });
  const qs = [...(g.edition === 'demo' ? ['demo=1'] : []), ...(cm ? ['console=1'] : [])], q = qs.length ? '?' + qs.join('&') : '';
  if (cm) { const game = g.game || id.split('--')[0]; csLog('game-start', { game, key: id, edition: g.edition || '', from: 'console' }); csGame(game, r => ({ sessions: r.sessions + 1, last: Date.now(), edition: g.edition || r.edition })); }
  w.loadURL(`psgame://${id}/${g.entry}${q}`);
  // the game page hands over its saved data as it unloads (game-preload.js); it goes to the account after the window closes
  w.on('closed', () => { gameWins.delete(id); const snap = lastSave.get(id); lastSave.delete(id);
    if (snap) { const p = cloudPush(id, snap).catch(() => {}); pushing.set(id, p); p.finally(() => pushing.delete(id)); }
    markStopped(id); });
  gameWins.set(id, w);
  markRunning(id);
}

// A Premium Engine game: its own program. It's told it runs from Premium Games (and on the console or the PC),
// and writes its trophies to an events file that is read here and put on the player's account.
const pgeProcs = new Map();   // install key -> child process
const PGE_EVENTS = id => path.join(app.getPath('userData'), 'events', id + '.txt');
function launchPge(id, g) {
  const old = pgeProcs.get(id); if (old && old.exitCode === null) { focusGame(id); if (splashKey === id) closeSplash(); return; }
  const dir = gameDir(id, g), ev = PGE_EVENTS(id);
  try { fs.mkdirSync(path.dirname(ev), { recursive: true }); fs.writeFileSync(ev, ''); } catch (e) {}
  const cm = isCon(id);   // the console's install is the game's console version (controller menus, lighter graphics)
  const exe = path.join(dir, g.entry);
  const child = require('child_process').spawn(exe, [], { cwd: path.dirname(exe), stdio: 'ignore', windowsHide: false,
    env: { ...process.env, PREMIUM_GAMES: '1', PREMIUM_CONSOLE: cm ? '1' : '0', PREMIUM_GAMES_EVENTS: ev, PREMIUM_GAMES_USER: playerName() || '' } });
  pgeProcs.set(id, child);
  let read = 0;
  const poll = setInterval(() => {   // trophies the game wrote
    try { const b = fs.readFileSync(ev, 'utf8'); if (b.length <= read) return; const lines = b.slice(read).split('\n'); read = b.length - lines[lines.length - 1].length;
      for (const l of lines.slice(0, -1)) { const m = /^trophy ([A-Za-z0-9_-]{1,40})$/.exec(l.trim()); if (m) grantTrophy(g.game || id, m[1]); } } catch (e) {}
  }, 1000);
  child.on('error', e => { child.spawnError = String(e && e.message || e); });
  child.on('exit', () => { clearInterval(poll); if (pgeProcs.get(id) === child) pgeProcs.delete(id);
    try { const b = fs.readFileSync(ev, 'utf8').slice(read); for (const l of b.split('\n')) { const m = /^trophy ([A-Za-z0-9_-]{1,40})$/.exec(l.trim()); if (m) grantTrophy(g.game || id, m[1]); } } catch (e) {}
    markStopped(id); });
  if (cm) { const game = g.game || id; csLog('game-start', { game, key: id, version: g.version || '', from: 'console' }); csGame(game, r => ({ sessions: r.sessions + 1, last: Date.now(), version: g.version || r.version })); }
  setTimeout(() => { if (splashKey === id) closeSplash(); }, 2500);
  gameKeys(true);
  markRunning(id);
}
// a window of another program to the front (Windows lets only the program in front do that: PremiumPad does it)
function focusPid(pid) {
  if (process.platform !== 'win32' || !pid) return;
  if (padProc) { try { padProc.stdin.write('focus ' + pid + '\n'); } catch (e) {} return; }
  if (fs.existsSync(PAD_EXE())) { try { require('child_process').spawn(PAD_EXE(), ['--focus', String(pid)], { stdio: 'ignore', windowsHide: true, detached: true }).unref(); } catch (e) {} }
}

// Installs from before editions were separate: give them their game id and an edition in the name and shortcut
function migrateReg() {
  try {
    const reg = readReg(); let changed = false;
    for (const [k, v] of Object.entries(reg)) {
      if (v.type === 'bf-launcher' || isCon(k)) continue;
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
// Older versions installed BLOCKFALL once per edition ("BLOCKFALL 3001 Gold", "… Demo"…): merge them into one
// "BLOCKFALL 3001" install with one icon. The launcher keeps the player's edition choice, so our old license.json goes.
function migrateBf() {
  try {
    const reg = readReg(), old = Object.entries(reg).filter(([k, v]) => v.type === 'bf-launcher' && !BFV[k]);
    for (const [k, v] of old) { removeShortcuts(v); delete reg[k]; }
    if (old.length && !reg.blockfall && fs.existsSync(BF_EXE)) {
      const v = old.map(x => x[1]).sort((a, b) => (b.lastPlayed || 0) - (a.lastPlayed || 0))[0];
      reg.blockfall = { ...v, game: 'blockfall', base: BFV.blockfall.name, name: BFV.blockfall.name, edition: undefined, unlock: undefined, entry: BFV.blockfall.exe, icon: 'resources/app/icon.ico' };
    }
    for (const [k, v] of Object.entries(reg)) if (v.type === 'bf-launcher' && (v.name !== BFV[k].name || v.edition)) { removeShortcuts(v); v.name = v.base = BFV[k].name; delete v.edition; delete v.unlock; }
    try { const f = path.join(BF_DATA, 'license.json'); const L = JSON.parse(fs.readFileSync(f, 'utf8')); if (L.ps) fs.rmSync(f, { force: true }); } catch (e) {}
    if (old.length || Object.values(reg).some(v => v.type === 'bf-launcher')) writeRegSync(reg);
    for (const k of Object.keys(BFV)) if (reg[k]) makeShortcuts(k, reg[k]);
  } catch (e) {}
}
/* BLOCKFALL's launcher program: Premium Games brings its app files up to date before starting it
   (the same files the launcher would fetch for itself), so an older launcher never runs. */
const vnum = v => String(v || '0').split('.').map(n => +n || 0);
const vgt = (a, b) => { const x = vnum(a), y = vnum(b); for (let i = 0; i < Math.max(x.length, y.length); i++) { if ((x[i] || 0) !== (y[i] || 0)) return (x[i] || 0) > (y[i] || 0); } return false; };
async function bfAppUpdate(m, game) {
  try {
    const site = (m && m.site) || 'https://abdullra7manx-cmyk.github.io/terravolt/', dir = path.join(bfv(game || 'blockfall').dir, 'resources', 'app');
    if (!fs.existsSync(dir)) return;
    const L = await fetchJSON(new URL('launcher/launcher.json', site).href), A = L && L.app; if (!A || !A.files) return;
    let cur = '0'; try { cur = JSON.parse(fs.readFileSync(path.join(dir, 'package.json'), 'utf8')).version; } catch (e) {}
    if (!vgt(A.version, cur)) return;
    const bufs = [];
    for (const f of A.files) { const r = await net.fetch(new URL(f.url, site).href); if (!r.ok) throw new Error('download ' + f.path);
      const b = Buffer.from(await r.arrayBuffer()); if (crypto.createHash('sha1').update(b).digest('hex') !== f.sha1) throw new Error('sha1 ' + f.path); bufs.push([f.path, b]); }
    for (const [p, b] of bufs) { const dest = path.join(dir, p); if (!dest.startsWith(dir)) continue; fs.writeFileSync(dest + '.new', b); fs.renameSync(dest + '.new', dest); }
    try { const pj = path.join(dir, 'package.json'), j = JSON.parse(fs.readFileSync(pj, 'utf8')); j.version = A.version; fs.writeFileSync(pj, JSON.stringify(j, null, 2)); } catch (e) {}
  } catch (e) { /* offline: the launcher updates itself next time */ }
}
// The launcher's own icons (from installs made before Premium Games managed it) open the launcher directly:
// ours (same name) replace them, so the game always starts through Premium Games, like Steam.
function fixBfShortcuts() {
  const own = Object.entries(installedNow()).filter(([k, v]) => v.type === 'bf-launcher' && !isCon(k));
  const menu = path.join(app.getPath('appData'), 'Microsoft', 'Windows', 'Start Menu', 'Programs');
  for (const [k, v] of own) {
    for (const p of [path.join(menu, v.name + '.lnk'), path.join(app.getPath('desktop'), v.name + '.lnk')]) {
      try { if (fs.existsSync(p) && shell.readShortcutLink(p).target !== process.execPath) fs.rmSync(p, { force: true }); } catch (e) {} }
    makeShortcuts(k, v);
  }
}

/* ---------------- install / uninstall ---------------- */
const busy = new Set();
function send(ch, payload) { if (mainWin && !mainWin.isDestroyed()) mainWin.webContents.send(ch, payload); }
// a download's progress, shown by the system it belongs to (the page sees the plain key)
const progNow = new Map();
function sendProg(p) { const k = p.id; if (p.phase === 'done' || p.phase === 'error') progNow.delete(k); else progNow.set(k, p);
  if (isCon(k) === consoleOn) send('ps:progress', { ...p, id: plainKey(k) }); }

async function fetchJSON(url) {
  const r = await net.fetch(url + (url.includes('?') ? '&' : '?') + 't=' + Date.now(), { cache: 'no-store' });
  if (!r.ok) throw new Error('HTTP ' + r.status + ' · ' + url);
  return r.json();
}
// (the end-to-end test can hand in the games list of the branch it tests)
const fetchManifest = () => global.__psManifestFile ? Promise.resolve(JSON.parse(fs.readFileSync(global.__psManifestFile, 'utf8'))) : fetchJSON(SITE + 'games.json');

// Puts a freshly unpacked folder in place of the installed one. Windows may still hold files of a game that just closed
// (the old copy is moved aside, with retries): if the new copy can't go in, the old one goes back, so a failed update
// never leaves the game missing.
async function swapDir(from, to) {
  const retry = async f => { for (let i = 0; ; i++) { try { return await f(); } catch (e) { if (i >= 20 || !['EPERM', 'EBUSY', 'EACCES', 'ENOTEMPTY'].includes(e.code)) throw e; if (global.__psTest) console.log('swapDir retry', i, e.code, e.path || ''); await new Promise(r => setTimeout(r, 250 + i * 150)); } } };
  const aside = to + '.old';
  await fsp.mkdir(path.dirname(to), { recursive: true });
  await fsp.rm(aside, { recursive: true, force: true }).catch(() => {});
  const had = fs.existsSync(to);
  if (had) await retry(() => fsp.rename(to, aside));
  try { await retry(() => fsp.rename(from, to)); }
  catch (e) { if (had) await retry(() => fsp.rename(aside, to)).catch(() => {}); throw e; }
  if (had) fsp.rm(aside, { recursive: true, force: true }).catch(() => {});
}
// "00.01.02" > "00.01.01": an update is only a newer version (a stale copy of the update file never reinstalls an older one)
const newerVersion = (a, b) => { const p = v => String(v || '').split(/[.\-]/).map(x => parseInt(x, 10) || 0); const x = p(a), y = p(b);
  for (let i = 0; i < Math.max(x.length, y.length); i++) { if ((x[i] || 0) !== (y[i] || 0)) return (x[i] || 0) > (y[i] || 0); } return false; };
// Latest version each game's own update source offers right now.
async function latestOf(m) {
  if (m.type === 'bf-launcher') { const p = await fetchJSON(m.parts); let game = ''; try { game = (await fetchJSON(m.gameManifest)).latest.release; } catch (e) {} return { version: p.version, game, size: p.size }; }
  if (m.type === 'manifest') { const j = await fetchJSON(m.base + 'manifest.json'); return { version: j.version, files: j.files }; }
  if (m.type === 'pge') { const j = await fetchJSON(m.dist + 'version.json'); return { version: j.version, size: (+j.size || 0) + (j.console ? +j.console.size || 0 : 0) }; }
  return { version: m.version };
}
async function getLatest() {
  const man = await fetchManifest(), out = {};
  await Promise.all(Object.entries(man.games || {}).map(async ([id, m]) => { try { const l = await latestOf(m); out[id] = { version: l.version, game: l.game || '' }; } catch (e) {} }));
  return out;
}

// Stream a URL to disk (or memory) with progress.
// download speed limit (Downloads → settings), 0 = no limit; bytes per second
const SETTINGS_FILE = () => path.join(app.getPath('userData'), 'settings.json');
const settings = () => { try { return JSON.parse(fs.readFileSync(SETTINGS_FILE(), 'utf8')); } catch (e) { return {}; } };
async function download(url, onBytes, toFile) {
  const limit = +settings().limit || 0, t0 = Date.now(); let got = 0;
  const res = await net.fetch(url, { cache: 'no-store' });
  if (!res.ok || !res.body) throw new Error('فشل التحميل (' + res.status + ')');
  const reader = res.body.getReader(), parts = [];
  const out = toFile ? fs.createWriteStream(toFile) : null;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    onBytes(value.length); got += value.length;
    if (limit > 0) { const ahead = got / limit * 1000 - (Date.now() - t0); if (ahead > 15) await new Promise(r => setTimeout(r, ahead)); }
    if (out) { if (!out.write(Buffer.from(value))) await new Promise(r => out.once('drain', r)); } else parts.push(Buffer.from(value));
  }
  if (out) { await new Promise((r, j) => out.end(err => err ? j(err) : r())); return null; }
  return Buffer.concat(parts);
}

async function install(id, opts) {
  opts = opts || {}; id = bfKey(id);
  if (!validId(id)) throw new Error('لعبة غير معروفة');
  if (busy.has(id)) return;
  busy.add(id);
  let stage = null, tmpZip = null;
  try {
    const info = keyInfo(id), man = await fetchManifest(), m = man.games && man.games[info.game];
    if (!m) throw new Error('اللعبة مو موجودة في القائمة');
    // BLOCKFALL's own launcher program has one fixed folder and save place on the PC: it can't be a second, separate copy
    if (isCon(id) && m.type === 'bf-launcher') throw new Error('هذي اللعبة على البي سي بس');
    const edition = info.edition;
    let received = 0, last = 0, total = 0;
    const tick = n => { received += n; const now = Date.now(); if (now - last > 150) { last = now; sendProg({ id, phase: 'download', received, total: total || received }); } };
    let entry = m.entry, version;

    const V = m.type === 'bf-launcher' ? bfv(info.game) : null;
    if (m.type === 'bf-launcher' && fs.existsSync(V.exe) && !readReg()[id] && !opts.force) {
      // the launcher is already on this PC (installed before): use it
      try { version = (await fetchJSON(m.parts)).version; } catch (e) { version = ''; }
      entry = V.image;
    } else if (m.type === 'bf-launcher') {
      // BLOCKFALL's official launcher package: encrypted parts on its CDN, unlocked with the studio's access code.
      const P = await fetchJSON(m.parts);
      if (P.enc && !opts.code) throw new Error('تحميل اللانشر يحتاج كود، تواصل مع الاستوديو'); total = P.zipSize || P.size; version = P.version;
      sendProg({ id, phase: 'download', received: 0, total });
      const base = m.site || new URL('..', m.parts).toString();   // part URLs are relative to the game's site root
      const chunks = [];
      for (const part of P.parts) chunks.push(await download(new URL(part.url, base).toString(), tick));
      sendProg({ id, phase: 'extract', received, total: received });
      const zip = P.enc ? decryptBf(Buffer.concat(chunks), P, opts.code) : Buffer.concat(chunks);
      if (P.sha1 && crypto.createHash('sha1').update(zip).digest('hex') !== P.sha1) throw new Error('الملف وصل ناقص، جرّب مرة ثانية');
      tmpZip = path.join(app.getPath('temp'), `ps-${id}-${Date.now()}.zip`);
      await fsp.writeFile(tmpZip, zip);
      stage = V.dir + '.new';
      await fsp.rm(stage, { recursive: true, force: true });
      await require('extract-zip')(tmpZip, { dir: stage });
      const inner = path.join(stage, m.zipRoot || '');
      if (process.platform === 'win32') { try { require('child_process').execSync(`taskkill /IM ${V.image} /F`, { stdio: 'ignore' }); } catch (e) {} }
      await fsp.rm(V.dir, { recursive: true, force: true });
      await fsp.rename(inner, V.dir);
      await bfAppUpdate(m, info.game);   // the launcher program is current before it ever runs
      await fsp.rm(stage, { recursive: true, force: true }); stage = null;
      entry = V.image;
    } else if (m.type === 'manifest') {
      // The game's own update channel: a manifest plus loose files (same source the game's standalone app uses).
      const j = await fetchJSON(m.base + 'manifest.json'); version = j.version; total = m.installSize || 0;
      sendProg({ id, phase: 'download', received: 0, total });
      stage = gameDir(id) + '.new';
      await fsp.rm(stage, { recursive: true, force: true });
      for (const f of j.files) {
        if (typeof f !== 'string' || f.includes('..') || path.isAbsolute(f)) throw new Error('ملف غير صالح في التحديث');
        const dest = path.join(stage, f);
        await fsp.mkdir(path.dirname(dest), { recursive: true });
        await download(m.base + f.split('/').map(encodeURIComponent).join('/'), tick, dest);
      }
      if (m.icon) await download(new URL(m.icon, SITE).toString(), () => {}, path.join(stage, 'icon.ico')).catch(() => {});
      sendProg({ id, phase: 'extract', received, total: received });
      await swapDir(stage, gameDir(id)); stage = null;
    } else if (m.type === 'pge') {
      // a game made with Premium Engine: version.json + one zip (checked with its sha256), unzipped into the game's folder.
      // Most come twice: the PC version and a console version (full controller play, console menus, at most 2 GB), which
      // goes into console\ inside the game's folder and is what runs in console mode.
      // The console installs only the console version (or the game itself when it has none), the PC only the PC version.
      const j = await fetchJSON(m.dist + 'version.json'); version = j.version;
      const bad = x => !x || typeof x !== 'object' || !x.file || /[\\/]|\.\./.test(x.file) || !x.exe || /[\\/]|\.\./.test(x.exe) || !/^[0-9a-f]{64}$/.test(x.sha256 || '') || /\.\./.test(x.zipRoot || '');
      const pick = isCon(id) && j.console ? j.console : j;
      if (bad(pick)) throw new Error('بيانات التحديث غير صالحة');
      total = +pick.size || 0;
      sendProg({ id, phase: 'download', received: 0, total });
      stage = gameDir(id) + '.new';
      await fsp.mkdir(path.dirname(stage), { recursive: true });
      await fsp.rm(stage, { recursive: true, force: true });
      tmpZip = path.join(app.getPath('temp'), `ps-${id}-${Date.now()}.zip`);
      await download(m.dist + encodeURIComponent(pick.file), tick, tmpZip);
      const h = crypto.createHash('sha256'); await new Promise((res, rej) => fs.createReadStream(tmpZip).on('data', d => h.update(d)).on('end', res).on('error', rej));
      if (h.digest('hex') !== pick.sha256) throw new Error('الملف وصل ناقص، جرّب مرة ثانية');
      const raw = stage + '.zip-out';
      await fsp.rm(raw, { recursive: true, force: true });
      await require('extract-zip')(tmpZip, { dir: raw });
      await fsp.rm(tmpZip, { force: true }).catch(() => {}); tmpZip = null;
      const inner = path.join(raw, pick.zipRoot || '');
      if (!(inner + path.sep).startsWith(raw + path.sep) || !fs.existsSync(path.join(inner, pick.exe))) { await fsp.rm(raw, { recursive: true, force: true }); throw new Error('اللعبة ناقصة في الملف'); }
      await fsp.rename(inner, stage);
      await fsp.rm(raw, { recursive: true, force: true });
      sendProg({ id, phase: 'extract', received, total: received });
      const old = pgeProcs.get(id); if (old) await stopGame(id).catch(() => {});
      await swapDir(stage, gameDir(id)); stage = null;
      entry = pick.exe;
    } else {
      throw new Error('نوع اللعبة غير معروف');
    }

    const reg = readReg(), bf = m.type === 'bf-launcher', one = bf || m.type === 'pge';   // one install for every edition
    reg[id] = { type: m.type, game: info.game, base: m.name, name: one ? m.name : edTitle(m.name, edition), version, entry, demoPatch: Array.isArray(m.demoPatch) ? m.demoPatch : undefined, account: m.account || undefined, icon: bf ? 'resources/app/icon.ico' : m.type === 'pge' ? entry : 'icon.ico', edition: one ? undefined : edition,
      size: await dirSize(bf ? V.dir : gameDir(id, m)), installedAt: Date.now(), lastPlayed: (readReg()[id] || {}).lastPlayed };
    if (bf) writeBfAccount();
    await writeReg(reg);
    makeShortcuts(id, reg[id]);
    sendProg({ id, phase: 'done' });
    if (isCon(id) === consoleOn) send('ps:installed-changed', {}); updateTray();
  } catch (e) {
    if (stage) await fsp.rm(stage, { recursive: true, force: true }).catch(() => {});
    sendProg({ id, phase: 'error', error: e.message || String(e) });
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
  id = bfKey(id);
  if (!validId(id)) throw new Error('لعبة غير معروفة');
  const w = gameWins.get(id); if (w && !w.isDestroyed()) w.close();
  if (pgeProcs.has(id)) await stopGame(id);
  const reg = readReg(), g = reg[id];
  if (g && g.type === 'bf-launcher') {
    const V = bfv(g.game || id);
    try { require('child_process').execSync(`taskkill /IM ${V.image} /F`, { stdio: 'ignore' }); } catch (e) {}
    await fsp.rm(V.dir, { recursive: true, force: true });
    for (const p of [path.join(app.getPath('desktop'), V.name + '.lnk'), path.join(app.getPath('appData'), 'Microsoft', 'Windows', 'Start Menu', 'Programs', V.name + '.lnk')]) { try { fs.rmSync(p, { force: true }); } catch (e) {} }
  } else {
    await fsp.rm(gameDir(id), { recursive: true, force: true });
  }
  if (g && !isCon(id)) removeShortcuts(g);   // the PC's icons belong to the PC's copy
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
    path.join(app.getPath('appData'), 'Microsoft', 'Windows', 'Start Menu', 'Programs', 'Premium Games', safe + '.lnk')
  ];
}
function makeShortcuts(id, g) {
  if (process.platform !== 'win32' || isCon(id)) return;   // the console's games have no icons outside the console
  for (const p of shortcutPaths(g)) {
    try {
      fs.mkdirSync(path.dirname(p), { recursive: true });
      shell.writeShortcutLink(p, 'create', {
        target: process.execPath, args: `--launch=${id}`, cwd: path.dirname(process.execPath),
        icon: path.join(gameDir(id, g), g.icon || 'icon.ico'), iconIndex: 0, description: g.name + ' — Premium Games'
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
      // Play under the Premium Games account: the game's saved player name is the username, and its name box is locked
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
var f=${J(a.nickField)};if(!f)return;var lock=function(){var i=document.getElementById(f);if(i&&!i.readOnly){i.readOnly=true;i.value=u.username;i.title='Premium Games';i.style.opacity='.75';}};
new MutationObserver(lock).observe(document.documentElement,{childList:true,subtree:true});})();</script>`;
}

/* ---------------- IPC ---------------- */
function guard(fn) {
  return async (e, ...args) => {
    if (!isTrusted(e.senderFrame && e.senderFrame.url)) throw new Error('غير مسموح');
    return fn(...args);
  };
}
/* ---------------- Premium Engine: install, update and launch it from Premium Games (like Epic Games Launcher with Unreal) ----------------
   Versions:  engine/versions.json on the site (written by the engine's Windows build)
   Installed: %LOCALAPPDATA%\PremiumStudio\engine\<version>\PremiumEngine.exe   (list in engine.json)
   Projects:  Documents\PremiumEngine\Projects\<Name>\<Name>.pge.json (+ Saved\thumbnail.bmp) — where the editor saves them */
const ENGINE_DIR = path.join(LOCAL, 'PremiumStudio', 'engine');
const ENGINE_REG = path.join(ENGINE_DIR, 'engine.json');
const ENGINE_ID = 'premium-engine';
const PROJECTS_DIR = () => path.join(app.getPath('documents'), 'PremiumEngine', 'Projects');
const validVer = v => typeof v === 'string' && /^\d{2}\.\d{2}\.\d{2}$/.test(v);
const engineReg = () => { try { const j = JSON.parse(fs.readFileSync(ENGINE_REG, 'utf8')); return j && j.versions ? j : { versions: {} }; } catch (e) { return { versions: {} }; } };
const engineExe = v => path.join(ENGINE_DIR, v, 'PremiumEngine.exe');
const engineRunning = new Map();   // child pid -> { version, project }
function engineInstalled() {
  const r = engineReg(), out = {};
  for (const [v, info] of Object.entries(r.versions)) if (validVer(v) && fs.existsSync(engineExe(v))) out[v] = info;
  return out;
}
async function saveEngineReg(fn) { const r = engineReg(); fn(r); await fsp.mkdir(ENGINE_DIR, { recursive: true }); const t = ENGINE_REG + '.tmp'; await fsp.writeFile(t, JSON.stringify(r, null, 1)); await fsp.rename(t, ENGINE_REG); }
async function engineVersions() { const j = await fetchJSON(SITE + 'engine/versions.json'); return { latest: j.latest || '', versions: (j.versions || []).filter(v => validVer(v.version)) }; }
function engineChanged() { send('eng:changed', null); }

async function engineProjects() {
  const out = [], root = PROJECTS_DIR();
  let dirs = []; try { dirs = await fsp.readdir(root, { withFileTypes: true }); } catch (e) { return out; }
  for (const d of dirs) {
    if (!d.isDirectory()) continue;
    const dir = path.join(root, d.name); let files = []; try { files = await fsp.readdir(dir); } catch (e) { continue; }
    for (const f of files.filter(f => f.toLowerCase().endsWith('.pge.json'))) {
      const file = path.join(dir, f);
      try {
        const st = await fsp.stat(file), j = JSON.parse(await fsp.readFile(file, 'utf8'));
        let thumb = '', thumbAt = 0; const tf = path.join(dir, 'Saved', 'thumbnail.bmp');
        try { const ts = await fsp.stat(tf); if (ts.size < 4e6) { thumb = 'data:image/bmp;base64,' + (await fsp.readFile(tf)).toString('base64'); thumbAt = ts.mtimeMs; } } catch (e) {}
        const levels = Array.isArray(j.levelOrder) ? j.levelOrder.length : Object.keys(j.levels || {}).length;
        out.push({ file, name: String(j.name || f.replace(/\.pge\.json$/i, '')).slice(0, 80), template: String(j.template || ''), engine: String(j.engine || ''), levels, modified: st.mtimeMs, thumb, thumbAt });
      } catch (e) {}
    }
  }
  return out.sort((a, b) => b.modified - a.modified);
}
const engineProjectOk = f => typeof f === 'string' && f.toLowerCase().endsWith('.pge.json') && path.resolve(f).startsWith(path.resolve(PROJECTS_DIR()) + path.sep) && fs.existsSync(f);

async function engineInstall(ver) {
  if (busy.has(ENGINE_ID)) return; busy.add(ENGINE_ID);
  let tmp = null, stage = null;
  try {
    const list = await engineVersions(); ver = ver || list.latest;
    const v = list.versions.find(x => x.version === ver); if (!v) throw new Error('هذي النسخة مو موجودة');
    if (engineRunningVersion(ver)) throw new Error('اقفل المحرك أول');
    let received = 0, last = 0; const total = v.size || 0;
    send('ps:progress', { id: ENGINE_ID, phase: 'download', received: 0, total, name: 'Premium Engine ' + ver });
    tmp = path.join(app.getPath('temp'), `pe-${ver}-${Date.now()}.zip`);
    await download(v.url, n => { received += n; const now = Date.now(); if (now - last > 150) { last = now; send('ps:progress', { id: ENGINE_ID, phase: 'download', received, total: total || received }); } }, tmp);
    send('ps:progress', { id: ENGINE_ID, phase: 'extract', received, total: received });
    if (v.sha256) { const h = crypto.createHash('sha256'); await new Promise((res, rej) => fs.createReadStream(tmp).on('data', d => h.update(d)).on('end', res).on('error', rej));
      if (h.digest('hex') !== v.sha256) throw new Error('الملف وصل ناقص، جرّب مرة ثانية'); }
    const dir = path.join(ENGINE_DIR, ver); stage = dir + '.new';
    await fsp.rm(stage, { recursive: true, force: true });
    await require('extract-zip')(tmp, { dir: stage });
    const inner = fs.existsSync(path.join(stage, 'PremiumEngine', 'PremiumEngine.exe')) ? path.join(stage, 'PremiumEngine') : stage;
    if (!fs.existsSync(path.join(inner, 'PremiumEngine.exe'))) throw new Error('ملف المحرك ناقص');
    await fsp.rm(dir, { recursive: true, force: true });
    await fsp.rename(inner, dir);
    await saveEngineReg(r => { r.versions[ver] = { installedAt: Date.now(), size: v.size || 0 }; });
    makeEngineShortcut(ver);
    send('ps:progress', { id: ENGINE_ID, phase: 'done', received, total: received });
    engineChanged();
  } catch (e) { send('ps:progress', { id: ENGINE_ID, phase: 'error', error: String(e && e.message || e) }); throw e; }
  finally { busy.delete(ENGINE_ID); if (tmp) fsp.rm(tmp, { force: true }).catch(() => {}); if (stage) fsp.rm(stage, { recursive: true, force: true }).catch(() => {}); }
}
// one Desktop / Start menu icon for the newest installed engine
function makeEngineShortcut(ver) {
  if (process.platform !== 'win32') return;
  const exe = engineExe(ver), opts = { target: exe, cwd: path.dirname(exe), icon: exe, iconIndex: 0, description: 'Premium Engine ' + ver };
  for (const dir of [app.getPath('desktop'), path.join(app.getPath('appData'), 'Microsoft', 'Windows', 'Start Menu', 'Programs')]) {
    try { shell.writeShortcutLink(path.join(dir, 'Premium Engine.lnk'), fs.existsSync(path.join(dir, 'Premium Engine.lnk')) ? 'replace' : 'create', opts); } catch (e) {}
  }
}
function removeEngineShortcutsIfTarget(ver) {
  if (process.platform !== 'win32') return;
  for (const dir of [app.getPath('desktop'), path.join(app.getPath('appData'), 'Microsoft', 'Windows', 'Start Menu', 'Programs')]) {
    const l = path.join(dir, 'Premium Engine.lnk');
    try { const real = f => { try { return fs.realpathSync.native(f).toLowerCase(); } catch (e) { return path.resolve(f).toLowerCase(); } };   // long names (no RUNNER~1)
      if (real(shell.readShortcutLink(l).target) === real(engineExe(ver))) fs.rmSync(l, { force: true }); } catch (e) {}
  }
}
function engineRunningVersion(ver) { for (const r of engineRunning.values()) if (r.version === ver) return true; return false; }
function engineLaunch(ver, args, project) {
  const exe = engineExe(ver); if (!fs.existsSync(exe)) throw new Error('النسخة مو مثبتة');
  const { spawn } = require('child_process');
  const child = spawn(exe, args, { cwd: path.dirname(exe), detached: true, stdio: 'ignore', windowsHide: false });
  child.on('error', () => {});
  engineRunning.set(child.pid, { version: ver, project: project || '' }); engineChanged();
  child.on('exit', () => { engineRunning.delete(child.pid); engineChanged(); });
  child.unref();
  return true;
}
async function engineStatus() {
  let list = { latest: '', versions: [] }, online = true; try { list = await engineVersions(); } catch (e) { online = false; }
  const inst = engineInstalled();
  return { online, latest: list.latest, available: list.versions.map(v => ({ version: v.version, size: v.size || 0, at: v.at || '' })),
    installed: Object.keys(inst).sort((a, b) => b.localeCompare(a, 'en', { numeric: true })).map(v => ({ version: v, installedAt: inst[v].installedAt || 0, dir: path.join(ENGINE_DIR, v) })),
    running: [...engineRunning.values()], projects: await engineProjects(), projectsDir: PROJECTS_DIR() };
}
ipcMain.handle('eng:status', guard(async () => engineStatus()));
ipcMain.handle('eng:install', guard(async ver => { if (ver != null && !validVer(ver)) throw new Error('bad version'); await engineInstall(ver); return true; }));
ipcMain.handle('eng:uninstall', guard(async ver => {
  if (!validVer(ver)) throw new Error('bad version'); if (engineRunningVersion(ver)) throw new Error('اقفل المحرك أول');
  removeEngineShortcutsIfTarget(ver);
  await fsp.rm(path.join(ENGINE_DIR, ver), { recursive: true, force: true }); await saveEngineReg(r => { delete r.versions[ver]; });
  const left = Object.keys(engineInstalled()).sort((a, b) => b.localeCompare(a, 'en', { numeric: true })); if (left.length) makeEngineShortcut(left[0]);
  engineChanged(); return true; }));
ipcMain.handle('eng:launch', guard(async (ver, project) => {
  if (!validVer(ver)) throw new Error('bad version');
  if (project != null && !engineProjectOk(project)) throw new Error('المشروع مو موجود');
  return engineLaunch(ver, project ? [project] : [], project); }));
ipcMain.handle('eng:new', guard(async (ver, templ, name) => {
  if (!validVer(ver)) throw new Error('bad version');
  if (!['Blank', 'FirstPerson', 'ThirdPerson'].includes(templ)) throw new Error('قالب غير معروف');
  name = String(name || '').trim(); if (!name || name.length > 40 || /[\\/:*?"<>|]/.test(name)) throw new Error('اسم المشروع ما يصلح');
  // same folder name the editor makes (sanitizeName): ASCII symbols → '_', other letters stay, max 40 UTF-8 bytes
  let folder = name.replace(/[\x00-\x7f]/g, c => /[A-Za-z0-9_-]/.test(c) ? c : '_'); while (Buffer.byteLength(folder) > 40) folder = [...folder].slice(0, -1).join('');
  if (fs.existsSync(path.join(PROJECTS_DIR(), folder))) throw new Error('فيه مشروع بنفس الاسم');
  return engineLaunch(ver, ['--new', templ, name], ''); }));
ipcMain.handle('eng:showFolder', guard(async what => {
  if (what === 'projects') { await fsp.mkdir(PROJECTS_DIR(), { recursive: true }); shell.openPath(PROJECTS_DIR()); }
  else if (validVer(what)) shell.openPath(path.join(ENGINE_DIR, what));
  else if (engineProjectOk(what)) shell.showItemInFolder(what);
  return true; }));
// projects change while the editor is open (save, new project): tell the page
let engWatch = null;
function watchProjects() { if (engWatch) return; try { fs.mkdirSync(PROJECTS_DIR(), { recursive: true }); let t = null;
  engWatch = fs.watch(PROJECTS_DIR(), { recursive: true }, () => { clearTimeout(t); t = setTimeout(engineChanged, 600); }); } catch (e) {} }
app.whenReady().then(watchProjects);

// free / total space of the drive the games are installed on (console mode: "تخزين الجهاز")
ipcMain.handle('ps:disk', guard(async () => {
  let dir = path.join(LOCAL, 'PremiumStudio'); while (!fs.existsSync(dir) && path.dirname(dir) !== dir) dir = path.dirname(dir);
  const st = await fsp.statfs(dir); return { free: st.bavail * st.bsize, total: st.blocks * st.bsize };
}));
ipcMain.handle('ps:getInstalled', guard(async () => installedFor(consoleOn)));
ipcMain.handle('ps:install', guard(async (id, opts) => { await install(fromPage(id), opts); return true; }));
ipcMain.handle('ps:getLatest', guard(async () => getLatest()));
ipcMain.handle('ps:uninstall', guard(async id => { await uninstall(fromPage(id)); return true; }));
ipcMain.handle('ps:launch', guard(async (id, edition, opts) => { const ver = opts && typeof opts.version === 'string' && /^[\w.\-]{1,24}$/.test(opts.version) ? opts.version : '';
  await launchWithUpdate(fromPage(id), EDITIONS.includes(edition) ? edition : undefined, { version: ver }); return true; }));
ipcMain.handle('ps:features', guard(async () => ({ editions: true, running: true })));
ipcMain.handle('ps:getRunning', guard(async () => runningList()));
ipcMain.handle('ps:stop', guard(async id => { id = fromPage(id); if (!validId(id)) throw new Error('bad id'); stopGame(id); return true; }));
ipcMain.handle('ps:showFolder', guard(async id => { id = fromPage(id); if (validId(id)) shell.openPath(gameDir(id, readReg()[id])); return true; }));
ipcMain.handle('ps:setUser', guard(async u => { await setUser(u); return true; }));
// the editions the player owns, per game (BLOCKFALL's launcher lets them pick among them)
ipcMain.handle('ps:setOwned', guard(async o => {
  if (!currentUser || !o || typeof o !== 'object') return false;
  const owned = {}; for (const [g, eds] of Object.entries(o)) if (validId(g) && Array.isArray(eds)) owned[g] = eds.filter(e => EDITIONS.includes(e));
  if (JSON.stringify(owned) === JSON.stringify(currentUser.owned || {})) return true;
  currentUser.owned = owned; try { fs.writeFileSync(USER_FILE, JSON.stringify(currentUser)); } catch (e) {} writeBfAccount(); return true;
}));
const pubSettings = x => ({ limit: x.limit || 0, parental: parentalPublic(), consoleBoot: !!x.consoleBoot, consolePad: x.consolePad !== false, startup: x.startup !== false, sounds: x.sounds !== false, glyphs: x.glyphs || 'auto' });
ipcMain.handle('ps:getSettings', guard(async () => pubSettings(settings())));
/* ---- parental controls (on this PC): a PIN, which games are allowed, and minutes per day ---- */
const pinHash = p => crypto.createHash('sha256').update('ps-parental:' + String(p || '')).digest('hex');
const today = () => new Date().toISOString().slice(0, 10);
function parental() { const x = settings().parental || {}; return { on: !!x.on, pin: x.pin || '', allowed: Array.isArray(x.allowed) ? x.allowed : [], daily: +x.daily || 0, used: x.day === today() ? (+x.used || 0) : 0 }; }
function parentalPublic() { const p = parental(); return { on: p.on, hasPin: !!p.pin, allowed: p.allowed, daily: p.daily, used: Math.round(p.used) }; }
function saveParental(p) { const x = settings(); x.parental = { on: p.on, pin: p.pin, allowed: p.allowed, daily: p.daily, used: p.used, day: today() }; fs.writeFileSync(SETTINGS_FILE(), JSON.stringify(x)); }
function parentalBlocks(gid) {
  const p = parental(); if (!p.on) return '';
  if (p.allowed.length && !p.allowed.includes(gid)) return 'هذي اللعبة مقفولة بالرقابة الأبوية';
  if (p.daily && p.used >= p.daily) return 'خلص وقت اللعب المسموح اليوم';
  return '';
}
ipcMain.handle('ps:setParental', guard(async o => {
  const p = parental();
  if (p.pin && pinHash(o && o.pin) !== p.pin) throw new Error('الرقم السري غلط');
  if (o.newPin != null) { if (!/^\d{4,8}$/.test(String(o.newPin))) throw new Error('الرقم السري من 4 إلى 8 أرقام'); p.pin = pinHash(o.newPin); }
  if ('on' in o) { if (o.on && !p.pin) throw new Error('حط رقم سري أول'); p.on = !!o.on; }
  if (Array.isArray(o.allowed)) p.allowed = o.allowed.filter(validId).slice(0, 50);
  if ('daily' in o) p.daily = Math.max(0, Math.min(1440, +o.daily || 0));
  saveParental(p); return parentalPublic();
}));
// count today's play time and close games when the day's time is over
setInterval(() => {
  const p = parental(); if (!p.on || !running.size) return;
  p.used += 1; saveParental(p);
  if (p.daily && p.used >= p.daily) { flash('⏰ خلص وقت اللعب اليوم'); for (const k of [...running.keys()]) stopGame(k); }
}, 60000);
ipcMain.handle('ps:setSettings', guard(async o => { const cur = settings(); if (o && typeof o === 'object') { if ('limit' in o) cur.limit = Math.max(0, Math.min(1e9, +o.limit || 0));
    for (const k of ['consoleBoot', 'consolePad', 'startup', 'sounds']) if (k in o) cur[k] = !!o[k];
    if ('glyphs' in o && ['auto', 'xbox', 'ps'].includes(o.glyphs)) cur.glyphs = o.glyphs; }
  fs.writeFileSync(SETTINGS_FILE(), JSON.stringify(cur)); applyStartup(cur); return pubSettings(cur); }));
// start with Windows (like a console that's always ready); with console mode on, the PC boots straight into it
function applyStartup(x) { if (!app.isPackaged || process.platform !== 'win32') return; x = x || settings();
  try { app.setLoginItemSettings({ openAtLogin: x.startup !== false, args: x.consoleBoot ? ['--hidden', '--console'] : ['--hidden'] }); } catch (e) {} }
// power menu in console mode: sleep / restart / shut down the PC, like a console
ipcMain.handle('ps:power', guard(async what => power(what)));
async function power(what) {
  if (process.platform !== 'win32') throw new Error('غير مدعوم');
  const { execFile } = require('child_process');
  if (what === 'sleep') execFile('rundll32.exe', ['powrprof.dll,SetSuspendState', '0,1,0']);
  else if (what === 'restart' || what === 'shutdown') { if (running.size) await Promise.all([...running.keys()].map(k => stopGame(k)));
    execFile('shutdown.exe', [what === 'restart' ? '/r' : '/s', '/t', '0']); }
  else if (what === 'quit') setTimeout(() => quitApp(), 50);
  else if (what === 'desktop') { if (mainWin && !mainWin.isDestroyed()) { mainWin.setFullScreen(false); mainWin.minimize(); } }
  else throw new Error('أمر غير معروف');
  return true; }
// back to the console screen when a game closes (like a console does)
ipcMain.handle('ps:focusMain', guard(async () => { if (mainWin && !mainWin.isDestroyed()) { if (mainWin.isMinimized()) mainWin.restore(); mainWin.show(); mainWin.focus(); } return true; }));
// game properties: check the files (download again what's wrong or missing), put the icon back on the desktop
ipcMain.handle('ps:verify', guard(async id => { id = fromPage(id); if (!validId(id) || !readReg()[id]) throw new Error('مو مثبتة'); if (running.has(id)) throw new Error('اقفل اللعبة أول'); await install(id, { force: true, edition: readReg()[id].edition }); return true; }));
ipcMain.handle('ps:shortcut', guard(async id => { id = fromPage(id); if (isCon(id)) throw new Error('ألعاب الكونسل تشتغل من الكونسل بس'); const g = readReg()[id]; if (!g) throw new Error('مو مثبتة'); makeShortcuts(id, g); return true; }));
/* ---- Workshop: share BLOCKFALL worlds; subscribed ones go into your worlds next time you play ---- */
ipcMain.handle('ps:myWorlds', guard(async () => {
  const dir = path.join(BF_DATA, 'worlds_export'); let files = []; try { files = (await fsp.readdir(dir)).filter(f => f.endsWith('.json')); } catch (e) { return []; }
  const out = []; for (const f of files) { try { const x = JSON.parse(await fsp.readFile(path.join(dir, f), 'utf8')); out.push({ id: x.meta.id, name: x.meta.name, mode: x.meta.mode, size: x.data.length }); } catch (e) {} }
  return out;
}));
ipcMain.handle('ps:workshopUpload', guard(async (worldId, title, desc) => {
  if (!currentUser || !currentUser.uid) throw new Error('سجّل دخولك');
  const f = path.join(BF_DATA, 'worlds_export', String(worldId).replace(/[^a-z0-9]/gi, '') + '.json');
  const x = JSON.parse(await fsp.readFile(f, 'utf8'));
  const b64 = zlib.gzipSync(Buffer.from(x.data, 'utf8'), { level: 9 }).toString('base64'); if (b64.length > 8e6) throw new Error('العالم كبير مرة (الحد 6 ميجا مضغوط)');
  const parts = []; for (let i = 0; i < b64.length; i += 700000) parts.push(b64.slice(i, i + 700000));
  const id = 'w' + Date.now().toString(36) + crypto.randomBytes(3).toString('hex');
  const t = await fbToken(); if (!t) throw new Error('ما قدرت أوصل لحسابك');
  const writes = [{ update: { name: `${FB_DB}/workshop/${id}`, fields: { game: { stringValue: 'blockfall' }, author: { stringValue: currentUser.uid }, authorName: { stringValue: currentUser.username || '' },
      title: { stringValue: String(title || x.meta.name || 'عالم').slice(0, 60) }, desc: { stringValue: String(desc || '').slice(0, 600) }, mode: { stringValue: String(x.meta.mode || 'c').slice(0, 4) },
      parts: { integerValue: String(parts.length) }, size: { integerValue: String(x.data.length) }, subs: { integerValue: '0' } } }, updateTransforms: [{ fieldPath: 'at', setToServerValue: 'REQUEST_TIME' }], currentDocument: { exists: false } }];
  parts.forEach((d, i) => writes.push({ update: { name: `${FB_DB}/workshop/${id}/parts/${i}`, fields: { d: { stringValue: d } } } }));
  const r = await net.fetch(`https://firestore.googleapis.com/v1/${FB_DB}:commit`, { method: 'POST', headers: { authorization: 'Bearer ' + t, 'content-type': 'application/json' }, body: JSON.stringify({ writes }) });
  if (!r.ok) throw new Error('ما ارتفع (' + r.status + ')');
  return id;
}));
ipcMain.handle('ps:workshopGet', guard(async (id, name, mode) => {
  if (!/^[a-z0-9]{4,40}$/i.test(String(id))) throw new Error('bad id');
  const t = await fbToken(); if (!t) throw new Error('ما قدرت أوصل لحسابك');
  const meta = await fbGet(`workshop/${id}`); if (!meta) throw new Error('انحذف من الورشة');
  const n = +meta.fields.parts.integerValue; let b64 = '';
  for (let i = 0; i < n; i++) { const d = await fbGet(`workshop/${id}/parts/${i}`); if (!d) throw new Error('ناقص'); b64 += d.fields.d.stringValue; }
  const data = zlib.gunzipSync(Buffer.from(b64, 'base64')).toString('utf8');
  const dir = path.join(BF_DATA, 'workshop_import'); await fsp.mkdir(dir, { recursive: true });
  await fsp.writeFile(path.join(dir, id + '.json'), JSON.stringify({ name: String(name || meta.fields.title.stringValue).slice(0, 40), mode: mode || meta.fields.mode.stringValue, data }));
  return true;
}));

// Remote Play Together: which game window the store may share, and the friend's keys/mouse for it
let shareWin = '';
ipcMain.handle('ps:shareGame', guard(async id => { id = fromPage(id); if (!validId(id) || !gameWins.has(id)) throw new Error('شغّل اللعبة أول'); shareWin = id; return true; }));
ipcMain.handle('ps:remoteInput', guard(async (id, ev) => { id = fromPage(id);
  const w = gameWins.get(id); if (!w || w.isDestroyed() || id !== shareWin || !ev || typeof ev !== 'object') return false;
  const T = ['keyDown', 'keyUp', 'char', 'mouseDown', 'mouseUp', 'mouseMove', 'mouseWheel'];
  if (!T.includes(ev.type)) return false;
  const [cw, ch] = w.getContentSize(), e = { type: ev.type };
  if (ev.type.startsWith('mouse')) { e.x = Math.round(Math.max(0, Math.min(1, +ev.x || 0)) * cw); e.y = Math.round(Math.max(0, Math.min(1, +ev.y || 0)) * ch); e.button = ['left', 'middle', 'right'][+ev.button || 0] || 'left'; e.clickCount = 1;
    if (ev.type === 'mouseWheel') { e.deltaX = +ev.dx || 0; e.deltaY = -(+ev.dy || 0); } }
  else e.keyCode = String(ev.key || '').slice(0, 20);
  try { w.webContents.sendInputEvent(e); } catch (er) {} return true;
}));
ipcMain.handle('ps:fullscreen', guard(async on => { if (mainWin && !mainWin.isDestroyed()) { mainWin.setFullScreen(!!on); if (on) { mainWin.show(); mainWin.focus(); } } return true; }));
/* ---- TV apps in console mode (YouTube, like on the PS5): the site's TV version in its own full-screen window,
   driven by the controller. Its sign-in is kept in its own storage. Home / F1 / the controller's guide button closes it. ---- */
const TV_UA = 'Mozilla/5.0 (PS4; Leanback Shell) Gecko/20100101 Firefox/65.0 LeanbackShell/01.00.01.75 Sony PS4/ (PS4, , no, CH)';
const TV_APPS = { youtube: { url: 'https://www.youtube.com/tv', title: 'YouTube', partition: 'persist:tv-youtube', hosts: /(^|\.)(youtube\.com|google\.com|googlevideo\.com|ytimg\.com|gstatic\.com|youtube-nocookie\.com|youtu\.be)$/ } };
let tvWin = null;
function closeTv() { if (tvWin && !tvWin.isDestroyed()) tvWin.close(); }
function openTv(app) {
  const a = TV_APPS[app]; if (!a) throw new Error('bad app');
  if (tvWin && !tvWin.isDestroyed()) { tvWin.show(); tvWin.focus(); return; }
  const w = tvWin = new BrowserWindow({ fullscreen: true, backgroundColor: '#000000', title: a.title, autoHideMenuBar: true, show: false,
    webPreferences: { partition: a.partition, preload: path.join(__dirname, 'tv-preload.js'), contextIsolation: true, sandbox: true, nodeIntegration: false, backgroundThrottling: false } });
  w.tvApp = app; w.setMenu(null);
  w.webContents.setUserAgent(TV_UA);
  w.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  w.webContents.on('will-navigate', (e, url) => { try { if (!a.hosts.test(new URL(url).hostname)) e.preventDefault(); } catch (x) { e.preventDefault(); } });
  w.webContents.on('before-input-event', (e, i) => { if (i.type === 'keyDown' && (i.key === 'Home' || i.key === 'F1')) { e.preventDefault(); closeTv(); } });
  w.once('ready-to-show', () => { if (!w.isDestroyed()) { w.show(); w.focus(); } });
  w.on('closed', () => { if (tvWin === w) tvWin = null;
    if (mainWin && !mainWin.isDestroyed()) { mainWin.show(); mainWin.focus(); mainWin.webContents.send('tv:closed', app); } });
  w.loadURL(a.url);
}
ipcMain.handle('tv:open', guard(async app => { openTv(String(app || '')); return true; }));
ipcMain.handle('tv:close', guard(async () => { closeTv(); return true; }));
ipcMain.handle('tv:state', guard(async () => tvWin && !tvWin.isDestroyed() ? { app: tvWin.tvApp, url: tvWin.webContents.getURL(), title: tvWin.webContents.getTitle(), ua: tvWin.webContents.getUserAgent(), visible: tvWin.isVisible() } : null));
// controller buttons from the TV window become real key presses (the TV site is driven by the keyboard)
const TV_KEYS = { up: 'Up', down: 'Down', left: 'Left', right: 'Right', a: 'Enter', b: 'Escape', x: 'MediaPlayPause', lb: 'MediaPreviousTrack', rb: 'MediaNextTrack' };
ipcMain.on('tv:pad', (e, k) => {
  if (!tvWin || tvWin.isDestroyed() || e.sender !== tvWin.webContents) return;
  if (k === 'guide') return closeTv();
  const code = TV_KEYS[k]; if (!code) return;
  tvWin.webContents.sendInputEvent({ type: 'keyDown', keyCode: code });
  if (code === 'Enter') tvWin.webContents.sendInputEvent({ type: 'char', keyCode: '\r' });
  tvWin.webContents.sendInputEvent({ type: 'keyUp', keyCode: code });
});
ipcMain.handle('ps:retryOnline', guard(async () => { if (mainWin) mainWin.loadURL(SITE); return true; }));
// Game achievements: a game calls PREMIUM_STUDIO.achieve('id'); it's kept on the player's account
ipcMain.on('game:achieve', (e, aid) => {
  const key = [...gameWins].find(([, w]) => !w.isDestroyed() && w.webContents === e.sender);
  if (!key || !currentUser || !currentUser.uid || typeof aid !== 'string' || !/^[a-z0-9_-]{1,40}$/i.test(aid)) return;
  grantTrophy((readReg()[key[0]] || {}).game || key[0].split('--')[0], aid);
});
// a trophy on the player's account (once: it isn't written again)
const granted = new Set();
function grantTrophy(game, aid) {
  if (!currentUser || !currentUser.uid || !validId(game) || !/^[a-z0-9_-]{1,40}$/i.test(aid) || granted.has(currentUser.uid + game + aid)) return;
  granted.add(currentUser.uid + game + aid);
  fbCommit([{ update: { name: `${FB_DB}/users/${currentUser.uid}/achievements/${game}__${aid}`, fields: { game: { stringValue: game }, id: { stringValue: aid } } },
    currentDocument: { exists: false }, updateTransforms: [{ fieldPath: 'at', setToServerValue: 'REQUEST_TIME' }] }]).catch(() => {});
  if (consoleOn) csLog('trophy', { game, id: aid });
}
ipcMain.on('game:guide', (e, long) => { for (const [k, w] of gameWins) if (w && !w.isDestroyed() && w.webContents === e.sender) { if (consoleOn || w.consoleMode) { if (!nativePad()) homePressed(!!long); } else if (!long) toggleOverlay(k); break; } });
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
    // a game in its own program asks for the console's Control Center (Home button) or the home screen (Home held)
    if (kind === 'overlay' || kind === 'home') { if (running.size) homePressed(kind === 'home'); else createMain(consoleOn ? 'bigpicture' : ''); return; }
    if (kind === 'launch' && validId(id) && !isCon(id) && installedNow()[bfKey(id)]) return playFromOutside(bfKey(id));
    if (kind === 'play' && validId(id)) {
      const mine = Object.entries(installedFor(false)).filter(([k, v]) => (v.game || k.split('--')[0]) === id)
        .sort((a, b) => ((b[1].lastPlayed || 0) - (a[1].lastPlayed || 0)) || (EDITIONS.indexOf(b[1].edition) - EDITIONS.indexOf(a[1].edition)));
      if (mine.length) return playFromOutside(mine[0][0]);
      return createMain('library');
    }
    if (kind === 'install' && validId(id)) return createMain(id.split('--')[0], 'install=' + encodeURIComponent(id));
    createMain(validId(id) ? id : (kind === 'library' ? 'library' : ''));
  } catch (e) { createMain(); }
}
function handleArgv(argv, firstRun) {
  const launch = argValue(argv, 'launch');
  const link = argv.find(a => a.startsWith('premiumstudio://'));
  if (launch && validId(launch) && !isCon(launch) && installedNow()[bfKey(launch)]) { playFromOutside(bfKey(launch)); return true; }
  if (link) { handleDeepLink(link); return true; }
  const consoleBoot = argv.includes('--console') || (firstRun && settings().consoleBoot);
  if (consoleBoot) { createMain('bigpicture'); return true; }   // console mode: straight to the big screen
  if (firstRun && argv.includes('--hidden') && !argv.includes('--show')) return true;   // started with Windows: stay in the tray
  createMain(); return true;
}

/* ---------------- updates (like Steam) ----------------
   On start: check; if there's a new version, a small window shows the download, then it installs and restarts by itself.
   While running: checks every 30 minutes and installs as soon as no game is running or installing. */
const PENDING = path.join(app.getPath('userData'), 'pending-start.json');
function updaterWindow() {
  const w = new BrowserWindow({ width: 420, height: 170, frame: false, resizable: false, alwaysOnTop: true, show: false, backgroundColor: '#11151f', title: 'Premium Games', icon: ICON, webPreferences: { sandbox: true, contextIsolation: true } });
  w.loadURL('data:text/html;charset=utf-8,' + encodeURIComponent(`<!doctype html><html dir="rtl"><head><meta charset="utf-8"><style>
    *{box-sizing:border-box;margin:0}body{font-family:"Segoe UI",Tahoma,sans-serif;background:#11151f;color:#e8ecf3;height:100vh;padding:22px;border:1px solid #2c3446;-webkit-app-region:drag;user-select:none}
    .ps{display:inline-block;background:#000;border:1px solid #2c3446;border-radius:5px;padding:3px 8px;font:700 20px sans-serif;direction:ltr}.ps i{font-style:normal;color:#fff}.ps i+i{color:#ffcc1f}
    h1{font-size:17px;margin:14px 0 6px}#t{color:#9aa4b8;font-size:13px}.bar{height:5px;background:#0b0f16;margin-top:14px}.bar i{display:block;height:100%;width:0;background:#ffcc1f;transition:width .3s}
    </style></head><body><span class="ps"><i>P</i><i>G</i></span><h1>يتحدّث Premium Games</h1><div id="t">يتحقق من التحديثات…</div><div class="bar"><i id="b"></i></div></body></html>`));
  w.once('ready-to-show', () => { if (!w.isDestroyed()) w.show(); });
  const set = (t, pct) => { if (!w.isDestroyed()) w.webContents.executeJavaScript(`document.getElementById('t').textContent=${JSON.stringify(t)};${pct != null ? `document.getElementById('b').style.width='${pct}%'` : ''}`).catch(() => {}); };
  return { w, set };
}
async function startupUpdate(argv) {
  let autoUpdater; try { ({ autoUpdater } = require('electron-updater')); } catch (e) { return false; }
  autoUpdater.autoDownload = false; autoUpdater.autoInstallOnAppQuit = true;
  let info = null;
  try { const r = await Promise.race([autoUpdater.checkForUpdates(), new Promise(res => setTimeout(() => res(null), 8000))]); info = r && r.updateInfo; } catch (e) { return false; }
  if (!info || !info.version || !vgt(info.version, app.getVersion())) return false;
  const quiet = argv.includes('--hidden');
  const ui = quiet ? null : updaterWindow();
  if (ui) ui.set('ينزّل النسخة ' + info.version.split('.').map(n => n.padStart(2, '0')).join('.') + '…', 0);
  autoUpdater.on('download-progress', p => ui && ui.set(`ينزّل التحديث… ${Math.round(p.percent || 0)}%`, Math.round(p.percent || 0)));
  try {
    await Promise.race([autoUpdater.downloadUpdate(), new Promise((_, rej) => setTimeout(() => rej(new Error('timeout')), 10 * 60000))]);
  } catch (e) { if (ui && !ui.w.isDestroyed()) ui.w.destroy(); return false; }   // couldn't update now: open normally, try again later
  if (ui) ui.set('يثبّت التحديث ويرجع يفتح…', 100);
  try { fs.writeFileSync(PENDING, JSON.stringify({ at: Date.now(), argv: argv.filter(a => a.startsWith('--launch=') || a.startsWith('premiumstudio://') || a === '--hidden' || a === '--console') })); } catch (e) {}
  quitting = true;
  setTimeout(() => { try { autoUpdater.quitAndInstall(true, true); } catch (e) { app.relaunch(); app.exit(0); } }, 600);
  return true;
}
function backgroundUpdates() {
  let autoUpdater; try { ({ autoUpdater } = require('electron-updater')); } catch (e) { return; }
  autoUpdater.autoDownload = true; autoUpdater.autoInstallOnAppQuit = true;
  let ready = false;
  const apply = () => { if (!ready || running.size || busy.size || stopping.size) return;
    try { fs.writeFileSync(PENDING, JSON.stringify({ at: Date.now(), argv: ['--hidden'].concat(mainWin && !mainWin.isDestroyed() && mainWin.isVisible() ? ['--show'] : []) })); } catch (e) {}
    quitting = true; try { autoUpdater.quitAndInstall(true, true); } catch (e) {} };
  autoUpdater.on('update-downloaded', () => { ready = true; apply(); });
  setInterval(apply, 60000);
  const check = () => autoUpdater.checkForUpdates().catch(() => {});
  setTimeout(check, 60000); setInterval(check, 30 * 60000);
}

if (!app.requestSingleInstanceLock()) {
  app.quit();
} else {
  if (process.defaultApp && process.argv.length >= 2) app.setAsDefaultProtocolClient('premiumstudio', process.execPath, [path.resolve(process.argv[1])]);
  else app.setAsDefaultProtocolClient('premiumstudio');
  app.setAppUserModelId('com.premiumstudio.client');
  app.on('second-instance', (e, argv) => handleArgv(argv, false));
  app.whenReady().then(async () => {
    Menu.setApplicationMenu(null);
    registerGameProtocol();
    migrateBf();
    migrateReg();
    renameLegacy();
    // the store may use the microphone (voice chat) and share a game window (Remote Play); nothing else
    const { session, desktopCapturer } = require('electron');
    session.defaultSession.setPermissionRequestHandler((wc, perm, cb, d) => cb((recWin && !recWin.isDestroyed() && wc === recWin.webContents && perm === 'media') || (isTrusted(d && d.requestingUrl) && ['media', 'notifications', 'fullscreen', 'clipboard-sanitized-write'].includes(perm))));
    session.defaultSession.setPermissionCheckHandler((wc, perm, origin) => (recWin && !recWin.isDestroyed() && wc === recWin.webContents) || ((origin && origin.startsWith(SITE_ORIGIN)) ? ['media', 'notifications', 'fullscreen'].includes(perm) : false));
    session.defaultSession.setDisplayMediaRequestHandler(async (req, cb) => {
      try { const w = shareWin && gameWins.get(shareWin); if (!w || w.isDestroyed()) return cb({});
        const id = w.getMediaSourceId(), src = (await desktopCapturer.getSources({ types: ['window'] })).find(x => x.id === id);
        cb(src ? { video: src } : {}); } catch (e) { cb({}); }
    });
    if (Object.values(installedNow()).some(v => v.type === 'bf-launcher')) fixBfShortcuts();   // leftover icon from older installs
    makeTray();
    // like Steam: on start, bring Premium Games up to date first (download, restart), then do what was asked
    let argv = process.argv;
    try { const p = JSON.parse(fs.readFileSync(PENDING, 'utf8')); fs.rmSync(PENDING, { force: true }); if (Date.now() - p.at < 300000) argv = p.argv; } catch (e) {}
    if (app.isPackaged && await startupUpdate(argv)) return;
    handleArgv(argv, true);
    // start with Windows, quietly in the tray (like Steam)
    applyStartup();
    if (app.isPackaged) backgroundUpdates();
  });
  app.on('window-all-closed', () => { if (!tray) app.quit(); });   // otherwise keep running in the tray
  app.on('before-quit', () => { quitting = true; if (padProc) { try { padProc.kill(); } catch (e) {} } for (const k of [...running.keys()]) if (!gameWins.has(k)) { const r = running.get(k); addPlaytime(r, Date.now() - r.since); } });
}
