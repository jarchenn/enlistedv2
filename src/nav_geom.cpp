// Navmesh input from the Jolt world: every static body (terrain tiles, height patches, structures, wall compounds,
// stumps, defences) and the player-only traversal volumes (barbed wire, marked forbidden). Movable leaves (doors,
// windows, shutters: kinematic) and simulated debris (dynamic) are left out, so doorways stay open in the mesh.
#include "nav_geom.h"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseQuery.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>

JPH::PhysicsSystem* navPhysicsSystem();   // physics.cpp (one-line hook)

struct NavBodySet {
  struct B { JPH::RefConst<JPH::Shape> s; JPH::Vec3 com; JPH::Quat rot; uint8_t kind; };
  std::vector<B> bodies;
};

int navBodyCount(const NavBodySet& set) { return (int)set.bodies.size(); }

std::shared_ptr<NavBodySet> navCollectBodies(vec3 lo, vec3 hi, const std::function<int(uint64_t)>& classify) {
  using namespace JPH;
  auto out = std::make_shared<NavBodySet>();
  PhysicsSystem* sys = navPhysicsSystem();
  if (!sys) return out;
  AllHitCollisionCollector<CollideShapeBodyCollector> col;
  sys->GetBroadPhaseQuery().CollideAABox(AABox(Vec3(lo.x, lo.y, lo.z), Vec3(hi.x, hi.y, hi.z)), col);
  const BodyLockInterface& li = sys->GetBodyLockInterfaceNoLock();   // main thread, between steps: nothing moves
  for (const BodyID& id : col.mHits) {
    BodyLockRead lock(li, id);
    if (!lock.Succeeded()) continue;
    const Body& b = lock.GetBody();
    if (!b.IsStatic()) continue;                       // doors / windows (kinematic), debris and grenades (dynamic)
    uint64_t ud = b.GetUserData();
    int kind = b.GetObjectLayer() == 2 ? NT_WIRE : (classify ? classify(ud) : NT_SOLID);   // layer 2 = PLAYER_ONLY (physics.cpp)
    if (kind == NT_SKIP) continue;
    out->bodies.push_back({b.GetShape(), b.GetCenterOfMassPosition(), b.GetRotation(), (uint8_t)kind});
  }
  return out;
}

void navTriangles(const NavBodySet& set, vec3 lo, vec3 hi, NavTris& out) {
  using namespace JPH;
  AABox box(Vec3(lo.x, lo.y, lo.z), Vec3(hi.x, hi.y, hi.z));
  constexpr int kBatch = 64;
  Float3 tv[kBatch * 3];
  for (const auto& b : set.bodies) {
    // compounds / decorated shapes have no triangles of their own: collect the leaf shapes in the box first
    AllHitCollisionCollector<TransformedShapeCollector> leaves;
    b.s->CollectTransformedShapes(box, b.com, b.rot, Vec3::sReplicate(1.0f), SubShapeIDCreator(), leaves, ShapeFilter());
    for (const TransformedShape& ts : leaves.mHits) {
    Shape::GetTrianglesContext ctx;
    ts.GetTrianglesStart(ctx, box, RVec3::sZero());
    for (;;) {
      int n = ts.GetTrianglesNext(ctx, kBatch, tv);
      if (n <= 0) break;
      for (int k = 0; k < n; ++k) {
        const Float3* p = &tv[k * 3];
        float mnx = std::min({p[0].x, p[1].x, p[2].x}), mxx = std::max({p[0].x, p[1].x, p[2].x});
        float mnz = std::min({p[0].z, p[1].z, p[2].z}), mxz = std::max({p[0].z, p[1].z, p[2].z});
        float mny = std::min({p[0].y, p[1].y, p[2].y}), mxy = std::max({p[0].y, p[1].y, p[2].y});
        if (mxx < lo.x || mnx > hi.x || mxz < lo.z || mnz > hi.z || mxy < lo.y || mny > hi.y) continue;   // coarse culling only upstream
        int base = (int)(out.v.size() / 3);
        for (int c = 0; c < 3; ++c) { out.v.push_back(p[c].x); out.v.push_back(p[c].y); out.v.push_back(p[c].z); }
        out.t.push_back(base); out.t.push_back(base + 1); out.t.push_back(base + 2);
        out.kind.push_back(b.kind);
      }
    }
    }
  }
}
