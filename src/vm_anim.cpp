// MG1 / MG2: animated first-person viewmodels (see vm_anim.h).
#include "vm_anim.h"
#include "app.h"
#include "character.h"
#include "anim_clip.h"
#include "assets.h"
#include "br_models.h"
#include "loot.h"   // BR10: lootAttachDraws
#include <glm/gtx/quaternion.hpp>

struct AnimVM {
  // MG4: the GLB (2048 textures) is uploaded on first use: only the rigs a player equips take video memory. Clip lengths
  // (reload timing) come from vm.json, so the weapon definition is complete without it.
  CharModel model; fs::path glb; std::string gunJointName; bool loaded = false, failed = false;
  std::map<std::string, float> dur;    // state -> clip length (s), from vm.json
  std::map<std::string, std::string> clipOf;   // state -> clip name in the GLB
  int gunJoint = -1;
  vec3 pt[3]{};                        // rest rig space: rear sight (scopes: the eye), front sight, muzzle
  vec3 up{0}; bool hasUp = false;      // MG11: the gun's up (rest rig space); ADS then rolls it upright (an idle clip that cants the gun)
  std::map<std::string, int> clip;     // state name -> clip index
  float idleHold = 0;                  // the idle is this one frame of the idle clip (s), held still (MG3: every rig; < 0 plays the clip)
  vec3 hipShift{0};                    // MG3: rig offset at the hip, camera space (m), faded out in ADS (the M249 closer)
  bool shells = false;                 // reload_start / reload_insert (once per round) / reload_end
  float fireRate = 1, reloadRate = 1;  // MG4: clips played faster (the shotguns' pump and shell loop); the timing follows
  vec3 dot{0}; bool hasDot = false;    // red-dot sight: the dot at this rig point (the front lens centre), seen along the sight line
  std::string slideName; int slideJoint = -1;   // PS1 "slideJoint": the rig joint the slide rides on (a pistol red dot moves with it)
  std::vector<std::string> opticMeshes;  // AT8 "opticMeshes": the factory optic (the bake's opticSplit / a kept attach): not drawn while another sight is fitted
  std::vector<std::string> ironMeshes;   // AT7 "ironMeshes": the gun's own sights (the bake's ironsSplit): not drawn while an optic is fitted
  std::vector<std::string> hideMeshes;   // AT5 "hideMeshes": mesh nodes never drawn (the SMG-9's own sight under its holographic one)
  vec3 dotCol{1, 0.03f, 0.015f}; float dotRing = 0;   // AT2: the mark's colour; a ring around it (in dot radii, 0 none)
  mat4 w2r{1}; bool w2rOk = false;     // BR3: weapon JSON space -> rest rig space (fitted optics), from the shared sight points
  ~AnimVM() { model.destroy(); }
};

// weapon space (JSON: +z forward, +y up) -> rest rig space: the eye / rear sight, the sight line and the muzzle below it
// fix rotation, scale and offset (BR3 optics are placed in weapon space)
static const mat4& weaponToRig(AnimVM& a, const WeaponDef& d) {
  if (a.w2rOk) return a.w2r;
  a.w2rOk = true;
  vec3 A0 = d.hasScope ? d.scopeEye : d.rear, fw = d.hasScope ? vec3(0, 0, 1) : glm::normalize(d.front - d.rear);
  auto frame = [](vec3 f, vec3 eye, vec3 muz) { vec3 u = eye - muz; u = glm::normalize(u - f * glm::dot(u, f)); return glm::mat3(glm::cross(u, f), u, f); };
  glm::mat3 Bw = frame(fw, A0, d.muzzle), Br = frame(glm::normalize(a.pt[1] - a.pt[0]), a.pt[0], a.pt[2]);
  float s = glm::length(a.pt[2] - a.pt[0]) / std::max(1e-4f, glm::length(d.muzzle - A0));
  a.w2r = glm::translate(mat4(1), a.pt[0]) * mat4(Br * glm::transpose(Bw)) * glm::scale(mat4(1), vec3(s)) * glm::translate(mat4(1), -A0);
  return a.w2r;
}

