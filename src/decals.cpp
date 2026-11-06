// Decal renderer (see decals.h). GLSL lives here, like the rest of the repo's shaders (embedded strings).
#include "decals.h"
#include "game.h"
#include "app.h"
#include <chrono>
#include <algorithm>

namespace {
// ---------------------------------------------------------------- shared GLSL: noise, shapes, ageing
const char* kCommon = R"GLSL(
float h21(vec2 p){ p = fract(p*vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x*p.y); }
float vnoise(vec2 p){ vec2 i = floor(p), f = fract(p); f = f*f*(3.0 - 2.0*f);
  return mix(mix(h21(i), h21(i + vec2(1,0)), f.x), mix(h21(i + vec2(0,1)), h21(i + vec2(1,1)), f.x), f.y); }
float fbm(vec2 p){ return 0.5*vnoise(p) + 0.25*vnoise(p*2.07 + 3.1) + 0.125*vnoise(p*4.31 + 7.7); }
uniform float uAgeBrown, uFade;
vec3 ageTint(float age){ float k = clamp(age/max(1.0, uAgeBrown), 0.0, 1.0); k = k*k*(3.0 - 2.0*k);
  return mix(vec3(0.62, 0.035, 0.03), vec3(0.30, 0.13, 0.075), k); }   // = goreAgeTint (gore.cpp)
float gloss(float age){ return 1.0 - clamp(age/max(1.0, uAgeBrown), 0.0, 1.0); }
// teardrop / round stain: q relative to the impact point, r head radius, asp elongation, t tail direction
float stain(vec2 q, float r, float asp, vec2 t){
  vec2 x = vec2(dot(q, t), dot(q, vec2(-t.y, t.x)));
  if (asp < 1.15) return 1.0 - smoothstep(r*0.78, r, length(q));
  float L = r*asp, cx = L - r, u = (x.x - cx)/L;
  float taper = mix(1.0, 0.22, smoothstep(-0.3, 1.0, u));
  float e = u*u + (x.y*x.y)/(r*r*taper*taper);
  float body = 1.0 - smoothstep(0.7, 1.0, e);
  // a bead past the tip (long streaks throw one)
  float bead = asp > 2.2 ? 1.0 - smoothstep(0.35*r, 0.5*r, length(x - vec2(cx + L*1.18, 0.0))) : 0.0;
  return max(body, bead);
}
vec3 decalMul(float a, float age, float dark, vec3 N, vec3 P, vec3 cam, vec3 sun, float glossAmt){
  vec3 tint = ageTint(age)*dark;
  vec3 V = normalize(cam - P), H = normalize(V + sun);
  float sp = pow(max(dot(N, H), 0.0), 70.0)*gloss(age)*glossAmt;
  return mix(vec3(1.0), tint, a) + vec3(sp*a);
}
)GLSL";

