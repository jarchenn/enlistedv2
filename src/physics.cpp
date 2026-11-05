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

using namespace JPH;

struct ShapeHandle { RefConst<Shape> s; };

namespace {
namespace Layers { constexpr ObjectLayer NON_MOVING = 0, MOVING = 1; }
class BPLayers final : public BroadPhaseLayerInterface {
public:
  uint GetNumBroadPhaseLayers() const override { return 2; }
  BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer l) const override { return BroadPhaseLayer((uint8)l); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char* GetBroadPhaseLayerName(BroadPhaseLayer) const override { return "layer"; }
#endif
};
class ObjVsBP final : public ObjectVsBroadPhaseLayerFilter {
public: bool ShouldCollide(ObjectLayer, BroadPhaseLayer) const override { return true; }
};
class ObjPair final : public ObjectLayerPairFilter {
public: bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override { return a == Layers::MOVING || b == Layers::MOVING; }
};

struct World {
  BPLayers bp; ObjVsBP ovb; ObjPair op;
  std::unique_ptr<TempAllocatorImpl> temp;
  std::unique_ptr<JobSystemThreadPool> jobs;
  std::unique_ptr<PhysicsSystem> sys;
  BodyID terrain;
  Ref<CharacterVirtual> character;
  RefConst<Shape> standShape, crouchShape;
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
    if (p.shape == Part::BOX) {
      vec3 h = glm::max(p.size * 0.5f, vec3(0.005f));
      float cr = std::min(0.02f, std::min(h.x, std::min(h.y, h.z)) * 0.5f);
      s = new BoxShape(jv(h), cr);
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

void Physics::setTerrain(const std::vector<float>& heights, int n, float size) {
  auto& bi = W->sys->GetBodyInterface();
  if (!W->terrain.IsInvalid()) { bi.RemoveBody(W->terrain); bi.DestroyBody(W->terrain); --W->bodies; }
  float cell = size / (n - 1);
  HeightFieldShapeSettings hs(heights.data(), Vec3(-size * 0.5f, 0, -size * 0.5f), Vec3(cell, 1, cell), (uint32)n);
  auto r = hs.Create();
  if (r.HasError()) { logf("heightfield error: %s", r.GetError().c_str()); return; }
  BodyCreationSettings b(r.Get(), RVec3::sZero(), Quat::sIdentity(), EMotionType::Static, Layers::NON_MOVING);
  b.mUserData = 0;
  W->terrain = bi.CreateAndAddBody(b, EActivation::DontActivate);
  ++W->bodies;
}

uint32_t Physics::addStatic(const ShapePtr& s, vec3 pos, quat rot, uint64_t ud) {
  if (!s) return 0xffffffffu;
  BodyCreationSettings b(s->s.GetPtr(), jr(pos), jq(rot), EMotionType::Static, Layers::NON_MOVING);
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

bool Physics::castRay(vec3 o, vec3 d, float maxDist, float& t, uint64_t& ud) {
  RRayCast ray{jr(o), jv(glm::normalize(d) * maxDist)};
  RayCastResult hit;
  if (!W->sys->GetNarrowPhaseQuery().CastRay(ray, hit)) return false;
  t = hit.mFraction * maxDist;
  ud = W->sys->GetBodyInterface().GetUserData(hit.mBodyID);
  return true;
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
  W->crouched = false;
}
void Physics::destroyCharacter() { if (W) W->character = nullptr; }
bool Physics::hasCharacter() { return W && W->character; }
void Physics::setCharacterPos(vec3 feet) {
  if (!W->character) return;
  W->character->SetPosition(jr(feet));
  W->character->SetLinearVelocity(Vec3::sZero());
}

void Physics::updateCharacter(float dt, vec3 hv, bool jump, bool wantCrouch) {
  auto& c = W->character;
  if (!c) return;
  auto bpf = W->sys->GetDefaultBroadPhaseLayerFilter(Layers::MOVING);
  auto olf = W->sys->GetDefaultLayerFilter(Layers::MOVING);
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
  s.groundUser = c->GetGroundBodyID().IsInvalid() ? ~0ull : W->sys->GetBodyInterface().GetUserData(c->GetGroundBodyID());
  return s;
}
