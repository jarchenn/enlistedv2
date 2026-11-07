// Natural ground dressing: boulders / outcrops (procedural hulls, pattern 29), scatter patches (flowers, tall grass,
// stones). See nature.h for the asset contract and docs/nature.md for the design notes.
#include "nature.h"
#include "app.h"
#include "physics.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace {
inline uint64_t mix64(uint64_t z) {
  z += 0x9E3779B97F4A7C15ull;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
inline uint64_t hcomb(uint64_t h, uint64_t v) { return mix64(h ^ (v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2))); }
inline uint32_t fbits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
uint64_t fnv(const std::string& s) { uint64_t h = 1469598103934665603ull; for (unsigned char c : s) h = (h ^ c) * 1099511628211ull; return h; }
struct Rng { uint64_t s; float f() { s = mix64(s); return (float)((s >> 40) & 0xFFFFFF) / 16777216.0f; } };
float hgrid3(int x, int y, int z, uint32_t s) {
  uint64_t k = mix64(((uint64_t)(uint32_t)x * 73856093ull) ^ ((uint64_t)(uint32_t)y * 19349663ull << 20) ^ ((uint64_t)(uint32_t)z * 83492791ull << 40) ^ ((uint64_t)s << 7));
  return (float)((k >> 40) & 0xFFFFFF) / 16777216.0f;
}
float vn3(vec3 p, uint32_t s) {   // value noise 0..1 (CPU, deterministic)
  vec3 i = glm::floor(p), f = p - i; f = f * f * (3.0f - 2.0f * f);
  int x = (int)i.x, y = (int)i.y, z = (int)i.z;
  auto L = [&](int a, int b, int c) { return hgrid3(x + a, y + b, z + c, s); };
  return glm::mix(glm::mix(glm::mix(L(0, 0, 0), L(1, 0, 0), f.x), glm::mix(L(0, 1, 0), L(1, 1, 0), f.x), f.y),
                  glm::mix(glm::mix(L(0, 0, 1), L(1, 0, 1), f.x), glm::mix(L(0, 1, 1), L(1, 1, 1), f.x), f.y), f.z);
}
float fbm3s(vec3 p, uint32_t s) { float a = 0.5f, t = 0; for (int k = 0; k < 4; ++k) { t += a * vn3(p, s + k * 101); p *= 2.03f; a *= 0.5f; } return t / 0.9375f; }
float vn2(vec2 p, uint32_t s) { return vn3(vec3(p.x, 0.37f, p.y), s); }
float smooth01(float a, float b, float x) { float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3 - 2 * t); }

void icosphere(int det, std::vector<vec3>& V, std::vector<glm::uvec3>& F) {
  const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
  V = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
  for (auto& v : V) v = glm::normalize(v);
  F = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
       {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
  for (int d = 0; d < det; ++d) {
    std::map<uint64_t, uint32_t> mid; std::vector<glm::uvec3> nf;
    auto M = [&](uint32_t a, uint32_t b) {
      uint64_t k = a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
      auto it = mid.find(k); if (it != mid.end()) return it->second;
      V.push_back(glm::normalize(V[a] + V[b])); return mid[k] = (uint32_t)V.size() - 1;
    };
    for (auto& f : F) { uint32_t a = M(f.x, f.y), b = M(f.y, f.z), c = M(f.z, f.x); nf.push_back({f.x, a, c}); nf.push_back({f.y, b, a}); nf.push_back({f.z, c, b}); nf.push_back({a, b, c}); }
    F.swap(nf);
  }
}
int lithFrom(const json& v) {
  if (v.is_number()) return std::clamp(v.get<int>(), -1, 2);
  std::string s = v.is_string() ? v.get<std::string>() : "limestone";
  return s == "granite" ? 0 : s == "schist" ? 2 : s == "mixed" ? -1 : 1;
}
vec3 lithTint(int lith, float r) {   // vertex albedo multiplies the pattern's own palette (~1 = as the cliffs)
  if (lith == 0) return vec3(1.0f, 0.98f, 0.97f) * (0.92f + 0.16f * r);
  if (lith == 2) return vec3(0.97f, 0.98f, 1.0f) * (0.9f + 0.18f * r);
  return vec3(1.0f, 0.99f, 0.96f) * (0.92f + 0.16f * r);
}
}  // namespace

int natSpeciesFrom(const std::string& s) {
  static const std::map<std::string, int> m = {{"tallgrass", NS_TALLMEADOW}, {"poppy", NS_POPPY}, {"daisy", NS_DAISY}, {"buttercup", NS_BUTTERCUP},
    {"cornflower", NS_CORNFLOWER}, {"cowparsley", NS_COWPARSLEY}, {"foxglove", NS_FOXGLOVE}, {"clover", NS_CLOVER}};
  auto it = m.find(s); return it == m.end() ? -1 : it->second;
}

