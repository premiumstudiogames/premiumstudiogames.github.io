// Block world: chunk data, generators (with writes that reach into the chunks around), background threads, meshing
// (hidden faces removed, corner shadows, greedy merging, shapes / carved / model blocks, liquids), collision shapes,
// block edits, ray casts and saving the player's changes. See voxel.h.
#include "voxel.h"
#include "renderer.h"
#include "gl.h"
#include "log.h"
#include <quickjs.h>
#include <nlohmann/json.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_set>
#include <fstream>
#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>

using json = nlohmann::json;
namespace pge {

static inline int floorDiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
static inline int posMod(int a, int b) { int m = a % b; return m < 0 ? m + b : m; }
static inline int64_t ckey(int cx, int cz) { return ((int64_t)(uint32_t)cx << 32) | (uint32_t)cz; }
static double msSince(std::chrono::steady_clock::time_point t) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); }

/* ------------------------------------------------------------------ noise (simplex, Gustavson) */
static const int GRAD3[12][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
struct Perm { uint8_t p[512]; };
static const Perm& perm(uint32_t seed) {
  static std::mutex m; static std::unordered_map<uint32_t, std::unique_ptr<Perm>> cache;
  std::lock_guard<std::mutex> g(m); auto& e = cache[seed];
  if (!e) { e.reset(new Perm); uint8_t a[256]; for (int i = 0; i < 256; i++) a[i] = (uint8_t)i; uint32_t s = seed * 2654435761u + 0x9e3779b9u;
    for (int i = 255; i > 0; i--) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; int j = (int)(s % (uint32_t)(i + 1)); std::swap(a[i], a[j]); }
    for (int i = 0; i < 512; i++) e->p[i] = a[i & 255]; }
  return *e;
}
float simplex2(float xin, float yin, uint32_t seed) {
  const uint8_t* p = perm(seed).p; const float F2 = 0.5f * (std::sqrt(3.f) - 1), G2 = (3 - std::sqrt(3.f)) / 6;
  float s = (xin + yin) * F2; int i = (int)std::floor(xin + s), j = (int)std::floor(yin + s); float t = (i + j) * G2;
  float x0 = xin - (i - t), y0 = yin - (j - t); int i1 = x0 > y0, j1 = !i1;
  float x1 = x0 - i1 + G2, y1 = y0 - j1 + G2, x2 = x0 - 1 + 2 * G2, y2 = y0 - 1 + 2 * G2; int ii = i & 255, jj = j & 255;
  auto c = [&](float x, float y, int g) { float t = 0.5f - x * x - y * y; if (t < 0) return 0.f; t *= t; return t * t * (GRAD3[g][0] * x + GRAD3[g][1] * y); };
  return 70.f * (c(x0, y0, p[ii + p[jj]] % 12) + c(x1, y1, p[ii + i1 + p[jj + j1]] % 12) + c(x2, y2, p[ii + 1 + p[jj + 1]] % 12));
}
float simplex3(float xin, float yin, float zin, uint32_t seed) {
  const uint8_t* p = perm(seed).p; const float F3 = 1.f / 3, G3 = 1.f / 6;
  float s = (xin + yin + zin) * F3; int i = (int)std::floor(xin + s), j = (int)std::floor(yin + s), k = (int)std::floor(zin + s); float t = (i + j + k) * G3;
  float x0 = xin - (i - t), y0 = yin - (j - t), z0 = zin - (k - t); int i1, j1, k1, i2, j2, k2;
  if (x0 >= y0) { if (y0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; } else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; } else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; } }
  else { if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; } else if (x0 < z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; } else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; } }
  float x1 = x0 - i1 + G3, y1 = y0 - j1 + G3, z1 = z0 - k1 + G3, x2 = x0 - i2 + 2 * G3, y2 = y0 - j2 + 2 * G3, z2 = z0 - k2 + 2 * G3, x3 = x0 - 1 + 3 * G3, y3 = y0 - 1 + 3 * G3, z3 = z0 - 1 + 3 * G3;
  int ii = i & 255, jj = j & 255, kk = k & 255;
  auto c = [&](float x, float y, float z, int g) { float t = 0.6f - x * x - y * y - z * z; if (t < 0) return 0.f; t *= t; return t * t * (GRAD3[g][0] * x + GRAD3[g][1] * y + GRAD3[g][2] * z); };
  return 32.f * (c(x0, y0, z0, p[ii + p[jj + p[kk]]] % 12) + c(x1, y1, z1, p[ii + i1 + p[jj + j1 + p[kk + k1]]] % 12) +
                 c(x2, y2, z2, p[ii + i2 + p[jj + j2 + p[kk + k2]]] % 12) + c(x3, y3, z3, p[ii + 1 + p[jj + 1 + p[kk + 1]]] % 12));
}
float fbm2(float x, float y, uint32_t seed, int oct, float lac, float gain) {
  float a = 1, f = 1, sum = 0, norm = 0; oct = std::max(1, std::min(oct, 10));
  for (int i = 0; i < oct; i++) { sum += a * simplex2(x * f, y * f, seed + (uint32_t)i * 101u); norm += a; a *= gain; f *= lac; }
  return sum / norm;
}
static float hash3(int x, int y, int z, uint32_t seed) {
  uint32_t h = seed ^ (uint32_t)x * 374761393u ^ (uint32_t)y * 668265263u ^ (uint32_t)z * 2147483647u; h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16;
  return (h & 0xffffff) / 16777216.f;
}

/* ---- BLOCKFALL's noise: JavaScript number rules (ToInt32, Math.imul, doubles) so the world is the same as the game's */
namespace bf {
static inline uint32_t u32(double v) {   // ECMAScript ToUint32 (ToInt32 has the same bits)
  if (!std::isfinite(v)) return 0; double t = std::trunc(v); double m = std::fmod(t, 4294967296.0); if (m < 0) m += 4294967296.0; return (uint32_t)m; }
static inline double mix32(uint32_t h) { h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16; return h / 4294967296.0; }
double h2(double x, double y, double s) { return mix32(u32(x) * 374761393u + u32(y) * 668265263u + u32(s) * 982451653u); }
double h3(double x, double y, double z, double s) { return mix32(u32(x) * 374761393u + u32(y) * 668265263u + u32(z) * 1440662683u + u32(s) * 982451653u); }
double hashf(double x, double y, double z) { return mix32((u32(x) * 73856093u) ^ (u32(y) * 19349663u) ^ (u32(z) * 83492791u)); }
double vn(double x, double y, double s) {
  const double xi = std::floor(x), yi = std::floor(y), xf = x - xi, yf = y - yi, u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
  const double a = h2(xi, yi, s), b = h2(xi + 1, yi, s), c = h2(xi, yi + 1, s), d = h2(xi + 1, yi + 1, s);
  return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
double fbm(double x, double y, double s) { return vn(x, y, s) * 0.6 + vn(x * 2.1, y * 2.1, s + 7) * 0.28 + vn(x * 4.3, y * 4.3, s + 13) * 0.12; }
double vn3(double x, double y, double z, double s) {
  const double xi = std::floor(x), yi = std::floor(y), zi = std::floor(z), xf = x - xi, yf = y - yi, zf = z - zi;
  const double u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf), w = zf * zf * (3 - 2 * zf);
  auto L = [](double a, double b, double t) { return a + (b - a) * t; };
  return L(L(L(h3(xi, yi, zi, s), h3(xi + 1, yi, zi, s), u), L(h3(xi, yi + 1, zi, s), h3(xi + 1, yi + 1, zi, s), u), v),
           L(L(h3(xi, yi, zi + 1, s), h3(xi + 1, yi, zi + 1, s), u), L(h3(xi, yi + 1, zi + 1, s), h3(xi + 1, yi + 1, zi + 1, s), u), v), w);
}
}

/* ------------------------------------------------------------------ shapes */
static const char* SHAPE_NAMES[SH_Count] = {"cube", "slope", "half", "hex", "arch", "wedge", "cylinder", "pyramid", "roof", "stairs", "cross"};
int shapeFromName(const std::string& s) { for (int i = 0; i < SH_Count; i++) if (s == SHAPE_NAMES[i]) return i; if (s == "slab") return SH_Half; if (s == "stair") return SH_Stairs; if (s == "plant") return SH_Cross; return -1; }
const char* shapeName(int sh) { return sh >= 0 && sh < SH_Count ? SHAPE_NAMES[sh] : "cube"; }

/* ------------------------------------------------------------------ chunk data */
struct Brick { Cell c[BRICK * BRICK * BRICK]; };
using ExtMap = std::unordered_map<uint32_t, BlockExtra>;
struct ChunkData {
  std::shared_ptr<std::vector<uint16_t>> coarse;    // 16 x 16 x H metres
  std::vector<std::shared_ptr<Brick>> bricks;       // near layer
  std::shared_ptr<std::vector<uint16_t>> fill;      // per brick: when there is no brick, the whole brick is 1 m blocks of this id (ground under the surface)
  std::shared_ptr<std::vector<uint32_t>> tint;      // 16 x 16 biome colours (r, g, b bytes, 64 = 1.0); null = none
  std::shared_ptr<ExtMap> ext;                      // turned / shaped / carved blocks, by origin (extKey)
};
struct Geo {
  int minY, maxY, H, bandLo, bandHi, bandH, BY;     // bandLo/bandHi in cells (fine y)
  explicit Geo(const VoxelConfig& c) : minY(c.minY), maxY(c.maxY), H(c.maxY - c.minY), bandLo(c.fineLo * VC), bandHi(c.fineHi * VC),
    bandH((c.fineHi - c.fineLo) * VC), BY(((c.fineHi - c.fineLo) * VC + BRICK - 1) / BRICK) {}
  bool inBand(int fy) const { return fy >= bandLo && fy < bandHi; }
  bool metreInBand(int my) const { return my * VC >= bandLo && my * VC < bandHi; }
  int coarseIdx(int x, int my, int z) const { return ((my - minY) * CHUNK_M + z) * CHUNK_M + x; }
  int brickIdx(int lx, int by, int lz) const { return ((by >> 3) * BRICKS_XZ + (lz >> 3)) * BRICKS_XZ + (lx >> 3); }
  static int inBrick(int lx, int by, int lz) { return ((by & 7) * BRICK + (lz & 7)) * BRICK + (lx & 7); }
  uint32_t extKey(int lx, int fy, int lz) const { return (uint32_t)(lx + 16) | (uint32_t)(lz + 16) << 8 | (uint32_t)(fy - bandLo + 16) << 16; }
};
static ChunkData newChunkData(const Geo& g) {
  ChunkData d; d.coarse = std::make_shared<std::vector<uint16_t>>((size_t)CHUNK_M * CHUNK_M * g.H, 0); d.bricks.resize((size_t)BRICKS_XZ * BRICKS_XZ * g.BY); d.fill = std::make_shared<std::vector<uint16_t>>(d.bricks.size(), 0); return d;
}
// lx, lz inside the chunk (0..79), fy world fine y
static Cell readCell(const ChunkData& d, const Geo& g, int lx, int fy, int lz) {
  if (g.inBand(fy)) { if (d.bricks.empty()) return 0; int by = fy - g.bandLo, bi = g.brickIdx(lx, by, lz); const auto& b = d.bricks[bi]; if (b) return b->c[Geo::inBrick(lx, by, lz)];
    int id = (*d.fill)[bi]; return id ? makeCell(id, VC, lx % VC, posMod(fy, VC), lz % VC) : 0; }
  int my = floorDiv(fy, VC); if (my < g.minY || my >= g.maxY || !d.coarse) return 0;
  int id = (*d.coarse)[g.coarseIdx(lx / VC, my, lz / VC)];
  return id ? makeCell(id, VC, lx % VC, posMod(fy, VC), lz % VC) : 0;
}
static const BlockExtra* extOf(const ChunkData& d, const Geo& g, int olx, int ofy, int olz) {
  if (!d.ext) return nullptr; auto it = d.ext->find(g.extKey(olx, ofy, olz)); return it == d.ext->end() ? nullptr : &it->second;
}

// a brick given as "all 1 m blocks of one id" turned into real cells (before one of them changes)
static std::shared_ptr<Brick> latticeBrick(const Geo& g, int bi, int id) {
  auto b = std::make_shared<Brick>(); int bx = bi % BRICKS_XZ, bz = (bi / BRICKS_XZ) % BRICKS_XZ, byk = bi / (BRICKS_XZ * BRICKS_XZ);
  for (int y = 0; y < BRICK; y++) for (int z = 0; z < BRICK; z++) for (int x = 0; x < BRICK; x++) {
    int lx = bx * BRICK + x, lz = bz * BRICK + z, fy = g.bandLo + byk * BRICK + y;
    b->c[Geo::inBrick(lx, y, lz)] = fy < g.bandHi ? makeCell(id, VC, lx % VC, posMod(fy, VC), lz % VC) : 0; }
  return b;
}
// bricks that are empty, or only whole 1 m blocks of one id, take no memory
static void compressBricks(ChunkData& d, const Geo& g) {
  for (size_t bi = 0; bi < d.bricks.size(); bi++) { auto& b = d.bricks[bi]; if (!b) continue;
    Cell first = b->c[0]; bool empty = true, lattice = first && cellSize(first) == VC; int id = cellId(first);
    int bx = (int)bi % BRICKS_XZ, bz = ((int)bi / BRICKS_XZ) % BRICKS_XZ, byk = (int)bi / (BRICKS_XZ * BRICKS_XZ);
    for (int y = 0; y < BRICK && (empty || lattice); y++) for (int z = 0; z < BRICK; z++) for (int x = 0; x < BRICK; x++) {
      int lx = bx * BRICK + x, lz = bz * BRICK + z, fy = g.bandLo + byk * BRICK + y; Cell c = b->c[Geo::inBrick(lx, y, lz)];
      if (c) empty = false;
      if (lattice && fy < g.bandHi && c != makeCell(id, VC, lx % VC, posMod(fy, VC), lz % VC)) lattice = false; }
    if (empty) { b.reset(); (*d.fill)[bi] = 0; } else if (lattice) { b.reset(); (*d.fill)[bi] = (uint16_t)id; }
  }
}

/* ------------------------------------------------------------------ block table */
struct BlockTable { std::vector<BlockType> t; std::vector<uint8_t> opaque, solid, liquid, cube; };
using BlockTablePtr = std::shared_ptr<const BlockTable>;
static const BlockType& typeOf(const BlockTable& T, int id) { static BlockType missing; if (id >= 0 && id < (int)T.t.size() && T.t[id].defined) return T.t[id];
  missing.color[0] = missing.color[1] = missing.color[2] = glm::vec3(1, 0, 1); return missing; }
static inline bool isLiquid(const BlockTable& T, int id) { return id > 0 && id < (int)T.liquid.size() && T.liquid[id]; }
static inline bool isCubeType(const BlockTable& T, int id) { return id >= (int)T.cube.size() || T.cube[id]; }
static inline bool special(const BlockTable& T, Cell c) { return cellExt(c) || !isCubeType(T, cellId(c)); }   // meshed one by one (shapes, models, carved, turned)
static inline bool opaqueCell(const BlockTable& T, Cell c) { int id = cellId(c); return id && !cellExt(c) && (id >= (int)T.opaque.size() || T.opaque[id]); }
static inline bool emptyish(const BlockTable& T, Cell c) { return !c || isLiquid(T, cellId(c)); }   // a block may go here (air or water)

