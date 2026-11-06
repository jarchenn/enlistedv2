#include "grass.h"
#include <chrono>
#include <cstring>
#include <algorithm>
#include <cmath>

const GrassPreset& grassPreset(GrassQuality q) {
  // density = blades per m^2 drawn at full detail; radius = draw distance; lodStart = distance where
  // blade count starts thinning (remaining blades widen to keep coverage).
  static const GrassPreset P[4] = {
    {"Off", 0.0f, 0.0f, 0.0f},
    {"Low", 8.0f, 38.0f, 14.0f},
    {"Medium", 16.0f, 58.0f, 20.0f},
    {"High", 40.0f, 85.0f, 30.0f},
  };
  return P[std::clamp((int)q, 0, 3)];
}

namespace {
inline uint64_t mix64(uint64_t z) {
  z += 0x9E3779B97F4A7C15ull;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
struct Rng {
  uint64_t s;
  float f() { s = mix64(s); return (float)((s >> 40) & 0xFFFFFF) / 16777216.0f; }
};
inline uint64_t hcomb(uint64_t h, uint64_t v) { return mix64(h ^ (v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2))); }
inline uint32_t fbits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
inline int64_t ckey(int cx, int cz) { return ((int64_t)cx << 32) ^ (uint32_t)cz; }
// value noise on a 2D lattice (CPU, deterministic)
float vn2(float x, float z, uint32_t seed) {
  int xi = (int)std::floor(x), zi = (int)std::floor(z);
  float u = x - xi, v = z - zi; u = u * u * (3 - 2 * u); v = v * v * (3 - 2 * v);
  auto H = [&](int a, int b) { return (float)(mix64(((uint64_t)(uint32_t)a << 32) ^ (uint32_t)b ^ ((uint64_t)seed << 17)) & 0xFFFF) / 65535.0f; };
  return glm::mix(glm::mix(H(xi, zi), H(xi + 1, zi), u), glm::mix(H(xi, zi + 1), H(xi + 1, zi + 1), u), v);
}
static float fpMargin(const GrassFootprint& f) { return f.margin >= 0 ? f.margin : Grass::kMargin; }
// distance (m) from p to a footprint's bounds; negative inside
float fpDist(const GrassFootprint& f, float x, float z) {
  float c = std::cos(radians(f.yaw)), s = std::sin(radians(f.yaw));
  float dx = x - f.pos.x, dz = z - f.pos.y;
  // inverse yaw rotation (yawQuat rotates about +Y)
  float lx = c * dx - s * dz, lz = s * dx + c * dz;
  vec2 ctr = (f.bmin + f.bmax) * 0.5f, he = (f.bmax - f.bmin) * 0.5f;
  vec2 d = glm::abs(vec2(lx, lz) - ctr) - he;
  return glm::length(glm::max(d, vec2(0))) + std::min(std::max(d.x, d.y), 0.0f);
}
float fpRadius(const GrassFootprint& f) { return glm::length(glm::max(glm::abs(f.bmin), glm::abs(f.bmax))); }
}


// Ground as grass sees it: inside a deformation tile, a fine cell whose four samples are untouched
// (height == base, no soil) reads the base surface, so activating a tile without changing it never
// alters grass placement by even one bit.
static bool sampleTouched(const Terrain& t, const Terrain::Tile& tl, int i, int j) {
  size_t k = (size_t)j * Terrain::kFineN + i;
  if (tl.dirt[k] != 0.0f) return true;
  vec2 o = t.tileOrigin(tl.tx, tl.tz);
  float fs = t.fineSpacing();
  return tl.h[k] != t.baseHeight(o.x + i * fs, o.y + j * fs);
}
static float groundH(const Terrain& t, float x, float z) {
  if (t.tiles.empty()) return t.baseHeight(x, z);
  float ts = t.tileSize();
  int tx = (int)std::floor((x - t.org.x) / ts), tz = (int)std::floor((z - t.org.y) / ts);
  const Terrain::Tile* tl = t.tileAt(tx, tz);
  if (!tl) return t.baseHeight(x, z);
  vec2 o = t.tileOrigin(tx, tz);
  float fs = t.fineSpacing();
  int i = std::clamp((int)((x - o.x) / fs), 0, Terrain::kFine - 1), j = std::clamp((int)((z - o.y) / fs), 0, Terrain::kFine - 1);
  if (!sampleTouched(t, *tl, i, j) && !sampleTouched(t, *tl, i + 1, j) && !sampleTouched(t, *tl, i, j + 1) && !sampleTouched(t, *tl, i + 1, j + 1))
    return t.baseHeight(x, z);
  return t.height(x, z);
}
static float groundNy(const Terrain& t, float x, float z) {
  float e = t.tiles.empty() ? t.cell() : t.fineSpacing();
  return glm::normalize(vec3(groundH(t, x - e, z) - groundH(t, x + e, z), 2 * e, groundH(t, x, z - e) - groundH(t, x, z + e))).y;
}


