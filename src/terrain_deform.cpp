// VFX rev3 spec 4.4: the ground deformation buffer (see terrain_deform.h). Storage texel i holds world texel w with
// w = i (mod N) inside the current window [o, o + N) (o = the window's lowest world texel index); the window follows the
// camera in whole texels. Each frame (when on): a pass clears the texels whose world texel changed with the move and,
// every 2 s, applies the decay; then the queued stamps are drawn by a second pass over each stamp's box.
#include "terrain_deform.h"
#include "texture_units.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
struct Stamp { vec2 a, b; float r, depth, dist; };
std::vector<Stamp> gQueue;
GLuint gTex = 0, gPass = 0, gStampP = 0;
int gN = 0; float gTexel = 0.0f; glm::ivec2 gO(0); bool gOn = false; double gDecayAcc = 0.0;

const char* kPass = R"GLSL(#version 450
layout(local_size_x = 16, local_size_y = 16) in;
layout(rg16f, binding = 0) uniform image2D uImg;
uniform ivec2 uOldO, uNewO; uniform int uN; uniform vec2 uK;
ivec2 worldOf(ivec2 i, ivec2 o) { return o + ((i - o) % uN + uN) % uN; }
void main() {
  ivec2 i = ivec2(gl_GlobalInvocationID.xy);
  if (i.x >= uN || i.y >= uN) return;
  vec2 v = imageLoad(uImg, i).rg;
  v = worldOf(i, uOldO) != worldOf(i, uNewO) ? vec2(0.0) : v * uK;   // a texel that now stands for new ground starts clean
  imageStore(uImg, i, vec4(v, 0.0, 0.0));
}
)GLSL";

const char* kStamp = R"GLSL(#version 450
layout(local_size_x = 16, local_size_y = 16) in;
layout(rg16f, binding = 0) uniform image2D uImg;
uniform ivec2 uO, uB0, uBN; uniform int uN; uniform float uTexel;
uniform vec4 uSeg; uniform vec3 uP;   // segment a.xy, b.xy (world xz); radius, depth, disturbance
void main() {
  ivec2 id = ivec2(gl_GlobalInvocationID.xy);
  if (id.x >= uBN.x || id.y >= uBN.y) return;
  ivec2 w = uB0 + id;
  if (any(lessThan(w, uO)) || any(greaterThanEqual(w, uO + ivec2(uN)))) return;   // outside the window: dropped
  vec2 p = (vec2(w) + 0.5) * uTexel, ab = uSeg.zw - uSeg.xy;
  float t = clamp(dot(p - uSeg.xy, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
  float d = length(p - (uSeg.xy + ab * t)), q = 1.0 - (d * d) / (uP.x * uP.x);
  if (q <= 0.0) return;
  float k = pow(q, 1.6);   // the soft bowl (spec 6.7 without the lip)
  ivec2 i = (w % uN + uN) % uN;
  vec2 v = imageLoad(uImg, i).rg;
  imageStore(uImg, i, vec4(max(v.x, uP.y * k), max(v.y, uP.z * k), 0.0, 0.0));   // max(): repeat passes do not dig deeper
}
)GLSL";

GLuint compute(const char* src) {
  GLuint s = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(s, 1, &src, nullptr); glCompileShader(s);
  GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[2048]; glGetShaderInfoLog(s, sizeof log, nullptr, log); logf("shader terrain_deform (cs) error:\n%s", log); }
  GLuint p = glCreateProgram(); glAttachShader(p, s); glLinkProgram(p); glDeleteShader(s);
  return ok ? p : 0;
}
}  // namespace

void deformStamp(vec3 p0, vec3 p1, float radius, float depth, float disturbance) {
  if (radius <= 0.0f || gQueue.size() > 4096) return;
  gQueue.push_back({vec2(p0.x, p0.z), vec2(p1.x, p1.z), radius, std::clamp(depth, 0.0f, 0.35f), std::clamp(disturbance, 0.0f, 1.0f)});
}

float deformDepthAtCPU(float, float) { return 0.0f; }

