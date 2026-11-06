// Perception, firing and grenades (AI_SPEC 4, 6.4, 7).
//   sight: 120 deg cone while moving, 90 deg while aiming; head and chest rays; detection time from distance
//     (difficulty detection range in the open), posture, movement, firing, vegetation (brush / marsh areas), smoke.
//   hearing: rifles / MGs ~250 m, SMGs / pistols ~120 m (rough position, better when close), footsteps 15 m,
//     explosions 200 m. Near misses within 2 m and hits give a cone toward the shooter and raise suppression.
//   aim model: every new target starts with an aim error (difficulty) that settles with time on target and grows
//     with own movement, suppression, the target's movement, range and posture. Not an aimbot.
//   grenades: several simulated arcs, landing within ~2 m, no bounce-back, nobody of our side near the landing or
//     the arc; per-bot cooldown from the difficulty, the loadout's count.
#include "ai_core.h"
#include "app.h"
#include <algorithm>
#include <cmath>

namespace {
float angDiff(float a, float b) { return wrapDeg(a - b + 180.0f) - 180.0f; }
float yawOf(vec3 d) { return degrees(std::atan2(-d.x, -d.z)); }
float pitchOf(vec3 d) { return degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z)))); }
bool isLeafUD(uint64_t u) { if (u == 0 || (u >> 60) == 0xF) return false; uint64_t l = u & 0xFFFF; return l != 0 && l != 0xFFFF && l != 0xFFF0; }
float lenXZ(vec3 v) { return glm::length(vec2(v.x, v.z)); }
bool pistolCal(const std::string& c) { return c.find("9x19") != std::string::npos || c.find("ACP") != std::string::npos || c.find(".45") != std::string::npos || c.find("7.65") != std::string::npos; }
}