// CPU port of the terrain shader's meadow palette (hash13 / vnoise / fbm), evaluated once per tuft
static float fractf(float x) { return x - std::floor(x); }
static float hash13(vec3 p) {
  p = vec3(fractf(p.x * 0.1031f), fractf(p.y * 0.1031f), fractf(p.z * 0.1031f));
  float d = glm::dot(p, vec3(p.z, p.y, p.x) + 31.32f);
  p += d;
  return fractf((p.x + p.y) * p.z);
}
static float vnoise3(vec3 p) {
  vec3 i = glm::floor(p), f = p - i; f = f * f * (3.0f - 2.0f * f);
  float a = hash13(i), b = hash13(i + vec3(1, 0, 0)), c = hash13(i + vec3(0, 1, 0)), d = hash13(i + vec3(1, 1, 0));
  float e = hash13(i + vec3(0, 0, 1)), g = hash13(i + vec3(1, 0, 1)), h = hash13(i + vec3(0, 1, 1)), k = hash13(i + vec3(1, 1, 1));
  return glm::mix(glm::mix(glm::mix(a, b, f.x), glm::mix(c, d, f.x), f.y), glm::mix(glm::mix(e, g, f.x), glm::mix(h, k, f.x), f.y), f.z);
}
static float fbm3(vec3 p) { float s = 0, a = 0.5f; for (int i = 0; i < 4; ++i) { s += a * vnoise3(p); p *= 2.03f; a *= 0.5f; } return s; }
static float smooth(float a, float b, float x) { float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3 - 2 * t); }
static float packAlbedo(float x, float z, float var, int species = 1) {
  // muted meadow palette by plant cluster (not per blade): broad fields, patches, then the species' own tint
  float n1 = fbm3(vec3(x * 0.025f, z * 0.025f, 1.0f)), n2 = fbm3(vec3(x * 0.18f, z * 0.18f, 2.0f));
  vec3 g1(0.12f, 0.19f, 0.055f), g2(0.19f, 0.25f, 0.075f), dry(0.30f, 0.28f, 0.15f), olive(0.20f, 0.22f, 0.09f);
  vec3 alb = glm::mix(g1, g2, smooth(0.3f, 0.7f, n1));
  alb = glm::mix(alb, olive, smooth(0.45f, 0.7f, n2) * 0.35f);
  alb = glm::mix(alb, dry, smooth(0.62f, 0.8f, n2) * 0.45f + var * 0.10f);
  if (species == 0) alb *= vec3(0.92f, 1.0f, 0.9f);                       // turf: a touch deeper green
  if (species == 2) alb = glm::mix(alb, vec3(0.27f, 0.27f, 0.13f), 0.35f); // tall stems: paler, yellowing
  if (species == 3) alb = glm::mix(alb, vec3(0.10f, 0.17f, 0.06f), 0.55f); // clover / weeds: dark broadleaf green
  if (species == 4) alb = glm::mix(alb, vec3(0.40f, 0.35f, 0.19f), 0.75f); // dry straw
  alb *= 0.82f + 0.36f * var;
  auto q = [](float v) { return (uint32_t)std::clamp((int)std::lround(v * 2.0f * 255.0f), 0, 255); };
  uint32_t u = q(alb.r) | (q(alb.g) << 8) | (q(alb.b) << 16) | ((0x38u + (uint32_t)std::clamp(species, 0, 4)) << 24);   // never a NaN pattern
  float f; std::memcpy(&f, &u, 4); return f;
}

