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
    if (gw) { gw.focus(); gw.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'F9' }); }
    await sleep(4000);
    if (gw) gw.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'F9' });
    const clipDir = path.join(app.getPath('videos'), 'Premium Games');
    ok('F9 records a clip', await waitFor(() => { try { return fs.readdirSync(clipDir).some(d => fs.readdirSync(path.join(clipDir, d)).some(f => f.endsWith('.webm') && fs.statSync(path.join(clipDir, d, f)).size > 1000)); } catch (e) { return false; } }, 15000));
    await call('ps:stop', 'last-recording--std');
    ok('horror game closes', await waitFor(async () => !(await call('ps:getRunning'))['last-recording--std'], 20000));
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
    const engProg = []; const wc = BrowserWindow.getAllWindows()[0].webContents, origSend = wc.send.bind(wc);
    wc.send = (ch, p) => { if (ch === 'ps:progress' && p && p.id === 'premium-engine') engProg.push(p.phase); return origSend(ch, p); };
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
    ok('engine: removed (files + icon), project kept', es.installed.length === 0 && !fs.existsSync(engLnk) && fs.existsSync(path.join(projDir, 'E2E_Game.pge.json')));
    fs.rmSync(projDir, { recursive: true, force: true });
  } catch (e) { ok('no crash', false, e && e.stack || String(e)); }
  fs.writeFileSync(path.join(__dirname, 'e2e-result.txt'), results.join('\n') + '\n');
  console.log('\n' + results.join('\n') + `\n${failed ? failed + ' FAILED' : 'ALL PASSED'}`);
  app.exit(failed ? 1 : 0);
});
