// --selftest-zombies (G2): the zombie AI type on the shipped 3-point village. A zombie paths to a standing player and
// its blows land (35 each, cooldown spacing); it moves between jog and sprint speed; it never runs the soldier brain
// (no cover, utility scores, weapon, grenades or shots); in Solo it picks the closest combatant; the player's knife
// kills a soldier; a mixed Team Deathmatch (soldiers v zombies) runs with kills on both sides. Z1: the zombie wears
// content/characters/zombie.glb: its clips load, every hit-capsule joint maps, the walk animates, a strike plays an
// attack clip, head and leg rays hit their zones.
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "ai_core.h"
#include <algorithm>
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

bool startMode(App& a, GameMode m, int solo, int teamA, int teamB, AiType ai0, AiType ai1) {
  if (a.mode == Mode::Explore) a.exitExplore();
  a.openMap(contentDir() / "examples" / "village_assault_3.nmap.json");
  json scj = a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object();
  Scenario sc = Scenario::fromJson(scj);
  sc.rules.mode = m; sc.rules.killCap = 999; sc.rules.soloPlayers = solo; sc.rules.teamPlayers[0] = teamA; sc.rules.teamPlayers[1] = teamB;
  sc.rules.respawnDelay = 2.0f; sc.rules.humanTeam = 0; sc.rules.aiType[0] = ai0; sc.rules.aiType[1] = ai1;
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

void build(App&) {
  push("headless", [](App&, float) {
    json mv = json::parse(readTextFile(contentDir() / "settings" / "movement.json"));
    float jog = mv.value("run_mps", 3.1625f), spr = mv.value("sprint_mps", 6.325f);
    chk(aiZombieSpeed() > jog && aiZombieSpeed() < spr, "zombie.json speed %.2f m/s lies between jog %.2f and sprint %.2f", aiZombieSpeed(), jog, spr);
    return true; });
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
    if ((k0 < 1 || k1 < 1) && t < 150) return false;
    // the zombies' kill depends on reaching 3 blows on one soldier before the rifles drop them: a kill's worth of landed blows also counts
    chk(k0 >= 1 && (k1 >= 1 || blows >= 3), "both sides fight: soldiers kill %d, zombies kill %d with %d blows landed in %.0f s (closest zombie to a soldier %.1f m)", k0, k1, blows, t, minD);
    pendingShot = outDir() / "zombies_tdm.png";
    return true; });
  push("done", [](App& a, float t) { if (t < 0.5f) return false; if (a.mode == Mode::Explore) a.exitExplore(); return true; });
}
}   // namespace

bool zombiesSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-zombies") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) { if (!pendingShot.empty()) { app.screenshot(pendingShot); pendingShot.clear(); } return true; }
  if (!started) { started = true; T = new MpTest("zombies"); build(app); cur = 0; st = 0; }
  if (cur < seq.size()) {
    bool done = seq[cur].fn(app, st);
    st += dt; total += dt;
    if (done) { ++cur; st = 0; }
    if (total > 600) { T->check(false, "zombies test timed out in step '" + seq[std::min(cur, seq.size() - 1)].name + "'"); cur = seq.size(); }
  }
  if (cur >= seq.size() && !app.selftest->done) { app.selftest->exitCode = T->finish(); app.selftest->done = true; }
  return true;
}
