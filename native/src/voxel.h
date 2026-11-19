#pragma once
// Block world (BLOCKFALL-style): the ground is split into 16 x 16 m chunks.
//  - the near layer (fineLo..fineHi metres) is made of 20 cm cells; a block there is 20, 40, 60, 80 or 100 cm wide,
//    20 - 100 cm tall, can be turned, can have a shape (stairs, half, slope ...), a carved 5 x 5 x 5 form or a model
//  - above and below it (minY..maxY) the world is made of 1 m blocks (depth, mountains)
// Chunks are generated and turned into meshes on background threads; a chunk is rebuilt only when it changes.
// The player and physics bodies collide with each chunk's triangles (one static body per chunk, no actor per block).
// Liquids (water) are drawn as a moving see-through surface, do not collide and can be swum in.
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <unordered_map>
#include <functional>
#include <glm/glm.hpp>
#include "physics.h"

namespace pge {
struct Frame; struct Camera;

constexpr int VC = 5;               // cells per metre (20 cm)
constexpr int CHUNK_M = 16;         // chunk size in metres
constexpr int CHUNK_C = CHUNK_M * VC;   // 80 cells
constexpr int BRICK = 8;            // the near layer is stored in 8x8x8-cell bricks (empty bricks take no memory)
constexpr int BRICKS_XZ = CHUNK_C / BRICK;
constexpr uint64_t VOXEL_TAG = 1ull << 41;   // physics userData of chunk bodies

// one 20 cm cell of the near layer: block id, block width (x, z) and height, where the block starts (offset of this cell
// inside it) and whether the block has extra data (turned, shaped, carved: see BlockExtra)
using Cell = uint32_t;
inline Cell makeCell(int id, int size, int ox, int oy, int oz, int sy = 0, bool ext = false) {
  if (!id) return 0; if (!sy) sy = size;
  return (Cell)(id & 0xffff) | (Cell)(size - 1) << 16 | (Cell)ox << 19 | (Cell)oy << 22 | (Cell)oz << 25 | (Cell)(sy - 1) << 28 | (ext ? 1u << 31 : 0u); }
inline int cellId(Cell c) { return (int)(c & 0xffff); }
inline int cellSize(Cell c) { return (int)((c >> 16) & 7) + 1; }
inline int cellSizeY(Cell c) { return (int)((c >> 28) & 7) + 1; }
inline bool cellExt(Cell c) { return (c >> 31) != 0; }
inline glm::ivec3 cellOffset(Cell c) { return {(int)((c >> 19) & 7), (int)((c >> 22) & 7), (int)((c >> 25) & 7)}; }

// block shapes (the numbers are BLOCKFALL's own "sh" values)
enum BlockShape { SH_Cube = 0, SH_Slope = 1, SH_Half = 2, SH_Hex = 3, SH_Arch = 4, SH_Wedge = 5, SH_Cylinder = 6, SH_Pyramid = 7, SH_Roof = 8, SH_Stairs = 9, SH_Cross = 10, SH_Count };
int shapeFromName(const std::string& s);            // "stairs" -> SH_Stairs, -1 if unknown
const char* shapeName(int sh);

// per placed block: turn (0-3 around Y, + 4 upside down, + 8 on its side, like BLOCKFALL's r), shape, carved form
struct BlockExtra {
  uint8_t rot = 0; uint8_t shape = 0; bool carved = false;
  std::array<uint64_t, 2> carve{{0, 0}};             // 125 bits: sub-cube (i, j, k) of 5 x 5 x 5 is solid when bit i + 5 * (j + 5 * k) is set
  bool any() const { return rot || shape || carved; }
  bool sub(int i, int j, int k) const { int b = i + 5 * (j + 5 * k); return (carve[b >> 6] >> (b & 63)) & 1; }
  void setSub(int i, int j, int k, bool on) { int b = i + 5 * (j + 5 * k); if (on) carve[b >> 6] |= 1ull << (b & 63); else carve[b >> 6] &= ~(1ull << (b & 63)); }
};
struct BlockOpts { int sy = 0; BlockExtra extra; bool onlyEmpty = false; };   // sy in cells (0 = same as the width); extra.shape is shape + 1 (0 = the block's own)
// {height, rotation, shape, carve, onlyEmpty} as JSON (the script's voxels.setBlock options)
bool blockOptsFromJson(const std::string& json, BlockOpts& o, std::string* why);

// a model block: boxes and cylinders in block space (0..1)
struct ModelPart { int kind = 0; glm::vec3 a{0}, b{1}; int axis = 1, sides = 16; glm::vec3 color{1}; int tile = -1; bool hasColor = false; };   // kind 0 box (a..b), 1 cylinder (a = centre of one end, b = (radius, length, 0))

struct VoxelConfig {
  int minY = -32, maxY = 96;        // metres: the world's bottom and top
  int fineLo = -4, fineHi = 8;      // metres: the 20 cm near layer (inside minY..maxY)
  int viewDistance = 6;             // chunks around the player that are drawn
  uint32_t seed = 1337;
  int threads = 0;                  // background threads (0 = automatic)
};

struct BlockType {
  bool defined = false;
  std::string name;
  glm::vec3 color[3] = {glm::vec3(1), glm::vec3(1), glm::vec3(1)};   // top, sides, bottom (display colours)
  int tile[3] = {-1, -1, -1};        // atlas tile for top, sides, bottom (-1 = colour only)
  bool solid = true;                 // collides
  bool opaque = true;                // hides the faces next to it
  bool liquid = false;               // water: see-through moving surface, no collision, swim in it
  int shape = 0;                     // default shape of this block (SH_*)
  std::vector<ModelPart> model;      // a model block (wheels, chairs, engines ...) when not empty
  uint8_t biome = 0;                 // faces coloured by the chunk's biome colour: bit 0 top, 1 sides, 2 bottom
  float vary = 0;                    // every block gets its own shade: 0 .. 0.5 (BLOCKFALL changes each block a little)
  float glow = 0;                    // light of its own (0 .. 2)
  float opacity = 0.75f;             // liquids
  bool cutout = false;               // see-through pixels of its texture are holes (leaves, flowers, glass): on by default for see-through textured blocks
};

struct BlockInfo { int id = 0; glm::ivec3 origin{0}; int size = 0, sizeY = 0; BlockExtra extra; };   // origin and sizes in cells (fine units)
struct VoxelHit {
  bool hit = false; int id = 0; glm::ivec3 origin{0}; int size = 0, sizeY = 0;   // the block that was hit (cells)
  glm::ivec3 cell{0}, normal{0};     // the 20 cm cell that was hit and the face it was entered through
  glm::vec3 point{0}; float distance = 0;
};

// what a generator writes into (a chunk being built); coordinates in metres, x and z inside the chunk (0..16), y in the world.
// Writes may reach into the 8 chunks around it (trees, buildings): they are added to those chunks when they are made.
class ChunkWriter {
public:
  virtual ~ChunkWriter() = default;
  virtual int cx() const = 0;
  virtual int cz() const = 0;
  virtual void column(int x, int z, int yBottom, int yTop, int id) = 0;     // 1 m blocks from yBottom up to (not including) yTop
  virtual void fill(int x0, int y0, int z0, int x1, int y1, int z1, int id) = 0;   // 1 m blocks in a box (max exclusive)
  virtual bool set(float x, float y, float z, int id, float size, const BlockOpts& o) = 0;   // one block (size 0.2 .. 1 m), corner at x, y, z
  bool set(float x, float y, float z, int id, float size) { return set(x, y, z, id, size, BlockOpts()); }
  virtual int get(float x, float y, float z) const = 0;
  virtual void tint(int x, int z, glm::vec3 rgb) = 0;                        // biome colour of a 1 m column (multiplies the block colour)
};
using GeneratorFn = std::function<void(ChunkWriter&)>;   // runs on background threads: must not touch the game

class VoxelWorld {
public:
  VoxelWorld(const VoxelConfig& c, Physics* phys);
  ~VoxelWorld();
  const VoxelConfig& config() const { return cfg_; }

