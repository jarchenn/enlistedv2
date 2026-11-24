// TRE2 --capture-tre2: the aiming view of each field gun with its trajectory line and landing ring, the UI included (the line
// is drawn over the frame): the 18th-century cannon at +8 deg, the mortar at +35 deg, the trebuchet at its default throw
// (30 m/s at 40 deg) and at full elevation (from its right and left sides). A farmhouse 70 m out stops the cannon's line.
// TRE3: clear weather at 12:30; the player stands by the side to work from a moment before crewing (the nearer side is taken).
// Out: selftest_out/tre2_<gun>.png
#include "selftest.h"

namespace {
int phase = 0; float t = 0; uint64_t ids[3] = {0, 0, 0}, house = 0; bool shot = false; int crewed = -1;   // the view the gun was crewed for (the trebuchet's two views re-crew from the other side)
struct View { int gun; float elev; int side; const char* name; };
const View kViews[] = {{0, 8, 0, "cannon18"}, {1, 35, 0, "mortar"}, {2, 0, 0, "trebuchet_right_default"}, {2, 15, 1, "trebuchet_left_full"}};
fs::path out(const std::string& n) { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p / ("tre2_" + n + ".png"); }
}

bool captureTre2SelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-tre2") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  t += dt;
  auto done = [&]() { if (a.occupied) a.leaveStation(); if (a.mode == Mode::Explore) a.exitExplore(); a.selftest->exitCode = 0; a.selftest->done = true; return true; };
  if (phase == 0) {
    a.newMap(*a.findTemplate("blank_slate"), "TRE2 capture");
    const char* assets[3] = {"gun_cannon18", "gun_cannon_mortar", "gun_trebuchet"}; const float xs[3] = {-30, 0, 40};
    for (int k = 0; k < 3; ++k) { ids[k] = a.placeInstance(a.lib.find(assets[k]), vec3(xs[k], a.terrain.height(xs[k], 0), 0), 0); if (!ids[k]) { logf("[tre2] could not place %s", assets[k]); return done(); } }
    house = a.placeInstance(a.lib.find("house_farmhouse_2story"), vec3(-30, a.terrain.height(-30, 70), 70), 0);
    a.doc.start = {true, vec3(-30, a.terrain.height(-30, -6), -6), 0};
    a.syncRuntime(); a.enterExplore(); a.useScripted = true;
    weatherForce(a.R, wxPresetFromName("clear")); a.R.lt.hour = 12.5f; a.R.lt.userHour = true;   // TRE3: midday, clear
    phase = 1; t = 0; return true;
  }
  const int vi = phase - 1;
  if (vi >= (int)(sizeof kViews / sizeof kViews[0])) { logf("[tre2] done: %d stills in %s", (int)(sizeof kViews / sizeof kViews[0]), (exeDir() / "selftest_out").string().c_str()); return done(); }
  const View& v = kViews[vi]; const uint64_t id = ids[v.gun];
  a.scriptedInput = ExploreInput{};
  StationRT* s = a.station(id); Instance* in = a.doc.find(id); const StationDef& d = a.rt[id].asset->station;
  if (a.occupied != id || crewed != vi) {
    if (a.occupied) a.leaveStation();
    if (t < 0.05f) { vec3 p = in->pos + yawQuat(in->yaw) * ((v.side && d.twoSides ? d.gunner2 : d.gunner) * 1.25f); p.y = a.terrain.height(p.x, p.z); Physics::setCharacterPos(p); return true; }   // stand there first
    if (t < 0.3f) return true;
    if (!a.occupyStation(id, ST_GUNNER)) { logf("[tre2] %s: could not crew it", v.name); phase++; t = 0; shot = false; return true; }
    t = 0.3f; crewed = vi;
  }
  s->elev = s->elevT = v.elev; s->yawT = 0;
  if (t > 1.6f && !shot) { shot = true; a.uiShot = out(v.name); logf("[tre2] %s: elevation %.0f deg, side %d, hour %.1f -> %s", v.name, v.elev, s->side, a.R.lt.hour, a.uiShot.string().c_str()); }
  if (t > 2.2f) { phase++; t = 0; shot = false; }
  return true;
}
