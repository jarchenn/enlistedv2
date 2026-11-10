// Terrain cuts (SPEC 3.6 trenches / AT ditches / foxholes, navmeta.h TerrainCut): placed assets lower the ground
// inside their outline. A cut is a flat floor at (instance y - depth); from the outline inward the ground ramps
// linearly down over `bank` metres (instance y at the outline), and the result never rises above the base ground.
// The cut ground is the "authored base" of the deformation tiles (Tile::base): craters and their persisted deltas are
// relative to it, so a saved map never stores the cut and reloading never cuts twice.
// Cuts are resolved on the 0.45 m tile grid, so every outline is padded outward by `pad` (one grid step by default):
// the full-depth floor then reaches the outline, where assets put their revetments, and the unavoidable grid
// transition lies just outside it (behind the wall) instead of rising in a sawtooth in front of it.
//
// GX1 stamps (docs/graphics-overhaul.md#gx1-terrain-conformance) live in the same layer: pads, bunker berms and trench
// networks are derived from the instances (terrain_blend.cpp) and evaluated here, over the sculpted ground and
// before the polygon cuts. Order: pad rings -> berms -> trench parapets -> pinned footprints (exact) -> trench carves.
#include "terrain.h"
#include <algorithm>

float terrainFbm(float x, float z, uint32_t s);   // terrain.cpp

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

float sstep(float e0, float e1, float x) { float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f); return t * t * (3 - 2 * t); }
vec2 toLocal(const TerrainStamp& s, float x, float z) {   // inverse of yawQuat about +Y on (x, z)
  float ca = std::cos(radians(s.yaw)), sa = std::sin(radians(s.yaw));
  float dx = x - s.c.x, dz = z - s.c.y;
  return vec2(ca * dx - sa * dz, sa * dx + ca * dz);
}
float rectOut(vec2 l, vec2 half) { return glm::length(glm::max(glm::abs(l) - half, vec2(0))); }   // 0 inside
float rectOut(vec2 l, vec4 r) { vec2 c((r.x + r.z) * 0.5f, (r.y + r.w) * 0.5f), h(std::fabs(r.z - r.x) * 0.5f, std::fabs(r.w - r.y) * 0.5f); return rectOut(l - c, h); }
float segDist(vec2 p, vec2 a, vec2 b) { vec2 ab = b - a; float L2 = glm::dot(ab, ab); float t = L2 > 1e-12f ? std::clamp(glm::dot(p - a, ab) / L2, 0.0f, 1.0f) : 0.0f; return glm::length(p - (a + ab * t)); }
float segDistT(vec2 p, vec2 a, vec2 b, float& t) { vec2 ab = b - a; float L2 = glm::dot(ab, ab); t = L2 > 1e-12f ? std::clamp(glm::dot(p - a, ab) / L2, 0.0f, 1.0f) : 0.0f; return glm::length(p - (a + ab * t)); }
float smin(float a, float b, float k) { float h = std::max(k - std::fabs(a - b), 0.0f) / k; return std::min(a, b) - h * h * k * 0.25f; }
float smax(float a, float b, float k) { float h = std::max(k - std::fabs(a - b), 0.0f) / k; return std::max(a, b) + h * h * k * 0.25f; }
bool excluded(const TerrainStamp& s, uint64_t ex, const std::vector<uint64_t>* ignore) {
  if (ex && std::find(s.members.begin(), s.members.end(), ex) != s.members.end()) return true;
  if (ignore) for (uint64_t m : s.members) if (std::find(ignore->begin(), ignore->end(), m) != ignore->end()) return true;
  return false;
}
// trench geometry at p: the smallest effective lateral distance over the segments and the floor height there.
// square (the carve): joined ends overlap their neighbours by `ext` (the floor half-width, so a corner is a mitre:
// two boxes meeting) and past an end the wall rises at once; otherwise (the parapet) the distance to the segment,
// so the spoil rounds the outside of a corner and the end of a run.
struct TrenchHit { float lat = 1e30f, floorY = 0; bool any = false, front = false; };
TrenchHit trenchAt(const TerrainStamp& s, vec2 p, float ext, bool square, float floorHalf) {
  TrenchHit r;
  for (size_t k = 0; k < s.seg.size(); ++k) {
    vec2 a(s.seg[k].x, s.seg[k].y), b(s.seg[k].z, s.seg[k].w);
    vec2 ab = b - a; float L = glm::length(ab);
    if (L < 1e-4f) continue;
    vec2 u = ab / L;
    float t = glm::dot(p - a, u), lat = std::fabs(u.x * (p.y - a.y) - u.y * (p.x - a.x));
    float e0 = (s.segJoin[k] & 1) ? ext : 0.0f, e1 = (s.segJoin[k] & 2) ? ext : 0.0f;
    float beyond = t < -e0 ? -e0 - t : t > L + e1 ? t - L - e1 : 0.0f;
    float le = beyond <= 0 ? lat : square ? std::max(lat, beyond + floorHalf) : glm::length(vec2(lat, beyond));
    if (le < r.lat) {
      r.lat = le; r.floorY = glm::mix(s.segY[k].x, s.segY[k].y, std::clamp(t / L, 0.0f, 1.0f)); r.any = true;
      float side = u.x * (p.y - a.y) - u.y * (p.x - a.x);   // > 0: left of the segment
      r.front = k < s.segFront.size() && s.segFront[k] != 0 && (side > 0) == (s.segFront[k] > 0);
    }
  }
  return r;
}
}   // namespace

