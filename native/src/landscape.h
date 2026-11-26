#pragma once
// Landscape: big outdoor ground (like Unreal's Landscape) - a grid of heights you sculpt in the editor (raise, lower, smooth,
// flatten, noise) and paint with up to 4 layers (materials of the project: grass, rock, dirt, sand), blended. It collides,
// characters walk on it (navigation), it is saved in the project.
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace pge {
struct Landscape {
  int res = 128;                 // vertices along each side
  float size = 128;              // metres along each side (centred on the actor)
  std::vector<float> h;          // heights (m), res × res, row by row (z), each row x
  std::vector<uint8_t> w;        // paint: 4 layers' weights per vertex
  std::string layer[4] = {"", "", "", ""}; float tiling[4] = {4, 4, 4, 4};   // the layers' materials; metres a texture covers
  uint64_t serial = 0; int version = 0;                                     // what is drawn / collided is made again when these change
  Landscape();
  void resize(int r, float s);   // flat, all layer 0
  float cell() const { return size / (res - 1); }
  float heightAt(float x, float z) const;   // local (the actor's) space
  glm::vec3 normalAt(float x, float z) const;
  bool raycast(const glm::vec3& o, const glm::vec3& d, float maxT, float& t) const;   // local space
  void changed() { version++; }
  // brushes: centre (local x, z), radius (m), strength; dt = seconds held
  enum Tool { Raise = 0, Lower, Smooth, Flatten, Noise, Paint };
  void brush(Tool tool, float cx, float cz, float radius, float strength, float dt, int layerIndex = 0, float flattenTo = 0);
  void hills(float height, float scale, uint32_t seed);   // rolling hills everywhere
  nlohmann::json toJson() const;
  bool fromJson(const nlohmann::json& j);
};
using LandscapeRef = std::shared_ptr<Landscape>;
struct MeshData; struct GpuMesh;
void landscapeMesh(const Landscape& l, std::vector<float>& pos, std::vector<uint32_t>& idx);   // triangles (local) for physics
const GpuMesh& landscapeGpu(const Landscape& l);     // cached by serial + version
void releaseLandscapes();
}
