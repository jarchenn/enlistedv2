// G2 zombies: AI types ticked per side (ScenarioRules::aiTypes). A zombie skips the whole soldier brain (perception,
// utility decisions, cover, aiming, grenades): it re-paths to the closest hostile about every 0.5 s, moves at
// speed_mps (between jog and sprint) and strikes in melee when in reach. Melee damage, from zombies and the
// player's knife, goes through Game::meleeHit -> Game::damage, so spawn protection, friendly fire and the LAN
// replication of health and deaths are the shared ones. Tunables: content/ai/zombie.json.
// Look (Z1): content/characters/zombie.glb with its own clips (anim_clip.h): walk / run blended by speed, an attack
// per strike with the blow at its swing frame, a flinch when hurt, death holding its end pose, idle with no target.
// PF1 packs (above pack.min_zombies): zombies group into packs of about pack.size. Only the leader picks the target
// and requests paths; followers slide along the navmesh (dtPathCorridor::moveTargetPosition, no A*) to a wedge slot
// behind it, with separation from pack mates and alignment to the leader, break off to strike any hostile within
// pack.break_off_m, and path on their own only when cut off. A dead leader's nearest member takes over. Pack mates
// never pick each other (Solo too). Far zombies sample their clips at a lower rate; far followers in step with their
// leader share its pose and skinned mesh (game.cpp drawItems).
#include <map>
#include "ai_core.h"
#include "app.h"
#include "mpai.h"
#include "crumb.h"
#include "enemy_types.h"
// CH6: "attacks" joint names, in CharJoint order (as character.cpp kSemanticNames, private there)
static const char* kSemanticNames[] = {"Hips", "Spine", "Spine1", "Spine2", "Neck", "Head", "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand", "RightShoulder", "RightArm", "RightForeArm", "RightHand",
  "LeftUpLeg", "LeftLeg", "LeftFoot", "LeftToeBase", "RightUpLeg", "RightLeg", "RightFoot", "RightToeBase"};
static_assert(sizeof(kSemanticNames) / sizeof(kSemanticNames[0]) == CJ_COUNT, "the CharJoint list changed");
#include <algorithm>
#include "overrides.h"
#include <cmath>
#include <cstdlib>
#include <set>
#include <array>

