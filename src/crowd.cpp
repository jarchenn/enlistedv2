// GC1 / GC2 GPU crowd (crowd.h). GC1 drawing. Bake: each type's procedural poses (poseCharacter, the held gun's sockets) sampled once
// per clip into palettes, its mesh simplified per part to two LODs; frame: the units culled (last frame's view, sphere) and
// bucketed by type x LOD by projected height (LOD0 over 40 px, LOD1 down to 1.5 px, none below), uploaded once, drawn instanced.
#include "crowd.h"
#include "game.h"
#include "app.h"
#include "settings.h"
#include "simplify.h"
#include "frustum.h"
#include "physics.h"
#include "ai_hooks.h"
#include <cstring>
#include <map>
#include <unordered_map>
#include <queue>


// ---- GC5 impostors: the LOD0 body and its gun rendered unlit (albedo x base colour) from kImpDirs directions around the model
// (azimuth d * 45 deg, 10 deg above) into 64 px cells of a kImpBox m orthographic box centred kImpMid m up; kept frames: every
// 2nd of the walk / run / crouch walk / fire, every 4th of the die, all of idle / prone / dead
static const char* kImpBakeVS = R"(#version 450
)" NMK_SKIN_GLSL R"(
layout(location = 0) in vec3 aPos; layout(location = 3) in vec2 aUV;
uniform mat4 uVP; uniform int uOff, uGunJ; out vec2 vUV;
void main() {
  vec3 P;
  if (uGunJ > 0) { P = (gSkP[uOff + uGunJ] * vec4(aPos, 1.0)).xyz; vUV = aUV; }
  else { NmkSkinV v = gSkV[gl_VertexID]; P = (nmkSkinM(v, uOff) * vec4(v.pos.xyz, 1.0)).xyz; vUV = v.uv.xy; }
  gl_Position = uVP * vec4(P, 1.0);
})";
static const char* kImpBakeFS = R"(#version 450
in vec2 vUV; uniform sampler2D uAlb; uniform int uHasAlb; uniform vec4 uBase; out vec4 o;
void main() { vec4 c = uBase; if (uHasAlb == 1) c *= texture(uAlb, vUV); o = vec4(c.rgb, 1.0); })";
static GLuint impProg() {
  static GLuint p = 0; if (p) return p;
  auto sh = [](GLenum ty, const char* s) { GLuint h = glCreateShader(ty); glShaderSource(h, 1, &s, nullptr); glCompileShader(h); GLint ok = 0; glGetShaderiv(h, GL_COMPILE_STATUS, &ok);
                                           if (!ok) { char log[4096]; glGetShaderInfoLog(h, sizeof log, nullptr, log); logf("crowd: impostor bake shader: %s", log); } return h; };
  GLuint v = sh(GL_VERTEX_SHADER, kImpBakeVS), f = sh(GL_FRAGMENT_SHADER, kImpBakeFS);
  p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p); glDeleteShader(v); glDeleteShader(f);
  GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok); if (!ok) { glDeleteProgram(p); p = 0; logf("crowd: impostor bake program did not link"); }
  return p;
}
static bool bakeImpostor(CrowdType& t, std::string& err) {
  const int step[CC_COUNT] = {1, 2, 2, 2, 1, 2, 4, 1};
  std::vector<int> keep;   // palette frames kept
  for (int c = 0; c < CC_COUNT; ++c) for (int f = 0; f < t.clip[c].y; ++f) {
    const int pf = t.clip[c].x + f;
    if (f % step[c] == 0) keep.push_back(pf);
    if (pf < 64) t.impTab[pf] = (int)keep.size() - 1;
  }
  t.impFrames = (int)keep.size(); const int cells = t.impFrames * kImpDirs; t.impRows = (cells + t.impCols - 1) / t.impCols;
  const int W = t.impCols * kImpCell, Hh = t.impRows * kImpCell;
  const GLuint prog = impProg(); if (!prog) { err = "impostor bake program"; return false; }
  GLuint col, dep, fbo;
  glCreateTextures(GL_TEXTURE_2D, 1, &col); const int lv = 1 + (int)std::floor(std::log2((float)kImpCell)); glTextureStorage2D(col, lv, GL_SRGB8_ALPHA8, W, Hh);
  glCreateTextures(GL_TEXTURE_2D, 1, &dep); glTextureStorage2D(dep, 1, GL_DEPTH_COMPONENT32F, W, Hh);
  glCreateFramebuffers(1, &fbo); glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, col, 0); glNamedFramebufferTexture(fbo, GL_DEPTH_ATTACHMENT, dep, 0);
  GLint prevFbo = 0, vp[4]; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo); glGetIntegerv(GL_VIEWPORT, vp);
  const GLboolean cull = glIsEnabled(GL_CULL_FACE), blend = glIsEnabled(GL_BLEND), scis = glIsEnabled(GL_SCISSOR_TEST); GLint df = GL_LESS; glGetIntegerv(GL_DEPTH_FUNC, &df); GLdouble dr[2]; glGetDoublev(GL_DEPTH_RANGE, dr);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo); glDisable(GL_CULL_FACE); glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glDepthRange(0, 1); glEnable(GL_FRAMEBUFFER_SRGB);
  const float bg[4] = {0.30f, 0.29f, 0.25f, 0.0f}; const float one = 1.0f;   // a mid-brown under the silhouette: mips bleed toward it, not black
  glClearNamedFramebufferfv(fbo, GL_COLOR, 0, bg); glClearNamedFramebufferfv(fbo, GL_DEPTH, 0, &one);
  glUseProgram(prog); glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 15, t.lod[0].rest); glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 16, t.pal);
  const GLint lVP = glGetUniformLocation(prog, "uVP"), lOff = glGetUniformLocation(prog, "uOff"), lGun = glGetUniformLocation(prog, "uGunJ"), lHas = glGetUniformLocation(prog, "uHasAlb"), lBase = glGetUniformLocation(prog, "uBase");
  glUniform1i(glGetUniformLocation(prog, "uAlb"), 0);
  const mat4 proj = glm::ortho(-kImpBox * 0.5f, kImpBox * 0.5f, -kImpBox * 0.5f, kImpBox * 0.5f, 0.1f, 20.0f); const vec3 mid(0, kImpMid, 0);
  auto drawParts = [&](const GpuModel& look, const std::vector<ModelPart>& parts) {
    for (const ModelPart& pt : parts) {
      const int mi = pt.mat >= 0 && pt.mat < (int)look.mats.size() ? pt.mat : -1; const GLuint alb = mi >= 0 ? look.mats[mi].alb : 0;
      glBindTextureUnit(0, alb); glUniform1i(lHas, alb ? 1 : 0); glUniform4fv(lBase, 1, glm::value_ptr(pt.base));
      glDrawElements(GL_TRIANGLES, (GLsizei)pt.count, GL_UNSIGNED_INT, (void*)(uintptr_t)(pt.first * 4u));
    }
  };
  glEnable(GL_SCISSOR_TEST);
  for (int fi = 0; fi < t.impFrames; ++fi) for (int d = 0; d < kImpDirs; ++d) {
    const int cell = fi * kImpDirs + d, cx = cell % t.impCols, cy = cell / t.impCols;
    glViewport(cx * kImpCell, cy * kImpCell, kImpCell, kImpCell); glScissor(cx * kImpCell, cy * kImpCell, kImpCell, kImpCell);
    const float a = d * 6.2831853f / kImpDirs; const vec3 eye = mid + glm::normalize(vec3(std::sin(a), 0.18f, std::cos(a))) * 8.0f;
    const mat4 VP = proj * glm::lookAt(eye, mid, vec3(0, 1, 0)); glUniformMatrix4fv(lVP, 1, GL_FALSE, glm::value_ptr(VP));
    const int off = keep[fi] * (t.J + 1); glUniform1i(lOff, off);
    glUniform1i(lGun, 0); glBindVertexArray(t.lod[0].vao); drawParts(t.m->gpu, t.lod[0].parts);
    if (t.gun && t.gun->vao) { glUniform1i(lGun, t.J); glBindVertexArray(t.gun->vao); drawParts(*t.gun, t.gun->parts); }
  }
  glDisable(GL_FRAMEBUFFER_SRGB); if (!scis) glDisable(GL_SCISSOR_TEST);
  glBindVertexArray(0); glUseProgram(0); glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo); glViewport(vp[0], vp[1], vp[2], vp[3]);
  if (cull) glEnable(GL_CULL_FACE); if (blend) glEnable(GL_BLEND); glDepthFunc((GLenum)df); glDepthRange(dr[0], dr[1]);
  glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &dep);
  glGenerateTextureMipmap(col); glTextureParameteri(col, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTextureParameteri(col, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(col, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(col, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  t.impTex = col; t.bytes += (size_t)W * Hh * 4 * 4 / 3;
  // the quad and its material
  CrowdType::Lod& q = t.lod[2]; const uint32_t qi[6] = {0, 1, 2, 0, 2, 3};
  glCreateBuffers(1, &q.ebo); glNamedBufferStorage(q.ebo, sizeof qi, qi, 0); glCreateVertexArrays(1, &q.vao); glVertexArrayElementBuffer(q.vao, q.ebo);
  ModelPart pt; pt.first = 0; pt.count = 6; pt.mat = 0; pt.base = vec4(1); pt.metal = 0; pt.rough = 0.9f; q.parts = {pt}; q.verts = 4; q.tris = 2;
  GpuModel::MatTex mt; mt.alb = col; t.impLook.mats = {mt};
  return true;
}

float crowdImpCoverage(const CrowdType& t, int frame, int dir) {
  if (!t.impTex || frame < 0 || frame >= t.impFrames) return 0;
  const int cell = frame * kImpDirs + dir, cx = cell % t.impCols, cy = cell / t.impCols; std::vector<uint8_t> px((size_t)kImpCell * kImpCell * 4);
  glGetTextureSubImage(t.impTex, 0, cx * kImpCell, cy * kImpCell, 0, kImpCell, kImpCell, 1, GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)px.size(), px.data());
  int n = 0; for (size_t k = 3; k < px.size(); k += 4) n += px[k] > 127; return (float)n / (kImpCell * kImpCell);
}

bool crowdBake(CrowdType& t, const CharModel& m, const WeaponDef* w, std::string& err) {
  t = CrowdType{}; t.m = &m; t.J = m.joints(); t.gun = w ? w->worldModel() : nullptr;
  if (t.J <= 0 || m.verts.empty() || m.indices.empty()) { err = "no skin"; return false; }
  // ---- poses: clip -> frames of PoseParams
  struct C { int n; bool loop; float period; };
  const C cl[CC_COUNT] = {{1, true, 1}, {16, true, 16 / 15.0f}, {12, true, 12 / 15.0f}, {16, true, 16 / 15.0f}, {1, true, 1}, {4, true, 4 / 15.0f}, {12, false, 12 / 15.0f}, {1, true, 1}};
  PoseParams base; base.holdWeapon = w != nullptr;
  if (w) { base.gripR = w->gripR; base.gripL = w->gripL; base.magPos = w->gripL + vec3(0, -0.1f, -0.12f); base.holdOffset = w->holdOffset; base.holdWeld = w->holdWeld; base.holdKind = w->holdKind; base.leftHold = w->leftHold;
           base.gripC = w->gripC; base.gripAx = w->gripAx; base.gripRad = w->gripRad; base.guardC = w->guardC; base.guardRad = w->guardRad; base.sightR = w->rear; }
  std::vector<mat4> P; PoseResult pr;
  for (int c = 0; c < CC_COUNT; ++c) {
    t.clip[c] = glm::ivec4(t.frames, cl[c].n, cl[c].loop ? 1 : 0, 0); t.dur[c] = cl[c].period;
    for (int f = 0; f < cl[c].n; ++f) {
      PoseParams pp = base; const float u = (float)f / cl[c].n;
      switch (c) {
        case CC_WALK: pp.walkAmount = 0.5f; pp.walkPhase = 6.2831853f * u; break;
        case CC_RUN: pp.walkAmount = 1.0f; pp.walkPhase = 6.2831853f * u; pp.lowReady = 0.2f; break;
        case CC_CROUCH: pp.walkAmount = 0.4f; pp.walkPhase = 6.2831853f * u; pp.crouch = 1; break;
        case CC_PRONE: pp.prone = 1; break;
        case CC_FIRE: pp.recoil = 1.0f - u; break;
        case CC_DIE: pp.death = (float)f / std::max(1, cl[c].n - 1); break;
        case CC_DEAD: pp.death = 1; break;
        default: break;
      }
      poseCharacter(m, pp, pr);
      for (int j = 0; j < t.J; ++j) P.push_back(j < (int)pr.skin.size() ? pr.skin[j] : mat4(1));
      P.push_back(pr.weapon);
      ++t.frames;
    }
  }
  glCreateBuffers(1, &t.pal); glNamedBufferStorage(t.pal, P.size() * sizeof(mat4), P.data(), 0); t.bytes += P.size() * sizeof(mat4);
  // ---- LODs: each part simplified on its own (its material range kept); a corner keeps its source vertex's joints and weights
  const int target[2] = {3000, 300};
  const float total = (float)std::max(1, (int)m.indices.size() / 3);
  for (int L = 0; L < 2; ++L) {
    CrowdType::Lod& lod = t.lod[L]; std::vector<CharModel::SkinVert> rv; std::vector<uint32_t> ri;
    for (const ModelPart& pt : m.gpu.parts) {
      if (pt.role == "hidden" || pt.count < 3) continue;
      std::unordered_map<uint32_t, uint32_t> loc; std::vector<TexVertex> tv; std::vector<uint32_t> ti;
      for (uint32_t k = pt.first; k < pt.first + pt.count && k < m.indices.size(); ++k) {
        const uint32_t s = m.indices[k]; auto it = loc.find(s);
        if (it == loc.end()) { it = loc.emplace(s, (uint32_t)tv.size()).first; const auto& sv = m.verts[s]; tv.push_back({sv.pos, sv.nrm, vec4((float)s, 0, 0, 1), sv.uv}); }   // tan.x: the source vertex (exact below 2^24)
        ti.push_back(it->second);
      }
      const size_t want = std::max<size_t>(4, (size_t)std::lround(target[L] * (pt.count / 3) / total));
      simplifyMesh(tv, ti, want);
      ModelPart np = pt; np.first = (uint32_t)ri.size(); const uint32_t b0 = (uint32_t)rv.size();
      for (const TexVertex& v : tv) { CharModel::SkinVert sv = m.verts[(size_t)v.tan.x]; sv.pos = v.pos; rv.push_back(sv); }
      for (uint32_t i : ti) ri.push_back(b0 + i);
      np.count = (uint32_t)ri.size() - np.first; if (np.count) lod.parts.push_back(np);
    }
    lod.verts = (int)rv.size(); lod.tris = (int)ri.size() / 3;
    if (rv.empty()) { err = "empty LOD"; return false; }
    glCreateBuffers(1, &lod.rest); glNamedBufferStorage(lod.rest, rv.size() * sizeof(CharModel::SkinVert), rv.data(), 0);
    glCreateBuffers(1, &lod.ebo); glNamedBufferStorage(lod.ebo, ri.size() * 4, ri.data(), 0);
    glCreateVertexArrays(1, &lod.vao); glVertexArrayElementBuffer(lod.vao, lod.ebo);
    t.bytes += rv.size() * sizeof(CharModel::SkinVert) + ri.size() * 4;
  }
  if (!bakeImpostor(t, err)) return false;   // GC5
  logf("crowd: %s baked: %d frames x %d joints, LOD0 %d tris / %d verts, LOD1 %d tris / %d verts, impostor %d frames x %d views in %dx%d, %.1f MB", m.id.c_str(), t.frames, t.J,
       t.lod[0].tris, t.lod[0].verts, t.lod[1].tris, t.lod[1].verts, t.impFrames, kImpDirs, t.impCols * kImpCell, t.impRows * kImpCell, t.bytes / 1048576.0);
  return true;
}

void crowdDestroy(CrowdType& t) {
  if (t.pal) glDeleteBuffers(1, &t.pal);
  for (auto& l : t.lod) { if (l.rest) glDeleteBuffers(1, &l.rest); if (l.ebo) glDeleteBuffers(1, &l.ebo); if (l.vao) glDeleteVertexArrays(1, &l.vao); }
  if (t.impTex) glDeleteTextures(1, &t.impTex);
  t = CrowdType{};
}

// ---------------------------------------------------------------- GC2: units on the GPU
namespace {
constexpr int kCap = 131072, kMaxTypes = 8, kSlots = 8;   // units; body x gun types (buckets = types x 2 LODs); units a hash cell holds
struct Grid { vec2 org{0}; float cell = 2; int nx = 0, nz = 0; std::vector<float> h; std::vector<uint8_t> walk; };
struct GpuCrowd {
  std::vector<int> typeKey;                 // type index -> key (model * 4096 + gun + 1)
  std::map<int, CrowdType> types;           // by key
  int n = 0;                                // units spawned
  std::vector<CrowdUnit> stageU; std::vector<CrowdSim> stageS; int stageFrom = 0;   // spawns not yet uploaded
  GLuint units = 0, sim = 0, cnt = 0, idx = 0, height = 0, costBuf = 0, vis = 0, visCnt = 0, cmd = 0, cmdBucket = 0;
  GLuint progHash = 0, progMove = 0, progCull = 0, progCmd = 0, progFight = 0;
  GLuint pc = 0, pid = 0, dmg = 0, deaths = 0; int cx = 0, cz = 0; float ccell = 16; int frame = 0;   // GC3
  int spawned[2] = {0, 0}, dead[2] = {0, 0}, promoted[2] = {0, 0}; double nextDeaths = 0;
  GLuint nearBuf = 0; bool nearOn = false; std::vector<std::pair<glm::uvec4, vec4>> nearRecs;   // GC4
  std::vector<vec4> hum; bool armyInit = false; int goalFor = -2; double nextDemote = 0; std::vector<float> farFor;
  Grid g; bool gridOk = false; int nCmd = 0; std::vector<int> cmdFirstOf;   // per bucket: its first command
  bool costDirty[2] = {false, false}; std::vector<float> cost[2]; double lastT = -1;
} C;

const char* kCrowdCommon = R"(#version 450
layout(local_size_x = 128) in;
struct U { vec4 pos; vec4 prev; vec4 anim; };
struct S { vec4 a; ivec4 b; vec4 c; };
layout(std430, binding = 0) buffer Units { U u[]; };
layout(std430, binding = 1) buffer Sims { S sm[]; };
layout(std430, binding = 2) buffer Cnt { uint cnt[]; };
layout(std430, binding = 3) buffer Idx { uint idx[]; };
layout(std430, binding = 4) readonly buffer Height { float hgt[]; };
layout(std430, binding = 5) readonly buffer Cost { float cst[]; };   // [team][cell]
layout(std430, binding = 10) buffer PCnt { uint pc[]; };    // GC3: [team][coarse cell] alive units
layout(std430, binding = 11) buffer PIdx { uint pid[]; };   // one of them (the last written)
layout(std430, binding = 12) buffer Dmg { uint dmg[]; };    // damage taken this frame, per unit
layout(std430, binding = 13) buffer Deaths { uint nd; uint dl[]; };   // deaths: (unit, team) pairs
struct Near { uvec4 a; vec4 p; };                            // GC4: unit, team, type, hp; its feet + yaw
layout(std430, binding = 14) buffer NearL { uint nn; uint pad0, pad1, pad2; Near nr[]; };
uniform vec4 uHum[16]; uniform int uNH, uNearOn;              // GC4: the humans' bubbles (xyz, radius)
uniform int uN, uNx, uNz, uCx, uCz, uFrame; uniform vec2 uOrg; uniform float uCell, uDt, uCCell;
ivec2 coarseOf(vec2 p) { return clamp(ivec2(floor((p - uOrg) / uCCell)), ivec2(0), ivec2(uCx - 1, uCz - 1)); }
float rnd(uint a, uint b) { uint h = a * 747796405u + b * 2891336453u + 12345u; h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u; h = (h >> 22u) ^ h; return float(h) / 4294967295.0; }
float erfa(float x) { return tanh(1.1283792 * x + 0.1009 * x * x * x); }   // erf to ~3e-3
ivec2 cellOf(vec2 p) { return clamp(ivec2(floor((p - uOrg) / uCell)), ivec2(0), ivec2(uNx - 1, uNz - 1)); }
float H(vec2 p) {   // bilinear terrain height
  vec2 g = (p - uOrg) / uCell - 0.5; ivec2 c = clamp(ivec2(floor(g)), ivec2(0), ivec2(uNx - 2, uNz - 2)); vec2 f = clamp(g - vec2(c), 0.0, 1.0);
  float a = hgt[c.y * uNx + c.x], b = hgt[c.y * uNx + c.x + 1], d = hgt[(c.y + 1) * uNx + c.x], e = hgt[(c.y + 1) * uNx + c.x + 1];
  return mix(mix(a, b, f.x), mix(d, e, f.x), f.y);
}
float K(int team, ivec2 c) { c = clamp(c, ivec2(0), ivec2(uNx - 1, uNz - 1)); return cst[team * uNx * uNz + c.y * uNx + c.x]; }
)";
const char* kHashCS = R"(
void main() { uint i = gl_GlobalInvocationID.x; if (i >= uint(uN) || sm[i].b.z != 0) return;
  ivec2 c = cellOf(u[i].pos.xz); uint ci = uint(c.y * uNx + c.x); uint k = atomicAdd(cnt[ci], 1u); if (k < 8u) idx[ci * 8u + k] = i;
  ivec2 q = coarseOf(u[i].pos.xz); uint qi = uint(clamp(sm[i].b.x, 0, 1) * uCx * uCz + q.y * uCx + q.x); atomicAdd(pc[qi], 1u); pid[qi] = i; })";
// GC3 fight: a target from the coarse presence grid (on a 30-frame stagger, or when the old one fell), then shots: hit
// probability from the shared aim model (sigma, the spread at range against a 0.5 x 1.7 m standing body), terrain line of sight
// (4 samples of the height grid), damage added to the target for the move pass; melee at contact
const char* kFightCS = R"(
void main() { uint i = gl_GlobalInvocationID.x; if (i >= uint(uN) || sm[i].b.z != 0) return;
  S s = sm[i]; vec3 p = u[i].pos.xyz; int t = clamp(s.b.x, 0, 1), o = 1 - t, tg = s.b.w;
  if (tg >= 0 && sm[tg].b.z != 0) tg = -1;
  if (tg < 0 || (int(i) + uFrame) % 30 == 0) {
    ivec2 c = coarseOf(p.xz); int R = int(ceil(s.c.w / uCCell)); float bd = 1e30; int bt = -1;
    for (int r = 0; r <= R && bt < 0; ++r) for (int dz = -r; dz <= r; ++dz) for (int dx = -r; dx <= r; ++dx) {
      if (max(abs(dx), abs(dz)) != r) continue; ivec2 q = c + ivec2(dx, dz); if (q.x < 0 || q.y < 0 || q.x >= uCx || q.y >= uCz) continue;
      uint qi = uint(o * uCx * uCz + q.y * uCx + q.x); if (pc[qi] == 0u) continue;
      int j = int(pid[qi]); float d = length(u[j].pos.xz - p.xz); if (d < bd) { bd = d; bt = j; }
    }
    if (bt >= 0 && bd <= s.c.w) tg = bt; else if (bt < 0) tg = -1;
  }
  s.b.w = tg; s.c.x -= uDt; s.c.y = max(0.0, s.c.y - uDt);
  if (tg >= 0 && s.c.x <= 0.0) {
    vec3 q = u[tg].pos.xyz; float d = length(q - p);
    if (d <= s.c.w) {
      s.c.x = 1.25 + rnd(i, uint(uFrame)) * 0.8; s.c.y = 0.27;
      bool los = true; for (int k = 1; k <= 4; ++k) { vec3 m = mix(p, q, float(k) / 5.0); if (H(m.xz) > m.y + 1.35) los = false; }
      float ph = d < 1.6 ? 0.5 : 0.0;
      if (los && d >= 1.6) { float sr = max(0.02, d * tan(s.c.z) * (length(s.a.xy) > 0.5 ? 1.6 : 1.0)); ph = erfa(0.25 / (1.41421 * sr)) * erfa(0.85 / (1.41421 * sr)); }
      if (rnd(i * 3u + 1u, uint(uFrame)) < ph) atomicAdd(dmg[tg], uint(40.0 + 60.0 * rnd(i * 7u + 3u, uint(uFrame))));
    }
  }
  sm[i] = s; })";
