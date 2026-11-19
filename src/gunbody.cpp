// GM1 / GM2: bodies with the gun built in: the gas-mask and desert soldiers (AiType::GasMask / Desert) and the Axis side's plain
// soldier bots (Game::begin: combat.json gunbody_models). A soldier brain (ai_layers.cpp) in a body that carries its own clips
// and its gun (content/characters/<kind>_<weapon>.glb, tools/bake_gunbody.py): no separate weapon model. The clips by state:
//   dead: Death; prone: Prone (getting down, its end held); reloading: Reload / CrouchReload (stretched to the reload time);
//   hit (health dropped): HitFlinch, its first 0.45 s;
//   moving (in place: the bake removed the root motion), by the velocity against the body's facing: Walk -> Run by speed,
//     WalkBack, StrafeLeft / StrafeRight; crouched: CrouchWalk; forward with a shot in the last 0.6 s: Walk -> RunShoot by speed;
//   a shot: ShootSingle / CrouchShootSingle from its flash frame, blended toward the aim-up / aim-down pose;
//   engaged and still: AimIdle / CrouchAimIdle blended toward AimUp / AimDown (CrouchAim*) by the aim pitch;
//   else: Idle / CrouchIdle.
// A clip missing from a model falls back: Idle <-> AimIdle, ShootSingle -> AimIdle, WalkBack / strafes / CrouchWalk -> Walk,
// no flinch without HitFlinch. The shots and flash come from the model's 'flash' joint: s.pose.weapon is set so that
// pose.weapon * the weapon's muzzle is that joint, with the bore along the joint's rest +z, so every pose.weapon user
// (soldierFire, net echoes) needs no change.
#include "game.h"
#include "character.h"
#include "anim_clip.h"
#include "ai_hooks.h"
#include "app.h"
#include "physics.h"
#include <map>

