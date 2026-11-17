#include "physics.h"
#include "mesh.h"
#include "log.h"
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <mutex>
#include <thread>
#include <algorithm>

using namespace JPH;
namespace pge {

namespace Layers { static constexpr ObjectLayer STATIC = 0, MOVING = 1, COUNT = 2; }
namespace BPL { static constexpr BroadPhaseLayer STATIC(0), MOVING(1); static constexpr uint COUNT = 2; }

class BPLayers final : public BroadPhaseLayerInterface {
public:
  uint GetNumBroadPhaseLayers() const override { return BPL::COUNT; }
  BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer l) const override { return l == Layers::STATIC ? BPL::STATIC : BPL::MOVING; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char* GetBroadPhaseLayerName(BroadPhaseLayer l) const override { return l == BPL::STATIC ? "STATIC" : "MOVING"; }
#endif
};
class ObjVsBP final : public ObjectVsBroadPhaseLayerFilter {
public:
  bool ShouldCollide(ObjectLayer a, BroadPhaseLayer b) const override { return a == Layers::MOVING || b == BPL::MOVING; }
};
class ObjPair final : public ObjectLayerPairFilter {
public:
  bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override { return a == Layers::MOVING || b == Layers::MOVING; }
};
class Contacts final : public ContactListener {
public:
  std::mutex m; std::vector<std::pair<uint64_t, uint64_t>> list;
  void OnContactAdded(const Body& a, const Body& b, const ContactManifold&, ContactSettings&) override {
    std::lock_guard<std::mutex> g(m); list.push_back({a.GetUserData(), b.GetUserData()});
  }
};

static Vec3 J(const glm::vec3& v) { return Vec3(v.x, v.y, v.z); }
static Quat J(const glm::quat& q) { return Quat(q.x, q.y, q.z, q.w).Normalized(); }
static glm::vec3 G(Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }

struct Physics::Impl {
  BPLayers bpl; ObjVsBP ovb; ObjPair op; Contacts contacts;
  std::unique_ptr<TempAllocatorImpl> temp; std::unique_ptr<JobSystemThreadPool> jobs; std::unique_ptr<PhysicsSystem> sys;
  float acc = 0; std::vector<BodyID> kinematicMoves;
  struct KTarget { BodyID id; Vec3 p; Quat q; }; std::vector<KTarget> ktargets;
  BodyInterface& bi() { return sys->GetBodyInterface(); }
};

static std::once_flag g_joltOnce;
static void ensureJolt() { std::call_once(g_joltOnce, [] { RegisterDefaultAllocator(); Factory::sInstance = new Factory(); RegisterTypes(); }); }
struct ShapeHandle { RefConst<Shape> s; };
ShapePtr buildTriangleShape(const std::vector<glm::vec3>& v, const std::vector<uint32_t>& idx) {
  ensureJolt(); if (v.empty() || idx.size() < 3) return nullptr;
  VertexList vl; vl.reserve(v.size()); for (auto& p : v) vl.push_back(Float3(p.x, p.y, p.z));
  IndexedTriangleList tl; tl.reserve(idx.size() / 3); for (size_t i = 0; i + 2 < idx.size(); i += 3) tl.push_back(IndexedTriangle(idx[i], idx[i + 1], idx[i + 2], 0));
  MeshShapeSettings ms(std::move(vl), std::move(tl)); auto r = ms.Create(); if (!r.IsValid()) return nullptr;
  auto h = std::make_shared<ShapeHandle>(); h->s = r.Get(); return h;
}
Physics::Physics() : d(new Impl) {}
Physics::~Physics() { shutdown(); }

bool Physics::init(float gravityY) {
  ensureJolt();
  d->temp.reset(new TempAllocatorImpl(16 * 1024 * 1024));
  int threads = std::max(1, (int)std::thread::hardware_concurrency() - 1);
  d->jobs.reset(new JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers, threads));
  d->sys.reset(new PhysicsSystem());
  d->sys->Init(16384, 0, 65536, 16384, d->bpl, d->ovb, d->op);
  d->sys->SetContactListener(&d->contacts);
  d->sys->SetGravity(Vec3(0, gravityY, 0));
  return true;
}
void Physics::shutdown() {
  if (!d || !d->sys) return;
  BodyIDVector ids; d->sys->GetBodies(ids);
  for (auto& id : ids) { d->bi().RemoveBody(id); d->bi().DestroyBody(id); }
  d->sys.reset(); d->jobs.reset(); d->temp.reset();
}

