'use strict';
// Premium Studio desktop client: hosts the store, installs / launches / removes games.
const { app, BrowserWindow, ipcMain, shell, protocol, net, Menu } = require('electron');
const path = require('path');
const fs = require('fs');
const fsp = fs.promises;
const crypto = require('crypto');
const { pathToFileURL } = require('url');

const SITE = 'https://abdullra7manx-cmyk.github.io/premium-studio/';
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
const gameDir = id => path.join(GAMES_DIR, id);
const validId = id => typeof id === 'string' && /^[a-z0-9-]{1,40}$/.test(id);

function readReg() { try { return JSON.parse(fs.readFileSync(REG_FILE, 'utf8')); } catch (e) { return {}; } }
async function writeReg(reg) { await fsp.mkdir(GAMES_DIR, { recursive: true }); await fsp.writeFile(REG_FILE, JSON.stringify(reg, null, 2)); }
function installedNow() {
  const reg = readReg(), out = {};
  for (const [id, v] of Object.entries(reg)) if (fs.existsSync(path.join(gameDir(id), v.entry || ''))) out[id] = v;
  return out;
}

/* ---------------- settings (player name for games) ---------------- */
let currentUser = null;

/* ---------------- windows ---------------- */
let mainWin = null;
const gameWins = new Map();

function isTrusted(frameUrl) {
  if (!frameUrl) return false;
  if (frameUrl.startsWith('file://') && frameUrl.includes('offline.html')) return true;
  try { return new URL(frameUrl).origin === SITE_ORIGIN; } catch (e) { return false; }
}

function createMain(hash) {
  if (mainWin && !mainWin.isDestroyed()) {
    if (hash) mainWin.loadURL(SITE + '#' + hash);
    if (mainWin.isMinimized()) mainWin.restore();
    mainWin.show(); mainWin.focus(); return mainWin;
  }
  mainWin = new BrowserWindow({
    width: 1320, height: 840, minWidth: 900, minHeight: 600,
    backgroundColor: '#0a0d14', title: 'Premium Studio', icon: ICON, autoHideMenuBar: true, show: false,
    webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, sandbox: true, nodeIntegration: false }
  });
  mainWin.once('ready-to-show', () => mainWin.show());
  mainWin.webContents.setWindowOpenHandler(({ url }) => { openExternal(url); return { action: 'deny' }; });
  mainWin.webContents.on('will-navigate', (e, url) => {
    if (url.startsWith('premiumstudio://')) { e.preventDefault(); handleDeepLink(url); return; }
    if (!isTrusted(url) && !url.startsWith('https://premium-studio-f9bc9.firebaseapp.com')) { e.preventDefault(); openExternal(url); }
  });
  mainWin.webContents.on('did-fail-load', (e, code, desc, url, isMain) => {
    if (isMain && code !== -3) mainWin.loadFile(path.join(__dirname, 'offline.html'));
  });
  mainWin.loadURL(SITE + (hash ? '#' + hash : ''));
  mainWin.on('closed', () => { mainWin = null; });
  return mainWin;
}
function openExternal(url) { if (/^https?:\/\//.test(url)) shell.openExternal(url); }

function launchGame(id) {
  const reg = installedNow(), g = reg[id];
  if (!g) throw new Error('اللعبة مو مثبتة');
  if (gameWins.has(id) && !gameWins.get(id).isDestroyed()) { const w = gameWins.get(id); w.show(); w.focus(); return; }
  const w = new BrowserWindow({
    width: 1600, height: 900, backgroundColor: '#000000', title: g.name, icon: path.join(gameDir(id), g.icon || 'icon.ico'),
    autoHideMenuBar: true, show: false,
    webPreferences: { preload: path.join(__dirname, 'game-preload.js'), contextIsolation: false, sandbox: false, nodeIntegration: false, backgroundThrottling: false }
  });
  w.setMenu(null);
  w.once('ready-to-show', () => { w.maximize(); w.show(); });
  w.webContents.setWindowOpenHandler(({ url }) => { openExternal(url); return { action: 'deny' }; });
  w.webContents.on('before-input-event', (e, input) => {
    if (input.type === 'keyDown' && input.key === 'F11') { w.setFullScreen(!w.isFullScreen()); e.preventDefault(); }
  });
  w.loadURL(`psgame://${id}/${g.entry}`);
  w.on('closed', () => { gameWins.delete(id); if (!BrowserWindow.getAllWindows().length) app.quit(); });
  gameWins.set(id, w);
}

/* ---------------- install / uninstall ---------------- */
const busy = new Set();
function send(ch, payload) { if (mainWin && !mainWin.isDestroyed()) mainWin.webContents.send(ch, payload); }

async function fetchManifest() {
  const r = await net.fetch(SITE + 'games.json?t=' + Date.now(), { cache: 'no-store' });
  if (!r.ok) throw new Error('ما قدرت أقرأ قائمة الألعاب (' + r.status + ')');
  return r.json();
}

async function install(id) {
  if (!validId(id)) throw new Error('لعبة غير معروفة');
  if (busy.has(id)) return;
  busy.add(id);
  const tmpZip = path.join(app.getPath('temp'), `ps-${id}-${Date.now()}.zip`);
  const stage = gameDir(id) + '.new';
  try {
    const man = await fetchManifest(), m = man.games && man.games[id];
    if (!m) throw new Error('اللعبة مو موجودة في القائمة');
    const total = m.zipSize || 0;
    send('ps:progress', { id, phase: 'download', received: 0, total });
    const res = await net.fetch(new URL(m.zip, SITE).toString(), { cache: 'no-store' });
    if (!res.ok || !res.body) throw new Error('فشل التحميل (' + res.status + ')');
    const hash = crypto.createHash('sha256');
    const out = fs.createWriteStream(tmpZip);
    const reader = res.body.getReader();
    let received = 0, last = 0;
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      hash.update(value); received += value.length;
      if (!out.write(Buffer.from(value))) await new Promise(r => out.once('drain', r));
      const now = Date.now();
      if (now - last > 150) { last = now; send('ps:progress', { id, phase: 'download', received, total: total || received }); }
    }
    await new Promise((r, j) => out.end(err => err ? j(err) : r()));
    if (m.sha256 && hash.digest('hex') !== m.sha256) throw new Error('الملف وصل ناقص، جرّب مرة ثانية');
    send('ps:progress', { id, phase: 'extract', received, total: received });
    await fsp.rm(stage, { recursive: true, force: true });
    await fsp.mkdir(stage, { recursive: true });
    const extract = require('extract-zip');
    await extract(tmpZip, { dir: stage });
    await fsp.rm(gameDir(id), { recursive: true, force: true });
    await fsp.rename(stage, gameDir(id));
    const size = await dirSize(gameDir(id));
    const reg = readReg();
    reg[id] = { name: m.name, version: m.version, entry: m.entry, icon: m.icon || 'icon.ico', size, installedAt: Date.now() };
    await writeReg(reg);
    makeShortcuts(id, reg[id]);
    send('ps:progress', { id, phase: 'done' });
    send('ps:installed-changed', {});
  } catch (e) {
    await fsp.rm(stage, { recursive: true, force: true }).catch(() => {});
    send('ps:progress', { id, phase: 'error', error: e.message || String(e) });
    throw e;
  } finally {
    busy.delete(id);
    fsp.rm(tmpZip, { force: true }).catch(() => {});
  }
}

async function uninstall(id) {
  if (!validId(id)) throw new Error('لعبة غير معروفة');
  const w = gameWins.get(id); if (w && !w.isDestroyed()) w.close();
  const reg = readReg(), g = reg[id];
  await fsp.rm(gameDir(id), { recursive: true, force: true });
  if (g) removeShortcuts(g);
  delete reg[id]; await writeReg(reg);
  send('ps:installed-changed', {});
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
        icon: path.join(gameDir(id), g.icon || 'icon.ico'), iconIndex: 0, description: g.name + ' — Premium Studio'
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
    return net.fetch(pathToFileURL(file).toString());
  });
}

