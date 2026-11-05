'use strict';
// End-to-end check on a real Windows machine (GitHub Actions): runs the real app code and plays both games.
const path = require('path'), fs = require('fs'), os = require('os');
const { app, ipcMain, BrowserWindow } = require('electron');
const tmp = path.join(os.tmpdir(), 'pse2e');
const FIRST = app.requestSingleInstanceLock();   // a premiumstudio:// link starts a second copy; it only passes the link on
if (FIRST) fs.rmSync(tmp, { recursive: true, force: true });
process.env.LOCALAPPDATA = path.join(tmp, 'Local'); process.env.APPDATA = path.join(tmp, 'Roaming');
fs.mkdirSync(process.env.APPDATA, { recursive: true });
app.setPath('appData', process.env.APPDATA); app.setPath('userData', path.join(tmp, 'ud'));
require('../main.js');
const ev = { senderFrame: { url: 'https://premiumstudiogames.github.io/' } };
const call = (ch, ...a) => ipcMain._invokeHandlers.get(ch)(ev, ...a);
const sleep = ms => new Promise(r => setTimeout(r, ms));
const results = []; let failed = 0;
const ok = (name, cond, extra) => { results.push(`${cond ? 'PASS' : 'FAIL'}  ${name}${extra ? '  (' + extra + ')' : ''}`); if (!cond) failed++; console.log(results[results.length - 1]); };
async function waitFor(fn, ms) { const t0 = Date.now(); while (Date.now() - t0 < ms) { try { if (await fn()) return true; } catch (e) {} await sleep(1000); } return false; }
const desk = path.join(os.homedir(), 'Desktop'), lnk = path.join(desk, 'BLOCKFALL 3001.lnk');
const bfData = path.join(process.env.APPDATA, '.terravolt');
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
    let lic = null; try { lic = JSON.parse(fs.readFileSync(path.join(bfData, 'license.json'), 'utf8')); } catch (e) {}
    ok('Gold edition handed to the launcher', lic && lic.ed === 'gold', lic && lic.ed);
    ok('app shows BLOCKFALL as running', !!(await call('ps:getRunning'))['blockfall--gold']);
    await sleep(5000);
    ok('no extra BLOCKFALL desktop icon', hadLnk || !fs.existsSync(lnk));
    await call('ps:stop', 'blockfall--gold');
    ok('BLOCKFALL closes from the Close button', await waitFor(async () => !(await call('ps:getRunning'))['blockfall--gold'], 40000));
    ok('BLOCKFALL process gone', await waitFor(() => !procAlive(), 15000));
    // ---- started outside Premium Studio: must hand over and come back through the app (like Steam)
    require('child_process').spawn(path.join(process.env.LOCALAPPDATA, 'Programs', 'TERRAVOLT', 'BLOCKFALL3001.exe'), [], { detached: true, stdio: 'ignore', env: process.env }).unref();
    ok('BLOCKFALL started directly goes through Premium Studio', await waitFor(async () => !!(await call('ps:getRunning'))['blockfall--gold'], 90000));
    await call('ps:stop', 'blockfall--gold');
    await waitFor(async () => !(await call('ps:getRunning'))['blockfall--gold'], 40000);
  } catch (e) { ok('no crash', false, e && e.stack || String(e)); }
  fs.writeFileSync(path.join(__dirname, 'e2e-result.txt'), results.join('\n') + '\n');
  console.log('\n' + results.join('\n') + `\n${failed ? failed + ' FAILED' : 'ALL PASSED'}`);
  app.exit(failed ? 1 : 0);
});
