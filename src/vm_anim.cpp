// MG1: animated first-person viewmodels (see vm_anim.h).
#include "vm_anim.h"
#include "app.h"
#include "character.h"
#include "anim_clip.h"
#include <glm/gtx/quaternion.hpp>

struct AnimVM {
  CharModel model;
  int gunJoint = -1;
  vec3 pt[3]{};                        // rest rig space: rear sight (scopes: the eye), front sight, muzzle
  std::map<std::string, int> clip;     // state name -> clip index
  ~AnimVM() { model.destroy(); }
};

namespace {
enum { K_IDLE, K_FIRE, K_RELOAD, K_DRAW };
struct St {
  CharInstance inst; AnimPlayer pl; PoseResult pose;
  int lastShot = -1; float lastReload = 0; bool ready = false;
  std::string clipName; mat4 rig{1};
};
std::map<const WeaponDef*, St> gSt;
const WeaponDef* gActive = nullptr;
vec3 jv(const json& a) { return vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>()); }
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
  const json& P = meta["points"];
  a->pt[0] = jv(P.contains("eye") ? P["eye"] : P["rear"]); a->pt[1] = jv(P["front"]); a->pt[2] = jv(P["muzzle"]);
  json map = block.value("clips", json::object());   // state -> clip name (defaults: the same name)
  for (const char* k : {"idle", "fire", "reload", "reload_empty", "draw"}) {
    int c = findClip(a->model, map.value(k, std::string(k)));
    if (c >= 0) a->clip[k] = c;
  }
  if (!a->clip.count("idle")) { err = "no idle clip"; return nullptr; }
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
  auto clip = [&](const char* n) { auto it = a.clip.find(n); return it == a.clip.end() ? -1 : it->second; };
  auto play = [&](int key, const char* name, bool loop, float fade, float rate = 1) {
    int c = clip(name); if (c < 0) { name = "idle"; c = clip("idle"); loop = true; key = K_IDLE; }
    st.pl.fadeLen = fade; st.pl.play(key, c, -1, 0, rate, loop, true); st.clipName = name;
  };
  if (gActive != &d) {   // just equipped: the draw clip (firing waits for most of it), then idle
    gActive = &d;
    play(K_DRAW, "draw", false, 0.0f);
    if (clip("draw") >= 0) s.cooldown = std::max(s.cooldown, 0.8f * a.model.clips[clip("draw")].dur);
    st.lastShot = s.shotIdx; st.lastReload = s.reload;
  }
  if (s.shotIdx != st.lastShot) { st.lastShot = s.shotIdx; play(K_FIRE, "fire", false, 0.04f); }   // automatics restart it at the cyclic rate
  else if (s.reload > 0 && (st.pl.key != K_RELOAD || s.reload > st.lastReload + 1e-4f)) {   // a reload began (or the next single round)
    bool empty = s.reloadKind == 1 && clip("reload_empty") >= 0;
    const char* n = empty ? "reload_empty" : "reload";
    float dur = clip(n) >= 0 ? a.model.clips[clip(n)].dur : 0;
    play(K_RELOAD, n, false, 0.1f, dur > 0 && s.reloadTotal > 0 ? dur / s.reloadTotal : 1.0f);   // the clip spans the reload exactly
  } else if (st.pl.key != K_IDLE && (st.pl.key != K_RELOAD || s.reload <= 0)) {
    const AnimClip& c = a.model.clips[st.pl.a];
    if (st.pl.time >= c.dur - 1e-3f || (st.pl.key == K_RELOAD && s.reload <= 0)) play(K_IDLE, "idle", true, 0.1f);
  }
  st.lastReload = s.reload;
  st.pl.update(a.model, dt, st.pose);
  st.inst.skin(st.pose.skin);
  // ---- rig -> world: camera * lowering (mantle / swim) * sway & bob * ADS (posed sights onto the view axis)
  vec3 e = app.eyePos(), f = App::forward(app.eyeYaw + app.viewKickYaw, app.eyePitch + app.viewKickPitch);
  mat4 cam = glm::inverse(glm::lookAt(e, e + f, app.viewUp(f)));
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
  mat4 low(1);
  if (float lw = std::max(app.mantleLower(), app.swimLower()); lw > 0) {
    float k = lw * lw * (3 - 2 * lw);
    low = glm::translate(mat4(1), vec3(0.05f * k, -0.34f * k, 0.08f * k)) * glm::mat4_cast(glm::angleAxis(radians(-60.0f * k), vec3(1, 0, 0)));
  }
  st.rig = cam * low * proc * ads;
  st.ready = true;
}

void animVmNotActive() { gActive = nullptr; }

bool animVmDraw(App& app, const WeaponDef& d) {
  auto it = gSt.find(&d);
  if (it == gSt.end() || !it->second.ready || gActive != &d) return false;
  St& st = it->second;
  for (size_t k = 0; k < st.inst.view.parts.size(); ++k) app.R.viewModelTex.push_back({&st.inst.view, (int)k, st.rig});
  return true;
}

bool animVmPoint(const App& app, const WeaponDef& d, int which, vec3& world) {
  (void)app;
  auto it = gSt.find(&d);
  if (!d.anim || it == gSt.end() || !it->second.ready || which < 0 || which > 2) return false;
  const St& st = it->second;
  world = vec3(st.rig * st.pose.delta[d.anim->gunJoint] * vec4(d.anim->pt[which], 1));
  return true;
}