// ================================================================ GX1 stamps
void Terrain::stampsNear(vec2 lo, vec2 hi, std::vector<const TerrainStamp*>& out) const {
  out.clear();
  for (auto& s : stamps) if (overlaps(s.bmin, s.bmax, lo, hi)) out.push_back(&s);
}

float Terrain::stampEval(float x, float z, float g, const std::vector<const TerrainStamp*>& list, uint64_t ex, vec4* mask, bool carve) const {
  float h = g, so = 0, mud = 0, fresh = 0, wet = 0;
  const vec2 p(x, z);
  auto in = [&](const TerrainStamp& s) { return x >= s.bmin.x && z >= s.bmin.y && x <= s.bmax.x && z <= s.bmax.y && !excluded(s, ex, cutIgnore); };
  // 1. pad rings: the levelled footprint blended into the ground around it (raised: embankment, lowered: a cut)
  for (auto* sp_ : list) {
    const TerrainStamp& s = *sp_;
    if (s.kind != TerrainStamp::PAD || !in(s)) continue;
    float d = rectOut(toLocal(s, x, z), s.half) - s.grow;
    if (d <= 0 || d >= s.ring) continue;
    float edge = (s.skirt > 0 && s.y - h > s.skirt) ? h + s.skirt : s.y;   // beyond the cap the retaining skirt holds the pad
    float t = sstep(0.0f, s.ring, d), nh = edge + (h - edge) * t;
    so = std::max(so, std::min(sp.soilMax, std::fabs(nh - h) * sp.soilPerMetre) * (1.0f - t));
    h = nh;
  }
  // 2. berms: earth banks against the walls, angle-of-repose profile, smoothing band past the toe
  const float tanR = std::tan(radians(std::clamp(sp.reposeDeg, 15.0f, 45.0f)));
  for (auto* sp_ : list) {
    const TerrainStamp& s = *sp_;
    if (s.kind != TerrainStamp::BERM || !in(s)) continue;
    vec2 l = toLocal(s, x, z);
    float dCore = rectOut(l, s.half);
    if (dCore <= 0) continue;   // under the bunker: its pad
    float n = terrainFbm(x / sp.noiseScale, z / sp.noiseScale, s.seed + 5);
    float jit = sp.edgeJitter * terrainFbm(x / (sp.noiseScale * 0.6f), z / (sp.noiseScale * 0.6f), s.seed + 9);
    // on ground falling away from the anchor the face runs on down to the natural ground (no step at the toe)
    const float fl = std::min(0.0f, h - s.y);
    float B = fl, toe = 1e30f, slope = 0;
    // repose profile per bank: a flat crest, a rounded shoulder, a straight face at the repose slope (less a margin for
    // the noise and the outline jitter), and a wide concave toe (the sparse-grass band); crest height interpolated along
    // the bank, so a flank tapers down toward an open face
    const float sl = tanR / 1.15f;
    for (size_t k = 0; k < s.banks.size(); ++k) {
      float t;
      float dist = segDistT(l, vec2(s.banks[k].x, s.banks[k].y), vec2(s.banks[k].z, s.banks[k].w), t) + jit * std::min(1.0f, dCore / 1.5f);
      float H = glm::mix(s.bankH[k].x, s.bankH[k].y, t) * (1.0f + sp.noise * n);
      if (H <= 0.01f) continue;
      float lin = H - sl * (dist - sp.crest), kc = 0.3f * H, kt = 0.5f * H;
      float hk = smax(smin(lin, H, kc), fl, kt);
      float dk = sl * std::clamp(std::min(lin + kt * 0.5f, H + kc * 0.5f - lin) / std::max(0.05f, std::min(kt, kc)), 0.0f, 1.0f);
      if (hk > B - sp.blendK) slope = std::max(slope, dk);
      B = k ? smax(B, hk, sp.blendK) : hk;
      toe = std::min(toe, dist - sp.crest - (H - fl) / sl - kt * 0.5f / sl);
    }
    for (size_t k = 0; k < s.holes.size(); ++k) {   // passages and embrasures stay clear
      float e = k < s.holeEdge.size() && s.holeEdge[k] > 0 ? s.holeEdge[k] : sp.holeEdge;
      B = fl + (B - fl) * sstep(0.0f, e, rectOut(l, s.holes[k]));
    }
    float W = 1.0f - sstep(0.0f, sp.smoothing, std::max(0.0f, toe));
    float T = s.y + B;
    if (T > h && W > 0) {
      h += (T - h) * W;
      float deg = degrees(std::atan(slope * W));
      // sparse dry grass over the face, bare packed earth only where it is steepest and in low-frequency patches
      float patch = sstep(0.25f, 0.6f, terrainFbm(x / 3.5f, z / 3.5f, s.seed + 13));
      float sb = 0.13f * sstep(sp.sparseSlope - 4, sp.sparseSlope + 4, deg) + 0.45f * std::max(sstep(sp.bareSlope - 2, sp.bareSlope + 2, deg), patch * sstep(sp.sparseSlope + 4, sp.bareSlope, deg));
      so = std::max(so, sb * W);
    }
    so = std::max(so, 0.7f * (1.0f - sstep(0.0f, sp.contactBand, dCore)));   // dirt band where the concrete meets the earth
    // entrance cuts (a sunken entrance, steps down a bank): the lane is held at the anchor between its retaining walls,
    // the ground beside it no higher than the wall top, and past the open end it ramps up to the natural ground
    for (size_t k = 0; k < s.cuts.size() && k < s.cutP.size(); ++k) {
      vec2 a(s.cuts[k].x, s.cuts[k].y), b(s.cuts[k].z, s.cuts[k].w), ab = b - a;
      float L = glm::length(ab);
      if (L < 1e-3f) continue;
      vec2 u = ab / L;
      float along = glm::dot(l - a, u), lat = std::fabs(u.x * (l.y - a.y) - u.y * (l.x - a.x));
      if (along < 0) continue;   // behind the wall face: the pad and the banks
      const float G = 0.55f;     // ramp and cut-side gradient (~29 degrees, walkable)
      float beyond = std::max(0.0f, along - L), wallH = glm::mix(s.cutP[k].y, s.cutP[k].z, std::min(1.0f, along / L));
      float allowed = s.y + G * beyond + (lat > s.cutP[k].x ? wallH + G * (lat - s.cutP[k].x) : 0.0f);
      if (h > allowed) h = allowed;
      if (lat <= s.cutP[k].x + 0.3f && beyond < 1.0f) so = std::max(so, 0.75f);   // trodden earth on the lane floor
    }
  }
  // 3. trench parapets: spoil heaped either side of the lip, lumpy, fading to the field
  const float lip = sp.floorHalf + sp.carvePad + 0.05f;   // the parapet rises where the carved wall reaches the surface
  for (auto* sp_ : list) {
    const TerrainStamp& s = *sp_;
    if (s.kind != TerrainStamp::TRENCH || !in(s)) continue;
    TrenchHit th = trenchAt(s, p, 0.0f, false, sp.floorHalf);
    if (!th.any) continue;
    float nn = terrainFbm(x / 2.6f, z / 2.6f, s.seed + 3), nw = terrainFbm(x / 4.1f, z / 4.1f, s.seed + 7);
    float r = (th.lat - lip) / (sp.parapetW * (1.0f + 0.22f * nw));
    if (r < -0.2f || r >= 1.0f) continue;
    float P = sp.parapetH * (th.front ? sp.parapetFront : 1.0f) * (1.0f + sp.parapetNoise * nn) * sstep(-0.08f, 0.22f, r) * (1.0f - sstep(0.32f, 1.0f, r));   // fire side low: a man on the firestep sees over it
    P += 0.07f * (terrainFbm(x / 1.3f, z / 1.3f, s.seed + 17) - 0.2f) * sstep(-0.05f, 0.15f, r) * (1.0f - sstep(0.45f, 0.85f, r));   // clods: thrown soil, not a smooth bank
    h += std::max(0.0f, P);
    so = std::max(so, 0.85f * (1.0f - sstep(0.15f, 0.8f, r)));
    // freshly turned dark earth on the crest, breaking up into the sparse dry grass of the outer slope over 1-2 m
    fresh = std::max(fresh, (1.0f - sstep(0.12f, 0.62f + 0.18f * nw, r + 0.12f * (nn - 0.1f))) * (0.8f + 0.2f * nn));
  }
  // 4. pinned footprints: exactly their anchor (a neighbour's ring or berm never moves a building's floor)
  for (auto* sp_ : list) {
    const TerrainStamp& s = *sp_;
    if (s.kind != TerrainStamp::PAD || !in(s)) continue;
    if (rectOut(toLocal(s, x, z), s.half) > s.grow) continue;
    so = std::max(so, std::min(sp.soilMax, std::fabs(s.y - h) * sp.soilPerMetre));
    h = s.y;
  }
  // 5. trench carves: floor, then walls leaning back by `batter`
  if (carve)
    for (auto* sp_ : list) {
      const TerrainStamp& s = *sp_;
      if (s.kind != TerrainStamp::TRENCH || !in(s)) continue;
      TrenchHit th = trenchAt(s, p, sp.floorHalf, true, sp.floorHalf);
      if (!th.any) continue;
      // the floor runs carvePad past the revetment line, so on the 0.45 m grid the wall slope never comes in front of it
      float target = th.floorY + std::max(0.0f, th.lat - sp.floorHalf - sp.carvePad) / std::max(0.02f, sp.batter);
      if (target < h + 0.02f || th.lat < sp.floorHalf + sp.carvePad + 0.5f) {   // floor and cut walls (to the top edge): packed mud, wetter low down
        h = std::min(h, target); so = 1.0f; mud = 1.0f;
        wet = std::max(wet, 1.0f - sstep(0.1f, 1.3f, h - th.floorY));
      }
    }
  if (mask) *mask = vec4(so, mud, std::min(fresh, 1.0f - mud), wet);
  return h;
}

