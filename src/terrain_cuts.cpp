// Terrain cuts (SPEC 3.6 trenches / AT ditches / foxholes, navmeta.h TerrainCut): placed assets lower the ground
// inside their outline. A cut is a flat floor at (instance y - depth); from the outline inward the ground ramps
// linearly down over `bank` metres (instance y at the outline), and the result never rises above the base ground.
// The cut ground is the "authored base" of the deformation tiles (Tile::base): craters and their persisted deltas are
// relative to it, so a saved map never stores the cut and reloading never cuts twice.
// Cuts are resolved on the 0.45 m tile grid, so every outline is padded outward by `pad` (one grid step by default):
// the full-depth floor then reaches the outline, where assets put their revetments, and the unavoidable grid
// transition lies just outside it (behind the wall) instead of rising in a sawtooth in front of it.
#include "terrain.h"
#include <algorithm>

namespace {
// signed distance from p to the polygon boundary: > 0 inside
float insideDistance(const std::vector<vec2>& poly, vec2 p) {
  bool in = false; float best = 1e30f;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    vec2 a = poly[j], b = poly[i];
    if (((b.y > p.y) != (a.y > p.y)) && (p.x < (a.x - b.x) * (p.y - b.y) / (a.y - b.y) + b.x)) in = !in;
    vec2 ab = b - a; float L2 = glm::dot(ab, ab);
    float t = L2 > 1e-12f ? std::clamp(glm::dot(p - a, ab) / L2, 0.0f, 1.0f) : 0.0f;
    best = std::min(best, glm::length(p - (a + ab * t)));
  }
  return in ? best : -best;
}
float cutTarget(const WorldCut& c, vec2 p) {
  if (p.x < c.bmin.x || p.y < c.bmin.y || p.x > c.bmax.x || p.y > c.bmax.y) return 1e30f;
  float d = insideDistance(c.poly, p) + c.pad;
  if (d <= 0) return 1e30f;
  float ramp = c.bank > 1e-4f ? std::min(1.0f, d / c.bank) : 1.0f;
  return c.topY + (c.floorY - c.topY) * ramp;
}
bool overlaps(vec2 alo, vec2 ahi, vec2 blo, vec2 bhi) { return alo.x <= bhi.x && alo.y <= bhi.y && blo.x <= ahi.x && blo.y <= ahi.y; }
}

float Terrain::cutBase(float x, float z, uint64_t excludeOwner) const {
  float b = groundBase(x, z);   // V8: cuts lower the sculpted ground
  for (auto& c : cuts) {
    if (excludeOwner && c.owner == excludeOwner) continue;
    b = std::min(b, cutTarget(c, vec2(x, z)));
  }
  return b;
}

float Terrain::placementHeight(float x, float z, uint64_t owner) const {
  float hh = height(x, z);
  if (!owner || cuts.empty()) return hh;
  bool mine = false;
  for (auto& c : cuts) if (c.owner == owner && x >= c.bmin.x && z >= c.bmin.y && x <= c.bmax.x && z <= c.bmax.y) { mine = true; break; }
  if (!mine) return hh;
  // the tile's own samples with this owner's cut taken out (crater deltas kept), interpolated exactly like height()
  float ts = tileSize();
  int tx = (int)std::floor((x - org.x) / ts), tz = (int)std::floor((z - org.y) / ts);
  const Tile* t = tileAt(tx, tz);
  if (!t) return cutBase(x, z, owner);
  vec2 o = tileOrigin(tx, tz);
  float fs = fineSpacing();
  float fx = std::clamp((x - o.x) / fs, 0.0f, kFine - 0.0001f), fz = std::clamp((z - o.y) / fs, 0.0f, kFine - 0.0001f);
  int xi = (int)fx, zi = (int)fz; float u = fx - xi, v = fz - zi;
  auto S = [&](int i, int j) { size_t k = (size_t)j * kFineN + i; return t->h[k] - t->base[k] + cutBase(o.x + i * fs, o.y + j * fs, owner); };
  float a = S(xi, zi), b = S(xi + 1, zi), c = S(xi, zi + 1), d = S(xi + 1, zi + 1);
  if (u >= v) return a + (b - a) * u + (d - b) * v;
  return a + (c - a) * v + (d - c) * u;
}

