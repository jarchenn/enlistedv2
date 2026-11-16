// --selftest-zombies (G2): the zombie AI type on the shipped 3-point village. A zombie paths to a standing player and
// its blows land (35 each, cooldown spacing); it moves between jog and sprint speed; it never runs the soldier brain
// (no cover, utility scores, weapon, grenades or shots); in Solo it picks the closest combatant; the player's knife
// kills a soldier; a mixed Team Deathmatch (soldiers v zombies) runs with kills on both sides. Z1: the zombie wears
// content/characters/zombie.glb: its clips load, every hit-capsule joint maps, the walk animates, a strike plays an
// attack clip, head and leg rays hit their zones. PF1: 200 zombies in Solo run in packs (only leaders path, followers
// keep up, a dead leader is replaced, strikes land) and a 100 soldiers v 100 zombies TDM, both without 150 ms frames.
// EN1: the enemy types (zombie.json types): each type's hp, blow damage, cooldown and speed match its data, the brute
// shoves the player, stands taller and survives a Garand clip of torso hits (and dies to enough), only a heavy hit
// staggers it, the stalker lunges and leans; "Zombies + heavies" packs carry the configured mix.
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "ai_core.h"
#include "selftest_crowd.h"
#include "enemy_types.h"
#include "damage_model.h"
#include <algorithm>
#include <cstdlib>
#include <cstdarg>
#include <functional>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0;
MpTest* T = nullptr;
int mode = -1; bool started = false;
fs::path pendingShot;
char B[700];
bool chk(bool ok, const char* fmt, ...) { va_list a; va_start(a, fmt); std::vsnprintf(B, sizeof B, fmt, a); va_end(a); return T->check(ok, B); }
void push(const std::string& n, std::function<bool(App&, float)> f) { seq.push_back({n, f}); }
fs::path outDir() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
float dXZ(vec3 a, vec3 b) { return glm::length(vec2(a.x - b.x, a.z - b.z)); }

bool startMode(App& a, GameMode m, int solo, int teamA, int teamB, AiType ai0, AiType ai1, const char* map = "examples/village_assault_3.nmap.json") {
  if (a.mode == Mode::Explore) a.exitExplore();
  a.openMap(contentDir() / map);
  json scj = a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object();
  Scenario sc = Scenario::fromJson(scj);
  sc.rules.mode = m; sc.rules.killCap = 999; sc.rules.soloPlayers = solo; sc.rules.teamPlayers[0] = teamA; sc.rules.teamPlayers[1] = teamB;
  sc.rules.respawnDelay = 2.0f; sc.rules.humanTeam = 0; sc.rules.aiTypes[0] = aiBit(ai0); sc.rules.aiTypes[1] = aiBit(ai1);
  gUi.loaded = false; gUi.matchScenario = sc.toJson();
  if (!gGame) gGame = new Game(a);
  gGame->pendingStart = true; a.doc.start = {true, vec3(0, 0, 30), 180};
  bool ok = a.enterExplore() && gGame->active;
  a.useScripted = true; a.scriptedInput = ExploreInput{};
  return ok;
}
// an open, walkable point about `dist` from `from` (on the navmesh, in plain sight of it)
bool openPoint(Game& G, vec3 from, float dist, vec3& out, float startAng = 0) {
  for (int k = 0; k < 24; ++k) {
    float a = startAng + k * 0.2618f;
    vec3 p = from + vec3(std::cos(a), 0, std::sin(a)) * dist; NavRef r; vec3 on;
    if (!G.nav.closest(p, 2.0f, r, on) || dXZ(on, p) > 1.0f || std::fabs(on.y - from.y) > 2.0f) continue;
    if (!G.lineOfSight(from + vec3(0, 1.4f, 0), on + vec3(0, 1.4f, 0))) continue;
    out = on; return true;
  }
  return false;
}
void place(Game& G, Soldier& s, vec3 p) {
  if (s.localPlayer) Physics::setCharacterPos(p); else if (s.agent >= 0) Physics::setAgentPos(s.agent, p);
  s.feet = p; s.vel = vec3(0);
}
Soldier* firstAi(Game& G, int skip = -1) { for (auto& s : G.sold) if (!s.human && s.alive && s.slot != skip) return &s; return nullptr; }
bool allSpawned(Game& G) { for (auto& s : G.sold) if (!s.alive) return false; return !G.sold.empty(); }
void guardLocal(Game& G) { if (Soldier* L = G.local()) if (L->alive) { L->hp = 100; L->protectUntil = G.t + 1; } }

// PF1 pack statistics, sampled every 0.5 s: pack count and size, followers within 12 m of their leader, path requests
struct PackStats {
  int lp0 = 0, fp0 = 0, near = 0, followers = 0, maxPacks = 0, sizeN = 0; float sizeSum = 0; double next = 0; bool on = false;
  void sample(Game& G, AiCore& A, float t, float from) {
    if (t < from) return;
    if (!on) { on = true; lp0 = A.pathsLeader; fp0 = A.pathsFollower; }
    if (t < next) return;
    next = t + 0.5;
    maxPacks = std::max(maxPacks, (int)A.packs.size());
    for (auto& p : A.packs) { sizeSum += p.members.size(); ++sizeN; const Soldier& L = G.sold[p.members[0]];
      for (size_t k = 1; k < p.members.size(); ++k) { ++followers; near += dXZ(G.sold[p.members[k]].feet, L.feet) < 12.0f; } }
  }
  float meanSize() const { return sizeN ? sizeSum / sizeN : 0.0f; }
  float nearPct() const { return followers ? 100.0f * near / followers : 0.0f; }
  // packs form, only leaders path, followers keep up (minimum mean size and share within 12 m)
  void check(AiCore& A, int zombies, float minSize, float minNear, const char* what) {
    int lp = A.pathsLeader - lp0, fp = A.pathsFollower - fp0;
    chk(maxPacks >= zombies / 10 && meanSize() >= minSize, "%s: packs form: up to %d packs, %.1f zombies each on average", what, maxPacks, meanSize());
    chk(lp > 0 && fp <= (lp + fp) / 5, "%s: only leaders path: %d leader and %d follower path requests (followers %.0f %%)", what, lp, fp, lp + fp ? 100.0f * fp / (lp + fp) : 0.0f);
    chk(followers && nearPct() >= minNear, "%s: followers keep up: %.0f %% of follower samples within 12 m of their leader (%d)", what, nearPct(), followers);
  }
};
bool packsEnabled() { const char* e = std::getenv("NMK_ZOMBIE_PACKS"); return !e || *e != '0'; }

