// Impact-centred, cumulative wall damage (docs/destruction.md#damage-field).
//
// Every destructible wall bay (Panel) carries, per building copy, a damage field: a grid of cells over the bay's
// face (4 cm by default). Each cell records the depth of material removed from the front (+normal) face and from
// the back face. An impact stamps removal around its true contact point in every bay it overlaps, so seams and
// corners are crossed consistently and nothing is centred on a bay. Removal only ever grows (union). The render
// mesh and the Jolt collision of a damaged bay are both rebuilt from the same greedy-merged boxes of surviving
// material, so an open hole never keeps collision and intact material never loses it.
#include "app.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <unordered_map>

namespace {
float fract(float x) { return x - std::floor(x); }
// identical to the shader's hash12 (render.cpp), so stone boundaries here match the stones the player sees
float hash12(float x, float y) {
  vec3 p3(fract(x * 0.1031f), fract(y * 0.1031f), fract(x * 0.1031f));
  float d = p3.x * (p3.y + 33.33f) + p3.y * (p3.z + 33.33f) + p3.z * (p3.x + 33.33f);
  p3 += vec3(d);
  return fract((p3.x + p3.y) * p3.z);
}
uint32_t mix32(uint32_t h) { h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16; return h; }
float rnd01(uint32_t a, uint32_t b, uint32_t c) { return (mix32(a * 0x9E3779B1u ^ mix32(b + 0x85ebca6bu) ^ mix32(c * 0xc2b2ae35u)) & 0xFFFFFF) / 16777216.0f; }

// Coursed limestone (shader pattern 1) stone id at face coordinates (u, v): courses of 0.62 m split into two or
// three rows, irregular block joints. Cells of the same stone break together, so breaches follow mortar lines.
int64_t stoneId(float u, float v) {
  float band = std::floor(v / 0.62f), fy = v - band * 0.62f;
  float s1 = 0.62f * (0.36f + 0.28f * hash12(band, 17.0f));
  float three = hash12(band, 29.0f) >= 0.68f ? 1.0f : 0.0f;
  float s2 = glm::mix(0.62f, s1 + (0.62f - s1) * (0.45f + 0.1f * hash12(band, 31.0f)), three);
  float cy0 = 0, cy1 = s1, row = band * 3.0f;
  if (fy >= s1 && fy < s2) { cy0 = s1; cy1 = s2; row += 1; } else if (fy >= s2) { cy0 = s2; cy1 = 0.62f; row += 2; }
  float rowH = cy1 - cy0;
  float L = std::max(rowH * 1.15f, 0.30f + 0.55f * hash12(row, 3.0f));
  float x = u + hash12(row, 7.0f) * 5.0f;
  float k = std::floor(x / L);
  float jl = (hash12(k, row) - 0.5f) * 0.45f * L, jr = (hash12(k + 1, row) - 0.5f) * 0.45f * L;
  if (x < k * L + jl) k -= 1; else if (x > (k + 1) * L + jr) k += 1;
  return (int64_t)row * 100003 + (int64_t)k;
}

struct Frame { int an, au, av; float ns, T, front; float u0, v0; int nu, nv; float cu, cv; const Panel* P; };   // P: frame -> asset (rotated bays)
Frame panelFrame(const Panel& pn, float cell) {
  Frame f{}; f.P = &pn;
  struct { vec3 normal, bmin, bmax; } p{panelFrameNormal(pn), panelFrameMin(pn), panelFrameMax(pn)};   // GM3: a rotated bay in its own frame
  vec3 n = p.normal, an = glm::abs(n);
  f.an = an.x > an.y && an.x > an.z ? 0 : an.y > an.z ? 1 : 2;
  f.ns = n[f.an] >= 0 ? 1.0f : -1.0f;
  if (f.an == 1) { f.au = 0; f.av = 2; } else { f.au = f.an == 2 ? 0 : 2; f.av = 1; }
  f.T = p.bmax[f.an] - p.bmin[f.an];
  f.front = f.ns > 0 ? p.bmax[f.an] : p.bmin[f.an];
  f.u0 = p.bmin[f.au]; f.v0 = p.bmin[f.av];
  float W = p.bmax[f.au] - p.bmin[f.au], H = p.bmax[f.av] - p.bmin[f.av];
  f.nu = std::max(1, (int)std::ceil(W / cell)); f.nv = std::max(1, (int)std::ceil(H / cell));
  f.cu = W / f.nu; f.cv = H / f.nv;
  return f;
}
float depthOf(const Frame& f, float x) { return f.ns > 0 ? f.front - x : x - f.front; }   // asset coord -> depth from the front face
float coordOf(const Frame& f, float s) { return f.front - f.ns * s; }

// stamp parameters (game tuning, docs/destruction.md): in-plane radius and removal depth into masonry of
// resistance 1 (HP 200), scaled by sqrt(damage / reference)
struct Profile { float r, depth, ref; };
Profile profileFor(int kind) {
  switch (kind) {
    case WALL_AP:      return {0.80f, 1.60f, 320.0f};   // AT / tank-gun shot: large, deep, perforates masonry and 1 m concrete in one hit
    case WALL_ROCKET:  return {0.30f, 0.55f, 290.0f};   // launchers: smaller breach than an AT-gun shell (0.29 m at Bazooka 260)
    case WALL_GRENADE: return {0.29f, 0.36f, 80.0f};    // grenades: the Bazooka's stamp radius (0.29 m at 80), deep crater, holes in thin walls
    case WALL_BULLET:  return {0.035f, 0.025f, 1.0f};   // small arms: plaster / face chips only
    default:           return {0.60f, 0.30f, 120.0f};   // HE shell (AT gun / tank gun): wide crater; a second hit there breaches
  }
}

// smooth value noise in [0, 1), deterministic per seed: ragged but continuous break edges (no per-cell speckle)
float vnoise(uint32_t seed, float x, float y) {
  float fx = std::floor(x), fy = std::floor(y), tx = x - fx, ty = y - fy;
  int ix = (int)fx, iy = (int)fy;
  tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
  auto h = [&](int a, int b) { return rnd01(seed, (uint32_t)a, (uint32_t)b); };
  return glm::mix(glm::mix(h(ix, iy), h(ix + 1, iy), tx), glm::mix(h(ix, iy + 1), h(ix + 1, iy + 1), tx), ty);
}

// Random oblong, lobed outline of one impact (same for every bay the impact reaches, so seams stay consistent):
// an ellipse of random aspect 1.2-1.65 and orientation (area-preserving), with four low harmonics on its radius.
struct Outline { float sa = 1, c = 1, s = 0; float amp[4]{}, ph[4]{}; };
Outline outlineFor(uint32_t seed) {
  Outline o;
  o.sa = std::sqrt(1.2f + 0.45f * rnd01(seed, 101u, 1u));
  float th = 3.14159265f * rnd01(seed, 103u, 2u);
  o.c = std::cos(th); o.s = std::sin(th);
  for (int h = 0; h < 4; ++h) { o.amp[h] = 0.075f / (1.0f + 0.6f * h) * (0.5f + rnd01(seed, 107u, (uint32_t)h)); o.ph[h] = 6.2831853f * rnd01(seed, 109u, (uint32_t)h); }
  return o;
}
// normalised distance of in-plane offset g (m) from the outline centre: 1 on the outline of radius `rad`
float outlineQ(const Outline& o, vec2 g, float rad) {
  float eu = (g.x * o.c + g.y * o.s) / o.sa, ev = (-g.x * o.s + g.y * o.c) * o.sa;
  float phi = std::atan2(ev, eu), m = 1.0f;
  for (int h = 0; h < 4; ++h) m += o.amp[h] * std::cos((h + 2) * phi + o.ph[h]);
  return std::sqrt(eu * eu + ev * ev) / (rad * m);
}
}

void App::initWallFields(InstanceRT& r) {
  const AssetDef* a = r.asset;
  r.wf.assign(a->panels.size(), WallField{});
  r.pmesh.resize(a->panels.size());
  r.pshape.assign(a->panels.size(), nullptr);
}

