#include "terrain.h"
#include "w5.h"
#include <algorithm>

std::vector<TemplateInfo> loadTemplates(const fs::path& dir) {
  std::vector<TemplateInfo> out;
  if (!fs::exists(dir)) return out;
  for (auto& e : fs::directory_iterator(dir)) {
    if (e.path().extension() != ".json") continue;
    try {
      json j = json::parse(readTextFile(e.path()));
      TemplateInfo t;
      t.raw = j; t.file = e.path().filename().string();
      t.id = j.value("id", e.path().stem().string());
      t.name = j.value("name", t.id);
      t.description = j.value("description", "");
      t.version = j.value("version", 1);
      t.available = j.value("available", true);
      t.size = j.value("size", 800.0f);
      t.buildHalf = j.value("buildHalf", 300.0f);
      // SPEC 9.1: width / depth of the buildable area; pre-9.1 templates give a square size / buildHalf
      t.width = j.value("width", t.buildHalf * 2.0f);
      t.depth = j.value("depth", t.width);
      t.margin = j.value("margin", std::max(0.0f, (t.size - t.buildHalf * 2.0f) * 0.5f));
      t.seed = j.value("seed", 1u);
      t.camPos = jvec3(j["camera"]["pos"], t.camPos);
      t.camYaw = j["camera"].value("yaw", 0.0f);
      t.camPitch = j["camera"].value("pitch", -20.0f);
      out.push_back(t);
    } catch (std::exception& ex) { logf("template %s: %s", e.path().string().c_str(), ex.what()); }
  }
  std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.raw.value("order", 0) < b.raw.value("order", 0); });
  return out;
}

static float hash2(int x, int y, uint32_t s) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + s * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16;
  return (h & 0xffffff) / float(0xffffff);
}
static float vnoise(float x, float y, uint32_t s) {
  int xi = (int)std::floor(x), yi = (int)std::floor(y);
  float fx = x - xi, fy = y - yi;
  fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
  float a = hash2(xi, yi, s), b = hash2(xi + 1, yi, s), c = hash2(xi, yi + 1, s), d = hash2(xi + 1, yi + 1, s);
  return glm::mix(glm::mix(a, b, fx), glm::mix(c, d, fx), fy) * 2 - 1;
}

float Terrain::baseFormula(float wx, float wz, uint32_t seed) const {
  // Blank slate: gentle undulation that gives scale without hiding placement previews.
  float v = 0.35f * vnoise(wx / 140.f, wz / 140.f, seed) + 0.12f * vnoise(wx / 45.f, wz / 45.f, seed + 7) +
            0.04f * vnoise(wx / 12.f, wz / 12.f, seed + 13);
  // outside the build area, rise gently into low hedge-bank country
  float edge = std::max(std::fabs(wx) - buildHalfX, std::fabs(wz) - buildHalfZ);
  if (edge > 0) v += edge * 0.03f + 2.0f * vnoise(wx / 60.f, wz / 60.f, seed + 3) * std::min(1.0f, edge / 40.f);
  return v;
}

// ---- V8 generator layers (MapDoc::terrainGen; the tools are in terrain_brush.cpp / beautify.cpp)
int Terrain::ringTiles() const {
  if (!gen.is_object() || !gen.contains("ring") || !gen["ring"].is_object()) return 0;
  return std::clamp(gen["ring"].value("tiles", 0), 0, 24);
}
static float fbm3(float x, float z, uint32_t s) { return 0.62f * vnoise(x, z, s) + 0.28f * vnoise(x * 2.07f, z * 2.07f, s + 17) + 0.10f * vnoise(x * 4.31f, z * 4.31f, s + 29); }
float terrainFbm(float x, float z, uint32_t s) { return fbm3(x, z, s); }   // shared with the brush (terrain_brush.cpp)
static float smooth01(float e0, float e1, float x) { float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f); return t * t * (3 - 2 * t); }