void build(App&) {
  push("headless", [](App&, float) {
    json mv = json::parse(readTextFile(contentDir() / "settings" / "movement.json"));
    float jog = mv.value("run_mps", 3.1625f), spr = mv.value("sprint_mps", 6.325f);
    chk(aiZombieSpeed() > jog && aiZombieSpeed() < spr, "zombie.json speed %.2f m/s lies between jog %.2f and sprint %.2f", aiZombieSpeed(), jog, spr);
    const auto& E = enemyTypes();
    const EnemyType& z = E[0];
    chk(E.size() == 3 && z.id == "zombie" && z.hp == 100 && z.damage == 35 && z.reach == 1.6f && z.cooldown == 0.9f && z.walk == 4.0f && z.run == 4.0f && z.scale == 1 && z.staggerMin == 0 && z.knock == 0,
        "enemy types: %zu (plain zombie first, unchanged: hp %.0f, %.0f per blow, reach %.1f m, cooldown %.1f s, %.1f m/s)", E.size(), z.hp, z.damage, z.reach, z.cooldown, z.run);
    {   // EN3: the AI type Heavies: trolls by the slider's share, the rest spiders, never a plain zombie
      int n[3][3] = {}; const float share[3] = {0.0f, 0.35f, 1.0f};
      for (int c = 0; c < 3; ++c) for (uint32_t k = 0; k < 1000; ++k) ++n[c][std::clamp(enemyTypeHeavyPick(k, share[c]), 0, 2)];
      chk(n[0][0] + n[1][0] + n[2][0] == 0 && n[0][1] == 0 && n[2][2] == 0 && std::abs(n[1][1] - 350) < 40,
          "Heavies: trolls at 0 %% / 35 %% / 100 %%: %d / %d / %d of 1000 (spiders %d / %d / %d, zombies %d)", n[0][1], n[1][1], n[2][1], n[0][2], n[1][2], n[2][2], n[0][0] + n[1][0] + n[2][0]);
    }
    for (auto& e : E) {
      std::snprintf(B, sizeof B, "type %-8s scale %.2f lean %.0f hp %.0f damage %.0f reach %.1f lunge %.1f cooldown %.2f attack_rate %.2f walk %.1f run %.1f (within %.0f m) weight %.1f zones x%.2f knock %.1f stagger >= %.0f for %.1f s",
                    e.id.c_str(), e.scale, e.lean, e.hp, e.damage, e.reach, e.lunge, e.cooldown, e.attackRate, e.walk, e.run, std::min(e.runWithin, 9999.0f), e.weight, e.zoneScale, e.knock, e.staggerMin, e.staggerS);
      T->note(B);
    }
    int cnt[3] = {0, 0, 0}; for (uint32_t k = 0; k < 1000; ++k) ++cnt[std::clamp(enemyTypePick(k), 0, 2)];
    float tw = 0; for (auto& e : E) tw += e.weight;
    bool share = E.size() == 3; for (int k = 0; k < 3 && share; ++k) share = std::fabs(cnt[k] / 1000.0f - E[k].weight / tw) < 0.04f;
    chk(share, "the weighted pick over 1000 slots: %d zombies, %d brutes, %d stalkers (weights %.0f / %.0f / %.0f)", cnt[0], cnt[1], cnt[2], E[0].weight, E.size() > 1 ? E[1].weight : 0.0f, E.size() > 2 ? E[2].weight : 0.0f);
    return true; });
  // ---- EN1: one of each type against the player (team A alone): hp, blow damage, cooldown, speed, knock-back, size, lunge, lean
  push("enemy types", [](App& a, float t) {
    static bool placed; static size_t log0; static float spMax[3], startMax[3], knockMax, headH[3], lean[3]; static int z3[3];
    static int counted;
    Game& G = *gGame;
    if (t == 0) {
      placed = false; knockMax = 0; counted = 0; for (int k = 0; k < 3; ++k) { spMax[k] = startMax[k] = headH[k] = 0; lean[k] = -9; z3[k] = -1; }
      log0 = aiZombieBlowLog().size();
      chk(startMode(a, GameMode::TDM, 4, 1, 3, AiType::Soldiers, AiType::ZombiesHeavies), "TDM: the player v one zombie, one brute, one stalker (%s)", gGame->setupError.c_str());
      int k = 0; for (auto& s : gGame->sold) if (s.zombie && k < 3) { s.etype = k; aiZombieAssign(*gGame, s); if (s.alive) s.hp = enemyType(k).hp; z3[k++] = s.slot; }
      return false;
    }
    Soldier* L = G.local();
    if (!L || !L->alive || !allSpawned(G)) return t > 20 ? (chk(false, "everyone spawns"), true) : false;
    L->protectUntil = -1; if (L->hp < 3000) L->hp = 5000;   // the player takes every blow and never falls
    if (!placed) {
      placed = true;
      int ok = 0;
      for (int k = 0; k < 3; ++k) { Soldier& z = G.sold[z3[k]]; vec3 p; z.protectUntil = -1;
        if (openPoint(G, L->feet, 14.0f, p, k * 2.1f)) { place(G, z, p); ++ok; }
        chk(z.hp == enemyType(k).hp, "%s spawns with %.0f hp (data %.0f)", enemyType(k).id.c_str(), z.hp, enemyType(k).hp); }
      chk(ok == 3, "the three placed 14 m from the player (%d)", ok);
      log0 = aiZombieBlowLog().size();
      return false;
    }
    for (int k = 0; k < 3; ++k) {
      Soldier& z = G.sold[z3[k]];
      if (!z.alive) continue;
      float d = dXZ(z.feet, L->feet);
      if (z.meleeWind < 0 && d > 3.5f) spMax[k] = std::max(spMax[k], z.speed);
      if (z.meleeWind >= 0 && z.meleeWind < 0.05f) startMax[k] = std::max(startMax[k], d);
      auto J = [&](int j) { return vec3(z.world * vec4(z.pose.joint[j], 1)); };
      headH[k] = std::max(headH[k], J(CJ_HEAD).y - z.feet.y);
      vec3 to = L->feet - z.feet; to.y = 0;
      if (d > 4 && z.meleeWind < 0 && glm::length(to) > 0.1f && z.speed > 1) { vec3 off = J(CJ_HEAD) - z.feet; off.y = 0; lean[k] = std::max(lean[k], glm::dot(off, glm::normalize(to))); }
    }
    knockMax = std::max(knockMax, glm::length(a.knockVel));
    const auto& Lg = aiZombieBlowLog();
    int n[3] = {0, 0, 0}; float lost[3] = {0, 0, 0}; double lastAt[3] = {-1, -1, -1}; (void)lastAt;
    for (size_t i = log0; i < Lg.size(); ++i) if (Lg[i].type >= 0 && Lg[i].type < 3) { ++n[Lg[i].type]; lost[Lg[i].type] += Lg[i].hpLost; }
    if ((n[0] < 3 || n[1] < 3 || n[2] < 3) && t < 45) return false;
    for (int k = 0; k < 3; ++k) {
      const EnemyType& e = enemyType(k);
      float per = n[k] ? lost[k] / n[k] : 0;
      chk(n[k] >= 3 && std::fabs(per - e.damage) < 0.5f, "%s blows take %.1f hp each (data %.0f, %d blows)", e.id.c_str(), per, e.damage, n[k]);
      chk(spMax[k] > 0.85f * e.run && spMax[k] < 1.08f * e.run, "%s runs at %.2f m/s (data %.2f)", e.id.c_str(), spMax[k], e.run);
    }
    // cooldown: a type's blows (one attacker each) no closer together than its cooldown -> at most t / cooldown + 1
    chk(n[1] <= (t - 1.0f) / enemyType(1).cooldown + 1, "brute: %d blows in %.0f s with a %.1f s cooldown (and a long wind-up)", n[1], t, enemyType(1).cooldown);
    chk(knockMax > 0.9f * enemyType(1).knock, "a brute blow shoves the player at %.1f m/s (data %.1f)", knockMax, enemyType(1).knock);
    { float want = G.sold[z3[1]].eBody, got = headH[1] / std::max(0.1f, headH[0]);
      chk(got > 0.85f * want && got < 1.15f * want, "the brute's head stands %.2f m up vs the zombie's %.2f m (x%.2f, body x%.2f%s)", headH[1], headH[0], got, want, G.sold[z3[1]].eFallback ? ", zombie rig" : ""); }
    chk(startMax[2] > enemyType(2).reach + 0.5f, "the stalker starts a lunge %.1f m out (reach %.1f, lunge %.1f); the zombie strikes from %.1f m", startMax[2], enemyType(2).reach, enemyType(2).lunge, startMax[0]);
    chk(lean[2] > lean[0] + 0.15f, "the stalker's head leads its feet by %.2f m while running (the zombie %.2f m)", lean[2], lean[0]);
    std::snprintf(B, sizeof B, "fastest: zombie %.2f, brute %.2f, stalker %.2f m/s", spMax[0], spMax[1], spMax[2]); T->note(B);
    // the brute against a Garand clip of torso hits (10 m), and the stagger
    Soldier& br = G.sold[z3[1]]; const EnemyType& be = enemyType(1);
    int ga = a.weaponIndex("m1_garand"); int clip = ga >= 0 ? a.weaponDefs[ga]->cap : 8;
    float per = dmDamage(dmRow("m1_garand"), 10.0f, DZ_UPPER_TORSO);
    br.hp = be.hp; br.staggerUntil = 0; br.protectUntil = -1;
    for (int k = 0; k < clip; ++k) G.damage(br.slot, per, L->slot, vec3(0, 0, 1), "upper torso");
    bool aliveClip = br.alive, stagRifle = br.staggerUntil > G.t;
    int more = 0; while (br.alive && more < 50) { G.damage(br.slot, per, L->slot, vec3(0, 0, 1), "upper torso"); ++more; }
    chk(aliveClip && !stagRifle && !br.alive, "a brute (%.0f hp) survives a Garand clip (%d torso hits of %.0f) without staggering, and falls to %d more", be.hp, clip, per, more);
    Soldier& br2 = G.sold[z3[0]];   // the stagger on a body that stays up: the zombie's slot made a brute for the moment
    int keep = br2.etype; br2.etype = 1; br2.hp = be.hp; br2.staggerUntil = 0; br2.protectUntil = -1;
    G.damage(br2.slot, be.staggerMin + 10, L->slot, vec3(0, 0, 1), "blast");
    bool stag = br2.staggerUntil > G.t;
    br2.etype = keep;
    chk(stag, "a %.0f hp blast staggers a brute for %.1f s", be.staggerMin + 10, be.staggerS);
    return true; });

  if (!std::getenv("NMK_ZOMBIES_ONLY_HORDE")) {   // diagnostics: just the 200-zombie runs
  // ---- one zombie against a standing player: pursuit, blows, speed, no soldier brain
  push("pursuit", [](App& a, float t) {
    static int Z; static bool placed; static float hp0, minD, spSum, spMax; static int spN; static std::vector<std::pair<float, float>> hits; static bool brainSeen, shot;
    static vec3 foot0; static float footDev; static int attackSeen;
    if (t == 0) { placed = false; hits.clear(); minD = 1e9f; spSum = spMax = 0; spN = 0; brainSeen = false; shot = false; footDev = 0; attackSeen = 0;
      chk(startMode(a, GameMode::Solo, 2, 1, 1, AiType::Zombies, AiType::Soldiers), "Solo, the player and one zombie (%s)", gGame->setupError.c_str()); return false; }
    Game& G = *gGame; Soldier* L = G.local();
    if (!placed) {
      if (t < 1.5f || !allSpawned(G) || !L) { if (t > 20) { chk(false, "everyone spawned"); return true; } return false; }
      Soldier* z = firstAi(G); if (!z) { chk(false, "a zombie exists"); return true; }
      Z = z->slot; vec3 p;
      chk(z->zombie && z->weapon < 0 && z->frags == 0 && z->smokes == 0, "the AI slot is a zombie: no weapon, no grenades (weapon %d, frags %d, smokes %d)", z->weapon, z->frags, z->smokes);
      if (!openPoint(G, L->feet, 18.0f, p)) { chk(false, "an open point 18 m from the player"); return true; }
      {   // Z1: the animated model
        const CharModel& M = *G.models[z->model];
        std::string miss;
        for (const char* n : {"walk01", "walk02", "run", "attack01", "attack02", "damage", "death", "idle01", "idle02"}) if (findClip(M, n) < 0) miss += std::string(" ") + n;
        int unmapped = 0; for (int j = 0; j < CJ_COUNT; ++j) unmapped += M.semantic[j] < 0;
        chk(M.id == "zombie" && miss.empty(), "the zombie wears zombie.glb with its clips (%s: %zu clips, missing:%s)", M.id.c_str(), M.clips.size(), miss.empty() ? " none" : miss.c_str());
        chk(unmapped == 0, "every CharJoint maps to a zombie bone (%d of %d unmapped)", unmapped, (int)CJ_COUNT);
        const AnimClip* a1 = findClip(M, "attack01") >= 0 ? &M.clips[findClip(M, "attack01")] : nullptr;
        const AnimClip* w1 = findClip(M, "walk01") >= 0 ? &M.clips[findClip(M, "walk01")] : nullptr;
        const AnimClip* r1 = findClip(M, "run") >= 0 ? &M.clips[findClip(M, "run")] : nullptr;
        chk(M.bmax.y > 1.6f && M.bmax.y < 2.1f && M.bmin.y > -0.1f, "scaled to a man's height, feet on the ground (y %.2f .. %.2f m)", M.bmin.y, M.bmax.y);
        chk(a1 && a1->swing > 0.1f && a1->swing < a1->dur, "attack01 has a swing frame (%.2f of %.2f s)", a1 ? a1->swing : -1.0f, a1 ? a1->dur : 0.0f);
        chk(w1 && r1 && w1->groundSpeed > 0.3f && r1->groundSpeed > w1->groundSpeed, "walk01 / run ground speeds measured (%.2f, %.2f m/s)", w1 ? w1->groundSpeed : 0.0f, r1 ? r1->groundSpeed : 0.0f);
      }
      place(G, *z, p); z->protectUntil = -1; L->protectUntil = -1; L->hp = hp0 = 100; placed = true; return false;
    }
    Soldier& z = G.sold[Z];
    if (!L->alive) { chk(false, "the player died before two blows were counted"); return true; }
    float d = dXZ(z.feet, L->feet); minD = std::min(minD, d);
    if (d > 4.0f && t > 2.5f) { spSum += z.speed; ++spN; spMax = std::max(spMax, z.speed); }
    if (d > 4.0f && t > 1.0f) { if (footDev == 0) foot0 = z.pose.joint[CJ_LFOOT] + vec3(1e-6f); footDev = std::max(footDev, glm::length(z.pose.joint[CJ_LFOOT] - foot0)); }
    if (z.anim.key == 2 && (z.anim.a == findClip(*G.models[z.model], "attack01") || z.anim.a == findClip(*G.models[z.model], "attack02"))) attackSeen |= 1;
    if (aiCore()) { BotMind& m = aiCore()->mind(z); for (float u : m.util) brainSeen |= u != 0; brainSeen |= m.cover >= 0 || z.shots > 0 || z.throwT >= 0; }
    if (!shot && d < 5.0f) { shot = true; vec3 v = z.feet - L->feet; a.eyeYaw = degrees(std::atan2(-v.x, -v.z)); a.eyePitch = -8; pendingShot = outDir() / "zombies_pursuit.png"; }
    if (L->hp < hp0 - 0.5f) { hits.push_back({t, hp0 - L->hp}); hp0 = L->hp; if (z.anim.key == 2) attackSeen |= 2; }
    if (hits.size() < 2 && t < 30) return false;
    chk(minD < 2.0f, "the zombie paths to the player from 18 m (closest %.2f m)", minD);
    chk(hits.size() >= 2, "its blows land: %zu in %.1f s", hits.size(), t);
    if (hits.size() >= 2) {
      chk(std::fabs(hits[0].second - 35) < 1 && std::fabs(hits[1].second - 35) < 1, "35 damage per blow (%.1f, %.1f)", hits[0].second, hits[1].second);
      chk(hits[1].first - hits[0].first >= 0.85f, "blows at least the 0.9 s cooldown apart (%.2f s)", hits[1].first - hits[0].first);
    }
    float mean = spN ? spSum / spN : 0;
    chk(mean > 3.3f && mean < 6.33f && spMax < 6.4f, "pursuit speed between jog and sprint: mean %.2f m/s, max %.2f (%d samples)", mean, spMax, spN);
    chk(!brainSeen && z.weapon < 0, "no soldier brain: no utility scores, cover, shots or throws");
    chk(footDev > 0.15f, "the walk / run clip moves the feet over time (left foot travel %.2f m)", footDev);
    chk(attackSeen == 3, "each strike plays attack01 / attack02 and the blow lands during it (seen %d)", attackSeen);
    {   // hit capsules follow the clip pose: head and leg rays from 3 m in front
      auto W = [&](int j) { return vec3(z.world * vec4(z.pose.joint[j], 1)); };
      vec3 fwd = glm::normalize(vec3(z.world * vec4(0, 0, 1, 0)));
      vec3 head = W(CJ_HEAD) + glm::normalize(W(CJ_HEAD) - W(CJ_NECK)) * 0.06f, leg = glm::mix(W(CJ_LUPLEG), W(CJ_LLEG), 0.5f);
      HitZone hh, hl; vec3 o1 = head + fwd * 3.0f, o2 = leg + fwd * 3.0f;
      bool h1 = G.hitTest(z, o1, glm::normalize(head - o1), 10, hh), h2 = G.hitTest(z, o2, glm::normalize(leg - o2), 10, hl);
      chk(h1 && hh.zone == 0, "a ray at the zombie's head hits the head zone (hit %d, zone %d, head %.2f m up)", (int)h1, hh.zone, head.y - z.feet.y);
      chk(h2 && hl.zone == 2, "a ray at its thigh hits a limb zone (hit %d, zone %d)", (int)h2, hl.zone);
      chk(W(CJ_HEAD).y > W(CJ_HIPS).y + 0.3f && W(CJ_HIPS).y > W(CJ_LFOOT).y + 0.4f, "posed upright: head %.2f, hips %.2f, foot %.2f m", W(CJ_HEAD).y - z.feet.y, W(CJ_HIPS).y - z.feet.y, W(CJ_LFOOT).y - z.feet.y);
    }
    L->hp = 100; L->protectUntil = G.t + 5;
    return true; });
  // ---- Solo: the closest combatant, zombie or human
  push("closest", [](App& a, float t) {
    static int Z1, Z2, phase;
    if (t == 0) { phase = 0; chk(startMode(a, GameMode::Solo, 3, 1, 1, AiType::Zombies, AiType::Soldiers), "Solo, the player and two zombies (%s)", gGame->setupError.c_str()); return false; }
    Game& G = *gGame; Soldier* L = G.local(); guardLocal(G);
    if (phase == 0) {
      if (t < 1.5f || !allSpawned(G) || !L) { if (t > 20) { chk(false, "everyone spawned"); return true; } return false; }
      Soldier* z1 = firstAi(G); Soldier* z2 = z1 ? firstAi(G, z1->slot) : nullptr;
      vec3 p1, p2;
      if (!z2 || !openPoint(G, L->feet, 14.0f, p1) || !openPoint(G, p1, 4.0f, p2, std::atan2(p1.z - L->feet.z, p1.x - L->feet.x))) { chk(false, "two zombies placed"); return true; }
      Z1 = z1->slot; Z2 = z2->slot; place(G, *z1, p1); place(G, *z2, p2); phase = 1; st = 0; return false;
    }
    Soldier& z1 = G.sold[Z1]; Soldier& z2 = G.sold[Z2];
    if (phase == 1 && t > 0.15f) {
      chk(z1.br.target == Z2 && z2.br.target == Z1, "Solo: each zombie goes for the other, 4 m away, not the player 14 m off (targets %d, %d)", z1.br.target, z2.br.target);
      vec3 p; if (!openPoint(G, z1.feet, 1.4f, p)) p = z1.feet + vec3(1.4f, 0, 0);
      place(G, *L, p); phase = 2; st = 0; return false;
    }
    if (phase == 2 && t > 0.15f) {
      chk(z1.br.target == G.localSlot, "...and switches to the player once the player is the closest (target %d, player %d)", z1.br.target, G.localSlot);
      return true;
    }
    return false; });
  // ---- the knife kills a soldier
  push("knife", [](App& a, float t) {
    static int E, phase; static float lastSwing;
    if (t == 0) { phase = 0; chk(startMode(a, GameMode::TDM, 2, 1, 1, AiType::Soldiers, AiType::Soldiers), "TDM 1 v 1 against a soldier (%s)", gGame->setupError.c_str()); return false; }
    Game& G = *gGame; Soldier* L = G.local();
    if (phase == 0) {
      if (t < 1.5f || !allSpawned(G) || !L || !aiCore()) { if (t > 20) { chk(false, "everyone spawned"); return true; } return false; }
      Soldier* e = firstAi(G); if (!e) { chk(false, "an enemy soldier"); return true; }
      E = e->slot; aiCore()->frozen = true;   // he stands still
      vec3 f = App::forward(a.eyeYaw, 0);
      place(G, *e, L->feet + vec3(f.x, 0, f.z) * 1.1f); e->protectUntil = -1; e->hp = 100; L->protectUntil = G.t + 30;
      a.selectWeapon(a.weaponIndex("knife_bayonet")); a.eyePitch = -12; phase = 1; lastSwing = t; return false;
    }
    Soldier& e = G.sold[E];
    a.scriptedInput.fire = false;
    if (e.alive && t - lastSwing > 0.9f) { a.scriptedInput.fire = true; lastSwing = t; }
    if (e.alive && t < 10) return false;
    chk(!e.alive && e.killer == G.localSlot, "the knife kills a soldier (alive %d, killer %d, hp %.0f, %d swings)", (int)e.alive, e.killer, e.hp, a.meleeSwings);
    aiCore()->frozen = false; a.scriptedInput = ExploreInput{};
    return true; });
  // ---- mixed TDM: soldiers (team A, the player's) v zombies (team B)
  push("mixed tdm", [](App& a, float t) {
    static float minD; static bool loadout; static int blows0;
    if (t == 0) { minD = 1e9f; loadout = false; blows0 = aiZombieBlows(); chk(startMode(a, GameMode::TDM, 8, 4, 4, AiType::Soldiers, AiType::Zombies), "TDM 4 v 4, soldiers v zombies (%s)", gGame->setupError.c_str()); return false; }
    Game& G = *gGame; guardLocal(G);
    if (t < 2.0f) return false;
    if (!loadout && allSpawned(G)) {
      loadout = true; int zs = 0, armed = 0;
      for (auto& s : G.sold) { if (s.human) continue; if (s.team == 1) zs += s.zombie && s.weapon < 0; else armed += !s.zombie && s.weapon >= 0; }
      chk(zs == 4 && armed == 3, "team B is 4 zombies, team A's 3 AI are armed soldiers (%d, %d)", zs, armed);
    }
    for (auto& z : G.sold) if (z.alive && z.zombie) for (auto& o : G.sold) if (o.alive && o.team == 0) minD = std::min(minD, dXZ(z.feet, o.feet));
    int k0 = G.match.teamKills(0), k1 = G.match.teamKills(1), blows = aiZombieBlows() - blows0;
    if ((k0 < 1 || (k1 < 1 && blows < 3)) && t < 240) return false;   // batch2 reached 3 blows at 150 s only just: give the zombies time
    // the zombies' kill depends on reaching 3 blows on one soldier before the rifles drop them: a kill's worth of landed blows also counts
    chk(k0 >= 1 && (k1 >= 1 || blows >= 3), "both sides fight: soldiers kill %d, zombies kill %d with %d blows landed in %.0f s (closest zombie to a soldier %.1f m)", k0, k1, blows, t, minD);
    pendingShot = outDir() / "zombies_tdm.png";
    return true; });
  }
  // ---- PF1 hordes: 200 zombies in Solo run in packs; only leaders path; followers keep up; strikes land; frame time.
  // Solo makes every zombie its own team: packs of strangers spawn spread out and brawl with other packs (members die
  // and join all the time), so the pack shape is checked loosely here and strictly in the one-team mixed match below.
  push("horde", [](App& a, float t) {
    static CrowdBench cb; static PackStats ps; static int blows0, promoted; static int deadLead, deadPack; static std::vector<int> mates; static vec3 deadAt;
    if (t == 0) {
      blows0 = aiZombieBlows(); deadLead = -1; promoted = -1; cb.begin(); ps = PackStats{};
      chk(startMode(a, GameMode::Solo, 201, 1, 1, AiType::Zombies, AiType::Soldiers), "Solo, the player and 200 zombies (%s)", gGame->setupError.c_str()); return false;
    }
    Game& G = *gGame; guardLocal(G);
    AiCore* A = aiCore(); if (!A) { chk(false, "the AI runs"); return true; }
    cb.frame(G, t, 5.0f); ps.sample(G, *A, t, 8.0f);
    if (packsEnabled() && deadLead < 0 && t > 20.0f) {   // a leader falls: its nearest member takes over
      for (int p = 0; p < (int)A->packs.size(); ++p) if (A->packs[p].members.size() >= 3) {
        deadPack = p; deadLead = A->packs[p].members[0]; mates.assign(A->packs[p].members.begin() + 1, A->packs[p].members.end());
        Soldier& L = G.sold[deadLead]; deadAt = L.feet; L.protectUntil = -1; G.damage(deadLead, 1000, -1, vec3(0, 0, 1), "test"); break; }
    }
    if (deadLead >= 0 && promoted < 0 && t > 21.0f) {
      int want = -1; float bd = 1e9f; for (int k : mates) if (G.sold[k].alive && dXZ(G.sold[k].feet, deadAt) < bd) { bd = dXZ(G.sold[k].feet, deadAt); want = k; }
      promoted = 0; for (auto& p : A->packs) if (!p.members.empty() && p.members[0] == want) promoted = 1;
      chk(promoted == 1, "a dead leader's pack re-forms behind its nearest member (slot %d, pack %d of %zu)", want, deadPack, A->packs.size());
    }
    if (t < 45) return false;
    int alive = 0; for (auto& s : G.sold) alive += s.alive;
    if (packsEnabled()) ps.check(*A, 200, 3.0f, 60.0f, "Solo"); else T->note("packs off (NMK_ZOMBIE_PACKS=0): pack checks skipped");
    chk(aiZombieBlows() - blows0 > 0, "strikes land: %d blows in 40 s (%d alive)", aiZombieBlows() - blows0, alive);
    chk(cb.over150() == 0, "200 zombies: %s", cb.line(G).c_str());
    if (!cb.stalls.empty()) T->note("stalls:" + cb.stalls);
    pendingShot = outDir() / "zombies_horde.png";
    return true; });
  // ---- PF1 mixed: 100 soldiers (the player's team) v 100 zombies, one team: packs keep their shape
  push("horde mixed", [](App& a, float t) {
    static CrowdBench cb; static PackStats ps; static int blows0;
    static int mixPacks, bigPacks, inPack[3];
    if (t == 0) { mixPacks = bigPacks = 0; inPack[0] = inPack[1] = inPack[2] = 0; blows0 = aiZombieBlows(); cb.begin(); ps = PackStats{}; chk(startMode(a, GameMode::TDM, 200, 100, 100, AiType::Soldiers, AiType::ZombiesHeavies), "TDM 100 soldiers v 100 zombies + heavies (%s)", gGame->setupError.c_str()); return false; }
    Game& G = *gGame; guardLocal(G);
    AiCore* A = aiCore(); if (!A) { chk(false, "the AI runs"); return true; }
    cb.frame(G, t, 5.0f); ps.sample(G, *A, t, 8.0f);
    if (t > 10.0f && inPack[0] + inPack[1] + inPack[2] == 0) for (auto& p : A->packs) {   // EN1: the mix inside the packs, once, early
      int seen[3] = {0, 0, 0}; for (int k : p.members) { int e = std::clamp(G.sold[k].etype, 0, 2); ++seen[e]; ++inPack[e]; }
      if (p.members.size() >= 4) { ++bigPacks; mixPacks += (seen[0] > 0) + (seen[1] > 0) + (seen[2] > 0) >= 2; }
    }
    int k0 = G.match.teamKills(0), k1 = G.match.teamKills(1);
    if (t < 45 || ((k0 < 1 || (k1 < 1 && aiZombieBlows() - blows0 < 3)) && t < 120)) return false;   // the teams spawn a few hundred metres apart
    if (packsEnabled()) ps.check(*A, 100, 5.0f, 80.0f, "mixed"); else T->note("packs off (NMK_ZOMBIE_PACKS=0): pack checks skipped");
    { int all[3] = {0, 0, 0}; for (auto& s : G.sold) if (s.zombie) ++all[std::clamp(s.etype, 0, 2)];
      float tw = 0; for (auto& e : enemyTypes()) tw += e.weight;
      (void)tw;   // EN3: heavies path alone, so the packs are plain zombies only
      chk(all[1] > 0 && all[2] > 0 && inPack[0] > 0 && inPack[1] + inPack[2] == 0 && mixPacks == 0, "the side mixes %d zombies, %d brutes, %d stalkers; packs hold %d zombies and %d / %d heavies (%d of %d packs of 4+ mixed)",
          all[0], all[1], all[2], inPack[0], inPack[1], inPack[2], mixPacks, bigPacks); }
    chk(G.sold.size() == 200 && k0 > 0 && (k1 > 0 || aiZombieBlows() - blows0 >= 3), "both sides fight: soldiers kill %d, zombies kill %d, %d blows in %.0f s", k0, k1, aiZombieBlows() - blows0, t);
    chk(cb.over150() == 0, "100 v 100 mixed: %s", cb.line(G).c_str());
    if (!cb.stalls.empty()) T->note("stalls:" + cb.stalls);
    pendingShot = outDir() / "zombies_mixed.png";
    return true; });
  push("done", [](App& a, float t) { if (t < 0.5f) return false; if (a.mode == Mode::Explore) a.exitExplore(); return true; });
}
// EN2 --capture-heavies: no checks, a look. The player is a fixed camera 6 m behind a soldier bot (the target and the
// size reference, with a plain 1.85 m zombie); a brute, a stalker and a creature come at the soldier from 10 m beyond him. Frames
// while they walk in and early / late in each heavy's first strike (selftest_out/heavies_*.png), with the playing clip
// and head height logged. Nobody falls (hp topped up every frame).
std::vector<std::pair<fs::path, std::string>> shotQ;
bool ch1Capture = false;   // --capture-ch1: the CH1 types instead (selftest_out/ch1_*.png), then their deaths
// the four types (NMK_CAPTURE_TYPES="a,b,c,d" overrides: the CH3 / CH4 types)
std::string capType(int k) {
  static std::vector<std::string> t;
  if (t.empty()) { t = {"knight", "animatronic", "alien", "alien_queen"};
    if (const char* e = std::getenv("NMK_CAPTURE_TYPES")) { std::string s = e; for (int i = 0; i < 4 && !s.empty(); ++i) { size_t c = s.find(','); t[i] = s.substr(0, c); s = c == std::string::npos ? "" : s.substr(c + 1); } } }
  return t[k];
}
void buildCapture(App&) {
  push("capture", [](App& a, float t) {
    static int z3[4], atk[4], mc0[4], sol; static double t0, atkAt[4]; static float yaw, prev;
    Game& G = *gGame;
    if (t == 0) {
      t0 = -1; prev = 0; for (int k = 0; k < 4; ++k) { z3[k] = -1; atk[k] = 0; atkAt[k] = -1; }
      chk(startMode(a, GameMode::TDM, 6, 2, 4, AiType::Soldiers, AiType::ZombiesHeavies), "TDM: the player and a soldier v a zombie, a brute, a stalker, a creature (%s)", gGame->setupError.c_str());
      return false;
    }
    Soldier* L = G.local();
    if (!L || !L->alive || !allSpawned(G)) return t > 20 ? (chk(false, "everyone spawns"), true) : false;
    L->protectUntil = -1; if (L->hp < 3000) L->hp = 5000;
    if (t0 < 0) {
      int k = 0; for (auto& s : G.sold) if (s.zombie && k < 4) { s.etype = ch1Capture ? std::max(0, enemyTypeIndex(capType(k))) : k < 3 ? k : std::max(0, enemyTypeIndex("creature")); aiZombieAssign(G, s); s.hp = enemyType(s.etype).hp; z3[k++] = s.slot;
        const EnemyType& e = enemyType(s.etype); if (s.agent >= 0 && e.capsuleH > 0) { Physics::destroyAgent(s.agent); s.agent = Physics::createAgent(s.feet, e.capsuleH, e.capsuleR); } }
      sol = -1; for (auto& s : G.sold) if (!s.zombie && !s.localPlayer && s.alive) sol = s.slot;
      vec3 sp; if (sol < 0 || !openPoint(G, L->feet, 6.0f, sp, 0.0f)) { chk(false, "the soldier placed"); return true; }
      place(G, G.sold[sol], sp);
      float ang = std::atan2(sp.z - L->feet.z, sp.x - L->feet.x);
      int ok = 0;
      for (int j = 0; j < 4; ++j) { vec3 p; if (z3[j] >= 0 && openPoint(G, sp, 10.0f, p, ang + 0.45f * (j - 1.5f))) { place(G, G.sold[z3[j]], p); G.sold[z3[j]].protectUntil = -1; ++ok; } }
      if (ok < 4) { chk(false, "the four placed (%d)", ok); return true; }
      for (int j = 0; j < 4; ++j) mc0[j] = G.sold[z3[j]].meleeCount;
      vec3 d = sp + (sp - L->feet) * 0.35f - L->feet; yaw = glm::degrees(std::atan2(-d.x, -d.z)); t0 = G.t; return false;
    }
    a.eyeYaw = yaw; a.eyePitch = -2;   // fixed
    if (sol >= 0) { G.sold[sol].hp = 5000; G.sold[sol].protectUntil = -1; }
    for (int k = 0; k < 4; ++k) if (G.sold[z3[k]].alive) G.sold[z3[k]].hp = std::max(G.sold[z3[k]].hp, enemyType(G.sold[z3[k]].etype).hp);
    float since = (float)(G.t - t0);
    char line[400]; int n = std::snprintf(line, sizeof line, "t %.2f", since);
    for (int k = 0; k < 4; ++k) {
      Soldier& z = G.sold[z3[k]]; const CharModel& m = *G.models[z.model];
      float hy = vec3(z.world * vec4(z.pose.joint[CJ_HEAD], 1)).y - z.feet.y;
      n += std::snprintf(line + n, sizeof line - n, " | %s %s %.2fm d%.1f v%.1f", enemyType(z.etype).id.c_str(), z.anim.a >= 0 ? m.clips[z.anim.a].name.c_str() : "-", hy, dXZ(z.feet, sol >= 0 ? G.sold[sol].feet : L->feet), z.speed);
      if ((k > 0 || ch1Capture) && atkAt[k] < 0 && z.meleeCount > mc0[k]) atkAt[k] = G.t;   // its first strike: frames 0.35 s and 0.8 s in
      if ((k > 0 || ch1Capture) && atkAt[k] >= 0) { float in = (float)(G.t - atkAt[k]); int stage = in > 0.8f ? 2 : in > 0.35f ? 1 : 0;
        if (stage > atk[k] && atk[k] < 2) { atk[k] = stage; char nm[64]; std::snprintf(nm, sizeof nm, "%s_%s_attack%d.png", ch1Capture ? "ch1" : "heavies", enemyType(z.etype).id.c_str(), stage); shotQ.push_back({outDir() / nm, line}); } }
    }
    for (float w : {0.6f, 1.2f, 1.8f}) if (since >= w && prev < w) { char nm[64]; std::snprintf(nm, sizeof nm, "%s_walk_%.1f.png", ch1Capture ? "ch1" : "heavies", w); shotQ.push_back({outDir() / nm, line}); }
    if ((int)(since * 4) != (int)(prev * 4)) T->note(line);
    prev = since;
    if (pendingShot.empty() && !shotQ.empty()) { pendingShot = shotQ.front().first; T->note("shot " + pendingShot.filename().string() + ": " + shotQ.front().second); shotQ.erase(shotQ.begin()); }
    return ((!ch1Capture || atk[0] >= 2) && atk[1] >= 2 && atk[2] >= 2 && atk[3] >= 2 && shotQ.empty() && pendingShot.empty()) || since > 40; });
  if (ch1Capture) push("line-up", [](App& a, float t) {   // close, one at a time: each in turn held ahead of the player facing it, the others far behind (selftest_out/ch1_close_<type>.png)
    Game& G = *gGame; Soldier* L = G.local(); if (!L) return true;
    static float yaw0, prevT; if (t == 0) { yaw0 = a.eyeYaw; prevT = 0; }
    vec3 f = App::forward(yaw0, 0); f.y = 0; f = glm::normalize(f);
    for (auto& s : G.sold) if (!s.zombie && !s.localPlayer && s.alive) { place(G, s, L->feet - f * 4.0f); s.hp = 5000; }   // their target behind the camera: they face it
    int turn = 3 - (int)(t / 1.5f), k = 0;   // the last-spawned first
    for (auto& s : G.sold) if (s.zombie && s.alive && k < 4) {
      bool big = enemyType(s.etype).id == "alien_queen";
      vec3 p = k == turn ? L->feet + f * (big ? 12.0f : 6.5f) : L->feet - f * (40.0f + 6.0f * k); p.y = a.terrain.height(p.x, p.z);
      place(G, s, p); s.yaw = glm::degrees(std::atan2(f.x, f.z)); s.meleeReady = G.t + 5; s.hp = enemyType(s.etype).hp; s.protectUntil = G.t + 1;   // unhurt: the soldier keeps shooting
      if (k == turn && std::fmod(t, 1.5f) >= 1.0f && std::fmod(prevT, 1.5f) < 1.0f) { pendingShot = outDir() / ("ch1_close_" + enemyType(s.etype).id + ".png"); char ln[200]; std::snprintf(ln, sizeof ln, "close %s at %.1f %.1f %.1f (player %.1f %.1f %.1f) clip %d hp %.0f", enemyType(s.etype).id.c_str(), s.feet.x, s.feet.y, s.feet.z, L->feet.x, L->feet.y, L->feet.z, s.anim.a, s.hp); T->note(ln); }
      ++k;
    }
    a.eyeYaw = yaw0; a.eyePitch = 6;
    prevT = t;
    return t > 6.2f && pendingShot.empty(); });
  if (ch1Capture) push("deaths", [](App& a, float t) {   // the four in a row ahead of the player, then they fall (their death clips)
    Game& G = *gGame; static bool shot1, shot2;
    if (t == 0) {
      shot1 = shot2 = false; Soldier* L = G.local(); vec3 f = App::forward(a.eyeYaw, 0); f.y = 0; f = glm::normalize(f); vec3 r(-f.z, 0, f.x); int k = 0;
      for (auto& s : G.sold) if (s.zombie && s.alive) {
        if (L) { vec3 p = L->feet + f * 8.0f + r * ((k++ - 1.5f) * 3.2f); p.y = a.terrain.height(p.x, p.z); place(G, s, p); }
        s.protectUntil = -1; G.killSoldier(s.slot, -1, -f);
      }
      return false; }
    if (!shot1 && t > 0.6f && pendingShot.empty()) { shot1 = true; pendingShot = outDir() / "ch1_death_0.6.png"; }
    if (!shot2 && t > 3.0f && pendingShot.empty()) { shot2 = true; pendingShot = outDir() / "ch1_death_3.0.png"; }
    return shot2 && pendingShot.empty() && t > 3.5f; });
  push("done", [](App& a, float t) { if (t < 0.5f) return false; if (a.mode == Mode::Explore) a.exitExplore(); return true; });
}
// AI4 --capture-standoff: no checks, numbers. Four soldier bots with five zombies 25 m out, then the same with golems
// (the local-only type; the brute without it). For 25 s each (everyone's health topped up) the soldiers' distance to
// the nearest melee enemy and how many are keeping their distance are logged every 2 s, with the minimum and mean.
void buildStandoff(App&) {
  for (int phase = 0; phase < 2; ++phase)
  push(phase ? "standoff: golems" : "standoff: zombies", [phase](App& a, float t) {
    static std::vector<int> sol, zs; static double t0, next; static float minD; static double sumD; static int nD, kiting, nK;
    if (t == 0) {
      t0 = -1; next = 0; minD = 1e9f; sumD = 0; nD = 0; kiting = 0; nK = 0;
      if (phase == 0 && !chk(startMode(a, GameMode::TDM, 10, 5, 5, AiType::Soldiers, AiType::Zombies), "TDM: soldiers v zombies (%s)", gGame->setupError.c_str())) return true;
      return false;
    }
    Game& G = *gGame; Soldier* L = G.local();
    if (!L || !allSpawned(G)) return t > 25 ? (chk(false, "everyone spawns"), true) : false;
    guardLocal(G); L->hp = 5000;
    if (t0 < 0) {
      sol.clear(); zs.clear();
      for (auto& s : G.sold) if (s.alive && !s.human) (s.zombie ? zs : sol).push_back(s.slot);
      if (sol.size() < 2 || zs.empty()) { chk(false, "soldiers %zu, zombies %zu", sol.size(), zs.size()); return true; }
      int gt = enemyTypeIndex("golem"); if (gt < 0) gt = std::max(0, enemyTypeIndex("brute"));
      for (int z : zs) { Soldier& s = G.sold[z]; s.etype = phase ? gt : 0; aiZombieAssign(G, s); s.hp = enemyType(s.etype).hp;
        const EnemyType& e = enemyType(s.etype); if (s.agent >= 0 && e.capsuleH > 0) { Physics::destroyAgent(s.agent); s.agent = Physics::createAgent(s.feet, e.capsuleH, e.capsuleR); } }
      vec3 c = G.sold[sol[0]].feet, p;
      for (size_t k = 1; k < sol.size(); ++k) if (openPoint(G, c, 2.5f, p, 1.3f * k)) place(G, G.sold[sol[k]], p);
      for (size_t k = 0; k < zs.size(); ++k) if (openPoint(G, c, 25.0f, p, 0.35f * k)) place(G, G.sold[zs[k]], p);
      if (openPoint(G, c, 60.0f, p, 3.6f)) place(G, *L, p);   // the player well out of the way
      T->note(std::string("standoff ") + enemyType(G.sold[zs[0]].etype).id + ": " + std::to_string(sol.size()) + " soldiers, " + std::to_string(zs.size()) + " melee enemies at 25 m");
      t0 = G.t; return false;
    }
    for (int k : sol) { G.sold[k].hp = 5000; G.sold[k].protectUntil = -1; }
    for (int z : zs) if (G.sold[z].alive) G.sold[z].hp = std::max(G.sold[z].hp, enemyType(G.sold[z].etype).hp);
    float since = (float)(G.t - t0);
    if (since >= next) {
      next += 0.5;
      float lineMin = 1e9f; int k2 = 0;
      for (int k : sol) {
        const Soldier& s = G.sold[k]; if (!s.alive) continue;
        float d = 1e9f; for (int z : zs) if (G.sold[z].alive) d = std::min(d, dXZ(G.sold[z].feet, s.feet));
        if (d > 1e8f) continue;
        lineMin = std::min(lineMin, d); if (since > 3) { minD = std::min(minD, d); sumD += d; ++nD; }
        bool kite = s.br.why.find("distance") != std::string::npos || s.br.why.find("gap") != std::string::npos || s.br.why.find("outrun") != std::string::npos || s.br.why.find("holding range") != std::string::npos;
        k2 += kite; if (since > 3) { kiting += kite; ++nK; }
      }
      if ((int)(since * 2) % 4 == 0) { char ln[200]; std::snprintf(ln, sizeof ln, "t %.1f: nearest melee enemy %.1f m, %d of %zu soldiers keeping distance (%s, speed %.1f, path %d)", since, lineMin, k2, sol.size(), G.sold[sol[0]].br.why.c_str(), G.sold[sol[0]].speed, aiCore() ? (int)aiCore()->mind(G.sold[sol[0]]).hasPath : -1); T->note(ln); }
    }
    if (since < 25) return false;
    chk(nD > 0, "%s: soldiers' distance to the nearest melee enemy after the first 3 s: min %.1f m, mean %.1f m; keeping distance in %.0f %% of samples",
        phase ? "golems" : "zombies", minD, nD ? (float)(sumD / nD) : 0.0f, nK ? 100.0f * kiting / nK : 0.0f);
    return true; });
  push("done", [](App& a, float t) { if (t < 0.5f) return false; if (a.mode == Mode::Explore) a.exitExplore(); return true; });
}
}   // namespace

