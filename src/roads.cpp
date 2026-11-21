// VFX rev3: authored road cross-sections and pools (see roads.h).
#include "roads.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
struct Road { std::vector<vec2> pts; float w = 1.8f, trackOffset = 0.8f, rutSigma = 0.25f; vec2 lo{1e9f}, hi{-1e9f}; };
struct Pool { vec2 c; float r, waterZ; };
std::vector<Road> gRoads;
std::vector<Pool> gPools;
uint32_t gRev = 0;
float ss(float a, float b, float v) { float t = std::clamp((v - a) / (b - a), 0.0f, 1.0f); return t * t * (3 - 2 * t); }
}  // namespace

void roadsLoad(const json& ex) {
  gRoads.clear(); gPools.clear(); ++gRev;
  if (!ex.is_object()) return;
  for (auto& r : ex.value("vfxRoads", json::array())) {
    Road d; d.w = r.value("w", 1.8f); d.trackOffset = r.value("trackOffset", 0.8f); d.rutSigma = r.value("rutSigma", 0.25f);
    for (auto& p : r.value("pts", json::array()))
      if (p.is_array() && p.size() >= 2) { vec2 q(p[0].get<float>(), p[1].get<float>()); d.pts.push_back(q); d.lo = glm::min(d.lo, q); d.hi = glm::max(d.hi, q); }
    if (d.pts.size() >= 2) gRoads.push_back(d);
  }
  for (auto& p : ex.value("vfxPools", json::array()))
    gPools.push_back({vec2(p.value("x", 0.0f), p.value("z", 0.0f)), p.value("r", 1.0f), p.value("waterZ", 0.0f)});
}

RoadQ roadLateral(float x, float z) {
  RoadQ best; float bd = 1e9f; const vec2 p(x, z);
  for (const Road& r : gRoads) {
    const float reach = 1.22f * r.w + 6.0f;
    if (x < r.lo.x - reach || x > r.hi.x + reach || z < r.lo.y - reach || z > r.hi.y + reach) continue;
    float acc = 0.0f;
    for (size_t k = 0; k + 1 < r.pts.size(); ++k) {
      vec2 a = r.pts[k], u = r.pts[k + 1] - a; float L = glm::length(u);
      if (L < 1e-6f) continue;
      float t = std::clamp(glm::dot(p - a, u) / (L * L), 0.0f, 1.0f);
      float d = glm::length(p - (a + u * t));
      if (d < bd) {
        bd = d;
        float side = u.x * (p.y - a.y) - u.y * (p.x - a.x) > 0 ? 1.0f : -1.0f;   // + = left of travel (relief._poly_dist)
        best.lat = d * side; best.w = r.w; best.s = acc + t * L;
        best.rutDist = std::min(std::fabs(best.lat - r.trackOffset), std::fabs(best.lat + r.trackOffset));
        best.traffic = 1.0f - ss(0.53f * r.w, 1.07f * r.w, d);
      }
      acc += L;
    }
  }
  return best;
}

float poolAt(float x, float z, float groundY) {
  for (const Pool& q : gPools)
    if (glm::length(vec2(x, z) - q.c) < q.r) return std::max(0.0f, q.waterZ - groundY);
  return 0.0f;
}

void roadsUniforms(GLuint p) {
  // per program, only when the data changed (setLighting runs often)
  static std::vector<std::pair<GLuint, uint32_t>> seen;
  for (auto& s : seen) if (s.first == p) { if (s.second == gRev) return; s.second = gRev; goto upload; }
  seen.push_back({p, gRev});
upload:
  GLint lN = glGetUniformLocation(p, "uRoadN");
  if (lN < 0) return;   // this program never asks
  std::vector<vec2> pts; vec4 k(0.0f), box(1e9f, 1e9f, -1e9f, -1e9f);
  if (!gRoads.empty()) {
    const Road& r = gRoads[0];
    for (size_t i = 0; i < r.pts.size() && i < 64; ++i) pts.push_back(r.pts[i]);
    k = vec4(r.w, r.trackOffset, r.rutSigma, 0.0f);
    float reach = 1.22f * r.w + 6.0f; box = vec4(r.lo.x - reach, r.lo.y - reach, r.hi.x + reach, r.hi.y + reach);
  }
  glProgramUniform1i(p, lN, (int)pts.size());
  if (!pts.empty()) glProgramUniform2fv(p, glGetUniformLocation(p, "uRoadPt"), (GLsizei)pts.size(), &pts[0].x);
  glProgramUniform4fv(p, glGetUniformLocation(p, "uRoadK"), 1, &k.x);
  glProgramUniform4fv(p, glGetUniformLocation(p, "uRoadBox"), 1, &box.x);
  std::vector<vec4> pl;
  for (size_t i = 0; i < gPools.size() && i < 16; ++i) pl.push_back(vec4(gPools[i].c, gPools[i].r, gPools[i].waterZ));
  glProgramUniform1i(p, glGetUniformLocation(p, "uPoolN"), (int)pl.size());
  if (!pl.empty()) glProgramUniform4fv(p, glGetUniformLocation(p, "uPool"), (GLsizei)pl.size(), &pl[0].x);
}
