// V1 --check-vsskin: the vertex-shader skin (vsskin, render.cpp NMK_SKIN_GLSL in kModelVS) equals the compute skin (kSkinCS) it replaces.
// For each model (NMK_VSSKIN_MODELS, default allied_body,skin_german): one compute instance and one vsskin instance get the same
// two palettes on consecutive frames (identity, then seeded random turns and moves per joint); kModelVS's outputs, captured by
// transform feedback with identity matrices, must match the compute buffer: the position and the previous position within
// 1e-4 m, the normal within 1e-3, the UV exactly. V0: the vsskin instance holds 2 J mat4 (gCharBytes), the compute one 64 B a vertex.
#include "selftest.h"
#include "game.h"
#include <random>
#include "crowd.h"

GLuint vsSkinCaptureProgram();   // render.cpp

namespace {
int fails = 0;
void chk(bool ok, const std::string& w) { logf("[vsskin] %s %s", ok ? "PASS" : "FAIL", w.c_str()); if (!ok) ++fails; }
std::vector<mat4> palette(int J, unsigned seed) {
  std::vector<mat4> S(J, mat4(1)); if (!seed) return S;
  std::mt19937 rng(seed); std::uniform_real_distribution<float> u(-1.0f, 1.0f);
  for (auto& m : S) m = glm::translate(mat4(1), vec3(u(rng), u(rng), u(rng)) * 0.05f) * glm::rotate(mat4(1), u(rng) * 0.6f, glm::normalize(vec3(u(rng), u(rng), u(rng)) + vec3(0, 0, 1e-3f)));
  return S;
}
void testModel(const std::string& id, GLuint prog) {
  const CharGpuBytes b0 = gCharBytes;   // before the load: destroying the model at the end takes its type and texture bytes away again
  CharModel m; std::string err;
  if (!m.load(contentDir() / "characters" / (id + ".glb"), err) && !m.load(contentDir() / "characters" / (id + ".glb"), err, 0, false)) { chk(false, id + ": load " + err); return; }
  const int V = (int)m.verts.size(), J = m.joints();
  chk(gCharBytes.nType - b0.nType == 1 && gCharBytes.type - b0.type == (long long)m.gpuBytesType && gCharBytes.tex - b0.tex == (long long)m.gpuBytesTex,
      id + ": the type counted once (" + std::to_string(m.gpuBytesType >> 10) + " KB geometry, " + std::to_string(m.gpuBytesTex >> 10) + " KB textures)");
  const CharGpuBytes b1 = gCharBytes;
  CharInstance A, B; A.create(m, false); B.create(m, true);
  chk(B.vs && B.view.skinJ == J && B.view.vbo == 0, id + ": the vsskin instance has no vertex buffer");
  const long long want = (long long)A.gpuBytes + (long long)B.gpuBytes;
  chk(gCharBytes.inst - b1.inst == want && gCharBytes.nInst - b1.nInst == 2 && B.gpuBytes == 2 * (size_t)J * sizeof(mat4) && A.gpuBytes == (size_t)V * 64 + (size_t)J * 64,
      id + ": bytes compute " + std::to_string(A.gpuBytes) + ", vsskin " + std::to_string(B.gpuBytes) + " (" + std::to_string(V) + " verts, " + std::to_string(J) + " joints)");
  GLuint tf; glCreateBuffers(1, &tf); glNamedBufferStorage(tf, (size_t)V * 12 * 4, nullptr, 0);
  glUseProgram(prog);
  const mat4 I(1);
  for (const char* n : {"uModel", "uVP", "uPrevModel", "uVPU", "uPrevVP"}) glUniformMatrix4fv(glGetUniformLocation(prog, n), 1, GL_FALSE, glm::value_ptr(I));
  glUniform1f(glGetUniformLocation(prog, "uThin"), 0.0f); glUniform1i(glGetUniformLocation(prog, "uVelMode"), 2);
  const GLint lsk = glGetUniformLocation(prog, "uSkinJ");
  unsigned seeds[3] = {0u, 7u, 1234u};
  for (int f = 0; f < 3; ++f) {
    std::vector<mat4> S = palette(J, seeds[f]);
    A.skin(S, f); B.skin(S, f);
    glUseProgram(prog); glBindVertexArray(B.view.vao); glUniform1i(lsk, B.view.skinJ);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 15, B.view.skinRest); glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 16, B.view.skinPal);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, tf);
    glEnable(GL_RASTERIZER_DISCARD); glBeginTransformFeedback(GL_POINTS); glDrawArrays(GL_POINTS, 0, V); glEndTransformFeedback(); glDisable(GL_RASTERIZER_DISCARD);
    glMemoryBarrier(GL_ALL_BARRIER_BITS); glFinish();
    std::vector<float> got((size_t)V * 12), ref((size_t)V * 12), prev((size_t)V * 4);
    glGetNamedBufferSubData(tf, 0, got.size() * 4, got.data());
    glGetNamedBufferSubData(A.view.vbo, 0, ref.size() * 4, ref.data());
    glGetNamedBufferSubData(A.prevBuf, 0, prev.size() * 4, prev.data());
    double ep = 0, en = 0, eu = 0, epp = 0;
    for (int i = 0; i < V; ++i) {
      const float* g = &got[(size_t)i * 12]; const float* r = &ref[(size_t)i * 12]; const float* q = &prev[(size_t)i * 4];   // got: vW 3, vN 3, vUV 2, vPC 4; ref: TexVertex pos 3, nrm 3, tan 4, uv 2
      for (int k = 0; k < 3; ++k) { ep = std::max(ep, (double)std::abs(g[k] - r[k])); en = std::max(en, (double)std::abs(g[3 + k] - r[3 + k])); epp = std::max(epp, (double)std::abs(g[8 + k] / g[11] - q[k])); }
      for (int k = 0; k < 2; ++k) eu = std::max(eu, (double)std::abs(g[6 + k] - r[10 + k]));
    }
    char w[256]; std::snprintf(w, sizeof w, "%s frame %d: position %.2g m, previous %.2g m (prev valid %d / %d), normal %.2g, uv %.2g", id.c_str(), f, ep, epp, (int)A.prevValid, (int)B.prevValid, en, eu);
    chk(ep < 1e-4 && epp < 1e-4 && en < 1e-3 && eu == 0 && A.prevValid == B.prevValid, w);
  }
  glUniform1i(lsk, 0); glUseProgram(0); glDeleteBuffers(1, &tf);
  A.destroy(); B.destroy();
  chk(gCharBytes.inst == b1.inst && gCharBytes.nInst == b1.nInst, id + ": destroying the two instances returns their bytes");
  m.destroy();
  chk(gCharBytes.inst == b0.inst && gCharBytes.nInst == b0.nInst && gCharBytes.type == b0.type && gCharBytes.tex == b0.tex, id + ": the counters return to where they were");
}

