// Explore mode: first-person walkthrough of the same placed instances shown in Creative.
#include "app.h"
#include "ui_glass.h"   // UI2: glass panel backgrounds
#include "mainmenu.h"
#include "ui_flat.h"
#include "editor_nav.h"
#include "loading.h"
#include "mpui.h"
#include "scenario.h"
#include "minimap.h"
#include "abilities.h"
#include "crumb.h"
#include "settings.h"
#include "mpai.h"
#include "rules.h"
#include "lighting.h"
#include "w3.h"
#include "water_query.h"
#include "loot.h"   // BR2 Battle Royale loot
#include "royale.h"
#include "game.h"
#include "commands.h"   // CMD1 "/" command line
#include "sd_game.h"   // SD3
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>

// Mouse look matched to the field of view (docs/fps-weapons.md#mouse-sensitivity). Running around, the view is 72 deg (vertical)
// and one count turns 0.1 deg. Near the crosshair a turn of a deg moves the image by f tan(a) pixels, f = (H / 2) /
// tan(fov / 2); keeping that the same in every view gives  a_view = a_hip x tan(fov_view / 2) / tan(72 / 2)  (small
// angles), i.e. the same mouse movement slides the target across the screen by the same distance with irons, scopes
// and tank sights as on foot. Applied to the FOV actually rendered (ADS blends and the RMB sight zoom included).
float App::lookScaleFor(float fovDeg) {
  fovDeg = std::clamp(fovDeg, 1.0f, 120.0f);
  return std::tan(radians(fovDeg) * 0.5f) / std::tan(radians(72.0f) * 0.5f);
}

static bool kd(GLFWwindow* w, int k) { return k >= 0 && glfwGetKey(w, k) == GLFW_PRESS; }   // UI1: -1 = an unbound action
static bool kp(GLFWwindow* w, int k) {
  static std::map<int, bool> prev;
  bool d = kd(w, k), p = prev[k]; prev[k] = d; return d && !p;
}

vec3 App::eyePos() const {
  auto c = Physics::character();
  return c.pos + vec3(0, eyeHeight, 0) + leanOffset();
}

// MV3 lean: the head swings on an arc about a waist pivot radius below the eye (sideways out to `side`, the drop ~4 cm
// follows from the arc); yaw / pitch untouched
vec3 App::leanOffset() const {
  if (std::fabs(lean.x) < 1e-4f) return vec3(0);
  vec3 f = forward(eyeYaw, 0); f.y = 0; f = glm::normalize(f); vec3 r = glm::normalize(glm::cross(f, vec3(0, 1, 0)));
  float R = std::max(0.3f, leanCfg.radius), a = std::asin(std::clamp(leanCfg.side * lean.x / R, -0.9f, 0.9f));
  return r * (R * std::sin(a)) - vec3(0, R * (1 - std::cos(a)), 0);
}

// MV3: the lean parameter: Q / E held (arbitrated with Interact / Next weapon in the input), a critically damped spring
// to the target (in_s going out, out_s coming back: a reversal passes smoothly through upright, a release mid-lean
// returns from where it is); no lean sprinting, prone, swimming, climbing or building; the head and the shoulders are
// kept clearance_m off any geometry on the leaning side (rays from the upright eye and shoulders), so it never looks
// through a wall
void App::stepLean(float dt, const ExploreInput& in, bool sprinting) {
  auto c = Physics::character();
  bool can = !sprinting && c.posture != Physics::PRONE && !swim.active && !mantle.active && !build.active && !occupied && !tanks.occupied;
  float tgt = can ? (in.leanR ? 1.0f : 0.0f) - (in.leanL ? 1.0f : 0.0f) : 0.0f;
  vec3 f = forward(eyeYaw, 0); f.y = 0; f = glm::normalize(f); vec3 r = glm::normalize(glm::cross(f, vec3(0, 1, 0)));
  vec3 eye0 = c.pos + vec3(0, eyeHeight, 0);
  for (int side = 0; side < 2; ++side) {   // 0 left, 1 right: free room for the head and the shoulders
    vec3 d = side ? r : -r; float room = 1e9f;
    for (float dy : {0.0f, -0.32f}) { Physics::Hit h; if (Physics::castRayEx(eye0 + vec3(0, dy, 0), d, leanCfg.side + leanCfg.clear + 0.05f, h)) room = std::min(room, h.t); }
    float lim = room > 1e8f ? 1.0f : std::clamp((room - leanCfg.clear) / std::max(0.05f, leanCfg.side), 0.0f, 1.0f);
    (side ? lean.limitR : lean.limitL) = lim;
  }
  tgt = std::clamp(tgt, -lean.limitL, lean.limitR);
  float T = std::fabs(tgt) > std::fabs(lean.x) && tgt * lean.x >= 0 ? leanCfg.in : leanCfg.out;
  float w = 4.6f / std::max(0.05f, T);   // settles (~1 %) in T
  lean.v += (w * w * (tgt - lean.x) - 2.0f * w * lean.v) * dt;
  lean.x += lean.v * dt;
  if (lean.x > lean.limitR) { lean.x = lean.limitR; lean.v = std::min(lean.v, 0.0f); }
  if (lean.x < -lean.limitL) { lean.x = -lean.limitL; lean.v = std::max(lean.v, 0.0f); }
}

bool App::enterExplore() {
  if (mode == Mode::Explore) return true;
  std::unique_ptr<LoadingJob> lj;   // UI2: a match start gets the loading screen (a plain play-test is quick)
  if (gGame && gGame->pendingStart) { Scenario sc = Scenario::fromJson(gUi.matchScenario); const TemplateInfo* tp = findTemplate(doc.templateId); lj = std::make_unique<LoadingJob>(*this, doc.name, std::string(gameModeName(sc.rules.mode)), 2, tp ? tp->thumbnail : 0u, doc.templateId); }
  { LoadStep ls("enterExplore syncRuntime"); syncRuntime(); Physics::optimizeBroadPhase(); }   // bodies added since load: balanced tree for play
  if (!doc.start.set) {
    // No marker yet: start on the ground at the centre of the view (or under the camera), facing the same way.
    vec3 hit;
    vec3 fwd = forward(camYaw, camPitch);
    if (!terrain.raycast(camPos, fwd, 400.0f, hit)) hit = camPos;
    hit.y = terrain.height(hit.x, hit.z);
    doc.start.set = true; doc.start.pos = hit; doc.start.yaw = camYaw; dirty = true;
    toast("No start marker was set, so you start where the view was pointing. Press M in Creative to choose a start point.", 6);
  }
  vec3 p = doc.start.pos;
  if (!startMarkerValid(p)) {
    bool found = false;
    for (float r = 0.5f; r <= 10.0f && !found; r += 0.5f)
      for (int k = 0; k < 24 && !found; ++k) {
        float a = k * 6.2831853f / 24;
        vec3 q = p + vec3(std::cos(a) * r, 0, std::sin(a) * r);
        float t; uint64_t ud;
        float top = q.y + 2.5f;
        if (Physics::castRay(vec3(q.x, top, q.z), vec3(0, -1, 0), 6.0f, t, ud)) q.y = top - t; else q.y = terrain.height(q.x, q.z);
        if (startMarkerValid(q)) { p = q; found = true; }
      }
    if (!found) { toast("Start marker is obstructed and no free ground was found within 10 m. Move the marker.", 6); return false; }
    char b[160]; std::snprintf(b, sizeof b, "Start marker was obstructed: starting %.1f m away at the nearest clear ground.", glm::length(vec2(p.x - doc.start.pos.x, p.z - doc.start.pos.z)));
    toast(b, 6);
  }
  savedCam.set = true; savedCam.pos = camPos; savedCam.yaw = camYaw; savedCam.pitch = camPitch;
  resetInteractStates();   // walkthrough is a playtest: always start from authored defaults
  resetDamage();           // ...including walls (authored/applied damage only)
  resetGround();           // ...and ground
  resetCombat();           // guns at authored positions/ammo, crates at authored stock
  resetWeapons(); flame.clear(this);   // FT1: no stream, parcels or roar left over
  Physics::createCharacter(p);
  exploreStartPos = p; exploreStartYaw = doc.start.yaw;
  eyeYaw = doc.start.yaw; eyePitch = 0; eyeHeight = 1.62f; riseT = riseDur = 0;
  swim = SwimState{}; swim.breath = swimCfg.breath;   // swim.cpp
  stamina = move.staminaMax; staminaIdle = 0;
  physAccum = 0; pauseMenu = false; target = {};
  if (looking) looking = false;
  R.resetExposure(1.2f);
  mode = Mode::Explore;
  syncCursor();
  { LoadStep ls("enterExplore match start"); mpaiEnterExplore(*this); }   // starts a match when the Play menu asked for one
  return true;
}