namespace {
struct Pose3 { int idle = -1, up = -1, down = -1; float pIdle = 0, pUp = 0, pDown = 0; };   // an aim loop and its up / down poses, their bore pitch (deg)
struct Rig {
  int flash = -1;                                  // skin joint
  int idle = -1, cidle = -1, walk = -1, run = -1, back = -1, left = -1, right = -1, cwalk = -1, runShoot = -1;
  int shot = -1, cshot = -1, reload = -1, creload = -1, death = -1, prone = -1, hit = -1;
  Pose3 aim, caim;
  float shotAt = 0.1f, cshotAt = 0.1f;             // the flash frame of the single shots
};
std::map<const CharModel*, Rig> gRigs;

float flashAt(const CharModel& m, int clip, int flash) {   // the first key at which the flash joint's scale goes above 0
  if (clip < 0 || flash < 0) return 0.1f;
  for (auto& ch : m.clips[clip].ch) if (ch.node == m.jointNode[flash] && ch.path == 2)
    for (size_t i = 0; i < ch.t.size(); ++i) if (ch.v[i * 3] > 0.01f) return ch.t[i];
  return 0.1f;
}
float borePitch(const CharModel& m, int clip, int flash) {   // degrees, + up: the flash joint's +z at the clip's first frame
  if (clip < 0 || flash < 0) return 0;
  NodeLocals L; restLocals(m, L); sampleClip(m, clip, 0, true, L);
  PoseResult pr; poseFromLocals(m, L, pr);
  vec3 z = glm::normalize(glm::mat3(pr.delta[flash]) * vec3(0, 0, 1));
  return glm::degrees(std::asin(std::clamp(z.y, -1.0f, 1.0f)));
}

const Rig& rigOf(const CharModel& m) {
  auto it = gRigs.find(&m); if (it != gRigs.end()) return it->second;
  Rig r;
  for (int k = 0; k < m.joints(); ++k) if (m.nodeName[m.jointNode[k]] == "flash") r.flash = k;
  auto c = [&](const char* n, int fb = -1) { int k = findClip(m, n); return k >= 0 ? k : fb; };
  r.walk = c("Walk"); r.run = c("Run"); r.runShoot = c("RunShoot");
  r.back = c("WalkBack", r.walk); r.left = c("StrafeLeft", r.walk); r.right = c("StrafeRight", r.walk); r.cwalk = c("CrouchWalk", r.walk);
  r.aim.idle = c("AimIdle", c("Idle")); r.aim.up = c("AimUp"); r.aim.down = c("AimDown");
  r.caim.idle = c("CrouchAimIdle", c("CrouchIdle", r.aim.idle)); r.caim.up = c("CrouchAimUp", r.aim.up); r.caim.down = c("CrouchAimDown", r.aim.down);
  r.idle = c("Idle", r.aim.idle); r.cidle = c("CrouchIdle", r.caim.idle);
  r.shot = c("ShootSingle"); r.cshot = c("CrouchShootSingle", r.shot);
  r.reload = c("Reload"); r.creload = c("CrouchReload", r.reload); r.death = c("Death"); r.prone = c("Prone"); r.hit = c("HitFlinch");
  r.shotAt = flashAt(m, r.shot, r.flash); r.cshotAt = flashAt(m, r.cshot, r.flash);
  for (Pose3* p : {&r.aim, &r.caim}) { p->pIdle = borePitch(m, p->idle, r.flash); p->pUp = borePitch(m, p->up, r.flash); p->pDown = borePitch(m, p->down, r.flash); }
  logf("gun body %s: aim pitch %.0f / %.0f / %.0f deg (down / level / up), crouched %.0f / %.0f / %.0f, shot flash at %.2f s",
       m.id.c_str(), r.aim.pDown, r.aim.pIdle, r.aim.pUp, r.caim.pDown, r.caim.pIdle, r.caim.pUp, r.shotAt);
  return gRigs[&m] = r;
}
// the pose clip and blend weight toward it for an aim pitch (deg): up or down from the level loop
void aimBlend(const Pose3& p, float pitch, int& b, float& w) {
  b = -1; w = 0;
  if (pitch >= p.pIdle && p.up >= 0 && p.pUp > p.pIdle + 2) { b = p.up; w = std::clamp((pitch - p.pIdle) / (p.pUp - p.pIdle), 0.0f, 1.0f); }
  else if (pitch < p.pIdle && p.down >= 0 && p.pDown < p.pIdle - 2) { b = p.down; w = std::clamp((p.pIdle - pitch) / (p.pIdle - p.pDown), 0.0f, 1.0f); }
  if (w < 0.01f) b = -1;
}
}   // namespace

