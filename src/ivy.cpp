// Ivy on walls (B1 / IV1, docs/asset-format.md#ivy). VR1: every vine in the game is drawn from one model, the ivy pack
// (LOLIPOP, CC-BY-4.0; content/assets/ivy_pack.json, one shared bake of 12 pieces with their own LODs): wall pieces
// climbing a building's stone / brick / rendered facades (or hanging upside down from a wall top as curtains), the
// inside-corner pieces where two facades meet in a re-entrant corner, and ground patches at the wall foot. A piece only
// goes where the facade's 10 cm grid is wall over its whole rectangle, so never across a door or window; a piece over a
// breached bay goes with the bay; the count thins with the grass quality and the Settings toggle (App::ivyOn). The
// colour is a per-instance tint (content/settings/ivy.json: dusty grey through dry brown to orange), close in tone on
// one building, apart between buildings. Drawn instanced through the model-foliage path (wind, culling, shadows).
#include "app.h"
#include "settings.h"
#include <algorithm>
#include <cfloat>
#include <limits>
#include <map>
#include <random>

namespace {
// one exterior wall face in asset space: normal axis n (0 x, 2 z), outward sign, plane, rectangle u (the other
// horizontal axis) x v (y), and the bay it belongs to (-1: static wall)
struct Face { int n; float sign, plane, u0, u1, v0, v1; int panel; };

static bool gIvyAnyWall = false;   // NT2b: a ruin / an abandoned map: ivy takes any rough, opaque, non-metal wall (render, concrete, precast)
bool ivyMaterial(const Material& m) {
  if (gIvyAnyWall && m.roughness >= 0.55f && m.metallic < 0.3f && m.emissive == vec3(0.0f) && m.name.find("glass") == std::string::npos) return true;
  return m.pattern == PAT_RUBBLE || m.pattern == PAT_ASHLAR || m.pattern == PAT_STONE || m.pattern == PAT_BRICKWORK || m.pattern == PAT_BRICK || m.pattern == PAT_LIMEWASH;
}

bool aligned(const Part& p) { return p.shape == Part::BOX && std::fabs(std::fabs(p.rot.w) - 1.0f) < 1e-4f; }
bool inSolid(const std::vector<std::pair<vec3, vec3>>& solids, vec3 q) {
  for (auto& [lo, hi] : solids) if (q.x > lo.x && q.x < hi.x && q.y > lo.y && q.y < hi.y && q.z > lo.z && q.z < hi.z) return true;
  return false;
}
// Vertical faces of axis-aligned stone boxes that look outward: walking out from the face to the asset's bounds
// meets no solid (a partition or the face behind a facade slab always does). Bays only offer their outward face.
// solids: every visible solid box, so a piece is also only kept where nothing stands in front of it (recess, pier).
void collectFaces(const AssetDef& a, std::vector<Face>& out, std::vector<std::pair<vec3, vec3>>& solids) {
  std::vector<Face> cand;
  auto add = [&](const Part& p, int panel, int panelN, float panelSign) {
    if (!aligned(p) || !p.visible) return;
    solids.push_back({p.center - p.size * 0.5f, p.center + p.size * 0.5f});   // visible, solid or not (railings, glass)
    if (std::max(p.size.x, p.size.z) < 1.2f && p.size.y < 2.5f) return;   // PR1: no creeper on small pieces (pumps, bins, cabinets)
    if (!p.collide || (p.bv > 0 && !gIvyAnyWall) || p.mat < 0 || p.mat >= (int)a.materials.size() || !ivyMaterial(a.materials[p.mat]) || p.size.y < 0.3f) return;   // bevelled: trim
    for (int n : {0, 2}) for (float sign : {-1.0f, 1.0f}) {
      if (panel >= 0 && (n != panelN || sign != panelSign)) continue;
      int u = 2 - n;
      float plane = p.center[n] + sign * p.size[n] * 0.5f;
      if (p.size[u] < 0.2f) continue;
      cand.push_back({n, sign, plane, p.center[u] - p.size[u] * 0.5f, p.center[u] + p.size[u] * 0.5f, p.center.y - p.size.y * 0.5f, p.center.y + p.size.y * 0.5f, panel});
    }
  };
  for (auto& p : a.parts) add(p, -1, 0, 0.0f);
  for (size_t k = 0; k < a.panels.size(); ++k) {
    const Panel& pn = a.panels[k];
    if (pn.family != "masonry" || std::fabs(pn.normal.y) > 0.5f) continue;
    int n = std::fabs(pn.normal.x) > 0.5f ? 0 : 2;
    for (auto& p : pn.parts[0]) add(p, (int)k, n, pn.normal[n] > 0 ? 1.0f : -1.0f);
  }
  for (const Face& f : cand) {
    int hits = 0;
    for (float s : {0.25f, 0.5f, 0.75f}) {
      vec3 q(0); q[2 - f.n] = f.u0 + s * (f.u1 - f.u0); q.y = 0.5f * (f.v0 + f.v1);
      for (float e = 0.3f; ; e += 0.4f) {
        q[f.n] = f.plane + f.sign * e;
        if (q[f.n] > a.bmax[f.n] || q[f.n] < a.bmin[f.n]) break;
        if (inSolid(solids, q)) { ++hits; break; }
      }
    }
    if (hits < 2) out.push_back(f);
  }
}

float u01(std::mt19937& g) { return (g() >> 8) * (1.0f / 16777216.0f); }

// growth patterns (IV1 names, kept so authored blocks and Beautify's per-copy "ivy" still mean the same): what each
// lays down in pieces is in growFacade below. The IV1 leaf types ("common", "creeper", ...) no longer change anything:
// every vine is the same model, coloured by the tint.
enum { P_FAN = 1, P_CORNER = 2, P_CURTAIN = 4, P_FRAME = 8, P_TOP = 16, P_SPIRAL = 32, P_GROUND = 64, P_ISLANDS = 128, P_BLANKET = 256 };
const char* const kPatName[9] = {"fan", "corner", "curtain", "frame", "top", "spiral", "ground", "islands", "blanket"};
int patternsOf(const std::string& s) {   // "fan+ground" -> P_FAN | P_GROUND
  int m = 0;
  for (size_t i = 0; i <= s.size();) {
    size_t j = s.find('+', i); std::string w = s.substr(i, j == std::string::npos ? std::string::npos : j - i);
    for (int k = 0; k < 9; ++k) if (w == kPatName[k]) m |= 1 << k;
    if (j == std::string::npos) break; i = j + 1;
  }
  return m;
}
const float kCell = 0.1f;

// one facade's 10 cm cells: 0 nothing (outside, breached), 1 wall, 2 inside the outline but no wall (an opening: glass,
// door, a gap), 3 too deep (a balcony slab in front), 5 a doorway (DR2: never grown); off = the surface in front of the facade plane (trim, a sill, a
// face a few cm proud), so leaves follow it instead of floating or cutting through
struct Grid {
  int nu = 0, nv = 0; float u0 = 0, v0 = 0; std::vector<uint8_t> st; std::vector<float> off;
  int at(float u, float v) const { int i = (int)std::floor((u - u0) / kCell), j = (int)std::floor((v - v0) / kCell); return i < 0 || j < 0 || i >= nu || j >= nv ? -1 : j * nu + i; }
  int s(float u, float v) const { int k = at(u, v); return k < 0 ? 0 : st[k]; }
  // over a rectangle: the share of wall cells, -1 if any cell is an opening or a deep slab (cells off the grid: not wall)
  float fit(float ua, float ub, float va, float vb, float* offMax = nullptr) const {
    int i0 = (int)std::floor((ua - u0) / kCell + 0.5f), i1 = (int)std::floor((ub - u0) / kCell - 0.5f), j0 = (int)std::floor((va - v0) / kCell + 0.5f), j1 = (int)std::floor((vb - v0) / kCell - 0.5f);
    if (i1 < i0 || j1 < j0) return 0.0f;
    int wall = 0, all = 0; float om = 0;
    for (int j = j0; j <= j1; ++j) for (int i = i0; i <= i1; ++i) {
      ++all;
      if (i < 0 || j < 0 || i >= nu || j >= nv) continue;
      int c = st[(size_t)j * nu + i];
      if (c == 2 || c == 3) return -1.0f;
      if (c == 1) { ++wall; om = std::max(om, off[(size_t)j * nu + i]); }
    }
    if (offMax) *offMax = om;
    return all ? wall / (float)all : 0.0f;
  }
  bool anyGone(const Grid& now, float ua, float ub, float va, float vb) const {   // this (intact) grid's wall cells the breached one lost
    int i0 = std::max(0, (int)std::floor((ua - u0) / kCell)), i1 = std::min(nu - 1, (int)std::floor((ub - u0) / kCell)), j0 = std::max(0, (int)std::floor((va - v0) / kCell)), j1 = std::min(nv - 1, (int)std::floor((vb - v0) / kCell));
    for (int j = j0; j <= j1; ++j) for (int i = i0; i <= i1; ++i) { size_t k = (size_t)j * nu + i; if (st[k] == 1 && now.st[k] != 1) return true; }
    return false;
  }
};

// ---------------------------------------------------------------- the pack and the colour settings
struct Pack {
  std::shared_ptr<GpuModel> m, lod;
  struct P { int first = -1, count = 0, firstL = -1, countL = 0; vec3 lo{FLT_MAX}, hi{-FLT_MAX}; };
  std::map<std::string, P> pieces;
  float lodDist = 18, cull = 140, wind = 0.3f; bool ok = false;
};
void ranges(const GpuModel& g, std::map<std::string, Pack::P>& out, bool lod) {
  for (int k = 0; k < (int)g.parts.size(); ++k) {
    const std::string& nm = g.parts[k].name; size_t us = nm.rfind('_');
    if (us == std::string::npos || (nm.compare(us, std::string::npos, "_leaf") != 0 && nm.compare(us, std::string::npos, "_stem") != 0)) continue;
    Pack::P& p = out[nm.substr(0, us)];
    int& f = lod ? p.firstL : p.first; int& c = lod ? p.countL : p.count;
    if (f < 0) f = k;
    c = k - f + 1;   // a piece's leaf and stem parts sit next to each other (parts are sorted by name)
    if (!lod) { p.lo = glm::min(p.lo, g.parts[k].bmin); p.hi = glm::max(p.hi, g.parts[k].bmax); }
  }
}
const Pack& pack(App& app) {
  static Pack P; static bool tried = false;
  if (tried) return P;
  tried = true;
  const AssetDef* a = app.lib.find("ivy_pack");
  if (!a || !a->model) { logf("ivy: the ivy pack (content/assets/ivy_pack.json) is not baked: no ivy is drawn"); return P; }
  P.m = a->model; P.lod = a->modelLod; P.lodDist = a->modelLodDist; P.cull = a->modelCull; P.wind = a->modelWind;
  ranges(*P.m, P.pieces, false);
  if (P.lod) ranges(*P.lod, P.pieces, true);
  for (auto it = P.pieces.begin(); it != P.pieces.end();) it = it->second.count > 0 ? std::next(it) : P.pieces.erase(it);
  P.ok = P.pieces.count("wall_1") && P.pieces.count("corner_1") && P.pieces.count("ground_1");
  logf("ivy: pack with %zu pieces%s", P.pieces.size(), P.ok ? "" : " (incomplete: no ivy is drawn)");
  return P;
}

// content/settings/ivy.json: the leaf colour ramp (sRGB, as the average leaf reads: "grey" -> "brown" -> "orange"),
// where a building's tone sits on it (centre +- spread / 2, uniform), how far a facade and a piece stray from their
// building's tone, a brightness jitter, and the stems' colour
struct Colours { vec3 grey{0.42f, 0.41f, 0.37f}, brown{0.40f, 0.29f, 0.17f}, orange{0.62f, 0.32f, 0.10f}, stem{0.33f, 0.29f, 0.25f};
                 float centre = 0.5f, spread = 1.0f, facadeSpread = 0.08f, pieceSpread = 0.06f, bright = 0.06f; };
const Colours& colours() {
  static const Colours C = [] {
    Colours c;
    try {
      json j = json::parse(readTextFile(contentDir() / "settings" / "ivy.json"));
      auto col = [&](const char* k, vec3& v) { if (j.contains(k) && j[k].is_array() && j[k].size() >= 3) v = vec3(j[k][0].get<float>(), j[k][1].get<float>(), j[k][2].get<float>()); };
      col("grey", c.grey); col("brown", c.brown); col("orange", c.orange); col("stem", c.stem);
      c.centre = j.value("centre", c.centre); c.spread = j.value("spread", c.spread); c.facadeSpread = j.value("facadeSpread", c.facadeSpread);
      c.pieceSpread = j.value("pieceSpread", c.pieceSpread); c.bright = j.value("brightness", c.bright);
    } catch (...) { logf("ivy: content/settings/ivy.json missing or unreadable, default colours"); }
    return c;
  }();
  return C;
}
vec3 lin(vec3 s) { return glm::pow(glm::clamp(s, vec3(0.0f), vec3(1.0f)), vec3(2.2f)); }
vec4 tintAt(float t, float b) {   // t 0 grey .. 0.5 brown .. 1 orange (linear rgb, a = 1: tinted)
  const Colours& C = colours(); t = std::clamp(t, 0.0f, 1.0f);
  vec3 s = t < 0.5f ? glm::mix(C.grey, C.brown, t * 2.0f) : glm::mix(C.brown, C.orange, t * 2.0f - 1.0f);
  return vec4(lin(s) * b, 1.0f);
}
float hash01(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return (x >> 8) * (1.0f / 16777216.0f); }

struct Look { float age; bool overgrown; int flags; float dens; };
}  // namespace

