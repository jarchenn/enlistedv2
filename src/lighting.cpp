// Lighting engine (L2, docs/lighting.md):
//  * time of day: the sun on a 49 deg N path through the Day sun (14:00 = exactly the pre-L2 constants), moon, sky,
//    fog, cloud and exposure ranges; the renderer's key light (sun or moon) drives the shadow cascades;
//  * local lights: the frame's LightDesc list (src/lights.h, L3) plus Renderer::testLights, nearest first, flicker on
//    the CPU, uploaded to an SSBO; a compute pass bins them into a 16 x 9 x 24 froxel grid (clustered forward);
//  * shadowed lights: the nearest shadow-casting point / spot lights get cube faces (spot: one tile) in a depth atlas,
//    rendered through progShadow in standard Z, cached and refreshed round robin;
//  * indirect light: DDGI-lite probe volumes around the camera (1.5 m near, 6 m far), relocated into building rooms
//    and out of the ground, captured a few per frame as 16 x 16 cube views drawn with the lit forward shader (sun, sky,
//    local lights, previous probes: multi-bounce), projected to L1 SH plus an octahedral distance tile (visibility).
// Hooks: Renderer::render (begin / prepass / end), setLighting (todColors / uniforms), exposure, app.cpp (gather).
#include "lighting.h"
#include "textures.h"   // L4: the capture shader samples the same texture arrays as progMain
#include "app.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <unordered_map>
#if __has_include("lights.h")
#include "lights.h"   // L3: void lightsGather(App&, std::vector<LightDesc>&)
#define NMK_HAVE_LIGHTS 1
#endif
#if __has_include("frustum.h")
#include "frustum.h"   // L1
#else
// local fallback until L1's src/frustum.h merges (same interface)
struct Frustum {
  glm::vec4 pl[6]{}; int n = 0;
  static Frustum fromVP(const glm::mat4& vp) {
    glm::dmat4 m(vp); Frustum f;
    glm::dvec4 r0(m[0][0], m[1][0], m[2][0], m[3][0]), r1(m[0][1], m[1][1], m[2][1], m[3][1]), r2(m[0][2], m[1][2], m[2][2], m[3][2]), r3(m[0][3], m[1][3], m[2][3], m[3][3]);
    const glm::dvec4 raw[6] = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r3 + r2, r3 - r2};
    for (const auto& p : raw) { double l = glm::length(glm::dvec3(p)); if (!(l > 1e-12) || !std::isfinite(l)) continue; f.pl[f.n++] = glm::vec4(p / l); }
    return f;
  }
  bool visible(glm::vec3 lo, glm::vec3 hi, float margin = 0) const {
    glm::vec3 c = (lo + hi) * 0.5f, e = (hi - lo) * 0.5f;
    for (int k = 0; k < n; ++k) { glm::vec3 nn(pl[k]); if (glm::dot(nn, c) + pl[k].w + glm::dot(glm::abs(nn), e) + margin < 0) return false; }
    return true;
  }
};
#endif

using glm::ivec3; using glm::ivec4; using glm::mat3; using glm::ivec2;
namespace {
// ============================================================== shaders
// light froxel binning: one invocation per froxel, sphere vs the froxel's view-space box
const char* kClusterCS = R"GLSL(
#version 450 core
layout(local_size_x = 64) in;
struct LLight { vec4 posR; vec4 colI; vec4 dirC; vec4 misc; };
layout(std430, binding = 5) readonly buffer NmkLights { LLight gLights[]; };
layout(std430, binding = 6) writeonly buffer NmkClusters { uint gCluster[]; };
uniform mat4 uViewM; uniform vec4 uClusterP, uClusterZ; uniform ivec4 uClusterDim; uniform int uLightCount;
void main(){
  int nx = uClusterDim.x, ny = uClusterDim.y, nz = uClusterDim.z, mp = uClusterDim.w;
  int ci = int(gl_GlobalInvocationID.x);
  if (ci >= nx*ny*nz) return;
  int x = ci % nx, y = (ci / nx) % ny, z = ci / (nx*ny);
  float d0 = z == 0 ? 0.0 : uClusterZ.x*exp(float(z)/uClusterZ.y), d1 = uClusterZ.x*exp(float(z + 1)/uClusterZ.y);
  vec2 n0 = vec2(x, y)/vec2(nx, ny)*2.0 - 1.0, n1 = vec2(x + 1, y + 1)/vec2(nx, ny)*2.0 - 1.0;
  vec3 mn = vec3(1e30), mx = vec3(-1e30);
  for (int k = 0; k < 8; ++k) {
    float d = (k & 4) != 0 ? d1 : d0;
    vec2 nd = vec2((k & 1) != 0 ? n1.x : n0.x, (k & 2) != 0 ? n1.y : n0.y);
    vec3 p = vec3((nd.x + uClusterP.z)*d/uClusterP.x, (nd.y + uClusterP.w)*d/uClusterP.y, -d);
    mn = min(mn, p); mx = max(mx, p);
  }
  int base = ci*(mp + 1), cnt = 0;
  for (int i = 0; i < uLightCount && cnt < mp; ++i) {
    vec3 c = (uViewM*vec4(gLights[i].posR.xyz, 1.0)).xyz; float r = gLights[i].posR.w;
    vec3 dd = c - clamp(c, mn, mx);
    if (dot(dd, dd) <= r*r) { gCluster[base + 1 + cnt] = uint(i); ++cnt; }
  }
  gCluster[base] = uint(cnt);
}
)GLSL";

// probe capture: the main vertex shader, one instance per (probe, cube face) layer the item touches
const char* kCaptureVS = R"GLSL(
#version 450 core
#extension GL_ARB_shader_viewport_layer_array : require
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec3 aAlb;
layout(location=3) in vec4 aPar; layout(location=4) in vec3 aPc; layout(location=5) in vec3 aPn; layout(location=6) in vec3 aEmi;
uniform mat4 uModel;
layout(std430, binding = 9) readonly buffer NmkCapVP { mat4 gCapVP[]; };
uniform int uLayer[96];
uniform vec4 uCapEye[16];   // probe position per capture slot (view vector of the shading)
out vec3 vW, vN, vAlb, vPc, vPn, vAp, vAn, vEmi; out vec4 vPar; flat out vec3 vEye;
void main(){
  int L = uLayer[gl_InstanceID];
  vec4 w = uModel*vec4(aPos, 1.0);
  vW = w.xyz; vN = mat3(uModel)*aNrm; vAlb = aAlb; vPar = aPar; vPc = aPc; vPn = aPn; vAp = aPos; vAn = aNrm; vEmi = aEmi;
  vEye = uCapEye[L/6].xyz;
  gl_Position = gCapVP[L]*w;
  gl_Layer = L;
}
)GLSL";