bool gMechRobot = true;  // mechrobot (Renderer::perf, set each frame by App): AI type Mechs spawns the KR-714 robot soldier (kit 12); off = the melee mech (zombie.json "mech")
bool gHorseKnight = true;   // horseknight (Renderer::perf, set each frame by App): AI type Mounted Knight = Knight 10 on the horse; off = on foot
bool gKnight2 = true;    // knight2 (Renderer::perf, set each frame by App): off = each type's json "legacy" patch applies when the table loads
namespace {
float zLenXZ(vec3 v) { return glm::length(vec2(v.x, v.z)); }
struct ZombieCfg {
  float repath = 0.5f, flinch = 0.6f;
  std::vector<EnemyType> types;   // EN1: [0] the plain zombie
  // PF1 packs and LOD
  bool packs = true; int packMin = 20, packSize = 8; float join = 35, breakOff = 4, spacing = 1.6f, leaderPace = 0.9f, catchUp = 1.25f;
  float sepR = 1.3f, sepGain = 2.0f, align = 0.35f, lost = 18;
  bool lod = true; float animNear = 40, animFar = 100, animHzNear = 15, animHzFar = 7.5f, shareM = 60;
};
bool gZLoaded = false;   // UI3: zombieCfgReload() (the menu's AI type editor) drops the table
const ZombieCfg& zcfg() {
  static ZombieCfg c;
  if (!gZLoaded) {
    gZLoaded = true; c = ZombieCfg{};
    try {
      json j = json::parse(readTextFile(contentDir() / "ai" / "zombie.json"));
      if (fs::exists(contentDir() / "ai" / "zombie_local.json")) {   // local-only extra enemy types (git-ignored), appended to the list
        json lj = json::parse(readTextFile(contentDir() / "ai" / "zombie_local.json"));
        if (!j.contains("types")) j["types"] = json::array();
        for (auto& e : lj.value("types", json::array())) j["types"].push_back(e);
      }
      { const json& ao = aiOverrides(); if (ao.contains("types") && ao["types"].is_object() && j.contains("types"))   // UI3 (h): the user's patches (overrides.h)
          for (auto& e : j["types"]) { std::string id = e.value("id", std::string("zombie")); if (ao["types"].contains(id) && ao["types"][id].is_object()) e.merge_patch(ao["types"][id]); } }
      c.repath = j.value("repath_s", c.repath); c.flinch = j.value("flinch_s", c.flinch);
      auto strs = [](const json& e, const char* k, std::vector<std::string>& v) {   // a name or a list of names
        if (e.contains(k) && e[k].is_string()) v = {e[k].get<std::string>()};
        else if (e.contains(k) && e[k].is_array() && !e[k].empty()) { v.clear(); for (auto& s : e[k]) v.push_back(s.get<std::string>()); } };
      for (auto e : j.value("types", json::array())) {   // EN1 enemy types; a missing field keeps the plain zombie's value
        if (!gKnight2 && e.contains("legacy") && e["legacy"].is_object()) e.merge_patch(e["legacy"]);   // knight2 off: the type as it was
        { const std::string eid = e.value("id", std::string());   // RM1: the fighters; RM2: the alien, alien queen and animatronic (the clown): an old local file
          if (eid.rfind("fighter", 0) == 0 || eid == "alien" || eid == "alien_queen" || eid == "animatronic") { logf("enemy types: '%s' is a removed type: ignored", eid.c_str()); continue; } }
        EnemyType t;
        t.id = e.value("id", t.id); t.name = e.value("name", t.id); t.model = e.value("model", t.model); t.height = e.value("height_m", t.height);
        t.models = {t.model}; strs(e, "models", t.models); t.model = t.models[0];
        t.scale = std::clamp(e.value("scale", t.scale), 0.3f, 100.0f); t.lean = e.value("lean_deg", t.lean);   // giants are drawn many times life size
        t.fallbackScale = std::clamp(e.value("fallback_scale", t.scale), 0.3f, 100.0f); t.fallbackLean = e.value("fallback_lean_deg", t.lean);
        if (e.contains("tint") && e["tint"].is_array() && e["tint"].size() == 3) t.tint = vec3(e["tint"][0].get<float>(), e["tint"][1].get<float>(), e["tint"][2].get<float>());
        t.hp = std::max(1.0f, e.value("hp", t.hp)); t.damage = e.value("damage", t.damage); t.reach = e.value("reach_m", t.reach); t.lunge = e.value("lunge_m", t.lunge);
        t.cooldown = e.value("cooldown_s", t.cooldown); t.windup = e.value("windup_s", t.windup); t.attackRate = std::clamp(e.value("attack_rate", t.attackRate), 0.25f, 4.0f);
        t.walk = e.value("walk_mps", t.walk); t.run = e.value("run_mps", t.run); t.runWithin = e.value("run_within_m", t.runWithin);
        t.weight = std::max(0.0f, e.value("weight", t.weight)); t.zoneScale = std::clamp(e.value("hit_zone_scale", t.zoneScale), 0.3f, 4.0f);
        t.groundShake = std::clamp(e.value("ground_shake", t.groundShake), 0.0f, 1.0f);
        for (auto& a : e.value("attacks", json::array())) {   // CH6
          EnemyType::AttackSpec x; x.clip = a.value("clip", ""); std::string jn = a.value("joint", "");
          for (int q = 0; q < CJ_COUNT; ++q) if (jn == kSemanticNames[q]) x.joint = q;
          x.bone = a.value("bone", std::string());   // KN8
          if (a.contains("at") && a["at"].is_array() && a["at"].size() == 3) { x.at = vec3(a["at"][0].get<float>(), a["at"][1].get<float>(), a["at"][2].get<float>()); x.hasAt = !x.bone.empty(); }
          x.extend = a.value("extend", 0.0f); x.radius = a.value("radius", 0.0f); x.damage = a.value("damage", -1.0f); x.weight = std::max(0.0f, a.value("weight", 1.0f));
          if (a.contains("range") && a["range"].is_array() && a["range"].size() == 2) { x.rangeMin = a["range"][0]; x.rangeMax = a["range"][1]; }
          if (a.contains("window") && a["window"].is_array() && a["window"].size() == 2) { x.pre = a["window"][0]; x.post = a["window"][1]; }
          x.reach = a.value("reach", 0.0f); x.leap = a.value("leap", false); x.victims = a.value("victims", 0); x.swing = a.value("swing", 0.0f);
          x.faceDeg = a.value("face_deg", 0.0f);   // HK1
          if (a.contains("shoot") && a["shoot"].is_object()) { const json& sh = a["shoot"]; x.shoot = true; x.speed = std::max(5.0f, sh.value("speed", x.speed)); x.spreadDeg = std::max(0.0f, sh.value("spread_deg", x.spreadDeg)); x.ammo = sh.value("ammo", std::string("crossbow_bolt")); x.massG = sh.value("mass_g", x.massG); }   // AR1
          if (!x.clip.empty()) t.attackSpecs.push_back(x);
        }
        t.attackDust = e.value("attack_dust", t.attackDust); t.tankDamage = e.value("tank_damage", t.hp >= 500); t.headMult = std::max(0.0f, e.value("head_mult", t.headMult));
        t.explosiveMult = std::max(0.0f, e.value("explosive_mult", t.explosiveMult)); t.blockR = std::clamp(e.value("block_radius", t.blockR), 0.0f, 0.5f); t.locoRate = std::clamp(e.value("walk_rate", t.locoRate), 0.25f, 4.0f); t.stompArea = std::clamp(e.value("stomp_area", t.stompArea), 0.0f, 1.0f);
        if (e.contains("horse_death") && e["horse_death"].is_object()) {   // HK2
          const json& h = e["horse_death"]; t.horseDeathClip = h.value("clip", ""); t.footType = h.value("foot_type", "");
          if (h.contains("foot_at") && h["foot_at"].is_array() && h["foot_at"].size() == 3) t.footAt = vec3(h["foot_at"][0].get<float>(), h["foot_at"][1].get<float>(), h["foot_at"][2].get<float>());
          strs(h, "kinds", t.horseKinds); strs(h, "hide", t.corpseHide);
        }
        t.horseHp = std::max(0.0f, e.value("horse_hp", 0.0f));   // HK3
        for (auto& z : e.value("horse_zones", json::array())) if (z.is_array() && z.size() == 3) t.horseZones.push_back({z[0].get<std::string>(), z[1].get<std::string>(), z[2].get<float>()});
        t.capsuleH = e.value("capsule_height_m", t.capsuleH); t.capsuleR = e.value("capsule_radius_m", t.capsuleR);
        t.climbM = e.value("climb_m", t.climbM); t.stepGap = e.value("step_gap_m", t.stepGap);
        t.knock = e.value("knockback_mps", t.knock); t.staggerMin = e.value("stagger_min_damage", t.staggerMin); t.staggerS = e.value("stagger_s", t.staggerS);
        json sd = e.value("sounds", json::object()); t.sndAttack = sd.value("attack", ""); t.sndIdle = sd.value("idle", ""); t.sndStep = sd.value("step", "");
        t.sndContact = sd.value("contact", ""); t.sndDeath = sd.value("death", ""); t.sndLoop = sd.value("loop", "");
        t.sndWindup = sd.value("windup", ""); t.voicePitch = std::clamp(e.value("voice_pitch", 1.0f), 0.5f, 2.0f);
        json cl = e.value("clips", json::object());
        strs(cl, "walk", t.walkClips); strs(cl, "idle", t.idleClips); strs(cl, "attack", t.attackClips);
        t.runClip = cl.value("run", t.runClip); t.climbClip = cl.value("climb", t.climbClip); strs(cl, "damage", t.damageClips); strs(cl, "death", t.deathClips);
        c.types.push_back(t);
      }
      json p = j.value("pack", json::object());
      c.packs = p.value("on", c.packs); c.packMin = p.value("min_zombies", c.packMin); c.packSize = std::clamp(p.value("size", c.packSize), 2, 16);
      c.join = p.value("join_m", c.join); c.breakOff = p.value("break_off_m", c.breakOff); c.spacing = p.value("slot_spacing_m", c.spacing);
      c.leaderPace = p.value("leader_pace", c.leaderPace); c.catchUp = p.value("catch_up", c.catchUp);
      c.sepR = p.value("separation_m", c.sepR); c.sepGain = p.value("separation_gain", c.sepGain); c.align = p.value("alignment", c.align); c.lost = p.value("lost_m", c.lost);
      json l = j.value("lod", json::object());
      c.lod = l.value("on", c.lod); c.animNear = l.value("anim_near_m", c.animNear); c.animFar = l.value("anim_far_m", c.animFar);
      c.animHzNear = std::max(1.0f, l.value("anim_hz_near", c.animHzNear)); c.animHzFar = std::max(1.0f, l.value("anim_hz_far", c.animHzFar)); c.shareM = l.value("share_pose_m", c.shareM);
    } catch (std::exception& e) { logf("zombie: content/ai/zombie.json: %s", e.what()); }
    if (c.types.empty()) c.types.push_back(EnemyType{});   // no table: the plain zombie
    if (const char* e = std::getenv("NMK_ZOMBIE_PACKS")) c.packs = *e != '0';   // A/B measurements
    if (const char* e = std::getenv("NMK_ZOMBIE_LOD")) c.lod = *e != '0';
  }
  return c;
}
// the body slam's footprint: an oval about her upper body (0.36 x 0.2 of her height, the long axis along f) centred at c;
// returns the normalised radius (< 1 inside)
float inSlam(vec3 p, vec3 c, vec3 f, const EnemyType& e, float scale) {
  vec3 d = p - c; d.y = 0; float A = 0.18f * e.height * scale, B = 0.10f * e.height * scale;
  float u = glm::dot(d, f) / A, v = (d.x * f.z - d.z * f.x) / B;
  return std::sqrt(u * u + v * v);
}
bool inReach(const Game& G, const Soldier& z, const Soldier& v, float reach) {
  return v.alive && zLenXZ(v.feet - z.feet) < reach && std::fabs(v.feet.y - z.feet.y) < 1.2f && G.lineOfSight(G.chest(z), G.chest(v));
}
const EnemyType& typeOf(const Soldier& s) { return enemyType(s.etype); }
const EnemyType& clipsOf(const Soldier& s) { return s.eFallback ? enemyType(0) : enemyType(s.etype); }   // clip names: the zombie's on the fallback rig
int attackClip(const CharModel& m, const EnemyType& e, int k) { return findClip(m, e.attackClips[k % e.attackClips.size()].c_str()); }
// strike wind-up: the attack clip's swing frame at attack_rate (the json fallback without clips)
float windupOf(const Game& G, const Soldier& s) {
  const EnemyType& e = typeOf(s);
  if (s.model < 0 || s.model >= (int)G.models.size()) return e.windup;
  int k = attackClip(*G.models[s.model], clipsOf(s), s.meleeClip);
  if (k >= 0) { const auto& ac = clipsOf(s).attackClips; for (auto& a : e.attackSpecs) if (a.swing > 0 && a.clip == ac[s.meleeClip % ac.size()]) return a.swing * G.models[s.model]->clips[k].dur / e.attackRate; }   // CH6: its own blow moment
  return k >= 0 && G.models[s.model]->clips[k].swing > 0 ? G.models[s.model]->clips[k].swing / e.attackRate : e.windup;
}
// CH6: an attack's strike, from the clip's analysis (the fastest hand and where it is at the swing) and the type's "attacks"
struct Strike { int bone = -1; vec3 at{0}; int joint = CJ_RHAND; float extend = 0, radius = 0.5f, damage = 0, lo = 0, hi = 0, weight = 1, pre = 0.15f, post = 0.1f, dist = 1; bool leap = false; int victims = 0;
  bool shoot = false; float speed = 55, spreadDeg = 1, massG = 60, faceDeg = 0; std::string ammo; };   // AR1; HK1 faceDeg
Strike strikeOf(const Game& G, const Soldier& s, int i) {
  const EnemyType& e = typeOf(s); const EnemyType& ce = clipsOf(s); Strike r;
  r.radius = std::max(0.45f, 0.12f * e.height * e.scale); r.damage = e.damage; r.dist = e.reach;
  const CharModel& m = *G.models[s.model]; int k = attackClip(m, ce, i);
  if (k >= 0 && m.clips[k].swingJoint >= 0) { r.joint = m.clips[k].swingJoint; vec3 a = m.clips[k].swingAt; r.dist = std::max(0.6f, glm::length(vec2(a.x, a.z)) * s.eScale); }
  for (auto& a : e.attackSpecs) if (a.clip == ce.attackClips[i % ce.attackClips.size()]) {
    if (a.joint >= 0) r.joint = a.joint; r.extend = a.extend; if (a.radius > 0) r.radius = a.radius; if (a.damage >= 0) r.damage = a.damage;
    r.lo = a.rangeMin; r.hi = a.rangeMax; r.weight = a.weight; r.pre = a.pre; r.post = a.post; r.leap = a.leap; r.victims = a.victims;
    r.shoot = a.shoot; r.speed = a.speed; r.spreadDeg = a.spreadDeg; r.massG = a.massG; r.ammo = a.ammo; r.faceDeg = a.faceDeg;
    if (a.extend > 0) r.dist += a.extend;
    if (a.reach > 0) r.dist = a.reach;
    if (a.hasAt) for (int k = 0; k < (int)m.jointNode.size(); ++k) if (m.nodeName[m.jointNode[k]] == a.bone) {   // KN8: on the weapon itself
      r.bone = k; r.at = vec3(m.nodeWorld[m.jointNode[k]] * vec4(a.at, 1));   // the point at rest, model space
      if (a.reach <= 0) r.dist = std::max(r.dist, glm::length(r.at - m.jointHead[m.semantic[CJ_RHAND]]) * s.eScale);   // the swing's reach: at least the weapon's
      break;
    }
  }
  if (r.hi <= 0) { r.lo = 0; r.hi = std::max({r.dist + r.radius + 0.3f, e.reach, e.lunge}); }   // within the swing's reach (or the type's lunge)
  return r;
}
// where the strike sphere is now: the joint, carried `extend` metres on along its limb (forearm -> hand, neck -> head, shin -> foot)
vec3 strikePoint(const Soldier& s, const Strike& r) {
  if (r.bone >= 0 && r.bone < (int)s.pose.delta.size()) return vec3(s.world * s.pose.delta[r.bone] * vec4(r.at, 1));   // KN8: posed = delta * rest
  auto W = [&](int j) { return vec3(s.world * vec4(s.pose.joint[j], 1)); };
  vec3 p = W(r.joint);
  int par = r.joint == CJ_LHAND ? CJ_LFOREARM : r.joint == CJ_RHAND ? CJ_RFOREARM : r.joint == CJ_HEAD ? CJ_NECK : r.joint == CJ_LFOOT ? CJ_LLEG : r.joint == CJ_RFOOT ? CJ_RLEG : -1;
  if (r.extend > 0 && par >= 0) { vec3 d = p - W(par); if (glm::length(d) > 1e-4f) p += glm::normalize(d) * r.extend; }
  return p;
}
// distance from a point to a soldier's body (a 0.3 m capsule from the feet to its height)
// TK1: a target crewing an armoured tank sits inside its hull: reach and strike distances are measured to the hull side
// AR1: a ranged attack's projectile, at the swing: from the left (bow) hand toward the target's chest, aimed over by its
// drop at that range, a little spread; damage from the ammo's weapon row (the crossbow bolt's: DMG1), owner the shooter
static vec3 arrowAim(vec3 from, vec3 aim, float speed) {   // over the drop at that range (flat fire: a lob's small extra time ignored)
  float tf = glm::length(aim - from) / speed; aim.y += 0.5f * 9.81f * tf * tf; return glm::normalize(aim - from);
}
static void loose(Game& G, Soldier& s, const Soldier& t, const Strike& r) {
  vec3 from = vec3(s.world * vec4(s.pose.joint[CJ_LHAND], 1)), e = G.eye(s);
  Physics::Hit hb; if (Physics::castRayEx(e, from - e, glm::length(from - e) + 1e-3f, hb)) from = e;   // the hand in a wall: from the eye
  vec3 d = arrowAim(from, G.eye(t) - vec3(0, 0.3f, 0), r.speed), rt = glm::normalize(glm::cross(d, vec3(0, 1, 0))), up = glm::cross(rt, d);
  float a = G.rnd(s) * 6.2831853f, k = std::sqrt(G.rnd(s)) * std::tan(radians(r.spreadDeg));
  d = glm::normalize(d + (rt * std::cos(a) + up * std::sin(a)) * k);
  G.app.fireProjectile(from, d, r.speed, PROJ_BULLET, 0, ~0ull, -1, r.ammo);   // tracer kind -1: drawn as an arrow shaft (Fx::bullet)
  Projectile& p = G.app.projectiles.back(); p.mass = r.massG * 0.001f; p.owner = s.slot;
  if (G.app.audio.hasEvent("archer_release")) G.app.audio.play("archer_release", from, 1.0f, 0.96f + 0.08f * G.rnd(s));   // the twang (pf3's bow recording; its event sets range and voices)
}
static float armourGap(Game& G, const Soldier& o) {
  if (!o.localPlayer || !G.app.tanks.occupied) return 0;
  const TankRT* t = G.app.tanks.get(G.app.tanks.occupied);
  if (!t || !t->def || t->def->car || t->def->heli || t->wrecked) return 0;
  float hx = 0, hz = 0; for (auto& b : t->def->hullBoxes) { hx = std::max(hx, std::fabs(b.c.x) + b.h.x); hz = std::max(hz, std::fabs(b.c.z) + b.h.z); }
  return std::min(hx, hz);   // shortcut: the narrower half-extent (the side), not the true box distance; fine for strikes at the flanks and ends
}
float bodyDist(const Soldier& o, vec3 p) {
  float h = 1.75f * (o.zombie ? o.eBody : 1.0f); vec3 a = o.feet + vec3(0, 0.25f, 0), b = o.feet + vec3(0, h, 0);
  float u = std::clamp((p.y - a.y) / std::max(1e-3f, b.y - a.y), 0.0f, 1.0f); return glm::length(p - (a + (b - a) * u)) - 0.3f * (o.zombie ? std::max(1.0f, o.eBody) : 1.0f);
}
static std::map<int, std::vector<int>> gStruck;   // slot -> who this strike has hit (once each)
static std::map<int, bool> gWindVoiced;            // SND4: slot -> this attack opened with the type's windup sound
BotMind* mindAt(AiCore& A, int slot) { return slot >= 0 && slot < (int)A.minds.size() ? A.minds[slot].get() : nullptr; }
bool packMate(AiCore& A, const Soldier& o, int pack) { if (pack < 0) return false; BotMind* m = mindAt(A, o.slot); return m && m->life == o.lifeId && m->pack == pack; }
bool packCompatible(const Game& G, const Soldier& a, const Soldier& b) { return ffaMode(G.match.mode()) || a.team == b.team; }

// every 0.5 s: drop the dead, promote a new leader, put the packless into the nearest pack with room, merge small packs
void maintainPacks(Game& G, AiCore& A) {
  const ZombieCfg& c = zcfg();
  A.packT = G.t + 0.5;
  std::vector<int> zs; for (auto& s : G.sold) if (s.alive && s.zombie && !s.human && s.etype == 0) zs.push_back(s.slot);   // EN3: heavies never pack
  bool on = c.packs && (int)zs.size() > c.packMin;
  auto& P = A.packs;
  for (int p = 0; p < (int)P.size(); ++p) {
    auto& mem = P[p].members;
    if (mem.empty()) continue;
    int lead = mem[0]; vec3 at = G.sold[lead].feet;
    mem.erase(std::remove_if(mem.begin(), mem.end(), [&](int k) { Soldier& s = G.sold[k]; return !on || !s.alive || A.mind(s).pack != p; }), mem.end());
    if (!mem.empty() && mem[0] != lead) {   // the leader died: the member nearest to where it fell takes over
      auto it = std::min_element(mem.begin(), mem.end(), [&](int a, int b) { return glm::length(G.sold[a].feet - at) < glm::length(G.sold[b].feet - at); });
      std::iter_swap(mem.begin(), it);
    }
  }
  auto renumber = [&] {
    P.erase(std::remove_if(P.begin(), P.end(), [](const AiCore::ZPack& z) { return z.members.empty(); }), P.end());
    for (int p = 0; p < (int)P.size(); ++p) for (size_t k = 0; k < P[p].members.size(); ++k) { BotMind& m = A.mind(G.sold[P[p].members[k]]); m.pack = p; m.follower = k > 0; }
  };
  renumber();
  if (!on) {
    for (int k : zs) { BotMind& m = A.mind(G.sold[k]); if (m.pack >= 0 || m.follower) { m.pack = -1; m.follower = false; m.speedScale = 1; m.flockPush = vec3(0); } }
    return;
  }
  for (int k : zs) {
    Soldier& s = G.sold[k]; BotMind& m = A.mind(s);
    if (m.pack >= 0) continue;
    int best = -1; float bd = c.join;
    for (int p = 0; p < (int)P.size(); ++p) {
      const Soldier& L = G.sold[P[p].members[0]];
      float d = glm::length(L.feet - s.feet);
      if ((int)P[p].members.size() < c.packSize && d < bd && packCompatible(G, s, L)) { bd = d; best = p; }
    }
    if (best < 0) { P.push_back({}); best = (int)P.size() - 1; }
    P[best].members.push_back(k); m.pack = best; m.follower = P[best].members.size() > 1;
  }
  // small packs fold into a neighbour with room
  for (int p = 0; p < (int)P.size(); ++p) {
    if (P[p].members.empty() || (int)P[p].members.size() * 2 > c.packSize) continue;
    const Soldier& L = G.sold[P[p].members[0]];
    int best = -1; float bd = c.join;
    for (int q = 0; q < (int)P.size(); ++q) {
      if (q == p || P[q].members.empty() || P[q].members.size() + P[p].members.size() > (size_t)c.packSize) continue;
      const Soldier& Q = G.sold[P[q].members[0]];
      float d = glm::length(Q.feet - L.feet);
      if (d < bd && packCompatible(G, L, Q)) { bd = d; best = q; }
    }
    if (best >= 0) { for (int k : P[p].members) P[best].members.push_back(k); P[p].members.clear(); }
  }
  renumber();
  for (auto& z : P) { BotMind& m = A.mind(G.sold[z.members[0]]); m.speedScale = z.members.size() > 1 ? c.leaderPace : 1.0f; m.flockPush = vec3(0); }
}

// EN3: a heavy's own target and path. The closest hostile it can reach, re-chosen about every second (a new one must be
// clearly closer: 0.8 x the distance, 1 m less); a target whose path failed or stops short is skipped for 6 s. Path
// requests: every 0.5 s near, up to 1.5 s at 80 m+, at most kHeavyPathsPerFrame per frame across all heavies, far ones
// at a lower priority (the path service starts the most urgent first, ai_path.cpp).
constexpr int kHeavyPathsPerFrame = 3;
const Soldier* heavyStep(Game& G, AiCore& A, Soldier& s, BotMind& m) {
  static uint64_t frame = ~0ull; static int asked = 0;
  if (G.steps != frame) { frame = G.steps; asked = 0; }
  Brain& b = s.br;
  auto ok = [&](int k) { return k >= 0 && k < (int)G.sold.size() && G.sold[k].alive && Game::hostile(s, G.sold[k]); };
  const Soldier* cur = ok(b.target) ? &G.sold[b.target] : nullptr;
  if (cur && m.badTarget != cur->slot && G.t - m.pathFailAt < 1.5 && m.pathFails >= 1) { m.badTarget = cur->slot; m.badUntil = G.t + 6; }
  if (cur && m.badTarget != cur->slot && m.hasPath && m.partial) {
    const float* e = m.corridor->getTarget();
    if (zLenXZ(vec3(e[0], e[1], e[2]) - cur->feet) > 3.0f) { m.badTarget = cur->slot; m.badUntil = G.t + 6; }
  }
  bool bad = cur && m.badTarget == cur->slot && G.t < m.badUntil;
  if (!cur || bad || G.t >= m.tgtT) {
    m.tgtT = G.t + 1.0 + 0.25 * ((s.slot * 37) % 8) / 8.0;   // staggered over the heavies
    const Soldier* pick = nullptr; float pd = 1e9f;
    for (auto& o : G.sold) if (o.alive && Game::hostile(s, o) && !(o.slot == m.badTarget && G.t < m.badUntil)) { float d = glm::length(o.feet - s.feet); if (d < pd) { pd = d; pick = &o; } }
    float cd = cur && !bad ? glm::length(cur->feet - s.feet) : 1e9f;
    if (pick && (!cur || bad || pd < 0.8f * cd - 1.0f)) cur = pick;
    if (!pick && bad) cur = nullptr;
  }
  b.target = cur ? cur->slot : -1;
  if (!cur) { m.speed = SPD_STOP; b.why = "heavy: no one left"; return nullptr; }
  m.speed = SPD_ZOMBIE; b.why = "heavy: pursuing alone";
  float d = zLenXZ(cur->feet - s.feet);
  // EN4: no progress for 2 s (down a trench, a crater, against a low wall) and not striking: climb the nearest lip
  if (m.trav.on || zLenXZ(s.feet - m.hAnchor) > 0.75f || d < typeOf(s).reach + 1.5f || s.meleeWind >= 0 || G.t < s.staggerUntil) { m.hAnchor = s.feet; m.hAnchorT = G.t; }
  else if (G.t - m.hAnchorT > 2.0 && G.t >= m.hClimbTry) {
    m.hClimbTry = G.t + 1.5;
    if (A.heavyClimbOut(s, m, cur->feet)) { m.hAnchor = s.feet; m.hAnchorT = G.t; b.why = "heavy: climbing out"; return cur; }
  }
  if (G.t >= b.repathT && (!m.hasPath || zLenXZ(cur->feet - m.goal) > 0.75f) && asked < kHeavyPathsPerFrame) {
    A.requestPath(s, cur->feet, 0.5f, 6.0f - std::min(4.0f, d / 40.0f), true); ++asked;
    b.repathT = (float)G.t + std::clamp(0.5f + d / 80.0f, 0.5f, 1.5f);
  }
  return cur;
}

// a follower's local move: slide the corridor's end along the mesh toward `goal` (no path query); false when it cannot get near
bool slideTo(Game& G, Soldier& s, BotMind& m, vec3 goal, float near) {
  dtNavMeshQuery* q = G.nav.query(); dtNavMesh* mesh = G.nav.mesh();
  if (!q || !mesh) return false;
  const dtQueryFilter* f = &G.nav.defaultFilter();
  dtPathCorridor& c = *m.corridor;
  if (!m.hasPath || !mesh->isValidPolyRef(c.getFirstPoly()) || m.wantPath || m.pendingReq) {
    if (m.wantPath || m.pendingReq) return true;   // its own path is on the way
    NavRef r = 0; vec3 on;
    if (!G.nav.closest(s.feet + vec3(0, 0.3f, 0), vec3(1.0f, 1.6f, 1.0f), r, on, f)) return false;
    c.reset(r, &on.x); m.hasPath = true; m.partial = false;
  }
  c.moveTargetPosition(&goal.x, q, f);
  m.goal = goal;
  return zLenXZ(vec3(c.getTarget()[0], c.getTarget()[1], c.getTarget()[2]) - goal) < near;
}

void followerStep(Game& G, AiCore& A, Soldier& s, BotMind& m, const Soldier& L) {
  const ZombieCfg& c = zcfg();
  Brain& b = s.br;
  auto& mem = A.packs[m.pack].members;
  int idx = (int)(std::find(mem.begin(), mem.end(), s.slot) - mem.begin());
  if (G.t >= m.flockT) {
    m.flockT = G.t + 0.1;
    // break off: the closest hostile within reach of a lunge (not a pack mate)
    const Soldier* v = nullptr; float vd = c.breakOff;
    A.forNear(s.feet, c.breakOff, [&](const Soldier& o) { if (o.alive && Game::hostile(s, o) && !packMate(A, o, m.pack)) { float d = zLenXZ(o.feet - s.feet); if (d < vd && std::fabs(o.feet.y - s.feet.y) < 2.0f) { vd = d; v = &o; } } });   // AI2: grid, not every soldier
    // the wedge slot behind the leader, in its heading
    vec3 fwd = zLenXZ(L.vel) > 0.5f ? vec3(L.vel.x, 0, L.vel.z) : (mindAt(A, L.slot) ? mindAt(A, L.slot)->moveDir : vec3(0, 0, 1));
    fwd.y = 0; fwd = zLenXZ(fwd) > 1e-3f ? glm::normalize(fwd) : vec3(0, 0, 1);
    vec3 right(-fwd.z, 0, fwd.x);
    int row = (idx + 1) / 2; float side = (idx & 1) ? 1.0f : -1.0f;
    vec3 slot = L.feet - fwd * (row * c.spacing) + right * (side * row * c.spacing * 0.75f);
    vec3 goal = v ? v->feet : slot;
    b.target = v ? v->slot : L.br.target;
    float dL = zLenXZ(L.feet - s.feet);
    bool ownPath = G.t < m.ownPathT;   // cut off: following its own path to the leader for a while
    if (!ownPath) {
      bool ok = slideTo(G, s, m, goal, 3.0f) || (!v && slideTo(G, s, m, L.feet, 3.0f));
      if ((!ok && dL > 5.0f) || dL > c.lost) { A.requestPath(s, L.feet, 2.0f, 5.0f, true); m.ownPathT = G.t + 2.5; }
    }
    m.goalRadius = v ? 0.5f : 0.8f;
    // pace: catch up when behind the slot, settle when on it; full speed for a strike
    float d = zLenXZ(goal - s.feet);
    m.speedScale = v || ownPath ? 1.0f : std::clamp(0.35f + d / 3.0f, 0.35f, c.catchUp);
    // separation from pack mates, alignment with the leader
    vec3 push(0);
    for (int k : mem) {
      if (k == s.slot) continue;
      vec3 dd = s.feet - G.sold[k].feet; dd.y = 0; float l = glm::length(dd);
      if (l < c.sepR && l > 1e-3f) push += dd / l * (1.0f - l / c.sepR) * c.sepGain;
    }
    if (d < 3.0f && !v) push += vec3(L.vel.x - s.vel.x, 0, L.vel.z - s.vel.z) * c.align;
    float pl = glm::length(push), cap = typeOf(s).run * 0.6f;
    m.flockPush = pl > cap ? push / pl * cap : push;
  }
}
}   // namespace

