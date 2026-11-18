# BLOCKFALL 3001 — how the world is stored, made and drawn

For B3, to match Premium Engine's block world to the game. Source: `world.js` (the game's worker code, game 00.00.74),
`blocks.json` (every full block: BLOCKFALL id, name, colour, transparent, glow).

## Units
- `C = 0.2` m: one cell. Every position in the game is in cells. A block is `s` cells wide (1–5 = 20 cm–1 m) and `sy` cells tall (usually `s`).
- Chunk: `CS = 80` cells (16 m) square. Chunk `(cx, cz)` starts at cell `x0 = cx*80`, `z0 = cz*80`. World ids: `cx*1000+cz`.
- Near layer: cells `y = 0 .. HC-1`, `HC = 100` (0 to 20 m). Blocks of any size live here.
- Coarse layer (1 m blocks): cells `CY_MIN = -490` to `CY_MAX = 1510` (−98 m to +302 m), in 5-cell steps: `NCY = 400` rows, `CBW = 16` columns a side.
  Inside the near layer the coarse grid is not used (the near layer's own blocks are there).
- Sea / lakes: `WATER = 2.4` m.

## A chunk (output of `gen`)
- `rec`: one record per block of the near layer, 8 numbers: `x, y, z, s, m, flag, sy, r` (cells; `m` = block id; `flag` 1 = placed by a player; `r` = rotation 0–3).
  A block's local id is its record index + 1.
- `g` (`Uint16Array(80*80*100)`): local id of the block in each cell; index `(x-x0) + 80*((z-z0) + 80*y)`.
- `sol` (`Uint8Array`, same index): 1 if that cell hides the faces next to it (full, not transparent, not leaves).
- `gc` (`Uint16Array(16*16*400)`): coarse layer block id per 1 m cell; index `cIdx(X, Y, Z) = X + 16*(Z + 16*Y)`, `Y = (cellY - CY_MIN)/5`.
- `ctop`: highest coarse block per column; `cav`: cave flags.

## Making a world (`genNatural`, then `genDeep`)
- `terrainCells(x, z)` → ground height in cells for a 20 cm column. Per world kind (`GENV`): 1/2 normal, 5 deep (mountains `mountainM`, caves `caveAir`/`bandCave`), `'flat'`, `'flat1'`, `'ocean'`, `'dragon'`, 3/4 Earth map (`earthCells`, cities `cityTop`).
- `biomeAt(mx, mz)` (metres) → snow, desert, savanna, jungle, forest, plains. Picks the top block, trees, flowers and grass.
- `genNatural` fills the near layer: big blocks where the ground is flat, 20 cm where it slopes, then trees, flowers, rocks, water.
- `genDeep` fills the 1 m layer below and above it: stone, deep slate, ores (`oreVeins`: veins per chunk, spawn rates like Minecraft), caves, bedrock at the bottom.
- The game adds after the worker (main thread, in `finishGen`): the Stargate, dungeons, players' saved changes.
- Everything is seeded (`seed`), so the same seed gives the same world on every thread.

## Drawing (`mesh` and `cmesh`)
- `buildGeoArrays(list, get, kindOf)`: near-layer blocks → vertices. A face is skipped when the cells next to it are solid (`get` < 0).
  Each vertex gets: position, normal, colour (per-block tint `t`), atlas tile, UV, and AO (corner darkness from the 3 neighbours).
  Carved blocks (players sculpt a block's 5×5×5 shape) use `shapeTris`.
- `buildCoarseArrays`: the 1 m layer, split in an upper and lower part (`U`, `L`), same vertex data plus `dk` (cave darkness).
- Chunks far away are drawn with fewer details; carved blocks show their shape only within 14 m.

## Saved changes (per chunk key)
`{ r: [local ids removed], p: [[x,y,z,s,m,sy,r,sh,md], …] placed blocks, c: [[coarseIndex, id], …] }`
