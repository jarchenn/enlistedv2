// GM1 / GM2: bodies with the gun built in: the desert soldiers (AiType::Desert; the gas-mask ones removed, RM2) and the Axis side's plain
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
#include <mutex>
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

// the muzzle frame at a body's flash joint (bore = its rest +z): pose.weapon x the weapon's muzzle lands on it
void flashMuzzle(Game& G, Soldier& s, const CharModel& m, int flash) {
  if (flash < 0 || s.weapon < 0 || s.pose.delta.empty()) return;
  const mat4 F = s.pose.delta[flash] * m.nodeWorld[m.jointNode[flash]];
  const glm::mat3 R = glm::mat3(s.pose.delta[flash]);
  vec3 z = glm::normalize(R * vec3(0, 0, 1)), y = R * vec3(0, 1, 0);
  vec3 x = glm::normalize(glm::cross(y, z)); y = glm::cross(z, x);
  mat4 W(1); W[0] = vec4(x, 0); W[1] = vec4(y, 0); W[2] = vec4(z, 0);
  W[3] = vec4(vec3(F[3]) - glm::mat3(W) * G.app.weaponDefs[s.weapon]->muzzle, 1);
  s.pose.weapon = W;
}

std::recursive_mutex gPoseCacheMx;   // CP: the lazily filled rig / socket caches below, read from the pose workers (game.cpp posesStep)
const Rig& rigOf(const CharModel& m) {
  std::lock_guard<std::recursive_mutex> lk(gPoseCacheMx);
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
  auto gs = [&](int k, float def) { return (k >= 0 && m.clips[k].groundSpeed > 0.2f ? m.clips[k].groundSpeed : def) * s.eScale; };   // RB6: a scaled kit's longer stride
  const float vw = gs(r.walk, 1.4f), vr = std::max(vw + 0.5f, gs(r.run, 4.5f));
  const bool moving = s.speed > (A.key % 10 == K_LOCO ? 0.3f : 0.6f) && r.walk >= 0;   // hysteresis: no idle / walk flicker
  float hold = -1;   // >= 0: the time (s, or the phase of a blend) set rather than played
  int b = -1; float w = 0; aimBlend(crouch ? r.caim : r.aim, pitch, b, w);
  if (!s.alive && r.death >= 0) A.play(K_DEATH, r.death, -1, 0, 0.85f + 0.3f * (float)(((uint32_t)s.slot * 2654435761u ^ (uint32_t)s.lifeId * 40503u) % 1000u) / 1000.0f, false);   // one death clip in these packs: its pace varies
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
  flashMuzzle(G, s, m, r.flash);
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
struct DRig { int walk[8], run[8], sprint[8], cwalk[8]; int flash = -1, shot = -1, cshot = -1, reload = -1, creload = -1; float shotAt = 0.1f, cshotAt = 0.1f; int throwS[2] = {-1, -1}, throwC = -1, turnL = -1, turnR = -1, cturnL = -1, cturnR = -1; int idle = -1, aim = -1, cidle = -1, caim = -1, pIdle = -1, pKneel = -1, pWalk = -1, pRun = -1, pWalkB = -1, pRunB = -1; int death[6]; int cdeath = -1;
  int dFront = -1, dBack = -1, dRight = -1, dHeadF = -1, dHeadB = -1, dCrouchHead = -1, dCrouch = -1, dFlyBack = -1, dFallFwd = -1, dReactFwd = -1, dFallBack = -1, hit = -1, walkHit = -1; };
std::map<const CharModel*, DRig> gDRigs;
const DRig& dRigOf(const CharModel& m) {
  std::lock_guard<std::recursive_mutex> lk(gPoseCacheMx);
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
  auto ci = [&](const char* n) {   // the packs spell the same clip "Crouch Death" / "crouch death": match without case
    for (size_t k = 0; k < m.clips.size(); ++k) { const std::string& a = m.clips[k].name; if (a.size() == std::strlen(n) && std::equal(a.begin(), a.end(), n, [](char x, char y) { return std::tolower((unsigned char)x) == std::tolower((unsigned char)y); })) return (int)k; }
    return -1; };
  r.dFront = ci("death from the front"); r.dBack = ci("death from the back"); r.dRight = ci("death from right"); r.dHeadF = ci("death from front headshot");
  r.dHeadB = ci("death from back headshot"); r.dCrouchHead = ci("death crouching headshot front"); r.dCrouch = ci("crouch death"); r.dFlyBack = ci("flying back death");
  r.dFallFwd = ci("falling forward death"); r.dReactFwd = ci("standing react death forward"); r.dFallBack = ci("falling back death"); r.hit = ci("hit reaction"); r.walkHit = ci("walking hit reaction");
  static const char* deaths[6] = {"death from the front", "death from the back", "death from right", "death from front headshot", "death from back headshot", "death from the front"};
  for (int k = 0; k < 6; ++k) r.death[k] = findClip(m, deaths[k]);
  r.cdeath = findClip(m, "death crouching headshot front");
  if (r.death[0] < 0) {   // NA1: a pistol set's deaths
    const int d0 = findClip(m, "falling back death"), d1 = findClip(m, "flying back death"), d2 = findClip(m, "standing react death forward"), d3 = d0 >= 0 ? d0 : d1 >= 0 ? d1 : d2;
    const int dd[6] = {d0, d1, d2, d0, d1, d2}; for (int k = 0; k < 6; ++k) r.death[k] = dd[k] >= 0 ? dd[k] : d3;
    if (r.cdeath < 0) r.cdeath = findClip(m, "crouch death");
  }
  if (r.walk[0] < 0 && r.pWalk >= 0) {   // NA1: a pistol-only set: its walk / run / backward / strafes stand in for the 8 directions
    const int pl = findClip(m, "pistol strafe left"), pr = findClip(m, "pistol strafe right"), pb = r.pWalkB >= 0 ? r.pWalkB : r.pWalk, prb = r.pRunB >= 0 ? r.pRunB : pb;
    const int wv[8] = {r.pWalk, r.pWalk, pl >= 0 ? pl : r.pWalk, pb, pb, pb, pr >= 0 ? pr : r.pWalk, r.pWalk};
    const int rv[8] = {r.pRun >= 0 ? r.pRun : r.pWalk, r.pRun >= 0 ? r.pRun : r.pWalk, pl >= 0 ? pl : r.pWalk, prb, prb, prb, pr >= 0 ? pr : r.pWalk, r.pRun >= 0 ? r.pRun : r.pWalk};
    for (int k = 0; k < 8; ++k) { r.walk[k] = wv[k]; r.run[k] = r.sprint[k] = rv[k]; r.cwalk[k] = wv[k]; }
    if (r.idle < 0) r.idle = r.aim = r.pIdle; if (r.cidle < 0) r.cidle = r.caim = r.pKneel >= 0 ? r.pKneel : r.pIdle;
  }
  // the pack's "Throw Grenade" is a prone throw (hips 15 cm up): prone Delta soldiers take the procedural prone pose instead
  r.throwS[0] = r.throwS[1] = findClip(m, "Grenade Throwstand"); r.throwC = findClip(m, "Throw Grenadecrouch");
  if (r.throwS[0] < 0) { r.throwS[0] = r.throwS[1] = findClip(m, "grenade throw"); r.throwC = findClip(m, "throw grenade"); }   // NA1: the FigRig sets (standing, crouched)
  for (int k = 0; k < m.joints(); ++k) if (m.nodeName[m.jointNode[k]] == "flash") r.flash = k;   // NA1: a body with its gun built in
  r.shot = findClip(m, "shoot single"); r.cshot = findClip(m, "crouch shoot single"); if (r.cshot < 0) r.cshot = r.shot;
  r.reload = findClip(m, "reload"); r.creload = findClip(m, "crouch reload"); if (r.creload < 0) r.creload = r.reload;
  r.shotAt = flashAt(m, r.shot, r.flash); r.cshotAt = flashAt(m, r.cshot, r.flash);
  if (r.throwC < 0) r.throwC = r.throwS[0];
  r.turnL = findClip(m, "turn 90 left"); r.turnR = findClip(m, "turn 90 right"); r.cturnL = findClip(m, "crouching turn 90 left"); r.cturnR = findClip(m, "crouching turn 90 right");
  if (r.cturnL < 0) r.cturnL = r.turnL; if (r.cturnR < 0) r.cturnR = r.turnR;
  return gDRigs[&m] = r;
}
}   // namespace