float Grass::footprintDistance(const GrassFootprint& f, float x, float z) { return fpDist(f, x, z); }

void Grass::ensureGL() {
  if (vao) return;
  // one tuft: kBlades blades, each 5 rows x 2 columns; x = side (-1/1), y = height fraction, z = blade index
  std::vector<vec3> b; std::vector<uint16_t> idx;
  for (int bl = 0; bl < kBlades; ++bl) {
    uint16_t base = (uint16_t)b.size();
    for (int r = 0; r <= 4; ++r) { float v = r / 4.0f; b.push_back({-1, v, (float)bl}); b.push_back({1, v, (float)bl}); }
    for (int r = 0; r < 4; ++r) {
      uint16_t a = base + r * 2;
      idx.insert(idx.end(), {a, (uint16_t)(a + 1), (uint16_t)(a + 2), (uint16_t)(a + 1), (uint16_t)(a + 3), (uint16_t)(a + 2)});
    }
  }
  indexCount = (int)idx.size();
  glCreateBuffers(1, &bladeVbo); glCreateBuffers(1, &bladeEbo);
  glNamedBufferData(bladeVbo, b.size() * sizeof(vec3), b.data(), GL_STATIC_DRAW);
  glNamedBufferData(bladeEbo, idx.size() * sizeof(uint16_t), idx.data(), GL_STATIC_DRAW);
  glCreateVertexArrays(1, &vao);
  glVertexArrayVertexBuffer(vao, 0, bladeVbo, 0, sizeof(vec3));
  glVertexArrayElementBuffer(vao, bladeEbo);
  glEnableVertexArrayAttrib(vao, 0); glVertexArrayAttribBinding(vao, 0, 0);
  glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
  for (int k = 1; k <= 2; ++k) {
    glEnableVertexArrayAttrib(vao, k); glVertexArrayAttribBinding(vao, k, 1);
    glVertexArrayAttribFormat(vao, k, 4, GL_FLOAT, GL_FALSE, (k - 1) * sizeof(vec4));
  }
  glVertexArrayBindingDivisor(vao, 1, 1);
}

void Grass::clear() {
  for (auto& [k, c] : chunks) if (c.vbo) glDeleteBuffers(1, &c.vbo);
  chunks.clear();
}

bool Grass::density(float x, float z, const Terrain& t, const std::vector<GrassFootprint>& fp) const {
  if (x < t.org.x + 1 || z < t.org.y + 1 || x > t.org.x + t.size - 1 || z > t.org.y + t.sizeZ - 1) return false;
  float y = groundH(t, x, z);
  if (y < t.waterLevel + 0.12f) return false;                 // water / shoreline
  if (groundNy(t, x, z) < 0.80f) return false;                // steep banks and crater walls
  if (t.dirtAt(x, z) > 0.18f) return false;                  // disturbed soil
  if (t.wearAt(x, z) > 0.62f) return false;                  // worn track / foundation soil
  for (auto& f : fp) {
    if (glm::length(vec2(x, z) - f.pos) > fpRadius(f) + kMargin + 1) continue;
    if (fpDist(f, x, z) < fpMargin(f)) return false;              // footprints, steps, thresholds, berms
  }
  return true;
}

