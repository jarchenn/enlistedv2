// Ivy on stone walls (B1, docs/asset-format.md#ivy): alpha-tested leaf cards hugging the exterior faces of a building's
// stone / brick / rendered walls, seeded per instance. IV1: five types (common ivy with its mature berried shoots,
// Virginia creeper green / autumn, Boston ivy, small-leaved ivy, dead ivy) drawn from the leaves.json layers, woody
// stems that thicken with age, and growth patterns (climbing fans, corners, curtains, window framing, wall tops,
// spirals, ground cover, islands, blankets) walked over a 10 cm grid of each facade. Drawn through the foliage
// path (wind, culling, shadows); the cards on a bay go when the bay is breached; density follows the grass quality
// and the Settings toggle (App::ivyOn).
#include "app.h"
#include "settings.h"
#include <algorithm>
#include <cfloat>
#include <limits>
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
// solids: every visible solid box, so a card is also only kept where nothing stands in front of it (recess, pier).
void collectFaces(const AssetDef& a, std::vector<Face>& out, std::vector<std::pair<vec3, vec3>>& solids) {
  std::vector<Face> cand;
  auto add = [&](const Part& p, int panel, int panelN, float panelSign) {
    if (!aligned(p) || !p.visible) return;
    solids.push_back({p.center - p.size * 0.5f, p.center + p.size * 0.5f});   // visible, solid or not (railings, glass)
    if (!p.collide || (p.bv > 0 && !gIvyAnyWall) || p.mat < 0 || p.mat >= (int)a.materials.size() || !ivyMaterial(a.materials[p.mat]) || p.size.y < 0.3f) return;   // bevelled: trim, the ivy goes round it
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

// ---------------------------------------------------------------- IV1 types and patterns
enum { T_COMMON, T_CREEPER, T_BOSTON, T_SMALL, T_DEAD, T_N };
enum { P_FAN = 1, P_CORNER = 2, P_CURTAIN = 4, P_FRAME = 8, P_TOP = 16, P_SPIRAL = 32, P_GROUND = 64, P_ISLANDS = 128, P_BLANKET = 256 };
const char* const kTypeName[T_N] = {"common", "creeper", "boston", "small", "dead"};
const char* const kPatName[9] = {"fan", "corner", "curtain", "frame", "top", "spiral", "ground", "islands", "blanket"};
int typeOf(const std::string& s) { for (int k = 0; k < T_N; ++k) if (s == kTypeName[k]) return k; return -1; }
int patternsOf(const std::string& s) {   // "fan+ground" -> P_FAN | P_GROUND
  int m = 0;
  for (size_t i = 0; i <= s.size();) {
    size_t j = s.find('+', i); std::string w = s.substr(i, j == std::string::npos ? std::string::npos : j - i);
    for (int k = 0; k < 9; ++k) if (w == kPatName[k]) m |= 1 << k;
    if (j == std::string::npos) break; i = j + 1;
  }
  return m;
}
// leaf species (leaves.json, tools/gen_leaf_textures.mjs); upper: the mature (flowering, berried) shoots high on old ivy;
// autumn: the colour after the turn. size: leaf length (m); perNode: leaves per 9 cm of stem at full vigour;
// flat: how closely the leaves lie against the wall; col: procedural fallback colour (no leaf library)
struct TypeP { const char *leaf, *upper, *autumn; float size, perNode, flat; bool deciduous; vec3 col; };
const TypeP kType[T_N] = {
  {"ivy", "ivy_mature", nullptr, 0.11f, 2.6f, 0.55f, false, {0.045f, 0.085f, 0.028f}},
  {"creeper", nullptr, "creeper_red", 0.17f, 1.6f, 0.35f, true, {0.06f, 0.1f, 0.03f}},
  {"boston", nullptr, "creeper_red", 0.17f, 2.2f, 0.85f, true, {0.05f, 0.09f, 0.03f}},
  {"ivy_small", nullptr, nullptr, 0.065f, 3.8f, 0.6f, false, {0.04f, 0.075f, 0.025f}},
  {"ivy_dead", nullptr, nullptr, 0.09f, 0.6f, 0.4f, false, {0.16f, 0.1f, 0.05f}},
};
const float kStep = 0.09f, kCell = 0.1f;

// one facade's 10 cm cells: 0 nothing (outside, breached), 1 wall, 2 inside the outline but no wall (an opening: glass,
// door, a gap), 3 too deep (a balcony slab in front); off = the surface in front of the facade plane (trim, a sill, a
// face a few cm proud), so leaves follow it instead of floating or cutting through
struct Grid {
  int nu = 0, nv = 0; float u0 = 0, v0 = 0; std::vector<uint8_t> st; std::vector<float> off;
  int at(float u, float v) const { int i = (int)std::floor((u - u0) / kCell), j = (int)std::floor((v - v0) / kCell); return i < 0 || j < 0 || i >= nu || j >= nv ? -1 : j * nu + i; }
  int s(float u, float v) const { int k = at(u, v); return k < 0 ? 0 : st[k]; }
  float o(float u, float v) const { int k = at(u, v); return k < 0 ? 0.0f : off[k]; }
};

struct Grower {
  std::vector<float>& v; std::mt19937& g; int cards = 0, cap = 20000;
  std::vector<int> leafL[T_N], upperL, autumnL[T_N], stemL;
  explicit Grower(std::vector<float>& out, std::mt19937& gen) : v(out), g(gen) {}
  int pick(const std::vector<int>& L) { return L.empty() ? -1 : L[g() % L.size()]; }
  void card(vec3 c, vec3 T, vec3 B, vec3 n, int layer, float mask, float shade) {
    if (cards >= cap) return;
    ++cards;
    vec3 q[4] = {c - T - B, c + T - B, c + T + B, c - T + B}; vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    float sh = shade + (layer >= 0 ? 2.0f * (layer + 1) : 0.0f);   // kLeafVS: shade + 2 (layer + 1) -> a leaves.json layer
    for (int k : {0, 1, 2, 0, 2, 3}) v.insert(v.end(), {q[k].x, q[k].y, q[k].z, n.x, n.y, n.z, uv[k].x, uv[k].y, mask, sh});
  }
  // a leaf whose petiole base is at p on a surface with outward normal n, pointing along a (in the surface), tilted out
  void leaf(vec3 p, vec3 n, vec3 a, float size, float tilt, int layer, float mask, float shade) {
    vec3 ax = glm::normalize(a * std::cos(tilt) + n * std::sin(tilt)), s = glm::normalize(glm::cross(n, a));
    vec3 B = ax * size * 0.56f, T = s * size * 0.56f;   // the layer's 16-texel gutter: the leaf spans ~89 % of the card
    card(p + B * 0.9f + n * 0.004f, T, B, glm::normalize(n + ax * 0.25f), layer, mask, shade);
  }
  // a woody stem segment flat on the surface; the stem layers draw it 14 / 10 / 7 % of their width (thick / mid / thin)
  void stem(vec3 p0, vec3 p1, vec3 n, float w) {
    if (stemL.size() < 3 || w < 0.0025f) return;
    vec3 d = p1 - p0; float L = glm::length(d); if (L < 1e-3f) return;
    int k = w > 0.016f ? 0 : w > 0.007f ? 1 : 2; const float frac[3] = {0.14f, 0.10f, 0.07f};
    vec3 B = d / L * (L * 0.56f + 0.01f), T = glm::normalize(glm::cross(n, d)) * (w / frac[k] * 0.5f);
    card((p0 + p1) * 0.5f + n * 0.006f, T, B, n, stemL[k], 0.0f, 0.85f + 0.15f * u01(g));
  }
};

// growth on one facade: stems walk the cells, leaves along them. dir: the facade's frame (eu along u, plane offset)
struct Facet { int n; float sign, plane; vec3 eu, nrm; };
vec3 wpos(const Facet& f, float u, float v, float off) { vec3 p = f.eu * u + vec3(0, v, 0); p[f.n] = f.plane + f.sign * off; return p; }

struct Walker { float u, v, ang, w, len, maxLen; int depth; bool hang; float targetU; };
struct Look { int type; float age, vig; bool overgrown; int season; float ground, H; int flags; };

void grow(Grower& G, const Facet& F, const Grid& gr, std::vector<Walker> ws, const Look& L, float ua, float ub, float va, float vb) {
  std::mt19937& g = G.g; const TypeP& tp = kType[L.type];
  auto ok = [&](float u, float v, bool droop) { if (u < ua || u > ub || v < va || v > vb) return false; int s = gr.s(u, v); return s == 1 || ((L.overgrown || droop) && s == 2); };
  auto nearOpening = [&](float u, float v) { for (int dj = -2; dj <= 2; ++dj) for (int di = -2; di <= 2; ++di) if (gr.s(u + di * kCell, v + dj * kCell) == 2) return true; return false; };
  int leaves = 0;
  for (size_t wi = 0; wi < ws.size() && wi < 900; ++wi) {
    Walker w = ws[wi];
    // stems: one card per 3 steps (27 cm); side shoots past the first branching only on old ivy (under the leaves otherwise)
    const bool drawStem = w.depth <= 1 || L.age > 0.6f;
    float su = w.u, sv = w.v, sw = w.w; int sn = 0;
    auto flush = [&]() { if (sn && drawStem) G.stem(wpos(F, su, sv, gr.o(su, sv) + 0.004f), wpos(F, w.u, w.v, gr.o(w.u, w.v) + 0.004f), F.nrm, sw); su = w.u; sv = w.v; sn = 0; };
    for (int stepN = 0; stepN < 600 && w.len < w.maxLen; ++stepN) {
      // steer: climbers toward up, curtains toward down, corner growth toward its edge; then the first open cell of
      // the straight-on, +-20, +-40, +-63 degree headings (frame growth prefers the cells round an opening)
      float want = w.hang ? -1.5708f : 1.5708f;
      w.ang += (want - w.ang) * (w.hang ? 0.3f : 0.12f) + (u01(g) - 0.5f) * 0.55f;
      if (w.targetU == w.targetU) w.ang += std::clamp((w.targetU - w.u) * 0.6f, -0.35f, 0.35f) * (w.hang ? -1.0f : 1.0f);
      float best = 1e9f, nu = 0, nv = 0;
      for (float d : {0.0f, 0.35f, -0.35f, 0.7f, -0.7f, 1.1f, -1.1f}) {
        float a = w.ang + d, cu = w.u + std::cos(a) * kStep, cv = w.v + std::sin(a) * kStep;
        if (!ok(cu, cv, w.hang)) continue;   // NT4: hanging curtains drape over the openings below a band / parapet
        float score = std::fabs(d) + ((L.flags & P_FRAME) && nearOpening(cu, cv) ? -0.6f : 0.0f);
        if (score < best) { best = score; nu = cu; nv = cv; w.ang = a; }
      }
      if (best > 1e8f) break;
      float t = std::clamp((w.v - L.ground) / std::max(L.H, 0.5f), 0.0f, 1.0f), r = w.len / std::max(w.maxLen, 0.01f);
      if (sn == 0) sw = w.w * (1.0f - 0.6f * r);
      w.u = nu; w.v = nv; w.len += kStep;
      if (++sn == 3) flush();
      // leaves: denser low and on old ivy, sparser at the ragged leading tips; deciduous types bare in winter
      float rate = tp.perNode * std::max(L.vig, 0.9f) * (1.15f - 0.45f * t) * (r > 0.85f ? 0.55f : 1.0f);   // NT3: a thinned budget means fewer stems, each still in leaf (not bare stems)
      if (tp.deciduous && L.season == 2) rate *= 0.06f;
      for (float q = rate; q > 0.0f; q -= 1.0f) {
        if (q < 1.0f && u01(g) > q) break;
        float side = (leaves++ & 1) ? 1.0f : -1.0f, lu = w.u + side * (0.02f + 0.07f * u01(g)), lv = w.v + (u01(g) - 0.5f) * 0.05f;
        if (!ok(lu, lv, false) && !L.overgrown) continue;   // openings stay clear
        int layer = G.pick(G.leafL[L.type]);
        if (L.type == T_COMMON && L.age > 0.55f && t > 0.5f && u01(g) < 0.7f && !G.upperL.empty()) layer = G.pick(G.upperL);
        if (tp.deciduous && L.season >= 1 && !G.autumnL[L.type].empty()) layer = G.pick(G.autumnL[L.type]);
        float rot = -1.5708f + side * (0.35f + 0.8f * u01(g)) + (u01(g) - 0.5f) * 0.4f;   // mostly hanging, splayed
        vec3 a = glm::normalize(F.eu * std::cos(rot) + vec3(0, std::sin(rot), 0));
        float size = tp.size * (0.8f + 0.45f * u01(g)) * (r > 0.85f ? 0.7f : 1.0f) * (1.0f + 0.15f * (1.0f - t)) * (L.age < 0.3f ? 0.85f : 1.0f);
        float tilt = (1.0f - tp.flat) * (0.25f + 0.6f * u01(g));
        float lift = 0.008f + (0.012f + 0.035f * L.age) * u01(g);   // old ivy stands further off the wall
        G.leaf(wpos(F, lu, lv, gr.o(lu, lv) + lift), F.nrm, a, size, tilt, layer, 0.04f + 0.05f * t, 0.78f + 0.24f * u01(g) + 0.08f * t);
        if ((L.flags & P_CORNER) && w.targetU == w.targetU && std::fabs(w.u - w.targetU) < 0.12f && u01(g) < 0.5f) {   // wrapping the corner
          vec3 n2 = glm::normalize(F.nrm + F.eu * (w.targetU > 0.5f * (ua + ub) ? 1.0f : -1.0f));
          G.leaf(wpos(F, w.targetU, lv, gr.o(lu, lv) + 0.02f), n2, vec3(0, -1, 0), size, tilt, layer, 0.05f, 0.85f);
        }
      }
      // a stem crossing a lintel droops a few leaves into the opening below (frame growth)
      if ((L.flags & P_FRAME) && gr.s(w.u, w.v - kCell) == 2 && u01(g) < 0.5f)
        for (int k = 0; k < 3; ++k) {
          float dv = 0.05f + 0.18f * u01(g), lu = w.u + (u01(g) - 0.5f) * 0.12f;
          G.leaf(wpos(F, lu, w.v - dv, gr.o(w.u, w.v) + 0.02f), F.nrm, vec3(0, -1, 0), tp.size, 0.2f, G.pick(G.leafL[L.type]), 0.08f, 0.8f);
        }
      // branching: often near the root, never past four levels
      float pb = (w.hang ? 0.05f : 0.08f) + (t < 0.25f ? 0.12f : 0.0f);   // bushy at the base
      if (w.depth < 4 && u01(g) < pb && ws.size() < 900) {
        float s = u01(g) < 0.5f ? -1.0f : 1.0f;
        ws.push_back({w.u, w.v, w.ang + s * (0.5f + 0.6f * u01(g)), w.w * 0.65f, 0.0f, (w.maxLen - w.len) * (0.45f + 0.45f * u01(g)), w.depth + 1, w.hang, w.targetU});
      }
    }
    flush();
  }
}

// leaves lying on a horizontal surface (ground cover in front of the wall foot, the top of a wall)
void flatLeaves(Grower& G, const Look& L, vec3 c, vec3 du, vec3 dw, float lenU, float depth, float perM2) {
  const TypeP& tp = kType[L.type];
  int n = (int)(lenU * depth * perM2 * L.vig);
  if (tp.deciduous && L.season == 2) n /= 12;
  for (int k = 0; k < n && G.cards < G.cap; ++k) {
    float a = (u01(G.g) - 0.5f) * lenU, b = u01(G.g) * u01(G.g) * depth;   // denser near the wall
    float th = u01(G.g) * 6.2831853f; vec3 ax = glm::normalize(du * std::cos(th) + dw * std::sin(th));
    int layer = G.pick(tp.deciduous && L.season >= 1 && !G.autumnL[L.type].empty() ? G.autumnL[L.type] : G.leafL[L.type]);
    G.leaf(c + du * a + dw * b + vec3(0, 0.01f + 0.02f * u01(G.g), 0), vec3(0, 1, 0), ax, tp.size * (0.8f + 0.4f * u01(G.g)), 0.15f + 0.4f * u01(G.g), layer, 0.06f, 0.8f + 0.2f * u01(G.g));
  }
}
}  // namespace

struct IvyEnv { bool abandoned = false; int season = 0; bool arid = false; };   // NT4 arid: dry, sparse creepers (desert)   // the open map's look: "style": "abandoned", "season"

// Cards (pos, normal, uv, mask, shade: the TreeDef card layout) for one instance; breached bays get none.
// Budget: at most ~6000 cards (12k triangles) a building at Medium, 14000 on a ruin / an abandoned map (x the quality
// density); a building over it grows
// again with the vigour scaled down, so every facade thins evenly instead of the last ones getting nothing.
static void growIvy(const AssetDef& a, const IvyDef& iv, const std::vector<uint8_t>& state, uint32_t seed, float density, const IvyEnv& env, std::vector<float>& v, int cap);
void buildIvyCards(const AssetDef& a, const IvyDef& iv, const std::vector<uint8_t>& state, uint32_t seed, float density, const IvyEnv& env, std::vector<float>& v) {
  if (density <= 0) return;
  const bool heavy = (a.raw.is_object() && a.raw.value("ruin", false)) || env.abandoned;
  const vec3 ext = a.bmax - a.bmin;   // NT3: a ruin's budget follows its wall area (big blocks get curtains too, not a thin film)
  const float wallA = 2.0f * (ext.x + ext.z) * std::max(ext.y, 1.0f);
  const int cap = (int)((heavy ? std::clamp(22.0f * wallA, 16000.0f, 60000.0f) : 6000.0f) * std::min(1.15f, density));   // ordinary 1944 buildings: moderate
  growIvy(a, iv, state, seed, density, env, v, 1 << 30);
  int n = (int)v.size() / 60;
  if (n > cap) { v.clear(); growIvy(a, iv, state, seed, density * cap / (float)n * 0.95f, env, v, cap); }
}
static void growIvy(const AssetDef& a, const IvyDef& iv, const std::vector<uint8_t>& state, uint32_t seed, float density, const IvyEnv& env, std::vector<float>& v, int cap) {
  std::mt19937 g(seed * 2654435761u + 0x9e3779b9u);
  Grower G(v, g); G.cap = cap;
  for (int t = 0; t < T_N; ++t) { G.leafL[t] = leafSpeciesLayers(kType[t].leaf); if (kType[t].autumn) G.autumnL[t] = leafSpeciesLayers(kType[t].autumn); }
  G.upperL = leafSpeciesLayers("ivy_mature"); G.stemL = leafSpeciesLayers("ivy_stem");
  // style: a ruin variant / an abandoned map (heavy, varied, overgrown), a modern building, or an old stone / brick one
  const bool ruin = a.raw.is_object() && a.raw.value("ruin", false), heavy = ruin || env.abandoned, modern = a.id.rfind("mod_", 0) == 0;
  auto pickW = [&](std::initializer_list<std::pair<int, float>> opts) { float s = 0; for (auto& o : opts) s += o.second; float x = u01(g) * s; for (auto& o : opts) { if ((x -= o.second) <= 0) return o.first; } return opts.begin()->first; };
  auto lookFor = [&](const std::string& ty, const std::string& pat, float age, bool over, float dens) {
    Look L{};
    L.type = typeOf(ty);
    if (L.type < 0 && env.arid) L.type = pickW({{T_DEAD, 70}, {T_CREEPER, 30}});
    if (L.type < 0) L.type = heavy ? pickW({{T_COMMON, 45}, {T_CREEPER, 20}, {T_DEAD, 5}, {T_BOSTON, 30}})   // NT2b: lush, few dead
                    : modern ? pickW({{T_BOSTON, 45}, {T_CREEPER, 30}, {T_COMMON, 25}}) : pickW({{T_COMMON, 75}, {T_SMALL, 15}, {T_CREEPER, 10}});
    L.flags = patternsOf(pat);
    if (!L.flags) L.flags = heavy ? pickW({{P_BLANKET, 35}, {P_FAN | P_CURTAIN, 40}, {P_ISLANDS | P_GROUND, 10}, {P_CORNER | P_FRAME, 15}})   // NT3: curtains and blankets
                          : modern ? pickW({{P_CORNER, 35}, {P_FRAME, 30}, {P_FAN, 20}, {P_CURTAIN, 15}}) : pickW({{P_FAN | P_GROUND, 50}, {P_CORNER, 25}, {P_FRAME, 25}});
    L.age = age >= 0 ? std::clamp(age, 0.0f, 1.0f) : heavy ? 0.7f + 0.3f * u01(g) : 0.35f + 0.45f * u01(g);
    L.overgrown = over || (heavy && (L.flags & P_BLANKET));
    L.vig = density * dens * (heavy ? 1.25f : 1.0f) * (env.arid ? 0.45f : 1.0f);
    L.season = env.season;
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
  auto gridOf = [&](const Facade& d) {
    Grid gr; gr.u0 = d.u0; gr.v0 = d.v0; gr.nu = std::max(1, (int)std::ceil((d.u1 - d.u0) / kCell)); gr.nv = std::max(1, (int)std::ceil((d.v1 - d.v0) / kCell));
    gr.st.assign((size_t)gr.nu * gr.nv, 0); gr.off.assign(gr.st.size(), 0.0f);
    auto raster = [&](float u0, float u1, float v0, float v1, auto&& fn) {
      int i0 = std::max(0, (int)std::ceil((u0 - gr.u0) / kCell - 0.5f)), i1 = std::min(gr.nu - 1, (int)std::floor((u1 - gr.u0) / kCell - 0.5f));
      int j0 = std::max(0, (int)std::ceil((v0 - gr.v0) / kCell - 0.5f)), j1 = std::min(gr.nv - 1, (int)std::floor((v1 - gr.v0) / kCell - 0.5f));
      for (int j = j0; j <= j1; ++j) for (int i = i0; i <= i1; ++i) fn(j * gr.nu + i);
    };
    for (int f : d.faces) {
      const Face& F = faces[f];
      bool gone = F.panel >= 0 && F.panel < (int)state.size() && state[F.panel] == PANEL_BREACHED;
      raster(F.u0, F.u1, F.v0, F.v1, [&](int k) { if (gone) gr.st[k] = 4; else if (gr.st[k] != 4) { gr.st[k] = 1; gr.off[k] = std::max(gr.off[k], d.sign * (F.plane - d.plane)); } });
    }
    for (int k = 0; k < (int)gr.st.size(); ++k) if (gr.st[k] == 4) gr.st[k] = 0;   // a breached bay: a hole, never grown over
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
    return gr;
  };
  auto facetOf = [&](const Facade& d) { Facet F; F.n = d.n; F.sign = d.sign; F.plane = d.plane; F.eu = vec3(0); F.eu[2 - d.n] = 1.0f; F.nrm = vec3(0); F.nrm[d.n] = d.sign; return F; };

  // one facade grown with one look inside [ua, ub] x [va, vb]
  auto growFacade = [&](const Facade& d, Look L, float ua, float ub, float va, float vb) {
    Grid gr = gridOf(d); Facet F = facetOf(d);
    L.ground = std::max(va, d.v0); L.H = std::max(0.5f, std::min(vb, d.v1) - L.ground);
    float W = ub - ua, wRoot = 0.004f + 0.032f * L.age, reach = L.H * (0.35f + 0.75f * L.age);
    std::vector<Walker> ws; const float NaN = std::numeric_limits<float>::quiet_NaN();
    auto rootAt = [&](float u) { for (float y = L.ground + 0.05f; y < L.ground + 0.8f; y += kCell) if (gr.s(u, y) == 1 || (L.overgrown && gr.s(u, y) == 2)) return y; return NaN; };
    const float vs = std::clamp(L.vig / 0.9f, 0.25f, 1.0f);   // NT3: a thinned budget shortens the stems (leaves stay full on them)
    auto climb = [&](float u, float len, float tu) { float y = rootAt(u); if (y != y) return; int n = 3 + (int)(u01(g) * 3); for (int k = 0; k < n; ++k) ws.push_back({u, y, 1.5708f + (u01(g) - 0.5f) * 1.6f, wRoot, 0.0f, vs * len * (0.6f + 0.5f * u01(g)), 0, false, tu}); };
    auto topEdges = [&](float every, float len) {   // eaves, parapets, sills, balcony edges: wall cells with no wall above
      for (int j = 0; j < gr.nv; ++j) for (int i = 0; i < gr.nu; ++i) {
        int k = j * gr.nu + i; if (gr.st[k] != 1 || (j + 1 < gr.nv && gr.st[k + gr.nu] == 1)) continue;
        float u = gr.u0 + (i + 0.5f) * kCell, vv = gr.v0 + (j + 0.5f) * kCell;
        if (u < ua || u > ub || vv < va || vv > vb || u01(g) > kCell / every) continue;
        ws.push_back({u, vv, -1.5708f, wRoot * 0.6f, 0.0f, vs * len * (0.5f + 0.6f * u01(g)), 1, true, NaN});
      }
    };
    if (L.flags & P_FAN) { int n = std::max(1, (int)std::lround(W / 4.5f * (0.6f + 0.6f * u01(g)))); for (int k = 0; k < n; ++k) climb(ua + 0.2f + u01(g) * std::max(0.0f, W - 0.4f), reach, NaN); }
    if (L.flags & P_BLANKET) { for (float u = ua + 0.3f; u < ub - 0.1f; u += 0.6f) climb(u + (u01(g) - 0.5f) * 0.2f, L.H * 1.1f, NaN); topEdges(0.9f, L.H * 0.6f); }
    if (L.flags & P_CORNER) for (float e : {ua + 0.15f, ub - 0.15f}) if (u01(g) < 0.8f) climb(e, reach * 1.1f, e < 0.5f * (ua + ub) ? ua : ub);
    if (L.flags & P_CURTAIN) topEdges(0.5f, 0.8f + 2.5f * L.age);
    if (L.flags & P_FRAME) {   // roots under the openings' jambs
      for (int i = 1; i < gr.nu - 1; ++i) for (int j = 0; j < std::min(gr.nv, 30); ++j) {
        int k = j * gr.nu + i; bool edge = gr.st[k] == 1 && (gr.st[k - 1] == 2 || gr.st[k + 1] == 2);
        if (edge && u01(g) < 0.25f) { climb(gr.u0 + (i + 0.5f) * kCell, reach, NaN); break; }
      }
    }
    if (L.flags & P_ISLANDS) {   // patchy remnants
      int n = 2 + (int)(W * L.H / 10.0f * (0.5f + u01(g)));
      for (int k = 0; k < n * 4 && n > 0; ++k) {
        float u = ua + u01(g) * W, vv = L.ground + u01(g) * L.H; if (gr.s(u, vv) != 1) continue; --n;
        for (int s = 0; s < 3; ++s) ws.push_back({u, vv, (u01(g) < 0.5f ? 1.5708f : -1.5708f) + (u01(g) - 0.5f) * 2.0f, wRoot * 0.5f, 0.0f, 0.4f + 1.0f * u01(g), 1, u01(g) < 0.4f, NaN});
      }
    }
    if (L.flags & P_TOP) {   // along the wall top and over it, a little spilling down
      float vt = std::min(vb, d.v1) - 0.08f;
      for (int k = 0; k < 2; ++k) ws.push_back({ua + u01(g) * W, vt, k ? 0.0f : 3.1416f, wRoot * 0.6f, 0.0f, W * 0.6f, 1, false, NaN});
      vec3 c = wpos(F, 0.5f * (ua + ub), std::min(vb, d.v1), gr.o(0.5f * (ua + ub), vt));
      flatLeaves(G, L, c, F.eu, -F.nrm, W, 0.3f, 45.0f);
      topEdges(0.6f, 0.2f + 0.5f * L.age);
    }
    grow(G, F, gr, ws, L, ua, ub, va, vb);
    if ((L.flags & (P_GROUND | P_BLANKET)) && L.ground <= d.v0 + 0.3f)   // ground cover spreading out from the wall foot
      flatLeaves(G, L, wpos(F, 0.5f * (ua + ub), d.v0, 0.0f) + F.nrm * 0.02f, F.eu, F.nrm, W * 0.8f, 0.4f + 1.1f * L.age, 30.0f);
  };

  if (iv.mode == IvyDef::AUTO) {
    // facades large enough get growth with a chance by style (abandoned: most of them); "auto" always shows some
    float share = env.arid ? 0.3f : iv.share >= 0 ? iv.share : heavy ? 0.85f : modern ? 0.4f : 0.5f; int done = 0, best = -1; float bl = 0;
    for (int k = 0; k < (int)fac.size(); ++k) {
      const Facade& d = fac[k];
      if (d.u1 - d.u0 < 1.0f || d.v1 - d.v0 < 1.0f) continue;
      if (d.u1 - d.u0 > bl) { bl = d.u1 - d.u0; best = k; }
      if (u01(g) > share) continue;
      growFacade(d, lookFor(iv.type, iv.pattern, iv.age, iv.overgrown, iv.density), d.u0, d.u1, d.v0, d.v1); ++done;
    }
    if (!done && best >= 0) growFacade(fac[best], lookFor(iv.type, iv.pattern, iv.age, iv.overgrown, iv.density), fac[best].u0, fac[best].u1, fac[best].v0, fac[best].v1);
  } else {
    for (auto& b : iv.blocks) {   // authored blocks: on the facade facing that way that covers most of the rectangle (the outermost of near ties)
      auto ov = [&](const Facade& d) { return d.n != b.axis || d.sign != b.sign ? 0.0f : std::max(0.0f, std::min(b.a1, d.u1) - std::max(b.a0, d.u0)) * std::max(0.0f, std::min(b.y1, d.v1) - std::max(b.y0, d.v0)); };
      float best = 0; for (auto& d : fac) best = std::max(best, ov(d));
      int k = -1;
      for (int f = 0; f < (int)fac.size(); ++f) if (best > 0 && ov(fac[f]) >= 0.5f * best && (k < 0 || fac[f].sign * fac[f].plane > fac[k].sign * fac[k].plane)) k = f;
      if (k < 0) continue;
      Look L = lookFor(b.type.empty() ? iv.type : b.type, b.pattern.empty() ? (iv.pattern.empty() ? std::string("fan") : iv.pattern) : b.pattern, b.age >= 0 ? b.age : iv.age, b.overgrown || iv.overgrown, b.density);
      growFacade(fac[k], L, std::max(b.a0, fac[k].u0), std::min(b.a1, fac[k].u1), b.y0, b.y1);
    }
  }
  // spiral: posts, columns, pipes and trunks (upright cylinders, slim upright boxes) when the pattern asks for it, or
  // downpipes beside a facade grown as a corner
  int pflags = patternsOf(iv.pattern); for (auto& b : iv.blocks) pflags |= patternsOf(b.pattern);
  if (pflags & (P_SPIRAL | P_CORNER)) {
    Look L = lookFor(iv.type, "spiral", iv.age, false, iv.density); int n = 0;
    for (const Part& p : a.parts) {
      if (!p.visible || std::fabs(std::fabs(p.rot.w) - 1.0f) > 1e-3f || p.size.y < 0.8f) continue;
      bool cyl = p.shape == Part::CYL && std::max(p.size.x, p.size.z) < 1.2f, post = p.shape == Part::BOX && std::max(p.size.x, p.size.z) < 0.45f;
      if (!(cyl || post) || (!(pflags & P_SPIRAL) && !(cyl && std::max(p.size.x, p.size.z) < 0.2f)) || n >= 40 || u01(g) > 0.7f) continue;
      ++n;
      float y0 = p.center.y - p.size.y * 0.5f, top = y0 + p.size.y * (0.4f + 0.6f * L.age), R = std::max(p.size.x, p.size.z) * 0.5f + 0.012f, ph = u01(g) * 6.28f;
      vec3 prev; const TypeP& tp = kType[L.type];
      for (float y = y0 + 0.03f; y < top; y += 0.03f) {
        float a = ph + (y - y0) / 0.9f * 6.2831853f; vec3 rn(std::cos(a), 0, std::sin(a)), q = vec3(p.center.x, y, p.center.z) + rn * R;
        if (y > y0 + 0.031f) G.stem(prev, q, rn, 0.004f + 0.015f * L.age);
        prev = q;
        if (u01(g) < tp.perNode * L.vig * 0.45f * (tp.deciduous && L.season == 2 ? 0.06f : 1.0f)) {
          float th = a + 1.5708f + (u01(g) - 0.5f) * 1.2f; vec3 ax = glm::normalize(vec3(-std::sin(a), 0, std::cos(a)) * std::cos(th - a) * 0.3f + vec3(0, -1, 0));
          G.leaf(q + rn * 0.01f, rn, ax, tp.size * (0.8f + 0.4f * u01(g)), (1.0f - tp.flat) * 0.6f, G.pick(G.leafL[L.type]), 0.05f, 0.8f + 0.2f * u01(g));
        }
      }
    }
  }
}
void buildIvyCards(const AssetDef& a, const IvyDef& iv, const std::vector<uint8_t>& state, uint32_t seed, float density, std::vector<float>& v) {
  buildIvyCards(a, iv, state, seed, density, IvyEnv{}, v);
}

void ivyDestroy(InstanceRT& r) {
  if (r.ivy) { glDeleteVertexArrays(1, &r.ivy->cardVao); glDeleteBuffers(1, &r.ivy->cardVbo); r.ivy.reset(); }
  r.ivyKey = 0;
}

// Adds the instance's ivy to the foliage list, rebuilding the cards when a bay breaks, the quality, the toggle or the
// map's look changes. IV1: an instance's extra "ivy" ({type, pattern, age, density, overgrown}: Beautify's decor
// buildings) overrides the asset's block and grows on any building with stone / brick / rendered walls.
void ivyDrawItem(App& app, const Instance& i, InstanceRT& r, const mat4& M, float camDist) {
  const AssetDef& a = *r.asset;
  const json* ov = i.extra.is_object() && i.extra.contains("ivy") && i.extra["ivy"].is_object() ? &i.extra["ivy"] : nullptr;
  if (a.ivy.mode == IvyDef::NONE && !ov) return;
  int gq = (int)app.grass.quality;
  float density = !app.ivyOn || gq == 0 ? 0.0f : gq == 1 ? 0.6f : gq == 2 ? 1.0f : 1.15f;
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
    std::vector<float> v;
    IvyDef iv = a.ivy;
    if (ov) {
      if (iv.mode == IvyDef::NONE) iv.mode = IvyDef::AUTO;
      iv.type = ov->value("type", iv.type); iv.pattern = ov->value("pattern", iv.pattern); iv.age = ov->value("age", iv.age);
      iv.density = ov->value("density", iv.density); iv.overgrown = ov->value("overgrown", iv.overgrown); iv.share = ov->value("share", iv.share);
      if (ov->contains("share")) iv.mode = IvyDef::AUTO;   // every facade by share, not the asset's authored blocks
    }
    if (density > 0) buildIvyCards(a, iv, r.state, (uint32_t)(i.id * 7919u + 17u), density, env, v);
    if (std::getenv("NMK_IVY_LOG")) logf("ivy: %s season %d -> %zu cards", a.id.c_str(), env.season, v.size() / 60);
    if (!v.empty()) {
      auto t = std::make_shared<TreeDef>();
      t->leafCol = leafLibReady() ? vec3(1.0f) : vec3(0.045f, 0.085f, 0.028f);   // textured: the leaf layers carry their own colour
      t->cardVerts = (int)v.size() / 10;
      t->cardMin = vec3(FLT_MAX); t->cardMax = vec3(-FLT_MAX);
      for (size_t k = 0; k + 2 < v.size(); k += 10) { vec3 p(v[k], v[k + 1], v[k + 2]); t->cardMin = glm::min(t->cardMin, p); t->cardMax = glm::max(t->cardMax, p); }
      glCreateVertexArrays(1, &t->cardVao); glCreateBuffers(1, &t->cardVbo);
      glNamedBufferStorage(t->cardVbo, v.size() * 4, v.data(), 0);
      glVertexArrayVertexBuffer(t->cardVao, 0, t->cardVbo, 0, 40);
      int comps[5] = {3, 3, 2, 1, 1}, off = 0;
      for (int k = 0; k < 5; ++k) { glEnableVertexArrayAttrib(t->cardVao, k); glVertexArrayAttribFormat(t->cardVao, k, comps[k], GL_FLOAT, GL_FALSE, off * 4); glVertexArrayAttribBinding(t->cardVao, k, 0); off += comps[k]; }
      r.ivy = t;
    }
  }
  if (r.ivy && camDist < (gq >= 3 ? 260.0f : 140.0f) * settings().drawDist()) app.R.foliage.push_back({r.ivy.get(), M});   // IV1: Medium / Low to 140 m
}
