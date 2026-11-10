// Natural ground dressing: boulders / outcrops (procedural hulls, pattern 29), scatter patches (flowers, tall grass,
// stones). See nature.h for the asset contract and docs/nature.md for the design notes.
#include "nature.h"
#include "app.h"
#include "physics.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_map>
bool groundPaintBlocksNature(float x, float z);   // lasso ground paint / water (ground_paint.cpp)
float lassoWaterAtCurrent(float x, float z);
uint64_t lassoGroundRev();
uint64_t lassoGroundSig(vec2 lo, vec2 hi);

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
    {"cornflower", NS_CORNFLOWER}, {"cowparsley", NS_COWPARSLEY}, {"foxglove", NS_FOXGLOVE}, {"clover", NS_CLOVER},
    {"turf", NS_TURF}, {"meadowgrass", NS_MEADOWGRASS}, {"seedgrass", NS_SEEDGRASS}, {"weeds", NS_WEEDS}, {"straw", NS_STRAW},
    {"wheat", NS_WHEAT}, {"brush", NS_BRUSH}, {"stubble", NS_STUBBLE}};
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
  if (lith == 0) { for (int k = 0; k < 7; ++k) addPl(rdir(), 0.76f, 0.93f); }   // NT: more, deeper spalls (granite read as smooth blobs)
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
  int target = std::clamp((int)std::lround(D * (n.square ? 4.0f : 3.14159265f) * R * R), 1, cap);
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
    if (n.square) q.p = vec2((u * 2 - 1) * R, (w * 2 - 1) * R);                          // tiles: full to the straight edge
    else {
      q.p = vec2(rr * std::cos(th), rr * std::sin(th));
      if (rr > 0.7f * R && rs < smooth01(0.7f * R, R, rr)) continue;                     // soft, ragged rim
    }
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
  for (auto& p : pts) { h = hcomb(h, ((uint64_t)fbits(p.p.x) << 32) | fbits(p.p.y)); h = hcomb(h, ((uint64_t)fbits(p.size) << 32) | (uint32_t)std::max(0, p.species)); }
  return h ^ pts.size();
}

