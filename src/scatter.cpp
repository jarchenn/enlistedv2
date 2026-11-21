// BF B2 / B3 ground scatter: stones, twigs, roots, litter sheets and moss cushions (scatter.h).
#include "scatter.h"
#include "app.h"
#include "assets.h"
#include "grass.h"
#include "ground_paint.h"
#include "moss.h"
#include "render.h"
#include "roads.h"
#include "water_query.h"
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <unordered_map>

void appendStone(MeshData& out, int variant, const mat4& M, vec3 tint, int lith);   // nature.cpp
void natureStone(MeshData& m, vec3 w, vec3 groundNrm, float size, float r0, float r1, float r2, float r3, int lith);   // nature.cpp

namespace {
constexpr float kChunk = 16.0f;
constexpr float kFarStones = 60.0f, kNearStones = 18.0f, kRoots = 40.0f;
ScatterStats gStats;

inline float sat(float x) { return std::clamp(x, 0.0f, 1.0f); }
inline float ss(float a, float b, float x) { float t = sat((x - a) / (b - a)); return t * t * (3 - 2 * t); }
inline uint32_t hash4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {   // stable across runs, builds and presets
  uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du ^ (d + 0x27D4EB2Fu) * 0x27D4EB2Fu;
  h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15; return h;
}
inline float u01(uint32_t h) { return (h & 0xFFFFFFu) / 16777216.0f; }
enum Layer : uint32_t { L_CLUSTER = 1, L_STONE, L_TWIG, L_ROOT, L_LITTER, L_CUSHION, L_LOOSE };

struct Cushion { vec4 a, q, c; };   // (pos, scale), rotation quaternion (xyzw), (height scale, hue, variant, -)
struct Chunk {
  int cx = 0, cz = 0; uint64_t sig = 0; bool built = false; uint64_t used = 0;
  GpuMesh nearM, farM;          // small stones + twigs (to 18 m); large stones + roots (to 60 m)
  std::vector<vec4> contacts;   // 3 vec4 each (Renderer::pushContactAO)
  std::vector<Cushion> cush;
  GLuint litVao = 0, litVbo = 0, litEbo = 0; int litCount = 0;
  vec4 contact[6]{};            // DrawItem::contact: the chunk's ground grid (groundH) and habitat for the rock moss
  void release() {
    nearM.destroy(); farM.destroy(); contacts.clear(); cush.clear();
    if (litVao) glDeleteVertexArrays(1, &litVao); if (litVbo) glDeleteBuffers(1, &litVbo); if (litEbo) glDeleteBuffers(1, &litEbo);
    litVao = litVbo = litEbo = 0; litCount = 0; built = false;
  }
};
std::unordered_map<int64_t, Chunk> gChunks;
inline int64_t ckey(int x, int z) { return (int64_t(uint32_t(x)) << 32) | uint32_t(z); }

struct MapCfg { float density = 0; bool moss = true; uint32_t seed = 1; };
MapCfg readCfg(const App& app) {
  MapCfg c; c.seed = app.doc.seed;
  const json& e = app.doc.extra;
  if (e.is_object() && e.contains("look") && e["look"].is_object() && e["look"].contains("scatter")) {
    const json& s = e["look"]["scatter"];
    if (s.is_number()) c.density = s.get<float>();
    else if (s.is_object()) { c.density = s.value("density", 1.0f); c.moss = s.value("moss", 1) != 0; }
  }
  return c;
}

// ---- the context a chunk build reads (footprints, trees, paint)
struct Ctx {
  const App* app; const Terrain* t; MapCfg cfg; float mul; int tier; vec2 wind;
  const std::vector<GrassFootprint>* fp; float bias = 0;
};
float fpSignedDist(const GrassFootprint& f, vec2 p) {   // metres outside the footprint rectangle (negative inside)
  const float c = std::cos(radians(f.yaw)), s = std::sin(radians(f.yaw));
  const vec2 d = p - f.pos, l(c * d.x - s * d.y, s * d.x + c * d.y);
  const vec2 ctr = 0.5f * (f.bmin + f.bmax), he = 0.5f * (f.bmax - f.bmin);
  const vec2 q = glm::abs(l - ctr) - he;
  return glm::length(glm::max(q, vec2(0))) + std::min(std::max(q.x, q.y), 0.0f);
}
float nearestFp(const Ctx& x, vec2 p, vec2* dirOut = nullptr) {
  float best = 1e9f;
  for (const auto& f : *x.fp) {
    if (f.soft) continue;
    const float r = glm::length(glm::max(glm::abs(f.bmin), glm::abs(f.bmax))) + 3.0f;
    if (glm::dot(p - f.pos, p - f.pos) > r * r) continue;
    const float d = fpSignedDist(f, p);
    if (d < best) { best = d; if (dirOut) *dirOut = p - f.pos; }
  }
  return best;
}
float traffic(vec2 p) {   // paths and tracks: ground paint (2 dirt, 3 mud, 4 gravel, 6 cobbles) and the authored road's trail (roads.h)
  return sat(std::max({groundPaintWeight(p.x, p.y, 2), 0.8f * groundPaintWeight(p.x, p.y, 3), 0.7f * groundPaintWeight(p.x, p.y, 4), groundPaintWeight(p.x, p.y, 6), roadLateral(p.x, p.y).traffic}));
}
float roadRim(vec2 p) {   // 1 on the road out to its rim (4.3: rim = 1 - ss(0.72 w, 1.22 w, |x|)), 0 beyond: moss stops dead there
  const RoadQ r = roadLateral(p.x, p.y); return r.w > 0.0f ? 1.0f - ss(0.72f * r.w, 1.22f * r.w, std::fabs(r.lat)) : 0.0f;
}
// a candidate spot: ground height and normal; false where nothing may rest (water, footprints, steeper than repose)
bool spot(const Ctx& x, vec2 p, float reposeDeg, float& h, vec3& n, float fpClear = 0.15f) {
  h = x.t->height(p.x, p.y); n = x.t->normal(p.x, p.y);
  if (n.y < std::cos(radians(reposeDeg))) return false;
  const float wl = waterAt(*x.app, p.x, p.y); if (!waterNone(wl) && wl > h - 0.01f) return false;
  if (poolAt(p.x, p.y, h) > 0.005f) return false;   // the map's authored pools (roads.h)
  if (nearestFp(x, p) < fpClear) return false;
  return true;
}

void pushTube(MeshData& m, const std::vector<vec3>& pts, const std::vector<float>& rad, int sides, vec3 alb) {
  const uint32_t base = (uint32_t)m.v.size();
  for (size_t k = 0; k < pts.size(); ++k) {
    const vec3 dir = glm::normalize(pts[std::min(k + 1, pts.size() - 1)] - pts[k > 0 ? k - 1 : 0]);
    vec3 a = glm::normalize(glm::cross(dir, vec3(0, 1, 0))); if (!std::isfinite(a.x)) a = vec3(1, 0, 0);
    const vec3 b = glm::cross(a, dir);
    for (int s = 0; s < sides; ++s) {
      const float t = 6.2831853f * s / sides; const vec3 nn = std::cos(t) * a + std::sin(t) * b;
      Vertex v{}; v.pos = pts[k] + nn * rad[k]; v.nrm = nn; v.albedo = alb; v.params = vec4(0, 0.85f, 0, 0); v.pcoord = v.pos; v.pnrm = nn;
      m.v.push_back(v);
    }
  }
  for (size_t k = 0; k + 1 < pts.size(); ++k) for (int s = 0; s < sides; ++s) {
    const uint32_t a0 = base + uint32_t(k * sides + s), a1 = base + uint32_t(k * sides + (s + 1) % sides), b0 = a0 + sides, b1 = a1 + sides;
    m.i.insert(m.i.end(), {a0, b0, a1, a1, b0, b1});
  }
}
void pushContact(Chunk& c, vec3 base, float yaw, vec2 half, float k, float lo, float hi) {
  const float w = std::clamp(0.1f + 0.15f * std::min(half.x, half.y), 0.1f, 0.25f);
  c.contacts.push_back(vec4(base, yaw)); c.contacts.push_back(vec4(half, w, k)); c.contacts.push_back(vec4(lo, hi, 0, 0));
}

void buildChunk(const Ctx& x, Chunk& c) {
  c.release();
  const vec2 o(c.cx * kChunk, c.cz * kChunk);
  const uint32_t sd = x.cfg.seed, ux = uint32_t(c.cx), uz = uint32_t(c.cz);
  const bool check = std::getenv("NMK_SCATTER_CHECK") != nullptr;
  auto inChunk = [&](vec2 p) { return p.x >= o.x && p.y >= o.y && p.x < o.x + kChunk && p.y < o.y + kChunk; };
  auto fail = [&](const char* what, vec3 p, uint32_t s) { ++gStats.checkFails; logf("scatter check: %s at (%.2f, %.2f, %.2f) chunk %d,%d seed %u cand %08x ground %.3f", what, p.x, p.y, p.z, c.cx, c.cz, sd, s, x.t->height(p.x, p.z)); };
  MeshData nearM, farM;
  // ---- stones: two-level Poisson. Cluster centres on a 7 m jittered lattice (0.01-0.03 /m^2), 2-8 stones /m^2 within
  // 1-3 m of each, a few large ones at the cores; plus sparse loose stones. Cells beyond the chunk feed clusters that reach in.
  const float Lc = 7.0f;
  const int l0x = int(std::floor((o.x - 3) / Lc)), l1x = int(std::floor((o.x + kChunk + 3) / Lc)), l0z = int(std::floor((o.y - 3) / Lc)), l1z = int(std::floor((o.y + kChunk + 3) / Lc));
  for (int lz = l0z; lz <= l1z; ++lz) for (int lx = l0x; lx <= l1x; ++lx) {
    const uint32_t hc = hash4(sd, uint32_t(lx), uint32_t(lz), L_CLUSTER);
    const vec2 cc = vec2(lx + u01(hc), lz + u01(hc >> 7)) * Lc;
    const float rockPaint = std::max({groundPaintWeight(cc.x, cc.y, 7), groundPaintWeight(cc.x, cc.y, 12), groundPaintWeight(cc.x, cc.y, 4)});
    if (u01(hash4(hc, 1, 0, 0)) > 0.75f * x.cfg.density * (1.0f + 1.5f * rockPaint)) continue;   // ~0.015 /m^2 at density 1
    const float R = 1.0f + 2.0f * u01(hash4(hc, 2, 0, 0)), dens = 2.0f + 6.0f * u01(hash4(hc, 3, 0, 0));
    const int n = int(dens * 3.14159f * R * R * 0.35f);   // the core is dense, the rim sparse (radius below)
    for (int k = 0; k < n; ++k) {
      const uint32_t hk = hash4(hc, uint32_t(k), L_STONE, 7);
      if (u01(hk) >= x.mul) continue;   // the preset's share: by the candidate's own seed, never re-placed
      const float rr = R * std::pow(u01(hk >> 3), 0.75f), th = 6.2831853f * u01(hash4(hk, 1, 0, 0));
      const vec2 p = cc + rr * vec2(std::cos(th), std::sin(th));
      if (!inChunk(p)) continue;
      const bool large = k < 2 && u01(hash4(hk, 2, 0, 0)) < 0.6f;
      float h; vec3 nrm; if (!spot(x, p, large ? 40.0f : 34.0f, h, nrm)) continue;
      if (traffic(p) > 0.5f) continue;   // not on the trodden line
      const float uu = u01(hash4(hk, 3, 0, 0));
      const float size = large ? 0.2f + 0.4f * uu * uu : 0.03f * std::pow(4.0f, uu * uu);
      const int lith = u01(hash4(hk, 4, 0, 0)) < 0.25f + 0.5f * mossVnCpu(vec3(p.x, 0, p.y) * 0.02f) ? 1 : 2;
      MeshData& m = large ? farM : nearM; const size_t v0 = m.v.size();
      natureStone(m, vec3(p.x, h, p.y), nrm, size, u01(hash4(hk, 5, 0, 0)), u01(hash4(hk, 6, 0, 0)), u01(hash4(hk, 7, 0, 0)), 0.0f, lith);
      // sink 25-45 % of the stone's height: shift the appended vertices so the bottom is that far below the ground
      float mnY = 1e9f, mxY = -1e9f; for (size_t q = v0; q < m.v.size(); ++q) { mnY = std::min(mnY, m.v[q].pos.y); mxY = std::max(mxY, m.v[q].pos.y); }
      const float H = mxY - mnY, sink = 0.25f + 0.20f * u01(hash4(hk, 8, 0, 0)), dy = (h - sink * H) - mnY;
      for (size_t q = v0; q < m.v.size(); ++q) { m.v[q].pos.y += dy; m.v[q].pcoord = m.v[q].pos; }
      if (check && (mnY + dy > h - 0.2f * H || mnY + dy < h - 0.5f * H)) fail("stone sink out of 25-45 %", vec3(p.x, h, p.y), hk);
      if (size > 0.06f) pushContact(c, vec3(p.x, h, p.y), 0.0f, vec2(0.45f * size), large ? 0.6f : 0.45f, h - 0.4f, h + 0.25f);
      ++gStats.stones;
    }
  }
  // ---- twigs: 0.1-0.5 m sticks under and near canopies (0.3-1.5 /m^2), lying in the ground, to 14 m with the small stones
  for (int gz = 0; gz < int(kChunk); ++gz) for (int gx = 0; gx < int(kChunk); ++gx) {
    const vec2 cell = o + vec2(gx, gz);
    const float canopy = mossHabitat(cell.x + 0.5f, cell.y + 0.5f).w;
    const float dens = (0.2f + 1.3f * canopy) * x.cfg.density;
    const uint32_t hg = hash4(sd, ux * 64u + uint32_t(gx), uz * 64u + uint32_t(gz), L_TWIG);
    const int n = int(dens + u01(hg));
    for (int k = 0; k < n; ++k) {
      const uint32_t hk = hash4(hg, uint32_t(k), 0, 0); if (u01(hk) >= x.mul) continue;
      const vec2 p = cell + vec2(u01(hk >> 2), u01(hash4(hk, 1, 0, 0)));
      float h; vec3 nrm; if (!spot(x, p, 40.0f, h, nrm) || traffic(p) > 0.4f) continue;
      const float len = 0.1f + 0.4f * std::pow(u01(hash4(hk, 2, 0, 0)), 1.5f), r = 0.004f + 0.008f * u01(hash4(hk, 3, 0, 0)), th = 6.2831853f * u01(hash4(hk, 4, 0, 0));
      const vec2 d = 0.5f * len * vec2(std::cos(th), std::sin(th));
      const vec2 a = p - d, b = p + d;
      const vec3 A(a.x, x.t->height(a.x, a.y) + 0.4f * r, a.y), B(b.x, x.t->height(b.x, b.y) + 0.4f * r, b.y), M = 0.5f * (A + B) + vec3(0, 0.3f * r, 0);
      const float tone = 0.8f + 0.4f * u01(hash4(hk, 5, 0, 0));
      pushTube(nearM, {A, M, B}, {r, r * 0.9f, r * 0.7f}, 4, vec3(0.13f, 0.10f, 0.075f) * tone);
      if (check) for (vec3 e : {A, B}) if (std::fabs(e.y - 0.4f * r - x.t->height(e.x, e.z)) > 0.005f) fail("twig end off the ground", e, hk);
      ++gStats.twigs;
    }
  }
  // ---- exposed roots: 3-6 per model tree standing in this chunk, 0.4-2 m, emerging at the trunk and diving back in
  for (const Instance& i : x.app->doc.instances) {
    if (!inChunk(vec2(i.pos.x, i.pos.z))) continue;
    auto it = x.app->rt.find(i.id); if (it == x.app->rt.end() || !it->second.asset) continue;
    const AssetDef* a = it->second.asset; if (!(a->tree.on || a->modelFoliage) || a->tree.hedge) continue;
    const float H = a->bmax.y - a->bmin.y; if (H < 4.0f) continue;
    const uint32_t ht = hash4(sd, uint32_t(i.id), uint32_t(i.id >> 32), L_ROOT);
    const int n = 3 + int(4 * u01(ht)); const float rt = a->tree.on ? a->tree.trunkR : 0.15f + 0.012f * H;
    for (int k = 0; k < n; ++k) {
      const uint32_t hk = hash4(ht, uint32_t(k), 0, 0); if (u01(hk) >= x.mul) continue;
      const float th = 6.2831853f * (k + 0.6f * u01(hk >> 4)) / n, len = 0.4f + 1.6f * u01(hash4(hk, 1, 0, 0)), r0 = std::min(0.04f + 0.35f * rt, 0.14f);
      std::vector<vec3> pts; std::vector<float> rad;
      for (int s = 0; s <= 6; ++s) {
        const float t = s / 6.0f, bend = 0.25f * (u01(hash4(hk, 2, 0, 0)) - 0.5f) * t;
        const vec2 p = vec2(i.pos.x, i.pos.z) + (rt * 0.8f + len * t) * vec2(std::cos(th + bend), std::sin(th + bend));
        const float rr = r0 * (1.0f - 0.75f * t);
        pts.push_back(vec3(p.x, x.t->height(p.x, p.y) + rr * (0.5f - 0.9f * t), p.y)); rad.push_back(rr);   // half proud at the trunk, under by the tip
      }
      pushTube(farM, pts, rad, 6, vec3(0.12f, 0.095f, 0.075f) * (0.85f + 0.3f * u01(hash4(hk, 3, 0, 0))));
      ++gStats.roots;
    }
  }
  // ---- litter sheets: where wind and water put leaves (against walls, on lee sides, in hollows, under canopies)
  struct LV { vec3 p; vec2 uv; float seed; vec3 n; };
  std::vector<LV> lv; std::vector<uint32_t> li;
  for (float gz = 0; gz < kChunk; gz += 1.2f) for (float gx = 0; gx < kChunk; gx += 1.2f) {
    const uint32_t hk = hash4(sd, ux * 1024u + uint32_t(gx * 4), uz * 1024u + uint32_t(gz * 4), L_LITTER);
    if (u01(hk) >= x.mul) continue;
    const vec2 p = o + vec2(gx, gz) + 1.2f * vec2(u01(hk >> 3), u01(hash4(hk, 1, 0, 0)));
    float h; vec3 nrm; if (!spot(x, p, 40.0f, h, nrm, 0.05f)) continue;
    const RoadQ rq = roadLateral(p.x, p.y);
    const float rutK = rq.traffic > 0.5f ? 1.0f - ss(0.1f, 0.3f, rq.rutDist) : 0.0f;   // in a wheel rut
    const float tr = traffic(p); if (tr > 0.5f && rutK <= 0.0f) continue;
    vec2 away(0); const float dw = nearestFp(x, p, &away);
    const float wall = dw < 1.5f ? 1.0f - dw / 1.5f : 0.0f;
    const float lee = wall * sat(glm::dot(glm::normalize(away + 1e-4f), x.wind));
    float ring = 0; for (int q = 0; q < 4; ++q) { const float aq = 1.5708f * q; ring += x.t->height(p.x + 1.5f * std::cos(aq), p.y + 1.5f * std::sin(aq)); }
    const float hollow = ss(0.02f, 0.12f, ring * 0.25f - h);
    const float canopy = mossHabitat(p.x, p.y).w;
    const float W = std::max((0.55f * canopy + 0.6f * wall + 0.3f * lee + 0.35f * hollow + 0.8f * groundPaintWeight(p.x, p.y, 10)) * (1.0f - tr), 0.7f * rutK) * x.cfg.density;
    if (u01(hash4(hk, 2, 0, 0)) > W) continue;
    const float S = (rutK > 0.0f ? 0.35f : 0.5f) + 0.4f * u01(hash4(hk, 3, 0, 0)), yw = 6.2831853f * u01(hash4(hk, 4, 0, 0)), seed = u01(hash4(hk, 5, 0, 0)) * 97.0f;   // rut litter: smaller sheets
    const vec2 ax(std::cos(yw), std::sin(yw)), az(-ax.y, ax.x);
    const uint32_t b = (uint32_t)lv.size();
    for (int vz = 0; vz < 3; ++vz) for (int vx = 0; vx < 3; ++vx) {   // 3 x 3 grid snapped to the ground: no floating corners
      const vec2 uv(vx * 0.5f, vz * 0.5f); const vec2 w = p + (uv.x - 0.5f) * S * ax + (uv.y - 0.5f) * S * az;
      lv.push_back({vec3(w.x, x.t->height(w.x, w.y), w.y), uv, seed, x.t->normal(w.x, w.y)});
    }
    for (int vz = 0; vz < 2; ++vz) for (int vx = 0; vx < 2; ++vx) { const uint32_t a0 = b + vz * 3 + vx; li.insert(li.end(), {a0, a0 + 3, a0 + 1, a0 + 1, a0 + 3, a0 + 4}); }
    ++gStats.litter;
  }
  if (!li.empty()) {
    glCreateVertexArrays(1, &c.litVao); glCreateBuffers(1, &c.litVbo); glCreateBuffers(1, &c.litEbo);
    glNamedBufferStorage(c.litVbo, lv.size() * sizeof(LV), lv.data(), 0); glNamedBufferStorage(c.litEbo, li.size() * 4, li.data(), 0);
    glVertexArrayVertexBuffer(c.litVao, 0, c.litVbo, 0, sizeof(LV)); glVertexArrayElementBuffer(c.litVao, c.litEbo);
    const GLuint off[4] = {offsetof(LV, p), offsetof(LV, uv), offsetof(LV, seed), offsetof(LV, n)}; const GLint cnt[4] = {3, 2, 1, 3};
    for (int k = 0; k < 4; ++k) { glEnableVertexArrayAttrib(c.litVao, k); glVertexArrayAttribFormat(c.litVao, k, cnt[k], GL_FLOAT, GL_FALSE, off[k]); glVertexArrayAttribBinding(c.litVao, k, 0); }
    c.litCount = (int)li.size();
  }
  // ---- moss cushions where the field's G passes: candidates on a 0.3 m jittered grid (4.5: 0.2-0.45 m spacing)
  if (x.cfg.moss && x.tier >= 2) for (int gz = 0; gz < int(kChunk / 0.3f); ++gz) for (int gx = 0; gx < int(kChunk / 0.3f); ++gx) {
    const uint32_t hk = hash4(sd, ux * 4096u + uint32_t(gx), uz * 4096u + uint32_t(gz), L_CUSHION);
    if (u01(hk) >= x.mul) continue;
    const vec2 p = o + (vec2(gx, gz) + vec2(0.15f) + 0.25f * vec2(u01(hk >> 2) - 0.5f, u01(hash4(hk, 1, 0, 0)) - 0.5f)) * 0.3f;
    float h; vec3 n; if (!spot(x, p, 45.0f, h, n, 0.3f)) continue;
    const vec4 hb = mossHabitat(p.x, p.y);
    const vec3 P(p.x, h, p.y);
    const vec2 g = mossGroundCpu(P, n.y, hb, std::max(traffic(p), roadRim(p)), groundPaintWeight(p.x, p.y, 10), x.bias);   // the ground moss field (kCommon mossGround)
    const vec3 f(g.x, g.y, 0.0f);
    if (u01(hash4(hk, 2, 0, 0)) >= f.y) continue;
    // align +Y between the surface normal and up, then turn about it by a smooth noise field: neighbours lean together
    const vec3 ax = glm::normalize(glm::mix(n, vec3(0, 1, 0), 0.35f));
    const float spin = 6.2831853f * 2.0f * mossVnCpu(P * 0.5f);
    const vec3 rx = glm::cross(vec3(0, 1, 0), ax); const float rs = glm::length(rx);
    const quat qa = rs > 1e-5f ? glm::angleAxis(std::atan2(rs, ax.y), rx / rs) : quat(1, 0, 0, 0);
    const quat q = qa * glm::angleAxis(spin, vec3(0, 1, 0));
    const float wdt = 0.03f + 0.13f * std::pow(u01(hash4(hk, 3, 0, 0)), 1.6f), rel = 0.006f + 0.049f * u01(hash4(hk, 4, 0, 0)) * sat(0.4f + f.y);
    const float s = wdt / 0.065f, hs = rel / (0.042f * s), sink = (0.20f + 0.15f * u01(hash4(hk, 5, 0, 0))) * rel;
    Cushion cu; cu.a = vec4(P - ax * sink, s); cu.q = vec4(q.x, q.y, q.z, q.w);
    cu.c = vec4(hs, 0.16f * (u01(hash4(hk, 6, 0, 0)) - 0.5f), float(hash4(hk, 7, 0, 0) % 6u), 0);
    c.cush.push_back(cu); ++gStats.cushions;
  }
  if (!nearM.i.empty()) c.nearM.upload(nearM);
  if (!farM.i.empty()) c.farM.upload(farM);
  // the chunk's ground grid (identity model: asset space = world) and habitat: groundH() and uMossHab for the stones' moss
  c.contact[0] = vec4(o, o + vec2(kChunk));
  for (int z = 0; z < 4; ++z) for (int xx = 0; xx < 4; ++xx) { const vec2 w = o + vec2(xx, z) * (kChunk / 3.0f); c.contact[1 + z][xx] = x.t->height(w.x, w.y); }
  c.contact[5] = mossHabitat(o.x + 0.5f * kChunk, o.y + 0.5f * kChunk);
  c.built = true;
}

// ---- cushion meshes (content/models/bf_moss: the CC0 apple moss scan, docs/credits.md) and the GL programs
struct CushMesh { int first = 0, count = 0, base = 0; };
CushMesh gCm[6][3]; GLuint gCVao = 0, gCVbo = 0, gCEbo = 0, gCInst = 0, gCAlb = 0; size_t gCInstBytes = 0; bool gCTried = false;
GLuint gProgC = 0, gProgL = 0;
std::vector<vec4> gFrameInst; int gBucket[6][3][2]{};   // per frame: instances grouped by (variant, lod): [first, count]
std::vector<const Chunk*> gFrameLitter;
float gCushRange = 4.0f;

bool loadCushions() {
  if (gCTried) return gCVao != 0;
  gCTried = true;
  const fs::path dir = contentDir() / "models" / "bf_moss";
  std::ifstream f(dir / "moss_cushions.bin", std::ios::binary); if (!f) { logf("scatter: moss cushions missing (%s)", dir.string().c_str()); return false; }
  char mg[4]; uint32_t ver = 0, n = 0; f.read(mg, 4); f.read((char*)&ver, 4); f.read((char*)&n, 4);
  if (std::memcmp(mg, "MOSC", 4) != 0 || ver != 1 || n > 64) { logf("scatter: moss cushions: bad header"); return false; }
  std::vector<float> V; std::vector<uint32_t> I;
  for (uint32_t k = 0; k < n; ++k) {
    uint32_t vi, li, nv, ni; f.read((char*)&vi, 4); f.read((char*)&li, 4); f.read((char*)&nv, 4); f.read((char*)&ni, 4);
    if (!f || vi >= 6 || li >= 3 || nv > 100000 || ni > 300000) { logf("scatter: moss cushions: bad mesh %u", k); return false; }
    gCm[vi][li] = {int(I.size()), int(ni), int(V.size() / 8)};
    const size_t v0 = V.size(); V.resize(v0 + size_t(nv) * 8); f.read((char*)&V[v0], std::streamsize(nv) * 32);
    const size_t i0 = I.size(); I.resize(i0 + ni); f.read((char*)&I[i0], std::streamsize(ni) * 4);
    for (size_t q = i0; q < I.size(); ++q) if (I[q] >= nv) { logf("scatter: moss cushions: index out of range"); return false; }
  }
  if (!f) { logf("scatter: moss cushions truncated"); return false; }
  int w, h, ch; stbi_set_flip_vertically_on_load(false);
  stbi_uc* px = stbi_load((dir / "moss_cushion_albedo.png").string().c_str(), &w, &h, &ch, 4);
  if (!px) { logf("scatter: moss albedo missing"); return false; }
  int lv = 1; while ((std::max(w, h) >> lv) > 0) ++lv;
  glCreateTextures(GL_TEXTURE_2D, 1, &gCAlb); glTextureStorage2D(gCAlb, lv, GL_SRGB8_ALPHA8, w, h);
  glTextureSubImage2D(gCAlb, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px); glGenerateTextureMipmap(gCAlb); stbi_image_free(px);
  glTextureParameteri(gCAlb, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(gCAlb, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glCreateVertexArrays(1, &gCVao); glCreateBuffers(1, &gCVbo); glCreateBuffers(1, &gCEbo);
  glNamedBufferStorage(gCVbo, V.size() * 4, V.data(), 0); glNamedBufferStorage(gCEbo, I.size() * 4, I.data(), 0);
  glVertexArrayVertexBuffer(gCVao, 0, gCVbo, 0, 32); glVertexArrayElementBuffer(gCVao, gCEbo);
  const GLint cn[3] = {3, 3, 2}; for (int k = 0; k < 3; ++k) { glEnableVertexArrayAttrib(gCVao, k); glVertexArrayAttribFormat(gCVao, k, cn[k], GL_FLOAT, GL_FALSE, k == 0 ? 0 : k == 1 ? 12 : 24); glVertexArrayAttribBinding(gCVao, k, 0); }
  for (int k = 0; k < 3; ++k) { glEnableVertexArrayAttrib(gCVao, 3 + k); glVertexArrayAttribFormat(gCVao, 3 + k, 4, GL_FLOAT, GL_FALSE, k * 16); glVertexArrayAttribBinding(gCVao, 3 + k, 1); }
  glVertexArrayBindingDivisor(gCVao, 1, 1);
  logf("scatter: moss cushions loaded (%u meshes, %zu tris)", n, I.size() / 3);
  return true;
}

const char* kCushVS = R"GLSL(
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec2 aUV;
layout(location=3) in vec4 iA; layout(location=4) in vec4 iQ; layout(location=5) in vec4 iC;
uniform mat4 uVP;
out vec3 vW; out vec3 vN; out vec2 vUV; out float vH; flat out vec2 vHF;
vec3 qrot(vec4 q, vec3 v){ return v + 2.0*cross(q.xyz, cross(q.xyz, v) + q.w*v); }
void main(){
  float gk = uDefOn == 1 && deformAt(iA.xz).y > fract(iA.x*12.9898 + iA.z*78.233) ? 0.0 : 1.0;   // 4.4: the deformation buffer's G clears cushions (density x (1 - G))
  vec3 p = aPos*vec3(iA.w, iA.w*iC.x, iA.w)*gk;   // no wind: moss does not move (4.5)
  vW = iA.xyz + qrot(iQ, p); vN = normalize(qrot(iQ, aNrm*vec3(iC.x, 1.0, iC.x)));
  vUV = aUV; vH = aPos.y/0.042; vHF = vec2(iC.y, iC.w);
  gl_Position = uVP*vec4(vW, 1.0);
}
)GLSL";
const char* kCushFS = R"GLSL(
in vec3 vW; in vec3 vN; in vec2 vUV; in float vH; flat in vec2 vHF;
uniform sampler2D uMossAlb;
layout(location=0) out vec4 oColor; layout(location=2) out vec4 oAmb; layout(location=3) out vec4 oNR; layout(location=4) out vec4 oVel;
void main(){
  if (vHF.y > 0.0 && pixelNoiseFixed() < vHF.y) discard;   // dithered hand-over to the mat at the range edge
  vec3 alb = texture(uMossAlb, vUV).rgb;
  float Y = dot(alb, vec3(0.25, 0.5, 0.25)), Co = 0.5*(alb.r - alb.b), Cg = -0.25*alb.r + 0.5*alb.g - 0.25*alb.b;   // per-cushion hue (3.6, YCoCg)
  float ca = cos(vHF.x), sa = sin(vHF.x); vec2 cc = vec2(ca*Co - sa*Cg, sa*Co + ca*Cg);
  alb = max(vec3(Y + cc.x - cc.y, Y + cc.y, Y - cc.x - cc.y), vec3(0.0));
  float ao = mix(0.55, 1.0, clamp(vH, 0.0, 1.0));   // darker into the mat: the self-shadowing cue
  vec3 N = normalize(vN); if (!gl_FrontFacing) N = -N;
  vec3 c = shade(vW, N, alb, 0.88, 0.0, ao);
  if (uMoss.y > 0.5) c += mossTransmit(N, uSunDir, normalize(uCamPos - vW), 0.8)*uSunCol*sunShadowT(vW, -N, 1.0, 1);
  c = applyFog(c, vW);
  oColor = vec4(c, 1.0); oAmb = vec4(gInd*gFogT, 1.0); oNR = vec4(N, 0.88 + (1.0 - gFogT)); oVel = vec4(0.0);
}
)GLSL";
const char* kLitVS = R"GLSL(
layout(location=0) in vec3 aPos; layout(location=1) in vec2 aUV; layout(location=2) in float aSeed; layout(location=3) in vec3 aNrm;
uniform mat4 uVP; uniform float uRange;
out vec3 vW; out vec2 vUV; flat out float vSeed; out vec3 vN; out float vFade;
void main(){
  float d = length(aPos - uCamPos);
  vW = aPos + aNrm*(0.004 + 0.0008*d);   // ground decal offset (3.5): thicker than a wall's, the ground is seen at grazing angles
  vUV = aUV; vSeed = aSeed; vN = aNrm; vFade = smoothstep(uRange*0.8, uRange, d);
  gl_Position = uVP*vec4(vW, 1.0);
}
)GLSL";
const char* kLitFS = R"GLSL(
in vec3 vW; in vec2 vUV; flat in float vSeed; in vec3 vN; in float vFade;
layout(location=0) out vec4 oColor; layout(location=2) out vec4 oAmb; layout(location=3) out vec4 oNR; layout(location=4) out vec4 oVel;
float h21(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7)))*43758.5453); }
void main(){
  if (vFade > 0.0 && pixelNoiseFixed() < vFade) discard;
  // a sheet of fallen leaves: 7 x 7 cells, one leaf per cell (rotated, 3-10 cm), thinning to a ragged edge; the topmost wins
  vec2 s = vUV*7.0, cell = floor(s); float best = -1.0; vec3 alb = vec3(0.0); float vein = 0.0; vec2 tilt = vec2(0.0);
  for (int j = -1; j <= 1; ++j) for (int i = -1; i <= 1; ++i) {
    vec2 c = cell + vec2(i, j);
    float h1 = h21(c + vSeed*1.37), h2 = h21(c*1.71 + vSeed), h3 = h21(c*2.33 + 3.1 + vSeed), h4 = h21(c*3.7 + 9.2 + vSeed);
    float r = length((c + 0.5)/7.0 - 0.5)*2.0;
    if (h3 > 0.92*(1.0 - smoothstep(0.45, 1.05, r))) continue;
    vec2 ctr = c + vec2(h1, h2); float ang = 6.2832*h4; vec2 dir = vec2(cos(ang), sin(ang));
    float L = 0.55 + 0.6*h1;
    vec2 d = s - ctr; float u = dot(d, dir)/L + 0.5, v = dot(d, vec2(-dir.y, dir.x))/L;
    if (u < 0.0 || u > 1.0 || abs(v) > 0.3*pow(sin(3.14159*u), 0.85)) continue;
    if (h4 + h1 <= best) continue;
    best = h4 + h1; vein = 1.0 - smoothstep(0.0, 0.025, abs(v));
    float k = h2;   // autumn browns, ochres, old grey-browns, dark wet ones
    alb = k < 0.35 ? vec3(0.13, 0.07, 0.03) : k < 0.6 ? vec3(0.28, 0.16, 0.05) : k < 0.85 ? vec3(0.12, 0.10, 0.075) : vec3(0.06, 0.045, 0.03);
    alb *= 0.8 + 0.4*h3; tilt = (vec2(h1, h3) - 0.5)*0.5;
  }
  if (best < 0.0) discard;
  if (uDefOn == 1 && deformAt(vW.xz).y > fract(best*7.31)) discard;   // 4.4: disturbance clears litter (per leaf)
  alb *= 1.0 - 0.3*vein;
  vec3 N = normalize(vN + vec3(tilt.x, 0.0, tilt.y));
  vec3 c = shade(vW, N, alb, 0.78, 0.0, 0.85);
  c = applyFog(c, vW);
  oColor = vec4(c, 1.0); oAmb = vec4(gInd*gFogT, 1.0); oNR = vec4(N, 0.78 + (1.0 - gFogT)); oVel = vec4(0.0);
}
)GLSL";

