// "Drop in" explorer (SPEC 9.3, Street View style): a soldier figure in the editor toolbar is dragged onto the terrain.
// While dragging, a ground marker and a facing arrow follow the cursor; the direction the cursor moves over the ground
// sets the facing. Releasing saves the point as the map's explore start and enters first person there. In a session
// entered from Creative (drop-in or "Explore from start"), Esc returns to the editor camera exactly where it was and
// P opens the pause menu (the pause menu still offers "Return to Creative").
#include "editor_nav.h"
#include "app.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>

static bool kedge2(GLFWwindow* w, int k) { static std::map<int, bool> prev; bool d = glfwGetKey(w, k) == GLFW_PRESS, p = prev[k]; prev[k] = d; return d && !p; }

static void soldierIcon(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
  // helmeted rifleman: helmet brim, head, torso, legs, slung rifle
  dl->AddCircleFilled(ImVec2(c.x, c.y - 0.62f * s), 0.16f * s, col);
  dl->AddRectFilled(ImVec2(c.x - 0.27f * s, c.y - 0.66f * s), ImVec2(c.x + 0.27f * s, c.y - 0.58f * s), col, 0.05f * s);
  dl->AddRectFilled(ImVec2(c.x - 0.17f * s, c.y - 0.44f * s), ImVec2(c.x + 0.17f * s, c.y + 0.08f * s), col, 0.06f * s);
  dl->AddLine(ImVec2(c.x - 0.09f * s, c.y + 0.06f * s), ImVec2(c.x - 0.14f * s, c.y + 0.6f * s), col, 0.11f * s);
  dl->AddLine(ImVec2(c.x + 0.09f * s, c.y + 0.06f * s), ImVec2(c.x + 0.14f * s, c.y + 0.6f * s), col, 0.11f * s);
  dl->AddLine(ImVec2(c.x - 0.30f * s, c.y + 0.10f * s), ImVec2(c.x + 0.28f * s, c.y - 0.52f * s), col, 0.06f * s);
}

void w3Toolbar(App& app) {
  NavState& n = navState();
  float hgt = ImGui::GetFrameHeight();
  ImGui::InvisibleButton("##dropin", ImVec2(hgt * 1.1f, hgt));
  bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
  ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(a, b, ImGui::GetColorU32(act ? ImGuiCol_ButtonActive : hov ? ImGuiCol_ButtonHovered : ImGuiCol_Button), ImGui::GetStyle().FrameRounding);
  soldierIcon(dl, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f + hgt * 0.05f), hgt * 0.78f, n.dropDrag ? IM_COL32(255, 220, 120, 120) : IM_COL32(235, 225, 190, 255));
  if (hov && !n.dropDrag) ImGui::SetTooltip("Drop in: drag the soldier onto the terrain to explore from there.\nThe direction you drag over the ground sets the facing.\nThe drop point becomes the map's explore start. In first person: Esc returns here, P pauses.");
  if (act && !n.dropDrag && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f) && app.mode == Mode::Creative) {
    n.dropDrag = true; n.dropValid = false; n.dropFaceSet = false; n.dropYaw = app.camYaw;
  }
}

bool w3EnterExplore(App& app) {
  NavState& n = navState();
  navFinishFlight(app);
  n.creativeSession = true; n.escReturn = false;
  bool ok = app.enterExplore();
  if (!ok || app.mode != Mode::Explore) { n.creativeSession = false; return false; }
  if (!app.selftest) app.toast("Esc: back to the editor  |  P: pause menu  |  B: build with the Assets palette (map edits)", 4);
  return true;
}

void dropinDragUpdate(App& app, vec3 hit, bool valid) {
  NavState& n = navState();
  n.dropValid = valid;
  if (!valid) return;
  n.dropPos = vec3(hit.x, app.terrain.height(hit.x, hit.z), hit.z);
  if (!n.dropFaceSet) { n.dropFacePos = n.dropPos; n.dropFaceSet = true; return; }
  vec3 d = n.dropPos - n.dropFacePos; d.y = 0;
  if (glm::length(d) >= navSettings().facingMinDrag) { n.dropYaw = yawOfDir(d); n.dropFacePos = n.dropPos; }
}