namespace {
enum { K_IDLE, K_FIRE, K_RELOAD };
struct St {
  CharInstance inst; AnimPlayer pl; PoseResult pose;
  int lastShot = -1; float lastReload = 0, kick = 0, lastThrow = -1; bool ready = false;   // lastThrow: the melee swing (MG7 knife)
  std::string clipName; mat4 local{1};   // rig -> camera (lowering, sway / bob, ADS); the camera is applied when drawn
  NodeLocals idleL; bool haveRest = false;   // WP4c: the last idle pose: a shot's fire clip moves only the parts under the gun joint
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
  a->glb = dir / "vm.glb"; a->gunJointName = meta.value("gunJoint", "");
  if (!fs::exists(a->glb)) { err = "missing " + a->glb.string(); return nullptr; }
  json P = meta["points"];
  if (block.contains("points")) for (auto& [k, v] : jkey(block, "points").items()) P[k] = v;   // rig-space points measured on the rig itself
  a->pt[0] = jv(P.contains("eye") ? P["eye"] : P["rear"]); a->pt[1] = jv(P["front"]); a->pt[2] = jv(P["muzzle"]);
  if (P.contains("up")) { a->up = glm::normalize(jv(P["up"])); a->hasUp = true; }
  // state -> clip name (defaults: the same name). No draw clip (equip goes straight to the idle) and no movement clips.
  json map = block.value("clips", json::object()), mc = meta.value("clips", json::object());
  for (const char* k : {"idle", "idle_empty", "fire", "reload", "reload_empty", "reload_start", "reload_insert", "reload_end"}) {
    std::string n = map.value(k, std::string(k));
    if (mc.contains(n)) { a->dur[k] = mc[n].get<float>(); a->clipOf[k] = n; }
  }
  if (!a->dur.count("idle")) { err = "no idle clip"; return nullptr; }
  a->idleHold = block.value("idleHold", 0.0f);
  if (block.contains("hipShift")) a->hipShift = jv(block["hipShift"]);
  a->shells = a->dur.count("reload_insert") > 0;
  a->fireRate = std::max(0.1f, block.value("fireRate", 1.0f)); a->reloadRate = std::max(0.1f, block.value("reloadRate", 1.0f));
  for (auto& [k, len] : a->dur) len /= k == "fire" ? a->fireRate : k.rfind("reload", 0) == 0 ? a->reloadRate : 1.0f;   // the weapon's pump / reload times
  for (auto& h : block.value("hideMeshes", json::array())) a->hideMeshes.push_back(h.get<std::string>());   // AT5
  for (auto& h : block.value("ironMeshes", json::array())) a->ironMeshes.push_back(h.get<std::string>());   // AT7
  for (auto& h : block.value("opticMeshes", json::array())) a->opticMeshes.push_back(h.get<std::string>());   // AT8
  a->slideName = block.value("slideJoint", std::string());   // PS1
  if (block.contains("redDot")) {   // AT2: the mark itself is drawn on the HUD at its angular size (sight_overlay.cpp drawReflexReticle)
    a->hasDot = true; a->dot = jv(block["redDot"]);
    a->dotCol = block.contains("dotColor") ? jv(block["dotColor"]) : vec3(1, 0.03f, 0.015f);   // MG7: the AK-12's mark is green
    a->dotRing = block.value("dotRing", 0.0f) / std::max(1e-5f, block.value("redDotRadius", 0.0006f));   // MG7: its ring, in dot radii
  }
  return a;
}

float animVmClipDur(const AnimVM& a, const std::string& clip) { auto it = a.dur.find(clip); return it == a.dur.end() ? -1.0f : it->second; }

// the GLB on first use; false (logged once) when it cannot be used: the static weapon is drawn instead
static bool ensureLoaded(AnimVM& a, const WeaponDef& d) {
  if (a.loaded || a.failed) return a.loaded;
  std::string err;
  a.model.wantNormals = true;   // WP2: a rig exported with a normal map (the Scorpion) draws it
  if (!a.model.load(a.glb, err, 0, false)) { a.failed = true; logf("weapon %s: animated viewmodel failed to load (%s): static model", d.id.c_str(), err.c_str()); return false; }
  for (int j = 0; j < a.model.joints(); ++j) if (a.model.nodeName[a.model.jointNode[j]] == a.gunJointName) a.gunJoint = j;
  for (int j = 0; j < a.model.joints(); ++j) if (!a.slideName.empty() && a.model.nodeName[a.model.jointNode[j]] == a.slideName) a.slideJoint = j;   // PS1
  for (auto& [k, n] : a.clipOf) { int c = findClip(a.model, n); if (c >= 0) a.clip[k] = c; }
  if (a.gunJoint < 0 || !a.clip.count("idle")) { a.failed = true; logf("weapon %s: animated viewmodel unusable (gun joint / idle clip): static model", d.id.c_str()); return false; }
  return a.loaded = true;
}

std::string animVmClipNow(const WeaponDef& d, float* time) {
  auto it = gSt.find(&d); if (it == gSt.end()) return "";
  if (time) *time = it->second.pl.time;
  return it->second.clipName;
}

void animVmStep(App& app, const WeaponDef& d, WeaponState& s, float dt) {
  AnimVM& a = *d.anim;
  if (!ensureLoaded(a, d)) { animVmNotActive(); return; }
  St& st = gSt[&d];
  if (!st.inst.model && !st.inst.create(a.model)) return;
  auto clip = [&](const std::string& n) { auto it = a.clip.find(n); return it == a.clip.end() ? -1 : it->second; };
  auto idle = [&](float fade) {   // idle_empty: the launcher without its rocket
    const char* n = s.loaded == 0 && clip("idle_empty") >= 0 ? "idle_empty" : "idle";
    st.pl.fadeLen = fade; st.pl.play(K_IDLE, clip(n), -1, 0, a.idleHold >= 0 ? 0.0f : 1.0f, true, true); st.clipName = n;
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
  bool swing = d.melee.on && s.throwT >= 0 && st.lastThrow < 0;   // MG7: a knife swing plays the fire clip (the slash)
  st.lastThrow = s.throwT;
  if (s.shotIdx != st.lastShot || swing) {   // automatics restart the fire clip at the cyclic rate
    st.lastShot = s.shotIdx;
    if (clip("fire") >= 0) play(K_FIRE, "fire", 0.04f, a.fireRate);   // the bolt / slide / hands; the gun's own swing in it is cancelled below (WP4c)
  } else if (!want.empty() && (want != st.clipName || st.pl.key != K_RELOAD || s.reload > st.lastReload + 1e-4f)) {
    float dur = animVmClipDur(a, want) * a.reloadRate, rate = a.shells ? a.reloadRate : 1.0f;   // dur: the clip's own length
    if (!a.shells && s.reloadKind != 4 && dur > 0 && s.reloadTotal > 0) rate = dur / s.reloadTotal;   // a magazine clip spans the reload exactly
    play(K_RELOAD, want, want == "reload_insert" && st.clipName == "reload_insert" ? 0.05f : 0.1f, rate);
  } else if (st.pl.key != K_IDLE && (st.pl.key != K_RELOAD || s.reload <= 0)) {
    const AnimClip& c = a.model.clips[st.pl.a];
    if (st.pl.time >= c.dur - 1e-3f || (st.pl.key == K_RELOAD && s.reload <= 0)) idle(0.1f);
  }
  st.lastReload = s.reload;
  st.pl.update(a.model, dt, st.pose);
  // WP4c: a shot's fire clip works only the parts under the gun joint (slide / bolt / mag); the gun, arms and hands keep
  // the idle pose, so the clip neither rocks the gun nor swings the arms (the straight push comes from the recoil springs)
  if (st.pl.key == K_IDLE) { st.idleL = st.pl.last; st.haveRest = true; }
  else if (st.pl.key == K_FIRE && st.haveRest && !d.melee.on && st.idleL.R.size() == st.pl.last.R.size()) {
    const int gn = a.model.jointNode[a.gunJoint];
    NodeLocals L = st.pl.last;
    for (size_t n = 0; n < L.R.size(); ++n) {
      int p = a.model.nodeParent[n]; while (p >= 0 && p != gn) p = a.model.nodeParent[p];
      if (p != gn) { L.T[n] = st.idleL.T[n]; L.R[n] = st.idleL.R[n]; L.S[n] = st.idleL.S[n]; L.set[n] = st.idleL.set[n]; }
    }
    st.pl.last = L; poseFromLocals(a.model, L, st.pose);
  }
  st.inst.skin(st.pose.skin);
  st.kick = std::max(0.0f, st.kick - dt / 0.12f);
  // ---- rig -> camera: lowering (mantle / swim) * sway & bob * kick * ADS (posed sights onto the view axis)
  const mat4 D = st.pose.delta[a.gunJoint];
  vec3 R = vec3(D * vec4(a.pt[0], 1)), F = vec3(D * vec4(a.pt[1], 1));
  bool scoped = app.vmScoped(d);
  float relief = scoped ? d.scopeRelief : d.eyeRelief;
  { vec3 oR, oF; float oRel;   // BR3: a fitted optic's eye line replaces the gun's own sights
    if (!scoped && brSightLine(d, s, oR, oF, oRel)) { mat4 G = D * weaponToRig(a, d); R = vec3(G * vec4(oR, 1)); F = vec3(G * vec4(oF, 1)); relief = oRel; } }
  float t = d.adsZoom > 0 ? 0.0f : app.adsT * app.adsT * (3 - 2 * app.adsT);   // MG13d: zoom-only aiming (the minigun): the rig keeps its hip pose
  quat q = glm::rotation(glm::normalize(F - R), vec3(0, 0, -1));
  if (a.hasUp) {   // MG11: sight line ahead and the gun's up straight up (no cant), not just the shortest turn onto the line
    vec3 f = glm::normalize(F - R), u = glm::mat3(D) * a.up; u = u - f * glm::dot(u, f);
    if (glm::length(u) > 1e-4f) { u = glm::normalize(u); q = glm::inverse(glm::quat_cast(glm::mat3(glm::cross(f, u), u, -f))); }
  }
  mat4 ads = glm::translate(mat4(1), glm::mix(R, vec3(0, 0, -relief), t)) * glm::mat4_cast(glm::slerp(quat(1, 0, 0, 0), q, t)) * glm::translate(mat4(1), -R);
  mat4 proc(1);
  if (app.vm.procedural) {
    vec3 piv(0, 0, -0.3f);
    proc = glm::translate(mat4(1), app.vm.swayP + app.vm.bob) * glm::translate(mat4(1), piv) *
           glm::mat4_cast(glm::angleAxis(radians(app.vm.swayR.x), vec3(0, 1, 0)) * glm::angleAxis(radians(app.vm.swayR.y), vec3(1, 0, 0))) * glm::translate(mat4(1), -piv);
  }
  if (s.kick > 0 || s.kickP != 0) {   // WP4c: the weapon state's recoil springs (weapons.cpp): straight back along the view / sight line, a tiny rise about the rear sight
    float rm = app.reducedMotion ? 0.3f : 1.0f, back = std::min(s.kick * glm::mix(1.0f, 0.6f, t), glm::mix(1.0f, 0.45f * relief, t)) * rm;
    vec3 Rv = glm::mix(R, vec3(0, 0, -relief), t);
    proc = proc * glm::translate(mat4(1), vec3(0, 0, back)) * glm::translate(mat4(1), Rv) * glm::mat4_cast(glm::angleAxis(radians(s.kickP * rm), vec3(1, 0, 0))) * glm::translate(mat4(1), -Rv);
  }
  mat4 low(1);
  if (float lw = std::max(app.mantleLower(), app.swimLower()); lw > 0) {
    float k = lw * lw * (3 - 2 * lw);
    low = glm::translate(mat4(1), vec3(0.05f * k, -0.34f * k, 0.08f * k)) * glm::mat4_cast(glm::angleAxis(radians(-60.0f * k), vec3(1, 0, 0)));
  }
  st.local = low * proc * glm::translate(mat4(1), a.hipShift * (1 - t)) * ads;
  st.ready = true;
}

void animVmNotActive() { gActive = nullptr; }
void animVmReset() { gActive = nullptr; gSt.clear(); }   // ED2: the weapon definitions are about to be rebuilt

bool animVmDraw(App& app, const WeaponDef& d) {
  auto it = gSt.find(&d);
  if (it == gSt.end() || !it->second.ready || gActive != &d) return false;
  St& st = it->second;
  mat4 rig = camNow(app) * st.local;
  const bool optic = fittedSight(d, app.wstate[d.id]) != 0;
  for (size_t k = 0; k < st.inst.view.parts.size(); ++k) {
    if (std::find(d.anim->hideMeshes.begin(), d.anim->hideMeshes.end(), st.inst.view.parts[k].name) != d.anim->hideMeshes.end()) continue;   // AT5
    if (optic && std::find(d.anim->ironMeshes.begin(), d.anim->ironMeshes.end(), st.inst.view.parts[k].name) != d.anim->ironMeshes.end()) continue;   // AT7: folded under the optic
    if (app.wstate[d.id].att[0] && std::find(d.anim->opticMeshes.begin(), d.anim->opticMeshes.end(), st.inst.view.parts[k].name) != d.anim->opticMeshes.end()) continue;   // AT8: the factory optic off
    app.R.viewModelTex.push_back({&st.inst.view, (int)k, rig});
  }
  const WeaponState& ws = app.wstate[d.id];
  if (const int fs = fittedSight(d, ws)) {   // PS1: a pistol red dot rides the slide (its joint), every other optic the gun
    const int j = fs == 7 && d.anim->slideJoint >= 0 ? d.anim->slideJoint : d.anim->gunJoint;
    brSightDraws(app, d, ws, rig * st.pose.delta[j] * weaponToRig(*d.anim, d), app.R.viewModelTex, app.R.viewModel);
  }   // BR3
  lootAttachDraws(app, d, ws, rig * st.pose.delta[d.anim->gunJoint] * weaponToRig(*d.anim, d), app.R.viewModelTex, app.R.viewModel);   // BR10: muzzle, magazine, recoil pad
  return true;
}

bool animVmWeaponMatrix(const App& app, const WeaponDef& d, mat4& W) {   // AT2: weapon JSON space -> world for the posed rig in hand
  auto it = gSt.find(&d);
  if (!d.anim || it == gSt.end() || !it->second.ready) return false;
  const St& st = it->second;
  W = camNow(app) * st.local * st.pose.delta[d.anim->gunJoint] * weaponToRig(*d.anim, d);
  return true;
}

bool animVmDot(const WeaponDef& d, vec3& col, float& ring) { if (!d.anim || !d.anim->hasDot) return false; col = d.anim->dotCol; ring = d.anim->dotRing; return true; }

bool animVmPoint(const App& app, const WeaponDef& d, int which, vec3& world) {
  auto it = gSt.find(&d);
  if (!d.anim || it == gSt.end() || !it->second.ready || which < 0 || which > 3) return false;
  const St& st = it->second;
  if (which == 3) { world = vec3(camNow(app) * st.local * st.pose.delta[d.anim->gunJoint] * weaponToRig(*d.anim, d) * vec4(d.gripR, 1)); return true; }   // WP8: the firing-hand grip socket
  world = vec3(camNow(app) * st.local * st.pose.delta[d.anim->gunJoint] * vec4(d.anim->pt[which], 1));
  return true;
}
