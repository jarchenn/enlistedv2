// Multiplayer + AI user interface: Play menu (Singleplayer / Host LAN / Join LAN + match setup), the Creative
// Scenario panel (ordered objectives, spawn markers, approach routes, validation) with its world overlay, and the
// in-match HUD (objective sequence bar, counts, grenades, death screen, results).
#include "mpui.h"
#include "game.h"
#include "app.h"
#include "net.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include "physics.h"
#include "ai_hooks.h"
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
  (void)app;
  {   // called inside the editor's main menu bar
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
  }
}

// ---------------------------------------------------------------- scenario editing helpers (panel, mouse tools, tests)
static vec3 groundAt(App& app, vec3 p) { vec3 q; if (app.terrain.raycast(vec3(p.x, p.y + 50, p.z), vec3(0, -1, 0), 200, q)) return q; return p; }

uint64_t MpUi::placeRack(App& app, const SpawnMarker& m) {
  Faction f = m.role == CombatRole::Defender ? sc.rules.defendingFaction : otherFaction(sc.rules.defendingFaction);
  const AssetDef* a = app.lib.find(f == Faction::Allies ? "rack_allied" : "rack_axis");
  if (!a) return 0;
  vec3 fw = App::forward(m.yaw, 0), rt = glm::normalize(glm::cross(fw, vec3(0, 1, 0)));
  for (float side : {2.5f, -2.5f, 4.0f, -4.0f}) {   // beside the marker, never on it
    vec3 p = groundAt(app, m.pos + rt * side + fw * 0.5f);
    if (!app.checkPlacement(a, p, m.yaw + 90.0f, 0).ok) continue;
    uint64_t saved = app.selected;
    uint64_t id = app.placeInstance(a, p, m.yaw + 90.0f);
    app.selected = saved;
    if (id) return id;
  }
  return 0;
}
void MpUi::removeRack(App& app, uint64_t id) {
  if (!id) return;
  auto& v = app.doc.instances;
  auto it = std::find_if(v.begin(), v.end(), [&](const Instance& i) { return i.id == id; });
  if (it != v.end() && (it->assetId == "rack_allied" || it->assetId == "rack_axis")) { app.pushUndo(); v.erase(std::find_if(v.begin(), v.end(), [&](const Instance& i) { return i.id == id; })); app.dirty = true; }
}
bool MpUi::addObjectiveAt(App& app, vec3 p) {
  Scenario& s = scenario(app);
  if (!s.canAddObjective()) { app.toast("A map holds at most 10 objectives (A to J).", 5); return false; }
  Objective o; o.id = s.newId("obj"); o.order = (int)s.objectives.size(); o.pos = p;
  o.label = std::string("Objective ") + objectiveLetter(o.order);
  o.attackYaw = s.objectives.empty() ? wrapDeg(app.camYaw) : s.objectives.back().attackYaw;
  s.objectives.push_back(o); selObj = o.id; commit(app);
  return true;
}
std::string MpUi::addSpawnAt(App& app, CombatRole role, vec3 p, int phase) {
  Scenario& s = scenario(app);
  if (s.objectives.empty()) { app.toast("Place an objective first: spawns belong to an objective's phase.", 5); return ""; }
  phase = std::clamp(phase, 0, (int)s.objectives.size() - 1);
  SpawnMarker m; m.id = s.newId("spawn"); m.role = role; m.phases = {phase}; m.pos = p;
  vec3 to = s.objectives[phase].pos - p;   // face the objective
  m.yaw = glm::length(vec2(to.x, to.z)) > 0.1f ? wrapDeg(degrees(std::atan2(-to.x, -to.z))) : app.camYaw;
  m.label = std::string(role == CombatRole::Attacker ? "Attackers " : "Defenders ") + objectiveLetter(phase);
  m.rack = placeRack(app, m);
  s.spawns.push_back(m); selSpawn = m.id; commit(app);
  return m.id;
}
bool MpUi::deleteSpawn(App& app, const std::string& id) {
  Scenario& s = scenario(app);
  auto it = std::find_if(s.spawns.begin(), s.spawns.end(), [&](const SpawnMarker& m) { return m.id == id; });
  if (it == s.spawns.end()) return false;
  uint64_t rack = it->rack; s.spawns.erase(it); removeRack(app, rack); commit(app);
  return true;
}
bool MpUi::deleteObjective(App& app, const std::string& id) {
  Scenario& s = scenario(app);
  std::vector<uint64_t> racks;
  if (!s.removeObjective(id, &racks)) return false;
  for (auto r : racks) removeRack(app, r);
  if (selObj == id) selObj.clear();
  commit(app);
  return true;
}