void App::exitExplore() {
  if (mode != Mode::Explore) return;
  clearGrenades();
  mpaiExitExplore(*this);
  Physics::destroyCharacter();
  closeRack("mode change");
  leaveStation();
  resetInteractStates();   // discard temporary exploration state
  resetDamage();
  resetGround();
  resetCombat();
  resetWeapons(); flame.clear(this);   // FT1: no stream, parcels or roar left over
  removeSessionPlacements();   // defences built in Explore are a playtest overlay unless applied
  camPos = savedCam.pos; camYaw = savedCam.yaw; camPitch = savedCam.pitch;
  mode = Mode::Creative; pauseMenu = false; target = {};
  syncCursor();
}

void App::returnToStart() {
  Physics::setCharacterPos(exploreStartPos);
  eyeYaw = exploreStartYaw; eyePitch = 0;
}

bool App::leafBlocked(const Instance& i, const InteractDef& d, float p) const {
  if (!Physics::hasCharacter()) return false;
  auto c = Physics::character();
  float h = c.crouched ? Physics::crouchHeight() : Physics::standHeight(), r = Physics::radius();
  vec3 a = c.pos + vec3(0, r, 0), b = c.pos + vec3(0, h - r, 0);
  quat iq = yawQuat(i.yaw);
  for (auto& lf : d.leaves) {
    mat4 L = leafMatrix(i, lf, d, p);
    quat lq = iq * glm::angleAxis(radians(lf.sign * leafAngle(d, p)), d.axis);
    for (auto& col : lf.colliders) {
      vec3 wc = vec3(L * vec4(col.center, 1));
      if (segOBBDist2(a, b, wc, col.size * 0.5f, lq * col.rot) < (r + 0.015f) * (r + 0.015f)) return true;
    }
  }
  return false;
}

InteractTarget App::findTarget(vec3 eye, vec3 dir) const {
  const App::ActiveSet& act = const_cast<App&>(*this).activeSet();
  InteractTarget best;
  const float reach = kInteractReach;            // doors / windows: 2.0 -> 2.5 m (+25 %)
  float tWall = reach + 0.5f; uint64_t ud = 0;
  float th;
  bool rayHit = Physics::castRay(eye, dir, reach + 0.5f, th, ud);
  if (rayHit) tWall = th;
  float bestT = 1e9f;
  for (auto& [idx, rp] : act.inter) {
    const Instance& i = doc.instances[idx]; const InstanceRT& r = *rp;
    if (glm::length(i.pos - eye) > glm::length(r.asset->bmax - r.asset->bmin) + 4) continue;
    quat iq = yawQuat(i.yaw);
    mat4 M = instanceMatrix(i);
    for (size_t k = 0; k < r.asset->interactables.size(); ++k) {
      auto& d = r.asset->interactables[k];
      float t;
      auto consider = [&](float tt) {
        if (tt <= reach && tt <= tWall + 0.08f && tt < bestT) { bestT = tt; best = {true, i.id, (int)k, tt}; }
      };
      for (auto& lf : d.leaves) {
        mat4 L = leafMatrix(i, lf, d, r.progress[k]);
        quat lq = iq * glm::angleAxis(radians(lf.sign * leafAngle(d, r.progress[k])), d.axis);
        for (auto& p : lf.parts) {
          if (rayOBB(eye, dir, vec3(L * vec4(p.center, 1)), p.size * 0.5f + vec3(0.01f), lq * p.rot, t)) consider(t);
        }
      }
      if (r.progress[k] < 0.5f || d.apAlways) {
        vec3 c = vec3(M * vec4((d.apMin + d.apMax) * 0.5f, 1));
        if (rayOBB(eye, dir, c, (d.apMax - d.apMin) * 0.5f, iq, t)) consider(t);
      }
    }
  }
  // guns, crates, racks: the whole object is selectable. A physics hit on any of its colliders (shield, wheels,
  // trails, rack edge -- child bodies carry the owner's id in the high bits) resolves to the owner at the true
  // surface distance; otherwise a slightly shrunk bounding box, still occluded by anything nearer.
  const float objReach = kInteractReach + 0.5f;  // 2.4 -> 3.0 m
  float tObj = 1e9f; uint64_t objId = 0; int objKind = 0;
  auto kindOf = [&](const InstanceRT& r) {
    bool isStation = !r.asset->station.kind.empty(), isCrate = r.asset->crate.is_object() && !r.asset->crate.empty();
    return isStation ? (r.st.wrecked ? 0 : 1) : r.asset->rack ? 3 : isCrate ? 2 : 0;
  };
  if (rayHit && th <= objReach) {
    uint64_t owner = ud >> 16;
    auto it = rt.find(owner);
    if (it != rt.end() && owner != occupied && kindOf(it->second)) { tObj = th; objId = owner; objKind = kindOf(it->second); }
  }
  if (!objId)
    for (auto& [idx, rp] : act.objs) {
      const Instance& i = doc.instances[idx]; const InstanceRT& r = *rp;
      int kind = kindOf(r);
      if (!kind || i.id == occupied) continue;
      bool isStation = kind == 1;
      mat4 M = isStation ? stationMatrix(i, r) : instanceMatrix(i);
      vec3 c = vec3(M * vec4((r.asset->bmin + r.asset->bmax) * 0.5f, 1));
      vec3 half = (r.asset->bmax - r.asset->bmin) * 0.475f;   // 5 % inside the bounds: no oversized trigger
      if (glm::length(c - eye) > objReach + glm::length(half)) continue;
      float t;
      quat q = yawQuat(isStation ? r.st.carYaw : i.yaw);
      if (rayOBB(eye, dir, c, half, q, t) && t <= objReach && t <= tWall + 0.15f && t < tObj) { tObj = t; objId = i.id; objKind = kind; }
    }
  if (objId && tObj < bestT) best = {true, objId, -1, tObj, objKind};
  return best;
}

