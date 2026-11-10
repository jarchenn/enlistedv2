// FA1 first-person arms (docs/fps-arms.md): the rigged "Fps arms" model (tools/rig_fps_arms.mjs -> content/viewmodel)
// posed every frame from the viewmodel's arm IK (viewmodel.cpp armDrawItems) and skinned on the GPU (character.cpp),
// drawn with the weapon's textured parts (Renderer::viewModelTex: same pass, lighting and shadows as the weapon).
//  * each hand has a weapon-space frame (content/settings/fps_arms.json: finger direction, palm normal, the contact
//    point between the palm and the grip, finger / index / thumb curl; per weapon overrides);
//  * the wrist is solved for, not the grip: two-bone IK from the shoulder to the wrist (pole down and out), then the
//    hand frame is turned part of the way toward the forearm (soften) and the IK solved again, so wrists do not break;
//  * the forearm twist bone takes the hand's roll, the forearm bone the elbow's: the twist spreads along the forearm;
//  * fingers, index and thumb curl about the hand's own axes from their rest landmarks.
#include "app.h"
#include "character.h"
#include "viewmodel.h"
#include <glm/gtx/quaternion.hpp>

namespace {
struct ArmSide { vec3 end{0}, elbow{0}, wrist{0}, knuckle{0}, mid{0}, thumbBase{0}; float forearm = 0.268f; };
struct ArmsRig {
  bool tried = false, ok = false;
  CharModel model[2];                 // 0 US (olive drab sleeves), 1 German (field grey)
  CharInstance inst[2];
  ArmSide side[2];                    // 0 right, 1 left
  json cfg = json::object();
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
  for (int k = 0; k < 2; ++k) {
    if (!gArms.model[k].load(dir / files[k], err, 0.0f, false) || !gArms.inst[k].create(gArms.model[k])) { logf("fps arms: %s: %s (box arms kept)", files[k], err.c_str()); return false; }
  }
  try {
    json r = json::parse(readTextFile(dir / "fps_arms.json"));
    for (int s = 0; s < 2; ++s) {
      const json& j = r[s ? "left" : "right"]; ArmSide& a = gArms.side[s];
      a.end = jv3(j, "end", a.end); a.elbow = jv3(j, "elbow", a.elbow); a.wrist = jv3(j, "wrist", a.wrist);
      a.knuckle = jv3(j, "knuckle", a.knuckle); a.mid = jv3(j, "mid", a.mid); a.thumbBase = jv3(j, "thumbBase", a.thumbBase); a.forearm = j.value("forearm", a.forearm);
    }
    gArms.cfg = json::parse(readTextFile(contentDir() / "settings" / "fps_arms.json"));
  } catch (std::exception& e) { logf("fps arms: %s (box arms kept)", e.what()); return false; }
  if (gArms.model[0].joints() != 18) { logf("fps arms: %d joints, expected 18 (box arms kept)", gArms.model[0].joints()); return false; }
  logf("fps arms: rigged model loaded (%zu verts, 18 joints)", gArms.model[0].verts.size());
  return gArms.ok = true;
}

// the hand spec for this weapon and side: defaults, then the weapon's own keys
json handSpec(const std::string& weapon, bool left) {
  const char* s = left ? "left" : "right";
  json h = gArms.cfg.value("default", json::object()).value(s, json::object());
  const json& w = gArms.cfg.value("weapons", json::object());
  if (w.contains(weapon) && w[weapon].contains(s)) for (auto it = w[weapon][s].begin(); it != w[weapon][s].end(); ++it) h[it.key()] = it.value();
  return h;
}
// a hand's rotation (camera space) from a spec's weapon-space finger direction and palm normal
glm::mat3 handBasis(const json& h, const glm::mat3& W, bool left) {
  vec3 X = glm::normalize(W * jv3(h, "fwd", vec3(0, 0, 1))), palm = glm::normalize(W * jv3(h, "palm", left ? vec3(1, 0, 0) : vec3(-1, 0, 0)));
  vec3 Z = left ? palm : -palm;            // rest: Z = +x model, the back of the right hand / the palm of the left
  vec3 Y = glm::normalize(glm::cross(Z, X)); Z = glm::cross(X, Y);
  return glm::mat3(X, Y, Z);
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
}   // namespace

bool fpsArmsReady() { return armsLoad(); }

// Poses and draws one pair of arms. Camera space in: shoulders, grip targets (tR, tL), hand rotations (hR, hL, rest:
// model axes X = fingers, Z = +x), and a 0..1 grip weight per hand (1 = curled on the weapon, 0 = open). Out: the
// elbows and wrists solved (for the self-tests). Returns false when the rig is not available (box arms then).
bool fpsArmsDraw(App& app, const mat4& cam, bool german, vec3 S[2], const vec3 target[2], const glm::mat3 hand[2], const json spec[2], float gripW[2],
                 vec3 elbowOut[2], vec3 wristOut[2], vec3 handErr) {
  (void)handErr;
  if (!armsLoad()) return false;
  const CharModel& m = gArms.model[german ? 1 : 0];
  S[0] = jv3(gArms.cfg, "shoulderR", S[0]); S[1] = jv3(gArms.cfg, "shoulderL", S[1]);   // the model's shoulders (camera space), off-screen
  std::vector<mat4> skin(18, mat4(1));
  for (int s = 0; s < 2; ++s) {
    const ArmSide& a = gArms.side[s];
    const json& h = spec[s];
    int o = s * 9;
    // wrist: the grip contact point given in the hand frame (fingers, pinky side, palm side)
    vec3 c = jv3(h, "contact", vec3(0.075f, 0.0f, 0.035f));
    float soften = h.value("soften", 0.25f);
    glm::mat3 B = hand[s];
    vec3 palmN = s ? B[2] : -B[2];
    vec3 pole = s ? vec3(-0.45f, -1, 0.1f) : vec3(0.45f, -1, 0.1f);   // elbows down and out
    vec3 W = target[s] - B[0] * c.x - B[1] * c.y - palmN * c.z, E, Wsolved;
    for (int it = 0; it < 2; ++it) {
      solveTwoBone(S[s], W, kUpperArm, a.forearm, pole, E, Wsolved);
      if (it == 0 && soften > 0) {   // turn the hand part of the way toward the forearm, then solve for its wrist again
        vec3 X = glm::normalize(glm::mix(B[0], glm::normalize(Wsolved - E), soften));
        vec3 Z = B[2] - X * glm::dot(B[2], X); Z = glm::normalize(Z);
        B = glm::mat3(X, glm::cross(Z, X), Z);
        palmN = s ? B[2] : -B[2];
        W = target[s] - B[0] * c.x - B[1] * c.y - palmN * c.z;
      }
    }
    elbowOut[s] = E; wristOut[s] = Wsolved;
    // elbow-out direction: from the shoulder-wrist line toward the elbow
    vec3 u = glm::normalize(Wsolved - S[s]), eo = (E - S[s]) - u * glm::dot(E - S[s], u);
    if (glm::dot(eo, eo) < 1e-8f) eo = pole;
    mat4 up = frameOf(boneBasis(S[s] - E, eo), E), fore = frameOf(boneBasis(Wsolved - E, eo), E);
    mat4 twist = frameOf(boneBasis(Wsolved - E, B[1]), E);   // the hand's pinky side: the roll the twist spreads along the forearm
    mat4 handM = frameOf(B, Wsolved);
    skin[o + 0] = up * m.invBind[o + 0];
    skin[o + 1] = fore * m.invBind[o + 1];
    skin[o + 2] = twist * m.invBind[o + 2];
    mat4 H = handM * m.invBind[o + 3];
    skin[o + 3] = H;
    // curls (degrees; positive toward the palm), blended open by the grip weight
    float g = gripW[s];
    auto pair = [&](const char* k, float d0, float d1) { const json& cj = h.value("curl", json::object()); vec2 v(d0, d1);
      if (cj.contains(k) && cj[k].is_array() && cj[k].size() == 2) v = vec2(cj[k][0].get<float>(), cj[k][1].get<float>()); return v * g; };
    glm::mat3 R0 = glm::mat3(m.invBind[o + 3]); R0 = glm::transpose(R0);   // rest hand basis (model space)
    float sg = s ? -1.0f : 1.0f;   // the palm is -Z on the right hand, +Z on the left
    vec2 fc = pair("fingers", 70, 60), ic = pair("index", 30, 20);
    float tc = h.value("curl", json::object()).value("thumb", 25.0f) * g;
    mat4 f1 = rotAbout(a.knuckle, R0[1], sg * fc.x), f2 = rotAbout(a.mid, R0[1], sg * fc.y);
    mat4 i1 = rotAbout(a.knuckle, R0[1], sg * ic.x), i2 = rotAbout(a.mid, R0[1], sg * ic.y);
    skin[o + 4] = H * f1; skin[o + 5] = H * f1 * f2;
    skin[o + 6] = H * i1; skin[o + 7] = H * i1 * i2;
    skin[o + 8] = H * rotAbout(a.thumbBase, R0[0], sg * tc);
  }
  CharInstance& ci = gArms.inst[german ? 1 : 0];
  ci.skin(skin);
  for (int k = 0; k < (int)ci.view.parts.size(); ++k) app.R.viewModelTex.push_back({&ci.view, k, cam});
  return true;
}

// the hand spec for a weapon (shared with viewmodel.cpp)
json fpsArmsSpec(const std::string& weapon, bool left) { armsLoad(); return handSpec(weapon, left); }
glm::mat3 fpsArmsBasis(const json& h, const glm::mat3& W, bool left) { return handBasis(h, W, left); }
bool fpsArmsIsModel(const GpuModel* g) { return g && (g == &gArms.inst[0].view || g == &gArms.inst[1].view); }   // self-tests: an arms draw
