#include "physics.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <thread>
#include <unordered_map>
#include <algorithm>
#include <cstdlib>
#include <cmath>

using namespace JPH;

struct ShapeHandle { RefConst<Shape> s; };

namespace {
// PLAYER_ONLY: traversal volumes (barbed wire) that only the player capsule collides with. No simulated body
// (debris, grenades, dropped leaves) touches them and every ray / projectile query skips them, so rounds and
// thrown objects pass through the real gaps of a wire obstacle while the player cannot walk through it.
namespace Layers { constexpr ObjectLayer NON_MOVING = 0, MOVING = 1, PLAYER_ONLY = 2; }
class BPLayers final : public BroadPhaseLayerInterface {
public:
  uint GetNumBroadPhaseLayers() const override { return 3; }
  BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer l) const override { return BroadPhaseLayer((uint8)l); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char* GetBroadPhaseLayerName(BroadPhaseLayer) const override { return "layer"; }
#endif
};
class ObjVsBP final : public ObjectVsBroadPhaseLayerFilter {
public: bool ShouldCollide(ObjectLayer, BroadPhaseLayer) const override { return true; }
};
class ObjPair final : public ObjectLayerPairFilter {
public: bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override {
  if (a == Layers::PLAYER_ONLY || b == Layers::PLAYER_ONLY) return false;
  return a == Layers::MOVING || b == Layers::MOVING;
}
};
class AllLayers final : public ObjectLayerFilter { public: bool ShouldCollide(ObjectLayer) const override { return true; } };       // the player
class NoPlayerOnly final : public ObjectLayerFilter { public: bool ShouldCollide(ObjectLayer l) const override { return l != Layers::PLAYER_ONLY; } };   // rays

struct World {
  BPLayers bp; ObjVsBP ovb; ObjPair op;
  std::unique_ptr<TempAllocatorImpl> temp;
  std::unique_ptr<JobSystemThreadPool> jobs;
  std::unique_ptr<PhysicsSystem> sys;
  BodyID terrain;
  Ref<CharacterVirtual> character;
  RefConst<Shape> standShape, crouchShape, proneShape;
  int posture = 0; float bodyYaw = 0;
  float jumpBuf = 0, sinceGround = 99; bool jumped = false; int jumps = 0;   // jump buffer / coyote state
  bool crouched = false;
  int bodies = 0;
};
World* W = nullptr;

inline Vec3 jv(vec3 v) { return Vec3(v.x, v.y, v.z); }
inline RVec3 jr(vec3 v) { return RVec3(v.x, v.y, v.z); }
inline Quat jq(quat q) { return Quat(q.x, q.y, q.z, q.w); }
inline vec3 gv(Vec3 v) { return vec3(v.GetX(), v.GetY(), v.GetZ()); }
}

bool Physics::init() {
  RegisterDefaultAllocator();
  Factory::sInstance = new Factory();
  RegisterTypes();
  W = new World();
  W->temp = std::make_unique<TempAllocatorImpl>(32 * 1024 * 1024);
  W->jobs = std::make_unique<JobSystemThreadPool>(cMaxPhysicsJobs, cMaxPhysicsBarriers, std::max(1, (int)std::thread::hardware_concurrency() - 2));
  W->sys = std::make_unique<PhysicsSystem>();
  W->sys->Init(20000, 0, 65536, 20000, W->bp, W->ovb, W->op);
  W->sys->SetGravity(Vec3(0, -9.81f, 0));
  auto mk = [](float h) {
    float r = Physics::radius();
    float half = h * 0.5f - r;
    return RefConst<Shape>(RotatedTranslatedShapeSettings(Vec3(0, h * 0.5f, 0), Quat::sIdentity(), new CapsuleShape(half, r)).Create().Get());
  };
  W->standShape = mk(standHeight());
  W->crouchShape = mk(crouchHeight());
  // prone: a body-length capsule lying along local -z behind the head (feet at the origin plane), turned with the body yaw
  {
    float r = proneRadius(), half = proneHalfLength() - r;
    W->proneShape = RefConst<Shape>(RotatedTranslatedShapeSettings(Vec3(0, r, 0.55f), Quat::sRotation(Vec3::sAxisX(), JPH_PI * 0.5f), new CapsuleShape(half, r)).Create().Get());
  }
  return true;
}

void Physics::shutdown() {
  if (!W) return;
  W->character = nullptr;
  delete W; W = nullptr;
  UnregisterTypes();
  delete Factory::sInstance; Factory::sInstance = nullptr;
}

ShapePtr Physics::buildShape(const std::vector<Part>& parts) {
  if (parts.empty()) return nullptr;
  StaticCompoundShapeSettings cs;
  int n = 0;
  RefConst<Shape> single; Vec3 singlePos; Quat singleRot;
  for (auto& p : parts) {
    RefConst<Shape> s; Vec3 pos = jv(p.center); Quat rot = jq(p.rot);
    if (p.shape == Part::BOX || p.shape == Part::ROUND) {   // ROUND: Jolt's convex radius rounds the box edges
      vec3 h = glm::max(p.size * 0.5f, vec3(0.005f));
      float cr = std::min(p.shape == Part::ROUND ? std::max(0.02f, p.rad) : 0.02f, std::min(h.x, std::min(h.y, h.z)) * (p.shape == Part::ROUND ? 0.999f : 0.5f));
      s = new BoxShape(jv(h), cr);
    } else if (p.shape != Part::PRISM) {   // CYL / ARCH / WEDGE: their convex pieces, in the part frame
      std::vector<std::vector<vec3>> hulls; partHulls(p, hulls);
      for (auto& hl : hulls) {
        Array<Vec3> pts; for (auto& q : hl) pts.push_back(jv(q));
        auto r = ConvexHullShapeSettings(pts, 0.0f).Create();
        if (r.HasError()) continue;
        s = r.Get(); cs.AddShape(pos, rot, s.GetPtr());
        single = s; singlePos = pos; singleRot = rot; ++n;
      }
      continue;
    } else {
      Array<Vec3> pts;
      for (int k = 0; k < 3; ++k) for (float a : {p.a0, p.a1}) {
        vec2 uv = p.tri[k]; vec3 q = p.axis == 0 ? vec3(a, uv.y, uv.x) : p.axis == 2 ? vec3(uv.x, uv.y, a) : vec3(uv.x, a, uv.y);
        pts.push_back(jv(q - p.center));
      }
      auto r = ConvexHullShapeSettings(pts, 0.0f).Create();
      if (r.HasError()) continue;
      s = r.Get(); rot = Quat::sIdentity();
    }
    cs.AddShape(pos, rot, s.GetPtr());
    single = s; singlePos = pos; singleRot = rot; ++n;
  }
  auto h = std::make_shared<ShapeHandle>();
  if (n == 0) return nullptr;
  if (n == 1) {
    h->s = RotatedTranslatedShapeSettings(singlePos, singleRot, single.GetPtr()).Create().Get();
  } else {
    auto r = cs.Create();
    if (r.HasError()) { logf("shape error: %s", r.GetError().c_str()); return nullptr; }
    h->s = r.Get();
  }
  return h;
}