const char* kMoveCS = R"(
void main() { uint i = gl_GlobalInvocationID.x; if (i >= uint(uN)) return;
  U x = u[i]; S s = sm[i]; x.prev = x.pos;
  if (s.b.z == 0 && dmg[i] > 0u) { s.a.z -= float(dmg[i]); dmg[i] = 0u;   // GC3: the frame's hits
    if (s.a.z <= 0.0) { s.b.z = 1; x.anim.x = 6.0; x.anim.y = 0.0; s.a.xy = vec2(0); uint k = atomicAdd(nd, 1u); if (k < 65536u) { dl[k * 2u] = i; dl[k * 2u + 1u] = uint(s.b.x); } } }
  if (s.b.z != 0) { x.anim.y += uDt; if (s.b.z == 1 && x.anim.y > 0.8) { s.b.z = 2; x.anim.x = 7.0; x.anim.y = 0.0; } else if (s.b.z == 2 && x.anim.y > 60.0) s.b.z = 3; u[i] = x; sm[i] = s; return; }   // dying -> dead -> gone (60 s)
  vec2 p = x.pos.xz; ivec2 c = cellOf(p); int t = clamp(s.b.x, 0, 1);
  // down the flow field: the cheapest of the 8 neighbours (unwalkable = huge), blended with the cost gradient
  float k0 = K(t, c); vec2 best = vec2(0); float bk = k0;
  for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) { if (dx == 0 && dz == 0) continue; float k = K(t, c + ivec2(dx, dz)); if (k < bk) { bk = k; best = normalize(vec2(dx, dz)); } }
  vec2 want = k0 < 6.0 || bk >= k0 ? vec2(0) : best;   // at the goal (or boxed in): hold
  // GC3 / GC4c: a target in range is fired at; the unit keeps closing (firing on the move, its aim widened) until 70 m, then
  // stands. Stopping at the range's edge (150 m, ~1.6 % a shot) stalled a battle line with only its front rank firing
  int tg = s.b.w; float tdist = tg >= 0 ? length(u[tg].pos.xz - p) : 1e9; bool engaged = tdist <= min(s.c.w, 70.0);
  if (engaged) want = vec2(0);
  bool inB = false; for (int h = 0; h < uNH; ++h) { vec2 d = p - uHum[h].xz; if (dot(d, d) < uHum[h].w * uHum[h].w) inB = true; }   // GC4: inside a human's bubble
  if (inB) { want = vec2(0); if (uNearOn == 1) { uint k = atomicAdd(nn, 1u); if (k < 1024u) { nr[k].a = uvec4(i, uint(clamp(s.b.x, 0, 1)), uint(s.b.y), uint(max(s.a.z, 1.0))); nr[k].p = x.pos; } } }
  // separation from the 3 x 3 cells' units
  vec2 push = vec2(0);
  for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
    ivec2 q = c + ivec2(dx, dz); if (q.x < 0 || q.y < 0 || q.x >= uNx || q.y >= uNz) continue; uint ci = uint(q.y * uNx + q.x), m = min(cnt[ci], 8u);
    for (uint k = 0u; k < m; ++k) { uint j = idx[ci * 8u + k]; if (j == i) continue; vec2 d = p - u[j].pos.xz; float l = length(d); if (l > 1e-3 && l < 0.9) push += d / l * (0.9 - l); }
  }
  vec2 v = s.a.xy, tv = want * s.a.w + push * 2.5;
  v += (tv - v) * clamp(uDt * 4.0, 0.0, 1.0);
  float sp = length(v); if (sp > s.a.w * 1.3) { v *= s.a.w * 1.3 / sp; sp = s.a.w * 1.3; }
  p += v * uDt; ivec2 nc = cellOf(p); if (K(t, nc) > 1e8) { p = x.pos.xz; v = vec2(0); sp = 0.0; }   // never onto an unwalkable cell
  x.pos.xz = p; x.pos.y = H(p);
  if (tg >= 0 && tdist <= s.c.w && (engaged || sp < 0.2)) { vec2 e = u[tg].pos.xz - p; float yw = atan(-e.x, -e.y); float d = yw - x.pos.w; d = mod(d + 3.14159265, 6.2831853) - 3.14159265; x.pos.w += d * clamp(uDt * 6.0, 0.0, 1.0); }
  else if (sp > 0.2) { float yw = atan(-v.x, -v.y); float d = yw - x.pos.w; d = mod(d + 3.14159265, 6.2831853) - 3.14159265; x.pos.w += d * clamp(uDt * 6.0, 0.0, 1.0); }
  float clip = s.c.y > 0.0 ? 5.0 : sp > 2.6 ? 2.0 : sp > 0.35 ? 1.0 : 0.0;   // GC3: the fire clip while a shot is in its window
  if (clip != x.anim.x && (x.anim.x <= 2.0 || x.anim.x == 5.0)) { if (clip == 5.0) x.anim.y = 0.0; x.anim.x = clip; }
  x.anim.y += uDt * (clip == 1.0 ? sp / 1.4 : clip == 2.0 ? sp / 4.0 : 1.0);
  s.a.xy = v; u[i] = x; sm[i] = s; })";