const std::vector<EnemyType>& enemyTypes() { return zcfg().types; }
void zombieCfgReload() { gZLoaded = false; }   // UI3: from App::reloadGameplayDefs (the menu only, never mid-match)
const EnemyType& enemyType(int k) { const auto& T = zcfg().types; return T[std::clamp(k, 0, (int)T.size() - 1)]; }
int enemyTypeIndex(const std::string& id) { const auto& T = zcfg().types; for (int k = 0; k < (int)T.size(); ++k) if (T[k].id == id) return k; return -1; }
int enemyTypePick(uint32_t seed) {
  const auto& T = zcfg().types;
  float tot = 0; for (auto& t : T) tot += t.weight;
  if (tot <= 0) return 0;
  float u = ((seed * 2654435761u) >> 8) / 16777216.0f * tot;   // deterministic per slot: host and LAN clients agree
  for (int k = 0; k < (int)T.size(); ++k) { u -= T[k].weight; if (u < 0) return k; }
  return (int)T.size() - 1;
}
int enemyTypeHeavyPick(uint32_t seed, float trollShare) {
  const auto& T = zcfg().types;
  int br = -1, st = -1; float tot = 0;
  for (int k = 1; k < (int)T.size(); ++k) { if (T[k].id == "brute") br = k; if (T[k].id == "stalker") st = k; tot += T[k].weight; }
  float u = ((seed * 2246822519u + 0x9E3779B9u) >> 8) / 16777216.0f;   // deterministic per slot (host and clients agree)
  if (br > 0 && st > 0) return u < trollShare ? br : st;
  if (T.size() < 2) return 0;
  u *= tot; for (int k = 1; k < (int)T.size(); ++k) { u -= T[k].weight; if (u < 0) return k; }
  return (int)T.size() - 1;
}
float aiZombieSpeed() { return enemyType(0).run; }
static int gBlows = 0;
static std::vector<ZombieBlow> gBlowLog;
int aiZombieBlows() { return gBlows; }
// SND1: a type's "contact" sound where its blow landed (only on a hit). shortcut: host / single player only
void contactSound(Game& G, const EnemyType& e, const Soldier& v) { if (!e.sndContact.empty()) G.app.audio.play(e.sndContact.c_str(), v.feet + vec3(0, 1.3f, 0), 1.0f); }
// SND2: a type's "loop" sound on each living one of it, the nearest three to the listener within 60 m (a pack does not stack),
// each started at its own offset into the loop. Run once per game step from aiZombieStep; aiZombieLoopsStop at match end.
// shortcut: host / single player only (LAN clients run no zombie brains)
static std::map<int, std::pair<int, uint32_t>> gLoops;   // slot -> loop handle, life
void aiZombieLoopsStop(App& app) { for (auto& [k, v] : gLoops) app.audio.loopStop(v.first); gLoops.clear(); }
static void enemyLoops(Game& G) {
  static double last = -1; if (G.t == last || (last >= 0 && G.t > last && G.t - last < 0.1)) return; last = G.t;
  vec3 ear = G.app.eyePos(); std::vector<std::pair<float, int>> near;
  for (auto& s : G.sold) if (s.zombie && s.alive && !enemyType(s.etype).sndLoop.empty()) { float d = glm::length(s.feet - ear); if (d < 60) near.push_back({d, s.slot}); }
  std::sort(near.begin(), near.end()); if (near.size() > 3) near.resize(3);
  for (auto it = gLoops.begin(); it != gLoops.end();) {   // stop those no longer among the nearest (or dead, or a new life)
    bool keep = false; for (auto& n : near) keep |= n.second == it->first && G.sold[it->first].lifeId == it->second.second;
    if (!keep) { G.app.audio.loopStop(it->second.first); it = gLoops.erase(it); } else ++it;
  }
  for (auto& n : near) {
    Soldier& s = G.sold[n.second]; const EnemyType& e = enemyType(s.etype); vec3 at = s.feet + vec3(0, 0.6f * e.height * e.scale, 0);
    auto it = gLoops.find(s.slot);
    if (it == gLoops.end()) { int h = G.app.audio.loopStart(e.sndLoop, at, 0.8f, 0.95f + 0.1f * (s.slot % 7) / 6.0f, G.rnd(s)); if (h >= 0) gLoops[s.slot] = {h, s.lifeId}; }   // own offset and pitch: a pack is not in unison
    else G.app.audio.loopSet(it->second.first, at, 0.8f, 0.95f + 0.1f * (s.slot % 7) / 6.0f);
  }
}
const std::vector<ZombieBlow>& aiZombieBlowLog() { return gBlowLog; }