ShapePtr Physics::buildHulls(const std::vector<std::vector<vec3>>& hulls) {
  StaticCompoundShapeSettings cs; RefConst<Shape> single; int n = 0;
  for (auto& pts : hulls) {
    if (pts.size() < 4) continue;
    Array<Vec3> jp; for (auto& q : pts) jp.push_back(jv(q));
    auto r = ConvexHullShapeSettings(jp, 0.01f).Create();
    if (r.HasError()) { logf("hull error: %s", r.GetError().c_str()); continue; }
    single = r.Get(); cs.AddShape(Vec3::sZero(), Quat::sIdentity(), single.GetPtr()); ++n;
  }
  if (n == 0) return nullptr;
  auto h = std::make_shared<ShapeHandle>();
  if (n == 1) { h->s = single; return h; }
  auto r = cs.Create();
  if (r.HasError()) { logf("shape error: %s", r.GetError().c_str()); return nullptr; }
  h->s = r.Get();
  return h;
}

// Base ground collision: one heightfield body per terrain chunk, holes under deformation tiles (terrain_chunks.cpp,
// via addHeightPatch with cNoCollisionValue samples).

uint32_t Physics::addStatic(const ShapePtr& s, vec3 pos, quat rot, uint64_t ud) {
  if (!s) return 0xffffffffu;
  BodyCreationSettings b(s->s.GetPtr(), jr(pos), jq(rot), EMotionType::Static, Layers::NON_MOVING);
  b.mUserData = ud;
  BodyID id = W->sys->GetBodyInterface().CreateAndAddBody(b, EActivation::DontActivate);
  if (!id.IsInvalid()) ++W->bodies;
  return id.GetIndexAndSequenceNumber();
}
void Physics::setMaterial(uint32_t body, float friction, float restitution, bool continuous) {
  if (body == 0xffffffffu) return;
  BodyID id(body); auto& bi = W->sys->GetBodyInterface();
  bi.SetFriction(id, friction); bi.SetRestitution(id, restitution);
  if (continuous) bi.SetMotionQuality(id, EMotionQuality::LinearCast);   // small and fast: no tunnelling through walls
}
void Physics::setDamping(uint32_t body, float linear, float angular) {
  if (body == 0xffffffffu) return;
  BodyLockWrite lock(W->sys->GetBodyLockInterface(), BodyID(body));
  if (!lock.Succeeded() || !lock.GetBody().GetMotionProperties()) return;
  lock.GetBody().GetMotionProperties()->SetLinearDamping(linear); lock.GetBody().GetMotionProperties()->SetAngularDamping(angular);
}
void Physics::setVelocity(uint32_t body, vec3 linear, vec3 angular) {
  if (body == 0xffffffffu) return;
  BodyID id(body); W->sys->GetBodyInterface().SetLinearAndAngularVelocity(id, jv(linear), jv(angular));
}
uint32_t Physics::addPlayerOnly(const ShapePtr& s, vec3 pos, quat rot, uint64_t ud) {
  if (!s) return 0xffffffffu;
  BodyCreationSettings b(s->s.GetPtr(), jr(pos), jq(rot), EMotionType::Static, Layers::PLAYER_ONLY);
  b.mUserData = ud;
  BodyID id = W->sys->GetBodyInterface().CreateAndAddBody(b, EActivation::DontActivate);
  if (!id.IsInvalid()) ++W->bodies;
  return id.GetIndexAndSequenceNumber();
}
uint32_t Physics::addKinematic(const ShapePtr& s, vec3 pos, quat rot, uint64_t ud) {
  if (!s) return 0xffffffffu;
  BodyCreationSettings b(s->s.GetPtr(), jr(pos), jq(rot), EMotionType::Kinematic, Layers::MOVING);
  b.mUserData = ud;
  BodyID id = W->sys->GetBodyInterface().CreateAndAddBody(b, EActivation::DontActivate);
  if (!id.IsInvalid()) ++W->bodies;
  return id.GetIndexAndSequenceNumber();
}
uint32_t Physics::addDynamic(const ShapePtr& s, vec3 pos, quat rot, uint64_t ud, float mass) {
  if (!s) return 0xffffffffu;
  BodyCreationSettings b(s->s.GetPtr(), jr(pos), jq(rot), EMotionType::Dynamic, Layers::MOVING);
  b.mUserData = ud;
  b.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
  b.mMassPropertiesOverride.mMass = std::max(0.1f, mass);
  b.mFriction = 0.7f; b.mRestitution = 0.1f; b.mLinearDamping = 0.05f; b.mAngularDamping = 0.2f;
  BodyID id = W->sys->GetBodyInterface().CreateAndAddBody(b, EActivation::Activate);
  if (!id.IsInvalid()) ++W->bodies;
  return id.GetIndexAndSequenceNumber();
}
void Physics::addImpulse(uint32_t body, vec3 j, vec3 at) {
  if (body == 0xffffffffu) return;
  W->sys->GetBodyInterface().AddImpulse(BodyID(body), jv(j), jr(at));
}
void Physics::addAngularImpulse(uint32_t body, vec3 a) {
  if (body == 0xffffffffu) return;
  W->sys->GetBodyInterface().AddAngularImpulse(BodyID(body), jv(a));
}
bool Physics::getTransform(uint32_t body, vec3& pos, quat& rot) {
  if (body == 0xffffffffu) return false;
  RVec3 p; Quat q;
  W->sys->GetBodyInterface().GetPositionAndRotation(BodyID(body), p, q);
  pos = vec3((float)p.GetX(), (float)p.GetY(), (float)p.GetZ()); rot = quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
  return true;
}
bool Physics::isActive(uint32_t body) { return body != 0xffffffffu && W->sys->GetBodyInterface().IsActive(BodyID(body)); }
vec3 Physics::velocity(uint32_t body) { if (body == 0xffffffffu) return vec3(0); return gv(W->sys->GetBodyInterface().GetLinearVelocity(BodyID(body))); }
void Physics::setTransform(uint32_t body, vec3 pos, quat rot) {
  if (body == 0xffffffffu) return;
  W->sys->GetBodyInterface().SetPositionAndRotation(BodyID(body), jr(pos), jq(rot), EActivation::DontActivate);
}
void Physics::remove(uint32_t body) {
  if (body == 0xffffffffu) return;
  auto& bi = W->sys->GetBodyInterface();
  bi.RemoveBody(BodyID(body)); bi.DestroyBody(BodyID(body)); --W->bodies;
}
int Physics::bodyCount() { return W->bodies; }
void Physics::step(float dt) { W->sys->Update(dt, 1, W->temp.get(), W->jobs.get()); }
void Physics::optimizeBroadPhase() { W->sys->OptimizeBroadPhase(); }