const char* kCullCS = R"(
layout(std430, binding = 6) buffer Vis { uint vis[]; };
layout(std430, binding = 7) buffer VisCnt { uint vcnt[]; };
uniform vec4 uPl[6]; uniform vec3 uEye; uniform float uK, uCapB;
void main() { uint i = gl_GlobalInvocationID.x; if (i >= uint(uN) || sm[i].b.z == 3) return;
  vec3 c = u[i].pos.xyz + vec3(0, 0.9, 0);
  for (int k = 0; k < 6; ++k) if (dot(uPl[k].xyz, c) + uPl[k].w < -1.3) return;
  float px = uK * u[i].anim.w / max(0.5, length(c - uEye)); if (px < 1.5) return;
  uint b = uint(sm[i].b.y) * 3u + (px > 40.0 ? 0u : px > 12.0 ? 1u : 2u);   // GC5: LOD0 over 40 px, LOD1 12-40, the impostor 1.5-12 uint k = atomicAdd(vcnt[b], 1u); if (k < uint(uCapB)) vis[b * uint(uCapB) + k] = i; })";
const char* kCmdCS = R"(#version 450
layout(local_size_x = 64) in;
struct Cmd { uint count, inst, first, baseV, baseI; };
layout(std430, binding = 7) readonly buffer VisCnt { uint vcnt[]; };
layout(std430, binding = 8) buffer Cmds { Cmd cmd[]; };
layout(std430, binding = 9) readonly buffer CmdB { uint cb[]; };
uniform int uNCmd; uniform uint uCapB;
void main() { uint k = gl_GlobalInvocationID.x; if (k >= uint(uNCmd)) return; cmd[k].inst = min(vcnt[cb[k]], uCapB); })";