void AiCore::perceive(Soldier& s) {
  Brain& b = s.br;
  BotMind& m = mind(s);
  const float dtp = 0.2f;
  vec3 e = G.eye(s);
  vec3 look = App::forward(b.aimYaw, 0);
  bool aiming = m.action == A_RETURN_FIRE || m.action == A_SUPPRESS || m.action == A_OVERWATCH || s.speed < 0.6f;
  float fov = aiming ? tj("perception", "fovAiming_deg", 90.0f) : tj("perception", "fovMoving_deg", 120.0f);
  float cosHalf = std::cos(radians(fov * 0.5f));
  float range = diff.detectOpen;
  for (auto& mm : b.mem) mm.visible = false;
  for (auto& o : G.sold) {
    if (!o.alive || o.role == s.role) continue;
    vec3 tc = G.chest(o), th = G.eye(o);
    float dist = glm::length(tc - e);
    EnemyMemory* mm = nullptr; for (auto& x : b.mem) if (x.enemy == o.slot) mm = &x;
    bool firing = G.t - o.lastShot < 0.6;
    if (o.localPlayer && o.weapon >= 0 && o.weapon < (int)G.app.weaponDefs.size()) { auto it = G.app.wstate.find(G.app.weaponDefs[o.weapon]->id); if (it != G.app.wstate.end() && it->second.sinceShot < 0.6f) firing = true; }
    if (dist > range * (firing ? 1.4f : 1.0f)) { if (mm) mm->awareness = std::max(0.0f, mm->awareness - dtp * 0.5f); continue; }
    vec3 dir = (tc - e) / std::max(dist, 1e-3f);
    bool inFov = dist < 6.0f || glm::dot(glm::normalize(vec3(dir.x, 0, dir.z) + vec3(1e-5f)), look) >= cosHalf;
    float T = 0; bool los = false;
    if (inFov) { raysThisFrame += 1; los = G.lineOfSight(e, tc, &T); if (!los) { raysThisFrame += 1; los = G.lineOfSight(e, th, &T); } }
    if (los && T > 0.35f) {
      if (!mm) { b.mem.push_back({o.slot}); mm = &b.mem.back(); }
      float base = dist < 15 ? 4.0f : dist < 50 ? 1.8f : 0.9f;
      float rate = base * std::max(0.12f, 1.0f - dist / range);
      if (o.speed > 1.0f) rate *= o.speed > 4.5f ? 2.0f : 1.5f;
      if (o.posture == Physics::PRONE) rate *= 0.35f; else if (o.posture == Physics::CROUCH) rate *= 0.65f;
      if (firing) rate *= 2.5f;
      NavArea ar = navarea::toNav(G.nav.areaAt(o.feet, 0.8f));
      if (ar == NavArea::Brush || ar == NavArea::Marsh) rate *= tj("perception", "vegetationFactor", 0.55f);
      rate *= std::clamp((T - 0.35f) / 0.4f, 0.15f, 1.0f);              // thin smoke slows detection, dense smoke stops it
      mm->awareness = std::min(1.0f, mm->awareness + rate * dtp);
      if (mm->awareness >= 1.0f) { mm->visible = true; mm->lastPos = o.feet; mm->lastSeen = G.t; mm->heardOnly = false; }
    } else if (mm) mm->awareness = std::max(0.0f, mm->awareness - dtp * 0.5f);
    // footsteps: running enemies close by
    if (!los && dist < tj("perception", "footsteps_m", 15.0f) && o.speed > 2.0f && o.posture == Physics::STAND) {
      if (!mm) { b.mem.push_back({o.slot}); mm = &b.mem.back(); }
      if (!mm->visible && G.t - mm->lastSeen > 0.8) { mm->lastPos = o.feet + vec3((G.rnd(s) - 0.5f) * 3, 0, (G.rnd(s) - 0.5f) * 3); mm->lastSeen = G.t; mm->heardOnly = true; mm->awareness = std::max(mm->awareness, 0.5f); }
    }
  }
  // hearing: shots by weapon class, explosions, grenade landings (approximate positions)
  for (auto& snd : G.sounds) {
    if (G.t - snd.t > 0.25 || snd.src < 0 || snd.src >= (int)G.sold.size()) continue;
    const Soldier& o = G.sold[snd.src];
    if (o.role == s.role || !o.alive) continue;
    float d = glm::length(snd.p - s.feet), R = snd.range;
    if (snd.kind == 0) { bool pistol = o.weapon >= 0 && pistolCal(G.app.weaponDefs[o.weapon]->caliber); R = pistol ? tj("perception", "hearSmg_m", 120.0f) : tj("perception", "hearRifle_m", 250.0f); }
    else if (snd.kind == 1) R = tj("perception", "hearExplosion_m", 200.0f);
    if (d > R) continue;
    EnemyMemory* mm = nullptr; for (auto& x : b.mem) if (x.enemy == o.slot) mm = &x;
    if (!mm) { b.mem.push_back({o.slot}); mm = &b.mem.back(); }
    if (mm->visible || G.t - mm->lastSeen < 1.0) continue;
    float err = std::min(12.0f, d * 0.08f);
    mm->lastPos = snd.p + vec3((G.rnd(s) - 0.5f) * 2 * err, 0, (G.rnd(s) - 0.5f) * 2 * err);
    mm->lastSeen = G.t; mm->heardOnly = true; mm->awareness = std::max(mm->awareness, 0.5f);
  }
  float memS = tj("perception", "memory_s", 20.0f);
  b.mem.erase(std::remove_if(b.mem.begin(), b.mem.end(), [&](const EnemyMemory& x) { return (G.t - x.lastSeen > memS && x.awareness <= 0.01f) || x.enemy < 0 || !G.sold[x.enemy].alive; }), b.mem.end());
  // grenade danger: seen in the air / landing nearby, or heard landing close (AI_SPEC 7)
  for (auto& g : G.gren.live) {
    if (g.kind != GK_FRAG) continue;
    // ours ("frag out"): known without seeing it; keep clear of its whole hurt radius (AI_SPEC 12.3: no grenade hurts a teammate)
    bool ours = g.owner >= 0 && g.owner < (int)G.sold.size() && G.sold[g.owner].role == s.role;
    float d = glm::length(g.p - s.feet);
    if (d > (ours ? tj("grenade", "friendlyClear_m", 11.0f) : tj("grenade", "reactRadius_m", 8.0f))) continue;
    bool seen = ours || (glm::dot(glm::normalize(g.p - e + vec3(1e-4f)), look) > 0.3f && G.lineOfSight(e, g.p));
    if (seen || (g.resting && d < 5.0f)) { b.evadeFrom = g.p; b.evadeUntil = G.t + g.fuse + 0.5; m.grenadeSeenAt = G.t; }
  }
  // target: visible enemies; the fireteam's assignment, the objective and the current target preferred;
  // spawn-protected soldiers get near-zero priority
  TeamBrain& tb = teams[(int)s.role];
  int assigned = -1;
  if (m.ft >= 0 && m.ft < (int)tb.fts.size()) { auto it = tb.fts[m.ft].targetFor.find(s.slot); if (it != tb.fts[m.ft].targetFor.end()) assigned = it->second; }
  int best = -1; float bs = 1e9f;
  const Objective* ob = G.match.activeObjective();
  for (auto& x : b.mem) {
    if (!x.visible) continue;
    float sc = glm::length(x.lastPos - s.feet);
    if (ob && ob->contains(x.lastPos)) sc *= 0.7f;
    if (x.enemy == b.target) sc *= 0.8f;
    if (x.enemy == assigned) sc *= 0.75f;
    if (G.sold[x.enemy].isProtected(G.t)) sc *= 60.0f;
    if (sc < bs) { bs = sc; best = x.enemy; }
  }
  if (best != b.target) { b.target = best; b.targetTime = 0; b.aimSteady = 0; }
}

