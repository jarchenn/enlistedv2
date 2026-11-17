// GP1 --selftest-prone: Z goes prone wherever a prone body fits. Three maps, 50 random standable points each (flat
// enough to walk: ground normal y > 0.8, the standing body fits); at each the player presses Z (scripted input) and must be
// prone within 0.8 s, then Z again stands up. A point where no heading / nudge fits a 1.7 m body is listed (a space
// physically smaller than a prone body). selftest_out/report_prone.txt.
#include "selftest.h"
#include "mptest.h"
#include "physics.h"
#include <random>

namespace {
MpTest* T = nullptr;
int mode = -1, mapI = -1, pt = 0, ok = 0, refused = 0; int stage = 0; float st = 0; std::mt19937 rng(1944);
vec3 at{0}; std::vector<std::string> misses;
const char* kMaps[] = {"templates/chateau_town.nmap.json", "examples/village_assault_3.nmap.json", "templates/newtown.nmap.json"};

bool pick(App& a, vec3& out) {
  std::uniform_real_distribution<float> ux(-a.terrain.buildHalfX * 0.9f, a.terrain.buildHalfX * 0.9f), uz(-a.terrain.buildHalfZ * 0.9f, a.terrain.buildHalfZ * 0.9f);
  for (int k = 0; k < 400; ++k) {
    float x = ux(rng), z = uz(rng); Physics::Hit h;
    if (!Physics::castRayEx(vec3(x, a.terrain.height(x, z) + 40, z), vec3(0, -1, 0), 80, h)) continue;
    if (h.normal.y < 0.8f || h.pos.y < a.terrain.waterLevel + 0.3f) continue;
    if (!Physics::shapeFitsAt(Physics::STAND, h.pos, false)) continue;
    out = h.pos; return true;
  }
  return false;
}
}   // namespace

bool proneSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-prone") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!T) T = new MpTest("prone");
  st += dt;
  auto nextMap = [&]() {
    if (mapI >= 0) { char b[200]; std::snprintf(b, sizeof b, "%s: prone at %d of 50 random walkable points (%d refused)", kMaps[mapI], ok, refused); T->check(ok == 50, b); for (auto& m : misses) T->note(m); }
    ++mapI; pt = ok = refused = 0; stage = 0; st = 0; misses.clear();
    if (mapI >= 3) { if (app.mode == Mode::Explore) app.exitExplore(); app.selftest->exitCode = T->finish(); app.selftest->done = true; return; }
    if (app.mode == Mode::Explore) app.exitExplore();
    app.openMap(contentDir() / kMaps[mapI]); app.enterExplore(); app.useScripted = true; app.scriptedInput = ExploreInput{};
  };
  if (mapI < 0 || app.mode != Mode::Explore) { nextMap(); return true; }
  app.scriptedInput.prone = false;
  if (stage == 0) {   // a new point
    if (pt >= 50) { nextMap(); return true; }
    if (!pick(app, at)) { T->note("no standable point found"); ++pt; return true; }
    Physics::setCharacterPos(at + vec3(0, 0.05f, 0)); app.eyeYaw = std::uniform_real_distribution<float>(0, 360)(rng);
    stage = 1; st = 0; return true;
  }
  if (stage == 1 && st > 0.25f) { app.scriptedInput.prone = true; stage = 2; st = 0; return true; }   // Z
  if (stage == 2 && st > 0.8f) {
    bool prone = Physics::character().posture == Physics::PRONE;
    if (prone) ++ok; else { ++refused; char b[160]; std::snprintf(b, sizeof b, "refused at (%.1f %.1f %.1f), facing %.0f", at.x, at.y, at.z, app.eyeYaw); misses.push_back(b); }
    if (prone) app.scriptedInput.prone = true;   // Z again: stand up
    stage = 3; st = 0; return true;
  }
  if (stage == 3 && st > 1.6f) { ++pt; stage = 0; }
  return true;
}