// the centre of a gun's parts of one role (weapons.cpp WPart: 1 bolt, 2 mag, 3 cover, 4 clip, 7 handle), weapon space, from the
// kept vertex positions; `def` when it has none
vec3 weaponPartCentre(const WeaponDef& d, int role, vec3 def) {
  std::lock_guard<std::recursive_mutex> lk(gPoseCacheMx);
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

// the death clip for this life (once, at the first dead pose): the candidates the hit allows, by what killed and where -- a blast or a
// vehicle throws back; crouched: the crouching deaths; a head hit from the front / back its headshot; from the right its own; from behind
// while running falling forward; else front / back -- then one at random among them, never the clip a soldier within 20 m took in the
// last 4 s when another is valid. -1: none of them in this model (the old pick)
static int pickDeath(Game& G, Soldier& s, const DRig& r, const CharModel& m, bool crouch) {
  const std::string& how = s.lastHow;
  const bool blast = how == "blast" || how == "vehicle", head = how == "head" || how == "neck";
  const vec3 f = App::forward(s.yaw, 0), rt(-f.z, 0, f.x);
  vec3 d = s.deathDir; d.y = 0; const float L = glm::length(d); if (L > 1e-3f) d /= L;
  const float along = L > 1e-3f ? glm::dot(f, d) : -1.0f, side = L > 1e-3f ? glm::dot(rt, d) : 0.0f;   // along > 0: the round travelled the way he faced (hit from behind); side < 0: from his right
  const bool fromBack = along > 0.35f, fromRight = side < -0.6f;
  std::vector<int> c;
  auto add = [&](int k) { if (k >= 0 && std::find(c.begin(), c.end(), k) == c.end()) c.push_back(k); };
  if (blast) { add(r.dFlyBack); add(r.dFallBack); }
  if (c.empty() && crouch) { if (head) add(r.dCrouchHead); add(r.dCrouch); if (c.empty()) add(r.dCrouchHead); }
  if (c.empty() && head) add(fromBack ? r.dHeadB : r.dHeadF);
  if (c.empty() && fromRight) add(r.dRight);
  if (c.empty() && fromBack) { add(r.dBack); if (s.deathRunning) add(r.dFallFwd); }
  if (c.empty()) { add(r.dFront); add(r.dReactFwd); add(r.dFallBack); }
  if (c.empty()) return -1;
  struct Recent { int clip; vec3 p; double t; const CharModel* m; }; static std::deque<Recent> recent;
  while (!recent.empty() && (G.t - recent.front().t > 4.0 || G.t < recent.front().t)) recent.pop_front();
  std::vector<int> fresh; for (int k : c) { bool rep = false; for (auto& q : recent) rep = rep || (q.m == &m && q.clip == k && glm::length(q.p - s.feet) < 20.0f); if (!rep) fresh.push_back(k); }
  const std::vector<int>& pool = fresh.empty() ? c : fresh;
  uint32_t h = (uint32_t)s.slot * 2654435761u ^ (uint32_t)s.lifeId * 40503u ^ (uint32_t)(G.t * 1000.0); h ^= h >> 15; h *= 0x2c1b3c6dU; h ^= h >> 12;
  const int pick = pool[h % pool.size()];
  recent.push_back({pick, s.feet, G.t, &m}); if (recent.size() > 16) recent.pop_front();
  logf("[death] slot %d: '%s' (%s, from %s%s, %zu candidates)", s.slot, m.clips[pick].name.c_str(), how.empty() ? "-" : how.c_str(),
       fromBack ? "behind" : fromRight ? "the right" : side > 0.6f ? "the left" : "the front", crouch ? ", crouched" : "", c.size());
  return pick;
}

bool deltaBodyPose(Game& G, Soldier& s, float dt, PoseParams& pp) {
  enum { K_IDLE, K_AIM, K_FIRE = 4, K_LOCO = 3, K_DEATH = 6, K_THROW = 7, K_TURN = 8, K_RELOAD = 9 };
  const CharModel& m = *G.models[s.model];
  if (m.clips.empty()) return false;
  const DRig& r = dRigOf(m);
  if (r.walk[0] < 0 || r.idle < 0) return false;
  if (s.alive && (s.posture == Physics::PRONE || pp.swim > 0.3f || pp.climb >= 0)) { s.anim.key = -1; return false; }   // WSM2: swimming / climbing: the procedural soldier   // no prone clips in the packs: the soldier's procedural prone
  // a long gun whose pistol grip sits at its butt end (sawn-off, the revolving grenade launcher): held as a long gun, not a pistol
  if (pp.holdKind < 0 && pp.gripR.z < 0.12f && pp.gripL.z - pp.gripR.z > 0.2f) pp.holdKind = 1;
  AnimPlayer& A = s.anim;
  if (A.life != s.lifeId) { A = AnimPlayer{}; A.life = s.lifeId; }
  const int key0 = A.key;
  const bool crouch = s.posture == Physics::CROUCH;
  const bool bi = s.gunBody && r.flash >= 0;   // NA1: the gun built into the body (SS / gas-mask 2): its own reload and shot clips, no arm IK
  if (bi) { if (A.life == s.lifeId && s.lastShot != s.gmShotAt) { s.gmShotAt = s.lastShot; s.gmFireT = 0; } else s.gmFireT += dt; }
  const int shotC = crouch ? r.cshot : r.shot, relC = crouch ? r.creload : r.reload; const float shotAt = crouch ? r.cshotAt : r.shotAt;
  const bool pistol = s.weapon >= 0 && (pp.holdKind >= 0 ? pp.holdKind == 0 : pp.gripR.z < 0.12f);
  const bool hot = s.br.target >= 0 || G.t - s.lastShot < 1.5 || s.reload > 0;
  const float bodyYaw = (hot && !s.localPlayer ? s.anGunYaw : s.yaw) - s.dfYawOff;   // the body as drawn: turned so the clip's gun points at the aim (game.cpp)
  const vec3 fwd = App::forward(bodyYaw, 0), left(-std::cos(radians(bodyYaw)), 0, std::sin(radians(bodyYaw)));
  const float vf = glm::dot(s.vel, fwd), vl = glm::dot(s.vel, left);
  auto gs = [&](int k, float def) { return (k >= 0 && m.clips[k].groundSpeed > 0.2f ? m.clips[k].groundSpeed : def) * s.eScale; };   // RB6: a scaled kit's longer stride
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
    if (s.deathPickLife != s.lifeId) { s.deathPickLife = s.lifeId; s.deathClip = pickDeath(G, s, r, m, crouch); }
    int d = s.deathClip;
    if (d < 0) d = crouch && r.cdeath >= 0 ? r.cdeath : r.death[(s.slot + s.lifeId) % 6];
    if (d < 0) d = r.death[0];
    if (d >= 0) A.play(K_DEATH, d, -1, 0, 1, false);
  } else if (s.alive && (r.hit >= 0 || r.walkHit >= 0) && G.t - s.br.hurtAt < 0.45 && s.br.hurtAt > s.diedAt && s.reload <= 0 && s.throwT < 0) {   // a non-lethal hit: the pack's flinch (walking: its own)
    const int hc = moving && r.walkHit >= 0 ? r.walkHit : r.hit >= 0 ? r.hit : r.walkHit;
    A.play(K_TURN + 20, hc, -1, 0, 1, false);
  } else if (bi && s.reload > 0 && relC >= 0) {   // its reload clip, stretched over the reload time
    A.play(K_RELOAD + 10 * crouch, relC, -1, 0, m.clips[relC].dur / std::max(0.3f, s.reloadTotal), false);
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
  } else if (bi && shotC >= 0 && s.gmFireT < m.clips[shotC].dur - shotAt) {   // the recoil of the last shot, from its flash frame
    A.play(K_FIRE + 10 * crouch, shotC, -1, 0, 0, false); hold = shotAt + s.gmFireT;
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
    static std::map<std::pair<const CharModel*, int>, float> yaw0; float yaw0v;
    auto k0 = std::make_pair(&m, turnClip);
    { std::lock_guard<std::recursive_mutex> lk(gPoseCacheMx);
    if (!yaw0.count(k0)) { NodeLocals L; restLocals(m, L); sampleClip(m, turnClip, 0, true, L); PoseResult P0; poseFromLocals(m, L, P0); yaw0[k0] = hipsYaw(m, P0); }
    yaw0v = yaw0[k0]; }
    const float yw = hipsYaw(m, base) - yaw0v; const vec3 piv = vec3(base.delta[m.semantic[CJ_HIPS]] * vec4(m.jointHead[m.semantic[CJ_HIPS]], 1));
    const mat4 R = glm::translate(mat4(1), vec3(piv.x, 0, piv.z)) * glm::mat4_cast(glm::angleAxis(-yw, vec3(0, 1, 0))) * glm::translate(mat4(1), -vec3(piv.x, 0, piv.z));
    for (auto& d : base.delta) d = R * d;
  }
  s.poseFrom = -1; ++s.poseRev;
  if (!s.alive) { s.pose = base; return true; }
  if (bi) {   // the built-in gun: the bore yaw meets the aim (the body turned by it), the upper body and the root-level hand / arm IK chains pitch with the aim
    const glm::mat3 Rf = glm::mat3(base.delta[r.flash]); vec3 b = Rf * vec3(0, 0, 1); b.y = 0;
    const float want = s.throwT >= 0 || glm::length(b) < 0.1f ? s.dfYawOff : glm::degrees(std::atan2(b.x, b.z));
    s.dfYawOff += (want - s.dfYawOff) * (1.0f - std::exp(-dt / 0.2f));
    if (glm::length(b) > 0.1f) b = glm::normalize(b); else b = vec3(0, 0, 1);
    const int sp1 = m.semantic[CJ_SPINE1] >= 0 ? m.semantic[CJ_SPINE1] : m.semantic[CJ_SPINE];
    if (sp1 >= 0 && s.reload <= 0 && s.throwT < 0) {
      std::vector<char> sub(m.joints(), 0);
      for (int k = 0; k < m.joints(); ++k) { const std::string& nn = m.nodeName[m.jointNode[k]]; if (k == sp1 || nn.rfind("wrist_ik", 0) == 0 || nn.rfind("arm_ik", 0) == 0) sub[k] = 1; }
      for (int k = 0; k < m.joints(); ++k) for (int a = m.jointParent[k]; a >= 0 && !sub[k]; a = m.jointParent[a]) if (sub[a]) sub[k] = 1;
      const vec3 piv = vec3(base.delta[sp1] * vec4(m.jointHead[sp1], 1));
      const mat4 R = glm::translate(mat4(1), piv) * glm::mat4_cast(glm::angleAxis(-pp.aimPitch, vec3(b.z, 0, -b.x))) * glm::translate(mat4(1), -piv);
      for (int k = 0; k < m.joints(); ++k) if (sub[k]) base.delta[k] = R * base.delta[k];
      for (int k = 0; k < m.joints(); ++k) base.skin[k] = base.delta[k] * m.nodeWorld[m.jointNode[k]] * m.invBind[k];
      for (int c = 0; c < CJ_COUNT; ++c) { int k = m.semantic[c]; base.joint[c] = k >= 0 ? vec3(base.delta[k] * vec4(m.jointHead[k], 1)) : vec3(0); }
    }
    s.pose = base;
    flashMuzzle(G, s, m, r.flash);
    return true;
  }
  const int sp = m.semantic[CJ_SPINE1] >= 0 ? m.semantic[CJ_SPINE1] : m.semantic[CJ_SPINE];
  // the clip's gun line (model space): rifles the right -> left wrist line, a pistol shoulders -> hands; the body is drawn turned by
  // its yaw so that line points at the aim (game.cpp yawUse), and the whole chest pitches with the aim about the line's side axis
  vec3 gun(0, 0, 1);
  {
    auto at = [&](int c) { int k = m.semantic[c]; return k >= 0 ? vec3(base.delta[k] * vec4(m.jointHead[k], 1)) : vec3(0); };
    vec3 g = pistol ? 0.5f * (at(CJ_LHAND) + at(CJ_RHAND)) - 0.5f * (at(CJ_LARM) + at(CJ_RARM)) : at(CJ_LHAND) - at(CJ_RHAND);
    g.y = 0; if (glm::length(g) > 0.05f && s.throwT < 0) gun = glm::normalize(g);
    const bool tube = s.weapon >= 0 && G.app.weaponDefs[s.weapon]->action == "launcher";   // launchers ride the shoulder (the soldier placement), along the body
    const bool ruleHold = s.holdBody;   // WS1: held by rule along the body's forward, not along the clip's hands (WSM2: every skin on the shared skeleton)
    const float want = s.throwT >= 0 ? s.dfYawOff : tube || ruleHold ? 0.0f : glm::degrees(std::atan2(gun.x, gun.z));
    pp.clipGun = !tube; pp.pistolRaise = pistol && hot && s.reload <= 0 ? 1.0f : 0.0f;
    if (ruleHold && pistol) {   // WS1: the approved pistol postures: shooting / crouched = aimed; idle / walking = at the chest, muzzle up; running = the clip's own arms
      s.dfPUp += ((hot || crouch ? 0.0f : 1.0f) - s.dfPUp) * (1.0f - std::exp(-dt / 0.12f)); pp.pistolUp = s.dfPUp; pp.pistolRun = !hot && !crouch && s.speed > 3.2f; }
    s.dfYawOff += (want - s.dfYawOff) * (1.0f - std::exp(-dt / 0.2f));
  }
  if (sp >= 0) {   // the chest pitches with the aim (the clip's arms and the gun with it)
    const vec3 piv = vec3(base.delta[sp] * vec4(m.jointHead[sp], 1));
    const mat4 R = glm::translate(mat4(1), piv) * glm::mat4_cast(glm::angleAxis(-pp.aimPitch, vec3(gun.z, 0, -gun.x))) * glm::translate(mat4(1), -piv);
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
    pp.lowReady = pp.reload > 0 && pp.reload < 1 ? std::min(1.0f, std::min(pp.reload, 1 - pp.reload) / 0.1f) : 0.0f;   // the clips hold their own ready / aim   // off the cheek to reload: the relaxed low ready (muzzle ~35 deg down), back up at the end
    // what the draw moves (game.cpp): the magazine's span out of the gun; the bolt's / pump's travel
    if (s.reload > 0) s.dfMagWin = pp.reloadStyle == 0 ? vec2(0.25f, 0.8f) : pp.reloadStyle == 2 ? vec2(0.35f, 0.65f) : vec2(0);
    auto open = [](float c) { return c < 0.3f ? 0.0f : c < 0.55f ? (c - 0.3f) / 0.25f : c < 0.75f ? 1.0f - (c - 0.55f) / 0.2f : 0.0f; };
    if (pp.cycleKind == 1) s.dfBoltBack = 0.08f * open(pp.cycle);
    else if (bolt && pp.reload > 0 && pp.reload < 1) s.dfBoltBack = 0.08f * std::clamp(std::min(pp.reload / 0.12f, (1 - pp.reload) / 0.12f), 0.0f, 1.0f);
    if (pp.cycleKind == 2) s.dfPumpBack = 0.09f * std::sin(3.14159265f * pp.cycle);
  }
  pp.crouch = pp.kneel = pp.prone = pp.hunch = pp.lean = pp.swim = 0; pp.gait = false; pp.climb = -1; pp.aimYawOffset = 0;
  pp.carry = 0; pp.moveAim = 0; pp.clipHold = !s.holdBody;   /* WS1: the Allied / SS soldiers hold by rule (the soldier placement: butt in the right shoulder, barrel on the aim, palms on each gun's own sockets, hold.left styles), not in the clip's hands */
  poseCharacter(m, pp, s.pose);
  return true;
}