/* ---------------- IPC ---------------- */
function guard(fn) {
  return async (e, ...args) => {
    if (!isTrusted(e.senderFrame && e.senderFrame.url)) throw new Error('غير مسموح');
    return fn(...args);
  };
}
ipcMain.handle('ps:getInstalled', guard(async () => installedNow()));
ipcMain.handle('ps:install', guard(async id => { await install(id); return true; }));
ipcMain.handle('ps:uninstall', guard(async id => { await uninstall(id); return true; }));
ipcMain.handle('ps:launch', guard(async id => { launchGame(id); return true; }));
ipcMain.handle('ps:showFolder', guard(async id => { if (validId(id)) shell.openPath(gameDir(id)); return true; }));
ipcMain.handle('ps:setUser', guard(async u => { currentUser = u && typeof u === 'object' ? { name: String(u.name || '').slice(0, 32) } : null; return true; }));
ipcMain.handle('ps:retryOnline', guard(async () => { if (mainWin) mainWin.loadURL(SITE); return true; }));
ipcMain.on('game:exit', e => { const w = BrowserWindow.fromWebContents(e.sender); if (w) w.close(); });
ipcMain.on('game:nick', e => { e.returnValue = currentUser && currentUser.name ? currentUser.name : ''; });
ipcMain.on('app:version', e => { e.returnValue = VERSION_LABEL; });

/* ---------------- deep links + startup ---------------- */
function argValue(argv, name) { const a = argv.find(x => x.startsWith(`--${name}=`)); return a ? a.split('=')[1] : null; }
function handleDeepLink(url) {
  // premiumstudio://game/<id>  → open store page;  premiumstudio://launch/<id> → start the game
  try {
    const u = new URL(url), kind = u.hostname, id = u.pathname.replace(/^\/+/, '').split('/')[0];
    if (kind === 'launch' && validId(id) && installedNow()[id]) return launchGame(id);
    createMain(validId(id) ? id : (kind === 'library' ? 'library' : ''));
  } catch (e) { createMain(); }
}
function handleArgv(argv, firstRun) {
  const launch = argValue(argv, 'launch');
  const link = argv.find(a => a.startsWith('premiumstudio://'));
  if (launch && validId(launch) && installedNow()[launch]) { try { launchGame(launch); return; } catch (e) {} }
  if (link) return handleDeepLink(link);
  if (!firstRun || !launch) createMain();
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
    handleArgv(process.argv, true);
    if (!BrowserWindow.getAllWindows().length) createMain();
    if (app.isPackaged) {
      try {
        const { autoUpdater } = require('electron-updater');
        autoUpdater.autoDownload = true;
        autoUpdater.checkForUpdatesAndNotify().catch(() => {});
      } catch (e) {}
    }
  });
  app.on('window-all-closed', () => app.quit());
}