int aiZombieModel(Game& G, int type, int variant) {
  const EnemyType& t = enemyType(type);
  const std::string& model = t.models.empty() ? t.model : t.models[(size_t)std::max(0, variant) % t.models.size()];
  std::string key = model + "@" + std::to_string(t.height);
  auto it = G.modelIdx.find(key); if (it != G.modelIdx.end()) return it->second;
  static std::set<std::string> failed; if (failed.count(key)) return -1;
  auto cm = std::make_unique<CharModel>(); std::string e;
  if (!cm->load(contentDir() / "characters" / (model + ".glb"), e, t.height)) {
    failed.insert(key); logf("zombie: model %s: %s", model.c_str(), e.c_str());
    return variant ? aiZombieModel(G, type, 0) : -1;   // a missing variant wears the type's first model
  }
  G.modelIdx[key] = (int)G.models.size(); G.models.push_back(std::move(cm));
  return G.modelIdx[key];
}
void aiZombieAssign(Game& G, Soldier& s) {
  const EnemyType& t = enemyType(s.etype);
  int m = aiZombieModel(G, s.etype, s.evariant);
  bool own = m >= 0 && findClip(*G.models[m], t.walkClips[0]) >= 0;   // clips usable: at least its walk
  if (!own && s.etype != 0) { m = aiZombieModel(G, 0); logf("zombie: type %s falls back to the zombie rig", t.id.c_str()); }
  if (m >= 0) s.model = m;
  s.eFallback = !own && s.etype != 0;
  s.eScale = s.eFallback ? t.fallbackScale : t.scale; s.eLean = s.eFallback ? t.fallbackLean : t.lean;
  s.eBody = s.eFallback ? t.fallbackScale : t.scale * t.height / 1.85f;   // chest / eye heights and hit sphere vs the 1.85 m zombie
}
void aiZombieStep(Game& G, Soldier& s, float dt) {
  enemyLoops(G);   // SND2
  AiCore* A = aiCore();
  if (!A) return;
  const ZombieCfg& c = zcfg();
  const EnemyType& e = typeOf(s);
  if (G.t >= A->packT) { crumb("zombie: packs"); maintainPacks(G, *A); }
  BotMind& m = A->mind(s);
  Brain& b = s.br;
  m.posture = Physics::STAND; m.action = A_ADVANCE; m.cover = -1;
  const bool heavy = s.etype > 0;   // EN3: a heavy paths alone
  if (heavy && (m.follower || m.pack >= 0)) { m.follower = false; m.pack = -1; m.flockPush = vec3(0); m.speedScale = 1; }
  const Soldier* L = !heavy && m.follower && m.pack >= 0 && m.pack < (int)A->packs.size() ? &G.sold[A->packs[m.pack].members[0]] : nullptr;
  const Soldier* best = nullptr;
  crumb("zombie: target");
  if (heavy) best = heavyStep(G, *A, s, m);
  else if (L && L->alive && L->slot != s.slot) {
    followerStep(G, *A, s, m, *L);
    best = b.target >= 0 && b.target < (int)G.sold.size() && G.sold[b.target].alive && zLenXZ(G.sold[b.target].feet - s.feet) < c.breakOff ? &G.sold[b.target] : nullptr;
    m.speed = SPD_ZOMBIE; b.why = best ? "zombie: pack, striking" : "zombie: pack, following";
  } else {
    if (m.follower) { m.follower = false; m.flockPush = vec3(0); m.speedScale = 1; }   // its leader fell: on its own until the next pack pass
    // the closest hostile, human or bot, wherever it is (no perception: zombies always know); never a pack mate
    // AI2: re-picked 4x a second (staggered), not every step: a full scan per leader / lone zombie per step is O(n^2) at 1000
    const Soldier* cur = b.target >= 0 && b.target < (int)G.sold.size() && G.sold[b.target].alive ? &G.sold[b.target] : nullptr;
    if (!cur || G.t >= m.tgtT) {
      m.tgtT = G.t + 0.25 + 0.1 * ((s.slot * 37) % 8) / 8.0;
      float bd = 1e9f;
      for (auto& o : G.sold) if (o.alive && Game::hostile(s, o) && !packMate(*A, o, m.pack)) { float d = glm::length(o.feet - s.feet); if (d < bd) { bd = d; best = &o; } }
    } else best = cur;
    b.target = best ? best->slot : -1;
    if (best) {
      m.speed = SPD_ZOMBIE;
      b.why = m.pack >= 0 ? "zombie: leading a pack" : "zombie: pursuing";
      if (G.t >= b.repathT && (!m.hasPath || zLenXZ(best->feet - m.goal) > 0.75f)) { A->requestPath(s, best->feet, 0.5f, 6.0f, true); b.repathT = (float)G.t + c.repath; }
    } else { m.speed = SPD_STOP; b.why = "zombie: no one left"; }
  }
  // EN1: its own pace: run near the target, walk (lumber) beyond run_within_m; a stagger stops it
  m.zSpeed = L || (best && zLenXZ(best->feet - s.feet) < e.runWithin) ? e.run : e.walk;   // a pack follower keeps the pack's pace
  if (G.t < s.staggerUntil) { m.speed = SPD_STOP; b.why = "zombie: staggered"; }
  if (best || L) {
    vec3 d = best ? best->feet - s.feet : vec3(0);
    const bool locked = s.meleeWind >= 0 && s.meleeWind >= windupOf(G, s);   // CH6: from the blow on, the facing holds
    vec3 look = best && (s.meleeWind >= 0 || zLenXZ(d) < std::max(4.0f, e.reach + e.lunge + 2.0f)) ? d : m.moveDir;   // close or striking: face the victim; else the way the path goes (no sideways walk round corners)
    float off = best && s.meleeWind >= 0 && s.meleeClip >= 0 && s.model >= 0 && s.model < (int)G.models.size() ? strikeOf(G, s, s.meleeClip).faceDeg : 0.0f;   // HK1: a side attack faces past the target
    if (zLenXZ(look) > 0.05f && !locked) b.aimYaw = degrees(std::atan2(-look.x, -look.z)) + off;
    b.aimPitch = 0;
  }
  {   // the plain zombie groans now and then when near the listener. shortcut: host / single player only, one voice at a time
    static double nextGroan = 0;
    if (s.etype == 0 && G.t >= nextGroan && glm::length(s.feet - G.app.eyePos()) < 35.0f && G.rnd(s) < 0.02f) { G.app.audio.play("zombie_groan", s.feet + vec3(0, 1.4f, 0), 0.9f); nextGroan = G.t + 5.0 + 6.0 * G.rnd(s); }
    if (nextGroan > G.t + 30.0) nextGroan = 0;   // a new match restarts the clock
  }
  if ((e.id == "brute" || !e.sndIdle.empty()) && s.meleeWind < 0 && glm::length(s.feet - G.app.eyePos()) < 50.0f) {   // the troll's idle noises between attacks. shortcut: host / single player only
    static std::map<int, double> nextIdle;
    double& ni = nextIdle[s.slot];
    if (ni > G.t + 40.0) ni = 0;
    if (G.t >= ni) { if (ni > 0) G.app.audio.play(e.sndIdle.empty() ? "troll_idle" : e.sndIdle.c_str(), s.feet + vec3(0, 2, 0), 0.9f, e.sndIdle.empty() ? 1.0f : e.voicePitch * (0.96f + 0.08f * G.rnd(s))); ni = G.t + 7.0 + 8.0 * G.rnd(s); }
  }
  const bool down = s.anim.a >= 0 && s.model >= 0 && s.anim.a < (int)G.models[s.model]->clips.size() && G.models[s.model]->clips[s.anim.a].name.find("drop") != std::string::npos;   // in the body slam: no standing column to push out of
  if (e.blockR > 0 && !down && G.localSlot >= 0 && G.localSlot < (int)G.sold.size() && G.sold[G.localSlot].alive && !G.app.tanks.occupied) {   // solid to the player: pushed out of the body column. shortcut: local player only (bots, LAN clients and vehicles pass)
    Soldier& lp = G.sold[G.localSlot]; float BR = e.blockR * e.height * e.scale; vec3 d = lp.feet - s.feet; float dy = d.y; d.y = 0; float L = glm::length(d);
    if (L < BR && dy > -2.0f && dy < e.height * e.scale) Physics::setCharacterPos(s.feet + (L > 1e-3f ? d / L : vec3(1, 0, 0)) * BR + vec3(0, dy, 0));
  }
  const bool spiderT = e.id == "stalker", stepT = !e.sndStep.empty();
  if ((spiderT || stepT) && s.speed > 0.5f && glm::length(s.feet - G.app.eyePos()) < 40.0f) {   // a type's own footsteps (the spider's scuttle). shortcut: host / single player only
    static std::map<int, double> nextStep;
    double& ns = nextStep[s.slot];
    if (G.t >= ns || ns > G.t + 5.0) { G.app.audio.play(stepT ? e.sndStep.c_str() : "step_spider", s.feet, 0.7f); ns = G.t + std::clamp(1.2f / std::max(1.0f, s.speed), 0.18f, 0.45f); }   // the spider scuttles: quicker steps
  }
  crumb("zombie: strike");
  // the strike: wind-up (the attack clip up to its swing frame), then the blow lands if the victim is still in reach
  // EN1 lunge: a strike may start lunge_m out; the wind-up closes the gap at whatever speed that takes
  // a body slam ("drop" in the clip name: down on all fours, then flat) lands her upper body on an oval centred where her
  // chest comes down (the clip's swing chest x the draw scale ahead): chosen when two or more hostiles stand in that oval (one, if it is the only attack),
  // the other attacks for anyone within reach
  const auto& ac = clipsOf(s).attackClips; int di = -1; float dropR = 0;
  for (int i = 0; i < (int)ac.size(); ++i) if (ac[i].find("drop") != std::string::npos) { int k = findClip(*G.models[s.model], ac[i]); if (k >= 0) { di = i; vec3 c = G.models[s.model]->clips[k].swingChest; dropR = std::max(1.0f, glm::length(vec2(c.x, c.z)) * s.eScale); } }
  // CH6: the attacks whose strike distance (or json range: a leap's band) fits the target's distance
  const bool strikes = s.model >= 0 && s.model < (int)G.models.size();
  std::vector<int> fit; float wsum = 0; const float gapT = best ? armourGap(G, *best) : 0.0f, dT = best ? std::max(0.0f, zLenXZ(best->feet - s.feet) - gapT) : 1e9f;
  bool shootFit = false;   // AR1: a ranged attack fits: stand, face the target
  if (best && strikes) for (int i = 0; i < (int)ac.size(); ++i) if (i != di) { Strike r = strikeOf(G, s, i); if (dT >= r.lo && dT <= r.hi && r.weight > 0) {
    if (r.shoot && !G.lineOfSight(G.eye(s), G.eye(*best) - vec3(0, 0.3f, 0))) continue;   // AR1: only with a clear line to the chest
    fit.push_back(i); wsum += r.weight; shootFit = shootFit || r.shoot; } }
  if (shootFit && s.meleeWind < 0) { m.speed = SPD_STOP; vec3 d = best->feet - s.feet; if (zLenXZ(d) > 0.05f) b.aimYaw = degrees(std::atan2(-d.x, -d.z)); b.why = "archer: in range, shooting"; }
  const bool inR = strikes ? !fit.empty() : best && inReach(G, s, *best, std::max(e.reach, e.lunge));
  if (best && strikes && s.meleeWind < 0 && di < 0) {   // CH6: hold back at the strike distance instead of walking into the target (the swing would pass over it)
    float hold = 1e9f; for (int i = 0; i < (int)ac.size(); ++i) { Strike r = strikeOf(G, s, i); if (!r.leap && !r.shoot) hold = std::min(hold, 0.8f * r.dist); }
    if (hold < 1e8f && dT < hold) m.speed = SPD_STOP;
  }
  bool dropOk = false;
  if (best && di >= 0) {
    vec3 f = best->feet - s.feet; f.y = 0; f = glm::length(f) > 1e-3f ? glm::normalize(f) : vec3(0, 0, 1);   // she turns to her target
    int n = 0; for (auto& o : G.sold) if (o.alive && Game::hostile(s, o) && inSlam(o.feet, s.feet + f * dropR, f, e, s.eScale) < 1.0f) ++n;
    dropOk = n >= ((int)ac.size() > 1 ? 2 : 1);   // two or more; one will do when the slam is her only attack
  }
  if (s.meleeWind < 0 && best && G.t >= s.meleeReady && G.t >= s.staggerUntil && ((inR && (int)ac.size() > (di >= 0 ? 1 : 0)) || dropOk)) { if (e.id == "stalker") G.app.audio.play("spider_attack", s.feet + vec3(0, 1, 0), 1.0f); else if (e.id == "brute") G.app.audio.play("troll_attack", s.feet + vec3(0, 2, 0), 1.0f); else if (!e.sndWindup.empty() && best->human) { G.app.audio.play(e.sndWindup.c_str(), s.feet + vec3(0, 0.8f * e.height * e.scale, 0), 1.0f, e.voicePitch * (0.96f + 0.08f * G.rnd(s))); gWindVoiced[s.slot] = true; }   /* SND4: just before the blow on a player */
    else if (!e.sndAttack.empty()) { gWindVoiced.erase(s.slot); G.app.audio.play(e.sndAttack.c_str(), s.feet + vec3(0, 0.6f * e.height * e.scale, 0), 1.0f, e.voicePitch * (0.93f + 0.14f * G.rnd(s))); }   /* one of three, picked at random */
    s.meleeWind = 0; int n = (int)ac.size() - (di >= 0 ? 1 : 0);   // EN2: any of the type's attacks (the drop only at its distance)
    if (dropOk || n <= 0) s.meleeClip = di;   // two or more in the slam's oval: the slam; otherwise the others
    else if (strikes && !fit.empty()) { float x = G.rnd(s) * wsum; s.meleeClip = fit.back(); for (int i : fit) { x -= strikeOf(G, s, i).weight; if (x <= 0) { s.meleeClip = i; break; } } }   // CH6: by weight among those that fit
    else { int pick = std::min(n - 1, (int)(G.rnd(s) * n)); s.meleeClip = di >= 0 && pick >= di ? pick + 1 : pick; }
    gStruck[s.slot].clear();
    if (strikes && s.meleeClip >= 0 && strikeOf(G, s, s.meleeClip).shoot && G.app.audio.hasEvent("archer_draw")) G.app.audio.play("archer_draw", s.feet + vec3(0, 0.75f * e.height * e.scale, 0), 1.0f, 0.96f + 0.08f * G.rnd(s));   // AR1: the nock and draw, at chest height
    ++s.meleeCount; }
  if (s.meleeWind >= 0 && strikes && s.meleeClip != di) {   // CH6: the strike lands where the joint is, not on whoever was chosen
    float wu = windupOf(G, s); Strike r = strikeOf(G, s, s.meleeClip);
    if (best && s.meleeWind < wu && !r.shoot) {   // step (a leap: dash) in to the strike distance by the blow, no closer; a target that got away is not chased mid-swing
      float gap = zLenXZ(best->feet - s.feet) - armourGap(G, *best) - 0.85f * r.dist;
      if (gap > 0.15f && (r.leap ? gap < r.hi : gap < 3.0f)) { float v = gap / std::max(0.1f, wu - s.meleeWind); m.zSpeed = std::max(m.zSpeed, r.leap ? v : std::min(v, 1.5f * e.run)); m.speed = SPD_ZOMBIE; } else m.speed = SPD_STOP;
    }
    else m.speed = SPD_STOP;   // the blow and its follow-through: planted
    float w0 = s.meleeWind; s.meleeWind += dt;
    if (w0 < wu && s.meleeWind >= wu && gWindVoiced.erase(s.slot) && wu >= 1.0f && !e.sndAttack.empty()) G.app.audio.play(e.sndAttack.c_str(), s.feet + vec3(0, 0.6f * e.height * e.scale, 0), 1.0f, e.voicePitch * (0.93f + 0.14f * G.rnd(s)));   // SND4: after a windup sound, the attack sound at the blow (not if they would overlap)
    std::vector<int>& hit = gStruck[s.slot];
    if (r.shoot) { if (best && w0 < wu && s.meleeWind >= wu) loose(G, s, *best, r); }   // AR1: the arrow leaves at the release; no strike sphere
    else if (s.meleeWind >= wu - r.pre) {
      vec3 p = strikePoint(s, r);
      { static const bool lg = std::getenv("NMK_STRIKE_LOG") != nullptr; if (lg && w0 < wu && s.meleeWind >= wu) { vec3 q = p - s.feet; logf("strike %s clip %d joint %d at feet+(%.2f %.2f %.2f) target %d body dist %.2f radius %.2f range %.1f-%.1f leap %d",
        e.id.c_str(), s.meleeClip, r.joint, q.x, q.y, q.z, best ? best->slot : -1, best ? bodyDist(*best, p) : -1.0f, r.radius, r.lo, r.hi, (int)r.leap); } }   // diagnostics
      if (e.stompArea > 0) {   // a slam: the area where the limb meets the ground, at the blow
        if (w0 < wu && s.meleeWind >= wu) {
          vec3 at(p.x, G.app.terrain.height(p.x, p.z), p.z); float R = e.stompArea * e.height * e.scale;
          if (e.groundShake > 0) G.app.blastShake = std::max(G.app.blastShake, e.groundShake * std::max(0.0f, 1.0f - glm::length(at - G.app.eyePos()) / 30.0f));
          G.app.audio.play("heavy_stomp", at, 1.0f);   // shortcut: host / single player only
          if (e.attackDust) for (int k = 0; k < 28; ++k) {
            float an = k * 0.2244f, rr = k % 4 == 3 ? R * 0.35f : R; vec3 dirv(std::cos(an), 0, std::sin(an)); vec3 pp = at + dirv * rr; pp.y = G.app.terrain.height(pp.x, pp.z) + 0.3f;
            G.app.fx.smoke(pp, dirv * (R * 0.25f) + vec3(0, 0.6f + 0.05f * R, 0), std::max(1.5f, R * 0.14f), 3.5f, vec4(0.52f, 0.46f, 0.38f, 0.55f), 1.6f, 0.4f);
          }
          for (auto& o : G.sold) { if (!o.alive || &o == &s || !Game::hostile(s, o)) continue; float rr = glm::length(o.feet - at); if (rr < R) { float hp0 = o.hp; vec3 dir = o.feet - at; dir.y = 0.3f; G.damage(o.slot, r.damage * (1.0f - rr / R), s.slot, glm::normalize(dir), "melee"); if (!o.alive) o.flat = true;
            if (o.hp < hp0) { ++gBlows; gBlowLog.push_back({s.slot, s.etype, hp0 - o.hp}); contactSound(G, e, o); } } }
        }
      } else {   // a strike: everyone hostile the sphere touches while it is live, once each
        if (w0 < wu && s.meleeWind >= wu && e.groundShake > 0) G.app.blastShake = std::max(G.app.blastShake, e.groundShake * std::max(0.0f, 1.0f - glm::length(p - G.app.eyePos()) / 30.0f));
        for (auto& o : G.sold) {
          if (!o.alive || &o == &s || !Game::hostile(s, o) || std::find(hit.begin(), hit.end(), o.slot) != hit.end() || (r.victims > 0 && (int)hit.size() >= r.victims)) continue;
          if (bodyDist(o, p) - armourGap(G, o) > r.radius) continue;
          float hp0 = o.hp; hit.push_back(o.slot);
          bool landed = G.meleeHit(s.slot, o.slot, r.damage);
          { static const bool lg = std::getenv("NMK_STRIKE_LOG") != nullptr; if (lg) logf("strike %s touches %d (dist %.2f): meleeHit %d hp %.0f -> %.0f", e.id.c_str(), o.slot, bodyDist(o, p), (int)landed, hp0, o.hp); }   // diagnostics
          if (landed) {
            ++gBlows; gBlowLog.push_back({s.slot, s.etype, hp0 - o.hp}); contactSound(G, e, o);
            if (gBlowLog.size() > 4096) gBlowLog.erase(gBlowLog.begin(), gBlowLog.begin() + 2048);
            if (e.knock > 0 && o.localPlayer) { vec3 d = o.feet - s.feet; d.y = 0; if (glm::length(d) > 1e-3f) G.app.knockVel = glm::normalize(d) * e.knock; }   // shortcut: only the local player is shoved
          }
        }
      }
    }
    if (s.meleeWind >= wu + r.post) {
      int k = attackClip(*G.models[s.model], clipsOf(s), s.meleeClip); float rest = k >= 0 ? (G.models[s.model]->clips[k].dur - G.models[s.model]->clips[k].swing) / e.attackRate : 0.0f;
      s.meleeWind = -1; s.meleeReady = G.t + std::max(e.cooldown, rest);
    }
  } else if (s.meleeWind >= 0) {
    float wu = windupOf(G, s);
    if (best && e.lunge > e.reach) { float gap = zLenXZ(best->feet - s.feet) - 0.8f * e.reach; if (gap > 0) { m.zSpeed = std::max(m.zSpeed, gap / std::max(0.1f, wu - s.meleeWind)); m.speed = SPD_ZOMBIE; } }
    s.meleeWind += dt;
    if (s.meleeWind >= wu) {
      float hp0 = best ? best->hp : 0;
      vec3 at = s.feet;   // where the blow lands: toward the target within reach, or under the torso for a body drop
      if (best) { vec3 d = best->feet - s.feet; d.y = 0; float L = glm::length(d); at = s.feet + (L > 1e-3f ? d / L * std::min(L, e.reach) : vec3(0)); }
      if (e.groundShake > 0) G.app.blastShake = std::max(G.app.blastShake, e.groundShake * std::max(0.0f, 1.0f - glm::length(at - G.app.eyePos()) / 30.0f));   // CH3: the ground shakes (local player's view)
      if (s.meleeClip == di) {   // the body slam: her upper body lands on its oval; full damage at the centre to none at the edge, no crater
        vec3 c = vec3(s.world * vec4(s.pose.joint[CJ_SPINE2], 1)); at = vec3(c.x, G.app.terrain.height(c.x, c.z), c.z);
        vec3 f = c - s.feet; f.y = 0; f = glm::length(f) > 1e-3f ? glm::normalize(f) : vec3(0, 0, 1); vec3 sd(-f.z, 0, f.x);
        float A = 0.18f * e.height * s.eScale, B = 0.10f * e.height * s.eScale;
        G.app.audio.play("heavy_stomp", at, 1.0f);   // shortcut: host / single player only
        if (e.attackDust) for (int k = 0; k < 28; ++k) {   // the dust ring, round the oval's edge
          float an = k * 0.2244f; vec3 out = f * std::cos(an) * A + sd * std::sin(an) * B, pp = at + out; pp.y = G.app.terrain.height(pp.x, pp.z) + 0.3f;
          G.app.fx.smoke(pp, glm::normalize(out) * (B * 0.25f) + vec3(0, 0.6f + 0.05f * B, 0), std::max(1.5f, B * 0.14f), 3.5f, vec4(0.52f, 0.46f, 0.38f, 0.55f), 1.6f, 0.4f);
        }
        for (auto& o : G.sold) { if (!o.alive || &o == &s || !Game::hostile(s, o)) continue; float r = inSlam(o.feet, at, f, e, s.eScale); if (r < 1.0f) { vec3 dir = o.feet - at; dir.y = 0.3f; G.damage(o.slot, e.damage * (1.0f - r), s.slot, glm::normalize(dir), "melee"); if (!o.alive) o.flat = true; } }   // flat: host / single player only
        if (best && hp0 > best->hp) { ++gBlows; gBlowLog.push_back({s.slot, s.etype, hp0 - best->hp}); contactSound(G, e, *best); }
      } else {
      if (e.stompArea > 0 && best) {   // area blow: everyone hostile round the landing point, full damage at the centre falling to none at the edge
        float R = e.stompArea * e.height * e.scale;
        G.app.audio.play("heavy_stomp", at, 1.0f);   // shortcut: host / single player only
        if (e.attackDust) for (int k = 0; k < 28; ++k) {   // dust thrown out from the blow: a ring at the edge of the damage radius and a core
          float an = k * 0.2244f, rr = k % 4 == 3 ? R * 0.35f : R; vec3 dirv(std::cos(an), 0, std::sin(an)); vec3 pp = at + dirv * rr; pp.y = G.app.terrain.height(pp.x, pp.z) + 0.3f;
          G.app.fx.smoke(pp, dirv * (R * 0.25f) + vec3(0, 0.6f + 0.05f * R, 0), std::max(1.5f, R * 0.14f), 3.5f, vec4(0.52f, 0.46f, 0.38f, 0.55f), 1.6f, 0.4f);
        }
        for (auto& o : G.sold) { if (!o.alive || &o == &s || !Game::hostile(s, o)) continue; float r = glm::length(o.feet - at); if (r < R) { vec3 dir = o.feet - at; dir.y = 0.3f; G.damage(o.slot, e.damage * (1.0f - r / R), s.slot, glm::normalize(dir), "melee"); if (!o.alive) o.flat = true; } }   // flat: host / single player only
        if (hp0 > best->hp) { ++gBlows; gBlowLog.push_back({s.slot, s.etype, hp0 - best->hp}); contactSound(G, e, *best); }
      } else if (best && inReach(G, s, *best, e.reach + 0.2f) && G.meleeHit(s.slot, best->slot, e.damage)) {
        ++gBlows; gBlowLog.push_back({s.slot, s.etype, hp0 - best->hp}); contactSound(G, e, *best);
        if (gBlowLog.size() > 4096) gBlowLog.erase(gBlowLog.begin(), gBlowLog.begin() + 2048);
        if (e.knock > 0 && best->localPlayer) { vec3 d = best->feet - s.feet; d.y = 0; if (glm::length(d) > 1e-3f) G.app.knockVel = glm::normalize(d) * e.knock; }   // shortcut: only the local player is shoved; bots and LAN clients are not
      }
      }
      { int k = attackClip(*G.models[s.model], clipsOf(s), s.meleeClip); float rest = k >= 0 ? (G.models[s.model]->clips[k].dur - G.models[s.model]->clips[k].swing) / e.attackRate : 0.0f;
        s.meleeWind = -1; s.meleeReady = G.t + std::max(e.cooldown, rest); }   // the next strike waits for this attack clip to finish (the body slam is long)
    }
  }
  crumb("zombie: move"); A->move(s, dt);
}