// Mouse tools and drag handles in Creative (called by the editor before its own picking; true = mouse consumed).
bool MpUi::creativeMouse(App& app) {
  if (!showPanel || app.mode != Mode::Creative) { tool = T_NONE; drag = D_NONE; return false; }
  ImGuiIO& io = ImGui::GetIO();
  bool down = glfwGetMouseButton(app.win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  bool press = down && !lmbPrev; lmbPrev = down;
  if (io.WantCaptureMouse && drag == D_NONE) return tool != T_NONE;
  Scenario& s = scenario(app);
  vec3 ro, rd; app.cursorRay(ro, rd);
  vec3 hit; Physics::Hit h; bool ok = Physics::castRayEx(ro, rd, 3000, h) ? (hit = h.pos, true) : app.terrain.raycast(ro, rd, 3000, hit);
  double mx, my; glfwGetCursorPos(app.win, &mx, &my); ImVec2 mouse((float)mx, (float)my);
  Objective* so = s.find(selObj);
  // ---- active drag
  if (drag != D_NONE) {
    if (!down) { drag = D_NONE; commit(app); return true; }
    if (!ok) return true;
    if (drag == D_OBJ && so) { vec3 d = hit - so->pos; so->pos = hit; for (auto& q : so->polygon) q += vec2(d.x, d.z); }
    else if (drag == D_RADIUS && so) so->radius = std::clamp(glm::length(vec2(hit.x - so->pos.x, hit.z - so->pos.z)), 2.0f, 200.0f);
    else if (drag == D_VERTEX && so && dragIndex < (int)so->polygon.size()) so->polygon[dragIndex] = vec2(hit.x, hit.z);
    else if (drag == D_SPAWN) for (auto& m : s.spawns) if (m.id == selSpawn) {
      vec3 d = hit - m.pos; m.pos = hit;
      if (Instance* r = app.doc.find(m.rack)) { r->pos = groundAt(app, r->pos + d); app.bump(*r); }
    }
    return true;
  }
  // ---- armed tools
  if (tool != T_NONE) {
    if (!press || !ok) return true;
    switch (tool) {
      case T_OBJECTIVE: addObjectiveAt(app, hit); tool = T_NONE; break;
      case T_ATK: addSpawnAt(app, CombatRole::Attacker, hit, so ? so->order : 0); tool = T_NONE; break;
      case T_DEF: addSpawnAt(app, CombatRole::Defender, hit, so ? so->order : 0); tool = T_NONE; break;
      case T_POLY: if (so) { if (so->shape != CaptureShape::Polygon) { so->polygon.clear(); so->shape = CaptureShape::Polygon; } so->polygon.push_back(vec2(hit.x, hit.z)); commit(app); } break;
      case T_SLOT: if (so) { DefenseSlot d; d.pos = hit; d.yaw = wrapDeg(so->attackYaw + 180.0f); d.kind = (SlotKind)slotKind; d.priority = slotPriority; so->defenseSlots.push_back(d); commit(app); } break;
      case T_LINK: if (so) {
        float t; uint64_t id = app.pickInstance(ro, rd, t);
        if (id) { auto& L = so->linkedStructures; auto it = std::find(L.begin(), L.end(), id); if (it == L.end()) L.push_back(id); else L.erase(it); commit(app); }
      } break;
      default: break;
    }
    return true;
  }
  // ---- handles (only on press near one; otherwise the editor picks as usual)
  if (!press) return false;
  auto near = [&](vec3 p, float px) { ImVec2 sp; return project(app, p, sp) && std::hypot(sp.x - mouse.x, sp.y - mouse.y) < px; };
  if (so) {
    if (so->shape == CaptureShape::Polygon) { for (int k = 0; k < (int)so->polygon.size(); ++k) if (near(groundAt(app, vec3(so->polygon[k].x, so->pos.y, so->polygon[k].y)), 10)) { drag = D_VERTEX; dragIndex = k; return true; } }
    else if (near(groundAt(app, so->pos + vec3(so->radius, 0, 0)), 10)) { drag = D_RADIUS; return true; }
  }
  for (auto& o : s.objectives) if (near(o.pos + vec3(0, 0.5f, 0), 14)) { selObj = o.id; drag = D_OBJ; return true; }
  for (auto& m : s.spawns) if (near(m.pos + vec3(0, 0.9f, 0), 12)) { selSpawn = m.id; drag = D_SPAWN; return true; }
  return false;
}

static const char* kSlotLabels[(int)SlotKind::Count] = {"Position", "MG nest", "Trench bay", "Window", "Foxhole", "Bunker embrasure"};

void MpUi::scenarioPanel(App& app) {
  if (!showPanel) return;
  Scenario& s = scenario(app);
  ImGui::SetNextWindowSize(ImVec2(450, 680), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 470, 40), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Scenario: objectives, spawns, defense", &showPanel)) { ImGui::End(); return; }
  if (!migration.empty()) for (auto& m : migration) ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.5f, 1), "%s", m.c_str());
  ImGui::TextWrapped("Click a tool, then click the ground. Drag an objective's centre or a spawn to move it; drag the ring handle "
                     "(or polygon corners) to reshape. Drag rows to reorder. Ids stay stable when reordered.");
  auto toolBtn = [&](const char* label, Tool t) {
    bool on = tool == t; if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.55f, 0.2f, 1));
    if (ImGui::Button(label)) tool = on ? T_NONE : t;
    if (on) ImGui::PopStyleColor();
  };
  ImGui::BeginDisabled(!s.canAddObjective()); toolBtn("Place objective", T_OBJECTIVE); ImGui::EndDisabled();
  ImGui::SameLine(); ImGui::BeginDisabled(s.objectives.empty()); toolBtn("Attacker spawn", T_ATK); ImGui::SameLine(); toolBtn("Defender spawn", T_DEF); ImGui::EndDisabled();
  if (!s.canAddObjective()) ImGui::TextDisabled("10 objectives placed: the maximum (A to J).");
  if (tool != T_NONE) ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "Click the ground to place (Esc or click the tool again to cancel).");
  if (tool != T_NONE && ImGui::IsKeyPressed(ImGuiKey_Escape)) tool = T_NONE;
  bool changed = false;
  ImGui::SeparatorText("Objectives (attack order A -> J)");
  for (size_t k = 0; k < s.objectives.size(); ++k) {
    Objective& o = s.objectives[k];
    ImGui::PushID(o.id.c_str());
    bool sel = selObj == o.id;
    char hdr[180]; std::snprintf(hdr, sizeof hdr, "%c  %s  (%s)", o.letter(), o.displayName().c_str(), o.id.c_str());
    if (ImGui::Selectable(hdr, sel)) selObj = sel ? std::string() : o.id;
    if (ImGui::BeginDragDropSource()) { ImGui::SetDragDropPayload("NMK_OBJ", &k, sizeof k); ImGui::Text("Move %c %s", o.letter(), o.displayName().c_str()); ImGui::EndDragDropSource(); }
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("NMK_OBJ")) { size_t from = *(const size_t*)p->Data; if (from < s.objectives.size() && s.moveTo(s.objectives[from].id, (int)k)) { commit(app); ImGui::EndDragDropTarget(); ImGui::PopID(); break; } }
      ImGui::EndDragDropTarget();
    }
    if (sel) {
      ImGui::Indent();
      char lb[96]; std::snprintf(lb, sizeof lb, "%s", o.label.c_str());
      if (ImGui::InputText("Display name", lb, sizeof lb)) { o.label = lb; changed = true; }
      if (ImGui::Button("Move earlier") && s.moveEarlier(o.id)) { changed = true; commit(app); ImGui::Unindent(); ImGui::PopID(); break; }
      ImGui::SameLine(); if (ImGui::Button("Move later") && s.moveLater(o.id)) { changed = true; commit(app); ImGui::Unindent(); ImGui::PopID(); break; }
      int shp = (int)o.shape;
      if (ImGui::RadioButton("Circle", &shp, 0)) { o.shape = CaptureShape::Circle; changed = true; }
      ImGui::SameLine(); if (ImGui::RadioButton("Polygon", &shp, 1)) { o.shape = CaptureShape::Polygon; changed = true; }
      if (o.shape == CaptureShape::Circle) { if (ImGui::SliderFloat("Capture radius (m)", &o.radius, 2, 100, "%.1f")) changed = true; }
      else {
        ImGui::Text("Polygon: %zu corners", o.polygon.size()); ImGui::SameLine(); toolBtn("Add corners", T_POLY);
        ImGui::SameLine(); if (ImGui::SmallButton("Clear")) { o.polygon.clear(); changed = true; }
        if (o.polygon.size() < 3) ImGui::TextColored(ImVec4(1, 0.6f, 0.45f, 1), "Needs at least 3 corners.");
      }
      if (ImGui::SliderFloat("Capture time, 1 attacker (s)", &o.captureTime, 5, 180, "%.0f")) changed = true;
      ImGui::TextDisabled("Faster with more attackers, up to %.0fx; any defender on the point pauses it.", s.rules.captureSpeedCap);
      if (ImGui::DragFloatRange2("Vertical bounds (m)", &o.yMin, &o.yMax, 0.1f, -10, 30, "below %.1f", "above %.1f")) changed = true;
      if (ImGui::SliderFloat("Attack direction (deg)", &o.attackYaw, 0, 360, "%.0f")) changed = true;
      if (ImGui::Button("Aim attack arrow along view")) { o.attackYaw = wrapDeg(app.camYaw); changed = true; }
      ImGui::Text("Linked structures: %zu", o.linkedStructures.size()); ImGui::SameLine(); toolBtn("Pick in scene", T_LINK);
      ImGui::SameLine(); if (ImGui::SmallButton("Clear##ls")) { o.linkedStructures.clear(); changed = true; }
      for (size_t q = 0; q < o.linkedStructures.size(); ++q) { Instance* in = app.doc.find(o.linkedStructures[q]); ImGui::BulletText("%s #%llu%s", in ? in->assetId.c_str() : "(deleted)", (unsigned long long)o.linkedStructures[q], in ? "" : " - will be ignored"); }
      ImGui::Text("Defense slots: %zu", o.defenseSlots.size());
      ImGui::SetNextItemWidth(130); ImGui::Combo("##kind", &slotKind, kSlotLabels, (int)SlotKind::Count); ImGui::SameLine();
      ImGui::SetNextItemWidth(70); ImGui::SliderInt("priority", &slotPriority, 1, 5); ImGui::SameLine(); toolBtn("Add slots", T_SLOT);
      for (size_t q = 0; q < o.defenseSlots.size(); ++q) {
        auto& d = o.defenseSlots[q]; ImGui::PushID((int)q);
        ImGui::BulletText("%s  p%d  facing %.0f", kSlotLabels[(int)d.kind], d.priority, d.yaw); ImGui::SameLine();
        if (ImGui::SmallButton("x")) { o.defenseSlots.erase(o.defenseSlots.begin() + q); changed = true; ImGui::PopID(); break; }
        ImGui::PopID();
      }
      ImGui::Text("Lane hints (optional; left / right relative to the attack arrow):");
      const char* ln[3] = {"Left", "Main", "Right"};
      vec3 vc; bool vcOk = viewCentrePoint(app, vc);
      for (int L = 0; L < 3; ++L) {
        ImGui::PushID(L);
        int npts = 0; for (auto& r : o.routes) if (r.lane == L) npts += (int)r.points.size();
        ImGui::Text("  %s: %d points", ln[L], npts); ImGui::SameLine();
        if (ImGui::SmallButton("add at view centre") && vcOk) { bool f = false; for (auto& r : o.routes) if (r.lane == L) { r.points.push_back(vc); f = true; } if (!f) o.routes.push_back({L, {vc}}); changed = true; }
        ImGui::SameLine(); if (ImGui::SmallButton("clear")) { o.routes.erase(std::remove_if(o.routes.begin(), o.routes.end(), [&](const ApproachRoute& r) { return r.lane == L; }), o.routes.end()); changed = true; }
        ImGui::PopID();
      }
      if (ImGui::Button("Delete objective")) { std::string id = o.id; ImGui::Unindent(); ImGui::PopID(); deleteObjective(app, id); break; }
      ImGui::Unindent();
    }
    ImGui::PopID();
  }
  ImGui::SeparatorText("Spawns (each with its team's weapons rack)");
  for (size_t m = 0; m < s.spawns.size(); ++m) {
    auto& sp = s.spawns[m];
    ImGui::PushID(sp.id.c_str());
    bool sel = selSpawn == sp.id;
    std::string ph; for (int p : sp.phases) { if (!ph.empty()) ph += ","; ph += objectiveLetter(p); }
    char hdr[160]; std::snprintf(hdr, sizeof hdr, "%s  %s  [%s]%s", sp.role == CombatRole::Attacker ? "ATK" : "DEF", sp.label.c_str(), ph.c_str(), sp.rack && app.doc.find(sp.rack) ? "  + rack" : "");
    if (ImGui::Selectable(hdr, sel)) selSpawn = sel ? std::string() : sp.id;
    if (sel) {
      ImGui::Indent();
      ImGui::Text("Active in phases:");
      for (auto& o : s.objectives) {
        ImGui::SameLine(); bool on = sp.activeIn(o.order); char b[8]; std::snprintf(b, sizeof b, "%c", o.letter());
        if (ImGui::Checkbox(b, &on)) {
          if (on) sp.phases.push_back(o.order); else if (sp.phases.size() > 1) sp.phases.erase(std::remove(sp.phases.begin(), sp.phases.end(), o.order), sp.phases.end());
          std::sort(sp.phases.begin(), sp.phases.end()); changed = true;
        }
      }
      if (!(sp.rack && app.doc.find(sp.rack)) && ImGui::SmallButton("Place weapons rack")) { sp.rack = placeRack(app, sp); changed = true; }
      if (ImGui::SmallButton("Delete spawn (and its rack)")) { std::string id = sp.id; ImGui::Unindent(); ImGui::PopID(); deleteSpawn(app, id); break; }
      ImGui::Unindent();
    }
    ImGui::PopID();
  }
  ImGui::SeparatorText("Validation");
  if (ImGui::Button("Validate now")) { lastValidated = validateScenario(s, true); validatedOnce = true; }
  ImGui::SameLine(); ImGui::TextDisabled("(also runs on save)");
  auto issues = validateScenario(s, true);
  int ne = 0, nw = 0; for (auto& i : issues) (i.error ? ne : nw)++;
  if (issues.empty()) ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1), "Ready: %zu objectives, %zu spawns.", s.objectives.size(), s.spawns.size());
  else ImGui::Text("%d error%s (block play), %d warning%s", ne, ne == 1 ? "" : "s", nw, nw == 1 ? "" : "s");
  for (auto& i : issues) ImGui::TextColored(i.error ? ImVec4(1, 0.55f, 0.45f, 1) : ImVec4(0.95f, 0.85f, 0.5f, 1), "%s %s", i.error ? "ERROR" : "warn ", i.what.c_str());
  if (!worldIssues.empty()) { ImGui::Text("World checks (last match start / check):"); for (auto& w : worldIssues) ImGui::TextColored(ImVec4(1, 0.6f, 0.45f, 1), "%s", w.c_str()); }
  if (changed) commit(app);
  ImGui::End();
}