void App::toggleInteract(uint64_t id, int k) {
  auto it = rt.find(id); if (it == rt.end()) return;
  auto& r = it->second;
  const Instance* inst = nullptr;
  for (auto& i : doc.instances) if (i.id == id) inst = &i;
  if (!inst) return;
  auto& d = r.asset->interactables[k];
  int& dir = r.dir[k];
  if (dir == 0) dir = r.progress[k] > 0.5f ? -1 : 1;
  else dir = -dir;  // reverse smoothly from the current angle
  vec3 c = vec3(instanceMatrix(*inst) * vec4((d.apMin + d.apMax) * 0.5f, 1));
  if (dir > 0) audio.play(d.sound + "_open", c);
}

void App::exploreStep(float dt, const ExploreInput& in) {
  // ---- animate interactables (only those moving)
  for (auto& [idx, rp] : activeSet().inter) {
    const Instance& i = doc.instances[idx]; InstanceRT& r = *rp;
    for (size_t k = 0; k < r.dir.size(); ++k) {
      if (!r.dir[k]) continue;
      auto& d = r.asset->interactables[k];
      float np = std::clamp(r.progress[k] + r.dir[k] * dt / d.duration, 0.0f, 1.0f);
      if (leafBlocked(i, d, np)) {
        if (r.dir[k] < 0) r.dir[k] = 1;  // closing into the player: reopen smoothly
        else r.dir[k] = 0;               // opening into the player: stop
        continue;
      }
      r.progress[k] = np;
      if (np <= 0.0f && r.dir[k] < 0) {
        r.dir[k] = 0;
        audio.play(d.sound + "_close", vec3(instanceMatrix(i) * vec4((d.apMin + d.apMax) * 0.5f, 1)));
      } else if (np >= 1.0f && r.dir[k] > 0) r.dir[k] = 0;
      updateLeafBodies(i, r, (int)k);
      if (!r.dir[k]) { vec3 c = vec3(instanceMatrix(i) * vec4((d.apMin + d.apMax) * 0.5f, 1)); lightingInvalidate(c - vec3(4.0f), c + vec3(4.0f)); }   // R2: a door / shutter opened or closed: the GI around it is re-captured first
    }
  }
  {   // tanks (tank_sim.cpp): every tank steps (crew loader, engines, wrecks); the occupied one takes all input
    bool wasInTank = tanks.occupied != 0;
    tanks.stepAll(*this, dt, in);
    if (wasInTank || tanks.occupied) { target = {}; blastShake = std::max(0.0f, blastShake - dt * 1.5f); stepProjectiles(dt); fx.update(dt, terrain); return; }   // VH5c: the shake decays in vehicles too
  }
  if (!occupied) { build.dt = dt; stepBuilder(in); }
  if (occupied) { stepStation(dt, in); }
  else if (build.active || mantle.active || swim.active) { ExploreInput w = in; w.fire = w.aim = w.reload = false; w.nextWeapon = false; w.choose1 = w.choose2 = w.slot3 = w.slot4 = false; w.wheel = 0; if (swim.active) w.quickGrenade = false; stepWeapons(dt, w); }   // weapon lowered while building / climbing / swimming
  else if (lootInventoryOpen()) { ExploreInput w = in; w.fire = w.aim = w.reload = false; lootBeforeWeapons(*this); stepWeapons(dt, w); lootAfterWeapons(*this); }   // BR2: the inventory screen is open
  else { lootBeforeWeapons(*this); stepWeapons(dt, in); lootAfterWeapons(*this); }   // BR2: pooled ammo as magazines (no-op outside Battle Royale)
  blastShake = std::max(0.0f, blastShake - dt * 1.5f);
  if (mpaiLocalFrozen(*this)) { stepProjectiles(dt); fx.update(dt, terrain); return; }   // dead in a match: spectating
  swimBreath(dt);   // swim.cpp: breath runs down with the eye under water (swimming or not), then drowning
  // ---- move
  vec3 f = forward(eyeYaw, 0), rgt(std::cos(radians(eyeYaw)), 0, -std::sin(radians(eyeYaw)));
  vec3 wish = f * in.move.y + rgt * in.move.x;
  if (glm::length(wish) > 1) wish = glm::normalize(wish);   // diagonal input never exceeds the axis speed
  // ---- posture: Standing / Crouched / Prone, mutually exclusive, real collision shapes
  //   Ctrl held = crouch; Z toggles prone (entering prone clears crouch intent; while prone Ctrl does not exit);
  //   Z out of prone requests crouch if Ctrl is held, else standing; a request that does not fit stays pending
  //   (the current posture is kept) and resolves when space frees up. Explicit Z overrides pending requests.
  auto before = Physics::character();
  if (in.prone) proneToggle = before.posture != Physics::PRONE;   // GP1: Z always asks for prone when not prone (it no longer cancels a pending request), and gets up when prone
  // Space while prone gets up (to a crouch if Ctrl is held) instead of jumping, like Z
  bool spaceUp = in.jump && before.posture == Physics::PRONE && postureWanted == Physics::PRONE;
  if (spaceUp) proneToggle = false;
  if (before.posture == Physics::PRONE && !proneToggle && postureWanted == Physics::PRONE) postureWanted = in.crouch ? Physics::CROUCH : Physics::STAND;
  else if (proneToggle) postureWanted = Physics::PRONE;
  else postureWanted = in.crouch ? Physics::CROUCH : Physics::STAND;
  int pose = before.posture;
  bool lowered = pose != Physics::STAND || postureWanted != Physics::STAND;
  // ---- climb (mantle.cpp): a held Space at an obstacle 0.4-2.2 m tall pulls the player up ONTO it (the press itself
  // still jumps immediately; a climb that starts during that jump adds no second impulse). One climb per hold.
  if (!in.jumpHeld) { vault.held = 0; vault.latch = false; }
  else vault.held += dt;
  if (gAdmin.fly) vault.latch = true;   // CMD2: no climbing or vaulting while flying (Space is "up")
  if (gAdmin.flyAllowed && in.jump && !occupied) {   // CMD2: a double-tap of Space toggles flying
    static double last = -1; double now = glfwGetTime();
    if (now - last < 0.3) { gAdmin.fly = !gAdmin.fly; if (!gAdmin.fly) gAdmin.fallSafe = true; toast(gAdmin.fly ? "Flying: Space up, Ctrl down, Shift fast; double-tap Space to land" : "Flying off", 3); last = -1; }
    else last = now;
  }
  if (mantle.active) { stepMantle(dt); return; }
  if (!occupied && stepSwim(dt, in, before)) { target = {}; stepProjectiles(dt); fx.update(dt, terrain); return; }   // swim.cpp: chest-deep water
  if (in.jumpHeld && !vault.latch && vault.held >= vaultCfg.hold && before.posture == Physics::STAND && !occupied && (tryStartWindowVault(before.pos, eyeYaw) || tryStartMantle(before.pos, eyeYaw))) { vault.latch = true; spendStamina(move.staminaJump); return; }   // a climb costs like a jump (never refused: trench exits)
  bool rising = riseT < riseDur;
  bool sprinting = in.sprint && adsT < 0.5f && !lowered && !rising && stamina > 1.0f;   // Shift while aiming holds the breath; no sprint while lowered, getting up or out of stamina
  float speed = pose == Physics::PRONE ? move.prone : pose == Physics::CROUCH ? move.crouch : sprinting ? move.sprint : move.run;
  if (activeWeapon >= 0 && activeWeapon < (int)weaponDefs.size()) speed *= weaponDefs[activeWeapon]->moveSpeed;   // GM1: only the weapon in hand (knife 1.3)
  if (rising) speed = std::min(speed, riseSpeed);   // still getting up: the lower posture's pace
  speed *= traversalFactor(Physics::character().pos);   // inside a "slow" wire volume
  Physics::tuning.gravity = move.gravity; Physics::tuning.jumpVelocity = move.jumpVelocity; Physics::tuning.jumpBuffer = move.jumpBuffer; Physics::tuning.coyote = move.coyote;
  bool jump = in.jump && !spaceUp && !(rising && riseProne) && stamina >= move.staminaJump;   // only the prone get-up holds a jump back; too tired: no jump
  const bool flying = gAdmin.fly && !occupied;
  if (flying) {   // CMD2 /fly: where you look, Space up, Ctrl down, Shift fast; collisions kept
    vec3 d = forward(eyeYaw, eyePitch) * in.move.y + rgt * in.move.x; if (glm::length(d) > 1) d = glm::normalize(d);
    d.y += (in.jumpHeld ? 1.0f : 0.0f) - (in.crouch ? 1.0f : 0.0f);
    Physics::flyCharacter(dt, d * (in.sprint ? 30.0f : 9.0f));
  }
  bool jumped = !occupied && !flying && Physics::updateCharacter(dt, wish * speed + knockVel, jump, postureWanted, eyeYaw);
  if (in.prone && proneToggle && !occupied && !flying && Physics::character().posture != Physics::PRONE) { toast("No room to lie down here", 1.5); proneToggle = false; postureWanted = in.crouch ? Physics::CROUCH : Physics::STAND; }   // GP1: only a space smaller than a prone body refuses; the request does not linger (no surprise prone later)
  knockVel *= std::exp(-5.0f * dt);
  if (jumped) spendStamina(move.staminaJump);   // buffered and coyote jumps too
  { int pp = Physics::character().posture; tickStamina(dt, sprinting && glm::length(wish) > 0.1f, pp == Physics::CROUCH || pp == Physics::PRONE ? move.staminaLowRegen : 1.0f); }   // crouched / prone: faster recovery
  auto c = Physics::character();
  if (gAdmin.fly || gAdmin.fallSafe) { if (!gAdmin.fly && c.grounded) gAdmin.fallSafe = false; }   // CMD2: no fall damage while flying or on the way down after it
  else rulesAfterMove(*this, before, c, dt);   // W1: fall damage (match, player only)
  vm.speed = glm::length(vec2(c.pos.x - before.pos.x, c.pos.z - before.pos.z)) / std::max(dt, 1e-4f);
  float eyeTarget = c.posture == Physics::PRONE ? move.eyeProne : c.posture == Physics::CROUCH ? move.eyeCrouch : move.eyeStand;
  // getting up takes time: prone -> stand 1.1 s, prone -> crouch 0.75 s, crouch -> stand 0.45 s (movement.json),
  // an eased rise from where the camera is; going down keeps the quick exponential drop
  if (c.posture != before.posture && (before.posture == Physics::PRONE || (before.posture == Physics::CROUCH && c.posture == Physics::STAND))) {
    riseDur = before.posture == Physics::PRONE ? (c.posture == Physics::STAND ? move.proneToStand : move.proneToCrouch) : move.crouchToStand;
    riseT = 0; riseFrom = eyeHeight; riseSpeed = before.posture == Physics::PRONE ? move.prone : move.crouch; riseProne = before.posture == Physics::PRONE;
  } else if (c.posture != before.posture) riseDur = 0;   // lowering cancels a rise
  if (riseT < riseDur) {
    riseT += dt;
    float u = std::clamp(riseT / std::max(riseDur, 1e-3f), 0.0f, 1.0f);
    eyeHeight = glm::mix(riseFrom, eyeTarget, u * u * (3 - 2 * u));
  } else eyeHeight += (eyeTarget - eyeHeight) * std::min(1.0f, dt * move.eyeRate);
  stepLean(dt, in, sprinting && glm::length(in.move) > 0.1f);   // MV3
  if (eyeHeight > eyeTarget + 0.02f) {   // never raise the camera through a ceiling while standing up
    Physics::Hit ch; vec3 f0 = c.pos + vec3(0, eyeTarget * 0.5f, 0);
    if (Physics::castRayEx(f0, vec3(0, 1, 0), eyeHeight - eyeTarget * 0.5f + 0.1f, ch) && ch.pos.y - 0.1f < c.pos.y + eyeHeight) eyeHeight = std::max(eyeTarget, ch.pos.y - 0.1f - c.pos.y);
  }
  // ---- footsteps by surface
  if (c.grounded) {
    stepTimer += glm::length(vec2(c.pos.x - before.pos.x, c.pos.z - before.pos.z));
    float stride = in.sprint ? 1.1f : 0.75f;
    if (stepTimer > stride) {
      stepTimer = 0;
      std::string s = "step_grass";
      std::string ps = pathSurfaceAt(c.pos);
      if (ps == "gravel" || ps == "concrete") s = "step_stone";   // dirt stays the soft step
      { extern const std::string& groundPaintStep(float, float); const std::string& gs = groundPaintStep(c.pos.x, c.pos.z); if (!gs.empty() && audio.hasEvent(gs)) s = gs; }   // lasso ground paint
      if (c.groundUser != ~0ull && c.groundUser != 0) {
        uint64_t iid = c.groundUser >> 16;
        for (auto& i : doc.instances) if (i.id == iid) s = (c.pos.y - i.pos.y > 0.9f) ? "step_wood" : "step_stone";
      }
      if (c.pos.y < waterAt(*this, c.pos.x, c.pos.z) - 0.05f && audio.hasEvent("step_water")) s = "step_water";   // wading: the bank's deep-water steps (any water, water_query.h)
      audio.play(s, c.pos, in.crouch ? 0.4f : 0.8f);
    }
  }
  // ---- interaction
  target = findTarget(eyePos(), forward(eyeYaw, eyePitch));
  stepRack(in);
  if (in.packUp && !occupied && !rackOpen) packUpHeavy();
  if (occupied) { if (in.interact) { logf("station: leave on interact"); leaveStation(); } }
  else if (in.interact && !showLoadout && !rackOpen && !choiceFor && lootInteract(*this)) {}   // BR2: floor loot
  else if (in.interact && !showLoadout && !rackOpen && !choiceFor && mpaiPickupWeapon(*this)) {}   // GM1: E on a weapon an AI dropped
  else if (!showLoadout && !rackOpen && !choiceFor && tanks.handleInteract(*this, in)) {}   // E on a tank: enter
  else if (showLoadout || rackOpen) {}
  else if (choiceFor) {
    if (in.choose1) occupyStation(choiceFor, ST_GUNNER);
    else if (in.choose2) occupyStation(choiceFor, ST_HANDLER);
    else if (in.interact) choiceFor = 0;
  } else if (in.interact && target.valid) {
    if (target.kind == 1) choiceFor = target.inst;
    else if (target.kind == 2) toast(crateTransaction(target.inst), 5);
    else if (target.kind == 3) openRack(target.inst);
    else toggleInteract(target.inst, target.idx);
  }
  stepProjectiles(dt);
  gm2NearMisses(*this, dt);   // GM2: enemy rounds past the eye (suppression vignette)
  fx.update(dt, terrain);
  if (c.pos.y < -60) { returnToStart(); toast("Fell out of the world: returned to start."); }
}

