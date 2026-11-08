// Ivy on stone walls (B1, docs/asset-format.md#ivy): alpha-tested leaf cards hugging the exterior faces of a building's
// stone / brick / rendered walls, grown from the ground in colonies seeded per instance. Drawn through the foliage
// path (wind, culling, shadows); the cards on a bay go when the bay is breached; density follows the grass quality
// and the Settings toggle (App::ivyOn).
#include "app.h"
#include "settings.h"
#include <algorithm>
#include <cfloat>
#include <random>

namespace {
// one exterior wall face in asset space: normal axis n (0 x, 2 z), outward sign, plane, rectangle u (the other
// horizontal axis) x v (y), and the bay it belongs to (-1: static wall)
struct Face { int n; float sign, plane, u0, u1, v0, v1; int panel; };

bool ivyMaterial(const Material& m) {
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
    if (!p.collide || p.bv > 0 || p.mat < 0 || p.mat >= (int)a.materials.size() || !ivyMaterial(a.materials[p.mat]) || p.size.y < 0.3f) return;   // bevelled: trim, the ivy goes round it
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
}  // namespace

// Cards (pos, normal, uv, mask, shade: the TreeDef card layout) for one instance; breached bays get none.
void buildIvyCards(const AssetDef& a, const IvyDef& iv, const std::vector<uint8_t>& state, uint32_t seed, float density, std::vector<float>& v) {
  std::vector<Face> faces; std::vector<std::pair<vec3, vec3>> solids; collectFaces(a, faces, solids);
  if (faces.empty() || density <= 0) return;
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
  struct Colony { int facade; float uc, w0, gy, top, dens; };
  std::vector<Colony> cols;
  std::mt19937 g(seed * 2654435761u + 0x9e3779b9u);
  if (iv.mode == IvyDef::AUTO) {
    for (int k = 0; k < (int)fac.size(); ++k) {
      const Facade& d = fac[k]; float L = d.u1 - d.u0;
      if (L < 1.0f || d.v1 - d.v0 < 1.0f) continue;
      int n = std::clamp((int)(L / 7.0f * (0.3f + 0.9f * u01(g)) + 0.35f), 0, 4);
      for (int c = 0; c < n; ++c) cols.push_back({k, d.u0 + (0.08f + 0.84f * u01(g)) * L, 0.4f + 0.8f * u01(g), d.v0, std::min(d.v1 - 0.35f, d.v0 + 1.8f + 3.4f * u01(g)), iv.density});
    }
    if (cols.empty()) {   // "auto" always shows some: one colony on the widest facade
      int best = -1; float bl = 0;
      for (int k = 0; k < (int)fac.size(); ++k) if (fac[k].u1 - fac[k].u0 > bl && fac[k].v1 - fac[k].v0 >= 1.0f) { bl = fac[k].u1 - fac[k].u0; best = k; }
      if (best >= 0) { const Facade& d = fac[best]; cols.push_back({best, (d.u0 + d.u1) * 0.5f, 0.9f, d.v0, std::min(d.v1 - 0.35f, d.v0 + 3.2f), iv.density}); }
    }
  } else {
    for (auto& b : iv.blocks) {   // authored blocks: colonies inside the rectangle, on the facade facing that way that covers most of it
      // of the facades facing that way that cover at least half as much as the best one, the outermost (a facade slab
      // over a solid core: the slab)
      auto ov = [&](const Facade& d) { return d.n != b.axis || d.sign != b.sign ? 0.0f : std::max(0.0f, std::min(b.a1, d.u1) - std::max(b.a0, d.u0)) * std::max(0.0f, std::min(b.y1, d.v1) - std::max(b.y0, d.v0)); };
      float best = 0; for (auto& d : fac) best = std::max(best, ov(d));
      int k = -1;
      for (int f = 0; f < (int)fac.size(); ++f) if (best > 0 && ov(fac[f]) >= 0.5f * best && (k < 0 || fac[f].sign * fac[f].plane > fac[k].sign * fac[k].plane)) k = f;
      if (k < 0) continue;
      {
        const Facade& d = fac[k];
        float L = b.a1 - b.a0;
        int n = std::max(1, (int)(L / 2.5f));
        for (int c = 0; c < n; ++c) cols.push_back({k, b.a0 + (c + 0.5f) / n * L + (u01(g) - 0.5f) * 0.4f, std::min(L / n * 0.55f, 1.4f), std::max(b.y0, d.v0), std::min(b.y1, d.v1 - 0.2f), b.density});
      }
    }
  }
  const float step = 0.11f;
  for (const Colony& c : cols) {
    const Facade& d = fac[c.facade];
    float H = c.top - c.gy; if (H < 0.4f) continue;
    float dens = density * c.dens;
    for (float y = c.gy + 0.04f; y < c.top; y += step) {
      float t = (y - c.gy) / H;
      float w = c.w0 * (1.0f + 0.7f * t) * std::sqrt(std::max(0.0f, 1.0f - t * t * t)) * (0.8f + 0.4f * u01(g));
      for (float uu = c.uc - w; uu <= c.uc + w; uu += step) {
        float u = uu + (u01(g) - 0.5f) * step * 0.8f, yy = y + (u01(g) - 0.5f) * step * 0.8f;
        float edge = std::clamp((1.0f - std::fabs(u - c.uc) / std::max(w, 0.05f)) / 0.35f, 0.0f, 1.0f);
        if (u01(g) > dens * (0.92f - 0.4f * t) * (0.25f + 0.75f * edge)) continue;
        int on = -1;   // on a wall face (not in an opening); a breached bay has none
        for (int f : d.faces) { const Face& F = faces[f]; if (u > F.u0 + 0.03f && u < F.u1 - 0.03f && yy > F.v0 + 0.02f && yy < F.v1 - 0.03f) { on = f; break; } }
        if (on < 0) continue;
        int pk = faces[on].panel;
        if (pk >= 0 && pk < (int)state.size() && state[pk] == PANEL_BREACHED) continue;
        vec3 nrm(0), eu(0), ev(0, 1, 0);
        nrm[d.n] = d.sign; eu[2 - d.n] = 1.0f;
        vec3 ctr = eu * u + vec3(0, yy, 0); ctr[d.n] = faces[on].plane + d.sign * (0.02f + 0.035f * u01(g));
        { vec3 front = ctr; front[d.n] = faces[on].plane + d.sign * 0.06f; if (inSolid(solids, front) || inSolid(solids, ctr)) continue; }   // covered: a pier, a trim, a slab in front
        float s = (0.09f + 0.07f * u01(g)) * (0.75f + 0.25f * (1.0f - t)), th = u01(g) * 6.2831853f;
        vec3 T = (eu * std::cos(th) + ev * std::sin(th)) * s, B = (-eu * std::sin(th) + ev * std::cos(th)) * s;
        vec3 ln = glm::normalize(nrm + eu * (u01(g) - 0.5f) * 0.5f + ev * (0.1f + 0.25f * u01(g)));
        float shade = std::clamp(0.55f + 0.3f * t + 0.2f * u01(g), 0.4f, 1.0f), mask = 0.06f + 0.06f * t;
        vec3 q[4] = {ctr - T - B, ctr + T - B, ctr + T + B, ctr - T + B};
        vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (int k : {0, 1, 2, 0, 2, 3}) v.insert(v.end(), {q[k].x, q[k].y, q[k].z, ln.x, ln.y, ln.z, uv[k].x, uv[k].y, mask, shade});
      }
    }
  }
}

void ivyDestroy(InstanceRT& r) {
  if (r.ivy) { glDeleteVertexArrays(1, &r.ivy->cardVao); glDeleteBuffers(1, &r.ivy->cardVbo); r.ivy.reset(); }
  r.ivyKey = 0;
}

// Adds the instance's ivy to the foliage list, rebuilding the cards when a bay breaks, the quality or the toggle changes.
void ivyDrawItem(App& app, const Instance& i, InstanceRT& r, const mat4& M, float camDist) {
  const AssetDef& a = *r.asset;
  if (a.ivy.mode == IvyDef::NONE) return;
  int gq = (int)app.grass.quality;
  float density = !app.ivyOn || gq == 0 ? 0.0f : gq == 1 ? 0.6f : gq == 2 ? 1.0f : 1.15f;
  uint64_t key = 1469598103934665603ull ^ (uint64_t)(density * 100) ^ ((uint64_t)i.rev << 20);
  for (uint8_t s : r.state) key = (key ^ (s == PANEL_BREACHED)) * 1099511628211ull;
  if (key != r.ivyKey) {
    ivyDestroy(r);
    r.ivyKey = key;
    std::vector<float> v;
    if (density > 0) buildIvyCards(a, a.ivy, r.state, (uint32_t)(i.id * 7919u + 17u), density, v);
    if (!v.empty()) {
      auto t = std::make_shared<TreeDef>();
      t->leafCol = vec3(0.045f, 0.085f, 0.028f);
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
  if (r.ivy && camDist < 260.0f * settings().drawDist()) app.R.foliage.push_back({r.ivy.get(), M});
}
