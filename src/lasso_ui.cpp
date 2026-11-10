// Lasso tool: editor input, overlay and panel (docs/lasso.md). The operations live in lasso.cpp / ground_paint.cpp /
// lasso_land.cpp, so the self-test drives exactly what the UI calls.
#include "lasso.h"
#include "geom2d.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#if __has_include("ground_paint.h")
#include "ground_paint.h"
#define LASSO_HAS_PAINT 1
#endif
#if __has_include("lasso_land.h")
#include "lasso_land.h"
#define LASSO_HAS_LAND 1
#endif

namespace {
struct UiState {
  int gesture = 0;              // 0 none, 1 drawing, 2 group drag (pending until 4 px), 3 group drag, 4 move-land drag
  bool prevDown = false;
  bool subtract = false, addTo = false;
  std::vector<vec2> stroke;     // ground points (freehand) or {centre} (circle)
  vec2 lastPx{0}, pressPx{0};
  float travel = 0, circleR = 0;
  vec3 pressGround{0};
  float dragYaw = 0;
  bool elevDirty = false; float sinceElev = 0;
  bool rotGesture = false; float rotAccum = 0;
  vec2 landDelta{0}; float landYaw = 0;
} gU;

ElevParams elevParams() { const LassoParams& P = lassoParams(); return ElevParams{P.amount, P.falloff, P.profile, P.linearEdge}; }

std::vector<vec2> closeStroke() {
  if (lassoParams().circle) {
    if (gU.stroke.empty() || gU.circleR < 0.5f) return {};
    int n = std::clamp((int)(gU.circleR * 1.5f), 24, 96);
    return geom2d::circle(gU.stroke[0], gU.circleR, n);
  }
  if (gU.stroke.size() < 3) return {};
  return geom2d::simplifyLoop(gU.stroke, 0.15f);
}

void finishShape(App& app) {
  LassoParams& P = lassoParams(); LassoRegion& R = lassoRegion();
  std::vector<vec2> poly = closeStroke();
  gU.stroke.clear();
  bool click = poly.empty() || gU.travel < 6.0f;
  if (click) {   // a click: pick one object (Objects) or clear the region
    if (P.mode == LM_OBJECTS) {
      vec3 ro, rd; app.cursorRay(ro, rd); float t;
      uint64_t id = app.pickInstance(ro, rd, t);
      if (id) {
        auto it = std::find(app.selection.begin(), app.selection.end(), id);
        if (gU.addTo) { if (it == app.selection.end()) app.selection.push_back(id); }
        else if (gU.subtract) { if (it != app.selection.end()) app.selection.erase(it); }
        else app.selection = {id};
        app.selected = app.selection.empty() ? 0 : app.selection.front();
      } else if (!gU.addTo && !gU.subtract) { app.selection.clear(); app.selected = 0; R.clear(); }
    } else if (!gU.addTo && !gU.subtract) { if (lassoElevActive()) lassoElevCancel(app); R.clear(); }
    return;
  }
  if (!gU.addTo && !gU.subtract) R.clear();
  if (P.mode == LM_OBJECTS) {   // the selection follows this shape: replace, add or remove
    LassoRegion one; one.add(poly);
    R.add(poly, gU.subtract);
    lassoSelectObjects(app, one, gU.subtract ? 2 : gU.addTo ? 1 : 0, P.touching);
    return;
  }
  R.add(poly, gU.subtract);
  if (P.mode == LM_ELEVATION) {   // a new region starts a new live edit (an unapplied one is discarded)
    if (lassoElevActive()) { lassoElevCancel(app); app.toast("Previous elevation edit discarded (Apply keeps an edit).", 3); }
    if (lassoElevBegin(app, R)) lassoElevPreview(app, elevParams());
  }
#ifdef LASSO_HAS_PAINT
  if (P.mode == LM_PAINT) groundPaintRegion(app, R, P.paintType, P.paintStrength, P.paintFeather);
#endif
}

void drapedLoop(const App& app, Overlay& ov, const std::vector<vec2>& p, vec4 c, float lift = 0.15f, bool closed = true) {
  const Terrain& T = app.terrain;
  size_t n = p.size(); if (n < 2) return;
  for (size_t i = 0; i + (closed ? 0 : 1) < n; ++i) {
    vec2 a = p[i], b = p[(i + 1) % n];
    int steps = std::clamp((int)(glm::length(b - a) / 1.0f), 1, 64);
    for (int s = 0; s < steps; ++s) {
      vec2 u = glm::mix(a, b, (float)s / steps), v = glm::mix(a, b, (float)(s + 1) / steps);
      ov.line(vec3(u.x, T.height(u.x, u.y) + lift, u.y), vec3(v.x, T.height(v.x, v.y) + lift, v.y), c);
    }
  }
}
}   // namespace

