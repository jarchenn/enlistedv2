// Lasso "Move land" (docs/lasso.md#move-land): see lasso_land.h for the model.
#include "lasso_land.h"
#include "geom2d.h"
#include "ground_paint.h"
#include "terrain_tools.h"
#include <cfloat>
#include <cstring>

namespace {
constexpr float kSculptMin = -60.0f, kSculptMax = 80.0f;
float sstep(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3 - 2 * t); }

// a window of the global fine lattice
struct Grid {
  int i0 = 0, j0 = 0, W = 0, H = 0; float fs = 1; vec2 org{0};
  vec2 at(int x, int z) const { return org + fs * vec2((float)x, (float)z); }
  size_t idx(int x, int z) const { return (size_t)z * W + x; }
  float sample(const std::vector<float>& g, vec2 p) const {   // bilinear, clamped
    vec2 q = (p - org) / fs; int x = (int)std::floor(q.x), z = (int)std::floor(q.y); float fx = q.x - x, fz = q.y - z;
    x = std::clamp(x, 0, W - 2); z = std::clamp(z, 0, H - 2); fx = std::clamp(fx, 0.0f, 1.0f); fz = std::clamp(fz, 0.0f, 1.0f);
    return glm::mix(glm::mix(g[idx(x, z)], g[idx(x + 1, z)], fx), glm::mix(g[idx(x, z + 1)], g[idx(x + 1, z + 1)], fx), fz);
  }
};
Grid makeGrid(const Terrain& T, vec2 lo, vec2 hi) {
  Grid g; g.fs = T.fineSpacing();
  lo = glm::max(lo, T.org); hi = glm::min(hi, T.org + vec2(T.size, T.sizeZ));
  g.i0 = (int)std::floor((lo.x - T.org.x) / g.fs); g.j0 = (int)std::floor((lo.y - T.org.y) / g.fs);
  int i1 = (int)std::ceil((hi.x - T.org.x) / g.fs), j1 = (int)std::ceil((hi.y - T.org.y) / g.fs);
  g.W = std::max(0, i1 - g.i0 + 1); g.H = std::max(0, j1 - g.j0 + 1);
  g.org = T.org + g.fs * vec2((float)g.i0, (float)g.j0);
  return g;
}

// Laplace fill of the cells where `in` is set, held to `g` everywhere else (SOR Gauss-Seidel from the rim mean)
std::vector<float> membrane(const Grid& G, const std::vector<float>& g, const std::vector<char>& in) {
  std::vector<float> u = g;
  double rimSum = 0; int rimN = 0;
  for (int z = 1; z < G.H - 1; ++z) for (int x = 1; x < G.W - 1; ++x) {
    if (in[G.idx(x, z)]) continue;
    bool rim = in[G.idx(x + 1, z)] || in[G.idx(x - 1, z)] || in[G.idx(x, z + 1)] || in[G.idx(x, z - 1)];
    if (rim) { rimSum += g[G.idx(x, z)]; ++rimN; }
  }
  float start = rimN ? (float)(rimSum / rimN) : 0.0f;
  std::vector<int> cells;
  for (int z = 1; z < G.H - 1; ++z) for (int x = 1; x < G.W - 1; ++x) if (in[G.idx(x, z)]) { u[G.idx(x, z)] = start; cells.push_back((int)G.idx(x, z)); }
  if (cells.empty()) return u;
  int n = std::max(G.W, G.H);
  float w = 2.0f / (1.0f + std::sin(3.14159265f / (float)n));   // optimal SOR factor for an n x n box
  for (int it = 0; it < 6 * n + 200; ++it) {
    float worst = 0;
    for (int k : cells) {
      float nv = 0.25f * (u[k - 1] + u[k + 1] + u[k - G.W] + u[k + G.W]);
      float d = (nv - u[k]) * w; u[k] += d; worst = std::max(worst, std::fabs(d));
    }
    if (worst < 2e-5f) break;
  }
  return u;
}
struct Xf {   // world xz map: source -> destination
  vec2 pivot, delta; float c, s;
  // the editor's yaw convention (yawQuat): +yaw turns +x toward -z, as instance yaws do
  vec2 fwd(vec2 p) const { vec2 d = p - pivot; return pivot + delta + vec2(d.x * c + d.y * s, -d.x * s + d.y * c); }
  vec2 inv(vec2 q) const { vec2 d = q - pivot - delta; return pivot + vec2(d.x * c - d.y * s, d.x * s + d.y * c); }
};
}   // namespace