/* ------------------------------------------------------------------ generators */
// a write that reaches into a chunk next to the one being generated; origin in that chunk's own cells
struct SpillBlock {
  int8_t dx = 0, dz = 0; uint8_t kind = 0;           // kind 0: one block, 1: a 1 m column (x, z metres; y0..y1 metres)
  int16_t ox = 0, oz = 0; int32_t oy = 0, y1 = 0; uint8_t s = 5, sy = 5; uint16_t id = 0; bool onlyEmpty = false, hasExt = false; BlockExtra ext;
};
struct GenWriter final : ChunkWriter {
  ChunkData& d; const Geo& g; const BlockTable& T; int cx_, cz_; std::vector<SpillBlock> spills; std::string err;
  GenWriter(ChunkData& d, const Geo& g, const BlockTable& T, int cx, int cz) : d(d), g(g), T(T), cx_(cx), cz_(cz) {}
  int cx() const override { return cx_; }
  int cz() const override { return cz_; }
  void putFine(int lx, int fy, int lz, Cell c) {
    int by = fy - g.bandLo, bi = g.brickIdx(lx, by, lz); auto& b = d.bricks[bi];
    if (!b) { int f = (*d.fill)[bi]; if (f) { b = latticeBrick(g, bi, f); (*d.fill)[bi] = 0; } else { if (!c) return; b = std::make_shared<Brick>(); std::memset(b->c, 0, sizeof b->c); } }
    b->c[Geo::inBrick(lx, by, lz)] = c;
  }
  void metre(int x, int my, int z, int id) {
    if (my < g.minY || my >= g.maxY) return;
    if (x < 0 || x >= CHUNK_M || z < 0 || z >= CHUNK_M) {   // a neighbour's metre: one column piece
      int dx = floorDiv(x, CHUNK_M), dz = floorDiv(z, CHUNK_M); if (std::abs(dx) > 1 || std::abs(dz) > 1) return;
      SpillBlock s; s.dx = (int8_t)dx; s.dz = (int8_t)dz; s.kind = 1; s.ox = (int16_t)(x - dx * CHUNK_M); s.oz = (int16_t)(z - dz * CHUNK_M); s.oy = my; s.y1 = my + 1; s.id = (uint16_t)id; spills.push_back(s); return; }
    if (g.metreInBand(my)) { for (int a = 0; a < VC; a++) for (int b = 0; b < VC; b++) for (int c = 0; c < VC; c++) putFine(x * VC + a, my * VC + b, z * VC + c, makeCell(id, VC, a, b, c)); }
    else (*d.coarse)[g.coarseIdx(x, my, z)] = (uint16_t)id;
  }
  void column(int x, int z, int y0, int y1, int id) override {
    y0 = std::max(y0, g.minY); y1 = std::min(y1, g.maxY); if (y0 >= y1) return;
    if (x < 0 || x >= CHUNK_M || z < 0 || z >= CHUNK_M) {
      int dx = floorDiv(x, CHUNK_M), dz = floorDiv(z, CHUNK_M); if (std::abs(dx) > 1 || std::abs(dz) > 1) { tooFar(); return; }
      SpillBlock s; s.dx = (int8_t)dx; s.dz = (int8_t)dz; s.kind = 1; s.ox = (int16_t)(x - dx * CHUNK_M); s.oz = (int16_t)(z - dz * CHUNK_M); s.oy = y0; s.y1 = y1; s.id = (uint16_t)id; spills.push_back(s); return; }
    for (int y = y0; y < y1; y++) metre(x, y, z, id);
  }
  void fill(int x0, int y0, int z0, int x1, int y1, int z1, int id) override {
    for (int x = x0; x < x1; x++) for (int z = z0; z < z1; z++) column(x, z, y0, y1, id);
  }
  void tooFar() { if (err.empty()) err = "a generator wrote more than one chunk (16 m) outside its chunk; that part was left out"; }
  bool set(float x, float y, float z, int id, float size, const BlockOpts& o) override {
    int s = (int)std::lround(size * VC); if (s < 1 || s > VC) return false; int sy = o.sy > 0 ? o.sy : s; if (sy > VC) return false;
    int ox = (int)std::floor(x * VC + 1e-3f), oy = (int)std::floor(y * VC + 1e-3f), oz = (int)std::floor(z * VC + 1e-3f);
    if (ox < -CHUNK_C || oz < -CHUNK_C || ox + s > 2 * CHUNK_C || oz + s > 2 * CHUNK_C) { tooFar(); return false; }
    bool ext = o.extra.any();
    if (g.inBand(oy) && g.inBand(oy + sy - 1)) {
      if (o.onlyEmpty) for (int a = 0; a < s; a++) for (int b = 0; b < sy; b++) for (int c = 0; c < s; c++) {
        int lx = ox + a, lz = oz + c; if (lx < 0 || lx >= CHUNK_C || lz < 0 || lz >= CHUNK_C) continue; if (!emptyish(T, readCell(d, g, lx, oy + b, lz))) return false; }
      bool inside = false;
      for (int a = 0; a < s; a++) for (int b = 0; b < sy; b++) for (int c = 0; c < s; c++) {
        int lx = ox + a, lz = oz + c; if (lx < 0 || lx >= CHUNK_C || lz < 0 || lz >= CHUNK_C) continue;
        putFine(lx, oy + b, lz, makeCell(id, s, a, b, c, sy, ext)); inside = true; }
      if (ext && inside) { if (!d.ext) d.ext = std::make_shared<ExtMap>(); (*d.ext)[g.extKey(ox, oy, oz)] = o.extra; }
      // the parts in the chunks around: added when those chunks are made
      for (int dz = floorDiv(oz, CHUNK_C); dz <= floorDiv(oz + s - 1, CHUNK_C); dz++) for (int dx = floorDiv(ox, CHUNK_C); dx <= floorDiv(ox + s - 1, CHUNK_C); dx++) {
        if (!dx && !dz) continue;
        SpillBlock sp; sp.dx = (int8_t)dx; sp.dz = (int8_t)dz; sp.ox = (int16_t)(ox - dx * CHUNK_C); sp.oz = (int16_t)(oz - dz * CHUNK_C); sp.oy = oy; sp.s = (uint8_t)s; sp.sy = (uint8_t)sy;
        sp.id = (uint16_t)id; sp.onlyEmpty = o.onlyEmpty; sp.hasExt = ext; sp.ext = o.extra; spills.push_back(sp); }
      return true;
    }
    if (s != VC || sy != VC || ext || g.inBand(oy) || g.inBand(oy + sy - 1)) return false;
    int mx = floorDiv(ox, VC), my = floorDiv(oy, VC), mz = floorDiv(oz, VC);
    if (o.onlyEmpty && mx >= 0 && mx < CHUNK_M && mz >= 0 && mz < CHUNK_M && my >= g.minY && my < g.maxY && (*d.coarse)[g.coarseIdx(mx, my, mz)] && !isLiquid(T, (*d.coarse)[g.coarseIdx(mx, my, mz)])) return false;
    metre(mx, my, mz, id); return true;
  }
  int get(float x, float y, float z) const override {
    int lx = (int)std::floor(x * VC + 1e-3f), fy = (int)std::floor(y * VC + 1e-3f), lz = (int)std::floor(z * VC + 1e-3f);
    if (lx < 0 || lx >= CHUNK_C || lz < 0 || lz >= CHUNK_C) return 0; return cellId(readCell(d, g, lx, fy, lz));
  }
  void cells(float x, float y, float z, int w, int h, int dd, const uint8_t* ids, int bpe) {
    int ox = (int)std::floor(x * VC + 1e-3f), oy = (int)std::floor(y * VC + 1e-3f), oz = (int)std::floor(z * VC + 1e-3f);
    for (int j = 0; j < h; j++) { int fy = oy + j; if (!g.inBand(fy)) continue;
      for (int k = 0; k < dd; k++) { int lz = oz + k; if (lz < 0 || lz >= CHUNK_C) continue;
        for (int i = 0; i < w; i++) { int lx = ox + i; if (lx < 0 || lx >= CHUNK_C) continue; size_t at = (size_t)i + (size_t)w * (k + (size_t)dd * j);
          uint32_t id = bpe == 2 ? ((const uint16_t*)ids)[at] : ((const uint32_t*)ids)[at]; if (id) putFine(lx, fy, lz, makeCell((int)(id & 0xffff), 1, 0, 0, 0)); } } }
  }
  void tint(int x, int z, glm::vec3 c) override {
    if (x < 0 || x >= CHUNK_M || z < 0 || z >= CHUNK_M) return;
    if (!d.tint) d.tint = std::make_shared<std::vector<uint32_t>>(CHUNK_M * CHUNK_M, 0x404040u);
    auto q = [](float v) { return (uint32_t)std::lround(glm::clamp(v, 0.f, 3.98f) * 64.f); };
    (*d.tint)[z * CHUNK_M + x] = q(c.r) | q(c.g) << 8 | q(c.b) << 16;
  }
};

static GeneratorFn builtinGenerator(const std::string& kind, const json& p, uint32_t seed) {
  int top = p.value("top", 1), mid = p.value("dirt", 2), deep = p.value("stone", 3), water = p.value("water", 0);
  float ground = p.value("ground", 0.f);
  if (kind == "flat") return [=](ChunkWriter& w) { int gy = (int)std::floor(ground); for (int x = 0; x < CHUNK_M; x++) for (int z = 0; z < CHUNK_M; z++) {
      w.column(x, z, -1000, gy - 3, deep); w.column(x, z, gy - 3, gy - 1, mid); w.column(x, z, gy - 1, gy, top); } };
  float amp = p.value("amplitude", 9.f), scale = p.value("scale", 0.012f); int oct = p.value("octaves", 4); bool smooth = p.value("smooth", true);
  return [=](ChunkWriter& w) {
    for (int x = 0; x < CHUNK_M; x++) for (int z = 0; z < CHUNK_M; z++) {
      float wx = (float)(w.cx() * CHUNK_M + x), wz = (float)(w.cz() * CHUNK_M + z);
      float h = ground + fbm2(wx * scale, wz * scale, seed, oct, 2.f, 0.5f) * amp;
      int hm = (int)std::floor(h);
      w.column(x, z, -1000, hm - 3, deep); w.column(x, z, hm - 3, hm - 1, mid); w.column(x, z, hm - 1, hm, top);
      if (smooth) { int extra = (int)std::floor((h - hm) * VC); for (int k = 0; k < extra; k++) for (int a = 0; a < VC; a++) for (int c = 0; c < VC; c++)
        w.set(x + a / (float)VC, hm + k / (float)VC, z + c / (float)VC, top, 1.f / VC); }
      if (water && hm < (int)ground - 1) w.column(x, z, hm, (int)ground - 1, water);
    }
  };
}

/* ---- block options (turn, height, shape, carved form) from JSON (main script) and from JavaScript objects (generators) */
static bool carveFromJson(const json& j, BlockExtra& e) {
  if (j.is_array() && j.size() == 125) { e.carve = {{0, 0}}; for (int b = 0; b < 125; b++) if (j[b].is_number() ? j[b].get<double>() != 0 : j[b].is_boolean() && j[b].get<bool>()) e.carve[b >> 6] |= 1ull << (b & 63); e.carved = true; return true; }
  if (j.is_string() && j.get<std::string>().size() == 125) { e.carve = {{0, 0}}; const std::string s = j; for (int b = 0; b < 125; b++) if (s[b] == '1') e.carve[b >> 6] |= 1ull << (b & 63); e.carved = true; return true; }
  return false;
}
bool blockOptsFromJson(const std::string& text, BlockOpts& o, std::string* why) {
  json j; try { j = json::parse(text); } catch (...) { if (why) *why = "block options are not valid"; return false; }
  if (!j.is_object()) return true;
  if (j.contains("height")) { double h = j["height"].is_number() ? j["height"].get<double>() : 0; int sy = (int)std::lround(h * VC);
    if (sy < 1 || sy > VC || std::abs(h * VC - sy) > 0.05) { if (why) *why = "height must be 0.2, 0.4, 0.6, 0.8 or 1"; return false; } o.sy = sy; }
  if (j.contains("rotation")) { int r = j["rotation"].is_number() ? j["rotation"].get<int>() : 0; if (r < 0 || r > 11) { if (why) *why = "rotation must be 0..11 (0-3 turns, +4 upside down, +8 on its side)"; return false; } o.extra.rot = (uint8_t)r; }
  if (j.contains("shape")) { int sh = j["shape"].is_number() ? j["shape"].get<int>() : j["shape"].is_string() ? shapeFromName(j["shape"].get<std::string>()) : -1;
    if (sh < 0 || sh >= SH_Count) { if (why) *why = "unknown shape (cube, slope, half, hex, arch, wedge, cylinder, pyramid, roof, stairs, cross)"; return false; } o.extra.shape = (uint8_t)(sh + 1); }
  if (j.contains("carve") && !j["carve"].is_null()) { if (!carveFromJson(j["carve"], o.extra)) { if (why) *why = "carve must be 125 numbers (0 or 1) or a string of 125 '0' / '1'"; return false; } }
  o.onlyEmpty = j.value("onlyEmpty", false);
  return true;
}
static double JD(JSContext* c, JSValueConst v) { double d = 0; JS_ToFloat64(c, &d, v); return d; }
static bool optsFromJs(JSContext* c, JSValueConst v, BlockOpts& o, std::string& why) {
  if (!JS_IsObject(v)) return true;
  JSValue h = JS_GetPropertyStr(c, v, "height"); if (!JS_IsUndefined(h)) { double m = JD(c, h); int sy = (int)std::lround(m * VC); if (sy < 1 || sy > VC) { JS_FreeValue(c, h); why = "height must be 0.2 .. 1"; return false; } o.sy = sy; } JS_FreeValue(c, h);
  JSValue r = JS_GetPropertyStr(c, v, "rotation"); if (!JS_IsUndefined(r)) { int x = (int)JD(c, r); if (x < 0 || x > 11) { JS_FreeValue(c, r); why = "rotation must be 0..11"; return false; } o.extra.rot = (uint8_t)x; } JS_FreeValue(c, r);
  JSValue s = JS_GetPropertyStr(c, v, "shape"); if (!JS_IsUndefined(s)) { int sh = -1; if (JS_IsString(s)) { const char* t = JS_ToCString(c, s); if (t) { sh = shapeFromName(t); JS_FreeCString(c, t); } } else sh = (int)JD(c, s);
    if (sh < 0 || sh >= SH_Count) { JS_FreeValue(c, s); why = "unknown shape"; return false; } o.extra.shape = (uint8_t)(sh + 1); } JS_FreeValue(c, s);
  JSValue k = JS_GetPropertyStr(c, v, "carve"); if (!JS_IsUndefined(k) && !JS_IsNull(k)) {
    size_t n = 0; json arr = json::array(); bool ok = false;
    if (JS_IsString(k)) { const char* t = JS_ToCStringLen(c, &n, k); if (t) { ok = carveFromJson(json(std::string(t, n)), o.extra); JS_FreeCString(c, t); } }
    else { JSValue len = JS_GetPropertyStr(c, k, "length"); int L = (int)JD(c, len); JS_FreeValue(c, len);
      if (L == 125) { o.extra.carve = {{0, 0}}; for (int b = 0; b < 125; b++) { JSValue e = JS_GetPropertyUint32(c, k, b); if (JS_ToBool(c, e)) o.extra.carve[b >> 6] |= 1ull << (b & 63); JS_FreeValue(c, e); } o.extra.carved = ok = true; } }
    if (!ok) { JS_FreeValue(c, k); why = "carve must be 125 numbers (0 or 1)"; return false; } }
  JS_FreeValue(c, k);
  JSValue e = JS_GetPropertyStr(c, v, "onlyEmpty"); if (!JS_IsUndefined(e)) o.onlyEmpty = JS_ToBool(c, e); JS_FreeValue(c, e);
  return true;
}

/* ---- JavaScript generators: one QuickJS engine per background thread */
struct JsGen { JSRuntime* rt = nullptr; JSContext* ctx = nullptr; JSValue fn = JS_UNDEFINED, params = JS_UNDEFINED, chunk = JS_UNDEFINED, noise = JS_UNDEFINED; int version = -1;
  ~JsGen() { if (ctx) { JS_FreeValue(ctx, fn); JS_FreeValue(ctx, params); JS_FreeValue(ctx, chunk); JS_FreeValue(ctx, noise); JS_FreeContext(ctx); JS_FreeRuntime(rt); } } };