bool lassoOwnsKeys(const App& app) { return app.selection.size() > 1; }

// Z / X turn the group about its centre (15 deg per press when snapping, continuous while held otherwise); one
// undo step per press (snap) or per held gesture; Del / Ctrl+D act on the whole group (editor.cpp routes them here)
void lassoGroupKeys(App& app, float dt) {
  ImGuiIO& io = ImGui::GetIO();
  if (io.WantCaptureKeyboard || io.KeyCtrl || gU.gesture >= 2) return;
  float d = 0;
  if (app.rotSnap) { if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) d = -15; if (ImGui::IsKeyPressed(ImGuiKey_X, false)) d = 15; }
  else { if (ImGui::IsKeyDown(ImGuiKey_Z)) d = -45 * dt; if (ImGui::IsKeyDown(ImGuiKey_X)) d = 45 * dt; }
  if (app.rotSnap) { if (d != 0) lassoGroupMove(app, vec2(0), d, lassoParams().groupPolicy); return; }
  if (d != 0) {
    if (!gU.rotGesture) { if (!lassoGroupBegin(app)) return; gU.rotGesture = true; gU.rotAccum = 0; }
    gU.rotAccum += d; lassoGroupSet(app, vec2(0), gU.rotAccum);
  } else if (gU.rotGesture) { gU.rotGesture = false; lassoGroupEnd(app, true, lassoParams().groupPolicy); }
}

void lassoToolUpdate(App& app, float dt, bool uiMouse) {
  LassoParams& P = lassoParams(); LassoRegion& R = lassoRegion();
  ImGuiIO& io = ImGui::GetIO();
  bool down = glfwGetMouseButton(app.win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  bool press = down && !gU.prevDown; gU.prevDown = down;
  double mx, my; glfwGetCursorPos(app.win, &mx, &my); vec2 px((float)mx, (float)my);
  vec3 ro, rd; app.cursorRay(ro, rd);
  vec3 gp; bool g = app.terrain.raycast(ro, rd, 20000, gp);
  lassoSanitize(app);
  if (P.mode == LM_LINE) { gU.prevDown = down; lassoLineUpdate(app, uiMouse); return; }   // LN1: drawing and editing lines
  if (!io.WantCaptureKeyboard) {
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && lassoElevActive()) lassoElevApply(app, P.fillWater && P.amount < 0);
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && gU.gesture == 0) { if (lassoElevActive()) lassoElevCancel(app); R.clear(); }
  }
  if (gU.gesture == 0 && press && !uiMouse && !app.looking && g) {
    gU.addTo = io.KeyShift; gU.subtract = io.KeyAlt && !io.KeyShift;
    gU.pressPx = gU.lastPx = px; gU.travel = 0; gU.pressGround = gp; gU.dragYaw = 0;
    float t; uint64_t id = P.mode == LM_OBJECTS ? app.pickInstance(ro, rd, t) : 0;
    bool onSel = id && std::find(app.selection.begin(), app.selection.end(), id) != app.selection.end() && !gU.addTo && !gU.subtract;
    if (onSel) gU.gesture = 2;
    else if (P.mode == LM_MOVELAND && !R.empty() && R.contains(vec2(gp.x, gp.z)) && !gU.addTo && !gU.subtract) { gU.gesture = 4; gU.landDelta = vec2(0); gU.landYaw = 0; }
    else { gU.gesture = 1; gU.stroke = {vec2(gp.x, gp.z)}; gU.circleR = 0; }
  }
  if (gU.gesture == 1) {
    if (down) {
      gU.travel = std::max(gU.travel, glm::length(px - gU.pressPx));
      if (P.circle) { if (g) gU.circleR = glm::length(vec2(gp.x, gp.z) - gU.stroke[0]); }
      else if (g && glm::length(px - gU.lastPx) >= 6.0f) { gU.stroke.push_back(vec2(gp.x, gp.z)); gU.lastPx = px; }
    } else { finishShape(app); gU.gesture = 0; }
  }
  if (gU.gesture == 2 || gU.gesture == 3) {
    if (gU.gesture == 2 && down && glm::length(px - gU.pressPx) > 4.0f) { if (lassoGroupBegin(app)) gU.gesture = 3; else gU.gesture = 0; }
    if (gU.gesture == 3 && down) {
      if (!io.WantCaptureKeyboard) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) gU.dragYaw -= app.rotSnap ? 15.0f : 5.0f;
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) gU.dragYaw += app.rotSnap ? 15.0f : 5.0f;
      }
      if (g) {
        vec2 d(gp.x - gU.pressGround.x, gp.z - gU.pressGround.z);
        if (app.gridSnap) d = glm::round(d);
        lassoGroupSet(app, d, gU.dragYaw);
      }
    }
    if (!down) {
      if (gU.gesture == 3) lassoGroupEnd(app, true, P.groupPolicy);
      else {   // a click on a member: make it the primary
        float t; vec3 o, r; app.cursorRay(o, r);
        if (uint64_t id = app.pickInstance(o, r, t)) app.selected = id;
      }
      gU.gesture = 0;
    }
  }
  if (gU.gesture == 4) {
    if (down) {
      if (!io.WantCaptureKeyboard) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) gU.landYaw -= 15.0f;
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) gU.landYaw += 15.0f;
      }
      if (g) { gU.landDelta = vec2(gp.x - gU.pressGround.x, gp.z - gU.pressGround.z); if (app.gridSnap) gU.landDelta = glm::round(gU.landDelta); }
    } else {
#ifdef LASSO_HAS_LAND
      if (glm::length(gU.landDelta) > 0.25f || gU.landYaw != 0) {
        LandMoveReport rep = lassoMoveLand(app, R, gU.landDelta, gU.landYaw, P.moveFalloff);
        if (!rep.ok) app.toast("Move land refused: " + rep.why, 4);
        else { vec2 lo, hi; R.bounds(lo, hi); R = R.moved(gU.landDelta, gU.landYaw, (lo + hi) * 0.5f); }   // the region follows the land
      }
#endif
      gU.gesture = 0;
    }
  }
  // the elevation preview follows the panel (throttled: each preview re-meshes the touched tiles)
  gU.sinceElev += dt;
  if (gU.elevDirty && lassoElevActive() && gU.sinceElev > 0.12f) { lassoElevPreview(app, elevParams()); gU.elevDirty = false; gU.sinceElev = 0; }
}