void AiCore::nearMisses() {
  const float R = tj("perception", "nearMiss_m", 2.0f);
  for (auto& p : G.app.projectiles) {
    if (p.kind != PROJ_BULLET) continue;
    int shooter = G.soldierOfOwner(p.owner);
    if (shooter < 0 || shooter >= (int)G.sold.size()) continue;
    const Soldier& sh = G.sold[shooter];
    vec3 a = p.p - p.v * (1.0f / 60.0f), b = p.p;
    vec3 ab = b - a; float L2 = glm::dot(ab, ab); if (L2 < 1e-6f) continue;
    for (auto& s : G.sold) {
      if (!s.alive || s.human || s.role == sh.role) continue;
      vec3 c = G.chest(s);
      float u = std::clamp(glm::dot(c - a, ab) / L2, 0.0f, 1.0f);
      float d = glm::length(a + ab * u - c);
      if (d > R) continue;
      BotMind& m = mind(s);
      float add = (d < 0.8f ? 0.22f : 0.12f) * (1.0f - 0.6f * diff.suppressionResist);
      s.br.suppressed = std::min(1.0f, s.br.suppressed + add);
      m.nearMissAt = G.t;
      float dist = glm::length(sh.feet - s.feet);
      vec3 jit((G.rnd(s) - 0.5f) * dist * 0.2f, 0, (G.rnd(s) - 0.5f) * dist * 0.2f);
      m.nearMissFrom = sh.feet + jit;                                   // a cone toward the shooter
      EnemyMemory* mm = nullptr; for (auto& x : s.br.mem) if (x.enemy == shooter) mm = &x;
      if (!mm) { s.br.mem.push_back({shooter}); mm = &s.br.mem.back(); }
      if (!mm->visible) { mm->lastPos = m.nearMissFrom; mm->lastSeen = G.t; mm->heardOnly = true; mm->awareness = std::max(mm->awareness, 0.55f); }
    }
  }
  // hits (Game::damage sets hurtFrom / hurtAt): the shooter's rough position joins the memory too
  for (auto& s : G.sold) {
    if (!s.alive || s.human || G.t - s.br.hurtAt > 0.02) continue;
    for (auto& o : G.sold) if (o.alive && o.role != s.role && glm::length(o.feet - s.br.hurtFrom) < 0.5f) {
      EnemyMemory* mm = nullptr; for (auto& x : s.br.mem) if (x.enemy == o.slot) mm = &x;
      if (!mm) { s.br.mem.push_back({o.slot}); mm = &s.br.mem.back(); }
      if (!mm->visible) { mm->lastPos = o.feet; mm->lastSeen = G.t; mm->heardOnly = true; mm->awareness = std::max(mm->awareness, 0.7f); }
    }
  }
}

