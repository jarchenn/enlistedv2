// --selftest-pak: Milestone 3 end-to-end with a real fired shell (no injected impacts).
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <functional>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float tcur = 0;
uint64_t P = 0, C = 0, F = 0, K = 0;
int panelK = -1, shots0 = 0, hits = 0;
float impactT0 = -1, yaw0 = 0;
vec3 craterAt0{0};
StationRT* stP(App& a) { return a.station(P); }
Instance* I(App& a, uint64_t id) { return a.doc.find(id); }
vec3 Wp(App& a, uint64_t id, vec3 l) { Instance* i = I(a, id); return i->pos + yawQuat(i->yaw) * l; }
void aimAt(App& a, vec3 target) {   // gun-local traverse/elevation that puts the bore on a world point (with drop)
  StationRT* s = stP(a); auto& r = a.rt[P];
  vec3 dir; vec3 m = a.stationMuzzle(*I(a, P), r, dir);
  vec3 d = target - m;
  vec3 l = glm::inverse(yawQuat(s->carYaw)) * d;
  float yaw = degrees(std::atan2(l.x, l.z));
  float dist = glm::length(vec2(l.x, l.z)), tof = dist / 550.0f, drop = 0.5f * 9.81f * tof * tof;
  float elev = degrees(std::atan2(l.y + drop, dist));
  a.scriptedInput.dyaw = yaw - s->yawT; a.scriptedInput.dpitch = elev - s->elevT;
}
float angleBetween(vec3 a, vec3 b) { return degrees(std::acos(std::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f))); }
}