static thread_local GenWriter* t_writer = nullptr;
static thread_local uint32_t t_seed = 0;
static thread_local std::chrono::steady_clock::time_point t_deadline;
static std::string jsErr(JSContext* c) {
  JSValue e = JS_GetException(c); const char* s = JS_ToCString(c, e); std::string m = s ? s : "error"; if (s) JS_FreeCString(c, s);
  JSValue st = JS_GetPropertyStr(c, e, "stack"); if (!JS_IsUndefined(st)) { const char* t = JS_ToCString(c, st); if (t) { std::string ts = t; JS_FreeCString(c, t);
      auto p = ts.find(':'); if (p != std::string::npos) { size_t q = p + 1; std::string num; while (q < ts.size() && std::isdigit((unsigned char)ts[q])) num += ts[q++]; if (!num.empty()) m += " (line " + num + ")"; } } }
  JS_FreeValue(c, st); JS_FreeValue(c, e); return m;
}
#define GFN(obj, name, argc, ...) JS_SetPropertyStr(c, obj, name, JS_NewCFunction(c, +[](JSContext* c, JSValueConst, int argn, JSValueConst* a) -> JSValue { (void)c; (void)a; (void)argn; __VA_ARGS__ }, name, argc))
static JSValue genSet(JSContext* c, int argn, JSValueConst* a, bool onlyEmpty) {
  if (!t_writer) return JS_FALSE; BlockOpts o; std::string why;
  if (argn > 5 && !optsFromJs(c, a[5], o, why)) return JS_ThrowTypeError(c, "chunk.set: %s", why.c_str());
  if (onlyEmpty) o.onlyEmpty = true;
  return JS_NewBool(c, t_writer->set((float)JD(c, a[0]), (float)JD(c, a[1]), (float)JD(c, a[2]), (int)JD(c, a[3]), argn > 4 && !JS_IsUndefined(a[4]) ? (float)JD(c, a[4]) : 0.2f, o));
}
static JSValue bfGrid(JSContext* c, int argn, JSValueConst* a, int kind) {
  if (argn < 8) return JS_ThrowTypeError(c, "noise.bf.fbmGrid(out, nx, nz, x0, z0, step, scale, seed)");
  size_t off = 0, bl = 0, bpe = 0; JSValue buf = JS_GetTypedArrayBuffer(c, a[0], &off, &bl, &bpe); if (JS_IsException(buf)) return JS_ThrowTypeError(c, "out must be a Float64Array or Float32Array");
  size_t sz = 0; uint8_t* base = JS_GetArrayBuffer(c, &sz, buf); JS_FreeValue(c, buf); if (!base || (bpe != 8 && bpe != 4)) return JS_ThrowTypeError(c, "out must be a Float64Array or Float32Array");
  int nx = (int)JD(c, a[1]), nzz = (int)JD(c, a[2]); double x0 = JD(c, a[3]), z0 = JD(c, a[4]), step = JD(c, a[5]), sc = JD(c, a[6]), seed = JD(c, a[7]);
  if (nx <= 0 || nzz <= 0 || (size_t)nx * nzz > bl / bpe) return JS_ThrowRangeError(c, "out is smaller than nx * nz");
  for (int j = 0; j < nzz; j++) for (int i = 0; i < nx; i++) { double X = (x0 + i * step) * sc, Z = (z0 + j * step) * sc, r = kind ? bf::vn(X, Z, seed) : bf::fbm(X, Z, seed);
    if (bpe == 8) ((double*)(base + off))[i + (size_t)nx * j] = r; else ((float*)(base + off))[i + (size_t)nx * j] = (float)r; }
  return JS_UNDEFINED;
}
static bool jsGenSetup(JsGen& G, const std::string& src, const std::string& params, std::string& err) {
  G.rt = JS_NewRuntime(); JS_SetMemoryLimit(G.rt, 256u << 20); JS_SetMaxStackSize(G.rt, 1u << 20);
  JS_SetInterruptHandler(G.rt, +[](JSRuntime*, void*) -> int { return std::chrono::steady_clock::now() > t_deadline ? 1 : 0; }, nullptr);
  G.ctx = JS_NewContext(G.rt); JSContext* c = G.ctx; t_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  G.chunk = JS_NewObject(c); JSValue ch = G.chunk;
  GFN(ch, "column", 5, { if (t_writer) t_writer->column((int)std::floor(JD(c, a[0])), (int)std::floor(JD(c, a[1])), (int)std::floor(JD(c, a[2])), (int)std::floor(JD(c, a[3])), (int)JD(c, a[4])); return JS_UNDEFINED; });
  GFN(ch, "fill", 7, { if (t_writer) t_writer->fill((int)std::floor(JD(c, a[0])), (int)std::floor(JD(c, a[1])), (int)std::floor(JD(c, a[2])), (int)std::floor(JD(c, a[3])), (int)std::floor(JD(c, a[4])), (int)std::floor(JD(c, a[5])), (int)JD(c, a[6])); return JS_UNDEFINED; });
  GFN(ch, "set", 6, { return genSet(c, argn, a, false); });
  GFN(ch, "place", 6, { return genSet(c, argn, a, true); });
  // 20 cm blocks stacked in one 20 cm column: chunk.stack(x, z, yFrom, [ids...]) - one call instead of one per block
  GFN(ch, "stack", 4, { if (!t_writer || argn < 4) return JS_UNDEFINED; float x = (float)JD(c, a[0]), z = (float)JD(c, a[1]), y = (float)JD(c, a[2]);
    JSValue len = JS_GetPropertyStr(c, a[3], "length"); int n = (int)JD(c, len); JS_FreeValue(c, len); BlockOpts o;
    for (int i = 0; i < n; i++) { JSValue e = JS_GetPropertyUint32(c, a[3], (uint32_t)i); int id = (int)JD(c, e); JS_FreeValue(c, e); if (id) t_writer->set(x, y + i / (float)VC, z, id, 1.f / VC, o); }
    return JS_UNDEFINED; });
  // many 20 cm blocks at once from a typed array (no copy): chunk.cells(x, y, z, w, h, d, ids) - ids[i + w * (k + d * j)] is the block at
  // (x + i / 5, y + j / 5, z + k / 5); 0 leaves the cell as it is. Only cells inside this chunk and inside the near layer.
  GFN(ch, "cells", 7, { if (!t_writer || argn < 7) return JS_UNDEFINED; size_t off = 0, bl = 0, bpe = 0; JSValue buf = JS_GetTypedArrayBuffer(c, a[6], &off, &bl, &bpe);
    if (JS_IsException(buf)) return JS_ThrowTypeError(c, "chunk.cells wants a Uint16Array or Uint32Array of block ids");
    size_t sz = 0; uint8_t* base = JS_GetArrayBuffer(c, &sz, buf); JS_FreeValue(c, buf); if (!base || (bpe != 2 && bpe != 4)) return JS_ThrowTypeError(c, "chunk.cells wants a Uint16Array or Uint32Array");
    int w = (int)JD(c, a[3]), h = (int)JD(c, a[4]), d = (int)JD(c, a[5]); if (w <= 0 || h <= 0 || d <= 0 || (size_t)w * h * d > bl / bpe) return JS_ThrowRangeError(c, "chunk.cells: the array is smaller than w x h x d");
    t_writer->cells((float)JD(c, a[0]), (float)JD(c, a[1]), (float)JD(c, a[2]), w, h, d, base + off, (int)bpe); return JS_UNDEFINED; });
  GFN(ch, "get", 3, { return JS_NewInt32(c, t_writer ? t_writer->get((float)JD(c, a[0]), (float)JD(c, a[1]), (float)JD(c, a[2])) : 0); });
  GFN(ch, "tint", 5, { if (!t_writer) return JS_UNDEFINED; glm::vec3 v(1);
    if (argn >= 5) v = {(float)JD(c, a[2]), (float)JD(c, a[3]), (float)JD(c, a[4])};
    else if (argn >= 3) for (int i = 0; i < 3; i++) { JSValue e = JS_GetPropertyUint32(c, a[2], (uint32_t)i); v[i] = (float)JD(c, e); JS_FreeValue(c, e); }
    t_writer->tint((int)std::floor(JD(c, a[0])), (int)std::floor(JD(c, a[1])), v); return JS_UNDEFINED; });
  G.noise = JS_NewObject(c); JSValue nz = G.noise;
  GFN(nz, "simplex2", 2, { return JS_NewFloat64(c, simplex2((float)JD(c, a[0]), (float)JD(c, a[1]), t_seed)); });
  GFN(nz, "simplex3", 3, { return JS_NewFloat64(c, simplex3((float)JD(c, a[0]), (float)JD(c, a[1]), (float)JD(c, a[2]), t_seed)); });
  GFN(nz, "fbm", 5, { return JS_NewFloat64(c, fbm2((float)JD(c, a[0]), (float)JD(c, a[1]), t_seed, argn > 2 ? (int)JD(c, a[2]) : 4, argn > 3 ? (float)JD(c, a[3]) : 2.f, argn > 4 ? (float)JD(c, a[4]) : 0.5f)); });
  GFN(nz, "hash", 3, { return JS_NewFloat64(c, hash3((int)std::floor(JD(c, a[0])), (int)std::floor(JD(c, a[1])), (int)std::floor(JD(c, a[2])), t_seed)); });
  // BLOCKFALL's own noise (the same numbers as the game's JavaScript, much faster)
  JSValue b = JS_NewObject(c);
  GFN(b, "h2", 3, { return JS_NewFloat64(c, bf::h2(JD(c, a[0]), JD(c, a[1]), JD(c, a[2]))); });
  GFN(b, "vn", 3, { return JS_NewFloat64(c, bf::vn(JD(c, a[0]), JD(c, a[1]), JD(c, a[2]))); });
  GFN(b, "fbm", 3, { return JS_NewFloat64(c, bf::fbm(JD(c, a[0]), JD(c, a[1]), JD(c, a[2]))); });
  GFN(b, "h3", 4, { return JS_NewFloat64(c, bf::h3(JD(c, a[0]), JD(c, a[1]), JD(c, a[2]), JD(c, a[3]))); });
  GFN(b, "vn3", 4, { return JS_NewFloat64(c, bf::vn3(JD(c, a[0]), JD(c, a[1]), JD(c, a[2]), JD(c, a[3]))); });
  GFN(b, "hashf", 3, { return JS_NewFloat64(c, bf::hashf(JD(c, a[0]), JD(c, a[1]), JD(c, a[2]))); });
  // a whole grid in one call (much faster than one call per point): out[i + nx * j] = f((x0 + i * step) * scale, (z0 + j * step) * scale, seed)
  GFN(b, "fbmGrid", 8, { return bfGrid(c, argn, a, 0); });
  GFN(b, "vnGrid", 8, { return bfGrid(c, argn, a, 1); });
  JS_SetPropertyStr(c, nz, "bf", b);
  std::string code = "(" + src + ")";
  G.fn = JS_Eval(c, code.c_str(), code.size(), "generator", JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(G.fn)) { err = jsErr(c); return false; }
  if (!JS_IsFunction(c, G.fn)) { err = "setGenerator wants a function"; return false; }
  G.params = JS_ParseJSON(c, params.c_str(), params.size(), "params"); if (JS_IsException(G.params)) { JS_FreeValue(c, JS_GetException(c)); G.params = JS_NewObject(c); }
  return true;
}
#undef GFN

/* ------------------------------------------------------------------ meshing */
// 32 bytes: position, texture coordinates (block units), colour (x 0.5) + corner light, normal, atlas tile, flags (glow 0-15)
struct VVert { float x, y, z, u, v; uint8_t r, g, b, ao; int8_t nx, ny, nz, pad; uint16_t tile; uint8_t flags, pad2; };
static_assert(sizeof(VVert) == 32, "vertex layout");

struct MeshPart { std::vector<VVert> v; std::vector<uint32_t> i; glm::vec3 bmin{1e9f}, bmax{-1e9f}; };
struct MeshOut { MeshPart solid, water; std::vector<glm::vec3> cv; std::vector<uint32_t> ci; };
struct Snapshot { int cx = 0, cz = 0; ChunkData n[3][3]; };

static const glm::ivec3 DIRS[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
// the two axes a face of direction d spans: u, v
static void faceAxes(int d, int& ua, int& va) { int ax = d / 2; if (ax == 0) { ua = 2; va = 1; } else if (ax == 1) { ua = 0; va = 2; } else { ua = 0; va = 1; } }

// unit-block triangles of the shapes (outward winding), BLOCKFALL's shapeTris + stairs and plants
struct UTri { glm::vec3 p[3]; };
static void prism(std::vector<UTri>& T, int axis, const std::vector<glm::vec2>& poly, float a0, float a1) {
  // poly in the plane of the other two axes (u, v) = axis 0: (y, z), axis 1: (x, z), axis 2: (x, y); extruded from a0 to a1
  int ua = axis == 0 ? 1 : 0, va = axis == 2 ? 1 : 2;
  auto P = [&](glm::vec2 q, float a) { glm::vec3 r(0); r[axis] = a; r[ua] = q.x; r[va] = q.y; return r; };
  float area = 0; size_t n = poly.size(); for (size_t i = 0; i < n; i++) { glm::vec2 p = poly[i], q = poly[(i + 1) % n]; area += p.x * q.y - q.x * p.y; }
  glm::vec3 center(0); for (auto& q : poly) center += P(q, (a0 + a1) * 0.5f); center /= (float)n;
  auto add = [&](glm::vec3 A, glm::vec3 B, glm::vec3 C, glm::vec3 out) { glm::vec3 nn = glm::cross(B - A, C - A); if (glm::dot(nn, out) < 0) std::swap(B, C); T.push_back({{A, B, C}}); };
  for (size_t i = 0; i < n; i++) { glm::vec2 p = poly[i], q = poly[(i + 1) % n]; glm::vec2 e = q - p, on = area > 0 ? glm::vec2(e.y, -e.x) : glm::vec2(-e.y, e.x);
    glm::vec3 out(0); out[ua] = on.x; out[va] = on.y;
    add(P(p, a0), P(q, a0), P(q, a1), out); add(P(p, a0), P(q, a1), P(p, a1), out); }
  glm::vec3 up(0); up[axis] = 1;
  for (size_t i = 1; i + 1 < n; i++) { add(P(poly[0], a0), P(poly[i], a0), P(poly[i + 1], a0), -up); add(P(poly[0], a1), P(poly[i], a1), P(poly[i + 1], a1), up); }
}
static std::vector<glm::vec2> ring(int k, float r = 0.5f) { std::vector<glm::vec2> p; for (int i = 0; i < k; i++) { float a = i / (float)k * 6.2831853f + (k == 6 ? 0.f : 3.14159265f / k); p.push_back({0.5f + std::cos(a) * r, 0.5f + std::sin(a) * r}); } return p; }
static const std::vector<UTri>& shapeTris(int sh) {
  static std::vector<UTri> cache[SH_Count]; static std::once_flag once;
  std::call_once(once, [] {
    for (int s = 0; s < SH_Count; s++) { auto& T = cache[s];
      switch (s) {
        case SH_Slope: prism(T, 0, {{0, 0}, {1, 0}, {0, 1}}, 0, 1); break;
        case SH_Half: prism(T, 1, {{0, 0}, {1, 0}, {1, 1}, {0, 1}}, 0, 0.5f); break;
        case SH_Hex: prism(T, 1, ring(6), 0, 1); break;
        case SH_Cylinder: prism(T, 1, ring(14), 0, 1); break;
        case SH_Arch: { std::vector<glm::vec2> p; for (int i = 0; i <= 10; i++) { float a = i / 10.f * 3.14159265f; p.push_back({std::sin(a) * 0.5f, 0.5f + std::cos(a) * 0.5f}); } prism(T, 0, p, 0, 1); break; }
        case SH_Wedge: prism(T, 0, {{0, 0}, {0.5f, 0}, {0, 1}}, 0, 1); break;
        case SH_Roof: prism(T, 0, {{0, 0}, {1, 0.5f}, {0, 1}}, 0, 1); break;
        case SH_Stairs: prism(T, 0, {{0.5f, 0.5f}, {0.5f, 0}, {0, 0}, {0, 1}, {1, 1}, {1, 0.5f}}, 0, 1); break;
        case SH_Pyramid: { glm::vec3 A(0.5f, 1, 0.5f), q[4] = {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}; glm::vec3 cen(0.5f, 0.3f, 0.5f);
          auto add = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) { glm::vec3 n = glm::cross(b - a, c - a); if (glm::dot(n, a - cen) < 0) std::swap(b, c); T.push_back({{a, b, c}}); };
          for (int i = 0; i < 4; i++) add(q[i], q[(i + 1) % 4], A); add(q[0], q[1], q[2]); add(q[0], q[2], q[3]); break; }
        default: prism(T, 1, {{0, 0}, {1, 0}, {1, 1}, {0, 1}}, 0, 1); break;   // cube (turned / carved blocks use it)
      } }
  });
  return cache[sh >= 0 && sh < SH_Count ? sh : 0];
}
// BLOCKFALL's turn: r & 3 quarter turns around Y, r >> 2: 1 upside down, 2 on its side
static glm::vec3 turnUnit(glm::vec3 p, int r) {
  float X = p.x - 0.5f, Y = p.y - 0.5f, Z = p.z - 0.5f; int tilt = r >> 2;
  if (tilt == 1) { Y = -Y; X = -X; } else if (tilt == 2) { float t = Y; Y = -Z; Z = t; }
  for (int i = 0; i < (r & 3); i++) { float t = X; X = Z; Z = -t; }
  return {X + 0.5f, Y + 0.5f, Z + 0.5f};
}

struct Mesher {
  const Geo& g; const BlockTable& T; const Snapshot& S; MeshOut out;
  // dense copies with a one-cell border: the near layer (fine) and the 1 m layer (coarse)
  int FX = CHUNK_C + 2, FY, FZ = CHUNK_C + 2, fy0; std::vector<Cell> F;
  int CX = CHUNK_M + 2, CY, CZ = CHUNK_M + 2, cy0; std::vector<uint16_t> C;
  std::vector<uint32_t> tints;   // tint palette of this mesh (index 0 = no tint)
  Mesher(const Geo& g, const BlockTable& T, const Snapshot& S) : g(g), T(T), S(S) { tints.push_back(0x404040u); }

