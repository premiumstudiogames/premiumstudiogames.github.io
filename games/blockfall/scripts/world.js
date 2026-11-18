// BLOCKFALL 3001 on Premium Engine — step 1: the world.
// The ground is made by BLOCKFALL's own rules (the same height formula, mountains, biomes, lakes and trees as the game),
// in 16 m chunks on the engine's background threads: 20 cm blocks near the surface, 1 m blocks deeper and in the mountains.
// Left click: break.  Right click: place.  1 - 5: block size 20 cm - 1 m.  Q / E: change block.
// Block numbers are BLOCKFALL's own (MATS ids), so worlds and designs can move over later.
const BLOCKS = [
  [1, 'Grass', ['rgb(101,168,67)', 'rgb(120,104,62)', 'rgb(136,91,57)']],
  [2, 'Dirt', 'rgb(136,91,57)'],
  [3, 'Stone', 'rgb(141,144,149)'],
  [4, 'Sand', 'rgb(225,205,142)'],
  [5, 'Log', 'rgb(144,104,61)'],
  [6, 'Leaves', 'rgb(54,113,43)'],
  [7, 'Planks', 'rgb(183,142,83)'],
  [8, 'Brick', 'rgb(176,98,77)'],
  [10, 'White', 'rgb(231,228,220)'],
  [19, 'Cobblestone', 'rgb(83,86,92)'],
  [20, 'Clay', 'rgb(165,114,76)'],
  [23, 'Black', 'rgb(42,42,48)'],
  [26, 'Dark planks', 'rgb(97,63,36)'],
  [27, 'Pine leaves', 'rgb(45,105,61)'],
  [168, 'Gravel', 'rgb(129,123,121)'],
  [338, 'Snow', 'rgb(239,243,245)'],
  [900, 'Water', 'rgb(52,112,186)', { solid: false, transparent: true }],
];
const PICK = BLOCKS.filter(b => b[0] !== 900);
const NAME = {}; for (const b of BLOCKS) NAME[b[0]] = b[1];
// BLOCKFALL's world: 1000 m square, you start in the middle (the engine's 0, 0 is BLOCKFALL's 500, 500)
const PARAMS = { seed: 2026, size: 1000, ox: 500, oz: 500, water: 2.4, nearTop: 20 };
let size = 0.2, pick = 0, spawned = false;