bool dropinCommit(App& app, vec3 p, float yaw) {
  NavState& n = navState();
  if (app.mode != Mode::Creative) return false;
  navFinishFlight(app);
  n.dropSavedCam = {app.camPos, app.camYaw, app.camPitch};
  app.pushUndo();
  app.doc.start.set = true; app.doc.start.pos = vec3(p.x, app.terrain.height(p.x, p.z), p.z); app.doc.start.yaw = wrapDeg(yaw);
  app.dirty = true;
  return w3EnterExplore(app);
}

bool dropinReturn(App& app) {
  if (app.mode != Mode::Explore) return false;
  app.exitExplore();   // restores the editor camera saved by enterExplore (position, yaw, pitch)
  navState().creativeSession = false;
  return true;
}

void dropinUI(App& app) {
  NavState& n = navState();
  if (!n.dropDrag) return;
  ImGuiIO& io = ImGui::GetIO();
  if (app.mode != Mode::Creative || (!app.selftest && glfwGetKey(app.win, GLFW_KEY_ESCAPE) == GLFW_PRESS)) { n.dropDrag = false; return; }
  bool overUI = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
  if (!app.selftest) {
    vec3 ro, rd, hit;
    app.cursorRay(ro, rd);
    bool ok = !overUI && app.terrain.raycast(ro, rd, 20000.0f, hit) && app.terrain.inTerrain(hit.x, hit.z);
    dropinDragUpdate(app, hit, ok);
  }
  ImDrawList* fg = ImGui::GetForegroundDrawList();
  soldierIcon(fg, ImVec2(io.MousePos.x, io.MousePos.y - 18), 30, n.dropValid ? IM_COL32(120, 220, 255, 255) : IM_COL32(255, 120, 100, 220));
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !app.selftest) {
    n.dropDrag = false;
    if (n.dropValid && !overUI) dropinCommit(app, n.dropPos, n.dropYaw);
  }
}

void dropinOverlay(App& app, Overlay& ov) {
  NavState& n = navState();
  if (!n.dropDrag || !n.dropValid) return;
  const NavCamSettings& s = navSettings();
  vec4 c(0.35f, 0.85f, 1.0f, 1.0f);
  vec3 p = n.dropPos;
  for (int k = 0; k < 32; ++k) {   // draped ring
    float a0 = k * 6.2831853f / 32, a1 = (k + 1) * 6.2831853f / 32;
    vec3 q0 = p + vec3(std::cos(a0), 0, std::sin(a0)) * s.markerRadius, q1 = p + vec3(std::cos(a1), 0, std::sin(a1)) * s.markerRadius;
    q0.y = app.terrain.height(q0.x, q0.z) + 0.05f; q1.y = app.terrain.height(q1.x, q1.z) + 0.05f;
    ov.line(q0, q1, c, true);
  }
  ov.capsule(p, Physics::standHeight(), Physics::radius(), vec4(c.r, c.g, c.b, 0.55f));
  vec3 f = App::forward(n.dropYaw, 0), side = glm::normalize(glm::cross(f, vec3(0, 1, 0)));
  vec3 a = p + vec3(0, 0.08f, 0) + f * s.markerRadius, b = a + f * s.arrowLength;
  ov.line(a, b, c, true);
  ov.line(b, b - f * 0.8f + side * 0.45f, c, true);
  ov.line(b, b - f * 0.8f - side * 0.45f, c, true);
}

bool w3ExploreKeys(App& app) {
  NavState& n = navState();
  if (app.selftest || !n.creativeSession || app.mode != Mode::Explore) return false;
  bool esc = kedge2(app.win, GLFW_KEY_ESCAPE), pause = kedge2(app.win, GLFW_KEY_P);
  if (esc && !app.rackOpen && !app.build.palette) n.escReturn = true;   // an open rack / build palette closes first (explore.cpp)
  if (n.escReturn) {   // return on release, so the editor does not also see the press (it would drop the selection)
    if (glfwGetKey(app.win, GLFW_KEY_ESCAPE) != GLFW_PRESS) { n.escReturn = false; dropinReturn(app); }
    return true;
  }
  if (pause) {
    app.pauseMenu = !app.pauseMenu;
    app.syncCursor();
  }
  return false;
}
