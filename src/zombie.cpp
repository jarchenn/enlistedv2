// G2 zombies: an AI type per side (ScenarioRules::aiType). A zombie skips the whole soldier brain (perception,
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
#include "ai_core.h"
#include "app.h"
#include "mpai.h"
#include "crumb.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
float zLenXZ(vec3 v) { return glm::length(vec2(v.x, v.z)); }
struct ZombieCfg {
  float speed = 4.0f, repath = 0.5f, reach = 1.6f, damage = 35, cooldown = 0.9f, windup = 0.25f;
  std::string model = "zombie"; float height = 1.85f, attackRate = 1.4f, flinch = 0.6f;
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
      c.speed = j.value("speed_mps", c.speed); c.repath = j.value("repath_s", c.repath); c.reach = j.value("reach_m", c.reach);
      c.damage = j.value("damage", c.damage); c.cooldown = j.value("cooldown_s", c.cooldown); c.windup = j.value("windup_s", c.windup);
      c.model = j.value("model", c.model); c.height = j.value("height_m", c.height);
      c.attackRate = std::clamp(j.value("attack_rate", c.attackRate), 0.25f, 4.0f); c.flinch = j.value("flinch_s", c.flinch);
      json p = j.value("pack", json::object());
      c.packs = p.value("on", c.packs); c.packMin = p.value("min_zombies", c.packMin); c.packSize = std::clamp(p.value("size", c.packSize), 2, 16);
      c.join = p.value("join_m", c.join); c.breakOff = p.value("break_off_m", c.breakOff); c.spacing = p.value("slot_spacing_m", c.spacing);
      c.leaderPace = p.value("leader_pace", c.leaderPace); c.catchUp = p.value("catch_up", c.catchUp);
      c.sepR = p.value("separation_m", c.sepR); c.sepGain = p.value("separation_gain", c.sepGain); c.align = p.value("alignment", c.align); c.lost = p.value("lost_m", c.lost);
      json l = j.value("lod", json::object());
      c.lod = l.value("on", c.lod); c.animNear = l.value("anim_near_m", c.animNear); c.animFar = l.value("anim_far_m", c.animFar);
      c.animHzNear = std::max(1.0f, l.value("anim_hz_near", c.animHzNear)); c.animHzFar = std::max(1.0f, l.value("anim_hz_far", c.animHzFar)); c.shareM = l.value("share_pose_m", c.shareM);
    } catch (std::exception& e) { logf("zombie: content/ai/zombie.json: %s", e.what()); }
    if (const char* e = std::getenv("NMK_ZOMBIE_PACKS")) c.packs = *e != '0';   // A/B measurements
    if (const char* e = std::getenv("NMK_ZOMBIE_LOD")) c.lod = *e != '0';
  }
  return c;
}
bool inReach(const Game& G, const Soldier& z, const Soldier& v, float reach) {
  return v.alive && zLenXZ(v.feet - z.feet) < reach && std::fabs(v.feet.y - z.feet.y) < 1.2f && G.lineOfSight(G.chest(z), G.chest(v));
}
int attackClip(const CharModel& m, int k) { return findClip(m, k ? "attack02" : "attack01"); }
// strike wind-up: the attack clip's swing frame at attack_rate (the json fallback without clips)
float windupOf(const Game& G, const Soldier& s) {
  const ZombieCfg& c = zcfg();
  if (s.model < 0 || s.model >= (int)G.models.size()) return c.windup;
  int k = attackClip(*G.models[s.model], s.meleeClip);
  return k >= 0 && G.models[s.model]->clips[k].swing > 0 ? G.models[s.model]->clips[k].swing / c.attackRate : c.windup;
}
BotMind* mindAt(AiCore& A, int slot) { return slot >= 0 && slot < (int)A.minds.size() ? A.minds[slot].get() : nullptr; }
bool packMate(AiCore& A, const Soldier& o, int pack) { if (pack < 0) return false; BotMind* m = mindAt(A, o.slot); return m && m->life == o.lifeId && m->pack == pack; }
bool packCompatible(const Game& G, const Soldier& a, const Soldier& b) { return G.match.mode() == GameMode::Solo || a.team == b.team; }

// every 0.5 s: drop the dead, promote a new leader, put the packless into the nearest pack with room, merge small packs
void maintainPacks(Game& G, AiCore& A) {
  const ZombieCfg& c = zcfg();
  A.packT = G.t + 0.5;
  std::vector<int> zs; for (auto& s : G.sold) if (s.alive && s.zombie && !s.human) zs.push_back(s.slot);
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
    float pl = glm::length(push), cap = c.speed * 0.6f;
    m.flockPush = pl > cap ? push / pl * cap : push;
  }
}
}   // namespace

float aiZombieSpeed() { return zcfg().speed; }
static int gBlows = 0;
int aiZombieBlows() { return gBlows; }