void lassoOverlay(App& app, Overlay& ov) {
  lassoSanitize(app);
  const Terrain& T = app.terrain;
  vec4 selc(1.0f, 0.65f, 0.15f, 1.0f), bad(1.0f, 0.3f, 0.25f, 1.0f);
  // the group (shown in every tool while it exists)
  if (app.selection.size() > 1 || (app.tool == Tool::Lasso && !app.selection.empty())) {
    vec4 c = lassoGroupActive() && !app.lastCheck.ok ? vec4(1.0f, 0.85f, 0.3f, 1.0f) : selc;
    for (uint64_t id : app.selection) {
      Instance* i = app.doc.find(id); if (!i) continue;
      const AssetDef* a = app.assetFor(*i);
      ov.box(app.instanceMatrix(*i), a->bmin - vec3(0.05f), a->bmax + vec3(0.05f), lassoGroupActive() && lassoGroupIsBlocked(id) ? bad : c);
    }
    vec3 ce = lassoGroupCentre(app); ce.y = T.height(ce.x, ce.z);
    ov.line(ce, ce + vec3(0, 2.5f, 0), c, true);
    ov.line(ce - vec3(1, 0, 0), ce + vec3(1, 0, 0), c, true); ov.line(ce - vec3(0, 0, 1), ce + vec3(0, 0, 1), c, true);
  }
#if !__has_include("water.h")
  for (auto& w : app.doc.waterBodies) {   // until V1's water pass is in this build: a translucent sheet at the level
    std::vector<vec2> tri = geom2d::triangulate(w.outline);
    vec4 wc(0.25f, 0.5f, 0.75f, 0.35f);
    for (size_t k = 0; k + 2 < tri.size(); k += 3)
      ov.tri(vec3(tri[k].x, w.level, tri[k].y), vec3(tri[k + 1].x, w.level, tri[k + 1].y), vec3(tri[k + 2].x, w.level, tri[k + 2].y), wc);
  }
#endif
  if (app.tool != Tool::Lasso) return;
  const LassoParams& P = lassoParams(); const LassoRegion& R = lassoRegion();
  if (P.mode == LM_LINE) { lassoLineOverlay(app, ov); return; }
  for (auto& s : R.shapes) drapedLoop(app, ov, s.poly, s.subtract ? vec4(1, 0.35f, 0.3f, 0.95f) : vec4(0.35f, 0.9f, 1.0f, 0.95f));
  for (auto& w : app.doc.waterBodies) drapedLoop(app, ov, w.outline, vec4(0.3f, 0.55f, 1.0f, 0.8f), 0.05f);
  if (gU.gesture == 1) {
    vec4 c(1.0f, 0.9f, 0.3f, 1.0f);
    if (P.circle && !gU.stroke.empty()) drapedLoop(app, ov, geom2d::circle(gU.stroke[0], std::max(0.1f, gU.circleR), 48), c);
    else drapedLoop(app, ov, gU.stroke, c, 0.15f, false);
  }
  if (gU.gesture == 4 && !R.empty()) {   // move land: where the patch lands
    vec2 lo, hi; R.bounds(lo, hi);
    LassoRegion m = R.moved(gU.landDelta, gU.landYaw, (lo + hi) * 0.5f);
    for (auto& s : m.shapes) drapedLoop(app, ov, s.poly, vec4(1.0f, 0.85f, 0.35f, 0.95f), 0.3f);
  }
}