std::vector<float> lassoLandRelief(const App& app, const LassoRegion& r, float falloff, const std::vector<vec2>& pts) {
  std::vector<float> out(pts.size(), 0.0f);
  vec2 lo, hi; if (!r.bounds(lo, hi)) return out;
  const Terrain& T = app.terrain;
  Grid G = makeGrid(T, lo - vec2(falloff + 3), hi + vec2(falloff + 3));
  if (G.W < 4 || G.H < 4) return out;
  std::vector<float> g(G.W * (size_t)G.H); std::vector<char> in(g.size());
  for (int z = 0; z < G.H; ++z) for (int x = 0; x < G.W; ++x) { vec2 p = G.at(x, z); g[G.idx(x, z)] = T.groundBase(p.x, p.y); in[G.idx(x, z)] = r.signedDist(p) > -falloff; }
  std::vector<float> m = membrane(G, g, in);
  for (size_t k = 0; k < pts.size(); ++k) out[k] = T.groundBase(pts[k].x, pts[k].y) - G.sample(m, pts[k]);
  return out;
}

LandMoveReport lassoMoveLand(App& app, const LassoRegion& r, vec2 delta, float yawDeg, float falloff) {
  LandMoveReport rep;
  Terrain& T = app.terrain;
  vec2 lo, hi;
  if (!r.bounds(lo, hi) || T.h.empty()) { rep.why = "draw a region first"; return rep; }
  falloff = std::clamp(falloff, 0.0f, 60.0f);
  Xf M; M.pivot = (lo + hi) * 0.5f; M.delta = delta; M.c = std::cos(radians(yawDeg)); M.s = std::sin(radians(yawDeg));
  // destination bounds: the grown region's corners carried over
  float reach = glm::length(hi - lo) * 0.5f + falloff + 3;
  vec2 dc = M.fwd(M.pivot);
  vec2 wlo = glm::min(lo - vec2(falloff + 3), dc - vec2(reach)), whi = glm::max(hi + vec2(falloff + 3), dc + vec2(reach));
  Grid G = makeGrid(T, wlo, whi);
  if (G.W < 4 || G.H < 4) { rep.why = "the region is outside the terrain"; return rep; }
  if ((size_t)G.W * G.H > (size_t)4 << 20) { rep.why = "too far or too large to move in one go (keep it within about 900 m)"; return rep; }
  const size_t NN = (size_t)G.W * G.H;
  // ---- heights
  std::vector<float> g0(NN), sdS(NN), sdD(NN);
  std::vector<char> inS(NN), inD(NN);
  for (int z = 0; z < G.H; ++z) for (int x = 0; x < G.W; ++x) {
    vec2 p = G.at(x, z); size_t k = G.idx(x, z);
    g0[k] = T.groundBase(p.x, p.y);
    sdS[k] = r.signedDist(p); inS[k] = sdS[k] > -falloff;
    sdD[k] = r.signedDist(M.inv(p)); inD[k] = sdD[k] > -falloff;
  }
  auto weight = [&](float sd) { return falloff > 1e-3f ? sstep((sd + falloff) / falloff) : (sd >= 0 ? 1.0f : 0.0f); };
  std::vector<float> ms = membrane(G, g0, inS);
  std::vector<float> rel(NN, 0.0f), g1(NN);
  for (size_t k = 0; k < NN; ++k) {
    if (inS[k]) { rel[k] = g0[k] - ms[k]; rep.maxRelief = std::max(rep.maxRelief, std::fabs(rel[k])); }
    g1[k] = g0[k] - weight(sdS[k]) * rel[k];
  }
  std::vector<float> md = membrane(G, g1, inD);
  std::vector<float> g2 = g1;
  for (int z = 0; z < G.H; ++z) for (int x = 0; x < G.W; ++x) {
    size_t k = G.idx(x, z); if (!inD[k]) continue;
    float w = weight(sdD[k]);
    float rr = G.sample(rel, M.inv(G.at(x, z)));
    g2[k] = glm::mix(g1[k], md[k] + rr, w);
  }
  // ---- placements that ride along (generated furniture follows its host)
  std::vector<uint64_t> movers;
  for (auto& i : app.doc.instances) if (!(i.extra.is_object() && i.extra.contains("furnish")) && r.contains(vec2(i.pos.x, i.pos.z))) movers.push_back(i.id);
  std::map<uint64_t, float> oldGround;
  for (uint64_t id : movers) if (Instance* i = app.doc.find(id)) oldGround[id] = T.height(i->pos.x, i->pos.z);
  vec2 sc = (lo + hi) * 0.5f;
  std::vector<SeatRec> seats = terrainSeats(app, glm::mix(sc, dc, 0.5f), glm::length(dc - sc) * 0.5f + reach + 8);
  seats.erase(std::remove_if(seats.begin(), seats.end(), [&](const SeatRec& s) { return std::find(movers.begin(), movers.end(), s.id) != movers.end(); }), seats.end());
  // ---- write: one undo step
  app.pushUndo();
  size_t undoMark = app.undoStack.size();
  const int F = Terrain::kFine, N = Terrain::kFineN;
  const float ts = T.tileSize();
  int tx0 = std::max(0, (int)std::floor((G.org.x - T.org.x) / ts)), tx1 = std::min(T.tilesX() - 1, (int)std::floor((G.org.x + G.fs * (G.W - 1) - T.org.x) / ts));
  int tz0 = std::max(0, (int)std::floor((G.org.y - T.org.y) / ts)), tz1 = std::min(T.tilesZ() - 1, (int)std::floor((G.org.y + G.fs * (G.H - 1) - T.org.y) / ts));
  std::vector<int> keys;
  std::map<int, std::vector<float>> origS;   // the tiles' sculpt before (a refused move puts them back)
  bool clamped = false;
  for (int tz = tz0; tz <= tz1; ++tz) for (int tx = tx0; tx <= tx1; ++tx) {
    int key = T.key(tx, tz);
    auto it = T.sculpt.find(key);
    std::vector<float> s = it != T.sculpt.end() ? it->second : std::vector<float>((size_t)N * N, 0.0f);
    bool ch = false;
    for (int j = 0; j < N; ++j) for (int i = 0; i < N; ++i) {
      int gx = tx * F + i - G.i0, gz = tz * F + j - G.j0;
      if (gx < 0 || gz < 0 || gx >= G.W || gz >= G.H) continue;
      size_t k = G.idx(gx, gz);
      if (std::fabs(g2[k] - g0[k]) < 1e-5f) continue;
      float x = T.org.x + (float)(tx * F + i) * G.fs, z = T.org.y + (float)(tz * F + j) * G.fs;
      float v = g2[k] - T.baseHeight(x, z), cv = std::clamp(v, kSculptMin, kSculptMax);
      if (cv != v) clamped = true;
      float& o = s[(size_t)j * N + i];
      if (o != cv) { o = cv; ch = true; }
    }
    if (!ch) continue;
    origS[key] = it != T.sculpt.end() ? it->second : std::vector<float>();
    bool any = false; for (float v : s) if (std::fabs(v) > 1e-5f) { any = true; break; }
    if (any) T.sculpt[key] = std::move(s); else T.sculpt.erase(key);
    keys.push_back(key);
  }
  if (!keys.empty()) T.sculptChanged(keys);
  rep.tiles = (int)keys.size();
  terrainReseat(app, seats);
  // placements: carried by the map, keeping their height over the ground
  bool fit = true; std::string why;
  app.placeIgnore = &movers;
  for (uint64_t id : movers) {
    Instance* i = app.doc.find(id); if (!i) continue;
    vec2 np = M.fwd(vec2(i->pos.x, i->pos.z));
    float rise = i->pos.y - oldGround[id];
    i->pos = vec3(np.x, T.height(np.x, np.y) + rise, np.y); i->yaw = wrapDeg(i->yaw + yawDeg);
    app.bump(*i);
  }
  for (uint64_t id : movers) {
    Instance* i = app.doc.find(id); if (!i) continue;
    const AssetDef* a = app.assetFor(*i);
    PlaceCheck c = app.checkPlacement(a, i->pos, i->yaw, i->id);
    if (!c.ok) { fit = false; why = (a ? a->name : i->assetId) + ": " + c.reason; break; }
    if (!(a && (a->tree.on || a->nature.kind != NatureDef::NONE))) i->pos.y = c.y;   // buildings: the footprint rule at the new spot
  }
  app.placeIgnore = nullptr;
  if (!fit) {   // revert everything (the undo step we pushed holds the old map)
    for (auto& [k, v] : origS) { if (v.empty()) T.sculpt.erase(k); else T.sculpt[k] = v; }
    if (!keys.empty()) T.sculptChanged(keys);
    if (app.undoStack.size() == undoMark) { app.restore(app.undoStack.back()); app.undoStack.pop_back(); }
    rep.why = why; return rep;
  }
  rep.objects = (int)movers.size();
  // paint: the region's texels move, what they leave behind is cleared
  if (!app.doc.groundPaint.empty()) {
    MapDoc before; before.groundPaint = app.doc.groundPaint;
    auto texel = [&](const MapDoc& d, int i, int j) -> uint8_t* {
      int cx = i >= 0 ? i / kPaintChunk : -((-i + kPaintChunk - 1) / kPaintChunk), cz = j >= 0 ? j / kPaintChunk : -((-j + kPaintChunk - 1) / kPaintChunk);
      auto it = const_cast<MapDoc&>(d).groundPaint.find(groundPaintKey(cx, cz));
      if (it == d.groundPaint.end()) return nullptr;
      return &it->second[((size_t)(j - cz * kPaintChunk) * kPaintChunk + (i - cx * kPaintChunk)) * 4];
    };
    auto texelW = [&](int i, int j) -> uint8_t* {
      int cx = i >= 0 ? i / kPaintChunk : -((-i + kPaintChunk - 1) / kPaintChunk), cz = j >= 0 ? j / kPaintChunk : -((-j + kPaintChunk - 1) / kPaintChunk);
      auto& v = app.doc.groundPaint[groundPaintKey(cx, cz)];
      if (v.empty()) v.assign((size_t)kPaintChunk * kPaintChunk * 4, 0);
      return &v[((size_t)(j - cz * kPaintChunk) * kPaintChunk + (i - cx * kPaintChunk)) * 4];
    };
    int a0 = (int)std::floor(lo.x / kPaintTexel), a1 = (int)std::ceil(hi.x / kPaintTexel), b0 = (int)std::floor(lo.y / kPaintTexel), b1 = (int)std::ceil(hi.y / kPaintTexel);
    for (int j = b0; j <= b1; ++j) for (int i = a0; i <= a1; ++i) {
      vec2 c((i + 0.5f) * kPaintTexel, (j + 0.5f) * kPaintTexel);
      if (!r.contains(c)) continue;
      if (uint8_t* t = texel(app.doc, i, j)) { if (t[1] || t[3]) ++rep.texels; std::memset(t, 0, 4); }
    }
    vec2 dlo(FLT_MAX), dhi(-FLT_MAX);
    for (vec2 q : {lo, hi, vec2(lo.x, hi.y), vec2(hi.x, lo.y)}) { vec2 m = M.fwd(q); dlo = glm::min(dlo, m); dhi = glm::max(dhi, m); }
    for (int j = (int)std::floor(dlo.y / kPaintTexel); j <= (int)std::ceil(dhi.y / kPaintTexel); ++j)
      for (int i = (int)std::floor(dlo.x / kPaintTexel); i <= (int)std::ceil(dhi.x / kPaintTexel); ++i) {
        vec2 s = M.inv(vec2((i + 0.5f) * kPaintTexel, (j + 0.5f) * kPaintTexel));
        if (!r.contains(s)) continue;
        const uint8_t* src = texel(before, (int)std::floor(s.x / kPaintTexel), (int)std::floor(s.y / kPaintTexel));
        if (src && (src[1] || src[3])) std::memcpy(texelW(i, j), src, 4);
        else if (uint8_t* t = texel(app.doc, i, j)) std::memset(t, 0, 4);
      }
    for (auto it = app.doc.groundPaint.begin(); it != app.doc.groundPaint.end();) {
      bool any = false; for (size_t t = 0; t < it->second.size(); t += 4) if (it->second[t + 1] || it->second[t + 3]) { any = true; break; }
      it = any ? std::next(it) : app.doc.groundPaint.erase(it);
    }
    app.doc.paintSig = (app.doc.paintSig * 6364136223846793005ull + 1442695040888963407ull) | 1;
  }
  // water bodies whose seed is inside: carried, the level following the ground under the seed
  for (auto& w : app.doc.waterBodies) {
    if (!r.contains(w.seed)) continue;
    float g0s = G.sample(g0, w.seed);
    for (auto& p : w.outline) p = M.fwd(p);
    w.seed = M.fwd(w.seed);
    w.level += T.groundBase(w.seed.x, w.seed.y) - g0s;
    ++rep.water;
  }
  terrainCommitSculpt(app, keys);
  ++app.groundRev; app.dirty = true;
  if (clamped) app.toast("Move land: some ground hit the sculpt limits (-60 / +80 m).", 4);
  rep.ok = true;
  return rep;
}
