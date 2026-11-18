// BLOCKFALL 3001 on Premium Engine — step 1: the world.
// The ground is made by BLOCKFALL's own rules (the same noise, height formula, mountains, biomes and their colours, lakes and
// trees as the game, from the same seed), in 16 m chunks on the engine's background threads: 20 cm blocks in the near
// layer (0 - 20 m, like the game), 1 m blocks below it and in the mountains. The world is 400 m tall like the game's.
// Left click: break.  Right click: place.  1 - 5: block size 20 cm - 1 m.  Q / E: change block.  Your changes are saved.
// Block numbers are BLOCKFALL's own (MATS ids), so worlds and designs can move over later.
const BLOCKS = [
  [1, 'عشب', ['rgb(101,168,67)', 'rgb(120,104,62)', 'rgb(136,91,57)'], { biome: 'top' }],
  [2, 'تراب', 'rgb(136,91,57)'],
  [3, 'حجر', 'rgb(141,144,149)'],
  [4, 'رمل', 'rgb(225,205,142)'],
  [5, 'جذع', 'rgb(144,104,61)'],
  [6, 'أوراق', 'rgb(54,113,43)', { biome: true }],
  [7, 'ألواح', 'rgb(183,142,83)'],
  [8, 'طوب', 'rgb(176,98,77)'],
  [10, 'أبيض', 'rgb(231,228,220)'],
  [19, 'حجر مرصوف', 'rgb(83,86,92)'],
  [20, 'طين', 'rgb(165,114,76)'],
  [23, 'أسود', 'rgb(42,42,48)'],
  [26, 'ألواح داكنة', 'rgb(97,63,36)'],
  [27, 'أوراق صنوبر', 'rgb(45,105,61)', { biome: true }],
  [168, 'حصى', 'rgb(129,123,121)'],
  [338, 'ثلج', 'rgb(239,243,245)'],
  [900, 'ماء', 'rgb(52,112,186)', { liquid: true, opacity: 0.72 }],
];
const PICK = BLOCKS.filter(b => b[0] !== 900);
const NAME = {}; for (const b of BLOCKS) NAME[b[0]] = b[1];
// BLOCKFALL's world: 1000 m square, you start in the middle (the engine's 0, 0 is BLOCKFALL's 500, 500).
// tint = the game's BTINT (biome colours of grass and leaves)
const PARAMS = { seed: 2026, size: 1000, ox: 500, oz: 500, water: 2.4, nearTop: 20,
  tint: { plains: [1, 1, 1], forest: [0.78, 0.92, 0.68], jungle: [0.55, 1.08, 0.5], savanna: [1.45, 1.12, 0.52], snow: [2.3, 2.25, 2.45], desert: [1.3, 1.1, 0.6] } };
let size = 0.2, pick = 0, spawned = false, saveIn = 0;

