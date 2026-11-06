// Quadric-error mesh simplification (Garland & Heckbert 1997) for baking supplied models to a runtime budget.
// Topology is welded by position so UV / normal seams do not tear; every triangle corner keeps its own source
// vertex attributes (UV, normal, tangent) and only takes the collapsed position, so texture seams survive.
// Thin tubes (barbed wire) lose length segments long before their cross-section: the error of sliding a vertex
// along a gently curved tube is far smaller than flattening it.
#include "simplify.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <tuple>
#include <unordered_map>

namespace {
struct Quadric {
  double a[10] = {0};   // symmetric 4x4: aa ab ac ad bb bc bd cc cd dd
  void addPlane(double x, double y, double z, double d, double w) {
    a[0] += w * x * x; a[1] += w * x * y; a[2] += w * x * z; a[3] += w * x * d;
    a[4] += w * y * y; a[5] += w * y * z; a[6] += w * y * d;
    a[7] += w * z * z; a[8] += w * z * d; a[9] += w * d * d;
  }
  void add(const Quadric& o) { for (int k = 0; k < 10; ++k) a[k] += o.a[k]; }
  double eval(const glm::dvec3& v) const {
    double x = v.x, y = v.y, z = v.z;
    return a[0] * x * x + 2 * a[1] * x * y + 2 * a[2] * x * z + 2 * a[3] * x + a[4] * y * y + 2 * a[5] * y * z + 2 * a[6] * y + a[7] * z * z + 2 * a[8] * z + a[9];
  }
};
struct Cand { double cost; int a, b; uint32_t va, vb; glm::dvec3 p; bool operator<(const Cand& o) const { return cost > o.cost; } };
}