// probe projection: one work group per captured probe. Radiance -> L1 SH (stored as irradiance / pi), the ground plane
// and the sky for directions with no geometry in range; distance (mean, mean^2) per octahedral texel for visibility.
const char* kProbeCS = R"GLSL(
layout(local_size_x = 64) in;
uniform sampler2DArray uCapCol, uCapDepth;   // units 18, 19
uniform mat4 uFaceRot[6];                    // world from face view
uniform int uCapN;
uniform vec2 uCapNF;
uniform int uProbeIdx[16];
uniform vec4 uProbePos[16];                  // xyz, ground height below
uniform float uProbeBlend[16];               // weight of this capture (1 = replace)
uniform int uLayerBase;                      // first capture slot of this dispatch
layout(rg16f, binding = 0) uniform image2D uVisImg;
shared vec3 sS0[64], sSx[64], sSy[64], sSz[64];
shared float sBack[64], sCnt[64];
vec3 faceDir(int f, vec2 ndc){ return normalize(mat3(uFaceRot[f])*vec3(ndc, -1.0)); }
float groundDist(vec4 pp, vec3 dir){ return (dir.y < -0.01 && pp.y > pp.w) ? (pp.w - pp.y)/dir.y : 1e9; }
float capDist(int k, int f, ivec2 tc, vec3 dir){
  float d = texelFetch(uCapDepth, ivec3(tc, (uLayerBase + k)*6 + f), 0).r;
  float far = uCapNF.y*1.5;
  if (d >= 1.0) return min(groundDist(uProbePos[k], dir), far);
  float zn = d*2.0 - 1.0, n = uCapNF.x, fa = uCapNF.y;
  float zv = 2.0*n*fa/(fa + n - zn*(fa - n));
  vec2 ndc = (vec2(tc) + 0.5)/float(uCapN)*2.0 - 1.0;
  return zv*length(vec3(ndc, 1.0));
}
float distAlong(int k, vec3 dir){
  vec3 a = abs(dir);
  int f = (a.x >= a.y && a.x >= a.z) ? (dir.x > 0.0 ? 0 : 1) : (a.y >= a.z ? (dir.y > 0.0 ? 2 : 3) : (dir.z > 0.0 ? 4 : 5));
  vec3 v = transpose(mat3(uFaceRot[f]))*dir;
  vec2 ndc = v.xy/max(-v.z, 1e-4);
  ivec2 tc = clamp(ivec2((ndc*0.5 + 0.5)*float(uCapN)), ivec2(0), ivec2(uCapN - 1));
  return capDist(k, f, tc, dir);
}
vec3 missRadiance(vec4 pp, vec3 dir){
  float t = groundDist(pp, dir);
  if (t > 1e8) return dir.y >= 0.0 ? uSkyCol : uGroundCol;
  vec3 hit = pp.xyz + dir*t, up = vec3(0.0, 1.0, 0.0);
  vec3 ld, ls; localLights(hit, up, up, up, -1.0, vec3(0.04), ld, ls);
  return uGroundCol + vec3(0.22, 0.20, 0.16)/PI*ld;   // the ground under the probe: the analytic bounce plus lamp pools
}
void main(){
  int k = int(gl_WorkGroupID.x), t = int(gl_LocalInvocationIndex);
  int N = uCapN, total = 6*N*N;
  vec4 pp = uProbePos[k];
  vec3 S0 = vec3(0.0), Sx = vec3(0.0), Sy = vec3(0.0), Sz = vec3(0.0); float back = 0.0, cnt = 0.0;
  for (int i = t; i < total; i += 64) {
    int f = i/(N*N), r = i - f*N*N; ivec2 tc = ivec2(r % N, r / N);
    vec2 ndc = (vec2(tc) + 0.5)/float(N)*2.0 - 1.0;
    float dw = 4.0/(float(N*N)*pow(1.0 + dot(ndc, ndc), 1.5));
    vec3 dir = faceDir(f, ndc);
    float d = texelFetch(uCapDepth, ivec3(tc, (uLayerBase + k)*6 + f), 0).r;
    vec3 L;
    if (d >= 1.0) L = missRadiance(pp, dir);
    else { vec4 c = texelFetch(uCapCol, ivec3(tc, (uLayerBase + k)*6 + f), 0); L = c.rgb; if (c.a < 0.5) { back += dw; L = vec3(0.0); } }
    L = (any(isnan(L)) || any(isinf(L))) ? vec3(0.0) : clamp(L, vec3(0.0), vec3(64.0));
    S0 += L*dw; Sx += L*(dir.x*dw); Sy += L*(dir.y*dw); Sz += L*(dir.z*dw); cnt += dw;
  }
  sS0[t] = S0; sSx[t] = Sx; sSy[t] = Sy; sSz[t] = Sz; sBack[t] = back; sCnt[t] = cnt;
  barrier();
  for (int s = 32; s > 0; s >>= 1) {
    if (t < s) { sS0[t] += sS0[t + s]; sSx[t] += sSx[t + s]; sSy[t] += sSy[t + s]; sSz[t] += sSz[t + s]; sBack[t] += sBack[t + s]; sCnt[t] += sCnt[t + s]; }
    barrier();
  }
  int idx = uProbeIdx[k];
  float a = uProbeBlend[k];
  if (t == 0) {
    float norm = 4.0*PI/max(sCnt[0], 1e-4);   // the texel solid angles sum to ~4 pi
    vec3 c0 = sS0[0]*norm/(4.0*PI), cx = sSx[0]*norm/(2.0*PI), cy = sSy[0]*norm/(2.0*PI), cz = sSz[0]*norm/(2.0*PI);
    bool dead = sBack[0]/max(sCnt[0], 1e-4) > 0.25;   // inside geometry: sees back faces
    vec4 nR = vec4(c0.r, cx.r, cy.r, cz.r), nG = vec4(c0.g, cx.g, cy.g, cz.g), nB = vec4(c0.b, cx.b, cy.b, cz.b);
    gProbe[idx*4 + 0] = mix(gProbe[idx*4 + 0], nR, a);
    gProbe[idx*4 + 1] = mix(gProbe[idx*4 + 1], nG, a);
    gProbe[idx*4 + 2] = mix(gProbe[idx*4 + 2], nB, a);
    gProbe[idx*4 + 3] = vec4(pp.xyz, dead ? 0.0 : 1.0);
  }
  ivec2 ot = ivec2(t % 8, t / 8);
  float m1 = 0.0, m2 = 0.0;
  for (int sy = 0; sy < 4; ++sy) for (int sx = 0; sx < 4; ++sx) {
    vec2 o = clamp((vec2(ot) + (vec2(sx, sy) + 0.5)/4.0 - 0.5)/7.0, 0.0, 1.0);
    float dist = distAlong(k, octDecode(o*2.0 - 1.0));
    m1 += dist; m2 += dist*dist;
  }
  ivec2 tile = ivec2(idx % uGiVisCols, idx / uGiVisCols);
  imageStore(uVisImg, tile*8 + ot, vec4(m1/16.0, m2/16.0, 0.0, 0.0));
}
)GLSL";

// ============================================================== time of day
struct Tod {
  bool day = true;
  float sunEl = 46.5f, moonEl = -46.5f;
  vec3 sunTrue{0}, moon{0}, key{0}; bool keyIsMoon = false;
  vec3 keyMul{1}, skyMul{1}, groundMul{1}, fogMul{1}, zen{0.28f, 0.40f, 0.62f}, hor{0.80f, 0.84f, 0.87f}, skySunMul{1}, cloudLit{1}, moonCol{0};
  float stars = 0, nightK = 0, expoKey = 0.24f, expoMin = 0.9f, expoMax = 1.45f, fixedExpo = 1, giOpenK = 1;
};
const vec3 kDaySun = glm::normalize(vec3(-0.45f, 0.62f, 0.38f));
const float kDayHour = 14.0f;
struct Key { float e; vec3 v; };
vec3 keyframes(float e, std::initializer_list<Key> ks) {   // ascending elevation, clamped at both ends
  const Key* b = ks.begin(); const Key* end = ks.end();
  if (e <= b->e) return b->v;
  for (const Key* k = b; k + 1 < end; ++k)
    if (e <= k[1].e) { float t = (e - k->e) / (k[1].e - k->e); return glm::mix(k->v, k[1].v, t); }
  return (end - 1)->v;
}
float lum(vec3 c) { return glm::dot(c, vec3(0.2126f, 0.7152f, 0.0722f)); }
float smooth(float a, float b, float x) { float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3 - 2 * t); }
vec3 sunAt(float hour) {
  // 49 deg N, north = -Z, east = +X: the sun turns about the celestial pole 15 deg per hour through the Day sun
  const float lat = radians(49.0f);
  vec3 pole(0, std::sin(lat), -std::cos(lat));
  float a = -radians(15.0f * (hour - kDayHour));
  return glm::normalize(vec3(glm::rotate(mat4(1), a, pole) * vec4(kDaySun, 0)));
}
Tod todCompute(float hour) {
  Tod t;
  hour = std::fmod(std::fmod(hour, 24.0f) + 24.0f, 24.0f);
  t.day = std::fabs(hour - kDayHour) < 1e-3f;
  t.sunTrue = t.day ? kDaySun : sunAt(hour);
  t.moon = -t.sunTrue;   // full moon opposite the sun
  t.sunEl = degrees(std::asin(std::clamp(t.sunTrue.y, -1.0f, 1.0f))); t.moonEl = -t.sunEl;
  t.key = t.sunTrue;
  if (t.day) return t;   // exactly the Day constants
  float e = t.sunEl;
  vec3 sunMul = keyframes(e, {{-1, vec3(0)}, {0, vec3(0.22f, 0.10f, 0.04f)}, {2, vec3(0.55f, 0.30f, 0.12f)}, {5, vec3(0.80f, 0.55f, 0.30f)},
                              {12, vec3(0.95f, 0.86f, 0.70f)}, {25, vec3(1.0f)}});
  float moonK = smooth(0.0f, 10.0f, t.moonEl);
  vec3 moonMul = vec3(0.0175f, 0.021f, 0.029f) * moonK;
  if (e > -1.0f) { t.key = t.sunTrue; t.keyMul = sunMul; }
  else { t.key = t.moon; t.keyMul = moonMul; t.keyIsMoon = true; }
  if (t.key.y < 0.05f) t.key = glm::normalize(vec3(t.key.x, 0.05f, t.key.z));   // the cascades never look up from below
  vec3 dz(0.28f, 0.40f, 0.62f), dh(0.80f, 0.84f, 0.87f);
  t.zen = keyframes(e, {{-18, vec3(0.0030f, 0.0040f, 0.0080f)}, {-12, vec3(0.006f, 0.008f, 0.016f)}, {-6, vec3(0.020f, 0.026f, 0.055f)}, {-2, vec3(0.07f, 0.09f, 0.17f)},
                        {2, vec3(0.16f, 0.22f, 0.38f)}, {8, vec3(0.24f, 0.34f, 0.54f)}, {20, dz}});
  t.hor = keyframes(e, {{-18, vec3(0.0045f, 0.0050f, 0.0070f)}, {-12, vec3(0.010f, 0.011f, 0.016f)}, {-6, vec3(0.06f, 0.05f, 0.065f)}, {-2, vec3(0.36f, 0.24f, 0.20f)},
                        {2, vec3(0.70f, 0.52f, 0.38f)}, {8, vec3(0.78f, 0.74f, 0.68f)}, {20, dh}});
  t.zen += moonK * vec3(0.006f, 0.008f, 0.014f); t.hor += moonK * vec3(0.008f, 0.009f, 0.013f);   // moonlit sky
  t.skyMul = (t.zen + t.hor) / (dz + dh);
  vec3 daySun(3.30f, 3.05f, 2.62f);
  float keyRel = lum(t.keyMul * daySun) * std::max(t.key.y, 0.0f) / (lum(daySun) * kDaySun.y);   // key light on the ground vs Day
  t.groundMul = t.skyMul * 0.45f + vec3(keyRel * 0.55f) * glm::mix(vec3(1), t.keyMul / std::max(1e-4f, std::max(t.keyMul.x, std::max(t.keyMul.y, t.keyMul.z))), 0.5f);
  t.fogMul = t.hor / dh;
  t.skySunMul = e >= 0 ? sunMul : keyframes(0, {{0, vec3(0.22f, 0.10f, 0.04f)}}) * smooth(-10.0f, 0.0f, e);   // twilight glow after sunset
  t.cloudLit = glm::mix(t.skyMul, glm::max(sunMul, moonMul * 3.0f), 0.4f);
  t.moonCol = vec3(0.55f, 0.6f, 0.7f) * smooth(-2.0f, 4.0f, t.moonEl) * smooth(2.0f, -6.0f, e);
  t.stars = smooth(-6.0f, -14.0f, e);
  t.nightK = smooth(4.0f, -12.0f, e);
  t.expoKey = glm::mix(0.24f, 0.11f, t.nightK);
  t.expoMin = glm::mix(0.9f, 0.35f, t.nightK);
  t.expoMax = glm::mix(1.45f, 5.0f, t.nightK);
  float lNow = lum(t.skyMul * vec3(0.30f, 0.36f, 0.47f)) + lum(t.keyMul * daySun) * std::max(t.key.y, 0.0f) * 0.25f;
  float lDay = lum(vec3(0.30f, 0.36f, 0.47f)) + lum(daySun) * kDaySun.y * 0.25f;
  t.fixedExpo = std::clamp(std::pow(lDay / std::max(lNow, 1e-5f), 0.6f), 1.0f, t.expoMax * 0.65f);   // Creative / benchmarks: night stays dark
  t.giOpenK = 1.0f - t.nightK;
  return t;
}