function onStart() {
  voxels.create({ bottom: -98, top: 302, nearLayer: [0, PARAMS.nearTop], viewDistance: 6, seed: PARAMS.seed });
  for (const [id, name, color, o] of BLOCKS) voxels.defineBlock(id, Object.assign({ name, color, vary: 0.06 }, o || {}));   // the game shades each block ±6 %
  voxels.setGenerator(function (chunk, noise, P) {
    const N = noise.bf, sd = P.seed, WSZ = P.size, SPX = P.size / 2, SPZ = P.size / 2;
    const smooth = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
    const biomeAt = (mx, mz) => { let t = N.fbm(mx * 0.0035, mz * 0.0035, sd + 101), h = N.fbm(mx * 0.0035, mz * 0.0035, sd + 202);
      { const d = Math.hypot(mx - SPX, mz - SPZ), w = 1 - smooth(40, 90, d); t = t + (0.48 - t) * w; h = h + (0.5 - h) * w; }   // start in green plains
      if (t < 0.36) return 'snow'; if (t > 0.6 && h < 0.44) return 'desert'; if (t > 0.56 && h < 0.56) return 'savanna'; if (h > 0.62) return t > 0.52 ? 'jungle' : 'forest'; return 'plains'; };
    // the game's terrainH for every 20 cm column of the chunk, from whole grids of BLOCKFALL noise (one C++ call each)
    const G = globalThis.__g || (globalThis.__g = { a: [] }); const grid = (i, fn, scale, s) => { const o = G.a[i] || (G.a[i] = new Float64Array(6400)); fn(o, 80, 80, chunk.x + P.ox + 0.1, chunk.z + P.oz + 0.1, 0.2, scale, s); return o; };
    const f1 = grid(0, N.fbmGrid, 0.012, sd), f2 = grid(1, N.fbmGrid, 0.045, sd + 5), v3 = grid(2, N.vnGrid, 0.4, sd + 3), lk = grid(3, N.fbmGrid, 0.008, sd + 77),
      m0 = grid(4, N.fbmGrid, 0.0017, sd + 500), m1 = grid(5, N.fbmGrid, 0.0055, sd + 501), m2 = grid(6, N.fbmGrid, 0.02, sd + 502), m3 = grid(7, N.vnGrid, 0.09, sd + 503);
    const hc = G.hc || (G.hc = new Int32Array(6400));     // ground height of every 20 cm column, in cells (the game rounds to whole cells)
    for (let i = 0; i < 6400; i++) {
      const mx = chunk.x + P.ox + ((i % 80) + 0.5) / 5, mz = chunk.z + P.oz + (Math.floor(i / 80) + 0.5) / 5;
      let h = 4.4 + (f1[i] - 0.5) * 4.6 + (f2[i] - 0.5) * 1.6 + (v3[i] - 0.5) * 0.3;
      if (lk[i] > 0.66) h = h + (1.3 - h) * smooth(0.66, 0.74, lk[i]);
      const e = Math.min(mx, mz, WSZ - mx, WSZ - mz); if (e < 45) h = 1 + (h - 1) * smooth(4, 45, e);
      const mask = smooth(.5, .66, m0[i]);                // the game's mountainM
      if (mask > 0) { const r = 1 - Math.abs(2 * m1[i] - 1); let mh = (Math.pow(r, 2.3) * 84 + m2[i] * 16 + (m3[i] - .5) * 3) * mask;
        mh *= smooth(140, 260, Math.hypot(mx - SPX, mz - SPZ)); mh *= smooth(40, 120, Math.min(mx, mz, WSZ - mx, WSZ - mz)); h += Math.max(0, mh); }
      hc[i] = Math.max(3, Math.round(Math.min(297, h) * 5));
    }
    const WC = Math.round(P.water * 5), FH = P.nearTop * 5, ids = G.ids || (G.ids = new Uint16Array(80 * 80 * FH)); ids.fill(0);
    let usedTop = 0;
    for (let X = 0; X < 16; X++) for (let Z = 0; Z < 16; Z++) {
      const mx = chunk.x + P.ox + X + 0.5, mz = chunk.z + P.oz + Z + 0.5, bio = biomeAt(mx, mz), tn = P.tint[bio];
      chunk.tint(X, Z, tn);
      let lo = 1e9, hi = -1e9; for (let a = 0; a < 5; a++) for (let c = 0; c < 5; c++) { const v = hc[(Z * 5 + c) * 80 + X * 5 + a]; lo = Math.min(lo, v); hi = Math.max(hi, v); }
      const steep = hi - lo > 6, rock = hi > 140 || steep && hi > 60;   // bare rock on peaks and cliffs
      const surf = rock ? 3 : bio === 'snow' || hi > 300 ? 338 : bio === 'desert' ? 4 : lo <= WC + 2 ? 4 : 1, under = rock ? 3 : surf === 4 ? 4 : 2;
      if (lo >= FH) {                                     // above the near layer (mountains): 1 m blocks, like the game's coarse layer
        const tm = Math.round((lo + hi) / 10);
        chunk.column(X, Z, -98, tm - 2, 3); chunk.column(X, Z, tm - 2, tm - 1, under); chunk.column(X, Z, tm - 1, tm, surf);
      } else {
        const full = Math.max(0, Math.floor((lo - 1) / 5));   // whole metres under every 20 cm column (each keeps at least its top cell)
        chunk.column(X, Z, -98, Math.max(-98, full - 1), 3);
        if (full >= 1) chunk.column(X, Z, full - 1, full, under);
        for (let a = 0; a < 5; a++) for (let c = 0; c < 5; c++) {
          const top = hc[(Z * 5 + c) * 80 + X * 5 + a], x = X * 5 + a, z = Z * 5 + c;
          for (let k = full * 5; k < Math.min(top, FH); k++) ids[x + 80 * (z + 80 * k)] = k === top - 1 ? surf : (top - k <= 4 && !rock ? under : 3);
          for (let k = top; k < WC; k++) ids[x + 80 * (z + 80 * k)] = 900;   // lakes and the sea, up to the game's water level
          usedTop = Math.max(usedTop, Math.min(FH, Math.max(top, WC)));
        }
        if (hi > FH) { const tm = Math.round(hi / 5); chunk.column(X, Z, FH / 5, tm - 1, rock ? 3 : 2); chunk.column(X, Z, tm - 1, tm, surf); }   // a slope that climbs out of the near layer
      }
    }
    if (usedTop) chunk.cells(0, 0, 0, 80, usedTop, 80, ids);   // all the 20 cm blocks of the chunk in one call
    // trees, as many as the game's biomes have (they may reach into the chunks next to this one)
    for (let X = 0; X < 16; X++) for (let Z = 0; Z < 16; Z++) {
      const mx = chunk.x + P.ox + X + 0.5, mz = chunk.z + P.oz + Z + 0.5, bio = biomeAt(mx, mz);
      const dens = bio === 'jungle' ? .05 : bio === 'forest' ? .035 : bio === 'plains' ? .006 : bio === 'savanna' ? .004 : bio === 'snow' ? .012 : 0;
      if (!dens || N.hashf(Math.floor(mx), 11, Math.floor(mz)) >= dens) continue;
      let lo = 1e9, hi = -1e9; for (let a = 0; a < 5; a++) for (let c = 0; c < 5; c++) { const v = hc[(Z * 5 + c) * 80 + X * 5 + a]; lo = Math.min(lo, v); hi = Math.max(hi, v); }
      if (lo <= WC + 2 || hi - lo > 6 || hi >= FH - 40) continue;
      const pine = bio === 'snow', base = hc[(Z * 5 + 2) * 80 + X * 5 + 2] / 5, th = 3 + Math.floor(N.hashf(Math.floor(mx), 12, Math.floor(mz)) * 3);
      for (let k = 0; k < th * 5; k += 2) chunk.set(X + 0.4, base + k / 5, Z + 0.4, 5, 0.4);
      const leaf = pine ? 27 : 6, cy = base + th;
      for (let dy = -1; dy <= 1; dy++) { const r = pine ? 1 - dy : dy === 1 ? 1 : 2;
        for (let dx = -r; dx <= r; dx++) for (let dz = -r; dz <= r; dz++) { if (Math.abs(dx) + Math.abs(dz) > r + (pine ? 0 : 1)) continue; if (!dx && !dz && dy < 1) continue;
          chunk.place(X + dx, Math.floor(cy + dy), Z + dz, leaf, 1); } }
    }
  }, PARAMS);
  voxels.load('world');   // your changes from last time (nothing the first time)
  world.hud('help', 'الزر اليسار: كسر   الزر اليمين: بناء   1-5: الحجم   Q / E: البلوكة');
}

