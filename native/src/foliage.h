#pragma once
// Foliage (like Unreal's Foliage tool): many copies of meshes (grass, bushes, trees, rocks) painted onto the ground. Each
// kind has its mesh, how dense, sizes, whether it turns with the slope, sways in the wind, collides, and how far it shows.
// Drawn in groups (cells of the world) as instances: one draw for thousands of blades of grass.
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace pge {
struct FoliageType {
  std::string mesh = "Cone", material;
  float density = 0.5f;                  // copies per square metre when painting
  float scaleMin = 0.8f, scaleMax = 1.2f, cullDistance = 150, wind = 0, sink = 0;   // sink: m into the ground
  bool alignToNormal = false, randomYaw = true, collision = false, castShadow = true;
  struct Inst { glm::vec3 p{0}; float yaw = 0, scale = 1; glm::vec3 n{0, 1, 0}; };
  std::vector<Inst> inst;                // world space
  glm::mat4 matrix(const Inst& i) const;
};
struct Foliage {
  std::vector<FoliageType> types; uint64_t serial = 0; int version = 0;
  Foliage();
  void changed() { version++; }
  int count() const;
  // painting: a circle of radius r at c; surface(x, z, &point, &normal) finds the ground under a place
  template <class F> int paint(int type, const glm::vec3& c, float r, float dt, F surface, uint32_t seed);
  int erase(int type, const glm::vec3& c, float r);   // type -1: every kind
  nlohmann::json toJson() const;
  bool fromJson(const nlohmann::json& j);
};
using FoliageRef = std::shared_ptr<Foliage>;

// what a renderer draws: per kind, cells of 32 m with their instances' matrices (cached by serial + version)
struct FoliageCell { int type = 0; glm::vec3 mn{0}, mx{0}, center{0}; std::shared_ptr<std::vector<glm::mat4>> mats; };
const std::vector<FoliageCell>& foliageCells(const Foliage& f);

uint32_t foliageRand(uint32_t& s);
template <class F> int Foliage::paint(int type, const glm::vec3& c, float r, float dt, F surface, uint32_t seed) {
  if (type < 0 || type >= (int)types.size()) return 0; FoliageType& t = types[type];
  float area = 3.14159f * r * r, want = t.density * area * std::min(dt, 0.1f) * 4;   // fills a circle in about a quarter of a second
  float spacing = 0.7f / std::sqrt(std::max(t.density, 1e-4f)); int added = 0; uint32_t s = seed;
  int tries = (int)std::ceil(want) + 1;
  for (int k = 0; k < tries && added < want + 1; k++) {
    float a = (foliageRand(s) & 0xffff) / 65535.f * 6.2831853f, d = std::sqrt((foliageRand(s) & 0xffff) / 65535.f) * r;
    glm::vec3 p(c.x + std::cos(a) * d, c.y, c.z + std::sin(a) * d), n(0, 1, 0); if (!surface(p.x, p.z, p, n)) continue;
    bool near = false; for (auto& i : t.inst) { glm::vec3 q = i.p - p; if (q.x * q.x + q.z * q.z < spacing * spacing && std::abs(q.y) < 2) { near = true; break; } } if (near) continue;
    if (t.inst.size() >= 200000) break;
    FoliageType::Inst in; in.p = p - n * t.sink; in.n = n; in.yaw = t.randomYaw ? (foliageRand(s) & 0xffff) / 65535.f * 6.2831853f : 0;
    in.scale = t.scaleMin + (t.scaleMax - t.scaleMin) * ((foliageRand(s) & 0xffff) / 65535.f); t.inst.push_back(in); added++; }
  if (added) changed(); return added;
}
}
