// Multiplayer + AI user interface: Play menu (Singleplayer / Host LAN / Join LAN + match setup), the Creative
// Scenario panel (ordered objectives, spawn markers, approach routes, validation) with its world overlay, and the
// in-match HUD (objective sequence bar, counts, grenades, death screen, results).
#include "mpui.h"
#include "game.h"
#include "app.h"
#include "net.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <algorithm>

MpUi gUi;

static ImU32 factionCol(Faction f, int a = 255) { return f == Faction::Allies ? IM_COL32(150, 175, 95, a) : IM_COL32(150, 160, 175, a); }
static const char* factionTag(Faction f) { return f == Faction::Allies ? "[US]" : "[GER]"; }

Scenario& MpUi::scenario(App& app) {
  json cur = app.doc.extra.is_object() && app.doc.extra.contains("scenario") ? app.doc.extra["scenario"] : json();
  if (!loaded || cur != lastSaved) { std::vector<std::string> notes; sc = Scenario::fromJson(cur, &notes); lastSaved = cur; loaded = true; migration = notes; }
  return sc;
}
void MpUi::commit(App& app) {
  sc.renumber();
  if (!app.doc.extra.is_object()) app.doc.extra = json::object();
  app.doc.extra["scenario"] = sc.toJson();
  lastSaved = app.doc.extra["scenario"];
  app.dirty = true;
}

// ground (or floor) point under the screen centre in Creative
static bool viewCentrePoint(App& app, vec3& out) {
  vec3 ro = app.camPos, rd = App::forward(app.camYaw, app.camPitch);
  Physics::Hit h;
  if (Physics::castRayEx(ro, rd, 2000, h)) { out = h.pos; return true; }
  return app.terrain.raycast(ro, rd, 2000, out);
}

static bool project(App& app, vec3 p, ImVec2& s) {
  vec4 c = app.projM * app.viewM * vec4(p, 1);
  if (c.w <= 0.05f) return false;
  ImGuiIO& io = ImGui::GetIO();
  s = ImVec2((c.x / c.w * 0.5f + 0.5f) * io.DisplaySize.x, (1 - (c.y / c.w * 0.5f + 0.5f)) * io.DisplaySize.y);
  return true;
}

void MpUi::creativeMenus(App& app) {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("Play")) {
      if (ImGui::MenuItem("Singleplayer...")) { showSetup = true; setupMode = 0; }
      if (ImGui::MenuItem("Host LAN...")) { showSetup = true; setupMode = 1; }
      if (ImGui::MenuItem("Join LAN...")) { showJoin = true; }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Scenario")) {
      ImGui::MenuItem("Objectives and spawns panel", nullptr, &showPanel);
      ImGui::MenuItem("Show scenario overlay", nullptr, &showOverlay);
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }
}