// Protect shapes of a hill stamp: {"o":[[cx,cz,hx,hz,yaw]...]} oriented boxes, {"c":[[x,z,r]...]} circles,
// {"l":[[width, x0,z0, x1,z1, ...]...]} polylines. shapeDist is <= 0 inside.
namespace {
struct HillShape { int type = 0; vec2 c{0}, h{0}; float yaw = 0, r = 0; std::vector<vec2> pts; vec2 lo{0}, hi{0}; };
float shapeDist(const HillShape& s, vec2 p) {
  if (s.type == 0) {
    float ca = std::cos(radians(s.yaw)), sa = std::sin(radians(s.yaw));
    vec2 d = p - s.c; vec2 l(ca * d.x - sa * d.y, sa * d.x + ca * d.y);   // inverse of yawQuat about +Y on (x, z)
    vec2 q = glm::abs(l) - s.h;
    return glm::length(glm::max(q, vec2(0))) + std::min(std::max(q.x, q.y), 0.0f);
  }
  if (s.type == 1) return glm::length(p - s.c) - s.r;
  float best = 1e30f;
  for (size_t k = 0; k + 1 < s.pts.size(); ++k) {
    vec2 a = s.pts[k], b = s.pts[k + 1], ab = b - a; float L2 = glm::dot(ab, ab);
    float t = L2 > 1e-9f ? std::clamp(glm::dot(p - a, ab) / L2, 0.0f, 1.0f) : 0.0f;
    best = std::min(best, glm::length(p - (a + ab * t)));
  }
  if (s.pts.size() == 1) best = glm::length(p - s.pts[0]);
  return best - s.r;
}
}