// --capture-fighter2: no checks, a look at the grounding (zombie.cpp aiZombieFeet). On Chateau town Fighter 2 (her local
// model) stands 60-70 m in front of the player and a soldier 35 m to her side, so she walks across the view; the player is a
// camera at ground level aimed at her feet. Twelve frames through her stride (selftest_out/fighter2_walk_NN.png), then
// eight through her body drop (fighter2_drop_NN.png) when she does one.
void buildFighterCapture(App&) {
  push("fighter2", [](App& a, float t) {
    static int fz, sol, shots; static double t0, next;
    Game& G = *gGame;
    if (t == 0) {
      t0 = -1; fz = sol = -1; shots = 0;
      chk(startMode(a, GameMode::TDM, 3, 2, 1, AiType::Soldiers, AiType::Fighters2, "templates/chateau_town.nmap.json"), "TDM on Chateau town: the player and a soldier v Fighter 2 (%s)", gGame->setupError.c_str());
      return false;
    }
    Soldier* L = G.local();
    if (!L || !L->alive || !allSpawned(G)) return t > 30 ? (chk(false, "everyone spawns"), true) : false;
    L->protectUntil = -1; L->hp = 5000;
    if (t0 < 0) {
      for (auto& s : G.sold) { if (s.zombie) fz = s.slot; else if (!s.localPlayer) sol = s.slot; }
      // her spot, and a camera spot (the player moves there) that sees the ground under her and 15 m along her path
      NavRef r; vec3 F, P, C; bool ok = false;
      for (int i = 0; i < 16 && !ok; ++i) {
        float a0 = i * 0.3927f; if (!G.nav.closest(L->feet + vec3(std::cos(a0), 0, std::sin(a0)) * (40.0f + 15.0f * (i % 4)), 15.0f, r, F)) continue;
        for (float dist : {70.0f, 60.0f}) for (int k = 0; k < 24 && !ok; ++k) {
          float an = k * 0.2618f; vec3 dir(std::cos(an), 0, std::sin(an)), side(-dir.z, 0, dir.x);
          if (!G.nav.closest(F - dir * dist, 10.0f, r, C) || !G.nav.closest(F + side * 35.0f, 10.0f, r, P)) continue;   // nearer than the camera: she walks across the view
          vec3 eye = C + vec3(0, 1.7f, 0);
          ok = G.lineOfSight(eye, F + vec3(0, 0.3f, 0)) && G.lineOfSight(eye, F + side * 15.0f + vec3(0, 0.5f, 0));
        }
      }
      if (fz < 0 || sol < 0 || !ok) { chk(false, "the fighter and the soldier placed in view"); return true; }
      place(G, *L, C);
      place(G, G.sold[fz], F); place(G, G.sold[sol], P); G.sold[fz].protectUntil = -1;
      vec3 d = F - C; a.eyeYaw = degrees(std::atan2(-d.x, -d.z)); a.eyePitch = degrees(std::atan2(F.y - (C.y + 1.7f), dXZ(F, C)));
      t0 = G.t; next = G.t + 1.0; return false;
    }
    if (sol >= 0) { G.sold[sol].hp = 5000; G.sold[sol].protectUntil = -1; }
    Soldier& z = G.sold[fz]; z.hp = std::max(z.hp, 1000.0f);
    if (G.t >= next && pendingShot.empty() && shots < 12) {
      const CharModel& m = *G.models[z.model];
      char nm[64], line[200]; std::snprintf(nm, sizeof nm, "fighter2_walk_%02d.png", shots++); next = G.t + 0.12;
      float sole[2];   // each foot's sole above the terrain under it (as aiZombieFeet measures it): the lower one should be 0
      for (int f = 0; f < 2; ++f) { sole[f] = 1e9f; for (int j : {f ? (int)CJ_RFOOT : (int)CJ_LFOOT, f ? (int)CJ_RTOE : (int)CJ_LTOE}) {
        vec3 w = vec3(z.world * vec4(z.pose.joint[j], 1)); sole[f] = std::min(sole[f], w.y - m.jointHead[m.semantic[j]].y * z.eScale - a.terrain.height(w.x, w.z)); } }
      vec3 fl = vec3(z.world * vec4(z.pose.joint[CJ_LFOOT], 1)), fr = vec3(z.world * vec4(z.pose.joint[CJ_RFOOT], 1));   // a planted foot keeps its ground spot (no glide)
      std::snprintf(line, sizeof line, "%s clip %s speed %.1f sole L %.2f m R %.2f m | L at %.1f %.1f R at %.1f %.1f", nm, z.anim.a >= 0 ? m.clips[z.anim.a].name.c_str() : "-", z.speed, sole[0], sole[1], fl.x, fl.z, fr.x, fr.z);
      pendingShot = outDir() / nm; T->note(line);
    }
    // then the body drop: eight frames through it once it plays (it is chosen when the target stands where the torso lands)
    static int dshots; static double dnext; if (shots < 12) { dshots = 0; dnext = 0; }
    const CharModel& mm = *G.models[z.model];
    bool dropping = z.anim.a >= 0 && mm.clips[z.anim.a].name == "body_drop";
    if (shots >= 12 && dropping && pendingShot.empty() && dshots < 8 && G.t >= dnext) {
      int k = findClip(mm, "body_drop"); vec3 c = mm.clips[k].swingChest; char nm[64], line[200];
      std::snprintf(nm, sizeof nm, "fighter2_drop_%02d.png", dshots++); dnext = G.t + 0.4;
      std::snprintf(line, sizeof line, "%s t %.2f of %.2f s, chest lands %.1f m ahead, target %.1f m", nm, z.anim.time, mm.clips[k].dur, glm::length(vec2(c.x, c.z)) * z.eScale, sol >= 0 ? dXZ(G.sold[sol].feet, z.feet) : -1.0f);
      pendingShot = outDir() / nm; T->note(line);
    }
    return (shots >= 12 && dshots >= 8 && pendingShot.empty()) || G.t - t0 > 70; });
  push("done", [](App& a, float t) { if (t < 0.5f) return false; if (a.mode == Mode::Explore) a.exitExplore(); return true; });
}

