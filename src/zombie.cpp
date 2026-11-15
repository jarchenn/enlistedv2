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
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

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
const ZombieCfg& zcfg() {
  static ZombieCfg c; static bool loaded = false;
  if (!loaded) {
    loaded = true;
    try {
      json j = json::parse(readTextFile(contentDir() / "ai" / "zombie.json"));
      c.repath = j.value("repath_s", c.repath); c.flinch = j.value("flinch_s", c.flinch);
      auto strs = [](const json& e, const char* k, std::vector<std::string>& v) {   // a name or a list of names
        if (e.contains(k) && e[k].is_string()) v = {e[k].get<std::string>()};
        else if (e.contains(k) && e[k].is_array() && !e[k].empty()) { v.clear(); for (auto& s : e[k]) v.push_back(s.get<std::string>()); } };
      for (auto& e : j.value("types", json::array())) {   // EN1 enemy types; a missing field keeps the plain zombie's value
        EnemyType t;
        t.id = e.value("id", t.id); t.name = e.value("name", t.id); t.model = e.value("model", t.model); t.height = e.value("height_m", t.height);
        t.models = {t.model}; strs(e, "models", t.models); t.model = t.models[0];
        t.scale = std::clamp(e.value("scale", t.scale), 0.3f, 25.0f); t.lean = e.value("lean_deg", t.lean);   // the fighters are drawn at 20
        t.fallbackScale = std::clamp(e.value("fallback_scale", t.scale), 0.3f, 25.0f); t.fallbackLean = e.value("fallback_lean_deg", t.lean);
        if (e.contains("tint") && e["tint"].is_array() && e["tint"].size() == 3) t.tint = vec3(e["tint"][0].get<float>(), e["tint"][1].get<float>(), e["tint"][2].get<float>());
        t.hp = std::max(1.0f, e.value("hp", t.hp)); t.damage = e.value("damage", t.damage); t.reach = e.value("reach_m", t.reach); t.lunge = e.value("lunge_m", t.lunge);
        t.cooldown = e.value("cooldown_s", t.cooldown); t.windup = e.value("windup_s", t.windup); t.attackRate = std::clamp(e.value("attack_rate", t.attackRate), 0.25f, 4.0f);
        t.walk = e.value("walk_mps", t.walk); t.run = e.value("run_mps", t.run); t.runWithin = e.value("run_within_m", t.runWithin);
        t.weight = std::max(0.0f, e.value("weight", t.weight)); t.zoneScale = std::clamp(e.value("hit_zone_scale", t.zoneScale), 0.3f, 4.0f);
        t.explosiveMult = std::max(0.0f, e.value("explosive_mult", t.explosiveMult)); t.blastOnly = e.value("blast_only", t.blastOnly); t.locoRate = std::clamp(e.value("walk_rate", t.locoRate), 0.25f, 4.0f); t.nonBlastMult = std::clamp(e.value("non_blast_mult", t.nonBlastMult), 0.0f, 1.0f); t.stompArea = std::clamp(e.value("stomp_area", t.stompArea), 0.0f, 1.0f); t.kickCrater = std::clamp(e.value("kick_crater", t.kickCrater), 0.0f, 3.0f);
        t.capsuleH = e.value("capsule_height_m", t.capsuleH); t.capsuleR = e.value("capsule_radius_m", t.capsuleR);
        t.climbM = e.value("climb_m", t.climbM); t.stepGap = e.value("step_gap_m", t.stepGap);
        t.knock = e.value("knockback_mps", t.knock); t.staggerMin = e.value("stagger_min_damage", t.staggerMin); t.staggerS = e.value("stagger_s", t.staggerS);
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
  return k >= 0 && G.models[s.model]->clips[k].swing > 0 ? G.models[s.model]->clips[k].swing / e.attackRate : e.windup;
}
BotMind* mindAt(AiCore& A, int slot) { return slot >= 0 && slot < (int)A.minds.size() ? A.minds[slot].get() : nullptr; }
bool packMate(AiCore& A, const Soldier& o, int pack) { if (pack < 0) return false; BotMind* m = mindAt(A, o.slot); return m && m->life == o.lifeId && m->pack == pack; }
bool packCompatible(const Game& G, const Soldier& a, const Soldier& b) { return G.match.mode() == GameMode::Solo || a.team == b.team; }

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
    for (auto& o : G.sold) if (o.alive && Game::hostile(s, o) && !packMate(A, o, m.pack)) { float d = zLenXZ(o.feet - s.feet); if (d < vd && std::fabs(o.feet.y - s.feet.y) < 2.0f) { vd = d; v = &o; } }
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
    float bd = 1e9f;
    for (auto& o : G.sold) if (o.alive && Game::hostile(s, o) && !packMate(*A, o, m.pack)) { float d = glm::length(o.feet - s.feet); if (d < bd) { bd = d; best = &o; } }
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
    vec3 look = best && zLenXZ(d) < 4.0f ? d : m.moveDir;   // close: face the victim; else the way the path goes (no sideways walk round corners)
    if (zLenXZ(look) > 0.05f) b.aimYaw = degrees(std::atan2(-look.x, -look.z));
    b.aimPitch = 0;
  }
  {   // the plain zombie groans now and then when near the listener. shortcut: host / single player only, one voice at a time
    static double nextGroan = 0;
    if (s.etype == 0 && G.t >= nextGroan && glm::length(s.feet - G.app.eyePos()) < 35.0f && G.rnd(s) < 0.02f) { G.app.audio.play("zombie_groan", s.feet + vec3(0, 1.4f, 0), 0.9f); nextGroan = G.t + 5.0 + 6.0 * G.rnd(s); }
    if (nextGroan > G.t + 30.0) nextGroan = 0;   // a new match restarts the clock
  }
  const bool fighterT = e.id.rfind("fighter", 0) == 0, spiderT = e.id == "stalker";
  if ((fighterT || spiderT) && s.speed > 0.5f && glm::length(s.feet - G.app.eyePos()) < (fighterT ? 70.0f : 40.0f)) {   // the Fighters' heavy footsteps. shortcut: host / single player only
    static std::map<int, double> nextStep;
    double& ns = nextStep[s.slot];
    if (G.t >= ns || ns > G.t + 5.0) { G.app.audio.play(fighterT ? "step_fighter" : "step_spider", s.feet, fighterT ? 1.0f : 0.7f); ns = G.t + (fighterT ? std::clamp(2.6f / std::max(1.0f, s.speed), 0.35f, 0.9f) : std::clamp(1.2f / std::max(1.0f, s.speed), 0.18f, 0.45f)); }   // the spider scuttles: quicker steps
  }
  crumb("zombie: strike");
  // the strike: wind-up (the attack clip up to its swing frame), then the blow lands if the victim is still in reach
  // EN1 lunge: a strike may start lunge_m out; the wind-up closes the gap at whatever speed that takes
  if (s.meleeWind < 0 && best && G.t >= s.meleeReady && G.t >= s.staggerUntil && inReach(G, s, *best, std::max(e.reach, e.lunge))) { if (e.id == "stalker") G.app.audio.play("spider_attack", s.feet + vec3(0, 1, 0), 1.0f);   /* one of three, picked at random */ s.meleeWind = 0; int n = (int)clipsOf(s).attackClips.size(); s.meleeClip = std::min(n - 1, (int)(G.rnd(s) * n)); ++s.meleeCount; }   // EN2: any of the type's attacks
  if (s.meleeWind >= 0) {
    float wu = windupOf(G, s);
    if (best && e.lunge > e.reach) { float gap = zLenXZ(best->feet - s.feet) - 0.8f * e.reach; if (gap > 0) { m.zSpeed = std::max(m.zSpeed, gap / std::max(0.1f, wu - s.meleeWind)); m.speed = SPD_ZOMBIE; } }
    s.meleeWind += dt;
    if (s.meleeWind >= wu) {
      float hp0 = best ? best->hp : 0;
      if (e.kickCrater > 0 && best) {   // shortcut: host / single player only, the crater is not sent to LAN clients or the navmesh
        vec3 d = best->feet - s.feet; d.y = 0; float L = glm::length(d);
        G.app.deformGround(s.feet + (L > 1e-3f ? d / L * std::min(L, e.reach) : vec3(0)), e.kickCrater);   // repeated blows on one spot dig deeper
      }
      if (e.stompArea > 0 && best) {   // area blow: everyone hostile round the landing point, full damage at the centre falling to none at the edge
        vec3 d = best->feet - s.feet; d.y = 0; float L = glm::length(d); vec3 at = s.feet + (L > 1e-3f ? d / L * std::min(L, e.reach) : vec3(0));
        float R = e.stompArea * e.height * e.scale;
        G.app.audio.play("fighter_stomp", at, 1.0f);   // shortcut: host / single player only
        for (auto& o : G.sold) { if (!o.alive || &o == &s || !Game::hostile(s, o)) continue; float r = glm::length(o.feet - at); if (r < R) { vec3 dir = o.feet - at; dir.y = 0.3f; G.damage(o.slot, e.damage * (1.0f - r / R), s.slot, glm::normalize(dir), "melee"); } }
        if (hp0 > best->hp) { ++gBlows; gBlowLog.push_back({s.slot, s.etype, hp0 - best->hp}); }
      } else if (best && inReach(G, s, *best, e.reach + 0.2f) && G.meleeHit(s.slot, best->slot, e.damage)) {
        ++gBlows; gBlowLog.push_back({s.slot, s.etype, hp0 - best->hp});
        if (gBlowLog.size() > 4096) gBlowLog.erase(gBlowLog.begin(), gBlowLog.begin() + 2048);
        if (e.knock > 0 && best->localPlayer) { vec3 d = best->feet - s.feet; d.y = 0; if (glm::length(d) > 1e-3f) G.app.knockVel = glm::normalize(d) * e.knock; }   // shortcut: only the local player is shoved; bots and LAN clients are not
      }
      s.meleeWind = -1; s.meleeReady = G.t + e.cooldown;
    }
  }
  crumb("zombie: move"); A->move(s, dt);
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
  if (!s.alive) A.play(K_DEATH, pickAt(ce.deathClips, s.slot + s.lifeId), -1, 0, 1, false);
  else if (climbing) { int cc = ce.climbClip.empty() ? -1 : findClip(m, ce.climbClip); if (cc >= 0) A.play(K_CLIMB, cc, -1, 0, m.clips[cc].dur / std::max(0.3f, tm->trav.dur), false); else A.play(K_CLIMB, walk, -1, 0, 1.4f, true); }   // holds its end pose (no ragdoll in this game)
  else if (A.strike != s.meleeCount) { A.strike = s.meleeCount; A.play(K_ATTACK, attackClip(m, ce, s.meleeClip), -1, 0, e.attackRate, false, true); }
  else if (A.key == K_ATTACK && playing(1e9f)) {}   // the swing plays out
  else if (hurt) A.play(K_DAMAGE, pickAt(ce.damageClips, s.slot + (int)s.hp), -1, 0, 1, false, true);   // EN2: a hit reaction of the list
  else if (A.key == K_DAMAGE && playing(c.flinch)) {}
  else if (s.speed > 0.3f && walk >= 0) {
    float w = run >= 0 ? std::clamp((s.speed - vw) / (vr - vw), 0.0f, 1.0f) : 0.0f;
    A.play(K_LOCO, walk, run, w, std::clamp(s.speed / glm::mix(vw, vr, w), 0.5f / std::max(1.0f, s.eScale), 2.0f) * enemyType(s.etype).locoRate, true);   // a giant's long strides play slowly (no foot slide); walk_rate speeds the legs up
  } else A.play(K_IDLE, idle, -1, 0, 1, true);
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