static WallField& fieldFor(InstanceRT& r, size_t k, const Frame& f) {
  WallField& w = r.wf[k];
  if (w.rf.empty()) { w.rf.assign((size_t)f.nu * f.nv, 0.0f); w.rb.assign((size_t)f.nu * f.nv, 0.0f); }
  return w;
}

// bays in the same wall slab as bay k (same normal axis, overlapping thickness); opening trim is never one
static std::vector<size_t> coplanarBays(const AssetDef* a, size_t k, int ank, float cell) {
  std::vector<size_t> out;
  const Panel& pk = a->panels[k];
  if (pk.rot) return out;   // GM3: a roof slab has no coplanar neighbours
  for (size_t j = 0; j < a->panels.size(); ++j) {
    const Panel& pj = a->panels[j];
    if (j == k || pj.trim >= 0 || pj.rot || panelFrame(pj, cell).an != ank) continue;
    float ov = std::min(pk.bmax[ank], pj.bmax[ank]) - std::max(pk.bmin[ank], pj.bmin[ank]);
    if (ov < 0.5f * std::min(pk.bmax[ank] - pk.bmin[ank], pj.bmax[ank] - pj.bmin[ank])) continue;   // not the same wall slab
    out.push_back(j);
  }
  return out;
}
// Is the cell of a coplanar bay at bay-plane point (u, v) open (material gone through)? -1: no coplanar bay there
// (an opening, the outside), 0: solid, 1: open. Used so the sliver cleanup sees across seams between bays.
static int openInBay(const InstanceRT& r, const std::vector<size_t>& cands, float u, float v, float cell) {
  const AssetDef* a = r.asset;
  for (size_t j : cands) {
    const Panel& pj = a->panels[j];
    Frame fj = panelFrame(pj, cell);
    if (u < pj.bmin[fj.au] || u > pj.bmax[fj.au] || v < pj.bmin[fj.av] || v > pj.bmax[fj.av]) continue;
    if (j >= r.wf.size() || r.wf[j].rf.empty()) return 0;
    int iu = std::clamp((int)std::floor((u - fj.u0) / fj.cu), 0, fj.nu - 1), iv = std::clamp((int)std::floor((v - fj.v0) / fj.cv), 0, fj.nv - 1);
    int idx = iv * fj.nu + iu;
    return r.wf[j].rf[idx] + r.wf[j].rb[idx] >= fj.T - 1e-4f ? 1 : 0;
  }
  return -1;
}

// ---------------------------------------------------------------------------------------------- support (docs/destruction.md#support)
// What each bay rests on and leans against, per edge cell, found once per asset (and cell size) by probing half a cell
// across each edge: another bay (looked up live), a static part or the ground, the trim of an opening (holds while the
// trim stands), or nothing (the inside of an opening, air). And for each opening's trim, the cells of the bays around
// it that carry it: a band under the sill and one beside each jamb.
namespace {
enum : int { EDGE_AIR = -1, EDGE_STATIC = -2, EDGE_BAY = -3 };   // >= 0: trim group
struct BaySupport { std::array<std::vector<int>, 4> edge; std::vector<size_t> cands; bool on = false; };   // 0 bottom, 1 u-, 2 u+, 3 top
struct TrimBand { size_t k; int idx; int side; };   // 0 under, 1 u-, 2 u+
struct SupportInfo {
  float cell = 0;
  std::vector<BaySupport> bay;
  std::vector<std::vector<TrimBand>> band;
};
std::unordered_map<const AssetDef*, SupportInfo> gSupport;

const SupportInfo& supportFor(const AssetDef* a, float cell) {
  SupportInfo& S = gSupport[a];
  if (S.cell == cell && S.bay.size() == a->panels.size()) return S;
  S = SupportInfo{}; S.cell = cell;
  S.bay.resize(a->panels.size()); S.band.resize(a->trims.size());
  std::vector<std::pair<vec3, vec3>> statics;
  for (size_t i = 0; i < a->furnFirst && i < a->parts.size(); ++i) {
    const Part& p = a->parts[i];
    if (!p.collide || p.playerOnly) continue;
    glm::mat3 R = glm::mat3_cast(p.rot); vec3 h = glm::abs(R[0]) * p.size.x * 0.5f + glm::abs(R[1]) * p.size.y * 0.5f + glm::abs(R[2]) * p.size.z * 0.5f;
    statics.push_back({p.center - h, p.center + h});
  }
  for (size_t k = 0; k < a->panels.size(); ++k) {
    const Panel& pn = a->panels[k];
    Frame f = panelFrame(pn, cell);
    if (pn.trim >= 0 || f.an == 1) continue;
    BaySupport& B = S.bay[k];
    B.on = true;
    B.cands = coplanarBays(a, k, f.an, cell);
    const float mid = coordOf(f, f.T * 0.5f), W = f.nu * f.cu, H = f.nv * f.cv, e = 0.5f * cell;
    auto classify = [&](float u, float v, bool bottom) {
      for (size_t j : B.cands) {
        const Panel& pj = a->panels[j]; Frame fj = panelFrame(pj, cell);
        if (u >= pj.bmin[fj.au] && u <= pj.bmax[fj.au] && v >= pj.bmin[fj.av] && v <= pj.bmax[fj.av]) return (int)EDGE_BAY;
      }
      vec3 P(0); P[f.an] = mid; P[f.au] = u; P[f.av] = v;
      for (size_t g = 0; g < a->trims.size(); ++g) {
        const TrimGroup& t = a->trims[g];
        if (t.an != f.an || mid < t.d0 - 0.05f || mid > t.d1 + 0.05f) continue;
        if (glm::all(glm::greaterThanEqual(P, t.bmin - vec3(0.02f))) && glm::all(glm::lessThanEqual(P, t.bmax + vec3(0.02f)))) return (int)g;
        if (bottom && u >= t.u0 && u <= t.u1 && t.v1 <= v + 0.1f && t.v1 >= v - 0.35f) return (int)g;   // over the opening: the lintel / head
      }
      for (auto& [mn, mx] : statics)
        if (glm::all(glm::greaterThanEqual(P, mn - vec3(0.01f))) && glm::all(glm::lessThanEqual(P, mx + vec3(0.01f)))) return (int)EDGE_STATIC;
      if (bottom && v <= a->bmin.y + 0.05f) return (int)EDGE_STATIC;   // the ground
      return (int)EDGE_AIR;
    };
    B.edge[0].resize(f.nu); B.edge[3].resize(f.nu); B.edge[1].resize(f.nv); B.edge[2].resize(f.nv);
    for (int iu = 0; iu < f.nu; ++iu) {
      float u = f.u0 + (iu + 0.5f) * f.cu;
      B.edge[0][iu] = classify(u, f.v0 - e, true);
      B.edge[3][iu] = classify(u, f.v0 + H + e, false);
    }
    for (int iv = 0; iv < f.nv; ++iv) {
      float v = f.v0 + (iv + 0.5f) * f.cv;
      B.edge[1][iv] = classify(f.u0 - e, v, false);
      B.edge[2][iv] = classify(f.u0 + W + e, v, false);
    }
    // the cells of this bay that carry an opening's trim
    for (size_t g = 0; g < a->trims.size(); ++g) {
      const TrimGroup& t = a->trims[g];
      if (t.an != f.an) continue;
      float ov = std::min(pn.bmax[f.an], t.d1) - std::max(pn.bmin[f.an], t.d0);
      if (ov < 0.5f * std::min(f.T, t.d1 - t.d0)) continue;
      const float band = 0.3f, bot = std::min(t.v0, t.bmin.y);
      for (int iv = 0; iv < f.nv; ++iv)
        for (int iu = 0; iu < f.nu; ++iu) {
          float u = f.u0 + (iu + 0.5f) * f.cu, v = f.v0 + (iv + 0.5f) * f.cv;
          int side = -1;
          if (u >= t.u0 && u <= t.u1 && v < bot + 0.02f && v >= bot - band) side = 0;
          else if (v >= t.v0 && v <= t.v1 && u < t.u0 + 0.02f && u >= t.u0 - band) side = 1;
          else if (v >= t.v0 && v <= t.v1 && u > t.u1 - 0.02f && u <= t.u1 + band) side = 2;
          if (side >= 0) S.band[g].push_back({k, iv * f.nu + iu, side});
        }
    }
  }
  return S;
}
bool cellOpen(const InstanceRT& r, size_t k, int idx, float T) { return k < r.wf.size() && !r.wf[k].rf.empty() && r.wf[k].rf[idx] + r.wf[k].rb[idx] >= T - 1e-4f; }
}

