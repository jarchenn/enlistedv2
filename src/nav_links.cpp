// Automatic off-mesh links (AI_SPEC 3.2). Every boundary edge of a tile's polygon mesh is sampled; from each sample
// a probe walks outward over the raw voxel columns (before ledge filtering) and classifies what lies beyond the edge:
//   up obstacle 0.5-1.6 m with a standable top          -> mantle (two way)
//   obstacle <= 1.1 m high, <= 0.6 m thick, same level on the far side -> vault (two way)
//   lower floor 0.5-1.6 m down                           -> mantle (two way: climb-out of trenches, ditches, craters)
//   lower floor 1.6-3.0 m down                           -> drop (one way)
//   gap <= 1.5 m (no floor or >= 0.9 m deep), same level beyond -> jump (two way)
// Anything taller (anti-tank ditch faces, quarry faces, walls) gets no link, so bots use ramps and gaps.
#include "nav_internal.h"
#include <algorithm>
#include <cmath>

namespace {
enum Cls { SAME, UP, DOWN, TALL, VOID_ };
struct Res { Cls c = VOID_; float y = 0; bool stand = false; };

Res classify(const NavConfig& c, const NavColumns& C, int ci, float ys) {
  Res r;
  if (ci < 0) return r;
  bool same = false, tall = false; float sameY = 0, upTop = -1e9f, downY = -1e9f; bool upStand = false;
  float prevTop = -1e9f, prevClear = 0;
  for (int k = C.start[ci]; k < C.start[ci + 1]; ++k) {
    const NavColumns::Floor& f = C.floors[k];
    float bot = k == C.start[ci] ? -1e9f : prevTop + prevClear;
    prevTop = f.top; prevClear = f.clear;
    bool standable = f.walk && f.clear >= c.crouchHeight - 0.02f;
    if (f.top <= ys + c.climb && f.top >= ys - c.climb) { if (standable) { same = true; sameY = f.top; } continue; }
    if (f.top > ys + c.climb) {
      if (bot >= ys + c.crouchHeight - 0.05f) continue;                 // overhead: a crouching body passes under
      if (f.wire) { tall = true; continue; }                            // barbed wire is never vaulted or climbed
      if (f.top <= ys + c.mantleMax + 0.06f) { if (f.top > upTop) { upTop = f.top; upStand = standable; } }
      else tall = true;
      continue;
    }
    if (standable && f.top >= ys - c.dropMax - 0.15f) downY = std::max(downY, f.top);
  }
  if (tall) { r.c = TALL; return r; }
  if (upTop > -1e8f) { r.c = UP; r.y = upTop; r.stand = upStand; return r; }
  if (same) { r.c = SAME; r.y = sameY; r.stand = true; return r; }
  if (downY > -1e8f) { r.c = DOWN; r.y = downY; r.stand = true; return r; }
  return r;
}

void probe(const NavConfig& c, const NavColumns& C, vec3 P, vec3 n, std::vector<NavLink>& out) {
  // the floor the sample stands on
  int c0 = C.col(P.x, P.z); if (c0 < 0) return;
  float ys = -1e9f, best = 1e9f;
  for (int k = C.start[c0]; k < C.start[c0 + 1]; ++k) { const auto& f = C.floors[k]; if (f.walk && std::fabs(f.top - P.y) < best && std::fabs(f.top - P.y) < 0.7f) { best = std::fabs(f.top - P.y); ys = f.top; } }
  if (ys < -1e8f) return;
  const float step = C.cs;
  const float dMax = c.gapMax + 2 * c.radius + 0.8f;
  std::vector<float> D; std::vector<Res> R;
  for (float d = step; d <= dMax; d += step) { vec3 q = P + n * d; D.push_back(d); R.push_back(classify(c, C, C.col(q.x, q.z), ys)); }
  const int N = (int)R.size();
  int k0 = 0; while (k0 < N && R[k0].c == SAME) ++k0;
  if (k0 >= N || R[k0].c == TALL) return;
  const int landCells = (int)std::ceil((c.radius + 0.3f) / step);
  auto at = [&](int k) { return P + n * D[k]; };
  auto add = [&](int kind, vec3 b, bool bidir, float top) { NavLink l; l.a = P; l.b = b; l.kind = kind; l.bidir = bidir; l.top = top; out.push_back(l); };
  if (R[k0].c == UP) {
    int k1 = k0; float maxTop = -1e9f; while (k1 < N && R[k1].c == UP) { maxTop = std::max(maxTop, R[k1].y); ++k1; }
    float h = maxTop - ys, run = (k1 - k0) * step;
    if (k1 < N && R[k1].c == SAME && run <= c.vaultThick + step * 0.5f && h <= c.vaultMax && h > c.climb) {
      int kl = std::min(N - 1, k1 + (int)std::ceil((c.radius + 0.5f) / step));   // land clear of the far face (copings overhang)
      while (kl > k1 && R[kl].c != SAME) --kl;
      vec3 b = at(kl); b.y = R[kl].y;
      add(LINK_VAULT, b, true, maxTop);
      return;
    }
    if (h >= c.mantleMin && h <= c.mantleMax + 0.06f) {
      int kl = k0 + landCells;
      if (kl < k1 && R[kl].stand && std::fabs(R[kl].y - R[k0].y) < c.climb + 0.05f) { vec3 b = at(kl); b.y = R[kl].y; add(LINK_MANTLE, b, true, R[kl].y); }
    }
    return;
  }
  // DOWN or VOID: a ditch, trench, crater, gap or ledge
  int k1 = k0; float minDepth = 1e9f; bool anyFloor = false; int kd = -1;
  while (k1 < N && (R[k1].c == DOWN || R[k1].c == VOID_)) {
    if (R[k1].c == DOWN) { minDepth = std::min(minDepth, ys - R[k1].y); anyFloor = true; if (kd < 0) kd = k1; }
    ++k1;
  }
  if (k1 < N && R[k1].c == SAME && (k1 - k0) * step <= c.gapMax && (!anyFloor || minDepth >= 0.9f)) {
    int kl = std::min(N - 1, k1 + landCells - 1);
    while (kl > k1 && R[kl].c != SAME) --kl;
    vec3 b = at(kl); b.y = R[kl].y;
    add(LINK_JUMP, b, true, ys);
  }
  if (kd >= 0) {
    float y = R[kd].y, fall = ys - y;
    int kl = kd + landCells;
    if (kl >= N || kl >= k1 || R[kl].c != DOWN || std::fabs(R[kl].y - y) > c.climb) kl = (kd + std::max(kd, k1 - 1)) / 2;
    if (kl < N && R[kl].c == DOWN && std::fabs(R[kl].y - y) <= c.climb) {
      vec3 b = at(kl); b.y = R[kl].y;
      if (fall >= c.mantleMin && fall <= c.mantleMax) add(LINK_MANTLE, b, true, ys);
      else if (fall > c.mantleMax && fall <= c.dropMax) add(LINK_DROP, b, false, ys);
    }
  }
}
}