void simplifyMesh(std::vector<TexVertex>& verts, std::vector<uint32_t>& idx, size_t targetTris) {
  size_t nt = idx.size() / 3;
  if (targetTris >= nt || nt == 0) return;
  // ---- weld by position
  std::vector<int> wid(verts.size());
  std::vector<glm::dvec3> P;
  {
    std::map<std::tuple<long, long, long>, int> at;
    for (size_t k = 0; k < verts.size(); ++k) {
      auto key = std::make_tuple(std::lround(verts[k].pos.x * 1e6), std::lround(verts[k].pos.y * 1e6), std::lround(verts[k].pos.z * 1e6));
      auto it = at.find(key);
      if (it == at.end()) { it = at.emplace(key, (int)P.size()).first; P.push_back(glm::dvec3(verts[k].pos)); }
      wid[k] = it->second;
    }
  }
  int nv = (int)P.size();
  std::vector<std::array<int, 3>> F;  F.reserve(nt);     // welded ids
  std::vector<std::array<uint32_t, 3>> C; C.reserve(nt); // source vertex per corner
  for (size_t t = 0; t < nt; ++t) {
    int a = wid[idx[3 * t]], b = wid[idx[3 * t + 1]], c = wid[idx[3 * t + 2]];
    if (a == b || b == c || a == c) continue;
    F.push_back({a, b, c}); C.push_back({idx[3 * t], idx[3 * t + 1], idx[3 * t + 2]});
  }
  std::vector<uint32_t> repV(nv, 0);   // a source vertex standing for each welded vertex (its attributes win on collapse)
  for (size_t k = verts.size(); k-- > 0;) repV[wid[k]] = (uint32_t)k;
  std::vector<std::vector<int>> vf(nv);
  for (size_t f = 0; f < F.size(); ++f) for (int c = 0; c < 3; ++c) vf[F[f][c]].push_back((int)f);
  std::vector<uint8_t> fdead(F.size(), 0), vdead(nv, 0);
  std::vector<uint32_t> ver(nv, 0);
  std::vector<Quadric> Q(nv);
  auto fnormal = [&](int f, glm::dvec3& n, double& area) {
    glm::dvec3 e = glm::cross(P[F[f][1]] - P[F[f][0]], P[F[f][2]] - P[F[f][0]]);
    area = glm::length(e) * 0.5; n = area > 1e-30 ? e / (2 * area) : glm::dvec3(0);
  };
  for (size_t f = 0; f < F.size(); ++f) {
    glm::dvec3 n; double ar; fnormal((int)f, n, ar);
    double d = -glm::dot(n, P[F[f][0]]);
    for (int c = 0; c < 3; ++c) Q[F[f][c]].addPlane(n.x, n.y, n.z, d, ar);
  }
  // border edges: a plane through the edge perpendicular to the face keeps open ends from shrinking
  {
    std::map<std::pair<int, int>, int> ec;
    for (size_t f = 0; f < F.size(); ++f) for (int c = 0; c < 3; ++c) { int a = F[f][c], b = F[f][(c + 1) % 3]; ec[{std::min(a, b), std::max(a, b)}]++; }
    for (size_t f = 0; f < F.size(); ++f) {
      glm::dvec3 n; double ar; fnormal((int)f, n, ar);
      for (int c = 0; c < 3; ++c) {
        int a = F[f][c], b = F[f][(c + 1) % 3];
        if (ec[{std::min(a, b), std::max(a, b)}] != 1) continue;
        glm::dvec3 e = P[b] - P[a]; double L = glm::length(e); if (L < 1e-12) continue;
        glm::dvec3 pn = glm::cross(e / L, n); double pl = glm::length(pn); if (pl < 1e-12) continue; pn /= pl;
        double w = L * L * 10.0, d = -glm::dot(pn, P[a]);
        Q[a].addPlane(pn.x, pn.y, pn.z, d, w); Q[b].addPlane(pn.x, pn.y, pn.z, d, w);
      }
    }
  }
  std::priority_queue<Cand> heap;
  auto push = [&](int a, int b) {
    Quadric q = Q[a]; q.add(Q[b]);
    glm::dvec3 cands[3] = {P[a], P[b], (P[a] + P[b]) * 0.5};
    int best = 0; double bc = q.eval(cands[0]);
    for (int k = 1; k < 3; ++k) { double c = q.eval(cands[k]); if (c < bc) { bc = c; best = k; } }
    heap.push({std::max(0.0, bc), a, b, ver[a], ver[b], cands[best]});
  };
  for (size_t f = 0; f < F.size(); ++f) for (int c = 0; c < 3; ++c) { int a = F[f][c], b = F[f][(c + 1) % 3]; if (a < b) push(a, b); }
  size_t alive = F.size();
  auto flips = [&](int v, int other, const glm::dvec3& np) {
    for (int f : vf[v]) {
      if (fdead[f]) continue;
      auto& t = F[f];
      if (t[0] == other || t[1] == other || t[2] == other) continue;   // collapses away
      glm::dvec3 p0 = P[t[0]], p1 = P[t[1]], p2 = P[t[2]];
      glm::dvec3 n0 = glm::cross(p1 - p0, p2 - p0);
      for (int c = 0; c < 3; ++c) if (t[c] == v) { if (c == 0) p0 = np; else if (c == 1) p1 = np; else p2 = np; }
      glm::dvec3 n1 = glm::cross(p1 - p0, p2 - p0);
      double l0 = glm::length(n0), l1 = glm::length(n1);
      if (l1 < 1e-18) return true;
      if (l0 > 1e-18 && glm::dot(n0, n1) / (l0 * l1) < 0.2) return true;
    }
    return false;
  };
  while (alive > targetTris && !heap.empty()) {
    Cand c = heap.top(); heap.pop();
    if (vdead[c.a] || vdead[c.b] || ver[c.a] != c.va || ver[c.b] != c.vb) continue;
    if (flips(c.a, c.b, c.p) || flips(c.b, c.a, c.p)) continue;   // would fold a face over: drop this candidate
    int a = c.a, b = c.b;
    P[a] = c.p; Q[a].add(Q[b]); vdead[b] = 1; ++ver[a];
    for (int f : vf[b]) {
      if (fdead[f]) continue;
      auto& t = F[f];
      bool hasA = t[0] == a || t[1] == a || t[2] == a;
      if (hasA) { fdead[f] = 1; --alive; continue; }
      for (int k = 0; k < 3; ++k) if (t[k] == b) {
        t[k] = a;
        // take the survivor's attributes unless this corner is across a texture seam from it
        if (glm::length(verts[C[f][k]].uv - verts[repV[a]].uv) < 0.1f) C[f][k] = repV[a];
      }
      vf[a].push_back(f);
    }
    vf[b].clear();
    // compact a's face list and re-queue its edges
    std::vector<int> keep; keep.reserve(vf[a].size());
    for (int f : vf[a]) if (!fdead[f]) keep.push_back(f);
    std::sort(keep.begin(), keep.end()); keep.erase(std::unique(keep.begin(), keep.end()), keep.end());
    vf[a].swap(keep);
    for (int f : vf[a]) for (int k = 0; k < 3; ++k) { int o = F[f][k]; if (o != a) push(std::min(a, o), std::max(a, o)); }
  }
  // ---- rebuild: one output vertex per (final welded position, UV on a 1/1024 grid): corners that collapsed onto
  // the same point share a vertex unless they sit on different sides of a texture seam
  std::unordered_map<uint64_t, uint32_t> out;
  std::vector<TexVertex> NV; std::vector<uint32_t> NI;
  for (size_t f = 0; f < F.size(); ++f) {
    if (fdead[f]) continue;
    for (int k = 0; k < 3; ++k) {
      const glm::vec2& uv = verts[C[f][k]].uv;
      uint64_t qu = (uint64_t)(std::lround(uv.x * 1024.0) & 0xFFFF), qv = (uint64_t)(std::lround(uv.y * 1024.0) & 0xFFFF);
      uint64_t key = ((uint64_t)(uint32_t)F[f][k] << 32) | (qu << 16) | qv;
      auto it = out.find(key);
      if (it == out.end()) { TexVertex v = verts[C[f][k]]; v.pos = glm::vec3(P[F[f][k]]); it = out.emplace(key, (uint32_t)NV.size()).first; NV.push_back(v); }
      NI.push_back(it->second);
    }
  }
  verts.swap(NV); idx.swap(NI);
}