namespace {
class IgnoreUD final : public BodyFilter {
public:
  explicit IgnoreUD(uint64 u) : ud(u) {}
  bool ShouldCollideLocked(const Body& b) const override { return b.GetUserData() != ud; }
  uint64 ud; bool skipTerrain = false;
};
class IgnoreUDTerrain final : public BodyFilter {
public:
  explicit IgnoreUDTerrain(uint64 u, bool t) : ud(u), terr(t) {}
  bool ShouldCollideLocked(const Body& b) const override {
    uint64 u = b.GetUserData();
    if (u == ud) return false;
    if (terr && (u == 0 || (u >> 60) == 0xF)) return false;   // base terrain or terrain patch
    return true;
  }
  uint64 ud; bool terr;
};
}

bool Physics::castRay(vec3 o, vec3 d, float maxDist, float& t, uint64_t& ud) {
  RRayCast ray{jr(o), jv(glm::normalize(d) * maxDist)};
  RayCastResult hit;
  NoPlayerOnly nl;
  if (!W->sys->GetNarrowPhaseQuery().CastRay(ray, hit, {}, nl)) return false;
  t = hit.mFraction * maxDist;
  ud = W->sys->GetBodyInterface().GetUserData(hit.mBodyID);
  return true;
}

bool Physics::castRayEx(vec3 o, vec3 d, float maxDist, Hit& out, uint64_t ignoreUd) {
  vec3 dn = glm::normalize(d);
  RRayCast ray{jr(o), jv(dn * maxDist)};
  RayCastResult hit;
  IgnoreUD filt(ignoreUd);
  NoPlayerOnly nl;
  if (!W->sys->GetNarrowPhaseQuery().CastRay(ray, hit, {}, nl, filt)) return false;
  out.t = hit.mFraction * maxDist;
  out.pos = o + dn * out.t;
  BodyLockRead lock(W->sys->GetBodyLockInterface(), hit.mBodyID);
  if (lock.Succeeded()) {
    const Body& b = lock.GetBody();
    out.ud = b.GetUserData();
    out.normal = gv(b.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction)));
  }
  return true;
}

namespace { class OnlyUD final : public BodyFilter { public: explicit OnlyUD(uint64 u) : ud(u) {} bool ShouldCollideLocked(const Body& b) const override { return b.GetUserData() == ud; } uint64 ud; }; }
bool Physics::castRayOnly(vec3 o, vec3 d, float maxDist, uint64_t onlyUd, Hit& out) {
  vec3 dn = glm::normalize(d);
  RRayCast ray{jr(o), jv(dn * maxDist)};
  RayCastResult hit;
  OnlyUD filt(onlyUd);
  NoPlayerOnly nl;
  if (!W->sys->GetNarrowPhaseQuery().CastRay(ray, hit, {}, nl, filt)) return false;
  out.t = hit.mFraction * maxDist; out.pos = o + dn * out.t; out.ud = onlyUd;
  return true;
}

bool Physics::boxBlocked(vec3 c, vec3 he, quat q, uint64_t ignoreUd, bool ignoreTerrain) {
  BoxShape box(jv(glm::max(he, vec3(0.01f))), 0.0f);
  box.SetEmbedded();
  CollideShapeSettings cs;
  AnyHitCollisionCollector<CollideShapeCollector> col;
  IgnoreUDTerrain filt(ignoreUd, ignoreTerrain);
  W->sys->GetNarrowPhaseQuery().CollideShape(&box, Vec3::sReplicate(1.0f), RMat44::sRotationTranslation(jq(q), jr(c)), cs, RVec3::sZero(), col, {}, {}, filt);
  return col.HadHit();
}

