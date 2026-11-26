#include "physics.h"
#include "mesh.h"
#include "assets.h"
#include <map>
#include <cstdio>
#include <cstdlib>
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
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
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
static glm::quat fromTo(glm::vec3 a, glm::vec3 b) {   // the turn from direction a to b
  float c = glm::dot(a, b); if (c > 0.99999f) return glm::quat(1, 0, 0, 0);
  if (c < -0.99999f) { glm::vec3 ax = std::abs(a.x) < 0.9f ? glm::cross(a, glm::vec3(1, 0, 0)) : glm::cross(a, glm::vec3(0, 1, 0)); return glm::angleAxis(3.14159265f, glm::normalize(ax)); }
  glm::vec3 x = glm::cross(a, b); return glm::normalize(glm::quat(1 + c, x.x, x.y, x.z));
}

struct Physics::Impl {
  BPLayers bpl; ObjVsBP ovb; ObjPair op; Contacts contacts;
  std::unique_ptr<TempAllocatorImpl> temp; std::unique_ptr<JobSystemThreadPool> jobs; std::unique_ptr<PhysicsSystem> sys;
  float acc = 0; std::vector<BodyID> kinematicMoves;
  struct KTarget { BodyID id; Vec3 p; Quat q; }; std::vector<KTarget> ktargets;
  BodyInterface& bi() { return sys->GetBodyInterface(); }
  struct Joint { Ref<TwoBodyConstraint> c; BodyID a, b; int type = 0; float breakForce = 0; };
  std::map<uint32_t, Joint> joints; uint32_t nextJoint = 1; std::vector<uint32_t> broken;
  struct Vehicle { Ref<VehicleConstraint> c; BodyID body; };
  std::map<uint32_t, Vehicle> vehicles; uint32_t nextVehicle = 1;
  Ref<GroupFilterTable> groups; uint32_t nextGroup = 1;
  void dropFor(BodyID b) {   // joints and vehicles go before their bodies
    for (auto it = joints.begin(); it != joints.end();) { if (it->second.a == b || it->second.b == b) { sys->RemoveConstraint(it->second.c); it = joints.erase(it); } else ++it; }
    for (auto it = vehicles.begin(); it != vehicles.end();) { if (it->second.body == b) { sys->RemoveStepListener(it->second.c); sys->RemoveConstraint(it->second.c); it = vehicles.erase(it); } else ++it; }
  }
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
  for (auto& [k, v] : d->vehicles) { d->sys->RemoveStepListener(v.c); d->sys->RemoveConstraint(v.c); } d->vehicles.clear();
  for (auto& [k, j] : d->joints) d->sys->RemoveConstraint(j.c); d->joints.clear();
  BodyIDVector ids; d->sys->GetBodies(ids);
  for (auto& id : ids) { if (d->bi().IsAdded(id)) d->bi().RemoveBody(id); d->bi().DestroyBody(id); }   // (a body taken out of the world is only destroyed)
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
  const MeshData& md = meshShape(mesh);   // a shape or a model file
  if (md.v.empty()) return new BoxShape(Vec3(0.5f * s.x, 0.5f * s.y, 0.5f * s.z), 0.0f);
  // the same model at the same size: one shape for all of them (a forest of one tree model)
  static std::map<std::string, RefConst<Shape>> cache; char key[160]; std::snprintf(key, sizeof key, "|%d|%.4f|%.4f|%.4f|%zu", (int)(m == Motion::Dynamic), s.x, s.y, s.z, md.v.size());
  std::string k = assetRoot() + "|" + mesh + key; if (isModelFile(mesh)) { auto it = cache.find(k); if (it != cache.end()) return it->second; }
  auto keep = [&](RefConst<Shape> sh) { if (isModelFile(mesh)) { if (cache.size() > 512) cache.clear(); cache[k] = sh; } return sh; };
  if (m == Motion::Dynamic) {   // moving bodies need a convex shape (big models: a spread of their points)
    Array<Vec3> pts; size_t step = std::max<size_t>(1, md.v.size() / 2048); for (size_t i = 0; i < md.v.size(); i += step) pts.push_back(J(md.v[i].p * s));
    ConvexHullShapeSettings cs(pts, std::min(0.05f, std::min({s.x, s.y, s.z}) * 0.1f));
    auto r = cs.Create(); if (r.IsValid()) return keep(r.Get());
    logMsg("warning: physics: convex hull failed for " + mesh + ", using a box", LogKind::Warn);
  } else {   // static and script-moved: the exact triangles (a ring can be walked through)
    VertexList vl; IndexedTriangleList tl;
    for (auto& v : md.v) vl.push_back(Float3(v.p.x * s.x, v.p.y * s.y, v.p.z * s.z));
    for (size_t i = 0; i + 2 < md.i.size(); i += 3) tl.push_back(IndexedTriangle(md.i[i], md.i[i + 1], md.i[i + 2], 0));
    MeshShapeSettings ms(vl, tl); auto r = ms.Create(); if (r.IsValid()) return keep(r.Get());
    logMsg("warning: physics: mesh shape failed for " + mesh + ", using a box", LogKind::Warn);
  }
  glm::vec3 c = (md.bmin + md.bmax) * 0.5f * s, h = glm::max((md.bmax - md.bmin) * 0.5f * s, glm::vec3(0.01f));   // its bounds
  return new RotatedTranslatedShape(J(c), Quat::sIdentity(), new BoxShape(J(h), 0.0f));
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
  bs.mFriction = 0; bs.mRestitution = 0; bs.mAllowSleeping = false; bs.mGravityFactor = 1; bs.mLinearDamping = 0.05f; bs.mUserData = PLAYER_TAG;
  bs.mMotionQuality = EMotionQuality::LinearCast;   // no tunnelling through thin floors when falling fast
  bs.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia; bs.mMassPropertiesOverride.mMass = 70;
  Body* b = d->bi().CreateBody(bs); if (!b) return NO_BODY;
  d->bi().AddBody(b->GetID(), EActivation::Activate); return b->GetID().GetIndexAndSequenceNumber();
}
void Physics::remove(uint32_t id) { if (id == NO_BODY || !d->sys) return; BodyID b(id); d->dropFor(b); if (d->bi().IsAdded(b)) d->bi().RemoveBody(b); d->bi().DestroyBody(b);
  d->ktargets.erase(std::remove_if(d->ktargets.begin(), d->ktargets.end(), [&](auto& k) { return k.id == b; }), d->ktargets.end()); }
