// CQ1 --selftest-cq1: two hostile bots 0.6 m apart, chest to chest (each rifle's muzzle past the other's torso): a shot from
// one hurts the other. Before CQ1 the round started at the muzzle, already behind the target, and never crossed its hit zones.
// A team match on the village map; one bot of each side held facing each other in front of the player at full health while
// their poses settle, then each fires once through Game::soldierFire (the bots' own path); the other's health must drop.
// selftest_out/report_cq1.txt.
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "physics.h"

namespace {
vec3 xz(vec3 v) { v.y = 0; return v; }
MpTest* T = nullptr;
int mode = -1, phase = 0, shooterIdx = 0; float st = 0; int aSlot = -1, bSlot = -1;
bool start(App& a) {
  if (a.mode == Mode::Explore) a.exitExplore();
  a.openMap(contentDir() / "examples/village_assault_3.nmap.json");
  json scj = a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object();
  Scenario sc = Scenario::fromJson(scj);
  sc.rules.mode = GameMode::TDM; sc.rules.killCap = 999; sc.rules.teamPlayers[0] = 3; sc.rules.teamPlayers[1] = 3; sc.rules.humanTeam = 0;
  for (int t = 0; t < 2; ++t) { for (int& x : sc.rules.aiCount[t]) x = 0; sc.rules.aiTypes[t] = aiBit(AiType::Soldiers); }
  gUi.loaded = false; gUi.matchScenario = sc.toJson();
  if (!gGame) gGame = new Game(a);
  gGame->pendingStart = true;
  bool ok = a.enterExplore() && gGame->active;
  a.useScripted = true; a.scriptedInput = ExploreInput{};
  return ok;
}
}   // namespace

bool cq1SelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-cq1") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!T) T = new MpTest("cq1");
  auto finish = [&]() { if (app.mode == Mode::Explore) app.exitExplore(); app.selftest->exitCode = T->finish(); app.selftest->done = true; };
  st += dt;
  if (phase == 0) { bool ok = start(app); T->check(ok, "the match starts"); if (!ok) { finish(); return true; } phase = 1; st = 0; return true; }
  Game& g = *gGame; Soldier* L = g.local();
  if (phase == 1) {   // one living bot of each side
    if (!L || !L->alive) { if (st > 30) { T->check(false, "the player spawns"); finish(); } return true; }
    aSlot = bSlot = -1;
    for (auto& s : g.sold) if (s.alive && !s.localPlayer && s.agent >= 0) { if (aSlot < 0) aSlot = s.slot; else if (bSlot < 0 && Game::hostile(g.sold[aSlot], s)) bSlot = s.slot; }
    if (aSlot >= 0 && bSlot >= 0) { phase = 2; st = 0; } else if (st > 30) { T->check(false, "two hostile bots alive"); finish(); }
    return true;
  }
  Soldier& A = g.sold[aSlot]; Soldier& B = g.sold[bSlot];
  Soldier& S = shooterIdx == 0 ? A : B; Soldier& V = shooterIdx == 0 ? B : A;
  // held: 0.6 m apart, facing each other, 10 m in front of the player, never hurt while their poses settle
  vec3 f(-std::sin(radians(app.eyeYaw)), 0, -std::cos(radians(app.eyeYaw))), r(f.z, 0, -f.x);
  vec3 c = L->feet + f * 10.0f; c.y = app.terrain.height(c.x, c.z) + 0.05f;
  vec3 pa = c - r * 0.3f, pb = c + r * 0.3f;
  auto hold = [&](Soldier& s, vec3 p, vec3 at) { Physics::setAgentPos(s.agent, p); s.feet = p; s.yaw = degrees(std::atan2(-(at.x - p.x), -(at.z - p.z))); s.br.aimYaw = s.yaw; s.br.aimPitch = 0; s.protectUntil = -1; };
  hold(A, pa, pb); hold(B, pb, pa);
  if (phase == 2) {   // settle 0.6 s at full health; then one shot
    A.hp = B.hp = 100;
    if (st < 0.6f) return true;
    if (!A.alive || !B.alive || S.weapon < 0) { T->check(false, "both bots alive and armed before the shot"); finish(); return true; }
    const WeaponDef& d = *app.weaponDefs[S.weapon];
    vec3 e = g.eye(S), muz = vec3(S.world * S.pose.weapon * vec4(d.muzzle, 1)), dir = glm::normalize(g.chest(V) - e);
    char b[240]; std::snprintf(b, sizeof b, "shooter %d (%s) -> %d: %.2f m apart; eye to muzzle %.2f m (the muzzle %.2f m past the target's feet along the line)", S.slot, d.id.c_str(), V.slot,
                               glm::length(xz(A.feet - B.feet)), glm::length(muz - e), glm::dot(xz(muz - V.feet), glm::normalize(xz(V.feet - S.feet))));
    T->note(b);
    S.ws.loaded = std::max(S.ws.loaded, 1); S.ws.cooldown = 0; S.reload = 0; S.ws.bolt = 0;
    bool fired = g.soldierFire(S, dir);
    T->check(fired, std::string("the shot is fired (") + d.id + ")");
    phase = 3; st = 0; return true;
  }
  if (phase == 3) {   // the round resolves on the next steps
    if (st < 0.3f) return true;
    char b[160]; std::snprintf(b, sizeof b, "point blank (0.6 m): the target's health %.0f after one shot (was 100)%s", V.hp, V.alive ? "" : ", killed");
    T->check(!V.alive || V.hp < 100, b);
    if (shooterIdx == 0) { shooterIdx = 1; phase = 2; st = 0; return true; }
    finish();
  }
  return true;
}