// HK3: the ray a + d t (0 <= t <= maxT) against the horse's capsules (json horse_zones, on its posed joints): the first entry t
float aiHorseHit(const Game& G, const Soldier& s, vec3 a, vec3 d, float maxT) {
  const EnemyType& e = typeOf(s);
  if (e.horseZones.empty() || s.model < 0 || s.model >= (int)G.models.size() || s.pose.delta.empty()) return -1;
  {   // coarse: a sphere round the horse
    vec3 c = s.feet + vec3(0, 1.1f, 0), w = c - a; float tc = glm::clamp(glm::dot(w, d), 0.0f, maxT);
    if (glm::length(a + d * tc - c) > 2.6f * s.eScale) return -1;
  }
  const CharModel& m = *G.models[s.model];
  static std::map<const CharModel*, std::vector<std::array<int, 2>>> idx;   // the zones' joint indices per model
  auto it = idx.find(&m);
  if (it == idx.end()) {
    std::vector<std::array<int, 2>> v;
    auto jn = [&](const std::string& n) { for (int k = 0; k < (int)m.jointNode.size(); ++k) if (m.nodeName[m.jointNode[k]] == n) return k; return -1; };
    for (auto& z : e.horseZones) v.push_back({jn(z.a), jn(z.b)});
    it = idx.emplace(&m, v).first;
  }
  float best = -1;
  for (size_t q = 0; q < e.horseZones.size(); ++q) {
    const int ja = it->second[q][0], jb = it->second[q][1];
    if (ja < 0 || jb < 0 || ja >= (int)s.pose.delta.size() || jb >= (int)s.pose.delta.size()) continue;
    vec3 A = vec3(s.world * s.pose.delta[ja] * vec4(m.jointHead[ja], 1)), B = vec3(s.world * s.pose.delta[jb] * vec4(m.jointHead[jb], 1));
    float r = e.horseZones[q].r * s.eScale;
    // closest points of the ray segment and the capsule's axis
    vec3 u = B - A, w0 = a - A; float uu = glm::dot(u, u), du = glm::dot(d, u), dw = glm::dot(d, w0), uw = glm::dot(u, w0), den = uu - du * du;
    float t = den > 1e-8f ? (du * uw - uu * dw) / den : -dw, tc = glm::clamp(t, 0.0f, maxT);
    float sc = uu > 1e-8f ? glm::clamp((uw + du * tc) / uu, 0.0f, 1.0f) : 0.0f;
    tc = glm::clamp(glm::dot(A + u * sc - a, d), 0.0f, maxT);
    float dist = glm::length(a + d * tc - (A + u * sc));
    if (dist > r) continue;
    float th = std::max(0.0f, tc - std::sqrt(r * r - dist * dist));   // entering the capsule (taken square to the ray)
    if (best < 0 || th < best) best = th;
  }
  return best;
}

