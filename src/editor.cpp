// Creative mode: flying camera, asset browser, placement, selection, transforms, undo, start marker.
#include "app.h"
#include "mpai.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>
#include <chrono>

extern double gScrollY;

static bool keyDown(GLFWwindow* w, int k) { return glfwGetKey(w, k) == GLFW_PRESS; }
static bool keyPressed(GLFWwindow* w, int k) {
  static std::map<int, bool> prev;
  bool d = keyDown(w, k), p = prev[k]; prev[k] = d; return d && !p;
}
static bool mousePressed(GLFWwindow* w, int b) {
  static std::map<int, bool> prev;
  bool d = glfwGetMouseButton(w, b) == GLFW_PRESS, p = prev[b]; prev[b] = d; return d && !p;
}
static ImTextureID tex(GLuint t) { return (ImTextureID)(intptr_t)t; }

void App::updateCreative(float dt) {
  ImGuiIO& io = ImGui::GetIO();
  bool uiMouse = io.WantCaptureMouse && !looking, uiKey = io.WantCaptureKeyboard;
  // ---- look (RMB held)
  bool rmb = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
  static double lmx = 0, lmy = 0;
  if (rmb && !looking && !uiMouse && !selftest) { looking = true; glfwGetCursorPos(win, &lmx, &lmy); glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED); }
  if (!rmb && looking) { looking = false; glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL); }
  if (looking) {
    double mx, my; glfwGetCursorPos(win, &mx, &my);
    camYaw -= (float)(mx - lmx) * 0.12f; camPitch -= (float)(my - lmy) * 0.12f;
    camPitch = std::clamp(camPitch, -89.0f, 89.0f); camYaw = wrapDeg(camYaw);
    lmx = mx; lmy = my;
  }
  // ---- fly
  bool ctrl = keyDown(win, GLFW_KEY_LEFT_CONTROL) || keyDown(win, GLFW_KEY_RIGHT_CONTROL);
  if (!uiKey && !ctrl && !selftest) {
    vec3 f = forward(camYaw, camPitch), r = glm::normalize(glm::cross(f, vec3(0, 1, 0)));
    vec3 mv(0);
    if (keyDown(win, GLFW_KEY_W)) mv += f;
    if (keyDown(win, GLFW_KEY_S)) mv -= f;
    if (keyDown(win, GLFW_KEY_D)) mv += r;
    if (keyDown(win, GLFW_KEY_A)) mv -= r;
    if (keyDown(win, GLFW_KEY_E)) mv.y += 1;
    if (keyDown(win, GLFW_KEY_Q)) mv.y -= 1;
    float sp = flySpeed * (keyDown(win, GLFW_KEY_LEFT_SHIFT) ? 4.0f : 1.0f);
    if (glm::length(mv) > 0) camPos += glm::normalize(mv) * sp * dt;
    float g = terrain.height(camPos.x, camPos.z) + 0.6f;
    camPos.y = std::max(camPos.y, g);
  }
  // ---- scroll: context-sensitive
  float scroll = (float)gScrollY; gScrollY = 0;
  if (uiMouse) scroll = 0;
  Instance* sel = doc.find(selected);
  if (scroll != 0) {
    float step = rotSnap ? 15.0f : 3.0f;
    if (tool == Tool::Place || tool == Tool::Marker) placeYaw = wrapDeg(placeYaw + scroll * step);
    else if (dragging && sel) {
      float ny = wrapDeg(sel->yaw + scroll * step);
      PlaceCheck c = checkPlacement(assetFor(*sel), sel->pos, ny, sel->id);
      lastCheck = c;
      if (c.ok) { sel->yaw = ny; sel->pos.y = c.y; bump(*sel); }
    } else flySpeed = std::clamp(flySpeed * std::pow(1.2f, scroll), 2.0f, 400.0f);
  }
  // ---- shortcuts
  if (!uiKey && !selftest) {
    if (!selftest && keyPressed(win, GLFW_KEY_F5)) { enterExplore(); return; }   // scripted runs ignore the real keyboard
    if (ctrl && keyPressed(win, GLFW_KEY_Z)) { if (keyDown(win, GLFW_KEY_LEFT_SHIFT)) redo(); else undo(); }
    if (ctrl && keyPressed(win, GLFW_KEY_Y)) redo();
    if (ctrl && keyPressed(win, GLFW_KEY_S)) { if (docPath.empty()) showSaveAs = true; else saveMapTo(docPath); }
    if (ctrl && keyPressed(win, GLFW_KEY_D)) duplicateSelected();
    if (ctrl && keyPressed(win, GLFW_KEY_N)) showNewMap = true;
    if (ctrl && keyPressed(win, GLFW_KEY_O)) showOpen = true;
    if (keyPressed(win, GLFW_KEY_DELETE)) deleteSelected();
    if (keyPressed(win, GLFW_KEY_ESCAPE)) { if (tool != Tool::Select) { tool = Tool::Select; placing = nullptr; } else selected = 0; }
    if (!ctrl && keyPressed(win, GLFW_KEY_G)) gridSnap = !gridSnap;
    if (!ctrl && keyPressed(win, GLFW_KEY_H)) gridOverlay = !gridOverlay;
    if (!ctrl && keyPressed(win, GLFW_KEY_M)) { tool = Tool::Marker; placing = nullptr; }
    if (!ctrl && keyPressed(win, GLFW_KEY_1)) { tool = Tool::Select; placing = nullptr; }
    if (keyPressed(win, GLFW_KEY_F3)) showStats = !showStats;
    // rotate selection with Z / X (15° steps when snapping; continuous when held otherwise)
    if (sel && !dragging && !ctrl) {
      float d = 0;
      if (rotSnap) { if (keyPressed(win, GLFW_KEY_Z)) d = -15; if (keyPressed(win, GLFW_KEY_X)) d = 15; }
      else { if (keyDown(win, GLFW_KEY_Z)) d = -45 * dt; if (keyDown(win, GLFW_KEY_X)) d = 45 * dt; }
      static bool rotGesture = false;
      if (d != 0) {
        float ny = wrapDeg(sel->yaw + d);
        PlaceCheck c = checkPlacement(assetFor(*sel), sel->pos, ny, sel->id);
        if (c.ok) { if (!rotGesture || rotSnap) pushUndo(); rotGesture = true; sel = doc.find(selected); sel->yaw = ny; sel->pos.y = c.y; bump(*sel); }
        else toast(c.reason, 2);
      } else rotGesture = false;
    }
  }
  // ---- ground under cursor
  vec3 ro, rd; cursorRay(ro, rd);
  hoverValid = terrain.raycast(ro, rd, 3000, hoverGround);
  vec3 hp = hoverGround;
  if (gridSnap) { hp.x = std::round(hp.x); hp.z = std::round(hp.z); }
  if (selftest) return;  // the scripted test drives tools directly
  bool lmbPress = !uiMouse && !looking && mousePressed(win, GLFW_MOUSE_BUTTON_LEFT);
  bool lmbDown = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

  if (tool == Tool::Place && placing) {
    if (hoverValid && placing->defense.kind == "sandbag") snapDefense(placing, hp, placeYaw);   // chain sandbag runs
    if (hoverValid && placing->path.on) snapPath(placing, hp, placeYaw);                          // paths join end to end
    if (hoverValid) lastCheck = checkPlacement(placing, hp, placeYaw, 0);
    if (lmbPress && hoverValid) {
      if (lastCheck.ok) { uint64_t id = placeInstance(placing, hp, placeYaw); if (id) selected = id; }
      else toast(lastCheck.reason);
    }
  } else if (tool == Tool::Marker) {
    if (lmbPress && hoverValid) {
      pushUndo();
      doc.start.set = true; doc.start.pos = vec3(hp.x, terrain.height(hp.x, hp.z), hp.z); doc.start.yaw = placeYaw; dirty = true;
      if (!startMarkerValid(doc.start.pos)) toast("Start marker is obstructed: the player capsule does not fit here.");
      tool = Tool::Select;
    }
  } else {
    if (lmbPress) {
      float t; uint64_t id = pickInstance(ro, rd, t);
      vec3 gp; bool g = terrain.raycast(ro, rd, 3000, gp);
      if (id && (!g || t < glm::length(gp - ro) + 0.01f)) {
        selected = id; dragPending = true;
        double mx, my; glfwGetCursorPos(win, &mx, &my); dragStartMouse = vec2(mx, my);
        Instance* s = doc.find(id);
        dragOffset = s->pos - hoverGround; dragOffset.y = 0;
      } else if (!editInteract) selected = 0;
    }
    if (dragPending && lmbDown) {
      double mx, my; glfwGetCursorPos(win, &mx, &my);
      if (!dragging && glm::length(vec2(mx, my) - dragStartMouse) > 4) {
        Instance* s = doc.find(selected);
        if (s) { pushUndo(); s = doc.find(selected); dragOrig = *s; dragging = true; }
      }
      if (dragging && hoverValid) {
        Instance* s = doc.find(selected);
        vec3 np = hoverGround + dragOffset;
        if (gridSnap) { np.x = std::round(np.x); np.z = std::round(np.z); }
        lastCheck = checkPlacement(assetFor(*s), np, s->yaw, s->id);
        s->pos = vec3(np.x, lastCheck.ok ? lastCheck.y : s->pos.y, np.z);
        bump(*s);
      }
    }
    if (!lmbDown) {
      if (dragging) {
        Instance* s = doc.find(selected);
        if (s && !lastCheck.ok) { *s = dragOrig; bump(*s); undoStack.pop_back(); toast("Move rejected: " + lastCheck.reason); }
      }
      dragging = false; dragPending = false;
    }
  }
}

