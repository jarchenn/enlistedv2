// GM1 deploy screen: before every spawn of the local player (and the first) pick a class, its weapon options and a
// spawn point (the markers for the player's role / team plus GM3 outposts) on a top-down map. Auto-deploys after 5 s.
// LAN client: the choice goes to the host (Net::clientDeploy), which checks it against the allowed classes at the spawn.
#include "game.h"
#include "app.h"
#include "classes.h"
#include "net.h"
#include "minimap.h"
#include "engineer.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

std::vector<Game::SpawnOption> Game::spawnOptions(int slot) const {
  std::vector<SpawnOption> out;
  if (slot < 0 || slot >= (int)sold.size()) return out;
  const Soldier& s = sold[slot];
  if (match.mode() == GameMode::Assault) { for (auto* m : match.sc.spawnsFor(s.role, match.active)) out.push_back({m->pos, m->yaw, m->label.empty() ? m->id : m->label}); }
  else for (auto& m : match.sc.spawns) if (m.team == s.team) out.push_back({m.pos, m.yaw, m.label.empty() ? m.id : m.label});
  for (auto& o : outposts) if (o.team == s.team) out.push_back(o);   // GM3: team-built outposts
  for (auto& o : engOutposts(app, s.team)) out.push_back({o.pos + yawQuat(o.yaw) * vec3(0, 0, -2.2f), o.yaw, "Outpost", o.team});   // GM3: Engineer outposts standing on the map (spawn behind the sandbags)
  return out;
}

void Game::deployNow() {
  const auto& C = classDefs();
  if (app.deployClass < 0 || app.deployClass >= (int)C.size() || !match.rules.classAllowed(C[app.deployClass].id))
    for (int k = 0; k < (int)C.size(); ++k) if (match.rules.classAllowed(C[k].id)) { app.deployClass = k; break; }
  deployReady = true;
  if (isClient && gNet && app.deployClass >= 0) gNet->clientDeploy(C[app.deployClass].id, app.deployPick[0], app.deployPick[1]);
}