// --capture-fighter2-slam: no checks, a look at the body slam. On Chateau town two soldiers are held on the spot the torso
// lands (so the slam is chosen: two hostiles in its oval) and the player is a camera 80 m to the side of her line; one
// frame each on all fours, flat and getting up (selftest_out/fighter2_slam_{allfours,flat,getup}.png).
void buildSlamCapture(App&) {
  push("slam", [](App& a, float t) {
    static int fz, sz[2], got; static double t0; static vec3 F, P, sd;
    Game& G = *gGame;
    if (t == 0) {
      t0 = -1; got = 0; fz = sz[0] = sz[1] = -1;
      chk(startMode(a, GameMode::TDM, 4, 3, 1, AiType::Soldiers, AiType::Fighters2, "templates/chateau_town.nmap.json"), "TDM on Chateau town: the player and two soldiers v Fighter 2 (%s)", gGame->setupError.c_str());
      return false;
    }
    Soldier* L = G.local();
    if (!L || !L->alive || !allSpawned(G)) return t > 30 ? (chk(false, "everyone spawns"), true) : false;
    L->protectUntil = -1; L->hp = 5000;
    if (t0 < 0) {
      int k = 0; for (auto& s : G.sold) { if (s.zombie) fz = s.slot; else if (!s.localPlayer && k < 2) sz[k++] = s.slot; }
      if (fz < 0 || k < 2) { chk(false, "the fighter and two soldiers"); return true; }
      const CharModel& m = *G.models[G.sold[fz].model]; int kc = findClip(m, "body_drop");
      if (kc < 0) { chk(false, "Fighter 2 has a body_drop clip"); return true; }
      float R = glm::length(vec2(m.clips[kc].swingChest.x, m.clips[kc].swingChest.z)) * G.sold[fz].eScale;
      NavRef r; vec3 C; bool ok = false;
      for (int i = 0; i < 16 && !ok; ++i) {
        float a0 = i * 0.3927f; if (!G.nav.closest(L->feet + vec3(std::cos(a0), 0, std::sin(a0)) * (40.0f + 15.0f * (i % 4)), 15.0f, r, F)) continue;
        for (int j = 0; j < 24 && !ok; ++j) {
          float an = j * 0.2618f; vec3 f(std::cos(an), 0, std::sin(an)); sd = vec3(-f.z, 0, f.x);
          for (float cd : {80.0f, 60.0f}) {
            if (ok || !G.nav.closest(F + f * R, 12.0f, r, P) || !G.nav.closest((F + P) * 0.5f + sd * cd, 12.0f, r, C)) continue;
            vec3 eye = C + vec3(0, 1.7f, 0);
            ok = G.lineOfSight(eye, F + vec3(0, 3.0f, 0)) && G.lineOfSight(eye, P + vec3(0, 0.5f, 0));
          }
        }
      }
      if (!ok) { chk(false, "the slam placed in view"); return true; }
      place(G, G.sold[fz], F); place(G, *L, C);
      vec3 mid = (F + P) * 0.5f, d = mid - C; a.eyeYaw = degrees(std::atan2(-d.x, -d.z)); a.eyePitch = degrees(std::atan2(mid.y + 6.0f - (C.y + 1.7f), dXZ(mid, C)));
      T->note("slam: chest lands " + std::to_string(R) + " m ahead");
      t0 = G.t; return false;
    }
    for (int j = 0; j < 2; ++j) { Soldier& o = G.sold[sz[j]]; place(G, o, P + sd * (j ? 4.0f : -4.0f)); o.hp = 5000; o.protectUntil = -1; }   // held on the landing spot
    Soldier& z = G.sold[fz]; z.hp = std::max(z.hp, 1000.0f);
    const CharModel& m = *G.models[z.model];
    { static double nl = 0; if (G.t >= nl) { nl = G.t + 0.25; char b[160]; std::snprintf(b, sizeof b, "t %.2f clip %s time %.2f strikes %d wind %.2f", G.t - t0, z.anim.a >= 0 ? m.clips[z.anim.a].name.c_str() : "-", z.anim.time, z.meleeCount, z.meleeWind); T->note(b); } }
    if (z.anim.a >= 0 && m.clips[z.anim.a].name == "body_drop" && pendingShot.empty()) {
      static const float at[3] = {1.05f, 1.7f, 2.4f}; static const char* nm[3] = {"fighter2_slam_allfours.png", "fighter2_slam_flat.png", "fighter2_slam_getup.png"};
      if (got < 3 && z.anim.time >= at[got]) { pendingShot = outDir() / nm[got]; T->note(std::string(nm[got]) + " at " + std::to_string(z.anim.time) + " s"); ++got; }
    }
    return (got >= 3 && pendingShot.empty()) || G.t - t0 > 60; });
  push("done", [](App& a, float t) { if (t < 0.5f) return false; if (a.mode == Mode::Explore) a.exitExplore(); return true; });
}