uint64_t Grass::signature(int cx, int cz, const Terrain& t, const std::vector<GrassFootprint>& fp) const {
  uint64_t h = hcomb(hcomb(hcomb(hcomb(seed_, t.generation), t.surfSig), (uint32_t)cx), (uint32_t)cz);
  float ox = cx * kChunk, oz = cz * kChunk;
  // deformation tiles under (and one fine sample around) the chunk
  if (!t.tiles.empty()) {
    float ts = t.tileSize(), hx = -t.org.x, hz = -t.org.y;
    int tx0 = (int)std::floor((ox - 1 + hx) / ts), tx1 = (int)std::floor((ox + kChunk + 1 + hx) / ts);
    int tz0 = (int)std::floor((oz - 1 + hz) / ts), tz1 = (int)std::floor((oz + kChunk + 1 + hz) / ts);
    for (int tz = tz0; tz <= tz1; ++tz) for (int tx = tx0; tx <= tx1; ++tx)
      if (const Terrain::Tile* tl = t.tileAt(tx, tz)) h = hcomb(hcomb(h, (uint64_t)t.key(tx, tz)), tl->rev);
  }
  // footprints that can reach the chunk
  vec2 cc(ox + kChunk * 0.5f, oz + kChunk * 0.5f);
  for (auto& f : fp) {
    if (glm::length(f.pos - cc) > fpRadius(f) + kMargin + kChunk * 0.75f) continue;
    h = hcomb(h, fbits(f.pos.x)); h = hcomb(h, fbits(f.pos.y)); h = hcomb(h, fbits(f.yaw));
    h = hcomb(h, fbits(f.bmin.x)); h = hcomb(h, fbits(f.bmin.y)); h = hcomb(h, fbits(f.bmax.x)); h = hcomb(h, fbits(f.bmax.y));
  }
  return h;
}

uint64_t Grass::contentSignature(int cx, int cz, const Terrain& t, const std::vector<GrassFootprint>& fp) const {
  uint64_t h = hcomb(hcomb(hcomb(seed_, t.generation), (uint32_t)cx), (uint32_t)cz);
  float ox = cx * kChunk, oz = cz * kChunk;
  vec2 cc(ox + kChunk * 0.5f, oz + kChunk * 0.5f);
  for (auto& f : fp) {
    if (glm::length(f.pos - cc) > fpRadius(f) + kMargin + kChunk * 0.75f) continue;
    h = hcomb(h, fbits(f.pos.x)); h = hcomb(h, fbits(f.pos.y)); h = hcomb(h, fbits(f.yaw));
    h = hcomb(h, fbits(f.bmin.x)); h = hcomb(h, fbits(f.bmin.y)); h = hcomb(h, fbits(f.bmax.x)); h = hcomb(h, fbits(f.bmax.y));
  }
  if (!t.surf.empty()) {   // wear texels under the chunk: a route / foundation edit rebuilds only the chunks it touches
    const int R = t.surfW; float hx = -t.org.x, hz = -t.org.y, tx = t.size / t.surfW;
    int x0 = std::max(0, (int)((ox - 1 + hx) / tx)), x1 = std::min(R - 1, (int)((ox + kChunk + 1 + hx) / tx));
    int y0 = std::max(0, (int)((oz - 1 + hz) / tx)), y1 = std::min(t.surfH - 1, (int)((oz + kChunk + 1 + hz) / tx));
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) h = hcomb(h, t.surf[((size_t)y * R + x) * 4]);
  }
  if (t.tiles.empty()) return h;
  // every fine sample that can influence a blade in this chunk (heights, normals, soil): bounds + 2 samples
  float ts = t.tileSize(), hx = -t.org.x, hz = -t.org.y, fs = t.fineSpacing(), pad = 2 * fs;
  int tx0 = (int)std::floor((ox - pad + hx) / ts), tx1 = (int)std::floor((ox + kChunk + pad + hx) / ts);
  int tz0 = (int)std::floor((oz - pad + hz) / ts), tz1 = (int)std::floor((oz + kChunk + pad + hz) / ts);
  for (int tz = tz0; tz <= tz1; ++tz) for (int tx = tx0; tx <= tx1; ++tx) {
    const Terrain::Tile* tl = t.tileAt(tx, tz);
    if (!tl) continue;
    vec2 o = t.tileOrigin(tx, tz);
    int i0 = std::max(0, (int)std::floor((ox - pad - o.x) / fs)), i1 = std::min(Terrain::kFine, (int)std::ceil((ox + kChunk + pad - o.x) / fs));
    int j0 = std::max(0, (int)std::floor((oz - pad - o.y) / fs)), j1 = std::min(Terrain::kFine, (int)std::ceil((oz + kChunk + pad - o.y) / fs));
    if (i0 > i1 || j0 > j1) continue;
    for (int j = j0; j <= j1; ++j) for (int i = i0; i <= i1; ++i) {
      if (!sampleTouched(t, *tl, i, j)) continue;          // untouched samples read as base ground
      size_t k = (size_t)j * Terrain::kFineN + i;
      h = hcomb(hcomb(h, ((uint64_t)t.key(tx, tz) << 16) ^ k), ((uint64_t)fbits(tl->h[k]) << 32) | fbits(tl->dirt[k]));
    }
  }
  return h;
}