// The renderer, once a frame: tier 0 = off; 1 = 8 cm over 48 m; 2 = 4 cm over 64 m; 3 = 4 cm over 96 m.
void deformFrame(vec3 cam, float dt, int tier, bool& on, GLuint& tex, glm::vec4& map) {
  static const int kN[4] = {0, 600, 1600, 2400};
  static const float kT[4] = {0.0f, 0.08f, 0.04f, 0.04f};
  if (tier <= 0) { gOn = on = false; gQueue.clear(); tex = gTex; return; }
  if (!gPass) { gPass = compute(kPass); gStampP = compute(kStamp); }
  if (!gPass || !gStampP) { on = false; return; }
  if (gN != kN[tier] || !gTex) {
    if (gTex) glDeleteTextures(1, &gTex);
    gN = kN[tier]; gTexel = kT[tier];
    glCreateTextures(GL_TEXTURE_2D, 1, &gTex); glTextureStorage2D(gTex, 1, GL_RG16F, gN, gN);
    glTextureParameteri(gTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(gTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(gTex, GL_TEXTURE_WRAP_S, GL_REPEAT); glTextureParameteri(gTex, GL_TEXTURE_WRAP_T, GL_REPEAT);
    const float z[2] = {0.0f, 0.0f}; glClearTexImage(gTex, 0, GL_RG, GL_FLOAT, z);
    gO = glm::ivec2((int)std::floor(cam.x / gTexel), (int)std::floor(cam.z / gTexel)) - gN / 2;
  }
  {   // NMK_DEFORM_TEST="x0,z0,x1,z1": a pair of 0.1 m wheel ruts 1.6 m apart along that line, once the buffer is on (captures)
    static bool done = false; const char* e = std::getenv("NMK_DEFORM_TEST");
    float x0, z0, x1, z1;
    if (!done && e && std::sscanf(e, "%f,%f,%f,%f", &x0, &z0, &x1, &z1) == 4) {
      done = true; vec2 d = glm::normalize(vec2(x1 - x0, z1 - z0)), n(-d.y, d.x);
      for (float s : {-0.8f, 0.8f}) deformStamp(vec3(x0 + n.x * s, 0, z0 + n.y * s), vec3(x1 + n.x * s, 0, z1 + n.y * s), 0.32f, 0.10f, 1.0f);
    }
  }
  const glm::ivec2 nO = glm::ivec2((int)std::floor(cam.x / gTexel), (int)std::floor(cam.z / gTexel)) - gN / 2;
  gDecayAcc += dt;
  const bool decay = gDecayAcc >= 2.0;   // half floats: a per-frame factor this close to 1 rounds back to the same value
  if (nO != gO || decay) {
    const float t = decay ? (float)gDecayAcc : 0.0f;
    glUseProgram(gPass);
    glUniform2i(glGetUniformLocation(gPass, "uOldO"), gO.x, gO.y); glUniform2i(glGetUniformLocation(gPass, "uNewO"), nO.x, nO.y);
    glUniform1i(glGetUniformLocation(gPass, "uN"), gN);
    glUniform2f(glGetUniformLocation(gPass, "uK"), std::exp(-t / 300.0f), std::exp(-t / 1800.0f));   // tauR 300 s (soil), tauG 1800 s
    glBindImageTexture(0, gTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RG16F);
    glDispatchCompute((gN + 15) / 16, (gN + 15) / 16, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    gO = nO;
    if (decay) gDecayAcc = 0.0;
  }
  if (!gQueue.empty()) {
    glUseProgram(gStampP);
    glUniform2i(glGetUniformLocation(gStampP, "uO"), gO.x, gO.y); glUniform1i(glGetUniformLocation(gStampP, "uN"), gN);
    glUniform1f(glGetUniformLocation(gStampP, "uTexel"), gTexel);
    glBindImageTexture(0, gTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RG16F);
    for (const Stamp& s : gQueue) {
      vec2 lo = glm::min(s.a, s.b) - s.r, hi = glm::max(s.a, s.b) + s.r;
      glm::ivec2 b0((int)std::floor(lo.x / gTexel), (int)std::floor(lo.y / gTexel)), b1((int)std::ceil(hi.x / gTexel), (int)std::ceil(hi.y / gTexel));
      glm::ivec2 bn = glm::min(b1 - b0, glm::ivec2(gN));
      if (bn.x <= 0 || bn.y <= 0) continue;
      glUniform2i(glGetUniformLocation(gStampP, "uB0"), b0.x, b0.y); glUniform2i(glGetUniformLocation(gStampP, "uBN"), bn.x, bn.y);
      glUniform4f(glGetUniformLocation(gStampP, "uSeg"), s.a.x, s.a.y, s.b.x, s.b.y);
      glUniform3f(glGetUniformLocation(gStampP, "uP"), s.r, s.depth, s.dist);
      glDispatchCompute((bn.x + 15) / 16, (bn.y + 15) / 16, 1);
      glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);   // the next stamp reads what this one wrote (max())
    }
    gQueue.clear();
  }
  glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
  gOn = on = true; tex = gTex;
  map = glm::vec4(gO.x * gTexel, gO.y * gTexel, 1.0f / (gN * gTexel), gTexel);
}
