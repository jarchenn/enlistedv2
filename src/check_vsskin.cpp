// V1 --check-vsskin: the vertex-shader skin (vsskin, render.cpp NMK_SKIN_GLSL in kModelVS) equals the compute skin (kSkinCS) it replaces.
// For each model (NMK_VSSKIN_MODELS, default allied_body,skin_german): one compute instance and one vsskin instance get the same
// two palettes on consecutive frames (identity, then seeded random turns and moves per joint); kModelVS's outputs, captured by
// transform feedback with identity matrices, must match the compute buffer: the position and the previous position within
// 1e-4 m, the normal within 1e-3, the UV exactly. V0: the vsskin instance holds 2 J mat4 (gCharBytes), the compute one 64 B a vertex.
#include "selftest.h"
#include "game.h"
#include <random>

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
  CharModel m; std::string err;
  if (!m.load(contentDir() / "characters" / (id + ".glb"), err) && !m.load(contentDir() / "characters" / (id + ".glb"), err, 0, false)) { chk(false, id + ": load " + err); return; }
  const int V = (int)m.verts.size(), J = m.joints();
  const CharGpuBytes b0 = gCharBytes;
  CharInstance A, B; A.create(m, false); B.create(m, true);
  chk(B.vs && B.view.skinJ == J && B.view.vbo == 0, id + ": the vsskin instance has no vertex buffer");
  const long long want = (long long)A.gpuBytes + (long long)B.gpuBytes;
  chk(gCharBytes.inst - b0.inst == want && gCharBytes.nInst - b0.nInst == 2 && B.gpuBytes == 2 * (size_t)J * sizeof(mat4) && A.gpuBytes == (size_t)V * 64 + (size_t)J * 64,
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
  A.destroy(); B.destroy(); m.destroy();
  chk(gCharBytes.inst == b0.inst && gCharBytes.nInst == b0.nInst && gCharBytes.type == b0.type && gCharBytes.tex == b0.tex, id + ": the counters return to where they were");
}
}

bool vsSkinSelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--check-vsskin") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  std::string err; GLuint prog = 0;
  if (!initSkinning(err)) chk(false, "compute skinning: " + err);
  else if (!(prog = vsSkinCaptureProgram())) chk(false, "the model vertex shader did not build");
  else {
    std::string list = std::getenv("NMK_VSSKIN_MODELS") ? std::getenv("NMK_VSSKIN_MODELS") : "allied_body,skin_german";
    for (size_t p = 0; p <= list.size();) { size_t q = list.find(',', p); if (q == std::string::npos) q = list.size(); if (q > p) testModel(list.substr(p, q - p), prog); p = q + 1; }
    glDeleteProgram(prog);
  }
  logf("[vsskin] %s", vramLine().c_str());
  logf("[vsskin] done: %d failures", fails); a.selftest->exitCode = fails ? 1 : 0; a.selftest->done = true;
  return true;
}
