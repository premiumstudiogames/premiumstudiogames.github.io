'use strict';
// End-to-end check on a real Windows machine (GitHub Actions): runs the real app code and plays both games.
const path = require('path'), fs = require('fs'), os = require('os');
const { app, ipcMain, BrowserWindow } = require('electron');
const tmp = path.join(os.tmpdir(), 'pse2e');
const FIRST = app.requestSingleInstanceLock();   // a premiumstudio:// link starts a second copy; it only passes the link on
if (FIRST) fs.rmSync(tmp, { recursive: true, force: true });
// the launcher finds %APPDATA% itself (not from the environment), so both use the machine's real one, like on a player's PC
process.env.LOCALAPPDATA = path.join(tmp, 'Local');
app.setPath('userData', path.join(tmp, 'ud'));
if (FIRST) fs.rmSync(path.join(app.getPath('appData'), '.terravolt'), { recursive: true, force: true });
// the app used to be "Premium Studio": leave an old icon and an old screenshots folder, the app must carry them over
const oldLnk = path.join(os.homedir(), 'Desktop', 'Premium Studio.lnk'), newLnk = path.join(os.homedir(), 'Desktop', 'Premium Games.lnk');
const oldShots = path.join(app.getPath('pictures'), 'Premium Studio', 'Old Game');
if (FIRST) { try { fs.rmSync(newLnk, { force: true }); require('electron').shell.writeShortcutLink(oldLnk, { target: process.execPath, description: 'Premium Studio' });
  fs.mkdirSync(oldShots, { recursive: true }); fs.writeFileSync(path.join(oldShots, 'old.png'), 'x'); } catch (e) { console.log('legacy setup:', e.message); } }
// an older version installed BLOCKFALL once per edition, with an icon per edition: the app must merge them into one
const oldBf = ['BLOCKFALL 3001 Gold', 'BLOCKFALL 3001 Demo'].map(n => path.join(os.homedir(), 'Desktop', n + '.lnk'));
if (FIRST) { try {
  const exe = path.join(process.env.LOCALAPPDATA, 'Programs', 'TERRAVOLT', 'BLOCKFALL3001.exe'); fs.mkdirSync(path.dirname(exe), { recursive: true }); fs.writeFileSync(exe, 'old');
  const gd = path.join(process.env.LOCALAPPDATA, 'PremiumStudio', 'games'); fs.mkdirSync(gd, { recursive: true });
  const old = (ed, n) => ({ type: 'bf-launcher', game: 'blockfall', base: 'BLOCKFALL 3001', name: n, edition: ed, version: '01.00.00', entry: 'BLOCKFALL3001.exe', icon: 'resources/app/icon.ico' });
  fs.writeFileSync(path.join(gd, 'installed.json'), JSON.stringify({ 'blockfall--gold': old('gold', 'BLOCKFALL 3001 Gold'), 'blockfall--demo': old('demo', 'BLOCKFALL 3001 Demo') }));
  for (const l of oldBf) require('electron').shell.writeShortcutLink(l, { target: process.execPath, args: '--launch=blockfall--gold' });
  fs.mkdirSync(path.join(app.getPath('appData'), '.terravolt'), { recursive: true });
  fs.writeFileSync(path.join(app.getPath('appData'), '.terravolt', 'license.json'), JSON.stringify({ key: 'PREMIUM-STUDIO', ed: 'gold', ps: true }));
} catch (e) { console.log('old BLOCKFALL setup:', e.message); } }
require('../main.js');
const ev = { senderFrame: { url: 'https://premiumstudiogames.github.io/' } };
const call = (ch, ...a) => ipcMain._invokeHandlers.get(ch)(ev, ...a);
const sleep = ms => new Promise(r => setTimeout(r, ms));
const results = []; let failed = 0;
const ok = (name, cond, extra) => { results.push(`${cond ? 'PASS' : 'FAIL'}  ${name}${extra ? '  (' + extra + ')' : ''}`); if (!cond) failed++; console.log(results[results.length - 1]); };
async function waitFor(fn, ms) { const t0 = Date.now(); while (Date.now() - t0 < ms) { try { if (await fn()) return true; } catch (e) {} await sleep(1000); } return false; }
const desk = path.join(os.homedir(), 'Desktop'), lnk = path.join(desk, 'BLOCKFALL 3001.lnk');
const bfData = path.join(app.getPath('appData'), '.terravolt');
const state = () => { try { return JSON.parse(fs.readFileSync(path.join(bfData, 'premium_state.json'), 'utf8')); } catch (e) { return null; } };
const procAlive = () => require('child_process').execSync('tasklist /FI "IMAGENAME eq BLOCKFALL3001.exe" /NH').toString().includes('BLOCKFALL3001.exe');

