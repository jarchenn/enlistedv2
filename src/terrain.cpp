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
}

float Terrain::height(float x, float z) const {
  if (h.empty()) return 0;
  float cell = size / (n - 1);
  float fx = (x + size * 0.5f) / cell, fz = (z + size * 0.5f) / cell;
  fx = std::clamp(fx, 0.0f, (float)n - 1.001f); fz = std::clamp(fz, 0.0f, (float)n - 1.001f);
  int xi = (int)fx, zi = (int)fz; float tx = fx - xi, tz = fz - zi;
  float a = h[zi * n + xi], b = h[zi * n + xi + 1], c = h[(zi + 1) * n + xi], d = h[(zi + 1) * n + xi + 1];
  // match the triangle split used by mesh + physics heightfield (diagonal a-d)
  if (tx >= tz) return a + (b - a) * tx + (d - b) * tz;
  return a + (c - a) * tz + (d - c) * tx;
}
vec3 Terrain::normal(float x, float z) const {
  float e = size / (n - 1);
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
    step = std::min(8.0f, 0.5f + t * 0.01f);
  }
  return false;
}
void Terrain::draw() const { if (!count) return; glBindVertexArray(vao); glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr); }
void Terrain::destroy() {
  if (vao) glDeleteVertexArrays(1, &vao);
  if (vbo) glDeleteBuffers(1, &vbo);
  if (ebo) glDeleteBuffers(1, &ebo);
  vao = vbo = ebo = 0; count = 0;
}