int Physics::boxOverlaps(vec3 c, vec3 he, quat q, Overlap* out, int maxOut, uint64_t ignoreUd, bool ignoreTerrain) {
  BoxShape box(jv(glm::max(he, vec3(0.01f))), 0.0f);
  box.SetEmbedded();
  CollideShapeSettings cs;
  AllHitCollisionCollector<CollideShapeCollector> col;
  IgnoreUDTerrain filt(ignoreUd, ignoreTerrain);
  W->sys->GetNarrowPhaseQuery().CollideShape(&box, Vec3::sReplicate(1.0f), RMat44::sRotationTranslation(jq(q), jr(c)), cs, RVec3::sZero(), col, {}, {}, filt);
  int n = 0;
  for (auto& h : col.mHits) {
    uint32_t id = h.mBodyID2.GetIndexAndSequenceNumber();
    int k = 0;
    while (k < n && out[k].body != id) ++k;
    if (k < n) { if (h.mPenetrationDepth > out[k].depth) { out[k].depth = h.mPenetrationDepth; out[k].pos = gv(Vec3(h.mContactPointOn2)); } continue; }
    if (n >= maxOut) continue;
    out[n].body = id; out[n].ud = W->sys->GetBodyInterface().GetUserData(h.mBodyID2);
    out[n].pos = gv(Vec3(h.mContactPointOn2)); out[n].depth = h.mPenetrationDepth; ++n;
  }
  return n;
}

ShapePtr Physics::buildCompound(const std::vector<ShapePtr>& shapes) {
  StaticCompoundShapeSettings cs;
  int n = 0; const Shape* single = nullptr;
  for (auto& s : shapes) if (s && s->s) { cs.AddShape(Vec3::sZero(), Quat::sIdentity(), s->s.GetPtr()); single = s->s.GetPtr(); ++n; }
  if (n == 0) return nullptr;
  auto h = std::make_shared<ShapeHandle>();
  if (n == 1) { h->s = single; return h; }
  auto r = cs.Create();
  if (r.HasError()) { logf("compound error: %s", r.GetError().c_str()); return nullptr; }
  h->s = r.Get();
  return h;
}

void Physics::setShape(uint32_t body, const ShapePtr& s) {
  if (body == 0xffffffffu || !s) return;
  W->sys->GetBodyInterface().SetShape(BodyID(body), s->s.GetPtr(), false, EActivation::DontActivate);
}

uint32_t Physics::addHeightPatch(const std::vector<float>& h, int n, vec3 origin, float spacing, uint64_t ud) {
  HeightFieldShapeSettings hs(h.data(), jv(origin), Vec3(spacing, 1, spacing), (uint32)n);
  hs.mBlockSize = 2;
  auto r = hs.Create();
  if (r.HasError()) { logf("patch heightfield error: %s", r.GetError().c_str()); return 0xffffffffu; }
  BodyCreationSettings b(r.Get(), RVec3::sZero(), Quat::sIdentity(), EMotionType::Static, Layers::NON_MOVING);
  b.mUserData = ud;
  BodyID id = W->sys->GetBodyInterface().CreateAndAddBody(b, EActivation::DontActivate);
  if (!id.IsInvalid()) ++W->bodies;
  return id.GetIndexAndSequenceNumber();
}

bool Physics::capsuleBlocked(vec3 feet, float height, float r) {
  CapsuleShape cap(height * 0.5f - r, r);
  cap.SetEmbedded();
  CollideShapeSettings cs;
  AnyHitCollisionCollector<CollideShapeCollector> col;
  W->sys->GetNarrowPhaseQuery().CollideShape(&cap, Vec3::sReplicate(1.0f), RMat44::sTranslation(jr(feet + vec3(0, height * 0.5f, 0))), cs, RVec3::sZero(), col);
  return col.HadHit();
}

void Physics::createCharacter(vec3 feet) {
  Ref<CharacterVirtualSettings> s = new CharacterVirtualSettings();
  s->mShape = W->standShape;
  s->mMaxSlopeAngle = DegreesToRadians(50.0f);
  s->mSupportingVolume = Plane(Vec3::sAxisY(), -radius());
  s->mCharacterPadding = 0.02f;
  s->mPenetrationRecoverySpeed = 1.0f;
  s->mPredictiveContactDistance = 0.1f;
  s->mMaxStrength = 50.0f;
  s->mEnhancedInternalEdgeRemoval = true;
  W->character = new CharacterVirtual(s, jr(feet), Quat::sIdentity(), 0, W->sys.get());
  W->crouched = false; W->posture = STAND; W->bodyYaw = 0;
  W->jumpBuf = 0; W->sinceGround = 99; W->jumped = false; W->jumps = 0;
}
int Physics::jumpCount() { return W ? W->jumps : 0; }
void Physics::destroyCharacter() { if (W) W->character = nullptr; }
bool Physics::hasCharacter() { return W && W->character; }
void Physics::setCharacterPos(vec3 feet) {
  if (!W->character) return;
  W->character->SetPosition(jr(feet));
  W->character->SetLinearVelocity(Vec3::sZero());
  W->jumpBuf = 0; W->sinceGround = 99; W->jumped = false;   // a teleport carries no pending jump
}

Physics::MoveTuning Physics::tuning;
Physics::SwimTuning Physics::swimTuning;
std::function<float(float, float)> Physics::waterQuery;

void Physics::updateSwim(float dt, vec3 vel) {
  auto& c = W->character;
  if (!c) return;
  auto bpf = W->sys->GetDefaultBroadPhaseLayerFilter(Layers::MOVING);
  AllLayers olf;   // the capsule also meets PLAYER_ONLY traversal volumes
  BodyFilter bf; ShapeFilter sf;
  if (W->posture != STAND && postureFits(STAND, W->bodyYaw) && c->SetShape(W->standShape.GetPtr(), 1.5f * 0.02f, bpf, olf, bf, sf, *W->temp)) {
    W->posture = STAND; W->crouched = false; c->SetRotation(Quat::sIdentity());
  }
  W->jumpBuf = 0; W->jumped = false; W->sinceGround = 99;   // no buffered / coyote jump carries out of the water
  c->UpdateGroundVelocity();
  c->SetLinearVelocity(jv(vel));
  CharacterVirtual::ExtendedUpdateSettings us;
  us.mStickToFloorStepDown = Vec3::sZero();
  us.mWalkStairsStepUp = c->GetGroundState() == CharacterBase::EGroundState::OnGround ? Vec3(0, 0.35f, 0) : Vec3::sZero();
  c->ExtendedUpdate(dt, Vec3::sZero(), us, bpf, olf, bf, sf, *W->temp);
}