GLuint compileCS(const std::string& src, const char* name) {
  GLuint s = glCreateShader(GL_COMPUTE_SHADER); const char* p = src.c_str(); glShaderSource(s, 1, &p, nullptr); glCompileShader(s);
  GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof log, nullptr, log); logf("crowd: %s: %s", name, log); glDeleteShader(s); return 0; }
  GLuint pr = glCreateProgram(); glAttachShader(pr, s); glLinkProgram(pr); glDeleteShader(s); glGetProgramiv(pr, GL_LINK_STATUS, &ok);
  if (!ok) { logf("crowd: %s link failed", name); glDeleteProgram(pr); return 0; }
  return pr;
}
GLuint ssbo(size_t bytes, const void* data = nullptr) { GLuint b; glCreateBuffers(1, &b); glNamedBufferStorage(b, std::max<size_t>(bytes, 16), data, GL_DYNAMIC_STORAGE_BIT); return b; }
constexpr int kCapB = kCap;   // a bucket's visible list (worst case: every unit one type and LOD)

// the walkable 2 m grid over the build area: the navmesh where it is baked (a polygon within 1.5 m of the ground), else slope
bool buildGrid(Game& g) {
  const Terrain& T = g.app.terrain; Grid& G = C.g;
  const float hx = T.buildHalfX, hz = T.buildHalfZ; G.cell = std::max(2.0f, std::max(hx, hz) * 2.0f / 1024.0f);
  G.nx = std::max(2, (int)std::ceil(2 * hx / G.cell)); G.nz = std::max(2, (int)std::ceil(2 * hz / G.cell)); G.org = vec2(-hx, -hz);
  G.h.assign((size_t)G.nx * G.nz, 0); G.walk.assign((size_t)G.nx * G.nz, 1);
  for (int z = 0; z < G.nz; ++z) for (int x = 0; x < G.nx; ++x) { const float px = G.org.x + (x + 0.5f) * G.cell, pz = G.org.y + (z + 0.5f) * G.cell; G.h[(size_t)z * G.nx + x] = T.height(px, pz); }
  const bool nav = g.nav.valid(); int walk = 0;
  for (int z = 0; z < G.nz; ++z) for (int x = 0; x < G.nx; ++x) {
    const size_t c = (size_t)z * G.nx + x; const float px = G.org.x + (x + 0.5f) * G.cell, pz = G.org.y + (z + 0.5f) * G.cell, h = G.h[c];
    bool ok = h > T.waterLevel + 0.3f;
    if (ok && nav) { NavRef r = 0; vec3 on; ok = g.nav.closest(vec3(px, h, pz), vec3(G.cell * 0.6f, 1.5f, G.cell * 0.6f), r, on) && std::fabs(on.y - h) < 1.5f; }
    else if (ok) { const float sx = x + 1 < G.nx ? std::fabs(G.h[c + 1] - h) : 0, sz = z + 1 < G.nz ? std::fabs(G.h[c + G.nx] - h) : 0; ok = std::max(sx, sz) < G.cell * 0.7f; }
    G.walk[c] = ok; walk += ok;
  }
  logf("crowd: grid %d x %d cells of %.1f m (%s), %d walkable", G.nx, G.nz, G.cell, nav ? "navmesh" : "slope", walk);
  return walk > 0;
}
// Dijkstra over 8 neighbours from the goal's cell (or the walkable cell nearest it); unwalkable 1e9
void buildCost(int team, vec3 goal) {
  Grid& G = C.g; const int N = G.nx * G.nz; std::vector<float>& d = C.cost[team]; d.assign(N, 1e9f);
  int gx = std::clamp((int)((goal.x - G.org.x) / G.cell), 0, G.nx - 1), gz = std::clamp((int)((goal.z - G.org.y) / G.cell), 0, G.nz - 1);
  if (!G.walk[(size_t)gz * G.nx + gx]) { int best = -1; float bd = 1e30f; for (int c = 0; c < N; ++c) if (G.walk[c]) { float dx = (float)(c % G.nx - gx), dz = (float)(c / G.nx - gz), e = dx * dx + dz * dz; if (e < bd) { bd = e; best = c; } } if (best < 0) return; gx = best % G.nx; gz = best / G.nx; }
  using Q = std::pair<float, int>; std::priority_queue<Q, std::vector<Q>, std::greater<Q>> q;
  d[(size_t)gz * G.nx + gx] = 0; q.push({0.0f, gz * G.nx + gx});
  while (!q.empty()) {
    auto [dc, c] = q.top(); q.pop(); if (dc > d[c]) continue;
    const int cx = c % G.nx, cz = c / G.nx;
    for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
      if (!dx && !dz) continue; const int x = cx + dx, z = cz + dz; if (x < 0 || z < 0 || x >= G.nx || z >= G.nz) continue;
      const int n = z * G.nx + x; if (!G.walk[n]) continue;
      if (dx && dz && (!G.walk[cz * G.nx + x] || !G.walk[z * G.nx + cx])) continue;   // no corner cutting
      const float nd = dc + G.cell * (dx && dz ? 1.41421356f : 1.0f) * (1.0f + 0.5f * std::fabs(G.h[n] - G.h[c]) / G.cell);   // climbs cost more
      if (nd < d[n]) { d[n] = nd; q.push({nd, n}); }
    }
  }
  C.costDirty[team] = true;
}
bool ensureGpu(Game* g) {   // g null: the grid is already set (the self-test)
  if (C.units) return C.progMove != 0;
  if (!C.gridOk && g) C.gridOk = buildGrid(*g);
  if (!C.gridOk) return false;
  const std::string com = kCrowdCommon;
  C.progHash = compileCS(com + kHashCS, "hash"); C.progFight = compileCS(com + kFightCS, "fight"); C.progMove = compileCS(com + kMoveCS, "move"); C.progCull = compileCS(com + kCullCS, "cull"); C.progCmd = compileCS(kCmdCS, "commands");
  const size_t cells = (size_t)C.g.nx * C.g.nz;
  C.units = ssbo(kCap * sizeof(CrowdUnit)); C.sim = ssbo(kCap * sizeof(CrowdSim)); C.cnt = ssbo(cells * 4); C.idx = ssbo(cells * kSlots * 4);
  C.height = ssbo(cells * 4, C.g.h.data()); C.costBuf = ssbo(cells * 2 * 4);
  C.vis = ssbo((size_t)kMaxTypes * 3 * kCapB * 4); C.visCnt = ssbo(kMaxTypes * 3 * 4);
  C.cx = std::max(1, (int)std::ceil(C.g.nx * C.g.cell / C.ccell)); C.cz = std::max(1, (int)std::ceil(C.g.nz * C.g.cell / C.ccell));
  C.nearBuf = ssbo(16 + 1024 * 32); { const uint32_t z = 0; glClearNamedBufferData(C.nearBuf, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &z); }
  C.pc = ssbo((size_t)2 * C.cx * C.cz * 4); C.pid = ssbo((size_t)2 * C.cx * C.cz * 4); C.dmg = ssbo(kCap * 4); C.deaths = ssbo(4 + 65536 * 8);
  { const uint32_t z = 0; glClearNamedBufferData(C.dmg, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &z); glClearNamedBufferData(C.deaths, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &z); }
  for (int t = 0; t < 2; ++t) if (C.cost[t].empty()) { C.cost[t].assign(cells, 0.0f); for (size_t c = 0; c < cells; ++c) if (!C.g.walk[c]) C.cost[t][c] = 1e9f; C.costDirty[t] = true; }   // no goal yet: stand
  logf("crowd: GPU buffers %.1f MB for %d units", (kCap * (sizeof(CrowdUnit) + sizeof(CrowdSim)) + cells * (4 + kSlots * 4 + 4 + 8) + (size_t)kMaxTypes * 3 * kCapB * 4) / 1048576.0, kCap);
  return C.progHash && C.progMove && C.progCull && C.progCmd && C.progFight;
}
int typeIndex(Game& g, int model, int gun) {
  const int key = model * 4096 + std::max(-1, gun) + 1;
  for (int k = 0; k < (int)C.typeKey.size(); ++k) if (C.typeKey[k] == key) return k;
  if ((int)C.typeKey.size() >= kMaxTypes || model < 0 || model >= (int)g.models.size()) return -1;
  std::string err; CrowdType& t = C.types[key];
  if (!crowdBake(t, *g.models[model], gun >= 0 && gun < (int)g.app.weaponDefs.size() ? g.app.weaponDefs[gun].get() : nullptr, err)) { logf("crowd: type %d: %s", model, err.c_str()); C.types.erase(key); return -1; }
  C.typeKey.push_back(key);
  // indirect commands: per type x LOD, the body's parts then the gun's
  std::vector<uint32_t> cmds, cb; C.cmdFirstOf.clear(); C.nCmd = 0;
  for (int ti = 0; ti < (int)C.typeKey.size(); ++ti) for (int L = 0; L < 3; ++L) {
    const CrowdType& ct = C.types[C.typeKey[ti]]; const int b = ti * 3 + L;
    C.cmdFirstOf.push_back(C.nCmd);
    for (const ModelPart& p : ct.lod[L].parts) { uint32_t c[5] = {p.count, 0, p.first, 0, 0}; cmds.insert(cmds.end(), c, c + 5); cb.push_back(b); ++C.nCmd; }
    C.cmdFirstOf.push_back(C.nCmd);
    if (ct.gun && L < 2) for (const ModelPart& p : ct.gun->parts) { uint32_t c[5] = {p.count, 0, p.first, 0, 0}; cmds.insert(cmds.end(), c, c + 5); cb.push_back(b); ++C.nCmd; }
  }
  if (C.cmd) { glDeleteBuffers(1, &C.cmd); glDeleteBuffers(1, &C.cmdBucket); }
  glCreateBuffers(1, &C.cmd); glNamedBufferStorage(C.cmd, std::max<size_t>(16, cmds.size() * 4), cmds.data(), 0);
  C.cmdBucket = ssbo(cb.size() * 4, cb.data());
  return (int)C.typeKey.size() - 1;
}
void testSource(Game& g) {   // NMK_CROWD=n: two armies of n / 2 on their halves of the build area, each walking to the other's
  static const int n = std::getenv("NMK_CROWD") ? std::atoi(std::getenv("NMK_CROWD")) : 0;
  if (n <= 0 || C.n > 0) return;
  auto model = [&](const char* fac) { const json& l = g.combat.value("models", json::object()).value(fac, json::array()); if (l.empty()) return -1; auto it = g.modelIdx.find(l[0].get<std::string>()); return it == g.modelIdx.end() ? -1 : it->second; };
  auto gun = [&](const char* id) { for (int k = 0; k < (int)g.app.weaponDefs.size(); ++k) if (g.app.weaponDefs[k]->id == id) return k; return -1; };
  const int mA = model("Allies"), mX = model("Axis"), gA = gun("m1_garand"), gX = gun("kar98k");
  const float hx = g.app.terrain.buildHalfX * 0.85f, hz = g.app.terrain.buildHalfZ * 0.85f; const int side = (int)std::ceil(std::sqrt((double)n));
  for (int i = 0; i < n; ++i) {
    const float x = -hx + 2 * hx * ((i % side) + 0.5f) / side, z = -hz + 2 * hz * ((i / side) + 0.5f) / side; const int team = x > 0 ? 1 : 0;
    crowdSpawn(g, vec3(x, 0, z), team ? 1.5708f : -1.5708f, team, team ? mX : mA, team ? gX : gA, i % 7 == 0 ? 4.0f : 1.4f);
  }
  crowdGoal(g, 0, vec3(hx, 0, 0)); crowdGoal(g, 1, vec3(-hx, 0, 0));
  logf("crowd: test source, %d units", C.n);
}
}