void AiCore::aim(Soldier& s, float dt) {
  Brain& b = s.br; BotMind& m = mind(s);
  b.suppressed = std::max(0.0f, b.suppressed - dt * tj("perception", "suppressionDecay", 0.28f));
  b.targetTime += dt;
  float wantYaw = b.aimYaw, wantPitch = 0;
  vec3 e = G.eye(s);
  bool shoot = false, suppress = false; vec3 aimPoint(0); float smokeT = 1;
  EnemyMemory* tm = nullptr; for (auto& x : b.mem) if (x.enemy == b.target) tm = &x;
  TeamBrain& tb = teams[(int)s.role];
  bool blockedAct = m.action == A_EVADE || m.action == A_RELOAD || (m.action == A_CLEAR && m.set && b.target < 0) || m.trav.on;
  if (tm && tm->visible && G.sold[b.target].alive) {
    const Soldier& o = G.sold[b.target];
    aimPoint = G.chest(o) + o.vel * 0.12f;
    shoot = true;
    G.lineOfSight(e, aimPoint, &smokeT);
  } else if (m.action == A_SUPPRESS || (m.action == A_OVERWATCH && G.app.weaponDefs[s.weapon]->action == "auto")) {
    // suppressive fire at the last known position (own or shared), bursts
    const Known* best = nullptr; for (auto& [en, k] : tb.known) if (!k.heard && G.t - k.seen < 6 && (!best || k.seen > best->seen)) best = &k;
    for (auto& x : b.mem) if (!x.heardOnly && G.t - x.lastSeen < 6 && (!best || x.lastSeen > best->seen)) { static Known tmp; tmp.pos = x.lastPos; tmp.seen = x.lastSeen; best = &tmp; }
    if (best && s.ws.loaded > 2) { aimPoint = best->pos + vec3(0, 1.0f, 0); shoot = true; suppress = true; }
  }
  if (shoot) { vec3 d = aimPoint - e; wantYaw = yawOf(d); wantPitch = pitchOf(d); }
  else if (m.hasPath && glm::length(m.lastVel) > 0.5f) {
    wantYaw = yawOf(m.lastVel);
    const Known* k0 = nullptr; for (auto& [en, k] : tb.known) if (G.t - k.seen < 6 && (!k0 || k.seen > k0->seen)) k0 = &k;
    if (k0 && m.speed != SPD_SPRINT) wantYaw = yawOf(k0->pos - s.feet);                  // never turn the back on a known threat
  } else {
    const Objective* ob = G.match.activeObjective();
    if (ob && s.role == CombatRole::Defender) wantYaw = yawOf(-attackForward(ob->attackYaw));
    if (m.set && m.cover >= 0 && m.cover < (int)cover.size() && glm::length(cover[m.cover].n) > 0.1f) wantYaw = yawOf(cover[m.cover].n);
    const Known* k0 = nullptr; for (auto& [en, k] : tb.known) if (G.t - k.seen < 10 && (!k0 || k.seen > k0->seen)) k0 = &k;
    if (k0) wantYaw = yawOf(k0->pos - s.feet);
  }
  // finite aim turn rate, separate from the body (thinking LOD: far from every player, no fine aiming)
  float hd = nearestHumanDist(s.feet);
  bool lod = hd < 1e8f && hd > 300.0f;
  float dy = angDiff(wantYaw, b.aimYaw), dp = wantPitch - b.aimPitch;
  float stepA = b.aimTurn * dt;
  if (lod) { b.aimYaw = wrapDeg(wantYaw); b.aimPitch = wantPitch; dy = dp = 0; }
  else { b.aimYaw = wrapDeg(b.aimYaw + std::clamp(dy, -stepA, stepA)); b.aimPitch = std::clamp(b.aimPitch + std::clamp(dp, -stepA, stepA), -60.0f, 60.0f); }
  float turnErr = std::sqrt(dy * dy + dp * dp);
  b.aimSteady = turnErr < 3.0f ? b.aimSteady + dt : 0.0f;
  // aim error (AI_SPEC 6.4)
  if (b.target != m.aimTarget) { m.aimTarget = b.target; m.onTarget = 0; float a = G.rnd(s) * 6.2831853f; m.aimOff = vec2(std::cos(a), std::sin(a)); }
  m.onTarget += dt;
  if (!shoot || blockedAct || s.throwT >= 0 || s.reload > 0 || s.weapon < 0) return;
  const WeaponDef& d = *G.app.weaponDefs[s.weapon];
  bool rifle = d.action != "auto";
  if (rifle && s.speed > 1.2f && !suppress) return;                         // rifles stop and shoot
  if (b.targetTime < diff.reaction && !suppress) return;
  if (turnErr > 4.0f) return;
  if (b.burstPause > 0) { b.burstPause -= dt; return; }
  if (s.ws.cooldown > 0 || s.ws.bolt > 0) return;
  float dist = glm::length(aimPoint - e);
  const Soldier& tgtS = G.sold[std::max(0, b.target)];
  float settle = std::exp(-m.onTarget / std::max(0.1f, diff.aimSettle));
  float err = diff.aimError * settle + b.sigma * 0.35f;
  err *= (s.speed > 0.5f ? 1.0f + s.speed / 3.0f : 1.0f) * (1.0f + b.suppressed * (1.0f - diff.suppressionResist) * 1.5f)
       * (!suppress && tgtS.speed > 1.0f ? 1.3f : 1.0f) * (s.posture == Physics::PRONE ? 0.7f : s.posture == Physics::CROUCH ? 0.85f : 1.0f)
       * (1.0f + dist / 150.0f) * (smokeT < 0.6f ? 1.5f : 1.0f) * (suppress ? 2.5f : 1.0f);
  if (d.feed == "belt" && s.posture == Physics::STAND) err *= 1.8f;
  // a slowly wandering offset (consecutive shots correlate) plus a per-shot gaussian part
  float a0 = (G.rnd(s) - 0.5f) * 0.6f, ca = std::cos(a0), sa = std::sin(a0); m.aimOff = glm::normalize(vec2(m.aimOff.x * ca - m.aimOff.y * sa, m.aimOff.x * sa + m.aimOff.y * ca) + vec2(1e-4f));
  float g1 = G.rnd(s), g2 = G.rnd(s);
  float r = std::sqrt(-2.0f * std::log(std::max(g1, 1e-6f))) * err * 0.5f, ang = g2 * 6.2831853f;
  vec2 off = m.aimOff * err * 0.7f + vec2(std::cos(ang), std::sin(ang)) * r;
  vec3 dir = App::forward(b.aimYaw + off.x, b.aimPitch + off.y);
  if (G.soldierFire(s, dir)) {
    if (d.action == "auto") {
      if (b.burstLeft <= 0) b.burstLeft = (float)(b.burstMin + (int)(G.rnd(s) * (b.burstMax - b.burstMin + 1)));
      if (--b.burstLeft <= 0) b.burstPause = (suppress ? 0.5f : 0.35f) + G.rnd(s) * 0.4f;
    } else b.burstPause = 0.25f + G.rnd(s) * 0.45f + (dist > 60 ? 0.4f : 0.0f);
  }
}

