// GM1: the gas-mask soldier (AiType::GasMask). A soldier brain (ai_layers.cpp) in a body that carries its own clips and its
// gun (content/characters/gasmask_<weapon>.glb, tools/bake_gasmask.py): no separate weapon model. The clips by state:
//   dead: Death; prone: Prone (its end held); reloading: Reload / CrouchReload (stretched to the reload time);
//   moving: Walk -> Run by speed (in place: the bake removed the root motion); engaged and still: Shoot / CrouchShoot held at
//   the raised frame before its first flash, each shot replaying the clip's first recoil; else standing: Shoot's first
//   (lowered) frame, crouched: CrouchIdle.
// Not in the export, so not played: a hit flinch, a crouched walk (the walk plays), a standing idle (the lowered Shoot frame),
// strafes / backpedal (the walk), a grenade throw (gas-mask soldiers carry none). Climb, Jump, Lean* are baked but unused.
// The shots and flash come from the model's 'flash' joint: s.pose.weapon is set so that pose.weapon * the weapon's muzzle is
// that joint, with the bore along the joint's rest +z, so every pose.weapon user (soldierFire, net echoes) needs no change.
#include "game.h"
#include "character.h"
#include "anim_clip.h"
#include "ai_hooks.h"
#include "app.h"
#include "physics.h"
#include <map>

namespace {
struct Rig {
  int flash = -1;                                  // skin joint
  int walk = -1, run = -1, shoot = -1, cshoot = -1, cidle = -1, reload = -1, creload = -1, death = -1, prone = -1;
  float fire0 = 0.7f, recoil = 0.15f;              // Shoot: the first flash, and the gap to the next (the replayed recoil)
};
std::map<const CharModel*, Rig> gRigs;

const Rig& rigOf(const CharModel& m) {
  auto it = gRigs.find(&m); if (it != gRigs.end()) return it->second;
  Rig r;
  for (int k = 0; k < m.joints(); ++k) if (m.nodeName[m.jointNode[k]] == "flash") r.flash = k;
  r.walk = findClip(m, "Walk"); r.run = findClip(m, "Run"); r.shoot = findClip(m, "Shoot"); r.cshoot = findClip(m, "CrouchShoot");
  r.cidle = findClip(m, "CrouchIdle"); r.reload = findClip(m, "Reload"); r.creload = findClip(m, "CrouchReload");
  r.death = findClip(m, "Death"); r.prone = findClip(m, "Prone");
  if (r.shoot >= 0 && r.flash >= 0) {   // the flash joint's scale goes above 0 on each shot
    std::vector<float> on;
    for (auto& ch : m.clips[r.shoot].ch) if (ch.node == m.jointNode[r.flash] && ch.path == 2)
      for (size_t i = 0; i < ch.t.size(); ++i) if (ch.v[i * 3] > 0.01f && (i == 0 || ch.v[(i - 1) * 3] <= 0.01f)) on.push_back(ch.t[i]);
    if (!on.empty()) r.fire0 = on[0];
    if (on.size() > 1) r.recoil = std::clamp(on[1] - on[0], 0.08f, 0.35f);
  }
  return gRigs[&m] = r;
}
}   // namespace

void gasmaskPose(Game& G, Soldier& s, float dt) {
  enum { K_IDLE, K_AIM, K_LOCO, K_RELOAD, K_DEATH, K_PRONE };
  const CharModel& m = *G.models[s.model];
  const Rig& r = rigOf(m);
  AnimPlayer& A = s.anim;
  if (A.life != s.lifeId) { A = AnimPlayer{}; A.life = s.lifeId; s.gmShotAt = s.lastShot; s.gmFireT = 1; }
  const int key0 = A.key;
  const bool crouch = s.posture == Physics::CROUCH, prone = s.posture == Physics::PRONE;
  if (s.lastShot != s.gmShotAt) { s.gmShotAt = s.lastShot; s.gmFireT = 0; } else s.gmFireT += dt;
  const bool hot = s.br.target >= 0 || G.t - s.lastShot < 1.5;
  const int shoot = crouch && r.cshoot >= 0 ? r.cshoot : r.shoot, rel = crouch && r.creload >= 0 ? r.creload : r.reload;
  auto gs = [&](int k, float def) { return k >= 0 && m.clips[k].groundSpeed > 0.2f ? m.clips[k].groundSpeed : def; };
  const float vw = gs(r.walk, 1.4f), vr = std::max(vw + 0.5f, gs(r.run, 4.5f));
  float hold = -1;   // >= 0: a still frame of the clip (aim / low ready)
  if (!s.alive && r.death >= 0) A.play(K_DEATH, r.death, -1, 0, 1, false);
  else if (prone && r.prone >= 0) A.play(K_PRONE, r.prone, -1, 0, 1, false);   // getting down, then its last frame
  else if (s.reload > 0 && rel >= 0) A.play(K_RELOAD, rel, -1, 0, m.clips[rel].dur / std::max(0.3f, s.reloadTotal), false);
  else if (s.speed > (A.key == K_LOCO ? 0.3f : 0.6f) && r.walk >= 0) {   // hysteresis: no idle / walk flicker
    float w = r.run >= 0 ? std::clamp((s.speed - vw) / (vr - vw), 0.0f, 1.0f) : 0.0f;
    A.play(K_LOCO, r.walk, r.run, w, std::clamp(s.speed / glm::mix(vw, vr, w), 0.5f, 2.0f), true);
  } else if ((hot || s.gmFireT < r.recoil) && shoot >= 0) {
    A.play(K_AIM, shoot, -1, 0, 0, false);
    hold = s.gmFireT < r.recoil ? r.fire0 + s.gmFireT : std::max(0.0f, r.fire0 - 0.03f);   // raised; a shot replays the first recoil
  } else if (crouch && r.cidle >= 0) A.play(K_IDLE, r.cidle, -1, 0, 1, true);
  else if (shoot >= 0) { A.play(K_IDLE, shoot, -1, 0, 0, false); hold = 0; }   // no standing idle in the export: Shoot's lowered first frame
  if (hold >= 0) A.time = hold;
  if (A.key != key0) A.fadeLen = A.key == K_DEATH ? 0.2f : A.key == K_AIM ? 0.15f : 0.25f;
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
