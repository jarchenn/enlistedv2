// AI5 --check-turn-engage: no suite, a look at the facing rule. TDM on the village map: a soldier bot with an enemy
// soldier placed 15 m straight behind it (both unhurt, the player a camera 9 m to the side). Logged every 0.1 s: the
// body yaw's error to the enemy and the aim's twist off the body; the first shot's time and body error. Frames at 0.15,
// 0.4 and 1.0 s (selftest_out/turn_*.png).
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "ai_core.h"

namespace { int phase = 0; double t0 = 0; int A = -1, B = -1; double shot0 = -100; fs::path pend; bool logged = false; }

bool turnEngageSelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--check-turn-engage") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) { if (!pend.empty()) { a.screenshot(pend); pend.clear(); } return true; }
  auto finish = [&]() { if (!a.selftest->done) { a.selftest->exitCode = 0; a.selftest->done = true; } return true; };
  if (phase == 0) {
    a.openMap(contentDir() / "examples/village_assault_3.nmap.json");
    json scj = a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object();
    Scenario sc = Scenario::fromJson(scj);
    bool ops = std::getenv("NMK_TURN_OPS") != nullptr;   // AI5 operators: both sides Operators, 10 a side, their weapons logged
    sc.rules.mode = GameMode::TDM; sc.rules.killCap = 999; sc.rules.soloPlayers = ops ? 21 : 3; sc.rules.teamPlayers[0] = ops ? 11 : 2; sc.rules.teamPlayers[1] = ops ? 10 : 1;
    sc.rules.respawnDelay = 2.0f; sc.rules.humanTeam = 0;
    sc.rules.aiTypes[0] = aiBit(ops ? AiType::Operators : AiType::Soldiers); sc.rules.aiTypes[1] = aiBit(ops ? AiType::Operators : AiType::Soldiers);
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(a);
    gGame->pendingStart = true; a.doc.start = {true, vec3(0, 0, 30), 180};
    logf("[turn] start %s", a.enterExplore() && gGame->active ? "ok" : "FAILED");
    a.useScripted = true; a.scriptedInput = ExploreInput{};
    phase = 1; t0 = a.now; return true;
  }
  Game& G = *gGame; Soldier* L = G.local();
  if (!L) return a.now - t0 > 30 ? finish() : true;
  if (phase == 1) {
    bool all = true; for (auto& s : G.sold) if (!s.alive) all = false;
    if (!all) return a.now - t0 > 30 ? finish() : true;
    for (auto& s : G.sold) if (!s.human) { if (s.team == L->team && A < 0) A = s.slot; else if (s.team != L->team && B < 0) B = s.slot; }
    if (A < 0 || B < 0) { logf("[turn] FAIL: bots A %d B %d", A, B); return finish(); }
    for (auto& s : G.sold) if (!s.human) {
      std::string w = s.weapon >= 0 ? a.weaponDefs[s.weapon]->id : "-", c;
      for (int k : s.carried) c += a.weaponDefs[k]->id + " ";
      logf("[turn] bot %d team %d op %d: weapon %s carried %s", s.slot, s.team, (int)s.op, w.c_str(), c.c_str());
    }
    Soldier& sa = G.sold[A]; Soldier& sb = G.sold[B];
    vec3 f = App::forward(sa.yaw, 0); f.y = 0; f = glm::normalize(f);
    vec3 pb = sa.feet - f * 15.0f; NavRef r; vec3 on; if (G.nav.closest(pb, 4.0f, r, on)) pb = on;
    sb.feet = pb; if (sb.agent >= 0) Physics::setAgentPos(sb.agent, pb);
    vec3 side(-f.z, 0, f.x); vec3 pl = sa.feet + side * 9.0f; if (G.nav.closest(pl, 4.0f, r, on)) pl = on;
    Physics::setCharacterPos(pl); L->feet = pl;
    vec3 d = sa.feet - pl; a.eyeYaw = glm::degrees(std::atan2(-d.x, -d.z)); a.eyePitch = -4;
    shot0 = sa.lastShot; phase = 2; t0 = G.t;   // game time: a load hitch must not eat the window logf("[turn] bot %d faces away from enemy %d 15 m behind it", A, B); return true;
  }
  Soldier& sa = G.sold[A]; Soldier& sb = G.sold[B];
  for (Soldier* s : {&sa, &sb, L}) { s->hp = 100; s->protectUntil = G.t + 1; }
  float since = (float)(G.t - t0);
  vec3 to = sb.feet - sa.feet; float want = glm::degrees(std::atan2(-to.x, -to.z));
  auto ad = [](float x) { x = std::fmod(x + 540.0f, 360.0f) - 180.0f; return x; };
  float bodyErr = ad(sa.yaw - want), twist = ad(sa.br.aimYaw - sa.yaw);
  static float nextLog = 0; if (since >= nextLog) { nextLog += 0.1f; logf("[turn] t %.2f body error %.0f deg, aim twist %.0f deg, target %d, why %s", since, bodyErr, twist, sa.br.target, sa.br.why.c_str()); }
  if (!logged && sa.lastShot > shot0) { logged = true; logf("[turn] first shot at %.2f s, body error %.1f deg, aim twist %.1f deg", since, bodyErr, twist); }
  for (float w : {0.15f, 0.4f, 1.0f}) if (since >= w && since - dt < w) { char nm[64]; std::snprintf(nm, sizeof nm, "turn_%.2f.png", w); pend = exeDir() / "selftest_out" / nm; }
  if (since > 6.0f) { if (!logged) logf("[turn] no shot within 6 s"); return finish(); }
  return true;
}