  Cell snapCell(int lx, int fy, int lz) const {   // lx, lz may be -80..159
    int ix = lx < 0 ? 0 : lx >= CHUNK_C ? 2 : 1, iz = lz < 0 ? 0 : lz >= CHUNK_C ? 2 : 1;
    const ChunkData& d = S.n[ix][iz]; if (!d.coarse) return 0;
    return readCell(d, g, lx - (ix - 1) * CHUNK_C, fy, lz - (iz - 1) * CHUNK_C);
  }
  uint16_t snapCoarse(int x, int my, int z) const {
    if (my < g.minY || my >= g.maxY) return 0;
    int ix = x < 0 ? 0 : x >= CHUNK_M ? 2 : 1, iz = z < 0 ? 0 : z >= CHUNK_M ? 2 : 1; const ChunkData& d = S.n[ix][iz]; if (!d.coarse) return 0;
    return (*d.coarse)[g.coarseIdx(x - (ix - 1) * CHUNK_M, my, z - (iz - 1) * CHUNK_M)];
  }
  // biome colour of the metre column (x, z), x / z may be -16..31
  uint32_t tintRaw(int mx, int mz) const {
    int ix = mx < 0 ? 0 : mx >= CHUNK_M ? 2 : 1, iz = mz < 0 ? 0 : mz >= CHUNK_M ? 2 : 1; const ChunkData& d = S.n[ix][iz]; if (!d.tint) return 0x404040u;
    return (*d.tint)[(mz - (iz - 1) * CHUNK_M) * CHUNK_M + (mx - (ix - 1) * CHUNK_M)];
  }
  int tintIndex(uint32_t raw) { if (raw == 0x404040u) return 0; for (size_t i = 1; i < tints.size(); i++) if (tints[i] == raw) return (int)i; if (tints.size() >= 255) return 0; tints.push_back(raw); return (int)tints.size() - 1; }
  static glm::vec3 tintColor(uint32_t raw) { return glm::vec3((float)(raw & 255), (float)((raw >> 8) & 255), (float)((raw >> 16) & 255)) / 64.f; }
  // every block its own shade: 16 steps
  int shadeOf(int id, int gx, int gy, int gz) const { const BlockType& bt = typeOf(T, id); if (bt.vary <= 0) return 8; return (int)(bf::hashf(gx, gy * 7 + 3, gz) * 15.999); }
  float shadeMul(int id, int shade) const { const BlockType& bt = typeOf(T, id); if (bt.vary <= 0) return 1.f; return 1.f + (shade / 15.f - 0.5f) * 2.f * bt.vary; }

  // which layers can have faces at all: something in it, and not buried (it and the layers above and below all opaque)
  std::vector<uint8_t> fActive, cActive;
  void build() {
    fy0 = g.bandLo - 1; FY = g.bandH + 2; F.assign((size_t)FX * FY * FZ, 0);
    // the near layer brick by brick (8 cells at a time); the one-cell border from the neighbours
    for (int y = 0; y < FY; y++) { int fy = y + fy0;
      if (!g.inBand(fy)) { for (int z = 0; z < FZ; z++) for (int x = 0; x < FX; x++) F[((size_t)y * FZ + z) * FX + x] = snapCell(x - 1, fy, z - 1); continue; }
      int by = fy - g.bandLo;
      for (int z = 0; z < FZ; z++) { int lz = z - 1; int iz = lz < 0 ? 0 : lz >= CHUNK_C ? 2 : 1; int llz = lz - (iz - 1) * CHUNK_C; Cell* row = &F[((size_t)y * FZ + z) * FX];
        row[0] = snapCell(-1, fy, lz); row[FX - 1] = snapCell(CHUNK_C, fy, lz);
        const ChunkData& d = S.n[1][iz]; if (!d.coarse) continue;
        for (int bx = 0; bx < BRICKS_XZ; bx++) { int bi = g.brickIdx(bx * BRICK, by, llz); const auto& b = d.bricks[bi]; Cell* dst = row + 1 + bx * BRICK;
          if (b) std::memcpy(dst, &b->c[Geo::inBrick(0, by, llz)], BRICK * sizeof(Cell));
          else if (int id = (*d.fill)[bi]) for (int k = 0; k < BRICK; k++) dst[k] = makeCell(id, VC, (bx * BRICK + k) % VC, posMod(fy, VC), llz % VC); } } }
    cy0 = g.minY - 1; CY = g.H + 2; C.assign((size_t)CX * CY * CZ, 0);
    for (int iz = 0; iz < 3; iz++) for (int ix = 0; ix < 3; ix++) { const ChunkData& d = S.n[ix][iz]; if (!d.coarse) continue;
      for (int z = 0; z < CZ; z++) { int mz = z - 1; if ((mz < 0 ? 0 : mz >= CHUNK_M ? 2 : 1) != iz) continue;
        for (int x = 0; x < CX; x++) { int mx = x - 1; if ((mx < 0 ? 0 : mx >= CHUNK_M ? 2 : 1) != ix) continue;
          int sx = mx - (ix - 1) * CHUNK_M, sz = mz - (iz - 1) * CHUNK_M;
          for (int y = 1; y < CY - 1; y++) { int my = y + cy0; if (!g.metreInBand(my)) C[((size_t)y * CZ + z) * CX + x] = (*d.coarse)[g.coarseIdx(sx, my, sz)]; } } } }
    // layer flags
    std::vector<uint8_t> occ(FY), full(FY);
    for (int y = 0; y < FY; y++) { bool o = false, f = true; const Cell* L = &F[(size_t)y * FZ * FX];
      for (int i = 0; i < FX * FZ && (f || !o); i++) { Cell c = L[i]; int x = i % FX, z = i / FX; bool inner = x >= 1 && x <= CHUNK_C && z >= 1 && z <= CHUNK_C;
        if (c && inner) o = true; if (!opaqueCell(T, c)) f = false; }
      occ[y] = o; full[y] = f; }
    fActive.assign(FY, 0); for (int y = 0; y < FY; y++) fActive[y] = occ[y] && !(full[y] && y > 0 && full[y - 1] && y + 1 < FY && full[y + 1]);
    std::vector<uint8_t> cocc(CY), cfull(CY);
    for (int y = 0; y < CY; y++) { bool o = false, f = true; for (int z = 0; z < CZ; z++) for (int x = 0; x < CX; x++) { uint16_t id = C[((size_t)y * CZ + z) * CX + x]; bool inner = x >= 1 && x <= CHUNK_M && z >= 1 && z <= CHUNK_M;
        if (id && inner) o = true; if (!opaqueId(id)) f = false; }
      int my = y + cy0; if (g.metreInBand(my)) { o = false; f = false; } cocc[y] = o; cfull[y] = f; }
    cActive.assign(CY, 0); for (int y = 0; y < CY; y++) cActive[y] = cocc[y] && !(cfull[y] && y > 0 && cfull[y - 1] && y + 1 < CY && cfull[y + 1]);
  }
  bool fineLayerActive(int fy) const { int y = fy - fy0; return y >= 0 && y < FY && fActive[y]; }
  bool coarseLayerActive(int my) const { int y = my - cy0; return y >= 0 && y < CY && cActive[y]; }
  Cell fcell(int lx, int fy, int lz) const { int y = fy - fy0; if (y < 0 || y >= FY || lx < -1 || lx > CHUNK_C || lz < -1 || lz > CHUNK_C) return 0; return F[((size_t)y * FZ + (lz + 1)) * FX + (lx + 1)]; }
  bool opaqueId(int id) const { return id && (id >= (int)T.opaque.size() || T.opaque[id]); }
  // the block id of the metre cell (x, my, z) (band metres: the fine cell in its middle) and whether it is opaque
  Cell cellM(int x, int my, int z) const {
    if (my < g.minY || my >= g.maxY) return 0;
    if (g.metreInBand(my)) { int lx = glm::clamp(x * VC + 2, -1, CHUNK_C), lz = glm::clamp(z * VC + 2, -1, CHUNK_C); return fcell(lx, my * VC + 2, lz); }
    if (x < -1 || x > CHUNK_M || z < -1 || z > CHUNK_M) return 0; int id = C[((size_t)(my - cy0) * CZ + (z + 1)) * CX + (x + 1)]; return id ? makeCell(id, VC, 0, 0, 0) : 0;
  }
  bool opaqueM(int x, int my, int z) const { if (my < g.minY) return true; return opaqueCell(T, cellM(x, my, z)); }
  bool opaqueF(int lx, int fy, int lz) const {   // fine coords, may step into the 1 m layer
    if (g.inBand(fy) || fy == g.bandLo - 1 || fy == g.bandHi) return opaqueCell(T, fcell(lx, fy, lz));
    return opaqueM(floorDiv(lx, VC), floorDiv(fy, VC), floorDiv(lz, VC));
  }
  // is the face from cell p towards neighbour q hidden?
  bool hidden(Cell p, Cell q) const {
    if (!q) return false; if (opaqueCell(T, q)) return true;
    // water next to water, see-through colour blocks next to their own kind; cut-out blocks (leaves) keep the faces between them, like the game
    return cellId(q) == cellId(p) && !special(T, q) && !opaqueId(cellId(p)) && !typeOf(T, cellId(p)).cutout;
  }
  // ambient occlusion of the 4 corners of a face (0 = darkest .. 3 = open)
  template <class Op> uint8_t aoPack(glm::ivec3 p, int d, Op op) const {
    int ua, va; faceAxes(d, ua, va); glm::ivec3 n = DIRS[d], q = p + n; uint8_t pack = 0; int k = 0;
    for (int sv = -1; sv <= 1; sv += 2) for (int su = -1; su <= 1; su += 2) {
      glm::ivec3 du(0), dv(0); du[ua] = su; dv[va] = sv;
      bool s1 = op(q + du), s2 = op(q + dv), cc = op(q + du + dv);
      int ao = (s1 && s2) ? 0 : 3 - (int)s1 - (int)s2 - (int)cc; pack |= (uint8_t)(ao << (k * 2)); k++; }
    return pack;   // corners: (-u,-v), (+u,-v), (-u,+v), (+u,+v)
  }
  static void putColor(VVert& v, glm::vec3 col) { col = glm::clamp(col * 0.5f, 0.f, 1.f); v.r = (uint8_t)std::lround(col.r * 255); v.g = (uint8_t)std::lround(col.g * 255); v.b = (uint8_t)std::lround(col.b * 255); }
  // vertex flags: glow 0-15, bit 4 = cutout (see-through texture pixels are holes)
  uint8_t glowFlags(int id) const { const BlockType& bt = typeOf(T, id); return (uint8_t)(std::lround(glm::clamp(bt.glow / 2.f, 0.f, 1.f) * 15) | (bt.cutout && !bt.liquid ? 16 : 0)); }
  void quad(int d, glm::vec3 base, float du, float dv, int ua, int va, int id, uint8_t aoP, float u0, float v0, float uScale, float vScale, glm::vec3 mul) {
    const BlockType& bt = typeOf(T, id); bool liq = isLiquid(T, id); MeshPart& M = liq ? out.water : out.solid;
    glm::ivec3 n = DIRS[d]; int face = d == 2 ? 0 : d == 3 ? 2 : 1; glm::vec3 col = bt.color[face] * mul; int tile = bt.tile[face];
    glm::vec3 eu(0), ev(0); eu[ua] = du; ev[va] = dv;
    glm::vec3 P[4] = {base, base + eu, base + ev, base + eu + ev}; float U[4] = {u0, u0 + du * uScale, u0, u0 + du * uScale}, V[4] = {v0, v0, v0 + dv * vScale, v0 + dv * vScale};
    uint8_t ao[4] = {(uint8_t)(aoP & 3), (uint8_t)((aoP >> 2) & 3), (uint8_t)((aoP >> 4) & 3), (uint8_t)((aoP >> 6) & 3)};
    uint32_t b = (uint32_t)M.v.size(); uint8_t fl = glowFlags(id);
    for (int k = 0; k < 4; k++) { VVert v{}; v.x = P[k].x; v.y = P[k].y; v.z = P[k].z; v.u = U[k]; v.v = V[k]; putColor(v, col);
      v.ao = liq ? (uint8_t)std::lround(glm::clamp(bt.opacity, 0.05f, 1.f) * 255) : (uint8_t)(ao[k] * 85); v.nx = (int8_t)(n.x * 127); v.ny = (int8_t)(n.y * 127); v.nz = (int8_t)(n.z * 127); v.tile = tile < 0 ? 0xffff : (uint16_t)tile; v.flags = fl;
      M.v.push_back(v); M.bmin = glm::min(M.bmin, P[k]); M.bmax = glm::max(M.bmax, P[k]); }
    glm::vec3 cr = glm::cross(eu, ev); bool flipWind = glm::dot(cr, glm::vec3(n)) < 0;
    bool flipDiag = ao[0] + ao[3] < ao[1] + ao[2];
    uint32_t i0, i1, i2, i3, i4, i5;
    if (!flipDiag) { i0 = 0; i1 = 1; i2 = 3; i3 = 0; i4 = 3; i5 = 2; } else { i0 = 0; i1 = 1; i2 = 2; i3 = 1; i4 = 3; i5 = 2; }
    uint32_t tri[6] = {i0, i1, i2, i3, i4, i5}; if (flipWind) { std::swap(tri[1], tri[2]); std::swap(tri[4], tri[5]); }
    for (uint32_t t : tri) M.i.push_back(b + t);
    if (id < (int)T.solid.size() ? T.solid[id] : true) {
      uint32_t cb = (uint32_t)out.cv.size(); for (auto& p : P) out.cv.push_back(p); for (uint32_t t : tri) out.ci.push_back(cb + t);
    }
  }
  // greedy: merge equal keys of a U x V mask into rectangles
  template <class Emit> static void greedy(std::vector<uint64_t>& m, int U, int V, Emit emit) {
    for (int v = 0; v < V; v++) for (int u = 0; u < U;) {
      uint64_t k = m[(size_t)v * U + u]; if (!k) { u++; continue; }
      int w = 1; while (u + w < U && m[(size_t)v * U + u + w] == k) w++;
      int h = 1; for (; v + h < V; h++) { bool ok = true; for (int i = 0; i < w; i++) if (m[(size_t)(v + h) * U + u + i] != k) { ok = false; break; } if (!ok) break; }
      emit(u, v, w, h, k); for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) m[(size_t)(v + j) * U + u + i] = 0; u += w;
    }
  }
  glm::vec3 colourMul(int id, int face, int shade, int tintIdx) const {
    glm::vec3 m(shadeMul(id, shade)); if (tintIdx && (typeOf(T, id).biome >> face & 1)) m *= tintColor(tints[tintIdx]); return m; }
  void coarsePass() {
    // only the metres that have blocks: skip air above the mountains and solid rock rows quickly
    for (int d = 0; d < 6; d++) {
      int ax = d / 2, ua, va; faceAxes(d, ua, va); glm::ivec3 n = DIRS[d];
      int dimA = ax == 1 ? g.H : CHUNK_M, dimU = ua == 1 ? g.H : CHUNK_M, dimV = va == 1 ? g.H : CHUNK_M;
      std::vector<uint64_t> mask((size_t)dimU * dimV);
      for (int s = 0; s < dimA; s++) {
        std::fill(mask.begin(), mask.end(), 0); bool any = false;
        if (ax == 1 && !coarseLayerActive(s + g.minY)) continue;
        for (int v = 0; v < dimV; v++) { if (va == 1 && !coarseLayerActive(v + g.minY)) continue; for (int u = 0; u < dimU; u++) {
          glm::ivec3 p(0); p[ax] = s; p[ua] = u; p[va] = v; p.y += g.minY;
          if (g.metreInBand(p.y)) continue; uint16_t id = C[((size_t)(p.y - cy0) * CZ + (p.z + 1)) * CX + (p.x + 1)]; if (!id) continue;
          glm::ivec3 q = p + n; if (g.metreInBand(q.y)) continue;   // that face is made by the near layer pass
          if (q.y < g.minY) continue;
          Cell pc = makeCell(id, VC, 0, 0, 0), qc = cellM(q.x, q.y, q.z); if (hidden(pc, qc)) continue;
          bool liq = isLiquid(T, id);
          uint8_t ao = liq ? 0xff : aoPack(p, d, [&](glm::ivec3 c) { return opaqueM(c.x, c.y, c.z); });
          int shade = shadeOf(id, p.x + S.cx * CHUNK_M, p.y, p.z + S.cz * CHUNK_M), ti = typeOf(T, id).biome ? tintIndex(tintRaw(p.x, p.z)) : 0;
          mask[(size_t)v * dimU + u] = (uint64_t)id | ((uint64_t)ao << 16) | ((uint64_t)shade << 24) | ((uint64_t)ti << 28) | (1ull << 63); any = true;
        } }
        if (!any) continue;
        greedy(mask, dimU, dimV, [&](int u, int v, int w, int h, uint64_t k) {
          int id = (int)(k & 0xffff); uint8_t ao = (uint8_t)((k >> 16) & 255); int shade = (int)((k >> 24) & 15), ti = (int)((k >> 28) & 255);
          glm::vec3 base(0); base[ax] = (float)(s + (n[ax] > 0 ? 1 : 0)); base[ua] = (float)u; base[va] = (float)v; base.y += (float)g.minY;   // y is always one of the three
          int face = d == 2 ? 0 : d == 3 ? 2 : 1;
          quad(d, base, (float)w, (float)h, ua, va, id, ao, (float)u, (float)v + (va == 1 ? g.minY : 0), 1.f, 1.f, colourMul(id, face, shade, ti));
        });
      }
    }
  }
  void finePass() {
    for (int d = 0; d < 6; d++) {
      int ax = d / 2, ua, va; faceAxes(d, ua, va); glm::ivec3 n = DIRS[d];
      // slices along the axis; for y also the 1 m layers just outside the band (their faces towards the band)
      int sLo = ax == 1 ? g.bandLo - 1 : 0, sHi = ax == 1 ? g.bandHi + 1 : CHUNK_C;
      int dimU = ua == 1 ? g.bandH : CHUNK_C, dimV = va == 1 ? g.bandH : CHUNK_C, offU = ua == 1 ? g.bandLo : 0, offV = va == 1 ? g.bandLo : 0;
      std::vector<uint64_t> mask((size_t)dimU * dimV);
      for (int s = sLo; s < sHi; s++) {
        if (ax == 1 && s == g.bandLo - 1 && d != 2) continue; if (ax == 1 && s == g.bandHi && d != 3) continue;
        std::fill(mask.begin(), mask.end(), 0); bool any = false;
        if (ax == 1 && !fineLayerActive(s)) continue;
        for (int v = 0; v < dimV; v++) { if (va == 1 && !fineLayerActive(v + offV)) continue; for (int u = 0; u < dimU; u++) {
          glm::ivec3 p(0); p[ax] = s; p[ua] = u + offU; p[va] = v + offV;
          Cell c = fcell(p.x, p.y, p.z); if (!c || special(T, c)) continue;
          glm::ivec3 q = p + n; Cell qc = (g.inBand(q.y) || q.y == g.bandLo - 1 || q.y == g.bandHi) ? fcell(q.x, q.y, q.z) : cellM(floorDiv(q.x, VC), floorDiv(q.y, VC), floorDiv(q.z, VC));
          if (hidden(c, qc)) continue;
          int id = cellId(c), sz = cellSize(c), szy = cellSizeY(c); glm::ivec3 off = cellOffset(c); bool liq = isLiquid(T, id);
          // the block's grid phase along u and v (in world cells) so merged faces keep one texture per block
          int gx = p.x + S.cx * CHUNK_C, gz = p.z + S.cz * CHUNK_C; glm::ivec3 gp(gx, p.y, gz), org = gp - off;
          int su = ua == 1 ? szy : sz, sv = va == 1 ? szy : sz;
          int phU = posMod(org[ua], su), phV = posMod(org[va], sv);
          uint8_t ao = liq ? 0xff : aoPack(p, d, [&](glm::ivec3 x) { return opaqueF(x.x, x.y, x.z); });
          int shade = shadeOf(id, org.x, org.y, org.z), ti = typeOf(T, id).biome ? tintIndex(tintRaw(floorDiv(p.x - off.x, VC), floorDiv(p.z - off.z, VC))) : 0;
          mask[(size_t)v * dimU + u] = (uint64_t)id | ((uint64_t)(sz - 1) << 16) | ((uint64_t)phU << 19) | ((uint64_t)phV << 22) | ((uint64_t)ao << 25) | ((uint64_t)(szy - 1) << 33) |
                                       ((uint64_t)shade << 36) | ((uint64_t)ti << 40) | (1ull << 63); any = true;
        } }
        if (!any) continue;
        greedy(mask, dimU, dimV, [&](int u, int v, int w, int h, uint64_t k) {
          int id = (int)(k & 0xffff), sz = (int)((k >> 16) & 7) + 1, phU = (int)((k >> 19) & 7), phV = (int)((k >> 22) & 7); uint8_t ao = (uint8_t)((k >> 25) & 255);
          int szy = (int)((k >> 33) & 7) + 1, shade = (int)((k >> 36) & 15), ti = (int)((k >> 40) & 255);
          glm::ivec3 cp(0); cp[ax] = s + (n[ax] > 0 ? 1 : 0); cp[ua] = u + offU; cp[va] = v + offV;
          glm::vec3 base = glm::vec3(cp) / (float)VC;
          // texture coordinates in block units: whole numbers on block edges
          glm::ivec3 gc(cp.x + S.cx * CHUNK_C, cp.y, cp.z + S.cz * CHUNK_C);
          int su = ua == 1 ? szy : sz, sv = va == 1 ? szy : sz;
          float u0 = (float)(posMod(gc[ua] - phU, su * 4096)) / su, v0 = (float)(posMod(gc[va] - phV, sv * 4096)) / sv;
          int face = d == 2 ? 0 : d == 3 ? 2 : 1;
          quad(d, base, (float)w / VC, (float)h / VC, ua, va, id, ao, u0, v0, (float)VC / su, (float)VC / sv, colourMul(id, face, shade, ti));
        });
      }
    }
  }
  // shaped, model, carved and turned blocks: one by one, as triangles
  void tri(MeshPart& M, glm::vec3 A, glm::vec3 B, glm::vec3 Cc, glm::vec3 org, glm::vec3 size, const BlockType& bt, int id, glm::vec3 mul, const ModelPart* part, bool collide, bool twoSided) {
    glm::vec3 nn = glm::cross(B - A, Cc - A); float L = glm::length(nn); if (L < 1e-9f) return; nn /= L;
    int face = nn.y > 0.6f ? 0 : nn.y < -0.6f ? 2 : 1;
    glm::vec3 col = (part && part->hasColor ? part->color : bt.color[face]) * mul; int tile = part && part->tile >= 0 ? part->tile : bt.tile[face];
    glm::vec3 an = glm::abs(nn); int ua, va; if (an.y >= an.x && an.y >= an.z) { ua = 0; va = 2; } else if (an.x >= an.z) { ua = 2; va = 1; } else { ua = 0; va = 1; }
    uint32_t b = (uint32_t)M.v.size(); uint8_t fl = glowFlags(id);
    for (glm::vec3 P : {A, B, Cc}) { VVert v{}; v.x = P.x; v.y = P.y; v.z = P.z; glm::vec3 l = (P - org) / size; v.u = l[ua]; v.v = l[va]; putColor(v, col); v.ao = 255;
      v.nx = (int8_t)std::lround(nn.x * 127); v.ny = (int8_t)std::lround(nn.y * 127); v.nz = (int8_t)std::lround(nn.z * 127); v.tile = tile < 0 ? 0xffff : (uint16_t)tile; v.flags = fl;
      M.v.push_back(v); M.bmin = glm::min(M.bmin, P); M.bmax = glm::max(M.bmax, P); }
    M.i.push_back(b); M.i.push_back(b + 1); M.i.push_back(b + 2);
    if (twoSided) { M.i.push_back(b); M.i.push_back(b + 2); M.i.push_back(b + 1); }
    if (collide) { uint32_t cb = (uint32_t)out.cv.size(); out.cv.push_back(A); out.cv.push_back(B); out.cv.push_back(Cc); out.ci.push_back(cb); out.ci.push_back(cb + 1); out.ci.push_back(cb + 2); }
  }
  void specialPass() {
    const ChunkData& me = S.n[1][1];
    for (int fy = g.bandLo; fy < g.bandHi; fy++) { if (!fineLayerActive(fy)) continue; for (int lz = 0; lz < CHUNK_C; lz++) for (int lx = 0; lx < CHUNK_C; lx++) {
      Cell c = fcell(lx, fy, lz); if (!c || !special(T, c) || cellOffset(c) != glm::ivec3(0)) continue;
      int id = cellId(c), s = cellSize(c), sy = cellSizeY(c); const BlockType& bt = typeOf(T, id);
      BlockExtra ex; if (cellExt(c)) if (const BlockExtra* e = extOf(me, g, lx, fy, lz)) ex = *e;
      int sh = ex.shape ? ex.shape - 1 : bt.shape;
      glm::vec3 org = glm::vec3(lx, fy, lz) / (float)VC, size = glm::vec3((float)s, (float)sy, (float)s) / (float)VC;
      bool collide = id < (int)T.solid.size() ? T.solid[id] != 0 : true; bool liq = isLiquid(T, id); MeshPart& M = liq ? out.water : out.solid;
      int shade = shadeOf(id, lx + S.cx * CHUNK_C, fy, lz + S.cz * CHUNK_C);
      int ti = bt.biome ? tintIndex(tintRaw(floorDiv(lx, VC), floorDiv(lz, VC))) : 0;
      // faces on the block's outside that touch opaque blocks are left out
      int hiddenSide[6]; for (int d = 0; d < 6; d++) {
        int ax = d / 2, ua, va; faceAxes(d, ua, va); glm::ivec3 o(lx, fy, lz), n = DIRS[d]; glm::ivec3 sz3(s, sy, s); bool all = true;
        for (int i = 0; i < sz3[ua] && all; i++) for (int j = 0; j < sz3[va] && all; j++) { glm::ivec3 q = o; q[ax] += n[ax] > 0 ? sz3[ax] : -1; q[ua] += i; q[va] += j; if (!opaqueF(q.x, q.y, q.z)) all = false; }
        hiddenSide[d] = all; }
      auto place = [&](glm::vec3 u) { return org + turnUnit(u, ex.rot) * size; };
      auto emit = [&](const glm::vec3* p, const ModelPart* part, bool twoSided) {
        glm::vec3 A = place(p[0]), B = place(p[1]), Cq = place(p[2]);
        glm::vec3 nn = glm::cross(B - A, Cq - A); float L = glm::length(nn); if (L < 1e-9f) return; nn /= L;
        for (int d = 0; d < 6; d++) { if (!hiddenSide[d]) continue; int ax = d / 2; glm::vec3 dn = glm::vec3(DIRS[d]); if (glm::dot(nn, dn) < 0.999f) continue;
          float plane = DIRS[d][ax] > 0 ? org[ax] + size[ax] : org[ax]; if (std::abs(A[ax] - plane) < 1e-4f && std::abs(B[ax] - plane) < 1e-4f && std::abs(Cq[ax] - plane) < 1e-4f) return; }
        int face = nn.y > 0.6f ? 0 : nn.y < -0.6f ? 2 : 1;
        tri(M, A, B, Cq, org, size, bt, id, colourMul(id, face, shade, ti), part, collide && !twoSided, twoSided); };
      if (!bt.model.empty()) {   // a model: boxes and cylinders
        for (const ModelPart& mp : bt.model) {
          std::vector<UTri> tris;
          if (mp.kind == 0) { std::vector<UTri> u = shapeTris(SH_Cube); glm::vec3 a = glm::min(mp.a, mp.b), b = glm::max(mp.a, mp.b);
            for (auto t : u) { for (auto& p : t.p) p = a + p * (b - a); tris.push_back(t); } }
          else { int ax = glm::clamp(mp.axis, 0, 2); std::vector<glm::vec2> poly; int k = glm::clamp(mp.sides, 3, 48); float r = mp.b.x, len = mp.b.y;
            int ua = ax == 0 ? 1 : 0, va = ax == 2 ? 1 : 2;
            for (int i = 0; i < k; i++) { float an = i / (float)k * 6.2831853f; poly.push_back({mp.a[ua] + std::cos(an) * r, mp.a[va] + std::sin(an) * r}); }
            prism(tris, ax, poly, mp.a[ax], mp.a[ax] + len); }
          for (auto& t : tris) emit(t.p, &mp, false); }
        continue; }
      if (ex.carved) {   // a carved block: the solid 1/5 pieces, inside faces left out
        for (int k = 0; k < 5; k++) for (int j = 0; j < 5; j++) for (int i = 0; i < 5; i++) { if (!ex.sub(i, j, k)) continue;
          for (int d = 0; d < 6; d++) { glm::ivec3 q = glm::ivec3(i, j, k) + DIRS[d]; if (q.x >= 0 && q.x < 5 && q.y >= 0 && q.y < 5 && q.z >= 0 && q.z < 5 && ex.sub(q.x, q.y, q.z)) continue;
            int ax = d / 2, ua, va; faceAxes(d, ua, va); glm::vec3 base = glm::vec3(i, j, k) / 5.f; if (DIRS[d][ax] > 0) base[ax] += 0.2f;
            glm::vec3 eu(0), ev(0); eu[ua] = 0.2f; ev[va] = 0.2f; glm::vec3 P[4] = {base, base + eu, base + ev, base + eu + ev};
            bool flip = glm::dot(glm::cross(eu, ev), glm::vec3(DIRS[d])) < 0;
            glm::vec3 t1[3] = {P[0], flip ? P[3] : P[1], flip ? P[1] : P[3]}, t2[3] = {P[0], flip ? P[2] : P[3], flip ? P[3] : P[2]};
            emit(t1, nullptr, false); emit(t2, nullptr, false); } }
        continue; }
      if (sh == SH_Cross) {   // plants: two crossed see-through planes
        float n = 0.08f; glm::vec3 q[2][4] = {{{n, 0, n}, {1 - n, 0, 1 - n}, {1 - n, 1, 1 - n}, {n, 1, n}}, {{n, 0, 1 - n}, {1 - n, 0, n}, {1 - n, 1, n}, {n, 1, 1 - n}}};
        for (auto& Q : q) { glm::vec3 t1[3] = {Q[0], Q[1], Q[2]}, t2[3] = {Q[0], Q[2], Q[3]}; emit(t1, nullptr, true); emit(t2, nullptr, true); }
        continue; }
      for (const UTri& t : shapeTris(sh)) emit(t.p, nullptr, false);
    } }
  }
};
static MeshOut meshChunk(const Geo& g, const BlockTable& T, const Snapshot& S) {
  Mesher m(g, T, S); m.build(); m.coarsePass(); m.finePass(); m.specialPass();
  return std::move(m.out);
}

