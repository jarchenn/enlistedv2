// VH5 helicopter (AH-64D Apache, content/vehicles/ah64d_apache.json, docs/vehicles.md#helicopter): a TankDef with
// "heli": true that keeps the tanks' entry / exit, collision body, damage matrix (as an armoured car), wreck fire and
// persistence, with its own step:
//   flight   forgiving arcade model: velocities chase stick targets (W/S forward / back, A/D strafe, Space climb,
//            Ctrl descend) at flight.accel; no input = hover; the nose turns toward the mouse yaw at flight.yawRate;
//            lift only once the rotor is spun up (spinUp_s after entering, spinDown_s after leaving);
//            ground: terrain or anything solid under the skids; buildings: the hull boxes and the rotor disc are swept
//            per axis, so it slides along walls and never passes through them
//   rotors   main / tail rotor parts spin about their hubs with the rotor speed
//   weapons  LMB: M230 chin gun toward the crosshair (within its traverse / elevation), explosive 30 mm rounds
//            (Projectile::cannon30: a small blast per hit), heat-limited; R: one Hydra rocket per press from the pod
//            muzzles, left / right in turn (R only reloads elsewhere: the explore step does not run while piloting)
//   death    a counted hit that empties the durability: power lost, it spins and falls, crashes in a blast and burns as
//            a wreck; the crew dies on impact
// Single player / host only: vehicles are not replicated over LAN.
#include "app.h"
#include "game.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>

namespace {
float approach(float v, float t, float step) { float d = t - v; return std::fabs(d) <= step ? t : v + (d > 0 ? step : -step); }
float wrap180(float d) { d = std::fmod(d + 180.0f, 360.0f); if (d < 0) d += 360.0f; return d - 180.0f; }
uint64_t bodyUD(uint64_t id) { return id << 16; }
}

// ground under the skids: anything solid below (a pad, a roof), else the terrain
float TankSys::heliGround(App& app, const TankRT& t, vec3 p) const {
  float g = app.terrain.height(p.x, p.z);
  Physics::Hit h;
  if (Physics::castRayEx(p + vec3(0, 1.5f, 0), vec3(0, -1, 0), 60.0f, h, bodyUD(t.id))) g = std::max(g, h.pos.y);
  return g;
}

// the airframe (hull boxes) and the rotor disc at a pose overlap something solid (own body and the terrain ignored)
bool TankSys::heliBlocked(const TankRT& t, vec3 pos, float yaw) const {
  const TankDef& d = *t.def;
  quat q = yawQuat(yaw);
  vec3 lift(0, 0.12f, 0);   // the skids rest on the ground: the boxes start a little above it
  for (auto& b : d.hullBoxes) if (Physics::boxBlocked(pos + q * b.c + lift, b.h, q, bodyUD(t.id), true)) return true;
  if (d.rotorDisc.h.x > 0 && Physics::boxBlocked(pos + q * d.rotorDisc.c, d.rotorDisc.h, q, bodyUD(t.id), true)) return true;
  return false;
}