function onUpdate(dt) {
  if (!spawned && voxels.ready(0, 0, 0)) {   // stand on the ground once it is built
    world.player.teleport(0.5, voxels.topY(0.5, 0.5) + 1.2, 0.5); spawned = true;
  }
  for (let i = 1; i <= 5; i++) if (input.pressed(String(i))) size = i / 5;
  if (input.pressed('e')) pick = (pick + 1) % PICK.length;
  if (input.pressed('q')) pick = (pick + PICK.length - 1) % PICK.length;
  const cam = world.camera, hit = voxels.raycast(cam.position, cam.forward, 7);
  if (hit && input.pressed('MouseLeft') && voxels.removeBlock(hit.point.clone().addScaledVector(hit.normal, -0.05))) saveIn = 1;
  if (hit && input.pressed('MouseRight')) {
    const p = hit.placePosition(size);
    if (!voxels.setBlock(p, PICK[pick][0], size)) world.message(voxels.lastError, 1.5); else saveIn = 1;
  }
  if (saveIn > 0 && (saveIn -= dt) <= 0) voxels.save('world');   // a second after a change (only the chunks you changed)
  world.hud('block', PICK[pick][1] + '  ' + Math.round(size * 100) + ' سم' + (hit ? '   تطالع: ' + (NAME[hit.id] || hit.id) : '') + (world.player.swimming ? '   تسبح' : ''));
}
