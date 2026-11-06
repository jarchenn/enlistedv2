#include "terrain.h"
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

void Terrain::generate(const TemplateInfo& t) {
  destroy();
  clearTiles();
  ++generation;
  size = t.size; buildHalf = t.buildHalf; waterLevel = t.waterLevel;
  h.assign(n * n, 0.0f);
  float cell = size / (n - 1);
  for (int z = 0; z < n; ++z)
    for (int x = 0; x < n; ++x) {
      float wx = -size * 0.5f + x * cell, wz = -size * 0.5f + z * cell;
      // Blank slate: gentle undulation that gives scale without hiding placement previews.
      float v = 0.35f * vnoise(wx / 140.f, wz / 140.f, t.seed) + 0.12f * vnoise(wx / 45.f, wz / 45.f, t.seed + 7) +
                0.04f * vnoise(wx / 12.f, wz / 12.f, t.seed + 13);
      // outside the build area, rise gently into low hedge-bank country
      float edge = std::max(std::fabs(wx), std::fabs(wz)) - buildHalf;
      if (edge > 0) v += edge * 0.03f + 2.0f * vnoise(wx / 60.f, wz / 60.f, t.seed + 3) * std::min(1.0f, edge / 40.f);
      h[z * n + x] = v;
    }
  // mesh
  struct TV { vec3 p, n; };
  std::vector<TV> v(n * n);
  for (int z = 0; z < n; ++z)
    for (int x = 0; x < n; ++x) {
      float wx = -size * 0.5f + x * cell, wz = -size * 0.5f + z * cell;
      v[z * n + x] = {vec3(wx, h[z * n + x], wz), normal(wx, wz)};
    }
  std::vector<uint32_t> idx;
  for (int z = 0; z < n - 1; ++z)
    for (int x = 0; x < n - 1; ++x) {
      uint32_t a = z * n + x, b = a + 1, c = a + n, d = c + 1;
      idx.insert(idx.end(), {a, c, d, a, d, b});
    }
  count = (int)idx.size();
  glCreateVertexArrays(1, &vao); glCreateBuffers(1, &vbo); glCreateBuffers(1, &ebo);
  glNamedBufferData(vbo, v.size() * sizeof(TV), v.data(), GL_STATIC_DRAW);
  glNamedBufferData(ebo, idx.size() * 4, idx.data(), GL_STATIC_DRAW);
  glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(TV));
  glVertexArrayElementBuffer(vao, ebo);
  for (int k = 0; k < 2; ++k) {
    glEnableVertexArrayAttrib(vao, k);
    glVertexArrayAttribFormat(vao, k, 3, GL_FLOAT, GL_FALSE, k * 12);
    glVertexArrayAttribBinding(vao, k, 0);
  }
  holeMask.assign((size_t)n * n, 0);
  glCreateTextures(GL_TEXTURE_2D, 1, &maskTex);
  glTextureStorage2D(maskTex, 1, GL_R8, tilesN(), tilesN());
  std::vector<uint8_t> zero((size_t)tilesN() * tilesN(), 0);
  glTextureSubImage2D(maskTex, 0, 0, 0, tilesN(), tilesN(), GL_RED, GL_UNSIGNED_BYTE, zero.data());
  glTextureParameteri(maskTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTextureParameteri(maskTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

float Terrain::baseHeight(float x, float z) const {
  if (h.empty()) return 0;
  float cl = size / (n - 1);
  float fx = (x + size * 0.5f) / cl, fz = (z + size * 0.5f) / cl;
  fx = std::clamp(fx, 0.0f, (float)n - 1.001f); fz = std::clamp(fz, 0.0f, (float)n - 1.001f);
  int xi = (int)fx, zi = (int)fz; float tx = fx - xi, tz = fz - zi;
  float a = h[zi * n + xi], b = h[zi * n + xi + 1], c = h[(zi + 1) * n + xi], d = h[(zi + 1) * n + xi + 1];
  // match the triangle split used by mesh + physics heightfield (diagonal a-d)
  if (tx >= tz) return a + (b - a) * tx + (d - b) * tz;
  return a + (c - a) * tz + (d - c) * tx;
}

float Terrain::height(float x, float z) const {
  if (!tiles.empty()) {
    float ts = tileSize();
    int tx = (int)std::floor((x + size * 0.5f) / ts), tz = (int)std::floor((z + size * 0.5f) / ts);
    if (tx >= 0 && tz >= 0 && tx < tilesN() && tz < tilesN()) {
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
  int tx = (int)std::floor((x + size * 0.5f) / ts), tz = (int)std::floor((z + size * 0.5f) / ts);
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
  float e = tiles.empty() ? size / (n - 1) : fineSpacing();
  return glm::normalize(vec3(height(x - e, z) - height(x + e, z), 2 * e, height(x, z - e) - height(x, z + e)));
}

bool Terrain::raycast(vec3 o, vec3 d, float maxT, vec3& hit) const {
  float step = 1.0f, t = 0, prev = 0;
  bool above = o.y > height(o.x, o.z);
  if (!above) return false;
  while (t < maxT) {
    t += step;
    vec3 p = o + d * t;
    if (std::fabs(p.x) > size * 0.5f || std::fabs(p.z) > size * 0.5f) { if (t > size * 2) return false; }
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
  // base-vertex collision holes: the interior vertices of the tile remove every base quad it covers
  for (int j = tz * kTileCells + 1; j < (tz + 1) * kTileCells; ++j)
    for (int i = tx * kTileCells + 1; i < (tx + 1) * kTileCells; ++i) holeMask[(size_t)j * n + i] = v;
}

Terrain::Tile* Terrain::activate(int tx, int tz) {
  if (tx < 0 || tz < 0 || tx >= tilesN() || tz >= tilesN()) return nullptr;
  auto it = tiles.find(key(tx, tz));
  if (it != tiles.end()) return &it->second;
  Tile t; t.tx = tx; t.tz = tz;
  t.h.resize(kFineN * kFineN); t.dirt.assign(kFineN * kFineN, 0.0f);
  vec2 o = tileOrigin(tx, tz);
  float fs = fineSpacing();
  for (int j = 0; j < kFineN; ++j)
    for (int i = 0; i < kFineN; ++i) t.h[j * kFineN + i] = baseHeight(o.x + i * fs, o.y + j * fs);
  auto& ref = tiles[key(tx, tz)] = std::move(t);
  setMask(tx, tz, 1);
  ++revision;
  return &ref;
}

std::vector<int> Terrain::crater(vec3 c, float R, float D, const std::function<float(float, float)>& weight, float maxDepth) {
  std::vector<int> touched;
  float reach = R * 1.45f, ts = tileSize(), fs = fineSpacing();
  int tx0 = (int)std::floor((c.x - reach + size * 0.5f) / ts), tx1 = (int)std::floor((c.x + reach + size * 0.5f) / ts);
  int tz0 = (int)std::floor((c.z - reach + size * 0.5f) / ts), tz1 = (int)std::floor((c.z + reach + size * 0.5f) / ts);
  for (int tz = tz0; tz <= tz1; ++tz)
    for (int tx = tx0; tx <= tx1; ++tx) {
      Tile* t = activate(tx, tz);
      if (!t) continue;
      vec2 o = tileOrigin(tx, tz);
      bool any = false;
      for (int j = 0; j < kFineN; ++j)
        for (int i = 0; i < kFineN; ++i) {
          float x = o.x + i * fs, z = o.y + j * fs;
          float r = glm::length(vec2(x - c.x, z - c.z)) / R;
          if (r > 1.45f) continue;
          // Z_new = Z_old - D * (max(0, 1 - d^2/R^2))^p, p = 2 (no rim term: the profile is exactly the formula)
          float u = std::max(0.0f, 1 - r * r);
          float bowl = -D * u * u;
          float rim = 0.0f;
          float w = weight(x, z);
          if (w <= 0.0f) continue;
          float& hh = t->h[j * kFineN + i];
          float base = baseHeight(x, z);
          hh = std::max(base - maxDepth, hh + w * (bowl + rim));   // cap accumulated depth
          t->dirt[j * kFineN + i] = std::max(t->dirt[j * kFineN + i], w * std::clamp((1.25f - r) / 0.5f, 0.0f, 1.0f));
          any = true;
        }
      (void)any;
      touched.push_back(key(tx, tz));   // every activated tile needs its physics patch, even if untouched (its base quads are holed)
    }
  // neighbours need fresh normals along shared borders
  std::set<int> remesh(touched.begin(), touched.end());
  for (int k : touched) {
    int tx = k % tilesN(), tz = k / tilesN();
    for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) if (tiles.count(key(tx + dx, tz + dz))) remesh.insert(key(tx + dx, tz + dz));
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
    if (maskTex) setMask(t.tx, t.tz, 0);
  }
  tiles.clear();
  if (!holeMask.empty()) std::fill(holeMask.begin(), holeMask.end(), 0);
  ++revision;
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
        float v = t.h[j * kFineN + i] - baseHeight(o.x + i * fs, o.y + j * fs);
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
    Tile* t = activate(k % tilesN(), k / tilesN());
    if (!t) continue;
    vec2 o = tileOrigin(t->tx, t->tz);
    float fs = fineSpacing();
    for (int j = 0; j < kFineN; ++j)
      for (int i = 0; i < kFineN; ++i) {
        float v = dv[j * kFineN + i];
        t->h[j * kFineN + i] = baseHeight(o.x + i * fs, o.y + j * fs) + v;   // absolute: reloading never deepens
        t->dirt[j * kFineN + i] = std::clamp(-v * 3.0f, 0.0f, 1.0f);
      }
  }
  for (auto& [k, t] : tiles) rebuildMesh(t);
  ++revision;
}

void Terrain::draw(GLuint prog) const {
  if (!count) return;
  GLint uDis = glGetUniformLocation(prog, "uDiscard");
  glUniform1i(glGetUniformLocation(prog, "uTileMask"), 3);
  glUniform1f(glGetUniformLocation(prog, "uTileSize"), tileSize());
  glUniform1f(glGetUniformLocation(prog, "uTerrainSize"), size);
  glBindTextureUnit(3, maskTex);
  glUniform1i(uDis, tiles.empty() ? 0 : 1);
  glBindVertexArray(vao); glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
  glUniform1i(uDis, 0);
  for (auto& [k, t] : tiles) { if (!t.count) continue; glBindVertexArray(t.vao); glDrawElements(GL_TRIANGLES, t.count, GL_UNSIGNED_INT, nullptr); }
}

void Terrain::destroy() {
  if (vao) glDeleteVertexArrays(1, &vao);
  if (vbo) glDeleteBuffers(1, &vbo);
  if (ebo) glDeleteBuffers(1, &ebo);
  if (maskTex) glDeleteTextures(1, &maskTex);
  vao = vbo = ebo = maskTex = 0; count = 0;
}