void Terrain::applyGenLayers(const TemplateInfo& t, int innerNx, int innerNz) {
  if (!gen.is_object() || gen.empty()) return;
  // ---- outer ring: the inner grid is untouched; ring vertices continue the edge of the inner grid (an authored
  // heightmap's edge level is kept) plus the procedural relief, and gentle hills fade in beyond the old edge
  if (int ring = ringTiles(); ring > 0) {
    const json& r = gen["ring"];
    const int off = ring * kTileCells;
    uint32_t seed = r.value("seed", 1u);
    float amp = r.value("amp", 3.5f), scale = std::max(20.0f, r.value("scale", 110.0f)), rise = r.value("rise", 0.015f), fade = std::max(1.0f, r.value("fade", 45.0f));
    std::vector<float> src = h;
    for (int z = 0; z < nz; ++z)
      for (int x = 0; x < nx; ++x) {
        int ix = x - off, iz = z - off;
        if (ix >= 0 && iz >= 0 && ix < innerNx && iz < innerNz) continue;
        int cx = std::clamp(ix, 0, innerNx - 1) + off, cz = std::clamp(iz, 0, innerNz - 1) + off;
        float wx = org.x + x * kCell, wz = org.y + z * kCell, ex = org.x + cx * kCell, ez = org.y + cz * kCell;
        float dist = glm::length(vec2(wx - ex, wz - ez));
        float rel = baseFormula(wx, wz, t.seed) - baseFormula(ex, ez, t.seed);   // the blank-slate relief, continued
        float hills = amp * fbm3(wx / scale, wz / scale, seed + 101);
        h[(size_t)z * nx + x] = src[(size_t)cz * nx + cx] + rel + rise * dist + hills * smooth01(0.0f, fade, dist);
      }
  }
  // ---- generated hills (Terrain > Generate hills...): seeded fbm on the base grid, zero inside the protected
  // shapes (+pad), feathered over `feather` metres, faded out toward open water
  if (gen.contains("hills") && gen["hills"].is_array()) {
    for (auto& st : gen["hills"]) {
      float amp = st.value("amp", 4.0f), scale = std::max(10.0f, st.value("scale", 120.0f)), pad = st.value("pad", 4.0f), feather = std::max(0.5f, st.value("feather", 18.0f));
      uint32_t seed = st.value("seed", 1u);
      std::vector<HillShape> shapes;
      const json pr = st.value("protect", json::object());
      for (auto& o : pr.value("o", json::array())) if (o.size() >= 5) { HillShape s; s.type = 0; s.c = vec2(o[0].get<float>(), o[1].get<float>()); s.h = vec2(o[2].get<float>(), o[3].get<float>()); s.yaw = o[4].get<float>(); float rr = glm::length(s.h); s.lo = s.c - vec2(rr); s.hi = s.c + vec2(rr); shapes.push_back(s); }
      for (auto& o : pr.value("c", json::array())) if (o.size() >= 3) { HillShape s; s.type = 1; s.c = vec2(o[0].get<float>(), o[1].get<float>()); s.r = o[2].get<float>(); s.lo = s.c - vec2(s.r); s.hi = s.c + vec2(s.r); shapes.push_back(s); }
      for (auto& o : pr.value("l", json::array())) if (o.size() >= 3) {
        HillShape s; s.type = 2; s.r = o[0].get<float>() * 0.5f; s.lo = vec2(1e30f); s.hi = vec2(-1e30f);
        for (size_t k = 1; k + 1 < o.size(); k += 2) { vec2 p(o[k].get<float>(), o[k + 1].get<float>()); s.pts.push_back(p); s.lo = glm::min(s.lo, p); s.hi = glm::max(s.hi, p); }
        s.lo -= vec2(s.r); s.hi += vec2(s.r);
        if (!s.pts.empty()) shapes.push_back(s);
      }
      // 32 m buckets of the shapes whose reach (pad + feather) touches them
      const float B = 32.0f, reach = pad + feather;
      int bx = std::max(1, (int)std::ceil(size / B) + 1), bz = std::max(1, (int)std::ceil(sizeZ / B) + 1);
      std::vector<std::vector<int>> buckets((size_t)bx * bz);
      for (int k = 0; k < (int)shapes.size(); ++k) {
        int x0 = std::clamp((int)std::floor((shapes[k].lo.x - reach - org.x) / B), 0, bx - 1), x1 = std::clamp((int)std::floor((shapes[k].hi.x + reach - org.x) / B), 0, bx - 1);
        int z0 = std::clamp((int)std::floor((shapes[k].lo.y - reach - org.y) / B), 0, bz - 1), z1 = std::clamp((int)std::floor((shapes[k].hi.y + reach - org.y) / B), 0, bz - 1);
        for (int zz = z0; zz <= z1; ++zz) for (int xx = x0; xx <= x1; ++xx) buckets[(size_t)zz * bx + xx].push_back(k);
      }
      for (int z = 0; z < nz; ++z)
        for (int x = 0; x < nx; ++x) {
          float wx = org.x + x * kCell, wz = org.y + z * kCell;
          float w = 1.0f;
          int b = std::clamp((int)std::floor((wz - org.y) / B), 0, bz - 1) * bx + std::clamp((int)std::floor((wx - org.x) / B), 0, bx - 1);
          for (int k : buckets[(size_t)b]) { float d = shapeDist(shapes[k], vec2(wx, wz)) - pad; if (d < feather) { w = std::min(w, smooth01(0.0f, feather, d)); if (w <= 0) break; } }
          if (w <= 0) continue;
          float& hv = h[(size_t)z * nx + x];
          if (waterLevel > -999.0f) w *= smooth01(waterLevel + 0.5f, waterLevel + 3.0f, hv);
          hv += amp * fbm3(wx / scale, wz / scale, seed + 211) * w;
        }
    }
  }
}

