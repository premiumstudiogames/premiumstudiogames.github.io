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
  await sleep(2000);
  try {
    await call('ps:setUser', { username: 'e2etest', name: 'E2E', pid: '0000000001', email: 'e2e@test.local', uid: 'E2E', refreshToken: '' });
    // ---- التسجيل الأخير
    await call('ps:install', 'last-recording--std', { edition: 'std' });
    ok('horror game installs', !!(await call('ps:getInstalled'))['last-recording--std']);
    await call('ps:launch', 'last-recording--std');
    ok('horror game runs', await waitFor(async () => !!(await call('ps:getRunning'))['last-recording--std'], 30000));
    await sleep(4000);
    await call('ps:stop', 'last-recording--std');
    ok('horror game closes', await waitFor(async () => !(await call('ps:getRunning'))['last-recording--std'], 20000));
    // ---- BLOCKFALL
    const t0 = Date.now();
    await call('ps:install', 'blockfall--gold', { edition: 'gold' });
    const inst = (await call('ps:getInstalled'))['blockfall--gold'];
    ok('BLOCKFALL installs', !!inst, Math.round((Date.now() - t0) / 1000) + 's');
    const L = await (await fetch('https://abdullra7manx-cmyk.github.io/terravolt/launcher/launcher.json')).json();
    let appv = ''; try { appv = JSON.parse(fs.readFileSync(path.join(process.env.LOCALAPPDATA, 'Programs', 'TERRAVOLT', 'resources', 'app', 'package.json'), 'utf8')).version; } catch (e) {}
    ok('launcher program is current before first run', appv === L.app.version, appv + ' vs ' + L.app.version);
    const hadLnk = fs.existsSync(lnk);
    await call('ps:launch', 'blockfall--gold');
    ok('BLOCKFALL opens (launcher reports open)', await waitFor(() => { const s = state(); return s && s.open && Date.now() - s.at < 15000; }, 90000), JSON.stringify(state()));
    if (!(state() && state().open)) {   // diagnose: run the launcher directly and show what it says
      console.log('alive?', procAlive(), 'files in .terravolt:', (() => { try { return fs.readdirSync(bfData).join(','); } catch (e) { return e.message; } })());
      try { require('child_process').execSync('taskkill /IM BLOCKFALL3001.exe /F', { stdio: 'ignore' }); } catch (e) {}
      const cp = require('child_process').spawn(path.join(process.env.LOCALAPPDATA, 'Programs', 'TERRAVOLT', 'BLOCKFALL3001.exe'), ['--premium', '--enable-logging'], { env: { ...process.env, ELECTRON_ENABLE_LOGGING: '1' } });
      let out = ''; cp.stdout.on('data', d => out += d); cp.stderr.on('data', d => out += d);
      let code = 'running'; cp.on('exit', c => code = c);
      await sleep(30000);
      console.log('---- launcher output (exit: ' + code + ') ----\n' + out.slice(-6000) + '\n---- state: ' + JSON.stringify(state()));
      try { require('child_process').execSync('taskkill /IM BLOCKFALL3001.exe /F', { stdio: 'ignore' }); } catch (e) {}
    }
    let lic = null; try { lic = JSON.parse(fs.readFileSync(path.join(bfData, 'license.json'), 'utf8')); } catch (e) {}
    ok('Gold edition handed to the launcher', lic && lic.ed === 'gold', lic && lic.ed);
    ok('app shows BLOCKFALL as running', !!(await call('ps:getRunning'))['blockfall--gold']);
    await sleep(5000);
    ok('no extra BLOCKFALL desktop icon', hadLnk || !fs.existsSync(lnk));
    await call('ps:stop', 'blockfall--gold');
    ok('BLOCKFALL closes from the Close button', await waitFor(async () => !(await call('ps:getRunning'))['blockfall--gold'], 40000));
    ok('BLOCKFALL process gone', await waitFor(() => !procAlive(), 15000));
    // ---- the Developer edition and the editions the launcher can switch between
    await call('ps:setOwned', { blockfall: ['demo', 'gold', 'dev'] });
    let acc = {}; try { acc = JSON.parse(fs.readFileSync(path.join(bfData, 'premium_account.json'), 'utf8')); } catch (e) {}
    ok('launcher knows the owned editions', JSON.stringify((acc.owned || {}).blockfall) === '["demo","gold","dev"]', JSON.stringify(acc.owned));
    await call('ps:install', 'blockfall--dev', { edition: 'dev' });
    await call('ps:launch', 'blockfall--dev');
    let lic2 = null; try { lic2 = JSON.parse(fs.readFileSync(path.join(bfData, 'license.json'), 'utf8')); } catch (e) {}
    ok('Developer edition handed to the launcher', lic2 && lic2.ed === 'dev', lic2 && lic2.ed);
    ok('BLOCKFALL Developer opens', await waitFor(() => { const s = state(); return s && s.open && Date.now() - s.at < 15000; }, 90000));
    await call('ps:stop', 'blockfall--dev');
    ok('BLOCKFALL Developer closes', await waitFor(async () => !(await call('ps:getRunning'))['blockfall--dev'], 40000));
    await waitFor(() => !procAlive(), 15000);
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
  } catch (e) { ok('no crash', false, e && e.stack || String(e)); }
  fs.writeFileSync(path.join(__dirname, 'e2e-result.txt'), results.join('\n') + '\n');
  console.log('\n' + results.join('\n') + `\n${failed ? failed + ' FAILED' : 'ALL PASSED'}`);
  app.exit(failed ? 1 : 0);
});