bool crowdShadersBuild(std::string& err) {   // --check-crowd: every crowd compute program compiles and links on this driver
  const std::string com = kCrowdCommon; const std::pair<std::string, const char*> src[5] = {{com + kHashCS, "hash"}, {com + kFightCS, "fight"}, {com + kMoveCS, "move"}, {com + kCullCS, "cull"}, {kCmdCS, "commands"}};
  for (auto& [s, n] : src) { GLuint p = compileCS(s, n); if (!p) { err = n; return false; } glDeleteProgram(p); }
  return true;
}
static void uploadStaged(); static void bindAll(); static void simPasses(float dt); static void readDeaths();   // below crowdFrame
// ---- --check-crowd: the simulation on a synthetic map (no Game): 200 x 200 cells of 2 m, flat at y 5. Two armies of 2,000
// (20 ranks deep along x, 100 files along z, 1.5 m apart) each sent to the other's side, every unit read back at the end.
//  march: 200 m apart with a wall of unwalkable cells at x 0..2 m from z -200 to +80 (the way round: north of it), 30 s
//  fight: 80 m apart, no wall, 20 s: the shared aim model at 70-110 m gives ~4-7 % a shot, ~12 shots a unit: hundreds fall
std::vector<std::pair<bool, std::string>> crowdSimSelfTest() {
  std::vector<std::pair<bool, std::string>> r; auto chk = [&](bool ok, const std::string& w) { r.push_back({ok, w}); };
  auto run = [&](const char* name, bool wall, float gap, int frames, int fallenLo, int fallenHi, float travelMin, float advanceMin) {
    crowdReset();
    Grid& G = C.g; G.cell = 2; G.nx = G.nz = 200; G.org = vec2(-200, -200); const size_t N = (size_t)G.nx * G.nz;
    G.h.assign(N, 5.0f); G.walk.assign(N, 1); if (wall) for (int z = 0; z < 140; ++z) G.walk[(size_t)z * G.nx + 100] = 0;
    C.gridOk = true;
    const std::string nm = std::string("sim ") + name + ": ";
    if (!ensureGpu(nullptr)) { chk(false, nm + "GPU buffers / programs"); crowdReset(); return; }
    const int per = 2000; std::vector<vec3> start;
    for (int t = 0; t < 2; ++t) for (int k = 0; k < per; ++k) {
      const float rank = (float)(k % 20) * 1.5f, x = t ? gap * 0.5f + rank : -gap * 0.5f - rank, z = -60.0f + (k / 20) * 1.5f;
      CrowdUnit u; u.pos = vec4(x, 5.0f, z, t ? 1.5708f : -1.5708f); u.prev = u.pos; u.anim = vec4(0, 0, (float)t, 1);
      CrowdSim s; s.a = vec4(0, 0, 100, 1.4f); s.b = glm::ivec4(t, 0, 0, -1); s.c = vec4((float)(k % 97) * 0.02f, 0, glm::radians(1.1f), 150.0f);
      if (C.stageU.empty()) C.stageFrom = 0; C.stageU.push_back(u); C.stageS.push_back(s); C.spawned[t]++; start.push_back(vec3(u.pos));
    }
    buildCost(0, vec3(170, 0, 0)); buildCost(1, vec3(-170, 0, 0));
    uploadStaged(); const float dt = 1.0f / 30.0f;
    for (int f = 0; f < frames; ++f) { bindAll(); ++C.frame; simPasses(dt); if (f % 8 == 7) readDeaths(); }
    readDeaths(); glFinish();
    std::vector<CrowdUnit> U(C.n); std::vector<CrowdSim> S(C.n);
    glGetNamedBufferSubData(C.units, 0, U.size() * sizeof(CrowdUnit), U.data()); glGetNamedBufferSubData(C.sim, 0, S.size() * sizeof(CrowdSim), S.data());
    int nan = 0, off = 0, onWall = 0, tooFar = 0, st[2][4] = {}, nAlive[2] = {0, 0}; double dx[2] = {0, 0}, trav[2] = {0, 0};
    const float secs = frames * dt;
    for (int i = 0; i < C.n; ++i) {
      const vec4 p = U[i].pos; const int t = std::clamp(S[i].b.x, 0, 1), z = std::clamp(S[i].b.z, 0, 3); ++st[t][z];
      if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(p.w) || !std::isfinite(S[i].a.x) || !std::isfinite(S[i].a.z)) { ++nan; continue; }
      if (std::fabs(p.y - 5.0f) > 0.01f) ++off;
      const int cx = std::clamp((int)((p.x - G.org.x) / G.cell), 0, G.nx - 1), cz = std::clamp((int)((p.z - G.org.y) / G.cell), 0, G.nz - 1);
      if (!G.walk[(size_t)cz * G.nx + cx]) ++onWall;
      const float moved = glm::length(vec2(p.x - start[i].x, p.z - start[i].z));
      if (moved > 1.4f * 1.3f * secs + 3.0f) ++tooFar;
      if (z == 0) { dx[t] += p.x - start[i].x; trav[t] += moved; ++nAlive[t]; }
    }
    chk(nan == 0, nm + "no NaN (" + std::to_string(nan) + ")");
    chk(off == 0, nm + "every unit on the ground (" + std::to_string(off) + " off)");
    chk(onWall == 0, nm + "none on an unwalkable cell (" + std::to_string(onWall) + ")");
    chk(tooFar == 0, nm + "none faster than its speed cap (" + std::to_string(tooFar) + ")");
    for (int t = 0; t < 2; ++t) {
      const int fallen = st[t][1] + st[t][2] + st[t][3]; const std::string tn = "team " + std::to_string(t);
      chk(st[t][0] + fallen == per, nm + tn + " counted: " + std::to_string(st[t][0]) + " alive + " + std::to_string(fallen) + " fallen = " + std::to_string(per));
      chk(fallen >= fallenLo && fallen <= fallenHi, nm + tn + " lost " + std::to_string(fallen) + " (band " + std::to_string(fallenLo) + "-" + std::to_string(fallenHi) + ")");
      chk(C.dead[t] == fallen, nm + tn + " deaths read back " + std::to_string(C.dead[t]) + " = fallen " + std::to_string(fallen));
      const double m = nAlive[t] ? dx[t] / nAlive[t] : 0, tr = nAlive[t] ? trav[t] / nAlive[t] : 0; char b[160];
      std::snprintf(b, sizeof b, "%s advanced %.1f m toward the enemy (min %.0f), travelled %.1f m (min %.0f)", tn.c_str(), t ? -m : m, advanceMin, tr, travelMin);
      chk((t ? -m : m) >= advanceMin && tr >= travelMin, nm + b);
    }
    crowdReset();
  };
  // march: walking 1.4 m/s for ~30 s round the wall's end, closing to 70 m (a 130 m close in all): ~40 m each; the front ranks
  // come into range (150 m) after ~25 s, so a few fall at most
  run("march", true, 200.0f, 900, 0, 200, 30.0f, 25.0f);
  // fight: the armies 80 m apart (every rank within 150 m from the start), close to 70 m and stand: hundreds fall a side
  run("fight", false, 80.0f, 600, 150, 1900, 0.0f, -5.0f);
  return r;
}