int aiZombieModel(Game& G) {
  const ZombieCfg& c = zcfg();
  auto it = G.modelIdx.find(c.model); if (it != G.modelIdx.end()) return it->second;
  static bool failed = false; if (failed) return -1;
  auto cm = std::make_unique<CharModel>(); std::string e;
  if (!cm->load(contentDir() / "characters" / (c.model + ".glb"), e, c.height)) { failed = true; logf("zombie: model %s: %s (soldier model kept)", c.model.c_str(), e.c_str()); return -1; }
  G.modelIdx[c.model] = (int)G.models.size(); G.models.push_back(std::move(cm));
  return G.modelIdx[c.model];
}
void aiZombieStep(Game& G, Soldier& s, float dt) {
  AiCore* A = aiCore();
  if (!A) return;
  const ZombieCfg& c = zcfg();
  if (G.t >= A->packT) { crumb("zombie: packs"); maintainPacks(G, *A); }
  BotMind& m = A->mind(s);
  Brain& b = s.br;
  m.posture = Physics::STAND; m.action = A_ADVANCE; m.cover = -1;
  const Soldier* L = m.follower && m.pack >= 0 && m.pack < (int)A->packs.size() ? &G.sold[A->packs[m.pack].members[0]] : nullptr;
  const Soldier* best = nullptr;
  crumb("zombie: target");
  if (L && L->alive && L->slot != s.slot) {
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
  if (best || L) {
    vec3 d = best ? best->feet - s.feet : vec3(0);
    vec3 look = best && zLenXZ(d) < 4.0f ? d : m.moveDir;   // close: face the victim; else the way the path goes (no sideways walk round corners)
    if (zLenXZ(look) > 0.05f) b.aimYaw = degrees(std::atan2(-look.x, -look.z));
    b.aimPitch = 0;
  }
  crumb("zombie: strike");
  // the strike: wind-up (the attack clip up to its swing frame), then the blow lands if the victim is still in reach
  if (s.meleeWind < 0 && best && G.t >= s.meleeReady && inReach(G, s, *best, c.reach)) { s.meleeWind = 0; s.meleeClip = G.rnd(s) < 0.5f ? 0 : 1; ++s.meleeCount; }
  if (s.meleeWind >= 0) {
    s.meleeWind += dt;
    if (s.meleeWind >= windupOf(G, s)) {
      if (best && inReach(G, s, *best, c.reach + 0.2f) && G.meleeHit(s.slot, best->slot, c.damage)) ++gBlows;
      s.meleeWind = -1; s.meleeReady = G.t + c.cooldown;
    }
  }
  crumb("zombie: move"); A->move(s, dt);
}

// the look: which clip plays (host and LAN clients alike; hp and speed replicate, strikes only on the host)
void aiZombiePose(Game& G, Soldier& s, float dt) {
  enum { K_LOCO, K_IDLE, K_ATTACK, K_DAMAGE, K_DEATH };
  const CharModel& m = *G.models[s.model];
  const ZombieCfg& c = zcfg();
  AnimPlayer& A = s.anim;
  if (A.life != s.lifeId) { A = AnimPlayer{}; A.life = s.lifeId; A.lastHp = s.hp; A.strike = s.meleeCount; }   // new life: no fade from the corpse
  int key0 = A.key, a0 = A.a;
  auto clip = [&](const char* a, const char* b) { int k = findClip(m, a); return k >= 0 ? k : findClip(m, b); };
  bool alt = s.slot & 1;   // per zombie: walk01 / walk02, idle01 / idle02
  int walk = clip(alt ? "walk02" : "walk01", "walk01"), run = findClip(m, "run"), idle = clip(alt ? "idle02" : "idle01", "idle01");
  auto gs = [&](int k, float def) { return k >= 0 && m.clips[k].groundSpeed > 0.2f ? m.clips[k].groundSpeed : def; };
  float vw = gs(walk, 1.2f), vr = std::max(vw + 0.5f, gs(run, 4.0f));
  bool hurt = s.alive && s.hp < A.lastHp - 0.5f; A.lastHp = s.hp;
  auto playing = [&](float limit) { return A.a >= 0 && A.time < std::min(limit, m.clips[A.a].dur); };
  if (!s.alive) A.play(K_DEATH, findClip(m, "death"), -1, 0, 1, false);   // holds its end pose (no ragdoll in this game)
  else if (A.strike != s.meleeCount) { A.strike = s.meleeCount; A.play(K_ATTACK, attackClip(m, s.meleeClip), -1, 0, c.attackRate, false, true); }
  else if (A.key == K_ATTACK && playing(1e9f)) {}   // the swing plays out
  else if (hurt) A.play(K_DAMAGE, findClip(m, "damage"), -1, 0, 1, false, true);
  else if (A.key == K_DAMAGE && playing(c.flinch)) {}
  else if (s.speed > 0.3f && walk >= 0) {
    float w = run >= 0 ? std::clamp((s.speed - vw) / (vr - vw), 0.0f, 1.0f) : 0.0f;
    A.play(K_LOCO, walk, run, w, std::clamp(s.speed / glm::mix(vw, vr, w), 0.5f, 2.0f), true);
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