static RefConst<Shape> makeShape(const std::string& mesh, glm::vec3 s, Motion m) {
  s = glm::max(glm::abs(s), glm::vec3(1e-3f));
  if (mesh == "Cube" || mesh == "Plane") {
    Vec3 h = mesh == "Plane" ? Vec3(0.5f * s.x, 0.02f, 0.5f * s.z) : Vec3(0.5f * s.x, 0.5f * s.y, 0.5f * s.z);
    float r = std::min(0.05f, h.ReduceMin() * 0.5f);
    return new BoxShape(h, r);
  }
  if (mesh == "Sphere" && std::abs(s.x - s.y) < 1e-3f && std::abs(s.y - s.z) < 1e-3f) return new SphereShape(0.5f * s.x);
  MeshData md = makePrimitive(mesh);
  if (m == Motion::Dynamic) {   // moving bodies need a convex shape
    Array<Vec3> pts; for (auto& v : md.v) pts.push_back(J(v.p * s));
    ConvexHullShapeSettings cs(pts, std::min(0.05f, std::min({s.x, s.y, s.z}) * 0.1f));
    auto r = cs.Create(); if (r.IsValid()) return r.Get();
    logMsg("warning: physics: convex hull failed for " + mesh + ", using a box", LogKind::Warn);
  } else {   // static and script-moved: the exact triangles (a ring can be walked through)
    VertexList vl; IndexedTriangleList tl;
    for (auto& v : md.v) vl.push_back(Float3(v.p.x * s.x, v.p.y * s.y, v.p.z * s.z));
    for (size_t i = 0; i + 2 < md.i.size(); i += 3) tl.push_back(IndexedTriangle(md.i[i], md.i[i + 1], md.i[i + 2], 0));
    MeshShapeSettings ms(vl, tl); auto r = ms.Create(); if (r.IsValid()) return r.Get();
    logMsg("warning: physics: mesh shape failed for " + mesh + ", using a box", LogKind::Warn);
  }
  return new BoxShape(Vec3(0.5f * s.x, 0.5f * s.y, 0.5f * s.z), 0.0f);
}

uint32_t Physics::addMesh(const std::string& mesh, const glm::vec3& scale, const glm::vec3& pos, const glm::quat& rot, Motion m, float mass, uint64_t userData) {
  auto shape = makeShape(mesh, scale, m);
  EMotionType mt = m == Motion::Static ? EMotionType::Static : m == Motion::Kinematic ? EMotionType::Kinematic : EMotionType::Dynamic;
  BodyCreationSettings bs(shape, RVec3(pos.x, pos.y, pos.z), J(rot), mt, m == Motion::Static ? Layers::STATIC : Layers::MOVING);
  bs.mFriction = 0.4f; bs.mRestitution = 0.1f; bs.mUserData = userData;
  if (m == Motion::Dynamic) { bs.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia; bs.mMassPropertiesOverride.mMass = std::max(0.01f, mass); }
  Body* b = d->bi().CreateBody(bs); if (!b) { logMsg("error: physics: too many bodies", LogKind::Error); return NO_BODY; }
  d->bi().AddBody(b->GetID(), m == Motion::Static ? EActivation::DontActivate : EActivation::Activate);
  return b->GetID().GetIndexAndSequenceNumber();
}
uint32_t Physics::addStatic(const ShapePtr& shape, const glm::vec3& pos, uint64_t userData) {
  if (!shape || !d->sys) return NO_BODY;
  BodyCreationSettings bs(shape->s, RVec3(pos.x, pos.y, pos.z), Quat::sIdentity(), EMotionType::Static, Layers::STATIC);
  bs.mFriction = 0.5f; bs.mRestitution = 0.f; bs.mUserData = userData;
  Body* b = d->bi().CreateBody(bs); if (!b) { logMsg("error: physics: too many bodies", LogKind::Error); return NO_BODY; }
  d->bi().AddBody(b->GetID(), EActivation::DontActivate); return b->GetID().GetIndexAndSequenceNumber();
}
void Physics::setStaticShape(uint32_t id, const ShapePtr& shape) { if (id != NO_BODY && shape && d->sys) d->bi().SetShape(BodyID(id), shape->s, false, EActivation::DontActivate); }
uint32_t Physics::addPlayer(const glm::vec3& c) {
  BodyCreationSettings bs(new CapsuleShape(0.55f, 0.35f), RVec3(c.x, c.y, c.z), Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING);
  bs.mAllowedDOFs = EAllowedDOFs::TranslationX | EAllowedDOFs::TranslationY | EAllowedDOFs::TranslationZ;
  bs.mFriction = 0; bs.mRestitution = 0; bs.mAllowSleeping = false; bs.mLinearDamping = 0.05f; bs.mUserData = PLAYER_TAG;
  bs.mMotionQuality = EMotionQuality::LinearCast;   // no tunnelling through thin floors when falling fast
  bs.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia; bs.mMassPropertiesOverride.mMass = 70;
  Body* b = d->bi().CreateBody(bs); if (!b) return NO_BODY;
  d->bi().AddBody(b->GetID(), EActivation::Activate); return b->GetID().GetIndexAndSequenceNumber();
}
void Physics::remove(uint32_t id) { if (id == NO_BODY || !d->sys) return; BodyID b(id); d->bi().RemoveBody(b); d->bi().DestroyBody(b);
  d->ktargets.erase(std::remove_if(d->ktargets.begin(), d->ktargets.end(), [&](auto& k) { return k.id == b; }), d->ktargets.end()); }