int App::cursorWanted(bool focused) const {
  if (mode == Mode::Explore) return wantCursor() || !focused ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED;
  return looking ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL;   // editor: captured only while RMB / LMB looking or orbiting
}

bool App::syncCursor() {
  if (selftest || !win) return false;
  static bool raw = false;
  if (!raw) { raw = true; if (glfwRawMouseMotionSupported()) glfwSetInputMode(win, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE); }   // unaccelerated look while disabled
  bool focused = glfwGetWindowAttrib(win, GLFW_FOCUSED) != 0;
  static bool wasFocused = true;
  bool changed = focused != wasFocused; wasFocused = focused;
  int want = cursorWanted(focused);
  if (glfwGetInputMode(win, GLFW_CURSOR) != want) { glfwSetInputMode(win, GLFW_CURSOR, want); ++cursorChanges; changed = true; }
  if (changed) cursorSettle = 2;
  return changed;
}

void App::updateExplore(float dt) {
  ImGuiIO& io = ImGui::GetIO();
  if (w3ExploreKeys(*this)) return;   // creative sessions: P pause (dropin.cpp); Esc opens the pause menu below (UI3)
  if (commandsTakeCreative()) { exitExplore(); return; }   // CMD1 /creative: as F5
  if (!selftest && commandsOwnKeys()) (void)kp(win, GLFW_KEY_ESCAPE);   // CMD1: the command line has the keyboard (its Esc must not open the pause menu)
  else if (!selftest) {
    if ((rackOpen || build.palette) && kp(win, GLFW_KEY_ESCAPE)) { if (rackOpen) closeRack("closed"); else setBuildPalette(false); }
    else if (lootInventoryOpen() && kp(win, GLFW_KEY_ESCAPE)) { lootSetInventory(false); lootOpenCase(0); }   // BR6: Esc closes the inventory / crate first
    else if (sdBuyMenuOpen() && kp(win, GLFW_KEY_ESCAPE)) sdCloseBuyMenu();   // SD3: Esc closes the buy menu first
    else if (kp(win, GLFW_KEY_ESCAPE)) {
      pauseMenu = !pauseMenu; syncCursor();
    }
    if (kp(win, GLFW_KEY_F5)) { exitExplore(); return; }
    if (kp(win, GLFW_KEY_F3)) showStats = !showStats;
    if (kp(win, GLFW_KEY_F12)) settings().s.devOverlay = !settings().s.devOverlay;   // UI1b: developer overlay
    if (kp(win, GLFW_KEY_F4)) panelDebug = !panelDebug;
    if (kp(win, bk("loadout")) && !pauseMenu) { showLoadout = !showLoadout; syncCursor(); }
    if (kp(win, bk("flashlight")) && !pauseMenu) flashlight = !flashlight;   // FX1 headlamp
    if (kp(win, GLFW_KEY_V)) scopeIronsMode = !scopeIronsMode;
  }
  // the cursor is captured unless a menu needs it: every look delta below assumes GLFW_CURSOR_DISABLED (a free cursor
  // stops at the screen edge and look then stops in that direction). syncCursor is the only place that sets the mode.
  syncCursor();
  const bool panelCursor = uiCursor;   // a HUD panel that wanted the cursor last frame (deploy, inventory, buy menu...): no look while it is open
  uiCursor = false;
  if (pauseMenu) return;
  ExploreInput in;
  static double lmx = 0, lmy = 0; static bool haveLast = false;   // not a negative sentinel: captured, the virtual cursor is unbounded and goes negative after panning left; "lmx >= 0" then dropped every look delta until the player panned back right
  if (cursorSettle > 0) { --cursorSettle; haveLast = false; }   // the mode / focus just changed: the cursor jumped, no look from it
  if (useScripted) {
    in = scriptedInput; scriptedInput.interact = false; scriptedInput.jump = false; scriptedInput.reload = false; scriptedInput.switchPos = false; scriptedInput.toggleAmmo = false; scriptedInput.build = false;
    scriptedInput.fireMode = scriptedInput.prone = scriptedInput.packUp = scriptedInput.rackUp = scriptedInput.rackDown = scriptedInput.rackEquip = scriptedInput.rackClose = false;
    scriptedInput.choose1 = scriptedInput.choose2 = false; scriptedInput.nextWeapon = false; scriptedInput.quickGrenade = false; scriptedInput.slot3 = scriptedInput.slot4 = false;
    if (!occupied) { eyeYaw = wrapDeg(eyeYaw + in.dyaw); eyePitch = std::clamp(eyePitch + in.dpitch, -85.0f, 85.0f); }
  } else if (!commandsOwnKeys()) {   // CMD1: while the command line is open no key, button or look reaches the game
    double mx, my; glfwGetCursorPos(win, &mx, &my);
    if (haveLast && !wantCursor() && !panelCursor) {   // not io.WantCaptureMouse: the hidden cursor can sit over a HUD window
      StationRT* gs = station(occupied);
      bool mgMount = gs && (rt[occupied].asset->station.kind == "m2hb" || rt[occupied].asset->station.kind == "mg42");
      // a mounted MG aims like the carried gun (the player's look sensitivity and ADS scale; stepStation drops the rates);
      // field guns keep their handwheels
      float hs = mgMount ? settings().lookDegPerCount(adsT) * lookScaleFor(worldFov) : 0.012f;
      if (gs && gs->pos2 == ST_GUNNER) { in.dyaw = -(float)(mx - lmx) * hs; in.dpitch = -(float)(my - lmy) * hs * (mgMount ? settings().pitchSign() : 1.0f); }   // handwheels / spade grips
      else { float k = settings().lookDegPerCount(adsT) * lookScaleFor(worldFov); in.lookYaw = -(float)(mx - lmx) * k; in.lookPitch = -(float)(my - lmy) * k * settings().pitchSign(); eyeYaw = wrapDeg(eyeYaw + in.lookYaw); eyePitch = std::clamp(eyePitch + in.lookPitch, -85.0f, 85.0f); }
    }
    lmx = mx; lmy = my; haveLast = true;
    if (kd(win, bk("forward"))) in.move.y += 1;
    if (kd(win, bk("back"))) in.move.y -= 1;
    if (kd(win, bk("right"))) in.move.x += 1;
    if (kd(win, bk("left"))) in.move.x -= 1;
    in.sprint = kd(win, bk("sprint"));
    in.crouch = kd(win, binds.crouch) || kd(win, GLFW_KEY_C);
    if (!glfwGetWindowAttrib(win, GLFW_FOCUSED)) { in = ExploreInput{}; }   // focus loss clears held input (crouch, fire, sprint)
    in.jump = kp(win, bk("jump"));
    in.jumpHeld = bk("jump") >= 0 && glfwGetKey(win, bk("jump")) == GLFW_PRESS;
    in.interact = kp(win, bk("interact"));
    {   // MV3 lean keys. A lean key that is also Interact / Next weapon (Q / E by default): with an interact prompt on screen E
        // interacts at once; otherwise a tap (released within tap_s, before any lean) does the action on release and a
        // hold leans. In vehicles, at a gun, building, in the inventory or in the air the keys keep their old meaning.
      bool keep = occupied || tanks.occupied || build.active || lootInventoryOpen() || (gGame && gGame->active && royaleAirborne(gGame->localSlot));
      int kl = bk("leanLeft"), kr = bk("leanRight"), kn = bk("nextWeapon"), ki = bk("interact");
      bool prompt = target.valid || tanks.target;
      auto arb = [&](int idx, int key, int actKey, bool promptNow, bool& action, bool& leanHeld) {
        LeanKey& K = leanKeys[idx];
        bool down = key >= 0 && glfwGetKey(win, key) == GLFW_PRESS;
        if (key != actKey || keep) {   // not shared (rebound), or the old meaning applies here
          leanHeld = !keep && down; K = LeanKey{}; K.held = down; return;
        }
        action = false;   // the plain edge press of the shared key is decided here
        // MV3b: the lean starts on the press itself; released within tap_s it was a tap (the action; the small lean
        // already begun springs back), held longer it was a lean
        if (down && !K.held) { K.down = now; K.consumed = promptNow; K.leaning = false; if (promptNow) action = true; }
        if (down && !K.consumed && now - K.down >= leanCfg.tap) K.leaning = true;
        if (!down && K.held && !K.consumed && !K.leaning) action = true;   // a tap
        leanHeld = down && !K.consumed;
        K.held = down;
      };
      bool next = false, inter = false;
      arb(0, kl, kn, false, next, in.leanL);
      arb(1, kr, ki, prompt, inter, in.leanR);
      if (kl == kn && !keep) in.nextWeapon = next; else if (kl == kn) in.nextWeapon = kp(win, kn);
      if (kr == ki && !keep) in.interact = inter;
      if (kl == ki && !keep) { bool a = false, h = false; arb(0, kl, ki, prompt, a, h); in.interact = in.interact || a; in.leanL = h; }   // swapped bindings
      if (kr == kn && !keep) { bool a = false, h = false; arb(1, kr, kn, false, a, h); in.nextWeapon = in.nextWeapon || a; in.leanR = h; }
    }
    in.fire = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS && !io.WantCaptureMouse;
    in.aim = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS && !io.WantCaptureMouse;
    in.reload = kp(win, bk("reload"));
    in.switchPos = kp(win, GLFW_KEY_T);
    in.toggleAmmo = kp(win, GLFW_KEY_X);
    in.build = kp(win, bk("build"));
    in.fireMode = kp(win, binds.fireMode);
    in.prone = kp(win, binds.prone);
    in.packUp = kp(win, binds.packUp);
    in.quickGrenade = kp(win, binds.grenade);
    builderKeys(in);   // build mode: Z / X rotate (not prone / ammo), Del, Ctrl+Z, Tab (builder.cpp)
    if (rackOpen) {   // the panel owns the keyboard: no movement, fire or look leaks through
      in.rackUp = kp(win, GLFW_KEY_UP) || kp(win, bk("forward")); in.rackDown = kp(win, GLFW_KEY_DOWN) || kp(win, bk("back")); in.rackEquip = kp(win, GLFW_KEY_ENTER);
      in.move = vec2(0); in.fire = in.aim = in.reload = in.jump = in.sprint = false; in.interact = in.nextWeapon = in.fireMode = in.prone = in.build = false;
      in.dyaw = in.dpitch = in.lookYaw = in.lookPitch = 0;
    }
    { extern double gScrollY; in.wheel = io.WantCaptureMouse ? 0.0f : (float)gScrollY; gScrollY = 0; }
    in.choose1 = kp(win, bk("slot1")); in.choose2 = kp(win, bk("slot2"));
    in.slot3 = kp(win, bk("slot3")); in.slot4 = kp(win, bk("slot4"));   // G4: knife, grenades
    if (bk("leanLeft") != bk("nextWeapon") && bk("leanRight") != bk("nextWeapon")) in.nextWeapon = kp(win, bk("nextWeapon"));   // MV3: else decided by the lean arbitration above
    if (kp(win, bk("map"))) mapLarge = !mapLarge;   // GM2: the large map (rebindable, UI1)
    if (kp(win, bk("artillery"))) gm2ToggleArtyMap(*this);   // GM2: artillery (ability; rebindable, default 7)
    if (kp(win, GLFW_KEY_BACKSPACE)) returnToStart();
  }
  // Edge presses (kp: Space, E, R, ...) and station hand-wheel deltas seen on a frame that runs no fixed step (any
  // frame shorter than 1/60 s) are carried to the next frame that does, instead of being dropped (the "Space
  // sometimes does nothing" bug at high frame rates).
  {
    static bool ExploreInput::* const kEdges[] = {&ExploreInput::interact, &ExploreInput::jump, &ExploreInput::reload, &ExploreInput::switchPos, &ExploreInput::toggleAmmo, &ExploreInput::build,
      &ExploreInput::fireMode, &ExploreInput::prone, &ExploreInput::packUp, &ExploreInput::rackUp, &ExploreInput::rackDown, &ExploreInput::rackEquip, &ExploreInput::rackClose,
      &ExploreInput::choose1, &ExploreInput::choose2, &ExploreInput::nextWeapon, &ExploreInput::quickGrenade, &ExploreInput::slot3, &ExploreInput::slot4};
    for (auto m : kEdges) in.*m = in.*m || edgeCarry.*m;
    in.dyaw += edgeCarry.dyaw; in.dpitch += edgeCarry.dpitch; in.wheel += edgeCarry.wheel;
    edgeCarry = ExploreInput{};
    if (physAccum + dt < 1.0f / 60.0f) { for (auto m : kEdges) edgeCarry.*m = in.*m; edgeCarry.dyaw = in.dyaw; edgeCarry.dpitch = in.dpitch; edgeCarry.wheel = in.wheel; }
  }
  if (rackOpen) {   // the panel owns input (keyboard and scripted alike): nothing leaks into movement or weapons
    in.move = vec2(0); in.fire = in.aim = in.reload = in.jump = in.sprint = false; in.interact = in.nextWeapon = in.fireMode = in.prone = in.build = in.packUp = false;
    in.dyaw = in.dpitch = in.lookYaw = in.lookPitch = 0;
  }
  mpaiInput(*this, in);   // match: dead / ended input, grenade keys
  physAccum += dt;
  int steps = 0;
  while (physAccum >= 1.0f / 60.0f && steps < 8) {
    crumb("explore step");
    exploreStep(1.0f / 60.0f, in);
    crumb("match step");
    mpaiStep(*this, 1.0f / 60.0f);   // match: soldiers, AI, grenades, capture
    in.interact = false; in.jump = false; in.reload = false; in.switchPos = false; in.toggleAmmo = false; in.build = false; in.fireMode = in.prone = in.packUp = in.rackUp = in.rackDown = in.rackEquip = in.rackClose = false; in.choose1 = in.choose2 = false; in.nextWeapon = false; in.quickGrenade = false; in.slot3 = in.slot4 = false; in.dyaw = in.dpitch = 0;
    in.wheel = 0;   // one scroll notch turns the build ghost once, however many steps run
    physAccum -= 1.0f / 60.0f; ++steps;
  }
  if (steps == 8) physAccum = 0;   // beyond 8 fixed steps per frame, drop time rather than spiral
  crumb("physics step");
  Physics::step(dt);
  crumb("explore leaves / interior / grenades");
  stepDynLeaves(dt);
  interior.step(*this, dt);
  stepGrenades(dt);
  gm2Step(*this, dt);   // GM2: packs, artillery, near misses
}