float Terrain::stampHeight(float x, float z, float g, const std::vector<const TerrainStamp*>& list, uint64_t ex, vec4* mask) const {
  return stampEval(x, z, g, list, ex, mask, true);
}

float Terrain::trenchLipY(const TerrainStamp& s, float x, float z) const {
  std::vector<const TerrainStamp*> L;
  stampsNear(vec2(x, z), vec2(x, z), L);
  (void)s;
  return stampEval(x, z, groundBase(x, z), L, 0, nullptr, false);
}

float Terrain::stampSoilAt(float x, float z) const {
  if (tiles.empty()) return 0;
  float ts = tileSize();
  const Tile* t = tileAt((int)std::floor((x - org.x) / ts), (int)std::floor((z - org.y) / ts));
  if (!t || t->sdirt.empty()) return 0;
  vec2 o = tileOrigin(t->tx, t->tz);
  float fs = fineSpacing();
  float fx = std::clamp((x - o.x) / fs, 0.0f, kFine - 0.0001f), fz = std::clamp((z - o.y) / fs, 0.0f, kFine - 0.0001f);
  int xi = (int)fx, zi = (int)fz; float u = fx - xi, v = fz - zi;
  const auto& D = t->sdirt;
  return glm::mix(glm::mix(D[zi * kFineN + xi], D[zi * kFineN + xi + 1], u), glm::mix(D[(zi + 1) * kFineN + xi], D[(zi + 1) * kFineN + xi + 1], u), v);
}