void Grass::build(Chunk& c, const Terrain& t, const std::vector<GrassFootprint>& fp) {
  c.inst.clear();
  float ox = c.cx * kChunk, oz = c.cz * kChunk;
  // only footprints near this chunk participate (keeps the per-blade test cheap)
  std::vector<GrassFootprint> near;
  vec2 cc(ox + kChunk * 0.5f, oz + kChunk * 0.5f);
  for (auto& f : fp) if (glm::length(f.pos - cc) <= fpRadius(f) + kMargin + kChunk * 0.75f) near.push_back(f);
  int N = (int)(kMaxDensity * kChunk * kChunk);
  c.ymin = 1e9f; c.ymax = -1e9f;
  for (int i = 0; i < N; ++i) {
    Rng r{hcomb(hcomb(hcomb(seed_ * 7919ull, (uint32_t)c.cx), (uint32_t)c.cz), (uint64_t)i)};
    float x = ox + r.f() * kChunk, z = oz + r.f() * kChunk;
    float keep = r.f(), thin = r.f(), var = r.f();
    // meadow patchiness: sparse and lush areas, plus occasional bare spots
    float patch = vn2(x * 0.09f, z * 0.09f, seed_), fine = vn2(x * 0.6f, z * 0.6f, seed_ + 11);
    if (keep > 0.30f + 0.70f * patch) continue;
    if (fine < 0.12f) continue;
    if (!density(x, z, t, near)) continue;
    // thinning band next to footprints and around craters
    float d = t.dirtAt(x, z);
    if (d > 0.02f && thin < d * 5.5f) continue;
    bool skip = false;
    for (auto& f : near) { float fd = fpDist(f, x, z) - fpMargin(f); if (fd < 0.9f && thin > fd / 0.9f) { skip = true; break; } }
    if (skip) continue;
    // routes: thinned and cropped short toward the track, tall rank verges just beyond its edge
    float wear = t.wearAt(x, z);
    if (wear > 0.05f && thin < wear * 1.5f) continue;
    float verge = std::clamp(1.0f - std::fabs(wear - 0.08f) / 0.08f, 0.0f, 1.0f);
    // weeds against walls and foundations: taller, darker clumps
    float wall = 0; for (auto& f : near) { float fd = fpDist(f, x, z) - fpMargin(f); if (fd < 2.2f) wall = std::max(wall, 1.0f - fd / 2.2f); }
    float y = groundH(t, x, z);
    // species from correlated fields: tall meadow in broad patches (and on neglected verges / wall feet), clover in
    // small clumps, straw where the sward dries out, short turf as the dense base everywhere
    float nTall = vn2(x * 0.06f, z * 0.06f, seed_ + 21), nClo = vn2(x * 0.35f, z * 0.35f, seed_ + 31), nDry = vn2(x * 0.12f, z * 0.12f, seed_ + 41);
    float rs = r.f(), r1 = r.f(), r2 = r.f();
    float pTall = 0.03f + 0.45f * smooth(0.55f, 0.82f, nTall) + 0.35f * verge + 0.3f * wall;
    float pClo = 0.10f * smooth(0.6f, 0.85f, nClo), pStraw = 0.07f * smooth(0.62f, 0.82f, nDry), pTurf = 0.5f * (1.0f - 0.6f * smooth(0.55f, 0.82f, nTall));
    if (wear > 0.05f) { pTall *= 0.2f; pTurf = 0.8f; }   // trodden: cropped short
    int sp = rs < pTall ? 2 : rs < pTall + pClo ? 3 : rs < pTall + pClo + pStraw ? 4 : rs < pTall + pClo + pStraw + pTurf ? 0 : 1;
    float hgt, wid;
    switch (sp) {
      case 0: hgt = 0.09f + 0.10f * r1; wid = 0.016f + 0.006f * r2; break;
      case 2: hgt = 0.55f + 0.40f * r1; wid = 0.0065f + 0.0035f * r2; break;
      case 3: hgt = 0.05f + 0.04f * r1; wid = 0.028f + 0.012f * r2; break;
      case 4: hgt = 0.25f + 0.25f * r1; wid = 0.012f + 0.004f * r2; break;
      default: hgt = (0.26f + 0.28f * r1) * (0.7f + 0.6f * patch); wid = 0.018f + 0.008f * r2; break;
    }
    hgt *= (1.0f - 0.6f * std::min(wear * 2.0f, 1.0f)) * (1.0f + 0.5f * verge + 0.4f * wall * patch);
    if (verge > 0.2f || wall > 0.3f) var = std::min(1.0f, var * 0.6f);
    float phase = r.f() * 6.2831853f, yaw = r.f() * 6.2831853f;
    c.inst.push_back({vec4(x, y, z, yaw), vec4(hgt, wid, packAlbedo(x, z, var, sp), phase)});
    c.ymin = std::min(c.ymin, y); c.ymax = std::max(c.ymax, y + hgt);
  }
  c.count = (int)c.inst.size();
  if (!c.vbo) glCreateBuffers(1, &c.vbo);
  glNamedBufferData(c.vbo, std::max<size_t>(1, c.inst.size()) * sizeof(GrassInstance), c.inst.empty() ? nullptr : c.inst.data(), GL_STATIC_DRAW);
}