const char* kWorldVS = R"GLSL(
#version 450 core
layout(location=0) in vec4 aPos;   // xyz world, w = decal index
uniform mat4 uVP;
flat out int vId;
void main(){ vId = int(aPos.w + 0.5); gl_Position = uVP*vec4(aPos.xyz, 1.0); }
)GLSL";
const char* kWorldFSHead = R"GLSL(
#version 450 core
struct D { vec4 a, n, t, b, g, sa[8], sb[8]; };
layout(std430, binding = 3) readonly buffer Decals { D dd[]; };
uniform sampler2D uDepth, uMask; uniform mat4 uInvVP; uniform vec2 uSize; uniform vec3 uCam, uSun;
flat in int vId;
layout(location=0) out vec4 o;
)GLSL";
const char* kWorldFS = R"GLSL(
void main(){
  float z = texelFetch(uDepth, ivec2(gl_FragCoord.xy), 0).r;
  vec4 w = uInvVP*vec4(gl_FragCoord.xy/uSize*2.0 - 1.0, z*2.0 - 1.0, 1.0);
  vec3 P = w.xyz/w.w;
  vec3 Ng = cross(dFdx(P), dFdy(P));
  if (z >= 1.0 || texelFetch(uMask, ivec2(gl_FragCoord.xy), 0).r > 0.5) discard;   // never onto soldiers
  vec3 c = dd[vId].a.xyz, N = dd[vId].n.xyz, T = dd[vId].t.xyz, B = cross(N, T);
  float ext = dd[vId].a.w, dep = dd[vId].n.w;
  int kind = int(dd[vId].t.w + 0.5);
  float age = dd[vId].b.x, fadeAge = dd[vId].b.y, seed = dd[vId].b.z; int ns = int(dd[vId].b.w + 0.5);
  vec3 rel = P - c;
  float dist = length(P - uCam);
  float lz = dot(rel, N), slab = dep + 8e-6*dist*dist;
  if (abs(lz) > slab) discard;
  vec2 uv = vec2(dot(rel, T), dot(rel, B));
  if (abs(uv.x) > ext || abs(uv.y) > ext) discard;
  float facing = length(Ng) > 1e-12 ? abs(dot(normalize(Ng), N)) : 1.0;
  float a = 0.0, dark = 1.0, glossAmt = 1.4;
  if (kind == 0) {            // landed droplets of one burst
    for (int i = 0; i < ns; ++i) { vec4 s = dd[vId].sa[i]; vec4 t = dd[vId].sb[i];
      a = max(a, stain(uv - s.xy, s.z, s.w, t.xy)); }
    dark = 0.8;
  } else if (kind == 1) {     // splatter on the surface behind the target
    float r1 = dd[vId].g.y, asp = dd[vId].sa[0].w;
    vec2 q = vec2(uv.x/(0.55 + 0.45*asp), uv.y);
    float rr = length(q), th = atan(q.y, q.x);
    float edge = r1*(0.42 + 0.22*fbm(vec2(th*2.0 + seed, seed*0.37)));
    float spike = pow(vnoise(vec2(th*9.0 + seed*1.7, 0.5)), 5.0)*r1*1.1;
    a = 1.0 - smoothstep(edge*0.85, edge + spike, rr);
    vec2 cell = floor(q/(r1*0.16)); float hh = h21(cell + seed);
    vec2 cc = (cell + 0.25 + 0.5*vec2(h21(cell + seed + 1.3), h21(cell + seed + 2.9)))*(r1*0.16);
    if (hh > 0.72 && length(cc) < r1*1.5) a = max(a, 1.0 - smoothstep(r1*0.018, r1*0.03 + r1*0.02*hh, length(q - cc)));
    a *= 0.95; dark = 0.75;
  } else {                    // pool under a corpse, spreading
    float r0 = dd[vId].g.x, r1 = dd[vId].g.y, gt = max(0.1, dd[vId].g.z);
    float k = clamp(age/gt, 0.0, 1.0); k = 1.0 - (1.0 - k)*(1.0 - k);
    float R = mix(r0, r1, k);
    float rr = length(uv), th = atan(uv.y, uv.x);
    float edge = R*(0.86 + 0.24*fbm(vec2(th*1.6 + seed, seed*0.21)) + 0.08*fbm(uv*9.0/max(r1, 0.1)));
    a = 1.0 - smoothstep(edge*0.93, edge, rr);
    dark = mix(0.55, 0.85, smoothstep(0.0, edge, rr));
    glossAmt = 2.2; age *= 0.6;    // thick blood stays wet longer
  }
  a *= smoothstep(0.15, 0.45, facing)*(1.0 - smoothstep(slab*0.6, slab, abs(lz)));
  if (fadeAge >= 0.0) a *= 1.0 - clamp(fadeAge/uFade, 0.0, 1.0);
  a *= 1.0 - smoothstep(120.0, 150.0, dist);
  if (a < 0.004) discard;
  o = vec4(decalMul(a, age, dark, N, P, uCam, uSun, glossAmt), 1.0);
}
)GLSL";

