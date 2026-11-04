'use strict';
// Premium Studio desktop client: hosts the store, installs / launches / removes games.
const { app, BrowserWindow, ipcMain, shell, protocol, net, Menu } = require('electron');
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
function setUser(u) {
  const clip = (v, n) => String(v || '').slice(0, n);
  currentUser = u && typeof u === 'object' ? { username: clip(u.username, 20), name: clip(u.name, 32), pid: clip(u.pid, 10), email: clip(u.email, 120) } : null;
  try { if (currentUser) fs.writeFileSync(USER_FILE, JSON.stringify(currentUser)); else fs.rmSync(USER_FILE, { force: true }); } catch (e) {}
  writeBfAccount();
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
  mainWin.loadURL(SITE + q + (hash ? '#' + hash : ''));
  mainWin.on('closed', () => { mainWin = null; });
  return mainWin;
}
function openExternal(url) { if (/^https?:\/\//.test(url)) shell.openExternal(url); }

// Before starting a game that updates from its own manifest, pull the newest version (like the game's standalone app does).
async function launchWithUpdate(id, edition) {
  const g = installedNow()[id];
  if (g && g.type === 'manifest' && !busy.has(id)) {
    try {
      const m = ((await fetchManifest()).games || {})[g.game];
      if (m && JSON.stringify(m.account || null) !== JSON.stringify(readReg()[id].account || null)) { const reg = readReg(); reg[id].account = m.account || undefined; await writeReg(reg); }
      if (m) { const l = await latestOf(m); if (l.version && l.version !== g.version) await install(id); }
    } catch (e) { /* offline: play the copy we have */ }
  }
  launchGame(id, edition);
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
    const { spawn } = require('child_process');
    const child = spawn(BF_EXE, [], { cwd: BF_DIR, detached: true, stdio: 'ignore' });
    child.unref();
    // the launcher re-adds its own "BLOCKFALL 3001" shortcuts each start; put ours (one per edition) back
    setTimeout(fixBfShortcuts, 6000);
    if (!BrowserWindow.getAllWindows().length) setTimeout(() => app.quit(), 8000);   // started from a desktop shortcut: nothing else to keep open
    return;
  }
  if (gameWins.has(id) && !gameWins.get(id).isDestroyed()) { const w = gameWins.get(id); w.show(); w.focus(); return; }
  const w = new BrowserWindow({
    width: 1600, height: 900, backgroundColor: '#000000', title: g.name, icon: path.join(gameDir(id, g), g.icon || 'icon.ico'),
    autoHideMenuBar: true, show: false,
    webPreferences: { preload: path.join(__dirname, 'game-preload.js'), contextIsolation: false, sandbox: false, nodeIntegration: false, backgroundThrottling: false }
  });
  w.setMenu(null);
  w.once('ready-to-show', () => { w.maximize(); w.show(); });
  w.webContents.setWindowOpenHandler(({ url }) => { openExternal(url); return { action: 'deny' }; });
  w.webContents.on('before-input-event', (e, input) => {
    if (input.type === 'keyDown' && input.key === 'F11') { w.setFullScreen(!w.isFullScreen()); e.preventDefault(); }
  });
  const q = g.edition === 'demo' ? '?demo=1' : '';
  w.loadURL(`psgame://${id}/${g.entry}${q}`);
  w.on('closed', () => { gameWins.delete(id); if (!BrowserWindow.getAllWindows().length) app.quit(); });
  gameWins.set(id, w);
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
    send('ps:installed-changed', {});
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
ipcMain.handle('ps:features', guard(async () => ({ editions: true })));
ipcMain.handle('ps:showFolder', guard(async id => { if (validId(id)) shell.openPath(gameDir(id, readReg()[id])); return true; }));
ipcMain.handle('ps:setUser', guard(async u => { setUser(u); return true; }));
ipcMain.handle('ps:retryOnline', guard(async () => { if (mainWin) mainWin.loadURL(SITE); return true; }));
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
    if (kind === 'launch' && validId(id) && installedNow()[id]) return launchWithUpdate(id).catch(() => createMain());
    if (kind === 'install' && validId(id)) return createMain(id.split('--')[0], 'install=' + encodeURIComponent(id));
    createMain(validId(id) ? id : (kind === 'library' ? 'library' : ''));
  } catch (e) { createMain(); }
}
function handleArgv(argv, firstRun) {
  const launch = argValue(argv, 'launch');
  const link = argv.find(a => a.startsWith('premiumstudio://'));
  if (launch && validId(launch) && installedNow()[launch]) { launchWithUpdate(launch).catch(() => createMain()); return; }
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
    migrateReg();
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