int crowdCount() { return C.n; }

// ---------------------------------------------------------------- GC4 army mode
static Faction teamFaction(const Game& g, int t) {
  const ScenarioRules& r = g.match.rules;
  if (g.match.mode() == GameMode::Assault) return t == (int)CombatRole::Defender ? r.defendingFaction : otherFaction(r.defendingFaction);
  return r.teamFaction[std::clamp(t, 0, 1)];
}
static std::vector<vec3> teamSpawns(const Game& g, int t) {
  std::vector<vec3> v;
  if (g.match.mode() == GameMode::Assault) { for (auto* m : g.match.sc.spawnsFor(t == 1 ? CombatRole::Attacker : CombatRole::Defender, g.match.active)) v.push_back(m->pos); }
  else for (auto& m : g.match.sc.spawns) if (m.team == t) v.push_back(m.pos);
  return v;
}
static vec3 centroid(const std::vector<vec3>& v, vec3 def) { if (v.empty()) return def; vec3 c(0); for (auto& p : v) c += p; return c / (float)v.size(); }
static void parkSoldier(Game& g, int i) {
  Soldier& s = g.sold[i];
  if (s.agent >= 0) { Physics::destroyAgent(s.agent); s.agent = -1; }
  aiForget(g, i); s.alive = false; s.diedAt = -1e9; s.hp = 0;   // no corpse (diedAt far past), no death
  if (s.ciReady) { s.ci.destroy(); s.ciReady = false; }
  g.match.park(i);
}
static void retireUnit(uint32_t i, int team, int type) {   // GC4: a promoted unit leaves the crowd (state 3: not drawn, hashed or targeted)
  const glm::ivec4 b(team, type, 3, -1); glNamedBufferSubData(C.sim, (GLintptr)i * sizeof(CrowdSim) + 16, 16, &b);
}
void crowdArmyStep(Game& g, float dt) {
  if (const char* e = std::getenv("NMK_ARMY"); e && !C.armyInit && g.match.rules.armyUnits[0] + g.match.rules.armyUnits[1] == 0) {   // dev / bench: NMK_ARMY=<team 0>,<team 1>
    int a = 0, b = 0; std::sscanf(e, "%d,%d", &a, &b); g.match.rules.armyUnits[0] = std::clamp(a, 0, ScenarioRules::kMaxArmy); g.match.rules.armyUnits[1] = std::clamp(b, 0, ScenarioRules::kMaxArmy - g.match.rules.armyUnits[0]); }
  const ScenarioRules& r = g.match.rules;
  const bool army = !g.isClient && r.armyUnits[0] + r.armyUnits[1] > 0 && (g.match.mode() == GameMode::Assault || g.match.mode() == GameMode::TDM) && g.app.R.perf.crowd;
  if (!army) return;
  // the humans' bubbles: 150 m, 350 m for the local player aiming through a scope
  C.hum.clear();
  for (auto& s : g.sold) if (s.inUse && s.alive && (s.human || s.localPlayer)) {
    float R = 150.0f;
    if (s.localPlayer && g.app.adsT > 0.5f && s.weapon >= 0 && s.weapon < (int)g.app.weaponDefs.size() && g.app.weaponDefs[s.weapon]->hasScope) R = 350.0f;
    C.hum.push_back(vec4(s.feet, R));
  }
  if (C.hum.empty()) C.hum.push_back(vec4(g.app.eyePos(), 150.0f));   // spectating: the camera's
  // reserves, once: in blocks behind each side's spawns, facing its goal
  const Objective* ob = g.match.activeObjective();
  auto goalOf = [&](int t) { if (ob) return ob->pos; return centroid(teamSpawns(g, 1 - t), vec3(t ? -g.app.terrain.buildHalfX * 0.7f : g.app.terrain.buildHalfX * 0.7f, 0, 0)); };
  if (!C.armyInit) {
    C.armyInit = true;
    auto gun = [&](const char* id) { for (int k = 0; k < (int)g.app.weaponDefs.size(); ++k) if (g.app.weaponDefs[k]->id == id) return k; return -1; };
    for (int t = 0; t < 2; ++t) {
      const Faction f = teamFaction(g, t); const json& ml = g.combat.value("models", json::object()).value(factionName(f), json::array());
      std::vector<int> mods; for (auto& m : ml) { auto it = g.modelIdx.find(m.get<std::string>()); if (it != g.modelIdx.end()) mods.push_back(it->second); }
      if (mods.empty() || r.armyUnits[t] <= 0) continue;
      const int gn = gun(f == Faction::Allies ? "m1_garand" : "kar98k");
      const vec3 c = centroid(teamSpawns(g, t), vec3(t ? g.app.terrain.buildHalfX * 0.7f : -g.app.terrain.buildHalfX * 0.7f, 0, 0));
      vec3 fw = goalOf(t) - c; fw.y = 0; fw = glm::length(fw) > 1 ? glm::normalize(fw) : vec3(0, 0, 1); const vec3 side(fw.z, 0, -fw.x);
      const int W = std::max(20, (int)std::sqrt((double)r.armyUnits[t] * 2)); const float yaw = std::atan2(-fw.x, -fw.z);
      const float hx = g.app.terrain.buildHalfX - 3, hz = g.app.terrain.buildHalfZ - 3; int made = 0;
      for (int k = 0; k < r.armyUnits[t]; ++k) {
        vec3 p = c - fw * (8.0f + (k / W) * 1.6f) + side * ((k % W - W * 0.5f) * 1.6f);
        p.x = std::clamp(p.x, -hx, hx); p.z = std::clamp(p.z, -hz, hz);
        made += crowdSpawn(g, p, yaw, t, mods[k % mods.size()], gn, k % 9 == 0 ? 4.0f : 1.4f) >= 0;
      }
      logf("crowd: army %s (team %d): %d of %d reserves spawned", factionName(f), t, made, r.armyUnits[t]);
    }
  }
  if (g.match.active != C.goalFor) { C.goalFor = g.match.active; for (int t = 0; t < 2; ++t) crowdGoal(g, t, goalOf(t)); }   // the objective moved on
  // promotion: the near list (read on the GPU's 15th frames), nearest to a human first, onto a parked slot of the unit's side
  if (!C.nearRecs.empty()) {
    auto dh = [&](vec4 p) { float d = 1e30f; for (auto& h : C.hum) d = std::min(d, glm::length(vec2(p.x - h.x, p.z - h.z))); return d; };
    std::sort(C.nearRecs.begin(), C.nearRecs.end(), [&](const auto& a, const auto& b) { return dh(a.second) < dh(b.second); });
    int done = 0;
    for (auto& [a, p] : C.nearRecs) {
      if (done >= 32) break;
      int slot = -1; for (auto& rs : g.match.slots) if (rs.parked && !rs.human && rs.team == (int)a.y) { slot = rs.id; break; }
      if (slot < 0) continue;   // none free on its side: it holds at the bubble's edge
      Physics::Hit h; vec3 feet = vec3(p);
      if (Physics::castRayEx(feet + vec3(0, 2.0f, 0), vec3(0, -1, 0), 6.0f, h)) feet = h.pos;
      g.match.unpark(slot); g.placeSoldier(slot, feet, glm::degrees(p.w));
      g.sold[slot].hp = std::min(g.sold[slot].hp, (float)a.w);
      retireUnit(a.x, (int)a.y, (int)a.z); C.promoted[std::min<uint32_t>(a.y, 1)]++; ++done;
    }
    if (done) logf("crowd: %d promoted to soldiers", done);
    C.nearRecs.clear();
  }
  // demotion: an AI soldier beyond every bubble by 60 m for 3 s, not firing in the last 3 s
  if (g.t >= C.nextDemote || g.t + 2 < C.nextDemote) {
    C.nextDemote = g.t + 0.5; C.farFor.resize(g.sold.size(), 0.0f); int done = 0;
    for (int i = 0; i < (int)g.sold.size() && done < 64; ++i) {
      Soldier& s = g.sold[i];
      if (!s.inUse || !s.alive || s.human || s.localPlayer || s.zombie || s.team < 0 || s.team > 1 || i >= (int)g.match.slots.size() || g.match.slots[i].human) { C.farFor[i] = 0; continue; }
      bool far = true; for (auto& h : C.hum) { const float dx = s.feet.x - h.x, dz = s.feet.z - h.z, R = h.w + 60.0f; if (dx * dx + dz * dz < R * R) { far = false; break; } }
      C.farFor[i] = far ? C.farFor[i] + 0.5f : 0.0f;
      if (C.farFor[i] < 3.0f || g.t - s.lastShot < 3.0) continue;
      if (crowdSpawn(g, s.feet, glm::radians(s.yaw), s.team, s.model, s.weapon, 1.4f, s.hp) < 0) break;   // the crowd is full
      parkSoldier(g, i); C.farFor[i] = 0; ++done;
    }
    if (done) logf("crowd: %d soldiers went into the crowd", done);
  }
  for (int t = 0; t < 2; ++t) { g.match.armyAlive[t] = crowdAlive(t); g.match.armyKills[1 - t] = C.dead[t]; }   // the score
  (void)dt;
}
int crowdAlive(int team) { return team < 0 || team > 1 ? 0 : C.spawned[team] - C.dead[team] - C.promoted[team]; }
int crowdDeaths(int team) { return team < 0 || team > 1 ? 0 : C.dead[team]; }

