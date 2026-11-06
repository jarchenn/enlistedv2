// Arsenal: rebindable key bindings, the carried loadout, fire-mode selector, launchers, the carried M2
// (deploy / pack up), faction weapon racks (display models, interaction panel) and the AT-gun wreck transition.
//
// Loadout policy (single player; there is no network layer in this engine):
//  - Explore starts with the sandbox loadout: every small arm in the registry (launchers and heavy weapons
//    excluded). Q cycles the carried list.
//  - A rack equips any weapon of its faction. A weapon already carried is simply selected; otherwise it
//    replaces the active slot. Every weapon keeps its own ammunition state for the whole session, so
//    swapping never refills or duplicates rounds; only ammunition crates restock (existing rule).
//  - Returning to Creative (F5) resets the loadout, ammunition and fire modes (the existing session reset).
#include "app.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>
#include <fstream>

// ---------------------------------------------------------------- key bindings
static int keyFromName(const std::string& n) {
  if (n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z') return GLFW_KEY_A + (n[0] - 'A');
  if (n.size() == 1 && n[0] >= '0' && n[0] <= '9') return GLFW_KEY_0 + (n[0] - '0');
  static const std::map<std::string, int> m = {{"LEFT_CONTROL", GLFW_KEY_LEFT_CONTROL}, {"RIGHT_CONTROL", GLFW_KEY_RIGHT_CONTROL}, {"LEFT_SHIFT", GLFW_KEY_LEFT_SHIFT},
    {"LEFT_ALT", GLFW_KEY_LEFT_ALT}, {"TAB", GLFW_KEY_TAB}, {"CAPS_LOCK", GLFW_KEY_CAPS_LOCK}, {"SPACE", GLFW_KEY_SPACE}, {"F6", GLFW_KEY_F6}, {"F7", GLFW_KEY_F7}, {"F8", GLFW_KEY_F8}};
  auto it = m.find(n); return it == m.end() ? GLFW_KEY_UNKNOWN : it->second;
}

void App::loadBindings() {
  // defaults (conflict-checked against every existing Explore key), overridable in content/settings/bindings.json
  binds = Bindings{};
  json j; try { j = json::parse(readTextFile(contentDir() / "settings" / "bindings.json")); } catch (...) {}
  auto get = [&](const char* action, const char* def) {
    std::string n = j.is_object() ? j.value(action, std::string(def)) : std::string(def);
    int k = keyFromName(n); if (k == GLFW_KEY_UNKNOWN) { logf("bindings: unknown key '%s' for %s, using %s", n.c_str(), action, def); k = keyFromName(def); n = def; }
    binds.names[action] = n; return k;
  };
  binds.fireMode = get("fireMode", "F");
  binds.prone = get("prone", "Z");
  binds.crouch = get("crouch", "LEFT_CONTROL");
  binds.packUp = get("packUp", "G");
  binds.grenade = get("grenade", "H");
  binds.smokeGrenade = get("smokeGrenade", "J");   // match: quick smoke (mpai)
}

void App::loadMoveSettings() {
  move = MoveSettings{};
  json j; try { j = json::parse(readTextFile(contentDir() / "settings" / "movement.json")); } catch (...) { return; }
  move.run = j.value("run_mps", move.run); move.sprint = j.value("sprint_mps", move.sprint); move.crouch = j.value("crouch_mps", move.crouch); move.prone = j.value("prone_mps", move.prone);
  move.gravity = j.value("gravity_mps2", move.gravity); move.jumpVelocity = j.value("jumpVelocity_mps", move.jumpVelocity);
  move.jumpBuffer = j.value("jumpBuffer_s", move.jumpBuffer); move.coyote = j.value("coyote_s", move.coyote);
  move.eyeStand = j.value("eyeStand_m", move.eyeStand); move.eyeCrouch = j.value("eyeCrouch_m", move.eyeCrouch); move.eyeProne = j.value("eyeProne_m", move.eyeProne);
  move.eyeRate = j.value("eyeSmoothing_per_s", move.eyeRate);
  move.proneToStand = j.value("proneToStand_s", move.proneToStand); move.proneToCrouch = j.value("proneToCrouch_s", move.proneToCrouch);
  move.crouchToStand = j.value("crouchToStand_s", move.crouchToStand);
  move.adsTime = j.value("adsTime_s", move.adsTime);
}

// ---------------------------------------------------------------- loadout
bool App::isSmallArm(const WeaponDef& d) const { return d.action != "launcher" && d.action != "heavy"; }

void App::resetLoadout() {
  carried.clear();
  for (size_t k = 0; k < weaponDefs.size(); ++k) if (isSmallArm(*weaponDefs[k])) carried.push_back((int)k);
  if (!carried.empty() && std::find(carried.begin(), carried.end(), activeWeapon) == carried.end()) activeWeapon = carried[0];
  closeRack("reset");
}

bool App::equipWeapon(int idx, std::string& msg) {
  if (idx < 0 || idx >= (int)weaponDefs.size()) { msg = "No such weapon."; return false; }
  const WeaponDef& d = *weaponDefs[idx];
  if (strictDDay && d.laterWar) { msg = d.name + " is hidden by the strict 6 June 1944 preset."; return false; }
  if (occupied) { msg = "Leave the gun first."; return false; }
  auto it = std::find(carried.begin(), carried.end(), idx);
  if (it == carried.end()) {
    auto cur = std::find(carried.begin(), carried.end(), activeWeapon);
    if (cur == carried.end()) carried.push_back(idx); else *cur = idx;   // replaces the active slot
  }
  selectWeapon(idx);
  WeaponState& s = wstate[d.id];
  s.triggerReleased = false;   // a held trigger never fires the newly equipped weapon
  msg = "Equipped " + d.name + " (" + std::to_string(s.loaded) + " + " + std::to_string(s.reserveRounds()) + ")";
  ++equips;
  return true;
}

// ---------------------------------------------------------------- fire modes
const char* App::fireModeName(const WeaponDef& d, const WeaponState& s) const {
  if (d.action == "launcher") return "SINGLE";
  if (d.modes.empty()) return d.action == "auto" ? "AUTO" : d.action == "heavy" ? "CARRY" : "SEMI";
  return d.modes[s.mode % d.modes.size()] == "semi" ? "SEMI" : "AUTO";
}

void App::toggleFireMode(const WeaponDef& d, WeaponState& s) {
  if (d.modes.size() < 2) { weaponMsg = d.name + " has no fire selector"; return; }
  s.mode = (s.mode + 1) % (int)d.modes.size();
  s.triggerReleased = false;   // release required: switching never produces a shot or keeps an auto timer
  s.burst = 0;
  weaponMsg = d.name + ": " + fireModeName(d, s);
  audio.play("click", eyePos(), 0.3f);
}

// ---------------------------------------------------------------- carried M2: deploy / pack up
bool App::deployHeavy(const WeaponDef& d, WeaponState& s) {
  const AssetDef* a = lib.find(d.deploys);
  if (!a) { weaponMsg = "Missing asset " + d.deploys; return false; }
  vec3 f = forward(eyeYaw, 0);
  vec3 p = eyePos() + f * 1.6f; p.y = terrain.height(p.x, p.z);
  float yaw = wrapDeg(eyeYaw + 180.0f);   // the gun faces where the player looks (asset +z forward, eyeYaw 0 = -z)
  PlaceCheck c = checkPlacement(a, p, yaw, 0);
  if (!c.ok) { weaponMsg = "Cannot deploy here: " + c.reason; return false; }
  if (groundSlopeDeg(p) > 25.0f) { weaponMsg = "Cannot deploy here: too steep for the tripod"; return false; }
  vec3 half = (a->bmax - a->bmin) * 0.45f, ctr = p + yawQuat(yaw) * ((a->bmin + a->bmax) * 0.5f);
  if (Physics::boxBlocked(ctr + vec3(0, 0.1f, 0), half, yawQuat(yaw), ~0ull, true)) { weaponMsg = "Cannot deploy here: blocked"; return false; }
  Instance i; i.id = doc.newId(); i.assetId = a->id; i.assetVersion = a->version; i.pos = vec3(p.x, c.y, p.z); i.yaw = yaw;
  i.extra = json::object(); i.extra["session"] = true; i.extra["deployedBy"] = "player";
  // carried belt state moves onto the gun (rounds are moved, never created)
  i.extra["station"] = {{"loaded", s.loaded}, {"reserve", s.reserveRounds()}};
  bump(i); dirty = false;
  doc.instances.push_back(i);
  syncRuntime();
  resetStation(doc.instances.back());
  auto cur = std::find(carried.begin(), carried.end(), activeWeapon);
  if (cur != carried.end()) carried.erase(cur);
  if (!carried.empty()) selectWeapon(carried[0]);
  occupyStation(i.id, ST_GUNNER);
  weaponMsg = "M2HB deployed (" + binds.names["packUp"] + " beside it packs it up)";
  ++deploys;
  return true;
}

bool App::packUpHeavy() {
  if (occupied) return false;
  int m2 = weaponIndex("m2hb_carry");
  if (m2 < 0) return false;
  vec3 eye = eyePos();
  for (size_t k = 0; k < doc.instances.size(); ++k) {
    Instance& i = doc.instances[k];
    if (!i.extra.is_object() || i.extra.value("deployedBy", "") != "player") continue;
    if (glm::length(vec2(i.pos.x - eye.x, i.pos.z - eye.z)) > 3.0f) continue;
    StationRT* st = station(i.id);
    if (!st || st->wrecked) continue;
    WeaponState& s = wstate["m2hb_carry"];
    s.loaded = st->loaded; s.containers.clear();
    for (int r = st->reserve; r > 0; r -= 110) s.containers.push_back(std::min(r, 110));
    doc.instances.erase(doc.instances.begin() + k);
    syncRuntime();
    std::string msg; equipWeapon(m2, msg);
    weaponMsg = "M2HB packed up (" + std::to_string(s.loaded) + " + " + std::to_string(s.reserveRounds()) + " rounds)";
    ++packs;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------- AT-gun wreck (one idempotent transition)
void App::wreckStation(uint64_t id, const std::string& why) {
  StationRT* s = station(id);
  Instance* i = doc.find(id);
  if (!s || !i || s->wrecked) return;
  s->wrecked = true;
  if (occupied == id) leaveStation();            // release the operator safely
  if (choiceFor == id) choiceFor = 0;            // clear interaction state
  s->reload = 0; s->trans = 0; s->armed = false; s->pos2 = ST_NONE; s->loaded = 0;   // cancel pending shots
  s->status = "Destroyed (" + why + ")";
  vec3 c = vec3(stationMatrix(*i, rt[id]) * vec4((rt[id].asset->bmin + rt[id].asset->bmax) * 0.5f, 1));
  fx.explosion(c, vec3(0, 1, 0), "concrete", false);       // effects exactly once
  for (int n = 0; n < 10; ++n) fx.smoke(c + vec3(fx.rnd() - 0.5f, 0.3f, fx.rnd() - 0.5f), vec3(0, 1.2f, 0), 0.5f, 6.0f, vec4(0.12f, 0.11f, 0.1f, 0.55f), 1.5f, 0.8f);
  audio.play("explosion", c, 1.0f);
  ++stationsWrecked;
  lastWreck = {id, why, (float)now};
}

bool App::isAtGun(const AssetDef* a) const { return a && (a->station.kind == "pak40" || a->station.kind == "flak18"); }   // field guns: the one-shot direct-hit rule

// ---------------------------------------------------------------- racks
std::vector<int> App::rackArsenal(const std::string& faction) const {
  std::vector<int> v;
  for (size_t k = 0; k < weaponDefs.size(); ++k) if (weaponDefs[k]->side == faction) v.push_back((int)k);
  return v;
}

void App::rackDrawItems(const Instance& i, const InstanceRT& r) {
  mat4 M = instanceMatrix(i);
  for (auto& dsp : r.asset->rackDisplay) {
    int wi = weaponIndex(dsp.weapon);
    if (wi < 0) continue;
    const WeaponDef& d = *weaponDefs[wi];
    mat4 W = M * dsp.m;
    if (d.useModel()) {
      for (int p = 0; p < 8; ++p) for (int k : d.modelParts[p]) R.worldModels.push_back({d.model.get(), k, W});
    } else {
      for (int p = 0; p < 8; ++p) if (d.mesh[p].count) worldBoxDraws.push_back({&d.mesh[p], W});
    }
  }
}

// rack display transform: butt in the tray, leaning back onto the upper slotted rail; computed at asset load
void App::layoutRackDisplays() {
  for (auto& ap : lib.assets) {
    AssetDef& a = *ap;
    if (!a.rack) continue;
    for (auto& dsp : a.rackDisplay) {
      int wi = weaponIndex(dsp.weapon);
      if (wi < 0) continue;
      const WeaponDef& d = *weaponDefs[wi];
      vec3 bmin = d.useModel() ? d.model->bmin : vec3(-0.05f, -0.1f, 0), bmax = d.useModel() ? d.model->bmax : vec3(0.05f, 0.1f, 1.0f);
      mat4 R(1);
      if (dsp.pose == "lean") {
        float lean = radians(dsp.lean);
        vec3 Z(0, std::cos(lean), -std::sin(lean)), X(1, 0, 0), Y = glm::cross(Z, X);
        R = mat4(vec4(X, 0), vec4(Y, 0), vec4(Z, 0), vec4(0, 0, 0, 1));
      } else if (dsp.pose == "flat") {   // launcher on the side supports
        vec3 Z(0, 0, dsp.dir), Y(0, 1, 0), X = glm::cross(Y, Z);   // lying on the side hooks, along the rack depth
        R = mat4(vec4(X, 0), vec4(Y, 0), vec4(Z, 0), vec4(0, 0, 0, 1));
      } else {   // "stand": on its own tripod / bipod on the ground, facing +z
        R = glm::mat4_cast(glm::angleAxis(radians(dsp.yaw), vec3(0, 1, 0)));
      }
      // drop it onto its support height: the lowest bbox corner of the posed weapon sits at dsp.p.y
      float lowest = 1e9f, nearZ = -1e9f;
      for (int c = 0; c < 8; ++c) {
        vec3 q(c & 1 ? bmax.x : bmin.x, c & 2 ? bmax.y : bmin.y, c & 4 ? bmax.z : bmin.z);
        vec3 w = vec3(R * vec4(q, 1)); lowest = std::min(lowest, w.y); nearZ = std::max(nearZ, w.z);
      }
      vec3 t = dsp.p - vec3(0, lowest, 0);
      if (dsp.pose == "lean") t.z = dsp.p.z - nearZ;   // stock heel against the tray's front lip
      dsp.m = glm::translate(mat4(1), t) * R;
    }
  }
}

void App::openRack(uint64_t id) {
  Instance* i = doc.find(id);
  if (!i) return;
  rackOpen = true; rackId = id; rackSel = 0;
  const AssetDef* a = assetFor(*i);
  auto ars = rackArsenal(a->rackFaction);
  for (size_t k = 0; k < ars.size(); ++k) if (ars[k] == activeWeapon) rackSel = (int)k;
  if (!selftest) glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  ++rackOpens;
}

void App::closeRack(const char* why) {
  if (!rackOpen) return;
  rackOpen = false; rackId = 0;
  lastRackClose = why;
  scriptedInput.fire = false;
  if (!selftest && mode == Mode::Explore && !pauseMenu && !showLoadout) glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void App::stepRack(const ExploreInput& in) {
  if (!rackOpen) return;
  Instance* i = doc.find(rackId);
  if (!i || mode != Mode::Explore) { closeRack("rack removed"); return; }
  if (glm::length(i->pos - Physics::character().pos) > 3.5f) { closeRack("left range"); return; }
  const AssetDef* a = assetFor(*i);
  auto ars = rackArsenal(a->rackFaction);
  int n = (int)ars.size() + (int)a->rackDeploy.size();
  if (in.rackUp) rackSel = (rackSel + n - 1) % std::max(1, n);
  if (in.rackDown) rackSel = (rackSel + 1) % std::max(1, n);
  if (in.rackEquip) rackActivate();
  if (in.rackClose) closeRack("closed");
}

void App::rackActivate() {
  Instance* i = doc.find(rackId); if (!i) return;
  const AssetDef* a = assetFor(*i);
  auto ars = rackArsenal(a->rackFaction);
  if (rackSel < (int)ars.size()) {
    std::string msg; bool ok = equipWeapon(ars[rackSel], msg);
    toast(msg, 3);
    if (ok) closeRack("equipped");
  } else {
    int k = rackSel - (int)ars.size();
    if (k < (int)a->rackDeploy.size()) {
      closeRack("deploy");
      build.active = true; build.forceAsset = a->rackDeploy[k];
      toast("Deploy " + a->rackDeploy[k] + ": aim at the ground, LMB to place, RMB to cancel", 4);
    }
  }
}

void App::drawRackUI() {
  if (!rackOpen) return;
  Instance* i = doc.find(rackId); if (!i) return;
  const AssetDef* a = assetFor(*i);
  auto ars = rackArsenal(a->rackFaction);
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(560, 0));
  std::string title = a->name + "##rack";
  bool open = true;
  ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
  ImGui::TextDisabled("%s arsenal from the weapon registry. Up/Down or click to select, Enter / Equip, Esc closes.", a->rackFaction == "US" ? "Allied" : "Axis");
  ImGui::Separator();
  for (size_t k = 0; k < ars.size(); ++k) {
    const WeaponDef& d = *weaponDefs[ars[k]];
    const WeaponState& s = wstate[d.id];
    bool carriedNow = std::find(carried.begin(), carried.end(), ars[k]) != carried.end();
    char b[256];
    std::snprintf(b, sizeof b, "%-26s %-16s %3d + %-4d %s%s%s", d.name.c_str(), d.caliber.c_str(), s.loaded, s.reserveRounds(),
                  ars[k] == activeWeapon ? "[in hand]" : carriedNow ? "[carried]" : "", d.action == "heavy" ? " heavy: carry & deploy" : d.action == "launcher" ? " launcher" : "",
                  strictDDay && d.laterWar ? " (hidden by preset)" : "");
    if (ImGui::Selectable(b, rackSel == (int)k)) rackSel = (int)k;
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { rackSel = (int)k; rackActivate(); }
  }
  if (!a->rackDeploy.empty()) {
    ImGui::Separator(); ImGui::TextDisabled("Deployable equipment (placed through build mode)");
    for (size_t k = 0; k < a->rackDeploy.size(); ++k) {
      const AssetDef* da = lib.find(a->rackDeploy[k]);
      std::string lbl = (da ? da->name : a->rackDeploy[k]) + "  - deploy";
      if (ImGui::Selectable(lbl.c_str(), rackSel == (int)(ars.size() + k))) rackSel = (int)(ars.size() + k);
    }
  }
  ImGui::Separator();
  if (rackSel < (int)ars.size()) {
    const WeaponDef& d = *weaponDefs[ars[rackSel]];
    const WeaponState& s = wstate[d.id];
    ImGui::Text("%s - %s, %s feed, fire mode %s", d.name.c_str(), d.action.c_str(), d.feed.c_str(), fireModeName(d, s));
    ImGui::TextWrapped("Ammunition: %d loaded, %d in reserve (%s x %d). Swapping keeps each weapon's own ammunition; crates restock.", s.loaded, s.reserveRounds(), d.container.c_str(), d.containerSize);
  }
  if (ImGui::Button("Equip / deploy (Enter)", ImVec2(200, 0))) rackActivate();
  ImGui::SameLine(); if (ImGui::Button("Close (Esc)", ImVec2(120, 0))) closeRack("closed");
  ImGui::End();
  if (!open) closeRack("closed");
}