void Grass::update(const Terrain& t, uint32_t seed, const std::vector<GrassFootprint>& fp, vec3 cam, double budgetMs) {
  auto t0 = std::chrono::high_resolution_clock::now();
  ++frame;
  lastBuilt = 0; pending = 0;
  if (seed != seed_ || t.size != terrainSize || t.sizeZ != terrainSizeZ) { clear(); seed_ = seed; terrainSize = t.size; terrainSizeZ = t.sizeZ; }
  const GrassPreset& P = grassPreset(quality);
  if (P.radius <= 0) { clear(); lastBuildMs = 0; return; }
  ensureGL();
  float camH = cam.y - t.height(cam.x, cam.z);
  if (camH > P.radius) { lastBuildMs = 0; return; }           // flying high: nothing visible to build
  float R = P.radius + kChunk;
  int x0 = (int)std::floor((cam.x - R) / kChunk), x1 = (int)std::floor((cam.x + R) / kChunk);
  int z0 = (int)std::floor((cam.z - R) / kChunk), z1 = (int)std::floor((cam.z + R) / kChunk);
  struct Todo { float d; int cx, cz; uint64_t sig; };
  std::vector<Todo> todo;
  for (int cz = z0; cz <= z1; ++cz) for (int cx = x0; cx <= x1; ++cx) {
    vec2 cc((cx + 0.5f) * kChunk, (cz + 0.5f) * kChunk);
    float d = glm::length(cc - vec2(cam.x, cam.z));
    if (d > R) continue;
    if (cc.x < t.org.x - kChunk || cc.y < t.org.y - kChunk || cc.x > t.org.x + t.size + kChunk || cc.y > t.org.y + t.sizeZ + kChunk) continue;
    uint64_t sig = signature(cx, cz, t, fp);
    auto it = chunks.find(ckey(cx, cz));
    if (it != chunks.end()) {
      it->second.lastUsed = frame;
      if (it->second.sig == sig) continue;
      uint64_t content = contentSignature(cx, cz, t, fp);
      if (content == it->second.content) { it->second.sig = sig; continue; }   // tiles remeshed, ground unchanged here
    }
    todo.push_back({d, cx, cz, sig});
  }
  std::sort(todo.begin(), todo.end(), [](const Todo& a, const Todo& b) { return a.d < b.d || (a.d == b.d && (a.cz < b.cz || (a.cz == b.cz && a.cx < b.cx))); });
  for (auto& w : todo) {
    double el = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
    if (budgetMs >= 0 && lastBuilt > 0 && el > budgetMs) { ++pending; continue; }
    Chunk& c = chunks[ckey(w.cx, w.cz)];
    c.cx = w.cx; c.cz = w.cz; c.sig = w.sig; c.content = contentSignature(w.cx, w.cz, t, fp); c.lastUsed = frame;
    build(c, t, fp);
    ++lastBuilt;
  }
  // evict chunks that have been out of range for a while
  for (auto it = chunks.begin(); it != chunks.end();) {
    if (frame - it->second.lastUsed > 90) { if (it->second.vbo) glDeleteBuffers(1, &it->second.vbo); it = chunks.erase(it); }
    else ++it;
  }
  lastBuildMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
  if (lastBuilt) peakBuildMs = std::max(peakBuildMs, lastBuildMs);
}