void MpUi::scenarioPanel(App& app) {
  if (!showPanel) return;
  Scenario& s = scenario(app);
  ImGui::SetNextWindowSize(ImVec2(430, 620), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 450, 40), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Scenario: objectives, spawns, routes", &showPanel)) { ImGui::End(); return; }
  // crosshair: placement targets the ground under the view centre
  ImDrawList* fg = ImGui::GetForegroundDrawList(); ImVec2 c(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
  fg->AddLine(ImVec2(c.x - 10, c.y), ImVec2(c.x + 10, c.y), IM_COL32(255, 230, 120, 220), 1.5f); fg->AddLine(ImVec2(c.x, c.y - 10), ImVec2(c.x, c.y + 10), IM_COL32(255, 230, 120, 220), 1.5f);
  vec3 vc; bool vcOk = viewCentrePoint(app, vc);
  ImGui::TextWrapped("Placement uses the point under the screen centre (crosshair). Stable ids never change when objectives are reordered.");
  if (!migration.empty()) for (auto& m : migration) ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.5f, 1), "%s", m.c_str());
  bool changed = false;
  if (ImGui::Checkbox("Developer test map (allows one objective)", &s.devTestMap)) changed = true;
  ImGui::SeparatorText("Objectives (attack order)");
  for (size_t k = 0; k < s.objectives.size(); ++k) {
    Objective& o = s.objectives[k];
    ImGui::PushID(o.id.c_str());
    bool sel = selObj == o.id;
    char hdr[160]; std::snprintf(hdr, sizeof hdr, "%zu. %s  (%s)", k + 1, o.label.empty() ? "unnamed" : o.label.c_str(), o.id.c_str());
    if (ImGui::Selectable(hdr, sel)) selObj = o.id;
    if (sel) {
      ImGui::Indent();
      char lb[64]; std::snprintf(lb, sizeof lb, "%s", o.label.c_str());
      if (ImGui::InputText("Label", lb, sizeof lb)) { o.label = lb; changed = true; }
      if (ImGui::Button("Move earlier") && s.moveEarlier(o.id)) { changed = true; ImGui::PopID(); ImGui::Unindent(); break; }
      ImGui::SameLine(); if (ImGui::Button("Move later") && s.moveLater(o.id)) { changed = true; ImGui::PopID(); ImGui::Unindent(); break; }
      ImGui::SameLine(); if (ImGui::Button("Move here") && vcOk) { o.pos = vc; changed = true; }
      if (ImGui::SliderFloat("Capture radius (m)", &o.radius, 2, 40, "%.1f")) changed = true;
      if (ImGui::DragFloatRange2("Vertical bounds (m)", &o.yMin, &o.yMax, 0.1f, -10, 20, "below %.1f", "above %.1f")) changed = true;
      if (ImGui::SliderFloat("Attack direction (deg)", &o.attackYaw, 0, 360, "%.0f")) changed = true;
      if (ImGui::Button("Aim attack arrow along view")) { o.attackYaw = wrapDeg(app.camYaw); changed = true; }
      ImGui::Text("Approach routes (left / right are relative to the attack arrow):");
      const char* ln[3] = {"Left", "Main", "Right"};
      for (int L = 0; L < 3; ++L) {
        ImGui::PushID(L);
        int npts = 0; for (auto& r : o.routes) if (r.lane == L) npts += (int)r.points.size();
        ImGui::Text("  %s: %d points", ln[L], npts); ImGui::SameLine();
        if (ImGui::SmallButton("add point") && vcOk) { bool f = false; for (auto& r : o.routes) if (r.lane == L) { r.points.push_back(vc); f = true; } if (!f) o.routes.push_back({L, {vc}}); changed = true; }
        ImGui::SameLine(); if (ImGui::SmallButton("clear")) { o.routes.erase(std::remove_if(o.routes.begin(), o.routes.end(), [&](const ApproachRoute& r) { return r.lane == L; }), o.routes.end()); changed = true; }
        ImGui::PopID();
      }
      ImGui::Text("Defender positions: %zu", o.defendPoints.size()); ImGui::SameLine();
      if (ImGui::SmallButton("add here") && vcOk) { o.defendPoints.push_back(vc); changed = true; }
      ImGui::SameLine(); if (ImGui::SmallButton("clear##dp")) { o.defendPoints.clear(); changed = true; }
      ImGui::Text("Spawns for this stage:"); ImGui::SameLine();
      if (ImGui::SmallButton("+ attacker here") && vcOk) { SpawnMarker m; m.id = s.newId("spawn"); m.role = CombatRole::Attacker; m.stage = o.order; m.pos = vc; m.yaw = app.camYaw; m.label = "Attackers " + std::to_string(o.order + 1); s.spawns.push_back(m); changed = true; }
      ImGui::SameLine(); if (ImGui::SmallButton("+ defender here") && vcOk) { SpawnMarker m; m.id = s.newId("spawn"); m.role = CombatRole::Defender; m.stage = o.order; m.pos = vc; m.yaw = app.camYaw; m.label = "Defenders " + std::to_string(o.order + 1); s.spawns.push_back(m); changed = true; }
      for (size_t m = 0; m < s.spawns.size(); ++m) {
        auto& sp = s.spawns[m];
        if (sp.stage != o.order) continue;
        ImGui::PushID(sp.id.c_str());
        ImGui::BulletText("%s %s  facing %.0f deg", sp.role == CombatRole::Attacker ? "ATK" : "DEF", sp.label.c_str(), sp.yaw);
        ImGui::SameLine(); if (ImGui::SmallButton("delete")) { s.spawns.erase(s.spawns.begin() + m); changed = true; ImGui::PopID(); break; }
        ImGui::PopID();
      }
      if (ImGui::Button("Delete objective")) {
        int ord = o.order;
        s.spawns.erase(std::remove_if(s.spawns.begin(), s.spawns.end(), [&](const SpawnMarker& m) { return m.stage == ord; }), s.spawns.end());
        for (auto& m : s.spawns) if (m.stage > ord) --m.stage;
        s.objectives.erase(s.objectives.begin() + k); selObj.clear(); changed = true; ImGui::Unindent(); ImGui::PopID(); break;
      }
      ImGui::Unindent();
    }
    ImGui::PopID();
  }
  if (ImGui::Button("Add objective at crosshair") && vcOk) {
    Objective o; o.id = s.newId("obj"); o.order = (int)s.objectives.size(); o.pos = vc; o.label = std::string("Point ") + char('A' + std::min<size_t>(25, s.objectives.size()));
    o.attackYaw = wrapDeg(app.camYaw); s.objectives.push_back(o); selObj = o.id; changed = true;
  }
  ImGui::SeparatorText("Validation");
  auto issues = validateScenario(s, true);
  if (issues.empty()) ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1), "Ready to publish: %zu objectives, %zu spawn markers.", s.objectives.size(), s.spawns.size());
  for (auto& i : issues) ImGui::TextColored(i.error ? ImVec4(1, 0.55f, 0.45f, 1) : ImVec4(0.95f, 0.85f, 0.5f, 1), "%s %s", i.error ? "ERROR" : "warn ", i.what.c_str());
  if (!worldIssues.empty()) { ImGui::Text("World checks (last match start / check):"); for (auto& w : worldIssues) ImGui::TextColored(ImVec4(1, 0.6f, 0.45f, 1), "%s", w.c_str()); }
  if (changed) commit(app);
  ImGui::End();
}

