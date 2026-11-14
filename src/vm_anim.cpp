// MG1 / MG2: animated first-person viewmodels (see vm_anim.h).
#include "vm_anim.h"
#include "app.h"
#include "character.h"
#include "anim_clip.h"
#include "assets.h"
#include <glm/gtx/quaternion.hpp>

struct AnimVM {
  CharModel model;
  int gunJoint = -1;
  vec3 pt[3]{};                        // rest rig space: rear sight (scopes: the eye), front sight, muzzle
  std::map<std::string, int> clip;     // state name -> clip index
  float idleHold = -1;                 // >= 0: the idle is this one frame of the idle clip (s), held still
  bool shells = false;                 // reload_start / reload_insert (once per round) / reload_end
  vec3 dot{0}; bool hasDot = false;    // red-dot sight: the dot at this rig point (the front lens centre), seen along the sight line
  GpuMesh dotMesh;
  ~AnimVM() { model.destroy(); dotMesh.destroy(); }
};

namespace {
enum { K_IDLE, K_FIRE, K_RELOAD };
struct St {
  CharInstance inst; AnimPlayer pl; PoseResult pose;
  int lastShot = -1; float lastReload = 0, kick = 0; bool ready = false;
  std::string clipName; mat4 local{1};   // rig -> camera (lowering, sway / bob, ADS); the camera is applied when drawn
};
std::map<const WeaponDef*, St> gSt;
const WeaponDef* gActive = nullptr;
vec3 jv(const json& a) { return vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>()); }
// the camera as it is when the frame is drawn (look, view kick and breathing are updated after the weapon step)
mat4 camNow(const App& app) {
  vec3 e = app.eyePos(), f = App::forward(app.eyeYaw + app.viewKickYaw, app.eyePitch + app.viewKickPitch);
  return glm::inverse(glm::lookAt(e, e + f, app.viewUp(f)));
}
}

std::shared_ptr<AnimVM> animVmLoad(const fs::path& dir, const json& block, std::string& err) {
  static bool skinOk = initSkinning(err);
  if (!skinOk) { err = "skinning: " + err; return nullptr; }
  json meta;
  try { meta = json::parse(readTextFile(dir / "vm.json")); } catch (...) { err = "missing or bad " + (dir / "vm.json").string(); return nullptr; }
  auto a = std::make_shared<AnimVM>();
  if (!a->model.load(dir / "vm.glb", err, 0, false)) return nullptr;
  std::string gj = meta.value("gunJoint", "");
  for (int j = 0; j < a->model.joints(); ++j) if (a->model.nodeName[a->model.jointNode[j]] == gj) a->gunJoint = j;
  if (a->gunJoint < 0) { err = "gun joint '" + gj + "' not in the skin"; return nullptr; }
  json P = meta["points"];
  if (block.contains("points")) for (auto& [k, v] : block["points"].items()) P[k] = v;   // rig-space points measured on the rig itself
  a->pt[0] = jv(P.contains("eye") ? P["eye"] : P["rear"]); a->pt[1] = jv(P["front"]); a->pt[2] = jv(P["muzzle"]);
  // state -> clip name (defaults: the same name). No draw clip (equip goes straight to the idle) and no movement clips.
  json map = block.value("clips", json::object());
  for (const char* k : {"idle", "fire", "reload", "reload_empty", "reload_start", "reload_insert", "reload_end"}) {
    int c = findClip(a->model, map.value(k, std::string(k)));
    if (c >= 0) a->clip[k] = c;
  }
  if (!a->clip.count("idle")) { err = "no idle clip"; return nullptr; }
  a->idleHold = block.value("idleHold", -1.0f);
  a->shells = a->clip.count("reload_insert") > 0;
  if (block.contains("redDot")) {   // a small emissive disc facing back along the sight line; the tube hides it off the axis
    a->hasDot = true; a->dot = jv(block["redDot"]);
    float r = block.value("redDotRadius", 0.0006f);
    Material m{"red_dot"}; m.albedo = vec3(1, 0.05f, 0.03f); m.emissive = vec3(40, 1.2f, 0.6f);
    Part p; p.shape = Part::CYL; p.size = vec3(2 * r, 0.0002f, 2 * r); p.collide = false; p.seg = 16;
    p.rot = glm::rotation(vec3(0, 1, 0), glm::normalize(a->pt[1] - a->pt[0])); p.center = a->dot;
    MeshData md; appendPart(md, p, m, mat4(1)); a->dotMesh.upload(md);
  }
  return a;
}

