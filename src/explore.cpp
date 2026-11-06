// Explore mode: first-person walkthrough of the same placed instances shown in Creative.
#include "app.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>

static bool kd(GLFWwindow* w, int k) { return glfwGetKey(w, k) == GLFW_PRESS; }
static bool kp(GLFWwindow* w, int k) {
  static std::map<int, bool> prev;
  bool d = kd(w, k), p = prev[k]; prev[k] = d; return d && !p;
}

vec3 App::eyePos() const {
  auto c = Physics::character();
  return c.pos + vec3(0, eyeHeight, 0);
}

bool App::enterExplore() {
  if (mode == Mode::Explore) return true;
  syncRuntime();
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
  resetWeapons();
  Physics::createCharacter(p);
  exploreStartPos = p; exploreStartYaw = doc.start.yaw;
  eyeYaw = doc.start.yaw; eyePitch = 0; eyeHeight = 1.62f;
  physAccum = 0; pauseMenu = false; target = {};
  if (looking) looking = false;
  if (!selftest) glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  R.resetExposure(1.2f);
  mode = Mode::Explore;
  return true;
}

void App::exitExplore() {
  if (mode != Mode::Explore) return;
  Physics::destroyCharacter();
  closeRack("mode change");
  leaveStation();
  resetInteractStates();   // discard temporary exploration state
  resetDamage();
  resetGround();
  resetCombat();
  resetWeapons();
  removeSessionPlacements();   // defences built in Explore are a playtest overlay unless applied
  camPos = savedCam.pos; camYaw = savedCam.yaw; camPitch = savedCam.pitch;
  glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  mode = Mode::Creative; pauseMenu = false; target = {};
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
  InteractTarget best;
  const float reach = 2.0f;
  float tWall = 2.5f; uint64_t ud;
  float th;
  if (Physics::castRay(eye, dir, 2.5f, th, ud)) tWall = th;
  float bestT = 1e9f;
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    auto& r = it->second;
    if (r.asset->interactables.empty()) continue;
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
  for (auto& i : doc.instances) {   // guns and crates: aim at the object within reach, occlusion as for doors
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    auto& r = it->second;
    bool isStation = !r.asset->station.kind.empty(), isCrate = r.asset->crate.is_object() && !r.asset->crate.empty(), isRack = r.asset->rack;
    if (!isStation && !isCrate && !isRack) continue;
    if (i.id == occupied) continue;
    if (isStation && r.st.wrecked) continue;   // a wreck offers no interaction
    mat4 M = isStation ? stationMatrix(i, r) : instanceMatrix(i);
    vec3 c = vec3(M * vec4((r.asset->bmin + r.asset->bmax) * 0.5f, 1));
    if (glm::length(c - eye) > 6) continue;
    float t;
    quat q = yawQuat(isStation ? r.st.carYaw : i.yaw);
    if (rayOBB(eye, dir, c, (r.asset->bmax - r.asset->bmin) * 0.5f, q, t) && t <= 2.4f && t <= tWall + 0.25f && t < bestT) {
      bestT = t; best = {true, i.id, -1, t, isStation ? 1 : isRack ? 3 : 2};
    }
  }
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
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    auto& r = it->second;
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
    }
  }
  if (!occupied) stepBuilder(in);
  if (occupied) { stepStation(dt, in); }
  else if (build.active) { ExploreInput w = in; w.fire = w.aim = w.reload = false; w.nextWeapon = false; stepWeapons(dt, w); }   // weapon lowered while building
  else stepWeapons(dt, in);
  blastShake = std::max(0.0f, blastShake - dt * 1.5f);
  // ---- move
  vec3 f = forward(eyeYaw, 0), rgt(std::cos(radians(eyeYaw)), 0, -std::sin(radians(eyeYaw)));
  vec3 wish = f * in.move.y + rgt * in.move.x;
  if (glm::length(wish) > 1) wish = glm::normalize(wish);
  auto before = Physics::character();
  bool sprinting = in.sprint && adsT < 0.5f;   // Shift while aiming holds the breath instead
  float speed = in.crouch ? 1.2f : sprinting ? 4.6f : 2.3f;
  if (!occupied) Physics::updateCharacter(dt, wish * speed, in.jump, in.crouch);
  auto c = Physics::character();
  vm.speed = glm::length(vec2(c.pos.x - before.pos.x, c.pos.z - before.pos.z)) / std::max(dt, 1e-4f);
  float eyeTarget = c.crouched ? 1.05f : 1.62f;
  eyeHeight += (eyeTarget - eyeHeight) * std::min(1.0f, dt * 10);
  // ---- footsteps by surface
  if (c.grounded) {
    stepTimer += glm::length(vec2(c.pos.x - before.pos.x, c.pos.z - before.pos.z));
    float stride = in.sprint ? 1.1f : 0.75f;
    if (stepTimer > stride) {
      stepTimer = 0;
      std::string s = "step_grass";
      if (c.groundUser != ~0ull && c.groundUser != 0) {
        uint64_t iid = c.groundUser >> 16;
        for (auto& i : doc.instances) if (i.id == iid) s = (c.pos.y - i.pos.y > 0.9f) ? "step_wood" : "step_stone";
      }
      audio.play(s, c.pos, in.crouch ? 0.4f : 0.8f);
    }
  }
  // ---- interaction
  target = findTarget(eyePos(), forward(eyeYaw, eyePitch));
  stepRack(in);
  if (in.packUp && !occupied && !rackOpen) packUpHeavy();
  if (occupied) { if (in.interact) leaveStation(); }
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
  fx.update(dt, terrain);
  if (c.pos.y < -60) { returnToStart(); toast("Fell out of the world: returned to start."); }
}