// ------------------------------------------------------------------ overlays
static void drapedRect(Overlay& ov, const Terrain& t, vec3 c, vec2 h, float yawDeg, float y, vec4 col, bool fill) {
  quat q = yawQuat(yawDeg);
  vec3 p[4] = {c + q * vec3(-h.x, 0, -h.y), c + q * vec3(h.x, 0, -h.y), c + q * vec3(h.x, 0, h.y), c + q * vec3(-h.x, 0, h.y)};
  for (auto& v : p) v.y = std::max(y, t.height(v.x, v.z)) + 0.06f;
  for (int k = 0; k < 4; ++k) ov.line(p[k], p[(k + 1) % 4], col);
  if (fill) { vec4 f = col; f.a *= 0.22f; ov.tri(p[0], p[1], p[2], f); ov.tri(p[0], p[2], p[3], f); }
}
static void arrow(Overlay& ov, vec3 from, vec3 dir, float len, vec4 col, bool top = false) {
  vec3 to = from + dir * len;
  vec3 side = glm::normalize(glm::cross(dir, vec3(0, 1, 0))) * (len * 0.18f);
  ov.line(from, to, col, top);
  ov.line(to, to - dir * len * 0.25f + side, col, top);
  ov.line(to, to - dir * len * 0.25f - side, col, top);
}