void MpUi::creativeOverlay(App& app, Overlay& ov) {
  if (!showOverlay && !showPanel) return;
  Scenario& s = scenario(app);
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  // attack path A -> B -> C ... (from the first attacker spawn group)
  {
    vec2 st = s.attackerStart();
    vec3 prev = groundAt(app, vec3(st.x, 0, st.y)) + vec3(0, 1.5f, 0);
    for (auto& o : s.objectives) { vec3 p = o.pos + vec3(0, 1.5f, 0); ov.line(prev, p, vec4(1, 0.9f, 0.4f, 0.9f), true); prev = p; }
  }
  for (auto& o : s.objectives) {
    bool sel = o.id == selObj;
    vec4 col = sel ? vec4(1.0f, 0.85f, 0.3f, 1) : vec4(0.9f, 0.7f, 0.25f, 0.8f);
    // exactly the authoritative capture volume between pos.y + yMin and pos.y + yMax
    std::vector<vec3> ring;
    if (o.shape == CaptureShape::Polygon && o.polygon.size() >= 3) for (auto& q : o.polygon) ring.push_back(vec3(q.x, o.pos.y, q.y));
    else for (int k = 0; k < 48; ++k) { float a = k * 6.2831853f / 48; ring.push_back(o.pos + vec3(std::cos(a) * o.radius, 0, std::sin(a) * o.radius)); }
    for (size_t k = 0; k < ring.size(); ++k) {
      vec3 p0 = ring[k], p1 = ring[(k + 1) % ring.size()];
      ov.line(p0 + vec3(0, o.yMin, 0), p1 + vec3(0, o.yMin, 0), col, true); ov.line(p0 + vec3(0, o.yMax, 0), p1 + vec3(0, o.yMax, 0), col, true);
      if (k % 6 == 0 || o.shape == CaptureShape::Polygon) ov.line(p0 + vec3(0, o.yMin, 0), p0 + vec3(0, o.yMax, 0), col * vec4(1, 1, 1, 0.6f), true);
    }
    if (sel) {   // handles
      ov.line(o.pos, o.pos + vec3(0, 3, 0), vec4(1, 1, 1, 1), true);
      if (o.shape == CaptureShape::Circle) { vec3 h = groundAt(app, o.pos + vec3(o.radius, 0, 0)); ov.line(h, h + vec3(0, 2, 0), vec4(1, 1, 1, 1), true); }
      for (auto id : o.linkedStructures) if (Instance* in = app.doc.find(id)) ov.line(o.pos + vec3(0, 2, 0), in->pos + vec3(0, 2, 0), vec4(0.5f, 1, 0.6f, 0.9f), true);
    }
    vec3 f = attackForward(o.attackYaw), r = attackRight(o.attackYaw);
    float R = o.extent();
    vec3 tail = o.pos - f * (R + 12) + vec3(0, 0.5f, 0), head = o.pos - f * (R + 2) + vec3(0, 0.5f, 0);
    ov.line(tail, head, vec4(1, 0.3f, 0.2f, 1), true); ov.line(head, head - f * 2.0f + r * 1.2f, vec4(1, 0.3f, 0.2f, 1), true); ov.line(head, head - f * 2.0f - r * 1.2f, vec4(1, 0.3f, 0.2f, 1), true);
    const vec4 lc[3] = {vec4(0.4f, 0.7f, 1, 1), vec4(1, 1, 1, 1), vec4(1, 0.55f, 0.85f, 1)};
    for (auto& rt : o.routes) for (size_t k = 0; k + 1 < rt.points.size(); ++k) ov.line(rt.points[k] + vec3(0, 0.3f, 0), rt.points[k + 1] + vec3(0, 0.3f, 0), lc[rt.lane], true);
    for (auto& rt : o.routes) if (!rt.points.empty()) ov.line(rt.points.back() + vec3(0, 0.3f, 0), o.pos + vec3(0, 0.3f, 0), lc[rt.lane] * vec4(1, 1, 1, 0.5f), true);
    for (auto& d : o.defenseSlots) { vec3 fw = App::forward(d.yaw, 0); ov.line(d.pos, d.pos + vec3(0, 1.4f, 0), vec4(0.3f, 0.6f, 1, 1), true); ov.line(d.pos + vec3(0, 1.2f, 0), d.pos + vec3(0, 1.2f, 0) + fw * 1.0f, vec4(0.3f, 0.6f, 1, 1), true); }
    ImVec2 sp;
    if (project(app, o.pos + vec3(0, o.yMax + 1, 0), sp)) {
      char b[140]; std::snprintf(b, sizeof b, "%c  %s  %.0f s", o.letter(), o.displayName().c_str(), o.captureTime);
      dl->AddText(ImVec2(sp.x - 40, sp.y), IM_COL32(255, 225, 140, 255), b);
    }
  }
  for (auto& m : s.spawns) {
    vec4 col = m.role == CombatRole::Attacker ? vec4(1, 0.35f, 0.25f, 1) : vec4(0.3f, 0.6f, 1, 1);
    if (m.id == selSpawn) col = vec4(1, 1, 1, 1);
    vec3 f = App::forward(m.yaw, 0);
    ov.line(m.pos, m.pos + vec3(0, 1.8f, 0), col, true);
    ov.line(m.pos + vec3(0, 0.1f, 0), m.pos + vec3(0, 0.1f, 0) + f * 1.2f, col, true);
    ImVec2 sp;
    if (project(app, m.pos + vec3(0, 2.0f, 0), sp)) {
      std::string ph; for (int p : m.phases) ph += objectiveLetter(p);
      char b[120]; std::snprintf(b, sizeof b, "%s %s  %s", m.role == CombatRole::Attacker ? "ATK" : "DEF", ph.c_str(), m.label.c_str());
      dl->AddText(sp, m.role == CombatRole::Attacker ? IM_COL32(255, 130, 110, 255) : IM_COL32(130, 170, 255, 255), b);
    }
  }
}