void Grass::draw(GLuint prog, const mat4& vp, vec3 cam) const {
  lastDrawnChunks = 0; lastDrawnBlades = 0;
  const GrassPreset& P = grassPreset(quality);
  if (P.radius <= 0 || !vao || chunks.empty()) return;
  glBindVertexArray(vao);
  GLint lw = glGetUniformLocation(prog, "uWidthScale");
  glUniform1f(glGetUniformLocation(prog, "uRadius"), P.radius);
  glUniform1f(glGetUniformLocation(prog, "uWind"), wind ? 1.0f : 0.0f);
  float frac = P.density / kMaxDensity;
  for (auto& [k, c] : chunks) {
    if (!c.count) continue;
    vec3 mn(c.cx * kChunk, c.ymin, c.cz * kChunk), mx(mn.x + kChunk, c.ymax + 0.3f, mn.z + kChunk);
    vec3 cl = glm::clamp(cam, mn, mx);
    float d = glm::length(cl - cam);
    if (d > P.radius) continue;
    // frustum: reject when all 8 corners are outside one clip plane
    bool out = false;
    for (int pl = 0; pl < 6 && !out; ++pl) {
      int axis = pl >> 1; float sgn = (pl & 1) ? -1.0f : 1.0f; int inside = 0;
      for (int q = 0; q < 8; ++q) {
        vec4 p = vp * vec4((q & 1) ? mx.x : mn.x, (q & 2) ? mx.y : mn.y, (q & 4) ? mx.z : mn.z, 1.0f);
        if (sgn * p[axis] <= p.w) ++inside;
      }
      out = inside == 0;
    }
    if (out) continue;
    float lod = d <= P.lodStart ? 1.0f : std::clamp((P.lodStart / d) * (P.lodStart / d), 0.12f, 1.0f);
    int n = (int)std::ceil(c.count * frac * lod);
    if (n <= 0) continue;
    // far chunks also draw fewer blades per tuft (index prefix) and widen what remains moderately
    int blades = lod > 0.5f ? kBlades : lod > 0.22f ? 3 : 2;
    glUniform1f(lw, std::min(2.2f, std::pow(lod * blades / (float)kBlades, -0.3f)));
    glVertexArrayVertexBuffer(vao, 1, c.vbo, 0, sizeof(GrassInstance));
    glDrawElementsInstanced(GL_TRIANGLES, indexCount / kBlades * blades, GL_UNSIGNED_SHORT, nullptr, n);
    ++lastDrawnChunks; lastDrawnBlades += (size_t)n * blades;
  }
}

size_t Grass::instanceCount() const { size_t n = 0; for (auto& [k, c] : chunks) n += c.count; return n; }
size_t Grass::gpuBytes() const { return instanceCount() * sizeof(GrassInstance) * 2 + (vao ? 80 : 0); }   // VBO + CPU mirror
uint64_t Grass::chunkHash(int cx, int cz) const {
  auto it = chunks.find(ckey(cx, cz));
  if (it == chunks.end()) return 0;
  uint64_t h = 1469598103934665603ull;
  for (auto& g : it->second.inst) {
    const uint32_t* w = reinterpret_cast<const uint32_t*>(&g);
    for (int k = 0; k < 8; ++k) h = (h ^ w[k]) * 1099511628211ull;
  }
  return h ^ (uint64_t)it->second.count;
}
uint64_t Grass::layoutHash() const {
  uint64_t h = 0;
  for (auto& [k, c] : chunks) h = hcomb(h, chunkHash(c.cx, c.cz));
  return h;
}