bool Physics::postureFits(int posture, float bodyYaw) {
  auto& c = W->character; if (!c) return false;
  const Shape* sh = posture == PRONE ? W->proneShape.GetPtr() : posture == CROUCH ? W->crouchShape.GetPtr() : W->standShape.GetPtr();
  CollideShapeSettings cs; cs.mMaxSeparationDistance = 0; cs.mActiveEdgeMode = EActiveEdgeMode::CollideOnlyWithActive;
  AnyHitCollisionCollector<CollideShapeCollector> col;
  Quat q = posture == PRONE ? Quat::sRotation(Vec3::sAxisY(), DegreesToRadians(bodyYaw)) : Quat::sIdentity();
  // lifted 4 cm so resting on the ground (and the stair nosing under the feet) is not a collision
  RVec3 p = c->GetPosition() + Vec3(0, 0.04f, 0);
  IgnoreUDTerrain filt(~0ull, true);   // ground contact is the controller's job; walls, ceilings and props block
  // CollideShape takes the shape's CENTRE-OF-MASS transform: offset by the (rotated) centre of mass of the
  // feet-origin capsule, or the test body sits 0.9 m too low (centred on the feet)
  RMat44 xf = RMat44::sRotationTranslation(q, p).PreTranslated(sh->GetCenterOfMass());
  W->sys->GetNarrowPhaseQuery().CollideShape(sh, Vec3::sReplicate(1.0f), xf, cs, RVec3::sZero(), col, {}, {}, filt);
  return !col.HadHit();
}

bool Physics::shapeFitsAt(int posture, vec3 feet, bool withTerrain) {
  auto& c = W->character; if (!c) return false;
  const Shape* sh = posture == PRONE ? W->proneShape.GetPtr() : posture == CROUCH ? W->crouchShape.GetPtr() : W->standShape.GetPtr();
  CollideShapeSettings cs; cs.mMaxSeparationDistance = 0; cs.mActiveEdgeMode = EActiveEdgeMode::CollideOnlyWithActive;
  AnyHitCollisionCollector<CollideShapeCollector> col;
  IgnoreUDTerrain filt(~0ull, !withTerrain);
  W->sys->GetNarrowPhaseQuery().CollideShape(sh, Vec3::sReplicate(1.0f), RMat44::sTranslation(jr(feet) + Vec3(0, 0.03f, 0) + sh->GetCenterOfMass()), cs, RVec3::sZero(), col, {}, {}, filt);
  return !col.HadHit();
}

uint64_t Physics::shapeHitUD(int posture, vec3 feet, bool withTerrain) {
  auto& c = W->character; if (!c) return 0;
  const Shape* sh = posture == PRONE ? W->proneShape.GetPtr() : posture == CROUCH ? W->crouchShape.GetPtr() : W->standShape.GetPtr();
  CollideShapeSettings cs; cs.mMaxSeparationDistance = 0; cs.mActiveEdgeMode = EActiveEdgeMode::CollideOnlyWithActive;
  ClosestHitCollisionCollector<CollideShapeCollector> col;
  IgnoreUDTerrain filt(~0ull, !withTerrain);
  W->sys->GetNarrowPhaseQuery().CollideShape(sh, Vec3::sReplicate(1.0f), RMat44::sTranslation(jr(feet) + Vec3(0, 0.03f, 0) + sh->GetCenterOfMass()), cs, RVec3::sZero(), col, {}, {}, filt);
  if (!col.HadHit()) return 0;
  BodyLockRead lock(W->sys->GetBodyLockInterface(), col.mHit.mBodyID2);
  return lock.Succeeded() ? lock.GetBody().GetUserData() : 1;
}