  void defineBlock(int id, const BlockType& t);
  const BlockType& block(int id) const;
  void setGenerator(GeneratorFn g);                 // C++ generator (plugins); replaces the world's chunks
  void setBuiltinGenerator(const std::string& kind, const std::string& paramsJson);   // "flat", "hills"
  void setScriptGenerator(const std::string& source, const std::string& paramsJson);   // JavaScript function source, one engine per thread
  bool setAtlas(const std::string& pngPath, int tilesPerRow);
  bool setAtlasPixels(const uint8_t* rgba, int width, int height, int tilesPerRow, std::string* why = nullptr);   // made by the game's code
  unsigned atlasTexture() const;                    // OpenGL texture (0 = none); for UI icons of blocks
  int atlasTiles() const;
  glm::ivec2 atlasSize() const;

  // called every frame on the main thread: loads / unloads chunks around `focus`, takes finished work, uploads meshes
  void update(const glm::vec3& focus);
  bool ready(const glm::vec3& p, float radius = 2.f) const;   // the chunks around p are loaded and collide
  void waitIdle(int maxMs = 20000);                         // tests: until everything near the focus is built
  void draw(Frame& f, const Camera& cam) const;

  // blocks (main thread). Positions in metres.
  BlockInfo getBlock(const glm::vec3& p) const;
  bool setBlock(const glm::vec3& corner, int id, float size, std::string* why = nullptr) { return setBlock(corner, id, size, BlockOpts(), why); }
  bool setBlock(const glm::vec3& corner, int id, float size, const BlockOpts& o, std::string* why = nullptr);   // replaces blocks it overlaps
  bool canPlace(const glm::vec3& corner, float size, float sizeY = 0) const;
  int removeBlock(const glm::vec3& p);                      // returns the id that was removed (0 = nothing)
  bool carve(const glm::vec3& p, int i, int j, int k, bool solid);   // one 1/5 piece of the block at p (the block becomes carved)
  VoxelHit raycast(const glm::vec3& from, const glm::vec3& dir, float maxDist, bool hitLiquids = false) const;
  glm::vec3 placeCorner(const VoxelHit& h, float size, float sizeY = 0) const;   // where a block of this size goes against the hit face
  float topY(float x, float z) const;                       // the top of the highest solid block (NaN if not loaded)
  bool solidAt(const glm::vec3& p) const;
  int liquidAt(const glm::vec3& p) const;                   // id of the liquid at p (0 = none)