void TankSys::stepHeli(App& app, TankRT& t, float dt, const ExploreInput* in) {
  const TankDef& d = *t.def;
  bool focused = app.selftest || glfwGetWindowAttrib(app.win, GLFW_FOCUSED);
  // rotor: spins up while someone is at the controls, runs down after
  t.rotor = approach(t.rotor, in ? 1.0f : 0.0f, dt / std::max(0.1f, in ? d.spinUp : d.spinDown));
  t.rotorAng = std::fmod(t.rotorAng + 360.0f * d.rotorRps * t.rotor * dt, 360.0f);
  t.tailAng = std::fmod(t.tailAng + 360.0f * d.tailRps * t.rotor * dt, 360.0f);
  t.rpm = t.rotor;
  float lift = std::clamp((t.rotor - 0.8f) / 0.2f, 0.0f, 1.0f);
  vec3 fwd = yawQuat(t.yaw) * vec3(0, 0, 1), rgt = glm::normalize(glm::cross(fwd, vec3(0, 1, 0)));
  // stick targets (no input: hover in place)
  vec3 want(0); float wantVy = 0;
  if (in && lift > 0) {
    float f = std::clamp(in->move.y, -1.0f, 1.0f), s = std::clamp(in->move.x, -1.0f, 1.0f);
    want = fwd * (f >= 0 ? f * d.hMaxFwd : f * d.hMaxBack) + rgt * (s * d.hMaxSide);
    wantVy = in->jumpHeld ? d.hClimb : in->crouch ? -d.hSink : 0.0f;
    // the nose follows the mouse yaw; the mouse pitch aims within limits
    app.eyePitch = std::clamp(app.eyePitch, d.hPitchMin, d.hPitchMax);
    vec3 cf = App::forward(app.eyeYaw, 0);
    float wantYaw = degrees(std::atan2(cf.x, cf.z));
    if (!t.grounded || lift >= 1.0f) t.yaw += std::clamp(wrap180(wantYaw - t.yaw), -d.hYawRate * dt, d.hYawRate * dt);
  }
  vec2 vh(t.vel.x, t.vel.z), wh(want.x, want.z), dv = wh - vh;
  float L = glm::length(dv), step = d.hAccel * dt * std::max(0.2f, lift);
  vh += L > step ? dv / L * step : dv;
  t.vel.y = approach(t.vel.y, wantVy, 6.0f * dt * lift) - 9.81f * (1.0f - lift) * dt;
  if (t.grounded && wantVy <= 0) vh *= std::exp(-5.0f * dt);   // skids on the ground: no sliding
  t.vel.x = vh.x; t.vel.z = vh.y;
  // move per axis (vertical first): blocked components stop, the rest slides along the obstacle
  vec3 p = t.pos;
  for (int ax : {1, 0, 2}) {
    vec3 q = p; q[ax] += t.vel[ax] * dt;
    if (ax == 1) { float g = heliGround(app, t, q); if (q.y < g) { q.y = g; if (t.vel.y < 0) t.vel.y = 0; } }
    if (heliBlocked(t, q, t.yaw)) { t.vel[ax] = 0; continue; }
    p = q;
  }
  t.pos = p;
  t.grounded = t.pos.y <= heliGround(app, t, t.pos) + 0.05f;
  // attitude for the eye: nose down with forward speed, banked with sideways speed
  float vf = glm::dot(t.vel, fwd), vs = glm::dot(t.vel, rgt);
  t.pitch = approach(t.pitch, t.grounded ? 0.0f : -12.0f * vf / std::max(1.0f, d.hMaxFwd) + 6.0f * std::max(0.0f, -vf) / std::max(1.0f, d.hMaxBack), 20.0f * dt);
  t.roll = approach(t.roll, t.grounded ? 0.0f : 12.0f * vs / std::max(1.0f, d.hMaxSide), 25.0f * dt);
  t.speed = glm::length(vec2(t.vel.x, t.vel.z));
  // ---- weapons
  t.gunCool = std::max(0.0f, t.gunCool - dt); t.rocketCool = std::max(0.0f, t.rocketCool - dt);
  t.gunHeat = std::max(0.0f, t.gunHeat - d.gun30Cool * dt);
  if (t.overheat && t.gunHeat <= d.gun30Resume) t.overheat = false;
  if (in) {
    // chin gun: toward what the crosshair is on, within its traverse / elevation (drawn through the turret / gun rig)
    vec3 aim = aimPoint(app, t);
    mat4 H = hullMatrix(t);
    vec3 piv = vec3(H * vec4(d.gunPivot, 1));
    vec3 dh = glm::normalize(vec3(glm::inverse(H) * vec4(aim - piv, 0)));
    t.tYaw = std::clamp(degrees(std::atan2(dh.x, dh.z)), d.gun30YawMin, d.gun30YawMax);
    t.gElev = std::clamp(degrees(std::asin(std::clamp(dh.y, -1.0f, 1.0f))), d.gun30ElevMin, d.gun30ElevMax);
    bool fireHeld = in->fire && focused;
    if (fireHeld && !t.overheat && t.gunAmmo > 0 && lift > 0.5f) {
      while (t.gunCool <= 0 && t.gunAmmo > 0 && !t.overheat) {
        vec3 dir; vec3 m = muzzleWorld(t, dir);
        float s = radians(d.gun30Spread);
        vec3 up = std::fabs(dir.y) > 0.95f ? vec3(1, 0, 0) : vec3(0, 1, 0), r2 = glm::normalize(glm::cross(dir, up)), u2 = glm::cross(r2, dir);
        vec3 fd = glm::normalize(dir + r2 * ((app.fx.rnd() * 2 - 1) * s) + u2 * ((app.fx.rnd() * 2 - 1) * s));
        app.fireProjectile(m, fd, d.gun30Vel, PROJ_HE, d.gun30Dmg, bodyUD(t.id), (t.gunShots % 3) == 0 ? 2 : 0, "30mm_hedp");
        Projectile& pr = app.projectiles.back(); pr.cannon30 = true; pr.blastR = d.gun30R; pr.craterScale = 0.12f;
        --t.gunAmmo; ++t.gunShots; t.gunCool += 60.0f / d.gun30Rpm; t.gunHeat += d.gun30Heat;
        app.fx.flash(m + dir * 0.1f, 0.35f, 0.04f, vec4(9, 6, 2.5f, 1));
        app.audio.play(d.gun30Sound, m, 1.0f);
        if (t.gunHeat >= 1.0f) { t.overheat = true; app.toast(d.gun30Name + " overheated - cooling", 2.5); }
      }
    } else if (fireHeld && t.gunAmmo <= 0 && t.gunCool <= 0) { app.audio.play("click", t.pos + vec3(0, 1, 4), 0.6f); t.gunCool = 0.4f; }
    if (t.gunCool < 0) t.gunCool = 0;
    // R: one rocket per press from the next pod (left / right in turn), toward the crosshair within 12 deg of the nose
    bool r = in->reload && focused;
    if (r && !t.reloadPrev && lift > 0.5f) {
      if (t.rockets <= 0 || d.pods.empty()) app.toast("No rockets left.", 1.5);
      else if (t.rocketCool <= 0) {
        vec3 pod = vec3(H * vec4(d.pods[t.rocketSide % d.pods.size()], 1));
        vec3 nose = glm::normalize(vec3(H * vec4(0, 0, 1, 0))), dir = glm::normalize(aim - pod);
        if (glm::dot(dir, nose) < std::cos(radians(12.0f))) dir = nose;
        app.fireProjectile(pod + dir * 0.4f, dir, d.rocketVel, PROJ_ROCKET, d.rocketDmg, bodyUD(t.id), false, "hydra70");
        Projectile& rp = app.projectiles.back();
        rp.blastDmg = d.rocketDmg; rp.blastR = d.rocketR; rp.craterScale = d.rocketCrater; rp.splashGun = d.rocketSplashGun; rp.atKill = true; rp.gravScale = d.rocketGrav;
        app.fx.backblast(pod - dir * 0.9f, -dir);
        app.fx.flash(pod + dir * 0.3f, 0.9f, 0.08f, vec4(9, 6, 3, 1));
        app.audio.play("shot_panzerschreck", pod, 1.0f);
        --t.rockets; ++t.rocketsFired; t.rocketSide = (t.rocketSide + 1) % std::max<int>(1, (int)d.pods.size()); t.rocketCool = d.rocketCooldown;
      }
    }
    t.reloadPrev = r;
    // the pilot's body rides along (AI targeting, audio listener); no collision update while flying
    Physics::setCharacterPos(t.pos + vec3(0, 0.6f, 0));
    if (in->interact) {
      if (!t.grounded || t.speed > 1.0f) app.toast("Land first to get out.", 2);
      else exitTank(app, false);
    }
  }
  updateBody(app, t, false);
  effects(app, t, dt);
}