// ================================================================ the composed base
float Terrain::cutBase(float x, float z, uint64_t excludeOwner) const {
  float b = groundBase(x, z);   // V8: cuts lower the sculpted ground
  if (!stamps.empty()) {
    thread_local std::vector<const TerrainStamp*> L;
    stampsNear(vec2(x, z), vec2(x, z), L);
    if (!L.empty()) b = stampEval(x, z, b, L, excludeOwner, nullptr, true);
  }
  for (auto& c : cuts) {
    if (excludeOwner && c.owner == excludeOwner) continue;
    if (cutIgnore && std::find(cutIgnore->begin(), cutIgnore->end(), c.owner) != cutIgnore->end()) continue;   // a moving lasso group
    b = std::min(b, cutTarget(c, vec2(x, z)));
  }
  return b;
}

float Terrain::placementHeight(float x, float z, uint64_t owner) const {
  float hh = height(x, z);
  if (!owner || (cuts.empty() && stamps.empty())) return hh;
  bool mine = false;
  for (auto& c : cuts) if ((c.owner == owner || (cutIgnore && std::find(cutIgnore->begin(), cutIgnore->end(), c.owner) != cutIgnore->end())) && x >= c.bmin.x && z >= c.bmin.y && x <= c.bmax.x && z <= c.bmax.y) { mine = true; break; }
  if (!mine) for (auto& s : stamps) if (x >= s.bmin.x && z >= s.bmin.y && x <= s.bmax.x && z <= s.bmax.y && excluded(s, owner, cutIgnore)) { mine = true; break; }
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
  float fs = fineSpacing(), ts = tileSize();
  std::vector<const TerrainStamp*> L;
  stampsNear(o - vec2(fs), o + vec2(ts + fs), L);
  if (t.sdirt.size() != t.h.size()) t.sdirt.assign(t.h.size(), 0.0f);
  if (t.sew.size() != t.h.size() * 3) t.sew.assign(t.h.size() * 3, 0.0f);
  t.cut = false;
  for (int j = 0; j < kFineN; ++j)
    for (int i = 0; i < kFineN; ++i) {
      float x = o.x + i * fs, z = o.y + j * fs, raw = groundBase(x, z);
      vec4 mk(0.0f);
      float nb = L.empty() ? raw : stampEval(x, z, raw, L, 0, &mk, true), so = mk.x;
      for (auto& c : cuts) nb = std::min(nb, cutTarget(c, vec2(x, z)));
      size_t k = (size_t)j * kFineN + i;
      float delta = t.h[k] - t.base[k];   // crater deltas survive a re-cut
      t.base[k] = nb; t.h[k] = nb + delta; t.sdirt[k] = so;
      t.sew[k * 3] = mk.y; t.sew[k * 3 + 1] = mk.z; t.sew[k * 3 + 2] = mk.w;
      t.cut |= std::fabs(nb - raw) > 1e-5f || so > 0.0f;
    }
}