bool Physics::updateCharacter(float dt, vec3 hv, bool jump, int want, float yawWanted) {
  auto& c = W->character;
  if (!c) return false;
  auto bpf = W->sys->GetDefaultBroadPhaseLayerFilter(Layers::MOVING);
  AllLayers olf;   // the capsule also meets PLAYER_ONLY traversal volumes
  BodyFilter bf; ShapeFilter sf;
  if (want != W->posture) {
    // expanding (toward standing / into prone) only when the full target body fits; SetShape also refuses
    // a shape that would penetrate, so the character never ends up inside geometry
    float y = want == PRONE ? yawWanted : W->bodyYaw;
    bool fits = postureFits(want, y);
    if (fits) {
      if (want == PRONE) c->SetRotation(Quat::sRotation(Vec3::sAxisY(), DegreesToRadians(y)));
      const Shape* sh = want == PRONE ? W->proneShape.GetPtr() : want == CROUCH ? W->crouchShape.GetPtr() : W->standShape.GetPtr();
      if (c->SetShape(sh, 1.5f * 0.02f, bpf, olf, bf, sf, *W->temp)) {
        W->posture = want; W->crouched = want != STAND; W->bodyYaw = y;
        if (want != PRONE) c->SetRotation(Quat::sIdentity());
      } else if (W->posture != PRONE) c->SetRotation(Quat::sIdentity());
    }
  }
  if (W->posture == PRONE && std::fabs(wrapDeg(yawWanted - W->bodyYaw + 180.0f) - 180.0f) > 0.5f) {
    // turning while prone sweeps a 1.7 m body: rotate only when the turned body fits (else keep the old heading)
    if (postureFits(PRONE, yawWanted)) { W->bodyYaw = yawWanted; c->SetRotation(Quat::sRotation(Vec3::sAxisY(), DegreesToRadians(yawWanted))); }
  }
  c->UpdateGroundVelocity();
  Vec3 gravity(0, -tuning.gravity, 0);   // player-only gravity
  Vec3 vert(0, c->GetLinearVelocity().GetY(), 0);
  Vec3 groundV = c->GetGroundVelocity();
  Vec3 nv;
  // Jump: a press is buffered for tuning.jumpBuffer s (pressed just before landing, or on a step that found no
  // ground), and honoured up to tuning.coyote s after the last settled ground contact (one-frame ground flicker on
  // slopes and step-ups, walking off an edge). One impulse per ground contact: steep ground never refreshes it, so
  // repeated jumps still cannot climb steep faces. Presses made while crouched / prone are not buffered.
  bool settled = c->GetGroundState() == CharacterBase::EGroundState::OnGround && (vert.GetY() - groundV.GetY()) < 0.1f;
  if (settled) { W->sinceGround = 0; W->jumped = false; }
  else W->sinceGround += dt;
  if (jump && W->posture == STAND) W->jumpBuf = tuning.jumpBuffer;
  if (W->posture != STAND) W->jumpBuf = 0;
  bool doJump = W->jumpBuf > 0 && !W->jumped && W->sinceGround <= tuning.coyote;
  nv = settled ? groundV : vert;
  if (doJump) {
    float jv0 = tuning.jumpVelocity + 0.5f * tuning.gravity * dt;   // W1: + half a step of gravity, so the stepped jump has the analytic apex v^2/2g and airtime 2v/g
    nv = settled ? groundV + Vec3(0, jv0, 0) : Vec3(0, std::max(0.0f, groundV.GetY()) + jv0, 0);
    W->jumped = true; W->jumpBuf = 0; W->sinceGround = 99; ++W->jumps;
  } else W->jumpBuf = std::max(0.0f, W->jumpBuf - dt);
  nv += gravity * dt;
  nv += jv(hv);
  c->SetLinearVelocity(nv);
  CharacterVirtual::ExtendedUpdateSettings us;
  us.mStickToFloorStepDown = Vec3(0, -0.5f, 0);
  // step-up only with real ground contact: "supported" also counts steep ground, which let repeated jumps walk the
  // character up steep faces and obstacle edges (the jump-climbing exploit)
  bool onGround = c->GetGroundState() == CharacterBase::EGroundState::OnGround;
  us.mWalkStairsStepUp = onGround ? Vec3(0, W->posture == PRONE ? 0.2f : 0.35f, 0) : Vec3::sZero();
  c->ExtendedUpdate(dt, gravity, us, bpf, olf, bf, sf, *W->temp);
  return doJump;
}

void Physics::updateCharacter(float dt, vec3 hv, bool jump, bool wantCrouch) {
  auto& c = W->character;
  if (!c) return;
  auto bpf = W->sys->GetDefaultBroadPhaseLayerFilter(Layers::MOVING);
  AllLayers olf;   // the capsule also meets PLAYER_ONLY traversal volumes
  BodyFilter bf; ShapeFilter sf;
  if (wantCrouch != W->crouched) {
    if (c->SetShape(wantCrouch ? W->crouchShape.GetPtr() : W->standShape.GetPtr(), 1.5f * 0.02f, bpf, olf, bf, sf, *W->temp))
      W->crouched = wantCrouch;
  }
  c->UpdateGroundVelocity();
  Vec3 gravity(0, -9.81f, 0);
  Vec3 vert(0, c->GetLinearVelocity().GetY(), 0);
  Vec3 groundV = c->GetGroundVelocity();
  Vec3 nv;
  if (c->GetGroundState() == CharacterBase::EGroundState::OnGround && (vert.GetY() - groundV.GetY()) < 0.1f) {
    nv = groundV;
    if (jump && !W->crouched) nv += Vec3(0, 4.2f, 0);
  } else nv = vert;
  nv += gravity * dt;
  nv += jv(hv);
  c->SetLinearVelocity(nv);
  CharacterVirtual::ExtendedUpdateSettings us;
  us.mStickToFloorStepDown = Vec3(0, -0.5f, 0);
  us.mWalkStairsStepUp = Vec3(0, 0.35f, 0);
  c->ExtendedUpdate(dt, gravity, us, bpf, olf, bf, sf, *W->temp);
}

Physics::CharState Physics::character() {
  CharState s{};
  auto& c = W->character;
  if (!c) return s;
  RVec3 p = c->GetPosition();
  s.pos = vec3((float)p.GetX(), (float)p.GetY(), (float)p.GetZ());
  s.grounded = c->GetGroundState() == CharacterBase::EGroundState::OnGround;
  s.crouched = W->crouched;
  s.posture = W->posture; s.bodyYaw = W->bodyYaw;
  Vec3 v = c->GetLinearVelocity(); s.vel = vec3(v.GetX(), v.GetY(), v.GetZ());
  s.groundUser = c->GetGroundBodyID().IsInvalid() ? ~0ull : W->sys->GetBodyInterface().GetUserData(c->GetGroundBodyID());
  return s;
}