void App::updateExplore(float dt) {
  ImGuiIO& io = ImGui::GetIO();
  if (!selftest) {
    if (rackOpen && kp(win, GLFW_KEY_ESCAPE)) closeRack("closed");
    else if (kp(win, GLFW_KEY_ESCAPE)) {
      pauseMenu = !pauseMenu;
      glfwSetInputMode(win, GLFW_CURSOR, pauseMenu ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
    }
    if (kp(win, GLFW_KEY_F5)) { exitExplore(); return; }
    if (kp(win, GLFW_KEY_F3)) showStats = !showStats;
    if (kp(win, GLFW_KEY_F4)) panelDebug = !panelDebug;
    if (kp(win, GLFW_KEY_L) && !pauseMenu) { showLoadout = !showLoadout; glfwSetInputMode(win, GLFW_CURSOR, showLoadout ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED); }
    if (kp(win, GLFW_KEY_V)) scopeIronsMode = !scopeIronsMode;
  }
  if (pauseMenu) return;
  ExploreInput in;
  static double lmx = -1, lmy = -1;
  if (useScripted) {
    in = scriptedInput; scriptedInput.interact = false; scriptedInput.jump = false; scriptedInput.reload = false; scriptedInput.switchPos = false; scriptedInput.toggleAmmo = false; scriptedInput.build = false;
    scriptedInput.fireMode = scriptedInput.prone = scriptedInput.packUp = scriptedInput.rackUp = scriptedInput.rackDown = scriptedInput.rackEquip = scriptedInput.rackClose = false;
    scriptedInput.choose1 = scriptedInput.choose2 = false; scriptedInput.nextWeapon = false;
    if (!occupied) { eyeYaw = wrapDeg(eyeYaw + in.dyaw); eyePitch = std::clamp(eyePitch + in.dpitch, -85.0f, 85.0f); }
  } else {
    double mx, my; glfwGetCursorPos(win, &mx, &my);
    if (lmx >= 0 && !io.WantCaptureMouse && !showLoadout && !rackOpen) {
      StationRT* gs = station(occupied);
      float hs = gs && rt[occupied].asset->station.kind == "m2hb" ? 0.08f : 0.012f;
      if (gs && gs->pos2 == ST_GUNNER) { in.dyaw = -(float)(mx - lmx) * hs; in.dpitch = -(float)(my - lmy) * hs; }   // handwheels / spade grips
      else { in.lookYaw = -(float)(mx - lmx) * 0.1f; in.lookPitch = -(float)(my - lmy) * 0.1f; eyeYaw = wrapDeg(eyeYaw + in.lookYaw); eyePitch = std::clamp(eyePitch + in.lookPitch, -85.0f, 85.0f); }
    }
    lmx = mx; lmy = my;
    if (kd(win, GLFW_KEY_W)) in.move.y += 1;
    if (kd(win, GLFW_KEY_S)) in.move.y -= 1;
    if (kd(win, GLFW_KEY_D)) in.move.x += 1;
    if (kd(win, GLFW_KEY_A)) in.move.x -= 1;
    in.sprint = kd(win, GLFW_KEY_LEFT_SHIFT);
    in.crouch = kd(win, GLFW_KEY_LEFT_CONTROL) || kd(win, GLFW_KEY_C);
    in.jump = kp(win, GLFW_KEY_SPACE);
    in.interact = kp(win, GLFW_KEY_E);
    in.fire = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS && !io.WantCaptureMouse;
    in.aim = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS && !io.WantCaptureMouse;
    in.reload = kp(win, GLFW_KEY_R);
    in.switchPos = kp(win, GLFW_KEY_T);
    in.toggleAmmo = kp(win, GLFW_KEY_X);
    in.build = kp(win, GLFW_KEY_B);
    in.fireMode = kp(win, binds.fireMode);
    in.prone = kp(win, binds.prone);
    in.packUp = kp(win, binds.packUp);
    if (rackOpen) {   // the panel owns the keyboard: no movement, fire or look leaks through
      in.rackUp = kp(win, GLFW_KEY_UP) || kp(win, GLFW_KEY_W); in.rackDown = kp(win, GLFW_KEY_DOWN) || kp(win, GLFW_KEY_S); in.rackEquip = kp(win, GLFW_KEY_ENTER);
      in.move = vec2(0); in.fire = in.aim = in.reload = in.jump = in.sprint = false; in.interact = in.nextWeapon = in.fireMode = in.prone = in.build = false;
      in.dyaw = in.dpitch = in.lookYaw = in.lookPitch = 0;
    }
    { extern double gScrollY; in.wheel = io.WantCaptureMouse ? 0.0f : (float)gScrollY; gScrollY = 0; }
    in.choose1 = kp(win, GLFW_KEY_1); in.choose2 = kp(win, GLFW_KEY_2);
    in.nextWeapon = kp(win, GLFW_KEY_Q);
    if (kp(win, GLFW_KEY_BACKSPACE)) returnToStart();
  }
  if (rackOpen) {   // the panel owns input (keyboard and scripted alike): nothing leaks into movement or weapons
    in.move = vec2(0); in.fire = in.aim = in.reload = in.jump = in.sprint = false; in.interact = in.nextWeapon = in.fireMode = in.prone = in.build = in.packUp = false;
    in.dyaw = in.dpitch = in.lookYaw = in.lookPitch = 0;
  }
  physAccum += dt;
  int steps = 0;
  while (physAccum >= 1.0f / 60.0f && steps < 8) {
    exploreStep(1.0f / 60.0f, in);
    in.interact = false; in.jump = false; in.reload = false; in.switchPos = false; in.toggleAmmo = false; in.build = false; in.fireMode = in.prone = in.packUp = in.rackUp = in.rackDown = in.rackEquip = in.rackClose = false; in.choose1 = in.choose2 = false; in.nextWeapon = false; in.dyaw = in.dpitch = 0;
    physAccum -= 1.0f / 60.0f; ++steps;
  }
  if (steps == 8) physAccum = 0;   // beyond 8 fixed steps per frame, drop time rather than spiral
  Physics::step(dt);
  stepDynLeaves(dt);
}

void App::drawExploreUI() {
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  if (!pauseMenu) {
    dl->AddCircleFilled(c, 2.5f, IM_COL32(240, 240, 230, 200));
    if (target.valid) {
      auto it = rt.find(target.inst);
      if (it != rt.end()) {
        if (target.kind != 0) {
          std::string t = target.kind == 1 ? "E  -  Crew " + it->second.asset->name : target.kind == 3 ? "E  -  Open " + it->second.asset->name : "E  -  Take ammunition (" + it->second.asset->name + ")";
          ImVec2 ts = ImGui::CalcTextSize(t.c_str());
          ImVec2 p(c.x - ts.x * 0.5f, c.y + 28);
          dl->AddRectFilled(ImVec2(p.x - 10, p.y - 5), ImVec2(p.x + ts.x + 10, p.y + ts.y + 5), IM_COL32(15, 15, 12, 170), 4);
          dl->AddText(p, IM_COL32(245, 238, 210, 255), t.c_str());
        } else {
        auto& d = it->second.asset->interactables[target.idx];
        bool opening = it->second.dir[target.idx] != 0 ? it->second.dir[target.idx] < 0 : it->second.progress[target.idx] < 0.5f;
        std::string t = std::string("E  —  ") + (opening ? "Open " : "Close ") + d.label;
        ImVec2 ts = ImGui::CalcTextSize(t.c_str());
        ImVec2 p(c.x - ts.x * 0.5f, c.y + 28);
        dl->AddRectFilled(ImVec2(p.x - 10, p.y - 5), ImVec2(p.x + ts.x + 10, p.y + ts.y + 5), IM_COL32(15, 15, 12, 170), 4);
        dl->AddText(p, IM_COL32(245, 238, 210, 255), t.c_str());
        }
      }
    }
    dl->AddText(ImVec2(14, 12), IM_COL32(230, 230, 220, 150), "WASD walk | Shift sprint | Ctrl/C crouch | Space jump | E interact | LMB fire | RMB sights | R reload | Q weapon | T switch gun position | Backspace start | F4 damage debug | Esc menu");
    drawCombatUI();
    drawWeaponUI();
    if (now < noticeUntil) dl->AddText(ImVec2(14, 32), IM_COL32(255, 210, 110, 230), notice.c_str());
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
  if (pauseMenu) {
    ImGui::SetNextWindowPos(c, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Paused", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
    if (ImGui::Button("Resume", ImVec2(240, 0))) { pauseMenu = false; glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED); }
    if (ImGui::Button("Return to start marker", ImVec2(240, 0))) { returnToStart(); pauseMenu = false; glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED); }
    if (ImGui::Button("Return to Creative", ImVec2(240, 0))) exitExplore();
    ImGui::Separator();
    if (ImGui::Button("Apply session changes to map + return", ImVec2(240, 0))) { applyDamageToMap(); exitExplore(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keeps the current wall damage and craters in the map as one undoable edit.\nWeapons, ammunition, moved guns and doors are not kept.");
    ImGui::End();
  }
}