/* ------------------------------------------------------------------ the world */
struct GpuPart { unsigned vao = 0, vbo = 0, ebo = 0; int count = 0; glm::vec3 bmin{0}, bmax{0}; };
struct Chunk {
  int cx = 0, cz = 0; ChunkData data; bool generated = false, genQueued = false, edited = false, meshing = false;
  int version = 0, meshedVersion = -1;
  GpuPart solid, water; uint32_t body = NO_BODY; bool hasMesh = false;
  std::unordered_set<int64_t> spillsDone;   // chunks whose generator writes are already in this chunk
};
struct Job { int prio; std::function<void()> fn; };

struct VoxelWorld::Impl {
  VoxelConfig cfg; Geo g; Physics* phys = nullptr;
  std::unordered_map<int64_t, std::unique_ptr<Chunk>> chunks;
  BlockTablePtr table; GeneratorFn gen; int genVersion = 0; std::string jsSrc, jsParams; bool jsGen = false;
  // generator writes into neighbours: target chunk -> source chunk -> blocks (kept, so a chunk made again gets them again)
  std::unordered_map<int64_t, std::unordered_map<int64_t, std::vector<SpillBlock>>> spills;
  // threads
  std::vector<std::thread> workers; std::mutex jm; std::condition_variable jcv; std::deque<Job> jobs; bool quit = false; std::atomic<int> busy{0};
  std::mutex rm; std::vector<std::function<void()>> results; std::vector<std::string> logs;
  glm::vec3 focus{0}; int inflightGen = 0, inflightMesh = 0; double genMs = 0, meshMs = 0; int genCount = 0, meshCount = 0;
  unsigned atlas = 0; int atlasTiles = 0, atlasW = 0, atlasH = 0;
  GpuPart items;   // voxels.items(): small blocks drawn with the world
  explicit Impl(const VoxelConfig& c) : cfg(c), g(c) {}
  Chunk* get(int cx, int cz) { auto it = chunks.find(ckey(cx, cz)); return it == chunks.end() ? nullptr : it->second.get(); }
  const Chunk* get(int cx, int cz) const { auto it = chunks.find(ckey(cx, cz)); return it == chunks.end() ? nullptr : it->second.get(); }
  void post(int prio, std::function<void()> fn) { { std::lock_guard<std::mutex> l(jm); if (prio > 0) jobs.push_front({prio, std::move(fn)}); else jobs.push_back({prio, std::move(fn)}); } jcv.notify_one(); }
  void result(std::function<void()> fn) { std::lock_guard<std::mutex> l(rm); results.push_back(std::move(fn)); }
  void log(const std::string& s) { std::lock_guard<std::mutex> l(rm); logs.push_back(s); }
  static void freePart(GpuPart& p) { if (p.vao && glDeleteVertexArrays) { glDeleteBuffers(1, &p.vbo); glDeleteBuffers(1, &p.ebo); glDeleteVertexArrays(1, &p.vao); } p.vao = p.vbo = p.ebo = 0; p.count = 0; }
  void freeGpu(Chunk& c) { freePart(c.solid); freePart(c.water); }
  void freeBody(Chunk& c) { if (c.body != NO_BODY && phys) phys->remove(c.body); c.body = NO_BODY; }
};

