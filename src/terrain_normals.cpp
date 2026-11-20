// VOT T1 (docs/visual-overhaul-plan.md 3.C): per-pixel terrain normals. The base heightfield is 3.14 m cells drawn with
// per-vertex normals, so the a-d triangle split shows as facets. Here the normals of a smooth (Catmull-Rom) surface
// through the same vertices are baked into a texture at the fine-tile spacing (kCell / 7, ~0.45 m; capped at 4096
// texels a side) and kTerrainFS reads them for the streamed base chunks. Shading is then independent of the chunk LOD,
// so LOD changes no longer change the light either. Deformation tiles keep their own (already fine) vertex normals.
// The texture follows the base: generate() bakes it all, baseEdited() re-bakes the touched rectangle on the next draw.
#include "terrain.h"
#include <algorithm>
#include <cmath>
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

void Terrain::skyViewSync() const {
  if (h.empty()) return;
  if (svGen != generation) {
    if (svTex) glDeleteTextures(1, &svTex);
    svW = std::min(kMaxSide, (nx - 1) * 2); svH = std::min(kMaxSide, (nz - 1) * 2);
    svTexelX = size / svW; svTexelZ = sizeZ / svH;
    glCreateTextures(GL_TEXTURE_2D, 1, &svTex);
    glTextureStorage2D(svTex, 1, GL_R8, svW, svH);
    glTextureParameteri(svTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(svTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(svTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(svTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    svDirty = glm::ivec4(0, 0, svW - 1, svH - 1);
    svGen = generation;
  }
  if (svDirty.z < svDirty.x) return;
  const glm::ivec4 r = svDirty;
  svDirty = glm::ivec4(1, 1, 0, 0);
  const int w = r.z - r.x + 1, hh = r.w - r.y + 1;
  std::vector<uint8_t> px((size_t)w * hh);
  auto rows = [&](int z0, int z1) {
    for (int j = z0; j < z1; ++j)
      for (int i = 0; i < w; ++i)
        px[(size_t)j * w + i] = (uint8_t)std::lround(std::clamp(skyViewAt(org.x + (r.x + i + 0.5f) * svTexelX, org.y + (r.y + j + 0.5f) * svTexelZ), 0.0f, 1.0f) * 255.0f);
  };
  int nt = std::clamp((int)std::thread::hardware_concurrency(), 1, 16);
  if ((size_t)w * hh < 8192) nt = 1;
  std::vector<std::thread> th;
  for (int k = 0; k < nt; ++k) { int a = hh * k / nt, b = hh * (k + 1) / nt; if (k + 1 < nt) th.emplace_back(rows, a, b); else rows(a, b); }
  for (auto& t : th) t.join();
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTextureSubImage2D(svTex, 0, r.x, r.y, w, hh, GL_RED, GL_UNSIGNED_BYTE, px.data());
}
