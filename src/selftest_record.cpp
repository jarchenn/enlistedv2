// --record-range: one continuous recording of the shipped range example, driven through the same
// ExploreInput path the keyboard and mouse use (scripted, fixed 1/60 s steps). Every effect on screen is
// the real system: shells fly from the world muzzle, walls and ground are resolved by the damage code,
// ammunition moves through the crate transaction. Output: selftest_out/range_recording.avi (MJPEG, 30 fps,
// captured after the HUD is drawn) plus report_record.txt with the checks made along the way.
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <functional>
#include <imgui.h>

namespace {
struct Step { std::string name, caption; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float tcur = 0;
uint64_t PAK = 0, BUN = 0, DAI = 0, CRATE = 0;
int rearK = -1; float impactSeen = -10; int shellsAtBreach = 0;
vec3 craterP{0}; float minFeetY = 1e9f; vec3 gunPos0{0};
std::string caption;
bool video = true;

uint64_t byAsset(App& a, const char* id) { for (auto& i : a.doc.instances) if (i.assetId == id) return i.id; return 0; }
vec3 Wp(App& a, uint64_t id, vec3 l) { Instance* i = a.doc.find(id); return i->pos + yawQuat(i->yaw) * l; }
void look(App& a, vec3 p) {
  vec3 e = a.eyePos();
  a.eyeYaw = degrees(std::atan2(-(p.x - e.x), -(p.z - e.z)));
  a.eyePitch = degrees(std::atan2(p.y - e.y, glm::length(vec2(p.x - e.x, p.z - e.z))));
}
// walk (optionally sprint) toward a ground point; true on arrival
bool walkTo(App& a, vec3 p, float r, bool sprint) {
  auto& in = a.scriptedInput;
  vec3 e = a.eyePos();
  look(a, vec3(p.x, e.y - 0.25f, p.z));
  in.sprint = sprint;
  if (glm::length(vec2(p.x - e.x, p.z - e.z)) < r) { in.move = vec2(0); in.sprint = false; return true; }
  in.move = vec2(0, 1);
  return false;
}
void aimGun(App& a, vec3 target) {
  StationRT* s = a.station(PAK); auto& r = a.rt[PAK];
  vec3 dir; vec3 m = a.stationMuzzle(*a.doc.find(PAK), r, dir);
  vec3 l = glm::inverse(yawQuat(s->carYaw)) * (target - m);
  float dist = glm::length(vec2(l.x, l.z)), tof = dist / 550.0f, drop = 0.5f * 9.81f * tof * tof;
  a.scriptedInput.dyaw = degrees(std::atan2(l.x, l.z)) - s->yawT;
  a.scriptedInput.dpitch = degrees(std::atan2(l.y + drop, dist)) - s->elevT;
}
bool settled(App& a) { StationRT* s = a.station(PAK); return std::fabs(s->yaw - s->yawT) < 0.02f && std::fabs(s->elev - s->elevT) < 0.02f; }
}

void SelfTest::recordUpdate(App& app, float dt) {
  if (stage == 990) {
    reportName = "record";
    fs::path map = contentDir() / "examples" / "range.nmap.json";
    check(app.openMap(map), "opened content/examples/range.nmap.json (as an unsaved copy)");
    PAK = byAsset(app, "gun_pak40"); BUN = byAsset(app, "bunker_mg_casemate"); DAI = byAsset(app, "small_stone_dairy"); CRATE = byAsset(app, "crate_mixed_supply");
    check(PAK && BUN && DAI && CRATE, "range map has the PaK, the rotated bunker, the masonry dairy and the mixed crate");
    const AssetDef* ba = app.lib.find("bunker_mg_casemate");
    for (size_t k = 0; k < ba->panels.size(); ++k) if (ba->panels[k].id == "floor-00/rear/bay-01") rearK = (int)k;
    app.grass.quality = GrassQuality::Medium;
    app.syncRuntime();
    check(app.enterExplore(), "F5 enters Explore at the range start marker");
    app.useScripted = true;
    fs::path out = exeDir() / "selftest_out" / "range_recording.avi";
    video = true; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--no-video") video = false;
    if (video) check(app.rec.open(out, 854, 480, 30, 62), "recording to " + out.string());
    seq.clear(); cur = 0; tcur = 0;
    auto push = [&](const char* n, const char* c, std::function<bool(App&, float)> f) { seq.push_back({n, c, f}); };
    // ------------------------------------------------------------ 1. rotated bunker breach with the PaK 40
    push("look downrange", "1/5  Range: rotated MG casemate (155 deg) and masonry dairy, PaK 40 in front", [](App& a, float t) {
      look(a, Wp(a, BUN, vec3(0, 1.2f, 0)));
      return t > 2.5f; });
    push("walk to gun", "1/5  Walk to the PaK 40", [](App& a, float t) {
      return walkTo(a, Wp(a, PAK, vec3(-1.05f, 0, -1.6f)), 0.6f, false) || t > 12; });
    push("crew prompt", "1/5  E at the gun: Aim (gunner) or Move (handler)", [this](App& a, float t) {
      look(a, Wp(a, PAK, vec3(0, 0.8f, 0)));
      if (t < 0.8f) return false;
      check(a.target.valid && a.target.kind == 1, "gun crew prompt shown");
      a.scriptedInput.interact = true; return true; });
    push("choose aim", "1/5  [1] Aim: gunner at the sight", [this](App& a, float t) {
      if (t < 0.8f) return false; a.scriptedInput.choose1 = true; return true; });
    push("engage", "1/5  HE at the casemate rear wall (450 hp concrete panel)", [this](App& a, float t) {
      auto& in = a.scriptedInput; StationRT* s = a.station(PAK);
      vec3 c(1.45f, 1.15f, -3.0f);   // rear bay face beside the entrance blast wall (same point as --selftest-pak)
      if (t < 0.05f) impactSeen = a.lastImpact.time;
      // keep laying the gun until the bore (from the muzzle's *current* position) is on the point:
      // traversing moves the muzzle sideways, so a single lay from the start position misses by ~1 m
      if (t < 5.0f && s->shots == 0) {
        aimGun(a, Wp(a, BUN, c));
        static bool shot = false; if (!shot && t > 4.0f) { shot = true; shotQueue.push_back("rec_sight_scene"); summary["sight_exposure"] = a.R.exposure(); }
        if (t < 4.5f || !settled(a)) return false; }
      in.dyaw = in.dpitch = 0;
      { static float logged = -10; if (a.lastImpact.time > logged) { logged = a.lastImpact.time; vec3 w = Wp(a, BUN, c);
        logf("[record] impact %s at (%.2f %.2f %.2f) target (%.2f %.2f %.2f) rear hp %.0f shots %d yaw %.2f/%.2f elev %.2f/%.2f", a.lastImpact.surface.c_str(), a.lastImpact.pos.x, a.lastImpact.pos.y, a.lastImpact.pos.z, w.x, w.y, w.z, a.rt[BUN].hp[rearK], s->shots, s->yaw, s->yawT, s->elev, s->elevT); } }
      if (a.rt[BUN].state[rearK] == PANEL_BREACHED) { shellsAtBreach = s->shots; return true; }
      if (s->reload > 0 || !settled(a)) { in.fire = false; return false; }
      if (s->loaded == 0) { in.reload = true; return false; }
      in.reload = false;
      if (a.lastImpact.time <= impactSeen && s->shots > 0 && t > 0.1f && s->recoilT < 2.5f) { in.fire = false; return false; }
      impactSeen = a.lastImpact.time;
      in.fire = !in.fire;
      return t > 70; });
    push("breached", "1/5  Rear wall breached", [this](App& a, float t) {
      a.scriptedInput.fire = false;
      if (t < 2.5f) return false;
      check(a.rt[BUN].state[rearK] == PANEL_BREACHED, "rotated casemate rear bay breached by " + std::to_string(shellsAtBreach) + " real PaK 40 shells");
      return true; });
    // ------------------------------------------------------------ 2. crater in the grassy plot
    push("shell pasture", "2/5  One shell into the grassy plot", [this](App& a, float t) {
      auto& in = a.scriptedInput; StationRT* s = a.station(PAK);
      vec3 g(-2.5f, 0, -15.0f); g.y = a.terrain.height(g.x, g.z);
      if (t < 0.05f) impactSeen = a.lastImpact.time;
      if (t < 4.0f) { aimGun(a, g); return false; }
      in.dyaw = in.dpitch = 0;
      if (a.lastImpact.time > impactSeen) { craterP = a.lastImpact.pos; in.fire = false; return true; }
      if (s->reload > 0 || !settled(a)) return false;
      if (s->loaded == 0) { in.reload = s->reserve > 0; if (!s->reserve) { check(false, "no shell left for the crater"); return true; } return false; }
      in.reload = false;
      in.fire = !in.fire;
      return t > 20; });
    push("crater check", "2/5  Crater in the pasture: disturbed soil, grass cleared", [this](App& a, float t) {
      if (t < 2.0f) return false;
      check(a.lastImpact.surface == "ground" && a.terrain.tiles.size() > 0, "shell cut a crater in the grassy plot");
      return true; });
    // ------------------------------------------------------------ 3. PaK reposition
    push("to move", "3/5  T: gunner -> handler (Move)", [this](App& a, float t) {
      if (t < 0.05f) { a.scriptedInput.switchPos = true; gunPos0 = a.station(PAK)->pos; return false; }
      a.scriptedInput.switchPos = false;
      return t > 2.0f; });
    push("push", "3/5  Handler pushes the gun forward and swings the trails", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      in.move = t < 2.6f ? vec2(0, 1) : t < 4.6f ? vec2(1, 0) : vec2(0);
      return t > 5.0f; });
    push("deploy", "3/5  T: deploy (trails spread, spades set)", [this](App& a, float t) {
      if (t < 0.05f) { a.scriptedInput.switchPos = true; return false; }
      a.scriptedInput.switchPos = false;
      if (t < 3.0f) return false;
      StationRT* s = a.station(PAK);
      float moved = glm::length(s->pos - gunPos0);
      check(s->deployed && moved > 1.0f, "PaK repositioned " + std::to_string(moved).substr(0, 4) + " m and redeployed");
      a.scriptedInput.interact = true;   // E leaves the gun
      return true; });
    // ------------------------------------------------------------ 4. crater traversal on foot
    push("dismount", "4/5  E: leave the gun", [](App& a, float t) { return t > 1.0f; });
    push("around gun", "4/5  Walk round the gun towards the crater", [](App& a, float t) {
      StationRT* s = a.station(PAK);
      vec3 side = s->pos + vec3(-3.2f, 0, 0.5f);
      bool ok = walkTo(a, side, 0.6f, false);
      if (ok || t > 8) { auto ch = Physics::character(); logf("[record] around gun: char (%.2f %.2f %.2f) gun (%.2f %.2f) crater (%.2f %.2f %.2f)", ch.pos.x, ch.pos.y, ch.pos.z, s->pos.x, s->pos.z, craterP.x, craterP.y, craterP.z); }
      return ok || t > 8; });
    push("to crater", "4/5  Walk into the crater", [](App& a, float t) {
      auto ch = Physics::character(); minFeetY = std::min(minFeetY, ch.pos.y);
      bool ok = walkTo(a, craterP + vec3(1.2f, 0, 1.6f), 0.6f, false);
      if (ok || t > 15) logf("[record] to crater: char (%.2f %.2f %.2f)", ch.pos.x, ch.pos.y, ch.pos.z);
      return ok || t > 15; });
    push("cross crater", "4/5  Through the bowl and out the far rim", [this](App& a, float t) {
      auto ch = Physics::character(); minFeetY = std::min(minFeetY, ch.pos.y);
      bool done = walkTo(a, craterP - vec3(2.2f, 0, 2.2f), 0.5f, false);
      a.eyePitch = -28;
      if (!done && t < 10) return false;
      float base = a.terrain.baseHeight(craterP.x, craterP.z);
      check(minFeetY < base - 0.15f, "character walked down into the crater (feet " + std::to_string(base - minFeetY).substr(0, 4) + " m below the original ground)");
      return true; });
    // ------------------------------------------------------------ 5. empty weapon, reload, resupply
    push("mp40", "5/5  MP 40, reserve trimmed to one magazine for the clip", [this](App& a, float t) {
      if (t < 0.05f) {
        a.selectWeapon(a.weaponIndex("mp40"));
        auto& ws = a.wstate["mp40"]; ws.loaded = 32; ws.containers = {32};
        return false; }
      look(a, Wp(a, DAI, vec3(0, 1.5f, 0)));
      return t > 1.5f; });
    push("fire mag", "5/5  Fire at the dairy wall until the weapon is empty", [this](App& a, float t) {
      auto& in = a.scriptedInput; auto& ws = a.wstate["mp40"];
      look(a, Wp(a, DAI, vec3(0, 1.5f, 0)));
      if (ws.reload > 0) { in.fire = false; in.reload = false; return false; }
      if (ws.loaded == 0 && !ws.containers.empty()) { in.fire = false; in.reload = true; return false; }
      in.reload = false;
      if (ws.loaded == 0 && ws.containers.empty()) { in.fire = false; return t > 1.0f; }
      in.fire = true; return t > 40; });
    push("empty click", "5/5  Empty: 0 + 0, the trigger only clicks", [this](App& a, float t) {
      auto& in = a.scriptedInput; auto& ws = a.wstate["mp40"];
      in.fire = t < 0.4f || (t > 0.8f && t < 1.1f);
      if (t < 1.6f) return false;
      in.fire = false;
      check(ws.loaded == 0 && ws.reserveRounds() == 0, "MP 40 empty: 0 loaded, 0 reserve; firing only clicks");
      return true; });
    push("to crate", "5/5  Walk to the supply crate", [](App& a, float t) {
      vec3 goal = Wp(a, CRATE, vec3(0, 0, -1.3f));   // near side: the walk comes from downrange (-Z)
      bool ok = walkTo(a, goal, 0.5f, true);
      { static int last = -1; int sec = (int)(t / 2); if (sec != last) { last = sec; auto ch = Physics::character(); vec3 e = a.eyePos();
        logf("[record] to crate t=%.1f char (%.2f %.2f %.2f) eye (%.2f %.2f) yaw %.1f goal (%.2f %.2f) sprint %d move %.1f,%.1f", t, ch.pos.x, ch.pos.y, ch.pos.z, e.x, e.z, a.eyeYaw, goal.x, goal.z, (int)a.scriptedInput.sprint, a.scriptedInput.move.x, a.scriptedInput.move.y); } }
      return ok || t > 20; });
    push("resupply", "5/5  E at the crate: whole magazines up to the allowance", [this](App& a, float t) {
      look(a, Wp(a, CRATE, vec3(0, 0.3f, 0)));
      if (t < 0.8f) return false;
      { static bool pressed = false; if (!pressed) { pressed = true; check(a.target.valid && a.target.kind == 2, "crate prompt shown"); a.scriptedInput.interact = true; return false; } }   // one E press
      if (t < 2.5f) return false;
      auto& ws = a.wstate["mp40"];
      check(ws.reserveRounds() > 0 && ws.reserveRounds() % 32 == 0, "crate resupplied the MP 40 with whole magazines (" + std::to_string(ws.reserveRounds()) + " rounds)");
      return true; });
    push("reload", "5/5  R: reload from the new reserve", [this](App& a, float t) {
      auto& ws = a.wstate["mp40"];
      if (t < 0.05f) { a.scriptedInput.reload = true; return false; }
      a.scriptedInput.reload = false;
      if (ws.reload > 0 || t < 0.5f) return false;
      check(ws.loaded == 32, "reloaded: 32 in the weapon");
      look(a, Wp(a, BUN, vec3(0, 1.5f, 0)));
      return t > 2.0f; });
    // ------------------------------------------------------------ F5 return
    push("f5", "F5: back to Creative; the playtest overlay is discarded", [this](App& a, float t) {
      if (t < 0.05f) { a.exitExplore(); a.useScripted = false; a.camPos = vec3(24, 12, 18); a.camYaw = 40; a.camPitch = -18; return false; }
      a.camPos = vec3(24 - 2 * t, 12, 18 - 1.5f * t);
      if (t < 4.0f) return false;
      int dmg = 0; for (auto& [id, r] : a.rt) dmg += r.damaged;
      check(dmg == 0 && a.terrain.tiles.empty() && glm::length(a.station(PAK)->pos - a.doc.find(PAK)->pos) < 1e-3f,
            "after F5 return: bunker intact, pasture flat, gun at its authored position");
      return true; });
    stage = 991;
    return;
  }
  // ---------------------------------------------------------------- run
  if (cur >= seq.size()) {
    app.rec.close(); app.recCaption.clear();
    summary["recording_frames"] = app.rec.frames();
    summary["recording_seconds"] = app.rec.frames() / 30.0;
    if (video) check(app.rec.frames() > 300, "recording written: " + std::to_string(app.rec.frames()) + " frames (" + std::to_string(app.rec.frames() / 30) + " s)");
    finish(app);
    return;
  }
  Step& s = seq[cur];
  caption = s.caption;
  app.recCaption = caption;   // drawn last in the frame (over the sight mask), see App::run
  // one-frame presses (E, [1]) live exactly for the frame a step sets them
  app.scriptedInput.interact = false; app.scriptedInput.choose1 = false;
  if (tcur == 0) {
    StationRT* st = app.station(PAK);
    logf("[record] step '%s' (occupied %d pos2 %d)", s.name.c_str(), (int)(app.occupied == PAK), st ? st->pos2 : -1);
  }
  bool done = s.fn(app, tcur);
  if (!done && tcur > 90.0f) { check(false, "step '" + s.name + "' timed out"); done = true; }
  if (done) { summary["step_seconds"][s.name] = tcur; cur++; tcur = 0; }
  else tcur += dt;
}