// ---- GC1 --check-crowd: a crowd bake (allied_body with the M1 Garand, or NMK_CROWD_MODEL / NMK_CROWD_GUN) has every clip's
// frames and two LODs under their budgets, and kModelVS in crowd mode (one unit, a walk frame, a yaw, a scale) puts every LOD
// vertex and every gun vertex where the CPU puts them from the same palettes (1e-4 m)
static mat4 crowdM(vec4 p, float s) { const float c = std::cos(p.w + 3.14159265f), n = std::sin(p.w + 3.14159265f); return mat4(vec4(c * s, 0, -n * s, 0), vec4(0, s, 0, 0), vec4(n * s, 0, c * s, 0), vec4(vec3(p), 1)); }
static void testCrowd(App& a, GLuint prog) {
  const char* mid = std::getenv("NMK_CROWD_MODEL") ? std::getenv("NMK_CROWD_MODEL") : "allied_body";
  const char* gid = std::getenv("NMK_CROWD_GUN") ? std::getenv("NMK_CROWD_GUN") : "m1_garand";
  { std::string e; chk(crowdShadersBuild(e), "crowd: compute programs build" + (e.empty() ? std::string() : " (" + e + " failed)")); }
  CharModel m; std::string err;
  if (!m.load(contentDir() / "characters" / (std::string(mid) + ".glb"), err)) { chk(false, std::string("crowd: load ") + mid + ": " + err); return; }
  const WeaponDef* w = nullptr; for (auto& d : a.weaponDefs) if (d->id == gid) w = d.get();
  chk(w && w->worldModel() && w->worldModel()->vao, std::string("crowd: gun ") + gid + " loaded");
  CrowdType t;
  if (!crowdBake(t, m, w, err)) { chk(false, "crowd: bake " + err); m.destroy(); return; }
  int frames = 0; for (int c = 0; c < CC_COUNT; ++c) frames += t.clip[c].y;
  chk(frames == t.frames && t.clip[CC_DEAD].x + t.clip[CC_DEAD].y == t.frames, "crowd: " + std::to_string(t.frames) + " frames in " + std::to_string((int)CC_COUNT) + " clips");
  chk(t.lod[0].tris > 0 && t.lod[0].tris <= 4000 && t.lod[1].tris > 0 && t.lod[1].tris <= 600 && t.lod[1].tris < t.lod[0].tris,
      "crowd: LOD0 " + std::to_string(t.lod[0].tris) + " tris, LOD1 " + std::to_string(t.lod[1].tris) + " (from " + std::to_string((int)m.indices.size() / 3) + ")");
  const int S = t.J + 1; std::vector<mat4> pal((size_t)t.frames * S); glGetNamedBufferSubData(t.pal, 0, pal.size() * sizeof(mat4), pal.data());
  CrowdUnit u; u.pos = vec4(12.5f, 3.25f, -40.0f, 0.7f); u.prev = u.pos - vec4(0.02f, 0, 0.01f, 0); u.anim = vec4((float)CC_WALK, 5.0f / t.fps + 0.01f, 0, 1.1f);
  GLuint ub; glCreateBuffers(1, &ub); glNamedBufferStorage(ub, sizeof u, &u, 0);
  const int o = (t.clip[CC_WALK].x + 5) * S; const mat4 Mw = crowdM(u.pos, u.anim.w);
  glUseProgram(prog);
  const mat4 I(1); for (const char* n : {"uModel", "uVP", "uPrevModel", "uVPU", "uPrevVP"}) glUniformMatrix4fv(glGetUniformLocation(prog, n), 1, GL_FALSE, glm::value_ptr(I));
  glUniform1f(glGetUniformLocation(prog, "uThin"), 0.0f); glUniform1i(glGetUniformLocation(prog, "uVelMode"), 1); glUniform1i(glGetUniformLocation(prog, "uSkinJ"), 0);
  glUniform1i(glGetUniformLocation(prog, "uCrowd"), S); glUniform1i(glGetUniformLocation(prog, "uCrowdBase"), 0); glUniform1f(glGetUniformLocation(prog, "uCrowdFps"), t.fps);
  glUniform4iv(glGetUniformLocation(prog, "uCrowdClip"), 8, glm::value_ptr(t.clip[0]));
  GLuint vb0; { const uint32_t z = 0; glCreateBuffers(1, &vb0); glNamedBufferStorage(vb0, 4, &z, 0); }   // GC2: the visible list (unit 0)
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 16, t.pal); glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 17, ub); glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 18, vb0);
  auto capture = [&](GLuint vao, int n, std::vector<float>& got) {
    GLuint tf; glCreateBuffers(1, &tf); glNamedBufferStorage(tf, (size_t)n * 12 * 4, nullptr, 0);
    glBindVertexArray(vao); glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, tf);
    glEnable(GL_RASTERIZER_DISCARD); glBeginTransformFeedback(GL_POINTS); glDrawArraysInstanced(GL_POINTS, 0, n, 1); glEndTransformFeedback(); glDisable(GL_RASTERIZER_DISCARD);
    glFinish(); got.resize((size_t)n * 12); glGetNamedBufferSubData(tf, 0, got.size() * 4, got.data()); glDeleteBuffers(1, &tf);
  };
  for (int L = 0; L < 2; ++L) {   // the body: each LOD vertex skinned with frame 5 of the walk, then placed
    const CrowdType::Lod& l = t.lod[L]; std::vector<CharModel::SkinVert> rv(l.verts); glGetNamedBufferSubData(l.rest, 0, rv.size() * sizeof(rv[0]), rv.data());
    glUniform1i(glGetUniformLocation(prog, "uCrowdGun"), 0); glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 15, l.rest);
    std::vector<float> got; capture(l.vao, l.verts, got); double e = 0;
    for (int i = 0; i < l.verts; ++i) {
      const auto& v = rv[i]; mat4 M(0.0f); for (int k = 0; k < 4; ++k) M += pal[o + v.j[k]] * v.w[k];
      const vec3 want = vec3(Mw * M * vec4(v.pos, 1)); for (int c = 0; c < 3; ++c) e = std::max(e, (double)std::abs(got[(size_t)i * 12 + c] - want[c]));
    }
    char b[160]; std::snprintf(b, sizeof b, "crowd: LOD%d body (%d verts) placed within %.2g m", L, l.verts, e); chk(e < 1e-4, b);
  }
  if (w && w->worldModel() && w->worldModel()->vao) {   // the gun: its own vertices at the world matrix x the frame's weapon matrix
    const GpuModel& gm = *w->worldModel(); GLint vb = 0; glGetVertexArrayIndexediv(gm.vao, 0, GL_VERTEX_BINDING_BUFFER, &vb); GLint64 sz = 0; glGetNamedBufferParameteri64v((GLuint)vb, GL_BUFFER_SIZE, &sz);
    GLint stride = 0; glGetVertexArrayIndexediv(gm.vao, 0, GL_VERTEX_BINDING_STRIDE, &stride);
    const int n = stride > 0 ? (int)(sz / stride) : 0; std::vector<uint8_t> raw((size_t)sz); glGetNamedBufferSubData((GLuint)vb, 0, sz, raw.data());
    glUniform1i(glGetUniformLocation(prog, "uCrowdGun"), 1);
    std::vector<float> got; capture(gm.vao, n, got); double e = 0; const mat4 G = Mw * pal[o + t.J];
    for (int i = 0; i < n; ++i) { vec3 p; std::memcpy(&p, &raw[(size_t)i * stride], 12); const vec3 want = vec3(G * vec4(p, 1)); for (int c = 0; c < 3; ++c) e = std::max(e, (double)std::abs(got[(size_t)i * 12 + c] - want[c])); }
    char b[160]; std::snprintf(b, sizeof b, "crowd: gun (%d verts) placed within %.2g m", n, e); chk(n > 0 && e < 1e-4, b);
  }
  glUniform1i(glGetUniformLocation(prog, "uCrowd"), 0); glUniform1i(glGetUniformLocation(prog, "uCrowdGun"), 0); glUseProgram(0); glBindVertexArray(0);
  glDeleteBuffers(1, &ub); glDeleteBuffers(1, &vb0); crowdDestroy(t); m.destroy();
}
}