// destroyed in the air: no power, a growing spin, falling; the crash is a blast and leaves the burning wreck
void TankSys::stepHeliFall(App& app, TankRT& t, float dt) {
  t.rotor = approach(t.rotor, 0.15f, dt / 3.0f);
  t.rotorAng = std::fmod(t.rotorAng + 360.0f * t.def->rotorRps * t.rotor * dt, 360.0f);
  t.tailAng = std::fmod(t.tailAng + 360.0f * t.def->tailRps * t.rotor * 0.3f * dt, 360.0f);
  t.yawRate = std::min(t.yawRate + 140.0f * dt, 320.0f);
  t.yaw = std::fmod(t.yaw + t.yawRate * dt, 360.0f);
  t.vel.y -= 9.81f * 0.85f * dt;
  t.vel.x *= std::exp(-0.4f * dt); t.vel.z *= std::exp(-0.4f * dt);
  t.roll = approach(t.roll, 25.0f, 15.0f * dt); t.pitch = approach(t.pitch, -15.0f, 10.0f * dt);
  vec3 p = t.pos;
  for (int ax : {1, 0, 2}) {
    vec3 q = p; q[ax] += t.vel[ax] * dt;
    if (ax == 1) { float g = heliGround(app, t, q); if (q.y <= g) { q.y = g; p = q; heliCrash(app, t); t.pos = p; return; } }
    if (ax != 1 && heliBlocked(t, q, t.yaw)) { t.vel[ax] = 0; continue; }
    p = q;
  }
  t.pos = p;
  // a smoke and fire trail from the engines on the way down
  mat4 H = hullMatrix(t);
  vec3 e = vec3(H * vec4(t.def->engine, 1));
  if (app.fx.rnd() < 0.7f) app.fx.smoke(e, vec3(0, 1.0f, 0) - t.vel * 0.2f, 0.9f, 4.0f, vec4(0.08f, 0.08f, 0.08f, 0.7f), 1.2f, 0.6f);
  if (app.fx.rnd() < 0.3f) app.fx.flash(e, 1.4f, 0.08f, vec4(8, 3.5f, 1, 1));
  if (Physics::hasCharacter() && occupied == t.id) Physics::setCharacterPos(t.pos + vec3(0, 0.6f, 0));
  updateBody(app, t, false);
}