// ============================================================ physics_agents (multiplayer + AI)
namespace {
struct AgentRec { Ref<CharacterVirtual> c; int posture = Physics::STAND; float bodyYaw = 0; bool used = false, ghost = false, swim = false; };
std::vector<AgentRec> gAgents;
// PF1: Jolt's simple character-vs-character list tests a mover against every character in each of a move's several
// collision queries (O(n^2): 200 AI). This one hands the base class only the characters in the 3 x 3 grid cells (4 m)
// around the mover; the grid is rebuilt every `all.size()` queries (about every quarter frame: they move centimetres).
// NMK_CVC_GRID=0: every character (A/B measurements).
class CvcNear final : public CharacterVsCharacterCollisionSimple {
public:
  void Add(CharacterVirtual* c) { all.push_back(c); grid.clear(); }
  void Remove(const CharacterVirtual* c) { auto it = std::find(all.begin(), all.end(), c); if (it != all.end()) all.erase(it); grid.clear(); }
  void CollideCharacter(const CharacterVirtual* ch, RMat44Arg com, const CollideShapeSettings& s, RVec3Arg base, CollideShapeCollector& out) const override {
    gather(ch, com.GetTranslation(), 0); CharacterVsCharacterCollisionSimple::CollideCharacter(ch, com, s, base, out);
  }
  void CastCharacter(const CharacterVirtual* ch, RMat44Arg com, Vec3Arg dir, const ShapeCastSettings& s, RVec3Arg base, CastShapeCollector& out) const override {
    gather(ch, com.GetTranslation(), dir.Length()); CharacterVsCharacterCollisionSimple::CastCharacter(ch, com, dir, s, base, out);
  }
private:
  static constexpr float kCell = 4.0f;
  std::vector<CharacterVirtual*> all;
  mutable std::unordered_map<int64_t, std::vector<CharacterVirtual*>> grid; mutable size_t queries = 0;
  static int64_t key(int x, int z) { return ((int64_t)x << 32) ^ (uint32_t)z; }
  void gather(const CharacterVirtual* me, RVec3 p, float reach) const {
    static const bool on = [] { const char* e = std::getenv("NMK_CVC_GRID"); return !(e && *e == '0'); }();
    auto& near = const_cast<Array<CharacterVirtual*>&>(mCharacters);   // the base class's list: this query's candidates
    near.clear();
    if (!on) { for (auto* c : all) near.push_back(c); return; }
    if (grid.empty() || ++queries > all.size()) {
      grid.clear(); queries = 0;
      for (auto* c : all) { RVec3 q = c->GetPosition(); grid[key((int)std::floor((float)q.GetX() / kCell), (int)std::floor((float)q.GetZ() / kCell))].push_back(c); }
    }
    int r = 1 + (int)std::ceil(reach / kCell);
    int cx = (int)std::floor((float)p.GetX() / kCell), cz = (int)std::floor((float)p.GetZ() / kCell);
    for (int dz = -r; dz <= r; ++dz) for (int dx = -r; dx <= r; ++dx) {
      auto it = grid.find(key(cx + dx, cz + dz));
      if (it != grid.end()) for (auto* c : it->second) if (c != me) near.push_back(c);
    }
  }
};
CvcNear gCvc;
const CharacterVirtual* gCvcPlayer = nullptr;
void syncCvcPlayer() {
  // the player's character is created / destroyed by explore.cpp: keep it registered without touching that code
  const CharacterVirtual* cur = W && W->character ? W->character.GetPtr() : nullptr;
  if (cur == gCvcPlayer) return;
  if (gCvcPlayer) gCvc.Remove(gCvcPlayer);
  gCvcPlayer = cur;
  if (cur) { gCvc.Add(W->character.GetPtr()); W->character->SetCharacterVsCharacterCollision(&gCvc); }
}
const Shape* agentShape(int posture) { return posture == Physics::PRONE ? W->proneShape.GetPtr() : posture == Physics::CROUCH ? W->crouchShape.GetPtr() : W->standShape.GetPtr(); }
}