int Terrain::recutRegions(const std::vector<std::pair<vec2, vec2>>& regions) {
  if (regions.empty() || h.empty()) return 0;
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
    bool want = false;
    for (auto& c : cuts) if (overlaps(c.bmin, c.bmax, tlo - vec2(pad), thi + vec2(pad))) { want = true; break; }
    if (!want) for (auto& s : stamps) if (overlaps(s.bmin, s.bmax, tlo - vec2(pad), thi + vec2(pad))) { want = true; break; }
    auto it = tiles.find(k);
    if (it == tiles.end()) {
      if (!want) continue;
      Tile* t = activate(tx, tz);   // starts at the cut / stamped base
      if (!t) continue;
      it = tiles.find(k);
    } else recutTile(it->second);
    Tile& t = it->second;
    ++touched;
    bool untouched = !t.cut && !sculpt.count(k);   // V8: sculpted tiles stay
    if (untouched) for (size_t s = 0; s < t.h.size() && untouched; ++s) untouched = std::fabs(t.h[s] - t.base[s]) < 1e-5f && t.dirt[s] == 0.0f;
    if (untouched) {   // nothing left on this tile (or a stamp that changes nothing here): the base mesh and chunk collider
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
  return recutRegions(regions);
}

int Terrain::setStamps(std::vector<TerrainStamp> next) {
  if (next == stamps) return 0;
  std::vector<std::pair<vec2, vec2>> regions;
  for (auto& s : stamps) if (std::find(next.begin(), next.end(), s) == next.end()) regions.push_back({s.bmin, s.bmax});
  for (auto& s : next) if (std::find(stamps.begin(), stamps.end(), s) == stamps.end()) regions.push_back({s.bmin, s.bmax});
  stamps = std::move(next);
  if (regions.empty() || h.empty()) return 0;
  ++cutRev;
  return recutRegions(regions);
}
