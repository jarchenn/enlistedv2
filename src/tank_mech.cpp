// Mech Tank (the user: "add mech tank in, autonomous and no driver required. should be able to go over small obstacles
// and break through walls"). A crewless vehicle on the tank system (content/vehicles/cyber_tank.json: its "mech" block);
// TankSys::stepAll runs mechThink before stepOne, which then takes the AI's throttle / steer / aim / fire instead of a
// player's. Everything else is the tank's: hull movement and slope limits, ramming (walls breached by the energy rule,
// low obstacles crushed: tank_ram.cpp, def.mechStep), the gun, the HE / AP loader, damage and the wreck.
//   * target: the nearest hostile it can see (a soldier of the other side, an enemy Mech Tank, or the tank the local player
//     is in when on the other side), within def.mechSight, with a clear ray from the turret and the weather's fog
//     transmittance over 0.06 (the soldiers' own sight rule). It keeps a target 3 s unless one 30 % nearer shows up.
//   * drive: toward the target to def.mechStandoff (backs off inside half of it), else toward the enemy spawn; a pivot
//     when the heading is far off; stuck (no speed for 2.5 s under throttle): 1.8 s back and turning, then on.
//   * gun: the barrel led for drop (HE / AP muzzle velocity), fired when laid within 1.5 deg, loaded and in range;
//     HE at soldiers, AP at vehicles (the loader's next choice).
// mechDecide is the drive / fire rule alone (no world), checked headless by --check-mech (mechSelfCheck).
#include "tank.h"
#include "app.h"
#include "game.h"
#include "net.h"
#include "physics.h"
#include <cmath>

namespace {
float wrap180(float d) { d = std::fmod(d + 180.0f, 360.0f); if (d < 0) d += 360.0f; return d - 180.0f; }   // tank_sim.cpp's (file-local there)
uint64_t bodyUD(uint64_t id) { return id << 16; }
}

struct MechDecision { float throttle = 0, steer = 0; bool fire = false; };
// yawDeg: the hull heading (asset convention: forward = (sin, 0, cos)); goal: where to drive (the target when there is
// one); dist: horizontal distance to it; aimErrDeg: barrel vs the wanted direction
MechDecision mechDecide(float yawDeg, vec3 pos, vec3 goal, bool haveTarget, bool visible, float standoff, float heRange, float aimErrDeg, bool loaded, bool recovering) {
  MechDecision m;
  const vec3 dv = goal - pos; const float dist = glm::length(vec2(dv.x, dv.z));
  const float want = glm::degrees(std::atan2(dv.x, dv.z)), err = wrap180(want - yawDeg);
  m.steer = std::clamp(-err / 25.0f, -1.0f, 1.0f);   // yawRate = -steer x turn: a positive error turns toward it
  if (haveTarget && dist < standoff) {
    m.throttle = dist < 0.5f * standoff ? -0.5f : 0.0f;
    if (std::fabs(err) < 20.0f) m.steer = 0;   // the turret does the rest
  } else if (!haveTarget && dist < 20.0f) { m.throttle = 0; m.steer = 0; }   // at the goal: hold
  else m.throttle = std::fabs(err) > 60.0f ? 0.2f : 1.0f;
  if (recovering) { m.throttle = -0.7f; m.steer = 0.8f; }
  m.fire = haveTarget && visible && loaded && aimErrDeg < 1.5f && dist <= heRange;
  return m;
}

