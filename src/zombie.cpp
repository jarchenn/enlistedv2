// G2 zombies: an AI type per side (ScenarioRules::aiType). A zombie skips the whole soldier brain (perception,
// utility decisions, cover, aiming, grenades): it re-paths to the closest hostile about every 0.5 s, moves at
// speed_mps (between jog and sprint) and strikes in melee when in reach. Melee damage, from zombies and the
// player's knife, goes through Game::meleeHit -> Game::damage, so spawn protection, friendly fire and the LAN
// replication of health and deaths are the shared ones. Tunables: content/ai/zombie.json.
// Look (Z1): content/characters/zombie.glb with its own clips (anim_clip.h): walk / run blended by speed, an attack
// per strike with the blow at its swing frame, a flinch when hurt, death holding its end pose, idle with no target.
#include "ai_core.h"
#include "app.h"
#include "mpai.h"
#include <algorithm>
#include <cmath>

namespace {
float zLenXZ(vec3 v) { return glm::length(vec2(v.x, v.z)); }
struct ZombieCfg {
  float speed = 4.0f, repath = 0.5f, reach = 1.6f, damage = 35, cooldown = 0.9f, windup = 0.25f;
  std::string model = "zombie"; float height = 1.85f, attackRate = 1.4f, flinch = 0.6f;
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
    } catch (std::exception& e) { logf("zombie: content/ai/zombie.json: %s", e.what()); }
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
}

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
  BotMind& m = A->mind(s);
  Brain& b = s.br;
  // the closest hostile, human or bot, wherever it is (no perception: zombies always know)
  const Soldier* best = nullptr; float bd = 1e9f;
  for (auto& o : G.sold) if (o.alive && Game::hostile(s, o)) { float d = glm::length(o.feet - s.feet); if (d < bd) { bd = d; best = &o; } }
  b.target = best ? best->slot : -1;
  m.posture = Physics::STAND; m.action = A_ADVANCE; m.cover = -1;
  if (best) {
    m.speed = SPD_ZOMBIE;
    b.why = "zombie: pursuing";
    if (G.t >= b.repathT && (!m.hasPath || zLenXZ(best->feet - m.goal) > 0.75f)) { A->requestPath(s, best->feet, 0.5f, 6.0f, true); b.repathT = (float)G.t + c.repath; }
    vec3 d = best->feet - s.feet;
    vec3 look = zLenXZ(d) < 4.0f ? d : m.moveDir;   // close: face the victim; else the way the path goes (no sideways walk round corners)
    if (zLenXZ(look) > 0.05f) b.aimYaw = degrees(std::atan2(-look.x, -look.z));
    b.aimPitch = 0;
  } else { m.speed = SPD_STOP; b.why = "zombie: no one left"; }
  // the strike: wind-up (the attack clip up to its swing frame), then the blow lands if the victim is still in reach
  if (s.meleeWind < 0 && best && G.t >= s.meleeReady && inReach(G, s, *best, c.reach)) { s.meleeWind = 0; s.meleeClip = G.rnd(s) < 0.5f ? 0 : 1; ++s.meleeCount; }
  if (s.meleeWind >= 0) {
    s.meleeWind += dt;
    if (s.meleeWind >= windupOf(G, s)) {
      if (best && inReach(G, s, *best, c.reach + 0.2f) && G.meleeHit(s.slot, best->slot, c.damage)) ++gBlows;
      s.meleeWind = -1; s.meleeReady = G.t + c.cooldown;
    }
  }
  A->move(s, dt);
}

// the look: which clip plays (host and LAN clients alike; hp and speed replicate, strikes only on the host)
void aiZombiePose(Game& G, Soldier& s, float dt) {
  enum { K_LOCO, K_IDLE, K_ATTACK, K_DAMAGE, K_DEATH };
  const CharModel& m = *G.models[s.model];
  const ZombieCfg& c = zcfg();
  AnimPlayer& A = s.anim;
  if (A.life != s.lifeId) { A = AnimPlayer{}; A.life = s.lifeId; A.lastHp = s.hp; A.strike = s.meleeCount; }   // new life: no fade from the corpse
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
  A.update(m, dt, s.pose);
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
