#pragma once
// Physics world (Jolt Physics). Bodies are referenced by a plain id; Jolt types stay inside physics.cpp.
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace pge {
enum class Motion { Static, Kinematic, Dynamic };
constexpr uint32_t NO_BODY = 0xffffffffu;
constexpr uint64_t PLAYER_TAG = 1ull << 40;

class Physics {
public:
  Physics();
  ~Physics();
  bool init(float gravityY);
  void shutdown();
  // a body for one of the primitive meshes (Cube, Sphere, ...), scaled; userData is reported in contacts
  uint32_t addMesh(const std::string& mesh, const glm::vec3& scale, const glm::vec3& pos, const glm::quat& rot, Motion m, float mass, uint64_t userData);
  uint32_t addPlayer(const glm::vec3& center);           // upright capsule (1.8 m), rotation locked
  void remove(uint32_t id);
  void setShape(uint32_t id, const std::string& mesh, const glm::vec3& scale);
  void step(float dt);                                   // fixed 60 Hz sub-steps (max 4)
  void transform(uint32_t id, glm::vec3& pos, glm::quat& rot) const;
  glm::vec3 position(uint32_t id) const;
  glm::vec3 velocity(uint32_t id) const;
  void setVelocity(uint32_t id, const glm::vec3& v);
  void addImpulse(uint32_t id, const glm::vec3& j);
  void teleport(uint32_t id, const glm::vec3& pos, const glm::quat& rot);
  void moveKinematic(uint32_t id, const glm::vec3& pos, const glm::quat& rot);   // applied over the next step
  bool rayHit(const glm::vec3& from, const glm::vec3& to, uint32_t ignore) const;
  void setGravity(float y);
  std::vector<std::pair<uint64_t, uint64_t>> takeContacts();   // new contacts since last call (userData pairs)
  int bodyCount() const;
private:
  struct Impl; std::unique_ptr<Impl> d;
};
}