static thread_local std::unique_ptr<JsGen> t_js;

VoxelWorld::VoxelWorld(const VoxelConfig& c0, Physics* phys) : cfg_(c0) {
  VoxelConfig& c = cfg_;
  c.minY = std::max(-512, std::min(c.minY, 0)); c.maxY = std::max(c.minY + 8, std::min(c.maxY, 1024));
  c.fineLo = std::max(c.minY, std::min(c.fineLo, c.maxY)); c.fineHi = std::max(c.fineLo, std::min(c.fineHi, c.maxY));
  c.viewDistance = std::max(1, std::min(c.viewDistance, 32));
  d.reset(new Impl(c)); d->phys = phys;
  auto t = std::make_shared<BlockTable>(); t->t.resize(256); t->opaque.assign(256, 1); t->solid.assign(256, 1); t->liquid.assign(256, 0); t->cube.assign(256, 1); d->table = t;
  int n = c.threads > 0 ? c.threads : std::max(1, std::min(6, (int)std::thread::hardware_concurrency() - 1));
  for (int i = 0; i < n; i++) d->workers.emplace_back([this] {
    Impl* D = d.get();
    for (;;) { Job j; { std::unique_lock<std::mutex> l(D->jm); D->jcv.wait(l, [&] { return D->quit || !D->jobs.empty(); }); if (D->quit) return; j = std::move(D->jobs.front()); D->jobs.pop_front(); D->busy++; }
      j.fn(); D->busy--; }
    });
  setBuiltinGenerator("hills", "{}");
}
VoxelWorld::~VoxelWorld() {
  { std::lock_guard<std::mutex> l(d->jm); d->quit = true; d->jobs.clear(); } d->jcv.notify_all();
  for (auto& t : d->workers) t.join();
  for (auto& [k, c] : d->chunks) { d->freeGpu(*c); d->freeBody(*c); }
  if (d->atlas && glDeleteTextures) glDeleteTextures(1, &d->atlas);
  Impl::freePart(d->items);
}

void VoxelWorld::defineBlock(int id, const BlockType& bt) {
  if (id <= 0 || id > 65535) return;
  auto t = std::make_shared<BlockTable>(*d->table);
  if ((int)t->t.size() <= id) { size_t n = (size_t)id + 1; t->t.resize(n); t->opaque.resize(n, 1); t->solid.resize(n, 1); t->liquid.resize(n, 0); t->cube.resize(n, 1); }
  BlockType b = bt; b.defined = true; if (b.liquid) { b.solid = false; b.opaque = false; }
  bool cube = b.shape == SH_Cube && b.model.empty();
  t->t[id] = b; t->opaque[id] = b.opaque && cube && !b.liquid; t->solid[id] = b.solid; t->liquid[id] = b.liquid; t->cube[id] = cube; d->table = t;
  for (auto& [k, c] : d->chunks) c->version++;   // colours change: rebuild
}
const BlockType& VoxelWorld::block(int id) const { return typeOf(*d->table, id); }

void VoxelWorld::setGenerator(GeneratorFn gfn) {
  d->gen = std::move(gfn); d->jsGen = false; d->genVersion++; d->spills.clear();
  for (auto& [k, c] : d->chunks) { d->freeGpu(*c); d->freeBody(*c); } d->chunks.clear();
}
void VoxelWorld::setBuiltinGenerator(const std::string& kind, const std::string& params) {
  json p = json::object(); try { p = json::parse(params); if (!p.is_object()) p = json::object(); } catch (...) {}
  if (kind != "flat" && kind != "hills") d->log("warning: voxels: unknown generator '" + kind + "', using 'hills'");
  setGenerator(builtinGenerator(kind == "flat" ? "flat" : "hills", p, cfg_.seed));
}
void VoxelWorld::setScriptGenerator(const std::string& src, const std::string& params) {
  { std::string err; JsGen test; if (!jsGenSetup(test, src, params, err)) { d->log("error: voxels.setGenerator: " + err); return; } }
  d->jsSrc = src; d->jsParams = params;
  static std::atomic<int> jsVersions{0}; int ver = ++jsVersions; Impl* D = d.get(); std::string src2 = src, par2 = params; uint32_t seed = cfg_.seed;
  setGenerator([D, ver, src2, par2, seed](ChunkWriter& w0) {
    GenWriter& w = static_cast<GenWriter&>(w0);
    if (!t_js || t_js->version != ver) { t_js.reset(new JsGen()); std::string err; if (!jsGenSetup(*t_js, src2, par2, err)) { D->log("error: voxels generator: " + err); t_js->version = ver; return; } t_js->version = ver; }
    JsGen& G = *t_js; if (!G.ctx || JS_IsUndefined(G.fn)) return; JSContext* c = G.ctx;
    t_writer = &w; t_seed = seed; t_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    JS_SetPropertyStr(c, G.chunk, "cx", JS_NewInt32(c, w.cx())); JS_SetPropertyStr(c, G.chunk, "cz", JS_NewInt32(c, w.cz()));
    JS_SetPropertyStr(c, G.chunk, "x", JS_NewInt32(c, w.cx() * CHUNK_M)); JS_SetPropertyStr(c, G.chunk, "z", JS_NewInt32(c, w.cz() * CHUNK_M));
    JSValue args[3] = {JS_DupValue(c, G.chunk), JS_DupValue(c, G.noise), JS_DupValue(c, G.params)};
    JSValue r = JS_Call(c, G.fn, JS_UNDEFINED, 3, args); for (auto& a : args) JS_FreeValue(c, a);
    if (JS_IsException(r)) { std::string m = jsErr(c); if (m.find("interrupted") != std::string::npos) m += " - the generator ran for more than 3 seconds on one chunk"; D->log("error: voxels generator (chunk " + std::to_string(w.cx()) + ", " + std::to_string(w.cz()) + "): " + m); }
    JS_FreeValue(c, r); t_writer = nullptr;
  });
  d->jsGen = true;
}
static bool uploadAtlas(VoxelWorld::Impl& D, const uint8_t* px, int w, int h, int tiles) {
  if (!glGenTextures) return false;
  if (!D.atlas) glGenTextures(1, &D.atlas);
  glBindTexture(GL_TEXTURE_2D, D.atlas); glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  D.atlasTiles = std::max(1, tiles); D.atlasW = w; D.atlasH = h; return true;
}
unsigned VoxelWorld::atlasTexture() const { return d->atlas; }
int VoxelWorld::atlasTiles() const { return d->atlasTiles; }
glm::ivec2 VoxelWorld::atlasSize() const { return {d->atlasW, d->atlasH}; }
bool VoxelWorld::setAtlas(const std::string& path, int tilesPerRow) {
  int w = 0, h = 0, n = 0; unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
  if (!px) { d->log("error: voxels.setAtlas: can't read " + path + (stbi_failure_reason() ? std::string(" (") + stbi_failure_reason() + ")" : "")); return false; }
  bool ok = uploadAtlas(*d, px, w, h, tilesPerRow); stbi_image_free(px); return ok;
}
bool VoxelWorld::setAtlasPixels(const uint8_t* rgba, int w, int h, int tilesPerRow, std::string* why) {
  if (!rgba || w <= 0 || h <= 0 || w > 16384 || h > 16384) { if (why) *why = "the atlas must be 1 .. 16384 pixels a side"; return false; }
  if (tilesPerRow <= 0 || w % tilesPerRow) { if (why) *why = "the width must be a whole number of tiles"; return false; }
  return uploadAtlas(*d, rgba, w, h, tilesPerRow);
}

std::vector<std::string> VoxelWorld::takeLogs() { std::lock_guard<std::mutex> l(d->rm); auto r = std::move(d->logs); d->logs.clear(); return r; }

/* ---- edits on the main thread (copy-on-write: meshes being built on other threads keep their copy) */
static void writeLocal(const Geo& g, ChunkData& cd, int lx, int fy, int lz, Cell v) {
  if (g.inBand(fy)) { int by = fy - g.bandLo, bi = g.brickIdx(lx, by, lz); auto& b = cd.bricks[bi];
    if (!b) { int f = (*cd.fill)[bi];
      if (f) { b = latticeBrick(g, bi, f); if (cd.fill.use_count() > 1) cd.fill = std::make_shared<std::vector<uint16_t>>(*cd.fill); (*cd.fill)[bi] = 0; }
      else { if (!v) return; b = std::make_shared<Brick>(); std::memset(b->c, 0, sizeof b->c); } }
    else if (b.use_count() > 1) b = std::make_shared<Brick>(*b);
    b->c[Geo::inBrick(lx, by, lz)] = v; }
  else { int my = floorDiv(fy, VC); if (my < g.minY || my >= g.maxY) return; if (cd.coarse.use_count() > 1) cd.coarse = std::make_shared<std::vector<uint16_t>>(*cd.coarse);
    (*cd.coarse)[g.coarseIdx(lx / VC, my, lz / VC)] = (uint16_t)cellId(v); }
}
static void setExtLocal(const Geo& g, ChunkData& cd, int olx, int ofy, int olz, const BlockExtra* e) {
  if (!e && !cd.ext) return;
  if (!cd.ext) cd.ext = std::make_shared<ExtMap>(); else if (cd.ext.use_count() > 1) cd.ext = std::make_shared<ExtMap>(*cd.ext);
  if (e) (*cd.ext)[g.extKey(olx, ofy, olz)] = *e; else cd.ext->erase(g.extKey(olx, ofy, olz));
}
static void writeCell(VoxelWorld::Impl& D, int fx, int fy, int fz, Cell v) {
  int cx = floorDiv(fx, CHUNK_C), cz = floorDiv(fz, CHUNK_C); Chunk* c = D.get(cx, cz); if (!c || !c->generated) return;
  writeLocal(D.g, c->data, fx - cx * CHUNK_C, fy, fz - cz * CHUNK_C, v); c->edited = true;
}
static void setExt(VoxelWorld::Impl& D, glm::ivec3 o, int s, const BlockExtra* e) {   // in every chunk the block touches
  for (int cx = floorDiv(o.x, CHUNK_C); cx <= floorDiv(o.x + s - 1, CHUNK_C); cx++) for (int cz = floorDiv(o.z, CHUNK_C); cz <= floorDiv(o.z + s - 1, CHUNK_C); cz++)
    if (Chunk* c = D.get(cx, cz)) { setExtLocal(D.g, c->data, o.x - cx * CHUNK_C, o.y, o.z - cz * CHUNK_C, e); c->edited = true; }
}
static void touch(VoxelWorld::Impl& D, glm::ivec3 lo, glm::ivec3 hi) {   // cells changed in [lo, hi): rebuild those chunks and neighbours that see them
  int m = VC + 1;
  for (int cx = floorDiv(lo.x - m, CHUNK_C); cx <= floorDiv(hi.x - 1 + m, CHUNK_C); cx++) for (int cz = floorDiv(lo.z - m, CHUNK_C); cz <= floorDiv(hi.z - 1 + m, CHUNK_C); cz++)
    if (Chunk* c = D.get(cx, cz)) c->version++;
}
// a generator's write from a neighbour chunk, into chunk c (cells outside c are left for their own chunk)
static void applySpill(VoxelWorld::Impl& D, Chunk& c, const SpillBlock& s) {
  const Geo& g = D.g; const BlockTable& T = *D.table; glm::ivec3 base(c.cx * CHUNK_C, 0, c.cz * CHUNK_C);
  if (s.kind == 1) {
    for (int my = std::max<int>(s.oy, g.minY); my < std::min<int>(s.y1, g.maxY); my++) {
      if (g.metreInBand(my)) { for (int a = 0; a < VC; a++) for (int b = 0; b < VC; b++) for (int e = 0; e < VC; e++) writeLocal(g, c.data, s.ox * VC + a, my * VC + b, s.oz * VC + e, makeCell(s.id, VC, a, b, e)); }
      else writeLocal(g, c.data, s.ox * VC, my * VC, s.oz * VC, makeCell(s.id, VC, 0, 0, 0)); }
    touch(D, base + glm::ivec3(s.ox * VC, s.oy * VC, s.oz * VC), base + glm::ivec3(s.ox * VC + VC, s.y1 * VC, s.oz * VC + VC)); return;
  }
  if (s.onlyEmpty) for (int a = 0; a < s.s; a++) for (int b = 0; b < s.sy; b++) for (int e = 0; e < s.s; e++) {
    int lx = s.ox + a, lz = s.oz + e; if (lx < 0 || lx >= CHUNK_C || lz < 0 || lz >= CHUNK_C) continue; if (!emptyish(T, readCell(c.data, g, lx, s.oy + b, lz))) return; }
  for (int a = 0; a < s.s; a++) for (int b = 0; b < s.sy; b++) for (int e = 0; e < s.s; e++) {
    int lx = s.ox + a, lz = s.oz + e; if (lx < 0 || lx >= CHUNK_C || lz < 0 || lz >= CHUNK_C) continue;
    writeLocal(g, c.data, lx, s.oy + b, lz, makeCell(s.id, s.s, a, b, e, s.sy, s.hasExt)); }
  if (s.hasExt) setExtLocal(g, c.data, s.ox, s.oy, s.oz, &s.ext);
  touch(D, base + glm::ivec3(s.ox, s.oy, s.oz), base + glm::ivec3(s.ox + s.s, s.oy + s.sy, s.oz + s.s));
}
static void applySpillsTo(VoxelWorld::Impl& D, Chunk& c) {
  auto it = D.spills.find(ckey(c.cx, c.cz)); if (it == D.spills.end()) return;
  for (auto& [src, list] : it->second) { if (c.spillsDone.count(src)) continue; for (auto& s : list) applySpill(D, c, s); c.spillsDone.insert(src); }
}