// ---------------------------------------------------------------- plant / stone tables
namespace {
struct SpeciesDef { float h0, h1, w0, w1; vec3 green; };
const SpeciesDef& spDef(int sp) {
  static const SpeciesDef S[16] = {
    {0.09f, 0.19f, 0.016f, 0.022f, {0.14f, 0.22f, 0.060f}},   // 0 turf (blade width)
    {0.26f, 0.54f, 0.018f, 0.026f, {0.16f, 0.23f, 0.070f}},   // 1 meadow grass tussock
    {0.55f, 0.95f, 0.0065f, 0.010f, {0.21f, 0.24f, 0.10f}},   // 2 seed-head grass (three stems)
    {0.05f, 0.09f, 0.028f, 0.040f, {0.10f, 0.17f, 0.060f}},   // 3 clover leaves / broadleaf weeds
    {0.25f, 0.50f, 0.012f, 0.016f, {0.38f, 0.33f, 0.18f}},    // 4 dry straw
    {0.60f, 1.20f, 0.0050f, 0.0060f, {0.22f, 0.25f, 0.11f}},   // 5 tall meadow grass (stem width)
    {0.35f, 0.65f, 0.034f, 0.046f, {0.17f, 0.24f, 0.08f}},     // 6 poppy (head radius)
    {0.30f, 0.60f, 0.024f, 0.034f, {0.16f, 0.23f, 0.08f}},     // 7 ox-eye daisy
    {0.20f, 0.50f, 0.011f, 0.015f, {0.15f, 0.24f, 0.07f}},     // 8 buttercup
    {0.30f, 0.60f, 0.017f, 0.024f, {0.19f, 0.25f, 0.13f}},     // 9 cornflower (grey-green)
    {0.70f, 1.20f, 0.050f, 0.080f, {0.17f, 0.25f, 0.09f}},     // 10 cow parsley (umbel radius)
    {0.80f, 1.40f, 0.024f, 0.032f, {0.14f, 0.21f, 0.08f}},     // 11 foxglove (bell spike half width)
    {0.08f, 0.18f, 0.011f, 0.014f, {0.10f, 0.18f, 0.06f}},     // 12 clover (head radius)
    {0.95f, 1.20f, 0.0f, 0.0f, {0.47f, 0.38f, 0.17f}},         // 13 wheat (shape.y carries the flatten code)
    {0.30f, 0.65f, 0.030f, 0.034f, {0.15f, 0.16f, 0.075f}},    // 14 brush shrublet (twig + leaf clusters)
    {0.08f, 0.18f, 0.0f, 0.0f, {0.46f, 0.40f, 0.24f}},         // 15 cereal stubble (cut straw)
  };
  return S[std::clamp(sp, 0, 15)];
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
    case NS_CLOVER: return {0.62f, 0.28f, 0.44f}; case NS_WHEAT: return {0.50f, 0.40f, 0.18f}; default: return {0.42f, 0.37f, 0.24f}; }
}
int defaultSpecies(const NatureDef& n) { return n.scatter == NatureDef::WHEAT ? NS_WHEAT : NS_TALLMEADOW; }
bool flatSpecies(int sp) { return sp == NS_WHEAT || sp == NS_STUBBLE; }
bool hasHead(int sp) { return (sp >= NS_TALLMEADOW && sp <= NS_CLOVER) || sp == NS_WHEAT; }
// the pebble layer under a plant patch: a stone scatter with the patch's shape and the pebble block's sizes
NatureDef pebbleDef(const NatureDef& n) {
  NatureDef p; p.kind = NatureDef::SCATTER; p.scatter = NatureDef::STONES; p.square = n.square;
  p.radius = n.radius; p.density = n.pebDensity; p.minR = n.minR; p.maxR = n.maxR; p.minD = 0.01f; p.maxD = 200;
  p.sizeMin = n.pebMin; p.sizeMax = n.pebMax; p.sizePow = 2.4f; p.lith = n.pebLith; p.clump = 0.6f;
  return p;
}
NatPatchParams pebbleParams(const NatureDef& n, const NatPatchParams& pp) {
  NatPatchParams q = pp; q.density = n.pebDensity * pp.density / std::max(0.01f, n.density); q.seed = pp.seed * 7u + 3u; return q;
}
// plant albedo: species green (or the asset's tint), per-plant variation, drying toward straw in world-space drifts,
// wheat: ripe gold with broad tone drifts and a few late, greener patches
vec3 plantColour(const NatureDef& n, int sp, const NatScatterPt& s, vec2 w) {
  const SpeciesDef& d = spDef(sp);
  vec3 g = d.green;
  for (auto& t : n.tint) if (t.first == sp) g = t.second;
  g *= 0.86f + 0.28f * s.r3;
  if (sp == NS_TALLMEADOW) g = glm::mix(g, vec3(0.30f, 0.28f, 0.14f), 0.4f * s.r0);   // yellowing stems
  if (sp == NS_WHEAT || sp == NS_STUBBLE) {
    float k = vn2(w * 0.07f, 911) * 0.7f + vn2(w * 0.31f, 912) * 0.3f;
    g = glm::mix(g * vec3(1.05f, 1.0f, 0.90f), g * vec3(0.90f, 0.88f, 0.92f), smooth01(0.25f, 0.75f, k));
    if (sp == NS_WHEAT && n.wheat == 1 && vn2(w * 0.13f, 913) > 0.74f) g = glm::mix(g, vec3(0.32f, 0.33f, 0.15f), 0.3f);   // later-ripening drifts
  }
  if (n.dryMax > 0 && !flatSpecies(sp)) {
    float dn = vn2(w * 0.16f, 917) * 0.7f + vn2(w * 0.6f, 919) * 0.3f;
    float amt = glm::mix(n.dryMin, n.dryMax, smooth01(0.2f, 0.8f, dn));
    vec3 straw = glm::mix(vec3(0.40f, 0.34f, 0.17f), vec3(0.47f, 0.40f, 0.22f), s.r0) * (0.9f + 0.2f * s.r1);
    g = glm::mix(g, straw, std::clamp(amt, 0.0f, 1.0f));
  }
  return glm::min(g, vec3(0.5f));
}
float plantHeight(const NatureDef& n, int sp, const NatScatterPt& s) {
  const SpeciesDef& d = spDef(sp);
  return glm::mix(d.h0, d.h1, s.r1) * glm::mix(n.hMin, n.hMax, s.r2);
}
// wheat flatten code in GrassInstance::shape.y (species 13 / 15): integer part = amount x 63, fraction = direction
float flatCode(float f, float dirRad) { float a = dirRad / 6.2831853f; a -= std::floor(a); return std::round(std::clamp(f, 0.0f, 1.0f) * 63.0f) + a * 0.98f; }
float flatOf(float code) { return std::floor(code) / 63.0f; }
// the crop's own state: ripe stands with a few lodged patches; trampled has swathes and trails laid flat; stubble is
// short cut straw with lying straw between
void bakedFlat(const NatureDef& n, int sp, const NatScatterPt& s, vec2 w, float& f, float& dir) {
  f = 0; dir = vn2(w * 0.031f, 921) * 12.566f + (s.r0 - 0.5f) * 0.6f;
  if (n.wheat == 1) f = smooth01(0.80f, 0.90f, vn2(w * 0.09f, 923)) * (0.75f + 0.25f * s.r1);
  else if (n.wheat == 2) {
    float sw = smooth01(0.44f, 0.60f, vn2(w * 0.11f, 925));                                    // swathes
    float tr = smooth01(0.055f, 0.0f, std::fabs(vn2(w * 0.035f, 927) - 0.5f));                  // winding trails
    f = std::max(sw, tr) * (0.7f + 0.3f * s.r1);
    if (tr > sw) dir = vn2(w * 0.035f + vec2(17), 929) * 12.566f;
    f = std::max(f, 0.18f * s.r2);                                                             // the rest leans, beaten
  } else if (n.wheat == 3 && sp == NS_STUBBLE && s.r3 > 0.78f) f = 1.0f;                       // lying straw
}
// catalogue mesh (thumbnail, placement ghost): plants as stems + coloured heads, stones as on flat ground
void previewMesh(AssetDef& a, MeshData& m) {
  const NatureDef& n = a.nature;
  NatPatchParams pp; pp.radius = n.radius; pp.density = n.density;
  std::vector<NatScatterPt> pts; natureLayout(n, a.id, pp, pts);
  auto stones = [&](const NatureDef& sn, const std::vector<NatScatterPt>& sp, size_t lim) {
    for (size_t k = 0; k < sp.size() && k < lim; ++k) {
      auto& q = sp[k]; int lith = sn.lith < 0 ? 1 + (q.r3 < 0.5f) : sn.lith;
      mat4 M = glm::translate(mat4(1), vec3(q.p.x, 0, q.p.y)) * glm::mat4_cast(yawQuat(q.r0 * 360.0f)) * glm::scale(mat4(1), vec3(q.size));
      appendStone(m, (int)(q.r2 * 8), M, lithTint(lith, q.r2), lith);
    }
  };
  if (n.scatter == NatureDef::STONES) { stones(n, pts, pts.size()); return; }
  if (n.pebDensity > 0) { NatureDef pn = pebbleDef(n); std::vector<NatScatterPt> pq; natureLayout(pn, a.id, pebbleParams(n, pp), pq); stones(pn, pq, 260); }
  if (n.scatter == NatureDef::WHEAT) {   // the soil / straw-litter layer
    Material sm{"soil", n.soil, 0.95f, 0.0f, PAT_PLAIN, 1};
    Part s; s.center = vec3(0, 0.01f, 0); s.size = vec3(2 * n.radius, 0.02f, 2 * n.radius); s.collide = false; appendPart(m, s, sm, mat4(1));
  }
  Material stem{"stem", vec3(0.16f, 0.24f, 0.08f), 0.9f, 0.0f, PAT_PLAIN, 1};
  bool wheat = n.scatter == NatureDef::WHEAT;
  size_t lim = std::min<size_t>(pts.size(), wheat ? 1400 : 260);
  for (size_t k = 0; k < lim; ++k) {
    auto& q = pts[k]; int sp = q.species >= 0 ? q.species : defaultSpecies(n);
    float h = plantHeight(n, sp, q), f = 0, dir = 0;
    if (flatSpecies(sp)) { bakedFlat(n, sp, q, q.p, f, dir); if (n.wheat == 3 && f > 0.5f) h *= 2.2f; }
    vec3 g = plantColour(n, sp, q, q.p);
    float up = h * std::cos(f * 1.42f), out = h * std::sin(f * 1.42f);
    vec3 base(q.p.x, 0, q.p.y), tip = base + vec3(std::sin(dir) * out, up, std::cos(dir) * out);
    float sw = sp == NS_BRUSH ? 0.05f : sp <= NS_STRAW ? 0.03f : 0.02f;
    Part s; s.center = (base + tip) * 0.5f; s.size = vec3(sw, std::max(0.02f, up), sw); s.collide = false;
    if (out > up) { s.center.y = std::max(0.02f, up * 0.5f); s.size = vec3(std::fabs(tip.x - base.x) + sw, std::max(0.02f, up), std::fabs(tip.z - base.z) + sw); }
    stem.albedo = g; appendPart(m, s, stem, mat4(1));
    if (!hasHead(sp)) continue;
    Material hm{"head", headColour(sp), 0.8f, 0.0f, PAT_PLAIN, 1};
    const SpeciesDef& d = spDef(sp);
    float hs = sp == NS_TALLMEADOW ? 0.04f : sp == NS_WHEAT ? 0.03f : sp == NS_FOXGLOVE ? 0.07f : std::max(0.05f, d.w1 * 2.2f);
    float hh = sp == NS_TALLMEADOW ? 0.14f : sp == NS_WHEAT ? 0.11f : sp == NS_FOXGLOVE ? h * 0.4f : hs * 0.5f;
    Part hd; hd.center = tip - vec3(0, (sp == NS_FOXGLOVE ? h * 0.2f : sp == NS_WHEAT && f < 0.5f ? hh * 0.5f : 0.0f), 0); hd.size = vec3(hs, f > 0.5f ? hs : hh, hs); hd.collide = false;
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
    n.scatter = k == "stones" ? NatureDef::STONES : k == "wheat" ? NatureDef::WHEAT : (k == "tallgrass" || k == "grass" || k == "brush") ? NatureDef::TALLGRASS : NatureDef::FLOWERS;
    n.radius = sj.value("radius", 3.0f); n.density = sj.value("density", 4.0f);
    n.minR = sj.value("minRadius", 0.5f); n.maxR = sj.value("maxRadius", 15.0f); n.minD = sj.value("minDensity", 0.2f); n.maxD = sj.value("maxDensity", 40.0f);
    n.clump = sj.value("clump", 0.5f);
    if (sj.contains("size")) { n.sizeMin = sj["size"][0].get<float>(); n.sizeMax = sj["size"][1].get<float>(); }
    n.sizePow = sj.value("sizePow", 2.2f); n.lith = lithFrom(sj.value("lith", json("limestone")));
    if (sj.contains("heightScale")) { n.hMin = sj["heightScale"][0].get<float>(); n.hMax = sj["heightScale"][1].get<float>(); }
    n.square = sj.value("shape", std::string("disc")) == "square";
    if (sj.contains("dry")) { n.dryMin = sj["dry"][0].get<float>(); n.dryMax = sj["dry"][1].get<float>(); }
    for (auto& [name, c] : sj.value("tint", json::object()).items()) { int sp = natSpeciesFrom(name); if (sp >= 0) n.tint.push_back({sp, jvec3(c, vec3(0.2f))}); }
    if (sj.contains("pebbles")) {
      auto& pb = sj["pebbles"]; n.pebDensity = pb.value("density", 2.0f); n.pebLith = lithFrom(pb.value("lith", json("mixed")));
      if (pb.contains("size")) { n.pebMin = pb["size"][0].get<float>(); n.pebMax = pb["size"][1].get<float>(); }
    }
    if (n.scatter == NatureDef::WHEAT) {
      std::string st = sj.value("state", std::string("ripe"));
      n.wheat = st == "stubble" ? 3 : st == "trampled" ? 2 : 1;
      if (sj.contains("soil")) n.soil = jvec3(sj["soil"], n.soil);
    }
    json mix = sj.value("mix", json::object());
    for (auto& [name, w] : mix.items()) { int sp = natSpeciesFrom(name); if (sp >= 0) n.mix.push_back({sp, w.get<float>()}); }
    if (n.mix.empty() && n.scatter == NatureDef::TALLGRASS) n.mix.push_back({NS_TALLMEADOW, 1.0f});
    if (n.mix.empty() && n.scatter == NatureDef::WHEAT) n.mix.push_back({n.wheat == 3 ? NS_STUBBLE : NS_WHEAT, 1.0f});
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
std::vector<GrassFootprint> gFp; uint32_t gFpRev = 0, gFpGround = 0; size_t gFpN = 0; const void* gFpDoc = nullptr; uint64_t gFpGen = 1;   // gFpGen: bumped on every rebuild (P2)
const std::vector<GrassFootprint>& footprints(App& app) {
  if (gFpRev != app.revCounter || gFpGround != app.groundRev || gFpDoc != &app.doc || gFpN != app.doc.instances.size() || gFp.empty()) {   // deletes do not bump revs
    ++gFpGen; gFp = app.grassFootprints(); gFpRev = app.revCounter; gFpGround = app.groundRev; gFpDoc = &app.doc; gFpN = app.doc.instances.size();
    gFp.erase(std::remove_if(gFp.begin(), gFp.end(), [](const GrassFootprint& f) { return f.soft; }), gFp.end());   // crop tiles clear pasture grass only
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

// ---- flattening state
struct FlatStamp { vec2 c{0}; float yaw = 0; vec2 half{0}; float r = 0, strength = 1; int kind = 0; vec2 mn{0}, mx{0}; };   // kind 0 blast (radius r), 1 hull rectangle
std::vector<FlatStamp> gStamps;
uint32_t gStampEpoch = 1;
struct FlatState { size_t applied = 0; uint32_t epoch = 0; std::vector<float> f, h0; };
std::unordered_map<const InstanceRT*, FlatState> gFlat;
std::map<uint64_t, vec3> gTankLast;
int instSpecies(const GrassInstance& g) { uint32_t u; std::memcpy(&u, &g.shape.z, 4); return (int)((u >> 24) & 0xffu) - 0x38; }
bool stampAt(const FlatStamp& s, vec2 p, float r0, float& f, float& dir) {
  vec2 d = p - s.c;
  if (s.kind == 0) {
    float L = glm::length(d); if (L > s.r * 1.15f) return false;
    f = s.strength * (1.0f - smooth01(0.4f * s.r, 1.15f * s.r, L));
    dir = std::atan2(d.x, d.y) + (r0 - 0.5f) * 0.5f;
    return f > 0.02f;
  }
  float y = radians(s.yaw); vec2 fwd(std::sin(y), std::cos(y)), right(std::cos(y), -std::sin(y));
  vec2 l(glm::dot(d, right), glm::dot(d, fwd));
  vec2 q = glm::abs(l) - s.half;
  float out = glm::length(glm::max(q, vec2(0)));
  if (out > 0.55f) return false;
  f = s.strength * (1.0f - smooth01(0.0f, 0.55f, out));
  vec2 v = fwd + right * ((l.x < 0 ? -1.0f : 1.0f) * 0.4f * std::min(1.0f, std::fabs(l.x) / std::max(0.1f, s.half.x)));   // laid along the track, splayed outward
  dir = std::atan2(v.x, v.y) + (r0 - 0.5f) * 0.35f;
  return f > 0.02f;
}
// applies stamps [from, gStamps.size()) to a built wheat patch; returns true when an instance changed
bool applyStamps(InstanceRT& r, FlatState& st, size_t from) {
  bool changed = false;
  for (size_t k = from; k < gStamps.size(); ++k) {
    const FlatStamp& s = gStamps[k];
    if (s.mx.x < r.natMin.x || s.mx.y < r.natMin.z || s.mn.x > r.natMax.x || s.mn.y > r.natMax.z) continue;
    for (size_t i = 0; i < r.natInst.size(); ++i) {
      GrassInstance& g = r.natInst[i];
      vec2 p(g.posYaw.x, g.posYaw.z);
      if (p.x < s.mn.x || p.y < s.mn.y || p.x > s.mx.x || p.y > s.mx.y) continue;
      float f, dir; if (!stampAt(s, p, std::fmod(g.shape.w * 0.6180339f, 1.0f), f, dir)) continue;
      if (f <= st.f[i] + 0.03f) continue;
      st.f[i] = f; changed = true;
      if (flatSpecies(instSpecies(g))) g.shape.y = flatCode(f, dir);
      else g.shape.x = st.h0[i] * (1.0f - 0.75f * f);   // flowers in the crop: broken down
    }
  }
  st.applied = gStamps.size();
  return changed;
}
bool isWheat(const AssetDef* a) { return a && a->nature.kind == NatureDef::SCATTER && a->nature.scatter == NatureDef::WHEAT; }
bool nearWheat(App& app, vec2 mn, vec2 mx) {
  for (auto& i : app.doc.instances) {
    const AssetDef* a = app.assetFor(i); if (!isWheat(a)) continue;
    float R = naturePatchParams(i, *a).radius * 1.4143f + 0.5f;
    if (mx.x < i.pos.x - R || mx.y < i.pos.z - R || mn.x > i.pos.x + R || mn.y > i.pos.z + R) continue;
    return true;
  }
  return false;
}
void pushStamp(App& app, const FlatStamp& s) {
  if (!nearWheat(app, s.mn, s.mx)) return;
  gStamps.push_back(s);
}

void rebuildPatch(App& app, const Instance& i, InstanceRT& r, const std::vector<GrassFootprint>& fpAll) {
  const AssetDef& a = *r.asset; const NatureDef& n = a.nature;
  NatPatchParams pp = naturePatchParams(i, a), lp = pp;
  bool wheat = n.scatter == NatureDef::WHEAT, stones = n.scatter == NatureDef::STONES;
  if (wheat) lp.seed = pp.seed ^ (uint32_t)mix64(((uint64_t)(uint32_t)std::lround(i.pos.x * 4) << 32) ^ (uint32_t)std::lround(i.pos.z * 4));   // neighbouring tiles never repeat
  std::vector<NatScatterPt> pts; natureLayout(n, a.id, lp, pts);
  quat q = yawQuat(i.yaw), qi = glm::inverse(q);
  const Terrain& t = app.terrain;
  std::vector<GrassFootprint> fp;
  vec2 c(i.pos.x, i.pos.z);
  float reachR = pp.radius * (n.square ? 1.4143f : 1.0f);
  for (auto& f : fpAll) { float rr = glm::length(glm::max(glm::abs(f.bmin), glm::abs(f.bmax))); if (glm::length(f.pos - c) <= reachR + rr + 1) fp.push_back(f); }
  auto blocked = [&](float x, float z) { for (auto& f : fp) if (Grass::footprintDistance(f, x, z) < 0.02f) return true; return false; };
  auto inside = [&](vec3 w) { return !(w.x < t.org.x + 0.5f || w.z < t.org.y + 0.5f || w.x > t.org.x + t.size - 0.5f || w.z > t.org.y + t.sizeZ - 0.5f); };
  r.natInst.clear(); r.natPos.clear(); r.natGy.clear();
  ++gNatRebuilds;
  r.natMin = vec3(1e9f); r.natMax = vec3(-1e9f);
  MeshData m;
  auto addStone = [&](const NatureDef& sn, const NatScatterPt& s) {
    vec3 w = i.pos + q * vec3(s.p.x, 0, s.p.y);
    if (!inside(w)) return;
    float y = t.height(w.x, w.z);
    if (blocked(w.x, w.z) || y < t.waterLevel - 0.3f || y < lassoWaterAtCurrent(w.x, w.z) - 0.3f) return;
    vec3 nn = t.normal(w.x, w.z);
    int lith = sn.lith < 0 ? 1 + (s.r3 < 0.5f) : sn.lith;
    vec3 nl = qi * nn, up = glm::normalize(glm::mix(vec3(0, 1, 0), nl, 0.75f));
    vec3 ax = glm::cross(vec3(0, 1, 0), up); float sa = glm::length(ax);
    quat align = sa > 1e-5f ? glm::angleAxis(std::asin(std::min(1.0f, sa)), ax / sa) : quat(1, 0, 0, 0);
    quat tilt = glm::angleAxis(radians((s.r2 - 0.5f) * 24.0f), vec3(1, 0, 0));
    float sink = s.size * 0.12f * s.r3;
    vec3 lp3(s.p.x, y - i.pos.y - sink, s.p.y);
    float asp = 0.75f + 0.5f * s.r0;
    mat4 M = glm::translate(mat4(1), lp3) * glm::mat4_cast(align * yawQuat(s.r0 * 360.0f) * tilt) * glm::scale(mat4(1), vec3(s.size, s.size * asp, s.size));
    appendStone(m, (int)(s.r2 * 8), M, lithTint(lith, s.r1), lith);
    r.natPos.push_back(vec4(lp3, s.size)); r.natGy.push_back(vec3(w.x, y, w.z));
    vec3 wp = i.pos + q * lp3;
    r.natMin = glm::min(r.natMin, wp - vec3(s.size)); r.natMax = glm::max(r.natMax, wp + vec3(s.size));
  };
  if (stones) for (auto& s : pts) addStone(n, s);
  else {
    if (n.pebDensity > 0) { NatureDef pn = pebbleDef(n); std::vector<NatScatterPt> pq; natureLayout(pn, a.id, pebbleParams(n, lp), pq); for (auto& s : pq) addStone(pn, s); }
    for (auto& s : pts) {
      vec3 w = i.pos + q * vec3(s.p.x, 0, s.p.y);
      if (!inside(w)) continue;
      float y = t.height(w.x, w.z);
      vec3 nn = t.normal(w.x, w.z);
      if (blocked(w.x, w.z)) continue;
      if (y < t.waterLevel + 0.05f || nn.y < 0.62f || t.dirtAt(w.x, w.z) > 0.25f) continue;
      if (y < lassoWaterAtCurrent(w.x, w.z) + 0.05f || groundPaintBlocksNature(w.x, w.z)) continue;   // lasso water / paint
      int sp = s.species >= 0 ? s.species : defaultSpecies(n);
      const SpeciesDef& d = spDef(sp);
      vec2 wxz(w.x, w.z);
      float h = plantHeight(n, sp, s);
      float wd = glm::mix(d.w0, d.w1, s.r2);
      if (flatSpecies(sp)) { float f, dir; bakedFlat(n, sp, s, wxz, f, dir); if (n.wheat == 3 && f > 0.5f) h *= 2.2f; wd = flatCode(f, dir); }
      vec3 g = plantColour(n, sp, s, wxz);
      r.natInst.push_back({vec4(w.x, y, w.z, s.r0 * 6.2831853f), vec4(h, wd, packPlant(g, sp), s.r3 * 6.2831853f + s.r1)}); r.natGy.push_back(vec3(w.x, y, w.z));
      r.natMin = glm::min(r.natMin, vec3(w.x, y, w.z) - vec3(0.3f, 0, 0.3f)); r.natMax = glm::max(r.natMax, vec3(w.x, y + h, w.z) + vec3(0.3f, 0.2f, 0.3f));
    }
  }
  if (wheat) {   // soil / straw-litter layer under the crop: a draped 1 m grid, holes where a crater or a footprint is
    float R = pp.radius; int N = std::max(2, (int)std::ceil(2 * R / 1.0f) + 1);
    std::vector<vec3> P((size_t)N * N); std::vector<uint8_t> ok((size_t)N * N);
    for (int iz = 0; iz < N; ++iz) for (int ix = 0; ix < N; ++ix) {
      vec2 l(-R + 2 * R * ix / (N - 1), -R + 2 * R * iz / (N - 1));
      vec3 w = i.pos + q * vec3(l.x, 0, l.y);
      bool in = inside(w);
      float y = in ? t.height(w.x, w.z) : i.pos.y;
      P[(size_t)iz * N + ix] = vec3(l.x, y - i.pos.y + 0.06f, l.y);
      ok[(size_t)iz * N + ix] = in && y > t.waterLevel + 0.05f && y > lassoWaterAtCurrent(w.x, w.z) + 0.05f && !groundPaintBlocksNature(w.x, w.z);
      if (in) { r.natGy.push_back(vec3(w.x, y, w.z)); r.natMin = glm::min(r.natMin, vec3(w.x, y, w.z)); r.natMax = glm::max(r.natMax, vec3(w.x, y + 0.1f, w.z)); }
    }
    uint32_t base = (uint32_t)m.v.size();
    for (int iz = 0; iz < N; ++iz) for (int ix = 0; ix < N; ++ix) {
      const vec3& p = P[(size_t)iz * N + ix];
      vec3 dx = P[(size_t)iz * N + std::min(ix + 1, N - 1)] - P[(size_t)iz * N + std::max(ix - 1, 0)];
      vec3 dz = P[(size_t)std::min(iz + 1, N - 1) * N + ix] - P[(size_t)std::max(iz - 1, 0) * N + ix];
      vec3 nr = glm::normalize(glm::cross(dz, dx)); if (nr.y < 0) nr = -nr;
      vec3 w = i.pos + q * p;
      float v = vn2(vec2(w.x, w.z) * 0.45f, 931) * 0.6f + vn2(vec2(w.x, w.z) * 1.7f, 933) * 0.4f;
      vec3 alb = n.soil * (0.8f + 0.4f * v);
      if (n.wheat != 1) alb = glm::mix(alb, vec3(0.36f, 0.31f, 0.20f), 0.3f);   // trampled / cut: more straw on the ground
      m.v.push_back(Vertex{p, nr, alb, vec4((float)PAT_PLAIN, 0.96f, 0.0f, 1.0f), p, nr});
    }
    for (int iz = 0; iz + 1 < N; ++iz) for (int ix = 0; ix + 1 < N; ++ix) {
      uint32_t a0 = (uint32_t)(iz * N + ix), b0 = a0 + 1, c0 = a0 + N, d0 = c0 + 1;
      if (!ok[a0] || !ok[b0] || !ok[c0] || !ok[d0]) continue;
      vec3 cw = i.pos + q * ((P[a0] + P[d0]) * 0.5f);
      if (blocked(cw.x, cw.z) || t.dirtAt(cw.x, cw.z) > 0.3f) continue;
      m.i.insert(m.i.end(), {base + a0, base + c0, base + b0, base + b0, base + c0, base + d0});
    }
  }
  r.natMesh.destroy(); if (!m.i.empty()) r.natMesh.upload(m);
  if (!stones) {
    if (wheat) {   // session flattening: start from the crop's own state, then every stamp that reaches this tile
      FlatState& st = gFlat[&r]; st = FlatState{}; st.epoch = gStampEpoch;
      st.f.resize(r.natInst.size()); st.h0.resize(r.natInst.size());
      for (size_t k = 0; k < r.natInst.size(); ++k) { auto& g = r.natInst[k]; st.h0[k] = g.shape.x; st.f[k] = flatSpecies(instSpecies(g)) ? flatOf(g.shape.y) : 0.0f; }
      applyStamps(r, st, 0);
    }
    if (!r.natVbo) glCreateBuffers(1, &r.natVbo);
    glNamedBufferData(r.natVbo, std::max<size_t>(1, r.natInst.size()) * sizeof(GrassInstance), r.natInst.empty() ? nullptr : r.natInst.data(), GL_STATIC_DRAW);
  }
  r.natCount = stones ? (int)r.natPos.size() : (int)r.natInst.size();
  r.natRev = i.rev; r.natGround = app.groundRev;
}
}  // namespace

void natureDrawItems(App& app, const Instance& i, InstanceRT& r, std::vector<DrawItem>& op, float camDist) {
  const AssetDef& a = *r.asset; const NatureDef& n = a.nature;
  if (n.kind == NatureDef::ROCK) { op.push_back({&a.opaque, app.instanceMatrix(i)}); return; }
  natureStepFlatten(app);
  NatPatchParams pp = naturePatchParams(i, a);
  bool stones = n.scatter == NatureDef::STONES, wheat = n.scatter == NatureDef::WHEAT;
  float R = pp.radius * (n.square ? 1.4143f : 1.0f);
  float meshReach = (wheat ? 480.0f : 160.0f) + R, plantReach = 140.0f + R;
  if (camDist > std::max(meshReach, stones ? 0.0f : plantReach)) return;
  const auto& fp = footprints(app);
  uint64_t fh;   // P2 dlcache: the footprint hash scans every footprint of the map; reused until the list or the patch changes
  {
    static std::unordered_map<const void*, std::array<uint64_t, 3>> fc;
    auto& e = fc[&r]; uint64_t key = ((uint64_t)i.rev << 32) ^ fbits(R);
    if (app.R.perf.dlCache && e[0] == gFpGen && e[1] == key) fh = e[2];
    else { fh = footHash(fp, vec2(i.pos.x, i.pos.z), R); e = {gFpGen, key, fh}; }
  }
  bool stale = r.natRev != i.rev || r.natFoot != fh;
  {   // lasso paint / water: re-check only when they changed somewhere, rebuild only when they changed here
    static std::unordered_map<const void*, std::pair<uint64_t, uint64_t>> lsig;
    uint64_t g = lassoGroundRev(); auto& s = lsig[&r];
    if (s.first != g) { vec2 c(i.pos.x, i.pos.z); uint64_t a = lassoGroundSig(c - vec2(R + 2), c + vec2(R + 2)); if (a != s.second && s.first != 0) stale = true; if (s.first == 0 && a != lassoGroundSig(vec2(1e9f), vec2(1e9f))) stale = true; s = {g, a}; }
  }
  if (wheat && !stale) { auto it = gFlat.find(&r); stale = it == gFlat.end() || it->second.epoch != gStampEpoch; }
  if (!stale && r.natGround != app.groundRev) {   // the ground changed somewhere: rebuild only if it moved under this patch
    size_t n = r.natGy.size(), step = std::max<size_t>(1, n / 384);
    for (size_t k = 0; k < n && !stale; k += step) stale = std::fabs(app.terrain.height(r.natGy[k].x, r.natGy[k].z) - r.natGy[k].y) > 1e-4f;
    vec3 c = i.pos; stale = stale || (r.natCount == 0 && std::fabs(app.terrain.height(c.x, c.z) - r.natCy) > 1e-4f);
    if (!stale) r.natGround = app.groundRev;
  }
  if (stale) { r.natFoot = fh; rebuildPatch(app, i, r, fp); r.natCy = app.terrain.height(i.pos.x, i.pos.z); }
  else if (wheat) {
    FlatState& st = gFlat[&r];
    if (st.applied < gStamps.size() && applyStamps(r, st, st.applied) && r.natVbo && !r.natInst.empty())
      glNamedBufferSubData(r.natVbo, 0, r.natInst.size() * sizeof(GrassInstance), r.natInst.data());
  }
  if (r.natMesh.count > 0 && camDist <= meshReach) op.push_back({&r.natMesh, app.instanceMatrix(i)});
  if (!stones && !r.natInst.empty() && camDist <= plantReach) {
    NatPatchDraw d; d.vbo = r.natVbo; d.count = (int)r.natInst.size(); d.mn = r.natMin; d.mx = r.natMax; d.reach = wheat ? 1.6f : 1.0f;
    app.R.natPatches.push_back(d);
  }
}
void natureDestroyRT(InstanceRT& r) {
  r.natMesh.destroy();
  if (r.natVbo) glDeleteBuffers(1, &r.natVbo);
  r.natVbo = 0; r.natCount = 0; r.natRev = 0; r.natGround = ~0u; r.natFoot = 0; r.natInst.clear(); r.natPos.clear(); r.natGy.clear();
  gFlat.erase(&r);
}
uint64_t naturePatchHash(const InstanceRT& r) {
  uint64_t h = 1469598103934665603ull;
  for (auto& g : r.natInst) { const uint32_t* w = reinterpret_cast<const uint32_t*>(&g); for (int k = 0; k < 8; ++k) h = (h ^ w[k]) * 1099511628211ull; }
  for (auto& p : r.natPos) for (int k = 0; k < 4; ++k) h = (h ^ fbits(p[k])) * 1099511628211ull;
  return h ^ (uint64_t)r.natCount;
}
size_t naturePatchItems(const InstanceRT& r) { return (size_t)r.natCount; }

// ---------------------------------------------------------------- wheat flattening
void natureBlast(App& app, vec3 p, float dmg, float radius) {
  float hg = p.y - app.terrain.height(p.x, p.z), R = std::max(radius, 1.5f) * 1.1f;
  if (hg > R) return;   // air burst too high to reach the crop
  float h = std::max(0.0f, hg);
  FlatStamp s; s.kind = 0; s.c = vec2(p.x, p.z); s.r = std::sqrt(std::max(0.25f, R * R - h * h));
  s.strength = std::clamp(dmg / 120.0f, 0.45f, 1.0f);
  s.mn = s.c - vec2(s.r * 1.15f); s.mx = s.c + vec2(s.r * 1.15f);
  pushStamp(app, s);
}
void natureTrample(App& app, vec2 c, float yaw, vec2 half, float strength) {
  FlatStamp s; s.kind = 1; s.c = c; s.yaw = yaw; s.half = half; s.strength = std::clamp(strength, 0.0f, 1.0f);
  float e = glm::length(half) + 0.6f; s.mn = c - vec2(e); s.mx = c + vec2(e);
  pushStamp(app, s);
}
void natureStepFlatten(App& app) {
  static double lastNow = -1;
  if (app.now == lastNow) return;
  lastNow = app.now;
  if (app.mode != Mode::Explore || app.tanks.tanks.empty()) return;
  for (auto& [id, t] : app.tanks.tanks) {
    if (!t.def) continue;
    auto it = gTankLast.find(id);
    if (it != gTankLast.end() && glm::length(vec2(t.pos.x - it->second.x, t.pos.z - it->second.z)) < 0.45f) continue;
    vec3 prev = it == gTankLast.end() ? t.pos : it->second;
    gTankLast[id] = t.pos;
    float y = radians(t.yaw); vec2 fwd(std::sin(y), std::cos(y)), mv(t.pos.x - prev.x, t.pos.z - prev.z);
    if (glm::length(mv) > 20.0f) continue;   // teleported / respawned: no swath
    float yaw = t.yaw + (glm::dot(mv, fwd) < 0 ? 180.0f : 0.0f);
    natureTrample(app, vec2(t.pos.x, t.pos.z), yaw, vec2(t.def->halfSize.x + 0.1f, t.def->halfSize.z + 0.15f), 1.0f);
  }
}
void natureResetFlatten() { gStamps.clear(); ++gStampEpoch; gTankLast.clear(); }
size_t natureFlattenStamps() { return gStamps.size(); }
float naturePatchFlatShare(const InstanceRT& r, float atLeast) {
  size_t n = 0, flat = 0;
  for (auto& g : r.natInst) { if (!flatSpecies(instSpecies(g))) continue; ++n; flat += flatOf(g.shape.y) >= atLeast; }
  return n ? (float)flat / (float)n : 0.0f;
}
bool natureGrassFootprint(const Instance& i, const AssetDef& a, GrassFootprint& out) {
  if (!isWheat(&a)) return false;
  float R = naturePatchParams(i, a).radius;
  out = GrassFootprint{vec2(i.pos.x, i.pos.z), i.yaw, vec2(-R + 0.15f), vec2(R - 0.15f)}; out.margin = 0.0f; out.soft = true;
  return true;
}

// ---------------------------------------------------------------- patch drawing (grass program)
void natureDrawPatches(GLuint prog, const std::vector<NatPatchDraw>& items, const mat4& vp, vec3 cam, float radius, float frac, float wind, int& draws, size_t& tufts) {
  static GLuint vao = 0, vbo = 0, ebo = 0; static int indexCount = 0;
  if (items.empty() || radius <= 0) return;
  if (!vao) {   // one plant: 7 blades (5 rows x 2) + 7 head quads (z = 10 + blade)
    std::vector<vec3> b; std::vector<uint16_t> idx;
    for (int bl = 0; bl < Grass::kPlantBlades; ++bl) {
      uint16_t base = (uint16_t)b.size();
      for (int r = 0; r <= 4; ++r) { float v = r / 4.0f; b.push_back({-1, v, (float)bl}); b.push_back({1, v, (float)bl}); }
      for (int r = 0; r < 4; ++r) { uint16_t a = base + r * 2; idx.insert(idx.end(), {a, (uint16_t)(a + 1), (uint16_t)(a + 2), (uint16_t)(a + 1), (uint16_t)(a + 3), (uint16_t)(a + 2)}); }
    }
    for (int bl = 0; bl < Grass::kPlantBlades; ++bl) {
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
  GLint lw = glGetUniformLocation(prog, "uWidthScale"), lr = glGetUniformLocation(prog, "uRadius");
  glUniform1f(lr, radius); float curR = radius;
  glUniform1f(glGetUniformLocation(prog, "uWind"), wind);
  for (auto& it : items) {
    if (!it.count || !it.vbo) continue;
    float R = radius * std::max(0.1f, it.reach), lodStart = R * 0.4f;   // wheat tiles reach further than meadow patches
    vec3 cl = glm::clamp(cam, it.mn, it.mx);
    float d = glm::length(cl - cam);
    if (d > R) continue;
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
    if (R != curR) { glUniform1f(lr, R); curR = R; }
    glUniform1f(lw, std::min(1.8f, std::pow(lod, -0.3f)));
    glVertexArrayVertexBuffer(vao, 1, it.vbo, 0, sizeof(GrassInstance));
    glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, nullptr, n);
    ++draws; tufts += (size_t)n;
  }
}