void lassoToolbar(App& app) {
  bool active = app.tool == Tool::Lasso;
  if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.50f, 0.25f, 1));
  if (ImGui::Button("Lasso (L)")) { if (active) app.tool = Tool::Select; else { app.tool = Tool::Lasso; app.placing = nullptr; } }
  if (active) ImGui::PopStyleColor();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Draw a loop or a circle on the map: select and move objects, raise or lower ground, fill hollows with water, paint ground types, move land");
  ImGui::SameLine();
}

void lassoEditorUI(App& app) {
  ImGuiIO& io = ImGui::GetIO();
  if (!io.WantCaptureKeyboard && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L, false) && app.mode == Mode::Creative) {
    app.tool = app.tool == Tool::Lasso ? Tool::Select : Tool::Lasso; app.placing = nullptr;
  }
  if (app.tool != Tool::Lasso) { if (lassoElevActive()) lassoElevCancel(app); return; }
  LassoParams& P = lassoParams(); LassoRegion& R = lassoRegion();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 330, 70), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
  ImGui::Begin("Lasso");
  int prevMode = P.mode;
  for (int m = 0; m < LM_COUNT; ++m) { if (m) ImGui::SameLine(); if (ImGui::RadioButton(lassoModeName(m), P.mode == m)) P.mode = m; }
  if (P.mode != prevMode && lassoElevActive()) lassoElevCancel(app);
  if (ImGui::RadioButton("Freehand", !P.circle)) P.circle = false;
  ImGui::SameLine(); if (ImGui::RadioButton("Circle", P.circle)) P.circle = true;
  ImGui::TextDisabled("LMB draw  |  Shift add  |  Alt subtract  |  Backspace clear");
  vec2 lo, hi; bool has = R.bounds(lo, hi);
  if (has) { ImGui::TextDisabled("Region: %zu shape(s), %.0f x %.0f m", R.shapes.size(), hi.x - lo.x, hi.y - lo.y); ImGui::SameLine(); if (ImGui::SmallButton("Clear")) { if (lassoElevActive()) lassoElevCancel(app); R.clear(); } }
  ImGui::Separator();
  if (P.mode == LM_OBJECTS) {
    ImGui::Checkbox("Touching counts (any footprint corner)", &P.touching);
    ImGui::SetNextItemWidth(170); ImGui::Combo("If some can't move", &P.groupPolicy, "Move the rest\0Cancel the move\0Place anyway\0");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move the rest: blocked pieces stay where they were.\nCancel: nothing moves.\nPlace anyway: blocked pieces move too, sitting on the ground (slope / overlap rules ignored).");
    ImGui::Text("%zu selected", app.selection.size());
    if (!app.selection.empty()) {
      std::map<std::string, int> counts;
      for (uint64_t id : app.selection) if (Instance* i = app.doc.find(id)) counts[app.assetFor(*i)->name]++;
      int shown = 0;
      for (auto& [n, c] : counts) { if (shown++ == 6) { ImGui::TextDisabled("  ..."); break; } ImGui::TextDisabled("  %d x %s", c, n.c_str()); }
      if (ImGui::Button("Turn -15")) lassoGroupMove(app, vec2(0), -15, P.groupPolicy);
      ImGui::SameLine(); if (ImGui::Button("Turn +15")) lassoGroupMove(app, vec2(0), 15, P.groupPolicy);
      ImGui::SameLine(); if (ImGui::Button("Duplicate")) lassoDuplicateGroup(app);
      ImGui::SameLine(); if (ImGui::Button("Delete")) lassoDeleteGroup(app);
      if (has && ImGui::Button("Reselect in region")) lassoSelectObjects(app, R, 0, P.touching);
      ImGui::SameLine(); if (ImGui::Button("Deselect")) { app.selection.clear(); app.selected = 0; }
    }
    ImGui::TextDisabled("Drag a selected object to move the group.\nZ / X turn it about its centre. Del / Ctrl+D.");
  } else if (P.mode == LM_ELEVATION) {
    bool ch = false;
    ch |= ImGui::SliderFloat("Raise / lower (m)", &P.amount, -40.0f, 40.0f, "%.2f");
    ch |= ImGui::SliderFloat("Falloff width (m)", &P.falloff, 0.0f, 60.0f, "%.1f");
    if (ImGui::RadioButton("Plateau", P.profile == 0)) { P.profile = 0; ch = true; }
    ImGui::SameLine(); if (ImGui::RadioButton("Dome / bowl", P.profile == 1)) { P.profile = 1; ch = true; }
    ImGui::SameLine(); if (ImGui::Checkbox("Linear edge", &P.linearEdge)) ch = true;
    if (ch) gU.elevDirty = true;
    ImGui::BeginDisabled(P.amount >= 0);
    ImGui::Checkbox("Fill with water up to the lip", &P.fillWater);
    ImGui::EndDisabled();
    if (lassoElevActive()) {
      if (ImGui::Button("Apply (Enter)")) { if (gU.elevDirty) lassoElevPreview(app, elevParams()); gU.elevDirty = false; lassoElevApply(app, P.fillWater && P.amount < 0); }
      ImGui::SameLine(); if (ImGui::Button("Cancel")) lassoElevCancel(app);
      ImGui::TextDisabled("Previewing: the edit is one undo step once applied.");
    } else if (has) {
      if (ImGui::Button("Edit region again")) { if (lassoElevBegin(app, R)) lassoElevPreview(app, elevParams()); }
    } else ImGui::TextDisabled("Draw a region to start.");
    ImGui::Separator();
    ImGui::BeginDisabled(!has || lassoElevActive());
    if (ImGui::Button("Fill hollow with water")) { WaterFill f = lassoFillWater(app, R); if (f.ok) app.toast("Water at " + std::to_string(f.level).substr(0, 6) + " m (the lip).", 3); else app.toast("No water: " + f.why, 4); }
    ImGui::SameLine(); if (ImGui::Button("Remove water")) { int n = lassoRemoveWater(app, R); app.toast(n ? "Removed " + std::to_string(n) + " water bodies." : "No water body starts in the region.", 3); }
    ImGui::EndDisabled();
    ImGui::TextDisabled("%zu water bodies in the map", app.doc.waterBodies.size());
  } else if (P.mode == LM_PAINT) {
#ifdef LASSO_HAS_PAINT
    groundPaintPicker(P.paintType);
    ImGui::SliderFloat("Strength", &P.paintStrength, 0.05f, 1.0f, "%.2f");
    ImGui::SliderFloat("Edge blend (m)", &P.paintFeather, 0.0f, 10.0f, "%.1f");
    ImGui::BeginDisabled(!has);
    if (ImGui::Button("Paint region again")) groundPaintRegion(app, R, P.paintType, P.paintStrength, P.paintFeather);
    ImGui::EndDisabled();
    ImGui::TextDisabled("Each shape you draw is painted (one undo step).\n\"Grass\" erases paint.");
#else
    ImGui::TextDisabled("Ground paint is not in this build.");
#endif
  } else if (P.mode == LM_LINE) {
    lassoLinePanel(app);
  } else if (P.mode == LM_MOVELAND) {
    ImGui::SliderFloat("Blend width (m)", &P.moveFalloff, 0.0f, 30.0f, "%.1f");
    ImGui::TextDisabled("Draw a region, then drag inside it to slide the\nground (with its objects, paint and water) to a\nnew spot. Z / X turn it while dragging.");
  }
  ImGui::End();
}