void gunBodyPose(Game& G, Soldier& s, float dt) {
  enum { K_IDLE, K_AIM, K_FIRE, K_LOCO, K_RELOAD, K_DEATH, K_PRONE, K_HIT };
  const CharModel& m = *G.models[s.model];
  const Rig& r = rigOf(m);
  AnimPlayer& A = s.anim;
  if (A.life != s.lifeId) { A = AnimPlayer{}; A.life = s.lifeId; A.lastHp = s.hp; s.gmShotAt = s.lastShot; s.gmFireT = 1; }
  const int key0 = A.key;
  const bool crouch = s.posture == Physics::CROUCH, prone = s.posture == Physics::PRONE;
  if (s.lastShot != s.gmShotAt) { s.gmShotAt = s.lastShot; s.gmFireT = 0; } else s.gmFireT += dt;
  const bool hurt = s.alive && s.hp < A.lastHp - 0.5f; A.lastHp = s.hp;
  const bool hot = s.br.target >= 0 || G.t - s.lastShot < 1.5;
  const int shot = crouch ? r.cshot : r.shot, rel = crouch ? r.creload : r.reload;
  const float shotAt = crouch ? r.cshotAt : r.shotAt;
  // the body faces the aim when engaged (posesStep: the clips cannot twist), so the move is read against that facing
  const float bodyYaw = hot && !s.localPlayer ? s.anGunYaw : s.yaw;
  const vec3 fwd = App::forward(bodyYaw, 0), left(-std::cos(radians(bodyYaw)), 0, std::sin(radians(bodyYaw)));
  const float vf = glm::dot(s.vel, fwd), vl = glm::dot(s.vel, left);
  const float pitch = s.localPlayer ? G.app.eyePitch : s.anGunPitch;
  auto gs = [&](int k, float def) { return k >= 0 && m.clips[k].groundSpeed > 0.2f ? m.clips[k].groundSpeed : def; };
  const float vw = gs(r.walk, 1.4f), vr = std::max(vw + 0.5f, gs(r.run, 4.5f));
  const bool moving = s.speed > (A.key % 10 == K_LOCO ? 0.3f : 0.6f) && r.walk >= 0;   // hysteresis: no idle / walk flicker
  float hold = -1;   // >= 0: the time (s, or the phase of a blend) set rather than played
  int b = -1; float w = 0; aimBlend(crouch ? r.caim : r.aim, pitch, b, w);
  if (!s.alive && r.death >= 0) A.play(K_DEATH, r.death, -1, 0, 1, false);
  else if (prone && r.prone >= 0) A.play(K_PRONE, r.prone, -1, 0, 1, false);
  else if (s.reload > 0 && rel >= 0) A.play(K_RELOAD, rel, -1, 0, m.clips[rel].dur / std::max(0.3f, s.reloadTotal), false);
  else if (r.hit >= 0 && (hurt || (A.key == K_HIT && A.time < 0.45f))) A.play(K_HIT, r.hit, -1, 0, 1, false, hurt);
  else if (moving) {   // keys K_LOCO + 10 * direction: a change of direction cross-fades
    if (crouch && r.cwalk >= 0) A.play(K_LOCO + 10, r.cwalk, -1, 0, std::clamp(s.speed / 0.9f, 0.5f, 2.0f), true);
    else if (std::fabs(vl) > std::fabs(vf) && vl > 0) A.play(K_LOCO + 20, r.left, -1, 0, std::clamp(s.speed / 0.62f, 0.5f, 2.0f), true);
    else if (std::fabs(vl) > std::fabs(vf)) A.play(K_LOCO + 30, r.right, -1, 0, std::clamp(s.speed / 0.62f, 0.5f, 2.0f), true);
    else if (vf < 0) A.play(K_LOCO + 40, r.back, -1, 0, std::clamp(s.speed / 0.95f, 0.5f, 2.0f), true);
    else if (r.runShoot >= 0 && s.gmFireT < 0.6f) {   // firing on the move: the run-and-shoot clip, blended from the walk by speed
      const float vs = std::max(vw + 0.5f, gs(r.runShoot, 5.0f)), wr = std::clamp((s.speed - vw) / (vs - vw), 0.0f, 1.0f);
      A.play(K_LOCO + 50, r.walk, r.runShoot, wr, std::clamp(s.speed / glm::mix(vw, vs, wr), 0.5f, 2.0f), true);
    } else { float wr = r.run >= 0 ? std::clamp((s.speed - vw) / (vr - vw), 0.0f, 1.0f) : 0.0f; A.play(K_LOCO, r.walk, r.run, wr, std::clamp(s.speed / glm::mix(vw, vr, wr), 0.5f, 2.0f), true); }
  } else if (shot >= 0 && s.gmFireT < m.clips[shot].dur - shotAt) {   // the recoil of the last shot, from its flash
    A.play(K_FIRE, shot, b, w, 0, false);
    const float t = shotAt + s.gmFireT;
    hold = b >= 0 ? std::min(t / m.clips[shot].dur, 0.999f) : t;
  } else if (hot) { const Pose3& p = crouch ? r.caim : r.aim; A.play(K_AIM, p.idle, b, w, 1, true); }
  else A.play(K_IDLE, crouch ? r.cidle : r.idle, -1, 0, 1, true);
  if (hold >= 0) A.time = hold;
  if (A.key != key0) A.fadeLen = A.key == K_DEATH ? 0.2f : A.key == K_FIRE || key0 == K_FIRE ? 0.06f : A.key == K_HIT ? 0.08f : A.key == K_AIM ? 0.15f : 0.25f;
  s.poseFrom = -1;
  A.update(m, dt, s.pose); ++s.poseRev;
  if (r.flash >= 0 && s.weapon >= 0 && !s.pose.delta.empty()) {   // the muzzle frame at the flash joint (bore = its rest +z)
    const mat4 F = s.pose.delta[r.flash] * m.nodeWorld[m.jointNode[r.flash]];
    const glm::mat3 R = glm::mat3(s.pose.delta[r.flash]);
    vec3 z = glm::normalize(R * vec3(0, 0, 1)), y = R * vec3(0, 1, 0);
    vec3 x = glm::normalize(glm::cross(y, z)); y = glm::cross(z, x);
    mat4 W(1); W[0] = vec4(x, 0); W[1] = vec4(y, 0); W[2] = vec4(z, 0);
    W[3] = vec4(vec3(F[3]) - glm::mat3(W) * G.app.weaponDefs[s.weapon]->muzzle, 1);
    s.pose.weapon = W;
  }
}