uint64_t mix64(uint64_t h, uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); return h; }
}   // namespace

const ScatterStats& scatterStats() { return gStats; }

void scatterFrame(App& app, std::vector<DrawItem>& op, vec3 eye) {
  gFrameInst.clear(); gFrameLitter.clear(); std::memset(gBucket, 0, sizeof gBucket);
  Renderer& R = app.R;
  const MapCfg cfg = readCfg(app);
  static const void* lastDoc = nullptr; static uint32_t lastSeed = 0;
  if (!R.perf.scatter || cfg.density <= 0 || R.q.scatterMul <= 0) {
    if (!gChunks.empty()) { for (auto& [k, c] : gChunks) c.release(); gChunks.clear(); }
    return;
  }
  if (lastDoc != &app.doc || lastSeed != cfg.seed) { for (auto& [k, c] : gChunks) c.release(); gChunks.clear(); lastDoc = &app.doc; lastSeed = cfg.seed; }
  static std::vector<GrassFootprint> fp; static uint64_t fpSig = 0;
  uint64_t is = app.doc.instances.size(); for (const Instance& i : app.doc.instances) is = mix64(is, i.id * 31 + i.rev);
  const uint64_t fs = mix64(is, app.groundRev);
  if (fs != fpSig) { fp = app.grassFootprints(); fpSig = fs; }
  const uint64_t sig = mix64(mix64(mix64(mix64(fs, lassoGroundRev()), mossHabitatRev()), uint64_t(R.q.scatterMul * 1000) | uint64_t(R.q.mossTier) << 16), uint64_t(cfg.density * 1000) | uint64_t(cfg.moss) << 20 | uint64_t((R.lt.mapMossBias + 1.0f) * 1000) << 24);
  Ctx x{&app, &app.terrain, cfg, R.q.scatterMul, R.q.mossTier, R.windDir2(), &fp, R.lt.mapMossBias};
  // chunks within the far-stone range: build the nearest stale ones (2 a frame), drop the far ones
  static uint64_t frameNo = 0; const uint64_t now = ++frameNo;
  const int cr = int(std::ceil(kFarStones / kChunk)) + 1, ecx = int(std::floor(eye.x / kChunk)), ecz = int(std::floor(eye.z / kChunk));
  std::vector<std::pair<float, Chunk*>> want;
  for (int dz = -cr; dz <= cr; ++dz) for (int dx = -cr; dx <= cr; ++dx) {
    const int cx = ecx + dx, cz = ecz + dz; const vec2 lo(cx * kChunk, cz * kChunk);
    const float d = glm::length(glm::max(glm::max(lo - vec2(eye.x, eye.z), vec2(eye.x, eye.z) - (lo + vec2(kChunk))), vec2(0)));
    if (d > kFarStones) continue;
    Chunk& c = gChunks[ckey(cx, cz)]; c.cx = cx; c.cz = cz; c.used = now;
    if (!c.built || c.sig != sig) want.push_back({d, &c});
  }
  std::sort(want.begin(), want.end(), [](auto& a, auto& b) { return a.first < b.first; });
  for (size_t k = 0; k < want.size() && k < 2; ++k) { buildChunk(x, *want[k].second); want[k].second->sig = sig; }
  for (auto it = gChunks.begin(); it != gChunks.end();) { if (now - it->second.used > 600) { it->second.release(); it = gChunks.erase(it); } else ++it; }
  // draws, contacts and this frame's cushions / litter
  gCushRange = R.q.mossTier >= 4 ? 4.5f : R.q.mossTier == 3 ? 4.0f : 2.0f;   // 8.2: Medium limited, High near, Ultra extra
  const int nVar = R.q.mossTier >= 4 ? 6 : R.q.mossTier == 3 ? 4 : 3;
  std::vector<vec4> inst[6][3];
  gStats.chunks = 0; gStats.cushionsDrawn = 0; int contacts = 0;
  for (auto& [k, c] : gChunks) {
    if (!c.built || c.used != now) continue;
    ++gStats.chunks;
    const vec2 lo(c.cx * kChunk, c.cz * kChunk);
    const float d = glm::length(glm::max(glm::max(lo - vec2(eye.x, eye.z), vec2(eye.x, eye.z) - (lo + vec2(kChunk))), vec2(0)));
    if (c.farM.count && d < kFarStones) { DrawItem it{&c.farM, mat4(1)}; it.castR = 25.0f; it.contact = c.contact; op.push_back(it); }
    if (c.nearM.count && d < kNearStones) { DrawItem it{&c.nearM, mat4(1)}; it.castR = 12.0f; it.contact = c.contact; op.push_back(it); }
    if (R.perf.contactAO && d < 25.0f) for (size_t q = 0; q + 2 < c.contacts.size() && contacts < 2000; q += 3) {
      const vec3 b = vec3(c.contacts[q]); if (glm::dot(b - eye, b - eye) > 625.0f) continue;
      R.contactAO.push_back(c.contacts[q]); R.contactAO.push_back(c.contacts[q + 1]); R.contactAO.push_back(c.contacts[q + 2]); ++contacts;
    }
    if (c.litCount && d < R.q.litterM) gFrameLitter.push_back(&c);
    if (!c.cush.empty() && d < gCushRange)
      for (const Cushion& cu : c.cush) {
        const float e = glm::length(vec3(cu.a) - eye); if (e > gCushRange) continue;
        const int v = int(cu.c.z) % nVar, l = e < 1.0f ? 0 : e < 2.0f ? 1 : 2;
        Cushion o = cu; o.c.w = ss(gCushRange - 0.8f, gCushRange, e);
        inst[v][l].push_back(o.a); inst[v][l].push_back(o.q); inst[v][l].push_back(o.c);
      }
  }
  for (int v = 0; v < 6; ++v) for (int l = 0; l < 3; ++l) {
    gBucket[v][l][0] = int(gFrameInst.size() / 3); gBucket[v][l][1] = int(inst[v][l].size() / 3);
    gFrameInst.insert(gFrameInst.end(), inst[v][l].begin(), inst[v][l].end()); gStats.cushionsDrawn += gBucket[v][l][1];
  }
}

