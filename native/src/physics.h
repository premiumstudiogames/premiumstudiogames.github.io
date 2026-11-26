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

// a collision shape built from triangles (any thread), for chunks of the block world
struct ShapeHandle;
using ShapePtr = std::shared_ptr<ShapeHandle>;
ShapePtr buildTriangleShape(const std::vector<glm::vec3>& verts, const std::vector<uint32_t>& idx);

class Physics {
public:
  Physics();
  ~Physics();
  bool init(float gravityY);
  void shutdown();
  // a body for one of the primitive meshes (Cube, Sphere, ...), scaled; userData is reported in contacts
  uint32_t addMesh(const std::string& mesh, const glm::vec3& scale, const glm::vec3& pos, const glm::quat& rot, Motion m, float mass, uint64_t userData);
  uint32_t addPlayer(const glm::vec3& center);           // upright capsule (1.8 m), rotation locked
  uint32_t addStatic(const ShapePtr& shape, const glm::vec3& pos, uint64_t userData);
  void setStaticShape(uint32_t id, const ShapePtr& shape);
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
  // the first surface on the way (staticOnly: the level's still things, not boxes and characters); a ray that starts inside a solid goes out through it
  struct Hit { glm::vec3 point{0}, normal{0, 1, 0}; uint64_t userData = 0; float fraction = 1; uint32_t body = NO_BODY; };
  bool rayCast(const glm::vec3& from, const glm::vec3& to, Hit& out, bool staticOnly, uint32_t ignore = NO_BODY) const;
  bool rayCastIgnoring(const glm::vec3& from, const glm::vec3& to, Hit& out, const std::vector<uint32_t>& ignore) const;   // every body but these
  bool boxOverlaps(const glm::vec3& center, const glm::vec3& half, bool staticOnly) const;   // anything in this box (upright)
  std::vector<uint32_t> sphereBodies(const glm::vec3& center, float radius) const;           // the bodies that touch this sphere
  void optimize();                                       // after adding many still bodies: faster queries
  void setGravity(float y);
  std::vector<std::pair<uint64_t, uint64_t>> takeContacts();   // new contacts since last call (userData pairs)
  int bodyCount() const;
  // more about one body
  uint64_t userData(uint32_t id) const;
  bool dynamic(uint32_t id) const;
  float mass(uint32_t id) const;
  glm::vec3 angularVelocity(uint32_t id) const;
  void setAngularVelocity(uint32_t id, const glm::vec3& w);
  void addImpulseAt(uint32_t id, const glm::vec3& j, const glm::vec3& point);
  void setEnabled(uint32_t id, bool on);                 // out of the world and back (the player sits in a car)
  // a moving (or still) body made of several shapes (an actor and the actors attached to it: welded, like Unreal)
  struct Part { std::string mesh; glm::vec3 scale{1}, pos{0}; glm::quat rot{1, 0, 0, 0}; };
  uint32_t addCompound(const std::vector<Part>& parts, const glm::vec3& pos, const glm::quat& rot, Motion m, float mass, uint64_t userData);
  // a capsule from a to b (ragdoll parts); group: bodies of the same group don't touch each other (0 = none)
  uint32_t addCapsule(const glm::vec3& a, const glm::vec3& b, float radius, float mass, uint64_t userData, uint32_t group, int part);
  uint32_t newGroup();

  // joints (Unreal's Physics Constraint): two bodies (or one and the world) held together
  enum JointType { JFixed = 0, JHinge, JBall, JSlider, JDistance, JCone };
  struct JointDesc {
    int type = JHinge; uint32_t a = NO_BODY, b = NO_BODY;   // b = NO_BODY: the world
    glm::vec3 pivot{0}, axis{0, 1, 0}, normal{1, 0, 0};    // world space: where, the hinge / slide axis, a direction across it
    glm::vec3 pivotB{0};                                    // Distance: the point on b
    bool limits = false; float min = 0, max = 0;            // Hinge degrees, Slider m, Distance m
    float motorSpeed = 0, motorForce = 0;                   // a powered hinge (deg/s) or slider (m/s); 0 force = no motor
    float friction = 0, breakForce = 0;                     // breakForce N (0 = never breaks)
    float swing = 45, twistMin = -30, twistMax = 30;        // Cone (ragdolls): degrees
    float spring = 0;                                       // Distance: springy (Hz, 0 = stiff)
  };
  uint32_t addJoint(const JointDesc& j);
  void removeJoint(uint32_t id);
  void jointMotor(uint32_t id, float speed, float force);   // Hinge deg/s, Slider m/s
  void jointTarget(uint32_t id, float value, float force);  // go to an angle (deg) / position (m)
  float jointValue(uint32_t id) const;                      // Hinge angle (deg), Slider position (m)
  bool jointAlive(uint32_t id) const;
  std::vector<uint32_t> takeBrokenJoints();

  // wheeled vehicles (Jolt's VehicleConstraint): body = a moving body; wheels in the body's own space (m), forward = -Z
  struct WheelDesc { glm::vec3 pos{0}; float radius = 0.4f, width = 0.3f, travel = 0.3f, stiffness = 1.5f, damping = 0.5f, steer = 0, brake = 1500, handbrake = 0; bool drive = false; };
  struct VehicleDesc { std::vector<WheelDesc> wheels; float torque = 500, maxRpm = 6000; glm::vec3 centerOfMass{0, -0.3f, 0}; };
  uint32_t addVehicle(uint32_t body, const VehicleDesc& v);
  void removeVehicle(uint32_t id);
  void vehicleInput(uint32_t id, float forward, float right, float brake, float handbrake);
  struct VehicleState { float speed = 0, rpm = 0; int gear = 0; bool grounded = false; std::vector<glm::mat4> wheels; };   // wheels: a Y cylinder → body space
  VehicleState vehicleState(uint32_t id, bool modelX = false) const;   // modelX: the wheel models' axle is X (else Y)
private:
  struct Impl; std::unique_ptr<Impl> d;
};
}
