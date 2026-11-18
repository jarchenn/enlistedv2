// SD9 --capture-sd9: the bomb on the M map in Search and Destroy. The local player attacks (bots on both sides, the AI
// frozen). Carried (grey, at the carrier), dropped (blue, where it lies), planted (red, pulsing, the site letter): what
// sdBombMarkFor gives the attackers and the defenders (planted only), and a capture of the large map in each state
// (selftest_out/sd9_<state>.png). selftest_out/report_sd9.txt.
#include "selftest.h"
#include "sd_mode.h"
#include "sd_game.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "ai_core.h"

namespace {
MpTest* T = nullptr;
int mode = -1, stage = 0, frames = 0; bool started = false; float total = 0;
const char* name(SdBombMark k) { return k == SdBombMark::Carried ? "carried" : k == SdBombMark::Dropped ? "dropped" : k == SdBombMark::Planted ? "planted" : "none"; }
SdBombMark viewAs(Game& g, int slot, vec3& p) { const int keep = g.localSlot; g.localSlot = slot; SdBombMark k = sdBombMarkFor(g, p); g.localSlot = keep; return k; }
}   // namespace

bool sd9Capture(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-sd9") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!started) {
    started = true; T = new MpTest("sd9");
    if (app.mode == Mode::Explore) app.exitExplore();
    app.openMap(contentDir() / "templates/sd_desert.nmap.json");
    Scenario sc = Scenario::fromJson(app.doc.extra.is_object() ? app.doc.extra.value("scenario", json::object()) : json::object());
    sc.rules.mode = GameMode::SearchDestroy; sc.rules.humanTeam = 1; sc.rules.sdWarmup = false;   // round 1: team 1 attacks
    for (int t = 0; t < 2; ++t) { for (int& x : sc.rules.aiCount[t]) x = 0; sc.rules.aiCount[t][(int)AiType::Soldiers] = t == 1 ? 4 : 5; sc.rules.teamPlayers[t] = 5; sc.rules.aiTypes[t] = aiBit(AiType::Soldiers); }
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(app);
    gGame->pendingStart = true;
    bool ok = app.enterExplore() && gGame->active;
    app.useScripted = true; app.scriptedInput = ExploreInput{};
    T->check(ok && sdMatch(), "Search and Destroy starts, the player attacking");
    if (SdMatch* m = sdMatch()) { m->cfg.freeze = 2; m->cfg.roundTime = 300; }
    return true;
  }
  total += dt; ++frames;
  Game& g = *gGame; SdMatch* mp = sdMatch();
  auto finish = [&]() { app.mapLarge = false; if (app.mode == Mode::Explore) app.exitExplore(); app.selftest->exitCode = T->finish(); app.selftest->done = true; };
  if (!mp || !g.active || total > 60) { if (total > 60) T->check(false, "timed out at stage " + std::to_string(stage)); finish(); return true; }
  SdMatch& m = *mp;
  if (AiCore* A = aiCore()) A->frozen = true;
  int def = -1; for (auto& s : g.sold) if (s.alive && m.sideOf(s.slot) == 1) { def = s.slot; break; }
  const int me = g.localSlot;
  vec3 p, q; char b[300];
  if (stage == 0 && m.phase == SdPhase::LiveUnplanted && m.t - m.liveStart > 0.5) {
    T->check(me >= 0 && m.sideOf(me) == 0, "the player is an attacker");
    SdBombMark a = viewAs(g, me, p), d = viewAs(g, def, q);
    const bool atCarrier = m.carrier >= 0 && glm::length(p - g.sold[m.carrier].feet) < 0.01f;
    std::snprintf(b, sizeof b, "carried by slot %d: attackers see '%s' at the carrier (%d), defenders see '%s'", m.carrier, name(a), (int)atCarrier, name(d));
    T->check(a == SdBombMark::Carried && atCarrier && d == SdBombMark::None, b);
    app.mapLarge = true; app.uiShot = exeDir() / "selftest_out" / "sd9_carried.png";
    stage = 1; frames = 0; return true;
  }
  if (stage == 1 && frames > 5) {   // dropped where the carrier stood, a few metres off
    const int c = m.carrier; vec3 at = g.sold[c].feet + vec3(4, 0, 4);
    m.onBombDropped(c, at);
    stage = 2; frames = 0; return true;
  }
  if (stage == 2 && frames > 3) {
    SdBombMark a = viewAs(g, me, p), d = viewAs(g, def, q);
    std::snprintf(b, sizeof b, "dropped: attackers see '%s' at the bomb (%.2f m off), defenders see '%s'", name(a), glm::length(p - m.bombPos), name(d));
    T->check(a == SdBombMark::Dropped && glm::length(p - m.bombPos) < 0.01f && d == SdBombMark::None, b);
    app.uiShot = exeDir() / "selftest_out" / "sd9_dropped.png";
    stage = 3; frames = 0; return true;
  }
  if (stage == 3 && frames > 5) {   // planted on site A (the controller's state set directly: the marker is under test, not the plant)
    const Objective& A = g.match.sc.objectives[0];
    m.bomb = SdBomb::Planted; m.carrier = -1; m.bombSite = 0; m.bombPos = A.pos; m.planted = true; m.phase = SdPhase::LivePlanted; m.detonateAt = m.t + 40;
    stage = 4; frames = 0; return true;
  }
  if (stage == 4 && frames > 3) {
    SdBombMark a = viewAs(g, me, p), d = viewAs(g, def, q);
    std::snprintf(b, sizeof b, "planted at A: attackers see '%s', defenders see '%s' (at the bomb: %d)", name(a), name(d), (int)(glm::length(q - m.bombPos) < 0.01f));
    T->check(a == SdBombMark::Planted && d == SdBombMark::Planted && glm::length(q - m.bombPos) < 0.01f, b);
    app.uiShot = exeDir() / "selftest_out" / "sd9_planted.png";
    stage = 5; frames = 0; return true;
  }
  if (stage == 5 && frames > 5) finish();
  return true;
}