void MpUi::creativeOverlay(App& app, Overlay& ov) {
  if (!showOverlay && !showPanel) return;
  Scenario& s = scenario(app);
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  for (auto& o : s.objectives) {
    bool sel = o.id == selObj;
    vec4 col = sel ? vec4(1.0f, 0.85f, 0.3f, 1) : vec4(0.9f, 0.7f, 0.25f, 0.8f);
    // exactly the authoritative capture volume: a vertical cylinder of `radius` between pos.y + yMin and pos.y + yMax
    const int N = 48;
    for (int k = 0; k < N; ++k) {
      float a0 = k * 6.2831853f / N, a1 = (k + 1) * 6.2831853f / N;
      vec3 p0 = o.pos + vec3(std::cos(a0) * o.radius, 0, std::sin(a0) * o.radius), p1 = o.pos + vec3(std::cos(a1) * o.radius, 0, std::sin(a1) * o.radius);
      ov.line(p0 + vec3(0, o.yMin, 0), p1 + vec3(0, o.yMin, 0), col, true); ov.line(p0 + vec3(0, o.yMax, 0), p1 + vec3(0, o.yMax, 0), col, true);
      if (k % 6 == 0) ov.line(p0 + vec3(0, o.yMin, 0), p0 + vec3(0, o.yMax, 0), col * vec4(1, 1, 1, 0.6f), true);
    }
    vec3 f = attackForward(o.attackYaw), r = attackRight(o.attackYaw);
    vec3 tail = o.pos - f * (o.radius + 12) + vec3(0, 0.5f, 0), head = o.pos - f * (o.radius + 2) + vec3(0, 0.5f, 0);
    ov.line(tail, head, vec4(1, 0.3f, 0.2f, 1), true); ov.line(head, head - f * 2.0f + r * 1.2f, vec4(1, 0.3f, 0.2f, 1), true); ov.line(head, head - f * 2.0f - r * 1.2f, vec4(1, 0.3f, 0.2f, 1), true);
    const vec4 lc[3] = {vec4(0.4f, 0.7f, 1, 1), vec4(1, 1, 1, 1), vec4(1, 0.55f, 0.85f, 1)};
    for (auto& rt : o.routes) for (size_t k = 0; k + 1 < rt.points.size(); ++k) ov.line(rt.points[k] + vec3(0, 0.3f, 0), rt.points[k + 1] + vec3(0, 0.3f, 0), lc[rt.lane], true);
    for (auto& rt : o.routes) if (!rt.points.empty()) ov.line(rt.points.back() + vec3(0, 0.3f, 0), o.pos + vec3(0, 0.3f, 0), lc[rt.lane] * vec4(1, 1, 1, 0.5f), true);
    for (auto& dp : o.defendPoints) ov.line(dp, dp + vec3(0, 1.4f, 0), vec4(0.3f, 0.6f, 1, 1), true);
    ImVec2 sp;
    if (project(app, o.pos + vec3(0, o.yMax + 1, 0), sp)) {
      char b[96]; std::snprintf(b, sizeof b, "%d. %s  r %.0f m", o.order + 1, o.label.c_str(), o.radius);
      dl->AddText(ImVec2(sp.x - 40, sp.y), IM_COL32(255, 225, 140, 255), b);
    }
  }
  for (auto& m : s.spawns) {
    vec4 col = m.role == CombatRole::Attacker ? vec4(1, 0.35f, 0.25f, 1) : vec4(0.3f, 0.6f, 1, 1);
    vec3 f = App::forward(m.yaw, 0);
    ov.line(m.pos, m.pos + vec3(0, 1.8f, 0), col, true);
    ov.line(m.pos + vec3(0, 0.1f, 0), m.pos + vec3(0, 0.1f, 0) + f * 1.2f, col, true);
    ImVec2 sp;
    if (project(app, m.pos + vec3(0, 2.0f, 0), sp)) { char b[96]; std::snprintf(b, sizeof b, "%s %d %s", m.role == CombatRole::Attacker ? "ATK" : "DEF", m.stage + 1, m.label.c_str()); dl->AddText(sp, m.role == CombatRole::Attacker ? IM_COL32(255, 130, 110, 255) : IM_COL32(130, 170, 255, 255), b); }
  }
}

