// Block world: chunk data, generators, background threads, meshing (hidden faces removed, corner shadows, greedy merging),
// collision shapes, block edits and ray casts. See voxel.h.
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

/* ------------------------------------------------------------------ chunk data */
struct Brick { Cell c[BRICK * BRICK * BRICK]; };
struct ChunkData {
  std::shared_ptr<std::vector<uint16_t>> coarse;    // 16 x 16 x H metres
  std::vector<std::shared_ptr<Brick>> bricks;       // near layer
  std::shared_ptr<std::vector<uint16_t>> fill;      // per brick: when there is no brick, the whole brick is 1 m blocks of this id (ground under the surface)
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

/* ------------------------------------------------------------------ generators */
struct GenWriter final : ChunkWriter {
  ChunkData& d; const Geo& g; int cx_, cz_;
  GenWriter(ChunkData& d, const Geo& g, int cx, int cz) : d(d), g(g), cx_(cx), cz_(cz) {}
  int cx() const override { return cx_; }
  int cz() const override { return cz_; }
  void putFine(int lx, int fy, int lz, Cell c) {
    int by = fy - g.bandLo, bi = g.brickIdx(lx, by, lz); auto& b = d.bricks[bi];
    if (!b) { int f = (*d.fill)[bi]; if (f) { b = latticeBrick(g, bi, f); (*d.fill)[bi] = 0; } else { if (!c) return; b = std::make_shared<Brick>(); std::memset(b->c, 0, sizeof b->c); } }
    b->c[Geo::inBrick(lx, by, lz)] = c;
  }
  void metre(int x, int my, int z, int id) {
    if (x < 0 || x >= CHUNK_M || z < 0 || z >= CHUNK_M || my < g.minY || my >= g.maxY) return;
    if (g.metreInBand(my)) { for (int a = 0; a < VC; a++) for (int b = 0; b < VC; b++) for (int c = 0; c < VC; c++) putFine(x * VC + a, my * VC + b, z * VC + c, makeCell(id, VC, a, b, c)); }
    else (*d.coarse)[g.coarseIdx(x, my, z)] = (uint16_t)id;
  }
  void column(int x, int z, int y0, int y1, int id) override { y0 = std::max(y0, g.minY); y1 = std::min(y1, g.maxY); for (int y = y0; y < y1; y++) metre(x, y, z, id); }
  void fill(int x0, int y0, int z0, int x1, int y1, int z1, int id) override {
    x0 = std::max(x0, 0); z0 = std::max(z0, 0); x1 = std::min(x1, CHUNK_M); z1 = std::min(z1, CHUNK_M);
    for (int x = x0; x < x1; x++) for (int z = z0; z < z1; z++) column(x, z, y0, y1, id);
  }
  bool set(float x, float y, float z, int id, float size) override {
    int s = (int)std::lround(size * VC); if (s < 1 || s > VC) return false;
    int ox = (int)std::floor(x * VC + 1e-3f), oy = (int)std::floor(y * VC + 1e-3f), oz = (int)std::floor(z * VC + 1e-3f);
    if (g.inBand(oy) && g.inBand(oy + s - 1)) {
      for (int a = 0; a < s; a++) for (int b = 0; b < s; b++) for (int c = 0; c < s; c++) {
        int lx = ox + a, lz = oz + c; if (lx < 0 || lx >= CHUNK_C || lz < 0 || lz >= CHUNK_C) continue;   // the neighbour chunk writes its part
        putFine(lx, oy + b, lz, makeCell(id, s, a, b, c)); }
      return true;
    }
    if (s != VC || g.inBand(oy) || g.inBand(oy + s - 1)) return false;
    metre(floorDiv(ox, VC), floorDiv(oy, VC), floorDiv(oz, VC), id); return true;
  }
  int get(float x, float y, float z) const override {
    int lx = (int)std::floor(x * VC + 1e-3f), fy = (int)std::floor(y * VC + 1e-3f), lz = (int)std::floor(z * VC + 1e-3f);
    if (lx < 0 || lx >= CHUNK_C || lz < 0 || lz >= CHUNK_C) return 0; return cellId(readCell(d, g, lx, fy, lz));
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

/* ---- JavaScript generators: one QuickJS engine per background thread */
struct JsGen { JSRuntime* rt = nullptr; JSContext* ctx = nullptr; JSValue fn = JS_UNDEFINED, params = JS_UNDEFINED, chunk = JS_UNDEFINED, noise = JS_UNDEFINED; int version = -1;
  ~JsGen() { if (ctx) { JS_FreeValue(ctx, fn); JS_FreeValue(ctx, params); JS_FreeValue(ctx, chunk); JS_FreeValue(ctx, noise); JS_FreeContext(ctx); JS_FreeRuntime(rt); } } };
static thread_local ChunkWriter* t_writer = nullptr;
static thread_local uint32_t t_seed = 0;
static thread_local std::chrono::steady_clock::time_point t_deadline;
static double JD(JSContext* c, JSValueConst v) { double d = 0; JS_ToFloat64(c, &d, v); return d; }
static std::string jsErr(JSContext* c) {
  JSValue e = JS_GetException(c); const char* s = JS_ToCString(c, e); std::string m = s ? s : "error"; if (s) JS_FreeCString(c, s);
  JSValue st = JS_GetPropertyStr(c, e, "stack"); if (!JS_IsUndefined(st)) { const char* t = JS_ToCString(c, st); if (t) { std::string ts = t; JS_FreeCString(c, t);
      auto p = ts.find(':'); if (p != std::string::npos) { size_t q = p + 1; std::string num; while (q < ts.size() && std::isdigit((unsigned char)ts[q])) num += ts[q++]; if (!num.empty()) m += " (line " + num + ")"; } } }
  JS_FreeValue(c, st); JS_FreeValue(c, e); return m;
}
#define GFN(obj, name, argc, body) JS_SetPropertyStr(c, obj, name, JS_NewCFunction(c, +[](JSContext* c, JSValueConst, int argn, JSValueConst* a) -> JSValue { (void)c; (void)a; (void)argn; body }, name, argc))
static bool jsGenSetup(JsGen& G, const std::string& src, const std::string& params, std::string& err) {
  G.rt = JS_NewRuntime(); JS_SetMemoryLimit(G.rt, 256u << 20); JS_SetMaxStackSize(G.rt, 1u << 20);
  JS_SetInterruptHandler(G.rt, +[](JSRuntime*, void*) -> int { return std::chrono::steady_clock::now() > t_deadline ? 1 : 0; }, nullptr);
  G.ctx = JS_NewContext(G.rt); JSContext* c = G.ctx; t_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  G.chunk = JS_NewObject(c); JSValue ch = G.chunk;
  GFN(ch, "column", 5, { if (t_writer) t_writer->column((int)std::floor(JD(c, a[0])), (int)std::floor(JD(c, a[1])), (int)std::floor(JD(c, a[2])), (int)std::floor(JD(c, a[3])), (int)JD(c, a[4])); return JS_UNDEFINED; });
  GFN(ch, "fill", 7, { if (t_writer) t_writer->fill((int)std::floor(JD(c, a[0])), (int)std::floor(JD(c, a[1])), (int)std::floor(JD(c, a[2])), (int)std::floor(JD(c, a[3])), (int)std::floor(JD(c, a[4])), (int)std::floor(JD(c, a[5])), (int)JD(c, a[6])); return JS_UNDEFINED; });
  GFN(ch, "set", 5, { bool r = t_writer && t_writer->set((float)JD(c, a[0]), (float)JD(c, a[1]), (float)JD(c, a[2]), (int)JD(c, a[3]), argn > 4 ? (float)JD(c, a[4]) : 0.2f); return JS_NewBool(c, r); });
  GFN(ch, "get", 3, { return JS_NewInt32(c, t_writer ? t_writer->get((float)JD(c, a[0]), (float)JD(c, a[1]), (float)JD(c, a[2])) : 0); });
  G.noise = JS_NewObject(c); JSValue nz = G.noise;
  GFN(nz, "simplex2", 2, { return JS_NewFloat64(c, simplex2((float)JD(c, a[0]), (float)JD(c, a[1]), t_seed)); });
  GFN(nz, "simplex3", 3, { return JS_NewFloat64(c, simplex3((float)JD(c, a[0]), (float)JD(c, a[1]), (float)JD(c, a[2]), t_seed)); });
  GFN(nz, "fbm", 5, { return JS_NewFloat64(c, fbm2((float)JD(c, a[0]), (float)JD(c, a[1]), t_seed, argn > 2 ? (int)JD(c, a[2]) : 4, argn > 3 ? (float)JD(c, a[3]) : 2.f, argn > 4 ? (float)JD(c, a[4]) : 0.5f)); });
  GFN(nz, "hash", 3, { return JS_NewFloat64(c, hash3((int)std::floor(JD(c, a[0])), (int)std::floor(JD(c, a[1])), (int)std::floor(JD(c, a[2])), t_seed)); });
  std::string code = "(" + src + ")";
  G.fn = JS_Eval(c, code.c_str(), code.size(), "generator", JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(G.fn)) { err = jsErr(c); return false; }
  if (!JS_IsFunction(c, G.fn)) { err = "setGenerator wants a function"; return false; }
  G.params = JS_ParseJSON(c, params.c_str(), params.size(), "params"); if (JS_IsException(G.params)) { JS_FreeValue(c, JS_GetException(c)); G.params = JS_NewObject(c); }
  return true;
}
#undef GFN

/* ------------------------------------------------------------------ meshing */
struct VVert { float x, y, z, u, v; uint8_t r, g, b, ao; uint8_t n, tlo, thi, flags; };
static_assert(sizeof(VVert) == 28, "vertex layout");
struct BlockTable { std::vector<BlockType> t; std::vector<uint8_t> opaque, solid; };
using BlockTablePtr = std::shared_ptr<const BlockTable>;
static const BlockType& typeOf(const BlockTable& T, int id) { static BlockType missing; if (id >= 0 && id < (int)T.t.size() && T.t[id].defined) return T.t[id];
  missing.color[0] = missing.color[1] = missing.color[2] = glm::vec3(1, 0, 1); return missing; }

struct MeshOut { std::vector<VVert> v; std::vector<uint32_t> i; std::vector<glm::vec3> cv; std::vector<uint32_t> ci; glm::vec3 bmin{1e9f}, bmax{-1e9f}; };
struct Snapshot { int cx = 0, cz = 0; ChunkData n[3][3]; };

static const glm::ivec3 DIRS[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
// the two axes a face of direction d spans: u, v
static void faceAxes(int d, int& ua, int& va) { int ax = d / 2; if (ax == 0) { ua = 2; va = 1; } else if (ax == 1) { ua = 0; va = 2; } else { ua = 0; va = 1; } }

struct Mesher {
  const Geo& g; const BlockTable& T; const Snapshot& S; MeshOut out;
  // dense copies with a one-cell border: the near layer (fine) and the 1 m layer (coarse)
  int FX = CHUNK_C + 2, FY, FZ = CHUNK_C + 2, fy0; std::vector<Cell> F;
  int CX = CHUNK_M + 2, CY, CZ = CHUNK_M + 2, cy0; std::vector<uint16_t> C;
  Mesher(const Geo& g, const BlockTable& T, const Snapshot& S) : g(g), T(T), S(S) {}

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
  void build() {
    fy0 = g.bandLo - 1; FY = g.bandH + 2; F.assign((size_t)FX * FY * FZ, 0);
    for (int y = 0; y < FY; y++) for (int z = 0; z < FZ; z++) for (int x = 0; x < FX; x++) F[((size_t)y * FZ + z) * FX + x] = snapCell(x - 1, y + fy0, z - 1);
    cy0 = g.minY - 1; CY = g.H + 2; C.assign((size_t)CX * CY * CZ, 0);
    for (int y = 0; y < CY; y++) for (int z = 0; z < CZ; z++) for (int x = 0; x < CX; x++) { int my = y + cy0; C[((size_t)y * CZ + z) * CX + x] = g.metreInBand(my) ? 0 : snapCoarse(x - 1, my, z - 1); }
  }
  Cell fcell(int lx, int fy, int lz) const { int y = fy - fy0; if (y < 0 || y >= FY) return 0; return F[((size_t)y * FZ + (lz + 1)) * FX + (lx + 1)]; }
  bool opaqueId(int id) const { return id && (id >= (int)T.opaque.size() || T.opaque[id]); }
  // is the metre cell (x, my, z) opaque? (band metres: look at the fine cell in its middle)
  bool opaqueM(int x, int my, int z) const {
    if (my < g.minY) return true; if (my >= g.maxY) return false;
    if (g.metreInBand(my)) { if (x < -1 || x > CHUNK_M || z < -1 || z > CHUNK_M) return false; int lx = x * VC + 2, lz = z * VC + 2; if (lx < -1) lx = -1; if (lx > CHUNK_C) lx = CHUNK_C; if (lz < -1) lz = -1; if (lz > CHUNK_C) lz = CHUNK_C;
      return opaqueId(cellId(fcell(lx, my * VC + 2, lz))); }
    int y = my - cy0; if (x < -1 || x > CHUNK_M || z < -1 || z > CHUNK_M) return false; return opaqueId(C[((size_t)y * CZ + (z + 1)) * CX + (x + 1)]);
  }
  bool opaqueF(int lx, int fy, int lz) const {   // fine coords, may step into the 1 m layer
    if (g.inBand(fy) || fy == g.bandLo - 1 || fy == g.bandHi) { if (lx < -1 || lx > CHUNK_C || lz < -1 || lz > CHUNK_C) return false; return opaqueId(cellId(fcell(lx, fy, lz))); }
    return opaqueM(floorDiv(lx, VC), floorDiv(fy, VC), floorDiv(lz, VC));
  }
  // ambient occlusion of the 4 corners of a face (0 = darkest .. 3 = open); unit = 1 (fine) or 5 (metre cells)
  template <class Op> uint8_t aoPack(glm::ivec3 p, int d, Op op) const {
    int ua, va; faceAxes(d, ua, va); glm::ivec3 n = DIRS[d], q = p + n; uint8_t pack = 0; int k = 0;
    for (int sv = -1; sv <= 1; sv += 2) for (int su = -1; su <= 1; su += 2) {
      glm::ivec3 du(0), dv(0); du[ua] = su; dv[va] = sv;
      bool s1 = op(q + du), s2 = op(q + dv), cc = op(q + du + dv);
      int ao = (s1 && s2) ? 0 : 3 - (int)s1 - (int)s2 - (int)cc; pack |= (uint8_t)(ao << (k * 2)); k++; }
    return pack;   // corners: (-u,-v), (+u,-v), (-u,+v), (+u,+v)
  }
  void quad(int d, glm::vec3 base, float du, float dv, int ua, int va, const BlockType& bt, uint8_t aoP, float u0, float v0, float uScale, float vScale, int id) {
    glm::ivec3 n = DIRS[d]; int face = d == 2 ? 0 : d == 3 ? 2 : 1; glm::vec3 col = bt.color[face]; int tile = bt.tile[face];
    glm::vec3 eu(0), ev(0); eu[ua] = du; ev[va] = dv;
    glm::vec3 P[4] = {base, base + eu, base + ev, base + eu + ev}; float U[4] = {u0, u0 + du * uScale, u0, u0 + du * uScale}, V[4] = {v0, v0, v0 + dv * vScale, v0 + dv * vScale};
    uint8_t ao[4] = {(uint8_t)(aoP & 3), (uint8_t)((aoP >> 2) & 3), (uint8_t)((aoP >> 4) & 3), (uint8_t)((aoP >> 6) & 3)};
    uint32_t b = (uint32_t)out.v.size();
    for (int k = 0; k < 4; k++) { VVert v; v.x = P[k].x; v.y = P[k].y; v.z = P[k].z; v.u = U[k]; v.v = V[k];
      v.r = (uint8_t)std::lround(glm::clamp(col.r, 0.f, 1.f) * 255); v.g = (uint8_t)std::lround(glm::clamp(col.g, 0.f, 1.f) * 255); v.b = (uint8_t)std::lround(glm::clamp(col.b, 0.f, 1.f) * 255);
      v.ao = (uint8_t)(ao[k] * 85); v.n = (uint8_t)d; uint16_t t = tile < 0 ? 0xffff : (uint16_t)tile; v.tlo = (uint8_t)(t & 255); v.thi = (uint8_t)(t >> 8); v.flags = 0;
      out.v.push_back(v); out.bmin = glm::min(out.bmin, P[k]); out.bmax = glm::max(out.bmax, P[k]); }
    glm::vec3 cr = glm::cross(eu, ev); bool flipWind = glm::dot(cr, glm::vec3(n)) < 0;
    bool flipDiag = ao[0] + ao[3] < ao[1] + ao[2];
    uint32_t i0, i1, i2, i3, i4, i5;
    if (!flipDiag) { i0 = 0; i1 = 1; i2 = 3; i3 = 0; i4 = 3; i5 = 2; } else { i0 = 0; i1 = 1; i2 = 2; i3 = 1; i4 = 3; i5 = 2; }
    uint32_t tri[6] = {i0, i1, i2, i3, i4, i5}; if (flipWind) { std::swap(tri[1], tri[2]); std::swap(tri[4], tri[5]); }
    for (uint32_t t : tri) out.i.push_back(b + t);
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
  void coarsePass() {
    for (int d = 0; d < 6; d++) {
      int ax = d / 2, ua, va; faceAxes(d, ua, va); glm::ivec3 n = DIRS[d];
      int dimA = ax == 1 ? g.H : CHUNK_M, dimU = ua == 1 ? g.H : CHUNK_M, dimV = va == 1 ? g.H : CHUNK_M;
      std::vector<uint64_t> mask((size_t)dimU * dimV);
      for (int s = 0; s < dimA; s++) {
        std::fill(mask.begin(), mask.end(), 0); bool any = false;
        for (int v = 0; v < dimV; v++) for (int u = 0; u < dimU; u++) {
          glm::ivec3 p(0); p[ax] = s; p[ua] = u; p[va] = v; p.y += g.minY;
          if (g.metreInBand(p.y)) continue; uint16_t id = C[((size_t)(p.y - cy0) * CZ + (p.z + 1)) * CX + (p.x + 1)]; if (!id) continue;
          glm::ivec3 q = p + n; if (g.metreInBand(q.y)) continue;   // that face is made by the near layer pass
          if (opaqueM(q.x, q.y, q.z)) continue;
          uint8_t ao = aoPack(p, d, [&](glm::ivec3 c) { return opaqueM(c.x, c.y, c.z); });
          mask[(size_t)v * dimU + u] = (uint64_t)id | ((uint64_t)ao << 16) | (1ull << 40); any = true;
        }
        if (!any) continue;
        greedy(mask, dimU, dimV, [&](int u, int v, int w, int h, uint64_t k) {
          int id = (int)(k & 0xffff); uint8_t ao = (uint8_t)((k >> 16) & 255);
          glm::vec3 base(0); base[ax] = (float)(s + (n[ax] > 0 ? 1 : 0)); base[ua] = (float)u; base[va] = (float)v; base.y += (float)g.minY;   // y is always one of the three
          quad(d, base, (float)w, (float)h, ua, va, typeOf(T, id), ao, (float)u, (float)v + (va == 1 ? g.minY : 0), 1.f, 1.f, id);
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
        for (int v = 0; v < dimV; v++) for (int u = 0; u < dimU; u++) {
          glm::ivec3 p(0); p[ax] = s; p[ua] = u + offU; p[va] = v + offV;
          Cell c = fcell(p.x, p.y, p.z); if (!c) continue;
          glm::ivec3 q = p + n; if (opaqueF(q.x, q.y, q.z)) continue;
          int id = cellId(c), sz = cellSize(c); glm::ivec3 off = cellOffset(c);
          // the block's grid phase along u and v (in world cells) so merged faces keep one texture per block
          int gx = p.x + S.cx * CHUNK_C, gz = p.z + S.cz * CHUNK_C; glm::ivec3 gp(gx, p.y, gz), org = gp - off;
          int phU = posMod(org[ua], sz), phV = posMod(org[va], sz);
          uint8_t ao = aoPack(p, d, [&](glm::ivec3 x) { return opaqueF(x.x, x.y, x.z); });
          mask[(size_t)v * dimU + u] = (uint64_t)id | ((uint64_t)(sz - 1) << 16) | ((uint64_t)phU << 19) | ((uint64_t)phV << 22) | ((uint64_t)ao << 25) | (1ull << 40); any = true;
        }
        if (!any) continue;
        greedy(mask, dimU, dimV, [&](int u, int v, int w, int h, uint64_t k) {
          int id = (int)(k & 0xffff), sz = (int)((k >> 16) & 7) + 1, phU = (int)((k >> 19) & 7), phV = (int)((k >> 22) & 7); uint8_t ao = (uint8_t)((k >> 25) & 255);
          glm::ivec3 cp(0); cp[ax] = s + (n[ax] > 0 ? 1 : 0); cp[ua] = u + offU; cp[va] = v + offV;
          glm::vec3 base = glm::vec3(cp) / (float)VC;
          // texture coordinates in block units: whole numbers on block edges
          glm::ivec3 gc(cp.x + S.cx * CHUNK_C, cp.y, cp.z + S.cz * CHUNK_C);
          float u0 = (float)(posMod(gc[ua] - phU, sz * 4096) ) / sz, v0 = (float)(posMod(gc[va] - phV, sz * 4096)) / sz;
          quad(d, base, (float)w / VC, (float)h / VC, ua, va, typeOf(T, id), ao, u0, v0, (float)VC / sz, (float)VC / sz, id);
        });
      }
    }
  }
};
static MeshOut meshChunk(const Geo& g, const BlockTable& T, const Snapshot& S) {
  Mesher m(g, T, S); m.build(); m.coarsePass(); m.finePass(); return std::move(m.out);
}

/* ------------------------------------------------------------------ the world */
struct Chunk {
  int cx = 0, cz = 0; ChunkData data; bool generated = false, genQueued = false, edited = false, meshing = false;
  int version = 0, meshedVersion = -1; int genGen = -1;
  unsigned vao = 0, vbo = 0, ebo = 0; int count = 0; glm::vec3 bmin{0}, bmax{0}; uint32_t body = NO_BODY; bool hasMesh = false;
};
struct Job { int prio; std::function<void()> fn; };

struct VoxelWorld::Impl {
  VoxelConfig cfg; Geo g; Physics* phys = nullptr;
  std::unordered_map<int64_t, std::unique_ptr<Chunk>> chunks;
  BlockTablePtr table; GeneratorFn gen; int genVersion = 0; std::string jsSrc, jsParams; bool jsGen = false;
  // threads
  std::vector<std::thread> workers; std::mutex jm; std::condition_variable jcv; std::deque<Job> jobs; bool quit = false; std::atomic<int> busy{0};
  std::mutex rm; std::vector<std::function<void()>> results; std::vector<std::string> logs;
  glm::vec3 focus{0}; int inflightGen = 0, inflightMesh = 0; double genMs = 0, meshMs = 0; int genCount = 0, meshCount = 0;
  unsigned atlas = 0; int atlasTiles = 0;
  explicit Impl(const VoxelConfig& c) : cfg(c), g(c) {}
  Chunk* get(int cx, int cz) { auto it = chunks.find(ckey(cx, cz)); return it == chunks.end() ? nullptr : it->second.get(); }
  const Chunk* get(int cx, int cz) const { auto it = chunks.find(ckey(cx, cz)); return it == chunks.end() ? nullptr : it->second.get(); }
  void post(int prio, std::function<void()> fn) { { std::lock_guard<std::mutex> l(jm); if (prio > 0) jobs.push_front({prio, std::move(fn)}); else jobs.push_back({prio, std::move(fn)}); } jcv.notify_one(); }
  void result(std::function<void()> fn) { std::lock_guard<std::mutex> l(rm); results.push_back(std::move(fn)); }
  void log(const std::string& s) { std::lock_guard<std::mutex> l(rm); logs.push_back(s); }
  void freeGpu(Chunk& c) { if (c.vao && glDeleteVertexArrays) { glDeleteBuffers(1, &c.vbo); glDeleteBuffers(1, &c.ebo); glDeleteVertexArrays(1, &c.vao); } c.vao = c.vbo = c.ebo = 0; c.count = 0; }
  void freeBody(Chunk& c) { if (c.body != NO_BODY && phys) phys->remove(c.body); c.body = NO_BODY; }
};

static thread_local std::unique_ptr<JsGen> t_js;

VoxelWorld::VoxelWorld(const VoxelConfig& c0, Physics* phys) : cfg_(c0) {
  VoxelConfig& c = cfg_;
  c.minY = std::max(-512, std::min(c.minY, 0)); c.maxY = std::max(c.minY + 8, std::min(c.maxY, 1024));
  c.fineLo = std::max(c.minY, std::min(c.fineLo, c.maxY)); c.fineHi = std::max(c.fineLo, std::min(c.fineHi, c.maxY));
  c.viewDistance = std::max(1, std::min(c.viewDistance, 32));
  d.reset(new Impl(c)); d->phys = phys;
  auto t = std::make_shared<BlockTable>(); t->t.resize(256); t->opaque.assign(256, 1); t->solid.assign(256, 1); d->table = t;
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
}

void VoxelWorld::defineBlock(int id, const BlockType& bt) {
  if (id <= 0 || id > 65535) return;
  auto t = std::make_shared<BlockTable>(*d->table);
  if ((int)t->t.size() <= id) { size_t n = (size_t)id + 1; t->t.resize(n); t->opaque.resize(n, 1); t->solid.resize(n, 1); }
  t->t[id] = bt; t->t[id].defined = true; t->opaque[id] = bt.opaque; t->solid[id] = bt.solid; d->table = t;
  for (auto& [k, c] : d->chunks) c->version++;   // colours change: rebuild
}
const BlockType& VoxelWorld::block(int id) const { return typeOf(*d->table, id); }

void VoxelWorld::setGenerator(GeneratorFn gfn) {
  d->gen = std::move(gfn); d->jsGen = false; d->genVersion++;
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
  setGenerator([D, ver, src2, par2, seed](ChunkWriter& w) {
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
bool VoxelWorld::setAtlas(const std::string& path, int tilesPerRow) {
  if (!glGenTextures) return false;
  int w = 0, h = 0, n = 0; unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
  if (!px) { d->log("error: voxels.setAtlas: can't read " + path + (stbi_failure_reason() ? std::string(" (") + stbi_failure_reason() + ")" : "")); return false; }
  if (!d->atlas) glGenTextures(1, &d->atlas);
  glBindTexture(GL_TEXTURE_2D, d->atlas); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  stbi_image_free(px); d->atlasTiles = std::max(1, tilesPerRow); return true;
}

std::vector<std::string> VoxelWorld::takeLogs() { std::lock_guard<std::mutex> l(d->rm); auto r = std::move(d->logs); d->logs.clear(); return r; }

/* ---- streaming */
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
    c->genQueued = true; D.inflightGen++; int cx = w.cx, cz = w.cz, ver = D.genVersion; GeneratorFn gen = D.gen; Geo g = D.g; Impl* P = &D;
    D.post(0, [P, cx, cz, ver, gen, g] {
      auto t0 = std::chrono::steady_clock::now(); ChunkData cd = newChunkData(g); GenWriter w(cd, g, cx, cz);
      try { if (gen) gen(w); } catch (const std::exception& e) { P->log(std::string("error: voxels generator: ") + e.what()); }
      compressBricks(cd, g);
      double ms = msSince(t0);
      P->result([P, cx, cz, ver, cd = std::move(cd), ms]() mutable {
        P->inflightGen--; P->genMs += ms; P->genCount++;
        Chunk* c = P->get(cx, cz); if (!c || ver != P->genVersion) return;
        c->data = std::move(cd); c->generated = true; c->genQueued = false; c->version++;
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
        if (glGenBuffers) {
          if (out->i.empty()) P->freeGpu(*c);
          else {
            if (!c->vao) { glGenVertexArrays(1, &c->vao); glGenBuffers(1, &c->vbo); glGenBuffers(1, &c->ebo);
              glBindVertexArray(c->vao); glBindBuffer(GL_ARRAY_BUFFER, c->vbo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c->ebo);
              glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VVert), (void*)0);
              glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(VVert), (void*)12);
              glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(VVert), (void*)20);
              glEnableVertexAttribArray(3); glVertexAttribIPointer(3, 4, GL_UNSIGNED_BYTE, sizeof(VVert), (void*)24);
              glBindVertexArray(0); }
            glBindVertexArray(c->vao); glBindBuffer(GL_ARRAY_BUFFER, c->vbo); glBufferData(GL_ARRAY_BUFFER, out->v.size() * sizeof(VVert), out->v.data(), GL_DYNAMIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c->ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, out->i.size() * 4, out->i.data(), GL_DYNAMIC_DRAW); glBindVertexArray(0);
            c->count = (int)out->i.size();
          }
        } else c->count = (int)out->i.size();
        c->bmin = out->bmin; c->bmax = out->bmax; c->hasMesh = true;
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
  for (auto& [k, cp] : d->chunks) { const Chunk& c = *cp; if (!c.vao || !c.count) continue;
    glm::vec3 org((float)(c.cx * CHUNK_M), 0, (float)(c.cz * CHUNK_M)), mn = org + c.bmin, mx = org + c.bmax;
    // frustum: skip the chunk when all 8 corners are outside one plane
    bool out = false; for (int pl = 0; pl < 6 && !out; pl++) { int ax = pl / 2; float sg = pl % 2 ? -1.f : 1.f; int inside = 0;
      for (int i = 0; i < 8; i++) { glm::vec4 q = vp * glm::vec4((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z, 1); if (sg * q[ax] <= q.w) { inside = 1; break; } }
      out = !inside; }
    // chunks outside the view still cast shadows into it (a hill behind the camera)
    if (out && glm::length(glm::vec2(glm::max(mn.x - cam.pos.x, glm::max(0.f, cam.pos.x - mx.x)), glm::max(mn.z - cam.pos.z, glm::max(0.f, cam.pos.z - mx.z)))) > 70.f) continue;
    VoxelDraw v; v.vao = c.vao; v.count = c.count; v.offset = org; v.mn = mn; v.mx = mx; v.visible = !out; f.voxels.push_back(v); }
  f.voxelAtlas = d->atlas; f.voxelTiles = d->atlasTiles;
}

/* ---- blocks */
Cell VoxelWorld::cell(int fx, int fy, int fz) const {
  int cx = floorDiv(fx, CHUNK_C), cz = floorDiv(fz, CHUNK_C); const Chunk* c = d->get(cx, cz); if (!c || !c->generated) return 0;
  return readCell(c->data, d->g, fx - cx * CHUNK_C, fy, fz - cz * CHUNK_C);
}
static glm::ivec3 toCell(const glm::vec3& p) { return {(int)std::floor(p.x * VC + 1e-3f), (int)std::floor(p.y * VC + 1e-3f), (int)std::floor(p.z * VC + 1e-3f)}; }
BlockInfo VoxelWorld::getBlock(const glm::vec3& p) const {
  glm::ivec3 f = {(int)std::floor(p.x * VC), (int)std::floor(p.y * VC), (int)std::floor(p.z * VC)}; Cell c = cell(f.x, f.y, f.z); BlockInfo b; if (!c) return b;
  b.id = cellId(c); b.size = cellSize(c); b.origin = f - cellOffset(c); return b;
}
bool VoxelWorld::solidAt(const glm::vec3& p) const { BlockInfo b = getBlock(p); return b.id && block(b.id).solid; }

// write one cell (copy-on-write: meshes being built on other threads keep their copy)
static void writeCell(VoxelWorld::Impl& D, int fx, int fy, int fz, Cell v) {
  int cx = floorDiv(fx, CHUNK_C), cz = floorDiv(fz, CHUNK_C); Chunk* c = D.get(cx, cz); if (!c || !c->generated) return;
  int lx = fx - cx * CHUNK_C, lz = fz - cz * CHUNK_C; const Geo& g = D.g;
  if (g.inBand(fy)) { int by = fy - g.bandLo, bi = g.brickIdx(lx, by, lz); auto& b = c->data.bricks[bi];
    if (!b) { int f = (*c->data.fill)[bi];
      if (f) { b = latticeBrick(g, bi, f); if (c->data.fill.use_count() > 1) c->data.fill = std::make_shared<std::vector<uint16_t>>(*c->data.fill); (*c->data.fill)[bi] = 0; }
      else { if (!v) return; b = std::make_shared<Brick>(); std::memset(b->c, 0, sizeof b->c); } }
    else if (b.use_count() > 1) b = std::make_shared<Brick>(*b);
    b->c[Geo::inBrick(lx, by, lz)] = v; }
  else { int my = floorDiv(fy, VC); if (my < g.minY || my >= g.maxY) return; if (c->data.coarse.use_count() > 1) c->data.coarse = std::make_shared<std::vector<uint16_t>>(*c->data.coarse);
    (*c->data.coarse)[g.coarseIdx(lx / VC, my, lz / VC)] = (uint16_t)cellId(v); }
  c->edited = true;
}
static void touch(VoxelWorld::Impl& D, glm::ivec3 lo, glm::ivec3 hi) {   // cells changed in [lo, hi): rebuild those chunks and neighbours that see them
  int m = VC + 1;
  for (int cx = floorDiv(lo.x - m, CHUNK_C); cx <= floorDiv(hi.x - 1 + m, CHUNK_C); cx++) for (int cz = floorDiv(lo.z - m, CHUNK_C); cz <= floorDiv(hi.z - 1 + m, CHUNK_C); cz++)
    if (Chunk* c = D.get(cx, cz)) c->version++;
}
static bool loadedBox(const VoxelWorld::Impl& D, glm::ivec3 lo, glm::ivec3 hi) {
  for (int cx = floorDiv(lo.x, CHUNK_C); cx <= floorDiv(hi.x - 1, CHUNK_C); cx++) for (int cz = floorDiv(lo.z, CHUNK_C); cz <= floorDiv(hi.z - 1, CHUNK_C); cz++) {
    const Chunk* c = D.get(cx, cz); if (!c || !c->generated) return false; }
  return true;
}
static void eraseBlockAt(VoxelWorld& W, VoxelWorld::Impl& D, glm::ivec3 f) {
  Cell c = W.cell(f.x, f.y, f.z); if (!c) return;
  if (!D.g.inBand(f.y)) { writeCell(D, f.x, f.y, f.z, 0); touch(D, f, f + 1); return; }
  int s = cellSize(c); glm::ivec3 o = f - cellOffset(c);
  for (int a = 0; a < s; a++) for (int b = 0; b < s; b++) for (int e = 0; e < s; e++) { glm::ivec3 q = o + glm::ivec3(a, b, e); Cell x = W.cell(q.x, q.y, q.z); if (x && cellId(x) == cellId(c) && q - cellOffset(x) == o) writeCell(D, q.x, q.y, q.z, 0); }
  touch(D, o, o + s);
}
bool VoxelWorld::canPlace(const glm::vec3& corner, float size) const {
  int s = (int)std::lround(size * VC); if (s < 1 || s > VC) return false; glm::ivec3 o = toCell(corner);
  if (!d->g.inBand(o.y) || !d->g.inBand(o.y + s - 1)) { if (s != VC) return false; o = glm::ivec3(floorDiv(o.x, VC), floorDiv(o.y, VC), floorDiv(o.z, VC)) * VC; if (d->g.inBand(o.y)) return false; }
  if (!loadedBox(*d, o, o + s)) return false;
  for (int a = 0; a < s; a++) for (int b = 0; b < s; b++) for (int e = 0; e < s; e++) if (cell(o.x + a, o.y + b, o.z + e)) return false;
  return true;
}
bool VoxelWorld::setBlock(const glm::vec3& corner, int id, float size, std::string* why) {
  auto fail = [&](const char* m) { if (why) *why = m; return false; };
  if (id < 0 || id > 65535) return fail("block id must be 0..65535");
  int s = (int)std::lround(size * VC); if (s < 1 || s > VC || std::abs(size * VC - s) > 0.05f) return fail("size must be 0.2, 0.4, 0.6, 0.8 or 1");
  glm::ivec3 o = toCell(corner); const Geo& g = d->g;
  bool inB = g.inBand(o.y) && g.inBand(o.y + s - 1), outB = !g.inBand(o.y) && !g.inBand(o.y + s - 1);
  if (!inB && !outB) return fail("the block would cross the edge of the 20 cm layer");
  if (outB) { if (s != VC) return fail("outside the 20 cm layer blocks are 1 m"); o = glm::ivec3(floorDiv(o.x, VC), floorDiv(o.y, VC), floorDiv(o.z, VC)) * VC;
    if (o.y / VC < g.minY || o.y / VC >= g.maxY) return fail("outside the world's height"); }
  if (!loadedBox(*d, o, o + s)) return fail("that part of the world is not loaded");
  if (outB) { writeCell(*d, o.x, o.y, o.z, makeCell(id, VC, 0, 0, 0)); touch(*d, o, o + s); return true; }
  for (int a = 0; a < s; a++) for (int b = 0; b < s; b++) for (int e = 0; e < s; e++) { glm::ivec3 q = o + glm::ivec3(a, b, e); if (cell(q.x, q.y, q.z)) eraseBlockAt(*this, *d, q); }
  if (id) for (int a = 0; a < s; a++) for (int b = 0; b < s; b++) for (int e = 0; e < s; e++) writeCell(*d, o.x + a, o.y + b, o.z + e, makeCell(id, s, a, b, e));
  touch(*d, o, o + s); return true;
}
int VoxelWorld::removeBlock(const glm::vec3& p) {
  glm::ivec3 f = {(int)std::floor(p.x * VC), (int)std::floor(p.y * VC), (int)std::floor(p.z * VC)}; Cell c = cell(f.x, f.y, f.z); if (!c) return 0;
  int id = cellId(c); eraseBlockAt(*this, *d, f); return id;
}
VoxelHit VoxelWorld::raycast(const glm::vec3& from, const glm::vec3& dir0, float maxDist) const {
  VoxelHit h; float L = glm::length(dir0); if (L < 1e-6f || maxDist <= 0) return h; glm::vec3 dir = dir0 / L;
  glm::vec3 p = from * (float)VC; glm::ivec3 c = glm::ivec3(glm::floor(p)), step; glm::vec3 tMax, tDelta; glm::ivec3 lastN(0);
  for (int i = 0; i < 3; i++) { step[i] = dir[i] > 0 ? 1 : dir[i] < 0 ? -1 : 0;
    tDelta[i] = step[i] ? std::abs(1.f / dir[i]) : 1e30f; float nb = step[i] > 0 ? std::floor(p[i]) + 1 : std::floor(p[i]); tMax[i] = step[i] ? (nb - p[i]) / dir[i] : 1e30f; }
  float maxT = maxDist * VC, t = 0;
  for (int it = 0; it < 100000 && t <= maxT; it++) {
    Cell x = cell(c.x, c.y, c.z);
    if (x) { h.hit = true; h.id = cellId(x); h.size = cellSize(x); h.origin = c - cellOffset(x); h.cell = c; h.normal = lastN; h.distance = t / VC; h.point = from + dir * h.distance; return h; }
    int a = tMax.x < tMax.y ? (tMax.x < tMax.z ? 0 : 2) : (tMax.y < tMax.z ? 1 : 2);
    t = tMax[a]; c[a] += step[a]; tMax[a] += tDelta[a]; lastN = glm::ivec3(0); lastN[a] = -step[a];
  }
  return h;
}
glm::vec3 VoxelWorld::placeCorner(const VoxelHit& h, float size) const {
  int s = std::max(1, std::min(VC, (int)std::lround(size * VC))); glm::ivec3 o;
  // against the face, centred on the hit point along the face, on the 20 cm grid
  glm::vec3 pc = h.point * (float)VC;
  for (int i = 0; i < 3; i++) {
    if (h.normal[i] > 0) o[i] = h.origin[i] + h.size; else if (h.normal[i] < 0) o[i] = h.origin[i] - s;
    else o[i] = (int)std::floor(pc[i] - s * 0.5f + 0.5f);
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
  for (auto& [k, c] : d->chunks) { s.chunks++; if (c->hasMesh) s.meshed++; s.triangles += c->count / 3; for (auto& b : c->data.bricks) s.bricks += b ? 1 : 0; }
  return s;
}
}
