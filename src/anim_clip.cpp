// glTF animation clips: parse, sample (lerp / slerp), blend, cross-fade, pose (see anim_clip.h).
#include "anim_clip.h"
#include "character.h"
#include <algorithm>
#include <cmath>

void clipsFromGltf(const json& J, const std::function<std::vector<float>(int)>& acc, std::vector<AnimClip>& out) {
  if (!J.contains("animations")) return;
  for (auto& an : J["animations"]) {
    AnimClip c; c.name = an.value("name", "clip" + std::to_string(out.size()));
    for (auto& chj : an["channels"]) {
      const json& tg = chj["target"];
      if (!tg.contains("node")) continue;
      std::string p = tg["path"].get<std::string>();
      int path = p == "translation" ? 0 : p == "rotation" ? 1 : p == "scale" ? 2 : -1;
      if (path < 0) continue;   // weights (morph targets): not used
      const json& sm = an["samplers"][chj["sampler"].get<int>()];
      AnimClip::Channel ch; ch.node = tg["node"].get<int>(); ch.path = path;
      std::string ip = sm.value("interpolation", "LINEAR");
      ch.step = ip == "STEP";
      ch.t = acc(sm["input"].get<int>()); ch.v = acc(sm["output"].get<int>());
      int nc = path == 1 ? 4 : 3;
      if (ip == "CUBICSPLINE") {   // [in, value, out] per key: keep the values
        std::vector<float> v; for (size_t k = 0; k < ch.t.size(); ++k) for (int q = 0; q < nc; ++q) v.push_back(ch.v[(k * 3 + 1) * nc + q]);
        ch.v = v;
      }
      if (ch.t.empty() || ch.v.size() < ch.t.size() * nc) continue;
      c.dur = std::max(c.dur, ch.t.back());
      c.ch.push_back(std::move(ch));
    }
    out.push_back(std::move(c));
  }
}

int findClip(const CharModel& m, const std::string& name) {
  for (size_t k = 0; k < m.clips.size(); ++k) if (m.clips[k].name == name) return (int)k;
  return -1;
}

void restLocals(const CharModel& m, NodeLocals& L) {
  size_t n = m.nodeLocal.size();
  L.T.resize(n); L.S.resize(n); L.R.resize(n); L.set.assign(n, 0);
  for (size_t k = 0; k < n; ++k) {
    const mat4& M = m.nodeLocal[k];
    vec3 s(glm::length(vec3(M[0])), glm::length(vec3(M[1])), glm::length(vec3(M[2])));
    L.T[k] = vec3(M[3]); L.S[k] = s;
    L.R[k] = glm::normalize(glm::quat_cast(glm::mat3(vec3(M[0]) / s.x, vec3(M[1]) / s.y, vec3(M[2]) / s.z)));
  }
}

void sampleClip(const CharModel& m, int clip, float t, bool loop, NodeLocals& L) {
  if (clip < 0 || clip >= (int)m.clips.size()) return;
  const AnimClip& c = m.clips[clip];
  if (c.dur > 0) t = loop ? t - c.dur * std::floor(t / c.dur) : std::clamp(t, 0.0f, c.dur);
  for (auto& ch : c.ch) {
    if (ch.node < 0 || ch.node >= (int)L.set.size()) continue;
    int nc = ch.path == 1 ? 4 : 3;
    size_t n = ch.t.size(), k1 = std::upper_bound(ch.t.begin(), ch.t.end(), t) - ch.t.begin();
    size_t k0 = k1 == 0 ? 0 : k1 - 1; if (k1 >= n) k1 = n - 1;
    float f = k1 == k0 || ch.step ? 0.0f : std::clamp((t - ch.t[k0]) / std::max(1e-6f, ch.t[k1] - ch.t[k0]), 0.0f, 1.0f);
    const float* a = &ch.v[k0 * nc]; const float* b = &ch.v[k1 * nc];
    if (ch.path == 1) L.R[ch.node] = glm::normalize(glm::slerp(quat(a[3], a[0], a[1], a[2]), quat(b[3], b[0], b[1], b[2]), f));
    else { vec3 v = glm::mix(vec3(a[0], a[1], a[2]), vec3(b[0], b[1], b[2]), f); (ch.path == 0 ? L.T : L.S)[ch.node] = v; }
    L.set[ch.node] = 1;
  }
}

