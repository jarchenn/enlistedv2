// SD8 --selftest-sd-time [map]: the time of day and the round clocks in a Search and Destroy match. Bots only (default
// Sun Fort, its own hour 13:00, halves of 2 rounds). Checks: the match opens at the map's hour; the Settings hour set in
// round 1's live phase (21:00) changes the sun (below the horizon, the key light moves) at once and is still in effect in
// round 2 and after the halftime swap (round 3); "Map default" brings 13:00 back; a round length changed mid-round 1
// (the pause menu's rules) is used from round 2, not in round 1. selftest_out/report_sdtime.txt.
#include "selftest.h"
#include "sd_mode.h"
#include "sd_game.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "lighting.h"

void settingsUiHour(App& app, float h);   // settings.cpp
void settingsUiMapHour(App& app);

namespace {
MpTest* T = nullptr;
int mode = -1, stage = 0, frames = 0; bool started = false; float total = 0, round1Len = 0; vec3 sunDay{0}, sunNight{0};
vec3 keyLight(App& app) { return lightingTod(app.R.lt.hour).key; }   // the frame's key light (the sun, the moon at night; R.sunDir is only swapped in during render)
float angleDeg(vec3 a, vec3 b) { return glm::degrees(std::acos(std::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f))); }
}   // namespace

bool sdTimeSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-sd-time") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!started) {
    started = true; T = new MpTest("sdtime");
    std::string map = "templates/sd_desert.nmap.json";
    for (int k = 1; k + 1 < __argc; ++k) if (std::string(__argv[k]) == "--selftest-sd-time" && __argv[k + 1][0] != '-') map = __argv[k + 1];
    if (app.mode == Mode::Explore) app.exitExplore();
    app.openMap(contentDir() / map);
    json scj = app.doc.extra.is_object() ? app.doc.extra.value("scenario", json::object()) : json::object();
    Scenario sc = Scenario::fromJson(scj);
    sc.rules.mode = GameMode::SearchDestroy; sc.rules.humanTeam = 0; sc.rules.sdWarmup = false;
    for (int t = 0; t < 2; ++t) { for (int& x : sc.rules.aiCount[t]) x = 0; sc.rules.aiCount[t][(int)AiType::Soldiers] = t == 0 ? 4 : 5; sc.rules.teamPlayers[t] = 5; sc.rules.aiTypes[t] = aiBit(AiType::Soldiers); }
    gUi.loaded = false; gUi.matchScenario = sc.toJson();
    if (!gGame) gGame = new Game(app);
    gGame->pendingStart = true;
    bool ok = app.enterExplore() && gGame->active;
    app.useScripted = true; app.scriptedInput = ExploreInput{};
    T->check(ok && sdMatch(), "Search and Destroy starts on " + map);
    if (SdMatch* m = sdMatch()) { m->cfg.freeze = 2; m->cfg.roundEnd = 1.0f; m->cfg.halftime = 1.0f; m->cfg.halfRounds = 2; m->cfg.winRounds = 9; round1Len = m->cfg.roundTime; }
    return true;
  }
  total += dt; ++frames;
  Game& g = *gGame; SdMatch* mp = sdMatch();
  auto finish = [&]() { if (app.mode == Mode::Explore) app.exitExplore(); app.selftest->exitCode = T->finish(); app.selftest->done = true; };
  if (!mp || !g.active || total > 150) { if (total > 150) T->check(false, "timed out at stage " + std::to_string(stage)); finish(); return true; }
  SdMatch& m = *mp;
  const bool live = m.phase == SdPhase::LiveUnplanted;
  auto hourIs = [&](float h) { return std::fabs(app.R.lt.hour - h) < 0.01f; };
  char b[300];
  if (stage == 0 && m.round == 1 && live && m.t - m.liveStart > 1.0) {   // round 1 live: the map's hour, then the Settings hour
    std::snprintf(b, sizeof b, "the match opens at the map's hour: %.2f (map %.2f, user override %d, enclosed %d)", app.R.lt.hour, app.R.lt.mapHour, (int)app.R.lt.userHour, (int)app.R.wx.enclosed);
    T->check(hourIs(13.0f) && !app.R.lt.userHour && !app.R.wx.enclosed, b);
    sunDay = keyLight(app); app.uiShot = exeDir() / "selftest_out" / "sd_time_r1_map.png";
    stage = 10; frames = 0; return true;
  }
  if (stage == 10 && frames > 3) {   // after the capture of the map's hour
    settingsUiHour(app, 21.0f);
    g.match.rules.sdRoundTime = 75;   // the pause menu's Round length: from the next round
    stage = 1; frames = 0; return true;
  }
  if (stage == 1 && frames > 5) {
    sunNight = keyLight(app);
    std::snprintf(b, sizeof b, "mid-round: Settings hour 21:00 in effect: hour %.2f, sun elevation %.1f deg (day %.1f), key light moved %.0f deg",
                  app.R.lt.hour, lightingTod(app.R.lt.hour).sunEl, lightingTod(13.0f).sunEl, angleDeg(sunDay, sunNight));
    T->check(hourIs(21.0f) && lightingTod(app.R.lt.hour).sunEl < 0 && angleDeg(sunDay, sunNight) > 5.0f, b);
    std::snprintf(b, sizeof b, "round 1 keeps its round length (%.0f s, the change waits for round 2)", m.roundDeadline - m.liveStart);
    T->check(std::fabs((float)(m.roundDeadline - m.liveStart) - round1Len) < 0.5f, b);
    for (auto& s : g.sold) if (s.alive && m.sideOf(s.slot) == 0) g.killSoldier(s.slot, -1, vec3(0, 0, 1));   // round 1 ends
    stage = 2; return true;
  }
  if (stage == 2 && m.round == 2 && live && m.t - m.liveStart > 0.5) {
    std::snprintf(b, sizeof b, "round 2: hour still %.2f, key light %.1f deg from the round-1 night sun", app.R.lt.hour, angleDeg(sunNight, keyLight(app)));
    T->check(hourIs(21.0f) && angleDeg(sunNight, keyLight(app)) < 1.0f, b);
    std::snprintf(b, sizeof b, "round 2 uses the new round length: %.0f s", m.roundDeadline - m.liveStart);
    T->check(std::fabs((float)(m.roundDeadline - m.liveStart) - 75.0f) < 0.5f, b);
    for (auto& s : g.sold) if (s.alive && m.sideOf(s.slot) == 0) g.killSoldier(s.slot, -1, vec3(0, 0, 1));   // round 2 ends: halftime
    app.uiShot = exeDir() / "selftest_out" / "sd_time_r2_night.png";
    stage = 3; return true;
  }
  if (stage == 3 && m.round == 3 && live && m.t - m.liveStart > 0.5) {
    std::snprintf(b, sizeof b, "round 3 (after the halftime swap, attackers now team %d): hour %.2f", m.attackerTeam, app.R.lt.hour);
    T->check(hourIs(21.0f), b);
    settingsUiMapHour(app);
    stage = 4; frames = 0; return true;
  }
  if (stage == 4 && frames > 5) {
    std::snprintf(b, sizeof b, "Map default: hour %.2f, key light %.1f deg from the round-1 day sun", app.R.lt.hour, angleDeg(sunDay, keyLight(app)));
    T->check(hourIs(13.0f) && !app.R.lt.userHour && angleDeg(sunDay, keyLight(app)) < 1.0f, b);
    finish();
  }
  return true;
}
