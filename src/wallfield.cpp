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

struct Frame { int an, au, av; float ns, T, front; float u0, v0; int nu, nv; float cu, cv; };
Frame panelFrame(const Panel& p, float cell) {
  Frame f{};
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

// bays in the same wall slab as bay k (same normal axis, overlapping thickness)
static std::vector<size_t> coplanarBays(const AssetDef* a, size_t k, int ank, float cell) {
  std::vector<size_t> out;
  const Panel& pk = a->panels[k];
  for (size_t j = 0; j < a->panels.size(); ++j) {
    const Panel& pj = a->panels[j];
    if (j == k || panelFrame(pj, cell).an != ank) continue;
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
    removedAt.push_back(pa);
  };
  for (size_t k = 0; k < a->panels.size(); ++k) {
    const Panel& pn = a->panels[k];
    Frame f = panelFrame(pn, cell);
    // resistance per unit depth (thickness already counts): masonry 1, reinforced concrete 1.2 (x HP / 450), timber 0.45
    float resist = std::max(0.2f, pn.family == "concrete" ? 1.2f * pn.hp / 450.0f : pn.hp / 200.0f);
    bool timber = pn.family == "timber", masonry = pn.family == "masonry";
    vec3 n(0); n[f.an] = f.ns;
    float dN = std::max({pn.bmin[f.an] - e.p[f.an], e.p[f.an] - pn.bmax[f.an], 0.0f});   // stand-off from the bay's slab
    vec3 dn = dn0;
    float standoff = 1.0f;
    if (blast) {   // omnidirectional: the bay is struck square on from the side the charge lies on, weaker with distance
      dn = vec3(0); dn[f.an] = e.p[f.an] >= 0.5f * (pn.bmin[f.an] + pn.bmax[f.an]) ? -1.0f : 1.0f;
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
      float du = std::max({pn.bmin[f.au] - e.p[f.au], e.p[f.au] - pn.bmax[f.au], 0.0f}), dv = std::max({pn.bmin[f.av] - e.p[f.av], e.p[f.av] - pn.bmax[f.av], 0.0f});
      if (dN > (blast ? 0.6f : 0.2f) || std::sqrt(du * du + dv * dv) > rad * elong * 1.3f) continue;
    }
    float D = pr.depth * strength * (0.55f + 0.45f * cosI) / resist * (blast ? 0.35f + 0.65f * standoff : 1.0f);
    bool fromFront = glm::dot(dn, n) <= 0.0f;   // travelling against the normal: struck the front face
    // tangential direction of travel in the bay's (u, v) plane
    vec2 t(dn[f.au], dn[f.av]); float tl = glm::length(t); t = tl > 1e-4f ? t / tl : vec2(1, 0);
    float pu = e.p[f.au], pv = e.p[f.av];
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
  //    (this also sees open cells in the coplanar bay across a seam); a ridge of at most that width standing
  //    proud between two deeper craters on the struck face is broken down to the shallower side. Wider strips
  //    are left alone, so holes that are genuinely apart stay apart.
  const int N = (int)std::floor(destruction.bridgeMax / cell + 1e-3f);   // 0: cleanup off (A/B in the self-test)
  for (int pass = 0, any = 1; pass < 4 && any && N >= 1; ++pass) {   // repeat while clearing exposes new bridges
    any = 0;
    for (auto& b : bays) {
      const Frame& f = b.f;
      WallField& w = r.wf[b.k];
      const std::vector<size_t> cands = coplanarBays(a, b.k, f.an, cell);
      auto openAt = [&](int iu, int iv) -> bool {
        if (iu >= 0 && iv >= 0 && iu < f.nu && iv < f.nv) { int idx = iv * f.nu + iu; return w.rf[idx] + w.rb[idx] >= f.T - 1e-4f; }
        return openInBay(r, cands, f.u0 + (iu + 0.5f) * f.cu, f.v0 + (iv + 0.5f) * f.cv, cell) == 1;
      };
      auto nearAt = [&](int iu, int iv) -> float {
        if (iu < 0 || iv < 0 || iu >= f.nu || iv >= f.nv) return -1.0f;
        int idx = iv * f.nu + iu; return b.fromFront ? w.rf[idx] : w.rb[idx];
      };
      const int dirs[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
      std::vector<int> open, raise; std::vector<float> raiseTo;
      int ju0 = std::max(0, b.iu0 - N), ju1 = std::min(f.nu - 1, b.iu1 + N), jv0 = std::max(0, b.iv0 - N), jv1 = std::min(f.nv - 1, b.iv1 + N);
      for (int iv = jv0; iv <= jv1; ++iv)
        for (int iu = ju0; iu <= ju1; ++iu) {
          int idx = iv * f.nu + iu;
          if (w.rf[idx] + w.rb[idx] >= f.T - 1e-4f) continue;
          float here = b.fromFront ? w.rf[idx] : w.rb[idx], best = here;
          bool bridge = false;
          for (auto& dd : dirs) {
            int lim = dd[0] && dd[1] ? std::max(1, (int)(N / 1.4142f)) : N;
            int sp = 0, sm = 0; float mp = -1, mm = -1;
            for (int s = 1; s <= lim; ++s) { if (!sp && openAt(iu + dd[0] * s, iv + dd[1] * s)) sp = s; mp = std::max(mp, nearAt(iu + dd[0] * s, iv + dd[1] * s)); }
            for (int s = 1; s <= lim; ++s) { if (!sm && openAt(iu - dd[0] * s, iv - dd[1] * s)) sm = s; mm = std::max(mm, nearAt(iu - dd[0] * s, iv - dd[1] * s)); }
            if (sp && sm && sp + sm - 1 <= lim) { bridge = true; break; }
            float ridge = std::min(mp, mm);
            if (ridge > here + 0.15f * f.T) best = std::max(best, ridge);
          }
          if (bridge) open.push_back(idx);
          else if (best > here) { raise.push_back(idx); raiseTo.push_back(best); }
        }
      for (int idx : open) { w.rb[idx] = f.T - w.rf[idx]; debrisAt(f, idx); }
      for (size_t q = 0; q < raise.size(); ++q) {
        int idx = raise[q];
        float& near = b.fromFront ? w.rf[idx] : w.rb[idx];
        float& far = b.fromFront ? w.rb[idx] : w.rf[idx];
        near = std::min(f.T - far, raiseTo[q]);
        if (f.T - near - far < 0.28f * f.T) { far = f.T - near; open.push_back(idx); debrisAt(f, idx); }   // the thinned ridge gives way
      }
      if (!open.empty()) any = 1;   // only new holes can expose new bridges
    }
  }
  for (auto& b : bays) {
    const Panel& pn = a->panels[b.k];
    const Frame& f = b.f;
    WallField& w = r.wf[b.k];
    // 5) unsupported fragments: solid cells no longer connected to the bay's edges (where it meets the rest of the
    //    building) fall out
    {
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
        if (!seen[idx] && solid(idx)) { w.rb[idx] = f.T - w.rf[idx]; debrisAt(f, idx); }
    }
    uint8_t oldState = r.state[b.k];
    rebuildPanel(r, b.k);
    ++touched;
    if (hitIds) hitIds->push_back(pn.id);
    if (newlyBreached && oldState != PANEL_BREACHED && r.state[b.k] == PANEL_BREACHED) ++*newlyBreached;
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
    if (p.shape != Part::BOX) continue;
    float c0 = p.center[f.an] - p.size[f.an] * 0.5f, c1 = p.center[f.an] + p.size[f.an] * 0.5f;
    float s0 = std::min(depthOf(f, c0), depthOf(f, c1)), s1 = std::max(depthOf(f, c0), depthOf(f, c1));
    layers.push_back({s0, s1, p.center[f.au] - p.size[f.au] * 0.5f, p.center[f.au] + p.size[f.au] * 0.5f, p.center[f.av] - p.size[f.av] * 0.5f, p.center[f.av] + p.size[f.av] * 0.5f, &p});
  }
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
        appendPart(md, p, a->materials[p.mat], mat4(1));
        if (p.collide) boxes.push_back(p);
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
          appendPart(md, p, a->materials[p.mat], mat4(1));
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
    for (int h = 0; h < hits; ++h) out.push_back({c + n * 0.5f * glm::dot(p.bmax - p.bmin, glm::abs(n)), -n, WALL_HE, 120.0f, (uint32_t)(k * 131 + h + 1)});
  }
}

void App::replayWallDamage(const Instance& i, InstanceRT& r) {
  initWallFields(r);
  for (size_t k = 0; k < r.asset->panels.size(); ++k) { r.state[k] = PANEL_INTACT; r.hp[k] = r.asset->panels[k].hp; }
  authoredWallEvents(i, r.asset, r.wallEvents);
  for (auto& e : r.wallEvents) stampWall(r, e, false, nullptr, nullptr);
}