struct IvyEnv { bool abandoned = false; int season = 0; bool arid = false; };   // the open map's look: "style": "abandoned", "season", NT4 "arid"

// The pieces of one instance (asset space). density: the quality share of pieces kept; tone: the building's place on the
// colour ramp. Deterministic per seed; a breached bay's pieces are left out (the layout itself is the intact one's).
void buildIvyPieces(const Pack& PK, const AssetDef& a, const IvyDef& iv, const std::vector<uint8_t>& state, uint32_t seed, float density, const IvyEnv& env, std::vector<IvyPlaced>& out) {
  if (density <= 0 || !PK.ok) return;
  std::mt19937 g(seed * 2654435761u + 0x9e3779b9u);
  const Colours& C = colours();
  const float tone = C.centre + C.spread * (hash01(seed * 7u + 3u) - 0.5f);
  size_t nput = 0;   // pieces laid out (the layout does not depend on the quality; the quality keeps a share of them)
  auto keep = [&](size_t k) { return density >= 1.0f || hash01(seed * 131u + (uint32_t)k * 977u + 11u) < density; };
  auto put = [&](const std::string& id, const mat4& L, float t) {
    auto it = PK.pieces.find(id); if (it == PK.pieces.end()) return;
    const Pack::P& p = it->second;
    IvyPlaced q; q.piece = id; q.first = p.first; q.count = p.count; q.firstL = p.firstL >= 0 ? p.firstL : p.first; q.countL = p.firstL >= 0 ? p.countL : p.count; q.m = L;
    q.tint = tintAt(t + C.pieceSpread * (u01(g) - 0.5f), 1.0f + C.bright * (u01(g) * 2.0f - 1.0f));
    if (keep(nput++)) out.push_back(q);
  };

  if (iv.mode == IvyDef::PIECES) {   // a vine prop: its pieces as authored
    for (auto& p : iv.pieces) {
      mat4 L = glm::translate(mat4(1), p.pos) * glm::rotate(mat4(1), glm::radians(p.yaw), vec3(0, 1, 0));
      if (p.hang) L = L * glm::rotate(mat4(1), 3.14159265f, vec3(0, 0, 1));
      put(p.id, L * glm::scale(mat4(1), p.scale), tone);
    }
    return;
  }

  const bool ruin = a.raw.is_object() && a.raw.value("ruin", false), heavy = ruin || env.abandoned, modern = a.id.rfind("mod_", 0) == 0;
  auto pickW = [&](std::initializer_list<std::pair<int, float>> opts) { float s = 0; for (auto& o : opts) s += o.second; float x = u01(g) * s; for (auto& o : opts) { if ((x -= o.second) <= 0) return o.first; } return opts.begin()->first; };
  auto lookFor = [&](const std::string& pat, float age, bool over, float dens) {
    Look L{};
    L.flags = patternsOf(pat) & ~P_SPIRAL;   // spiral (posts, pipes): no pack piece wraps a post
    if (!L.flags) L.flags = heavy ? pickW({{P_BLANKET, 35}, {P_FAN | P_CURTAIN, 40}, {P_ISLANDS | P_GROUND, 10}, {P_CORNER | P_FRAME, 15}})
                          : modern ? pickW({{P_CORNER, 35}, {P_FRAME, 30}, {P_FAN, 20}, {P_CURTAIN, 15}}) : pickW({{P_FAN | P_GROUND, 50}, {P_CORNER, 25}, {P_FRAME, 25}});
    L.age = age >= 0 ? std::clamp(age, 0.0f, 1.0f) : heavy ? 0.7f + 0.3f * u01(g) : 0.35f + 0.45f * u01(g);
    L.overgrown = over || (heavy && (L.flags & P_BLANKET));
    L.dens = dens * (heavy ? 1.25f : 1.0f) * (env.arid ? 0.45f : 1.0f);
    return L;
  };

  std::vector<Face> faces; std::vector<std::pair<vec3, vec3>> solids; gIvyAnyWall = heavy; collectFaces(a, faces, solids); gIvyAnyWall = false;
  // facades: faces sharing a normal and a plane (within 3 cm)
  struct Facade { int n; float sign, plane, u0 = FLT_MAX, u1 = -FLT_MAX, v0 = FLT_MAX, v1 = -FLT_MAX; std::vector<int> faces; };
  std::vector<Facade> fac;
  for (int f = 0; f < (int)faces.size(); ++f) {
    const Face& F = faces[f];
    Facade* d = nullptr;
    for (auto& q : fac) if (q.n == F.n && q.sign == F.sign && std::fabs(q.plane - F.plane) < 0.03f) { d = &q; break; }
    if (!d) { fac.push_back({F.n, F.sign, F.plane}); d = &fac.back(); }
    d->u0 = std::min(d->u0, F.u0); d->u1 = std::max(d->u1, F.u1); d->v0 = std::min(d->v0, F.v0); d->v1 = std::max(d->v1, F.v1); d->faces.push_back(f);
  }
  auto gridOf = [&](const Facade& d, bool breached) {
    Grid gr; gr.u0 = d.u0; gr.v0 = d.v0; gr.nu = std::max(1, (int)std::ceil((d.u1 - d.u0) / kCell)); gr.nv = std::max(1, (int)std::ceil((d.v1 - d.v0) / kCell));
    gr.st.assign((size_t)gr.nu * gr.nv, 0); gr.off.assign(gr.st.size(), 0.0f);
    auto raster = [&](float u0, float u1, float v0, float v1, auto&& fn) {
      int i0 = std::max(0, (int)std::ceil((u0 - gr.u0) / kCell - 0.5f)), i1 = std::min(gr.nu - 1, (int)std::floor((u1 - gr.u0) / kCell - 0.5f));
      int j0 = std::max(0, (int)std::ceil((v0 - gr.v0) / kCell - 0.5f)), j1 = std::min(gr.nv - 1, (int)std::floor((v1 - gr.v0) / kCell - 0.5f));
      for (int j = j0; j <= j1; ++j) for (int i = i0; i <= i1; ++i) fn(j * gr.nu + i);
    };
    for (int f : d.faces) {
      const Face& F = faces[f];
      bool gone = breached && F.panel >= 0 && F.panel < (int)state.size() && state[F.panel] == PANEL_BREACHED;
      raster(F.u0, F.u1, F.v0, F.v1, [&](int k) { if (gone) gr.st[k] = 4; else if (gr.st[k] != 4) { gr.st[k] = 1; gr.off[k] = std::max(gr.off[k], d.sign * (F.plane - d.plane)); } });
    }
    for (int k = 0; k < (int)gr.st.size(); ++k) if (gr.st[k] == 4) gr.st[k] = 0;   // a breached bay: a hole
    // what stands in front of the plane (trim, sills, pipes, slabs) lifts the surface; past 32 cm: too deep to climb
    int u = 2 - d.n;
    for (auto& [lo, hi] : solids) {
      float back = d.sign > 0 ? lo[d.n] - d.plane : d.plane - hi[d.n], front = d.sign > 0 ? hi[d.n] - d.plane : d.plane - lo[d.n];
      if (back < -0.03f || back > 0.3f || front < 0.004f) continue;
      raster(lo[u], hi[u], lo.y, hi.y, [&](int k) { if (front > 0.32f) gr.st[k] = gr.st[k] == 1 ? 3 : gr.st[k]; else gr.off[k] = std::max(gr.off[k], front); });
    }
    // inside the outline but not wall: an opening (a cell with wall cells both sides, horizontally or vertically)
    for (int j = 0; j < gr.nv; ++j) for (int i = 0; i < gr.nu; ++i) {
      int k = j * gr.nu + i; if (gr.st[k] != 0) continue;
      auto wallTo = [&](int di, int dj) { for (int s = 1; s < 40; ++s) { int x = i + di * s, y = j + dj * s; if (x < 0 || y < 0 || x >= gr.nu || y >= gr.nv) return false; if (gr.st[y * gr.nu + x] == 1) return true; } return false; };
      if ((wallTo(1, 0) && wallTo(-1, 0)) || (wallTo(0, 1) && wallTo(0, -1))) gr.st[k] = 2;
    }
    // DR2: a doorway (a door / gate interactable's aperture near this plane, 5 cm round it and 15 cm over the head) stays
    // bare: no leaves, no hanging curtains, no droop where the leaf swings and people walk
    for (const auto& it : a.interactables) {
      if (it.kind != "door" && it.kind != "gate") continue;
      if (d.plane < it.apMin[d.n] - 0.6f || d.plane > it.apMax[d.n] + 0.6f) continue;
      raster(it.apMin[u] - 0.05f, it.apMax[u] + 0.05f, it.apMin.y - 0.05f, it.apMax.y + 0.15f, [&](int k) { gr.st[k] = 5; });
    }
    return gr;
  };
  std::vector<Grid> intact(fac.size()), now(fac.size());
  for (size_t k = 0; k < fac.size(); ++k) { intact[k] = gridOf(fac[k], false); now[k] = gridOf(fac[k], true); }
  auto nrmOf = [](const Facade& d) { vec3 n(0); n[d.n] = d.sign; return n; };
  auto at = [](const Facade& d, float u, float v, float off) { vec3 p(0); p[2 - d.n] = u; p.y = v; p[d.n] = d.plane + d.sign * off; return p; };

  struct Rect { int f; float u0, u1, v0, v1; };
  std::vector<Rect> taken;
  auto overlap = [&](int f, float ua, float ub, float va, float vb) {   // the share of this rectangle already covered
    float a0 = std::max(1e-4f, (ub - ua) * (vb - va)), s = 0;
    for (auto& r : taken) if (r.f == f) s += std::max(0.0f, std::min(ub, r.u1) - std::max(ua, r.u0)) * std::max(0.0f, std::min(vb, r.v1) - std::max(va, r.v0));
    return s / a0;
  };
  size_t budget = heavy ? 90 : 40;   // pieces a building at most (instanced: the cost is the triangles near the eye)

  // a wall piece on facade f centred at u: climbing from vb0 (or hanging from vb0 down), at most maxH tall, scale in
  // [sMin, sMax]; the largest variant / scale that fits wins. Returns false when none fits.
  auto wallPiece = [&](int f, float tone, float u, float vb0, float maxH, bool hang, float sMin, float sMax, float maxOver = 0.35f) {
    if (nput >= budget) return false;
    const Facade& d = fac[f]; const Grid& gr = intact[f];
    const char* ids[3] = {"wall_1", "wall_2", "wall_3"}; int o0 = (int)(g() % 3);
    const float sxJ = 0.9f + 0.2f * u01(g);
    // tallest first; at each height the authored width, then squeezed (a strip up a pier between two windows)
    for (float s = sMax; s >= sMin - 1e-3f; s -= 0.1f) for (int k = 0; k < 3; ++k) for (float wf : {1.0f, 0.75f, 0.55f}) {
      const auto it = PK.pieces.find(ids[(o0 + k) % 3]); if (it == PK.pieces.end()) continue;
      const Pack::P& P = it->second;
      const float h = (P.hi.y - 0.05f) * s, w = (P.hi.x - P.lo.x) * s * sxJ * wf;
      if (h > maxH + 0.15f || h < 0.4f) continue;
      float ua = u - 0.5f * w, ub = u + 0.5f * w, va = hang ? vb0 - h : vb0, vb = hang ? vb0 : vb0 + h, om = 0;
      if (gr.fit(ua, ub, va, vb, &om) < 0.88f || overlap(f, ua, ub, va, vb) > maxOver) continue;
      if (gr.anyGone(now[f], ua, ub, va, vb)) { taken.push_back({f, ua, ub, va, vb}); ++nput; return true; }   // grew there; the bay is breached now
      vec3 N = nrmOf(d), Y(0, 1, 0), X = glm::cross(Y, N);
      mat4 R(1); R[0] = vec4(X, 0); R[1] = vec4(Y, 0); R[2] = vec4(N, 0);
      mat4 L = glm::translate(mat4(1), at(d, u, hang ? vb0 + 0.08f : vb0 - 0.05f, om + 0.012f)) * R;
      if (hang) L = L * glm::rotate(mat4(1), 3.14159265f, vec3(0, 0, 1));
      L = L * glm::scale(mat4(1), vec3(s * sxJ * wf, s, std::min(1.0f, s) * 0.75f));   // flatter than authored: it hugs the wall
      put(ids[(o0 + k) % 3], L, tone);
      taken.push_back({f, ua, ub, va, vb});
      return true;
    }
    return false;
  };
  // a ground patch spreading out from the foot of facade f, centred at u
  auto groundPiece = [&](int f, float tone, float u) {
    if (nput >= budget) return false;
    const Facade& d = fac[f]; const Grid& gr = intact[f];
    const char* ids[6] = {"ground_1", "ground_2", "ground_3", "gdirect_1", "gdirect_2", "gdirect_3"};
    const auto it = PK.pieces.find(ids[g() % 6]); if (it == PK.pieces.end()) return false;
    const Pack::P& P = it->second;
    const float wT = 1.6f + 1.8f * u01(g), dT = 0.6f + 0.9f * u01(g), ua = u - 0.5f * wT, ub = u + 0.5f * wT;
    const float g0 = std::max(d.v0, 0.0f);
    if (gr.fit(ua, ub, g0, g0 + 0.7f) < 0.5f) return false;   // not before a door or a porch step (an opening or a slab at the foot)
    const float sx = wT / std::max(0.1f, P.hi.x - P.lo.x), sz = dT / std::max(0.1f, P.hi.z - P.lo.z), sy = std::clamp(std::sqrt(sx * sz), 0.35f, 0.9f);
    vec3 N = nrmOf(d), Y(0, 1, 0), X = glm::cross(Y, N);
    mat4 R(1); R[0] = vec4(X, 0); R[1] = vec4(Y, 0); R[2] = vec4(N, 0);
    mat4 L = glm::translate(mat4(1), at(d, u, 0.0f, 0.5f * dT + 0.03f)) * R * glm::scale(mat4(1), vec3(sx, sy, sz));
    put(it->first, L, tone);
    return true;
  };

  // one facade with one look inside [ua, ub] x [va, vb]
  auto growFacade = [&](int f, const Look& L, float ua, float ub, float va, float vb) {
    const Facade& d = fac[f]; const Grid& gr = intact[f];
    const float ft = std::clamp(tone + C.facadeSpread * (u01(g) - 0.5f), 0.0f, 1.0f);   // a facade strays a little from its building
    const float g0 = std::max(va, d.v0), top = std::min(vb, d.v1), H = std::max(0.5f, top - g0), W = ub - ua;
    const float reach = std::max(1.2f, H * (0.35f + 0.75f * L.age));
    const float dens = std::clamp(L.dens, 0.2f, 2.0f);
    auto climbAt = [&](float u, float maxH, float sMin) { return wallPiece(f, ft, u, g0, maxH, false, sMin, std::min(1.5f, maxH / 2.6f + 0.1f)); };
    if (L.flags & P_FAN) {
      int n = std::max(1, (int)std::lround(W / 4.0f * (0.6f + 0.6f * u01(g)) * dens));
      for (int k = 0, tries = 0; k < n && tries < 4 * n + 4; ++tries) if (climbAt(ua + 0.6f + u01(g) * std::max(0.0f, W - 1.2f), reach, 0.45f)) ++k;
    }
    if (L.flags & P_BLANKET) {   // tiled along the wall, a second (third) row where it is tall
      for (float row = g0; row < top - 1.0f; ) {
        float rh = 0;
        for (float u = ua + 0.8f; u < ub - 0.4f; u += 2.2f + 0.8f * u01(g)) if (wallPiece(f, ft, u, row, std::min(3.6f, top - row), false, 0.45f, 1.3f, 0.25f)) rh = std::max(rh, 2.4f);
        if (rh <= 0) break;
        row += rh * (0.8f + 0.1f * u01(g));
      }
    }
    if (L.flags & P_CORNER) for (float e : {ua + 0.75f, ub - 0.75f}) if (u01(g) < 0.8f) for (float k = 0; k < 3; ++k) if (climbAt(e + (e < 0.5f * (ua + ub) ? 1.0f : -1.0f) * 0.3f * k, reach * 1.1f, 0.4f)) break;
    if (L.flags & P_FRAME) {   // the piers beside the openings: a narrow piece in the wall next to the jamb
      int n = 0;
      for (int i = 1; i < gr.nu - 1 && n < 6; ++i) for (int j = 0; j < std::min(gr.nv, 30); ++j) {
        int k = j * gr.nu + i; bool left = gr.st[k] == 1 && gr.st[k + 1] == 2, right = gr.st[k] == 1 && gr.st[k - 1] == 2;
        if (!(left || right) || u01(g) > 0.35f * dens) continue;
        float u = gr.u0 + (i + 0.5f) * kCell + (left ? -0.6f : 0.6f);
        if (u > ua && u < ub && climbAt(u, reach, 0.3f)) ++n;
        break;
      }
    }
    if (L.flags & (P_CURTAIN | P_TOP)) {   // hanging from the wall top (eaves, a parapet): the wall piece upside down
      const float drop = (L.flags & P_CURTAIN) ? 0.8f + 2.5f * L.age : 0.6f + 0.6f * L.age;
      int n = std::max(1, (int)std::lround(W / 3.2f * dens));
      for (int k = 0, tries = 0; k < n && tries < 3 * n + 3; ++tries) if (wallPiece(f, ft, ua + 0.6f + u01(g) * std::max(0.0f, W - 1.2f), top, std::min(drop, H), true, 0.35f, std::min(1.4f, drop / 2.6f + 0.15f))) ++k;
    }
    if (L.flags & P_ISLANDS) {   // patchy remnants: small pieces anywhere on the wall
      int n = 2 + (int)(W * H / 12.0f * (0.5f + u01(g)) * dens);
      for (int k = 0, tries = 0; k < n && tries < 4 * n; ++tries) if (wallPiece(f, ft, ua + u01(g) * W, g0 + u01(g) * std::max(0.0f, H - 1.2f), 1.4f, false, 0.3f, 0.5f, 0.1f)) ++k;
    }
    if ((L.flags & (P_GROUND | P_BLANKET)) && g0 <= d.v0 + 0.3f) {   // ground cover spreading out from the wall foot
      int n = std::max(1, (int)std::lround(W / 5.0f * dens));
      for (int k = 0, tries = 0; k < n && tries < 3 * n; ++tries) if (groundPiece(f, ft, ua + 1.0f + u01(g) * std::max(0.0f, W - 2.0f))) ++k;
    }
  };

  std::vector<char> grown(fac.size(), 0);
  if (iv.mode == IvyDef::AUTO) {
    // facades large enough get growth with a chance by style (abandoned: most of them); "auto" always shows some
    float share = env.arid ? 0.3f : iv.share >= 0 ? iv.share : heavy ? 0.85f : modern ? 0.4f : 0.5f; int done = 0, best = -1; float bl = 0;
    for (int k = 0; k < (int)fac.size(); ++k) {
      const Facade& d = fac[k];
      if (d.u1 - d.u0 < 1.0f || d.v1 - d.v0 < 1.0f) continue;
      if (d.u1 - d.u0 > bl) { bl = d.u1 - d.u0; best = k; }
      if (u01(g) > share) continue;
      growFacade(k, lookFor(iv.pattern, iv.age, iv.overgrown, iv.density), d.u0, d.u1, d.v0, d.v1); grown[k] = 1; ++done;
    }
    if (!done && best >= 0) { growFacade(best, lookFor(iv.pattern, iv.age, iv.overgrown, iv.density), fac[best].u0, fac[best].u1, fac[best].v0, fac[best].v1); grown[best] = 1; }
  } else {
    for (auto& b : iv.blocks) {   // authored blocks: on the facade facing that way that covers most of the rectangle (the outermost of near ties)
      auto ov = [&](const Facade& d) { return d.n != b.axis || d.sign != b.sign ? 0.0f : std::max(0.0f, std::min(b.a1, d.u1) - std::max(b.a0, d.u0)) * std::max(0.0f, std::min(b.y1, d.v1) - std::max(b.y0, d.v0)); };
      float best = 0; for (auto& d : fac) best = std::max(best, ov(d));
      int k = -1;
      for (int f = 0; f < (int)fac.size(); ++f) if (best > 0 && ov(fac[f]) >= 0.5f * best && (k < 0 || fac[f].sign * fac[f].plane > fac[k].sign * fac[k].plane)) k = f;
      if (k < 0) continue;
      Look L = lookFor(b.pattern.empty() ? (iv.pattern.empty() ? std::string("fan") : iv.pattern) : b.pattern, b.age >= 0 ? b.age : iv.age, b.overgrown || iv.overgrown, b.density);
      growFacade(k, L, std::max(b.a0, fac[k].u0), std::min(b.a1, fac[k].u1), b.y0, b.y1); grown[k] = 1;
    }
  }
  // re-entrant corners (an L-shaped plan, a wing, a chimney stack or buttress standing out of a wall): facade A ends
  // where facade B starts, B looking back over A's wall and running out from A's plane. The corner piece hugs both:
  // its model walls face +z (along -x) and -x (along +z), mapped to A's normal and B's.
  for (int ia = 0; ia < (int)fac.size(); ++ia) for (int ib = 0; ib < (int)fac.size(); ++ib) {
    const Facade &A = fac[ia], &B = fac[ib];
    if (A.n == B.n || !(grown[ia] || grown[ib]) || nput >= budget) continue;
    const bool atEnd1 = std::fabs(B.plane - A.u1) < 0.06f && B.sign < 0, atEnd0 = std::fabs(B.plane - A.u0) < 0.06f && B.sign > 0;
    if (!atEnd1 && !atEnd0) continue;
    const bool outward = A.sign > 0 ? std::fabs(B.u0 - A.plane) < 0.06f && B.u1 > A.plane + 0.8f : std::fabs(B.u1 - A.plane) < 0.06f && B.u0 < A.plane - 0.8f;
    const float g0 = std::max({A.v0, B.v0, 0.0f}), top = std::min(A.v1, B.v1);
    if (!outward || top - g0 < 1.5f || u01(g) > 0.75f) continue;
    const char* ids[3] = {"corner_1", "corner_2", "corner_3"};
    const auto it = PK.pieces.find(ids[g() % 3]); if (it == PK.pieces.end()) continue;
    const Pack::P& P = it->second;
    const float s = std::clamp((top - g0) * (0.5f + 0.35f * u01(g)) / std::max(1.0f, P.hi.y), 0.5f, 1.3f);
    const float h = P.hi.y * s, eA = -P.lo.x * s, eB = P.hi.z * s;
    const float uc = atEnd1 ? A.u1 : A.u0, ub0 = A.sign > 0 ? B.u0 : B.u1;
    const float fa = intact[ia].fit(atEnd1 ? uc - eA : uc, atEnd1 ? uc : uc + eA, g0, g0 + h), fb = intact[ib].fit(A.sign > 0 ? ub0 : ub0 - eB, A.sign > 0 ? ub0 + eB : ub0, g0, g0 + h);
    if (fa < 0.8f || fb < 0.8f) continue;
    if (intact[ia].anyGone(now[ia], atEnd1 ? uc - eA : uc, atEnd1 ? uc : uc + eA, g0, g0 + h) || intact[ib].anyGone(now[ib], A.sign > 0 ? ub0 : ub0 - eB, A.sign > 0 ? ub0 + eB : ub0, g0, g0 + h)) { ++nput; continue; }
    vec3 Cp(0); Cp[A.n] = A.plane; Cp[B.n] = B.plane; Cp.y = g0 - 0.05f;
    vec3 Z = nrmOf(A), X = -nrmOf(B);
    mat4 R(1); R[0] = vec4(X, 0); R[1] = vec4(0, 1, 0, 0); R[2] = vec4(Z, 0);   // a reflection for one hand of corner: the piece mirrored, still in the corner
    put(it->first, glm::translate(mat4(1), Cp + (Z - X) * 0.012f) * R * glm::scale(mat4(1), vec3(s, s, s)), std::clamp(tone + C.facadeSpread * (u01(g) - 0.5f), 0.0f, 1.0f));
  }
}