void Terrain::generate(const TemplateInfo& t, float width, float depth) {
  destroy();
  cuts.clear();
  sculpt.clear();   // V8: the sculpt layer is the document's (App::resetGround reinstalls it on the new grid)
  clearTiles();
  ++generation;
  vec2 wd = ((width <= 0 && depth <= 0) || (width == t.width && depth == t.depth)) ? vec2(t.width, t.depth)   // a template's own size is used as authored (W5b: also when passed explicitly, e.g. 850 x 1700 off the 100 m step)
                                       : mapSizeRules().clampSize(vec2(width > 0 ? width : t.width, depth > 0 ? depth : t.depth));
  const int ring = ringTiles();   // V8 outer ring (beautify.cpp): whole tiles on every side, so the inner grid keeps its world positions
  margin = t.margin;
  buildHalfX = wd.x * 0.5f; buildHalfZ = wd.y * 0.5f; buildHalf = std::max(buildHalfX, buildHalfZ);
  waterLevel = t.waterLevel;
  // grid: whole tiles covering build area + margin, centred on the origin (600 x 600 + 100 = the pre-9.1 800 m grid)
  auto cellsFor = [&](float ext) { int c = (int)std::ceil(ext / kCell - 1e-3f); return std::max(kTileCells * 2, (c + kTileCells - 1) / kTileCells * kTileCells); };
  int cx = cellsFor(wd.x + 2 * margin), cz = cellsFor(wd.y + 2 * margin);
  const int innerNx = cx + 1, innerNz = cz + 1;
  cx += 2 * ring * kTileCells; cz += 2 * ring * kTileCells;
  margin += ring * kTileCells * kCell;
  nx = cx + 1; nz = cz + 1;
  size = cx * kCell; sizeZ = cz * kCell;
  org = vec2(-size * 0.5f, -sizeZ * 0.5f);
  h.assign((size_t)nx * nz, 0.0f);
  for (int z = 0; z < nz; ++z)
    for (int x = 0; x < nx; ++x) {
      float wx = org.x + x * kCell, wz = org.y + z * kCell;
      h[(size_t)z * nx + x] = baseFormula(wx, wz, t.seed);
    }
  w5TerrainBase(*this, t);   // W5: authored template heightmap + water level (w5_maps.cpp)
  applyGenLayers(t, innerNx, innerNz);   // V8: outer ring + generated hills (terrain_brush.cpp)
  chunkRev.assign((size_t)chunksX() * chunksZ(), 0);
  glCreateTextures(GL_TEXTURE_2D, 1, &maskTex);
  glTextureStorage2D(maskTex, 1, GL_R8, tilesX(), tilesZ());
  std::vector<uint8_t> zero((size_t)tilesX() * tilesZ(), 0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTextureSubImage2D(maskTex, 0, 0, 0, tilesX(), tilesZ(), GL_RED, GL_UNSIGNED_BYTE, zero.data());
  glTextureParameteri(maskTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTextureParameteri(maskTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

void Terrain::baseEdited(vec2 lo, vec2 hi) {
  const float cs = kCell * kChunkCells;
  int x0 = std::max(0, (int)std::floor((lo.x - org.x) / cs) - 1), x1 = std::min(chunksX() - 1, (int)std::floor((hi.x - org.x) / cs) + 1);
  int z0 = std::max(0, (int)std::floor((lo.y - org.y) / cs) - 1), z1 = std::min(chunksZ() - 1, (int)std::floor((hi.y - org.y) / cs) + 1);
  for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) ++chunkRev[(size_t)z * chunksX() + x];
  ++revision;
}

float Terrain::baseHeight(float x, float z) const {
  if (h.empty()) return 0;
  float fx = (x - org.x) / kCell, fz = (z - org.y) / kCell;
  fx = std::clamp(fx, 0.0f, (float)nx - 1.001f); fz = std::clamp(fz, 0.0f, (float)nz - 1.001f);
  int xi = (int)fx, zi = (int)fz; float tx = fx - xi, tz = fz - zi;
  size_t r0 = (size_t)zi * nx, r1 = r0 + nx;
  float a = h[r0 + xi], b = h[r0 + xi + 1], c = h[r1 + xi], d = h[r1 + xi + 1];
  // match the triangle split used by mesh + physics heightfield (diagonal a-d)
  if (tx >= tz) return a + (b - a) * tx + (d - b) * tz;
  return a + (c - a) * tz + (d - c) * tx;
}

float Terrain::height(float x, float z) const {
  if (!tiles.empty()) {
    float ts = tileSize();
    int tx = (int)std::floor((x - org.x) / ts), tz = (int)std::floor((z - org.y) / ts);
    if (tx >= 0 && tz >= 0 && tx < tilesX() && tz < tilesZ()) {
      auto it = tiles.find(key(tx, tz));
      if (it != tiles.end()) {
        const auto& H = it->second.h;
        vec2 o = tileOrigin(tx, tz);
        float fs = fineSpacing();
        float fx = std::clamp((x - o.x) / fs, 0.0f, kFine - 0.0001f), fz = std::clamp((z - o.y) / fs, 0.0f, kFine - 0.0001f);
        int xi = (int)fx, zi = (int)fz; float u = fx - xi, v = fz - zi;
        float a = H[zi * kFineN + xi], b = H[zi * kFineN + xi + 1], c = H[(zi + 1) * kFineN + xi], d = H[(zi + 1) * kFineN + xi + 1];
        if (u >= v) return a + (b - a) * u + (d - b) * v;
        return a + (c - a) * v + (d - c) * u;
      }
    }
  }
  return baseHeight(x, z);
}

float Terrain::dirtAt(float x, float z) const {
  if (tiles.empty()) return 0;
  float ts = tileSize();
  int tx = (int)std::floor((x - org.x) / ts), tz = (int)std::floor((z - org.y) / ts);
  const Tile* t = tileAt(tx, tz);
  if (!t) return 0;
  vec2 o = tileOrigin(tx, tz);
  float fs = fineSpacing();
  float fx = std::clamp((x - o.x) / fs, 0.0f, kFine - 0.0001f), fz = std::clamp((z - o.y) / fs, 0.0f, kFine - 0.0001f);
  int xi = (int)fx, zi = (int)fz; float u = fx - xi, v = fz - zi;
  const auto& D = t->dirt;
  float a = D[zi * kFineN + xi], b = D[zi * kFineN + xi + 1], c = D[(zi + 1) * kFineN + xi], d = D[(zi + 1) * kFineN + xi + 1];
  return glm::mix(glm::mix(a, b, u), glm::mix(c, d, u), v);
}

vec3 Terrain::normal(float x, float z) const {
  float e = tiles.empty() ? kCell : fineSpacing();
  return glm::normalize(vec3(height(x - e, z) - height(x + e, z), 2 * e, height(x, z - e) - height(x, z + e)));
}
vec3 Terrain::baseNormal(float x, float z) const {
  const float e = kCell;
  return glm::normalize(vec3(baseHeight(x - e, z) - baseHeight(x + e, z), 2 * e, baseHeight(x, z - e) - baseHeight(x, z + e)));
}

bool Terrain::raycast(vec3 o, vec3 d, float maxT, vec3& hit) const {
  float step = 1.0f, t = 0, prev = 0;
  bool above = o.y > height(o.x, o.z);
  if (!above) return false;
  while (t < maxT) {
    t += step;
    vec3 p = o + d * t;
    if (!inTerrain(p.x, p.z)) { if (t > (size + sizeZ) * 2) return false; }
    if (p.y < height(p.x, p.z)) {
      float lo = prev, hi = t;
      for (int k = 0; k < 24; ++k) { float m = (lo + hi) * 0.5f; vec3 q = o + d * m; if (q.y < height(q.x, q.z)) hi = m; else lo = m; }
      hit = o + d * hi; return true;
    }
    prev = t;
    step = std::min(8.0f, 0.25f + t * 0.01f);
  }
  return false;
}

void Terrain::setMask(int tx, int tz, uint8_t v) {
  // GL_R8 is normalised: write 255 so the shader test (> 0.5) sees an active tile
  uint8_t texel = v ? 255 : 0;
  if (maskTex) { glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glTextureSubImage2D(maskTex, 0, tx, tz, 1, 1, GL_RED, GL_UNSIGNED_BYTE, &texel); }
  // base collision holes under the tile are applied by the chunk bodies (terrain_chunks.cpp)
  physDirty.insert(key(tx, tz));
}

Terrain::Tile* Terrain::activate(int tx, int tz) {
  if (tx < 0 || tz < 0 || tx >= tilesX() || tz >= tilesZ()) return nullptr;
  auto it = tiles.find(key(tx, tz));
  if (it != tiles.end()) return &it->second;
  Tile t; t.tx = tx; t.tz = tz;
  t.h.resize(kFineN * kFineN); t.base.resize(kFineN * kFineN); t.dirt.assign(kFineN * kFineN, 0.0f);
  vec2 o = tileOrigin(tx, tz);
  float fs = fineSpacing();
  for (int j = 0; j < kFineN; ++j)
    for (int i = 0; i < kFineN; ++i) {
      float raw = groundBase(o.x + i * fs, o.y + j * fs);   // V8: base + sculpt
      float b = cuts.empty() ? raw : cutBase(o.x + i * fs, o.y + j * fs);
      t.base[j * kFineN + i] = b; t.h[j * kFineN + i] = b;
      t.cut |= b < raw - 1e-5f;
    }
  auto& ref = tiles[key(tx, tz)] = std::move(t);
  setMask(tx, tz, 1);
  ++revision;
  return &ref;
}

// Irregular oblong crater outline, deterministic per impact point: an ellipse stretched 1.1-1.45x along a random
// axis, times four positive low harmonics (lobes). Every direction is at least the nominal radius.
static uint32_t cmix(uint32_t h) { h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16; return h; }
static float crnd(uint32_t s, uint32_t a) { return (cmix(s ^ cmix(a * 0x9E3779B1u + 0x85ebca6bu)) & 0xFFFFFF) / 16777216.0f; }
float Terrain::craterShape(vec2 c, float angle) {
  uint32_t s = cmix((uint32_t)(int32_t)std::floor(c.x * 20.0f) * 73856093u ^ (uint32_t)(int32_t)std::floor(c.y * 20.0f) * 19349663u);
  float stretch = 1.1f + 0.35f * crnd(s, 1), axis = 3.14159265f * crnd(s, 2);
  float ca = std::cos(angle - axis), sa = std::sin(angle - axis);
  float m = stretch / std::sqrt(ca * ca + stretch * stretch * sa * sa);   // ellipse radius, semi-axes stretch and 1
  float lobes = 1.0f;
  for (int h = 0; h < 4; ++h) lobes += 0.07f / (1.0f + 0.6f * h) * (0.5f + crnd(s, 10 + h)) * (0.5f + 0.5f * std::cos((h + 2) * angle + 6.2831853f * crnd(s, 20 + h)));
  return m * lobes;
}

std::vector<int> Terrain::crater(vec3 c, float R, float D, const std::function<float(float, float)>& weight, float maxDepth, bool irregular) {
  std::vector<int> touched;
  float reach = R * 1.45f * 1.45f * 1.21f, ts = tileSize(), fs = fineSpacing();   // stretch and lobes reach ~1.75 R
  int tx0 = (int)std::floor((c.x - reach - org.x) / ts), tx1 = (int)std::floor((c.x + reach - org.x) / ts);
  int tz0 = (int)std::floor((c.z - reach - org.y) / ts), tz1 = (int)std::floor((c.z + reach - org.y) / ts);
  for (int tz = tz0; tz <= tz1; ++tz)
    for (int tx = tx0; tx <= tx1; ++tx) {
      Tile* t = activate(tx, tz);
      if (!t) continue;
      vec2 o = tileOrigin(tx, tz);
      bool any = false;
      for (int j = 0; j < kFineN; ++j)
        for (int i = 0; i < kFineN; ++i) {
          float x = o.x + i * fs, z = o.y + j * fs;
          vec2 dd(x - c.x, z - c.z);
          float dl = glm::length(dd);
          if (dl > reach) continue;
          float r = dl / (R * (irregular && dl > 1e-5f ? craterShape(vec2(c.x, c.z), std::atan2(dd.y, dd.x)) : 1.0f));
          if (r > 1.45f) continue;
          // Z_new = Z_old - D * (max(0, 1 - d^2/R^2))^p, p = 2 (no rim term: the profile is exactly the formula)
          float u = std::max(0.0f, 1 - r * r);
          float bowl = -D * u * u;
          float rim = 0.0f;
          float w = weight(x, z);
          if (w <= 0.0f) continue;
          float& hh = t->h[j * kFineN + i];
          float base = t->base[j * kFineN + i];   // authored ground (terrain cuts included)
          hh = std::max(base - maxDepth, hh + w * (bowl + rim));   // cap accumulated depth
          t->dirt[j * kFineN + i] = std::max(t->dirt[j * kFineN + i], w * std::clamp((1.25f - r) / 0.5f, 0.0f, 1.0f));
          any = true;
        }
      (void)any;
      touched.push_back(key(tx, tz));   // every activated tile needs its physics patch, even if untouched (its base quads are holed)
      physDirty.insert(key(tx, tz));
    }
  // neighbours need fresh normals along shared borders
  std::set<int> remesh(touched.begin(), touched.end());
  for (int k : touched) {
    int tx = keyTx(k), tz = keyTz(k);
    for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) if (tileAt(tx + dx, tz + dz)) remesh.insert(key(tx + dx, tz + dz));
  }
  for (int k : remesh) rebuildMesh(tiles[k]);
  ++revision;
  return touched;
}

void Terrain::rebuildMesh(Tile& t) {
  t.rev = ++tileRev;
  struct TV { vec3 p, n; float dirt; };
  std::vector<TV> v(kFineN * kFineN);
  vec2 o = tileOrigin(t.tx, t.tz);
  float fs = fineSpacing();
  for (int j = 0; j < kFineN; ++j)
    for (int i = 0; i < kFineN; ++i) {
      float x = o.x + i * fs, z = o.y + j * fs;
      v[j * kFineN + i] = {vec3(x, t.h[j * kFineN + i], z), normal(x, z), t.dirt[j * kFineN + i]};
    }
  if (!t.vao) {
    std::vector<uint32_t> idx;
    for (int j = 0; j < kFine; ++j)
      for (int i = 0; i < kFine; ++i) {
        uint32_t a = j * kFineN + i, b = a + 1, c = a + kFineN, d = c + 1;
        idx.insert(idx.end(), {a, c, d, a, d, b});
      }
    t.count = (int)idx.size();
    glCreateVertexArrays(1, &t.vao); glCreateBuffers(1, &t.vbo); glCreateBuffers(1, &t.ebo);
    glNamedBufferData(t.ebo, idx.size() * 4, idx.data(), GL_STATIC_DRAW);
    glNamedBufferData(t.vbo, v.size() * sizeof(TV), nullptr, GL_DYNAMIC_DRAW);
    glVertexArrayVertexBuffer(t.vao, 0, t.vbo, 0, sizeof(TV));
    glVertexArrayElementBuffer(t.vao, t.ebo);
    for (int k = 0; k < 3; ++k) {
      glEnableVertexArrayAttrib(t.vao, k);
      glVertexArrayAttribFormat(t.vao, k, k == 2 ? 1 : 3, GL_FLOAT, GL_FALSE, k * 12);
      glVertexArrayAttribBinding(t.vao, k, 0);
    }
  }
  glNamedBufferSubData(t.vbo, 0, v.size() * sizeof(TV), v.data());
}

void Terrain::clearTiles() {
  for (auto& [k, t] : tiles) {
    if (t.vao) glDeleteVertexArrays(1, &t.vao);
    if (t.vbo) glDeleteBuffers(1, &t.vbo);
    if (t.ebo) glDeleteBuffers(1, &t.ebo);
    if (t.body != 0xffffffffu) stream.deadBodies.push_back(t.body);
    if (maskTex) setMask(t.tx, t.tz, 0); else physDirty.insert(k);
  }
  tiles.clear();
  ++revision;
  // the authored ground under terrain cuts lives in tiles: rebuild them from the cuts (no crater deltas)
  if (!cuts.empty()) { std::vector<WorldCut> keep; keep.swap(cuts); setCuts(std::move(keep)); }
  // V8: sculpted ground lives in tiles too (no crater deltas)
  if (!sculpt.empty()) { std::vector<int> keys; for (auto& [k, v] : sculpt) keys.push_back(k); sculptChanged(keys); }
}

std::map<int, std::vector<float>> Terrain::deltas() const {
  std::map<int, std::vector<float>> out;
  for (auto& [k, t] : tiles) {
    vec2 o = tileOrigin(t.tx, t.tz);
    float fs = fineSpacing();
    std::vector<float> d(kFineN * kFineN);
    bool any = false;
    for (int j = 0; j < kFineN; ++j)
      for (int i = 0; i < kFineN; ++i) {
        float v = t.h[j * kFineN + i] - t.base[j * kFineN + i];
        d[j * kFineN + i] = std::fabs(v) < 1e-4f ? 0.0f : v;
        any |= d[j * kFineN + i] != 0.0f;
      }
    if (any) out[k] = d;
  }
  return out;
}

void Terrain::applyDeltas(const std::map<int, std::vector<float>>& d) {
  for (auto& [k, dv] : d) {
    if (dv.size() != (size_t)kFineN * kFineN) continue;
    if (k < 0 || k >= tilesX() * tilesZ()) continue;
    Tile* t = activate(keyTx(k), keyTz(k));
    if (!t) continue;
    physDirty.insert(k);
    for (int j = 0; j < kFineN; ++j)
      for (int i = 0; i < kFineN; ++i) {
        float v = dv[j * kFineN + i];
        t->h[j * kFineN + i] = t->base[j * kFineN + i] + v;   // absolute: reloading never deepens
        t->dirt[j * kFineN + i] = std::clamp(-v * 3.0f, 0.0f, 1.0f);
      }
  }
  for (auto& [k, t] : tiles) rebuildMesh(t);
  ++revision;
}

void Terrain::draw(GLuint prog, vec3 cam, const mat4& vp) const {
  if (h.empty()) return;
  GLint uDis = glGetUniformLocation(prog, "uDiscard");
  // units 14 / 15: 3 and 6 belong to the scene shader (uMR, uShadowFar); binding the mask / surface there made the far
  // shadow cascade sample the surface mask on terrain (dark wedges wherever the far cascade reached)
  glUniform1i(glGetUniformLocation(prog, "uTileMask"), 15);
  glUniform1f(glGetUniformLocation(prog, "uTileSize"), tileSize());
  glUniform2f(glGetUniformLocation(prog, "uTerrainOrg"), org.x, org.y);
  glUniform2f(glGetUniformLocation(prog, "uTerrainExt"), size, sizeZ);
  glUniform2f(glGetUniformLocation(prog, "uBuildHalf"), buildHalfX, buildHalfZ);
  glBindTextureUnit(15, maskTex);
  glUniform1i(glGetUniformLocation(prog, "uSurf"), 14);
  glBindTextureUnit(14, surfTex);
  { extern void groundPaintBind(GLuint); groundPaintBind(prog); }   // lasso ground paint: unit 23 (ground_paint.cpp)
  glUniform1i(uDis, tiles.empty() ? 0 : 1);
  stream.update(*this, cam, vp, stream.cfg.buildBudgetMs);
  stream.drawBase(*this, cam, vp);
  glUniform1i(uDis, 0);
  for (auto& [k, t] : tiles) { if (!t.count) continue; glBindVertexArray(t.vao); glDrawElements(GL_TRIANGLES, t.count, GL_UNSIGNED_INT, nullptr); }
}

void Terrain::destroy() {
  stream.release();
  if (maskTex) glDeleteTextures(1, &maskTex);
  maskTex = 0;
}
