// MG1 / MG2: animated first-person viewmodels (see vm_anim.h).
#include "vm_anim.h"
#include "app.h"
#include "character.h"
#include "anim_clip.h"
#include "assets.h"
#include "br_models.h"
#include <glm/gtx/quaternion.hpp>

struct AnimVM {
  // MG4: the GLB (2048 textures) is uploaded on first use: only the rigs a player equips take video memory. Clip lengths
  // (reload timing) come from vm.json, so the weapon definition is complete without it.
  CharModel model; fs::path glb; std::string gunJointName; bool loaded = false, failed = false;
  std::map<std::string, float> dur;    // state -> clip length (s), from vm.json
  std::map<std::string, std::string> clipOf;   // state -> clip name in the GLB
  int gunJoint = -1;
  vec3 pt[3]{};                        // rest rig space: rear sight (scopes: the eye), front sight, muzzle
  std::map<std::string, int> clip;     // state name -> clip index
  float idleHold = 0;                  // the idle is this one frame of the idle clip (s), held still (MG3: every rig; < 0 plays the clip)
  vec3 hipShift{0};                    // MG3: rig offset at the hip, camera space (m), faded out in ADS (the M249 closer)
  bool shells = false;                 // reload_start / reload_insert (once per round) / reload_end
  float fireRate = 1, reloadRate = 1;  // MG4: clips played faster (the shotguns' pump and shell loop); the timing follows
  vec3 dot{0}; bool hasDot = false;    // red-dot sight: the dot at this rig point (the front lens centre), seen along the sight line
  GpuMesh dotMesh;
  mat4 w2r{1}; bool w2rOk = false;     // BR3: weapon JSON space -> rest rig space (fitted optics), from the shared sight points
  ~AnimVM() { model.destroy(); dotMesh.destroy(); }
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
  if (block.contains("points")) for (auto& [k, v] : block["points"].items()) P[k] = v;   // rig-space points measured on the rig itself
  a->pt[0] = jv(P.contains("eye") ? P["eye"] : P["rear"]); a->pt[1] = jv(P["front"]); a->pt[2] = jv(P["muzzle"]);
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
  if (block.contains("redDot")) {   // a small emissive disc facing back along the sight line; the tube hides it off the axis
    a->hasDot = true; a->dot = jv(block["redDot"]);
    float r = block.value("redDotRadius", 0.0006f);
    vec3 col = block.contains("dotColor") ? jv(block["dotColor"]) : vec3(1, 0.03f, 0.015f);   // MG7: the AK-12's mark is green
    Material m{"red_dot"}; m.albedo = col; m.emissive = col * 40.0f;
    vec3 ax = glm::normalize(a->pt[1] - a->pt[0]);
    Part p; p.shape = Part::CYL; p.size = vec3(2 * r, 0.0002f, 2 * r); p.collide = false; p.seg = 16;
    p.rot = glm::rotation(vec3(0, 1, 0), ax); p.center = a->dot;
    MeshData md; appendPart(md, p, m, mat4(1));
    if (float rr = block.value("dotRing", 0.0f); rr > 0) {   // MG7: a thin ring around the dot (24 short segments in the lens plane)
      vec3 u = glm::normalize(glm::cross(ax, std::fabs(ax.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0))), v = glm::cross(ax, u);
      for (int k = 0; k < 24; ++k) {
        float t = k * 6.2831853f / 24; Part q; q.collide = false;
        q.size = vec3(rr * 6.2831853f / 24 * 1.1f, 0.0002f, 0.35f * r);   // x: along the ring, z: the ring's width
        q.center = a->dot + (u * std::cos(t) + v * std::sin(t)) * rr;
        vec3 tg = -u * std::sin(t) + v * std::cos(t);
        q.rot = glm::rotation(vec3(0, 1, 0), ax); q.rot = glm::rotation(q.rot * vec3(1, 0, 0), tg) * q.rot;
        appendPart(md, q, m, mat4(1));
      }
    }
    a->dotMesh.upload(md);
  }
  return a;
}

float animVmClipDur(const AnimVM& a, const std::string& clip) { auto it = a.dur.find(clip); return it == a.dur.end() ? -1.0f : it->second; }

// the GLB on first use; false (logged once) when it cannot be used: the static weapon is drawn instead
static bool ensureLoaded(AnimVM& a, const WeaponDef& d) {
  if (a.loaded || a.failed) return a.loaded;
  std::string err;
  if (!a.model.load(a.glb, err, 0, false)) { a.failed = true; logf("weapon %s: animated viewmodel failed to load (%s): static model", d.id.c_str(), err.c_str()); return false; }
  for (int j = 0; j < a.model.joints(); ++j) if (a.model.nodeName[a.model.jointNode[j]] == a.gunJointName) a.gunJoint = j;
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
    if (clip("fire") >= 0) play(K_FIRE, "fire", 0.04f, a.fireRate); else st.kick = 1;   // no fire clip (the AR-15 set): a procedural kick
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
  st.inst.skin(st.pose.skin);
  st.kick = std::max(0.0f, st.kick - dt / 0.12f);
  // ---- rig -> camera: lowering (mantle / swim) * sway & bob * kick * ADS (posed sights onto the view axis)
  const mat4& D = st.pose.delta[a.gunJoint];
  vec3 R = vec3(D * vec4(a.pt[0], 1)), F = vec3(D * vec4(a.pt[1], 1));
  bool scoped = app.vmScoped(d);
  float relief = scoped ? d.scopeRelief : d.eyeRelief;
  { vec3 oR, oF; float oRel;   // BR3: a fitted optic's eye line replaces the gun's own sights
    if (!scoped && brSightLine(d, s, oR, oF, oRel)) { mat4 G = D * weaponToRig(a, d); R = vec3(G * vec4(oR, 1)); F = vec3(G * vec4(oF, 1)); relief = oRel; } }
  float t = app.adsT * app.adsT * (3 - 2 * app.adsT);
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
  st.local = low * proc * glm::translate(mat4(1), a.hipShift * (1 - t)) * ads;
  st.ready = true;
}

void animVmNotActive() { gActive = nullptr; }

bool animVmDraw(App& app, const WeaponDef& d) {
  auto it = gSt.find(&d);
  if (it == gSt.end() || !it->second.ready || gActive != &d) return false;
  St& st = it->second;
  mat4 rig = camNow(app) * st.local;
  for (size_t k = 0; k < st.inst.view.parts.size(); ++k) app.R.viewModelTex.push_back({&st.inst.view, (int)k, rig});
  const WeaponState& ws = app.wstate[d.id];
  if (d.anim->hasDot && app.adsT > 0.6f && !ws.att[0]) app.R.viewModel.push_back({&d.anim->dotMesh, rig * st.pose.delta[d.anim->gunJoint]});
  if (ws.att[0]) brSightDraws(app, d, ws, rig * st.pose.delta[d.anim->gunJoint] * weaponToRig(*d.anim, d), app.R.viewModelTex, app.R.viewModel);   // BR3
  return true;
}

bool animVmPoint(const App& app, const WeaponDef& d, int which, vec3& world) {
  auto it = gSt.find(&d);
  if (!d.anim || it == gSt.end() || !it->second.ready || which < 0 || which > 2) return false;
  const St& st = it->second;
  world = vec3(camNow(app) * st.local * st.pose.delta[d.anim->gunJoint] * vec4(d.anim->pt[which], 1));
  return true;
}
