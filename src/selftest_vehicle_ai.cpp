// --check-vehicle-ai: no suite, two short headless checks of where the AI thinks the player is (TDM, the player v four
// zombies on the village map):
//   vehicle  the player in a jeep that is moved 100 m: the seated body, the local soldier, the AI's nearest-human
//            distance and the zombies' chase goals follow the jeep; the frame culls from the vehicle camera
//   death    the player killed: no bot keeps it as a target or in team knowledge; after the respawn the zombies chase the
//            new position, not the spot where it fell
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "ai_core.h"
#include <cstdarg>

namespace {
MpTest* T = nullptr; int phase = 0; double t0 = 0; vec3 carAt{0}, diedAt{0}; uint64_t jeep = 0;
float dXZ(vec3 a, vec3 b) { return glm::length(vec2(a.x - b.x, a.z - b.z)); }
bool chk(bool ok, const char* fmt, ...) { char b[400]; va_list v; va_start(v, fmt); std::vsnprintf(b, sizeof b, fmt, v); va_end(v); return T->check(ok, b); }
int zombiesOnLocal(Game& G, vec3 near, float r, int& total) {
  AiCore* A = aiCore(); int n = 0; total = 0; if (!A) return 0;
  for (auto& s : G.sold) if (s.alive && s.zombie && s.br.target == G.localSlot) { ++total; BotMind& m = A->mind(s); if (dXZ(m.goal, near) < r) ++n; }
  return n;
}
}

bool vehicleAiSelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--check-vehicle-ai") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  auto finish = [&]() { if (!a.selftest->done) { a.selftest->exitCode = T->fails ? 1 : 0; a.selftest->done = true; logf("[vehicle-ai] %d passed, %d failed", T->passes, T->fails); } return true; };
  if (!T) T = new MpTest("vehicle-ai");
  Game* G = gGame;
  if (phase == 0) {   // the match
    a.openMap(contentDir() / "examples/village_assault_3.nmap.json");
    json scj = a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object();
    Scenario sc = Scenario::fromJson(scj);
    sc.rules.mode = GameMode::TDM; sc.rules.killCap = 999; sc.rules.soloPlayers = 5; sc.rules.teamPlayers[0] = 1; sc.rules.teamPlayers[1] = 4;
    sc.rules.respawnDelay = 2.0f; sc.rules.humanTeam = 0; sc.rules.aiTypes[0] = aiBit(AiType::Soldiers); sc.rules.aiTypes[1] = aiBit(AiType::Zombies);
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(a);
    gGame->pendingStart = true; a.doc.start = {true, vec3(0, 0, 30), 180};
    chk(a.enterExplore() && gGame->active, "TDM: the player v four zombies (%s)", gGame->setupError.c_str());
    a.useScripted = true; a.scriptedInput = ExploreInput{};
    phase = 1; t0 = a.now; return true;
  }
  Soldier* L = G ? G->local() : nullptr;
  if (!L) { if (a.now - t0 > 30) { chk(false, "the local soldier exists"); return finish(); } return true; }
  if (phase == 1) {   // everyone spawned: a jeep next to the player, in it, then the jeep moved 100 m
    bool all = true; for (auto& s : G->sold) if (!s.alive) all = false;
    if (!all) return true;
    L->protectUntil = G->t + 60; L->hp = 100;
    const AssetDef* as = a.lib.find("car_jeep_willys");
    if (!as) { chk(false, "jeep asset"); return finish(); }
    vec3 p = L->feet + vec3(5, 0, 0); p.y = a.terrain.height(p.x, p.z);
    jeep = a.placeInstance(as, p, 0); a.syncRuntime(); a.tanks.sync(a);
    chk(a.tanks.enter(a, jeep), "in the jeep");
    TankRT* t = a.tanks.get(jeep);
    if (t) { t->pos.x += 100; t->pos.y = a.terrain.height(t->pos.x, t->pos.z); carAt = t->pos; }
    phase = 2; t0 = a.now; return true;
  }
  if (phase == 2) {
    L->protectUntil = G->t + 60; L->hp = 100;
    if (a.now - t0 < 3.0) return true;
    TankRT* t = a.tanks.get(jeep); AiCore* A = aiCore();
    vec3 e, f; G->renderView(e, f); vec3 ce, cf; float fov; a.tanks.camera(a, ce, cf, fov);
    int total = 0, onCar = zombiesOnLocal(*G, t ? t->pos : carAt, 12.0f, total);
    chk(t && dXZ(L->feet, t->pos) < 2.5f, "the seated body rides with the jeep: local soldier %.1f m from it (it moved 100 m)", t ? dXZ(L->feet, t->pos) : -1.0f);
    chk(A && t && A->nearestHumanDist(t->pos) < 3.0f, "the AI's nearest human is at the jeep (%.1f m)", A && t ? A->nearestHumanDist(t->pos) : -1.0f);
    chk(glm::length(e - ce) < 1e-3f, "the frame culls from the vehicle camera (%.2f m from it; the seated eye is %.1f m away)", glm::length(e - ce), glm::length(a.eyePos() - ce));
    chk(total > 0 && onCar == total, "zombies chase the jeep: %d of %d zombies on the player head for within 12 m of it", onCar, total);
    a.tanks.exitTank(a, true);
    phase = 3; t0 = a.now; return true;
  }
  if (phase == 3) {   // the player falls
    diedAt = L->feet; L->protectUntil = -1; G->killSoldier(G->localSlot, -1, vec3(0, -1, 0));
    AiCore* A = aiCore(); int targeting = 0, known = 0;
    for (auto& s : G->sold) if (s.alive && s.br.target == G->localSlot) ++targeting;
    if (A) for (auto& tb : A->teams) known += (int)tb.known.count(G->localSlot);
    chk(!L->alive && targeting == 0 && known == 0, "killed: %d bots still target it, %d team-knowledge entries of it", targeting, known);
    phase = 4; t0 = a.now; return true;
  }
  if (phase == 4) {   // respawned elsewhere: chased there
    if (!L->alive) { if (a.now - t0 > 25) { chk(false, "the player respawns"); return finish(); } return true; }
    L->protectUntil = G->t + 60; L->hp = 100;
    if (phase == 4 && a.now - t0 < 30 && dXZ(L->feet, diedAt) < 20.0f) {   // respawned near the death spot: move it away to see the difference
      vec3 q = L->feet + vec3(0, 0, 60); NavRef r; vec3 on; if (G->nav.closest(q, 8.0f, r, on)) { Physics::setCharacterPos(on); }
    }
    phase = 5; t0 = a.now; return true;
  }
  if (phase == 5) {
    L->protectUntil = G->t + 60; L->hp = 100;
    if (a.now - t0 < 3.0) return true;
    int total = 0, onNew = zombiesOnLocal(*G, L->feet, 12.0f, total), t2 = 0, onOld = zombiesOnLocal(*G, diedAt, 6.0f, t2);
    chk(total > 0 && onNew == total && onOld == 0, "after the respawn (%.0f m from where it fell): %d of %d zombies chase the new position, %d head for the old one", dXZ(L->feet, diedAt), onNew, total, onOld);
    return finish();
  }
  return true;
}
