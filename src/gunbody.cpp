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

// DF1: Delta Force (AiType::Delta, combat.json kits.delta): the user's Mixamo rifle / pistol / grenade packs in one body
// (content/characters/delta_force.glb, tools/bake_gunbody.py --named; local-only) and the game's own guns. The clips pose the
// body (hips, legs, spine, head); pose.cpp then puts the arms, hands and fingers on the held gun's own measured grip and
// handguard (WeaponDef gripR / gripL / grip and guard capsules), so every gun is held by its own geometry, and runs the
// reload hand (to the magazine and a pouch), the recoil and the grenade arm as for any soldier. The clips by state:
//   dead: one of the six deaths (crouched: the crouching headshot);
//   moving: 8 directions against the body's facing (engaged, the body faces the aim), walk -> run -> sprint by speed at a
//     shared phase; crouched: walk crouching in 8 directions; a pistol forward / backward: the pistol pack's walk / run;
//   still: idle aiming (engaged) / idle; crouched: idle crouching aiming / idle crouching; a pistol: pistol idle / kneeling.
// The chest pitches with the aim (Spine1, 45 % of it), the arms and the weapon take the rest.
namespace {
struct DRig { int walk[8], run[8], sprint[8], cwalk[8]; int idle = -1, aim = -1, cidle = -1, caim = -1, pIdle = -1, pKneel = -1, pWalk = -1, pRun = -1, pWalkB = -1, pRunB = -1; int death[6]; int cdeath = -1; };
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
  return gDRigs[&m] = r;
}
}   // namespace

bool deltaBodyPose(Game& G, Soldier& s, float dt, PoseParams& pp) {
  enum { K_IDLE, K_AIM, K_LOCO = 3, K_DEATH = 6 };
  const CharModel& m = *G.models[s.model];
  if (m.clips.empty()) return false;
  const DRig& r = dRigOf(m);
  if (r.walk[0] < 0 || r.idle < 0) return false;
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
  if (!s.alive) {
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
  } else if (pistol && r.pIdle >= 0) A.play(K_IDLE + 20 + crouch, crouch && r.pKneel >= 0 ? r.pKneel : r.pIdle, -1, 0, 1, true);
  else if (hot) A.play(K_AIM + 10 * crouch, crouch ? r.caim : r.aim, -1, 0, 1, true);
  else A.play(K_IDLE + 10 * crouch, crouch ? r.cidle : r.idle, -1, 0, 1, true);
  if (A.key != key0) A.fadeLen = A.key == K_DEATH ? 0.15f : 0.22f;
  static thread_local PoseResult base;
  A.update(m, dt, base);
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
  pp.crouch = pp.kneel = pp.prone = pp.hunch = pp.lean = pp.swim = 0; pp.gait = false; pp.climb = -1; pp.aimYawOffset = 0;
  pp.carry = 0; pp.moveAim = 0;
  poseCharacter(m, pp, s.pose);
  return true;
}