void navGenerateLinks(const NavConfig& c, const NavColumns& cols, const rcPolyMesh& pm, float tminx, float tminz, float tileW, uint32_t idBase, std::vector<NavLink>& out) {
  const int nvp = pm.nvp;
  std::vector<NavLink> cand;
  for (int i = 0; i < pm.npolys; ++i) {
    const unsigned short* p = &pm.polys[i * nvp * 2];
    uint8_t area = pm.areas[i];
    if (navAreaCost(navarea::toNav(area)) < 0) continue;                  // never from a forbidden polygon
    int nv = 0; vec3 cen(0);
    for (; nv < nvp && p[nv] != RC_MESH_NULL_IDX; ++nv) { const unsigned short* v = &pm.verts[p[nv] * 3]; cen += vec3(pm.bmin[0] + v[0] * pm.cs, pm.bmin[1] + v[1] * pm.ch, pm.bmin[2] + v[2] * pm.cs); }
    if (nv < 3) continue;
    cen /= (float)nv;
    for (int k = 0; k < nv; ++k) {
      if (p[nvp + k] != RC_MESH_NULL_IDX) continue;                       // neighbour polygon or tile portal
      const unsigned short* va = &pm.verts[p[k] * 3]; const unsigned short* vb = &pm.verts[p[(k + 1) % nv] * 3];
      vec3 a(pm.bmin[0] + va[0] * pm.cs, pm.bmin[1] + va[1] * pm.ch, pm.bmin[2] + va[2] * pm.cs);
      vec3 b(pm.bmin[0] + vb[0] * pm.cs, pm.bmin[1] + vb[1] * pm.ch, pm.bmin[2] + vb[2] * pm.cs);
      vec3 d = b - a; d.y = 0; float L = glm::length(d);
      if (L < 0.25f) continue;
      vec3 n = glm::normalize(vec3(d.z, 0, -d.x));
      if (glm::dot(n, (a + b) * 0.5f - cen) < 0) n = -n;
      int ns = std::max(1, (int)std::lround(L / c.linkSpacing));
      for (int s = 0; s < ns; ++s) {
        vec3 P = glm::mix(a, b, (s + 0.5f) / ns);
        if (P.x < tminx || P.x >= tminx + tileW || P.z < tminz || P.z >= tminz + tileW) continue;   // Detour keeps starts inside the tile only
        probe(c, cols, P, n, cand);
      }
    }
  }
  // de-duplicate: one link per kind within the spacing (two-way links also match reversed)
  for (auto& l : cand) {
    bool dup = false;
    for (auto& o : out) {
      if (o.kind != l.kind) continue;
      float s = c.linkSpacing * 0.8f;
      if ((glm::length(o.a - l.a) < s && glm::length(o.b - l.b) < s * 1.5f) || (o.bidir && l.bidir && glm::length(o.a - l.b) < s * 1.5f && glm::length(o.b - l.a) < s * 1.5f)) { dup = true; break; }
    }
    if (dup) continue;
    if (out.size() >= 4000) break;
    l.id = idBase + (uint32_t)out.size();
    out.push_back(l);
  }
}