// DF1: Delta Force (AiType::Delta, combat.json kits.delta): the user's Mixamo rifle / pistol / grenade clips retargeted onto
// the US Marine body (br_marine, CC-BY-4.0; content/characters/delta_force.glb, tools/bake_gunbody.py --retarget; local-only:
// the clips' licence is not stated) and the game's own guns. The clips pose the
// body (hips, legs, spine, head); pose.cpp then puts the arms, hands and fingers on the held gun's own measured grip and
// handguard (WeaponDef gripR / gripL / grip and guard capsules), so every gun is held by its own geometry, and runs the
// reload hand (to the magazine and a pouch), the recoil and the grenade arm as for any soldier. The clips by state:
//   dead: one of the six deaths (crouched: the crouching headshot);
//   moving: 8 directions against the body's facing (engaged, the body faces the aim), walk -> run -> sprint by speed at a
//     shared phase; crouched: walk crouching in 8 directions; a pistol forward / backward: the pistol pack's walk / run;
//   still: idle aiming (engaged) / idle; crouched: idle crouching aiming / idle crouching; a pistol: pistol idle / kneeling.
// The chest pitches with the aim (Spine1, 45 % of it), the arms and the weapon take the rest.
namespace {
struct DRig { int walk[8], run[8], sprint[8], cwalk[8]; int throwS[2] = {-1, -1}, throwC = -1, turnL = -1, turnR = -1, cturnL = -1, cturnR = -1; int idle = -1, aim = -1, cidle = -1, caim = -1, pIdle = -1, pKneel = -1, pWalk = -1, pRun = -1, pWalkB = -1, pRunB = -1; int death[6]; int cdeath = -1; };
std::map<const CharModel*, DRig> gDRigs;
const DRig& dRigOf(const CharModel& m) {
  auto it = gDRigs.find(&m); if (it != gDRigs.end()) return it->second;
  DRig r; static const char* dir[8] = {"forward", "forward left", "left", "backward left", "backward", "backward right", "right", "forward right"};
  r.idle = findClip(m, "idle"); r.aim = findClip(m, "idle aiming"); if (r.aim < 0) r.aim = r.idle; if (r.idle < 0) r.idle = r.aim;
  r.cidle = findClip(m, "idle crouching"); r.caim = findClip(m, "idle crouching aiming"); if (r.caim < 0) r.caim = r.cidle >= 0 ? r.cidle : r.aim; if (r.cidle < 0) r.cidle = r.caim;
  for (int k = 0; k < 8; ++k) {
    r.walk[k] = findClip(m, std::string("walk ") + dir[k]); r.run[k] = findClip(m, std::string("run ") + dir[k]);
    r.sprint[k] = findClip(m, std::string("sprint ") + dir[k]); r.cwalk[k] = findClip(m, std::string("walk crouching ") + dir[k]);
  }
  for (int k = 0; k < 8; ++k) { if (r.walk[k] < 0) r.walk[k] = r.walk[0]; if (r.run[k] < 0) r.run[k] = r.walk[k]; if (r.sprint[k] < 0) r.sprint[k] = r.run[k]; if (r.cwalk[k] < 0) r.cwalk[k] = r.walk[k]; }
  r.pIdle = findClip(m, "pistol idle"); r.pKneel = findClip(m, "pistol kneeling idle"); r.pWalk = findClip(m, "pistol walk"); r.pRun = findClip(m, "pistol run");
  r.pWalkB = findClip(m, "pistol walk backward"); r.pRunB = findClip(m, "pistol run backward");
  static const char* deaths[6] = {"death from the front", "death from the back", "death from right", "death from front headshot", "death from back headshot", "death from the front"};
  for (int k = 0; k < 6; ++k) r.death[k] = findClip(m, deaths[k]);
  r.cdeath = findClip(m, "death crouching headshot front");
  // the pack's "Throw Grenade" is a prone throw (hips 15 cm up): prone Delta soldiers take the procedural prone pose instead
  r.throwS[0] = r.throwS[1] = findClip(m, "Grenade Throwstand"); r.throwC = findClip(m, "Throw Grenadecrouch");
  if (r.throwC < 0) r.throwC = r.throwS[0];
  r.turnL = findClip(m, "turn 90 left"); r.turnR = findClip(m, "turn 90 right"); r.cturnL = findClip(m, "crouching turn 90 left"); r.cturnR = findClip(m, "crouching turn 90 right");
  if (r.cturnL < 0) r.cturnL = r.turnL; if (r.cturnR < 0) r.cturnR = r.turnR;
  return gDRigs[&m] = r;
}
}   // namespace