// ============================================================== state
struct GpuLight { vec4 posR, colI, dirC, misc; };
struct Probe { ivec3 cell{INT32_MIN}; vec3 pos{0}; float groundY = 0; bool live = false, under = false, interior = false; int stamp = -1000000; };   // under: below the ground (never captured)
struct Cascade { bool on = false; float s = 1.5f, range = 24.0f; ivec3 n{16, 8, 16}; ivec3 org{0}; int first = 0; std::vector<Probe> p; };
struct Slot { bool used = false; uint32_t id = 0; vec3 pos{0}, dir{0}; float radius = 0, cosO = -2; bool spot = false; bool dirty = true; int light = -1; int age = 0; };
const int kMaxSlots = 8, kTile = 512, kCapN = 16, kMaxCap = 16;
const ivec3 kCluster(16, 9, 24); const int kClusterMax = 64; const float kClusterNear = 0.5f, kClusterFar = 300.0f;
const float kLampUnit = 0.25f;   // irradiance (scene units) at 1 m of an intensity-1 light; the Day sun is ~3.3

struct Sys {
  bool ready = false, layerExt = false, giAvail = false;
  GLuint lightBuf = 0, clusterBuf = 0, lsVPBuf = 0, probeBuf = 0, capVPBuf = 0;
  GLuint progCluster = 0, progProbe = 0, progCapture = 0;
  GLuint visTex = 0; int visCols = 64, visRows = 0;
  GLuint capCol = 0, capDepth = 0, capFbo = 0;
  GLuint atlasTex = 0, atlasFbo = 0, dummyShadow = 0; int atlasSlots = 0;
  Tod tod; vec3 daySun = kDaySun;
  std::vector<GpuLight> gpu; std::vector<LightDesc> sel; int count = 0;
  Slot slots[kMaxSlots]; int slotRR = 0, facesDrawn = 0;
  mat4 lsVP[kMaxSlots * 6]{};
  Cascade cas[2]; int probeTotal = 0, captured = 0, captureDraws = 0, roomsSig = -1;
  mat4 view{1}, proj{1}; vec3 cam{0}; int frame = 0; float time = 0;
  std::vector<mat4> roomInv;
  bool resetAll = false;
  float capHour = -100.0f;   // PF2: time of day of the last captures (a jump re-captures every probe)
} L;

GLuint compileCS(Renderer& R, const char* src, bool withCommon, const char* name, const char* kCommonSrc) {
  GLuint s = glCreateShader(GL_COMPUTE_SHADER);
  if (withCommon) {
    const char* parts[3] = {"#version 450 core\n#define NMK_CS 1\n#define NMK_PROBE_RW 1\n", kCommonSrc, src};
    glShaderSource(s, 3, parts, nullptr);
  } else glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof log, nullptr, log); logf("shader %s (cs) error:\n%s", name, log); ++R.shaderErrors; }
  GLuint p = glCreateProgram(); glAttachShader(p, s); glLinkProgram(p);
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) { char log[4096]; glGetProgramInfoLog(p, sizeof log, nullptr, log); logf("link %s error:\n%s", name, log); ++R.shaderErrors; }
  glDeleteShader(s);
  return p;
}
bool hasExt(const char* e) {
  GLint n = 0; glGetIntegerv(GL_NUM_EXTENSIONS, &n);
  for (GLint i = 0; i < n; ++i) { const char* x = (const char*)glGetStringi(GL_EXTENSIONS, i); if (x && std::strcmp(x, e) == 0) return true; }
  return false;
}
GLint U(GLuint p, const char* n) { return glGetUniformLocation(p, n); }

// mesh bounds: L1's GpuMesh::bmin / bmax when present, else read back once per vertex buffer
template <class M> bool meshBounds(const M& m, vec3& lo, vec3& hi) {
  if constexpr (requires { m.bmin; m.bmax; }) { lo = m.bmin; hi = m.bmax; return m.count > 0; }
  else {
    static std::unordered_map<uint64_t, std::pair<vec3, vec3>> cache;
    if (!m.vbo || !m.count) return false;
    uint64_t key = ((uint64_t)m.vbo << 32) ^ (uint64_t)m.count;
    auto it = cache.find(key);
    if (it == cache.end()) {
      GLint64 size = 0; glGetNamedBufferParameteri64v(m.vbo, GL_BUFFER_SIZE, &size);
      size_t nv = (size_t)size / sizeof(Vertex);
      std::vector<Vertex> v(nv);
      if (nv) glGetNamedBufferSubData(m.vbo, 0, (GLsizeiptr)(nv * sizeof(Vertex)), v.data());
      vec3 a(1e30f), b(-1e30f);
      for (auto& x : v) { a = glm::min(a, x.pos); b = glm::max(b, x.pos); }
      if (!nv) { a = b = vec3(0); }
      it = cache.emplace(key, std::make_pair(a, b)).first;
    }
    lo = it->second.first; hi = it->second.second; return true;
  }
}
void worldBox(const DrawItem& it, vec3& lo, vec3& hi) {
  vec3 a, b; if (!meshBounds(*it.mesh, a, b)) { lo = hi = vec3(it.model[3]); return; }
  lo = vec3(1e30f); hi = vec3(-1e30f);
  for (int k = 0; k < 8; ++k) { vec3 p = vec3(it.model * vec4((k & 1) ? b.x : a.x, (k & 2) ? b.y : a.y, (k & 4) ? b.z : a.z, 1)); lo = glm::min(lo, p); hi = glm::max(hi, p); }
}
float boxDist2(vec3 p, vec3 lo, vec3 hi) { vec3 d = glm::max(glm::max(lo - p, p - hi), vec3(0)); return glm::dot(d, d); }