app.whenReady().then(async () => {
  if (!FIRST) return;
  // the store page here has nobody signed in, and would sign the app out; the test signs in through the app directly
  require('electron').session.defaultSession.webRequest.onBeforeRequest({ urls: ['https://premiumstudiogames.github.io/app.js*'] }, (d, cb) => cb({ cancel: true }));
  for (const w of BrowserWindow.getAllWindows()) w.webContents.reload();
  await sleep(2000);
  try {
    // ---- the new name: one icon, old files kept
    ok('old "Premium Studio" icon renamed to "Premium Games" (one icon)', !fs.existsSync(oldLnk) && fs.existsSync(newLnk));
    ok('old screenshots carried over', fs.existsSync(path.join(app.getPath('pictures'), 'Premium Games', 'Old Game', 'old.png')));
        // ---- console mode
    let st = await call('ps:setSettings', { consoleBoot: true, sounds: false, glyphs: 'ps' });
    st = await call('ps:getSettings');
    ok('console mode settings are saved', st.consoleBoot === true && st.sounds === false && st.glyphs === 'ps', JSON.stringify(st));
    await call('ps:setSettings', { consoleBoot: false });
    let bad = false; try { await call('ps:power', 'format-c'); } catch (e) { bad = true; }
    ok('power menu rejects unknown commands', bad);
    app.emit('second-instance', {}, [process.execPath, '--console']);
    ok('--console opens straight into console mode', await waitFor(() => BrowserWindow.getAllWindows().some(w => !w.isDestroyed() && /#bigpicture$/.test(w.webContents.getURL())), 15000));
    await call('ps:fullscreen', false);
    await call('ps:setUser', { username: 'e2etest', name: 'E2E', pid: '0000000001', email: 'e2e@test.local', uid: 'E2E', refreshToken: '' });
    // ---- التسجيل الأخير
    await call('ps:install', 'last-recording--std', { edition: 'std' });
    ok('horror game installs', !!(await call('ps:getInstalled'))['last-recording--std']);
    await call('ps:launch', 'last-recording--std');
    ok('horror game runs', await waitFor(async () => !!(await call('ps:getRunning'))['last-recording--std'], 30000));
    await sleep(4000);
    // F12 takes a screenshot; Shift+Tab opens the overlay
    const gw = BrowserWindow.getAllWindows().find(w => w.webContents.getURL().startsWith('psgame'));
    if (gw) { gw.focus(); gw.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'F12' }); }
    const shotDir = path.join(app.getPath('pictures'), 'Premium Games');
    ok('F12 saves a screenshot', await waitFor(() => { try { return fs.readdirSync(shotDir).some(d => fs.readdirSync(path.join(shotDir, d)).some(f => f.endsWith('.png'))); } catch (e) { return false; } }, 10000));
    if (gw) gw.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'Tab', modifiers: ['shift'] });
    ok('Shift+Tab opens the overlay', await waitFor(() => BrowserWindow.getAllWindows().some(w => !w.isDestroyed() && w.webContents.getURL().startsWith('data:') && w.isVisible() && /%D8%A7%D9%84%D8%A5%D9%86%D8%AC%D8%A7%D8%B2%D8%A7%D8%AA|التروفيات/.test(decodeURIComponent(w.webContents.getURL()).slice(0, 20000))), 10000));
    if (gw) gw.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'Tab', modifiers: ['shift'] });
    await sleep(800);
    // the controller's Home button opens the overlay too
    const ovVisible = () => BrowserWindow.getAllWindows().some(w => !w.isDestroyed() && w.webContents.getURL().startsWith('data:') && w.isVisible() && /التروفيات/.test(decodeURIComponent(w.webContents.getURL()).slice(0, 20000)));
    if (!(await waitFor(() => !ovVisible(), 5000)) && gw) { ipcMain.emit('game:guide', { sender: gw.webContents }); await waitFor(() => !ovVisible(), 5000); }   // make sure it's closed first
    await sleep(500);
    if (gw) ipcMain.emit('game:guide', { sender: gw.webContents });
    ok('controller Home button opens the overlay', await waitFor(ovVisible, 10000));
    if (gw) ipcMain.emit('game:guide', { sender: gw.webContents });
    await waitFor(() => !ovVisible(), 8000); await sleep(1500);   // overlay fully closed before recording
    // F9 records a clip, F9 again saves it
    const recLog = []; for (const ch of ['rec:started', 'rec:error', 'rec:done']) ipcMain.on(ch, (e, a) => recLog.push(ch + (ch === 'rec:done' ? ' ' + (a && a.length) : a ? ' ' + a : '') + ' @' + Date.now() % 100000));
    if (gw) { gw.focus(); gw.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'F9' }); }
    await waitFor(() => recLog.some(l => l.startsWith('rec:started')), 15000); await sleep(4000);   // record 4 s once the recorder runs
    if (gw) gw.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'F9' });
    const clipDir = path.join(app.getPath('videos'), 'Premium Games');
    ok('F9 records a clip', await waitFor(() => { try { return fs.readdirSync(clipDir).some(d => fs.readdirSync(path.join(clipDir, d)).some(f => f.endsWith('.webm') && fs.statSync(path.join(clipDir, d, f)).size > 1000)); } catch (e) { return false; } }, 15000), recLog.join(' | ') + ' · gw=' + !!gw);
    await call('ps:stop', 'last-recording--std');
    ok('horror game closes', await waitFor(async () => !(await call('ps:getRunning'))['last-recording--std'], 20000));
    // ---- console mode with a game running (like a PS5): full screen, no launcher window, Home = Control Center over the game
    await call('ps:setConsole', true);
    const homeSent = []; { const mw = BrowserWindow.getAllWindows().find(w => /#bigpicture$/.test(w.webContents.getURL())); if (mw) { const orig = mw.webContents.send.bind(mw.webContents); mw.webContents.send = (ch, p) => { if (ch === 'bp:home') homeSent.push(p); return orig(ch, p); }; } }
    await call('ps:launch', 'last-recording--std');
    ok('console: game runs', await waitFor(async () => !!(await call('ps:getRunning'))['last-recording--std'], 30000));
    const cgw = await (async () => { let w = null; await waitFor(() => (w = BrowserWindow.getAllWindows().find(x => !x.isDestroyed() && x.webContents.getURL().startsWith('psgame') && x.isVisible())), 15000); return w; })();
    ok('console: the game is full screen without a window frame', !!cgw && cgw.consoleMode === true && cgw.isFullScreen());
    ok('console: no "starting game" window', !BrowserWindow.getAllWindows().some(w => !w.isDestroyed() && w.webContents.getURL().startsWith('data:') && /%D8%AC%D8%A7%D8%B1%D9%8A|جاري تشغيل/.test(w.webContents.getURL().slice(0, 4000))));
    const T = global.__psTest, padUp = () => !!(T && T.padProc());
    ok('console: the controller reader (PremiumPad) runs', await waitFor(padUp, 5000));
    ok('console: no Chromium fallback window when the reader runs', !BrowserWindow.getAllWindows().some(w => !w.isDestroyed() && w.getBounds().width <= 2 && w.isAlwaysOnTop()));
    const ccW = () => BrowserWindow.getAllWindows().find(w => !w.isDestroyed() && /cc\.html$/.test(w.webContents.getURL()) && w.isVisible());
    await sleep(3000); if (cgw) { cgw.focus(); await sleep(500); }
    T.padPress();   // a real Home press, through PremiumPad itself
    ok('console: Home opens the Control Center over the game', await waitFor(ccW, 10000));
    await sleep(1500);
    const ccd = await call('cc:data');
    ok('console: Control Center knows the game', ccd && ccd.key === 'last-recording--std' && ccd.running.length === 1, ccd && ccd.name);
    try { fs.mkdirSync(path.join(__dirname, 'shots'), { recursive: true }); const w = ccW(); if (w) fs.writeFileSync(path.join(__dirname, 'shots', 'control-center.png'), (await w.webContents.capturePage()).toPNG()); } catch (e) {}
    const focusedInCC = ccW() ? await ccW().webContents.executeJavaScript('document.activeElement && document.activeElement.dataset.a') : '';
    ok('console: Control Center is drawn and focused on the game card', focusedInCC === 'close', focusedInCC);
    ok('console: the game does not see the controller while the Control Center is open', cgw ? await cgw.webContents.executeJavaScript('[...navigator.getGamepads()].every(p => !p)') : false);
    ok('console: mute the game from the Control Center', (await call('cc:act', 'mute')) === true && cgw.webContents.isAudioMuted());
    await call('cc:act', 'mute');
    await call('cc:act', 'home');
    ok('console: Home → the console screen over the game (game keeps running)', await waitFor(async () => !ccW() && homeSent.length > 0 && !!(await call('ps:getRunning'))['last-recording--std'], 8000), JSON.stringify(homeSent));
    await call('ps:launch', 'last-recording--std');   // from the console's tile: back to the game, not a second copy
    ok('console: resume brings the same game back', await waitFor(() => cgw && cgw.isFocused(), 8000) && BrowserWindow.getAllWindows().filter(w => !w.isDestroyed() && w.webContents.getURL().startsWith('psgame')).length === 1);
    app.emit('second-instance', {}, [process.execPath, 'premiumstudio://overlay']);   // what a game in its own program sends for Home
    ok('console: premiumstudio://overlay opens the Control Center', await waitFor(ccW, 10000));
    await sleep(600); T.padPress();
    ok('console: Home again closes it, back to the game', await waitFor(() => !ccW(), 5000));
    await sleep(600); T.padLine('home 1'); await sleep(900); T.padLine('home 0');   // held Home: the console's home screen, game still running
    ok('console: held Home → the console screen', await waitFor(() => homeSent.length > 1, 5000) && !ccW() && !!(await call('ps:getRunning'))['last-recording--std']);
    await call('ps:launch', 'last-recording--std'); await sleep(1000);
    app.emit('second-instance', {}, [process.execPath, 'premiumstudio://overlay']); await waitFor(ccW, 10000);
    await call('cc:act', 'stop');
    ok('console: Close Game from the Control Center', await waitFor(async () => !(await call('ps:getRunning'))['last-recording--std'], 20000));
    await call('ps:setConsole', false);
    ok('console: the reader stops when console mode ends', await waitFor(() => !padUp(), 5000));
    // the console's own files: everything is written down
    const csDir = path.join(process.env.LOCALAPPDATA, 'PremiumStudio', 'console');
    let act = []; try { act = fs.readFileSync(path.join(csDir, 'activity.log'), 'utf8').trim().split('\n').map(l => JSON.parse(l)); } catch (e) {}
    const types = new Set(act.map(a => a.type));
    ok('console files: activity log', ['console-open', 'game-start', 'home', 'cc', 'game-stop', 'console-close'].every(t => types.has(t)), [...types].join(','));
    ok('console files: game start says console', act.some(a => a.type === 'game-start' && a.from === 'console'));
    await call('cs:set', 'system', { wall: 'night', sort: 'recent' }); await call('cs:set', 'lists', [{ name: 'E2E', games: ['blockfall'] }]); await call('cs:set', 'hq', { blockfall: { ed: 'gold' } });
    ok('console files: settings, lists, menu picks saved', (await call('cs:get', 'system')).wall === 'night' && (await call('cs:get', 'lists'))[0].name === 'E2E' && (await call('cs:get', 'hq')).blockfall.ed === 'gold');
    let roRefused = false; try { await call('cs:set', 'activity', []); } catch (e) { roRefused = true; }
    ok('console files: the log is read-only for the page', roRefused);
    let games = {}; try { for (const u of fs.readdirSync(path.join(csDir, 'users'))) try { Object.assign(games, JSON.parse(fs.readFileSync(path.join(csDir, 'users', u, 'games.json'), 'utf8'))); } catch (e) {} } catch (e) {}
    ok('console files: play time per game', !!games['last-recording'] || !!games['last-recording--std'], Object.keys(games).join(','));
    // ---- parental controls: a game that isn't allowed doesn't start
    await call('ps:setParental', { newPin: '4321', pin: '', on: true, allowed: ['blockfall'], daily: 0 });
    let blocked = false; try { await call('ps:launch', 'last-recording--std'); } catch (e) { blocked = /الرقابة/.test(String(e.message)); }
    ok('parental controls block a game', blocked && !(await call('ps:getRunning'))['last-recording--std']);
    let wrong = false; try { await call('ps:setParental', { pin: '0000', on: false }); } catch (e) { wrong = true; }
    ok('parental controls need the PIN', wrong);
    await call('ps:setParental', { pin: '4321', on: false, allowed: [] });
    // ---- BLOCKFALL: one game for Demo / Standard / Gold
    let reg0 = await call('ps:getInstalled');
    ok('old per-edition installs merged into one BLOCKFALL', !!reg0.blockfall && !Object.keys(reg0).some(k => k.startsWith('blockfall--')), Object.keys(reg0).join(','));
    ok('old per-edition icons removed', oldBf.every(l => !fs.existsSync(l)));
    ok('one "BLOCKFALL 3001" icon, opening through Premium Games', fs.existsSync(lnk) && require('electron').shell.readShortcutLink(lnk).target === process.execPath);
    ok('our old license.json is gone', !fs.existsSync(path.join(bfData, 'license.json')));
    const t0 = Date.now();
    await call('ps:install', 'blockfall--gold', { edition: 'gold' });   // older store pages still send an edition: same one install
    const inst = (await call('ps:getInstalled')).blockfall;
    ok('BLOCKFALL installs (one install)', !!inst && inst.name === 'BLOCKFALL 3001' && !inst.edition && !Object.keys(await call('ps:getInstalled')).some(k => k.startsWith('blockfall--')), Math.round((Date.now() - t0) / 1000) + 's');
    const L = await (await fetch('https://abdullra7manx-cmyk.github.io/terravolt/launcher/launcher.json')).json();
    let appv = ''; try { appv = JSON.parse(fs.readFileSync(path.join(process.env.LOCALAPPDATA, 'Programs', 'TERRAVOLT', 'resources', 'app', 'package.json'), 'utf8')).version; } catch (e) {}
    ok('launcher program is current before first run', appv === L.app.version, appv + ' vs ' + L.app.version);
    await call('ps:setOwned', { blockfall: ['demo', 'gold'], 'blockfall-dev': ['dev'] });
    await call('ps:launch', 'blockfall');
    ok('BLOCKFALL opens (launcher reports open)', await waitFor(() => { const s = state(); return s && s.open && Date.now() - s.at < 15000; }, 90000), JSON.stringify(state()));
    let acc = {}; try { acc = JSON.parse(fs.readFileSync(path.join(bfData, 'premium_account.json'), 'utf8')); } catch (e) {}
    ok('launcher gets the owned editions (no dev)', JSON.stringify((acc.owned || {}).blockfall) === '["demo","gold"]', JSON.stringify(acc.owned));
    ok('no license.json written', !fs.existsSync(path.join(bfData, 'license.json')));
    ok('app shows BLOCKFALL as running', !!(await call('ps:getRunning')).blockfall);
    await sleep(5000);
    const bfIcons = fs.readdirSync(desk).filter(f => /^BLOCKFALL/i.test(f));
    ok('only one BLOCKFALL desktop icon', bfIcons.length === 1 && bfIcons[0] === 'BLOCKFALL 3001.lnk', bfIcons.join(','));
    await call('ps:stop', 'blockfall');
    ok('BLOCKFALL closes from the Close button', await waitFor(async () => !(await call('ps:getRunning')).blockfall, 40000));
    ok('BLOCKFALL process gone', await waitFor(() => !procAlive(), 15000));
    // ---- BLOCKFALL on the console: no launcher; the console's game menu picks the edition and the version
    const vers = await call('ps:gameVersions', 'blockfall');
    ok('console: BLOCKFALL versions for its menu', !!vers && vers.versions.length > 5 && !!vers.latest, vers && vers.latest + ' · ' + vers.versions.length);
    const tvApp = path.join(process.env.LOCALAPPDATA, 'Programs', 'TERRAVOLT', 'resources', 'app');
    for (const f of ['main.js', 'premium.js']) fs.copyFileSync(path.join(__dirname, 'fixtures', 'launcher-app', f), path.join(tvApp, f));   // the launcher's console mode (1.10.0)
    { const pj = path.join(tvApp, 'package.json'), j = JSON.parse(fs.readFileSync(pj, 'utf8')); j.version = '1.10.0'; fs.writeFileSync(pj, JSON.stringify(j, null, 2)); }
    await call('ps:setConsole', true);
    const pick = vers && (vers.versions.find(v => v.installed) || vers.versions[0]).id;
    const tl = Date.now();
    await call('ps:launch', 'blockfall', 'gold', { version: pick });
    ok('console: BLOCKFALL goes straight into the game (no launcher)', await waitFor(() => { const s = state(); return s && s.game && s.at > tl; }, 240000), JSON.stringify(state()) + ' · ' + Math.round((Date.now() - tl) / 1000) + 's');
    let pl = {}; try { pl = JSON.parse(fs.readFileSync(path.join(bfData, 'premium_launch.json'), 'utf8')); } catch (e) {}
    ok('console: the launcher got the edition and version', pl.console === true && pl.edition === 'gold' && pl.version === pick, JSON.stringify(pl));
    let lic = {}; try { lic = JSON.parse(fs.readFileSync(path.join(bfData, 'license.json'), 'utf8')); } catch (e) {}
    ok('console: playing the picked edition', !lic.ed || lic.ed === 'gold', lic.ed);
    ok('console: the picked version is installed', fs.existsSync(path.join(bfData, 'versions', pick)));
    await sleep(3000); T.padPress();
    ok('console: Home over BLOCKFALL opens the Control Center', await waitFor(ccW, 10000));
    const bcd = await call('cc:data');
    ok('console: Control Center knows BLOCKFALL', bcd && bcd.key === 'blockfall', bcd && bcd.name);
    await sleep(600); T.padPress();
    ok('console: Home again back to BLOCKFALL', await waitFor(() => !ccW(), 5000));
    await call('ps:launch', 'blockfall');   // the tile again: the same game comes forward, no second launcher
    await sleep(3000);
    ok('console: one BLOCKFALL only', require('child_process').execSync('tasklist /FI "IMAGENAME eq BLOCKFALL3001.exe" /NH').toString().split('BLOCKFALL3001.exe').length - 1 >= 1 && state().game);
    await call('ps:stop', 'blockfall');
    ok('console: BLOCKFALL closes from the console', await waitFor(async () => !(await call('ps:getRunning')).blockfall, 40000));
    ok('console: the launcher quits with the game', await waitFor(() => !procAlive(), 20000));
    await call('ps:setConsole', false);
    // ---- the Developer edition: its own program
    const devData = path.join(app.getPath('appData'), '.terravolt-dev'), devExe = path.join(process.env.LOCALAPPDATA, 'Programs', 'BLOCKFALL3001-Dev', 'BLOCKFALL3001-Dev.exe');
    const devLnk = path.join(desk, 'BLOCKFALL 3001 المطور.lnk');
    const devState = () => { try { return JSON.parse(fs.readFileSync(path.join(devData, 'premium_state.json'), 'utf8')); } catch (e) { return null; } };
    await call('ps:install', 'blockfall-dev');
    ok('Developer edition installs in its own folder', fs.existsSync(devExe) && !!(await call('ps:getInstalled'))['blockfall-dev']);
    ok('Developer icon "BLOCKFALL 3001 المطور"', fs.existsSync(devLnk));
    await call('ps:launch', 'blockfall-dev');
    let dacc = {}; try { dacc = JSON.parse(fs.readFileSync(path.join(devData, 'premium_account.json'), 'utf8')); } catch (e) {}
    ok('Developer launcher gets its account with dev', JSON.stringify((dacc.owned || {}).blockfall) === '["dev"]' && dacc.uid === 'E2E', JSON.stringify(dacc.owned));
    ok('BLOCKFALL Developer opens', await waitFor(() => { const s = devState(); return s && s.open && Date.now() - s.at < 15000; }, 90000), JSON.stringify(devState()));
    ok('normal BLOCKFALL not started by it', !procAlive());
    await call('ps:stop', 'blockfall-dev');
    ok('BLOCKFALL Developer closes', await waitFor(async () => !(await call('ps:getRunning'))['blockfall-dev'], 40000));
    await sleep(3000);
    // ---- started outside Premium Studio: must hand over and come back through the app (like Steam)
    app.on('second-instance', (e, argv) => console.log('second-instance argv:', JSON.stringify(argv)));
    const { app: _a, shell } = require('electron');
    console.log('protocol handler:', app.getApplicationNameForProtocol('premiumstudio://play/blockfall'));
    const dcp = require('child_process').spawn(path.join(process.env.LOCALAPPDATA, 'Programs', 'TERRAVOLT', 'BLOCKFALL3001.exe'), ['--enable-logging'], { env: { ...process.env, ELECTRON_ENABLE_LOGGING: '1' } });
    let dout = ''; dcp.stdout.on('data', d => dout += d); dcp.stderr.on('data', d => dout += d); dcp.on('exit', c => console.log('direct launcher exited:', c, dout.slice(-3000)));
    setTimeout(() => console.log('after 20s: handoff file?', fs.existsSync(path.join(bfData, 'premium_handoff.json')), 'alive?', procAlive()), 20000);
    const bfRun = async () => Object.keys(await call('ps:getRunning')).find(k => k.startsWith('blockfall'));
    ok('BLOCKFALL started directly goes through Premium Studio', await waitFor(bfRun, 90000), await bfRun());
    const k = await bfRun(); if (k) { await call('ps:stop', k); await waitFor(async () => !(await bfRun()), 40000); }
    // ---- Premium Engine from Premium Games (like Epic Games Launcher with Unreal)
    const engAlive = () => require('child_process').execSync('tasklist /FI "IMAGENAME eq PremiumEngine.exe" /NH').toString().includes('PremiumEngine.exe');
    const killEng = () => { try { require('child_process').execSync('taskkill /IM PremiumEngine.exe /F', { stdio: 'ignore' }); } catch (e) {} };
    const projDir = path.join(app.getPath('documents'), 'PremiumEngine', 'Projects', 'E2E_Game'), engLnk = path.join(desk, 'Premium Engine.lnk');
    fs.rmSync(projDir, { recursive: true, force: true });
    let es = await call('eng:status');
    ok('engine: versions listed', !!es.latest && es.available.length > 0 && es.installed.length === 0, es.latest);
    const engProg = [];   // what the store page gets (every window, the store is one of them)
    for (const w of BrowserWindow.getAllWindows()) { const wc = w.webContents, orig = wc.send.bind(wc); wc.send = (ch, p) => { if (ch === 'ps:progress' && p && p.id === 'premium-engine') engProg.push(p.phase); return orig(ch, p); }; }
    await call('eng:install', es.latest);
    es = await call('eng:status');
    ok('engine: installs (download, checksum, unzip)', es.installed.length === 1 && es.installed[0].version === es.latest && fs.existsSync(path.join(es.installed[0].dir, 'PremiumEngine.exe')) && fs.existsSync(path.join(es.installed[0].dir, 'PremiumEnginePlayer.exe')), es.installed[0] && es.installed[0].dir);
    ok('engine: progress shown (download → done)', engProg.includes('download') && engProg[engProg.length - 1] === 'done', engProg.slice(-3).join(','));
    ok('engine: "Premium Engine" desktop icon', fs.existsSync(engLnk));
    let badName = false; try { await call('eng:new', es.latest, 'FirstPerson', 'a/b'); } catch (e) { badName = true; }
    ok('engine: bad project names refused', badName);
    await call('eng:new', es.latest, 'FirstPerson', 'E2E Game');
    ok('engine: running while open', (await call('eng:status')).running.length === 1);
    ok('engine: new project made from the app (file + thumbnail)', await waitFor(() => fs.existsSync(path.join(projDir, 'E2E_Game.pge.json')) && fs.existsSync(path.join(projDir, 'Saved', 'thumbnail.bmp')), 90000));
    es = await call('eng:status'); const pr = es.projects.find(p => p.name === 'E2E Game');
    ok('engine: project listed with thumbnail', !!pr && pr.thumb.startsWith('data:image/bmp;base64,') && pr.template === 'FirstPerson', pr && pr.file);
    killEng(); ok('engine: closed → not running', await waitFor(async () => (await call('eng:status')).running.length === 0, 20000));
    let outside = false; try { await call('eng:launch', es.latest, 'C:\\Windows\\win.ini'); } catch (e) { outside = true; }
    ok('engine: only opens projects from the projects folder', outside);
    await call('eng:launch', es.latest, pr.file);
    ok('engine: opens a project', await waitFor(() => engAlive(), 30000) && (await call('eng:status')).running[0].project === pr.file);
    let busyUn = false; try { await call('eng:uninstall', es.latest); } catch (e) { busyUn = true; }
    ok('engine: can\'t remove while it is open', busyUn);
    killEng(); await waitFor(async () => (await call('eng:status')).running.length === 0, 20000);
    await call('eng:uninstall', es.latest);
    es = await call('eng:status');
    ok('engine: removed (files + icon), project kept', es.installed.length === 0 && !fs.existsSync(engLnk) && fs.existsSync(path.join(projDir, 'E2E_Game.pge.json')), `installed=${es.installed.length} icon=${fs.existsSync(engLnk)} project=${fs.existsSync(path.join(projDir, 'E2E_Game.pge.json'))}`);
    fs.rmSync(projDir, { recursive: true, force: true });
    // ---- YouTube in console mode: the TV version, full screen, controller-driven, closes back to the console
    const tvShots = path.join(__dirname, 'shots'); fs.mkdirSync(tvShots, { recursive: true });
    await call('tv:open', 'youtube');
    const tvOk = await waitFor(async () => { const t = await call('tv:state'); return t && /youtube\.com\/tv/.test(t.url) && t.visible && /youtube/i.test(t.title); }, 45000);
    let tv = await call('tv:state');
    ok('youtube: TV version opens full screen', tvOk && /Leanback/.test(tv.ua), tv && (tv.url + ' · ' + tv.title));
    const tvw = BrowserWindow.getAllWindows().find(w => w.tvApp === 'youtube');
    await new Promise(r => setTimeout(r, 8000));
    tv = await call('tv:state');
    ok('youtube: stays on the TV site (not sent to the desktop site)', tv && /youtube\.com\/tv/.test(tv.url), tv && tv.url);
    let leanback = false; try { leanback = await tvw.webContents.executeJavaScript('!!document.querySelector("ytlr-app, ytlr-guide-response-renderer, #container ytlr-surface-page, ytlr-browse-page, [class*=ytLr], [class*=ytlr]")'); } catch (e) {}
    try { fs.writeFileSync(path.join(tvShots, 'youtube.png'), (await tvw.webContents.capturePage()).toPNG()); } catch (e) {}
    ok('youtube: TV interface loaded', leanback);
    ipcMain.emit('tv:pad', { sender: tvw.webContents }, 'down'); ipcMain.emit('tv:pad', { sender: tvw.webContents }, 'right');
    await new Promise(r => setTimeout(r, 1500));
    try { fs.writeFileSync(path.join(tvShots, 'youtube-moved.png'), (await tvw.webContents.capturePage()).toPNG()); } catch (e) {}
    const fake = await (async () => { ipcMain.emit('tv:pad', { sender: { id: -1 } }, 'guide'); await new Promise(r => setTimeout(r, 500)); return !!(await call('tv:state')); })();
    ok('youtube: other pages can\'t drive it', fake);
    ipcMain.emit('tv:pad', { sender: tvw.webContents }, 'guide');
    ok('youtube: guide button closes it, back to the console', await waitFor(async () => !(await call('tv:state')), 10000));
  } catch (e) { ok('no crash', false, e && e.stack || String(e)); }
  fs.writeFileSync(path.join(__dirname, 'e2e-result.txt'), results.join('\n') + '\n');
  console.log('\n' + results.join('\n') + `\n${failed ? failed + ' FAILED' : 'ALL PASSED'}`);
  app.exit(failed ? 1 : 0);
});
