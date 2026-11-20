// BR7 --selftest-royale-live: a 20-bot battle royale on Newtown in the real loop (AI, navmesh, drop, storm, loot).
// After every bot has landed it runs 60 s more, then checks that at least 90 % of the living bots have moved more than
// 10 m from where they landed and either hold a valid path or are in a fight. Each bot's state is logged
// (reason, path, path failures, distance from the navmesh) so a stuck bot shows why. The player rides along, protected.
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "ai_core.h"
#include "royale.h"
#include "loot.h"   // CV1: lootIndoorMap
#include <functional>
#include <cstdarg>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0;
int mode = -1; bool started = false;
MpTest* T = nullptr;
char B[600];
void note(const char* fmt, ...) { va_list a; va_start(a, fmt); std::vsnprintf(B, sizeof B, fmt, a); va_end(a); T->note(B); logf("[royale-live] %s", B); }
void push(const std::string& n, std::function<bool(App&, float)> f) { seq.push_back({n, f}); }
float dXZ(vec3 a, vec3 b) { return glm::length(vec2(a.x - b.x, a.z - b.z)); }
std::vector<vec3> landed; std::vector<double> landedAt;

void build(App&) {
  push("start", [](App& a, float t) {
    (void)t;
    if (a.mode == Mode::Explore) a.exitExplore();
    const char* mp = std::getenv("NMK_ROYALE_MAP");   // NT3: another map (outpost_desert, outpost_woods); its own player count
    a.openMap(contentDir() / "templates" / (std::string(mp && *mp ? mp : "newtown") + ".nmap.json"));
    json scj = a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object();
    Scenario sc = Scenario::fromJson(scj);
    sc.rules.mode = GameMode::BattleRoyale; if (!(mp && *mp)) sc.rules.royalePlayers = 21;
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(a);
    gGame->pendingStart = true;
    bool ok = a.enterExplore() && gGame->active;
    a.useScripted = true; a.scriptedInput = ExploreInput{};
    T->check(ok && gGame->match.mode() == GameMode::BattleRoyale, "battle royale on " + a.doc.name + " starts (" + gGame->setupError + ")");
    landed.assign(gGame->sold.size(), vec3(1e9f)); landedAt.assign(gGame->sold.size(), -1);
    return true; });
  push("drop", [](App& a, float t) {
    Game& G = *gGame; (void)a;
    if (Soldier* L = G.local()) if (L->alive) { L->hp = 100; L->protectUntil = G.t + 5; }
    int air = 0, alive = 0;
    for (auto& s : G.sold) {
      if (!s.alive || s.localPlayer) continue;
      ++alive;
      if (royaleAirborne(s.slot) || s.lifeId == 0) { ++air; continue; }
      if (landedAt[s.slot] < 0) {
        landedAt[s.slot] = G.t; landed[s.slot] = s.feet;
        NavRef r; vec3 on; bool nav = G.nav.closest(s.feet, 3.0f, r, on);
        note("bot %d landed at (%.0f %.1f %.0f)%s", s.slot, s.feet.x, s.feet.y, s.feet.z, nav ? "" : "  OFF THE NAVMESH");
      }
    }
    if (air == 0 && alive > 0) { note("all %d bots down after %.0f s", alive, t); return true; }
    if (t > 120) { T->check(false, "every bot lands within 120 s (" + std::to_string(air) + " still airborne)"); return true; }
    return false; });
  push("routes", [](App& a, float t) {   // CV1: on a map with TDM team spawns, a navmesh path from Team A's first spawn to every Team B spawn, and rock overhead on an indoor map
    (void)t;
    Game& G = *gGame;
    json scj = a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object();
    Scenario sc = Scenario::fromJson(scj);
    const SpawnMarker* A0 = nullptr; for (auto& m : sc.spawns) if (m.team == 0) { A0 = &m; break; }
    if (!A0) return true;
    int nB = 0, okB = 0, roofed = 0, nAll = 0; float longest = 0;
    for (auto& m : sc.spawns) {
      if (m.team == 0 || m.team == 1) { ++nAll; Physics::Hit h; roofed += Physics::castRayEx(m.pos + vec3(0, 1.9f, 0), vec3(0, 1, 0), 80.0f, h); }
      if (m.team != 1) continue;
      ++nB; std::vector<vec3> pts; float len = 0;
      bool ok = G.nav.pathSync(A0->pos, m.pos, pts, nullptr, nullptr, &len) && !pts.empty() && glm::length(pts.back() - m.pos) < 3.0f;
      okB += ok; if (ok) longest = std::max(longest, len);
      if (!ok) note("no route from Team A to %s at (%.0f %.1f %.0f)", m.label.c_str(), m.pos.x, m.pos.y, m.pos.z);
    }
    T->check(okB == nB, "routes: Team A reaches all " + std::to_string(nB) + " Team B spawns on the navmesh (" + std::to_string(okB) + " ok, longest " + std::to_string((int)longest) + " m)");
    if (lootIndoorMap(a)) T->check(roofed == nAll, "indoor map: rock overhead at all " + std::to_string(nAll) + " team spawns (" + std::to_string(roofed) + ")");
    return true; });
  push("ring shot", [](App& a, float t) {   // BR12 (NMK_ROYALE_RING): the storm wall from inside, buildings in front
    if (!std::getenv("NMK_ROYALE_RING")) return true;
    Game& G = *gGame; Soldier* L = G.local();
    if (!L || !L->alive) { T->check(false, "ring shot: the player is alive"); return true; }
    L->hp = 100; L->protectUntil = G.t + 5;
    static bool placed = false;
    if (!placed) {
      if (royaleAirborne(L->slot)) return t > 150 ? (T->check(false, "ring shot: the player lands"), true) : false;
      placed = true;
      royaleSetTime(260);   // circle 2 standing
      RoyaleRing r = royaleRing(false);
      vec2 dir = vec2(L->feet.x, L->feet.z) - r.c; dir = glm::length(dir) > 1 ? glm::normalize(dir) : vec2(1, 0);
      vec2 p = r.c + dir * std::max(0.0f, r.r - 50.0f);
      vec3 feet(p.x, a.terrain.height(p.x, p.y), p.y);
      NavRef nr; vec3 on; if (G.nav.closest(feet, 20.0f, nr, on)) feet = on;
      Physics::setCharacterPos(feet + vec3(0, 0.05f, 0));
      a.eyeYaw = degrees(std::atan2(-dir.x, -dir.y)); a.eyePitch = 6;
      note("ring shot: circle r %.0f m at (%.0f %.0f), player at (%.0f %.0f), facing the wall", r.r, r.c.x, r.c.y, feet.x, feet.z);
      return false;
    }
    if (t > 2.0f && t < 2.05f) a.uiShot = exeDir() / "selftest_out" / "br12_ring.png";
    if (t < 2.5f) return false;
    seq.erase(seq.begin() + cur + 1, seq.end() - 1);   // straight to "done"
    return true; });
  push("play 60 s", [](App& a, float t) {
    Game& G = *gGame; (void)a;
    if (Soldier* L = G.local()) if (L->alive) { L->hp = 100; L->protectUntil = G.t + 5; }
    for (auto& s : G.sold) if (s.alive && !s.localPlayer) s.hp = std::max(s.hp, 60.0f);   // nobody dies of the storm while we watch movement
    if (t < 60) return false;
    AiCore* A = aiCore();
    int ok = 0, n = 0;
    for (auto& s : G.sold) {
      if (!s.alive || s.localPlayer || landedAt[s.slot] < 0) continue;
      ++n;
      BotMind& m = A->mind(s);
      float moved = dXZ(s.feet, landed[s.slot]);
      bool fight = s.br.target >= 0 || G.t - s.lastShot < 5;
      bool good = moved > 10 && (m.hasPath || fight);
      ok += good;
      NavRef r; vec3 on; bool nav = G.nav.closest(s.feet, 3.0f, r, on);
      note("bot %2d %s moved %5.1f m  path %d%s (fails %.1f)  fight %d  nav %d  goal (%.0f %.0f)  why: %s", s.slot, good ? "ok   " : "STUCK", moved, (int)m.hasPath, m.partial ? " partial" : "", m.pathFails, (int)fight, (int)nav, m.goal.x, m.goal.z, s.br.why.c_str());
    }
    std::snprintf(B, sizeof B, "60 s after landing: %d of %d living bots moved more than 10 m and hold a path or fight (need 90 %%)", ok, n);
    T->check(n > 0 && ok >= (int)std::ceil(0.9f * n), B);
    return true; });
  push("done", [](App& a, float t) { if (t < 0.3f) return false; if (a.mode == Mode::Explore) a.exitExplore(); return true; });
}
}   // namespace

