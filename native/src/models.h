#pragma once
// Models made by scripts: parts built from boxes (colours, atlas tiles or a block's faces) or from your own vertices,
// put together like a skeleton (each part turns about its pivot and carries its children), drawn with the block world's
// light, shadows and atlas. Creatures (legs and heads that move), vehicles (wheels that turn, doors), held items.
// Particles: small blocks flying, falling, bouncing on blocks, shrinking (pieces of a broken block, smoke, sparks).
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace pge {
class VoxelWorld; struct Frame;

class Models {
public:
  Models(); ~Models();
  void setBlocks(VoxelWorld* v) { vox_ = v; }
  void clear();                                         // a new level

  std::string define(const std::string& name, const nlohmann::json& def);   // "" = ok, else what is wrong
  bool defined(const std::string& name) const { return defs_.count(name) > 0; }
  std::string spawn(const std::string& model, const nlohmann::json& opts, std::string& id);
  std::string set(const std::string& id, const nlohmann::json& o);
  nlohmann::json get(const std::string& id) const;
  void remove(const std::string& id);
  bool exists(const std::string& id) const { return inst_.count(id) > 0; }
  std::vector<std::string> parts(const std::string& id) const;
  glm::mat4 partWorld(const std::string& id, const std::string& part, bool* ok = nullptr) const;   // where a part is now (attach things to a hand)

  std::string emit(const nlohmann::json& o);            // particles
  void clearParticles();
  void update(float dt);                                // particles move; their mesh is made
  void draw(Frame& f) const;
  int particleCount() const { return (int)ps_.size(); }
  int instanceCount() const { return (int)inst_.size(); }
  int drawCount() const;                                // parts drawn (tests)

  struct Gpu { unsigned vao = 0, vbo = 0, ebo = 0; int count = 0; glm::vec3 mn{0}, mx{0}; };
private:
  struct Part { std::string name; int parent = -1; glm::vec3 pivot{0}; glm::vec3 rot{0}; Gpu gpu; };
  struct Def { std::vector<Part> parts; std::map<std::string, int> byName; ~Def(); };
  struct PartPose { glm::vec3 p{0}, r{0}, s{1}; bool visible = true; };
  struct Inst { std::string model; std::shared_ptr<Def> def; glm::vec3 p{0}, r{0}, s{1}, tint{1}; bool visible = true, shadow = true; std::vector<PartPose> pose; };
  struct Particle { glm::vec3 p, v, col; float size, life, age, spin, ang, bounce, drag, grav; int tile, glow; bool collide, shrink, rest; glm::vec3 axis; };
  std::map<std::string, std::shared_ptr<Def>> defs_;
  std::map<std::string, Inst> inst_;
  std::vector<Particle> ps_; Gpu pgpu_; uint32_t rng_ = 0x9e3779b9u; int nextId_ = 0;
  VoxelWorld* vox_ = nullptr;
  float rnd();
  glm::mat4 partMatrix(const Inst& in, int i) const;
};
glm::mat4 eulerXYZ(const glm::vec3& r);   // like three.js (rotation.x, .y, .z in radians, order XYZ)
}