// HK2: a dead mounted knight (host). The death kind decides once: a blast, shell, flame, burn, vehicle, melee or a hit on the
// rider's legs (they hang at the horse's flanks, below the saddle) brings the horse down (json horse_death.kinds); anything else
// shoots the rider off (the type's death clips). At the end of the horse death the horse stays as a corpse (its skin held at the
// last pose, the rider's meshes left out) and the slot lives on as the foot type, standing where the rider landed (foot_at),
// facing the same way.
void aiZombieDead(Game& G, Soldier& s) {
  const EnemyType& e = typeOf(s);
  if (e.horseDeathClip.empty() || s.model < 0 || s.model >= (int)G.models.size() || s.mountDeath >= 2) return;
  if (s.mountDeath < 0) s.mountDeath = e.horseHp > 0 ? 0 : std::find(e.horseKinds.begin(), e.horseKinds.end(), s.lastHow) != e.horseKinds.end() ? 1 : 0;   // HK3: with its own horse health a death not marked by damage() is the rider's
  if (s.mountDeath != 1) return;
  const CharModel& m = *G.models[s.model]; const int k = findClip(m, e.horseDeathClip.c_str()); const int ft = enemyTypeIndex(e.footType);
  if (k < 0 || ft < 0) { s.mountDeath = 2; return; }
  if (G.t - s.diedAt < m.clips[k].dur + 0.1) return;
  auto hc = std::make_unique<Game::HorseCorpse>();
  if (hc->ci.create(m)) {
    hc->ci.skin(s.pose.skin); hc->world = s.world;
    for (int p = 0; p < (int)hc->ci.view.parts.size(); ++p) if (std::find(e.corpseHide.begin(), e.corpseHide.end(), hc->ci.view.parts[p].name) == e.corpseHide.end()) hc->parts.push_back(p);
    G.horseCorpses.push_back(std::move(hc));
    if (G.horseCorpses.size() > 6) { G.horseCorpses.front()->ci.destroy(); G.horseCorpses.erase(G.horseCorpses.begin()); }   // the oldest goes
  }
  vec3 at = vec3(s.world * m.rootFix * vec4(e.footAt, 1)); Physics::Hit h;
  at = Physics::castRayEx(at + vec3(0, 1.5f, 0), vec3(0, -1, 0), 6.0f, h) ? h.pos : vec3(at.x, s.feet.y, at.z);
  if (s.ciReady) s.ci.destroy(); s.ciReady = false;
  s.etype = ft; s.mountDeath = -1; aiZombieAssign(G, s);
  const EnemyType& f = enemyType(ft);
  s.feet = at; s.alive = true; s.flat = false; s.hp = e.horseHp > 0 && s.riderHpKeep > 0 ? std::min(f.hp, s.riderHpKeep) : f.hp; s.horseHp = 0; s.killer = -1;   // HK3: the rider's health left s.staggerUntil = 0; s.posture = Physics::STAND; s.lastHow.clear();
  s.meleeWind = -1; s.meleeReady = G.t + 0.5; s.poseFrom = -1; ++s.lifeId;
  if (s.agent >= 0) Physics::destroyAgent(s.agent);
  s.agent = f.capsuleH > 0 ? Physics::createAgent(at, f.capsuleH, f.capsuleR) : Physics::createAgent(at);
  s.br.aimYaw = s.yaw;
  logf("HK2: slot %d: the horse is down; on foot as %s", s.slot, f.id.c_str());
}