bool royaleLiveSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-royale-live") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!started) { started = true; T = new MpTest("royale_live"); build(app); }
  if (cur < seq.size()) {
    bool done = seq[cur].fn(app, st);
    st += dt; total += dt;
    if (done) { ++cur; st = 0; }
    if (total > 400) { T->check(false, "timed out in step '" + seq[std::min(cur, seq.size() - 1)].name + "'"); cur = seq.size(); }
  }
  if (cur >= seq.size() && !app.selftest->done) { app.selftest->exitCode = T->finish(); app.selftest->done = true; }
  return true;
}

// CV1 fix check: --selftest-tdm-live. A big Team Deathmatch in the real loop (NMK_ROYALE_MAP, default tunnels;
// NMK_TDM_PLAYERS per team, default 240) while the player is teleported onto every team spawn marker in turn: a shot
// facing the marker's way and one looking up from each (selftest_out/tdmlive_NNN.png; analysed for white-outs).
namespace tdmlive {
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0; int mode = -1; bool started = false; MpTest* T = nullptr;
std::vector<SpawnMarker> marks; size_t mi = 0; int shot = 0;
void build(App&) {
  seq.push_back({"start", [](App& a, float) {
    if (a.mode == Mode::Explore) a.exitExplore();
    const char* mp = std::getenv("NMK_ROYALE_MAP");
    a.openMap(contentDir() / "templates" / (std::string(mp && *mp ? mp : "tunnels") + ".nmap.json"));
    json scj = a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object();
    Scenario sc = Scenario::fromJson(scj);
    const char* np = std::getenv("NMK_TDM_PLAYERS"); int n = np ? std::max(2, std::atoi(np)) : 240;
    sc.rules.mode = GameMode::TDM; sc.rules.teamPlayers[0] = sc.rules.teamPlayers[1] = n;
    for (auto& m : sc.spawns) if (m.team == 0 || m.team == 1) marks.push_back(m);
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(a);
    gGame->pendingStart = true;
    bool ok = a.enterExplore() && gGame->active;
    a.useScripted = true; a.scriptedInput = ExploreInput{};
    T->check(ok && gGame->match.mode() == GameMode::TDM, "team deathmatch on " + a.doc.name + " starts with " + std::to_string(gGame->sold.size()) + " soldiers (" + gGame->setupError + ")");
    return true; }});
  seq.push_back({"warm up", [](App& a, float t) { if (Soldier* L = gGame->local()) { L->hp = 100; L->protectUntil = gGame->t + 5; }
    if (std::getenv("NMK_AIM_TRACE") && t > 25.0f) { a.selftest->exitCode = 0; a.selftest->done = true; }   // AN1 step 3: the trace run ends after the warm-up
    return t > 20.0f && !std::getenv("NMK_AIM_TRACE"); }});
  seq.push_back({"spawn tour", [](App& a, float t) {
    Game& G = *gGame;
    if (Soldier* L = G.local()) { L->hp = 100; L->protectUntil = G.t + 5; }
    if (mi >= marks.size()) return true;
    float ph = std::fmod(t, 2.0f);
    static float last = -1; static int step = 0;
    if (t - last >= 2.0f || last < 0) {   // every 2 s: the next marker (shot looking ahead at 0.8 s, up at 1.6 s)
      last = t; step = 0;
      const SpawnMarker& m = marks[mi];
      Physics::setCharacterPos(m.pos + vec3(0, 0.05f, 0)); a.eyeYaw = m.yaw; a.eyePitch = 0;
    }
    char nm[64];
    if (ph > 0.8f && step == 0) { std::snprintf(nm, sizeof nm, "tdmlive_%03d_a.png", (int)mi); a.uiShot = exeDir() / "selftest_out" / nm; step = 1; }
    if (ph > 1.2f && step == 1) { a.eyePitch = 55; step = 2; }
    if (ph > 1.6f && step == 2) { std::snprintf(nm, sizeof nm, "tdmlive_%03d_b.png", (int)mi); a.uiShot = exeDir() / "selftest_out" / nm; step = 3; ++mi; ++shot; }
    return false; }});
  seq.push_back({"done", [](App& a, float t) { if (t < 0.3f) return false; T->check(shot > 0, std::to_string(shot) + " spawn markers toured (2 shots each)"); if (a.mode == Mode::Explore) a.exitExplore(); return true; }});
}
}   // namespace tdmlive

bool tdmLiveSelfTest(App& app, float dt) {
  namespace L = tdmlive;
  if (L::mode < 0) { L::mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-tdm-live") L::mode = 1; }
  if (L::mode != 1) return false;
  if (dt < 0) return true;
  if (!L::started) { L::started = true; L::T = new MpTest("tdm_live"); L::build(app); }
  if (L::cur < L::seq.size()) {
    bool done = L::seq[L::cur].fn(app, L::st);
    L::st += dt; L::total += dt;
    if (done) { ++L::cur; L::st = 0; }
    if (L::total > 600) { L::T->check(false, "timed out in step '" + L::seq[std::min(L::cur, L::seq.size() - 1)].name + "'"); L::cur = L::seq.size(); }
  }
  if (L::cur >= L::seq.size() && !app.selftest->done) { app.selftest->exitCode = L::T->finish(); app.selftest->done = true; }
  return true;
}
