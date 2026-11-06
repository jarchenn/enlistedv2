#pragma once
// Navmesh input geometry from the physics world (nav_geom.cpp). The body list is gathered on the main thread (between
// physics steps); the triangles are extracted from the immutable shapes on any thread.
#include "common.h"
#include <functional>
#include <memory>

struct NavBodySet;   // opaque: shape references + transforms
enum NavTriKind : uint8_t { NT_SOLID = 0, NT_WIRE = 1, NT_SKIP = 2, NT_CARVE = 3 };
struct NavTris {
  std::vector<float> v;          // xyz
  std::vector<int> t;            // 3 per triangle
  std::vector<uint8_t> kind;     // NavTriKind per triangle
};
// static and player-only bodies whose bounds touch lo..hi; classify(userData) -> NavTriKind (terrain: NT_SOLID)
std::shared_ptr<NavBodySet> navCollectBodies(vec3 lo, vec3 hi, const std::function<int(uint64_t)>& classify);
void navTriangles(const NavBodySet& set, vec3 lo, vec3 hi, NavTris& out);
int navBodyCount(const NavBodySet& set);