void App::drawExploreUI() {
  commandsFrame(*this);   // CMD1: "/" command line
  lootUi(*this);   // BR2: pick-up prompt, heal ring, inventory screen
  if (lootInventoryOpen()) return;   // the inventory replaces the play HUD
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  if (!pauseMenu) {
    // (no centre dot: removed at the user's request; the weapon's own sights are the aim reference)
    if (!occupied && !tanks.occupied && !weaponDefs.empty() && !scopeView()) {   // GM1b: launchers and flamethrowers have no usable sight: mark where the shot goes
      const WeaponDef& d = *weaponDefs[activeWeapon]; const WeaponState& s = wstate[d.id];
      if ((d.action == "launcher" && !d.anim) || d.flame.on) {   // MG3: an animated launcher (RPG-7) aims with its own sights
        vec3 eye = eyePos(), aimDir = forward(eyeYaw + viewKickYaw, eyePitch + viewKickPitch), muzzle = weaponMuzzleWorld(d, s), P;
        if (d.flame.on) {   // a parcel from the nozzle toward the aim point, 0.4 g droop (flame.cpp), at 70 % of its reach
          vec3 fxs = vmApparent(muzzle); Physics::Hit h; float reach = Physics::castRayEx(eye, aimDir, 20.0f, h) ? h.t : 20.0f;
          vec3 dir = reach > 3.0f ? glm::normalize(eye + aimDir * reach - fxs) : aimDir;
          float dist = std::min(reach, d.flame.range * 0.7f), tt = dist / std::max(1.0f, d.flame.speed);
          P = fxs + dir * dist - vec3(0, 0.5f * 3.9f * tt * tt, 0);
        } else {   // the rocket: muzzle toward the aim point, tilted for the zero, then its reduced-gravity drop (weapons.cpp)
          Physics::Hit h; float zr = std::max(5.0f, d.zero); float far = Physics::castRayEx(eye, aimDir, 600, h) ? h.t : 600.0f;
          vec3 dir = glm::normalize(eye + aimDir * far - muzzle);
          float g = 9.81f * d.rocketGrav, tz = zr / d.velocity;
          dir = glm::normalize(dir + vec3(0, 0.5f * g * tz * tz / zr, 0));
          float dist = std::min(far, zr), tt = dist / d.velocity;
          P = muzzle + dir * dist - vec3(0, 0.5f * g * tt * tt, 0);
        }
        vec4 q = projM * viewM * vec4(P, 1);
        if (q.w > 0.05f) {
          ImVec2 r((q.x / q.w * 0.5f + 0.5f) * io.DisplaySize.x, (1 - (q.y / q.w * 0.5f + 0.5f)) * io.DisplaySize.y);
          float k = std::max(1.0f, io.DisplaySize.y / 1080.0f);
          dl->AddCircle(r, 7 * k, IM_COL32(0, 0, 0, 160), 0, 3.0f * k); dl->AddCircle(r, 7 * k, IM_COL32(245, 240, 225, 230), 0, 1.4f * k);
          dl->AddCircleFilled(r, 1.6f * k, IM_COL32(245, 240, 225, 230));
        }
      }
    }
    if (!occupied && !tanks.occupied && !weaponDefs.empty() && !scopeView() && weaponDefs[activeWeapon]->dotCrosshair) {   // SW2: blasters: a white dot in a thin +, a little tighter when aimed
      float k = std::max(1.0f, io.DisplaySize.y / 1080.0f), t = adsT * adsT * (3 - 2 * adsT);
      float gap = (7.0f - 2.5f * t) * k, len = 5.0f * k, w = 1.4f * k;
      ImU32 white = IM_COL32(250, 250, 250, 235), dark = IM_COL32(0, 0, 0, 150);
      for (int q = 0; q < 4; ++q) {
        ImVec2 dv(q == 0 ? 1.0f : q == 1 ? -1.0f : 0.0f, q == 2 ? 1.0f : q == 3 ? -1.0f : 0.0f);
        ImVec2 a(c.x + dv.x * gap, c.y + dv.y * gap), b(c.x + dv.x * (gap + len), c.y + dv.y * (gap + len));
        dl->AddLine(a, b, dark, w + 2.0f * k); dl->AddLine(a, b, white, w);
      }
      dl->AddCircleFilled(c, 2.4f * k, dark); dl->AddCircleFilled(c, 1.5f * k, white);
    }
    // (GM1b's black aim mark in the MG 42's rear notch removed at the user's request: the gun's own sights are the reference)
    if (target.valid && !tanks.occupied && !tanks.target) {
      auto it = rt.find(target.inst);
      if (it != rt.end()) {
        if (target.kind != 0) {
          std::string t = target.kind == 1 ? "E  -  Crew " + it->second.asset->name : target.kind == 3 ? "E  -  Open " + it->second.asset->name : "E  -  Take ammunition (" + it->second.asset->name + ")";
          ImVec2 ts = ImGui::CalcTextSize(t.c_str());
          ImVec2 p(c.x - ts.x * 0.5f, c.y + 28);
          uiGlassPanel(dl, ImVec2(p.x - 10, p.y - 5), ImVec2(p.x + ts.x + 10, p.y + ts.y + 5), std::max(8.0f, (float)4));
          dl->AddText(p, IM_COL32(245, 238, 210, 255), t.c_str());
        } else {
        auto& d = it->second.asset->interactables[target.idx];
        bool opening = it->second.dir[target.idx] != 0 ? it->second.dir[target.idx] < 0 : it->second.progress[target.idx] < 0.5f;
        std::string t = std::string("E  —  ") + (opening ? "Open " : "Close ") + d.label;
        ImVec2 ts = ImGui::CalcTextSize(t.c_str());
        ImVec2 p(c.x - ts.x * 0.5f, c.y + 28);
        uiGlassPanel(dl, ImVec2(p.x - 10, p.y - 5), ImVec2(p.x + ts.x + 10, p.y + ts.y + 5), std::max(8.0f, (float)4));
        dl->AddText(p, IM_COL32(245, 238, 210, 255), t.c_str());
        }
      }
    }
    if (settings().s.devOverlay) dl->AddText(ImVec2(14, 12), IM_COL32(230, 230, 220, 150), "WASD walk | Shift sprint | Ctrl/C crouch | Space jump (stand up when prone) | E interact | LMB fire | RMB sights | R reload | Q weapon | N headlamp | T switch gun position | Backspace start | F4 damage debug | Esc menu");
    else {   // UI1b: a short hint for the first seconds of play; the full list is in the pause menu (Controls)
      static double seen = -10, until = 0; double tn = ImGui::GetTime(); if (tn - seen > 1.0) until = tn + 12.0; seen = tn;
      if (tn < until) { const char* h = gGame && gGame->active ? "Esc  menu & controls     M  map     Tab  scores" : "Esc  menu & controls     M  map";
        float a = (float)std::min(1.0, (until - tn) / 2.0); ImVec2 ts = ImGui::CalcTextSize(h), p(io.DisplaySize.x * 0.5f - ts.x * 0.5f, io.DisplaySize.y - 46);
        dl->AddRectFilled(ImVec2(p.x - 12, p.y - 6), ImVec2(p.x + ts.x + 12, p.y + ts.y + 6), IM_COL32(12, 14, 16, (int)(150 * a)), 6);
        dl->AddText(p, IM_COL32(225, 225, 215, (int)(220 * a)), h); }
    }
    tanks.drawWorldBars(*this);   // TK1: bars over the vehicles around
    if (tanks.occupied) { tanks.drawUI(*this); tanks.drawHealthBar(*this); }   // TK1: the bar over the sight mask
    else { drawCombatUI(); drawWeaponUI(); tanks.drawUI(*this); drawSwimUI(); drawStaminaUI(); }
    if ((showMinimap || mapLarge) && !gm2ArtyMapOpen()) minimapHud(*this, mapLarge);   // GM2
    gm2Hud(*this);   // GM2: suppression vignette, the artillery map
    if (now < noticeUntil) {   // UI2: a glass toast with the accent bar
      ImVec2 ts = ImGui::CalcTextSize(notice.c_str());
      uiGlassPanel(dl, ImVec2(10, 26), ImVec2(36 + ts.x, 38 + ts.y), 10);
      dl->AddRectFilled(ImVec2(10, 34), ImVec2(13, 30 + ts.y), IM_COL32(240, 118, 32, 255));
      dl->AddText(ImVec2(22, 32), IM_COL32(238, 234, 226, 240), notice.c_str());
    }
  }
  drawDamageDebug();
  drawBuilderUI();
  drawRackUI();
  if (showStats) {
    auto ch = Physics::character();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 280, 10), ImGuiCond_Always);
    ImGui::Begin("##estats", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
    ImGui::Text("frame %.2f ms (%.0f fps)", io.DeltaTime * 1000, io.Framerate);
    ImGui::Text("render cpu %.2f gpu %.2f ms", R.stats.cpuMs, R.stats.gpuMs);
    ImGui::Text("pos %.2f %.2f %.2f %s", ch.pos.x, ch.pos.y, ch.pos.z, ch.grounded ? "ground" : "air");
    ImGui::Text("bodies %d", Physics::bodyCount());
    ImGui::End();
  }
  static bool showControls = false;
  if (pauseMenu) {   // UI1b: one column of equal buttons under a title; Controls opens the key list
    (void)c;   // UI4: a flat page in the options language: the blurred game behind, PAUSED, a rule, a column of text actions
    const float W = io.DisplaySize.x, H = io.DisplaySize.y, sc = H / 1080.0f, X0 = 84 * sc, tp = 104 * sc, ry = 54 * sc + tp * 1.02f + 30 * sc;
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Paused", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* pdl = ImGui::GetWindowDrawList();
    flat::backdrop(pdl, ImVec2(0, 0), io.DisplaySize);
    pdl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W * 0.6f, H), IM_COL32(0, 0, 0, 110), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 110));
    flat::text(pdl, flat::titleFont(), tp, ImVec2(X0, 54 * sc), flat::ink(), "PAUSED", 0.01f);
    flat::rule(pdl, X0, W - X0, ry);
    ImGui::SetCursorScreenPos(ImVec2(X0, ry + 40 * sc));
    auto act = [&](const char* l) { ImGui::SetCursorScreenPos(ImVec2(X0, ImGui::GetCursorScreenPos().y)); const bool hit = flat::action(l); ImGui::Dummy(ImVec2(1, 10 * sc)); return hit; };
    if (act("Resume")) { pauseMenu = false; syncCursor(); }
    if (act("Controls")) showControls = true;
    if (act("Settings")) settings().open = true;
    ImGui::Dummy(ImVec2(1, 18 * sc));
    if (act("Return to start marker")) { returnToStart(); pauseMenu = false; syncCursor(); }
    if (act("Return to Creator")) { if (navState().creativeSession) dropinReturn(*this); else exitExplore(); }   // UI3: the editor camera where it was
    if (act("Main menu")) { exitExplore(); mainMenuOpen(*this); }   // UI2
    const bool keepHover = [&] { const bool hit = act("Keep session changes and return"); if (hit) { applyDamageToMap(); exitExplore(); } return ImGui::IsItemHovered(); }();
    if (keepHover) ImGui::SetTooltip("Keeps the current wall damage and craters in the map as one undoable edit.\nTanks keep their damage, wreck and ammunition (not their position).\nWeapons, ammunition, moved guns and doors are not kept.");
    flat::rule(pdl, X0, W - X0, H - 104 * sc);
    flat::text(pdl, uiFontHead(), 15 * sc, ImVec2(X0, H - 132 * sc), flat::grey(), "ESC  RESUME", 0.1f);
    ImGui::End();
  }
  controlsWindow(*this, &showControls);   // UI1b
  if (!pauseMenu) showControls = false;
}

// stamina bar: under the breath bar, only while not full; amber when a jump is no longer affordable
void App::drawStaminaUI() {
  if (stamina >= staminaCap() - 0.01f) return;
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  float W = 240, H = 6, x = io.DisplaySize.x * 0.5f - W * 0.5f, y = io.DisplaySize.y - 112;
  float f = std::clamp(stamina / std::max(0.01f, staminaCap()), 0.0f, 1.0f);
  ImU32 col = stamina < move.staminaJump ? IM_COL32(230, 150, 60, 230) : IM_COL32(225, 225, 210, 220);
  dl->AddRectFilled(ImVec2(x - 2, y - 2), ImVec2(x + W + 2, y + H + 2), IM_COL32(10, 14, 18, 150), 3);
  dl->AddRectFilled(ImVec2(x, y), ImVec2(x + W * f, y + H), col, 2);
}