void ivyDestroy(InstanceRT& r) { r.ivy.reset(); r.ivyKey = 0; }

// Adds the instance's ivy pieces to the model-foliage list, rebuilding them when a bay breaks, the quality, the toggle
// or the map's look changes. IV1: an instance's extra "ivy" ({pattern, age, density, overgrown, share}: Beautify's
// decor buildings) overrides the asset's block and grows on any building with stone / brick / rendered walls.
void ivyDrawItem(App& app, const Instance& i, InstanceRT& r, const mat4& M, float camDist) {
  const AssetDef& a = *r.asset;
  const json* ov = i.extra.is_object() && i.extra.contains("ivy") && i.extra["ivy"].is_object() ? &i.extra["ivy"] : nullptr;
  if (a.ivy.mode == IvyDef::NONE && !ov) return;
  static const bool abOff = std::getenv("NMK_IVY_OFF") != nullptr;   // VR1 perf A/B: no ivy drawn
  if (abOff) return;
  const Pack& PK = pack(app);
  if (!PK.ok) return;
  static bool once = [&] { app.R.ivyStem = lin(colours().stem); return true; }(); (void)once;
  int gq = (int)app.grass.quality;
  float density = !app.ivyOn || gq == 0 ? 0.0f : gq == 1 ? 0.6f : gq == 2 ? 1.0f : 1.15f;
  if (a.ivy.mode == IvyDef::PIECES && app.ivyOn && gq > 0) density = 1.0f;   // a placed vine prop is the prop itself: kept at every quality but Off
  IvyEnv env;
  if (app.doc.extra.is_object() && app.doc.extra.contains("look") && app.doc.extra["look"].is_object()) {
    const json& lk = app.doc.extra["look"];
    env.abandoned = lk.value("style", std::string()) == "abandoned";
    env.arid = lk.value("arid", false);
    std::string se = lk.value("season", std::string()); env.season = se == "autumn" ? 1 : se == "winter" ? 2 : 0;
  }
  uint64_t key = 1469598103934665603ull ^ (uint64_t)(density * 100) ^ ((uint64_t)i.rev << 20) ^ ((uint64_t)env.abandoned << 40) ^ ((uint64_t)env.season << 42) ^ ((uint64_t)env.arid << 45);
  for (uint8_t s : r.state) key = (key ^ (s == PANEL_BREACHED)) * 1099511628211ull;
  if (key != r.ivyKey) {
    ivyDestroy(r);
    r.ivyKey = key;
    IvyDef iv = a.ivy;
    if (ov && iv.mode != IvyDef::PIECES) {
      if (iv.mode == IvyDef::NONE) iv.mode = IvyDef::AUTO;
      iv.pattern = ov->value("pattern", iv.pattern); iv.age = ov->value("age", iv.age);
      iv.density = ov->value("density", iv.density); iv.overgrown = ov->value("overgrown", iv.overgrown); iv.share = ov->value("share", iv.share);
      if (ov->contains("share")) iv.mode = IvyDef::AUTO;   // every facade by share, not the asset's authored blocks
    }
    // a building's tone follows its id; a vine prop's follows where it stands (25 m cells), so the vines of one ruin
    // read as one plant, not a rainbow
    uint32_t seed = iv.mode == IvyDef::PIECES ? (uint32_t)((int)std::floor(i.pos.x / 25.0f) * 73856093 ^ (int)std::floor(i.pos.z / 25.0f) * 19349663) : (uint32_t)(i.id * 7919u + 17u);
    auto P = std::make_shared<IvyPieces>();
    if (density > 0) buildIvyPieces(PK, a, iv, r.state, seed, density, env, P->v);
    if (std::getenv("NMK_IVY_LOG")) logf("ivy: %s -> %zu pieces", a.id.c_str(), P->v.size());
    if (!P->v.empty()) r.ivy = P;
  }
  if (!r.ivy || camDist > (gq >= 3 ? 260.0f : PK.cull) * settings().drawDist()) return;   // IV1: Medium / Low to 140 m
  const bool far = PK.lod && camDist > PK.lodDist;
  const GpuModel* gm = far ? PK.lod.get() : PK.m.get();
  for (const IvyPlaced& q : r.ivy->v) {
    Renderer::FolModelDraw d{gm, M * q.m, PK.wind};
    d.first = far ? q.firstL : q.first; d.count = far ? q.countL : q.count; d.tint = q.tint;
    app.R.folModels.push_back(d);
  }
}

// self-test hook (selftest_render.cpp): the pieces an asset lays out for a seed, as asset-space matrices
std::vector<mat4> ivyLayout(App& app, const AssetDef& a, const IvyDef& iv, const std::vector<uint8_t>& state, uint32_t seed, float density) {
  std::vector<IvyPlaced> v; buildIvyPieces(pack(app), a, iv, state, seed, density, IvyEnv{}, v);
  std::vector<mat4> m; for (auto& q : v) m.push_back(q.m);
  return m;
}