// Broken trim falls out of the wall as pieces: each frame member, lintel and sill split along its length into one to
// three lengths, every one its own small rigid body (the debris bodies of shattered doors: capped, the oldest freeze),
// thrown with the shot and dropping out of the opening. Effects only: replay rebuilds the wall without them.
static void trimPieces(App& app, Instance& i, InstanceRT& r, int g, const WallEvent& e) {
  const AssetDef* a = r.asset;
  const TrimGroup& t = a->trims[g];
  if (t.panel < 0 || t.interact.empty() || a->interactables[t.interact[0]].leaves.empty()) return;
  mat4 M = app.instanceMatrix(i);
  quat qi = yawQuat(i.yaw);
  vec3 dn = glm::length(e.d) > 1e-5f ? glm::normalize(e.d) : vec3(0, 0, -1);
  if (e.kind == WALL_GRENADE) dn = -a->panels[t.panel].normal;
  vec3 dw = glm::normalize(vec3(M * vec4(dn, 0)));
  int made = 0;
  bool stone = false;
  for (const Part& p : a->panels[t.panel].parts[0]) {
    if (!p.visible || p.shape == Part::PRISM || made >= 14) continue;
    const std::string& mn = a->materials[p.mat].name;
    bool st = mn.find("stone") != std::string::npos;
    stone |= st;
    int ax = p.size.x >= p.size.y && p.size.x >= p.size.z ? 0 : p.size.y >= p.size.z ? 1 : 2;
    float L = p.size[ax];
    int n = std::clamp((int)std::round(L / 0.45f), 1, 3);
    std::vector<float> cut{-0.5f * L};
    for (int c = 1; c < n; ++c) cut.push_back(-0.5f * L + L * (c + (rnd01(e.seed, 211u + made, (uint32_t)c) - 0.5f) * 0.4f) / n);
    cut.push_back(0.5f * L);
    for (int c = 0; c < n && made < 14; ++c) {
      Part q = p;
      vec3 off(0); off[ax] = 0.5f * (cut[c] + cut[c + 1]);
      q.size[ax] = cut[c + 1] - cut[c];
      if (q.size[ax] < 0.02f) continue;
      vec3 pos = vec3(M * vec4(p.center + p.rot * off, 1));
      quat rot = qi * p.rot;
      Part local = q; local.center = vec3(0); local.rot = quat(1, 0, 0, 0);
      MeshData md; appendPart(md, local, a->materials[q.mat], mat4(1));
      Part box; box.shape = Part::BOX; box.center = vec3(0); box.size = glm::max(q.size - vec3(0.006f), vec3(0.012f));
      ShapePtr sh = Physics::buildShape({box});
      if (!sh) continue;
      float mass = std::clamp(q.size.x * q.size.y * q.size.z * (st ? 2300.0f : 600.0f), 0.6f, 80.0f);
      auto dl = std::make_unique<App::DynLeaf>();
      dl->inst = i.id; dl->k = t.interact[0]; dl->l = 0; dl->fragment = true; dl->shape = sh;
      dl->body = Physics::addDynamic(sh, pos, rot, (i.id << 16) | 0xFFF0u, mass);
      dl->fractured = std::make_unique<GpuMesh>(); dl->fractured->upload(md);
      uint32_t s = (uint32_t)made;
      vec3 J = (dw * (1.5f + 2.5f * rnd01(e.seed, 223u, s)) + vec3(rnd01(e.seed, 227u, s) - 0.5f, 0.6f * rnd01(e.seed, 229u, s), rnd01(e.seed, 233u, s) - 0.5f) * 2.0f) * mass;
      Physics::addImpulse(dl->body, J, pos + rot * ((vec3(rnd01(e.seed, 239u, s), rnd01(e.seed, 241u, s), rnd01(e.seed, 251u, s)) - 0.5f) * 0.08f));
      app.dynLeaves.push_back(std::move(dl));
      ++made;
    }
  }
  vec3 c = vec3(M * vec4(0.5f * (t.bmin + t.bmax), 1));
  for (int n = 0; n < 14; ++n) {
    vec3 v(app.fx.rnd() * 2 - 1, app.fx.rnd() * 1.4f, app.fx.rnd() * 2 - 1);
    float s = 0.02f + 0.05f * app.fx.rnd();
    app.fx.debris.push_back({c + vec3(app.fx.rnd() - 0.5f, app.fx.rnd() - 0.5f, app.fx.rnd() - 0.5f) * 0.8f, v * 2.0f + dw * 2.0f, vec3(app.fx.rnd(), app.fx.rnd(), app.fx.rnd()) * 10.0f, vec3(s, s * 0.5f, s * 1.6f)});
  }
  if (app.fx.debris.size() > app.fx.maxDebris) app.fx.debris.erase(app.fx.debris.begin(), app.fx.debris.begin() + (app.fx.debris.size() - app.fx.maxDebris));
  for (int n = 0; n < 4; ++n) app.fx.smoke(c + vec3(app.fx.rnd() - 0.5f, app.fx.rnd() - 0.5f, app.fx.rnd() - 0.5f), vec3(0, 0.5f, 0) + dw * 0.5f, 0.3f, 2.4f, vec4(0.6f, 0.56f, 0.5f, 0.45f), 2.0f, 0.2f);
  app.fx.impact(c, -dw, stone ? "stone" : "wood");
  app.audio.play("door_break", c, 0.8f);
  // cap the live rigid bodies: the oldest becomes static where it lies (as for shattered doors)
  size_t live = 0; for (auto& x : app.dynLeaves) live += !x->frozen;
  for (auto& x : app.dynLeaves) {
    if (live <= app.maxDynLeaves) break;
    if (x->frozen) continue;
    ShapePtr sh = x->shape;
    if (!sh) { auto it = app.rt.find(x->inst); if (it == app.rt.end()) continue; sh = it->second.asset->interactables[x->k].leaves[x->l].shape; }
    vec3 p; quat qq;
    Physics::getTransform(x->body, p, qq);
    Physics::remove(x->body);
    x->body = Physics::addStatic(sh, p, qq, (x->inst << 16) | 0xFFF0u);
    x->frozen = true; --live;
  }
}

bool App::trimIntact(const InstanceRT& r, int g) const {
  const AssetDef* a = r.asset;
  if (g < 0 || g >= (int)a->trims.size()) return true;
  int k = a->trims[g].panel;
  if (k < 0 || k >= (int)r.wf.size() || r.wf[k].rf.empty()) return true;
  Frame f = panelFrame(a->panels[k], destruction.cell);
  return !cellOpen(r, (size_t)k, 0, f.T);
}

