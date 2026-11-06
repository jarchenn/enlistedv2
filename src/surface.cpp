// Ground surface mask: worn routes (lanes, footpaths, gravel), wheel ruts, soil and splash wetness around
// foundations, puddles in rut low points. One RGBA8 texel per 0.78 m over the template (1024^2):
//   R = wear (0 meadow .. 1 bare compacted track), G = wetness, B = route kind (0 lane, 0.5 footpath, 1 gravel),
//   A = rut / track-edge mask. The terrain shader blends grass, soil, mud and gravel from it; grass placement
//   thins, shortens and clears along it. Routes are map data (MapDoc::extra["routes"]); the mask is rebuilt when
//   the routes or the placed footprints change.
#include "terrain.h"
#include <algorithm>
#include <cmath>

namespace {
float hash2(int x, int y, uint32_t s) { uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + s * 2246822519u; h = (h ^ (h >> 13)) * 1274126177u; return ((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.0f; }
float vnoise(float x, float y, uint32_t s) {
  int ix = (int)std::floor(x), iy = (int)std::floor(y); float fx = x - ix, fy = y - iy;
  fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
  float a = hash2(ix, iy, s), b = hash2(ix + 1, iy, s), c = hash2(ix, iy + 1, s), d = hash2(ix + 1, iy + 1, s);
  return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}
float segDist(vec2 p, vec2 a, vec2 b, float& along) {
  vec2 ab = b - a; float L2 = glm::dot(ab, ab);
  float t = L2 > 1e-8f ? std::clamp(glm::dot(p - a, ab) / L2, 0.0f, 1.0f) : 0.0f;
  along = t;
  return glm::length(p - (a + ab * t));
}
}

void Terrain::buildSurface(const json& routes, const std::vector<SurfaceFootprint>& feet) {
  const int R = kSurfRes;
  surf.assign((size_t)R * R * 4, 0);
  float half = size * 0.5f, texel = size / R;
  auto idx = [&](int x, int y) { return ((size_t)y * R + x) * 4; };
  auto toTex = [&](vec2 w) { return vec2((w.x + half) / texel, (w.y + half) / texel); };
  // ---- routes
  for (auto& r : routes) {
    float width = r.value("width", 3.0f);
    std::string kind = r.value("kind", "lane");
    uint8_t kb = kind == "gravel" ? 255 : kind == "footpath" ? 128 : 0;
    std::vector<vec2> pts;
    for (auto& p : r.value("pts", json::array())) pts.push_back(vec2(p[0].get<float>(), p[1].get<float>()));
    for (size_t s = 0; s + 1 < pts.size(); ++s) {
      vec2 a = pts[s], b = pts[s + 1];
      float reach = width * 0.5f + 2.5f;
      vec2 mn = toTex(glm::min(a, b) - vec2(reach)), mx = toTex(glm::max(a, b) + vec2(reach));
      int x0 = std::max(0, (int)mn.x), x1 = std::min(R - 1, (int)mx.x), y0 = std::max(0, (int)mn.y), y1 = std::min(R - 1, (int)mx.y);
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
          vec2 w(-half + (x + 0.5f) * texel, -half + (y + 0.5f) * texel);
          float along; float d = segDist(w, a, b, along);
          // irregular edges: the worn width wanders by +-35 cm along the track
          float wob = (vnoise(w.x * 0.35f, w.y * 0.35f, 77) - 0.5f) * 0.7f;
          float e = d / (width * 0.5f + wob);
          float wear = std::clamp(1.25f - e * 1.1f, 0.0f, 1.0f);
          wear = wear * wear * (3 - 2 * wear);
          // wheel ruts on lanes at +-0.75 m from the centre line; puddles where ruts and noise lows meet
          float rut = 0;
          if (kind == "lane") { float rd = std::fabs(d - 0.75f); rut = std::clamp(1.0f - rd / 0.28f, 0.0f, 1.0f); }
          float low = vnoise(w.x * 0.22f, w.y * 0.22f, 31);
          float wet = std::clamp(rut * (low - 0.45f) * 3.2f, 0.0f, 1.0f) + std::clamp((0.25f - low) * 2.0f, 0.0f, 0.5f) * wear;
          size_t i = idx(x, y);
          surf[i + 0] = std::max<uint8_t>(surf[i + 0], (uint8_t)(wear * 255));
          surf[i + 1] = std::max<uint8_t>(surf[i + 1], (uint8_t)(std::clamp(wet, 0.0f, 1.0f) * 255));
          if (wear > 0.3f) surf[i + 2] = kb;
          surf[i + 3] = std::max<uint8_t>(surf[i + 3], (uint8_t)(rut * 255));
        }
    }
  }
  // ---- foundations: a soil / splash band around every structure, damp right at the wall
  for (auto& f : feet) {
    float c = std::cos(radians(f.yaw)), s = std::sin(radians(f.yaw));
    vec2 hb = (f.bmax - f.bmin) * 0.5f, cen = (f.bmin + f.bmax) * 0.5f;
    float reach = glm::length(hb) + 2.0f;
    vec2 wc = f.pos + vec2(cen.x * c + cen.y * s, -cen.x * s + cen.y * c);
    vec2 mn = toTex(wc - vec2(reach)), mx = toTex(wc + vec2(reach));
    int x0 = std::max(0, (int)mn.x), x1 = std::min(R - 1, (int)mx.x), y0 = std::max(0, (int)mn.y), y1 = std::min(R - 1, (int)mx.y);
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) {
        vec2 w(-half + (x + 0.5f) * texel, -half + (y + 0.5f) * texel), d = w - f.pos;
        vec2 l(d.x * c - d.y * s, d.x * s + d.y * c);   // into asset space (yaw about +y)
        vec2 q = glm::abs(l - cen) - hb;
        float dist = glm::length(glm::max(q, vec2(0))) + std::min(std::max(q.x, q.y), 0.0f);
        if (dist < -0.2f || dist > 1.8f) continue;
        float n = vnoise(w.x * 0.8f, w.y * 0.8f, 5);
        float soil = std::clamp(1.0f - (dist - 0.1f) / (0.9f + 0.6f * n), 0.0f, 1.0f) * f.strength;
        float wet = std::clamp(1.0f - dist / 0.6f, 0.0f, 1.0f) * 0.55f * f.strength;
        size_t i = idx(x, y);
        surf[i + 0] = std::max<uint8_t>(surf[i + 0], (uint8_t)(soil * 0.75f * 255));
        surf[i + 1] = std::max<uint8_t>(surf[i + 1], (uint8_t)(wet * 255));
      }
  }
  if (!surfTex) {
    glCreateTextures(GL_TEXTURE_2D, 1, &surfTex);
    glTextureStorage2D(surfTex, 1, GL_RGBA8, R, R);
    glTextureParameteri(surfTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(surfTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(surfTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(surfTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTextureSubImage2D(surfTex, 0, 0, 0, R, R, GL_RGBA, GL_UNSIGNED_BYTE, surf.data());
  ++surfRev;
}

float Terrain::surfSample(float x, float z, int ch) const {
  if (surf.empty()) return 0;
  const int R = kSurfRes;
  float u = (x + size * 0.5f) / size * R - 0.5f, v = (z + size * 0.5f) / size * R - 0.5f;
  int ix = std::clamp((int)std::floor(u), 0, R - 2), iy = std::clamp((int)std::floor(v), 0, R - 2);
  float fx = std::clamp(u - ix, 0.0f, 1.0f), fy = std::clamp(v - iy, 0.0f, 1.0f);
  auto at = [&](int a, int b) { return surf[((size_t)b * R + a) * 4 + ch] / 255.0f; };
  return (at(ix, iy) * (1 - fx) + at(ix + 1, iy) * fx) * (1 - fy) + (at(ix, iy + 1) * (1 - fx) + at(ix + 1, iy + 1) * fx) * fy;
}