void scatterRenderPass(Renderer& r, const RenderView& v, const mat4& vp) {
  if (gFrameInst.empty() && gFrameLitter.empty()) return;
  if (!gProgC) { gProgC = r.compile(kCushVS, kCushFS, "bf_cushion"); gProgL = r.compile(kLitVS, kLitFS, "bf_litter");
    for (GLuint* p : {&gProgC, &gProgL}) { GLint ok = 0; if (*p) glGetProgramiv(*p, GL_LINK_STATUS, &ok); if (!ok) *p = 0; } }
  glDisable(GL_CULL_FACE);
  if (gProgL && !gFrameLitter.empty()) {
    r.setLighting(gProgL, v);
    glUniformMatrix4fv(glGetUniformLocation(gProgL, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    glUniform1f(glGetUniformLocation(gProgL, "uRange"), r.q.litterM);
    for (const Chunk* c : gFrameLitter) { glBindVertexArray(c->litVao); glDrawElements(GL_TRIANGLES, c->litCount, GL_UNSIGNED_INT, nullptr); r.stats.draws++; }
  }
  if (gProgC && !gFrameInst.empty() && loadCushions()) {
    const size_t bytes = gFrameInst.size() * sizeof(vec4);
    if (bytes > gCInstBytes) { if (gCInst) glDeleteBuffers(1, &gCInst); glCreateBuffers(1, &gCInst); gCInstBytes = std::max(bytes * 2, size_t(48 * 1024)); glNamedBufferStorage(gCInst, gCInstBytes, nullptr, GL_DYNAMIC_STORAGE_BIT); glVertexArrayVertexBuffer(gCVao, 1, gCInst, 0, 48); }
    glNamedBufferSubData(gCInst, 0, bytes, gFrameInst.data());
    r.setLighting(gProgC, v);
    glUniformMatrix4fv(glGetUniformLocation(gProgC, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    glBindTextureUnit(2, gCAlb); glUniform1i(glGetUniformLocation(gProgC, "uMossAlb"), 2);
    glBindVertexArray(gCVao);
    for (int vv = 0; vv < 6; ++vv) for (int l = 0; l < 3; ++l) {
      const int n = gBucket[vv][l][1]; const CushMesh& m = gCm[vv][l]; if (!n || !m.count) continue;
      glDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES, m.count, GL_UNSIGNED_INT, (void*)(uintptr_t)(m.first * 4u), n, m.base, GLuint(gBucket[vv][l][0]));
      r.stats.draws++; r.stats.tris += m.count / 3 * n;
    }
  }
  glBindVertexArray(0);
  glEnable(GL_CULL_FACE);
}