// the look: which clip plays (host and LAN clients alike; hp and speed replicate, strikes only on the host)
void aiZombiePose(Game& G, Soldier& s, float dt) {
  enum { K_LOCO, K_IDLE, K_ATTACK, K_DAMAGE, K_DEATH, K_CLIMB };
  const CharModel& m = *G.models[s.model];
  const ZombieCfg& c = zcfg();
  const EnemyType& e = typeOf(s);
  const EnemyType& ce = clipsOf(s);
  AnimPlayer& A = s.anim;
  if (A.life != s.lifeId) { A = AnimPlayer{}; A.life = s.lifeId; A.lastHp = s.hp; A.strike = s.meleeCount; }   // new life: no fade from the corpse
  int key0 = A.key, a0 = A.a;
  auto clip = [&](const char* a, const char* b) { int k = findClip(m, a); return k >= 0 ? k : findClip(m, b); };
  auto pickAt = [&](const std::vector<std::string>& v, int k) { return clip(v[(size_t)std::max(0, k) % v.size()].c_str(), v[0].c_str()); };
  auto pick = [&](const std::vector<std::string>& v) { return pickAt(v, s.slot); };   // per zombie: one of the type's walk / idle clips
  int walk = pick(ce.walkClips), run = findClip(m, ce.runClip.c_str()), idle = pick(ce.idleClips);
  auto gs = [&](int k, float def) { return (k >= 0 && m.clips[k].groundSpeed > 0.2f ? m.clips[k].groundSpeed : def) * s.eScale; };   // a bigger body takes longer strides
  float vw = gs(walk, 1.2f), vr = std::max(vw + 0.5f, gs(run, 4.0f));
  bool hurt = s.alive && s.hp < A.lastHp - std::max(0.5f, e.staggerMin); A.lastHp = s.hp;   // EN1: a brute flinches only from heavy hits
  auto playing = [&](float limit) { return A.a >= 0 && A.time < std::min(limit, m.clips[A.a].dur); };
  const BotMind* tm = nullptr; if (AiCore* Ac = aiCore()) tm = mindAt(*Ac, s.slot);
  bool climbing = s.alive && tm && tm->life == s.lifeId && tm->trav.on && tm->trav.heavy;   // EN4 (host: the traversal; clients see the walk)
  int hd = !s.alive && s.mountDeath == 1 ? findClip(m, e.horseDeathClip.c_str()) : -1;   // HK2: the horse down (decided on the host: aiZombieDead)
  if (!s.alive) A.play(K_DEATH, hd >= 0 ? hd : pickAt(ce.deathClips, s.slot + s.lifeId), -1, 0, 1, false);
  else if (climbing) { int cc = ce.climbClip.empty() ? -1 : findClip(m, ce.climbClip); if (cc >= 0) A.play(K_CLIMB, cc, -1, 0, m.clips[cc].dur / std::max(0.3f, tm->trav.dur), false); else A.play(K_CLIMB, walk, -1, 0, 1.4f, true); }   // holds its end pose (no ragdoll in this game)
  else if (A.strike != s.meleeCount) { A.strike = s.meleeCount; A.play(K_ATTACK, attackClip(m, ce, s.meleeClip), -1, 0, e.attackRate, false, true); }
  else if (A.key == K_ATTACK && playing(1e9f)) {}   // the swing plays out
  else if (hurt) A.play(K_DAMAGE, pickAt(ce.damageClips, s.slot + (int)s.hp), -1, 0, 1, false, true);   // EN2: a hit reaction of the list
  else if (A.key == K_DAMAGE && playing(c.flinch)) {}
  else if (s.speed > std::max(0.3f, 0.05f * s.eScale) * (A.key == K_LOCO ? 0.7f : 1.3f) && walk >= 0) {   // AN1 step 7: hysteresis (no idle / walk flicker at the threshold); a giant shuffling slower than that stands (its stride would freeze mid-step)
    float w = run >= 0 ? std::clamp((s.speed - vw) / (vr - vw), 0.0f, 1.0f) : 0.0f;
    A.play(K_LOCO, walk, run, w, std::clamp(s.speed / glm::mix(vw, vr, w), 0.5f / std::max(1.0f, s.eScale), 2.0f) * enemyType(s.etype).locoRate, true);   // a giant's long strides play slowly (no foot slide); walk_rate speeds the legs up
  } else A.play(K_IDLE, idle, -1, 0, 1, true);
  if (A.key != key0) A.fadeLen = A.key == K_ATTACK ? 0.12f : A.key == K_DAMAGE ? 0.1f : A.key == K_DEATH ? 0.25f : 0.3f * std::sqrt(std::max(1.0f, s.eScale));   // AN1 step 7: per transition (idle / walk / climb settle; hits snap; giants ease longer)
  s.poseFrom = -1;
  A.lodAcc += dt;
  bool changed = A.key != key0 || A.a != a0 || A.fade < 1 || s.pose.skin.empty();
  if (c.lod && !changed) {
    float cam = glm::length(s.feet - G.app.eyePos());
    // PF1: a far pack follower walking like its leader wears the leader's pose (and skinned mesh)
    if (cam > c.shareM && A.key == K_LOCO) if (AiCore* Ac = aiCore()) if (BotMind* bm = mindAt(*Ac, s.slot); bm && bm->follower && bm->life == s.lifeId && bm->pack >= 0 && bm->pack < (int)Ac->packs.size()) {
      const Soldier& Ld = G.sold[Ac->packs[bm->pack].members[0]];
      if (Ld.alive && Ld.slot != s.slot && Ld.model == s.model && Ld.anim.key == K_LOCO && !Ld.pose.skin.empty()) {
        s.pose = Ld.pose; s.poseFrom = Ld.slot; ++s.poseRev; A.time = Ld.anim.time; A.lodAcc = 0; return;
      }
    }
    float hz = cam > c.animFar ? c.animHzFar : cam > c.animNear ? c.animHzNear : 0.0f;
    if (hz > 0 && A.lodAcc < 1.0f / hz) return;   // far: keep the last pose until its next sample
  }
  A.update(m, A.lodAcc, s.pose); A.lodAcc = 0; ++s.poseRev;
}