void TankSys::mechThink(App& app, TankRT& t, float dt) {
  const TankDef& d = *t.def;
  Game* G = gGame && gGame->active ? gGame : nullptr;
  mat4 H, Tu, Gn, Rc; rig(t, H, Tu, Gn, Rc);
  vec3 mdir; const vec3 muz = muzzleWorld(t, mdir);
  const vec3 eye = vec3(Tu * vec4(d.turretPivot + vec3(0, 0.9f, 0), 1));
  auto sees = [&](vec3 p) {
    vec3 dv = p - eye; float L = glm::length(dv); if (L < 0.5f || L > d.mechSight) return false;
    Physics::Hit h; if (Physics::castRayEx(eye, dv / L, L - 0.7f, h, bodyUD(t.id))) return false;
    return app.R.fogT(eye, p) > 0.06f;
  };
  const int localTeam = G && G->local() ? G->local()->team : 0;   // a plain play-test (no match): the player counts as side A
  auto targetPos = [&](vec3& p, bool& vehicle) -> bool {
    if (t.aiTank) { TankRT* o = get(t.aiTank); if (!o || o->wrecked) return false; p = o->pos + vec3(0, 1.6f, 0); vehicle = true; return true; }
    if (t.aiSlot >= 0) {
      if (t.aiSlot == 1 << 20) { if (!G && !occupied) { p = app.eyePos() - vec3(0, 0.4f, 0); vehicle = false; return true; } return false; }
      if (!G || t.aiSlot >= (int)G->sold.size()) return false;
      const Soldier& s = G->sold[t.aiSlot]; if (!s.inUse || !s.alive) return false;
      p = s.feet + vec3(0, 1.1f, 0); vehicle = false; return true;
    }
    return false;
  };
  // ---- choose a target twice a second
  t.aiKeepT -= dt; t.aiThinkT -= dt;
  if (t.aiThinkT <= 0) {
    t.aiThinkT = 0.5f;
    vec3 cur; bool curVeh = false; const bool have = targetPos(cur, curVeh) && sees(cur);
    const float curD = have ? glm::length(cur - t.pos) : 1e9f;
    float best = 1e9f; int bSlot = -1; uint64_t bTank = 0;
    if (G) for (const Soldier& s : G->sold) {
      if (!s.inUse || !s.alive || s.team == t.aiSide) continue;
      if (s.localPlayer && occupied) continue;   // in a vehicle: the vehicle is the target
      const vec3 p = s.feet + vec3(0, 1.1f, 0); const float dd = glm::length(p - t.pos);
      if (dd < best && sees(p)) { best = dd; bSlot = s.slot; bTank = 0; }
    }
    else if (!occupied && t.aiSide != 0) { const vec3 p = app.eyePos() - vec3(0, 0.4f, 0); const float dd = glm::length(p - t.pos); if (dd < best && sees(p)) { best = dd; bSlot = 1 << 20; } }
    for (auto& [id, o] : tanks) {
      if (id == t.id || o.wrecked) continue;
      const bool hostile = o.aiSide >= 0 ? o.aiSide != t.aiSide : (id == occupied && localTeam != t.aiSide);
      if (!hostile) continue;
      const vec3 p = o.pos + vec3(0, 1.6f, 0); const float dd = glm::length(p - t.pos);
      if (dd < best && sees(p)) { best = dd; bTank = id; bSlot = -1; }
    }
    if (!have || t.aiKeepT <= 0 || best < 0.7f * curD) {
      if (best < 1e8f) { t.aiSlot = bSlot; t.aiTank = bTank; t.aiKeepT = 3.0f; }
      else if (!have) { t.aiSlot = -1; t.aiTank = 0; }
    }
  }
  vec3 tp; bool veh = false; const bool haveT = targetPos(tp, veh);
  const bool vis = haveT && sees(tp);
  // ---- unstick
  if (t.aiBackT > 0) t.aiBackT -= dt;
  else if (std::fabs(t.aiThrottle) > 0.3f && std::fabs(t.speed) < 0.3f) { t.aiStuckT += dt; if (t.aiStuckT > 2.5f) { t.aiBackT = 1.8f; t.aiStuckT = 0; } }
  else t.aiStuckT = 0;
  // ---- gun: led for drop with this round's velocity; the loader's next choice by target kind
  float aimErr = 180.0f;
  if (haveT) {
    const float v = veh ? d.velAP : d.velHE, L = glm::length(tp - muz), tof = L / std::max(v, 50.0f);
    const vec3 want = glm::normalize(tp + vec3(0, 0.5f * 9.81f * tof * tof, 0) - muz);
    t.aiAim = want; t.aiAimOn = true;
    aimErr = glm::degrees(std::acos(std::clamp(glm::dot(glm::normalize(mdir), want), -1.0f, 1.0f)));
    t.selected = veh ? SHELL_AP : SHELL_HE;
  } else t.aiAimOn = false;
  const MechDecision m = mechDecide(t.yaw, t.pos, haveT ? tp : t.aiGoal, haveT, vis, d.mechStandoff, d.mechHeRange, aimErr, t.loaded != SHELL_NONE && t.recoilT > 1.0f, t.aiBackT > 0);
  t.aiThrottle = m.throttle; t.aiSteer = m.steer; t.aiFire = m.fire;
}

uint64_t TankSys::spawnMech(App& app, vec3 near, float faceYaw, int side) {
  const AssetDef* a = app.lib.find("tank_cyber_tank");
  if (!a || !defFor(a)) { logf("mech tank: no vehicle definition (content/vehicles/cyber_tank.json): none spawned"); return 0; }
  for (float r : {0.0f, 8.0f, 12.0f, 16.0f, 22.0f, 30.0f, 40.0f})
    for (int k = 0; k < (r < 0.5f ? 1 : 12); ++k) {
      const float an = k * 0.5235988f; vec3 p = near + vec3(std::cos(an) * r, 0, std::sin(an) * r); p.y = app.terrain.height(p.x, p.z);
      bool crowded = false; for (auto& [id, o] : tanks) if (glm::length(vec2(o.pos.x - p.x, o.pos.z - p.z)) < 9.0f) crowded = true;
      if (crowded || !app.checkPlacement(a, p, faceYaw, 0).ok) continue;
      const uint64_t id = app.placeInstance(a, p, faceYaw);
      if (!id) continue;
      for (auto& i : app.doc.instances) if (i.id == id) { if (!i.extra.is_object()) i.extra = json::object(); i.extra["tank"]["aiSide"] = side; }
      app.syncRuntime(); sync(app);
      logf("mech tank: side %c at (%.1f %.1f %.1f), id %llu", side == 0 ? 'A' : 'B', p.x, p.y, p.z, (unsigned long long)id);
      return id;
    }
  logf("mech tank: no free spot near (%.0f %.0f) for side %d", near.x, near.z, side);
  return 0;
}