int Physics::createAgent(vec3 feet) {
  syncCvcPlayer();
  Ref<CharacterVirtualSettings> s = new CharacterVirtualSettings();
  s->mShape = W->standShape;
  s->mMaxSlopeAngle = DegreesToRadians(50.0f);
  s->mSupportingVolume = Plane(Vec3::sAxisY(), -radius());
  s->mCharacterPadding = 0.02f; s->mPenetrationRecoverySpeed = 1.0f; s->mPredictiveContactDistance = 0.1f;
  s->mMaxStrength = 50.0f; s->mEnhancedInternalEdgeRemoval = true;
  int h = -1;
  for (size_t k = 0; k < gAgents.size(); ++k) if (!gAgents[k].used) { h = (int)k; break; }
  if (h < 0) { h = (int)gAgents.size(); gAgents.emplace_back(); }
  AgentRec& a = gAgents[h];
  a.c = new CharacterVirtual(s, jr(feet), Quat::sIdentity(), 0, W->sys.get());
  a.c->SetCharacterVsCharacterCollision(&gCvc);
  gCvc.Add(a.c.GetPtr());
  a.posture = STAND; a.bodyYaw = 0; a.used = true; a.swim = false;
  return h;
}
void Physics::destroyAgent(int h) {
  if (h < 0 || h >= (int)gAgents.size() || !gAgents[h].used) return;
  if (!gAgents[h].ghost) gCvc.Remove(gAgents[h].c.GetPtr());
  gAgents[h].c = nullptr; gAgents[h].used = false; gAgents[h].ghost = false;
}
void Physics::setAgentGhost(int h, bool on) {   // W6b: a bot wedged against a teammate passes through soldiers for a moment
  if (h < 0 || h >= (int)gAgents.size() || !gAgents[h].used || gAgents[h].ghost == on) return;
  AgentRec& a = gAgents[h]; a.ghost = on;
  if (on) { gCvc.Remove(a.c.GetPtr()); a.c->SetCharacterVsCharacterCollision(nullptr); }
  else { gCvc.Add(a.c.GetPtr()); a.c->SetCharacterVsCharacterCollision(&gCvc); }
}
void Physics::destroyAllAgents() { for (int k = 0; k < (int)gAgents.size(); ++k) destroyAgent(k); }
void Physics::setAgentPos(int h, vec3 feet) {
  if (h < 0 || h >= (int)gAgents.size() || !gAgents[h].used) return;
  gAgents[h].c->SetPosition(jr(feet)); gAgents[h].c->SetLinearVelocity(Vec3::sZero());
}
Physics::CharState Physics::agent(int h) {
  CharState s{};
  if (h < 0 || h >= (int)gAgents.size() || !gAgents[h].used) return s;
  auto& c = gAgents[h].c;
  RVec3 p = c->GetPosition();
  s.pos = vec3((float)p.GetX(), (float)p.GetY(), (float)p.GetZ());
  s.grounded = c->GetGroundState() == CharacterBase::EGroundState::OnGround;
  s.posture = gAgents[h].posture; s.crouched = s.posture != STAND; s.bodyYaw = gAgents[h].bodyYaw; s.swim = gAgents[h].swim;
  Vec3 v = c->GetLinearVelocity(); s.vel = vec3(v.GetX(), v.GetY(), v.GetZ());
  s.groundUser = c->GetGroundBodyID().IsInvalid() ? ~0ull : W->sys->GetBodyInterface().GetUserData(c->GetGroundBodyID());
  return s;
}
void Physics::updateAgent(int h, float dt, vec3 hv, bool jump, int want, float yawWanted) {
  if (h < 0 || h >= (int)gAgents.size() || !gAgents[h].used) return;
  syncCvcPlayer();
  AgentRec& a = gAgents[h];
  auto& c = a.c;
  auto bpf = W->sys->GetDefaultBroadPhaseLayerFilter(Layers::MOVING);
  auto olf = W->sys->GetDefaultLayerFilter(Layers::MOVING);
  BodyFilter bf; ShapeFilter sf;
  if (want != a.posture) {
    float y = want == PRONE ? yawWanted : a.bodyYaw;
    if (want == PRONE) c->SetRotation(Quat::sRotation(Vec3::sAxisY(), DegreesToRadians(y)));
    if (c->SetShape(agentShape(want), 1.5f * 0.02f, bpf, olf, bf, sf, *W->temp)) { a.posture = want; a.bodyYaw = y; if (want != PRONE) c->SetRotation(Quat::sIdentity()); }
    else if (a.posture != PRONE) c->SetRotation(Quat::sIdentity());
  }
  c->UpdateGroundVelocity();
  Vec3 gravity(0, -9.81f, 0);
  Vec3 vert(0, c->GetLinearVelocity().GetY(), 0), groundV = c->GetGroundVelocity(), nv;
  bool onGround = c->GetGroundState() == CharacterBase::EGroundState::OnGround;
  // deep water: float at the surface (buoyancy toward floatDepth, slower strokes), never walk on the bed; hysteresis as
  // for the player (start above `enter` m of water over the feet, stop below `exit` once grounded)
  if (waterQuery) {
    RVec3 pp = c->GetPosition(); float y = (float)pp.GetY();
    float depth = waterQuery((float)pp.GetX(), (float)pp.GetZ()) - y;
    a.swim = a.swim ? !(onGround && depth < swimTuning.exit) && depth > 0.2f : depth > swimTuning.enter;
  } else a.swim = false;
  if (a.swim) {
    float surf = waterQuery((float)c->GetPosition().GetX(), (float)c->GetPosition().GetZ());
    float vy = std::clamp((surf - swimTuning.floatDepth - (float)c->GetPosition().GetY()) * 2.0f, -1.0f, 0.8f);
    c->SetLinearVelocity(jv(hv * swimTuning.speedScale) + Vec3(0, vy, 0));
    CharacterVirtual::ExtendedUpdateSettings us;
    us.mStickToFloorStepDown = Vec3::sZero();
    us.mWalkStairsStepUp = onGround ? Vec3(0, 0.35f, 0) : Vec3::sZero();
    c->ExtendedUpdate(dt, Vec3::sZero(), us, bpf, olf, bf, sf, *W->temp);
    return;
  }
  if (onGround && (vert.GetY() - groundV.GetY()) < 0.1f) { nv = groundV; if (jump && a.posture == STAND) nv += Vec3(0, 4.2f * 0.894427f, 0); }
  else nv = vert;
  nv += gravity * dt; nv += jv(hv);
  c->SetLinearVelocity(nv);
  CharacterVirtual::ExtendedUpdateSettings us;
  us.mStickToFloorStepDown = Vec3(0, -0.5f, 0);
  us.mWalkStairsStepUp = onGround ? Vec3(0, a.posture == PRONE ? 0.2f : 0.35f, 0) : Vec3::sZero();
  c->ExtendedUpdate(dt, gravity, us, bpf, olf, bf, sf, *W->temp);
}

namespace {
class SkipLeaves final : public BodyFilter {
public:
  bool skip = false;
  bool ShouldCollideLocked(const Body& b) const override {
    if (!skip) return true;
    uint64 u = b.GetUserData();
    if (u == 0 || (u >> 60) == 0xF) return true;          // terrain
    uint64 low = u & 0xFFFF;
    return low == 0 || low == 0xFFFF;                     // static core / wall compound (leaves have slot + 1)
  }
};
}
int Physics::castRayAll(vec3 o, vec3 d, float maxDist, Hit* out, int maxHits, bool skipLeaves) {
  vec3 dn = glm::normalize(d);
  RRayCast ray{jr(o), jv(dn * maxDist)};
  RayCastSettings rs; rs.mBackFaceModeTriangles = EBackFaceMode::IgnoreBackFaces; rs.mBackFaceModeConvex = EBackFaceMode::IgnoreBackFaces;
  AllHitCollisionCollector<CastRayCollector> col;
  SkipLeaves filt; filt.skip = skipLeaves;
  W->sys->GetNarrowPhaseQuery().CastRay(ray, rs, col, {}, {}, filt);
  col.Sort();
  int n = 0;
  for (auto& h : col.mHits) {
    if (n >= maxHits) break;
    Hit& o2 = out[n++];
    o2.t = h.mFraction * maxDist; o2.pos = o + dn * o2.t;
    BodyLockRead lock(W->sys->GetBodyLockInterface(), h.mBodyID);
    if (lock.Succeeded()) { o2.ud = lock.GetBody().GetUserData(); o2.normal = gv(lock.GetBody().GetWorldSpaceSurfaceNormal(h.mSubShapeID2, ray.GetPointOnRay(h.mFraction))); }
  }
  return n;
}
JPH::PhysicsSystem* navPhysicsSystem() { return W ? W->sys.get() : nullptr; }   // W6 (nav_geom.cpp): read-only body / shape access for the navmesh baker
