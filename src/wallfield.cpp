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
    case WALL_AP:      return {0.30f, 0.95f, 320.0f};   // AT shot: small, deep, perforates masonry in one hit
    case WALL_ROCKET:  return {0.30f, 0.55f, 290.0f};   // launchers: smaller breach than an AT-gun HE shell
    case WALL_GRENADE: return {0.22f, 0.07f, 80.0f};    // grenades: chips and cratering, no masonry breach
    case WALL_BULLET:  return {0.035f, 0.025f, 1.0f};   // small arms: plaster / face chips only
    default:           return {0.55f, 0.30f, 120.0f};   // HE shell: wide crater; a second hit there breaches
  }
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

int App::stampWall(InstanceRT& r, const WallEvent& e, bool effects, std::vector<std::string>* hitIds, int* newlyBreached) {
  const AssetDef* a = r.asset;
  const float cell = destruction.cell;
  vec3 dn = glm::length(e.d) > 1e-5f ? glm::normalize(e.d) : vec3(0, 0, -1);
  Profile pr = profileFor(e.kind);
  float strength = std::sqrt(std::max(e.dmg, 1e-3f) / pr.ref);
  int touched = 0;
  std::vector<vec3> removedAt;   // debris spawn points (asset space)
  for (size_t k = 0; k < a->panels.size(); ++k) {
    const Panel& pn = a->panels[k];
    Frame f = panelFrame(pn, cell);
    // resistance per unit depth (thickness already counts): masonry 1, reinforced concrete 1.2 (x HP / 450), timber 0.45
    float resist = std::max(0.2f, pn.family == "concrete" ? 1.2f * pn.hp / 450.0f : pn.hp / 200.0f);
    bool timber = pn.family == "timber", masonry = pn.family == "masonry";
    vec3 n(0); n[f.an] = f.ns;
    float cosI = std::fabs(glm::dot(dn, n));
    float elong = std::clamp(1.0f / std::max(cosI, 0.3f), 1.0f, 2.6f);   // oblique hits gouge along the shot
    float rad = pr.r * std::sqrt(strength) * (timber ? 1.25f : 1.0f);
    float reach = rad * elong * 1.2f + cell;
    // gate: the contact lies on / near this bay's face slab (within 0.2 m along its normal) and within the stamp's
    // reach in its plane -- so a corner hit also bites the edge of the wall around the corner, but walls in other
    // parallel planes are never touched
    {
      float dN = std::max({pn.bmin[f.an] - e.p[f.an], e.p[f.an] - pn.bmax[f.an], 0.0f});
      float du = std::max({pn.bmin[f.au] - e.p[f.au], e.p[f.au] - pn.bmax[f.au], 0.0f}), dv = std::max({pn.bmin[f.av] - e.p[f.av], e.p[f.av] - pn.bmax[f.av], 0.0f});
      if (dN > 0.2f || std::sqrt(du * du + dv * dv) > rad * elong) continue;
    }
    float D = pr.depth * strength * (0.55f + 0.45f * cosI) / resist;
    bool fromFront = glm::dot(dn, n) <= 0.0f;   // travelling against the normal: struck the front face
    // tangential direction of travel in the bay's (u, v) plane
    vec2 t(dn[f.au], dn[f.av]); float tl = glm::length(t); t = tl > 1e-4f ? t / tl : vec2(1, 0);
    float pu = e.p[f.au], pv = e.p[f.av];
    int iu0 = std::max(0, (int)std::floor((pu - reach - f.u0) / f.cu)), iu1 = std::min(f.nu - 1, (int)std::floor((pu + reach - f.u0) / f.cu));
    int iv0 = std::max(0, (int)std::floor((pv - reach - f.v0) / f.cv)), iv1 = std::min(f.nv - 1, (int)std::floor((pv + reach - f.v0) / f.cv));
    if (iu0 > iu1 || iv0 > iv1) continue;
    WallField& w = fieldFor(r, k, f);
    // 1) raw removal per cell
    struct Add { int idx; float add; int64_t stone; };
    std::vector<Add> adds;
    std::unordered_map<int64_t, std::pair<float, int>> stones;
    for (int iv = iv0; iv <= iv1; ++iv)
      for (int iu = iu0; iu <= iu1; ++iu) {
        float cu = f.u0 + (iu + 0.5f) * f.cu, cvv = f.v0 + (iv + 0.5f) * f.cv;
        vec2 d(cu - pu, cvv - pv);
        float along = glm::dot(d, t), perp = glm::dot(d, vec2(-t.y, t.x));
        if (tl > 1e-4f) along /= elong;
        if (timber) d.y /= 2.2f;   // boards split along the vertical grain
        float q = (timber ? glm::length(d) : std::sqrt(along * along + perp * perp)) / rad;
        q *= 0.85f + 0.3f * rnd01(e.seed, (uint32_t)iu * 7919u, (uint32_t)iv);   // bounded deterministic irregularity
        if (q >= 1.0f) continue;
        // brittle break: full removal over the core, a short steep shoulder (no shallow terraces around it)
        float fall = q < 0.55f ? 1.0f : std::pow(1.0f - (q - 0.55f) / 0.45f, 0.6f);
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
        if (effects && (near + far >= f.T) && removedAt.size() < 64) {
          int iu = ad.idx % f.nu, iv = ad.idx / f.nu;
          vec3 pa(0); pa[f.au] = f.u0 + (iu + 0.5f) * f.cu; pa[f.av] = f.v0 + (iv + 0.5f) * f.cv; pa[f.an] = coordOf(f, f.T * 0.5f);
          removedAt.push_back(pa);
        }
      }
    }
    if (!changed) continue;
    // 4) unsupported fragments: solid cells no longer connected to the bay's edges (where it meets the rest of the
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
        if (!seen[idx] && solid(idx)) {
          w.rb[idx] = f.T - w.rf[idx];
          if (effects && removedAt.size() < 64) { int iu = idx % f.nu, iv = idx / f.nu; vec3 pa(0); pa[f.au] = f.u0 + (iu + 0.5f) * f.cu; pa[f.av] = f.v0 + (iv + 0.5f) * f.cv; pa[f.an] = coordOf(f, f.T * 0.5f); removedAt.push_back(pa); }
        }
    }
    uint8_t oldState = r.state[k];
    rebuildPanel(r, k);
    ++touched;
    if (hitIds) hitIds->push_back(pn.id);
    if (newlyBreached && oldState != PANEL_BREACHED && r.state[k] == PANEL_BREACHED) ++*newlyBreached;
  }
  // a bounded handful of masonry / timber fragments thrown out of the far side and back toward the shooter
  if (effects && !removedAt.empty()) {
    const Instance* inst = doc.find(r.id);
    if (inst) {
      mat4 M = instanceMatrix(*inst);
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
  int open = 0; double removed = 0;
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
  }
  r.pmesh[k].upload(md);
  r.pshape[k] = boxes.empty() ? nullptr : Physics::buildShape(boxes);
  w.active = true;
  w.openArea = open * f.cu * f.cv;
  w.removedFrac = (float)(removed / std::max(1.0, (double)f.nu * f.nv * f.T));
  w.boxes = (int)boxes.size();
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