int crowdSpawn(Game& g, vec3 feet, float yaw, int team, int model, int gun, float speed, float hp) {
  if (C.n + (int)C.stageU.size() >= kCap) return -1;
  if (!ensureGpu(&g)) return -1;
  const int ti = typeIndex(g, model, gun); if (ti < 0) return -1;
  if (C.stageU.empty()) C.stageFrom = C.n + (int)C.stageU.size();
  CrowdUnit u; u.pos = vec4(feet.x, g.app.terrain.height(feet.x, feet.z), feet.z, yaw); u.prev = u.pos; u.anim = vec4(0, (float)((C.n * 37) % 100) * 0.0137f, (float)team, 1);
  CrowdSim s; s.a = vec4(0, 0, std::max(1.0f, hp), speed); s.b = glm::ivec4(team, ti, 0, -1); s.c = vec4((float)((C.n + (int)C.stageU.size()) % 97) * 0.02f, 0, glm::radians(1.1f), 150.0f);   // rifleman sigma (archetypes.json), 150 m
  C.spawned[std::clamp(team, 0, 1)]++;
  C.stageU.push_back(u); C.stageS.push_back(s);
  return C.stageFrom + (int)C.stageU.size() - 1;
}

void crowdGoal(Game& g, int team, vec3 goal) { if (team < 0 || team > 1) return; if (!C.gridOk) C.gridOk = buildGrid(g); if (C.gridOk) buildCost(team, goal); }

void crowdReset() {
  for (auto& [k, t] : C.types) crowdDestroy(t);
  for (GLuint b : {C.units, C.sim, C.cnt, C.idx, C.height, C.costBuf, C.vis, C.visCnt, C.cmd, C.cmdBucket, C.pc, C.pid, C.dmg, C.deaths, C.nearBuf}) if (b) glDeleteBuffers(1, &b);
  for (GLuint p : {C.progHash, C.progMove, C.progCull, C.progCmd, C.progFight}) if (p) glDeleteProgram(p);
  C = GpuCrowd{};
}