void MpUi::setupWindow(App& app) {
  if (!showSetup) return;
  Scenario& s = scenario(app);
  if (!rulesInit) { rules = s.rules; rulesInit = true; }
  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
  const char* title = setupMode == 0 ? "Singleplayer setup" : "Host LAN: lobby";
  if (!ImGui::Begin(title, &showSetup, ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }
  ImGui::Text("Map: %s   (%zu objectives)", app.doc.name.c_str(), s.objectives.size());
  int fac = (int)rules.defendingFaction;
  ImGui::Text("Defending faction (all humans defend this milestone):");
  ImGui::RadioButton("Allies [US] defend, Axis attack", &fac, 0); ImGui::SameLine(); ImGui::RadioButton("Axis [GER] defend, Allies attack", &fac, 1);
  rules.defendingFaction = (Faction)fac;
  int humans = setupMode == 1 && gNet ? gNet->humanCount() : 1;
  ImGui::SliderInt("Defender slots (humans + friendly AI)", &rules.defenderSlots, std::max(1, humans), ScenarioRules::kMaxCombatants - 1);
  ImGui::SliderInt("Attacking AI", &rules.attackerAI, 1, ScenarioRules::kMaxCombatants - rules.defenderSlots);
  ImGui::SliderInt("Attacker respawns per slot", &rules.attackerRespawns, 0, 20);
  ImGui::SliderFloat("Attacker wave interval (s)", &rules.attackerWave, 3, 60, "%.0f");
  ImGui::SliderFloat("Defender wave interval (s)", &rules.defenderWave, 3, 60, "%.0f");
  ImGui::SliderInt("Frag grenades per life", &rules.fragPerLife, 0, 4);
  ImGui::SliderInt("Smoke grenades per attacker life", &rules.smokePerLifeAttacker, 0, 3);
  ImGui::SliderInt("Smoke grenades per defender life (humans + AI)", &rules.smokePerLifeDefender, 0, 3);
  ImGui::Checkbox("Friendly fire (bullets and blasts hurt your own side)", &rules.friendlyFire);
  rules.defenderSlots = std::clamp(rules.defenderSlots, std::max(1, humans), ScenarioRules::kMaxCombatants - 1);
  rules.attackerAI = std::clamp(rules.attackerAI, 1, ScenarioRules::kMaxCombatants - rules.defenderSlots);
  int lives = 1 + rules.attackerRespawns;
  ImGui::Separator();
  ImGui::Text("Humans %d  |  Friendly AI %d  |  Enemy AI %d  |  Total %d / %d", humans, rules.defenderSlots - humans, rules.attackerAI, rules.defenderSlots + rules.attackerAI, ScenarioRules::kMaxCombatants);
  ImGui::Text("Each attacking slot: 1 initial life + %d respawns = %d lives  ->  %d attacker lives in total", rules.attackerRespawns, lives, lives * rules.attackerAI);
  ImGui::Text("Defenders respawn without limit. Minimum %.0f s from death to respawn.", rules.minRespawnDelay);
  auto issues = validateScenario(s, false);
  bool blocked = false;
  for (auto& i : issues) if (i.error) { blocked = true; ImGui::TextColored(ImVec4(1, 0.55f, 0.45f, 1), "%s", i.what.c_str()); }
  if (gGame && !gGame->setupError.empty()) ImGui::TextColored(ImVec4(1, 0.55f, 0.45f, 1), "Last start failed: %s", gGame->setupError.c_str());
  if (setupMode == 1) netLobby(app, blocked);
  else {
    ImGui::BeginDisabled(blocked);
    if (ImGui::Button("Start singleplayer match", ImVec2(260, 0))) startMatch(app);
    ImGui::EndDisabled();
  }
  ImGui::SameLine(); if (ImGui::Button("Save these rules as map defaults")) { s.rules = rules; commit(app); }
  if (blocked) ImGui::TextWrapped("Open Scenario > Objectives and spawns panel to fix the map first.");
  ImGui::End();
}

void MpUi::startMatch(App& app) {
  Scenario& s = scenario(app);
  // the match uses the setup rules; the map keeps its authored defaults unless saved above
  json scj = s.toJson(); scj["rules"] = rules.toJson();
  if (!app.doc.extra.is_object()) app.doc.extra = json::object();
  matchScenario = scj;
  if (!gGame) gGame = new Game(app);
  gGame->pendingStart = true;
  showSetup = false;
  if (!app.enterExplore()) { gGame->pendingStart = false; gGame->setupError = "could not enter Explore (start marker obstructed)"; showSetup = true; }
}

void MpUi::joinWindow(App& app) {
  if (!showJoin) return;
  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::Begin("Join LAN", &showJoin, ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }
  netJoin(app);
  ImGui::End();
}

// ---------------------------------------------------------------- in-match HUD
void Game::drawHud() {
  if (!active) return;
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  Soldier* L = local();
  CombatRole myRole = L ? L->role : CombatRole::Defender;
  Faction myFac = L ? L->faction : match.rules.defendingFaction;
  // ---- objective sequence across the top: every point, in order, with permanent progress and live occupancy
  int n = (int)match.obj.size();
  float W = std::min(io.DisplaySize.x - 40.0f, 170.0f * n), x0 = (io.DisplaySize.x - W) * 0.5f, y0 = 10, cw = W / std::max(1, n);
  for (int k = 0; k < n; ++k) {
    const ObjectiveRT& o = match.obj[k];
    float x = x0 + k * cw + 4, w = cw - 8;
    bool act = o.state == ObjState::Active;
    dl->AddRectFilled(ImVec2(x, y0), ImVec2(x + w, y0 + 50), IM_COL32(12, 12, 10, act ? 215 : 150), 4);
    if (act) dl->AddRect(ImVec2(x - 1, y0 - 1), ImVec2(x + w + 1, y0 + 51), IM_COL32(255, 210, 90, 255), 4, 0, 2.5f);
    char b[96]; std::snprintf(b, sizeof b, "%d. %s", k + 1, o.label.c_str());
    dl->AddText(ImVec2(x + 6, y0 + 3), act ? IM_COL32(255, 235, 180, 255) : IM_COL32(200, 200, 190, 220), b);
    float p = o.state == ObjState::Captured ? 100.0f : o.state == ObjState::Locked ? 0.0f : o.progress;
    dl->AddRectFilled(ImVec2(x + 6, y0 + 22), ImVec2(x + w - 6, y0 + 30), IM_COL32(60, 60, 55, 255), 2);
    dl->AddRectFilled(ImVec2(x + 6, y0 + 22), ImVec2(x + 6 + (w - 12) * p / 100.0f, y0 + 30), o.state == ObjState::Captured ? IM_COL32(190, 80, 60, 255) : IM_COL32(220, 150, 60, 255), 2);
    const char* st = o.state == ObjState::Locked ? "Locked" : o.state == ObjState::Captured ? "Captured" : o.A > o.D ? "Enemy advantage" : o.A > 0 && o.A == o.D ? "Contested (tied)" : o.D > 0 ? "Held" : "Empty";
    if (myRole == CombatRole::Attacker && act && o.A > o.D) st = "Advancing";
    std::snprintf(b, sizeof b, "%3.0f%%  %s", p, st);
    dl->AddText(ImVec2(x + 6, y0 + 32), o.state == ObjState::Captured ? IM_COL32(230, 120, 100, 255) : act && o.A > o.D ? IM_COL32(255, 170, 90, 255) : IM_COL32(210, 210, 200, 230), b);
  }
  // ---- active objective: direction / distance and occupancy
  if (const Objective* ob = match.activeObjective(); ob && L) {
    const ObjectiveRT& o = match.obj[match.active];
    vec3 from = L->alive ? app.eyePos() : L->feet;
    vec3 d = ob->pos - from; float dist = glm::length(vec2(d.x, d.z));
    float rel = wrapDeg(degrees(std::atan2(-d.x, -d.z)) - app.eyeYaw + 180.0f) - 180.0f;
    const char* arrow = std::fabs(rel) < 22 ? "^" : std::fabs(rel) > 158 ? "v" : rel > 0 ? "<" : ">";
    int fr = myRole == CombatRole::Attacker ? o.A : o.D, en = myRole == CombatRole::Attacker ? o.D : o.A;
    char b[200]; std::snprintf(b, sizeof b, "%s  %s %.0f m   |   on point: friendly %d, enemy %d", arrow, ob->label.c_str(), dist, fr, en);
    ImVec2 ts = ImGui::CalcTextSize(b);
    dl->AddRectFilled(ImVec2(io.DisplaySize.x * 0.5f - ts.x * 0.5f - 8, y0 + 56), ImVec2(io.DisplaySize.x * 0.5f + ts.x * 0.5f + 8, y0 + 76), IM_COL32(12, 12, 10, 170), 3);
    dl->AddText(ImVec2(io.DisplaySize.x * 0.5f - ts.x * 0.5f, y0 + 58), IM_COL32(240, 235, 210, 255), b);
  }
  // ---- side panel: forces, lives, grenades, health
  {
    char b[256];
    Faction ef = otherFaction(match.rules.defendingFaction);
    std::snprintf(b, sizeof b, "%s %s defending: %d alive", factionTag(match.rules.defendingFaction), factionName(match.rules.defendingFaction), match.livingDefenders());
    dl->AddText(ImVec2(14, io.DisplaySize.y - 150), factionCol(match.rules.defendingFaction), b);
    std::snprintf(b, sizeof b, "%s %s attacking: %d alive, %d respawns unused%s", factionTag(ef), factionName(ef), match.livingAttackers(), match.unusedAttackerLives(), match.pendingAttackers() ? " (+ wave pending)" : "");
    dl->AddText(ImVec2(14, io.DisplaySize.y - 132), factionCol(ef), b);
    if (L) {
      std::snprintf(b, sizeof b, "You: %s %s   Health %.0f   Frag %d   Smoke %d   [G] frag  [H] smoke", factionTag(myFac), roleName(myRole), std::max(0.0f, L->hp), L->frags, L->smokes);
      dl->AddText(ImVec2(14, io.DisplaySize.y - 112), IM_COL32(240, 235, 215, 255), b);
      float hp = std::clamp(L->hp / 100.0f, 0.0f, 1.0f);
      dl->AddRectFilled(ImVec2(14, io.DisplaySize.y - 94), ImVec2(214, io.DisplaySize.y - 86), IM_COL32(50, 20, 20, 220));
      dl->AddRectFilled(ImVec2(14, io.DisplaySize.y - 94), ImVec2(14 + 200 * hp, io.DisplaySize.y - 86), IM_COL32(200, 60, 50, 255));
    }
    if (!match.spawnProblem.empty()) dl->AddText(ImVec2(14, io.DisplaySize.y - 170), IM_COL32(255, 120, 90, 255), match.spawnProblem.c_str());
  }
  if (hitMarker > 0) { ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f); for (int k = 0; k < 4; ++k) { float a = 0.785f + k * 1.5708f; dl->AddLine(ImVec2(c.x + std::cos(a) * 6, c.y + std::sin(a) * 6), ImVec2(c.x + std::cos(a) * 13, c.y + std::sin(a) * 13), IM_COL32(255, 255, 255, (int)(255 * hitMarker * 4)), 2); } }
  if (hurtFlash > 0 && L && L->alive) {
    vec3 d = lastHurtDir; float rel = wrapDeg(degrees(std::atan2(-d.x, -d.z)) - app.eyeYaw + 180.0f) - 180.0f;
    ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f); float a = radians(-rel) - 1.5708f;
    dl->AddCircleFilled(ImVec2(c.x + std::cos(a) * 120, c.y + std::sin(a) * 120), 14, IM_COL32(220, 40, 30, (int)(200 * hurtFlash)));
  }
  // ---- death / spectate
  if (L && !L->alive && match.phase == MatchPhase::Running) {
    const RosterSlot& rs = match.slots[L->slot];
    float wave = match.nextWaveIn(rs.role), minLeft = std::max(0.0f, match.rules.minRespawnDelay - rs.deadFor);
    char b[200];
    if (L->lifeId == 0) std::snprintf(b, sizeof b, "Deploying...");
    else std::snprintf(b, sizeof b, "You were killed%s%s.   Respawn with the next wave in %.0f s%s", L->killer >= 0 ? " by " : "", L->killer >= 0 ? match.slots[L->killer].name.c_str() : "", std::max(wave, minLeft), spectate >= 0 ? "   (watching a soldier)" : "");
    ImVec2 ts = ImGui::CalcTextSize(b);
    dl->AddRectFilled(ImVec2(io.DisplaySize.x * 0.5f - ts.x * 0.5f - 14, io.DisplaySize.y * 0.7f - 10), ImVec2(io.DisplaySize.x * 0.5f + ts.x * 0.5f + 14, io.DisplaySize.y * 0.7f + 24), IM_COL32(10, 8, 8, 200), 5);
    dl->AddText(ImVec2(io.DisplaySize.x * 0.5f - ts.x * 0.5f, io.DisplaySize.y * 0.7f), IM_COL32(255, 220, 200, 255), b);
  }
  if (!L && isClient && match.phase == MatchPhase::Running) {
    const char* m = "Joined the match: you take a defender slot when its soldier next respawns (normal respawn flow). Watching meanwhile.";
    ImVec2 ts = ImGui::CalcTextSize(m);
    dl->AddRectFilled(ImVec2(io.DisplaySize.x * 0.5f - ts.x * 0.5f - 14, io.DisplaySize.y * 0.7f - 10), ImVec2(io.DisplaySize.x * 0.5f + ts.x * 0.5f + 14, io.DisplaySize.y * 0.7f + 24), IM_COL32(10, 8, 8, 200), 5);
    dl->AddText(ImVec2(io.DisplaySize.x * 0.5f - ts.x * 0.5f, io.DisplaySize.y * 0.7f), IM_COL32(255, 220, 200, 255), m);
  }
  // ---- result
  if (match.phase == MatchPhase::Ended) {
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.45f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Match result", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
    bool won = (match.winner == Winner::Defenders) == (myRole == CombatRole::Defender);
    ImGui::TextColored(won ? ImVec4(0.6f, 0.95f, 0.6f, 1) : ImVec4(1, 0.5f, 0.45f, 1), "%s", won ? "VICTORY" : "DEFEAT");
    ImGui::Text("%s after %.0f s.", match.result.c_str(), match.time);
    int caps = 0; for (auto& o : match.obj) caps += o.state == ObjState::Captured;
    ImGui::Text("Objectives captured: %d / %zu   Attacker lives left: %d", caps, match.obj.size(), match.unusedAttackerLives());
    if (L) ImGui::Text("Your kills %d, deaths %d", match.slots[L->slot].kills, match.slots[L->slot].deaths);
    if (isClient) ImGui::TextDisabled("Waiting for the host to return everyone to the lobby...");
    else if (ImGui::Button("Return to lobby", ImVec2(220, 0))) gUi.wantExit = true;
    ImGui::End();
    if (!app.selftest) glfwSetInputMode(app.win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  }
  if (debugOverlay) drawDebug();
}