// the centre of a gun's parts of one role (weapons.cpp WPart: 1 bolt, 2 mag, 3 cover, 4 clip, 7 handle), weapon space, from the
// kept vertex positions; `def` when it has none
vec3 weaponPartCentre(const WeaponDef& d, int role, vec3 def) {
  static std::map<std::pair<const WeaponDef*, int>, std::pair<bool, vec3>> memo;
  auto key = std::make_pair(&d, role);
  if (auto it = memo.find(key); it != memo.end()) return it->second.first ? it->second.second : def;
  vec3 sum(0); int n = 0;
  if (d.model && !d.model->cpuPos.empty() && role >= 0 && role < 8)
    for (int k : d.modelParts[role]) { const ModelPart& p = d.model->parts[k]; for (uint32_t i = p.first; i < p.first + p.count && i < d.model->cpuIdx.size(); ++i) { sum += d.model->cpuPos[d.model->cpuIdx[i]]; ++n; } }
  memo[key] = {n > 0, n ? sum / (float)n : vec3(0)};
  return n ? sum / (float)n : def;
}

namespace {
// the hip's turn about the vertical in a pose (rad, + = turned toward +x from +z, as the body's yaw)
float hipsYaw(const CharModel& m, const PoseResult& P) {
  const int h = m.semantic[CJ_HIPS]; if (h < 0) return 0;
  vec3 f = glm::mat3(P.delta[h]) * vec3(0, 0, 1);
  return std::atan2(f.x, f.z);
}
}   // namespace

