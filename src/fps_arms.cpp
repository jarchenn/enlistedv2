// FA1 first-person arms (docs/fps-arms.md): the rigged "Fps arms" model (tools/rig_fps_arms.mjs -> content/viewmodel)
// posed every frame from the viewmodel's arm targets (viewmodel.cpp armDrawItems) and skinned on the GPU (character.cpp),
// drawn with the weapon's textured parts (Renderer::viewModelTex: the same pass, lighting and shadows as the weapon).
//  * Where the hands go is measured on the weapon's own mesh (content/weapons/models/<dir>/mesh.bin), at the z of its
//    grip sockets: the trigger hand's palm on the right face of the stock wrist / pistol grip, the support hand's palm
//    under the fore-end, each with its knuckle line at the far edge so the fingers wrap round it.
//  * Each hand's orientation is given in weapon space (content/settings/fps_arms.json): "pinky" = the grip axis, from
//    the index finger toward the little finger (the knuckle line the fingers curl about), "palm" = the palm's normal.
//  * Two-bone IK from the shoulder (behind and below the eye) to the wrist, elbows down and out; the forearm twist
//    bone takes the hand's roll; the upper-arm stub folds into the elbow (it is never in view).
//  * Safety: a sample of the skinned vertices is checked on the CPU each frame. In ADS the support hand is lowered
//    until nothing of it is in a 6 degree cone round the sight line; an arm that still is, or that comes within 12 cm of
//    the eye within 27 degrees of the view centre, is not drawn.
#include "app.h"
#include "character.h"
#include "viewmodel.h"
#include <glm/gtx/quaternion.hpp>
#include <fstream>
#include <map>