bool vsSkinSelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) { if (std::string(__argv[k]) == "--check-vsskin") mode = 1; if (std::string(__argv[k]) == "--check-crowd") mode = 2; } }
  if (mode != 1 && mode != 2) return false;
  if (dt < 0) return true;
  std::string err; GLuint prog = 0;
  if (!initSkinning(err)) chk(false, "compute skinning: " + err);
  else if (!(prog = vsSkinCaptureProgram())) chk(false, "the model vertex shader did not build");
  else {
    if (mode == 2) { testCrowd(a, prog); glDeleteProgram(prog); logf("[vsskin] crowd done: %d failures", fails); a.selftest->exitCode = fails ? 1 : 0; a.selftest->done = true; return true; }
    std::string list = std::getenv("NMK_VSSKIN_MODELS") ? std::getenv("NMK_VSSKIN_MODELS") : "allied_body,skin_german";
    for (size_t p = 0; p <= list.size();) { size_t q = list.find(',', p); if (q == std::string::npos) q = list.size(); if (q > p) testModel(list.substr(p, q - p), prog); p = q + 1; }
    glDeleteProgram(prog);
  }
  logf("[vsskin] %s", vramLine().c_str());
  logf("[vsskin] done: %d failures", fails); a.selftest->exitCode = fails ? 1 : 0; a.selftest->done = true;
  return true;
}