int App::stampWall(InstanceRT& r, const WallEvent& e, bool effects, std::vector<std::string>* hitIds, int* newlyBreached) {
  const AssetDef* a = r.asset;
  const float cell = destruction.cell;
  vec3 dn0 = glm::length(e.d) > 1e-5f ? glm::normalize(e.d) : vec3(0, 0, -1);
  Profile pr = profileFor(e.kind);
  float strength = std::sqrt(std::max(e.dmg, 1e-3f) / pr.ref);
  const bool blast = e.kind == WALL_GRENADE;   // a charge lying on / near the surface: acts on whatever it lies against
  const Outline ol = outlineFor(e.seed);
  int touched = 0;
  std::vector<vec3> removedAt;   // debris spawn points (asset space)
  struct Touched { size_t k; Frame f; int iu0, iu1, iv0, iv1; bool fromFront; };
  std::vector<Touched> bays;
  auto debrisAt = [&](const Frame& f, int idx) {
    if (!effects || removedAt.size() >= 64) return;
    int iu = idx % f.nu, iv = idx / f.nu;
    vec3 pa(0); pa[f.au] = f.u0 + (iu + 0.5f) * f.cu; pa[f.av] = f.v0 + (iv + 0.5f) * f.cv; pa[f.an] = coordOf(f, f.T * 0.5f);
    removedAt.push_back(f.P ? panelToAsset(*f.P, pa) : pa);
  };
  for (size_t k = 0; k < a->panels.size() && e.kind != WALL_RAM; ++k) {   // a ram against trim stamps nothing
    const Panel& pn = a->panels[k];
    if (pn.trim >= 0) continue;   // opening trim is never cratered: it stands or breaks whole (below)
    Frame f = panelFrame(pn, cell);
    // resistance per unit depth (thickness already counts): masonry 1, reinforced concrete 1.2 (x HP / 450), timber 0.45
    float resist = std::max(0.2f, pn.family == "concrete" ? 1.2f * pn.hp / 450.0f : pn.hp / 200.0f);
    bool timber = pn.family == "timber", masonry = pn.family == "masonry";
    vec3 n(0); n[f.an] = f.ns;
    const vec3 ep = panelToLocal(pn, e.p), bmn = panelFrameMin(pn), bmx = panelFrameMax(pn);   // GM3: the bay's own frame (roof slabs)
    float dN = std::max({bmn[f.an] - ep[f.an], ep[f.an] - bmx[f.an], 0.0f});   // stand-off from the bay's slab
    vec3 dn = panelDirLocal(pn, dn0);
    float standoff = 1.0f;
    if (blast) {   // omnidirectional: the bay is struck square on from the side the charge lies on, weaker with distance
      dn = vec3(0); dn[f.an] = ep[f.an] >= 0.5f * (bmn[f.an] + bmx[f.an]) ? -1.0f : 1.0f;
      standoff = std::clamp(1.0f - dN / 0.75f, 0.0f, 1.0f);
    }
    float cosI = std::fabs(glm::dot(dn, n));
    float elong = std::clamp(1.0f / std::max(cosI, 0.3f), 1.0f, 2.6f);   // oblique hits gouge along the shot
    float rad = pr.r * std::sqrt(strength) * (timber ? 1.25f : 1.0f) * (blast ? 0.7f + 0.3f * standoff : 1.0f);
    float reach = rad * elong * 2.0f + cell;   // the oblong, lobed, ragged outline reaches up to ~1.9 rad
    // gate: the contact lies on / near this bay's face slab (within 0.2 m along its normal; a grenade 0.6 m) and
    // within the stamp's reach in its plane -- so a corner hit also bites the edge of the wall around the corner,
    // but walls in other parallel planes are never touched
    {
      float du = std::max({bmn[f.au] - ep[f.au], ep[f.au] - bmx[f.au], 0.0f}), dv = std::max({bmn[f.av] - ep[f.av], ep[f.av] - bmx[f.av], 0.0f});
      if (dN > (blast ? 0.6f : 0.2f) || std::sqrt(du * du + dv * dv) > rad * elong * 1.3f) continue;
    }
    float D = pr.depth * strength * (0.55f + 0.45f * cosI) / resist * (blast ? 0.35f + 0.65f * standoff : 1.0f);
    bool fromFront = glm::dot(dn, n) <= 0.0f;   // travelling against the normal: struck the front face
    // tangential direction of travel in the bay's (u, v) plane
    vec2 t(dn[f.au], dn[f.av]); float tl = glm::length(t); t = tl > 1e-4f ? t / tl : vec2(1, 0);
    float pu = ep[f.au], pv = ep[f.av];
    int iu0 = std::max(0, (int)std::floor((pu - reach - f.u0) / f.cu)), iu1 = std::min(f.nu - 1, (int)std::floor((pu + reach - f.u0) / f.cu));
    int iv0 = std::max(0, (int)std::floor((pv - reach - f.v0) / f.cv)), iv1 = std::min(f.nv - 1, (int)std::floor((pv + reach - f.v0) / f.cv));
    if (iu0 > iu1 || iv0 > iv1) continue;
    WallField& w = fieldFor(r, k, f);
    // 1) raw removal per cell inside a random oblong, lobed outline with a ragged (smooth-noise) edge
    struct Add { int idx; float add; int64_t stone; };
    std::vector<Add> adds;
    std::unordered_map<int64_t, std::pair<float, int>> stones;
    for (int iv = iv0; iv <= iv1; ++iv)
      for (int iu = iu0; iu <= iu1; ++iu) {
        float cu = f.u0 + (iu + 0.5f) * f.cu, cvv = f.v0 + (iv + 0.5f) * f.cv;
        vec2 d(cu - pu, cvv - pv);
        float q;
        if (timber) {   // boards split along the vertical grain: a tall, lobed slot
          d.y /= 2.2f;
          Outline og = ol; og.sa = 1.0f; og.c = 1.0f; og.s = 0.0f;
          q = outlineQ(og, d, rad);
        } else {
          float along = glm::dot(d, t), perp = glm::dot(d, vec2(-t.y, t.x));
          if (tl > 1e-4f) along /= elong;   // oblique: stretched along the shot
          q = outlineQ(ol, along * t + perp * vec2(-t.y, t.x), rad);
        }
        q += 0.22f * (vnoise(e.seed, cu / 0.09f, cvv / 0.09f) - 0.5f) + 0.08f * (vnoise(e.seed ^ 0x5bd1e995u, cu / 0.035f, cvv / 0.035f) - 0.5f);
        if (q >= 1.0f) continue;
        // brittle break: full removal over the core, a short steep shoulder (no shallow terraces around it);
        // the floor of the crater is rough, not a milled plane
        float fall = q < 0.55f ? 1.0f : std::pow(1.0f - (q - 0.55f) / 0.45f, 0.6f);
        fall *= 0.8f + 0.2f * vnoise(e.seed ^ 0x27d4eb2fu, cu / 0.06f, cvv / 0.06f);   // only ever less than the profile
        Add ad{iv * f.nu + iu, D * fall, 0};
        if (masonry) { ad.stone = stoneId(cu, cvv); auto& s = stones[ad.stone]; s.first = std::max(s.first, ad.add); s.second += 1; }
        adds.push_back(ad);
      }
    if (adds.empty()) continue;
    // 2) masonry breaks along mortar: a well-struck stone comes out as one piece (flat socket, uniform depth); a
    //    grazed one only chips its face shallowly
    if (masonry)
      for (auto& ad : adds) {
        float mx = stones[ad.stone].first;
        float j = 0.85f + 0.35f * rnd01(e.seed, (uint32_t)(ad.stone & 0xffffffff), 77u);
        if (mx > 0.45f * D) ad.add = mx * j;                                    // the whole stone
        else ad.add = std::min(ad.add, 0.06f) * j;                              // face chip, at most 6 cm
      }
    // 3) apply on the struck face; perforation and back-face spall when the remaining material gets thin
    bool changed = false;
    for (auto& ad : adds) {
      float& near = fromFront ? w.rf[ad.idx] : w.rb[ad.idx];
      float& far = fromFront ? w.rb[ad.idx] : w.rf[ad.idx];
      if (near + far >= f.T) continue;
      float before = near + far;
      near = std::min(f.T - far, near + ad.add);
      float left = f.T - near - far;
      if (left < 0.28f * f.T) far = f.T - near;                            // punched through
      else if (near > 0.5f * f.T) far = std::min(f.T - near, far + 0.35f * (near - 0.5f * f.T));   // spall on the far face
      if (near + far > before + 1e-5f) {
        changed = true;
        if (near + far >= f.T) debrisAt(f, ad.idx);
      }
    }
    if (!changed) continue;
    bays.push_back({k, f, iu0, iu1, iv0, iv1, fromFront});
  }
  // 4) no slivers: once every bay has its stamp, thin bridges of material left between damaged areas go. Along
  //    each of four directions, a solid run of at most destruction.bridgeMax between two open cells is opened
  //    (this also sees open cells in the coplanar bay across a seam). On the first pass, a ridge of at most that
  //    width standing proud between two deeper craters on the struck face (inside the impact window) is broken
  //    down to the shallower side. Wider strips are left alone, so holes that are genuinely apart stay apart.
  // 5) unsupported fragments: solid cells no longer connected to the bay's edges (where it meets the rest of the
  //    building) fall out.
  // Both repeat over each whole touched bay until neither changes anything: a fragment falling out or a bridge
  // going can expose a new bridge anywhere in the bay, so the result has no bridge left for any outline.
  const int N = (int)std::floor(destruction.bridgeMax / cell + 1e-3f);   // 0: cleanup off (A/B in the self-test)
  const int dirs[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
  std::vector<std::vector<size_t>> cands(bays.size());
  for (size_t q = 0; q < bays.size(); ++q) cands[q] = coplanarBays(a, bays[q].k, bays[q].f.an, cell);
  for (int pass = 0; pass < 64; ++pass) {
    bool any = false;
    for (size_t q = 0; q < bays.size() && N >= 1; ++q) {
      const Touched& b = bays[q];
      const Frame& f = b.f;
      WallField& w = r.wf[b.k];
      auto openAt = [&](int iu, int iv) -> bool {
        if (iu >= 0 && iv >= 0 && iu < f.nu && iv < f.nv) { int idx = iv * f.nu + iu; return w.rf[idx] + w.rb[idx] >= f.T - 1e-4f; }
        return openInBay(r, cands[q], f.u0 + (iu + 0.5f) * f.cu, f.v0 + (iv + 0.5f) * f.cv, cell) == 1;
      };
      auto nearAt = [&](int iu, int iv) -> float {
        if (iu < 0 || iv < 0 || iu >= f.nu || iv >= f.nv) return -1.0f;
        int idx = iv * f.nu + iu; return b.fromFront ? w.rf[idx] : w.rb[idx];
      };
      std::vector<int> open, raise; std::vector<float> raiseTo;
      for (int iv = 0; iv < f.nv; ++iv)
        for (int iu = 0; iu < f.nu; ++iu) {
          int idx = iv * f.nu + iu;
          if (w.rf[idx] + w.rb[idx] >= f.T - 1e-4f) continue;
          bool ridgeZone = pass == 0 && iu >= b.iu0 - N && iu <= b.iu1 + N && iv >= b.iv0 - N && iv <= b.iv1 + N;
          float here = b.fromFront ? w.rf[idx] : w.rb[idx], best = here;
          bool bridge = false;
          for (auto& dd : dirs) {
            int lim = dd[0] && dd[1] ? std::max(1, (int)(N / 1.4142f)) : N;
            int sp = 0, sm = 0; float mp = -1, mm = -1;
            for (int s = 1; s <= lim; ++s) { if (!sp && openAt(iu + dd[0] * s, iv + dd[1] * s)) sp = s; if (ridgeZone) mp = std::max(mp, nearAt(iu + dd[0] * s, iv + dd[1] * s)); }
            for (int s = 1; s <= lim; ++s) { if (!sm && openAt(iu - dd[0] * s, iv - dd[1] * s)) sm = s; if (ridgeZone) mm = std::max(mm, nearAt(iu - dd[0] * s, iv - dd[1] * s)); }
            if (sp && sm && sp + sm - 1 <= lim) { bridge = true; break; }
            float ridge = std::min(mp, mm);
            if (ridgeZone && ridge > here + 0.15f * f.T) best = std::max(best, ridge);
          }
          if (bridge) open.push_back(idx);
          else if (best > here) { raise.push_back(idx); raiseTo.push_back(best); }
        }
      for (int idx : open) { w.rb[idx] = f.T - w.rf[idx]; debrisAt(f, idx); }
      for (size_t j = 0; j < raise.size(); ++j) {
        int idx = raise[j];
        float& near = b.fromFront ? w.rf[idx] : w.rb[idx];
        float& far = b.fromFront ? w.rb[idx] : w.rf[idx];
        near = std::min(f.T - far, raiseTo[j]);
        if (f.T - near - far < 0.28f * f.T) { far = f.T - near; open.push_back(idx); debrisAt(f, idx); }   // the thinned ridge gives way
      }
      if (!open.empty()) any = true;   // only new holes can expose new bridges
    }
    for (auto& b : bays) {
      const Frame& f = b.f;
      WallField& w = r.wf[b.k];
      std::vector<uint8_t> seen((size_t)f.nu * f.nv, 0);
      std::vector<int> st;
      auto solid = [&](int idx) { return w.rf[idx] + w.rb[idx] < f.T - 1e-4f; };
      for (int iv = 0; iv < f.nv; ++iv) for (int iu = 0; iu < f.nu; ++iu)
        if ((iu == 0 || iv == 0 || iu == f.nu - 1 || iv == f.nv - 1)) { int idx = iv * f.nu + iu; if (solid(idx)) { seen[idx] = 1; st.push_back(idx); } }
      while (!st.empty()) {
        int idx = st.back(); st.pop_back(); int iu = idx % f.nu, iv = idx / f.nu;
        const int nb[4] = {iu > 0 ? idx - 1 : -1, iu < f.nu - 1 ? idx + 1 : -1, iv > 0 ? idx - f.nu : -1, iv < f.nv - 1 ? idx + f.nu : -1};
        for (int m : nb) if (m >= 0 && !seen[m] && solid(m)) { seen[m] = 1; st.push_back(m); }
      }
      for (int idx = 0; idx < f.nu * f.nv; ++idx)
        if (!seen[idx] && solid(idx)) { w.rb[idx] = f.T - w.rf[idx]; debrisAt(f, idx); any = true; }
    }
    if (!any) break;
  }
  // 6) opening trim and support between bays (docs/destruction.md#trim, #support)
  std::vector<uint8_t> dirty(a->panels.size(), 0);
  for (auto& b : bays) dirty[b.k] = 1;
  std::vector<int> trimBroken;
  auto breakTrim = [&](int g, int cause) {
    const TrimGroup& t = a->trims[g];
    if (t.panel < 0 || !trimIntact(r, g)) return false;
    Frame f = panelFrame(a->panels[t.panel], cell);
    WallField& w = fieldFor(r, (size_t)t.panel, f);
    std::fill(w.rf.begin(), w.rf.end(), f.T); std::fill(w.rb.begin(), w.rb.end(), 0.0f);
    dirty[t.panel] = 1;
    ++(cause == 0 ? trimStats.blast : cause == 1 ? trimStats.support : trimStats.ram);
    trimBroken.push_back(g);
    return true;
  };
  // a blast, shell or ram within reach of the trim breaks it whole (game tuning: about the crater's core)
  {
    float tr = e.kind == WALL_HE ? 0.5f : e.kind == WALL_AP ? 0.35f : e.kind == WALL_ROCKET ? 0.4f : e.kind == WALL_GRENADE ? 0.45f : e.kind == WALL_RAM ? 0.6f : -1.0f;
    if (e.kind != WALL_RAM) tr *= std::clamp(std::sqrt(strength), 0.5f, 2.0f);
    for (size_t g = 0; g < a->trims.size() && tr > 0; ++g) {
      const TrimGroup& t = a->trims[g];
      if (glm::length(glm::clamp(e.p, t.bmin, t.bmax) - e.p) <= tr) breakTrim((int)g, e.kind == WALL_RAM ? 2 : 0);
    }
  }
  bool through = false;
  for (auto& b : bays) {
    const WallField& w = r.wf[b.k];
    for (size_t idx = 0; idx < w.rf.size() && !through; ++idx) through = w.rf[idx] + w.rb[idx] >= b.f.T - 1e-4f;
  }
  if (destruction.support && (through || !trimBroken.empty())) {
    auto ts = std::chrono::high_resolution_clock::now();
    const SupportInfo& S = supportFor(a, cell);
    const float ec = 0.5f * cell;
    auto solidAt = [&](size_t k, int idx, const Frame& f) { return !cellOpen(r, k, idx, f.T); };
    // is the bottom of bay j lost under the in-plane position u?
    auto bottomLost = [&](size_t j, float u) {
      const BaySupport& B = S.bay[j];
      if (!B.on) return false;
      Frame f = panelFrame(a->panels[j], cell);
      int iu = std::clamp((int)std::floor((u - f.u0) / f.cu), 0, f.nu - 1);
      int kind = B.edge[0][iu];
      if (kind == EDGE_BAY) return openInBay(r, B.cands, f.u0 + (iu + 0.5f) * f.cu, f.v0 - ec, cell) == 1;
      return kind >= 0 && !trimIntact(r, kind);
    };
    // across a bay's side edge at its bottom row: an opening, broken trim, a hole, or a neighbour that has lost its own
    // bottom there too -- the lost span goes on past the edge (the triangle does not close at the seam)
    auto spanGoesOn = [&](size_t k, const Frame& f, int side) {
      const BaySupport& B = S.bay[k];
      int kind = B.edge[side][0];
      if (kind == EDGE_AIR) return true;
      if (kind >= 0) return !trimIntact(r, kind);
      if (kind == EDGE_STATIC) return false;
      float u = side == 1 ? f.u0 - ec : f.u0 + f.nu * f.cu + ec, v = f.v0 + ec;
      if (openInBay(r, B.cands, u, v, cell) == 1) return true;
      for (size_t j : B.cands) {
        const Panel& pj = a->panels[j]; Frame fj = panelFrame(pj, cell);
        if (u < pj.bmin[fj.au] || u > pj.bmax[fj.au] || v < pj.bmin[fj.av] || v > pj.bmax[fj.av]) continue;
        return std::fabs(fj.v0 - f.v0) < 0.1f && bottomLost(j, u);
      }
      return false;
    };
    auto anchorKind = [&](const BaySupport& B, int kind, float u, float v) {
      if (kind == EDGE_STATIC) return true;
      if (kind == EDGE_AIR) return false;
      if (kind >= 0) return trimIntact(r, kind);
      return openInBay(r, B.cands, u, v, cell) == 0;
    };
    for (int it = 0; it < 32; ++it) {
      bool ch = false;
      // trim: the bays under the sill or beside a jamb gone (half the band through) -- the frame falls out
      for (size_t g = 0; g < a->trims.size(); ++g) {
        if (S.band[g].empty() || !trimIntact(r, (int)g)) continue;
        int tot[3] = {0, 0, 0}, open[3] = {0, 0, 0};
        for (auto& c : S.band[g]) {
          ++tot[c.side];
          if (cellOpen(r, c.k, c.idx, panelFrame(a->panels[c.k], cell).T)) ++open[c.side];
        }
        bool fail = false;
        for (int s = 0; s < 3; ++s) fail |= tot[s] >= 8 && open[s] * 2 >= tot[s];
        if (fail && breakTrim((int)g, 1)) ch = true;
      }
      // bays: material over a span that lost its support falls in a triangle (the lintel load rule); what is left
      // hanging from nothing goes with it
      for (size_t k = 0; k < a->panels.size(); ++k) {
        const BaySupport& B = S.bay[k];
        if (!B.on) continue;
        Frame f = panelFrame(a->panels[k], cell);
        std::vector<uint8_t> sup((size_t)f.nu, 1);
        bool anyLost = false;
        for (int iu = 0; iu < f.nu; ++iu) {
          int kind = B.edge[0][iu];
          if (kind == EDGE_BAY) sup[iu] = openInBay(r, B.cands, f.u0 + (iu + 0.5f) * f.cu, f.v0 - ec, cell) != 1;
          else if (kind >= 0) sup[iu] = trimIntact(r, kind);
          anyLost |= !sup[iu];
        }
        if (!anyLost) continue;
        WallField& w = fieldFor(r, k, f);
        int removed = 0;
        for (int a0 = 0; a0 < f.nu;) {
          if (sup[a0]) { ++a0; continue; }
          int b0 = a0; while (b0 + 1 < f.nu && !sup[b0 + 1]) ++b0;
          bool openL = a0 == 0 && spanGoesOn(k, f, 1), openR = b0 == f.nu - 1 && spanGoesOn(k, f, 2);
          float uA = openL ? -1e9f : f.u0 + a0 * f.cu, uB = openR ? 1e9f : f.u0 + (b0 + 1) * f.cu;
          if (openL || openR || uB - uA >= destruction.archMinSpan)
            for (int iu = a0; iu <= b0; ++iu) {
              float uc = f.u0 + (iu + 0.5f) * f.cu, h = destruction.archSlope * std::min(uc - uA, uB - uc);
              for (int iv = 0; iv < f.nv && (iv + 0.5f) * f.cv < h; ++iv) {
                int idx = iv * f.nu + iu;
                if (w.rf[idx] + w.rb[idx] >= f.T - 1e-4f) continue;
                w.rb[idx] = f.T - w.rf[idx]; debrisAt(f, idx); ++removed;
              }
            }
          a0 = b0 + 1;
        }
        if (!removed) continue;
        // what is left of this bay stays only where it still touches something that holds
        std::vector<uint8_t> seen((size_t)f.nu * f.nv, 0);
        std::vector<int> st;
        auto seed = [&](int idx) { if (!seen[idx] && solidAt(k, idx, f)) { seen[idx] = 1; st.push_back(idx); } };
        for (int iu = 0; iu < f.nu; ++iu) {
          float u = f.u0 + (iu + 0.5f) * f.cu;
          if (sup[iu]) seed(iu);
          if (anchorKind(B, B.edge[3][iu], u, f.v0 + f.nv * f.cv + ec)) seed((f.nv - 1) * f.nu + iu);
        }
        for (int iv = 0; iv < f.nv; ++iv) {
          float v = f.v0 + (iv + 0.5f) * f.cv;
          if (anchorKind(B, B.edge[1][iv], f.u0 - ec, v)) seed(iv * f.nu);
          if (anchorKind(B, B.edge[2][iv], f.u0 + f.nu * f.cu + ec, v)) seed(iv * f.nu + f.nu - 1);
        }
        while (!st.empty()) {
          int idx = st.back(); st.pop_back(); int iu = idx % f.nu, iv = idx / f.nu;
          if (iu > 0) seed(idx - 1);
          if (iu < f.nu - 1) seed(idx + 1);
          if (iv > 0) seed(idx - f.nu);
          if (iv < f.nv - 1) seed(idx + f.nu);
        }
        int left = 0;
        for (int idx = 0; idx < f.nu * f.nv; ++idx) if (solidAt(k, idx, f)) { if (seen[idx]) ++left; else { w.rb[idx] = f.T - w.rf[idx]; debrisAt(f, idx); ++removed; } }
        if (left > 0 && left < 0.12f * f.nu * f.nv)   // a remnant this small does not stand on its own
          for (int idx = 0; idx < f.nu * f.nv; ++idx) if (solidAt(k, idx, f)) { w.rb[idx] = f.T - w.rf[idx]; ++removed; left = 0; }
        if (left == 0) ++trimStats.bays;
        trimStats.cells += removed;
        dirty[k] = 1; ch = true;
      }
      if (!ch) break;
    }
    trimStats.supportMs += (float)std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - ts).count();
  }
  for (size_t k = 0; k < a->panels.size(); ++k) {
    if (!dirty[k]) continue;
    const Panel& pn = a->panels[k];
    uint8_t oldState = r.state[k];
    rebuildPanel(r, k);
    ++touched;
    if (hitIds) hitIds->push_back(pn.id);
    if (newlyBreached && oldState != PANEL_BREACHED && r.state[k] == PANEL_BREACHED) ++*newlyBreached;
  }
  // broken trim takes its sash / door / shutters with it, and falls out of the wall in pieces
  if (!trimBroken.empty()) {
    Instance* inst = doc.find(r.id);
    for (int g : trimBroken) {
      const TrimGroup& t = a->trims[g];
      for (int k : t.interact) {
        if (k < 0 || k >= (int)a->interactables.size() || k >= (int)r.leafSt.size()) continue;
        const InteractDef& d = a->interactables[k];
        for (size_t l = 0; l < d.leaves.size() && l < r.leafSt[k].size(); ++l) {
          if (r.leafSt[k][l] & 2) continue;
          bool body = k < (int)r.leafBodies.size() && l < r.leafBodies[k].size() && r.leafBodies[k][l] != 0xffffffffu;
          if (effects && inst && body && !d.leaves[l].colliders.empty()) {
            vec3 c = vec3(leafMatrix(*inst, d.leaves[l], d, r.progress[k]) * vec4(d.leaves[l].colliders[0].center, 1));
            vec3 dw = vec3(instanceMatrix(*inst) * vec4(e.kind == WALL_GRENADE ? vec3(0, 1, 0) : dn0, 0));
            detachLeaf(*inst, r, k, (int)l, dw * 60.0f + vec3(0, 20, 0), c, true);
          }
          if (!(r.leafSt[k][l] & 2)) {   // replay (no effects) or a leaf no weapon breaks: gone with its frame all the same
            r.leafSt[k][l] |= 3;
            if (body) { Physics::remove(r.leafBodies[k][l]); r.leafBodies[k][l] = 0xffffffffu; }
          }
        }
      }
      if (effects && inst) trimPieces(*this, *inst, r, g, e);
    }
  }
  // a bounded handful of masonry / timber fragments thrown out of the far side and back toward the shooter
  if (effects && !removedAt.empty()) {
    const Instance* inst = doc.find(r.id);
    if (inst) {
      mat4 M = instanceMatrix(*inst);
      vec3 dn = blast ? vec3(0, 1, 0) : dn0;
      int n = std::min<int>(destruction.maxFragmentsPerHit, (int)removedAt.size());
      for (int j = 0; j < n; ++j) {
        vec3 pa = removedAt[(size_t)(rnd01(e.seed, 31u, (uint32_t)j) * removedAt.size()) % removedAt.size()];
        vec3 pw = vec3(M * vec4(pa, 1));
        vec3 dw = glm::normalize(vec3(M * vec4(dn, 0)));
        float sgn = rnd01(e.seed, 41u, (uint32_t)j) < 0.65f ? 1.0f : -0.6f;   // most go with the shot, some spall back
        vec3 v = dw * sgn * (2.0f + 4.0f * rnd01(e.seed, 43u, (uint32_t)j)) + vec3(rnd01(e.seed, 47u, j) - 0.5f, 1.0f + rnd01(e.seed, 53u, j), rnd01(e.seed, 59u, j) - 0.5f) * 2.0f;
        float s = 0.05f + 0.09f * rnd01(e.seed, 61u, (uint32_t)j);
        fx.debris.push_back({pw, v, vec3(rnd01(e.seed, 67u, j), rnd01(e.seed, 71u, j), rnd01(e.seed, 73u, j)) * 9.0f, vec3(s, s * 0.7f, s * 0.8f)});
      }
      for (int j = 0; j < 6; ++j) {
        vec3 pa = removedAt[(size_t)(rnd01(e.seed, 79u, (uint32_t)j) * removedAt.size()) % removedAt.size()];
        fx.smoke(vec3(M * vec4(pa, 1)), vec3(0, 0.6f, 0) + vec3(M * vec4(dn, 0)) * 0.8f, 0.25f, 2.6f, vec4(0.62f, 0.58f, 0.52f, 0.5f), 2.2f, 0.25f);
      }
    }
  }
  return touched;
}