// Game::begin: n[side] Mech Tanks at that side's vehicle spawn (else its infantry spawn, else the explore start), each
// heading for the other side's. Local only: tanks are not replicated on LAN, so a LAN match places none.
void mechSpawnForMatch(App& app, Game& G, const int n[2]) {
  if (n[0] + n[1] <= 0) return;
  if (gNet && gNet->role != Net::NONE) { logf("mech tank: not on LAN (vehicles are not replicated): %d / %d skipped", n[0], n[1]); return; }
  auto spawnOf = [&](int side, vec3& p, float& yaw) {
    for (auto& v : G.match.sc.vehicleSpawns) if (v.team == side) { p = v.pos; yaw = v.yaw; return true; }
    for (auto& s : G.match.sc.spawns) if (s.team == side || (s.team < 0 && (int)s.role == side)) { p = s.pos; yaw = s.yaw; return true; }
    return false;
  };
  for (int side = 0; side < 2; ++side) {
    vec3 home = app.exploreStartPos + vec3(side ? 30.0f : -30.0f, 0, 0); float yaw = 0; spawnOf(side, home, yaw);
    vec3 goal(0); float gy; if (!spawnOf(1 - side, goal, gy)) goal = vec3(0);
    for (int k = 0; k < std::min(n[side], 8); ++k) {
      const uint64_t id = app.tanks.spawnMech(app, home, yaw, side);
      if (TankRT* t = app.tanks.get(id)) t->aiGoal = goal;
    }
  }
}

// --check-mech (headless, no GL): the drive / fire rule on fixed cases
int mechSelfCheck() {
  int fails = 0;
  auto chk = [&](bool ok, const char* what) { logf("[mech] %s %s", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };
  MechDecision m = mechDecide(0, vec3(0), vec3(0, 0, 100), true, true, 45, 400, 10, true, false);
  chk(m.throttle == 1.0f && std::fabs(m.steer) < 1e-4f, "target 100 m dead ahead: full throttle, no steer");
  chk(!m.fire, "barrel 10 deg off: holds fire");
  m = mechDecide(0, vec3(0), vec3(100, 0, 0), true, true, 45, 400, 0.5f, true, false);
  chk(m.steer < -0.99f && m.throttle < 0.5f, "target 90 deg to +x (yaw +90 wanted): pivots that way (steer -1, little throttle)");
  chk(m.fire, "laid within 1.5 deg, loaded, visible, in range: fires");
  m = mechDecide(0, vec3(0), vec3(0, 0, 30), true, true, 45, 400, 0.5f, true, false);
  chk(m.throttle == 0.0f && m.fire, "inside the standoff (30 < 45 m): stops and fires");
  m = mechDecide(0, vec3(0), vec3(0, 0, 15), true, true, 45, 400, 0.5f, true, false);
  chk(m.throttle < 0.0f, "closer than half the standoff: backs off");
  m = mechDecide(0, vec3(0), vec3(0, 0, 500), true, true, 45, 400, 0.5f, true, false);
  chk(!m.fire, "beyond HE range (500 > 400 m): holds fire");
  m = mechDecide(0, vec3(0), vec3(0, 0, 100), true, false, 45, 400, 0.5f, true, false);
  chk(!m.fire, "no line of sight: holds fire");
  m = mechDecide(0, vec3(0), vec3(0, 0, 100), true, true, 45, 400, 0.5f, false, false);
  chk(!m.fire, "not loaded: holds fire");
  m = mechDecide(0, vec3(0), vec3(0, 0, 10), false, false, 45, 400, 180, true, false);
  chk(m.throttle == 0.0f, "no target, at its goal: holds");
  m = mechDecide(0, vec3(0), vec3(0, 0, 100), true, true, 45, 400, 0.5f, true, true);
  chk(m.throttle < 0.0f && m.steer > 0.5f, "stuck: backs and turns");
  logf("[mech] %s (%d failed)", fails ? "FAIL" : "all ok", fails);
  return fails ? 1 : 0;
}