// GC2-GC4 one frame's passes, shared by crowdFrame and the self-test
static void uploadStaged() {
  if (!C.stageU.empty()) {
    glNamedBufferSubData(C.units, (GLintptr)C.stageFrom * sizeof(CrowdUnit), C.stageU.size() * sizeof(CrowdUnit), C.stageU.data());
    glNamedBufferSubData(C.sim, (GLintptr)C.stageFrom * sizeof(CrowdSim), C.stageS.size() * sizeof(CrowdSim), C.stageS.data());
    C.n = C.stageFrom + (int)C.stageU.size(); C.stageU.clear(); C.stageS.clear();
  }
  const size_t cells = (size_t)C.g.nx * C.g.nz;
  for (int t = 0; t < 2; ++t) if (C.costDirty[t] && C.cost[t].size() == cells) { glNamedBufferSubData(C.costBuf, (GLintptr)(t * cells * 4), cells * 4, C.cost[t].data()); C.costDirty[t] = false; }
}
static void bindAll() {
  const GLuint bufs[15] = {C.units, C.sim, C.cnt, C.idx, C.height, C.costBuf, C.vis, C.visCnt, C.cmd, C.cmdBucket, C.pc, C.pid, C.dmg, C.deaths, C.nearBuf};
  for (int k = 0; k < 15; ++k) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, k, bufs[k]);
}
static void setCommon(GLuint p, float dt) {
  glUseProgram(p); glUniform1i(glGetUniformLocation(p, "uN"), C.n); glUniform1i(glGetUniformLocation(p, "uNx"), C.g.nx); glUniform1i(glGetUniformLocation(p, "uNz"), C.g.nz);
  glUniform2fv(glGetUniformLocation(p, "uOrg"), 1, glm::value_ptr(C.g.org)); glUniform1f(glGetUniformLocation(p, "uCell"), C.g.cell); glUniform1f(glGetUniformLocation(p, "uDt"), dt);
  glUniform1i(glGetUniformLocation(p, "uCx"), C.cx); glUniform1i(glGetUniformLocation(p, "uCz"), C.cz); glUniform1f(glGetUniformLocation(p, "uCCell"), C.ccell); glUniform1i(glGetUniformLocation(p, "uFrame"), C.frame);
}
static void simPasses(float dt) {   // hash, fight, move (+ the near list on every 15th frame)
  const GLuint groups = (GLuint)((C.n + 127) / 128); const uint32_t zero = 0;
  glClearNamedBufferData(C.cnt, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero); glClearNamedBufferData(C.pc, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
  setCommon(C.progHash, dt); glDispatchCompute(groups, 1, 1); glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  setCommon(C.progFight, dt); glDispatchCompute(groups, 1, 1); glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);   // GC3
  setCommon(C.progMove, dt);
  { const int nh = std::min<int>(16, (int)C.hum.size()); glUniform1i(glGetUniformLocation(C.progMove, "uNH"), nh); if (nh) glUniform4fv(glGetUniformLocation(C.progMove, "uHum"), nh, glm::value_ptr(C.hum[0]));
    C.nearOn = nh > 0 && C.frame % 15 == 0; glUniform1i(glGetUniformLocation(C.progMove, "uNearOn"), C.nearOn ? 1 : 0); }   // GC4: the near list on every 15th frame
  glDispatchCompute(groups, 1, 1); glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  if (C.nearOn) {   // GC4: read the frame's near list now (<= 1024 x 32 B) and restart it
    uint32_t nn = 0; glGetNamedBufferSubData(C.nearBuf, 0, 4, &nn); nn = std::min<uint32_t>(nn, 1024); C.nearRecs.clear();
    if (nn) { std::vector<uint32_t> raw((size_t)nn * 8); glGetNamedBufferSubData(C.nearBuf, 16, raw.size() * 4, raw.data());
              for (uint32_t k = 0; k < nn; ++k) { glm::uvec4 a(raw[k * 8], raw[k * 8 + 1], raw[k * 8 + 2], raw[k * 8 + 3]); vec4 p; std::memcpy(&p, &raw[k * 8 + 4], 16); C.nearRecs.push_back({a, p}); }
              const uint32_t z = 0; glNamedBufferSubData(C.nearBuf, 0, 4, &z); }
  }
}
static void readDeaths() {   // GC3: the deaths so far, then the list restarts
  uint32_t nd = 0; glGetNamedBufferSubData(C.deaths, 0, 4, &nd); nd = std::min<uint32_t>(nd, 65536);
  if (nd) { std::vector<uint32_t> dl((size_t)nd * 2); glGetNamedBufferSubData(C.deaths, 4, dl.size() * 4, dl.data()); for (uint32_t k = 0; k < nd; ++k) C.dead[std::min<uint32_t>(dl[k * 2 + 1], 1)]++;
            const uint32_t z = 0; glNamedBufferSubData(C.deaths, 0, 4, &z); }
}

void crowdFrame(Game& g) {
  Renderer& R = g.app.R; R.crowdDraws.clear();
  if (!R.perf.crowd) return;
  const float dt = C.lastT < 0 ? 0.0f : (float)std::clamp(g.t - C.lastT, 0.0, 0.1); C.lastT = g.t;
  testSource(g);
  if (!C.units || (C.n == 0 && C.stageU.empty()) || !C.progMove) return;
  uploadStaged(); bindAll(); ++C.frame;
  const GLuint groups = (GLuint)((C.n + 127) / 128); const uint32_t zero = 0;
  if (dt > 0) simPasses(dt);
  glClearNamedBufferData(C.visCnt, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
  const Frustum fr = Frustum::fromVP(R.lastViewProj()); const vec3 eye = g.app.eyePos();
  const float K = (float)std::max(1, R.displayH) * 1.8f / (2.0f * std::tan(glm::radians(settings().fov()) * 0.5f));
  setCommon(C.progCull, dt);
  vec4 pl[6]; for (int k = 0; k < 6; ++k) pl[k] = k < fr.n ? fr.pl[k] : vec4(0, 0, 0, 1);
  glUniform4fv(glGetUniformLocation(C.progCull, "uPl"), 6, glm::value_ptr(pl[0])); glUniform3fv(glGetUniformLocation(C.progCull, "uEye"), 1, glm::value_ptr(eye));
  glUniform1f(glGetUniformLocation(C.progCull, "uK"), K); glUniform1f(glGetUniformLocation(C.progCull, "uCapB"), (float)kCapB);
  glDispatchCompute(groups, 1, 1); glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  glUseProgram(C.progCmd); glUniform1i(glGetUniformLocation(C.progCmd, "uNCmd"), C.nCmd); glUniform1ui(glGetUniformLocation(C.progCmd, "uCapB"), (GLuint)kCapB);
  glDispatchCompute((GLuint)((C.nCmd + 63) / 64), 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
  glUseProgram(0);
  if (g.t >= C.nextDeaths || g.t + 1 < C.nextDeaths) { C.nextDeaths = g.t + 0.25; readDeaths(); }   // GC3: 4x a second
  { static double nextLog = 0; if (g.t >= nextLog || g.t + 10 < nextLog) { nextLog = g.t + 5;   // a status line every 5 s (a small read back)
      std::vector<uint32_t> vc(kMaxTypes * 3); glGetNamedBufferSubData(C.visCnt, 0, vc.size() * 4, vc.data()); std::string b;
      for (int k = 0; k < (int)C.typeKey.size() * 3; ++k) b += (k ? " " : "") + std::to_string(vc[k]);
      logf("[crowd] t %.0f s: %d units (alive %d / %d), %d types, visible per type x LOD: %s", g.t, C.n, crowdAlive(0), crowdAlive(1), (int)C.typeKey.size(), b.c_str()); } }
  // the draws: per type x LOD, the body and its gun, their instance counts the cull pass's
  for (int ti = 0; ti < (int)C.typeKey.size(); ++ti) for (int L = 0; L < 3; ++L) {
    const CrowdType& t = C.types[C.typeKey[ti]]; const CrowdType::Lod& l = t.lod[L]; const int b = ti * 3 + L;
    CrowdDraw d; d.look = &t.m->gpu; d.parts = &l.parts; d.vao = l.vao; d.rest = l.rest; d.pal = t.pal; d.J = t.J; d.base = b * kCapB; d.fps = t.fps;
    d.units = C.units; d.vis = C.vis; d.cmd = C.cmd; d.cmdFirst = C.cmdFirstOf[(size_t)b * 2];
    if (L == 2) { d.look = &t.impLook; d.rest = 0; d.imp = true; d.impCols = t.impCols; d.impRows = t.impRows; std::copy(t.impTab, t.impTab + 64, d.impTab); R.crowdDraws.push_back(d); continue; }   // GC5: the gun is in the atlas
    for (int c = 0; c < 8; ++c) d.clips[c] = t.clip[c];
    R.crowdDraws.push_back(d);
    if (t.gun && t.gun->vao) { CrowdDraw gd = d; gd.look = t.gun; gd.parts = &t.gun->parts; gd.vao = t.gun->vao; gd.rest = 0; gd.gun = true; gd.cmdFirst = C.cmdFirstOf[(size_t)b * 2 + 1]; R.crowdDraws.push_back(gd); }
  }
}
