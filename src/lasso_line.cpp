// LN1 lasso Line mode (docs/lines.md, docs/lasso.md): draw a line (click points or sketch freehand), pick its type and
// params, edit existing lines (drag, insert and delete points), split and delete them. The operations (lineCreate ...)
// are what the self-test drives; each is one undo step.
#include "lasso.h"
#include "lines.h"
#include <GLFW/glfw3.h>
#include <imgui.h>

namespace {
struct LineUi {
  std::vector<vec2> pts;          // the line being drawn
  bool drawing = false, sketch = false, prevDown = false;
  vec2 pressPx{0}, lastPx{0};
  uint64_t sel = 0;               // the line being edited
  int dragPt = -1;                // the control point being dragged (-1 none)
  bool dragMoved = false;
  std::string type = "trench";
  int spanKind = 0; float spanW[3] = {3.0f, 3.8f, 3.8f};   // LN2: Alt+click on the selected line: 0 gap, 1 its gate
  json params = json::object();   // the panel's params for new lines
  double lastClick = 0;
} gL;

float polyDist(vec2 p, const std::vector<LineSample>& sp, float* sAt = nullptr) {
  float best = 1e30f;
  for (size_t k = 0; k + 1 < sp.size(); ++k) {
    vec2 a = sp[k].p, ab = sp[k + 1].p - a; float L2 = glm::dot(ab, ab), t = L2 > 1e-9f ? std::clamp(glm::dot(p - a, ab) / L2, 0.0f, 1.0f) : 0.0f;
    float d = glm::length(p - (a + ab * t));
    if (d < best) { best = d; if (sAt) *sAt = sp[k].s + t * (sp[k + 1].s - sp[k].s); }
  }
  return best;
}
// a line end that lands within `r` of another line of the same type snaps onto its curve (the two then join)
vec2 snapEnd(const App& app, vec2 p, const std::string& type, uint64_t skip, float r = 2.0f) {
  vec2 best = p; float bd = r;
  for (auto& o : app.doc.instances) {
    if (o.id == skip) continue;
    const LineType* t = lineTypeOf(app.assetFor(o)); LineData d;
    if (!t || t->id != type || !lineGet(o, d)) continue;
    auto sp = lineSpline(d, 0.5f);
    for (auto& q : sp) { float dd = glm::length(q.p - p); if (dd < bd) { bd = dd; best = q.p; } }
  }
  return best;
}
}   // namespace

// ---------------------------------------------------------------- operations (one undo step each)
uint64_t lineCreate(App& app, const std::string& typeId, std::vector<vec2> pts, const json& params, uint32_t seed) {
  const LineType* t = lineType(typeId);
  const AssetDef* a = t ? app.lib.find(t->asset) : nullptr;
  if (!a || pts.size() < 2) return 0;
  pts.front() = snapEnd(app, pts.front(), typeId, 0);
  pts.back() = snapEnd(app, pts.back(), typeId, 0);
  app.pushUndo();
  LineData d;
  for (auto& p : pts) if (d.pts.empty() || glm::length(d.pts.back().p - p) > 0.2f) d.pts.push_back({p, 1.0f});
  if (d.pts.size() < 2) return 0;
  d.seed = seed ? seed : (uint32_t)(app.doc.nextId * 2654435761u);
  d.params = params.is_object() ? params : json::object();
  Instance i; i.id = app.doc.newId(); i.assetId = a->id; i.assetVersion = a->version;
  lineSet(i, d);
  i.pos.y = app.terrain.height(i.pos.x, i.pos.z);
  app.bump(i);
  app.doc.instances.push_back(i);
  return i.id;
}
bool lineSetData(App& app, uint64_t id, const LineData& d, bool undo) {
  Instance* i = app.doc.find(id); if (!i || d.pts.size() < 2) return false;
  if (undo) app.pushUndo();
  i = app.doc.find(id);
  lineSet(*i, d); i->pos.y = app.terrain.height(i->pos.x, i->pos.z);
  app.bump(*i);
  return true;
}
bool lineInsertPoint(App& app, uint64_t id, vec2 at) {   // a new control point where `at` falls on the curve
  Instance* i = app.doc.find(id); LineData d;
  if (!i || !lineGet(*i, d)) return false;
  float best = 1e30f; size_t k0 = 0;
  for (size_t k = 0; k + 1 < d.pts.size(); ++k) {
    vec2 a = d.pts[k].p, ab = d.pts[k + 1].p - a; float L2 = glm::dot(ab, ab), t = L2 > 1e-9f ? std::clamp(glm::dot(at - a, ab) / L2, 0.0f, 1.0f) : 0.0f;
    float dd = glm::length(at - (a + ab * t)); if (dd < best) { best = dd; k0 = k; }
  }
  d.pts.insert(d.pts.begin() + k0 + 1, {at, glm::mix(d.pts[k0].d, d.pts[k0 + 1].d, 0.5f)});
  return lineSetData(app, id, d, true);
}
bool lineDeletePoint(App& app, uint64_t id, int k) {
  Instance* i = app.doc.find(id); LineData d;
  if (!i || !lineGet(*i, d) || d.pts.size() <= 2 || k < 0 || k >= (int)d.pts.size()) return false;
  d.pts.erase(d.pts.begin() + k);
  return lineSetData(app, id, d, true);
}
uint64_t lineSplit(App& app, uint64_t id, int k) {   // at control point k (an inner one): two lines sharing that point
  Instance* i = app.doc.find(id); LineData d;
  if (!i || !lineGet(*i, d) || k <= 0 || k >= (int)d.pts.size() - 1) return 0;
  app.pushUndo();
  LineData a = d, b = d;
  a.pts.assign(d.pts.begin(), d.pts.begin() + k + 1);
  b.pts.assign(d.pts.begin() + k, d.pts.end()); b.seed = d.seed * 2654435761u + 1;
  i = app.doc.find(id);
  Instance nb = *i; nb.id = app.doc.newId();
  lineSet(*i, a); app.bump(*i);
  lineSet(nb, b); nb.pos.y = app.terrain.height(nb.pos.x, nb.pos.z); app.bump(nb);
  app.doc.instances.push_back(nb);
  return nb.id;
}
bool lineDelete(App& app, uint64_t id) {
  if (!app.doc.find(id)) return false;
  app.pushUndo();
  auto& in = app.doc.instances;
  in.erase(std::remove_if(in.begin(), in.end(), [&](const Instance& i) { return i.id == id; }), in.end());
  if (gL.sel == id) gL.sel = 0;
  return true;
}

