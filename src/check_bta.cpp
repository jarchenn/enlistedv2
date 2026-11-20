// BTA --check-bta: the winter footstep sets and the creak event load; Blackwater Thaw's ambience data is found by template
// id; without the map's snow layer (snowStepAt, BT1) unpainted ground keeps the default step; at the footbridge the wind bed, the creek line and the footbridge
// riffle sound (nothing else in range), and on the quarry overlook the ridge gust bed joins; the gust signal stays in 0..1
#include "selftest.h"
#include "map_ambience.h"
#include "physics.h"

bool btaSelfTest(App& a, float dt) {
  static int mode = -1, frame = 0;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--check-bta") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  static int fails = 0;
  auto chk = [&](bool ok, const std::string& w) { logf("[bta] %s %s", ok ? "PASS" : "FAIL", w.c_str()); if (!ok) ++fails; };
  auto ids = [&]() { std::string s; for (auto& i : a.audio.ambIds()) s += i + " "; return s; };
  auto has = [&](const char* id) { for (auto& i : a.audio.ambIds()) if (i == id) return true; return false; };
  auto at = [&](float x, float z) { Physics::setCharacterPos(vec3(x, a.terrain.height(x, z), z)); };   // the frame puts the ears at the player's eye
  if (frame == 0) {
    for (const char* e : {"step_snow", "step_snow_deep", "step_slush", "step_mud", "step_shallow", "amb_creak"}) chk(a.audio.hasEvent(e), std::string("event ") + e + " loaded");
    a.newMap(*a.findTemplate("blank_slate"), "BTA check");
    a.doc.templateId = "blackwater_thaw";   // a blank map standing in for the map (its ambience file is found by template id)
    const json* c = mapAudioConfig(a);
    chk(c && c->value("zones", json::array()).size() == 6 && c->value("loops", json::array()).size() == 8 && c->value("creaks", json::array()).size() == 5, "Blackwater Thaw ambience data: 6 zones, 8 loops, 5 creak spots");
    vec3 p(12, a.terrain.height(12, -31), -31);
    std::string st = mapStepEvent(a, p), ss = a.stepSound(p, 0);
    chk(st.empty() && ss == "step_grass", "no snow layer linked (snowStepAt null): unpainted ground keeps the default step (" + st + ", " + ss + ")");
    float g = mapWindGust(); chk(g >= 0 && g <= 1, "gust signal in 0..1 (" + std::to_string(g) + ")");
    a.doc.start = {true, p, 0}; chk(a.enterExplore(), "first person");
  }
  if (frame >= 1 && frame < 30) { at(12, -31); }
  if (frame == 30) chk(has("wind") && has("loop_creek") && has("loop_footbridge") && a.audio.ambIds().size() == 3, "at the footbridge: wind, creek, footbridge riffle only (" + ids() + ")");
  if (frame >= 31 && frame < 60) at(80, 72);
  if (frame == 60) {
    chk(has("zone_overlook"), "on the overlook the ridge gusts join (" + ids() + ")");
    logf("[bta] done: %d failures", fails); a.selftest->exitCode = fails ? 1 : 0; a.selftest->done = true;
  }
  ++frame;
  return true;   // handled (the app frame runs as usual: mapAmbienceFrame)
}