void Physics::setShape(uint32_t id, const std::string& mesh, const glm::vec3& scale) {
  if (id == NO_BODY) return; BodyID b(id); Motion m = d->bi().GetMotionType(b) == EMotionType::Dynamic ? Motion::Dynamic : Motion::Static;
  d->bi().SetShape(b, makeShape(mesh, scale, m), m == Motion::Dynamic, EActivation::Activate);
}
void Physics::step(float dt) {
  d->acc += dt; int n = 0; const float h = 1.f / 60.f;
  while (d->acc >= h && n < 4) { d->acc -= h; n++; }
  if (n == 4) d->acc = 0;
  if (!n) return;
  for (auto& k : d->ktargets) d->bi().MoveKinematic(k.id, RVec3(k.p), k.q, h * n);
  d->ktargets.clear();
  d->sys->Update(h * n, n, d->temp.get(), d->jobs.get());
  // kinematic bodies stop where they were moved to
  BodyIDVector ids; d->sys->GetActiveBodies(EBodyType::RigidBody, ids);
  for (auto& id : ids) if (d->bi().GetMotionType(id) == EMotionType::Kinematic) d->bi().SetLinearAndAngularVelocity(id, Vec3::sZero(), Vec3::sZero());
}
void Physics::transform(uint32_t id, glm::vec3& p, glm::quat& q) const {
  RVec3 pp; Quat qq; d->bi().GetPositionAndRotation(BodyID(id), pp, qq); p = G(Vec3(pp)); q = glm::quat(qq.GetW(), qq.GetX(), qq.GetY(), qq.GetZ());
}
glm::vec3 Physics::position(uint32_t id) const { return G(Vec3(d->bi().GetPosition(BodyID(id)))); }
glm::vec3 Physics::velocity(uint32_t id) const { return id == NO_BODY ? glm::vec3(0) : G(d->bi().GetLinearVelocity(BodyID(id))); }
void Physics::setVelocity(uint32_t id, const glm::vec3& v) { if (id != NO_BODY) { d->bi().SetLinearVelocity(BodyID(id), J(v)); d->bi().ActivateBody(BodyID(id)); } }
void Physics::addImpulse(uint32_t id, const glm::vec3& j) { if (id != NO_BODY) d->bi().AddImpulse(BodyID(id), J(j)); }
void Physics::teleport(uint32_t id, const glm::vec3& p, const glm::quat& q) { if (id != NO_BODY) d->bi().SetPositionAndRotation(BodyID(id), RVec3(p.x, p.y, p.z), J(q), EActivation::Activate); }
void Physics::moveKinematic(uint32_t id, const glm::vec3& p, const glm::quat& q) {
  if (id == NO_BODY) return; BodyID b(id);
  for (auto& k : d->ktargets) if (k.id == b) { k.p = J(p); k.q = J(q); return; }
  d->ktargets.push_back({b, J(p), J(q)});
}
bool Physics::rayHit(const glm::vec3& from, const glm::vec3& to, uint32_t ignore) const {
  RRayCast ray{RVec3(from.x, from.y, from.z), J(to - from)}; RayCastResult hit;
  IgnoreSingleBodyFilter f(BodyID(ignore == NO_BODY ? BodyID::cInvalidBodyID : ignore));
  return d->sys->GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, f);
}
void Physics::setGravity(float y) { d->sys->SetGravity(Vec3(0, y, 0)); }
std::vector<std::pair<uint64_t, uint64_t>> Physics::takeContacts() { std::lock_guard<std::mutex> g(d->contacts.m); auto r = std::move(d->contacts.list); d->contacts.list.clear(); return r; }
int Physics::bodyCount() const { return d->sys ? (int)d->sys->GetNumBodies() : 0; }
}