// ---------------------------------------------------------------- editor input / overlay / panel (lasso_ui.cpp calls these)
void lassoLineUpdate(App& app, bool uiMouse) {
  ImGuiIO& io = ImGui::GetIO();
  bool down = glfwGetMouseButton(app.win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  bool press = down && !gL.prevDown, release = !down && gL.prevDown; gL.prevDown = down;
  double mx, my; glfwGetCursorPos(app.win, &mx, &my); vec2 px((float)mx, (float)my);
  vec3 ro, rd; app.cursorRay(ro, rd);
  vec3 gp; bool g = app.terrain.raycast(ro, rd, 20000, gp);
  vec2 at(gp.x, gp.z);
  if (gL.sel && !app.doc.find(gL.sel)) gL.sel = 0;
  auto finish = [&]() {
    if (gL.pts.size() >= 2) {
      uint64_t id = lineCreate(app, gL.type, gL.pts, gL.params, 0);
      if (id) { gL.sel = id; app.toast("Line drawn (" + std::to_string(gL.pts.size()) + " points).", 2); }
    }
    gL.pts.clear(); gL.drawing = false; gL.sketch = false;
  };
  if (!io.WantCaptureKeyboard) {
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && gL.drawing) finish();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { gL.pts.clear(); gL.drawing = false; gL.dragPt = -1; }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
      if (gL.drawing && !gL.pts.empty()) { gL.pts.pop_back(); if (gL.pts.empty()) gL.drawing = false; }
      else if (gL.sel && gL.dragPt >= 0) { lineDeletePoint(app, gL.sel, gL.dragPt); gL.dragPt = -1; }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && gL.sel && !gL.drawing) lineDelete(app, gL.sel);
  }
  if (uiMouse || app.looking || !g) return;
  if (press) {
    gL.pressPx = gL.lastPx = px;
    bool dbl = glfwGetTime() - gL.lastClick < 0.3; gL.lastClick = glfwGetTime();
    if (gL.drawing) {
      if (dbl) { finish(); return; }
      gL.pts.push_back(at);
      return;
    }
    // editing: a handle of the selected line, Ctrl+click inserts a point on it, a click on another line selects it
    if (gL.sel) {
      Instance* i = app.doc.find(gL.sel); LineData d;
      if (i && lineGet(*i, d)) {
        for (size_t k = 0; k < d.pts.size(); ++k) if (glm::length(d.pts[k].p - at) < 1.2f) { gL.dragPt = (int)k; gL.dragMoved = false; return; }
        if (io.KeyCtrl && polyDist(at, lineSpline(d)) < 1.5f) { lineInsertPoint(app, gL.sel, at); return; }
        if (io.KeyAlt && polyDist(at, lineSpline(d)) < 1.5f) {   // LN2: a gate or gap there (or remove the one clicked)
          extern bool lineInsertSpan(App&, uint64_t, vec2, const std::string&, float);
          const LineType* t = lineTypeOf(app.assetFor(*i)); std::string gk = "gap";
          if (gL.spanKind == 1 && t) for (auto& g : t->raw.value("gates", json::array())) gk = g == "drs_gate_piers" ? "gate_piers" : "field_gate";
          app.pushUndo();
          if (!lineInsertSpan(app, gL.sel, at, gk, gL.spanW[gk == "gap" ? 0 : 1])) app.toast("No room for a gate or gap there.", 2);
          return;
        }
      }
    }
    for (auto& o : app.doc.instances) {
      LineData d;
      if (!isLineAsset(app.assetFor(o)) || !lineGet(o, d)) continue;
      if (polyDist(at, lineSpline(d)) < 1.5f) { gL.sel = o.id; gL.dragPt = -1; return; }
    }
    gL.sel = 0; gL.drawing = true; gL.sketch = false; gL.pts = {at};
    return;
  }
  if (down && gL.drawing && glm::length(px - gL.pressPx) > 6.0f) {   // held and dragged: sketch freehand (a point every 2 m)
    gL.sketch = true;
    if (glm::length(at - gL.pts.back()) >= 2.0f) gL.pts.push_back(at);
  }
  if (release && gL.drawing && gL.sketch) finish();   // a sketched line ends on release
  if (down && gL.sel && gL.dragPt >= 0 && glm::length(px - gL.pressPx) > 4.0f) {   // drag a handle: live (one undo step)
    Instance* i = app.doc.find(gL.sel); LineData d;
    if (i && lineGet(*i, d) && gL.dragPt < (int)d.pts.size()) {
      bool endPt = gL.dragPt == 0 || gL.dragPt == (int)d.pts.size() - 1;
      d.pts[gL.dragPt].p = endPt ? snapEnd(app, at, lineTypeOf(app.assetFor(*i))->id, gL.sel) : at;
      lineSetData(app, gL.sel, d, !gL.dragMoved); gL.dragMoved = true;
    }
  }
  if (release && gL.dragPt >= 0 && gL.dragMoved) gL.dragPt = -1;
}