void Game::drawDeploy() {
  Soldier* L = local();
  if (!L || L->alive || match.phase != MatchPhase::Running || classDefs().empty()) return;
  if (deployReady) return;
  if (!isClient && t - deployOpenedAt >= kDeployAuto) { deploySpawn = -1; deployNow(); return; }   // nothing chosen: a random marker
  const auto& C = classDefs();
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(760, 470), ImGuiCond_Always);
  ImGui::Begin("Deploy", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
  std::string fac = factionName(L->faction);
  if (app.deployClass < 0 || app.deployClass >= (int)C.size() || !match.rules.classAllowed(C[app.deployClass].id)) {
    app.deployClass = -1; for (int k = 0; k < (int)C.size() && app.deployClass < 0; ++k) if (match.rules.classAllowed(C[k].id)) app.deployClass = k;
  }
  ImGui::BeginChild("cls", ImVec2(200, 380), true);
  for (int k = 0; k < (int)C.size(); ++k) {
    if (!match.rules.classAllowed(C[k].id)) continue;
    if (ImGui::Selectable(C[k].side(fac).name.c_str(), app.deployClass == k)) { app.deployClass = k; app.deployPick[0] = app.deployPick[1] = 0; }
  }
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("opt", ImVec2(240, 380), true);
  if (app.deployClass >= 0) {
    const ClassDef& c = C[app.deployClass]; const ClassSide& cs = c.side(fac);
    auto combo = [&](const char* label, const std::vector<std::string>& ids, int& pick) {
      if (ids.empty()) return;
      pick = std::clamp(pick, 0, (int)ids.size() - 1);
      ImGui::TextUnformatted(label);
      for (int k = 0; k < (int)ids.size(); ++k) {
        int w = app.weaponIndex(ids[k]);
        ImGui::PushID(label); ImGui::PushID(k);
        if (ImGui::RadioButton(w >= 0 ? app.weaponDefs[w]->name.c_str() : ids[k].c_str(), pick == k)) pick = k;
        ImGui::PopID(); ImGui::PopID();
      }
    };
    combo("Primary", cs.primary, app.deployPick[0]);
    combo("Second slot", cs.secondary, app.deployPick[1]);
    if (!cs.pistol.empty()) { int w = app.weaponIndex(cs.pistol); ImGui::Text("Pistol: %s", w >= 0 ? app.weaponDefs[w]->name.c_str() : cs.pistol.c_str()); }
    ImGui::Separator();
    if (c.frags) ImGui::Text("Frag grenades: %d", c.frags);
    if (c.smokes) ImGui::Text("Smoke grenades: %d", c.smokes);
    if (c.packs) ImGui::Text("Explosion packs: %d", c.packs);
    if (c.ability == "artillery") ImGui::TextUnformatted("Ability: artillery");
    if (c.builds) ImGui::TextUnformatted("Builds field defences");
    if (c.drivesTanks) ImGui::TextUnformatted("Drives tanks");
    if (c.hpMul != 1) ImGui::Text("Health x%.2f", c.hpMul);
    if (c.staminaMul != 1) ImGui::Text("Stamina x%.2f", c.staminaMul);
  }
  ImGui::EndChild();
  ImGui::SameLine();
  // top-down map of the build area: spawn points as dots (click to choose), ourselves where we fell
  ImGui::BeginChild("map", ImVec2(0, 380), true);
  std::vector<SpawnOption> opts = isClient ? std::vector<SpawnOption>{} : spawnOptions(localSlot);   // client: the host picks the marker
  ImVec2 o = ImGui::GetCursorScreenPos(), sz = ImGui::GetContentRegionAvail();
  std::vector<MinimapMarker> mk;   // GM2 minimap: spawn points (chosen one green) and where we fell
  for (int k = 0; k < (int)opts.size(); ++k) mk.push_back({vec2(opts[k].pos.x, opts[k].pos.z), deploySpawn == k ? IM_COL32(120, 220, 120, 255) : IM_COL32(90, 160, 230, 255), 0, deploySpawn == k ? 8.0f : 6.0f, 0, opts[k].label});
  mk.push_back({vec2(L->feet.x, L->feet.z), IM_COL32(200, 200, 200, 255), 3, 6, 0, ""});
  MinimapView v = drawMinimap(app, ImGui::GetWindowDrawList(), vec2(o.x, o.y), vec2(o.x + sz.x, o.y + sz.y - 40), vec2(0), 0, MM_TERRAIN | MM_STRUCTURES | MM_OBJECTIVES | MM_TEAMMATES, mk);
  if (ImGui::IsMouseClicked(0) && v.contains(vec2(io.MousePos.x, io.MousePos.y))) {   // the nearest spawn point to the click (within 18 px)
    float bd = 18;
    for (int k = 0; k < (int)opts.size(); ++k) { vec2 q = v.toScreen(vec2(opts[k].pos.x, opts[k].pos.z)); float d = glm::length(q - vec2(io.MousePos.x, io.MousePos.y)); if (d < bd) { bd = d; deploySpawn = k; } }
  }
  ImGui::Dummy(ImVec2(sz.x, sz.y - 40));
  ImGui::TextWrapped("%s", deploySpawn >= 0 && deploySpawn < (int)opts.size() ? opts[deploySpawn].label.c_str() : "Spawn: random marker (click a dot to choose)");
  ImGui::EndChild();
  if (!isClient) ImGui::Text("Auto-deploy in %.0f s", std::max(0.0, kDeployAuto - (t - deployOpenedAt)));
  ImGui::SameLine(ImGui::GetWindowWidth() - 150);
  if (ImGui::Button("Deploy", ImVec2(130, 0))) deployNow();
  ImGui::End();
  app.uiCursor = true;
}

// ---- GM1 hooks used outside the match code (mpai.h)
bool mpaiMayDriveTanks(App& app) {   // in a match only the class that drives tanks (Tanker) may enter one; Creative: anyone
  (void)app;
  if (!gGame || !gGame->active) return true;
  Soldier* L = gGame->local();
  return !L || L->cls < 0 || L->cls >= (int)classDefs().size() || classDefs()[L->cls].drivesTanks;
}

bool mpaiPickupWeapon(App& app) {   // E within 1.6 m of a weapon an AI dropped: take it with its rounds (replaces the one in hand)
  if (!gGame || !gGame->active || gGame->isClient) return false;
  Soldier* L = gGame->local();
  if (!L || !L->alive) return false;
  auto& D = gGame->drops;
  int best = -1; float bd = 1.6f;
  for (int k = 0; k < (int)D.size(); ++k) { float d = glm::length(D[k].pos - L->feet); if (d < bd) { bd = d; best = k; } }
  if (best < 0) return false;
  Game::Drop dr = D[best];
  std::string msg;
  if (!app.equipWeapon(dr.weapon, msg)) { app.toast(msg, 2); return true; }
  app.wstate[app.weaponDefs[dr.weapon]->id] = dr.ws;   // the dead soldier's magazine and pouches
  D.erase(D.begin() + best);
  app.toast("Picked up " + app.weaponDefs[dr.weapon]->name, 2);
  return true;
}