const char* kMaskVS = R"GLSL(
#version 450 core
layout(location=0) in vec3 aPos; uniform mat4 uVP, uModel;
void main(){ gl_Position = uVP*(uModel*vec4(aPos, 1.0)); }
)GLSL";
const char* kMaskFS = R"GLSL(
#version 450 core
layout(location=0) out vec4 o; void main(){ o = vec4(1.0); }
)GLSL";
const char* kCharVS = R"GLSL(
#version 450 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm;
struct V { vec4 pos; vec4 nrm; vec4 uv; uvec4 j; vec4 w; };
layout(std430, binding = 0) readonly buffer Rest { V rv[]; };
uniform mat4 uVP, uModel;
out vec3 vRest, vRestN, vN, vW;
void main(){
  V r = rv[gl_VertexID];
  vRest = r.pos.xyz; vRestN = r.nrm.xyz;
  vec4 w = uModel*vec4(aPos, 1.0); vW = w.xyz; vN = mat3(uModel)*aNrm;
  gl_Position = uVP*w;
}
)GLSL";
const char* kCharFSHead = R"GLSL(
#version 450 core
in vec3 vRest, vRestN, vN, vW;
uniform vec4 uD0[24], uD1[24], uD2[24], uD3[24]; uniform int uCount;
uniform vec3 uCam, uSun;
layout(location=0) out vec4 o;
)GLSL";
const char* kCharFS = R"GLSL(
void main(){
  vec3 rn = normalize(vRestN), N = normalize(vN);
  vec3 mul = vec3(1.0); bool any = false;
  for (int i = 0; i < uCount; ++i) {
    vec3 c = uD0[i].xyz; float R = uD0[i].w; vec3 dn = uD1[i].xyz; int kind = int(uD1[i].w + 0.5);
    float age = uD2[i].x, fadeAge = uD2[i].y, seed = uD2[i].z, growTo = uD2[i].w, growT = uD3[i].x;
    if (kind == 5) R = min(growTo, R + growTo*sqrt(clamp(age/max(growT, 0.1), 0.0, 1.0)));
    vec3 rel = vRest - c; float lz = dot(rel, dn);
    if (abs(lz) > R*1.3 + 0.012) continue;
    float face = dot(rn, dn); if (face < 0.05) continue;
    vec3 tx = normalize(cross(abs(dn.y) < 0.9 ? vec3(0,1,0) : vec3(1,0,0), dn)), ty = cross(dn, tx);
    vec2 uv = vec2(dot(rel, tx), dot(rel, ty));
    float rr = length(uv), th = atan(uv.y, uv.x);
    float a = 0.0, dark = 1.0, gl = 1.6;
    if (kind == 3) {          // entry wound: small dark hole, wet ring
      float e = R*(0.85 + 0.3*vnoise(vec2(th*3.0 + seed, seed)));
      a = 1.0 - smoothstep(e*0.75, e, rr);
      dark = mix(0.12, 0.8, smoothstep(R*0.25, R*0.6, rr));
    } else if (kind == 4) {   // exit wound: larger, ragged
      float e = R*(0.7 + 0.55*fbm(vec2(th*2.5 + seed, seed*0.3)));
      a = 1.0 - smoothstep(e*0.8, e, rr);
      dark = mix(0.1, 0.7, smoothstep(R*0.2, R*0.7, rr));
    } else {                  // soak: blood spreading through the cloth
      float e = R*(0.75 + 0.4*fbm(vec2(th*1.7 + seed, seed*0.11) + uv*14.0));
      a = (1.0 - smoothstep(e*0.6, e, rr))*0.8;
      dark = 0.7; gl = 0.5;
    }
    a *= smoothstep(0.05, 0.35, face);
    if (fadeAge >= 0.0) a *= 1.0 - clamp(fadeAge/uFade, 0.0, 1.0);
    if (a < 0.004) continue;
    mul *= decalMul(a, age, dark, N, vW, uCam, uSun, gl); any = true;
  }
  if (!any) discard;
  o = vec4(mul, 1.0);
}
)GLSL";