void App::rebuildPanel(InstanceRT& r, size_t k) {
  auto t0 = std::chrono::high_resolution_clock::now();
  const AssetDef* a = r.asset;
  const Panel& pn = a->panels[k];
  Frame f = panelFrame(pn, destruction.cell);
  WallField& w = fieldFor(r, k, f);
  // layers: the bay's intact boxes, as depth slabs over (u, v) rectangles
  struct Layer { float s0, s1, u0, u1, v0, v1; const Part* p; };
  std::vector<Layer> layers;
  for (auto& p : pn.parts[0]) {
    if (p.shape != Part::BOX && !(pn.rot && p.shape == Part::ROUND)) continue;   // a thatch slab is a rounded box
    const vec3 pc = panelToLocal(pn, p.center);   // GM3: a roof slab's layers in its frame (the slab and the courses on it)
    float c0 = pc[f.an] - p.size[f.an] * 0.5f, c1 = pc[f.an] + p.size[f.an] * 0.5f;
    float s0 = std::min(depthOf(f, c0), depthOf(f, c1)), s1 = std::max(depthOf(f, c0), depthOf(f, c1));
    layers.push_back({s0, s1, pc[f.au] - p.size[f.au] * 0.5f, pc[f.au] + p.size[f.au] * 0.5f, pc[f.av] - p.size[f.av] * 0.5f, pc[f.av] + p.size[f.av] * 0.5f, &p});
  }
  const mat4 xf = pn.rot ? glm::translate(mat4(1), pn.o) * glm::mat4_cast(pn.q) * glm::translate(mat4(1), -pn.o) : mat4(1);   // frame -> asset
  const float qd = destruction.depthStep;
  MeshData md; std::vector<Part> boxes;
  int open = 0, chamfers = 0; double removed = 0;
  for (int idx = 0; idx < f.nu * f.nv; ++idx) { float rm = w.rf[idx] + w.rb[idx]; removed += std::min(rm, f.T); if (rm >= f.T - 1e-4f) ++open; }
  std::vector<int> key((size_t)f.nu * f.nv);
  for (auto& L : layers) {
    bool thin = L.s1 - L.s0 < 0.06f;   // plaster / render skins are either there or gone
    // per-cell key: quantised (a, b) depth interval of surviving material in this layer, -1 = none
    for (int iv = 0; iv < f.nv; ++iv)
      for (int iu = 0; iu < f.nu; ++iu) {
        int idx = iv * f.nu + iu;
        float cu = f.u0 + (iu + 0.5f) * f.cu, cvv = f.v0 + (iv + 0.5f) * f.cv;
        key[idx] = -1;
        if (cu < L.u0 || cu > L.u1 || cvv < L.v0 || cvv > L.v1) continue;
        float s0 = std::max(L.s0, w.rf[idx]), s1 = std::min(L.s1, f.T - w.rb[idx]);
        if (thin) { if (s1 - s0 < 0.5f * (L.s1 - L.s0)) continue; key[idx] = 0; continue; }
        if (s1 - s0 < 0.004f) continue;
        int a0 = (int)std::round((s0 - L.s0) / qd), a1 = (int)std::round((s1 - L.s0) / qd);
        if (a1 <= a0) continue;
        key[idx] = a0 * 4096 + a1;
      }
    // greedy merge into boxes: run along u, then extend over v while the whole run matches
    std::vector<uint8_t> used((size_t)f.nu * f.nv, 0);
    for (int iv = 0; iv < f.nv; ++iv)
      for (int iu = 0; iu < f.nu; ++iu) {
        int idx = iv * f.nu + iu;
        if (used[idx] || key[idx] < 0) continue;
        int kk = key[idx], ue = iu;
        while (ue + 1 < f.nu && !used[iv * f.nu + ue + 1] && key[iv * f.nu + ue + 1] == kk) ++ue;
        int ve = iv;
        for (;;) {
          if (ve + 1 >= f.nv) break;
          bool ok = true;
          for (int x = iu; x <= ue && ok; ++x) { int m = (ve + 1) * f.nu + x; ok = !used[m] && key[m] == kk; }
          if (!ok) break;
          ++ve;
        }
        for (int y = iv; y <= ve; ++y) for (int x = iu; x <= ue; ++x) used[y * f.nu + x] = 1;
        float s0 = thin ? L.s0 : L.s0 + (kk / 4096) * qd, s1 = thin ? L.s1 : L.s0 + (kk % 4096) * qd;
        s1 = std::min(s1, L.s1);
        Part p = *L.p;
        float ua = std::max(L.u0, f.u0 + iu * f.cu), ub = std::min(L.u1, f.u0 + (ue + 1) * f.cu);
        float va = std::max(L.v0, f.v0 + iv * f.cv), vb = std::min(L.v1, f.v0 + (ve + 1) * f.cv);
        float xa = coordOf(f, s0), xb = coordOf(f, s1);
        p.center[f.au] = 0.5f * (ua + ub); p.size[f.au] = ub - ua;
        p.center[f.av] = 0.5f * (va + vb); p.size[f.av] = vb - va;
        p.center[f.an] = 0.5f * (xa + xb); p.size[f.an] = std::fabs(xb - xa);
        if (pn.rot) { p.rot = quat(1, 0, 0, 0); p.shape = Part::BOX; }   // frame-aligned: xf turns it into place
        appendPart(md, p, a->materials[p.mat], xf);
        if (p.collide) { Part c = p; if (pn.rot) { c.center = panelToAsset(pn, p.center); c.rot = pn.q; } boxes.push_back(c); }
      }
    // broken edges, not stair-steps: where a cell stands back from two adjacent neighbours (and the cell between
    // them) -- missing entirely, or broken deeper on the front or back face -- it is the inner corner of a 4 cm step.
    // A triangular wedge over its half toward that corner, through the depth the neighbours stand proud, turns runs
    // of steps into straight diagonal break lines on the hole outline and on the crater rim alike. Render only (at
    // most half a cell; collision keeps the cell boxes).
    auto K = [&](int u, int v) { return u < 0 || v < 0 || u >= f.nu || v >= f.nv ? -1 : key[v * f.nu + u]; };
    auto A0 = [&](int kk) { return thin ? 0 : kk / 4096; };                        // front of the material (steps)
    auto A1 = [&](int kk) { return thin ? 1 : kk % 4096; };                        // back of the material
    for (int iv = 0; iv < f.nv; ++iv)
      for (int iu = 0; iu < f.nu; ++iu) {
        int idx = iv * f.nu + iu, kx = key[idx];
        float cu = f.u0 + (iu + 0.5f) * f.cu, cvv = f.v0 + (iv + 0.5f) * f.cv;
        if (cu < L.u0 || cu > L.u1 || cvv < L.v0 || cvv > L.v1) continue;
        for (int face = 0; face < (kx < 0 || thin ? 1 : 2); ++face) {
          // "proud": the neighbour has material where this cell has none on this face
          auto proud = [&](int kn) {
            if (kn < 0) return false;
            if (kx < 0) return true;
            return face == 0 ? A0(kn) < A0(kx) : A1(kn) > A1(kx);
          };
          int l = K(iu - 1, iv), rr = K(iu + 1, iv), dd = K(iu, iv - 1), uu = K(iu, iv + 1);
          int su = proud(l) && !proud(rr) ? -1 : proud(rr) && !proud(l) ? 1 : 0;
          int sv = proud(dd) && !proud(uu) ? -1 : proud(uu) && !proud(dd) ? 1 : 0;
          if (!su || !sv) continue;
          int ka = su < 0 ? l : rr, kb = sv < 0 ? dd : uu, kc = K(iu + su, iv + sv);
          if (!proud(kc)) continue;
          int a0, a1;   // depth steps of the wedge
          if (kx < 0) { a0 = std::max({A0(ka), A0(kb), A0(kc)}); a1 = std::min({A1(ka), A1(kb), A1(kc)}); }
          else if (face == 0) { a0 = std::max({A0(ka), A0(kb), A0(kc)}); a1 = A0(kx); }
          else { a0 = A1(kx); a1 = std::min({A1(ka), A1(kb), A1(kc)}); }
          if (a1 <= a0) continue;
          float s0 = thin ? L.s0 : L.s0 + a0 * qd, s1 = thin ? L.s1 : std::min(L.s1, L.s0 + a1 * qd);
          float ua = f.u0 + iu * f.cu, ub = ua + f.cu, va = f.v0 + iv * f.cv, vb = va + f.cv;
          float uc = su < 0 ? ua : ub, vc = sv < 0 ? va : vb, uo = su < 0 ? ub : ua, vo = sv < 0 ? vb : va;   // corner toward the standing cells / opposite
          Part p = *L.p;
          p.shape = Part::PRISM; p.collide = false; p.rot = quat(1, 0, 0, 0);
          p.axis = f.an;
          float xa = coordOf(f, s0), xb = coordOf(f, s1);
          p.a0 = std::min(xa, xb); p.a1 = std::max(xa, xb);
          p.tri[0] = vec2(uc, vc); p.tri[1] = vec2(uo, vc); p.tri[2] = vec2(uc, vo);
          p.center = vec3(0); p.center[f.an] = 0.5f * (xa + xb); p.center[f.au] = 0.5f * (ua + ub); p.center[f.av] = 0.5f * (va + vb);
          p.size = vec3(0); p.size[f.an] = p.a1 - p.a0; p.size[f.au] = f.cu; p.size[f.av] = f.cv;
          appendPart(md, p, a->materials[p.mat], xf);
          ++chamfers;
        }
      }
  }
  r.pmesh[k].upload(md);
  r.pshape[k] = boxes.empty() ? nullptr : Physics::buildShape(boxes);
  w.active = true;
  w.openArea = open * f.cu * f.cv;
  w.removedFrac = (float)(removed / std::max(1.0, (double)f.nu * f.nv * f.T));
  w.boxes = (int)boxes.size();
  w.chamfers = chamfers;
  r.state[k] = w.openArea >= destruction.breachArea ? PANEL_BREACHED : w.removedFrac > 1e-5f ? PANEL_DAMAGED : PANEL_INTACT;
  r.hp[k] = r.state[k] == PANEL_BREACHED ? 0.0f : pn.hp * (1.0f - std::min(1.0f, w.removedFrac * 2.5f));
  if (pn.trim >= 0) { r.state[k] = w.removedFrac > 1e-5f ? PANEL_DAMAGED : PANEL_INTACT; r.hp[k] = w.removedFrac > 1e-5f ? 0.0f : pn.hp; }   // trim fell out of an opening: no new opening
  r.lastPanelMs = (float)std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
}