void lassoLineOverlay(App& app, Overlay& ov) {
  const Terrain& T = app.terrain;
  auto drape = [&](const std::vector<vec2>& p, vec4 c, float lift) {
    for (size_t k = 0; k + 1 < p.size(); ++k) ov.line(vec3(p[k].x, T.height(p[k].x, p[k].y) + lift, p[k].y), vec3(p[k + 1].x, T.height(p[k + 1].x, p[k + 1].y) + lift, p[k + 1].y), c, true);
  };
  for (auto& o : app.doc.instances) {   // every line's centreline; the selected one with its handles and width
    LineData d; const LineType* t = lineTypeOf(app.assetFor(o));
    if (!t || !lineGet(o, d)) continue;
    std::vector<vec2> c; for (auto& q : lineSpline(d, 1.0f)) c.push_back(q.p);
    bool sel = o.id == gL.sel;
    drape(c, sel ? vec4(1, 0.8f, 0.2f, 1) : vec4(0.4f, 0.85f, 1, 0.7f), 0.3f);
    if (sel) for (size_t k = 0; k < d.pts.size(); ++k) {
      vec3 h(d.pts[k].p.x, T.height(d.pts[k].p.x, d.pts[k].p.y) + 0.3f, d.pts[k].p.y);
      vec4 hc = (int)k == gL.dragPt ? vec4(1, 0.3f, 0.2f, 1) : vec4(1, 1, 1, 1);
      ov.line(h - vec3(0.5f, 0, 0), h + vec3(0.5f, 0, 0), hc, true); ov.line(h - vec3(0, 0, 0.5f), h + vec3(0, 0, 0.5f), hc, true); ov.line(h, h + vec3(0, 1.5f, 0), hc, true);
    }
  }
  if (gL.drawing && !gL.pts.empty()) {   // the line being drawn: its smoothed curve and the width it will take
    LineData d; for (auto& p : gL.pts) d.pts.push_back({p, 1});
    vec3 ro, rd; app.cursorRay(ro, rd); vec3 gp;
    if (!gL.sketch && app.terrain.raycast(ro, rd, 20000, gp)) d.pts.push_back({vec2(gp.x, gp.z), 1});   // rubber band to the cursor
    if (d.pts.size() >= 2) {
      auto sp = lineSpline(d, 1.0f);
      std::vector<vec2> c, l, r;
      float hw = gL.type == "trench" ? T.sp.floorHalf + T.sp.parapetW : 1.0f;
      for (auto& q : sp) { vec2 n(-q.t.y, q.t.x); c.push_back(q.p); l.push_back(q.p + n * hw); r.push_back(q.p - n * hw); }
      drape(c, vec4(1, 0.9f, 0.3f, 1), 0.3f); drape(l, vec4(1, 0.9f, 0.3f, 0.5f), 0.2f); drape(r, vec4(1, 0.9f, 0.3f, 0.5f), 0.2f);
    }
  }
}