  // the player's changes: saved chunk by chunk (only changed chunks), loaded back over the generated world
  bool save(const std::string& file, std::string* why = nullptr) const;
  bool load(const std::string& file, std::string* why = nullptr);
  int changedChunks() const;

  struct Stats { int chunks = 0, meshed = 0, pendingGen = 0, pendingMesh = 0, triangles = 0, bricks = 0; double genMs = 0, meshMs = 0; size_t memory = 0; };
  Stats stats() const;
  std::vector<std::string> takeLogs();                      // messages from the background threads

  // used by the mesher and the tests
  Cell cell(int fx, int fy, int fz) const;                  // fine (20 cm) coordinates
  struct Impl;
private:
  VoxelConfig cfg_; std::unique_ptr<Impl> d;
};

// noise for generators (seeded, same on every thread)
float simplex2(float x, float y, uint32_t seed);
float simplex3(float x, float y, float z, uint32_t seed);
float fbm2(float x, float y, uint32_t seed, int octaves, float lacunarity, float gain);
// BLOCKFALL's own noise, bit for bit the same as the game's JavaScript (world.js: h2, vn, fbm, h3, vn3, hashf)
namespace bf {
double h2(double x, double y, double s);
double vn(double x, double y, double s);
double fbm(double x, double y, double s);
double h3(double x, double y, double z, double s);
double vn3(double x, double y, double z, double s);
double hashf(double x, double y, double z);
}
}