bool AiCore::planGrenade(Soldier& s, vec3 target, vec3& vel) {
  int fd = G.fragDef(s.faction);
  if (fd < 0 || s.frags <= 0) return false;
  float safe = std::max(8.0f, tj("grenade", "safeRadius_m", 11.0f)), arcClear = tj("grenade", "arcClearance_m", 2.0f);
  for (auto& o : G.sold) if (o.alive && o.role == s.role && &o != &s && glm::length(o.feet - target) < safe) return false;   // teammates (humans too)
  if (glm::length(s.feet - target) < safe) return false;
  // where teammates (and the thrower) will be when it goes off: bots along their planned route, humans by velocity
  {
    float fuse = G.app.weaponDefs[fd]->grenade.fuse + 0.5f;
    for (auto& o : G.sold) {
      if (!o.alive || o.role != s.role) continue;
      if (o.human || o.slot >= (int)minds.size() || !minds[o.slot]) {
        for (float t : {1.5f, 3.0f, fuse}) if (glm::length(o.feet + o.vel * t - target) < safe) return false;
        continue;
      }
      const BotMind& om = *minds[o.slot];
      float budget = std::max(o.speed, vSprint) * fuse, L = 0; vec3 prev = o.feet;
      size_t k0 = 0; float bd = 1e9f; for (size_t k = 0; k < om.route.size(); ++k) { float d = lenXZ(om.route[k] - o.feet); if (d < bd) { bd = d; k0 = k; } }
      for (size_t k = k0; k < om.route.size() && L < budget; ++k) {
        vec3 a = prev, b = om.route[k]; float seg = lenXZ(b - a);
        for (float u = 0; u <= 1.0f; u += 0.25f) { if (L + seg * u > budget) break; if (glm::length(glm::mix(a, b, u) - target) < safe) return false; }
        L += seg; prev = b;
      }
    }
  }
  float speed = G.app.weaponDefs[fd]->grenade.speed;
  vec3 hand = s.feet + vec3(0, 1.7f, 0);
  for (float f : {1.0f, 0.88f, 0.76f}) for (int hi = 0; hi < 2; ++hi) {
    vec3 v;
    if (!grenadeVelocity(hand, target, speed * f, hi == 1, v)) continue;
    vec3 p = hand, vv = v; bool ok = false, bad = false;
    for (int k = 0; k < 140 && !bad; ++k) {
      vec3 seg = vv * 0.04f; Physics::Hit h;
      for (auto& o : G.sold) if (o.alive && o.role == s.role && &o != &s && glm::length(G.chest(o) - p) < arcClear) bad = true;
      if (bad) break;
      if (ray(p, seg, glm::length(seg), &h)) {
        if (isLeafUD(h.ud)) { bad = true; break; }
        vec3 toMe = s.feet - h.pos; toMe.y = 0;
        bool bounceBack = std::fabs(h.normal.y) < 0.5f && glm::length(toMe) > 0.1f && glm::dot(glm::normalize(vec3(h.normal.x, 0, h.normal.z) + vec3(1e-4f)), glm::normalize(toMe)) > 0.4f && lenXZ(h.pos - target) > 1.0f;
        if (bounceBack || lenXZ(h.pos - s.feet) < 6.0f) { bad = true; break; }
        ok = lenXZ(h.pos - target) < tj("grenade", "landWithin_m", 2.0f) && std::fabs(h.pos.y - target.y) < 1.8f && h.normal.y > 0.6f;   // lands on the ground: a wall or a window frame sends it anywhere
        for (auto& o : G.sold) if (ok && o.alive && o.role == s.role && glm::length(o.feet - h.pos) < safe) ok = false;   // around where it really lands
        break;
      }
      p += seg; vv.y -= 9.81f * 0.04f;
    }
    if (ok && !bad) { vel = v; return true; }
  }
  return false;
}

