#pragma once
// Block world (BLOCKFALL-style): the ground is split into 16 x 16 m chunks.
//  - the near layer (fineLo..fineHi metres) is made of 20 cm cells; a block there is 20, 40, 60, 80 or 100 cm
//  - above and below it (minY..maxY) the world is made of 1 m blocks (depth, mountains)
// Chunks are generated and turned into meshes on background threads; a chunk is rebuilt only when it changes.
// The player and physics bodies collide with each chunk's triangles (one static body per chunk, no actor per block).
#include <cstdint>
#include <string>
#include <vector>
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

// one 20 cm cell of the near layer: block id, block size and where the block starts (offset of this cell inside it)
using Cell = uint32_t;
inline Cell makeCell(int id, int size, int ox, int oy, int oz) { return id ? (Cell)(id & 0xffff) | (Cell)(size - 1) << 16 | (Cell)ox << 19 | (Cell)oy << 22 | (Cell)oz << 25 : 0; }
inline int cellId(Cell c) { return (int)(c & 0xffff); }
inline int cellSize(Cell c) { return (int)((c >> 16) & 7) + 1; }
inline glm::ivec3 cellOffset(Cell c) { return {(int)((c >> 19) & 7), (int)((c >> 22) & 7), (int)((c >> 25) & 7)}; }

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
};

struct BlockInfo { int id = 0; glm::ivec3 origin{0}; int size = 0; };   // origin and size in cells (fine units)
struct VoxelHit {
  bool hit = false; int id = 0; glm::ivec3 origin{0}; int size = 0;   // the block that was hit (cells)
  glm::ivec3 cell{0}, normal{0};     // the 20 cm cell that was hit and the face it was entered through
  glm::vec3 point{0}; float distance = 0;
};

// what a generator writes into (a chunk being built); coordinates in metres, x and z inside the chunk (0..16), y in the world
class ChunkWriter {
public:
  virtual ~ChunkWriter() = default;
  virtual int cx() const = 0;
  virtual int cz() const = 0;
  virtual void column(int x, int z, int yBottom, int yTop, int id) = 0;     // 1 m blocks from yBottom up to (not including) yTop
  virtual void fill(int x0, int y0, int z0, int x1, int y1, int z1, int id) = 0;   // 1 m blocks in a box (max exclusive)
  virtual bool set(float x, float y, float z, int id, float size) = 0;     // one block (size 0.2 .. 1 m), corner at x, y, z
  virtual int get(float x, float y, float z) const = 0;
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

  // called every frame on the main thread: loads / unloads chunks around `focus`, takes finished work, uploads meshes
  void update(const glm::vec3& focus);
  bool ready(const glm::vec3& p, float radius = 2.f) const;   // the chunks around p are loaded and collide
  void waitIdle(int maxMs = 20000);                         // tests: until everything near the focus is built
  void draw(Frame& f, const Camera& cam) const;

  // blocks (main thread). Positions in metres.
  BlockInfo getBlock(const glm::vec3& p) const;
  bool setBlock(const glm::vec3& corner, int id, float size, std::string* why = nullptr);   // replaces blocks it overlaps
  bool canPlace(const glm::vec3& corner, float size) const;
  int removeBlock(const glm::vec3& p);                      // returns the id that was removed (0 = nothing)
  VoxelHit raycast(const glm::vec3& from, const glm::vec3& dir, float maxDist) const;
  glm::vec3 placeCorner(const VoxelHit& h, float size) const;   // where a block of this size goes against the hit face
  float topY(float x, float z) const;                       // the top of the highest solid block (NaN if not loaded)
  bool solidAt(const glm::vec3& p) const;

  struct Stats { int chunks = 0, meshed = 0, pendingGen = 0, pendingMesh = 0, triangles = 0, bricks = 0; double genMs = 0, meshMs = 0; };
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
}
