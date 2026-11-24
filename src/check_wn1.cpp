// WN1 --check-wallnormal (headless): does a city wall's bay report its true surface normal to a ray? The cw_wall section's
// twelve rampart bays (its panel states as play-final's content gives them, else the same boxes written here) are built as
// the asset loader builds them (Physics::buildShape per bay, buildCompound over the bays) and placed as a static body at
// yaw 0 and at yaw 37 deg; rays strike its outer face (+z), inner face (-z) and an end face (-x) at several heights. Each
// hit logs hit.normal, the struck leaf box's half extents, world rotation and centre, and the hit point in its frame; the
// normal must lie within 3 deg of the face's true outward direction.
#include "physics.h"
#include "common.h"
#include <cstdio>

namespace {
std::vector<std::vector<Part>> wallBays() {   // the collider boxes of each bay's intact state
  std::vector<std::vector<Part>> out;
  json j; const fs::path pf = fs::path("C:/Users/50bmg/Documents/MapMaker/play-final/content/assets/cw_wall.json");
  try { if (fs::exists(pf)) j = json::parse(readTextFile(pf)); } catch (...) {}
  auto box = [](vec3 c, vec3 s) { Part p; p.shape = Part::BOX; p.center = c; p.size = s; p.collide = true; return p; };
  if (j.contains("panels")) {
    for (auto& pj : j["panels"]) { std::vector<Part> v; for (auto& q : pj["states"][0]) v.push_back(box(jvec3(q["c"]), jvec3(q["s"]))); out.push_back(v); }
    std::printf("[wn1] bays from %s (%zu)\n", pf.string().c_str(), out.size());
  } else {   // the same section, written here: three courses of four bays, the foot with its plinths
    for (int col = 0; col < 4; ++col) { const float x = -4.875f + 3.25f * col;
      out.push_back({box({x, 0.85f, 0}, {3.25f, 1.7f, 4.0f}), box({x, 0.85f, 2.15f}, {3.25f, 1.7f, 0.3f}), box({x, 0.85f, -2.15f}, {3.25f, 1.7f, 0.3f})});
      out.push_back({box({x, 3.5f, 0}, {3.25f, 3.6f, 3.8f})}); out.push_back({box({x, 7.15f, 0}, {3.25f, 3.7f, 3.6f})}); }
    std::printf("[wn1] bays written here (12)\n");
  }
  return out;
}
}

int wallNormalSelfCheck() {
  int fails = 0; auto ok = [&](bool c, const std::string& m) { std::printf("[wn1] %s %s\n", c ? "PASS" : "FAIL", m.c_str()); if (!c) ++fails; };
  if (!Physics::init()) { std::printf("[wn1] FAIL physics init\n"); return 1; }
  std::vector<ShapePtr> bays; for (auto& b : wallBays()) if (auto s = Physics::buildShape(b)) bays.push_back(s);
  ShapePtr wall = Physics::buildCompound(bays);
  for (float yaw : {0.0f, 37.0f}) {
    const vec3 at(200.0f * (yaw > 0 ? 1 : -1), 0, 50); const quat q = glm::angleAxis(glm::radians(yaw), vec3(0, 1, 0));
    Physics::addStatic(wall, at, q, (uint64_t(7) << 16) | 0xFFFF);
    struct Ray { const char* face; vec3 local, outward; };   // a point just outside the face (wall frame) and the face's outward normal
    const Ray rays[] = {{"outer +z @ 0.8 m", {1.0f, 0.8f, 2.3f}, {0, 0, 1}}, {"outer +z @ 3.5 m", {-2.0f, 3.5f, 1.9f}, {0, 0, 1}}, {"outer +z @ 7 m", {0.4f, 7.0f, 1.8f}, {0, 0, 1}},
                        {"inner -z @ 3.5 m", {2.2f, 3.5f, -1.9f}, {0, 0, -1}}, {"inner -z @ 7 m", {-0.3f, 7.0f, -1.8f}, {0, 0, -1}}, {"end -x @ 4 m", {-6.5f, 4.0f, 0.3f}, {-1, 0, 0}},
                        {"outer +z @ 5.6 m (bay seam rows)", {0.05f, 5.6f, 1.8f}, {0, 0, 1}}, {"inner -z near a column seam", {-3.24f, 6.0f, -1.8f}, {0, 0, -1}}};
    for (auto& r : rays) {
      const vec3 n = q * r.outward, target = at + q * r.local, o = target + n * 6.0f;
      const Physics::Probe P = Physics::probeRay(o, -n, 12.0f);
      const float err = P.hit ? glm::degrees(std::acos(std::clamp(glm::dot(glm::normalize(P.normal), n), -1.0f, 1.0f))) : 180.0f;
      char b[512]; std::snprintf(b, sizeof b, "yaw %2.0f %-34s hit %d normal (%+.3f %+.3f %+.3f) vs (%+.3f %+.3f %+.3f): %5.1f deg | leaf %s half (%.3f %.3f %.3f) rot (%.3f %.3f %.3f %.3f) centre (%.2f %.2f %.2f) local (%+.3f %+.3f %+.3f)",
        yaw, r.face, (int)P.hit, P.normal.x, P.normal.y, P.normal.z, n.x, n.y, n.z, err, P.leafType == 1 ? "box" : "other", P.half.x, P.half.y, P.half.z, P.leafRot.w, P.leafRot.x, P.leafRot.y, P.leafRot.z, P.leafCentre.x, P.leafCentre.y, P.leafCentre.z, P.local.x, P.local.y, P.local.z);
      ok(P.hit && err < 3.0f, b);
    }
  }
  std::printf("[wn1] %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
  Physics::shutdown();
  return fails ? 1 : 0;
}