GLuint gMaskProg = 0, gMaskTex = 0, gMaskFbo = 0;
GLuint gWorldProg = 0, gCharProg = 0, gVao = 0, gVbo = 0, gSsbo = 0, gDepthCopy = 0;
int gDepthW = 0, gDepthH = 0;
bool gInitTried = false;
GLuint gQ[3][2]{}; int gQi = 0; bool gQv[3]{};
DecalGpuStats gStats;

GLuint compileProg(Renderer& r, const char* vs, std::initializer_list<const char*> fs, const char* name) {
  auto sh = [&](GLenum type, std::vector<const char*> src) -> GLuint {
    GLuint s = glCreateShader(type); glShaderSource(s, (GLsizei)src.size(), src.data(), nullptr); glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof log, nullptr, log); logf("decals: %s shader error: %s", name, log); ++r.shaderErrors; glDeleteShader(s); return 0; }
    return s;
  };
  std::vector<const char*> fsrc(fs);
  GLuint v = sh(GL_VERTEX_SHADER, {vs}), f = sh(GL_FRAGMENT_SHADER, fsrc);
  if (!v || !f) return 0;
  GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p); glDeleteShader(v); glDeleteShader(f);
  GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) { char log[4096]; glGetProgramInfoLog(p, sizeof log, nullptr, log); logf("decals: %s link error: %s", name, log); ++r.shaderErrors; glDeleteProgram(p); return 0; }
  return p;
}
bool init(Renderer& r) {
  if (gInitTried) return gWorldProg && gCharProg && gMaskProg;
  gInitTried = true;
  gWorldProg = compileProg(r, kWorldVS, {kWorldFSHead, kCommon, kWorldFS}, "world decal");
  gCharProg = compileProg(r, kCharVS, {kCharFSHead, kCommon, kCharFS}, "character decal");
  gMaskProg = compileProg(r, kMaskVS, {kMaskFS}, "decal soldier mask");
  glCreateVertexArrays(1, &gVao); glCreateBuffers(1, &gVbo); glCreateBuffers(1, &gSsbo);
  glVertexArrayVertexBuffer(gVao, 0, gVbo, 0, sizeof(vec4));
  glEnableVertexArrayAttrib(gVao, 0); glVertexArrayAttribFormat(gVao, 0, 4, GL_FLOAT, GL_FALSE, 0); glVertexArrayAttribBinding(gVao, 0, 0);
  for (auto& q : gQ) glGenQueries(2, q);
  return gWorldProg && gCharProg && gMaskProg;
}
GLint U(GLuint p, const char* n) { return glGetUniformLocation(p, n); }
struct GpuDecal { vec4 a, n, t, b, g, sa[8], sb[8]; };
}   // namespace

const DecalGpuStats& decalsLastStats() { return gStats; }

void decalsShutdown() {
  if (gWorldProg) glDeleteProgram(gWorldProg);
  if (gCharProg) glDeleteProgram(gCharProg);
  if (gVao) glDeleteVertexArrays(1, &gVao);
  GLuint b[2] = {gVbo, gSsbo}; glDeleteBuffers(2, b);
  if (gDepthCopy) glDeleteTextures(1, &gDepthCopy);
  if (gMaskTex) glDeleteTextures(1, &gMaskTex);
  if (gMaskFbo) glDeleteFramebuffers(1, &gMaskFbo);
  if (gMaskProg) glDeleteProgram(gMaskProg);
  gMaskProg = gMaskTex = gMaskFbo = 0;
  gWorldProg = gCharProg = gVao = gVbo = gSsbo = gDepthCopy = 0; gInitTried = false;
}