void AiCore::watchGrenades() {
  std::set<uint32_t> live;
  for (auto& g : G.gren.live) {
    if (g.kind != GK_FRAG || g.owner < 0 || g.owner >= (int)G.sold.size() || G.sold[g.owner].human) continue;
    live.insert(g.id); grenadeWatch[g.id] = {g.owner, g.p};
  }
  for (auto it = grenadeWatch.begin(); it != grenadeWatch.end();) {
    if (live.count(it->first)) { ++it; continue; }
    // exploded: would it have hurt one of the thrower's side? (friendly fire is usually off; count it anyway)
    int owner = it->second.first; vec3 p = it->second.second;
    if (owner >= 0 && owner < (int)G.sold.size()) {
      for (auto& o : G.sold) {
        if (!o.alive || o.role != G.sold[owner].role) continue;
        vec3 c = G.chest(o); float d = glm::length(c - p), R = G.gren.tun.fragMaxR;
        if (d > R) continue;
        float raw = G.gren.tun.fragDamage * std::pow(std::max(0.0f, 1.0f - std::max(0.0f, d - 0.5f) / R), 1.6f) * G.app.blastOcclusion(p, c);
        if (raw > tj("grenade", "hurtThreshold_hp", 10.0f)) { ++metrics.grenadesHurtTeammate; logf("ai: grenade by %d would hurt teammate %d (%.0f HP at %.1f m)", owner, o.slot, raw, d); break; }
      }
    }
    it = grenadeWatch.erase(it);
  }
}