void App::drawCreativeOverlay(Overlay& ov, std::vector<DrawItem>& ghosts) {
  // build area border
  float B = terrain.buildHalf;
  vec4 bc(0.95f, 0.85f, 0.45f, 0.8f);
  for (int side = 0; side < 4; ++side)
    for (float s = -B; s < B; s += 10) {
      auto P = [&](float u) { vec3 p = side == 0 ? vec3(u, 0, -B) : side == 1 ? vec3(B, 0, u) : side == 2 ? vec3(u, 0, B) : vec3(-B, 0, u); p.y = terrain.height(p.x, p.z) + 0.15f; return p; };
      ov.line(P(s), P(std::min(s + 10, B)), bc);
    }
  vec4 ok(0.35f, 0.95f, 0.45f, 1.0f), bad(1.0f, 0.3f, 0.25f, 1.0f), selc(1.0f, 0.65f, 0.15f, 1.0f);
  // placement preview
  if (tool == Tool::Place && placing && hoverValid) {
    vec3 hp = hoverGround; if (gridSnap) { hp.x = std::round(hp.x); hp.z = std::round(hp.z); }
    vec3 p(hp.x, lastCheck.ok ? lastCheck.y : terrain.height(hp.x, hp.z), hp.z);
    mat4 M = glm::translate(mat4(1), p) * glm::mat4_cast(yawQuat(placeYaw));
    vec4 tint = lastCheck.ok ? vec4(0.4f, 0.9f, 0.5f, 0.55f) : vec4(1.0f, 0.35f, 0.3f, 0.5f);
    ghosts.push_back({&placing->opaque, M, tint});
    ghosts.push_back({&placing->panelsIntact, M, tint});
    if (placing->model) {
      for (size_t k = 0; k < placing->model->parts.size(); ++k) R.worldModels.push_back({placing->model.get(), (int)k, M});
      ghosts.push_back({&placing->colliderGhost, M, vec4(vec3(tint), 0.22f)});
    }
    for (auto& d : placing->interactables)
      for (auto& lf : d.leaves)
        ghosts.push_back({&lf.opaque, M * glm::translate(mat4(1), lf.hinge) * glm::mat4_cast(glm::angleAxis(radians(lf.sign * (d.defaultOpen ? d.openAngle : 0)), d.axis)), tint});
    vec4 c = lastCheck.ok ? ok : bad;
    drapedRect(ov, terrain, p, placing->footprint * 0.5f, placeYaw, p.y, c, true);
    vec3 front = yawQuat(placeYaw) * vec3(0, 0, 1);
    arrow(ov, p + front * (placing->footprint.y * 0.5f) + vec3(0, 0.3f, 0), front, 3.0f, c, true);
  }
  // selection
  if (Instance* s = doc.find(selected)) {
    const AssetDef* a = assetFor(*s);
    mat4 M = instanceMatrix(*s);
    vec4 c = dragging && !lastCheck.ok ? bad : selc;
    ov.box(M, a->bmin - vec3(0.05f), a->bmax + vec3(0.05f), c);
    ov.box(M, a->bmin - vec3(0.05f), a->bmax + vec3(0.05f), vec4(c.r, c.g, c.b, 0.25f), true);
    drapedRect(ov, terrain, s->pos, a->footprint * 0.5f, s->yaw, s->pos.y, c, dragging);
    vec3 front = yawQuat(s->yaw) * vec3(0, 0, 1);
    arrow(ov, s->pos + front * (a->footprint.y * 0.5f) + vec3(0, 0.3f, 0), front, 2.5f, c, true);
    if (editInteract) {
      auto it = rt.find(s->id);
      for (size_t k = 0; k < a->interactables.size(); ++k) {
        auto& d = a->interactables[k];
        bool open = authoredOpen(*s, d);
        ov.box(M, d.apMin, d.apMax, open ? vec4(0.3f, 0.9f, 1.0f, 1) : vec4(1.0f, 0.5f, 0.9f, 1), true);
        (void)it;
      }
    }
  }
  // start marker
  auto marker = [&](vec3 p, float yaw, bool valid, float alpha) {
    vec4 c = valid ? vec4(0.3f, 0.75f, 1.0f, alpha) : vec4(1.0f, 0.25f, 0.2f, alpha);
    ov.capsule(p, Physics::standHeight(), Physics::radius(), c);
    vec3 eye = p + vec3(0, 1.62f, 0);
    arrow(ov, eye, forward(yaw, 0), 1.2f, c, true);
    ov.line(p, eye, c, true);
  };
  if (doc.start.set) marker(doc.start.pos, doc.start.yaw, startMarkerValid(doc.start.pos), 1.0f);
  if (tool == Tool::Marker && hoverValid) {
    vec3 hp = hoverGround; hp.y = terrain.height(hp.x, hp.z);
    marker(hp, placeYaw, startMarkerValid(hp), 0.6f);
  }
}