// cube faces: +X -X +Y -Y +Z -Z (forward, up)
const vec3 kFaceF[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
const vec3 kFaceU[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
mat4 faceView(vec3 p, int f) { return glm::lookAt(p, p + kFaceF[f], kFaceU[f]); }

void createAtlas(int slots) {
  if (L.atlasTex) { glDeleteTextures(1, &L.atlasTex); glDeleteFramebuffers(1, &L.atlasFbo); L.atlasTex = L.atlasFbo = 0; }
  L.atlasSlots = slots;
  for (auto& s : L.slots) s = Slot{};
  if (slots <= 0) return;
  glCreateTextures(GL_TEXTURE_2D, 1, &L.atlasTex);
  glTextureStorage2D(L.atlasTex, 1, GL_DEPTH_COMPONENT32F, 6 * kTile, slots * kTile);
  glTextureParameteri(L.atlasTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(L.atlasTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(L.atlasTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(L.atlasTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTextureParameteri(L.atlasTex, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE); glTextureParameteri(L.atlasTex, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  glCreateFramebuffers(1, &L.atlasFbo); glNamedFramebufferTexture(L.atlasFbo, GL_DEPTH_ATTACHMENT, L.atlasTex, 0);
  glNamedFramebufferDrawBuffer(L.atlasFbo, GL_NONE); glNamedFramebufferReadBuffer(L.atlasFbo, GL_NONE);
}

void setupCascades(int gi) {
  bool want0 = gi >= 1 && L.giAvail, want1 = gi >= 2 && L.giAvail;
  if (L.cas[0].on == want0 && L.cas[1].on == want1 && L.probeTotal) return;
  L.cas[0] = Cascade{}; L.cas[1] = Cascade{};
  L.cas[0].on = want0; L.cas[0].s = 1.5f; L.cas[0].range = 24.0f; L.cas[0].n = ivec3(16, 8, 16);
  L.cas[1].on = want1; L.cas[1].s = 6.0f; L.cas[1].range = 60.0f; L.cas[1].n = ivec3(16, 6, 16);
  int first = 0;
  for (auto& c : L.cas) { c.first = first; if (c.on) { c.p.assign((size_t)c.n.x * c.n.y * c.n.z, Probe{}); first += (int)c.p.size(); } }
  L.probeTotal = std::max(first, 1);
  std::vector<vec4> zero((size_t)L.probeTotal * 4, vec4(0));
  glNamedBufferData(L.probeBuf, zero.size() * sizeof(vec4), zero.data(), GL_DYNAMIC_DRAW);
  if (L.visTex) glDeleteTextures(1, &L.visTex);
  L.visRows = (L.probeTotal + L.visCols - 1) / L.visCols;
  glCreateTextures(GL_TEXTURE_2D, 1, &L.visTex); glTextureStorage2D(L.visTex, 1, GL_RG16F, L.visCols * 8, std::max(1, L.visRows) * 8);
  glTextureParameteri(L.visTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(L.visTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(L.visTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(L.visTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  float big[2] = {1000.0f, 1e6f}; glClearTexImage(L.visTex, 0, GL_RG, GL_FLOAT, big);
}

// probe position: the grid point, lifted out of the ground and pulled into the free space of a room it falls in
void placeProbe(const Cascade& c, Probe& p, const Terrain& t, const Renderer& R) {
  vec3 g = vec3(p.cell) * c.s;
  p.pos = g; p.under = false; p.interior = false;
  for (size_t k = 0; k < R.rooms.size(); ++k) {
    const auto& rb = R.rooms[k];
    vec3 lp = vec3(L.roomInv[k] * vec4(g, 1));
    vec3 h = rb.half, m = vec3(c.s * 0.5f);
    if (std::fabs(lp.x) > h.x + m.x || std::fabs(lp.y) > h.y + m.y || std::fabs(lp.z) > h.z + m.z) continue;
    vec3 in = glm::max(h - vec3(0.35f, 0.0f, 0.35f), vec3(0.05f));
    lp.x = std::clamp(lp.x, -in.x, in.x); lp.z = std::clamp(lp.z, -in.z, in.z);
    lp.y = std::clamp(lp.y, -h.y + std::min(0.5f, h.y), h.y - std::min(0.3f, h.y));
    p.pos = vec3(rb.m * vec4(lp, 1)); p.interior = true;
    break;
  }
  p.groundY = t.height(p.pos.x, p.pos.z);
  if (!p.interior && p.pos.y < p.groundY + 0.35f) {
    if (p.groundY + 0.35f - p.pos.y < c.s * 0.95f) p.pos.y = p.groundY + 0.35f; else p.under = true;   // underground
  }
}
void writeProbePos(const Cascade& c, int i, const Probe& p) {
  vec4 v(p.pos, 0.0f);   // not live until captured
  glNamedBufferSubData(L.probeBuf, (GLintptr)(((size_t)c.first + i) * 4 + 3) * sizeof(vec4), sizeof(vec4), &v);
}
int posmod(int a, int n) { int m = a % n; return m < 0 ? m + n : m; }
}  // namespace

// ============================================================== renderer hooks
struct LightingPass {
  static void init(Renderer& R, const char* kCommonSrc, const char* kMainFSSrc);
  static void begin(Renderer& R, const RenderView& v);
  static void prepass(Renderer& R, const RenderView& v, const std::vector<DrawItem>& opaque, const Terrain& t);
  static void end(Renderer& R) { R.sunDir = L.daySun; }
  static void todColors(const Renderer& R, vec3& sun, vec3& sky, vec3& ground, vec3& fog, vec3& fill, vec3& zen, vec3& hor, vec3& skySun, vec3& skySunCol);
  static void uniforms(Renderer& R, GLuint p);
  static void exposureUniforms(const Renderer& R, GLuint p);
  static float fixedExposure(const Renderer& R) { (void)R; return L.tod.fixedExpo; }
  static void shadowPass(Renderer& R, const std::vector<DrawItem>& opaque);
  static void probePass(Renderer& R, const RenderView& v, const std::vector<DrawItem>& opaque, const Terrain& t);
};

void LightingPass::init(Renderer& R, const char* kCommonSrc, const char* kMainFSSrc) {
  if (L.ready) return;
  GLint units = 0; glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
  L.layerExt = hasExt("GL_ARB_shader_viewport_layer_array");
  L.giAvail = L.layerExt && units >= 20;
  logf("lighting: %d fragment texture units, layered capture %s -> probe GI %s", units, L.layerExt ? "yes" : "no", L.giAvail ? "available" : "off");
  glCreateBuffers(1, &L.lightBuf); glNamedBufferData(L.lightBuf, sizeof(GpuLight) * 16, nullptr, GL_DYNAMIC_DRAW);
  glCreateBuffers(1, &L.clusterBuf);
  std::vector<uint32_t> zc((size_t)kCluster.x * kCluster.y * kCluster.z * (kClusterMax + 1), 0u);
  glNamedBufferData(L.clusterBuf, zc.size() * 4, zc.data(), GL_DYNAMIC_DRAW);
  glCreateBuffers(1, &L.lsVPBuf); glNamedBufferData(L.lsVPBuf, sizeof(L.lsVP), L.lsVP, GL_DYNAMIC_DRAW);
  glCreateBuffers(1, &L.probeBuf); glNamedBufferData(L.probeBuf, sizeof(vec4) * 4, nullptr, GL_DYNAMIC_DRAW);
  glCreateBuffers(1, &L.capVPBuf); glNamedBufferData(L.capVPBuf, sizeof(mat4) * kMaxCap * 6, nullptr, GL_DYNAMIC_DRAW);
  L.progCluster = compileCS(R, kClusterCS, false, "light_clusters", kCommonSrc);
  if (L.giAvail) {
    L.progProbe = compileCS(R, kProbeCS, true, "probe_project", kCommonSrc);
    L.progCapture = R.compile(kCaptureVS, kMainFSSrc, "probe_capture"); tex::attach(L.progCapture);
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &L.capCol); glTextureStorage3D(L.capCol, 1, GL_RGBA16F, kCapN, kCapN, kMaxCap * 6);
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &L.capDepth); glTextureStorage3D(L.capDepth, 1, GL_DEPTH_COMPONENT32F, kCapN, kCapN, kMaxCap * 6);
    for (GLuint tx : {L.capCol, L.capDepth}) { glTextureParameteri(tx, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTextureParameteri(tx, GL_TEXTURE_MAG_FILTER, GL_NEAREST); }
    glCreateFramebuffers(1, &L.capFbo);
    glNamedFramebufferTexture(L.capFbo, GL_COLOR_ATTACHMENT0, L.capCol, 0); glNamedFramebufferTexture(L.capFbo, GL_DEPTH_ATTACHMENT, L.capDepth, 0);
    if (glCheckNamedFramebufferStatus(L.capFbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { logf("probe capture framebuffer incomplete"); ++R.fboErrors; L.giAvail = false; }
  }
  // 1x1 stand-ins so units 16 / 17 always hold a texture of the declared type
  glCreateTextures(GL_TEXTURE_2D, 1, &L.dummyShadow); glTextureStorage2D(L.dummyShadow, 1, GL_DEPTH_COMPONENT32F, 1, 1);
  glTextureParameteri(L.dummyShadow, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE); glTextureParameteri(L.dummyShadow, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  { float one = 1.0f; glClearTexImage(L.dummyShadow, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &one); }
  L.ready = true;
  setupCascades(R.lt.gi);
}

void LightingPass::begin(Renderer& R, const RenderView& v) {
  L.daySun = R.sunDir;
  L.tod = todCompute(R.lt.hour);
  if (!L.tod.day) R.sunDir = L.tod.key;
  L.view = v.view; L.proj = v.proj; L.cam = v.camPos; ++L.frame; L.time += v.dt;
}

void LightingPass::todColors(const Renderer& R, vec3& sun, vec3& sky, vec3& ground, vec3& fog, vec3& fill, vec3& zen, vec3& hor, vec3& skySun, vec3& skySunCol) {
  const Tod& t = L.tod;
  if (R.thumbMode || t.day) return;   // thumbnails and model sheets always render at Day
  vec3 daySunCol = sun;
  sun *= t.keyMul; sky *= t.skyMul; ground *= t.groundMul; fog *= t.fogMul; fill *= t.groundMul;
  zen = t.zen; hor = t.hor; skySun = t.sunTrue; skySunCol = daySunCol * t.skySunMul;
}

void LightingPass::exposureUniforms(const Renderer& R, GLuint p) {
  (void)R;
  glUniform1f(U(p, "uExpoKey"), L.tod.expoKey); glUniform1f(U(p, "uExpoMin"), L.tod.expoMin); glUniform1f(U(p, "uExpoMax"), L.tod.expoMax);
}

void LightingPass::uniforms(Renderer& R, GLuint p) {
  bool on = L.ready && !R.thumbMode;
  glUniform1i(U(p, "uLightCount"), on ? L.count : 0);
  glUniform1i(U(p, "uLightMode"), 0);
  glUniform4f(U(p, "uClusterP"), L.proj[0][0], L.proj[1][1], L.proj[2][0], L.proj[2][1]);
  glUniform4f(U(p, "uClusterZ"), kClusterNear, kCluster.z / std::log(kClusterFar / kClusterNear), 0, 0);
  glUniform4i(U(p, "uClusterDim"), kCluster.x, kCluster.y, kCluster.z, kClusterMax);
  glUniform2f(U(p, "uScreen"), (float)R.width, (float)R.height);
  glUniformMatrix4fv(U(p, "uViewM"), 1, GL_FALSE, glm::value_ptr(L.view));
  glUniform1i(U(p, "uLShadow"), 16); glBindTextureUnit(16, L.atlasTex ? L.atlasTex : L.dummyShadow);
  glUniform4f(U(p, "uLShadowTile"), (float)kTile, 6.0f * kTile, (float)std::max(1, L.atlasSlots) * kTile, 0);
  glUniform1f(U(p, "uLampUnit"), kLampUnit);
  bool gi = on && L.giAvail && (L.cas[0].on || L.cas[1].on);
  glUniform1i(U(p, "uGiOn"), gi ? 1 : 0);
  int nc = 0; vec4 grid[2]{}; ivec4 org[2]{}, dim[2]{};
  for (auto& c : L.cas) if (c.on) { grid[nc] = vec4(c.s, c.range, 0, 0); org[nc] = ivec4(c.org, c.first); dim[nc] = ivec4(c.n, 0); ++nc; }
  glUniform1i(U(p, "uGiCascades"), nc);
  glUniform4fv(U(p, "uGiGrid"), 2, glm::value_ptr(grid[0])); glUniform4iv(U(p, "uGiOrg"), 2, glm::value_ptr(org[0])); glUniform4iv(U(p, "uGiDim"), 2, glm::value_ptr(dim[0]));
  glUniform1i(U(p, "uGiVis"), 17); glBindTextureUnit(17, L.visTex);
  glUniform1i(U(p, "uGiVisCols"), L.visCols);
  glUniform1f(U(p, "uGiOpenK"), R.thumbMode ? 1.0f : L.tod.giOpenK);
  // sky dome extras (kSkyFS) and emission
  bool night = !R.thumbMode && !L.tod.day;
  glUniform3fv(U(p, "uMoonDir"), 1, glm::value_ptr(L.tod.moon));
  vec3 mc = night ? L.tod.moonCol : vec3(0); glUniform3fv(U(p, "uMoonCol"), 1, glm::value_ptr(mc));
  glUniform1f(U(p, "uStars"), night ? L.tod.stars : 0.0f);
  vec3 cl = night ? L.tod.cloudLit : vec3(1); glUniform3fv(U(p, "uCloudLit"), 1, glm::value_ptr(cl));
  glUniform1f(U(p, "uGlow"), 1.0f); glUniform1f(U(p, "uEmissiveK"), R.lt.emissive);
  glUniform1i(U(p, "uCapture"), 0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, L.lightBuf); glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, L.clusterBuf);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, L.lsVPBuf); glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, L.probeBuf);
}

// ---- local light shadows: cube faces (spot: one tile) in a depth atlas, standard Z, cached per light
void LightingPass::shadowPass(Renderer& R, const std::vector<DrawItem>& opaque) {
  L.facesDrawn = 0;
  int want = std::clamp(R.lt.shadowLights, 0, kMaxSlots);
  if (want != L.atlasSlots) createAtlas(want);
  for (int i = 0; i < L.count; ++i) L.gpu[i].misc.y = -1;
  if (!want) { glNamedBufferSubData(L.lightBuf, 0, L.count * sizeof(GpuLight), L.gpu.data()); return; }
  // pick the nearest shadow casters (lights are sorted nearest first); keep slots stable per light id
  std::vector<int> pick;
  for (int i = 0; i < L.count && (int)pick.size() < want; ++i)
    if (L.sel[i].shadow && glm::length(L.sel[i].pos - L.cam) < 45.0f + L.sel[i].radius) pick.push_back(i);
  bool keep[kMaxSlots] = {};
  std::vector<int> unplaced;
  for (int li : pick) {
    int s = -1;
    for (int k = 0; k < want; ++k) if (L.slots[k].used && L.slots[k].id == L.sel[li].id) s = k;
    if (s < 0) { unplaced.push_back(li); continue; }
    keep[s] = true; L.slots[s].light = li;
  }
  for (int k = 0; k < want; ++k) if (!keep[k]) { L.slots[k].used = false; L.slots[k].light = -1; }
  for (int li : unplaced) for (int k = 0; k < want; ++k) if (!L.slots[k].used) { L.slots[k] = Slot{}; L.slots[k].used = true; L.slots[k].id = L.sel[li].id; L.slots[k].light = li; L.slots[k].dirty = true; break; }
  // matrices, dirty flags
  for (int k = 0; k < want; ++k) {
    Slot& s = L.slots[k];
    if (!s.used || s.light < 0) continue;
    const LightDesc& d = L.sel[s.light];
    bool spot = d.cosOuter > 0.25f;
    if (glm::length(d.pos - s.pos) > 0.01f || glm::length(d.dir - s.dir) > 0.01f || std::fabs(d.radius - s.radius) > 0.01f || spot != s.spot) s.dirty = true;
    s.pos = d.pos; s.dir = d.dir; s.radius = d.radius; s.cosO = d.cosOuter; s.spot = spot;
    float far = std::max(d.radius, 0.5f);
    if (spot) {
      vec3 dir = glm::length(d.dir) > 1e-4f ? glm::normalize(d.dir) : vec3(0, -1, 0);
      vec3 up = std::fabs(dir.y) > 0.95f ? vec3(0, 0, 1) : vec3(0, 1, 0);
      float fov = std::min(2.0f * std::acos(std::clamp(d.cosOuter, -1.0f, 1.0f)) + radians(8.0f), radians(150.0f));
      L.lsVP[k * 6] = glm::perspective(fov, 1.0f, 0.05f, far) * glm::lookAt(d.pos, d.pos + dir, up);
    } else for (int f = 0; f < 6; ++f) L.lsVP[k * 6 + f] = glm::perspective(radians(92.0f), 1.0f, 0.05f, far) * faceView(d.pos, f);
    L.gpu[s.light].misc.y = (float)k; L.gpu[s.light].misc.z = spot ? 1.0f : 0.0f;
  }
  glNamedBufferSubData(L.lightBuf, 0, L.count * sizeof(GpuLight), L.gpu.data());
  glNamedBufferSubData(L.lsVPBuf, 0, sizeof(L.lsVP), L.lsVP);
  // render: every dirty slot (at most 2 a frame) plus one round-robin refresh for moving objects
  std::vector<int> todo;
  for (int k = 0; k < want && (int)todo.size() < 2; ++k) if (L.slots[k].used && L.slots[k].light >= 0 && L.slots[k].dirty) todo.push_back(k);
  if (todo.empty()) for (int i = 0; i < want; ++i) { int k = (L.slotRR + i) % want; if (L.slots[k].used && L.slots[k].light >= 0) { todo.push_back(k); L.slotRR = k + 1; break; } }
  if (todo.empty()) return;
  glBindFramebuffer(GL_FRAMEBUFFER, L.atlasFbo);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.5f, 2.0f);
  glEnable(GL_SCISSOR_TEST);
  glUseProgram(R.progShadow);
  GLint lvp = U(R.progShadow, "uVP");
  std::vector<std::pair<vec3, vec3>> boxes(opaque.size());
  for (size_t i = 0; i < opaque.size(); ++i) if (opaque[i].mesh) worldBox(opaque[i], boxes[i].first, boxes[i].second);
  std::vector<DrawItem> sub;
  for (int k : todo) {
    Slot& s = L.slots[k];
    int faces = s.spot ? 1 : 6;
    for (int f = 0; f < faces; ++f) {
      glViewport(f * kTile, k * kTile, kTile, kTile); glScissor(f * kTile, k * kTile, kTile, kTile);
      glClear(GL_DEPTH_BUFFER_BIT);
      Frustum fr = Frustum::fromVP(L.lsVP[k * 6 + f]);
      sub.clear();
      for (size_t i = 0; i < opaque.size(); ++i) {
        if (!opaque[i].mesh || !opaque[i].mesh->count) continue;
        if (boxDist2(s.pos, boxes[i].first, boxes[i].second) > s.radius * s.radius) continue;
        if (!fr.visible(boxes[i].first, boxes[i].second)) continue;
        sub.push_back(opaque[i]);
      }
      glUniformMatrix4fv(lvp, 1, GL_FALSE, glm::value_ptr(L.lsVP[k * 6 + f]));
      R.drawItems(R.progShadow, sub, true);
      ++L.facesDrawn;
    }
    s.dirty = false;
  }
  glDisable(GL_SCISSOR_TEST); glDisable(GL_POLYGON_OFFSET_FILL);
}

// ---- probe volumes: scroll with the camera, pick a few probes, capture them in one layered pass, project
void LightingPass::probePass(Renderer& R, const RenderView& v, const std::vector<DrawItem>& opaque, const Terrain& t) {
  L.captured = 0; L.captureDraws = 0;
  setupCascades(R.lt.gi);
  if (!L.giAvail || (!L.cas[0].on && !L.cas[1].on)) return;
  // rooms changed (placement / new map): every probe position is re-evaluated
  int sig = (int)R.rooms.size() * 7919;
  for (auto& rb : R.rooms) sig += (int)(rb.m[3].x * 13 + rb.m[3].z * 7 + rb.half.x * 101);
  bool roomsChanged = sig != L.roomsSig;
  if (roomsChanged) { L.roomsSig = sig; L.roomInv.resize(R.rooms.size()); for (size_t k = 0; k < R.rooms.size(); ++k) L.roomInv[k] = glm::inverse(R.rooms[k].m); }
  if (R.perf.probeRefresh && std::fabs(R.lt.hour - L.capHour) > 0.1f) { L.resetAll = L.capHour > -50.0f || L.resetAll; L.capHour = R.lt.hour; }
  struct Cand { int c, i; float score; };
  std::vector<Cand> cand;
  for (int ci = 0; ci < 2; ++ci) {
    Cascade& c = L.cas[ci];
    if (!c.on) continue;
    ivec3 camCell = ivec3(glm::floor(v.camPos / c.s));
    c.org = camCell - ivec3(c.n.x / 2, c.n.y / 4, c.n.z / 2);
    for (int z = 0; z < c.n.z; ++z) for (int y = 0; y < c.n.y; ++y) for (int x = 0; x < c.n.x; ++x) {
      int i = (y * c.n.z + z) * c.n.x + x;
      Probe& p = c.p[i];
      ivec3 phys(x, y, z);
      ivec3 cell = c.org + ivec3(posmod(phys.x - c.org.x, c.n.x), posmod(phys.y - c.org.y, c.n.y), posmod(phys.z - c.org.z, c.n.z));
      if (cell != p.cell || roomsChanged || L.resetAll) {
        bool moved = cell != p.cell;
        p.cell = cell; vec3 old = p.pos;
        placeProbe(c, p, t, R);
        if (moved || L.resetAll || glm::length(old - p.pos) > 0.01f) { p.live = false; p.stamp = -1000000; writeProbePos(c, i, p); }
      }
      if (p.under) continue;   // below the ground: never captured (stays invalid on the GPU)
      float d = glm::length(p.pos - v.camPos);
      float score = !p.live ? 1e9f - d * 1000.0f : (float)(L.frame - p.stamp) * (p.interior ? 2.0f : 1.0f) / (1.0f + d * 0.05f);
      cand.push_back({ci, i, score});
    }
  }
  L.resetAll = false;
  int budget = R.lt.probeBudget > 0 ? R.lt.probeBudget : (R.lt.gi >= 2 ? 8 : 3);
  budget = std::min(budget, kMaxCap);
  // PF2 probeRefresh: the full budget only while probes wait for their first capture (camera moved, rooms or time of day
  // changed); once the volumes are converged, 2 (High) / 1 (Medium) re-captures per frame keep them fresh, oldest and
  // nearest first. Tests that set probeBudget keep their rate.
  if (R.perf.probeRefresh && R.lt.probeBudget <= 0) {
    int pending = (int)std::count_if(cand.begin(), cand.end(), [](const Cand& c) { return c.score > 1e8f; });
    budget = std::min(budget, std::max(pending, R.lt.gi >= 2 ? 2 : 1));
  }
  if ((int)cand.size() > budget) std::partial_sort(cand.begin(), cand.begin() + budget, cand.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
  cand.resize(std::min((int)cand.size(), budget));
  if (cand.empty()) return;
  std::stable_sort(cand.begin(), cand.end(), [](const Cand& a, const Cand& b) { return a.c < b.c; });   // one projection dispatch per cascade
  int K = (int)cand.size();
  // capture matrices (standard Z)
  std::vector<mat4> vps(K * 6);
  std::vector<Frustum> frs(K * 6);
  for (int k = 0; k < K; ++k) {
    const Cascade& c = L.cas[cand[k].c]; const Probe& p = c.p[cand[k].i];
    mat4 pr = glm::perspective(radians(90.0f), 1.0f, 0.05f, c.range);
    for (int f = 0; f < 6; ++f) { vps[k * 6 + f] = pr * faceView(p.pos, f); frs[k * 6 + f] = Frustum::fromVP(vps[k * 6 + f]); }
  }
  glNamedBufferSubData(L.capVPBuf, 0, vps.size() * sizeof(mat4), vps.data());
  vec4 eyes[kMaxCap]; for (int k = 0; k < K; ++k) eyes[k] = vec4(L.cas[cand[k].c].p[cand[k].i].pos, 1);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, L.capVPBuf);
  glBindFramebuffer(GL_FRAMEBUFFER, L.capFbo);
  glNamedFramebufferDrawBuffer(L.capFbo, GL_COLOR_ATTACHMENT0);
  glViewport(0, 0, kCapN, kCapN);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDisable(GL_POLYGON_OFFSET_FILL);
  glClearColor(0, 0, 0, 1); glClearDepth(1.0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  GLuint p = L.progCapture;
  R.setLighting(p, v);
  glUniform1i(U(p, "uLightMode"), 1); glUniform1i(U(p, "uCapture"), 1);
  glUniform1i(U(p, "uWeather"), 1); glUniform1i(U(p, "uNoWallRelief"), R.wallRelief ? 0 : 1);
  glUniform4fv(U(p, "uCapEye"), K, glm::value_ptr(eyes[0]));
  GLint lm = U(p, "uModel"), lt = U(p, "uTint"), li = U(p, "uInst"), ll = U(p, "uLayer");
  static const SurfaceParams kDefaultSurf;
  const SurfaceParams* lastSurf = nullptr;
  int layers[96];
  for (auto& it : opaque) {
    if (!it.mesh || !it.mesh->count || it.tint.a < 0.999f) continue;
    vec3 lo, hi; worldBox(it, lo, hi);
    // PF2 probecull: an item narrower than ~1 capture texel (90 deg / 16 = 0.098 rad) seen from the probe adds next to
    // nothing to its L1 SH but costs six layers of geometry; the probe skips it (a probe inside the box never does)
    const float minAng2 = R.perf.probeCull ? 0.098f * 0.098f : 0.0f, diag2 = glm::dot(hi - lo, hi - lo);
    int n = 0;
    for (int k = 0; k < K; ++k) {
      const Cascade& c = L.cas[cand[k].c]; const Probe& pr = c.p[cand[k].i];
      float d2 = boxDist2(pr.pos, lo, hi);
      if (d2 > c.range * c.range || diag2 < minAng2 * d2) continue;
      for (int f = 0; f < 6; ++f) if (frs[k * 6 + f].visible(lo, hi)) layers[n++] = k * 6 + f;
    }
    if (!n) continue;
    glUniform1iv(ll, n, layers);
    glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(it.model));
    glUniform4fv(lt, 1, glm::value_ptr(it.tint));
    const SurfaceParams* sp = it.surf ? it.surf : &kDefaultSurf;
    if (li >= 0 && sp != lastSurf) {
      lastSurf = sp;
      glUniform4fv(li, 1, glm::value_ptr(sp->inst));
      int nr = std::clamp((int)(sp->inst.w + 0.5f), 0, 16);
      if (nr) { glUniform4fv(U(p, "uRoomA"), nr, glm::value_ptr(sp->roomA[0])); glUniform4fv(U(p, "uRoomB"), nr, glm::value_ptr(sp->roomB[0])); }
      int ns = std::clamp(sp->sills, 0, 12);
      glUniform1i(U(p, "uSills"), ns);
      if (ns) { glUniform4fv(U(p, "uSillA"), ns, glm::value_ptr(sp->sillA[0])); glUniform4fv(U(p, "uSillB"), ns, glm::value_ptr(sp->sillB[0])); }
    }
    glBindVertexArray(it.mesh->vao);
    glDrawElementsInstanced(GL_TRIANGLES, it.mesh->count, GL_UNSIGNED_INT, nullptr, n);
    ++L.captureDraws;
  }
  // projection
  glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
  GLuint q = L.progProbe;
  R.setLighting(q, v);   // sky / ground colours, lights, shadows for the ground plane
  glUseProgram(q);
  int idx[kMaxCap]; vec4 pos[kMaxCap]; float blend[kMaxCap];
  for (int k = 0; k < K; ++k) {
    Cascade& c = L.cas[cand[k].c]; Probe& pr = c.p[cand[k].i];
    idx[k] = c.first + cand[k].i; pos[k] = vec4(pr.pos, pr.groundY); blend[k] = pr.live ? 0.6f : 1.0f;
    pr.live = true; pr.stamp = L.frame;
  }
  mat4 rot[6]; for (int f = 0; f < 6; ++f) rot[f] = glm::transpose(mat4(mat3(faceView(vec3(0), f))));
  glUniformMatrix4fv(U(q, "uFaceRot"), 6, GL_FALSE, glm::value_ptr(rot[0]));
  glUniform1i(U(q, "uCapN"), kCapN);
  // per probe near / far: one range per dispatch, so dispatch per cascade
  glBindTextureUnit(18, L.capCol); glBindTextureUnit(19, L.capDepth);
  glUniform1i(U(q, "uCapCol"), 18); glUniform1i(U(q, "uCapDepth"), 19);
  glBindImageTexture(0, L.visTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RG16F);
  {
    // one dispatch per cascade (the batch is sorted by cascade): the depth linearisation needs the capture's far plane
    int k0 = 0;
    while (k0 < K) {
      int k1 = k0; float range = L.cas[cand[k0].c].range;
      while (k1 < K && L.cas[cand[k1].c].range == range) ++k1;
      glUniform2f(U(q, "uCapNF"), 0.05f, range);
      glUniform1iv(U(q, "uProbeIdx"), k1 - k0, idx + k0); glUniform4fv(U(q, "uProbePos"), k1 - k0, glm::value_ptr(pos[k0])); glUniform1fv(U(q, "uProbeBlend"), k1 - k0, blend + k0);
      glUniform1i(U(q, "uLayerBase"), k0);
      glDispatchCompute(k1 - k0, 1, 1);
      k0 = k1;
    }
  }
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
  glBindTextureUnit(18, 0); glBindTextureUnit(19, 0);
  L.captured = K;
}

void LightingPass::prepass(Renderer& R, const RenderView& v, const std::vector<DrawItem>& opaque, const Terrain& t) {
  if (!L.ready || R.thumbMode) return;
  // ---- lights: nearest first, capped, flicker on the CPU
  L.sel.clear();
  L.sel.insert(L.sel.end(), R.lights.begin(), R.lights.end());
  L.sel.insert(L.sel.end(), R.testLights.begin(), R.testLights.end());
  vec3 cam = v.camPos;
  auto key = [&](const LightDesc& d) { return glm::length(d.pos - cam) - d.radius; };
  std::sort(L.sel.begin(), L.sel.end(), [&](const LightDesc& a, const LightDesc& b) { return key(a) < key(b); });
  int cap = std::clamp(R.lt.maxLights, 0, 4096);
  if ((int)L.sel.size() > cap) L.sel.resize(cap);
  L.count = (int)L.sel.size();
  L.gpu.resize(std::max(L.count, 1));
  for (int i = 0; i < L.count; ++i) {
    const LightDesc& d = L.sel[i];
    float ph = (float)(d.id % 977) * 0.618f * 6.2831853f, tt = L.time;
    float n = std::sin(tt * 7.3f + ph) * 0.5f + std::sin(tt * 13.1f + ph * 1.7f) * 0.3f + std::sin(tt * 23.7f + ph * 2.3f) * 0.2f;
    float fl = 1.0f - std::clamp(d.flicker, 0.0f, 1.0f) * (0.18f + 0.22f * (0.5f + 0.5f * n));
    vec3 dir = glm::length(d.dir) > 1e-5f ? glm::normalize(d.dir) : vec3(0, -1, 0);
    float co = d.cosOuter <= -1.0f ? -2.0f : d.cosOuter, ci = std::max(d.cosInner, co + 1e-3f);
    L.gpu[i] = {vec4(d.pos, std::max(d.radius, 0.0f)), vec4(glm::max(d.color, vec3(0)) * std::max(d.intensity, 0.0f) * fl, 0), vec4(dir, co), vec4(ci, -1, 0, 0)};
  }
  GLint64 bsize = 0; glGetNamedBufferParameteri64v(L.lightBuf, GL_BUFFER_SIZE, &bsize);
  if (bsize < (GLint64)(L.gpu.size() * sizeof(GpuLight))) glNamedBufferData(L.lightBuf, std::max<size_t>(L.gpu.size(), 64) * sizeof(GpuLight) * 2, nullptr, GL_DYNAMIC_DRAW);
  glNamedBufferSubData(L.lightBuf, 0, L.gpu.size() * sizeof(GpuLight), L.gpu.data());
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, L.lightBuf); glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, L.clusterBuf);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, L.lsVPBuf); glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, L.probeBuf);
  // ---- shadows of the nearest casters, then the probes (their captures use both)
  R.gpuMark(GP_LightShadows); shadowPass(R, opaque);
  R.gpuMark(GP_Probes);
  { auto c0 = std::chrono::steady_clock::now();
    if (R.perf.probeCapture) probePass(R, v, opaque, t); else L.captured = L.captureDraws = 0;   // PF2 A/B: GI frozen
    R.stats.probeCpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count(); R.stats.probes = L.captured; }
  R.gpuMark(GP_Clusters);
  // ---- froxel clusters for the camera
  glUseProgram(L.progCluster);
  glUniformMatrix4fv(U(L.progCluster, "uViewM"), 1, GL_FALSE, glm::value_ptr(L.view));
  glUniform4f(U(L.progCluster, "uClusterP"), L.proj[0][0], L.proj[1][1], L.proj[2][0], L.proj[2][1]);
  glUniform4f(U(L.progCluster, "uClusterZ"), kClusterNear, kCluster.z / std::log(kClusterFar / kClusterNear), 0, 0);
  glUniform4i(U(L.progCluster, "uClusterDim"), kCluster.x, kCluster.y, kCluster.z, kClusterMax);
  glUniform1i(U(L.progCluster, "uLightCount"), L.count);
  glDispatchCompute((kCluster.x * kCluster.y * kCluster.z + 63) / 64, 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  // ---- state the main pass expects
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDisable(GL_POLYGON_OFFSET_FILL);
  glBindVertexArray(R.emptyVao);
}

// render.cpp entry points (one line each at the call sites)
void lightingInit(Renderer& R, const char* kCommonSrc, const char* kMainFSSrc) { LightingPass::init(R, kCommonSrc, kMainFSSrc); }
void lightingBegin(Renderer& R, const RenderView& v) { LightingPass::begin(R, v); }
void lightingPrepass(Renderer& R, const RenderView& v, const std::vector<DrawItem>& opaque, const Terrain& t) { LightingPass::prepass(R, v, opaque, t); }
void lightingEnd(Renderer& R) { LightingPass::end(R); }
void lightingTodColors(const Renderer& R, vec3& sun, vec3& sky, vec3& ground, vec3& fog, vec3& fill, vec3& zen, vec3& hor, vec3& skySun, vec3& skySunCol) { LightingPass::todColors(R, sun, sky, ground, fog, fill, zen, hor, skySun, skySunCol); }
void lightingUniforms(Renderer& R, GLuint p) { LightingPass::uniforms(R, p); }
void lightingExposure(const Renderer& R, GLuint p) { LightingPass::exposureUniforms(R, p); }
float lightingFixedExposure(const Renderer& R) { return LightingPass::fixedExposure(R); }

// ============================================================== presets, read-back, app hook
float todPresetHour(TodPreset p) { return p == TodPreset::Dusk ? 18.6f : p == TodPreset::Night ? 0.0f : p == TodPreset::Dawn ? 5.0f : kDayHour; }
TodPreset todPresetOf(float h) {
  for (TodPreset p : {TodPreset::Day, TodPreset::Dusk, TodPreset::Night, TodPreset::Dawn}) if (std::fabs(h - todPresetHour(p)) < 1e-3f) return p;
  return TodPreset::Custom;
}
const char* todPresetName(TodPreset p) { return p == TodPreset::Day ? "Day" : p == TodPreset::Dusk ? "Dusk" : p == TodPreset::Night ? "Night" : p == TodPreset::Dawn ? "Dawn" : "Custom"; }
TodInfo lightingTod(float hour) {
  Tod t = todCompute(hour); TodInfo o;
  o.day = t.day; o.sunEl = t.sunEl; o.moonEl = t.moonEl; o.sun = t.sunTrue; o.moon = t.moon; o.key = t.key; o.keyIsMoon = t.keyIsMoon;
  o.keyMul = t.keyMul; o.skyMul = t.skyMul; o.stars = t.stars; o.nightK = t.nightK; o.fixedExpo = t.fixedExpo;
  o.expoMin = t.expoMin; o.expoMax = t.expoMax; o.expoKey = t.expoKey; o.giOpenK = t.giOpenK;
  return o;
}
LightingStats lightingStats() {
  LightingStats s; s.lights = L.count; s.shadowFacesDrawn = L.facesDrawn; s.captured = L.captured; s.captureDraws = L.captureDraws; s.giAvailable = L.giAvail;
  for (auto& sl : L.slots) s.shadowSlots += sl.used && sl.light >= 0;
  for (auto& c : L.cas) if (c.on) for (auto& p : c.p) { s.probesTotal++; s.probesLive += p.live && !p.under; s.probesDead += p.under; }
  return s;
}
void lightingResetProbes() { L.resetAll = true; }

bool lightingClusterCounts(std::vector<int>& gpu, std::vector<int>& cpu, glm::ivec3& dims) {
  dims = kCluster;
  int n = kCluster.x * kCluster.y * kCluster.z;
  std::vector<uint32_t> raw((size_t)n * (kClusterMax + 1));
  glGetNamedBufferSubData(L.clusterBuf, 0, raw.size() * 4, raw.data());
  gpu.assign(n, 0); cpu.assign(n, 0);
  for (int i = 0; i < n; ++i) gpu[i] = (int)raw[(size_t)i * (kClusterMax + 1)];
  float sc = kCluster.z / std::log(kClusterFar / kClusterNear);
  for (int ci = 0; ci < n; ++ci) {
    int x = ci % kCluster.x, y = (ci / kCluster.x) % kCluster.y, z = ci / (kCluster.x * kCluster.y);
    float d0 = z == 0 ? 0.0f : kClusterNear * std::exp(z / sc), d1 = kClusterNear * std::exp((z + 1) / sc);
    vec2 n0 = vec2(x, y) / vec2(kCluster.x, kCluster.y) * 2.0f - 1.0f, n1 = vec2(x + 1, y + 1) / vec2(kCluster.x, kCluster.y) * 2.0f - 1.0f;
    vec3 mn(1e30f), mx(-1e30f);
    for (int k = 0; k < 8; ++k) {
      float d = (k & 4) ? d1 : d0; vec2 nd((k & 1) ? n1.x : n0.x, (k & 2) ? n1.y : n0.y);
      vec3 p((nd.x + L.proj[2][0]) * d / L.proj[0][0], (nd.y + L.proj[2][1]) * d / L.proj[1][1], -d);
      mn = glm::min(mn, p); mx = glm::max(mx, p);
    }
    int c = 0;
    for (int i = 0; i < L.count && c < kClusterMax; ++i) {
      vec3 lc = vec3(L.view * vec4(vec3(L.gpu[i].posR), 1)); float r = L.gpu[i].posR.w;
      vec3 dd = lc - glm::clamp(lc, mn, mx);
      if (glm::dot(dd, dd) <= r * r) ++c;
    }
    cpu[ci] = c;
  }
  return true;
}

// CPU replica of giSample (one call reads the probe buffer and the visibility atlas back)
float lightingGiAt(glm::vec3 w, glm::vec3 N, glm::vec3& irr) {
  irr = vec3(0);
  if (!L.giAvail || L.probeTotal <= 1) return 0;
  std::vector<vec4> pb((size_t)L.probeTotal * 4);
  glGetNamedBufferSubData(L.probeBuf, 0, pb.size() * sizeof(vec4), pb.data());
  int vw = L.visCols * 8, vh = std::max(1, L.visRows) * 8;
  std::vector<glm::vec2> vis((size_t)vw * vh);
  glGetTextureImage(L.visTex, 0, GL_RG, GL_FLOAT, (GLsizei)(vis.size() * sizeof(glm::vec2)), vis.data());
  auto oct = [](vec3 n) { n /= (std::fabs(n.x) + std::fabs(n.y) + std::fabs(n.z)); vec2 p(n.x, n.z);
    if (n.y < 0) p = (vec2(1) - glm::abs(vec2(p.y, p.x))) * vec2(n.x >= 0 ? 1.f : -1.f, n.z >= 0 ? 1.f : -1.f); return p; };
  auto visAt = [&](int idx, vec3 d) {   // bilinear like the shader
    vec2 o = oct(d) * 0.5f + 0.5f; vec2 tile((float)(idx % L.visCols), (float)(idx / L.visCols));
    vec2 px = tile * 8.0f + o * 7.0f;   // texel space (centres at integers)
    ivec2 b = ivec2(glm::floor(px)); vec2 f = px - vec2(b);
    auto at = [&](int x, int y) { x = std::clamp(x, 0, vw - 1); y = std::clamp(y, 0, vh - 1); return vis[(size_t)y * vw + x]; };
    return glm::mix(glm::mix(at(b.x, b.y), at(b.x + 1, b.y), f.x), glm::mix(at(b.x, b.y + 1), at(b.x + 1, b.y + 1), f.x), f.y);
  };
  float rem = 1;
  int nc = 0; const Cascade* cs[2]; for (auto& c : L.cas) if (c.on) cs[nc++] = &c;
  for (int c = 0; c < nc; ++c) {
    const Cascade& C = *cs[c];
    vec3 g = w / C.s - vec3(C.org);
    if (glm::any(glm::lessThan(g, vec3(0))) || glm::any(glm::greaterThanEqual(g, vec3(C.n - 1)))) continue;
    vec3 edge = glm::min(g, vec3(C.n - 1) - g);
    float cw = c + 1 < nc ? std::clamp(std::min(std::min(edge.x, edge.y), edge.z), 0.0f, 1.0f) : 1.0f;
    ivec3 b = ivec3(glm::floor(g)); vec3 f = g - vec3(b);
    vec3 q = w + N * (0.18f * C.s);
    vec4 sR(0), sG(0), sB(0); float sw = 0, tw = 0;
    for (int k = 0; k < 8; ++k) {
      ivec3 o(k & 1, (k >> 1) & 1, k >> 2);
      ivec3 cell = C.org + b + o, m(posmod(cell.x, C.n.x), posmod(cell.y, C.n.y), posmod(cell.z, C.n.z));
      int idx = C.first + (m.y * C.n.z + m.z) * C.n.x + m.x;
      vec3 tri = glm::mix(vec3(1) - f, f, vec3(o));
      float wt = tri.x * tri.y * tri.z;
      vec4 pp = pb[(size_t)idx * 4 + 3];
      if (pp.w < 0.5f) continue;
      tw += wt;
      vec3 toP = vec3(pp) - w; float dp = glm::length(toP);
      float bf = (glm::dot(toP / std::max(dp, 1e-4f), N) + 1) * 0.5f; wt *= bf * bf + 0.05f;
      vec3 pd = q - vec3(pp); float r = glm::length(pd);
      vec2 mm = visAt(idx, pd / std::max(r, 1e-4f));
      if (r > mm.x) { float var = std::max(mm.y - mm.x * mm.x, 0.0004f), dd = r - mm.x, ch = var / (var + dd * dd); wt *= ch * ch * ch; }
      if (wt < 1e-6f) continue;
      sR += pb[(size_t)idx * 4] * wt; sG += pb[(size_t)idx * 4 + 1] * wt; sB += pb[(size_t)idx * 4 + 2] * wt; sw += wt;
    }
    if (sw <= 1e-5f) continue;
    float conf = std::clamp(tw * 4.0f, 0.0f, 1.0f) * std::clamp(sw / std::max(tw, 1e-4f) * 8.0f, 0.0f, 1.0f);
    sR /= sw; sG /= sw; sB /= sw;
    vec3 e = glm::max(vec3(sR.x + glm::dot(vec3(sR.y, sR.z, sR.w), N), sG.x + glm::dot(vec3(sG.y, sG.z, sG.w), N), sB.x + glm::dot(vec3(sB.y, sB.z, sB.w), N)), vec3(0));
    float take = rem * cw * conf;
    irr += e * take; rem -= take;
    if (rem <= 1e-3f) break;
  }
  return 1 - rem;
}

void lightingAppFrame(App& app) {
  Renderer& R = app.R;
  R.lights.clear();
#ifdef NMK_HAVE_LIGHTS
  lightsGather(app, R.lights);
#endif
  // room volumes of enterable buildings near the camera (probe placement), refreshed every half second
  static int tick = 0; static size_t lastN = (size_t)-1;
  if (++tick % 30 != 1 && app.doc.instances.size() == lastN) return;
  lastN = app.doc.instances.size();
  R.rooms.clear();
  if (R.lt.gi <= 0) return;
  vec3 cam = L.cam;
  std::unordered_map<std::string, const AssetDef*> defs;
  for (const Instance& in : app.doc.instances) {
    if (glm::length(vec2(in.pos.x - cam.x, in.pos.z - cam.z)) > 90.0f) continue;
    auto it = defs.find(in.assetId);
    const AssetDef* a = it != defs.end() ? it->second : (defs[in.assetId] = app.lib.find(in.assetId));
    if (!a || !a->enterable) continue;
    const RoomInfo* ri = app.interior.rooms(a);
    if (!ri) continue;
    mat4 im = app.instanceMatrix(in);
    for (auto& rm : ri->rooms) {
      if (rm.cells <= 0 || rm.mx.x - rm.mn.x < 0.6f || rm.mx.y - rm.mn.y < 0.6f) continue;
      vec3 c((rm.mn.x + rm.mx.x) * 0.5f, (rm.y0 + rm.y1) * 0.5f, (rm.mn.y + rm.mx.y) * 0.5f);
      Renderer::RoomBox b; b.m = im * glm::translate(mat4(1), c); b.half = vec3((rm.mx.x - rm.mn.x) * 0.5f, (rm.y1 - rm.y0) * 0.5f, (rm.mx.y - rm.mn.y) * 0.5f);
      R.rooms.push_back(b);
      if (R.rooms.size() >= 256) return;
    }
  }
}