void TankSys::heliCrash(App& app, TankRT& t) {
  t.falling = false; t.vel = vec3(0); t.yawRate = 0; t.rotor = 0; t.speed = 0; t.burnT = 0;
  t.roll = 18.0f; t.pitch = -6.0f;
  vec3 c = t.pos + vec3(0, 1.5f, 0);
  app.explode(c, vec3(0, -1, 0), 300.0f, 6.0f, 1.2f, bodyUD(t.id), WALL_HE);
  app.fx.explosion(c, vec3(0, 1, 0), "ground", true);
  app.fx.flash(c, 7.0f, 0.35f, vec4(9, 5, 2, 1));
  app.audio.play("explosion_big", c, 1.0f);
  app.toast(t.def->name + " crashed", 4);
  updateBody(app, t, true);
  if (occupied == t.id) heliKillCrew(app, t);
}

// the crew does not survive: out of the airframe (the tank bail-out search), then killed
void TankSys::heliKillCrew(App& app, TankRT& t) {
  if (occupied != t.id) return;
  exitTank(app, true);
  if (gGame && gGame->active)
    if (Soldier* s = gGame->local(); s && s->alive) gGame->killSoldier(gGame->localSlot, -1, vec3(0, -1, 0));
  app.toast("Your helicopter went down - you did not survive.", 5);
}

bool TankSys::heliCamera(const App& app, const TankRT& t, vec3& eye, vec3& fwd, float& fov) const {
  // chase camera: behind and above along the mouse look, pulled in front of anything between it and the airframe
  fwd = App::forward(app.eyeYaw, app.eyePitch);
  vec3 piv = vec3(hullMatrix(t) * vec4(0, 2.6f, 0.5f, 1));
  vec3 want = piv - fwd * 17.0f + vec3(0, 3.0f, 0), dir = want - piv;
  float L = glm::length(dir);
  Physics::Hit h;
  if (L > 1e-3f && Physics::castRayEx(piv, dir / L, L, h, bodyUD(t.id))) want = piv + dir / L * std::max(1.0f, h.t - 0.6f);
  eye = want; fov = 70;
  return true;
}

void TankSys::drawHeli(App& app, const TankRT& t, vec4 tint, const mat4& h) {
  const TankDef& d = *t.def; const GpuModel* gm = d.model.get();
  auto put = [&](const std::vector<int>& ps, const mat4& M) { for (int k : ps) app.R.worldModels.push_back({gm, k, M, tint}); };
  auto spin = [&](vec3 hub, vec3 axis, float deg) { return h * glm::translate(mat4(1), hub) * glm::rotate(mat4(1), radians(deg), axis) * glm::translate(mat4(1), -hub); };
  put(d.hullParts, h);
  put(d.rotorParts, spin(d.rotorHub, vec3(0, 1, 0), -t.rotorAng));
  put(d.tailRotorParts, spin(d.tailHub, vec3(1, 0, 0), t.tailAng));
  mat4 hm, tu, g, rc; rig(t, hm, tu, g, rc);
  put(d.turretParts, tu); put(d.gunParts, g);
}