bool Game::meleeHit(int attacker, int victim, float dmg) {
  if (!active || isClient || attacker < 0 || victim < 0 || attacker >= (int)sold.size() || victim >= (int)sold.size()) return false;
  Soldier& a = sold[attacker]; Soldier& v = sold[victim];
  if (!v.alive || !hostile(a, v)) return false;
  vec3 d = v.feet - a.feet; d.y = 0; d = glm::length(d) > 1e-3f ? glm::normalize(d) : vec3(0, 0, -1);
  float hp0 = v.hp;
  damage(victim, dmg, attacker, d, "melee");
  if (glm::length(chest(v) - app.eyePos()) < 60.0f) app.audio.play("knife_generic", chest(v), 0.7f);
  if (attacker == localSlot && v.hp < hp0) hitMarker = 0.25f;
  return v.hp < hp0;
}

// App::meleeSweep: one knife ray against the soldiers in front of the world hit (host / singleplayer match)
bool mpaiMeleeRay(App& app, vec3 o, vec3 d, float& t, float dmg) {
  Game* G = gGame;
  if (!G || !G->active || G->isClient) return false;   // shortcut: a LAN client's knife still only hits the world; add a melee request to net.cpp when needed
  int best = -1; float bt = t;
  for (auto& s : G->sold) {
    if (!s.alive || s.slot == G->localSlot) continue;
    HitZone hz;
    if (G->hitTest(s, o, d, bt, hz) && hz.t < bt) { bt = hz.t; best = s.slot; }
  }
  if (best < 0) return false;
  t = bt;
  uint64_t key = (0xEull << 60) | (uint64_t)best;   // one hit per soldier per swing
  if (app.meleeHits.insert(key).second) G->meleeHit(G->localSlot, best, dmg);
  return true;
}

// AR1 --check-ar1 (headless): Archer 1's type parses with its ranged attack, and an arrow aimed by arrowAim, flown the way
// App::stepProjectiles flies it (Euler, 1/120 s steps, gravity only), passes within 15 cm of the chest at 5, 10, 25 and 45 m
int ar1SelfCheck() {
  int fails = 0; auto chk = [&](bool ok, const std::string& what) { logf("[ar1] %s: %s", ok ? "ok" : "FAIL", what.c_str()); if (!ok) ++fails; };
  int k = enemyTypeIndex("archer1"); chk(k >= 0, "zombie.json has archer1");
  if (k >= 0) {
    const EnemyType& e = enemyType(k); const EnemyType::AttackSpec* sh = nullptr;
    for (auto& a : e.attackSpecs) if (a.shoot) sh = &a;
    chk(sh != nullptr, "a shoot attack");
    if (sh) { chk(sh->clip == "longbow shot" && std::find(e.attackClips.begin(), e.attackClips.end(), sh->clip) != e.attackClips.end(), "on the 'longbow shot' clip, listed in clips.attack");
      chk(sh->speed == 55 && sh->ammo == "crossbow_bolt" && sh->rangeMin == 4 && sh->rangeMax == 45 && std::fabs(sh->swing - 0.692f) < 1e-4f, "55 m/s, crossbow_bolt, 4-45 m, swing 0.692"); }
    chk(e.model == "archer_1" && fs::exists(contentDir() / "characters" / "archer_1.glb"), "model content/characters/archer_1.glb");
  }
  { int k2 = enemyTypeIndex("archer2"); bool sh2 = false; if (k2 >= 0) for (auto& a : enemyType(k2).attackSpecs) sh2 = sh2 || (a.shoot && a.clip == "longbow shot");
    chk(k2 >= 0 && sh2, "archer2 (AR2) with the same longbow shot"); }
  { int km = enemyTypeIndex("knight10m"); bool side = false; if (km >= 0) for (auto& a : enemyType(km).attackSpecs) side = side || (a.clip == "attack_right" && a.faceDeg == 44 && a.hasAt);
    chk(km >= 0 && side, "knight10m (HK1): the right thrust faces 44 deg past its target, its sphere on the spear tip");
    chk(km >= 0 && enemyType(km).horseDeathClip == "horse_death" && enemyTypeIndex(enemyType(km).footType) >= 0 && !enemyType(km).horseKinds.empty() && !enemyType(km).corpseHide.empty(), "knight10m (HK2): horse_death goes on foot as a known type");
    chk(km >= 0 && enemyType(km).horseHp > 0 && enemyType(km).horseZones.size() >= 10, "knight10m (HK3): the horse's own health and hit capsules"); }
  for (float D : {5.0f, 10.0f, 25.0f, 45.0f}) {
    vec3 from(0, 1.45f, 0), aim(0, 1.3f, -D), p = from, v = arrowAim(from, aim, 55.0f) * 55.0f; float best = 1e9f; const float h = 1.0f / 120;
    for (int i = 0; i < 600 && p.z > -D - 2; ++i) { vec3 q = p + v * h; float t = glm::clamp(glm::dot(aim - p, q - p) / std::max(1e-9f, glm::dot(q - p, q - p)), 0.0f, 1.0f); best = std::min(best, glm::length(p + (q - p) * t - aim)); p = q; v.y -= 9.81f * h; }
    chk(best < 0.15f, "arrow at " + std::to_string((int)D) + " m passes " + std::to_string((int)(best * 1000)) + " mm from the chest");
  }
  logf("[ar1] %s", fails ? "FAILED" : "all ok"); return fails ? 1 : 0;
}