// ------------------------------------------------------------------ UI
static std::string fmtFoot(const AssetDef* a) {
  char b[96]; std::snprintf(b, sizeof b, "%.0fx%.0f m | %d floor%s%s", a->footprint.x, a->footprint.y, a->floors, a->floors == 1 ? "" : "s", a->enterable ? " | enterable" : "");
  return b;
}

void App::drawCreativeUI() {
  ImGuiIO& io = ImGui::GetIO();
  float menuH = 0;
  if (ImGui::BeginMainMenuBar()) {
    menuH = ImGui::GetWindowSize().y;
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("New map...", "Ctrl+N")) showNewMap = true;
      if (ImGui::MenuItem("Open...", "Ctrl+O")) showOpen = true;
      if (ImGui::MenuItem("Save", "Ctrl+S")) { if (docPath.empty()) showSaveAs = true; else saveMapTo(docPath); }
      if (ImGui::MenuItem("Save as...")) showSaveAs = true;
      ImGui::Separator();
      if (ImGui::MenuItem("Quit")) glfwSetWindowShouldClose(win, GLFW_TRUE);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undoStack.empty())) undo();
      if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !redoStack.empty())) redo();
      ImGui::Separator();
      if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, selected != 0)) duplicateSelected();
      if (ImGui::MenuItem("Delete", "Del", false, selected != 0)) deleteSelected();
      ImGui::Separator();
      if (ImGui::MenuItem("Clear applied damage", nullptr, false, true)) clearAppliedDamage();
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
      ImGui::MenuItem("Asset browser", nullptr, &showBrowser);
      ImGui::MenuItem("Grid overlay", "H", &gridOverlay);
      ImGui::MenuItem("Performance stats", "F3", &showStats);
      if (ImGui::BeginMenu("Grass")) {
        for (int q = 0; q < 4; ++q) if (ImGui::MenuItem(grassPreset((GrassQuality)q).name, nullptr, (int)grass.quality == q)) grass.quality = (GrassQuality)q;
        ImGui::Separator(); ImGui::MenuItem("Wind (grass and trees)", nullptr, &grass.wind);
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Sky")) {
        ImGui::MenuItem("Clouds", nullptr, &R.clouds);
        ImGui::SetNextItemWidth(160); ImGui::SliderFloat("Coverage", &R.cloudCover, 0.0f, 1.0f, "%.2f");
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("First person")) {
        ImGui::MenuItem("Show arms", nullptr, &vm.showArms);
        ImGui::MenuItem("Imported weapon models (restart)", nullptr, &useImportedModels);
        ImGui::EndMenu();
      }
      ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Explore  (F5)")) enterExplore();
    mpaiMenus(*this);   // Play (Singleplayer / Host LAN / Join LAN) and Scenario menus
    ImGui::SameLine(ImGui::GetWindowWidth() - 360);
    ImGui::TextDisabled("%s%s  |  %s", doc.name.c_str(), dirty ? " *" : "", docPath.empty() ? "unsaved" : docPath.filename().string().c_str());
    ImGui::EndMainMenuBar();
  }
  // ---- toolbar
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, menuH + 6), ImGuiCond_Always, ImVec2(0.5f, 0));
  ImGui::Begin("##toolbar", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
  auto toolBtn = [&](const char* label, bool active) {
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.50f, 0.25f, 1));
    bool r = ImGui::Button(label);
    if (active) ImGui::PopStyleColor();
    ImGui::SameLine(); return r;
  };
  if (toolBtn("Select (1)", tool == Tool::Select)) { tool = Tool::Select; placing = nullptr; }
  if (toolBtn("Explore start (M)", tool == Tool::Marker)) { tool = Tool::Marker; placing = nullptr; }
  if (toolBtn(gridSnap ? "Grid snap: on (G)" : "Grid snap: off (G)", gridSnap)) gridSnap = !gridSnap;
  if (toolBtn(rotSnap ? "Rotate: 15 deg" : "Rotate: free", rotSnap)) rotSnap = !rotSnap;
  if (toolBtn("Doors & windows", editInteract)) editInteract = !editInteract;
  if (toolBtn("Undo", false)) undo();
  if (toolBtn("Redo", false)) redo();
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.38f, 0.50f, 1));
  if (ImGui::Button("Explore (F5)")) enterExplore();
  ImGui::PopStyleColor();
  ImGui::End();

  // ---- asset browser
  if (showBrowser) {
    ImGui::SetNextWindowPos(ImVec2(8, menuH + 8), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, io.DisplaySize.y - menuH - 60), ImGuiCond_FirstUseEver);
    ImGui::Begin("Assets", &showBrowser);
    std::vector<std::string> cats = {"All"};
    for (auto& a : lib.assets) if (std::find(cats.begin(), cats.end(), a->category) == cats.end()) cats.push_back(a->category);
    browserCategory = std::min(browserCategory, (int)cats.size() - 1);
    if (ImGui::BeginCombo("Category", cats[browserCategory].c_str())) {
      for (int k = 0; k < (int)cats.size(); ++k) if (ImGui::Selectable(cats[k].c_str(), k == browserCategory)) browserCategory = k;
      ImGui::EndCombo();
    }
    static char filt[64] = "";
    ImGui::InputTextWithHint("##search", "Search...", filt, sizeof filt);
    ImGui::Checkbox("Enterable only", &browserEnterableOnly); ImGui::SameLine();
    ImGui::SetNextItemWidth(80); ImGui::SliderInt("Min floors", &browserMinFloors, 0, 3);
    ImGui::Separator();
    ImGui::BeginChild("##grid");
    float avail = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, (int)(avail / 132));
    int n = 0;
    for (auto& a : lib.assets) {
      if (browserCategory > 0 && a->category != cats[browserCategory]) continue;
      if (browserEnterableOnly && !a->enterable) continue;
      if (a->floors < browserMinFloors) continue;
      std::string lname = a->name; std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
      std::string lf = filt; std::transform(lf.begin(), lf.end(), lf.begin(), ::tolower);
      if (!lf.empty() && lname.find(lf) == std::string::npos) continue;
      if (n % cols) ImGui::SameLine();
      ImGui::BeginGroup();
      ImGui::PushID(a->id.c_str());
      bool active = tool == Tool::Place && placing == a.get();
      if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.6f, 0.2f, 1));
      if (ImGui::ImageButton("##thumb", tex(a->thumbnail), ImVec2(120, 120), ImVec2(0, 1), ImVec2(1, 0))) { tool = Tool::Place; placing = a.get(); selected = 0; }
      if (active) ImGui::PopStyleColor();
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s\n%s\n\n%s", a->name.c_str(), a->category.c_str(), fmtFoot(a.get()).c_str(), a->description.c_str());
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 124);
      ImGui::TextUnformatted(a->name.c_str());
      ImGui::TextDisabled("%s", a->category.c_str());
      ImGui::TextDisabled("%s", fmtFoot(a.get()).c_str());
      ImGui::PopTextWrapPos();
      ImGui::PopID();
      ImGui::EndGroup();
      ++n;
    }
    if (n == 0) ImGui::TextDisabled("No assets match the filter.");
    ImGui::EndChild();
    ImGui::End();
  }

  // ---- properties
  if (Instance* s = doc.find(selected)) {
    const AssetDef* a = assetFor(*s);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 330, menuH + 8), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Selection");
    ImGui::Text("%s", a->name.c_str());
    ImGui::TextDisabled("%s | instance #%llu | asset %s v%d", a->category.c_str(), (unsigned long long)s->id, s->assetId.c_str(), s->assetVersion);
    if (a->placeholder) ImGui::TextColored(ImVec4(1, 0.4f, 0.6f, 1), "Asset definition missing - data preserved.");
    ImGui::Separator();
    float xz[2] = {s->pos.x, s->pos.z}; float yaw = s->yaw;
    bool changed = false, activated = false;
    ImGui::DragFloat2("Position X/Z (m)", xz, 0.05f, -1000, 1000, "%.2f"); activated |= ImGui::IsItemActivated(); changed |= ImGui::IsItemEdited();
    ImGui::DragFloat("Rotation (deg)", &yaw, 0.5f, -720, 720, "%.1f"); activated |= ImGui::IsItemActivated(); changed |= ImGui::IsItemEdited();
    ImGui::TextDisabled("Height %.2f m (follows highest ground under footprint)", s->pos.y);
    static bool editing = false;
    if (activated) { pushUndo(); editing = true; s = doc.find(selected); }
    if (changed && s) {
      PlaceCheck c = checkPlacement(a, vec3(xz[0], 0, xz[1]), yaw, s->id);
      lastCheck = c;
      if (c.ok) { s->pos = vec3(xz[0], c.y, xz[1]); s->yaw = wrapDeg(yaw); bump(*s); }
    }
    if (editing && !ImGui::IsAnyItemActive()) editing = false;
    if (!lastCheck.ok && editing) ImGui::TextColored(ImVec4(1, 0.45f, 0.35f, 1), "%s", lastCheck.reason.c_str());
    if (s && a->path.on) {   // path size: texture scale stays fixed, so resizing never stretches the surface
      vec2 wl = pathSize(*s, a); float L = wl.y, W = wl.x; bool act = false, ed = false;
      ImGui::DragFloat("Length (m)", &L, 0.1f, a->path.minL, a->path.maxL, "%.1f"); act |= ImGui::IsItemActivated(); ed |= ImGui::IsItemEdited();
      ImGui::DragFloat("Width (m)", &W, 0.05f, a->path.minW, a->path.maxW, "%.2f"); act |= ImGui::IsItemActivated(); ed |= ImGui::IsItemEdited();
      if (act) { pushUndo(); s = doc.find(selected); }
      if (ed && s) setPathSize(*s, vec2(W, L));
      ImGui::TextDisabled("Placing a path next to a free end continues it in line.");
    }
    if (ImGui::Button("Duplicate (Ctrl+D)")) duplicateSelected();
    ImGui::SameLine();
    if (ImGui::Button("Delete (Del)")) { deleteSelected(); ImGui::End(); return; }
    s = doc.find(selected);
    if (s && !a->interactables.empty() && ImGui::CollapsingHeader("Doors & windows: initial state", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::TextDisabled("Authored defaults. Explore always starts from these.");
      for (auto& d : a->interactables) {
        bool open = authoredOpen(*s, d);
        std::string lbl = d.id + " (" + d.label + ")";
        if (ImGui::Checkbox(lbl.c_str(), &open)) {
          pushUndo(); s = doc.find(selected);
          s->openDefaults[d.id] = open; bump(*s);
        }
      }
    }
    if (s && !a->furnitureNames.empty() && ImGui::CollapsingHeader("Furniture (moves with the building)")) {
      for (auto& f : a->furnitureNames) ImGui::BulletText("%s", f.c_str());
    }
    ImGui::End();

    // clickable labels for door/window defaults in the 3D view
    if (editInteract && s) {
      ImDrawList* dl = ImGui::GetForegroundDrawList();
      mat4 vp = projM * viewM, M = instanceMatrix(*s);
      for (auto& d : a->interactables) {
        vec4 c = vp * (M * vec4((d.apMin + d.apMax) * 0.5f, 1));
        if (c.w <= 0.1f) continue;
        vec2 sp((c.x / c.w * 0.5f + 0.5f) * io.DisplaySize.x, (0.5f - c.y / c.w * 0.5f) * io.DisplaySize.y);
        bool open = authoredOpen(*s, d);
        std::string t = d.label + (open ? ": starts open" : ": starts closed");
        ImVec2 ts = ImGui::CalcTextSize(t.c_str());
        ImVec2 a0(sp.x - ts.x * 0.5f - 4, sp.y - ts.y * 0.5f - 2), a1(sp.x + ts.x * 0.5f + 4, sp.y + ts.y * 0.5f + 2);
        bool hov = io.MousePos.x >= a0.x && io.MousePos.x <= a1.x && io.MousePos.y >= a0.y && io.MousePos.y <= a1.y;
        dl->AddRectFilled(a0, a1, hov ? IM_COL32(90, 80, 30, 230) : IM_COL32(20, 20, 20, 190), 3);
        dl->AddText(ImVec2(a0.x + 4, a0.y + 2), open ? IM_COL32(120, 230, 255, 255) : IM_COL32(255, 150, 230, 255), t.c_str());
        if (hov && ImGui::IsMouseClicked(0) && !io.WantCaptureMouse) {
          pushUndo(); s = doc.find(selected);
          s->openDefaults[d.id] = !open; bump(*s);
        }
      }
    }
  }

  // ---- status bar
  ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y), ImGuiCond_Always, ImVec2(0, 1));
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 0));
  ImGui::Begin("##status", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
  if (tool == Tool::Place && placing) {
    ImGui::TextColored(lastCheck.ok ? ImVec4(0.5f, 1, 0.55f, 1) : ImVec4(1, 0.5f, 0.4f, 1), "Placing %s  |  yaw %.0f deg  |  %s", placing->name.c_str(), placeYaw, lastCheck.reason.c_str());
    ImGui::SameLine(); ImGui::TextDisabled("  LMB place (repeats)  |  wheel rotate  |  Esc cancel");
  } else if (tool == Tool::Marker) {
    ImGui::Text("Explore start: click the ground. Wheel sets facing (%.0f deg). Esc cancels.", placeYaw);
  } else if (dragging) {
    ImGui::TextColored(lastCheck.ok ? ImVec4(0.5f, 1, 0.55f, 1) : ImVec4(1, 0.5f, 0.4f, 1), "Moving  |  wheel rotates  |  %s", lastCheck.reason.c_str());
  } else {
    ImGui::TextDisabled("RMB+mouse look | WASD fly | Q/E down/up | Shift fast | wheel speed (%.0f m/s) | LMB select/drag | Z/X rotate | F5 explore", flySpeed);
  }
  ImGui::End();
  if (now < noticeUntil) {  // prominent banner under the toolbar
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 ts = ImGui::CalcTextSize(notice.c_str());
    ImVec2 p(io.DisplaySize.x * 0.5f - ts.x * 0.5f, menuH + 58);
    dl->AddRectFilled(ImVec2(p.x - 16, p.y - 9), ImVec2(p.x + ts.x + 16, p.y + ts.y + 9), IM_COL32(28, 24, 12, 235), 5);
    dl->AddRect(ImVec2(p.x - 16, p.y - 9), ImVec2(p.x + ts.x + 16, p.y + ts.y + 9), IM_COL32(230, 190, 90, 255), 5, 0, 1.5f);
    dl->AddText(p, IM_COL32(255, 228, 160, 255), notice.c_str());
  }

  if (showStats) {
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 260, io.DisplaySize.y - 140), ImGuiCond_Always);
    ImGui::Begin("##stats", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("frame %.2f ms (%.0f fps)", io.DeltaTime * 1000, io.Framerate);
    ImGui::Text("render cpu %.2f ms gpu %.2f ms", R.stats.cpuMs, R.stats.gpuMs);
    ImGui::Text("draws %d  tris %d", R.stats.draws, R.stats.tris);
    ImGui::Text("instances %zu  bodies %d", doc.instances.size(), Physics::bodyCount());
    ImGui::Text("grass %s: %zu blades drawn, %d chunks", grassPreset(grass.quality).name, grass.lastDrawnBlades, grass.chunkCount());
    ImGui::End();
  }

  // ---- modals
  if (showNewMap) { ImGui::OpenPopup("New map"); showNewMap = false; }
  ImGui::SetNextWindowSize(ImVec2(760, 0), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("New map", nullptr)) {
    ImGui::InputText("Map name", nameBuf, sizeof nameBuf);
    ImGui::Separator();
    for (int k = 0; k < (int)templates.size(); ++k) {
      auto& t = templates[k];
      ImGui::PushID(k);
      ImGui::BeginDisabled(!t.available);
      bool sel = newTemplateIdx == k;
      if (t.thumbnail) ImGui::Image(tex(t.thumbnail), ImVec2(160, 100), ImVec2(0, 0.8f), ImVec2(1, 0.2f));
      else ImGui::Dummy(ImVec2(160, 100));
      ImGui::SameLine();
      ImGui::BeginGroup();
      if (ImGui::Selectable(t.name.c_str(), sel, 0, ImVec2(540, 0))) newTemplateIdx = k;
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 540);
      ImGui::TextDisabled("%s", t.description.c_str());
      if (!t.available) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Not included in this build yet.");
      ImGui::PopTextWrapPos();
      ImGui::EndGroup();
      ImGui::EndDisabled();
      ImGui::PopID();
    }
    ImGui::Separator();
    bool canCreate = newTemplateIdx < (int)templates.size() && templates[newTemplateIdx].available;
    ImGui::BeginDisabled(!canCreate);
    if (ImGui::Button("Create editable copy")) { newMap(templates[newTemplateIdx], nameBuf); ImGui::CloseCurrentPopup(); }
    ImGui::EndDisabled();
    ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  if (showOpen) { ImGui::OpenPopup("Open map"); showOpen = false; }
  ImGui::SetNextWindowSize(ImVec2(620, 420), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("Open map", nullptr)) {
    struct Entry { fs::path p; std::string name, when; bool example; };
    std::vector<Entry> list;
    auto scan = [&](const fs::path& dir, bool ex) {
      if (!fs::exists(dir)) return;
      for (auto& e : fs::directory_iterator(dir)) {
        auto fn = e.path().filename().string();
        if (fn.size() < 10 || fn.substr(fn.size() - 10) != ".nmap.json") continue;
        Entry en{e.path(), fn, "", ex};
        try { en.name = json::parse(readTextFile(e.path())).value("name", fn); } catch (...) {}
        auto ft = fs::last_write_time(e.path());
        auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        std::time_t tt = std::chrono::system_clock::to_time_t(sys);
        char b[64]; std::strftime(b, sizeof b, "%Y-%m-%d %H:%M", std::localtime(&tt)); en.when = b;
        list.push_back(en);
      }
    };
    scan(userMapsDir(), false);
    scan(contentDir() / "examples", true);
    ImGui::BeginChild("##list", ImVec2(0, -40), true);
    if (list.empty()) ImGui::TextDisabled("No saved maps yet in %s", userMapsDir().string().c_str());
    for (auto& e : list) {
      std::string lbl = (e.example ? "[example] " : "") + e.name + "##" + e.p.string();
      if (ImGui::Selectable(lbl.c_str(), false, ImGuiSelectableFlags_DontClosePopups)) { openMap(e.p); ImGui::CloseCurrentPopup(); }
      ImGui::SameLine(420); ImGui::TextDisabled("%s  %s", e.when.c_str(), e.p.filename().string().c_str());
    }
    ImGui::EndChild();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  if (showSaveAs) { ImGui::OpenPopup("Save map as"); showSaveAs = false; std::snprintf(nameBuf, sizeof nameBuf, "%s", doc.name.c_str()); }
  if (ImGui::BeginPopupModal("Save map as", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::InputText("Map name", nameBuf, sizeof nameBuf);
    ImGui::InputText("File name", saveAsBuf, sizeof saveAsBuf);
    ImGui::TextDisabled("Saved to %s", (userMapsDir() / (std::string(saveAsBuf) + ".nmap.json")).string().c_str());
    if (ImGui::Button("Save") && saveAsBuf[0]) {
      doc.name = nameBuf;
      std::string fn = saveAsBuf;
      for (char& c : fn) if (std::strchr("\\/:*?\"<>|", c)) c = '_';
      saveMapTo(userMapsDir() / (fn + ".nmap.json"));
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}