void Physics::setShape(uint32_t id, const std::string& mesh, const glm::vec3& scale) {
  if (id == NO_BODY) return; BodyID b(id); Motion m = d->bi().GetMotionType(b) == EMotionType::Dynamic ? Motion::Dynamic : Motion::Static;
  float ms = m == Motion::Dynamic ? mass(id) : 0; RefConst<Shape> sh = makeShape(mesh, scale, m);
  d->bi().SetShape(b, sh, false, EActivation::Activate);
  if (ms > 0) { BodyLockWrite l(d->sys->GetBodyLockInterface(), b); if (l.Succeeded()) { MassProperties mp = sh->GetMassProperties(); mp.ScaleToMass(ms); l.GetBody().GetMotionProperties()->SetMassProperties(EAllowedDOFs::All, mp); } }   // the same mass, a new size
}
void Physics::step(float dt) {
  d->acc += dt; int n = 0; const float h = 1.f / 60.f;
  while (d->acc >= h && n < 4) { d->acc -= h; n++; }
  if (n == 4) d->acc = 0;
  if (!n) return;
  for (auto& k : d->ktargets) d->bi().MoveKinematic(k.id, RVec3(k.p), k.q, h * n);
  d->ktargets.clear();
  d->sys->Update(h * n, n, d->temp.get(), d->jobs.get());
  // joints pulled harder than they hold break (Unreal's Breakable)
  for (auto it = d->joints.begin(); it != d->joints.end();) { auto& j = it->second; float f = 0;
    if (j.breakForce > 0) { TwoBodyConstraint* c = j.c.GetPtr();
      switch (j.type) { case JFixed: f = static_cast<FixedConstraint*>(c)->GetTotalLambdaPosition().Length(); break; case JHinge: f = static_cast<HingeConstraint*>(c)->GetTotalLambdaPosition().Length(); break;
        case JBall: f = static_cast<PointConstraint*>(c)->GetTotalLambdaPosition().Length(); break; case JSlider: f = static_cast<SliderConstraint*>(c)->GetTotalLambdaPosition().Length(); break;
        case JDistance: f = std::abs(static_cast<DistanceConstraint*>(c)->GetTotalLambdaPosition()); break; case JCone: f = static_cast<SwingTwistConstraint*>(c)->GetTotalLambdaPosition().Length(); break; }
      f /= h; }   // impulse of the last sub-step → force
    if (j.breakForce > 0 && f > j.breakForce) { d->sys->RemoveConstraint(j.c); d->broken.push_back(it->first); it = d->joints.erase(it); } else ++it; }
  // kinematic bodies stop where they were moved to
  BodyIDVector ids; d->sys->GetActiveBodies(EBodyType::RigidBody, ids);
  for (auto& id : ids) if (d->bi().GetMotionType(id) == EMotionType::Kinematic) d->bi().SetLinearAndAngularVelocity(id, Vec3::sZero(), Vec3::sZero());
}
void Physics::transform(uint32_t id, glm::vec3& p, glm::quat& q) const {
  RVec3 pp; Quat qq; d->bi().GetPositionAndRotation(BodyID(id), pp, qq); p = G(Vec3(pp)); q = glm::quat(qq.GetW(), qq.GetX(), qq.GetY(), qq.GetZ());
}
glm::vec3 Physics::position(uint32_t id) const { return G(Vec3(d->bi().GetPosition(BodyID(id)))); }
glm::vec3 Physics::velocity(uint32_t id) const { return id == NO_BODY ? glm::vec3(0) : G(d->bi().GetLinearVelocity(BodyID(id))); }
// a body out of the world (setEnabled false) is moved but never woken: waking it would put it back in the simulation half-way
void Physics::setVelocity(uint32_t id, const glm::vec3& v) { if (id == NO_BODY) return; BodyID b(id); if (!d->bi().IsAdded(b)) return; d->bi().SetLinearVelocity(b, J(v)); d->bi().ActivateBody(b); }
void Physics::addImpulse(uint32_t id, const glm::vec3& j) { if (id != NO_BODY && d->bi().IsAdded(BodyID(id)) && dynamic(id)) d->bi().AddImpulse(BodyID(id), J(j)); }
void Physics::teleport(uint32_t id, const glm::vec3& p, const glm::quat& q) { if (id == NO_BODY) return; BodyID b(id); d->bi().SetPositionAndRotation(b, RVec3(p.x, p.y, p.z), J(q), d->bi().IsAdded(b) ? EActivation::Activate : EActivation::DontActivate); }
void Physics::moveKinematic(uint32_t id, const glm::vec3& p, const glm::quat& q) {
  if (id == NO_BODY || !d->bi().IsAdded(BodyID(id))) return; BodyID b(id);
  for (auto& k : d->ktargets) if (k.id == b) { k.p = J(p); k.q = J(q); return; }
  d->ktargets.push_back({b, J(p), J(q)});
}
bool Physics::rayHit(const glm::vec3& from, const glm::vec3& to, uint32_t ignore) const {
  RRayCast ray{RVec3(from.x, from.y, from.z), J(to - from)}; RayCastResult hit;
  IgnoreSingleBodyFilter f(BodyID(ignore == NO_BODY ? BodyID::cInvalidBodyID : ignore));
  return d->sys->GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, f);
}
bool Physics::rayCast(const glm::vec3& from, const glm::vec3& to, Hit& out, bool staticOnly, uint32_t ignore) const {
  if (!d->sys) return false;
  RRayCast ray{RVec3(from.x, from.y, from.z), J(to - from)}; RayCastSettings rs; rs.mTreatConvexAsSolid = false; rs.mBackFaceModeTriangles = EBackFaceMode::IgnoreBackFaces; rs.mBackFaceModeConvex = EBackFaceMode::IgnoreBackFaces;
  ClosestHitCollisionCollector<CastRayCollector> col; IgnoreSingleBodyFilter bf(BodyID(ignore == NO_BODY ? BodyID::cInvalidBodyID : ignore));
  SpecifiedObjectLayerFilter only(Layers::STATIC); ObjectLayerFilter all;
  d->sys->GetNarrowPhaseQuery().CastRay(ray, rs, col, {}, staticOnly ? (const ObjectLayerFilter&)only : all, bf);
  if (!col.HadHit()) return false;
  out.fraction = col.mHit.mFraction; RVec3 p = ray.GetPointOnRay(col.mHit.mFraction); out.point = {(float)p.GetX(), (float)p.GetY(), (float)p.GetZ()};
  BodyLockRead lock(d->sys->GetBodyLockInterface(), col.mHit.mBodyID);
  if (lock.Succeeded()) { const Body& b = lock.GetBody(); out.normal = G(b.GetWorldSpaceSurfaceNormal(col.mHit.mSubShapeID2, p)); out.userData = b.GetUserData();
    if (glm::dot(out.normal, to - from) > 0) out.normal = -out.normal; }
  out.body = col.mHit.mBodyID.GetIndexAndSequenceNumber();
  return true;
}
class IgnoreBodies final : public BodyFilter {
public:
  const std::vector<uint32_t>& v; explicit IgnoreBodies(const std::vector<uint32_t>& x) : v(x) {}
  bool ShouldCollide(const BodyID& b) const override { uint32_t i = b.GetIndexAndSequenceNumber(); for (uint32_t x : v) if (x == i) return false; return true; }
};
bool Physics::rayCastIgnoring(const glm::vec3& from, const glm::vec3& to, Hit& out, const std::vector<uint32_t>& ignore) const {
  if (!d->sys) return false;
  RRayCast ray{RVec3(from.x, from.y, from.z), J(to - from)}; RayCastSettings rs; rs.mTreatConvexAsSolid = false; rs.mBackFaceModeTriangles = EBackFaceMode::IgnoreBackFaces; rs.mBackFaceModeConvex = EBackFaceMode::IgnoreBackFaces;
  ClosestHitCollisionCollector<CastRayCollector> col; IgnoreBodies bf(ignore);
  d->sys->GetNarrowPhaseQuery().CastRay(ray, rs, col, {}, {}, bf);
  if (!col.HadHit()) return false;
  out.fraction = col.mHit.mFraction; RVec3 p = ray.GetPointOnRay(col.mHit.mFraction); out.point = {(float)p.GetX(), (float)p.GetY(), (float)p.GetZ()}; out.body = col.mHit.mBodyID.GetIndexAndSequenceNumber();
  BodyLockRead lock(d->sys->GetBodyLockInterface(), col.mHit.mBodyID);
  if (lock.Succeeded()) { const Body& b = lock.GetBody(); out.normal = G(b.GetWorldSpaceSurfaceNormal(col.mHit.mSubShapeID2, p)); out.userData = b.GetUserData();
    if (glm::dot(out.normal, to - from) > 0) out.normal = -out.normal; }
  return true;
}
std::vector<uint32_t> Physics::sphereBodies(const glm::vec3& c, float r) const {
  std::vector<uint32_t> out; if (!d->sys) return out;
  SphereShape sph(std::max(0.01f, r)); sph.SetEmbedded(); CollideShapeSettings cs; AllHitCollisionCollector<CollideShapeCollector> col;
  d->sys->GetNarrowPhaseQuery().CollideShape(&sph, Vec3::sReplicate(1), RMat44::sTranslation(RVec3(c.x, c.y, c.z)), cs, RVec3::sZero(), col);
  for (auto& h : col.mHits) { uint32_t i = h.mBodyID2.GetIndexAndSequenceNumber(); if (std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i); }
  return out;
}
bool Physics::boxOverlaps(const glm::vec3& c, const glm::vec3& h, bool staticOnly) const {
  if (!d->sys) return false;
  BoxShape box(J(glm::max(h, glm::vec3(0.01f)))); box.SetEmbedded(); CollideShapeSettings cs; cs.mActiveEdgeMode = EActiveEdgeMode::CollideOnlyWithActive; cs.mBackFaceMode = EBackFaceMode::CollideWithBackFaces;
  AnyHitCollisionCollector<CollideShapeCollector> col; SpecifiedObjectLayerFilter only(Layers::STATIC); ObjectLayerFilter all;
  d->sys->GetNarrowPhaseQuery().CollideShape(&box, Vec3::sReplicate(1), RMat44::sTranslation(RVec3(c.x, c.y, c.z)), cs, RVec3::sZero(), col, {}, staticOnly ? (const ObjectLayerFilter&)only : all);
  return col.HadHit();
}
void Physics::optimize() { if (d->sys) d->sys->OptimizeBroadPhase(); }
void Physics::setGravity(float y) { d->sys->SetGravity(Vec3(0, y, 0)); }
std::vector<std::pair<uint64_t, uint64_t>> Physics::takeContacts() { std::lock_guard<std::mutex> g(d->contacts.m); auto r = std::move(d->contacts.list); d->contacts.list.clear(); return r; }
int Physics::bodyCount() const { return d->sys ? (int)d->sys->GetNumBodies() : 0; }

