// TRE2 / TRE4 --capture-tre2: each field gun's aiming view with its trajectory line ending on a wall, at a medium and a long
// distance (cannon and mortar 60 / 150 m, the trebuchet 100 / 200 m; the trebuchet once from each side), the UI included
// (the line is drawn over the frame). Every case has its own lane: a gun and, straight ahead, a wall whose face is at the
// distance: the city wall section (cw_wall, 11.3 m) where the content has it, else the two-storey farmhouse. The elevation is
// solved so the line ends on the wall's face (the lowest such elevation, near mid-height); then one real shot is fired and
// its impact logged against the line's end. Clear weather at 12:30.
// Out: selftest_out/tre2_<gun>_<distance>m[_left|_right].png; the log: [tre2] lines.
#include "selftest.h"

namespace {
struct Case { const char* asset; float dist; int side; const char* name; uint64_t gun = 0, wall = 0; float elev = 0, faceZ = 0, wallH = 0; vec3 end{0}; float tof = 0; bool onWall = false; };
Case kCases[] = {{"gun_cannon18", 60, 0, "cannon18_60m"}, {"gun_cannon18", 150, 0, "cannon18_150m"}, {"gun_cannon_mortar", 60, 0, "mortar_60m"}, {"gun_cannon_mortar", 150, 0, "mortar_150m"},
                 {"gun_trebuchet", 100, 0, "trebuchet_100m_right"}, {"gun_trebuchet", 200, 1, "trebuchet_200m_left"}};
const int kN = sizeof kCases / sizeof kCases[0];
int phase = 0, crewed = -1, sub = 0; float t = 0; bool shot = false; float impT0 = 0; size_t nProj = 0;
fs::path out(const std::string& n) { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p / ("tre2_" + n + ".png"); }
// the gun's predicted shot for its current aim: end point, flight time, what it struck
vec3 predict(App& a, Case& c, float& tof, uint64_t& ud) {
  Instance* in = a.doc.find(c.gun); auto& r = a.rt[c.gun]; const StationDef& d = r.asset->station; vec3 from, dir, end; float sp;
  if (d.kind == "trebuchet") from = a.trebuchetRelease(*in, r, dir, sp);
  else { mat4 b, y, e, rc; a.stationParts(*in, r, b, y, e, rc); const vec3 bore = d.muzzle - d.elevPivot; from = vec3(e * vec4(bore.x, bore.y, 0, 1)); dir = glm::normalize(vec3(e * vec4(0, 0, 1, 0))); sp = d.heVelocity; }
  a.shotArc(from, dir * sp, d.kind == "trebuchet" ? 1.0f : d.heGravity, (c.gun << 16), &end, &tof, &ud); return end;
}
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
    const AssetDef* wa = a.lib.find("cw_wall"); const char* wid = wa ? "cw_wall" : "house_farmhouse_2story"; if (!wa) wa = a.lib.find(wid);
    if (!wa) { logf("[tre2] no wall asset"); return done(); }
    for (int k = 0; k < kN; ++k) {
      Case& c = kCases[k]; const float x = -125.0f + 50.0f * k;
      c.gun = a.placeInstance(a.lib.find(c.asset), vec3(x, a.terrain.height(x, 0), 0), 0);
      const float wz = c.dist - wa->bmin.z;   // the wall's near face at the distance (its depth behind)
      c.wall = a.placeInstance(wa, vec3(x, a.terrain.height(x, wz), wz), 0); c.faceZ = c.dist; c.wallH = wa->bmax.y;
      if (!c.gun || !c.wall) { logf("[tre2] could not place case %s", c.name); return done(); }
    }
    logf("[tre2] wall: %s (%.1f m high, face %.1f m deep from its origin)", wid, wa->bmax.y, -wa->bmin.z);
    a.doc.start = {true, vec3(kCases[0].gun ? -125 : 0, 0, -8), 0}; a.doc.start.pos.y = a.terrain.height(a.doc.start.pos.x, a.doc.start.pos.z);
    a.syncRuntime(); a.enterExplore(); a.useScripted = true;
    weatherForce(a.R, wxPresetFromName("clear")); a.R.lt.hour = 12.5f; a.R.lt.userHour = true;   // midday, clear
    phase = 1; t = 0; return true;
  }
  const int vi = phase - 1;
  if (vi >= kN) {
    int on = 0; for (auto& c : kCases) on += c.onWall;
    logf("[tre2] done: %d stills in %s; %d of %d lines end on the wall face", kN, (exeDir() / "selftest_out").string().c_str(), on, kN); return done();
  }
  Case& c = kCases[vi]; a.scriptedInput = ExploreInput{};
  StationRT* s = a.station(c.gun); Instance* in = a.doc.find(c.gun); const StationDef& d = a.rt[c.gun].asset->station;
  if (a.occupied != c.gun || crewed != vi) {
    if (a.occupied) a.leaveStation();
    if (t < 0.05f) { vec3 p = in->pos + yawQuat(in->yaw) * ((c.side && d.twoSides ? d.gunner2 : d.gunner) * 1.25f); p.y = a.terrain.height(p.x, p.z); Physics::setCharacterPos(p); return true; }
    if (t < 0.3f) return true;
    if (!a.occupyStation(c.gun, ST_GUNNER)) { logf("[tre2] %s: could not crew it", c.name); phase++; t = 0; sub = 0; return true; }
    crewed = vi; t = 0.3f; sub = 0;
    // solve: the lowest elevation (0.1 deg steps) whose line ends on the wall face, preferring the end nearest mid-height
    float best = 1e9f; c.onWall = false;
    for (float el = d.elevMin; el <= d.elevMax + 1e-3f; el += 0.1f) {
      s->elev = s->elevT = el; float tof; uint64_t ud; vec3 e = predict(a, c, tof, ud);
      const bool face = (ud >> 16) == c.wall && std::fabs(e.z - c.faceZ) < 1.5f, wallHit = (ud >> 16) == c.wall;
      const float score = face ? std::fabs(e.y - a.terrain.height(e.x, e.z) - c.wallH * 0.5f) : 1e6f + std::fabs(e.z - c.faceZ) - (wallHit ? 1e5f : 0);
      if (score < best - 0.05f) { best = score; c.elev = el; }
      if (face && score < 1.0f) break;   // near mid-height: the low (direct) solution
    }
    s->elev = s->elevT = c.elev; uint64_t ud; c.end = predict(a, c, c.tof, ud);
    c.onWall = (ud >> 16) == c.wall;
    const float hy = c.end.y - a.terrain.height(c.end.x, c.end.z);
    logf("[tre2] %s: distance %.0f m, elevation %+.1f deg%s: the line ends %s (%.1f, %.1f, %.1f), flight %.2f s", c.name, c.dist, c.elev, d.kind == "trebuchet" ? " (speed setting)" : "",
         c.onWall ? ("on the wall at " + std::to_string(hy).substr(0, 4) + " m up, " + std::to_string(c.end.z - c.faceZ).substr(0, 4) + " m from its face").c_str() : (c.end.z < c.faceZ ? "on the ground short of the wall" : "past the wall"),
         c.end.x, c.end.y, c.end.z, c.tof);
  }
  s->elev = s->elevT = c.elev; s->yawT = 0;
  if (sub == 0 && t > 1.6f) { a.uiShot = out(c.name); sub = 1; }
  else if (sub == 1 && t > 2.2f) {   // the real shot: the gun's own fire path (the trebuchet's swing and release; a cannon's round at once)
    impT0 = a.lastImpact.time; nProj = a.projectiles.size();
    if (d.kind == "trebuchet") { s->treb = 1; s->trebT = 0; s->trebMine = true; } else a.stationShoot(c.gun, -1);
    sub = 2; t = 2.2f;
  } else if (sub == 2 && (a.lastImpact.time != impT0 || t > 16.0f)) {
    if (a.lastImpact.time != impT0) logf("[tre2] %s: the real shot struck %s at (%.1f, %.1f, %.1f): %.2f m from the line's end", c.name, a.lastImpact.surface.c_str(), a.lastImpact.pos.x, a.lastImpact.pos.y, a.lastImpact.pos.z, glm::length(a.lastImpact.pos - c.end));
    else logf("[tre2] %s: the real shot: no impact within 14 s", c.name);
    sub = 3; t = 0;
  } else if (sub == 3 && t > 0.3f) { phase++; t = 0; sub = 0; crewed = -1; }
  (void)nProj; return true;
}