void Terrain::recutTile(Tile& t) {
  vec2 o = tileOrigin(t.tx, t.tz);
  float fs = fineSpacing();
  t.cut = false;
  for (int j = 0; j < kFineN; ++j)
    for (int i = 0; i < kFineN; ++i) {
      float x = o.x + i * fs, z = o.y + j * fs, raw = groundBase(x, z);
      float nb = cutBase(x, z);
      size_t k = (size_t)j * kFineN + i;
      float delta = t.h[k] - t.base[k];   // crater deltas survive a re-cut
      t.base[k] = nb; t.h[k] = nb + delta;
      t.cut |= nb < raw - 1e-5f;
    }
}

int Terrain::setCuts(std::vector<WorldCut> next) {
  if (next == cuts) return 0;   // the per-frame sync: nothing moved
  for (auto& c : next) {
    c.bmin = vec2(1e30f); c.bmax = vec2(-1e30f);
    for (auto& p : c.poly) { c.bmin = glm::min(c.bmin, p); c.bmax = glm::max(c.bmax, p); }
    c.bmin -= vec2(c.pad); c.bmax += vec2(c.pad);
  }
  // regions that change: cuts that were removed, added or edited
  std::vector<std::pair<vec2, vec2>> regions;
  for (auto& c : cuts) if (std::find(next.begin(), next.end(), c) == next.end()) regions.push_back({c.bmin, c.bmax});
  for (auto& c : next) if (std::find(cuts.begin(), cuts.end(), c) == cuts.end()) regions.push_back({c.bmin, c.bmax});
  cuts = std::move(next);
  if (regions.empty() || h.empty()) return 0;
  ++cutRev;
  const float ts = tileSize(), pad = fineSpacing();
  std::set<int> keys;
  for (auto& [lo, hi] : regions) {
    int tx0 = std::max(0, (int)std::floor((lo.x - pad - org.x) / ts)), tx1 = std::min(tilesX() - 1, (int)std::floor((hi.x + pad - org.x) / ts));
    int tz0 = std::max(0, (int)std::floor((lo.y - pad - org.y) / ts)), tz1 = std::min(tilesZ() - 1, (int)std::floor((hi.y + pad - org.y) / ts));
    for (int tz = tz0; tz <= tz1; ++tz) for (int tx = tx0; tx <= tx1; ++tx) keys.insert(key(tx, tz));
  }
  int touched = 0;
  std::set<int> remesh;
  for (int k : keys) {
    int tx = keyTx(k), tz = keyTz(k);
    vec2 tlo = tileOrigin(tx, tz), thi = tlo + vec2(ts);
    bool wantCut = false;
    for (auto& c : cuts) if (overlaps(c.bmin, c.bmax, tlo - vec2(pad), thi + vec2(pad))) { wantCut = true; break; }
    auto it = tiles.find(k);
    if (it == tiles.end()) {
      if (!wantCut) continue;
      Tile* t = activate(tx, tz);   // starts at the cut base
      if (!t) continue;
      remesh.insert(k); ++touched;
      continue;
    }
    Tile& t = it->second;
    recutTile(t);
    ++touched;
    bool untouched = !t.cut && !sculpt.count(k);   // V8: sculpted tiles stay
    if (untouched) for (size_t s = 0; s < t.h.size() && untouched; ++s) untouched = std::fabs(t.h[s] - t.base[s]) < 1e-5f && t.dirt[s] == 0.0f;
    if (untouched) {   // nothing left on this tile: back to the base mesh and the chunk collider
      if (t.vao) glDeleteVertexArrays(1, &t.vao);
      if (t.vbo) glDeleteBuffers(1, &t.vbo);
      if (t.ebo) glDeleteBuffers(1, &t.ebo);
      if (t.body != 0xffffffffu) stream.deadBodies.push_back(t.body);
      tiles.erase(it);
      setMask(tx, tz, 0);
      for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) if (tileAt(tx + dx, tz + dz)) remesh.insert(key(tx + dx, tz + dz));
      continue;
    }
    physDirty.insert(k);
    remesh.insert(k);
  }
  // neighbours share border normals
  std::set<int> all = remesh;
  for (int k : remesh) {
    int tx = keyTx(k), tz = keyTz(k);
    for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) if (tileAt(tx + dx, tz + dz)) all.insert(key(tx + dx, tz + dz));
  }
  for (int k : all) { auto it = tiles.find(k); if (it != tiles.end()) rebuildMesh(it->second); }
  ++revision;
  return touched;
}