/* ---- one body */
uint64_t Physics::userData(uint32_t id) const { return id == NO_BODY || !d->sys ? 0 : d->bi().GetUserData(BodyID(id)); }
bool Physics::dynamic(uint32_t id) const { return id != NO_BODY && d->sys && d->bi().GetMotionType(BodyID(id)) == EMotionType::Dynamic; }
float Physics::mass(uint32_t id) const {
  if (id == NO_BODY || !d->sys) return 0; BodyLockRead l(d->sys->GetBodyLockInterface(), BodyID(id)); if (!l.Succeeded()) return 0;
  const Body& b = l.GetBody(); if (!b.IsDynamic()) return 0; float im = b.GetMotionProperties()->GetInverseMass(); return im > 0 ? 1 / im : 0;
}
glm::vec3 Physics::angularVelocity(uint32_t id) const { return id == NO_BODY ? glm::vec3(0) : G(d->bi().GetAngularVelocity(BodyID(id))); }
void Physics::setAngularVelocity(uint32_t id, const glm::vec3& w) { if (id == NO_BODY || !d->bi().IsAdded(BodyID(id))) return; d->bi().SetAngularVelocity(BodyID(id), J(w)); d->bi().ActivateBody(BodyID(id)); }
void Physics::addImpulseAt(uint32_t id, const glm::vec3& j, const glm::vec3& p) { if (id != NO_BODY && d->bi().IsAdded(BodyID(id)) && dynamic(id)) d->bi().AddImpulse(BodyID(id), J(j), RVec3(p.x, p.y, p.z)); }
void Physics::setEnabled(uint32_t id, bool on) {
  if (id == NO_BODY || !d->sys) return; BodyID b(id); bool in = d->bi().IsAdded(b);
  if (on && !in) d->bi().AddBody(b, EActivation::Activate); else if (!on && in) d->bi().RemoveBody(b);
}
uint32_t Physics::addCompound(const std::vector<Part>& parts, const glm::vec3& pos, const glm::quat& rot, Motion m, float mass, uint64_t userData) {
  if (parts.empty() || !d->sys) return NO_BODY;
  StaticCompoundShapeSettings cs; for (auto& p : parts) cs.AddShape(J(p.pos), J(p.rot), makeShape(p.mesh, p.scale, m));
  auto r = cs.Create(); if (!r.IsValid()) { logMsg("warning: physics: " + std::string(r.GetError().c_str()), LogKind::Warn); return addMesh(parts[0].mesh, parts[0].scale, pos, rot, m, mass, userData); }
  EMotionType mt = m == Motion::Static ? EMotionType::Static : m == Motion::Kinematic ? EMotionType::Kinematic : EMotionType::Dynamic;
  BodyCreationSettings bs(r.Get(), RVec3(pos.x, pos.y, pos.z), J(rot), mt, m == Motion::Static ? Layers::STATIC : Layers::MOVING);
  bs.mFriction = 0.4f; bs.mRestitution = 0.1f; bs.mUserData = userData;
  if (m == Motion::Dynamic) { bs.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia; bs.mMassPropertiesOverride.mMass = std::max(0.01f, mass); }
  Body* b = d->bi().CreateBody(bs); if (!b) return NO_BODY;
  d->bi().AddBody(b->GetID(), m == Motion::Static ? EActivation::DontActivate : EActivation::Activate); return b->GetID().GetIndexAndSequenceNumber();
}
uint32_t Physics::newGroup() { return d->nextGroup++; }
uint32_t Physics::addCapsule(const glm::vec3& a, const glm::vec3& b, float radius, float mass, uint64_t userData, uint32_t group, int part) {
  if (!d->sys) return NO_BODY;
  glm::vec3 ax = b - a; float len = glm::length(ax); radius = std::max(0.02f, radius); float hh = std::max(0.01f, len * 0.5f - radius * 0.5f);
  glm::quat q = len > 1e-5f ? fromTo(glm::vec3(0, 1, 0), ax / len) : glm::quat(1, 0, 0, 0); glm::vec3 c = (a + b) * 0.5f;
  BodyCreationSettings bs(new CapsuleShape(hh, radius), RVec3(c.x, c.y, c.z), J(q), EMotionType::Dynamic, Layers::MOVING);
  bs.mFriction = 0.7f; bs.mRestitution = 0.05f; bs.mUserData = userData; bs.mLinearDamping = 0.1f; bs.mAngularDamping = 0.6f; bs.mMotionQuality = EMotionQuality::LinearCast;
  bs.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia; bs.mMassPropertiesOverride.mMass = std::max(0.2f, mass);
  if (group) { if (!d->groups) { d->groups = new GroupFilterTable(64); for (uint i = 0; i < 64; i++) for (uint k = i + 1; k < 64; k++) d->groups->DisableCollision(i, k); }
    bs.mCollisionGroup = CollisionGroup(d->groups, group, (CollisionGroup::SubGroupID)std::clamp(part, 0, 63)); }
  Body* bd = d->bi().CreateBody(bs); if (!bd) return NO_BODY;
  d->bi().AddBody(bd->GetID(), EActivation::Activate); return bd->GetID().GetIndexAndSequenceNumber();
}