void TankSys::drawHeliUI(App& app, TankRT& t) {
  const TankDef& d = *t.def;
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  // crosshair (the camera ray: the gun and the rockets converge on what it is on)
  ImU32 col = t.overheat ? IM_COL32(255, 120, 60, 230) : IM_COL32(150, 255, 150, 230);
  dl->AddCircle(c, 14, col, 24, 1.5f);
  dl->AddLine(ImVec2(c.x - 26, c.y), ImVec2(c.x - 8, c.y), col, 1.5f); dl->AddLine(ImVec2(c.x + 8, c.y), ImVec2(c.x + 26, c.y), col, 1.5f);
  dl->AddLine(ImVec2(c.x, c.y - 26), ImVec2(c.x, c.y - 8), col, 1.5f); dl->AddLine(ImVec2(c.x, c.y + 8), ImVec2(c.x, c.y + 26), col, 1.5f);
  float agl = t.pos.y - heliGround(app, t, t.pos);
  char b[256];
  std::snprintf(b, sizeof b, "%s   ALT %3.0f m   %3.0f km/h   ROTOR %3.0f%%   DURABILITY %d\n30 mm %4d  heat %s   HYDRA %2d (%s pod next)",
                d.name.c_str(), agl, t.speed * 3.6f, t.rotor * 100, t.hp, t.gunAmmo, t.overheat ? "OVERHEATED" : "", t.rockets, t.rocketSide == 0 ? "left" : "right");
  ImVec2 ts = ImGui::CalcTextSize(b), p(c.x - ts.x * 0.5f, io.DisplaySize.y - ts.y - 70);
  dl->AddRectFilled(ImVec2(p.x - 12, p.y - 8), ImVec2(p.x + ts.x + 12, p.y + ts.y + 22), IM_COL32(12, 12, 10, 200), 4);
  dl->AddText(p, IM_COL32(235, 235, 220, 255), b);
  // heat bar
  float w = ts.x, hb = std::clamp(t.gunHeat, 0.0f, 1.0f);
  dl->AddRectFilled(ImVec2(p.x, p.y + ts.y + 6), ImVec2(p.x + w, p.y + ts.y + 14), IM_COL32(50, 50, 45, 220));
  dl->AddRectFilled(ImVec2(p.x, p.y + ts.y + 6), ImVec2(p.x + w * hb, p.y + ts.y + 14), t.overheat ? IM_COL32(255, 90, 40, 255) : IM_COL32(230, 190, 70, 255));
  const char* keys = "Space up | Ctrl down | W/S forward / back | A/D strafe | mouse: nose + aim | LMB 30 mm | R rocket | E get out (landed)";
  ImVec2 ks = ImGui::CalcTextSize(keys);
  dl->AddText(ImVec2(c.x - ks.x * 0.5f, io.DisplaySize.y - 34), IM_COL32(210, 210, 200, 220), keys);
}

void TankSys::heliEffects(App& app, TankRT& t, float dt) {
  const TankDef& d = *t.def;
  // rotor downwash: dust rings under the airframe when low
  float agl = t.pos.y - heliGround(app, t, t.pos);
  if (t.rotor > 0.5f && agl < 12.0f && app.terrain.wetAt(t.pos.x, t.pos.z) < 0.5f) {
    t.dustAcc += dt * 14.0f * t.rotor * (1.0f - agl / 12.0f);
    while (t.dustAcc >= 1) {
      t.dustAcc -= 1;
      float a = app.fx.rnd() * 6.2831853f, r = 3.0f + 4.0f * app.fx.rnd();
      vec3 p(t.pos.x + std::cos(a) * r, app.terrain.height(t.pos.x + std::cos(a) * r, t.pos.z + std::sin(a) * r) + 0.2f, t.pos.z + std::sin(a) * r);
      app.fx.smoke(p, vec3(std::cos(a), 0.25f, std::sin(a)) * 4.0f, 0.8f, 2.0f, vec4(0.5f, 0.45f, 0.36f, 0.18f), 1.8f, 0.1f);
    }
  }
  // turbine + rotor sound: the engine loop pitched up with the rotor (no recording supplied)
  vec3 ep = vec3(hullMatrix(t) * vec4(d.engine, 1));
  bool on = t.rotor > 0.02f;
  if (on && t.engineLoop < 0) t.engineLoop = app.audio.loopStart("engine_tiger", ep, 0.0f, 1.0f);
  if (!on && t.engineLoop >= 0) { app.audio.loopStop(t.engineLoop); t.engineLoop = -1; }
  if (t.engineLoop >= 0) app.audio.loopSet(t.engineLoop, ep, 0.7f * t.rotor, 0.6f + 1.4f * t.rotor);
}