bool deltaBodyPose(Game& G, Soldier& s, float dt, PoseParams& pp) {
  enum { K_IDLE, K_AIM, K_LOCO = 3, K_DEATH = 6, K_THROW = 7, K_TURN = 8 };
  const CharModel& m = *G.models[s.model];
  if (m.clips.empty()) return false;
  const DRig& r = dRigOf(m);
  if (r.walk[0] < 0 || r.idle < 0) return false;
  if (s.alive && s.posture == Physics::PRONE) { s.anim.key = -1; return false; }   // no prone clips in the packs: the soldier's procedural prone
  // a long gun whose pistol grip sits at its butt end (sawn-off, the revolving grenade launcher): held as a long gun, not a pistol
  if (pp.holdKind < 0 && pp.gripR.z < 0.12f && pp.gripL.z - pp.gripR.z > 0.2f) pp.holdKind = 1;
  AnimPlayer& A = s.anim;
  if (A.life != s.lifeId) { A = AnimPlayer{}; A.life = s.lifeId; }
  const int key0 = A.key;
  const bool crouch = s.posture == Physics::CROUCH;
  const bool pistol = s.weapon >= 0 && (pp.holdKind >= 0 ? pp.holdKind == 0 : pp.gripR.z < 0.12f);
  const bool hot = s.br.target >= 0 || G.t - s.lastShot < 1.5 || s.reload > 0;
  const float bodyYaw = hot && !s.localPlayer ? s.anGunYaw : s.yaw;
  const vec3 fwd = App::forward(bodyYaw, 0), left(-std::cos(radians(bodyYaw)), 0, std::sin(radians(bodyYaw)));
  const float vf = glm::dot(s.vel, fwd), vl = glm::dot(s.vel, left);
  auto gs = [&](int k, float def) { return k >= 0 && m.clips[k].groundSpeed > 0.2f ? m.clips[k].groundSpeed : def; };
  const bool moving = s.speed > (A.key >= K_LOCO * 10 ? 0.3f : 0.6f);
  const float yawNow = bodyYaw; float dy = std::remainder(yawNow - s.dfPrevYaw, 360.0f); s.dfPrevYaw = yawNow;
  if (dt > 0) s.dfYawRate += (std::clamp(dy / dt, -720.0f, 720.0f) - s.dfYawRate) * (1.0f - std::exp(-dt / 0.15f));
  int turnClip = -1; float hold = -1;
  const int thr = crouch ? r.throwC : r.throwS[(s.slot + s.lifeId) % 2];
  if (s.alive && s.throwT >= 0 && thr >= 0) {   // the clip's throw, its fastest hand (the release) on the game's release at 55 %
    const AnimClip& c = m.clips[thr]; const float sw = c.swing > 0 ? c.swing : c.dur * 0.55f;
    const float a0 = std::max(0.0f, sw - 1.1f), a1 = std::min(c.dur, sw + 0.8f), u = std::clamp(s.throwT, 0.0f, 1.0f);
    hold = u < 0.55f ? glm::mix(a0, sw, u / 0.55f) : glm::mix(sw, a1, (u - 0.55f) / 0.45f);
    A.play(K_THROW, thr, -1, 0, 0, false);
    pp.rightArmClip = true;
  } else if (!s.alive) {
    int d = crouch && r.cdeath >= 0 ? r.cdeath : r.death[(s.slot + s.lifeId) % 6];
    if (d < 0) d = r.death[0];
    if (d >= 0) A.play(K_DEATH, d, -1, 0, 1, false);
  } else if (moving) {
    int sec = (int)std::lround(std::atan2(vl, vf) / 0.78539816f); sec = (sec + 8) % 8;   // 0 forward, 2 left, 4 back, 6 right
    const float v = s.speed;
    if (crouch) A.play(K_LOCO * 10 + 8 + sec, r.cwalk[sec], -1, 0, std::clamp(v / gs(r.cwalk[sec], 1.9f), 0.4f, 2.0f), true);
    else if (pistol && (sec == 0 || sec == 4) && r.pWalk >= 0) {
      int wa = sec == 0 ? r.pWalk : r.pWalkB, ru = sec == 0 ? r.pRun : r.pRunB; if (wa < 0) wa = r.pWalk; if (ru < 0) ru = wa;
      const float a = gs(wa, 1.9f), b = std::max(a + 0.5f, gs(ru, 4.5f)), w = std::clamp((v - a) / (b - a), 0.0f, 1.0f);
      A.play(K_LOCO * 10 + 20 + sec, wa, ru, w, std::clamp(v / glm::mix(a, b, w), 0.4f, 2.0f), true);
    } else {
      const float a = gs(r.walk[sec], 1.9f), b = std::max(a + 0.5f, gs(r.run[sec], 4.6f)), c = std::max(b + 0.5f, gs(r.sprint[sec], 7.0f));
      if (v <= b || r.sprint[sec] == r.run[sec]) { float w = std::clamp((v - a) / (b - a), 0.0f, 1.0f); A.play(K_LOCO * 10 + sec, r.walk[sec], r.run[sec], w, std::clamp(v / glm::mix(a, b, w), 0.4f, 2.0f), true); }
      else { float w = std::clamp((v - b) / (c - b), 0.0f, 1.0f); A.play(K_LOCO * 10 + 30 + sec, r.run[sec], r.sprint[sec], w, std::clamp(v / glm::mix(b, c, w), 0.4f, 2.0f), true); }
    }
  } else if (std::fabs(s.dfYawRate) > 50.0f && (s.dfYawRate > 0 ? (crouch ? r.cturnL : r.turnL) : (crouch ? r.cturnR : r.turnR)) >= 0) {   // turning in place: the turn clip's feet, its hip turn taken out (the body turns by its yaw)
    turnClip = s.dfYawRate > 0 ? (crouch ? r.cturnL : r.turnL) : (crouch ? r.cturnR : r.turnR);
    const float clipRate = 90.0f / std::max(0.3f, m.clips[turnClip].dur);
    A.play(K_TURN + 10 * crouch + (s.dfYawRate > 0 ? 0 : 1), turnClip, -1, 0, std::clamp(std::fabs(s.dfYawRate) / clipRate, 0.6f, 2.0f), true);
  } else if (pistol && r.pIdle >= 0) A.play(K_IDLE + 20 + crouch, crouch && r.pKneel >= 0 ? r.pKneel : r.pIdle, -1, 0, 1, true);
  else if (hot) A.play(K_AIM + 10 * crouch, crouch ? r.caim : r.aim, -1, 0, 1, true);
  else A.play(K_IDLE + 10 * crouch, crouch ? r.cidle : r.idle, -1, 0, 1, true);
  if (A.key != key0) A.fadeLen = A.key == K_DEATH ? 0.15f : 0.22f;
  if (hold >= 0) A.time = hold;
  static thread_local PoseResult base;
  A.update(m, dt, base);
  if (turnClip >= 0 && A.a == turnClip && m.semantic[CJ_HIPS] >= 0) {   // take the clip's hip turn (since its first frame) out about the hips' vertical
    static std::map<std::pair<const CharModel*, int>, float> yaw0;
    auto k0 = std::make_pair(&m, turnClip);
    if (!yaw0.count(k0)) { NodeLocals L; restLocals(m, L); sampleClip(m, turnClip, 0, true, L); PoseResult P0; poseFromLocals(m, L, P0); yaw0[k0] = hipsYaw(m, P0); }
    const float yw = hipsYaw(m, base) - yaw0[k0]; const vec3 piv = vec3(base.delta[m.semantic[CJ_HIPS]] * vec4(m.jointHead[m.semantic[CJ_HIPS]], 1));
    const mat4 R = glm::translate(mat4(1), vec3(piv.x, 0, piv.z)) * glm::mat4_cast(glm::angleAxis(-yw, vec3(0, 1, 0))) * glm::translate(mat4(1), -vec3(piv.x, 0, piv.z));
    for (auto& d : base.delta) d = R * d;
  }
  s.poseFrom = -1; ++s.poseRev;
  if (!s.alive) { s.pose = base; return true; }
  const int sp = m.semantic[CJ_SPINE1] >= 0 ? m.semantic[CJ_SPINE1] : m.semantic[CJ_SPINE];
  if (sp >= 0) {   // the chest leans into the aim (the weapon and the arms take the rest)
    const vec3 piv = vec3(base.delta[sp] * vec4(m.jointHead[sp], 1));
    const mat4 R = glm::translate(mat4(1), piv) * glm::mat4_cast(glm::angleAxis(-0.45f * pp.aimPitch, vec3(1, 0, 0))) * glm::translate(mat4(1), -piv);
    std::vector<char> sub(m.joints(), 0); sub[sp] = 1;
    for (int k = 0; k < m.joints(); ++k) { int a = m.jointParent[k]; while (a >= 0 && !sub[k]) { if (a == sp) sub[k] = 1; a = m.jointParent[a]; } }
    for (int k = 0; k < m.joints(); ++k) if (sub[k]) base.delta[k] = R * base.delta[k];
  }
  pp.base = &base.delta;
  s.dfMagWin = vec2(0); s.dfBoltBack = s.dfPumpBack = 0;
  if (s.weapon >= 0) {   // the gun's family: its reload, the action it cycles, its kick
    const WeaponDef& d = *G.app.weaponDefs[s.weapon];
    const bool launcher = d.action == "launcher", bolt = d.action == "bolt";
    const bool pump = !launcher && d.feed == "single" && !d.modelParts[7].empty();
    const vec3 top = vec3(0, d.gripR.y + 0.10f, d.gripR.z + 0.06f);
    pp.reloadStyle = d.feed == "belt" ? 2 : d.feed == "magazine" ? 0 : 1;
    pp.portPos = launcher ? d.muzzle : pump ? vec3(0, d.gripR.y + 0.03f, d.gripR.z + 0.12f)
               : d.feed == "belt" ? weaponPartCentre(d, 3, top) : pistol ? vec3(0, d.gripR.y + 0.05f, d.gripR.z + 0.03f)
               : weaponPartCentre(d, 1, top) + vec3(0, 0.03f, 0.02f);
    const int need = std::max(1, d.cap - s.ws.loaded);
    pp.reloadRounds = launcher || d.feed == "stripper" || d.feed == "enbloc" ? 1 : std::min(4, need);
    pp.boltPos = bolt ? weaponPartCentre(d, 1, vec3(-0.03f, d.gripR.y + 0.07f, d.gripR.z + 0.02f)) : vec3(0);
    pp.boltReload = bolt;
    if (bolt && d.boltTime > 0 && s.ws.bolt > 0) { pp.cycleKind = 1; pp.cycle = 1.0f - s.ws.bolt / d.boltTime; }
    else if (pump && G.t - s.lastShot < 0.55) { pp.cycleKind = 2; pp.cycle = (float)(G.t - s.lastShot) / 0.55f; }
    pp.recoil *= std::clamp(d.vmBack / 0.035f, 0.6f, 2.5f);
    if (pp.reload > 0 && pp.reload < 1) pp.lowReady = std::max(pp.lowReady, std::min(1.0f, std::min(pp.reload, 1 - pp.reload) / 0.1f));   // off the cheek to reload: the relaxed low ready (muzzle ~35 deg down), back up at the end
    // what the draw moves (game.cpp): the magazine's span out of the gun; the bolt's / pump's travel
    if (s.reload > 0) s.dfMagWin = pp.reloadStyle == 0 ? vec2(0.25f, 0.8f) : pp.reloadStyle == 2 ? vec2(0.35f, 0.65f) : vec2(0);
    auto open = [](float c) { return c < 0.3f ? 0.0f : c < 0.55f ? (c - 0.3f) / 0.25f : c < 0.75f ? 1.0f - (c - 0.55f) / 0.2f : 0.0f; };
    if (pp.cycleKind == 1) s.dfBoltBack = 0.08f * open(pp.cycle);
    else if (bolt && pp.reload > 0 && pp.reload < 1) s.dfBoltBack = 0.08f * std::clamp(std::min(pp.reload / 0.12f, (1 - pp.reload) / 0.12f), 0.0f, 1.0f);
    if (pp.cycleKind == 2) s.dfPumpBack = 0.09f * std::sin(3.14159265f * pp.cycle);
  }
  pp.crouch = pp.kneel = pp.prone = pp.hunch = pp.lean = pp.swim = 0; pp.gait = false; pp.climb = -1; pp.aimYawOffset = 0;
  pp.carry = 0; pp.moveAim = 0;
  poseCharacter(m, pp, s.pose);
  return true;
}