/* ---- joints */
uint32_t Physics::addJoint(const JointDesc& j) {
  if (!d->sys || j.a == NO_BODY) return NO_BODY;
  Vec3 ax = J(glm::normalize(glm::length(j.axis) > 1e-5f ? j.axis : glm::vec3(0, 1, 0))), nm = J(j.normal);
  if (std::abs(nm.Dot(ax)) > 0.99f || nm.LengthSq() < 1e-8f) nm = ax.GetNormalizedPerpendicular(); nm = (nm - ax * nm.Dot(ax)).Normalized();
  RVec3 P(j.pivot.x, j.pivot.y, j.pivot.z); Ref<TwoBodyConstraintSettings> st;
  switch (j.type) {
    case JFixed: { auto* s = new FixedConstraintSettings; s->mAutoDetectPoint = true; st = s; break; }
    case JBall: { auto* s = new PointConstraintSettings; s->mPoint1 = s->mPoint2 = P; st = s; break; }
    case JSlider: { auto* s = new SliderConstraintSettings; s->mPoint1 = s->mPoint2 = P; s->mSliderAxis1 = s->mSliderAxis2 = ax; s->mNormalAxis1 = s->mNormalAxis2 = nm;
      if (j.limits) { s->mLimitsMin = std::min(j.min, j.max); s->mLimitsMax = std::max(j.min, j.max); } s->mMaxFrictionForce = j.friction;
      if (j.motorForce > 0) s->mMotorSettings.SetForceLimit(j.motorForce); st = s; break; }
    case JDistance: { auto* s = new DistanceConstraintSettings; s->mPoint1 = P; s->mPoint2 = RVec3(j.pivotB.x, j.pivotB.y, j.pivotB.z);
      if (j.limits) { s->mMinDistance = std::min(j.min, j.max); s->mMaxDistance = std::max(j.min, j.max); } if (j.spring > 0) { s->mLimitsSpringSettings.mFrequency = j.spring; s->mLimitsSpringSettings.mDamping = 0.5f; } st = s; break; }
    case JCone: { auto* s = new SwingTwistConstraintSettings; s->mPosition1 = s->mPosition2 = P; s->mTwistAxis1 = s->mTwistAxis2 = ax; s->mPlaneAxis1 = s->mPlaneAxis2 = nm;
      s->mNormalHalfConeAngle = s->mPlaneHalfConeAngle = glm::radians(std::clamp(j.swing, 0.f, 179.f)); s->mTwistMinAngle = glm::radians(std::clamp(std::min(j.twistMin, j.twistMax), -179.f, 179.f));
      s->mTwistMaxAngle = glm::radians(std::clamp(std::max(j.twistMin, j.twistMax), -179.f, 179.f)); s->mMaxFrictionTorque = j.friction; st = s; break; }
    default: { auto* s = new HingeConstraintSettings; s->mPoint1 = s->mPoint2 = P; s->mHingeAxis1 = s->mHingeAxis2 = ax; s->mNormalAxis1 = s->mNormalAxis2 = nm;
      if (j.limits) { s->mLimitsMin = glm::radians(std::clamp(std::min(j.min, j.max), -180.f, 0.f)); s->mLimitsMax = glm::radians(std::clamp(std::max(j.min, j.max), 0.f, 180.f)); }
      s->mMaxFrictionTorque = j.friction; if (j.motorForce > 0) s->mMotorSettings.SetTorqueLimit(j.motorForce); st = s; break; }
  }
  BodyID B = j.b == NO_BODY ? BodyID() : BodyID(j.b);
  TwoBodyConstraint* c = d->bi().CreateConstraint(st, B, BodyID(j.a)); if (!c) return NO_BODY;   // body 2 = a: the angle / position is a's, against b (or the world)
  d->sys->AddConstraint(c); d->bi().ActivateConstraint(c);
  uint32_t id = d->nextJoint++; d->joints[id] = {c, BodyID(j.a), B, j.type, j.breakForce};
  if (j.motorForce > 0 && j.type == JHinge) { auto* h = static_cast<HingeConstraint*>(c); h->SetMotorState(EMotorState::Velocity); h->SetTargetAngularVelocity(glm::radians(j.motorSpeed)); }
  if (j.motorForce > 0 && j.type == JSlider) { auto* h = static_cast<SliderConstraint*>(c); h->SetMotorState(EMotorState::Velocity); h->SetTargetVelocity(j.motorSpeed); }
  return id;
}
void Physics::removeJoint(uint32_t id) { auto it = d->joints.find(id); if (it == d->joints.end()) return; d->sys->RemoveConstraint(it->second.c); d->bi().ActivateBody(it->second.a); d->joints.erase(it); }
bool Physics::jointAlive(uint32_t id) const { return d->joints.count(id) > 0; }
void Physics::jointMotor(uint32_t id, float speed, float force) {
  auto it = d->joints.find(id); if (it == d->joints.end()) return; auto& j = it->second;
  if (j.type == JHinge) { auto* h = static_cast<HingeConstraint*>(j.c.GetPtr()); h->GetMotorSettings().SetTorqueLimit(std::max(0.f, force)); h->SetMotorState(force > 0 ? EMotorState::Velocity : EMotorState::Off); h->SetTargetAngularVelocity(glm::radians(speed)); }
  else if (j.type == JSlider) { auto* h = static_cast<SliderConstraint*>(j.c.GetPtr()); h->GetMotorSettings().SetForceLimit(std::max(0.f, force)); h->SetMotorState(force > 0 ? EMotorState::Velocity : EMotorState::Off); h->SetTargetVelocity(speed); }
  d->bi().ActivateConstraint(j.c);
}
void Physics::jointTarget(uint32_t id, float v, float force) {
  auto it = d->joints.find(id); if (it == d->joints.end()) return; auto& j = it->second; force = force > 0 ? force : 1e6f;
  if (j.type == JHinge) { auto* h = static_cast<HingeConstraint*>(j.c.GetPtr()); h->GetMotorSettings().SetTorqueLimit(force); h->SetMotorState(EMotorState::Position); h->SetTargetAngle(glm::radians(v)); }
  else if (j.type == JSlider) { auto* h = static_cast<SliderConstraint*>(j.c.GetPtr()); h->GetMotorSettings().SetForceLimit(force); h->SetMotorState(EMotorState::Position); h->SetTargetPosition(v); }
  d->bi().ActivateConstraint(j.c);
}
float Physics::jointValue(uint32_t id) const {
  auto it = d->joints.find(id); if (it == d->joints.end()) return 0; auto& j = it->second;
  if (j.type == JHinge) return glm::degrees(static_cast<HingeConstraint*>(j.c.GetPtr())->GetCurrentAngle());
  if (j.type == JSlider) return static_cast<SliderConstraint*>(j.c.GetPtr())->GetCurrentPosition();
  return 0;
}
std::vector<uint32_t> Physics::takeBrokenJoints() { auto r = std::move(d->broken); d->broken.clear(); return r; }

