// VOT T1 (docs/visual-overhaul-plan.md 3.C): per-pixel terrain normals. The base heightfield is 3.14 m cells drawn with
// per-vertex normals, so the a-d triangle split shows as facets. Here the normals of a smooth (Catmull-Rom) surface
// through the same vertices are baked into a texture at the fine-tile spacing (kCell / 7, ~0.45 m; capped at 4096
// texels a side) and kTerrainFS reads them for the streamed base chunks. Shading is then independent of the chunk LOD,
// so LOD changes no longer change the light either. Deformation tiles keep their own (already fine) vertex normals.
// The texture follows the base: generate() bakes it all, baseEdited() re-bakes the touched rectangle on the next draw.
#include "terrain.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>

namespace {
constexpr int kMaxSide = 4096;
inline void crW(float t, float w[4], float d[4]) {   // Catmull-Rom weights for samples p-1..p+2 and their derivatives
  float t2 = t * t, t3 = t2 * t;
  w[0] = 0.5f * (-t3 + 2 * t2 - t); w[1] = 0.5f * (3 * t3 - 5 * t2 + 2); w[2] = 0.5f * (-3 * t3 + 4 * t2 + t); w[3] = 0.5f * (t3 - t2);
  d[0] = 0.5f * (-3 * t2 + 4 * t - 1); d[1] = 0.5f * (9 * t2 - 10 * t); d[2] = 0.5f * (-9 * t2 + 8 * t + 1); d[3] = 0.5f * (3 * t2 - 2 * t);
}
}  // namespace

vec3 Terrain::smoothNormal(float x, float z) const {
  if (h.empty()) return vec3(0, 1, 0);
  float fx = std::clamp((x - org.x) / kCell, 0.0f, (float)nx - 1.001f), fz = std::clamp((z - org.y) / kCell, 0.0f, (float)nz - 1.001f);
  int ix = (int)fx, iz = (int)fz;
  float wx[4], dx[4], wz[4], dz[4];
  crW(fx - ix, wx, dx); crW(fz - iz, wz, dz);
  float gx = 0, gz = 0;
  for (int j = 0; j < 4; ++j) {
    int zz = std::clamp(iz - 1 + j, 0, nz - 1);
    const float* row = &h[(size_t)zz * nx];
    float rw = 0, rd = 0;
    for (int i = 0; i < 4; ++i) { float v = row[std::clamp(ix - 1 + i, 0, nx - 1)]; rw += wx[i] * v; rd += dx[i] * v; }
    gx += rd * wz[j]; gz += rw * dz[j];
  }
  return glm::normalize(vec3(-gx / kCell, 1.0f, -gz / kCell));
}

void Terrain::normalsDirty(vec2 lo, vec2 hi) const {
  if (nrmW <= 0) return;
  float pad = 2.5f * kCell;   // the Catmull-Rom footprint reaches two cells
  int x0 = std::max(0, (int)std::floor((lo.x - pad - org.x) / nrmTexelX)), x1 = std::min(nrmW - 1, (int)std::ceil((hi.x + pad - org.x) / nrmTexelX));
  int z0 = std::max(0, (int)std::floor((lo.y - pad - org.y) / nrmTexelZ)), z1 = std::min(nrmH - 1, (int)std::ceil((hi.y + pad - org.y) / nrmTexelZ));
  if (x1 < x0 || z1 < z0) return;
  if (nrmDirty.z < nrmDirty.x) nrmDirty = glm::ivec4(x0, z0, x1, z1);
  else nrmDirty = glm::ivec4(std::min(nrmDirty.x, x0), std::min(nrmDirty.y, z0), std::max(nrmDirty.z, x1), std::max(nrmDirty.w, z1));
  if (svW > 0) {   // T5: every texel whose horizon search reaches the edit
    float R = 38.0f;
    int a0 = std::max(0, (int)std::floor((lo.x - R - org.x) / svTexelX)), a1 = std::min(svW - 1, (int)std::ceil((hi.x + R - org.x) / svTexelX));
    int b0 = std::max(0, (int)std::floor((lo.y - R - org.y) / svTexelZ)), b1 = std::min(svH - 1, (int)std::ceil((hi.y + R - org.y) / svTexelZ));
    if (svDirty.z < svDirty.x) svDirty = glm::ivec4(a0, b0, a1, b1);
    else svDirty = glm::ivec4(std::min(svDirty.x, a0), std::min(svDirty.y, b0), std::max(svDirty.z, a1), std::max(svDirty.w, b1));
  }
  if (horAz < 1e8f) horEdit = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();   // T6: the horizon re-bakes once the edits pause
}