/* ---- streaming */
static void uploadPart(GpuPart& gp, const MeshPart& m) {
  if (!glGenBuffers) { gp.count = (int)m.i.size(); gp.bmin = m.bmin; gp.bmax = m.bmax; return; }
  if (m.i.empty()) { VoxelWorld::Impl::freePart(gp); return; }
  if (!gp.vao) { glGenVertexArrays(1, &gp.vao); glGenBuffers(1, &gp.vbo); glGenBuffers(1, &gp.ebo);
    glBindVertexArray(gp.vao); glBindBuffer(GL_ARRAY_BUFFER, gp.vbo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gp.ebo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VVert), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(VVert), (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(VVert), (void*)20);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_BYTE, GL_TRUE, sizeof(VVert), (void*)24);
    glEnableVertexAttribArray(4); glVertexAttribIPointer(4, 2, GL_UNSIGNED_SHORT, sizeof(VVert), (void*)28);
    glBindVertexArray(0); }
  glBindVertexArray(gp.vao); glBindBuffer(GL_ARRAY_BUFFER, gp.vbo); glBufferData(GL_ARRAY_BUFFER, m.v.size() * sizeof(VVert), m.v.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gp.ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, m.i.size() * 4, m.i.data(), GL_DYNAMIC_DRAW); glBindVertexArray(0);
  gp.count = (int)m.i.size(); gp.bmin = m.bmin; gp.bmax = m.bmax;
}
void VoxelWorld::update(const glm::vec3& focus) {
  Impl& D = *d; D.focus = focus;
  // finished work from the threads
  std::vector<std::function<void()>> res; { std::lock_guard<std::mutex> l(D.rm); res.swap(D.results); }
  for (auto& f : res) f();
  int fcx = floorDiv((int)std::floor(focus.x), CHUNK_M), fcz = floorDiv((int)std::floor(focus.z), CHUNK_M), R = cfg_.viewDistance;
  int nThreads = (int)D.workers.size();
  // ask for chunks, nearest first
  struct Want { int cx, cz, d2; }; std::vector<Want> want;
  // generate every chunk next to one that gets a mesh (meshes need their 8 neighbours)
  for (int dz = -R - 2; dz <= R + 2; dz++) for (int dx = -R - 2; dx <= R + 2; dx++) { int ax = std::max(std::abs(dx) - 1, 0), az = std::max(std::abs(dz) - 1, 0);
    if (ax * ax + az * az > R * R + 1) continue; want.push_back({fcx + dx, fcz + dz, dx * dx + dz * dz}); }
  std::sort(want.begin(), want.end(), [](const Want& a, const Want& b) { return a.d2 < b.d2; });
  for (auto& w : want) {
    if (D.inflightGen >= nThreads * 2) break;
    Chunk* c = D.get(w.cx, w.cz); if (c && (c->generated || c->genQueued)) continue;
    if (!c) { auto nc = std::make_unique<Chunk>(); nc->cx = w.cx; nc->cz = w.cz; c = nc.get(); D.chunks[ckey(w.cx, w.cz)] = std::move(nc); }
    c->genQueued = true; D.inflightGen++; int cx = w.cx, cz = w.cz, ver = D.genVersion; GeneratorFn gen = D.gen; Geo g = D.g; Impl* P = &D; BlockTablePtr T = D.table;
    D.post(0, [P, cx, cz, ver, gen, g, T] {
      auto t0 = std::chrono::steady_clock::now(); ChunkData cd = newChunkData(g); GenWriter w(cd, g, *T, cx, cz);
      try { if (gen) gen(w); } catch (const std::exception& e) { P->log(std::string("error: voxels generator: ") + e.what()); }
      if (!w.err.empty()) P->log("warning: voxels generator (chunk " + std::to_string(cx) + ", " + std::to_string(cz) + "): " + w.err);
      compressBricks(cd, g);
      double ms = msSince(t0); auto spills = std::make_shared<std::vector<SpillBlock>>(std::move(w.spills));
      P->result([P, cx, cz, ver, cd = std::move(cd), ms, spills]() mutable {
        P->inflightGen--; P->genMs += ms; P->genCount++;
        if (ver != P->genVersion) return;
        // writes into the chunks around: kept for them, and added now to those that are already there
        int64_t me = ckey(cx, cz); std::unordered_map<int64_t, std::vector<SpillBlock>> byTarget;
        for (auto& s : *spills) byTarget[ckey(cx + s.dx, cz + s.dz)].push_back(s);
        for (auto& [tk, list] : P->spills) list.erase(me);
        for (auto& [tk, list] : byTarget) { P->spills[tk][me] = list;
          Chunk* t = P->get((int)(tk >> 32), (int)(uint32_t)tk);
          if (t && t->generated && !t->spillsDone.count(me)) { for (auto& s : list) applySpill(*P, *t, s); t->spillsDone.insert(me); t->version++; } }
        Chunk* c = P->get(cx, cz); if (!c) return;
        c->genQueued = false; if (c->generated) return;   // loaded from a save meanwhile
        c->data = std::move(cd); c->generated = true; c->genQueued = false; c->version++; c->spillsDone.clear();
        applySpillsTo(*P, *c);
      });
    });
  }
  // build meshes for chunks whose neighbours are all there
  std::vector<Chunk*> toMesh;
  for (auto& [k, c] : D.chunks) {
    if (!c->generated || c->meshing || c->meshedVersion == c->version) continue;
    int dx = c->cx - fcx, dz = c->cz - fcz; if (dx * dx + dz * dz > R * R + 1) continue;
    bool all = true; for (int a = -1; a <= 1 && all; a++) for (int b = -1; b <= 1; b++) { Chunk* n = D.get(c->cx + a, c->cz + b); if (!n || !n->generated) { all = false; break; } }
    if (all) toMesh.push_back(c.get());
  }
  std::sort(toMesh.begin(), toMesh.end(), [&](Chunk* a, Chunk* b) {
    bool ea = a->meshedVersion >= 0, eb = b->meshedVersion >= 0; if (ea != eb) return ea;   // edits of visible chunks first
    int da = (a->cx - fcx) * (a->cx - fcx) + (a->cz - fcz) * (a->cz - fcz), db = (b->cx - fcx) * (b->cx - fcx) + (b->cz - fcz) * (b->cz - fcz); return da < db; });
  for (Chunk* c : toMesh) {
    bool edit = c->meshedVersion >= 0; if (!edit && D.inflightMesh >= nThreads * 2) continue;
    auto snap = std::make_shared<Snapshot>(); snap->cx = c->cx; snap->cz = c->cz;
    for (int a = -1; a <= 1; a++) for (int b = -1; b <= 1; b++) snap->n[a + 1][b + 1] = D.get(c->cx + a, c->cz + b)->data;
    c->meshing = true; D.inflightMesh++; int ver = c->version, cx = c->cx, cz = c->cz, gv = D.genVersion; BlockTablePtr T = D.table; Geo g = D.g; Impl* P = &D;
    D.post(edit ? 1 : 0, [P, snap, ver, cx, cz, gv, T, g] {
      auto t0 = std::chrono::steady_clock::now();
      auto out = std::make_shared<MeshOut>(meshChunk(g, *T, *snap));
      ShapePtr shape = out->ci.empty() ? nullptr : buildTriangleShape(out->cv, out->ci);
      out->cv.clear(); out->cv.shrink_to_fit(); out->ci.clear(); out->ci.shrink_to_fit();
      double ms = msSince(t0);
      P->result([P, out, shape, ver, cx, cz, gv, ms] {
        P->inflightMesh--; P->meshMs += ms; P->meshCount++;
        Chunk* c = P->get(cx, cz); if (!c || gv != P->genVersion) return; c->meshing = false;
        c->meshedVersion = std::max(c->meshedVersion, ver);
        uploadPart(c->solid, out->solid); uploadPart(c->water, out->water); c->hasMesh = true;
        glm::vec3 org((float)(cx * CHUNK_M), 0, (float)(cz * CHUNK_M));
        if (P->phys) { if (!shape) P->freeBody(*c); else if (c->body == NO_BODY) c->body = P->phys->addStatic(shape, org, VOXEL_TAG); else P->phys->setStaticShape(c->body, shape); }
      });
    });
  }
  // forget far chunks (edited ones keep their blocks)
  for (auto it = D.chunks.begin(); it != D.chunks.end();) {
    Chunk& c = *it->second; int dx = c.cx - fcx, dz = c.cz - fcz;
    if (dx * dx + dz * dz > (R + 3) * (R + 3) && !c.meshing && !c.genQueued) {
      D.freeGpu(c); D.freeBody(c); c.hasMesh = false; c.meshedVersion = -1;
      if (!c.edited) { it = D.chunks.erase(it); continue; }
    }
    ++it;
  }
}