// Save hook: refuses a map whose scenario has spawns but no objective; otherwise reports the validation summary.
bool MpUi::beforeSave(App& app) {
  if (!app.doc.extra.is_object() || !app.doc.extra.contains("scenario")) return true;
  Scenario& s = scenario(app);
  if (s.objectives.empty() && !s.spawns.empty()) { app.toast("Not saved: the scenario has spawns but no objective. Place at least one objective (Scenario panel) or delete the spawns.", 8); return false; }
  int ne = 0, nw = 0; for (auto& i : validateScenario(s, true)) (i.error ? ne : nw)++;
  if (ne || nw) app.toast("Scenario check: " + std::to_string(ne) + " error(s), " + std::to_string(nw) + " warning(s) - see Scenario panel", 6);
  return true;
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
  ImGui::Text("Defending side (humans join the defenders):");
  ImGui::RadioButton("Axis [GER] defend, Allies attack", &fac, 1); ImGui::SameLine(); ImGui::RadioButton("Allies [US] defend, Axis attack", &fac, 0);
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
  aiDifficultyCombo();   // W6: Recruit / Regular / Veteran (AI_SPEC 10)
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
    if (s.br.hasCover) { ImVec2 c; if (project(app, s.br.coverPos + vec3(0, 0.5f, 0), c)) dl->AddCircle(c, 6, IM_COL32(120, 220, 255, 200)); }
  }
  aiDrawDebug(*this);   // W6: navmesh, links, routes, utilities, cover, threats, stuck events and the AI debug window
}