void Terrain::normalsSync() const {
  if (h.empty()) return;
  if (nrmGen != generation) {   // a new grid: (re)create the texture and bake everything
    if (nrmTex) glDeleteTextures(1, &nrmTex);
    nrmTex = 0;
    nrmW = std::min(kMaxSide, (nx - 1) * 7); nrmH = std::min(kMaxSide, (nz - 1) * 7);
    nrmTexelX = size / nrmW; nrmTexelZ = sizeZ / nrmH;
    int levels = 1; for (int s = std::max(nrmW, nrmH); s > 1; s >>= 1) ++levels;
    glCreateTextures(GL_TEXTURE_2D, 1, &nrmTex);
    glTextureStorage2D(nrmTex, levels, GL_RG8_SNORM, nrmW, nrmH);
    glTextureParameteri(nrmTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(nrmTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(nrmTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(nrmTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameterf(nrmTex, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY */, 8.0f);
    nrmDirty = glm::ivec4(0, 0, nrmW - 1, nrmH - 1);
    nrmGen = generation;
  }
  if (nrmDirty.z < nrmDirty.x) return;
  const glm::ivec4 r = nrmDirty;
  nrmDirty = glm::ivec4(1, 1, 0, 0);
  const int w = r.z - r.x + 1, hh = r.w - r.y + 1;
  std::vector<int8_t> px((size_t)w * hh * 2);
  auto rows = [&](int z0, int z1) {
    for (int j = z0; j < z1; ++j)
      for (int i = 0; i < w; ++i) {
        vec3 n = smoothNormal(org.x + (r.x + i + 0.5f) * nrmTexelX, org.y + (r.y + j + 0.5f) * nrmTexelZ);
        int8_t* o = &px[((size_t)j * w + i) * 2];
        o[0] = (int8_t)std::lround(std::clamp(n.x, -1.0f, 1.0f) * 127.0f); o[1] = (int8_t)std::lround(std::clamp(n.z, -1.0f, 1.0f) * 127.0f);
      }
  };
  int nt = std::clamp((int)std::thread::hardware_concurrency(), 1, 16);
  if ((size_t)w * hh < 65536) nt = 1;
  std::vector<std::thread> th;
  for (int k = 0; k < nt; ++k) { int a = hh * k / nt, b = hh * (k + 1) / nt; if (k + 1 < nt) th.emplace_back(rows, a, b); else rows(a, b); }
  for (auto& t : th) t.join();
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTextureSubImage2D(nrmTex, 0, r.x, r.y, w, hh, GL_RG, GL_BYTE, px.data());
  glGenerateTextureMipmap(nrmTex);
}

void Terrain::normalsRelease() const {
  if (nrmTex) glDeleteTextures(1, &nrmTex);
  if (svTex) glDeleteTextures(1, &svTex);
  nrmTex = svTex = 0; nrmW = nrmH = svW = svH = 0; nrmGen = svGen = ~0u; nrmDirty = svDirty = glm::ivec4(1, 1, 0, 0);
  svPx.clear(); svPx.shrink_to_fit(); horAz = 1e9f; horEdit = -1;
}

// ------------------------------------------------------------------ VOT T5: terrain self-occlusion (sky-view factor)
// The sky-occlusion map is a top-down depth of the casters, so the ground never occludes itself: a hollow or the
// foot of a bank sees as much sky as a crest. Here every 1.57 m texel searches the base heightfield in 12 directions
// out to kSvRange for its horizon, measured above the local tangent plane (the slope's own tilt is the hemisphere
// term's job), and stores the cosine-weighted sky fraction 1 - mean(sin^2 horizon). kTerrainFS multiplies its ambient
// occlusion by it (indirect light only). Flat ground stays 1, so a flat map looks exactly as before.
namespace { constexpr float kSvRange = 36.0f; constexpr int kSvDirs = 12; }

float Terrain::skyViewAt(float x, float z) const {
  if (h.empty()) return 1.0f;
  const float h0 = baseHeight(x, z);
  vec3 n = smoothNormal(x, z);
  float sum = 0;
  for (int k = 0; k < kSvDirs; ++k) {
    float a = 6.2831853f * (k + 0.5f) / kSvDirs, dx = std::cos(a), dz = std::sin(a);
    float tanT = -(n.x * dx + n.z * dz) / std::max(n.y, 0.2f);   // the tangent plane's rise per metre in this direction
    float best = tanT;
    for (float d = 1.6f; d <= kSvRange; d *= 1.3f) best = std::max(best, (baseHeight(x + dx * d, z + dz * d) - h0) / d);
    float e = std::atan(best) - std::atan(tanT);   // horizon elevation above the tangent plane
    float s = std::sin(std::max(e, 0.0f));
    sum += 1.0f - s * s;
  }
  return sum / kSvDirs;
}

void Terrain::svEnsure() const {
  if (svGen == generation) return;
  if (svTex) glDeleteTextures(1, &svTex);
  svW = std::min(kMaxSide, (nx - 1) * 2); svH = std::min(kMaxSide, (nz - 1) * 2);
  svTexelX = size / svW; svTexelZ = sizeZ / svH;
  glCreateTextures(GL_TEXTURE_2D, 1, &svTex);
  glTextureStorage2D(svTex, 1, GL_RGBA8, svW, svH);   // T6: R sky view, G / B / A horizon (was R8)
  glTextureParameteri(svTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(svTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(svTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(svTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  svPx.assign((size_t)svW * svH * 4, 255);   // R open sky; G / B / A the lowest horizon (lit) until baked
  for (size_t k = 0; k < svPx.size(); k += 4) svPx[k + 1] = svPx[k + 2] = svPx[k + 3] = 0;
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTextureSubImage2D(svTex, 0, 0, 0, svW, svH, GL_RGBA, GL_UNSIGNED_BYTE, svPx.data());
  svDirty = glm::ivec4(0, 0, svW - 1, svH - 1);
  horAz = 1e9f; horEdit = -1;
  svGen = generation;
}

namespace {
void svUpload(GLuint tex, int W, const std::vector<uint8_t>& px, int z0, int z1) {   // whole rows z0..z1
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTextureSubImage2D(tex, 0, 0, z0, W, z1 - z0 + 1, GL_RGBA, GL_UNSIGNED_BYTE, px.data() + (size_t)z0 * W * 4);
}
template <class F> void svRows(int n, int perThreadMin, F f) {
  int nt = std::clamp((int)std::thread::hardware_concurrency(), 1, 16);
  if (n < perThreadMin) nt = 1;
  std::vector<std::thread> th;
  for (int k = 0; k < nt; ++k) { int a = n * k / nt, b = n * (k + 1) / nt; if (k + 1 < nt) th.emplace_back(f, a, b); else f(a, b); }
  for (auto& t : th) t.join();
}
constexpr float kHorRange = 900.0f, kHorBin = 0.20944f;   // 12 deg between the bins
constexpr float kHorLo = -5.0f, kHorSpan = 50.0f;           // stored elevation range (deg): -5 .. 45
}  // namespace

void Terrain::skyViewSync() const {
  if (h.empty()) return;
  svEnsure();
  if (svDirty.z < svDirty.x) return;
  const glm::ivec4 r = svDirty;
  svDirty = glm::ivec4(1, 1, 0, 0);
  const int w = r.z - r.x + 1, hh = r.w - r.y + 1;
  svRows(hh, 32, [&](int z0, int z1) {
    for (int j = z0; j < z1; ++j)
      for (int i = 0; i < w; ++i)
        svPx[((size_t)(r.y + j) * svW + r.x + i) * 4] = (uint8_t)std::lround(std::clamp(skyViewAt(org.x + (r.x + i + 0.5f) * svTexelX, org.y + (r.y + j + 0.5f) * svTexelZ), 0.0f, 1.0f) * 255.0f);
  });
  svUpload(svTex, svW, svPx, r.y, r.w);
}

float Terrain::horizonAt(float x, float z, float dx, float dz) const {
  const float h0 = baseHeight(x, z) + 0.3f;   // a little above the ground: its own texel-scale bumps do not shade it
  float best = -1.0f;
  for (float d = 3.0f; d <= kHorRange; d *= 1.12f) best = std::max(best, (baseHeight(x + dx * d, z + dz * d) - h0) / d);
  return best;
}

glm::vec4 Terrain::horizonWeights(vec3 s) const {
  float el = glm::degrees(std::asin(std::clamp(s.y, -1.0f, 1.0f)));
  if (horAz > 1e8f) return glm::vec4(0, 0, 0, el);
  float d = std::atan2(s.z, s.x) - horAz;
  d = std::remainder(d, 6.2831853f);
  float t = std::clamp(d / kHorBin, -1.0f, 1.0f);
  return t < 0 ? glm::vec4(-t, 1 + t, 0, el) : glm::vec4(0, 1 - t, t, el);
}

GLuint Terrain::horizonSync(vec3 s) const {
  if (h.empty()) return 0;
  svEnsure();
  const float az = std::atan2(s.z, s.x);
  const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  bool out = horAz > 1e8f || std::fabs(std::remainder(az - horAz, 6.2831853f)) > kHorBin * 0.75f;
  bool edited = horEdit >= 0 && now - horEdit > 1.0;
  if (s.y < -0.05f && horAz < 1e8f) out = false;   // night: keep the day's bake
  if (!out && !edited) return horAz < 1e8f ? svTex : 0;
  const float a0 = std::round(az / 0.0872665f) * 0.0872665f;   // 5 deg steps: a slowly turning sun re-bakes rarely
  float dx[3], dz[3];
  for (int k = 0; k < 3; ++k) { dx[k] = std::cos(a0 + (k - 1) * kHorBin); dz[k] = std::sin(a0 + (k - 1) * kHorBin); }
  svRows(svH, 8, [&](int z0, int z1) {
    for (int j = z0; j < z1; ++j)
      for (int i = 0; i < svW; ++i) {
        float x = org.x + (i + 0.5f) * svTexelX, z = org.y + (j + 0.5f) * svTexelZ;
        uint8_t* o = &svPx[((size_t)j * svW + i) * 4];
        for (int k = 0; k < 3; ++k) {
          float e = glm::degrees(std::atan(horizonAt(x, z, dx[k], dz[k])));
          o[1 + k] = (uint8_t)std::lround(std::clamp((e - kHorLo) / kHorSpan, 0.0f, 1.0f) * 255.0f);
        }
      }
  });
  svUpload(svTex, svW, svPx, 0, svH - 1);
  horAz = a0; horEdit = -1;
  return svTex;
}
