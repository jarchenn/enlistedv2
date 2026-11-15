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
    a.openMap(contentDir() / "templates" / "newtown.nmap.json");
    json scj = a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object();
    Scenario sc = Scenario::fromJson(scj);
    sc.rules.mode = GameMode::BattleRoyale; sc.rules.royalePlayers = 21;
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(a);
    gGame->pendingStart = true;
    bool ok = a.enterExplore() && gGame->active;
    a.useScripted = true; a.scriptedInput = ExploreInput{};
    T->check(ok && gGame->match.mode() == GameMode::BattleRoyale, "battle royale on Newtown starts (" + gGame->setupError + ")");
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