function onStart() {
  voxels.create({ bottom: -40, top: 110, nearLayer: [-2, 20], viewDistance: 6, seed: PARAMS.seed });
  for (const [id, name, color, o] of BLOCKS) voxels.defineBlock(id, Object.assign({ name, color }, o || {}));
  voxels.setGenerator(function (chunk, noise, P) {
    // BLOCKFALL's noise and height rules (copied from the game), made once per thread
    const B = globalThis.__bf || (globalThis.__bf = (function () {
      const sd = P.seed, WSZ = P.size, SPX = P.size / 2, SPZ = P.size / 2;
      function h2(x, y, s) { let h = Math.imul(x, 374761393) + Math.imul(y, 668265263) + Math.imul(s, 982451653) | 0; h = Math.imul(h ^ (h >>> 13), 1274126177); h ^= h >>> 16; return (h >>> 0) / 4294967296; }
      function vn(x, y, s) { const xi = Math.floor(x), yi = Math.floor(y), xf = x - xi, yf = y - yi, u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
        const a = h2(xi, yi, s), b = h2(xi + 1, yi, s), c = h2(xi, yi + 1, s), d = h2(xi + 1, yi + 1, s); return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v; }
      function fbm(x, y, s) { return vn(x, y, s) * 0.6 + vn(x * 2.1, y * 2.1, s + 7) * 0.28 + vn(x * 4.3, y * 4.3, s + 13) * 0.12; }
      const smooth = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
      function hashf(x, y, z) { let h = Math.imul(x | 0, 73856093) ^ Math.imul(y | 0, 19349663) ^ Math.imul(z | 0, 83492791); h = Math.imul(h ^ (h >>> 13), 1274126177); h ^= h >>> 16; return (h >>> 0) / 4294967296; }
      function mountainM(mx, mz) { const mask = smooth(.5, .66, fbm(mx * .0017, mz * .0017, sd + 500)); if (mask <= 0) return 0;
        const r = 1 - Math.abs(2 * fbm(mx * .0055, mz * .0055, sd + 501) - 1);
        let h = (Math.pow(r, 2.3) * 84 + fbm(mx * .02, mz * .02, sd + 502) * 16 + (vn(mx * .09, mz * .09, sd + 503) - .5) * 3) * mask;
        h *= smooth(140, 260, Math.hypot(mx - SPX, mz - SPZ)); h *= smooth(40, 120, Math.min(mx, mz, WSZ - mx, WSZ - mz)); return Math.max(0, h); }
      // height in metres at a point (BLOCKFALL's terrainH for the deep worlds)
      function terrainH(mx, mz) {
        let h = 4.4 + (fbm(mx * 0.012, mz * 0.012, sd) - 0.5) * 4.6 + (fbm(mx * 0.045, mz * 0.045, sd + 5) - 0.5) * 1.6 + (vn(mx * 0.4, mz * 0.4, sd + 3) - 0.5) * 0.3;
        const lk = fbm(mx * 0.008, mz * 0.008, sd + 77); if (lk > 0.66) h = h + (1.3 - h) * smooth(0.66, 0.74, lk);
        const e = Math.min(mx, mz, WSZ - mx, WSZ - mz); if (e < 45) h = 1 + (h - 1) * smooth(4, 45, e);
        return Math.min(105, h + mountainM(mx, mz)); }
      function biomeAt(mx, mz) { let t = fbm(mx * 0.0035, mz * 0.0035, sd + 101), h = fbm(mx * 0.0035, mz * 0.0035, sd + 202);
        { const d = Math.hypot(mx - SPX, mz - SPZ), w = 1 - smooth(40, 90, d); t = t + (0.48 - t) * w; h = h + (0.5 - h) * w; }   // start in green plains
        if (t < 0.36) return 'snow'; if (t > 0.6 && h < 0.44) return 'desert'; if (t > 0.56 && h < 0.56) return 'savanna'; if (h > 0.62) return t > 0.52 ? 'jungle' : 'forest'; return 'plains'; }
      return { terrainH, biomeAt, hashf, vn, sd };
    })());
    const ox = P.ox, oz = P.oz, WATER = P.water, WC = Math.round(WATER * 5), FH = P.nearTop * 5;
    const hc = new Int32Array(80 * 80);   // ground height of every 20 cm column, in cells (BLOCKFALL rounds to whole cells)
    for (let z = 0; z < 80; z++) for (let x = 0; x < 80; x++) hc[z * 80 + x] = Math.max(3, Math.round(B.terrainH(chunk.x + ox + (x + 0.5) / 5, chunk.z + oz + (z + 0.5) / 5) * 5));
    for (let X = 0; X < 16; X++) for (let Z = 0; Z < 16; Z++) {
      const mx = chunk.x + ox + X + 0.5, mz = chunk.z + oz + Z + 0.5, bio = B.biomeAt(mx, mz);
      let lo = 1e9, hi = -1e9; for (let a = 0; a < 5; a++) for (let c = 0; c < 5; c++) { const v = hc[(Z * 5 + c) * 80 + X * 5 + a]; lo = Math.min(lo, v); hi = Math.max(hi, v); }
      const steep = hi - lo > 6, rock = hi > 140 || steep && hi > 60;   // bare rock on peaks and cliffs
      const surf = rock ? 3 : bio === 'snow' || hi > 300 ? 338 : bio === 'desert' ? 4 : lo <= WC + 2 ? 4 : 1;
      if (lo >= FH) {                                     // above the near layer (mountains): 1 m blocks, like BLOCKFALL's coarse layer
        const tm = Math.round((lo + hi) / 10);
        chunk.column(X, Z, -40, tm - 2, 3); chunk.column(X, Z, tm - 2, tm - 1, rock ? 3 : 2); chunk.column(X, Z, tm - 1, tm, surf);
      } else {
        const full = Math.floor((lo - 1) / 5);            // whole metres under every 20 cm column (each keeps at least its top cell)
        chunk.column(X, Z, -40, Math.max(-40, full - 1), 3);
        if (full - 1 >= -40) chunk.column(X, Z, full - 1, full, rock ? 3 : bio === 'desert' ? 4 : 2);
        for (let a = 0; a < 5; a++) for (let c = 0; c < 5; c++) {
          const top = hc[(Z * 5 + c) * 80 + X * 5 + a], x = X + a / 5, z = Z + c / 5;
          for (let k = full * 5; k < Math.min(top, FH); k++) chunk.set(x, k / 5, z, k === top - 1 ? surf : (top - k <= 4 && !rock ? (surf === 4 ? 4 : 2) : 3), 0.2);
          for (let k = top; k < WC; k++) chunk.set(x, k / 5, z, 900, 0.2);   // lakes and the sea, up to BLOCKFALL's water level
        }
        if (hi > FH) { const tm = Math.round(hi / 5); chunk.column(X, Z, FH / 5, tm - 1, rock ? 3 : 2); chunk.column(X, Z, tm - 1, tm, surf); }   // a slope that climbs out of the near layer
      }
      // trees (whole tree inside the chunk), as many as BLOCKFALL's biomes have
      const dens = bio === 'jungle' ? .05 : bio === 'forest' ? .035 : bio === 'plains' ? .006 : bio === 'savanna' ? .004 : bio === 'snow' ? .012 : 0;
      if (dens && !rock && hi < FH - 40 && X >= 2 && X <= 13 && Z >= 2 && Z <= 13 && lo > WC + 2 && !steep && B.hashf(Math.floor(mx), 11, Math.floor(mz)) < dens) {
        const pine = bio === 'snow', base = hc[(Z * 5 + 2) * 80 + X * 5 + 2] / 5, th = 3 + Math.floor(B.hashf(Math.floor(mx), 12, Math.floor(mz)) * 3);
        for (let k = 0; k < th * 5; k += 2) chunk.set(X + 0.4, base + k / 5, Z + 0.4, 5, 0.4);
        const leaf = pine ? 27 : 6, cy = base + th;
        for (let dy = -1; dy <= 1; dy++) { const r = pine ? 1 - dy : dy === 1 ? 1 : 2;
          for (let dx = -r; dx <= r; dx++) for (let dz = -r; dz <= r; dz++) { if (Math.abs(dx) + Math.abs(dz) > r + (pine ? 0 : 1)) continue; if (!dx && !dz && dy < 1) continue;
            chunk.set(X + dx, Math.floor(cy + dy), Z + dz, leaf, 1); } }
      }
    }
  }, PARAMS);
  world.hud('help', 'Left click: break   Right click: place   1-5: size   Q / E: block');
}

function onUpdate(dt) {
  if (!spawned && voxels.ready(0, 0, 0)) {   // stand on the ground once it is built
    world.player.teleport(0.5, voxels.topY(0.5, 0.5) + 1.2, 0.5); spawned = true;
  }
  for (let i = 1; i <= 5; i++) if (input.pressed(String(i))) size = i / 5;
  if (input.pressed('e')) pick = (pick + 1) % PICK.length;
  if (input.pressed('q')) pick = (pick + PICK.length - 1) % PICK.length;
  const cam = world.camera, hit = voxels.raycast(cam.position, cam.forward, 7);
  if (hit && input.pressed('MouseLeft')) voxels.removeBlock(hit.point.clone().addScaledVector(hit.normal, -0.05));
  if (hit && input.pressed('MouseRight')) {
    const p = hit.placePosition(size);
    if (!voxels.setBlock(p, PICK[pick][0], size)) world.message(voxels.lastError, 1.5);
  }
  world.hud('block', PICK[pick][1] + '  ' + Math.round(size * 100) + ' cm' + (hit ? '   looking at: ' + (NAME[hit.id] || hit.id) : ''));
}