// the soldiers Game::drawItems draws this frame (same rules), with their skinned vertex buffers
static bool soldierDrawn(const Game& G, const Soldier& s, vec3 cam, double corpse) {
  if (!s.inUse || s.lifeId == 0 || !s.ciReady || s.ci.frameSkinned < 0 || !s.ci.model) return false;
  if (s.localPlayer && s.alive) return false;
  if (!s.alive && G.t - s.diedAt > corpse) return false;
  return glm::length(s.feet - cam) <= 300;
}

void decalsRender(Renderer& r, const RenderView& v, const mat4& vp, unsigned depthTex) {
  GoreState& S = goreState();
  double corpse = gGame && gGame->combat.is_object() && gGame->combat.contains("corpses") ? gGame->combat["corpses"].value("seconds", 40.0) : 40.0;
  auto c0 = std::chrono::steady_clock::now();
  // previous GPU timings (3-frame ring, never stalls)
  int qr = (gQi + 1) % 3;
  if (gQv[qr]) {
    GLint avail = 0; glGetQueryObjectiv(gQ[qr][1], GL_QUERY_RESULT_AVAILABLE, &avail);
    if (avail) { GLuint64 a = 0, b = 0; glGetQueryObjectui64v(gQ[qr][0], GL_QUERY_RESULT, &a); glGetQueryObjectui64v(gQ[qr][1], GL_QUERY_RESULT, &b); gStats.gpuMs = (b - a) / 1e6; S.st.gpuMs += gStats.gpuMs; ++S.st.gpuN; gQv[qr] = false; }
  }
  gStats.worldDrawn = gStats.charsDrawn = gStats.boneDrawn = 0;
  bool anyBone = false; for (auto& [k, rec] : S.sold) if (!rec.decals.empty()) { anyBone = true; break; }
  if (S.world.empty() && !anyBone) return;
  if (!init(r)) return;
  glQueryCounter(gQ[gQi][0], GL_TIMESTAMP);
  const float age0 = (float)S.t;
  float fadeT = S.cfg.decalFade;
  // ---- world decals: boxes over the depth buffer
  if (!S.world.empty()) {
    if (gDepthW != r.width || gDepthH != r.height || !gDepthCopy) {
      if (gDepthCopy) glDeleteTextures(1, &gDepthCopy);
      glCreateTextures(GL_TEXTURE_2D, 1, &gDepthCopy); glTextureStorage2D(gDepthCopy, 1, GL_DEPTH_COMPONENT32F, r.width, r.height);
      glTextureParameteri(gDepthCopy, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTextureParameteri(gDepthCopy, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      if (gMaskTex) { glDeleteTextures(1, &gMaskTex); glDeleteFramebuffers(1, &gMaskFbo); }
      glCreateTextures(GL_TEXTURE_2D, 1, &gMaskTex); glTextureStorage2D(gMaskTex, 1, GL_R8, r.width, r.height);
      glTextureParameteri(gMaskTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTextureParameteri(gMaskTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glCreateFramebuffers(1, &gMaskFbo);
      glNamedFramebufferTexture(gMaskFbo, GL_COLOR_ATTACHMENT0, gMaskTex, 0);
      glNamedFramebufferTexture(gMaskFbo, GL_DEPTH_ATTACHMENT, gDepthCopy, 0);
      if (glCheckNamedFramebufferStatus(gMaskFbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { logf("decals: mask framebuffer incomplete"); ++r.fboErrors; }
      gDepthW = r.width; gDepthH = r.height;
    }
    glCopyImageSubData(depthTex, GL_TEXTURE_2D, 0, 0, 0, 0, gDepthCopy, GL_TEXTURE_2D, 0, 0, 0, 0, r.width, r.height, 1);
    // soldiers near a decal: their visible pixels are masked out (a pool must not stain the corpse lying in it)
    {
      GLint prevFbo = 0; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
      float zero[4] = {0, 0, 0, 0}; glClearNamedFramebufferfv(gMaskFbo, GL_COLOR, 0, zero);
      glBindFramebuffer(GL_FRAMEBUFFER, gMaskFbo);
      glUseProgram(gMaskProg);
      glUniformMatrix4fv(U(gMaskProg, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
      glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
      glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.0f, -4.0f);
      if (gGame && gGame->active)
        for (auto& s : gGame->sold) {
          if (!soldierDrawn(*gGame, s, v.camPos, corpse)) continue;
          bool near = false;
          for (auto& d : S.world) if (glm::length(d.pos - s.feet) < d.extent + 2.2f) { near = true; break; }
          if (!near) continue;
          glUniformMatrix4fv(U(gMaskProg, "uModel"), 1, GL_FALSE, glm::value_ptr(s.world));
          glBindVertexArray(s.ci.view.vao);
          glDrawElements(GL_TRIANGLES, (GLsizei)s.ci.model->indices.size(), GL_UNSIGNED_INT, nullptr);
        }
      glDisable(GL_POLYGON_OFFSET_FILL);
      glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    }
    std::vector<GpuDecal> gd; gd.reserve(S.world.size());
    std::vector<vec4> verts; verts.reserve(S.world.size() * 36);
    for (auto& d : S.world) {
      if (glm::length(d.pos - v.camPos) > S.cfg.drawDistance + d.extent) continue;
      GpuDecal g{};
      g.a = vec4(d.pos, d.extent); g.n = vec4(d.n, d.depth); g.t = vec4(d.t, (float)d.kind);
      g.b = vec4(age0 - (float)d.born, d.fadeStart < 0 ? -1.0f : age0 - (float)d.fadeStart, (float)(d.seed % 997u) * 0.37f, (float)d.stamps.size());
      g.g = vec4(d.r0, d.r1, d.growT, 0);
      for (size_t k = 0; k < d.stamps.size() && k < 8; ++k) { auto& s = d.stamps[k]; g.sa[k] = vec4(s.off, s.radius, s.aspect); g.sb[k] = vec4(s.tail, 0, 0); }
      float idx = (float)gd.size();
      gd.push_back(g);
      vec3 b = glm::cross(d.n, d.t), ex = d.t * d.extent, ey = b * d.extent, ez = d.n * d.depth;
      vec3 cr[8]; for (int k = 0; k < 8; ++k) cr[k] = d.pos + ex * (k & 1 ? 1.0f : -1.0f) + ey * (k & 2 ? 1.0f : -1.0f) + ez * (k & 4 ? 1.0f : -1.0f);
      static const int F[6][4] = {{0, 2, 6, 4}, {1, 5, 7, 3}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 6, 7, 5}};
      for (auto& f : F) {
        int tri[2][3] = {{f[0], f[1], f[2]}, {f[0], f[2], f[3]}};
        for (auto& t : tri) {
          vec3 p0 = cr[t[0]], p1 = cr[t[1]], p2 = cr[t[2]];
          if (glm::dot(glm::cross(p1 - p0, p2 - p0), (p0 + p1 + p2) / 3.0f - d.pos) < 0) std::swap(p1, p2);   // outward CCW
          verts.push_back(vec4(p0, idx)); verts.push_back(vec4(p1, idx)); verts.push_back(vec4(p2, idx));
        }
      }
    }
    if (!gd.empty()) {
      glNamedBufferData(gSsbo, gd.size() * sizeof(GpuDecal), gd.data(), GL_STREAM_DRAW);
      glNamedBufferData(gVbo, verts.size() * sizeof(vec4), verts.data(), GL_STREAM_DRAW);
      glUseProgram(gWorldProg);
      glUniformMatrix4fv(U(gWorldProg, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
      glUniformMatrix4fv(U(gWorldProg, "uInvVP"), 1, GL_FALSE, glm::value_ptr(glm::inverse(vp)));
      glUniform2f(U(gWorldProg, "uSize"), (float)r.width, (float)r.height);
      glUniform3fv(U(gWorldProg, "uCam"), 1, glm::value_ptr(v.camPos)); glUniform3fv(U(gWorldProg, "uSun"), 1, glm::value_ptr(r.sunDir));
      glUniform1f(U(gWorldProg, "uAgeBrown"), S.cfg.ageToBrown); glUniform1f(U(gWorldProg, "uFade"), fadeT);
      glBindTextureUnit(14, gDepthCopy); glUniform1i(U(gWorldProg, "uDepth"), 14);
      glBindTextureUnit(15, gMaskTex); glUniform1i(U(gWorldProg, "uMask"), 15);
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, gSsbo);
      glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
      glEnable(GL_CULL_FACE); glCullFace(GL_FRONT);   // back faces: works with the camera inside a box
      glEnable(GL_BLEND); glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
      glBindVertexArray(gVao);
      glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
      glCullFace(GL_BACK);
      glBindTextureUnit(14, 0); glBindTextureUnit(15, 0);
      gStats.worldDrawn = (int)gd.size();
    }
  }
  // ---- bone decals: each decorated soldier's skinned mesh again, decals evaluated in rest space
  if (anyBone && gGame && gGame->active) {
    Game& G = *gGame;
    glUseProgram(gCharProg);
    glUniformMatrix4fv(U(gCharProg, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    glUniform3fv(U(gCharProg, "uCam"), 1, glm::value_ptr(v.camPos)); glUniform3fv(U(gCharProg, "uSun"), 1, glm::value_ptr(r.sunDir));
    glUniform1f(U(gCharProg, "uAgeBrown"), S.cfg.ageToBrown); glUniform1f(U(gCharProg, "uFade"), fadeT);
    GLint lM = U(gCharProg, "uModel"), lN = U(gCharProg, "uCount"), l0 = U(gCharProg, "uD0"), l1 = U(gCharProg, "uD1"), l2 = U(gCharProg, "uD2"), l3 = U(gCharProg, "uD3");
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.0f, -4.0f);
    glEnable(GL_BLEND); glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
    for (auto& [slot, rec] : S.sold) {
      if (rec.decals.empty() || slot < 0 || slot >= (int)G.sold.size()) continue;
      Soldier& s = G.sold[slot];
      if (s.lifeId != rec.lifeId || !soldierDrawn(G, s, v.camPos, corpse)) continue;
      vec4 d0[24], d1[24], d2[24], d3[24]; int n = 0;
      for (auto& d : rec.decals) {
        if (n >= 24) break;
        d0[n] = vec4(d.restPos, d.radius); d1[n] = vec4(d.restN, (float)d.kind);
        d2[n] = vec4(age0 - (float)d.born, d.fadeStart < 0 ? -1.0f : age0 - (float)d.fadeStart, (float)(d.seed % 997u) * 0.37f, d.growTo); d3[n] = vec4(d.growT, 0, 0, 0);
        ++n;
      }
      glUniformMatrix4fv(lM, 1, GL_FALSE, glm::value_ptr(s.world));
      glUniform1i(lN, n);
      glUniform4fv(l0, n, glm::value_ptr(d0[0])); glUniform4fv(l1, n, glm::value_ptr(d1[0])); glUniform4fv(l2, n, glm::value_ptr(d2[0])); glUniform4fv(l3, n, glm::value_ptr(d3[0]));
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s.ci.model->restSsbo);
      glBindVertexArray(s.ci.view.vao);
      glDrawElements(GL_TRIANGLES, (GLsizei)s.ci.model->indices.size(), GL_UNSIGNED_INT, nullptr);
      ++gStats.charsDrawn; gStats.boneDrawn += n;
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
  }
  glQueryCounter(gQ[gQi][1], GL_TIMESTAMP); gQv[gQi] = true; gQi = (gQi + 1) % 3;
  // restore the state the main pass continues with
  glDisable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
  glBindVertexArray(0); glUseProgram(0);
  S.st.cpuRenderMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count();
}
