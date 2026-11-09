#include "grass.h"
#include "ground_paint.h"                     // lasso ground paint: grass density, painted colour, water
#include <chrono>
#include <cstring>
#include <algorithm>
#include <cmath>

const GrassPreset& grassPreset(GrassQuality q) {
  // GX3 tiers (content/settings/grass.json overrides): Low is about the pre-GX3 Medium cost, Medium the same frame
  // time at ~3.5x the near blade density, bought with a shorter radius and sparse mid / far clumps.
  static GrassPreset P[4] = {
    {"Off", 0, 0, 0, 0, 2, 0.1f, 0, 0, 0, 0},
    {"Low", 12, 36, 10, 22, 2.0f, 0.12f, 5, 3, 0, 0},
    {"Medium", 24, 48, 15, 32, 1.6f, 0.14f, 6, 3, 13, 10},
    {"High", 30, 54, 18, 40, 1.8f, 0.12f, 8, 4, 15, 14},
  };
  static bool loaded = false;
  if (!loaded) {
    loaded = true;
    try {
      fs::path p = contentDir() / "settings" / "grass.json";
      if (fs::exists(p)) {
        json j = json::parse(readTextFile(p));
        for (int k = 1; k < 4; ++k) {
          if (!j.contains("tiers") || !j["tiers"].contains(P[k].name)) continue;
          const json& t = j["tiers"][P[k].name]; GrassPreset& g = P[k];
          g.density = std::clamp(t.value("density", g.density), 0.0f, Grass::kMaxDensity);
          g.radius = std::clamp(t.value("radius", g.radius), 5.0f, 150.0f);
          g.nearR = std::clamp(t.value("nearRadius", g.nearR), 1.0f, g.radius);
          g.midR = std::clamp(t.value("midRadius", g.midR), g.nearR, g.radius);
          g.falloff = std::clamp(t.value("falloff", g.falloff), 0.5f, 4.0f);
          g.floor = std::clamp(t.value("floor", g.floor), 0.02f, 1.0f);
          g.midBlades = std::clamp(t.value("midBlades", g.midBlades), 1, Grass::kBlades);
          g.farBlades = std::clamp(t.value("farBlades", g.farBlades), 1, g.midBlades);
          g.windR = std::clamp(t.value("windRadius", g.windR), 0.0f, g.radius);
          g.shadowR = std::clamp(t.value("shadowRadius", g.shadowR), 0.0f, g.radius);
        }
      }
    } catch (std::exception& e) { logf("grass: settings/grass.json: %s (defaults)", e.what()); }
  }
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
  if (tl.dirt[k] != 0.0f || (!tl.sdirt.empty() && tl.sdirt[k] != 0.0f)) return true;   // GX1 stamp soil too
  vec2 o = t.tileOrigin(tl.tx, tl.tz);
  float fs = t.fineSpacing();
  return tl.h[k] != t.baseHeight(o.x + i * fs, o.y + j * fs);
}
static bool groundTouched(const Terrain& t, float x, float z) {   // the fine cell at (x, z) differs from the base ground
  if (t.tiles.empty()) return false;
  float ts = t.tileSize();
  int tx = (int)std::floor((x - t.org.x) / ts), tz = (int)std::floor((z - t.org.y) / ts);
  const Terrain::Tile* tl = t.tileAt(tx, tz);
  if (!tl) return false;
  vec2 o = t.tileOrigin(tx, tz);
  float fs = t.fineSpacing();
  int i = std::clamp((int)((x - o.x) / fs), 0, Terrain::kFine - 1), j = std::clamp((int)((z - o.y) / fs), 0, Terrain::kFine - 1);
  return sampleTouched(t, *tl, i, j) || sampleTouched(t, *tl, i + 1, j) || sampleTouched(t, *tl, i, j + 1) || sampleTouched(t, *tl, i + 1, j + 1);
}
static float groundH(const Terrain& t, float x, float z) { return groundTouched(t, x, z) ? t.height(x, z) : t.baseHeight(x, z); }
static vec3 groundN(const Terrain& t, float x, float z) {
  // one fixed step whatever the tile state: untouched ground gives bit-identical normals (and clump tilt) whether or
  // not deformation tiles exist, so undo / reset restore the same grass
  float e = t.fineSpacing();
  return glm::normalize(vec3(groundH(t, x - e, z) - groundH(t, x + e, z), 2 * e, groundH(t, x, z - e) - groundH(t, x, z + e)));
}