// ---------------------------------------------------------------- rocks
// Noise-displaced icosphere, then plane cuts (fracture facets: rounded granite, blocky limestone, slabby schist), then
// the lower part flattened into a base that sits `sink` x height below the ground. Unindexed, half-flat normals.
void buildRockPiece(MeshData& m, const RockPiece& rp, vec3 tint, std::vector<vec3>* hull, std::vector<vec3>* tris) {
  std::vector<vec3> V; std::vector<glm::uvec3> F;
  icosphere(std::clamp(rp.detail, 0, 3), V, F);
  Rng r{mix64(rp.seed * 0x9E3779B1ull + (uint64_t)rp.lith * 7919ull)};
  int lith = std::clamp(rp.lith, 0, 2);
  float amp = lith == 0 ? 0.13f : lith == 1 ? 0.09f : 0.07f, freq = lith == 0 ? 1.3f : lith == 1 ? 1.7f : 2.3f;
  vec3 he = glm::max(rp.size * 0.5f, vec3(0.01f));
  uint32_t ns = (uint32_t)rp.seed * 31u + 5u;
  vec3 off(r.f() * 50, r.f() * 50, r.f() * 50);
  std::vector<vec3> P(V.size());
  for (size_t k = 0; k < V.size(); ++k) {
    vec3 d = V[k];
    float n = fbm3s(d * freq + off, ns), lump = vn3(d * 0.8f + off * 0.5f, ns + 9);
    float rr = 1.0f + amp * (n - 0.5f) * 2.2f + amp * 0.8f * (lump - 0.5f);
    P[k] = d * rr * he;
  }
  // fracture planes (support distance of the ellipsoid along n, times a fraction)
  struct Pl { vec3 n; float d; };
  std::vector<Pl> pl;
  auto support = [&](vec3 n) { return glm::length(he * n); };
  auto addPl = [&](vec3 n, float lo, float hi) { n = glm::normalize(n); pl.push_back({n, support(n) * (lo + (hi - lo) * r.f())}); };
  auto rdir = [&]() { float z = r.f() * 2 - 1, a = r.f() * 6.2831853f, s = std::sqrt(1 - z * z); return vec3(s * std::cos(a), z, s * std::sin(a)); };
  if (lith == 0) { for (int k = 0; k < 5; ++k) addPl(rdir(), 0.80f, 0.95f); }
  else if (lith == 1) {
    addPl(vec3((r.f() - 0.5f) * 0.25f, 1, (r.f() - 0.5f) * 0.25f), 0.62f, 0.8f);                      // bedding: flat top
    for (int k = 0; k < 4; ++k) { float a = r.f() * 6.2831853f; addPl(vec3(std::cos(a), (r.f() - 0.5f) * 0.3f, std::sin(a)), 0.62f, 0.85f); }   // joints: steep faces
    for (int k = 0; k < 4; ++k) addPl(rdir(), 0.7f, 0.9f);
  } else {
    float a = r.f() * 6.2831853f, tl = 0.25f + 0.35f * r.f();
    vec3 bed(std::cos(a) * tl, 1, std::sin(a) * tl);
    addPl(bed, 0.5f, 0.66f); addPl(-bed, 0.62f, 0.78f);                                                // slab: two parallel beds
    for (int k = 0; k < 5; ++k) addPl(rdir(), 0.62f, 0.88f);
  }
  for (int it = 0; it < 2; ++it)
    for (auto& p : P) for (auto& q : pl) { float s = glm::dot(p, q.n) - q.d; if (s > 0) p -= q.n * s; }
  // flat base: the lower ~22 % becomes one plane, sunk below the ground
  float y0 = 1e9f, y1 = -1e9f; for (auto& p : P) { y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
  float yb = y0 + 0.22f * (y1 - y0);
  for (auto& p : P) p.y = std::max(p.y, yb);
  float lift = -yb - rp.sink * (y1 - yb);
  mat4 X = glm::translate(mat4(1), rp.pos) * glm::mat4_cast(yawQuat(rp.yaw) * glm::angleAxis(radians(rp.tiltX), vec3(1, 0, 0)) * glm::angleAxis(radians(rp.tiltZ), vec3(0, 0, 1)));
  for (auto& p : P) { p.y += lift; p = vec3(X * vec4(p, 1)); }
  if (hull) hull->insert(hull->end(), P.begin(), P.end());
  // normals: smooth (area-weighted) blended with the face normal, keeps facets readable without hard aliasing
  vec3 ctr(0); for (auto& p : P) ctr += p; ctr /= (float)P.size();
  std::vector<vec3> vn(P.size(), vec3(0));
  for (auto& f : F) {
    vec3 c = glm::cross(P[f.y] - P[f.x], P[f.z] - P[f.x]);
    if (glm::dot(c, (P[f.x] + P[f.y] + P[f.z]) / 3.0f - ctr) < 0) std::swap(f.y, f.z), c = -c;
    vn[f.x] += c; vn[f.y] += c; vn[f.z] += c;
  }
  uint32_t base = (uint32_t)m.v.size();
  for (auto& f : F) {
    vec3 a = P[f.x], b = P[f.y], c = P[f.z];
    vec3 cr = glm::cross(b - a, c - a);
    float L = glm::length(cr);
    vec3 fn = L > 1e-10f ? cr / L : vec3(0, -1, 0);
    uint32_t idx[3] = {f.x, f.y, f.z};
    for (int k = 0; k < 3; ++k) {
      vec3 p = P[idx[k]];
      vec3 sn = glm::length(vn[idx[k]]) > 1e-12f ? glm::normalize(vn[idx[k]]) : fn;
      vec3 nn = glm::normalize(glm::mix(fn, sn, 0.55f));
      float var = vn3(p * 0.9f, ns + 77);
      Vertex v{p, nn, tint * (0.94f + 0.12f * var), vec4((float)PAT_ROCK, 0.88f, 0.0f, (float)lith), p, nn};
      m.v.push_back(v); m.i.push_back(base++);
      if (tris) tris->push_back(p);
    }
  }
}

// ---------------------------------------------------------------- scatter layout
NatPatchParams naturePatchParams(const Instance& i, const AssetDef& a) {
  const NatureDef& n = a.nature;
  NatPatchParams p; p.radius = n.radius; p.density = n.density; p.seed = 1;
  if (i.extra.is_object() && i.extra.contains("scatter") && i.extra["scatter"].is_object()) {
    auto& s = i.extra["scatter"];
    p.radius = s.value("radius", p.radius); p.density = s.value("density", p.density); p.seed = s.value("seed", p.seed);
  }
  p.radius = std::clamp(p.radius, n.minR, n.maxR); p.density = std::clamp(p.density, n.minD, n.maxD);
  return p;
}
void setNaturePatchParams(Instance& i, const AssetDef& a, NatPatchParams p) {
  const NatureDef& n = a.nature;
  if (!i.extra.is_object()) i.extra = json::object();
  i.extra["scatter"] = {{"radius", std::clamp(p.radius, n.minR, n.maxR)}, {"density", std::clamp(p.density, n.minD, n.maxD)}, {"seed", p.seed}};
  i.rev++;
}

void natureLayout(const NatureDef& n, const std::string& assetId, const NatPatchParams& pp, std::vector<NatScatterPt>& out) {
  out.clear();
  bool stones = n.scatter == NatureDef::STONES;
  float R = std::max(0.1f, pp.radius), D = std::max(0.01f, pp.density);
  int cap = stones ? 2500 : 7000;
  int target = std::clamp((int)std::lround(D * 3.14159265f * R * R), 1, cap);
  float spacing = 1.0f / std::sqrt(D), dmin = spacing * (stones ? 0.42f : 0.38f);
  float cell = std::max(dmin, stones ? n.sizeMax : dmin);
  uint64_t seed = hcomb(hcomb(fnv(assetId), pp.seed), ((uint64_t)fbits(R) << 32) | fbits(D));
  Rng rng{seed};
  uint32_t ns = (uint32_t)(seed >> 7);
  std::unordered_map<int64_t, std::vector<int>> grid;
  auto key = [&](int x, int z) { return ((int64_t)x << 32) ^ (uint32_t)z; };
  float wsum = 0; for (auto& m : n.mix) wsum += m.second;
  int attempts = target * 30;
  for (int a = 0; a < attempts && (int)out.size() < target; ++a) {
    float u = rng.f(), w = rng.f(), rs = rng.f(), rc = rng.f();
    NatScatterPt q; q.r0 = rng.f(); q.r1 = rng.f(); q.r2 = rng.f(); q.r3 = rng.f(); float rsp = rng.f();
    float rr = R * std::sqrt(u), th = w * 6.2831853f;
    q.p = vec2(rr * std::cos(th), rr * std::sin(th));
    if (rr > 0.7f * R && rs < smooth01(0.7f * R, R, rr)) continue;                       // soft, ragged rim
    float cn = vn2(q.p * (stones ? 0.55f : 0.42f), ns + 3) * 0.75f + vn2(q.p * 1.7f, ns + 4) * 0.25f;
    if (rc > glm::mix(1.0f, smooth01(0.28f, 0.72f, cn), std::clamp(n.clump, 0.0f, 1.0f))) continue;   // clumps and gaps
    if (stones) q.size = n.sizeMin * std::pow(n.sizeMax / std::max(1e-3f, n.sizeMin), std::pow(q.r1, std::max(0.3f, n.sizePow)));
    int gx = (int)std::floor(q.p.x / cell), gz = (int)std::floor(q.p.y / cell);
    bool clash = false;
    int reach = stones ? 2 : 1;
    for (int dz = -reach; dz <= reach && !clash; ++dz) for (int dx = -reach; dx <= reach && !clash; ++dx) {
      auto it = grid.find(key(gx + dx, gz + dz)); if (it == grid.end()) continue;
      for (int k : it->second) {
        float need = stones ? std::max(dmin * 0.7f, 0.5f * (q.size + out[k].size) * 0.9f) : dmin;
        if (glm::dot(q.p - out[k].p, q.p - out[k].p) < need * need) { clash = true; break; }
      }
    }
    if (clash) continue;
    // species: each one grows in its own drifts (noise per species), so a mixed meadow forms groups, not confetti
    if (!n.mix.empty()) {
      float sc[16]; float tot = 0;
      for (size_t k = 0; k < n.mix.size() && k < 16; ++k) { sc[k] = n.mix[k].second / std::max(1e-6f, wsum) * (0.08f + std::pow(vn2(q.p * 0.33f, ns + 17 * (uint32_t)(k + 1)), 2.0f)); tot += sc[k]; }
      float pick = rsp * tot; q.species = n.mix.back().first;
      for (size_t k = 0; k < n.mix.size() && k < 16; ++k) { if (pick < sc[k]) { q.species = n.mix[k].first; break; } pick -= sc[k]; }
    }
    grid[key(gx, gz)].push_back((int)out.size());
    out.push_back(q);
  }
}
uint64_t natureLayoutHash(const std::vector<NatScatterPt>& pts) {
  uint64_t h = 1469598103934665603ull;
  for (auto& p : pts) { h = hcomb(h, ((uint64_t)fbits(p.p.x) << 32) | fbits(p.p.y)); h = hcomb(h, ((uint64_t)fbits(p.size) << 32) | (uint32_t)p.species); }
  return h ^ pts.size();
}

// ---------------------------------------------------------------- plant / stone tables
namespace {
struct SpeciesDef { float h0, h1, w0, w1; vec3 green; };
const SpeciesDef& spDef(int sp) {
  static const SpeciesDef S[8] = {
    {0.60f, 1.20f, 0.0050f, 0.0060f, {0.22f, 0.25f, 0.11f}},   // 5 tall meadow grass (stem width)
    {0.35f, 0.65f, 0.034f, 0.046f, {0.17f, 0.24f, 0.08f}},     // 6 poppy (head radius)
    {0.30f, 0.60f, 0.024f, 0.034f, {0.16f, 0.23f, 0.08f}},     // 7 ox-eye daisy
    {0.20f, 0.50f, 0.011f, 0.015f, {0.15f, 0.24f, 0.07f}},     // 8 buttercup
    {0.30f, 0.60f, 0.017f, 0.024f, {0.19f, 0.25f, 0.13f}},     // 9 cornflower (grey-green)
    {0.70f, 1.20f, 0.050f, 0.080f, {0.17f, 0.25f, 0.09f}},     // 10 cow parsley (umbel radius)
    {0.80f, 1.40f, 0.024f, 0.032f, {0.14f, 0.21f, 0.08f}},     // 11 foxglove (bell spike half width)
    {0.08f, 0.18f, 0.011f, 0.014f, {0.10f, 0.18f, 0.06f}},     // 12 clover (head radius)
  };
  return S[std::clamp(sp, 5, 12) - 5];
}
float packPlant(vec3 alb, int sp) {
  auto q = [](float v) { return (uint32_t)std::clamp((int)std::lround(v * 2.0f * 255.0f), 0, 255); };
  uint32_t u = q(alb.r) | (q(alb.g) << 8) | (q(alb.b) << 16) | ((0x38u + (uint32_t)std::clamp(sp, 0, 15)) << 24);
  float f; std::memcpy(&f, &u, 4); return f;
}
const std::vector<MeshData>& stoneBases() {   // eight unit stone shapes (80 triangles each), built once
  static std::vector<MeshData> B;
  if (B.empty()) for (int k = 0; k < 8; ++k) {
    RockPiece rp; rp.lith = k % 3 == 0 ? 0 : 1 + (k & 1); rp.seed = 9001 + k * 37; rp.detail = 1; rp.sink = 0.25f;
    rp.size = vec3(1.0f, 0.45f + 0.08f * (k % 4), 0.7f + 0.07f * (k % 3));
    MeshData m; buildRockPiece(m, rp, vec3(1), nullptr); B.push_back(std::move(m));
  }
  return B;
}
void appendStone(MeshData& out, int variant, const mat4& M, vec3 tint, int lith) {
  const MeshData& b = stoneBases()[variant & 7];
  glm::mat3 Nm = glm::transpose(glm::inverse(glm::mat3(M)));
  uint32_t base = (uint32_t)out.v.size();
  for (auto v : b.v) {
    v.pos = vec3(M * vec4(v.pos, 1)); v.nrm = glm::normalize(Nm * v.nrm);
    v.albedo = tint * v.albedo; v.params.w = (float)lith; v.pcoord = v.pos; v.pnrm = v.nrm;
    out.v.push_back(v);
  }
  for (uint32_t i : b.i) out.i.push_back(base + i);
}
vec3 headColour(int sp) {
  switch (sp) { case NS_POPPY: return {0.62f, 0.05f, 0.03f}; case NS_DAISY: return {0.85f, 0.85f, 0.80f}; case NS_BUTTERCUP: return {0.85f, 0.66f, 0.03f};
    case NS_CORNFLOWER: return {0.14f, 0.27f, 0.72f}; case NS_COWPARSLEY: return {0.86f, 0.86f, 0.80f}; case NS_FOXGLOVE: return {0.62f, 0.24f, 0.50f};
    case NS_CLOVER: return {0.62f, 0.28f, 0.44f}; default: return {0.42f, 0.37f, 0.24f}; }
}
// catalogue mesh (thumbnail, placement ghost): plants as stems + coloured heads, stones as on flat ground
void previewMesh(AssetDef& a, MeshData& m) {
  const NatureDef& n = a.nature;
  NatPatchParams pp; pp.radius = n.radius; pp.density = n.density;
  std::vector<NatScatterPt> pts; natureLayout(n, a.id, pp, pts);
  if (n.scatter == NatureDef::STONES) {
    for (auto& q : pts) {
      mat4 M = glm::translate(mat4(1), vec3(q.p.x, 0, q.p.y)) * glm::mat4_cast(yawQuat(q.r0 * 360.0f)) * glm::scale(mat4(1), vec3(q.size));
      appendStone(m, (int)(q.r2 * 8), M, lithTint(n.lith < 0 ? 1 + (q.r3 < 0.5f) : n.lith, q.r2), n.lith < 0 ? 1 + (q.r3 < 0.5f) : n.lith);
    }
    return;
  }
  Material stem{"stem", vec3(0.16f, 0.24f, 0.08f), 0.9f, 0.0f, PAT_PLAIN, 1};
  size_t lim = std::min<size_t>(pts.size(), 220);
  for (size_t k = 0; k < lim; ++k) {
    auto& q = pts[k]; int sp = q.species ? q.species : NS_TALLMEADOW;
    const SpeciesDef& d = spDef(sp);
    float h = glm::mix(d.h0, d.h1, q.r1) * glm::mix(n.hMin, n.hMax, q.r2);
    Part s; s.center = vec3(q.p.x, h * 0.5f, q.p.y); s.size = vec3(0.02f, h, 0.02f); s.collide = false;
    stem.albedo = d.green; appendPart(m, s, stem, mat4(1));
    Material hm{"head", headColour(sp), 0.8f, 0.0f, PAT_PLAIN, 1};
    float hs = sp == NS_TALLMEADOW ? 0.04f : sp == NS_FOXGLOVE ? 0.07f : std::max(0.05f, d.w1 * 2.2f);
    Part hd; hd.center = vec3(q.p.x, h - (sp == NS_FOXGLOVE ? h * 0.2f : 0.0f), q.p.y); hd.size = vec3(hs, sp == NS_TALLMEADOW ? 0.14f : sp == NS_FOXGLOVE ? h * 0.4f : hs * 0.5f, hs); hd.collide = false;
    appendPart(m, hd, hm, mat4(1));
  }
}
}  // namespace

void buildNatureAsset(AssetDef& a, const json& j, std::vector<vec3>& pts) {
  NatureDef& n = a.nature;
  if (j.contains("rock")) {
    auto& rj = j["rock"]; n.kind = NatureDef::ROCK; n.collide = rj.value("collide", true);
    MeshData m; std::vector<std::vector<vec3>> hulls;
    int k = 0;
    for (auto& pj : rj.value("pieces", json::array())) {
      RockPiece rp; rp.lith = std::max(0, lithFrom(pj.value("lith", json("limestone")))); rp.seed = pj.value("seed", 1u + k);
      rp.size = jvec3(pj["size"], vec3(1)); rp.pos = jvec3(pj["pos"]); rp.yaw = pj.value("yaw", 0.0f);
      if (pj.contains("tilt")) { rp.tiltX = pj["tilt"][0].get<float>(); rp.tiltZ = pj["tilt"][1].get<float>(); }
      rp.sink = pj.value("sink", 0.25f);
      float big = std::max(rp.size.x, std::max(rp.size.y, rp.size.z));
      rp.detail = pj.value("detail", big > 1.4f ? 3 : 2);
      n.pieces.push_back(rp);
      std::vector<vec3> hp;
      buildRockPiece(m, rp, lithTint(rp.lith, Rng{mix64(rp.seed)}.f()), &hp, &n.tris);
      pts.insert(pts.end(), hp.begin(), hp.end());
      hulls.push_back(std::move(hp)); ++k;
    }
    a.opaque.destroy(); a.opaque.upload(m);
    if (n.collide) a.shape = Physics::buildHulls(hulls);
  }
  if (j.contains("scatter")) {
    auto& sj = j["scatter"]; n.kind = NatureDef::SCATTER;
    std::string k = sj.value("kind", std::string("flowers"));
    n.scatter = k == "stones" ? NatureDef::STONES : k == "tallgrass" ? NatureDef::TALLGRASS : NatureDef::FLOWERS;
    n.radius = sj.value("radius", 3.0f); n.density = sj.value("density", 4.0f);
    n.minR = sj.value("minRadius", 0.5f); n.maxR = sj.value("maxRadius", 15.0f); n.minD = sj.value("minDensity", 0.2f); n.maxD = sj.value("maxDensity", 40.0f);
    n.clump = sj.value("clump", 0.5f);
    if (sj.contains("size")) { n.sizeMin = sj["size"][0].get<float>(); n.sizeMax = sj["size"][1].get<float>(); }
    n.sizePow = sj.value("sizePow", 2.2f); n.lith = lithFrom(sj.value("lith", json("limestone")));
    if (sj.contains("heightScale")) { n.hMin = sj["heightScale"][0].get<float>(); n.hMax = sj["heightScale"][1].get<float>(); }
    json mix = sj.value("mix", json::object());
    for (auto& [name, w] : mix.items()) { int sp = natSpeciesFrom(name); if (sp >= 0) n.mix.push_back({sp, w.get<float>()}); }
    if (n.mix.empty() && n.scatter == NatureDef::TALLGRASS) n.mix.push_back({NS_TALLMEADOW, 1.0f});
    std::sort(n.mix.begin(), n.mix.end());
    MeshData m; previewMesh(a, m);
    a.opaque.destroy(); a.opaque.upload(m);
    float top = n.scatter == NatureDef::STONES ? n.sizeMax : 0.2f;
    for (auto& s : n.mix) top = std::max(top, spDef(s.first).h1 * n.hMax);
    pts.push_back(vec3(-n.radius, 0, -n.radius)); pts.push_back(vec3(n.radius, top, n.radius));
    a.shape = nullptr;   // never collides
  }
}

// ---------------------------------------------------------------- runtime
int gNatRebuilds = 0;
namespace {
std::vector<GrassFootprint> gFp; uint32_t gFpRev = 0, gFpGround = 0; size_t gFpN = 0; const void* gFpDoc = nullptr;
const std::vector<GrassFootprint>& footprints(App& app) {
  if (gFpRev != app.revCounter || gFpGround != app.groundRev || gFpDoc != &app.doc || gFpN != app.doc.instances.size() || gFp.empty()) {   // deletes do not bump revs
    gFp = app.grassFootprints(); gFpRev = app.revCounter; gFpGround = app.groundRev; gFpDoc = &app.doc; gFpN = app.doc.instances.size();
  }
  return gFp;
}
uint64_t footHash(const std::vector<GrassFootprint>& fp, vec2 c, float R) {
  uint64_t h = 77;
  for (auto& f : fp) {
    float rr = glm::length(glm::max(glm::abs(f.bmin), glm::abs(f.bmax)));
    if (glm::length(f.pos - c) > R + rr + 1) continue;
    h = hcomb(h, ((uint64_t)fbits(f.pos.x) << 32) | fbits(f.pos.y)); h = hcomb(h, ((uint64_t)fbits(f.yaw) << 32) | fbits(f.bmax.x - f.bmin.x));
  }
  return h;
}
void rebuildPatch(App& app, const Instance& i, InstanceRT& r, const std::vector<GrassFootprint>& fpAll) {
  const AssetDef& a = *r.asset; const NatureDef& n = a.nature;
  NatPatchParams pp = naturePatchParams(i, a);
  std::vector<NatScatterPt> pts; natureLayout(n, a.id, pp, pts);
  quat q = yawQuat(i.yaw), qi = glm::inverse(q);
  const Terrain& t = app.terrain;
  std::vector<GrassFootprint> fp;
  vec2 c(i.pos.x, i.pos.z);
  for (auto& f : fpAll) { float rr = glm::length(glm::max(glm::abs(f.bmin), glm::abs(f.bmax))); if (glm::length(f.pos - c) <= pp.radius + rr + 1) fp.push_back(f); }
  auto blocked = [&](float x, float z) { for (auto& f : fp) if (Grass::footprintDistance(f, x, z) < 0.02f) return true; return false; };
  r.natInst.clear(); r.natPos.clear(); r.natGy.clear();
  ++gNatRebuilds;
  r.natMin = vec3(1e9f); r.natMax = vec3(-1e9f);
  MeshData m;
  for (auto& s : pts) {
    vec3 w = i.pos + q * vec3(s.p.x, 0, s.p.y);
    if (w.x < t.org.x + 0.5f || w.z < t.org.y + 0.5f || w.x > t.org.x + t.size - 0.5f || w.z > t.org.y + t.sizeZ - 0.5f) continue;
    float y = t.height(w.x, w.z);
    vec3 nn = t.normal(w.x, w.z);
    if (blocked(w.x, w.z)) continue;
    if (n.scatter == NatureDef::STONES) {
      if (y < t.waterLevel - 0.3f) continue;
      int lith = n.lith < 0 ? 1 + (s.r3 < 0.5f) : n.lith;
      vec3 nl = qi * nn, up = glm::normalize(glm::mix(vec3(0, 1, 0), nl, 0.75f));
      vec3 ax = glm::cross(vec3(0, 1, 0), up); float sa = glm::length(ax);
      quat align = sa > 1e-5f ? glm::angleAxis(std::asin(std::min(1.0f, sa)), ax / sa) : quat(1, 0, 0, 0);
      quat tilt = glm::angleAxis(radians((s.r2 - 0.5f) * 24.0f), vec3(1, 0, 0));
      float sink = s.size * 0.12f * s.r3;
      vec3 lp(s.p.x, y - i.pos.y - sink, s.p.y);
      float asp = 0.75f + 0.5f * s.r0;
      mat4 M = glm::translate(mat4(1), lp) * glm::mat4_cast(align * yawQuat(s.r0 * 360.0f) * tilt) * glm::scale(mat4(1), vec3(s.size, s.size * asp, s.size));
      appendStone(m, (int)(s.r2 * 8), M, lithTint(lith, s.r1), lith);
      r.natPos.push_back(vec4(lp, s.size)); r.natGy.push_back(vec3(w.x, y, w.z));
      vec3 wp = i.pos + q * lp;
      r.natMin = glm::min(r.natMin, wp - vec3(s.size)); r.natMax = glm::max(r.natMax, wp + vec3(s.size));
    } else {
      if (y < t.waterLevel + 0.05f || nn.y < 0.62f || t.dirtAt(w.x, w.z) > 0.25f) continue;
      int sp = s.species ? s.species : NS_TALLMEADOW;
      const SpeciesDef& d = spDef(sp);
      float h = glm::mix(d.h0, d.h1, s.r1) * glm::mix(n.hMin, n.hMax, s.r2);
      float wd = glm::mix(d.w0, d.w1, s.r2);
      vec3 g = d.green * (0.86f + 0.28f * s.r3);
      if (sp == NS_TALLMEADOW) g = glm::mix(g, vec3(0.30f, 0.28f, 0.14f), 0.4f * s.r0);   // yellowing stems
      r.natInst.push_back({vec4(w.x, y, w.z, s.r0 * 6.2831853f), vec4(h, wd, packPlant(g, sp), s.r3 * 6.2831853f + s.r1)}); r.natGy.push_back(vec3(w.x, y, w.z));
      r.natMin = glm::min(r.natMin, vec3(w.x, y, w.z) - vec3(0.3f, 0, 0.3f)); r.natMax = glm::max(r.natMax, vec3(w.x, y + h, w.z) + vec3(0.3f, 0.2f, 0.3f));
    }
  }
  if (n.scatter == NatureDef::STONES) { r.natMesh.destroy(); r.natMesh.upload(m); r.natCount = (int)r.natPos.size(); }
  else {
    if (!r.natVbo) glCreateBuffers(1, &r.natVbo);
    glNamedBufferData(r.natVbo, std::max<size_t>(1, r.natInst.size()) * sizeof(GrassInstance), r.natInst.empty() ? nullptr : r.natInst.data(), GL_STATIC_DRAW);
    r.natCount = (int)r.natInst.size();
  }
  r.natRev = i.rev; r.natGround = app.groundRev;
}
}  // namespace

void natureDrawItems(App& app, const Instance& i, InstanceRT& r, std::vector<DrawItem>& op, float camDist) {
  const AssetDef& a = *r.asset; const NatureDef& n = a.nature;
  if (n.kind == NatureDef::ROCK) { op.push_back({&a.opaque, app.instanceMatrix(i)}); return; }
  NatPatchParams pp = naturePatchParams(i, a);
  bool stones = n.scatter == NatureDef::STONES;
  float reach = (stones ? 160.0f : 140.0f) + pp.radius;
  if (camDist > reach) return;
  const auto& fp = footprints(app);
  uint64_t fh = footHash(fp, vec2(i.pos.x, i.pos.z), pp.radius);
  bool stale = r.natRev != i.rev || r.natFoot != fh;
  if (!stale && r.natGround != app.groundRev) {   // the ground changed somewhere: rebuild only if it moved under this patch
    size_t n = r.natGy.size(), step = std::max<size_t>(1, n / 384);
    for (size_t k = 0; k < n && !stale; k += step) stale = std::fabs(app.terrain.height(r.natGy[k].x, r.natGy[k].z) - r.natGy[k].y) > 1e-4f;
    vec3 c = i.pos; stale = stale || (r.natCount == 0 && std::fabs(app.terrain.height(c.x, c.z) - r.natCy) > 1e-4f);
    if (!stale) r.natGround = app.groundRev;
  }
  if (stale) { r.natFoot = fh; rebuildPatch(app, i, r, fp); r.natCy = app.terrain.height(i.pos.x, i.pos.z); }
  if (!r.natCount) return;
  if (stones) op.push_back({&r.natMesh, app.instanceMatrix(i)});
  else app.R.natPatches.push_back({r.natVbo, r.natCount, r.natMin, r.natMax});
}
void natureDestroyRT(InstanceRT& r) {
  r.natMesh.destroy();
  if (r.natVbo) glDeleteBuffers(1, &r.natVbo);
  r.natVbo = 0; r.natCount = 0; r.natRev = 0; r.natGround = ~0u; r.natFoot = 0; r.natInst.clear(); r.natPos.clear(); r.natGy.clear();
}
uint64_t naturePatchHash(const InstanceRT& r) {
  uint64_t h = 1469598103934665603ull;
  for (auto& g : r.natInst) { const uint32_t* w = reinterpret_cast<const uint32_t*>(&g); for (int k = 0; k < 8; ++k) h = (h ^ w[k]) * 1099511628211ull; }
  for (auto& p : r.natPos) for (int k = 0; k < 4; ++k) h = (h ^ fbits(p[k])) * 1099511628211ull;
  return h ^ (uint64_t)r.natCount;
}
size_t naturePatchItems(const InstanceRT& r) { return (size_t)r.natCount; }

// ---------------------------------------------------------------- patch drawing (grass program)
void natureDrawPatches(GLuint prog, const std::vector<NatPatchDraw>& items, const mat4& vp, vec3 cam, float radius, float frac, float wind, int& draws, size_t& tufts) {
  static GLuint vao = 0, vbo = 0, ebo = 0; static int indexCount = 0;
  if (items.empty() || radius <= 0) return;
  if (!vao) {   // one plant: 7 blades (5 rows x 2) + 7 head quads (z = 10 + blade)
    std::vector<vec3> b; std::vector<uint16_t> idx;
    for (int bl = 0; bl < Grass::kBlades; ++bl) {
      uint16_t base = (uint16_t)b.size();
      for (int r = 0; r <= 4; ++r) { float v = r / 4.0f; b.push_back({-1, v, (float)bl}); b.push_back({1, v, (float)bl}); }
      for (int r = 0; r < 4; ++r) { uint16_t a = base + r * 2; idx.insert(idx.end(), {a, (uint16_t)(a + 1), (uint16_t)(a + 2), (uint16_t)(a + 1), (uint16_t)(a + 3), (uint16_t)(a + 2)}); }
    }
    for (int bl = 0; bl < Grass::kBlades; ++bl) {
      uint16_t a = (uint16_t)b.size();
      b.push_back({-1, 0, 10.0f + bl}); b.push_back({1, 0, 10.0f + bl}); b.push_back({-1, 1, 10.0f + bl}); b.push_back({1, 1, 10.0f + bl});
      idx.insert(idx.end(), {a, (uint16_t)(a + 1), (uint16_t)(a + 2), (uint16_t)(a + 1), (uint16_t)(a + 3), (uint16_t)(a + 2)});
    }
    indexCount = (int)idx.size();
    glCreateBuffers(1, &vbo); glCreateBuffers(1, &ebo);
    glNamedBufferData(vbo, b.size() * sizeof(vec3), b.data(), GL_STATIC_DRAW);
    glNamedBufferData(ebo, idx.size() * sizeof(uint16_t), idx.data(), GL_STATIC_DRAW);
    glCreateVertexArrays(1, &vao);
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(vec3));
    glVertexArrayElementBuffer(vao, ebo);
    glEnableVertexArrayAttrib(vao, 0); glVertexArrayAttribBinding(vao, 0, 0); glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
    for (int k = 1; k <= 2; ++k) { glEnableVertexArrayAttrib(vao, k); glVertexArrayAttribBinding(vao, k, 1); glVertexArrayAttribFormat(vao, k, 4, GL_FLOAT, GL_FALSE, (k - 1) * sizeof(vec4)); }
    glVertexArrayBindingDivisor(vao, 1, 1);
  }
  glUseProgram(prog);
  glBindVertexArray(vao);
  GLint lw = glGetUniformLocation(prog, "uWidthScale");
  glUniform1f(glGetUniformLocation(prog, "uRadius"), radius);
  glUniform1f(glGetUniformLocation(prog, "uWind"), wind);
  float lodStart = radius * 0.4f;
  for (auto& it : items) {
    if (!it.count || !it.vbo) continue;
    vec3 cl = glm::clamp(cam, it.mn, it.mx);
    float d = glm::length(cl - cam);
    if (d > radius) continue;
    bool out = false;
    for (int pl = 0; pl < 6 && !out; ++pl) {
      int axis = pl >> 1; float sgn = (pl & 1) ? -1.0f : 1.0f; int inside = 0;
      for (int q = 0; q < 8; ++q) {
        vec4 p = vp * vec4((q & 1) ? it.mx.x : it.mn.x, (q & 2) ? it.mx.y : it.mn.y, (q & 4) ? it.mx.z : it.mn.z, 1.0f);
        if (sgn * p[axis] <= p.w) ++inside;
      }
      out = inside == 0;
    }
    if (out) continue;
    float lod = d <= lodStart ? 1.0f : std::clamp((lodStart / d) * (lodStart / d), 0.15f, 1.0f);
    int n = std::max(1, (int)std::ceil(it.count * frac * lod));
    glUniform1f(lw, std::min(1.8f, std::pow(lod, -0.3f)));
    glVertexArrayVertexBuffer(vao, 1, it.vbo, 0, sizeof(GrassInstance));
    glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, nullptr, n);
    ++draws; tufts += (size_t)n;
  }
}
