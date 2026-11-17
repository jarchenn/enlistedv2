// SD7 --selftest-sd-freeze [map]: in the Search and Destroy freeze (buy time) every bot stands still with its gun up.
// Bots only on the map (default Sun Fort), an 8 s freeze; from 1 s into it to its end, per bot: the largest change of
// position and of body yaw (must be 0 / under 1 deg), and the gun's pitch (the weapon matrix's forward axis: within a few
// degrees of level); then the first 2 s of the live round: no snap (the body yaw and the gun pitch change smoothly).
// A capture of the spawn lineup in freeze: selftest_out/sd_freeze_lineup.png. selftest_out/report_sdfreeze.txt.
#include "selftest.h"
#include "sd_mode.h"
#include "sd_game.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "physics.h"
#include "loot.h"

namespace {
MpTest* T = nullptr;
int mode = -1, stage = 0; bool started = false; float total = 0;
struct Track { vec3 p0{0}; float yaw0 = 0, dPos = 0, dYaw = 0, pitchMin = 1e9f, pitchMax = -1e9f, lastYaw = 0, lastPitch = 0, jumpYaw = 0, jumpPitch = 0; bool init = false; };
std::map<int, Track> tr;
float gunPitch(const Soldier& s) { vec3 f = glm::normalize(vec3((s.world * s.pose.weapon)[2])); return degrees(std::asin(std::clamp(f.y, -1.0f, 1.0f))); }
}   // namespace

bool sdFreezeSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-sd-freeze") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!started) {
    started = true; T = new MpTest("sdfreeze");
    std::string map = "templates/sd_desert.nmap.json";
    for (int k = 1; k + 1 < __argc; ++k) if (std::string(__argv[k]) == "--selftest-sd-freeze" && __argv[k + 1][0] != '-') map = __argv[k + 1];
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
    if (SdMatch* m = sdMatch()) { m->cfg.freeze = 8; m->cfg.roundEnd = 1.5f; }   // measured in round 2: everyone has bought (rifles / SMGs; round 1 is pistols)
    return true;
  }
  total += dt;
  Game& g = *gGame; SdMatch* mp = sdMatch();
  auto finish = [&]() { if (app.mode == Mode::Explore) app.exitExplore(); app.selftest->exitCode = T->finish(); app.selftest->done = true; };
  if (!mp || !g.active || total > 120) { if (total > 120) T->check(false, "timed out"); finish(); return true; }
  SdMatch& m = *mp;
  if (m.round == 1 && m.phase == SdPhase::LiveUnplanted && m.t - m.liveStart > 0.5)   // round 1 ends at once (the attackers fall)
    for (auto& s : g.sold) if (s.alive && m.sideOf(s.slot) == 0) g.killSoldier(s.slot, -1, vec3(0, 0, 1));
  if (stage == 0 && m.round >= 2 && m.phase == SdPhase::Freeze && m.phaseEnd - m.t < 7.0) {   // 1 s in: the reference
    for (auto& s : g.sold) if (s.alive && !s.human && !s.localPlayer) { Track& k = tr[s.slot]; k.p0 = s.feet; k.yaw0 = s.yaw; k.init = true; }
    if (Soldier* L = g.local()) {   // the capture: the local player looks along its own team's lineup
      vec3 c(0); int n = 0; for (auto& s : g.sold) if (s.alive && !s.localPlayer && s.team == L->team) { c += s.feet; ++n; }
      if (n) { c /= (float)n; vec3 d = c - L->feet; app.eyeYaw = degrees(std::atan2(-d.x, -d.z)); app.eyePitch = -3; }
    }
    stage = 1;
  }
  if (stage == 1) {
    for (auto& s : g.sold) if (s.alive && tr.count(s.slot)) {
      Track& k = tr[s.slot];
      k.dPos = std::max(k.dPos, glm::length(s.feet - k.p0)); k.dYaw = std::max(k.dYaw, std::fabs(wrapDeg(s.yaw - k.yaw0 + 180.0f) - 180.0f));
      float gp = gunPitch(s); k.pitchMin = std::min(k.pitchMin, gp); k.pitchMax = std::max(k.pitchMax, gp);
    }
    if (m.phaseEnd - m.t < 3.0 && m.phaseEnd - m.t > 2.9) app.uiShot = exeDir() / "selftest_out" / "sd_freeze_lineup.png";
    if (m.phase != SdPhase::Freeze) {
      float worstPos = 0, worstYaw = 0, lo = 1e9f, hi = -1e9f;
      for (auto& [slot, k] : tr) { worstPos = std::max(worstPos, k.dPos); worstYaw = std::max(worstYaw, k.dYaw); lo = std::min(lo, k.pitchMin); hi = std::max(hi, k.pitchMax); }
      int rifles = 0; for (auto& [slot, k] : tr) { const Soldier& s = g.sold[slot]; if (s.weapon >= 0 && !lootIsPistol(*app.weaponDefs[s.weapon])) ++rifles; }
      char b[300]; std::snprintf(b, sizeof b, "freeze (%zu bots, %d with a long gun, 7 s): largest position change %.4f m, body yaw change %.2f deg; gun pitch %.1f to %.1f deg", tr.size(), rifles, worstPos, worstYaw, lo, hi);
      T->check(!tr.empty() && worstPos < 0.005f && worstYaw < 1.0f && lo > -8.0f && hi < 8.0f, b);
      for (auto& [slot, k] : tr) { k.lastYaw = g.sold[slot].yaw; k.lastPitch = gunPitch(g.sold[slot]); }
      stage = 2; total = 0;
    }
    return true;
  }
  if (stage == 2) {   // the first 2 s live: smooth (no per-frame snap of the body yaw or the gun pitch)
    for (auto& [slot, k] : tr) {
      const Soldier& s = g.sold[slot]; if (!s.alive) continue;
      k.jumpYaw = std::max(k.jumpYaw, std::fabs(wrapDeg(s.yaw - k.lastYaw + 180.0f) - 180.0f)); k.lastYaw = s.yaw;
      float gp = gunPitch(s); k.jumpPitch = std::max(k.jumpPitch, std::fabs(gp - k.lastPitch)); k.lastPitch = gp;
    }
    if (total > 2.0f) {
      float jy = 0, jp = 0; for (auto& [slot, k] : tr) { jy = std::max(jy, k.jumpYaw); jp = std::max(jp, k.jumpPitch); }
      char b[200]; std::snprintf(b, sizeof b, "round start: the largest per-frame change in the first 2 s: body yaw %.1f deg, gun pitch %.1f deg", jy, jp);
      T->check(jp < 12.0f, b);
      finish();
    }
  }
  return true;
}