json App::wallEventsJson(const InstanceRT& r) const {
  json a = json::array();
  for (auto& e : r.wallEvents) a.push_back({e.p.x, e.p.y, e.p.z, e.d.x, e.d.y, e.d.z, e.kind, e.dmg, e.seed});
  return a;
}

// Authored damage of a copy: wall events stored in the map, plus old maps' per-panel HP converted once into the
// equivalent number of centred HE hits on that bay (so they keep their opening, now as real local damage).
void App::authoredWallEvents(const Instance& i, const AssetDef* a, std::vector<WallEvent>& out) const {
  out.clear();
  if (i.extra.is_object() && i.extra.contains("wallDamage"))
    for (auto& v : i.extra["wallDamage"]) {
      if (!v.is_array() || v.size() < 9) continue;
      out.push_back({vec3(v[0], v[1], v[2]), vec3(v[3], v[4], v[5]), v[6].get<int>(), v[7].get<float>(), v[8].get<uint32_t>()});
    }
  for (size_t k = 0; k < a->panels.size(); ++k) {
    auto& p = a->panels[k];
    auto ov = i.panelHp.find(p.id);
    if (ov == i.panelHp.end() || ov->second >= p.hp) continue;
    int hits = ov->second <= 0 ? std::max(2, (int)std::ceil(p.hp / 120.0f)) : 1;
    vec3 c = (p.bmin + p.bmax) * 0.5f, n = glm::normalize(p.normal);
    if (p.rot) c = p.o;   // GM3: a roof slab's centre
    for (int h = 0; h < hits; ++h) out.push_back({c + n * 0.5f * glm::dot(p.bmax - p.bmin, glm::abs(n)), -n, WALL_HE, 120.0f, (uint32_t)(k * 131 + h + 1)});
  }
}

void App::replayWallDamage(const Instance& i, InstanceRT& r) {
  initWallFields(r);
  for (size_t k = 0; k < r.asset->panels.size(); ++k) { r.state[k] = PANEL_INTACT; r.hp[k] = r.asset->panels[k].hp; }
  authoredWallEvents(i, r.asset, r.wallEvents);
  for (auto& e : r.wallEvents) stampWall(r, e, false, nullptr, nullptr);
}