void Game::drawDebug() {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  for (auto& s : sold) {
    if (!s.alive) continue;
    ImVec2 sp;
    if (!project(app, s.feet + vec3(0, 2.1f, 0), sp)) continue;
    const char* mode[] = {"idle", "advance", "hold", "engage", "cover", "investigate", "evade", "return", "reload", "throw"};
    char b[200]; std::snprintf(b, sizeof b, "%s #%d %s %s\n%s | tgt %d | slot %d L%d", s.role == CombatRole::Attacker ? "ATK" : "DEF", s.slot, s.human ? "human" : mode[s.br.mode], s.br.lane == 0 ? "left" : s.br.lane == 2 ? "right" : "main",
                                s.br.why.c_str(), s.br.target, s.slot, match.slots[s.slot].livesLeft);
    dl->AddText(sp, s.role == CombatRole::Attacker ? IM_COL32(255, 140, 120, 255) : IM_COL32(140, 180, 255, 255), b);
    for (auto& m : s.br.mem) if (t - m.lastSeen < 6) { ImVec2 a, c; if (project(app, s.feet + vec3(0, 1.6f, 0), a) && project(app, m.lastPos + vec3(0, 1.0f, 0), c)) dl->AddLine(a, c, m.visible ? IM_COL32(255, 60, 60, 160) : IM_COL32(255, 200, 60, 120), 1); }
    for (size_t k = s.br.pathIdx; k + 1 < s.br.path.size(); ++k) { ImVec2 a, c; if (project(app, nav.pos(s.br.path[k]) + vec3(0, 0.2f, 0), a) && project(app, nav.pos(s.br.path[k + 1]) + vec3(0, 0.2f, 0), c)) dl->AddLine(a, c, IM_COL32(120, 255, 140, 140), 1.5f); }
    if (s.br.hasCover) { ImVec2 c; if (project(app, s.br.coverPos + vec3(0, 0.5f, 0), c)) dl->AddCircle(c, 6, IM_COL32(120, 220, 255, 200)); }
  }
}