/* ---- vehicles */
uint32_t Physics::addVehicle(uint32_t body, const VehicleDesc& v) {
  if (!d->sys || body == NO_BODY || v.wheels.empty()) return NO_BODY; BodyID b(body);
  // a lower centre of mass: it doesn't roll over in every turn
  { float m = mass(body); RefConst<Shape> sh = d->bi().GetShape(b);   // (its mass stays what it was set to, not what the new shape weighs)
    if (sh) { RefConst<Shape> off = new OffsetCenterOfMassShape(sh, J(v.centerOfMass)); d->bi().SetShape(b, off, false, EActivation::Activate);
      BodyLockWrite l(d->sys->GetBodyLockInterface(), b); if (l.Succeeded() && m > 0) { MassProperties mp = off->GetMassProperties(); mp.ScaleToMass(m); l.GetBody().GetMotionProperties()->SetMassProperties(EAllowedDOFs::All, mp); } } }
  VehicleConstraintSettings vs; vs.mUp = Vec3::sAxisY(); vs.mForward = -Vec3::sAxisZ(); vs.mMaxPitchRollAngle = glm::radians(80.f);
  for (auto& w : v.wheels) { auto* ws = new WheelSettingsWV; ws->mPosition = J(w.pos); ws->mSuspensionDirection = -Vec3::sAxisY(); ws->mSteeringAxis = Vec3::sAxisY(); ws->mWheelUp = Vec3::sAxisY(); ws->mWheelForward = -Vec3::sAxisZ();
    ws->mSuspensionMinLength = 0.05f; ws->mSuspensionMaxLength = 0.05f + std::max(0.05f, w.travel); ws->mSuspensionSpring.mFrequency = std::max(0.3f, w.stiffness); ws->mSuspensionSpring.mDamping = std::clamp(w.damping, 0.f, 2.f);
    ws->mRadius = std::max(0.05f, w.radius); ws->mWidth = std::max(0.02f, w.width); ws->mMaxSteerAngle = glm::radians(std::clamp(w.steer, 0.f, 80.f)); ws->mMaxBrakeTorque = w.brake; ws->mMaxHandBrakeTorque = w.handbrake;
    vs.mWheels.push_back(ws); }
  auto* ctl = new WheeledVehicleControllerSettings; ctl->mEngine.mMaxTorque = std::max(1.f, v.torque); ctl->mEngine.mMaxRPM = std::max(1500.f, v.maxRpm);
  // a differential per pair of driven wheels (left / right by x), the engine's torque shared between them
  std::vector<int> driven; for (size_t i = 0; i < v.wheels.size(); i++) if (v.wheels[i].drive) driven.push_back((int)i);
  if (driven.empty()) for (size_t i = 0; i < v.wheels.size(); i++) driven.push_back((int)i);
  std::vector<bool> used(v.wheels.size(), false);
  for (int i : driven) { if (used[i]) continue; used[i] = true; int pair = -1; float best = 1e9f;
    for (int k : driven) if (!used[k] && std::abs(v.wheels[k].pos.z - v.wheels[i].pos.z) < best && (v.wheels[k].pos.x > 0) != (v.wheels[i].pos.x > 0)) { best = std::abs(v.wheels[k].pos.z - v.wheels[i].pos.z); pair = k; }
    if (pair >= 0) used[pair] = true; VehicleDifferentialSettings df; bool iLeft = v.wheels[i].pos.x < 0;
    df.mLeftWheel = iLeft ? i : pair; df.mRightWheel = iLeft ? pair : i; ctl->mDifferentials.push_back(df); }
  for (auto& df : ctl->mDifferentials) df.mEngineTorqueRatio = 1.f / ctl->mDifferentials.size();
  vs.mController = ctl;
  // anti-roll bars on each axle (left / right wheels at the same z): the body leans less in turns
  { std::vector<bool> done(v.wheels.size(), false); for (size_t i = 0; i < v.wheels.size(); i++) { if (done[i]) continue; int best = -1; float bd = 1e9f;
      for (size_t k = 0; k < v.wheels.size(); k++) if (k != i && !done[k] && (v.wheels[k].pos.x > 0) != (v.wheels[i].pos.x > 0) && std::abs(v.wheels[k].pos.z - v.wheels[i].pos.z) < bd) { bd = std::abs(v.wheels[k].pos.z - v.wheels[i].pos.z); best = (int)k; }
      if (best < 0) continue; done[i] = done[best] = true; VehicleAntiRollBar rb; rb.mLeftWheel = v.wheels[i].pos.x < 0 ? (int)i : best; rb.mRightWheel = v.wheels[i].pos.x < 0 ? best : (int)i; rb.mStiffness = 6000; vs.mAntiRollBars.push_back(rb); } }
  Ref<VehicleConstraint> c = nullptr;
  { BodyLockWrite l(d->sys->GetBodyLockInterface(), b); if (!l.Succeeded()) return NO_BODY; c = new VehicleConstraint(l.GetBody(), vs); }
  c->SetVehicleCollisionTester(new VehicleCollisionTesterCastCylinder(Layers::MOVING));
  // tyres grip like on a road on the engine's usual surfaces (friction 0.4-0.5); less on slippery ones (ice: friction 0.05)
  c->SetCombineFriction([](uint, float& lon, float& lat, const Body& b2, const SubShapeID&) { float k = std::min(1.f, b2.GetFriction() * 2.2f); lon *= k; lat *= k; });
  d->sys->AddConstraint(c); d->sys->AddStepListener(c); d->bi().ActivateBody(b);
  uint32_t id = d->nextVehicle++; d->vehicles[id] = {c, b}; return id;
}
void Physics::removeVehicle(uint32_t id) { auto it = d->vehicles.find(id); if (it == d->vehicles.end()) return; d->sys->RemoveStepListener(it->second.c); d->sys->RemoveConstraint(it->second.c); d->vehicles.erase(it); }
void Physics::vehicleInput(uint32_t id, float f, float r, float br, float hb) {
  auto it = d->vehicles.find(id); if (it == d->vehicles.end()) return;
  auto* ctl = static_cast<WheeledVehicleController*>(it->second.c->GetController()); ctl->SetDriverInput(std::clamp(f, -1.f, 1.f), std::clamp(r, -1.f, 1.f), std::clamp(br, 0.f, 1.f), std::clamp(hb, 0.f, 1.f));
  if (f != 0 || r != 0 || br != 0 || hb != 0) d->bi().ActivateBody(it->second.body);
}
Physics::VehicleState Physics::vehicleState(uint32_t id, bool modelX) const {
  VehicleState s; auto it = d->vehicles.find(id); if (it == d->vehicles.end()) return s; VehicleConstraint& c = *it->second.c;
  auto* ctl = static_cast<WheeledVehicleController*>(c.GetController()); s.rpm = ctl->GetEngine().GetCurrentRPM(); s.gear = ctl->GetTransmission().GetCurrentGear();
  { BodyLockRead l(d->sys->GetBodyLockInterface(), it->second.body); if (l.Succeeded()) { const Body& b = l.GetBody(); s.speed = b.GetLinearVelocity().Dot(b.GetRotation() * -Vec3::sAxisZ()); } }
  for (uint i = 0; i < c.GetWheels().size(); i++) { s.grounded |= c.GetWheel(i)->HasContact();
    Mat44 m = modelX ? c.GetWheelLocalTransform(i, Vec3::sAxisX(), Vec3::sAxisY()) : c.GetWheelLocalTransform(i, Vec3::sAxisY(), Vec3::sAxisX());
    glm::mat4 g; for (int col = 0; col < 4; col++) { Vec4 v = m.GetColumn4(col); g[col] = glm::vec4(v.GetX(), v.GetY(), v.GetZ(), v.GetW()); } s.wheels.push_back(g); }
  return s;
}
}