// CPU port of the terrain shader's meadow palette (hash13 / vnoise / fbm), evaluated once per clump
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
static vec2 rot(float x, float z) { return vec2(0.8f * x + 0.6f * z, -0.6f * x + 0.8f * z); }   // GLSL mat2(0.8, -0.6, 0.6, 0.8) * p
// natural bare-earth patches: the same field kTerrainFS turns to soil (bare), so thinned clumps stand on visible earth
static float bareField(float x, float z) { vec2 q = rot(x, z); return smooth(0.62f, 0.8f, fbm3(vec3(q.x * 0.8f, q.y * 0.8f, 17.0f))); }
// the ground under a clump as kTerrainFS paints it (near field, before the L4 detail): the base of every card
// blends into it, so grass and ground never disagree at the root
// broad noise fields (metres-scale) sampled once per chunk on a 1 m lattice and interpolated (a crater rebuild stays cheap)
struct BroadNoise {
  static constexpr int N = (int)Grass::kChunk + 2;
  float ox = 0, oz = 0, v[4][N * N];   // 0 nB, 1 nM (terrain palette), 2 n1, 3 n2 (clump palette)
  void build(float x0, float z0) {
    ox = x0; oz = z0;
    for (int j = 0; j < N; ++j) for (int i = 0; i < N; ++i) {
      float x = x0 + i, z = z0 + j; int k = j * N + i;
      v[0][k] = fbm3(vec3(x * 0.012f, z * 0.012f, 1.0f)); v[1][k] = fbm3(vec3(x * 0.07f, z * 0.07f, 2.0f));
      v[2][k] = fbm3(vec3(x * 0.025f, z * 0.025f, 1.0f)); v[3][k] = fbm3(vec3(x * 0.18f, z * 0.18f, 2.0f));
    }
  }
  float at(int f, float x, float z) const {
    float u = std::clamp(x - ox, 0.0f, N - 1.001f), w = std::clamp(z - oz, 0.0f, N - 1.001f);
    int i = (int)u, j = (int)w; u -= i; w -= j;
    const float* a = v[f] + j * N + i;
    return glm::mix(glm::mix(a[0], a[1], u), glm::mix(a[N], a[N + 1], u), w);
  }
};
static vec3 groundColour(const Terrain& t, const BroadNoise& bn, float x, float z, float bare) {
  float nB = bn.at(0, x, z), nM = bn.at(1, x, z), nF = vnoise3(vec3(x * 0.9f, z * 0.9f, 3.0f));
  vec3 g = glm::mix(vec3(0.12f, 0.19f, 0.06f), vec3(0.21f, 0.26f, 0.085f), smooth(0.3f, 0.7f, nB));
  g = glm::mix(g, vec3(0.32f, 0.30f, 0.165f), smooth(0.6f, 0.82f, nM) * 0.5f);
  g *= 0.78f + 0.32f * nF;
  g *= 1.0f - 0.16f * smooth(0.65f, 0.8f, vnoise3(vec3(x * 0.9f, z * 0.9f, 9.0f)));
  vec2 q = rot(x, z) * 3.1f;
  g *= glm::mix(0.5f, 0.85f, vnoise3(vec3(q.x, q.y, 19.0f)));
  vec3 soil = glm::mix(vec3(0.20f, 0.155f, 0.10f), vec3(0.28f, 0.22f, 0.145f), nF);
  g = glm::mix(g, soil, bare * 0.65f);
  float wear = t.wearAt(x, z), dirt = t.dirtAt(x, z);
  g = glm::mix(g, soil, smooth(0.03f, 0.4f, std::max(wear * 1.6f, dirt * 2.5f)));   // exactly 0 for the traces an undo leaves
  vec3 pc; if (float pw = groundPaintTint(x, z, pc); pw > 0.001f) g = glm::mix(g, pc * 0.8f, pw);   // painted path, dry grass, mud...
  return g;
}
static uint32_t packRGB(vec3 c, uint32_t top) {   // x2 so 0..0.5 albedo uses the full byte; top byte 0x38.. (never NaN)
  auto q = [](float v) { return (uint32_t)std::clamp((int)std::lround(v * 2.0f * 255.0f), 0, 255); };
  return q(c.r) | (q(c.g) << 8) | (q(c.b) << 16) | (top << 24);
}
static float asFloat(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
static float packAlbedo(const BroadNoise& bn, float x, float z, float var, float tint, int species) {
  // muted meadow palette by plant cluster (not per blade): broad fields, patches, then the species' own tint
  float n1 = bn.at(2, x, z), n2 = bn.at(3, x, z);
  vec3 g1(0.12f, 0.19f, 0.055f), g2(0.19f, 0.25f, 0.075f), dry(0.30f, 0.28f, 0.15f), olive(0.20f, 0.22f, 0.09f);
  vec3 alb = glm::mix(g1, g2, smooth(0.3f, 0.7f, n1));
  alb = glm::mix(alb, olive, smooth(0.45f, 0.7f, n2) * 0.35f);
  alb = glm::mix(alb, dry, smooth(0.62f, 0.8f, n2) * 0.45f + var * 0.10f);
  // per-clump tint along green -> yellow-green -> straw (tint 0..1; most clumps stay green)
  vec3 yg = alb * vec3(1.32f, 1.16f, 0.72f) + vec3(0.02f, 0.015f, 0.0f), straw(0.40f, 0.35f, 0.19f);
  alb = tint < 0.5f ? glm::mix(alb, yg, tint * 2.0f) : glm::mix(yg, straw, (tint - 0.5f) * 1.6f);
  if (species == 0) alb *= vec3(0.92f, 1.0f, 0.9f);                       // turf: a touch deeper green
  if (species == 1) alb *= vec3(0.92f, 1.04f, 0.86f);                     // meadow grass: lush
  if (species == 2) alb = glm::mix(alb, vec3(0.27f, 0.27f, 0.13f), 0.30f); // tall seed-head grass: paler, yellowing
  if (species == 3) alb = glm::mix(alb, vec3(0.10f, 0.19f, 0.07f), 0.60f); // clover: fresh broadleaf green
  if (species == 4) alb = glm::mix(alb, vec3(0.40f, 0.35f, 0.19f), 0.75f); // dry straw
  if (species == 16) alb = glm::mix(alb, vec3(0.08f, 0.14f, 0.05f), 0.65f); // shrub: dark leathery leaves
  if (species == 17) alb = glm::mix(alb, vec3(0.11f, 0.18f, 0.07f), 0.55f); // plantain / dock: dark broad leaves
  alb *= 0.82f + 0.36f * var;
  return asFloat(packRGB(alb, 0x38u + (uint32_t)species));
}

float Grass::footprintDistance(const GrassFootprint& f, float x, float z) { return fpDist(f, x, z); }
int Grass::speciesOf(const GrassInstance& g) { return (int)(fbits(g.shape.z) >> 24) - 0x38; }

void Grass::ensureGL() {
  if (vao) return;
  // one clump: kBlades cards; x = side (-1/1), y = height fraction, z = card index. The first 8 cards (the tall
  // centre, kept by every LOD) have 4 segments, the short rim cards 2.
  std::vector<vec3> b; std::vector<uint16_t> idx;
  for (int bl = 0; bl < kBlades; ++bl) {
    int seg = bl < 8 ? 4 : 2;
    uint16_t base = (uint16_t)b.size();
    for (int r = 0; r <= seg; ++r) { float v = r / (float)seg; b.push_back({-1, v, (float)bl}); b.push_back({1, v, (float)bl}); }
    for (int r = 0; r < seg; ++r) {
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
static int cardIndexEnd(int nb) { int n = 0; for (int k = 0; k < nb; ++k) n += (k < 8 ? 4 : 2) * 6; return n; }   // index prefix of nb cards

void Grass::clear() {
  for (auto& [k, c] : chunks) if (c.vbo) glDeleteBuffers(1, &c.vbo);
  chunks.clear();
}

bool Grass::density(float x, float z, const Terrain& t, const std::vector<GrassFootprint>& fp) const {
  if (x < t.org.x + 1 || z < t.org.y + 1 || x > t.org.x + t.size - 1 || z > t.org.y + t.sizeZ - 1) return false;
  float y = groundH(t, x, z);
  if (y < t.waterLevel + 0.12f) return false;                 // water / shoreline
  if (groundN(t, x, z).y < 0.80f) return false;               // steep banks and crater walls
  if (t.dirtAt(x, z) > 0.18f) return false;                  // disturbed soil
  if (t.wearAt(x, z) > 0.62f) return false;                  // worn track / foundation soil
  if (y < lassoWaterAtCurrent(x, z) + 0.12f) return false;   // lasso water bodies
  if (float gp = groundPaintGrass(x, z); gp < 0.999f) {      // lasso ground paint thins / removes the sward
    uint32_t hx; float fx = x * 7.31f + z * 3.17f; std::memcpy(&hx, &fx, 4); hx ^= hx >> 15; hx *= 0x2c1b3c6du; hx ^= hx >> 12;
    if ((hx & 0xffff) / 65536.0f >= gp) return false;
  }
  for (auto& f : fp) {
    if (glm::length(vec2(x, z) - f.pos) > fpRadius(f) + kMargin + 1) continue;
    if (fpDist(f, x, z) < fpMargin(f)) return false;              // footprints, steps, thresholds, berms
  }
  return true;
}

uint64_t Grass::signature(int cx, int cz, const Terrain& t, const std::vector<GrassFootprint>& fp) const {
  uint64_t h = hcomb(hcomb(hcomb(hcomb(hcomb(seed_, t.generation), t.surfSig), (uint32_t)cx), (uint32_t)cz), lassoGroundRev());   // + lasso paint / water
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
  h = hcomb(h, lassoGroundSig(vec2(ox - 1, oz - 1), vec2(ox + kChunk + 1, oz + kChunk + 1)));   // lasso paint / water under the chunk
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
      h = hcomb(hcomb(h, ((uint64_t)t.key(tx, tz) << 16) ^ k), ((uint64_t)fbits(tl->h[k]) << 32) | fbits(tl->sdirt.empty() ? tl->dirt[k] : std::max(tl->dirt[k], tl->sdirt[k])));
    }
  }
  return h;
}

void Grass::build(Chunk& c, const Terrain& t, const std::vector<GrassFootprint>& fp) {
  c.inst.clear();
  float ox = c.cx * kChunk, oz = c.cz * kChunk;
  // only footprints near this chunk participate (keeps the per-clump test cheap)
  std::vector<GrassFootprint> near;
  vec2 cc(ox + kChunk * 0.5f, oz + kChunk * 0.5f);
  for (auto& f : fp) if (glm::length(f.pos - cc) <= fpRadius(f) + kMargin + kChunk * 0.75f) near.push_back(f);
  int N = (int)(kMaxDensity * kChunk * kChunk);
  c.ymin = 1e9f; c.ymax = -1e9f;
  static thread_local BroadNoise bn; bn.build(ox - 1, oz - 1);
  for (int i = 0; i < N; ++i) {
    Rng r{hcomb(hcomb(hcomb(seed_ * 7919ull, (uint32_t)c.cx), (uint32_t)c.cz), (uint64_t)i)};
    float x = ox + r.f() * kChunk, z = oz + r.f() * kChunk;
    float keep = r.f(), thin = r.f(), var = r.f();
    // meadow patchiness: lush and sparser areas, small gaps, and bare-earth patches where the ground shows soil
    float patch = vn2(x * 0.09f, z * 0.09f, seed_), fine = vn2(x * 0.6f, z * 0.6f, seed_ + 11);
    if (keep > 0.70f + 0.30f * patch) continue;
    if (fine < 0.14f) continue;
    float bare = bareField(x, z);
    if (thin < bare * 0.85f) continue;
    if (!density(x, z, t, near)) continue;
    vec3 n = groundN(t, x, z);
    if (n.y < 0.86f && thin < (0.86f - n.y) / 0.06f) continue;   // the sward thins toward the 0.8 cut-off
    // thinning band next to footprints and around craters
    float d = t.dirtAt(x, z);
    if (d > 0.02f && thin < d * 5.5f) continue;
    bool skip = false;
    for (auto& f : near) { float fd = fpDist(f, x, z) - fpMargin(f); if (fd < 2.0f && thin > fd / 2.0f) { skip = true; break; } }
    if (skip) continue;
    // routes: thinned and cropped short toward the track, tall rank verges just beyond its edge
    float wear = t.wearAt(x, z);
    if (wear > 0.05f && thin < wear * 1.5f) continue;
    float verge = std::clamp(1.0f - std::fabs(wear - 0.08f) / 0.08f, 0.0f, 1.0f);
    // weeds against walls and foundations: taller, darker clumps
    float wall = 0, fdMin = 1e9f; for (auto& f : near) { float fd = fpDist(f, x, z) - fpMargin(f); fdMin = std::min(fdMin, fd); if (fd < 2.2f) wall = std::max(wall, 1.0f - fd / 2.2f); }
    float y = groundH(t, x, z);
    float paintG = groundPaintGrass(x, z);   // < 1 on painted dry grass / litter / marsh: drier, sparser species
    // species layers from correlated noise masks: fine grass everywhere, seed-head grass in clumpy broad patches (and
    // on verges / wall feet), clover and broadleaf rosettes in small colonies, straw where the sward dries out,
    // occasional small shrubs (more against walls)
    float nTall = vn2(x * 0.06f, z * 0.06f, seed_ + 21), nClump = vn2(x * 0.45f, z * 0.45f, seed_ + 51), nClo = vn2(x * 0.35f, z * 0.35f, seed_ + 31);
    float nDry = vn2(x * 0.12f, z * 0.12f, seed_ + 41), nBroad = vn2(x * 0.22f, z * 0.22f, seed_ + 61), nShrub = vn2(x * 0.05f, z * 0.05f, seed_ + 71);
    float rs = r.f(), r1 = r.f(), r2 = r.f();
    float pTall = 0.015f + 0.26f * smooth(0.5f, 0.8f, nTall) * smooth(0.3f, 0.65f, nClump) + 0.35f * verge + 0.12f * wall;
    float pClo = 0.12f * smooth(0.55f, 0.8f, nClo), pBroad = 0.015f + 0.05f * smooth(0.5f, 0.8f, nBroad);
    float pShrub = 0.010f * smooth(0.55f, 0.8f, nShrub) + 0.03f * wall;
    float pStraw = 0.07f * smooth(0.62f, 0.82f, nDry) + 0.6f * (1.0f - paintG), pTurf = 0.30f * (1.0f - 0.6f * smooth(0.55f, 0.82f, nTall));
    if (wear > 0.05f) { pTall *= 0.2f; pShrub = 0; pTurf = 0.8f; }   // trodden: cropped short
    float acc = 0; int sp = 1;
    const std::pair<float, int> layers[] = {{pShrub, 16}, {pTall, 2}, {pClo, 3}, {pBroad, 17}, {pStraw, 4}, {pTurf, 0}};
    for (auto [p, s] : layers) { acc += p; if (rs < acc) { sp = s; break; } }
    float hgt, wid;
    switch (sp) {
      case 0: hgt = 0.20f + 0.14f * r1; wid = 0.014f + 0.005f * r2; break;
      case 2: hgt = 0.62f + 0.38f * r1; wid = 0.015f + 0.005f * r2; break;
      case 3: hgt = 0.07f + 0.05f * r1; wid = 0.024f + 0.008f * r2; break;
      case 4: hgt = 0.30f + 0.26f * r1; wid = 0.015f + 0.005f * r2; break;
      case 16: hgt = 0.28f + 0.30f * r1; wid = 0.075f + 0.025f * r2; break;
      case 17: hgt = 0.13f + 0.08f * r1; wid = 0.036f + 0.014f * r2; break;
      default: hgt = (0.34f + 0.28f * r1) * (0.75f + 0.5f * patch); wid = 0.018f + 0.007f * r2; break;
    }
    hgt *= (1.0f - 0.6f * std::min(wear * 2.0f, 1.0f)) * (1.0f + 0.5f * verge + 0.4f * wall * patch) * (0.45f + 0.55f * smooth(0.0f, 3.0f, fdMin));   // grass grows back from a footprint edge
    if (verge > 0.2f || wall > 0.3f) var = std::min(1.0f, var * 0.6f);
    float tint = std::clamp(std::pow(r.f(), 3.0f) * 0.55f + smooth(0.55f, 0.85f, nDry) * 0.35f + (1.0f - paintG) * 0.4f, 0.0f, 1.0f);
    float yaw = r.f() * 6.2831853f;
    // shape.y: width (0..0.1 m) and the ground normal's x / z (clumps lean with the slope); shape.w: the ground colour
    auto q8 = [](float v) { return (uint32_t)std::clamp((int)std::lround(v * 255.0f), 0, 255); };
    float sy = asFloat(q8(wid / 0.1f) | (q8(n.x * 0.5f + 0.5f) << 8) | (q8(n.z * 0.5f + 0.5f) << 16) | (0x3Cu << 24));
    float sw = asFloat(packRGB(groundColour(t, bn, x, z, bare), 0x3Cu));
    c.inst.push_back({vec4(x, y, z, yaw), vec4(hgt, sy, packAlbedo(bn, x, z, var, tint, sp), sw)});
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
  // only footprints that can reach a chunk in range (the per-chunk tests below use this reach from the chunk centre,
  // which is within R of the camera): the signatures walked every footprint of the map per chunk per frame
  static std::vector<GrassFootprint> nearFp; nearFp.clear();
  for (auto& f : fp) if (glm::length(f.pos - vec2(cam.x, cam.z)) <= R + fpRadius(f) + kMargin + kChunk) nearFp.push_back(f);
  const std::vector<GrassFootprint>& fpIn = nearFp;
  int x0 = (int)std::floor((cam.x - R) / kChunk), x1 = (int)std::floor((cam.x + R) / kChunk);
  int z0 = (int)std::floor((cam.z - R) / kChunk), z1 = (int)std::floor((cam.z + R) / kChunk);
  struct Todo { float d; int cx, cz; uint64_t sig; };
  std::vector<Todo> todo;
  for (int cz = z0; cz <= z1; ++cz) for (int cx = x0; cx <= x1; ++cx) {
    vec2 cc((cx + 0.5f) * kChunk, (cz + 0.5f) * kChunk);
    float d = glm::length(cc - vec2(cam.x, cam.z));
    if (d > R) continue;
    if (cc.x < t.org.x - kChunk || cc.y < t.org.y - kChunk || cc.x > t.org.x + t.size + kChunk || cc.y > t.org.y + t.sizeZ + kChunk) continue;
    uint64_t sig = signature(cx, cz, t, fpIn);
    auto it = chunks.find(ckey(cx, cz));
    if (it != chunks.end()) {
      it->second.lastUsed = frame;
      if (it->second.sig == sig) continue;
      uint64_t content = contentSignature(cx, cz, t, fpIn);
      if (content == it->second.content) { it->second.sig = sig; continue; }   // tiles remeshed, ground unchanged here
    }
    todo.push_back({d, cx, cz, sig});
  }
  std::sort(todo.begin(), todo.end(), [](const Todo& a, const Todo& b) { return a.d < b.d || (a.d == b.d && (a.cz < b.cz || (a.cz == b.cz && a.cx < b.cx))); });
  for (auto& w : todo) {
    double el = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
    if (budgetMs >= 0 && lastBuilt > 0 && el > budgetMs) { ++pending; continue; }
    Chunk& c = chunks[ckey(w.cx, w.cz)];
    c.cx = w.cx; c.cz = w.cz; c.sig = w.sig; c.content = contentSignature(w.cx, w.cz, t, fpIn); c.lastUsed = frame;
    build(c, t, fpIn);
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

// LOD per chunk from its nearest point (an upper bound for every clump in it): the drawn prefix of the shuffled list
// and the cards per clump. The vertex shader applies the same curves per clump with a soft edge at the cut (rank =
// instance index / count), so neither clumps nor cards pop as the eye moves.
void Grass::drawChunks(GLuint prog, const mat4& cullVP, vec3 cam, float maxD, int maxBlades, bool shadow) const {
  const GrassPreset& P = grassPreset(quality);
  glBindVertexArray(vao);
  GLint lCount = glGetUniformLocation(prog, "uCount");
  glUniform1f(glGetUniformLocation(prog, "uRadius"), P.radius);
  glUniform1f(glGetUniformLocation(prog, "uWind"), wind ? 1.0f : 0.0f);
  glUniform1f(glGetUniformLocation(prog, "uWidthScale"), 1.0f);
  float frac = P.density / kMaxDensity;
  glUniform4f(glGetUniformLocation(prog, "uLod"), frac, P.nearR, P.midR, P.falloff);
  glUniform4f(glGetUniformLocation(prog, "uLod2"), P.floor, (float)P.midBlades, (float)P.farBlades, wind && nearWind ? P.windR : 0.0f);
  struct D { float d; const Chunk* c; };
  std::vector<D> list; list.reserve(chunks.size());
  for (auto& [k, c] : chunks) {
    if (!c.count) continue;
    vec3 mn(c.cx * kChunk, c.ymin, c.cz * kChunk), mx(mn.x + kChunk, c.ymax + 0.3f, mn.z + kChunk);
    float d = glm::length(glm::clamp(cam, mn, mx) - cam);
    if (d > maxD) continue;
    // frustum: reject when all 8 corners are outside one clip plane
    bool out = false;
    for (int pl = 0; pl < 6 && !out; ++pl) {
      int axis = pl >> 1; float sgn = (pl & 1) ? -1.0f : 1.0f; int inside = 0;
      for (int q = 0; q < 8; ++q) {
        vec4 p = cullVP * vec4((q & 1) ? mx.x : mn.x, (q & 2) ? mx.y : mn.y, (q & 4) ? mx.z : mn.z, 1.0f);
        if (sgn * p[axis] <= p.w) ++inside;
      }
      out = inside == 0;
    }
    if (!out) list.push_back({d, &c});
  }
  std::sort(list.begin(), list.end(), [](const D& a, const D& b) { return a.d < b.d; });   // near first: early depth rejects the rest
  for (auto& [d, cp] : list) {
    const Chunk& c = *cp;
    float dens = d <= P.nearR ? 1.0f : std::max(P.floor, std::pow(P.nearR / d, P.falloff));
    int n = std::min(c.count, (int)std::ceil(c.count * frac * dens) + 1);
    float nb = d <= P.nearR ? kBlades : d <= P.midR ? glm::mix((float)kBlades, (float)P.midBlades, smooth(P.nearR, P.midR, d))
                                                    : glm::mix((float)P.midBlades, (float)P.farBlades, smooth(P.midR, P.radius, d));
    int blades = std::min(maxBlades, (int)std::ceil(nb));
    if (n <= 0 || blades <= 0) continue;
    glUniform1f(lCount, (float)c.count);
    glVertexArrayVertexBuffer(vao, 1, c.vbo, 0, sizeof(GrassInstance));
    glDrawElementsInstanced(GL_TRIANGLES, cardIndexEnd(blades), GL_UNSIGNED_SHORT, nullptr, n);
    if (shadow) { lastShadowClumps += (size_t)n; ++lastShadowDraws; }
    else { ++lastDrawnChunks; lastDrawnBlades += (size_t)n * blades; lastDrawnClumps += (size_t)n; }
  }
  glUniform1f(lCount, 0.0f);   // nature patches share the program: no rank fade for them
}

void Grass::draw(GLuint prog, const mat4& vp, vec3 cam) const {
  lastDrawnChunks = 0; lastDrawnBlades = 0; lastDrawnClumps = 0;
  const GrassPreset& P = grassPreset(quality);
  if (P.radius <= 0 || !vao || chunks.empty()) return;
  glUniform1f(glGetUniformLocation(prog, "uShadowPass"), 0.0f);
  drawChunks(prog, vp, cam, P.radius, kBlades, false);
}

void Grass::drawShadow(GLuint prog, const mat4& lightVP, const mat4& camVP, vec3 cam) const {
  lastShadowClumps = 0; lastShadowDraws = 0;
  const GrassPreset& P = grassPreset(quality);
  if (P.radius <= 0 || P.shadowR <= 0 || !vao || chunks.empty()) return;
  glUniformMatrix4fv(glGetUniformLocation(prog, "uVP"), 1, GL_FALSE, &lightVP[0][0]);
  glUniform1f(glGetUniformLocation(prog, "uShadowPass"), P.shadowR);
  drawChunks(prog, camVP, cam, P.shadowR, 8, true);   // chunks in the eye's view; the 8 tall centre cards cast
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