float animVmClipDur(const AnimVM& a, const std::string& clip) { auto it = a.clip.find(clip); return it == a.clip.end() ? -1.0f : a.model.clips[it->second].dur; }

std::string animVmClipNow(const WeaponDef& d, float* time) {
  auto it = gSt.find(&d); if (it == gSt.end()) return "";
  if (time) *time = it->second.pl.time;
  return it->second.clipName;
}

void animVmStep(App& app, const WeaponDef& d, WeaponState& s, float dt) {
  const AnimVM& a = *d.anim;
  St& st = gSt[&d];
  if (!st.inst.model && !st.inst.create(a.model)) return;
  auto clip = [&](const std::string& n) { auto it = a.clip.find(n); return it == a.clip.end() ? -1 : it->second; };
  auto idle = [&](float fade) {
    st.pl.fadeLen = fade; st.pl.play(K_IDLE, clip("idle"), -1, 0, a.idleHold >= 0 ? 0.0f : 1.0f, true, true); st.clipName = "idle";
    if (a.idleHold >= 0) st.pl.time = a.idleHold;
  };
  auto play = [&](int key, const std::string& name, float fade, float rate = 1) {
    int c = clip(name); if (c < 0) { idle(fade); return; }
    st.pl.fadeLen = fade; st.pl.play(key, c, -1, 0, rate, false, true); st.clipName = name;
  };
  if (gActive != &d) {   // just equipped: straight to the standing idle (the sources' draw clips start from odd poses)
    gActive = &d; idle(0.0f);
    st.lastShot = s.shotIdx; st.lastReload = s.reload; st.kick = 0;
  }
  // the reload clip the weapon state asks for ("" = none)
  std::string want;
  if (s.reload > 0 && s.reloadKind) {
    if (a.shells && d.feed == "single") {
      if (s.reloadKind == 4) want = "reload_end";
      else {
        float el = s.reloadTotal - s.reload, sd = animVmClipDur(a, "reload_start");
        want = s.reloadTotal > d.reloadRound + 1e-3f && el < sd ? "reload_start" : "reload_insert";
      }
    } else if (s.reloadKind == 4) want = st.pl.key == K_RELOAD ? st.clipName : "";
    else want = s.reloadKind == 1 && clip("reload_empty") >= 0 ? "reload_empty" : "reload";
  }
  if (s.shotIdx != st.lastShot) {   // automatics restart the fire clip at the cyclic rate
    st.lastShot = s.shotIdx;
    if (clip("fire") >= 0) play(K_FIRE, "fire", 0.04f); else st.kick = 1;   // no fire clip (the AR-15 set): a procedural kick
  } else if (!want.empty() && (want != st.clipName || st.pl.key != K_RELOAD || s.reload > st.lastReload + 1e-4f)) {
    float dur = animVmClipDur(a, want), rate = 1;
    if (!a.shells && s.reloadKind != 4 && dur > 0 && s.reloadTotal > 0) rate = dur / s.reloadTotal;   // a magazine clip spans the reload exactly
    play(K_RELOAD, want, want == "reload_insert" && st.clipName == "reload_insert" ? 0.05f : 0.1f, rate);
  } else if (st.pl.key != K_IDLE && (st.pl.key != K_RELOAD || s.reload <= 0)) {
    const AnimClip& c = a.model.clips[st.pl.a];
    if (st.pl.time >= c.dur - 1e-3f || (st.pl.key == K_RELOAD && s.reload <= 0)) idle(0.1f);
  }
  st.lastReload = s.reload;
  st.pl.update(a.model, dt, st.pose);
  st.inst.skin(st.pose.skin);
  st.kick = std::max(0.0f, st.kick - dt / 0.12f);
  // ---- rig -> camera: lowering (mantle / swim) * sway & bob * kick * ADS (posed sights onto the view axis)
  const mat4& D = st.pose.delta[a.gunJoint];
  vec3 R = vec3(D * vec4(a.pt[0], 1)), F = vec3(D * vec4(a.pt[1], 1));
  bool scoped = app.vmScoped(d);
  float relief = scoped ? d.scopeRelief : d.eyeRelief, t = app.adsT * app.adsT * (3 - 2 * app.adsT);
  quat q = glm::rotation(glm::normalize(F - R), vec3(0, 0, -1));
  mat4 ads = glm::translate(mat4(1), glm::mix(R, vec3(0, 0, -relief), t)) * glm::mat4_cast(glm::slerp(quat(1, 0, 0, 0), q, t)) * glm::translate(mat4(1), -R);
  mat4 proc(1);
  if (app.vm.procedural) {
    vec3 piv(0, 0, -0.3f);
    proc = glm::translate(mat4(1), app.vm.swayP + app.vm.bob) * glm::translate(mat4(1), piv) *
           glm::mat4_cast(glm::angleAxis(radians(app.vm.swayR.x), vec3(0, 1, 0)) * glm::angleAxis(radians(app.vm.swayR.y), vec3(1, 0, 0))) * glm::translate(mat4(1), -piv);
  }
  if (st.kick > 0) {   // back 15 mm and up 2 deg about the rear sight, decaying over 0.12 s
    float k = st.kick * st.kick;
    vec3 Rv = glm::mix(R, vec3(0, 0, -relief), t);
    proc = proc * glm::translate(mat4(1), vec3(0, 0, 0.015f * k)) * glm::translate(mat4(1), Rv) * glm::mat4_cast(glm::angleAxis(radians(2.0f * k), vec3(1, 0, 0))) * glm::translate(mat4(1), -Rv);
  }
  mat4 low(1);
  if (float lw = std::max(app.mantleLower(), app.swimLower()); lw > 0) {
    float k = lw * lw * (3 - 2 * lw);
    low = glm::translate(mat4(1), vec3(0.05f * k, -0.34f * k, 0.08f * k)) * glm::mat4_cast(glm::angleAxis(radians(-60.0f * k), vec3(1, 0, 0)));
  }
  st.local = low * proc * ads;
  st.ready = true;
}

void animVmNotActive() { gActive = nullptr; }

bool animVmDraw(App& app, const WeaponDef& d) {
  auto it = gSt.find(&d);
  if (it == gSt.end() || !it->second.ready || gActive != &d) return false;
  St& st = it->second;
  mat4 rig = camNow(app) * st.local;
  for (size_t k = 0; k < st.inst.view.parts.size(); ++k) app.R.viewModelTex.push_back({&st.inst.view, (int)k, rig});
  if (d.anim->hasDot && app.adsT > 0.6f) app.R.viewModel.push_back({&d.anim->dotMesh, rig * st.pose.delta[d.anim->gunJoint]});
  return true;
}

bool animVmPoint(const App& app, const WeaponDef& d, int which, vec3& world) {
  auto it = gSt.find(&d);
  if (!d.anim || it == gSt.end() || !it->second.ready || which < 0 || which > 2) return false;
  const St& st = it->second;
  world = vec3(camNow(app) * st.local * st.pose.delta[d.anim->gunJoint] * vec4(d.anim->pt[which], 1));
  return true;
}