void mixLocals(NodeLocals& a, const NodeLocals& b, float w) {
  for (size_t k = 0; k < a.set.size() && k < b.set.size(); ++k) {
    if (!a.set[k] && !b.set[k]) continue;
    a.T[k] = glm::mix(a.T[k], b.T[k], w); a.S[k] = glm::mix(a.S[k], b.S[k], w);
    a.R[k] = glm::normalize(glm::slerp(a.R[k], b.R[k], w)); a.set[k] = 1;
  }
}

void poseFromLocals(const CharModel& m, const NodeLocals& L, PoseResult& out) {
  size_t n = m.nodeLocal.size();
  std::vector<mat4> W(n, mat4(1));
  for (int k : m.nodeOrder) {
    mat4 l = k < (int)L.set.size() && L.set[k] ? glm::translate(mat4(1), L.T[k]) * glm::mat4_cast(L.R[k]) * glm::scale(mat4(1), L.S[k]) : m.nodeLocal[k];
    W[k] = m.nodeParent[k] < 0 ? m.rootFix * l : W[m.nodeParent[k]] * l;
  }
  int nj = m.joints();
  out.skin.resize(nj); out.delta.resize(nj);
  for (int j = 0; j < nj; ++j) {
    int nd = m.jointNode[j];
    out.skin[j] = W[nd] * m.invBind[j];
    out.delta[j] = W[nd] * glm::inverse(m.nodeWorld[nd]);
  }
  for (int s = 0; s < CJ_COUNT; ++s) out.joint[s] = m.semantic[s] >= 0 ? vec3(W[m.jointNode[m.semantic[s]]][3]) : vec3(0);
  out.weapon = mat4(1);
}

void analyzeClips(CharModel& m) {
  NodeLocals rest; restLocals(m, rest);
  PoseResult pr;
  for (size_t c = 0; c < m.clips.size(); ++c) {
    AnimClip& clip = m.clips[c];
    const int N = 60;
    std::vector<vec3> hand[2], foot[2];
    for (int i = 0; i <= N; ++i) {
      NodeLocals L = rest; sampleClip(m, (int)c, clip.dur * i / N, false, L); poseFromLocals(m, L, pr);
      hand[0].push_back(pr.joint[CJ_LHAND]); hand[1].push_back(pr.joint[CJ_RHAND]);
      foot[0].push_back(pr.joint[CJ_LFOOT]); foot[1].push_back(pr.joint[CJ_RFOOT]);
    }
    float dt = std::max(1e-4f, clip.dur / N), best = 0;
    for (int h = 0; h < 2; ++h)
      for (int i = 0; i < N; ++i) { float v = glm::length(hand[h][i + 1] - hand[h][i]) / dt; if (v > best) { best = v; clip.swing = (i + 0.5f) * dt; } }
    // planted foot (within 3 cm of its lowest point) slides toward -z (the model faces +z) at the ground speed
    float sum = 0; int cnt = 0;
    for (int f = 0; f < 2; ++f) {
      float lo = 1e9f; for (auto& p : foot[f]) lo = std::min(lo, p.y);
      for (int i = 0; i < N; ++i) if (foot[f][i].y < lo + 0.03f && foot[f][i + 1].y < lo + 0.03f) { sum += -(foot[f][i + 1].z - foot[f][i].z) / dt; ++cnt; }
    }
    clip.groundSpeed = cnt ? std::max(0.0f, sum / cnt) : 0.0f;
  }
}

void AnimPlayer::play(int k, int clipA, int clipB, float bw, float r, bool lp, bool restart) {
  if (k != key || restart) { from = last; fade = last.set.empty() ? 1.0f : 0.0f; key = k; time = 0; }
  a = clipA; b = clipB; w = bw; rate = r; loop = lp;
}

void AnimPlayer::update(const CharModel& m, float dt, PoseResult& out) {
  NodeLocals L; restLocals(m, L);
  if (b >= 0 && a >= 0) {   // phase-synced blend
    float da = m.clips[a].dur, db = m.clips[b].dur, d = std::max(1e-3f, glm::mix(da, db, w));
    time += dt * rate / d; time -= std::floor(time);
    NodeLocals B = L;
    sampleClip(m, a, time * da, true, L); sampleClip(m, b, time * db, true, B);
    mixLocals(L, B, w);
  } else {
    time += dt * rate;
    sampleClip(m, a, time, loop, L);
  }
  if (fade < 1 && !from.set.empty()) {
    fade = std::min(1.0f, fade + dt / std::max(1e-3f, fadeLen));
    NodeLocals F = from; mixLocals(F, L, fade * fade * (3 - 2 * fade)); L = F;
  }
  last = L;
  poseFromLocals(m, L, out);
}