namespace {
struct ArmSide { vec3 end{0}, elbow{0}, wrist{0}, knuckle{0}, mid{0}, thumbBase{0}; float forearm = 0.268f; };
struct ArmsRig {
  bool tried = false, ok = false;
  CharModel model[2];                 // 0 US (olive drab sleeves), 1 German (field grey)
  CharInstance inst[2];
  ArmSide side[2];                    // 0 right, 1 left
  json cfg = json::object();
  std::vector<int> sample[2];         // CPU safety sample: vertex indices per arm
};
ArmsRig gArms;

vec3 jv3(const json& j, const char* k, vec3 d) { return j.contains(k) && j[k].is_array() && j[k].size() == 3 ? vec3(j[k][0].get<float>(), j[k][1].get<float>(), j[k][2].get<float>()) : d; }

bool armsLoad() {
  if (gArms.tried) return gArms.ok;
  gArms.tried = true;
  std::string err;
  if (!initSkinning(err)) { logf("fps arms: skinning: %s (box arms kept)", err.c_str()); return false; }
  fs::path dir = contentDir() / "viewmodel";
  const char* files[2] = {"fps_arms_us.glb", "fps_arms_de.glb"};
  for (int k = 0; k < 2; ++k)
    if (!gArms.model[k].load(dir / files[k], err, 0.0f, false) || !gArms.inst[k].create(gArms.model[k])) { logf("fps arms: %s: %s (box arms kept)", files[k], err.c_str()); return false; }
  try {
    json r = json::parse(readTextFile(dir / "fps_arms.json"));
    for (int s = 0; s < 2; ++s) {
      const json& j = r[s ? "left" : "right"]; ArmSide& a = gArms.side[s];
      a.end = jv3(j, "end", a.end); a.elbow = jv3(j, "elbow", a.elbow); a.wrist = jv3(j, "wrist", a.wrist);
      a.knuckle = jv3(j, "knuckle", a.knuckle); a.mid = jv3(j, "mid", a.mid); a.thumbBase = jv3(j, "thumbBase", a.thumbBase); a.forearm = j.value("forearm", a.forearm);
    }
    gArms.cfg = json::parse(readTextFile(contentDir() / "settings" / "fps_arms.json"));
  } catch (std::exception& e) { logf("fps arms: %s (box arms kept)", e.what()); return false; }
  const CharModel& m = gArms.model[0];
  if (m.joints() != 18 || m.gpu.parts.size() != 2) { logf("fps arms: %d joints, %zu parts, expected 18 / 2 (box arms kept)", m.joints(), m.gpu.parts.size()); return false; }
  for (int v = 0; v < (int)m.verts.size(); v += 3) gArms.sample[m.verts[v].pos.x > 0 ? 1 : 0].push_back(v);   // the right arm is at x < 0
  logf("fps arms: rigged model loaded (%zu verts, 18 joints)", m.verts.size());
  return gArms.ok = true;
}

// the hand spec for this weapon and side: defaults, the weapon's grip style, then the weapon's own keys
json handSpec(const std::string& weapon, bool left) {
  const char* s = left ? "left" : "right";
  json h = gArms.cfg.value("default", json::object()).value(s, json::object());
  auto merge = [&](const json& o) { if (o.contains(s)) for (auto it = o[s].begin(); it != o[s].end(); ++it) h[it.key()] = it.value(); };
  const json& w = gArms.cfg.value("weapons", json::object());
  if (w.contains(weapon)) {
    if (w[weapon].contains("style")) merge(gArms.cfg.value("styles", json::object()).value(w[weapon]["style"].get<std::string>(), json::object()));
    merge(w[weapon]);
  }
  return h;
}
// a hand's rotation from a spec's weapon-space grip axis (index -> little finger) and palm normal, rotated by W.
// Rest axes (model space): X wrist -> knuckles, Y toward the little finger, Z = X x Y (the right hand's palm side, the
// left hand's back; the model's right arm is the one at x < 0, tools/rig_fps_arms.mjs).
glm::mat3 handBasis(const json& h, const glm::mat3& W, bool left) {
  vec3 palm = glm::normalize(W * jv3(h, "palm", left ? vec3(0, 1, 0) : vec3(1, 0, 0)));
  vec3 Y = glm::normalize(W * jv3(h, "pinky", left ? vec3(0, 0, -1) : vec3(0, -0.6f, -0.8f)));
  vec3 Z = left ? -palm : palm;   // rest: Z = +x model, the right hand's palm side, the left hand's back
  Y = glm::normalize(Y - Z * glm::dot(Y, Z));
  return glm::mat3(glm::cross(Y, Z), Y, Z);
}
mat4 frameOf(const glm::mat3& B, vec3 o) { mat4 m(B); m[3] = vec4(o, 1); return m; }
glm::mat3 boneBasis(vec3 X, vec3 out) {   // X along the bone, Y the elbow-out / pinky side made perpendicular
  X = glm::normalize(X);
  vec3 Y = out - X * glm::dot(out, X);
  if (glm::dot(Y, Y) < 1e-8f) Y = glm::cross(X, vec3(1, 0, 0));
  Y = glm::normalize(Y);
  return glm::mat3(X, Y, glm::cross(X, Y));
}
mat4 rotAbout(vec3 p, vec3 axis, float deg) { return glm::translate(mat4(1), p) * glm::mat4_cast(glm::angleAxis(radians(deg), glm::normalize(axis))) * glm::translate(mat4(1), -p); }

// The weapon's cross-section at each grip socket, from its baked mesh (weapon space). gR: the right face of the stock
// wrist / grip at the socket's height; gL: the bottom of the fore-end under the socket, centred; wR / wL: the section's
// depth / width the fingers wrap round.
struct GripGeo { bool ok = false; vec3 gR{0}, gL{0}; float wR = 0.04f, wL = 0.05f; };
const GripGeo& gripGeo(const WeaponDef& d) {
  static std::map<std::string, GripGeo> cache;
  auto it = cache.find(d.id);
  if (it != cache.end()) return it->second;
  GripGeo g; g.gR = d.gripR; g.gL = d.gripL;
  try {
    json j = json::parse(readTextFile(d.file));
    std::string dir = j.contains("model") ? j["model"].value("dir", std::string()) : std::string();
    std::ifstream f(contentDir() / "weapons" / "models" / dir / "mesh.bin", std::ios::binary);
    uint32_t hdr[3] = {0, 0, 0};
    if (!dir.empty() && f.read((char*)hdr, sizeof hdr) && hdr[0] == 0x31574D4Eu) {
      std::vector<TexVertex> V(hdr[1]); std::vector<uint32_t> I(hdr[2]);
      f.read((char*)V.data(), V.size() * sizeof(TexVertex)); f.read((char*)I.data(), I.size() * 4);
      // the cross-section: every triangle cut by the plane z = s.z (low-poly stocks have long triangles, few vertices)
      auto section = [&](vec3 s, float above, vec3& lo, vec3& hi) {
        lo = vec3(1e9f); hi = vec3(-1e9f); int n = 0;
        auto take = [&](vec3 p) { if (std::fabs(p.x - s.x) < 0.07f && p.y < s.y + above && p.y > s.y - 0.15f) { lo = glm::min(lo, p); hi = glm::max(hi, p); ++n; } };
        for (size_t t = 0; t + 2 < I.size(); t += 3) {
          vec3 q[3] = {V[I[t]].pos, V[I[t + 1]].pos, V[I[t + 2]].pos};
          for (int e = 0; e < 3; ++e) {
            vec3 a = q[e], b = q[(e + 1) % 3];
            if ((a.z - s.z) * (b.z - s.z) > 0 || a.z == b.z) continue;
            take(glm::mix(a, b, (s.z - a.z) / (b.z - a.z)));
          }
        }
        return n > 4;
      };
      vec3 lo, hi;
      if (f && section(d.gripL, 0.04f, lo, hi)) { g.gL = vec3((lo.x + hi.x) * 0.5f, lo.y, d.gripL.z); g.wL = hi.x - lo.x; g.ok = true; }
      if (f && section(d.gripR, 0.03f, lo, hi)) { g.gR = vec3(lo.x, d.gripR.y, d.gripR.z); g.wR = std::min(0.06f, hi.x - lo.x); }   // weapon +x is the shooter's left: the right face is min x
    }
  } catch (...) {}
  logf("fps arms: %s grips: trigger (%.3f, %.3f, %.3f) section %.3f, support (%.3f, %.3f, %.3f) width %.3f%s", d.id.c_str(), g.gR.x, g.gR.y, g.gR.z, g.wR, g.gL.x, g.gL.y, g.gL.z, g.wL, g.ok ? "" : " (sockets: no mesh)");
  return cache.emplace(d.id, g).first->second;
}
}   // namespace