bool zombiesSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) { std::string s = __argv[k]; if (s == "--selftest-zombies") mode = 1; else if (s == "--capture-heavies") mode = 2; else if (s == "--capture-ch1") mode = 5; else if (s == "--capture-fighter2") mode = 3; else if (s == "--capture-fighter2-slam") mode = 4; else if (s == "--capture-standoff") mode = 6; } }
  if (mode < 1 || mode > 6) return false;
  if (dt < 0) { if (!pendingShot.empty()) { app.screenshot(pendingShot); pendingShot.clear(); } return true; }
  if (!started) { started = true; ch1Capture = mode == 5; if (mode == 5) mode = 2; T = new MpTest(ch1Capture ? "ch1" : mode == 2 ? "heavies" : mode == 3 ? "fighter2" : mode == 4 ? "fighter2_slam" : mode == 6 ? "standoff" : "zombies"); if (mode == 6) buildStandoff(app); else if (mode == 2) buildCapture(app); else if (mode == 3) buildFighterCapture(app); else if (mode == 4) buildSlamCapture(app); else build(app); cur = 0; st = 0; }
  if (cur < seq.size()) {
    bool done = seq[cur].fn(app, st);
    st += dt; total += dt;
    if (done) { ++cur; st = 0; }
    if (total > 900) { T->check(false, "zombies test timed out in step '" + seq[std::min(cur, seq.size() - 1)].name + "'"); cur = seq.size(); }
  }
  if (cur >= seq.size() && !app.selftest->done) { app.selftest->exitCode = T->finish(); app.selftest->done = true; }
  return true;
}