void lassoLinePanel(App& app) {
  const auto& types = lineTypes();
  if (types.empty()) { ImGui::TextDisabled("No line types (content/settings/line_types.json)."); return; }
  const LineType* cur = lineType(gL.type); if (!cur) { gL.type = types[0].id; cur = &types[0]; }
  if (ImGui::BeginCombo("Type", cur->name.c_str())) {
    for (auto& t : types) if (ImGui::Selectable(t.name.c_str(), t.id == gL.type)) { gL.type = t.id; gL.params = json::object(); }
    ImGui::EndCombo();
  }
  // params: the type's defaults, edited for new lines (or the selected line's own when one is selected)
  Instance* si = gL.sel ? app.doc.find(gL.sel) : nullptr; LineData sd; bool editSel = si && lineGet(*si, sd) && lineTypeOf(app.assetFor(*si)) == cur;
  json p = lineParams(*cur, editSel ? sd : LineData{{}, 1, gL.params, {}});
  bool changed = false;
  for (auto it = cur->params.begin(); it != cur->params.end(); ++it) {
    if (!it.key().empty() && it.key()[0] == '_') continue;
    std::string k = it.key(); json& v = p[k];
    if (v.is_boolean()) { bool b = v.get<bool>(); if (ImGui::Checkbox(k.c_str(), &b)) { v = b; changed = true; } }
    else if (v.is_number()) { float f = v.get<float>(); if (ImGui::DragFloat(k.c_str(), &f, 0.1f)) { v = f; changed = true; } }
    else if (v.is_string()) {
      char buf[64]; std::snprintf(buf, sizeof buf, "%s", v.get<std::string>().c_str());
      if (ImGui::InputText(k.c_str(), buf, sizeof buf, ImGuiInputTextFlags_EnterReturnsTrue)) { v = std::string(buf); changed = true; }
    }
  }
  if (cur->params.contains("_doc")) ImGui::TextWrapped("%s", cur->params["_doc"].get<std::string>().c_str());
  if (changed) { if (editSel) { sd.params = p; lineSetData(app, gL.sel, sd, true); } else gL.params = p; }
  ImGui::Separator();
  if (gL.drawing) {
    ImGui::Text("Drawing: %zu points", gL.pts.size());
    if (ImGui::Button("Finish (Enter)")) { uint64_t id = lineCreate(app, gL.type, gL.pts, gL.params, 0); if (id) gL.sel = id; gL.pts.clear(); gL.drawing = false; }
    ImGui::SameLine(); if (ImGui::Button("Cancel (Esc)")) { gL.pts.clear(); gL.drawing = false; }
  } else if (editSel) {
    ImGui::Text("Line #%llu: %zu points", (unsigned long long)gL.sel, sd.pts.size());
    ImGui::BeginDisabled(gL.dragPt <= 0 || gL.dragPt >= (int)sd.pts.size() - 1);
    if (ImGui::Button("Split at point")) { lineSplit(app, gL.sel, gL.dragPt); gL.dragPt = -1; }
    ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::BeginDisabled(gL.dragPt < 0 || sd.pts.size() <= 2);
    if (ImGui::Button("Delete point")) { lineDeletePoint(app, gL.sel, gL.dragPt); gL.dragPt = -1; }
    ImGui::EndDisabled();
    ImGui::SameLine(); if (ImGui::Button("Delete line")) lineDelete(app, gL.sel);
    if (cur->builder == "bays") {   // LN2 gates and gaps
      bool gate = cur->raw.contains("gates") && !cur->raw["gates"].empty();
      if (!gate) gL.spanKind = 0;
      ImGui::RadioButton("Gap", &gL.spanKind, 0);
      if (gate) { ImGui::SameLine(); ImGui::RadioButton(cur->raw["gates"][0] == "drs_gate_piers" ? "Gate piers" : "Field gate", &gL.spanKind, 1); }
      if (gL.spanKind == 0) { ImGui::SetNextItemWidth(120); ImGui::DragFloat("Gap width", &gL.spanW[0], 0.1f, 1.0f, 12.0f, "%.1f m"); }
      ImGui::TextDisabled("Alt+click on the line: add a %s there (Alt+click a gate or gap removes it). %zu on this line.", gL.spanKind ? "gate" : "gap", sd.spans.size());
    }
  }
  ImGui::TextDisabled("Click points, or hold and drag to sketch.\nDouble-click / Enter finish, Esc cancel,\nBackspace removes the last point. Click a line\nto edit: drag its handles, Ctrl+click inserts,\nBackspace / Del delete. An end on another\nline of the same type joins into it.");
}