void SelfTest::pakUpdate(App& app, float dt) {
  if (stage == 600) {
    reportName = "pak";
    const TemplateInfo* t = app.findTemplate("blank_slate");
    app.newMap(*t, "PaK 40 range");
    P = app.placeInstance(app.lib.find("gun_pak40"), vec3(0, 0, 0), 0.0f);
    C = app.placeInstance(app.lib.find("bunker_mg_casemate"), vec3(0, 0, 60), 0.0f);
    F = app.placeInstance(app.lib.find("house_farmhouse_2story"), vec3(34, 0, 0), 37.0f);
    K = app.placeInstance(app.lib.find("crate_pak40_he"), vec3(3.0f, 0, -1.5f), 0.0f);
    check(P && C && F && K, "range: PaK 40, MG casemate 60 m downrange, rotated farmhouse, PaK ammunition crate placed from the catalog");
    const AssetDef* ca = app.lib.find("bunker_mg_casemate");
    for (size_t q = 0; q < ca->panels.size(); ++q) if (ca->panels[q].id == "floor-00/rear/bay-01") panelK = (int)q;
    app.doc.start = {true, Wp(app, P, vec3(-1.6f, 0, -2.0f)), 0};
    app.doc.start.pos.y = app.terrain.height(app.doc.start.pos.x, app.doc.start.pos.z);
    check(app.enterExplore(), "F5"); app.useScripted = true;
    seq.clear(); cur = 0; tcur = 0;
    // ---------------------------------------------------------------- crew the gun through the real E prompt
    seq.push_back({"look at gun", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      vec3 g = Wp(a, P, vec3(0, 0.8f, 0)); vec3 e = a.eyePos();
      a.eyeYaw = degrees(std::atan2(-(g.x - e.x), -(g.z - e.z))); a.eyePitch = degrees(std::atan2(g.y - e.y, glm::length(vec2(g.x - e.x, g.z - e.z))));
      if (t < 0.3f) return false;
      check(a.target.valid && a.target.kind == 1 && a.target.inst == P, "aiming at the PaK shows its crew prompt");
      in.interact = true; return true; }});
    seq.push_back({"choice", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      if (t < 0.1f) return false;
      check(a.choiceFor == P, "E opens the Aim / Move choice (consumed once)");
      in.choose1 = true; return true; }});
    seq.push_back({"gunner", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      if (t < 0.2f) return false;
      vec3 e, f; float fov;
      bool cam = a.stationCamera(e, f, fov);
      check(a.occupied == P && stP(a)->pos2 == ST_GUNNER && cam && fov < 15, "Aim: gunner at the ZF 3x8 eyepiece view (fov " + std::to_string((int)fov) + " deg)");
      shotQueue.push_back("40_pak_sight_empty_range");
      return true; }});
    // ---------------------------------------------------------------- breach the casemate with real shells
    seq.push_back({"engage casemate", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      StationRT* s = stP(a);
      vec3 target = Wp(a, C, vec3(0.55f, 1.15f, -2.9f));   // rear bay-01 (x -0.8..0.98), clear of the blast wall and wing pier
      if (t < 0.05f) { aimAt(a, target); return false; }
      in.dyaw = in.dpitch = 0;
      if (s->reload > 0) return false;
      if (s->loaded == 0) { if (s->reserve == 0) { check(false, "ran out of HE before the bunker opened"); return true; } in.reload = true; return false; }
      if (std::fabs(s->yaw - s->yawT) > 0.02f || std::fabs(s->elev - s->elevT) > 0.02f) return false;
      if (a.rt[C].state[panelK] == PANEL_BREACHED) return true;
      if (impactT0 >= 0 && a.lastImpact.time <= impactT0) return false;   // wait for the previous shell to land
      if (impactT0 >= 0 && a.lastImpact.time > impactT0) { ++hits; impactT0 = -1; if (hits == 1) check(a.rt[C].state[panelK] != PANEL_BREACHED, "casemate survives the first real PaK hit"); return false; }
      static int fired = 0;
      if (!in.fire) { in.fire = true; impactT0 = a.lastImpact.time; ++fired; if (fired == 1) shotQueue.push_back("41_pak_fire"); }
      else in.fire = false;
      return t > 60; }});
    seq.push_back({"breach check", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      in.fire = false;
      if (t < 1.0f) return false;
      bool br = a.rt[C].state[panelK] == PANEL_BREACHED;
      check(br, "PaK 40 HE opened casemate rear bay with " + std::to_string(stP(a)->shots) + " real shells (fired from the world muzzle, timed reloads)");
      summary["pak_shells_to_breach_casemate"] = stP(a)->shots;
      check(stP(a)->loaded + stP(a)->reserve == 6 - stP(a)->shots, "ammunition conserved: loaded + reserve = 6 - shots (" + std::to_string(stP(a)->shots) + " fired)");
      shotQueue.push_back("42_casemate_breached_by_pak");
      return true; }});
    // ---------------------------------------------------------------- the four mechanical stops
    auto stopTest = [this](const char* name, float dy, float dp, float wantYaw, float wantElev) {
      seq.push_back({name, [this, name, dy, dp, wantYaw, wantElev](App& a, float t) {
        auto& in = a.scriptedInput;
        if (t < 0.02f) { in.dyaw = dy; in.dpitch = dp; return false; }
        in.dyaw = in.dpitch = 0;
        if (t < 6.0f) return false;
        StationRT* s = stP(a);
        vec3 dir; a.stationMuzzle(*I(a, P), a.rt[P], dir);
        vec3 carF = yawQuat(s->carYaw) * vec3(0, 0, 1);
        float horiz = angleBetween(vec3(dir.x, 0, dir.z), carF);
        char b[200];
        std::snprintf(b, sizeof b, "%s stop: traverse %.2f elev %.2f (expected %.1f / %.1f), bore %.1f deg off the carriage centreline", name, s->yaw, s->elev, wantYaw, wantElev, horiz);
        bool okYaw = dy == 0 || (std::fabs(s->yaw - wantYaw) < 0.05f && std::fabs(horiz - std::fabs(wantYaw)) < 0.6f);
        bool okElev = dp == 0 || std::fabs(s->elev - wantElev) < 0.05f;
        check(okYaw && okElev && (dy == 0 || s->atYawStop) && (dp == 0 || s->atElevStop), b);
        return true; }});
    };
    stopTest("right", 80, 0, 32.5f, 0); stopTest("left", -120, 0, -32.5f, 0);
    seq.push_back({"centre", [this](App& a, float t) {
      auto& in = a.scriptedInput; if (t < 0.02f) { in.dyaw = -stP(a)->yawT; in.dpitch = -stP(a)->elevT; return false; } in.dyaw = in.dpitch = 0; return t > 3.5f; }});
    stopTest("up", 0, 60, 0, 22.0f); stopTest("down", 0, -60, 0, -5.0f);
    // ---------------------------------------------------------------- hold fire through Aim -> Move -> Aim
    seq.push_back({"to move", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      if (t < 0.02f) { shots0 = a.shotsFired; in.fire = true; in.switchPos = true; in.dpitch = 5; return false; }
      in.fire = true;
      if (t < 1.6f) return false;
      check(stP(a)->pos2 == ST_HANDLER && !stP(a)->deployed, "Switch position: gunner -> preparing -> handler (Move)");
      return true; }});
    seq.push_back({"push", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      in.fire = true; in.move = vec2(0, 1);
      if (t < 0.02f) craterAt0 = stP(a)->pos;
      if (t < 3.0f) return false;
      in.move = vec2(0);
      float moved = glm::length(stP(a)->pos - craterAt0);
      check(moved > 1.5f && moved < 2.5f, "handler pushes the carriage slowly (" + std::to_string(moved).substr(0, 4) + " m in 3 s)");
      shotQueue.push_back("43_pak_move");
      return true; }});
    seq.push_back({"turn 90", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      in.fire = true;
      if (t < 0.02f) yaw0 = stP(a)->carYaw;
      in.move = vec2(-1, 0);
      if (std::fabs(wrapDeg(stP(a)->carYaw - yaw0 + 180) - 180) < 90.0f && t < 12) return false;
      in.move = vec2(0);
      float turned = wrapDeg(stP(a)->carYaw - yaw0 + 180) - 180;
      check(std::fabs(turned - 90) < 1.5f, "carriage turned " + std::to_string(turned).substr(0, 5) + " deg");
      check(a.shotsFired == shots0, "no shot while moving with the trigger held");
      return true; }});
    seq.push_back({"deploy", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      in.fire = true;
      if (t < 0.02f) { in.switchPos = true; return false; }
      if (t < 0.3f) { check(stP(a)->pos2 == ST_DEPLOYING, "deploying (trails spread, spades set)"); return false; }
      if (t < 2.4f) return false;
      check(stP(a)->deployed && stP(a)->pos2 == ST_HANDLER && a.shotsFired == shots0, "deployed; still no shot");
      in.switchPos = true; return true; }});
    seq.push_back({"aim again", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      if (t < 1.0f) { in.fire = true; return false; }                 // still held after returning to Aim
      if (t < 1.05f) { check(stP(a)->pos2 == ST_GUNNER && a.shotsFired == shots0, "back in Aim with the trigger still held: no discharge (fresh press required)"); in.fire = false; return false; }
      return true; }});
    stopTest("right after 90 deg reposition", 80, 0, 32.5f, 0);
    stopTest("left after 90 deg reposition", -120, 0, -32.5f, 0);
    // ---------------------------------------------------------------- blocked moves and failed deployment
    seq.push_back({"to move 2", [this](App& a, float t) {
      auto& in = a.scriptedInput; if (t < 0.02f) { in.switchPos = true; return false; } return t > 1.6f; }});
    seq.push_back({"drive into house", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      // point the carriage at the farmhouse, then push until the sweep stops it
      StationRT* s = stP(a);
      vec3 to = a.doc.find(F)->pos - s->pos;
      float want = degrees(std::atan2(to.x, to.z));
      float dyaw = wrapDeg(want - s->carYaw + 180) - 180;
      if (std::fabs(dyaw) > 1.0f) { in.move = vec2(dyaw > 0 ? -1.0f : 1.0f, 0); return t > 30; }
      in.move = vec2(0, 1);
      static vec3 last{0}; static float still = 0;
      if (glm::length(s->pos - last) < 1e-4f) still += 1.0f / 60; else still = 0;
      last = s->pos;
      if (still < 1.0f && t < 60) return false;
      in.move = vec2(0);
      float dist = glm::length(vec2(s->pos.x - a.doc.find(F)->pos.x, s->pos.z - a.doc.find(F)->pos.z));
      check(!s->moveOk && s->status.find("structure") != std::string::npos && dist > 4.0f, "carriage sweep stops at the farmhouse (" + s->status + ", " + std::to_string((int)dist) + " m from centre)");
      return true; }});
    seq.push_back({"steep crater", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      StationRT* s = stP(a);
      if (t < 0.02f) {
        in.move = vec2(0, -1); return false; }
      if (t < 4.0f) { in.move = vec2(0, -1); return false; }           // back away 2.8 m
      if (t < 4.05f) {
        vec3 ahead = s->pos + yawQuat(s->carYaw) * vec3(0, 0, 1.8f);
        a.craterAt(vec3(ahead.x, a.terrain.height(ahead.x, ahead.z), ahead.z), 2.0f, 0.7f);
        return false; }
      in.move = vec2(0, 1);
      if (t < 9.0f) return false;
      in.move = vec2(0);
      check(!s->moveOk && (s->status.find("steep") != std::string::npos || s->status.find("uneven") != std::string::npos || s->status.find("spades") != std::string::npos),
            "carriage refuses to roll into a steep crater (" + s->status + ")");
      // deployment on unsupported ground fails and keeps Move
      std::string why; vec3 bad = s->pos + yawQuat(s->carYaw) * vec3(0, 0, 2.2f);
      bool fits = a.carriageFits(*I(a, P), a.rt[P], bad, s->carYaw, why);
      vec3 keep = s->pos; s->pos = bad;
      in.switchPos = true;
      a.switchStationPosition();
      check(!fits && s->pos2 == ST_HANDLER && !s->deployed && s->status.find("Cannot deploy") == 0, "deployment on the crater edge fails and stays in Move (" + why + ")");
      s->pos = keep; in.switchPos = false;
      return true; }});
    // ---------------------------------------------------------------- real-shell crater, empty the gun, crate resupply
    seq.push_back({"deploy 3", [this](App& a, float t) {
      auto& in = a.scriptedInput; if (t < 0.02f) { in.switchPos = true; return false; } if (t < 2.5f) return false; in.switchPos = true; return true; }});
    seq.push_back({"shell the ground", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      StationRT* s = stP(a);
      static bool groundHit = false; static float seen = -1;
      if (t < 0.02f) { groundHit = false; seen = a.lastImpact.time; }
      if (a.lastImpact.time > seen) { seen = a.lastImpact.time; if (a.lastImpact.surface == "ground" && a.lastExplosion.crater) groundHit = true; }
      summary["pasture_crater_from_shell"] = groundHit;
      if (t < 0.4f) {   // traverse to the stop that points away from the farmhouse, depress fully: into the pasture
        vec3 l = glm::inverse(yawQuat(s->carYaw)) * (a.doc.find(F)->pos - s->pos);
        float side = l.x > 0 ? -32.5f : 32.5f;
        in.dyaw = side - s->yawT; in.dpitch = -5.0f - s->elevT; return false; }    // depressed, off to the side: into the pasture
      in.dyaw = in.dpitch = 0;
      if (s->reload > 0) return false;
      if (s->loaded == 0 && s->reserve > 0) { in.reload = true; return false; }
      if (s->loaded == 0 && s->reserve == 0) return true;
      if (std::fabs(s->elev - s->elevT) > 0.02f || std::fabs(s->yaw - s->yawT) > 0.02f) return false;
      in.fire = !in.fire;
      return t > 60; }});
    seq.push_back({"crater by shell", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      in.fire = false;
      if (t < 1.5f) return false;
      check(summary.value("pasture_crater_from_shell", false), "a real HE shell on the pasture cut a crater");
      check(stP(a)->loaded == 0 && stP(a)->reserve == 0, "gun empty: 0 loaded, 0 reserve");
      in.interact = true; return true; }});   // E leaves the gun
    seq.push_back({"dismount", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      if (t < 0.3f) return false;
      auto ch = Physics::character();
      check(a.occupied == 0 && !Physics::capsuleBlocked(ch.pos + vec3(0, 0.1f, 0), 1.7f, 0.3f), "E leaves the gun at a valid dismount point");
      // gun was moved: the crate must refuse until the gun is back in reach
      std::string m = a.crateTransaction(K);
      check(m.find("out of reach") != std::string::npos, "crate refuses: moved gun is out of its service radius (" + m + ")");
      // bring the gun back beside the crate (session overlay), crate down to 1 shell
      StationRT* s = stP(a);
      s->pos = a.doc.find(P)->pos; s->carYaw = a.doc.find(P)->yaw;
      Physics::setTransform(a.rt[P].staticBody, s->pos, yawQuat(s->carYaw));
      a.crateStock[K]["75mm_he"] = 1;
      Physics::setCharacterPos(Wp(a, K, vec3(0, 0, -1.3f)));
      return true; }});
    seq.push_back({"crate prompt", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      vec3 g = Wp(a, K, vec3(0, 0.3f, 0)); vec3 e = a.eyePos();
      a.eyeYaw = degrees(std::atan2(-(g.x - e.x), -(g.z - e.z))); a.eyePitch = degrees(std::atan2(g.y - e.y, glm::length(vec2(g.x - e.x, g.z - e.z))));
      if (t < 0.3f) return false;
      check(a.target.valid && a.target.kind == 2 && a.target.inst == K, "aiming at the crate shows the ammunition prompt");
      in.interact = true; return true; }});
    seq.push_back({"resupply", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      if (t < 0.2f) return false;
      StationRT* s = stP(a);
      check(s->reserve == 1 && s->loaded == 0 && a.crateStock[K]["75mm_he"] == 0, "finite crate with one shell left moves exactly one: PaK 40 reserve 1, loaded still 0 until R");
      std::string m = a.crateTransaction(K);
      check(s->reserve == 1 && m.find("no 75mm_he") != std::string::npos, "an empty crate cannot create ammunition (" + m + ")");
      return true; }});
    seq.push_back({"reset", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      a.exitExplore();
      StationRT* s = stP(a);
      check(glm::length(s->pos - a.doc.find(P)->pos) < 1e-4f && s->loaded == 1 && s->reserve == 5, "return to Creative: gun back at its authored position with 1 + 5 rounds");
      check(a.crateStock[K]["75mm_he"] == 12, "crate back to its authored stock (12)");
      int dmg = 0; for (auto& [id, r] : a.rt) dmg += r.damaged;
      check(dmg == 0 && a.terrain.tiles.empty(), "casemate and pasture restored (playtest overlay discarded)");
      check(a.doc.find(P)->pos == vec3(0, a.doc.find(P)->pos.y, 0), "moved gun was not persisted to the map");
      return true; }});
  }
  stage = 601;
  if (cur >= seq.size()) { finish(app); return; }
  if (tcur == 0) logf("[selftest] step: %s", seq[cur].name.c_str());
  bool done2 = seq[cur].fn(app, tcur);
  tcur += dt;
  if (done2) { ++cur; tcur = 0; }
}