bool fpsArmsReady() { return armsLoad(); }
json fpsArmsSpec(const std::string& weapon, bool left) { armsLoad(); return handSpec(weapon, left); }
glm::mat3 fpsArmsBasis(const json& h, const glm::mat3& W, bool left) { return handBasis(h, W, left); }
// the weapon-space grip targets the hands are posed on (measured; the sockets when the weapon has no baked mesh)
void fpsArmsGrips(const WeaponDef& d, vec3& gR, vec3& gL) { const GripGeo& g = gripGeo(d); gR = g.gR; gL = g.gL; }
bool fpsArmsIsModel(const GpuModel* g) { return g && (g == &gArms.inst[0].view || g == &gArms.inst[1].view); }   // self-tests: an arms draw

// Poses and draws the arms. Camera space in: grip targets, hand rotations, hand specs, 0..1 grip weights (1 curled on
// the weapon); S out: the shoulders used. Out: elbows, wrists, which arms were drawn. False: no rig (box arms then).
bool fpsArmsDraw(App& app, const mat4& cam, const WeaponDef& d, vec3 S[2], const vec3 target0[2], const glm::mat3 hand[2], const json spec[2], float gripW[2],
                 vec3 elbowOut[2], vec3 wristOut[2], bool drawn[2]) {
  if (!armsLoad()) return false;
  bool german = d.side == "DE";
  const CharModel& m = gArms.model[german ? 1 : 0];
  S[0] = jv3(gArms.cfg, "shoulderR", S[0]); S[1] = jv3(gArms.cfg, "shoulderL", S[1]);   // behind and below the eye
  const GripGeo& gg = gripGeo(d);
  std::vector<mat4> skin(18, mat4(1));
  float ads = app.adsT * app.adsT * (3 - 2 * app.adsT);
  const float cone = std::tan(radians(gArms.cfg.value("sightConeDeg", 6.0f))), nearZ = gArms.cfg.value("nearClear", 0.12f), nearAds = gArms.cfg.value("nearClearAds", 0.25f);
  auto pose = [&](int s, vec3 target) {
    const ArmSide& a = gArms.side[s];
    const json& h = spec[s];
    int o = s * 9;
    glm::mat3 B = hand[s];
    vec3 palmN = s ? -B[2] : B[2];
    // knuckles at the far edge of the section: the palm centre sits (knuckle distance - half the section) along the fingers
    float knuck = glm::length(a.knuckle - a.wrist), half = 0.5f * (s ? gg.wL : gg.wR);
    vec3 c = jv3(h, "contact", vec3(knuck - half - 0.01f, 0.0f, 0.022f));
    vec3 W = target - B[0] * c.x - B[1] * c.y - palmN * c.z, E, Ws;
    vec3 pole = s ? vec3(-0.5f, -1, 0.15f) : vec3(0.5f, -1, 0.15f);   // elbows down and out
    solveTwoBone(S[s], W, kUpperArm, a.forearm, pole, E, Ws);
    float soften = h.value("soften", 0.0f);
    if (soften > 0) {   // optionally turn the hand part of the way toward the forearm (a straighter wrist), and solve again
      vec3 X = glm::normalize(glm::mix(B[0], glm::normalize(Ws - E), soften));
      vec3 Z = glm::normalize(B[2] - X * glm::dot(B[2], X));
      B = glm::mat3(X, glm::cross(Z, X), Z); palmN = s ? -B[2] : B[2];
      W = target - B[0] * c.x - B[1] * c.y - palmN * c.z;
      solveTwoBone(S[s], W, kUpperArm, a.forearm, pole, E, Ws);
    }
    elbowOut[s] = E; wristOut[s] = Ws;
    vec3 u = glm::normalize(Ws - S[s]), eo = (E - S[s]) - u * glm::dot(E - S[s], u);
    if (glm::dot(eo, eo) < 1e-8f) eo = pole;
    mat4 up = frameOf(boneBasis(S[s] - E, eo), E) * glm::scale(mat4(1), vec3(0.3f));   // the stub folds into the elbow
    mat4 fore = frameOf(boneBasis(Ws - E, eo), E), twist = frameOf(boneBasis(Ws - E, B[1]), E);
    skin[o + 0] = up * m.invBind[o + 0];
    skin[o + 1] = fore * m.invBind[o + 1];
    skin[o + 2] = twist * m.invBind[o + 2];
    mat4 H = frameOf(B, Ws) * m.invBind[o + 3];
    skin[o + 3] = H;
    float g = gripW[s];
    const json cj = h.value("curl", json::object());
    auto pair = [&](const char* k, float d0, float d1) { vec2 v(d0, d1); if (cj.contains(k) && cj[k].is_array() && cj[k].size() == 2) v = vec2(cj[k][0].get<float>(), cj[k][1].get<float>()); return v * g; };
    glm::mat3 R0 = glm::transpose(glm::mat3(m.invBind[o + 3]));   // rest hand axes (model space)
    float sg = s ? 1.0f : -1.0f;   // the palm is +Z on the right hand, -Z on the left: curls turn X toward it
    vec2 fc = pair("fingers", 80, 70), ic = pair("index", 35, 25);
    float tc = cj.value("thumb", 30.0f) * g;
    mat4 f1 = rotAbout(a.knuckle, R0[1], sg * fc.x), f2 = rotAbout(a.mid, R0[1], sg * fc.y);
    mat4 i1 = rotAbout(a.knuckle, R0[1], sg * ic.x), i2 = rotAbout(a.mid, R0[1], sg * ic.y);
    skin[o + 4] = H * f1; skin[o + 5] = H * f1 * f2;
    skin[o + 6] = H * i1; skin[o + 7] = H * i1 * i2;
    skin[o + 8] = H * rotAbout(a.thumbBase, R0[0], sg * tc);
  };
  // safety: sampled vertices in the ADS sight cone / inside the view too close to the eye (camera space: -z forward)
  auto check = [&](int s, int& inCone, int& tooNear) {
    inCone = tooNear = 0;
    for (int v : gArms.sample[s]) {
      const CharModel::SkinVert& sv = m.verts[v];
      vec3 p(0);
      for (int q = 0; q < 4; ++q) if (sv.w[q] > 0) p += sv.w[q] * vec3(skin[sv.j[q]] * vec4(sv.pos, 1));
      float fwd = -p.z, r = glm::length(vec2(p.x, p.y));
      if (fwd <= 0.005f) continue;
      if (ads > 0.3f && r < fwd * cone) ++inCone;
      if (fwd < (ads > 0.3f ? nearAds : nearZ) && r < fwd * 0.5f) ++tooNear;   // close to the eye near the middle of the view (a sleeve end-on, a wrist at the cheek)
    }
  };
  vec3 target[2] = {target0[0], target0[1]};
  int cone2[2] = {0, 0}, near2[2] = {0, 0};
  for (int s = 0; s < 2; ++s) {
    pose(s, target[s]);
    check(s, cone2[s], near2[s]);
    for (int it = 0; it < 6 && s == 1 && cone2[s] > 0; ++it) { target[s].y -= 0.012f; pose(s, target[s]); check(s, cone2[s], near2[s]); }   // the support hand drops below the sight line
    drawn[s] = cone2[s] == 0 && near2[s] == 0;
  }
  CharInstance& ci = gArms.inst[german ? 1 : 0];
  ci.skin(skin);
  for (int k = 0; k < 2 && k < (int)ci.view.parts.size(); ++k) if (drawn[k]) app.R.viewModelTex.push_back({&ci.view, k, cam});
  return true;
}