bool VoxelWorld::ready(const glm::vec3& p, float r) const {
  for (int x = (int)std::floor(p.x - r); x <= (int)std::floor(p.x + r); x += 1) for (int z = (int)std::floor(p.z - r); z <= (int)std::floor(p.z + r); z += 1) {
    const Chunk* c = d->get(floorDiv(x, CHUNK_M), floorDiv(z, CHUNK_M)); if (!c || !c->hasMesh) return false; }
  return true;
}
void VoxelWorld::waitIdle(int maxMs) {
  auto t0 = std::chrono::steady_clock::now();
  while (msSince(t0) < maxMs) {
    update(d->focus);
    bool pending = d->inflightGen > 0 || d->inflightMesh > 0; int fcx = floorDiv((int)std::floor(d->focus.x), CHUNK_M), fcz = floorDiv((int)std::floor(d->focus.z), CHUNK_M), R = cfg_.viewDistance;
    if (!pending) { bool all = true; for (auto& [k, c] : d->chunks) { int dx = c->cx - fcx, dz = c->cz - fcz; if (dx * dx + dz * dz <= R * R + 1 && c->meshedVersion != c->version) { all = false; break; } }
      if (all) { update(d->focus); if (d->inflightGen == 0 && d->inflightMesh == 0) return; } }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

void VoxelWorld::draw(Frame& f, const Camera& cam) const {
  glm::mat4 vp = cam.proj * cam.view;
  auto outside = [&](glm::vec3 mn, glm::vec3 mx) {   // frustum: all 8 corners outside one plane
    for (int pl = 0; pl < 6; pl++) { int ax = pl / 2; float sg = pl % 2 ? -1.f : 1.f; bool inside = false;
      for (int i = 0; i < 8 && !inside; i++) { glm::vec4 q = vp * glm::vec4((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z, 1); if (sg * q[ax] <= q.w) inside = true; }
      if (!inside) return true; }
    return false; };
  for (auto& [k, cp] : d->chunks) { const Chunk& c = *cp; glm::vec3 org((float)(c.cx * CHUNK_M), 0, (float)(c.cz * CHUNK_M));
    if (c.solid.vao && c.solid.count) { glm::vec3 mn = org + c.solid.bmin, mx = org + c.solid.bmax; bool out = outside(mn, mx);
      // chunks outside the view still cast shadows into it (a hill behind the camera)
      if (!(out && glm::length(glm::vec2(glm::max(mn.x - cam.pos.x, glm::max(0.f, cam.pos.x - mx.x)), glm::max(mn.z - cam.pos.z, glm::max(0.f, cam.pos.z - mx.z)))) > 70.f)) {
        VoxelDraw v; v.vao = c.solid.vao; v.count = c.solid.count; v.offset = org; v.mn = mn; v.mx = mx; v.visible = !out; f.voxels.push_back(v); } }
    if (c.water.vao && c.water.count) { glm::vec3 mn = org + c.water.bmin - glm::vec3(0, 0.1f, 0), mx = org + c.water.bmax;
      if (!outside(mn, mx)) { VoxelDraw v; v.vao = c.water.vao; v.count = c.water.count; v.offset = org; v.mn = mn; v.mx = mx; f.voxelWater.push_back(v); } }
  }
  if (d->items.vao && d->items.count) { VoxelDraw v; v.vao = d->items.vao; v.count = d->items.count; v.offset = glm::vec3(0); v.mn = d->items.bmin; v.mx = d->items.bmax; v.visible = true; f.voxels.push_back(v); }
  f.voxelAtlas = d->atlas; f.voxelTiles = d->atlasTiles;
}
int VoxelWorld::itemIndices() const { return d->items.count; }
void VoxelWorld::setItems(const std::vector<float>& r) {
  MeshPart M; const BlockTable& T = *d->table;
  for (size_t i = 0; i + 13 <= r.size(); i += 13) {
    int id = (int)r[i]; const BlockType& bt = typeOf(T, id); glm::vec3 c(r[i + 1], r[i + 2], r[i + 3]), sz(r[i + 4], r[i + 5], r[i + 6]); float yaw = r[i + 7], pitch = r[i + 8]; int tile = (int)r[i + 9];
    glm::vec3 mul(r[i + 10], r[i + 11], r[i + 12]); if (!std::isfinite(c.x + c.y + c.z + sz.x + sz.y + sz.z)) continue;
    glm::mat3 R = glm::mat3(glm::rotate(glm::mat4(1), yaw, glm::vec3(0, 1, 0)) * glm::rotate(glm::mat4(1), pitch, glm::vec3(1, 0, 0)));
    uint8_t fl = (uint8_t)(std::lround(glm::clamp(bt.glow / 2.f, 0.f, 1.f) * 15) | ((tile >= 0 || bt.cutout) ? 16 : 0));
    auto quad = [&](glm::vec3 p0, glm::vec3 eu, glm::vec3 ev, glm::vec3 n, int face) {   // p0 + u*eu + v*ev, u, v in 0..1 (local, -0.5 .. 0.5 box)
      glm::vec3 col = (tile >= 0 ? glm::vec3(1) : bt.color[face]) * mul; int t = tile >= 0 ? tile : bt.tile[face]; glm::vec3 wn = glm::normalize(R * n);
      uint32_t b = (uint32_t)M.v.size(); const float U[4] = {0, 1, 0, 1}, V[4] = {0, 0, 1, 1};
      for (int k = 0; k < 4; k++) { glm::vec3 P = c + R * ((p0 + eu * U[k] + ev * V[k]) * sz); VVert v{}; v.x = P.x; v.y = P.y; v.z = P.z; v.u = U[k]; v.v = V[k];
        glm::vec3 cc = glm::clamp(col * 0.5f, 0.f, 1.f); v.r = (uint8_t)std::lround(cc.r * 255); v.g = (uint8_t)std::lround(cc.g * 255); v.b = (uint8_t)std::lround(cc.b * 255); v.ao = 255;
        v.nx = (int8_t)std::lround(wn.x * 127); v.ny = (int8_t)std::lround(wn.y * 127); v.nz = (int8_t)std::lround(wn.z * 127); v.tile = t < 0 ? 0xffff : (uint16_t)t; v.flags = fl;
        M.v.push_back(v); M.bmin = glm::min(M.bmin, P); M.bmax = glm::max(M.bmax, P); }
      bool flip = glm::dot(glm::cross(eu, ev), n) < 0; uint32_t tri[6] = {0, 1, 3, 0, 3, 2}; if (flip) { std::swap(tri[1], tri[2]); std::swap(tri[4], tri[5]); }
      for (uint32_t t2 : tri) M.i.push_back(b + t2); };
    if (bt.shape == SH_Cross && tile < 0) {   // flowers, grass, items: two crossed pictures seen from both sides
      const float h = 0.3535534f; glm::vec3 a(-h, -0.5f, -h), b2(h, -0.5f, h), e(-h, -0.5f, h), f2(h, -0.5f, -h), up(0, 1, 0);
      quad(a, b2 - a, up, glm::normalize(glm::cross(b2 - a, up)), 1); quad(b2, a - b2, up, glm::normalize(glm::cross(a - b2, up)), 1);
      quad(e, f2 - e, up, glm::normalize(glm::cross(f2 - e, up)), 1); quad(f2, e - f2, up, glm::normalize(glm::cross(e - f2, up)), 1);
      continue; }
    for (int dd = 0; dd < 6; dd++) { glm::ivec3 n = DIRS[dd]; int ua, va; faceAxes(dd, ua, va); glm::vec3 eu(0), ev(0); eu[ua] = 1; ev[va] = 1;
      glm::vec3 p0(-0.5f); if (n.x > 0) p0.x = 0.5f; if (n.y > 0) p0.y = 0.5f; if (n.z > 0) p0.z = 0.5f;
      quad(p0, eu, ev, glm::vec3(n), dd == 2 ? 0 : dd == 3 ? 2 : 1); }
  }
  uploadPart(d->items, M);
}

/* ---- blocks */
Cell VoxelWorld::cell(int fx, int fy, int fz) const {
  int cx = floorDiv(fx, CHUNK_C), cz = floorDiv(fz, CHUNK_C); const Chunk* c = d->get(cx, cz); if (!c || !c->generated) return 0;
  return readCell(c->data, d->g, fx - cx * CHUNK_C, fy, fz - cz * CHUNK_C);
}
static glm::ivec3 toCell(const glm::vec3& p) { return {(int)std::floor(p.x * VC + 1e-3f), (int)std::floor(p.y * VC + 1e-3f), (int)std::floor(p.z * VC + 1e-3f)}; }
static glm::ivec3 floorCell(const glm::vec3& p) { return {(int)std::floor(p.x * VC), (int)std::floor(p.y * VC), (int)std::floor(p.z * VC)}; }
static BlockExtra extAt(const VoxelWorld::Impl& D, glm::ivec3 o) {
  int cx = floorDiv(o.x, CHUNK_C), cz = floorDiv(o.z, CHUNK_C);
  for (int a = 0; a <= 1; a++) for (int b = 0; b <= 1; b++) { const Chunk* c = D.get(cx + a, cz + b); if (!c) continue;   // the origin's chunk or one the block reaches into
    if (const BlockExtra* e = extOf(c->data, D.g, o.x - (cx + a) * CHUNK_C, o.y, o.z - (cz + b) * CHUNK_C)) return *e; }
  return BlockExtra();
}
BlockInfo VoxelWorld::getBlock(const glm::vec3& p) const {
  glm::ivec3 f = floorCell(p); Cell c = cell(f.x, f.y, f.z); BlockInfo b; if (!c) return b;
  b.id = cellId(c); b.size = cellSize(c); b.sizeY = cellSizeY(c); b.origin = f - cellOffset(c); if (cellExt(c)) b.extra = extAt(*d, b.origin); return b;
}
bool VoxelWorld::solidAt(const glm::vec3& p) const { BlockInfo b = getBlock(p); return b.id && block(b.id).solid; }
int VoxelWorld::liquidAt(const glm::vec3& p) const { BlockInfo b = getBlock(p); return b.id && block(b.id).liquid ? b.id : 0; }

static bool loadedBox(const VoxelWorld::Impl& D, glm::ivec3 lo, glm::ivec3 hi) {
  for (int cx = floorDiv(lo.x, CHUNK_C); cx <= floorDiv(hi.x - 1, CHUNK_C); cx++) for (int cz = floorDiv(lo.z, CHUNK_C); cz <= floorDiv(hi.z - 1, CHUNK_C); cz++) {
    const Chunk* c = D.get(cx, cz); if (!c || !c->generated) return false; }
  return true;
}
static void eraseBlockAt(VoxelWorld& W, VoxelWorld::Impl& D, glm::ivec3 f) {
  Cell c = W.cell(f.x, f.y, f.z); if (!c) return;
  if (!D.g.inBand(f.y)) { writeCell(D, f.x, f.y, f.z, 0); touch(D, f, f + 1); return; }
  int s = cellSize(c), sy = cellSizeY(c); glm::ivec3 o = f - cellOffset(c);
  for (int a = 0; a < s; a++) for (int b = 0; b < sy; b++) for (int e = 0; e < s; e++) { glm::ivec3 q = o + glm::ivec3(a, b, e); Cell x = W.cell(q.x, q.y, q.z); if (x && cellId(x) == cellId(c) && q - cellOffset(x) == o) writeCell(D, q.x, q.y, q.z, 0); }
  if (cellExt(c)) setExt(D, o, s, nullptr);
  touch(D, o, o + glm::ivec3(s, sy, s));
}
bool VoxelWorld::canPlace(const glm::vec3& corner, float size, float sizeY) const {
  int s = (int)std::lround(size * VC), sy = sizeY > 0 ? (int)std::lround(sizeY * VC) : s; if (s < 1 || s > VC || sy < 1 || sy > VC) return false; glm::ivec3 o = toCell(corner);
  if (!d->g.inBand(o.y) || !d->g.inBand(o.y + sy - 1)) { if (s != VC || sy != VC) return false; o = glm::ivec3(floorDiv(o.x, VC), floorDiv(o.y, VC), floorDiv(o.z, VC)) * VC; if (d->g.inBand(o.y)) return false; }
  if (!loadedBox(*d, o, o + s)) return false;
  for (int a = 0; a < s; a++) for (int b = 0; b < sy; b++) for (int e = 0; e < s; e++) if (!emptyish(*d->table, cell(o.x + a, o.y + b, o.z + e))) return false;
  return true;
}
bool VoxelWorld::setBlock(const glm::vec3& corner, int id, float size, const BlockOpts& opt, std::string* why) {
  auto fail = [&](const char* m) { if (why) *why = m; return false; };
  if (id < 0 || id > 65535) return fail("block id must be 0..65535");
  int s = (int)std::lround(size * VC); if (s < 1 || s > VC || std::abs(size * VC - s) > 0.05f) return fail("size must be 0.2, 0.4, 0.6, 0.8 or 1");
  int sy = opt.sy > 0 ? opt.sy : s; if (sy > VC) return fail("height must be 0.2 .. 1");
  glm::ivec3 o = toCell(corner); const Geo& g = d->g; bool ext = opt.extra.any();
  bool inB = g.inBand(o.y) && g.inBand(o.y + sy - 1), outB = !g.inBand(o.y) && !g.inBand(o.y + sy - 1);
  if (!inB && !outB) return fail("the block would cross the edge of the 20 cm layer");
  if (outB) { if (s != VC || sy != VC) return fail("outside the 20 cm layer blocks are 1 m"); if (ext) return fail("outside the 20 cm layer blocks can't be turned, shaped or carved");
    o = glm::ivec3(floorDiv(o.x, VC), floorDiv(o.y, VC), floorDiv(o.z, VC)) * VC;
    if (o.y / VC < g.minY || o.y / VC >= g.maxY) return fail("outside the world's height"); }
  if (!loadedBox(*d, o, o + s)) return fail("that part of the world is not loaded");
  if (opt.onlyEmpty && !canPlace(glm::vec3(o) / (float)VC, size, sy / (float)VC)) return fail("there is a block in the way");
  if (outB) { writeCell(*d, o.x, o.y, o.z, makeCell(id, VC, 0, 0, 0)); touch(*d, o, o + s); return true; }
  for (int a = 0; a < s; a++) for (int b = 0; b < sy; b++) for (int e = 0; e < s; e++) { glm::ivec3 q = o + glm::ivec3(a, b, e); if (cell(q.x, q.y, q.z)) eraseBlockAt(*this, *d, q); }
  if (id) { for (int a = 0; a < s; a++) for (int b = 0; b < sy; b++) for (int e = 0; e < s; e++) writeCell(*d, o.x + a, o.y + b, o.z + e, makeCell(id, s, a, b, e, sy, ext));
    if (ext) setExt(*d, o, s, &opt.extra); }
  touch(*d, o, o + glm::ivec3(s, sy, s)); return true;
}
int VoxelWorld::removeBlock(const glm::vec3& p) {
  glm::ivec3 f = floorCell(p); Cell c = cell(f.x, f.y, f.z); if (!c) return 0;
  int id = cellId(c); eraseBlockAt(*this, *d, f); return id;
}
bool VoxelWorld::carve(const glm::vec3& p, int i, int j, int k, bool solid) {
  if (i < 0 || i > 4 || j < 0 || j > 4 || k < 0 || k > 4) return false;
  BlockInfo b = getBlock(p); if (!b.id || !d->g.inBand(b.origin.y)) return false;
  BlockExtra e = b.extra; if (!e.carved) { e.carved = true; e.carve = {{0, 0}}; for (int z = 0; z < 5; z++) for (int y = 0; y < 5; y++) for (int x = 0; x < 5; x++) e.setSub(x, y, z, true); }
  e.setSub(i, j, k, solid);
  bool empty = !e.carve[0] && !e.carve[1]; if (empty) { eraseBlockAt(*this, *d, b.origin); return true; }
  // the cells get the "extra" mark, the extra goes into every chunk the block touches
  for (int a = 0; a < b.size; a++) for (int y = 0; y < b.sizeY; y++) for (int z = 0; z < b.size; z++) writeCell(*d, b.origin.x + a, b.origin.y + y, b.origin.z + z, makeCell(b.id, b.size, a, y, z, b.sizeY, true));
  setExt(*d, b.origin, b.size, &e); touch(*d, b.origin, b.origin + glm::ivec3(b.size, b.sizeY, b.size)); return true;
}
VoxelHit VoxelWorld::raycast(const glm::vec3& from, const glm::vec3& dir0, float maxDist, bool hitLiquids) const {
  VoxelHit h; float L = glm::length(dir0); if (L < 1e-6f || maxDist <= 0) return h; glm::vec3 dir = dir0 / L;
  glm::vec3 p = from * (float)VC; glm::ivec3 c = glm::ivec3(glm::floor(p)), step; glm::vec3 tMax, tDelta; glm::ivec3 lastN(0);
  for (int i = 0; i < 3; i++) { step[i] = dir[i] > 0 ? 1 : dir[i] < 0 ? -1 : 0;
    tDelta[i] = step[i] ? std::abs(1.f / dir[i]) : 1e30f; float nb = step[i] > 0 ? std::floor(p[i]) + 1 : std::floor(p[i]); tMax[i] = step[i] ? (nb - p[i]) / dir[i] : 1e30f; }
  float maxT = maxDist * VC, t = 0; const BlockTable& T = *d->table;
  for (int it = 0; it < 100000 && t <= maxT; it++) {
    Cell x = cell(c.x, c.y, c.z);
    if (x && (hitLiquids || !isLiquid(T, cellId(x)))) { h.hit = true; h.id = cellId(x); h.size = cellSize(x); h.sizeY = cellSizeY(x); h.origin = c - cellOffset(x); h.cell = c; h.normal = lastN; h.distance = t / VC; h.point = from + dir * h.distance; return h; }
    int a = tMax.x < tMax.y ? (tMax.x < tMax.z ? 0 : 2) : (tMax.y < tMax.z ? 1 : 2);
    t = tMax[a]; c[a] += step[a]; tMax[a] += tDelta[a]; lastN = glm::ivec3(0); lastN[a] = -step[a];
  }
  return h;
}
glm::vec3 VoxelWorld::placeCorner(const VoxelHit& h, float size, float sizeY) const {
  int s = std::max(1, std::min(VC, (int)std::lround(size * VC))), sy = sizeY > 0 ? std::max(1, std::min(VC, (int)std::lround(sizeY * VC))) : s; glm::ivec3 o;
  int hs[3] = {h.size, h.sizeY ? h.sizeY : h.size, h.size}, ns[3] = {s, sy, s};
  // against the face, centred on the hit point along the face, on the 20 cm grid
  glm::vec3 pc = h.point * (float)VC;
  for (int i = 0; i < 3; i++) {
    if (h.normal[i] > 0) o[i] = h.origin[i] + hs[i]; else if (h.normal[i] < 0) o[i] = h.origin[i] - ns[i];
    else o[i] = (int)std::floor(pc[i] - ns[i] * 0.5f + 0.5f);
  }
  if (h.normal == glm::ivec3(0)) o = h.cell;
  return glm::vec3(o) / (float)VC;
}
float VoxelWorld::topY(float x, float z) const {
  int fx = (int)std::floor(x * VC), fz = (int)std::floor(z * VC); const Chunk* c = d->get(floorDiv(fx, CHUNK_C), floorDiv(fz, CHUNK_C));
  if (!c || !c->generated) return std::nanf("");
  for (int fy = d->g.maxY * VC - 1; fy >= d->g.minY * VC; fy--) { Cell x = cell(fx, fy, fz); if (x && block(cellId(x)).solid) return (fy + 1) / (float)VC; }
  return (float)d->g.minY;
}
VoxelWorld::Stats VoxelWorld::stats() const {
  Stats s; s.pendingGen = d->inflightGen; s.pendingMesh = d->inflightMesh; s.genMs = d->genCount ? d->genMs / d->genCount : 0; s.meshMs = d->meshCount ? d->meshMs / d->meshCount : 0;
  for (auto& [k, c] : d->chunks) { s.chunks++; if (c->hasMesh) s.meshed++; s.triangles += (c->solid.count + c->water.count) / 3;
    for (auto& b : c->data.bricks) if (b) { s.bricks++; s.memory += sizeof(Brick); }
    if (c->data.coarse) s.memory += c->data.coarse->size() * 2; }
  return s;
}
int VoxelWorld::changedChunks() const { int n = 0; for (auto& [k, c] : d->chunks) n += c->edited; return n; }

/* ---- saving the player's changes: every changed chunk, whole (run-length packed) */
static const char SAVE_MAGIC[8] = {'P', 'G', 'V', 'O', 'X', 'E', 'L', '1'};
template <class T> static void wr(std::ofstream& o, const T& v) { o.write((const char*)&v, sizeof v); }
template <class T> static bool rd(std::ifstream& i, T& v) { return (bool)i.read((char*)&v, sizeof v); }
template <class T> static void wrRuns(std::ofstream& o, const T* p, size_t n) {   // (count, value) pairs
  std::vector<std::pair<uint32_t, T>> runs; for (size_t i = 0; i < n;) { size_t j = i + 1; while (j < n && p[j] == p[i]) j++; runs.push_back({(uint32_t)(j - i), p[i]}); i = j; }
  wr(o, (uint32_t)runs.size()); for (auto& r : runs) { wr(o, r.first); wr(o, r.second); }
}
template <class T> static bool rdRuns(std::ifstream& in, T* p, size_t n) {
  uint32_t k = 0; if (!rd(in, k)) return false; size_t at = 0;
  for (uint32_t r = 0; r < k; r++) { uint32_t cnt = 0; T v{}; if (!rd(in, cnt) || !rd(in, v) || at + cnt > n) return false; std::fill(p + at, p + at + cnt, v); at += cnt; }
  return at == n;
}
bool VoxelWorld::save(const std::string& file, std::string* why) const {
  namespace fs = std::filesystem; std::error_code ec; fs::path path = fs::u8path(file); if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
  fs::path tmp = path; tmp += ".tmp";
  { std::ofstream o(tmp, std::ios::binary); if (!o) { if (why) *why = "can't write " + file; return false; }
    o.write(SAVE_MAGIC, 8); const Geo& g = d->g; wr(o, (int32_t)g.minY); wr(o, (int32_t)g.maxY); wr(o, (int32_t)cfg_.fineLo); wr(o, (int32_t)cfg_.fineHi); wr(o, (uint32_t)cfg_.seed);
    std::vector<const Chunk*> list; for (auto& [k, c] : d->chunks) if (c->edited && c->generated) list.push_back(c.get());
    wr(o, (uint32_t)list.size());
    for (const Chunk* c : list) { const ChunkData& cd = c->data; wr(o, (int32_t)c->cx); wr(o, (int32_t)c->cz);
      wrRuns(o, cd.coarse->data(), cd.coarse->size()); wrRuns(o, cd.fill->data(), cd.fill->size());
      uint32_t nb = 0; for (auto& b : cd.bricks) nb += b ? 1 : 0; wr(o, nb);
      for (size_t i = 0; i < cd.bricks.size(); i++) if (cd.bricks[i]) { wr(o, (uint32_t)i); wrRuns(o, cd.bricks[i]->c, (size_t)BRICK * BRICK * BRICK); }
      wr(o, (uint8_t)(cd.tint ? 1 : 0)); if (cd.tint) o.write((const char*)cd.tint->data(), cd.tint->size() * 4);
      wr(o, (uint32_t)(cd.ext ? cd.ext->size() : 0)); if (cd.ext) for (auto& [key, e] : *cd.ext) { wr(o, key); wr(o, e.rot); wr(o, e.shape); wr(o, (uint8_t)e.carved); wr(o, e.carve[0]); wr(o, e.carve[1]); }
      wr(o, (uint32_t)c->spillsDone.size()); for (int64_t s : c->spillsDone) wr(o, s); }
    if (!o) { if (why) *why = "can't write " + file; return false; } }
  fs::rename(tmp, path, ec); if (ec) { fs::remove(path, ec); fs::rename(tmp, path, ec); }
  if (ec) { if (why) *why = "can't write " + file + ": " + ec.message(); return false; }
  return true;
}
bool VoxelWorld::load(const std::string& file, std::string* why) {
  std::ifstream in(std::filesystem::u8path(file), std::ios::binary); if (!in) { if (why) *why = "no saved world " + file; return false; }
  char m[8]; in.read(m, 8); if (!in || std::memcmp(m, SAVE_MAGIC, 8)) { if (why) *why = file + " is not a saved block world"; return false; }
  int32_t a, b, c2, e; uint32_t seed; rd(in, a); rd(in, b); rd(in, c2); rd(in, e); rd(in, seed); const Geo& g = d->g;
  if (a != g.minY || b != g.maxY || c2 != cfg_.fineLo || e != cfg_.fineHi) { if (why) *why = "the saved world has other heights (bottom / top / near layer) than voxels.create"; return false; }
  uint32_t n = 0; if (!rd(in, n)) { if (why) *why = file + " is damaged"; return false; }
  std::vector<std::pair<std::pair<int, int>, std::pair<ChunkData, std::unordered_set<int64_t>>>> got;
  for (uint32_t k = 0; k < n; k++) {
    int32_t cx, cz; if (!rd(in, cx) || !rd(in, cz)) { if (why) *why = file + " is damaged"; return false; }
    ChunkData cd = newChunkData(g); bool ok = rdRuns(in, cd.coarse->data(), cd.coarse->size()) && rdRuns(in, cd.fill->data(), cd.fill->size());
    uint32_t nb = 0; ok = ok && rd(in, nb);
    for (uint32_t i = 0; ok && i < nb; i++) { uint32_t bi = 0; ok = rd(in, bi) && bi < cd.bricks.size(); if (!ok) break; auto br = std::make_shared<Brick>(); ok = rdRuns(in, br->c, (size_t)BRICK * BRICK * BRICK); cd.bricks[bi] = br; }
    uint8_t ht = 0; ok = ok && rd(in, ht); if (ok && ht) { cd.tint = std::make_shared<std::vector<uint32_t>>(CHUNK_M * CHUNK_M); ok = (bool)in.read((char*)cd.tint->data(), cd.tint->size() * 4); }
    uint32_t ne = 0; ok = ok && rd(in, ne);
    if (ok && ne) { cd.ext = std::make_shared<ExtMap>(); for (uint32_t i = 0; ok && i < ne; i++) { uint32_t key; BlockExtra x; uint8_t cv; ok = rd(in, key) && rd(in, x.rot) && rd(in, x.shape) && rd(in, cv) && rd(in, x.carve[0]) && rd(in, x.carve[1]); x.carved = cv; (*cd.ext)[key] = x; } }
    uint32_t ns = 0; ok = ok && rd(in, ns); std::unordered_set<int64_t> done; for (uint32_t i = 0; ok && i < ns; i++) { int64_t s; ok = rd(in, s); done.insert(s); }
    if (!ok) { if (why) *why = file + " is damaged"; return false; }
    got.push_back({{cx, cz}, {std::move(cd), std::move(done)}});
  }
  for (auto& [pos, val] : got) {
    Chunk* c = d->get(pos.first, pos.second);
    if (!c) { auto nc = std::make_unique<Chunk>(); nc->cx = pos.first; nc->cz = pos.second; c = nc.get(); d->chunks[ckey(pos.first, pos.second)] = std::move(nc); }
    c->data = std::move(val.first); c->spillsDone = std::move(val.second); c->generated = true; c->edited = true;
    applySpillsTo(*d, *c);   // writes from chunks around that were made after the save
    glm::ivec3 lo(pos.first * CHUNK_C, d->g.minY * VC, pos.second * CHUNK_C); touch(*d, lo, lo + glm::ivec3(CHUNK_C, d->g.H * VC, CHUNK_C));
  }
  return true;
}
}
