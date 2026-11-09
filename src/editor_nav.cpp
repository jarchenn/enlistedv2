// Creative editor navigation (SPEC 9.1-9.2): map size + New Map dialog, terrain set-up, terrain cuts, the editor
// camera (wheel zoom toward the cursor, middle-drag pan, right-drag orbit, height-scaled WASD/QE, F / Home framing,
// camera bookmarks Ctrl+1-9 / 1-9) and the top-down overview map on M.
#include "editor_nav.h"
#include "app.h"
#include "scenario.h"
#include "snap.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>
#include <cstring>

const json& editorSettings();

const NavCamSettings& navSettings() {
  static NavCamSettings s = [] {
    NavCamSettings n;
    const json& j = editorSettings();
    if (j.contains("camera")) {
      const json& c = j["camera"];
      n.speedBase = c.value("speedBase", n.speedBase); n.speedPerMetre = c.value("speedPerMetre", n.speedPerMetre);
      n.speedMin = c.value("speedMin", n.speedMin); n.speedMax = c.value("speedMax", n.speedMax); n.shiftMult = c.value("shiftMult", n.shiftMult);
      n.zoomStep = c.value("zoomStep", n.zoomStep); n.zoomMinDist = c.value("zoomMinDist", n.zoomMinDist);
      n.orbitDegPerPixel = c.value("orbitDegPerPixel", n.orbitDegPerPixel); n.minClearance = c.value("minClearance", n.minClearance);
      n.frameMargin = c.value("frameMargin", n.frameMargin); n.flightSeconds = c.value("flightSeconds", n.flightSeconds); n.homePitch = c.value("homePitch", n.homePitch);
    }
    if (j.contains("overview")) { n.overviewTexture = j["overview"].value("textureSize", n.overviewTexture); n.overviewFraction = j["overview"].value("windowFraction", n.overviewFraction); }
    if (j.contains("terrainCut")) n.cutEdgePad = j["terrainCut"].value("edgePad", n.cutEdgePad);
    if (j.contains("dropIn")) {
      const json& d = j["dropIn"];
      n.facingMinDrag = d.value("facingMinDrag", n.facingMinDrag); n.markerRadius = d.value("markerRadius", n.markerRadius); n.arrowLength = d.value("arrowLength", n.arrowLength);
    }
    return n;
  }();
  return s;
}
NavState& navState() { static NavState s; return s; }

static const float kFovY = 60.0f;   // the creative projection (w3EditorProjection)

vec3 camForward(float yaw, float pitch) { return App::forward(yaw, pitch); }
float yawOfDir(vec3 d) { return wrapDeg(degrees(std::atan2(-d.x, -d.z))); }
static float pitchOfDir(vec3 d) { return degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z)))); }
static void basis(float yaw, float pitch, vec3& f, vec3& r, vec3& u) {
  f = camForward(yaw, pitch);
  r = glm::cross(f, vec3(0, 1, 0)); r = glm::length(r) < 1e-5f ? camForward(yaw - 90, 0) * -1.0f : glm::normalize(r);
  u = glm::cross(r, f);
}
float navAspect(const App& app) { return app.R.height > 0 ? (float)app.R.width / app.R.height : 16.0f / 9.0f; }

static void keepClear(App& app) {
  float g = app.terrain.height(app.camPos.x, app.camPos.z) + navSettings().minClearance;
  app.camPos.y = std::max(app.camPos.y, g);
}

float navFlySpeed(const App& app) {
  const NavCamSettings& s = navSettings();
  float hag = std::max(0.0f, app.camPos.y - app.terrain.height(app.camPos.x, app.camPos.z));
  return std::clamp(s.speedBase + hag * s.speedPerMetre, s.speedMin, s.speedMax);
}

// ------------------------------------------------------------------ gestures
static bool groundHit(const App& app, vec3 ro, vec3 rd, vec3& hit) { return app.terrain.raycast(ro, rd, 20000.0f, hit); }

static void rayFromCursor(const App& app, vec3& ro, vec3& rd) {
  double mx, my; glfwGetCursorPos(app.win, &mx, &my);
  int w, h; glfwGetWindowSize(app.win, &w, &h);
  vec2 ndc(2.0f * (float)mx / std::max(w, 1) - 1.0f, 1.0f - 2.0f * (float)my / std::max(h, 1));
  mat4 view = glm::lookAt(app.camPos, app.camPos + camForward(app.camYaw, app.camPitch), vec3(0, 1, 0));
  mat4 inv = glm::inverse(app.projM * view);
  vec4 a = inv * vec4(ndc, -1, 1), b = inv * vec4(ndc, 1, 1);
  ro = vec3(a) / a.w; rd = glm::normalize(vec3(b) / b.w - ro);
}

void navZoomAlongRay(App& app, float scroll, vec3 ro, vec3 rd) {
  const NavCamSettings& s = navSettings();
  vec3 hit;
  float dist;
  if (groundHit(app, ro, rd, hit)) dist = glm::length(hit - ro);
  else { dist = std::max(50.0f, (app.camPos.y - app.terrain.height(app.camPos.x, app.camPos.z)) * 2.0f); hit = ro + rd * dist; }
  float nd = dist * std::pow(1.0f - s.zoomStep, scroll);
  nd = std::max(nd, s.zoomMinDist);
  nd = std::min(nd, 12000.0f);
  app.camPos = hit - rd * nd + (app.camPos - ro);   // ro is the near-plane point: keep the camera's offset from it
  keepClear(app);
}

void navOrbit(App& app, vec3 pivot, float dyaw, float dpitch) {
  float np = std::clamp(app.camPitch + dpitch, -89.0f, 89.0f);
  vec3 f, r, u, f2, r2, u2;
  basis(app.camYaw, app.camPitch, f, r, u);
  basis(wrapDeg(app.camYaw + dyaw), np, f2, r2, u2);
  vec3 d = app.camPos - pivot;
  vec3 l(glm::dot(d, r), glm::dot(d, u), glm::dot(d, f));
  vec3 p = pivot + l.x * r2 + l.y * u2 + l.z * f2;
  float g = app.terrain.height(p.x, p.z) + navSettings().minClearance;
  if (p.y < g) return;   // would go underground: keep the last pose
  app.camPos = p; app.camYaw = wrapDeg(app.camYaw + dyaw); app.camPitch = np;
}

void navPanTo(App& app, vec3 anchor, float planeY, vec3 ro, vec3 rd) {
  if (std::fabs(rd.y) < 1e-4f) return;
  float t = (planeY - ro.y) / rd.y;
  if (t <= 0 || t > 50000.0f) return;
  vec3 q = ro + rd * t;
  vec3 d = anchor - q; d.y = 0;
  app.camPos += d;
  keepClear(app);
}

// ------------------------------------------------------------------ framing and flights
bool navBoxInView(vec3 lo, vec3 hi, CamPose cam, float aspect, float fovY, float* maxNdc) {
  mat4 V = glm::lookAt(cam.pos, cam.pos + camForward(cam.yaw, cam.pitch), vec3(0, 1, 0));
  mat4 P = glm::perspective(radians(fovY), aspect, 0.1f, 1e6f);
  float worst = 0; bool all = true;
  for (int k = 0; k < 8; ++k) {
    vec3 c((k & 1) ? hi.x : lo.x, (k & 2) ? hi.y : lo.y, (k & 4) ? hi.z : lo.z);
    vec4 q = P * V * vec4(c, 1);
    if (q.w <= 1e-4f) { all = false; worst = 1e9f; continue; }
    float m = std::max(std::fabs(q.x / q.w), std::fabs(q.y / q.w));
    worst = std::max(worst, m);
    if (m > 1.0f) all = false;
  }
  if (maxNdc) *maxNdc = worst;
  return all;
}

vec3 navFrameBox(vec3 lo, vec3 hi, float yaw, float pitch, float aspect, float fovY, float margin) {
  vec3 c = (lo + hi) * 0.5f, f = camForward(yaw, pitch);
  // smallest distance along -f at which every corner is inside 1/margin of the view (bisection)
  float a = 0.0f, b = 1.0f;
  auto fits = [&](float d) { float m; CamPose p{c - f * d, yaw, pitch}; navBoxInView(lo, hi, p, aspect, fovY, &m); return m * margin <= 1.0f; };
  while (!fits(b) && b < 1e6f) b *= 2;
  for (int k = 0; k < 40; ++k) { float m = (a + b) * 0.5f; if (fits(m)) b = m; else a = m; }
  return c - f * b;
}

void navFlyTo(App& app, CamPose to) {
  NavState& n = navState();
  n.from = {app.camPos, app.camYaw, app.camPitch};
  n.to = to; n.to.yaw = wrapDeg(n.to.yaw); n.to.pitch = std::clamp(n.to.pitch, -89.0f, 89.0f);
  n.flightT = 0; n.flightDur = std::max(0.0f, navSettings().flightSeconds); n.flying = true;
  if (n.flightDur <= 0) navFinishFlight(app);
}
void navFinishFlight(App& app) {
  NavState& n = navState();
  if (!n.flying) return;
  app.camPos = n.to.pos; app.camYaw = n.to.yaw; app.camPitch = n.to.pitch;
  n.flying = false;
}
void navStepFlight(App& app, float dt) {
  NavState& n = navState();
  if (!n.flying) return;
  n.flightT += dt;
  float u = std::clamp(n.flightT / std::max(1e-4f, n.flightDur), 0.0f, 1.0f);
  if (u >= 1.0f) { navFinishFlight(app); return; }
  float e = u * u * (3 - 2 * u);
  float dy = std::fmod(n.to.yaw - n.from.yaw + 540.0f, 360.0f) - 180.0f;   // shortest way round
  app.camPos = glm::mix(n.from.pos, n.to.pos, e);
  app.camYaw = wrapDeg(n.from.yaw + dy * e);
  app.camPitch = n.from.pitch + (n.to.pitch - n.from.pitch) * e;
}

static bool instanceBox(const App& app, const Instance& i, vec3& lo, vec3& hi) {
  const AssetDef* a = app.assetFor(i);
  if (!a) return false;
  mat4 M = app.instanceMatrix(i);
  lo = vec3(1e30f); hi = vec3(-1e30f);
  for (int k = 0; k < 8; ++k) {
    vec3 c((k & 1) ? a->bmax.x : a->bmin.x, (k & 2) ? a->bmax.y : a->bmin.y, (k & 4) ? a->bmax.z : a->bmin.z);
    vec3 w = vec3(M * vec4(c, 1)); lo = glm::min(lo, w); hi = glm::max(hi, w);
  }
  vec3 pad = glm::max(vec3(0), vec3(1.0f) - (hi - lo)) * 0.5f;   // tiny props: frame at least 1 m
  lo -= pad; hi += pad;
  return true;
}

bool navFrameSelection(App& app) {
  const Instance* s = nullptr;
  for (auto& i : app.doc.instances) if (i.id == app.selected) { s = &i; break; }
  vec3 lo, hi;
  if (!s || !instanceBox(app, *s, lo, hi)) { app.toast("Select something to frame it (F).", 2); return false; }
  float pitch = std::min(app.camPitch, -15.0f);
  CamPose to{navFrameBox(lo, hi, app.camYaw, pitch, navAspect(app), kFovY, navSettings().frameMargin), app.camYaw, pitch};
  float g = app.terrain.height(to.pos.x, to.pos.z) + navSettings().minClearance;
  to.pos.y = std::max(to.pos.y, g);
  navFlyTo(app, to);
  return true;
}

CamPose navHomePose(const App& app, float aspect) {
  const Terrain& t = app.terrain;
  float y0 = 1e30f, y1 = -1e30f;
  for (int j = 0; j <= 8; ++j) for (int i = 0; i <= 8; ++i) {
    float x = -t.buildHalfX + 2 * t.buildHalfX * i / 8, z = -t.buildHalfZ + 2 * t.buildHalfZ * j / 8, hh = t.height(x, z);
    y0 = std::min(y0, hh); y1 = std::max(y1, hh);
  }
  // looking along the map's long axis reads best: keep the current heading's nearest axis
  float yaw = std::round(app.camYaw / 90.0f) * 90.0f;
  float pitch = navSettings().homePitch;
  vec3 lo(-t.buildHalfX, y0, -t.buildHalfZ), hi(t.buildHalfX, y1 + 2.0f, t.buildHalfZ);
  return CamPose{navFrameBox(lo, hi, yaw, pitch, aspect, kFovY, navSettings().frameMargin), wrapDeg(yaw), pitch};
}
void navFrameMap(App& app) { navFlyTo(app, navHomePose(app, navAspect(app))); }

// ------------------------------------------------------------------ bookmarks
void navSaveBookmark(App& app, int n) {
  if (n < 1 || n > 9) return;
  if (!app.doc.cameraBookmarks.is_object()) app.doc.cameraBookmarks = json::object();
  app.doc.cameraBookmarks[std::to_string(n)] = {{"position", {app.camPos.x, app.camPos.y, app.camPos.z}}, {"yaw", app.camYaw}, {"pitch", app.camPitch}};
  app.dirty = true;
}
bool navBookmark(const App& app, int n, CamPose& out) {
  const json& b = app.doc.cameraBookmarks;
  std::string k = std::to_string(n);
  if (!b.is_object() || !b.contains(k)) return false;
  const json& e = b[k];
  out.pos = jvec3(e["position"]); out.yaw = e.value("yaw", 0.0f); out.pitch = e.value("pitch", -20.0f);
  return true;
}
bool navRecallBookmark(App& app, int n) {
  CamPose p;
  if (!navBookmark(app, n, p)) { app.toast("Bookmark " + std::to_string(n) + " is empty: Ctrl+" + std::to_string(n) + " saves the current view.", 3); return false; }
  navFlyTo(app, p);
  return true;
}

// ------------------------------------------------------------------ terrain set-up, cuts, frame hook
void w3ApplyTerrain(App& app, const TemplateInfo& t) {
  NavState& n = navState();
  if (n.pendingSize.x > 0) { app.doc.width = n.pendingSize.x; app.doc.depth = n.pendingSize.y; n.pendingSize = vec2(0); }
  app.terrain.gen = app.doc.terrainGen;   // V8: outer ring + generated hills (terrain_brush.cpp)
  app.terrain.generate(t, app.doc.width, app.doc.depth);
  terrainPhysicsBuild(app.terrain);
  n.flying = false; n.ovScale = 0;
}

int navSyncCuts(App& app) {
  // cheap gate: the instance list (ids, revisions, transforms) and the terrain are what the cuts depend on
  static uint64_t lastSig = 0;
  uint64_t sig = 1469598103934665603ull;
  auto mix = [&](uint64_t v) { sig = (sig ^ v) * 1099511628211ull; };
  mix(app.terrain.generation); mix(app.doc.instances.size()); mix((uint64_t)(uintptr_t)app.lib.assets.size());
  for (auto& i : app.doc.instances) { uint32_t b[4]; std::memcpy(&b[0], &i.pos.x, 4); std::memcpy(&b[1], &i.pos.y, 4); std::memcpy(&b[2], &i.pos.z, 4); std::memcpy(&b[3], &i.yaw, 4); mix(i.id); mix(i.rev); mix(b[0]); mix(b[1]); mix(b[2]); mix(b[3]); }
  if (sig == lastSig) { navState().cutsApplied = 0; return 0; }
  lastSig = sig;
  std::vector<WorldCut> cuts;
  for (auto& i : app.doc.instances) {
    const AssetDef* a = app.assetFor(i);
    if (!a || a->navMeta.terrainCuts.empty()) continue;
    quat q = yawQuat(i.yaw);
    for (auto& c : a->navMeta.terrainCuts) {
      WorldCut w; w.owner = i.id; w.topY = i.pos.y; w.pad = navSettings().cutEdgePad < 0 ? app.terrain.fineSpacing() : navSettings().cutEdgePad; w.floorY = i.pos.y - std::max(0.0f, c.depth); w.bank = std::max(0.0f, c.bank);
      for (auto& p : c.outline) { vec3 v = q * vec3(p.x, 0, p.y); w.poly.push_back(vec2(i.pos.x + v.x, i.pos.z + v.z)); }
      cuts.push_back(std::move(w));
    }
  }
  int n = app.terrain.setCuts(std::move(cuts));
  if (n) { ++app.groundRev; terrainPhysicsSync(app.terrain); }
  navState().cutsApplied = n;
  return n;
}

void w3Frame(App& app) {
  NavState& n = navState();
  snapSync(app);   // V6: joined snap sockets (caps, chain texture origins; snap.cpp)
  navSyncCuts(app);
  terrainPhysicsSync(app.terrain);
  if (app.mode == Mode::Creative) { navStepFlight(app, app.frameDt); n.creativeSession = false; }
}

mat4 w3EditorProjection(App& app, float aspect) {
  const Terrain& t = app.terrain;
  float hag = std::max(0.0f, app.camPos.y - t.height(app.camPos.x, app.camPos.z));
  float diag = std::sqrt(t.size * t.size + t.sizeZ * t.sizeZ);
  float farP = std::max(4000.0f, diag * 1.1f + hag * 1.5f);
  float nearP = std::clamp(hag * 0.004f, 0.1f, 4.0f);   // keeps depth precision when looking down from altitude
  return glm::perspective(radians(kFovY), aspect, nearP, farP);
}

// ------------------------------------------------------------------ input
static bool kdown(GLFWwindow* w, int k) { return glfwGetKey(w, k) == GLFW_PRESS; }
static bool kedge(GLFWwindow* w, int k) { static std::map<int, bool> prev; bool d = kdown(w, k), p = prev[k]; prev[k] = d; return d && !p; }

void w3EditorWheel(App& app, float scroll) {
  NavState& n = navState();
  if (kdown(app.win, GLFW_KEY_LEFT_ALT) || kdown(app.win, GLFW_KEY_RIGHT_ALT)) {   // Alt+wheel: fly speed multiplier
    n.speedMult = std::clamp(n.speedMult * std::pow(1.25f, scroll), 0.1f, 10.0f);
    char b[64]; std::snprintf(b, sizeof b, "Fly speed x%.2f", n.speedMult); app.toast(b, 1.2);
    return;
  }
  n.flying = false;
  vec3 ro, rd; rayFromCursor(app, ro, rd);
  navZoomAlongRay(app, scroll, ro, rd);
}

void w3EditorCamera(App& app, float dt, bool uiMouse, bool uiKey) {
  NavState& n = navState();
  if (app.selftest) return;   // scripted runs drive the camera directly
  GLFWwindow* w = app.win;
  const NavCamSettings& s = navSettings();
  double mx, my; glfwGetCursorPos(w, &mx, &my);
  // ---- right-drag: orbit about the ground under the cursor (sky: turn in place)
  bool rmb = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
  if (rmb && !n.orbiting && !uiMouse && !n.panning && !n.dropDrag && n.lmbGesture < 2) {
    vec3 ro, rd, hit; rayFromCursor(app, ro, rd);
    n.pivot = groundHit(app, ro, rd, hit) && glm::length(hit - app.camPos) < 6000.0f ? hit : app.camPos;
    n.orbiting = true; n.flying = false; app.looking = true; n.lastMx = mx; n.lastMy = my;
    glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  }
  if (n.orbiting) {
    if (!rmb) { n.orbiting = false; app.looking = false; glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL); }
    else {
      float dx = (float)(mx - n.lastMx), dy = (float)(my - n.lastMy);
      n.lastMx = mx; n.lastMy = my;
      if (dx != 0 || dy != 0) navOrbit(app, n.pivot, -dx * s.orbitDegPerPixel, -dy * s.orbitDegPerPixel);
    }
  }
  // ---- middle-drag: grab the ground and pan
  bool mmb = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
  if (mmb && !n.panning && !uiMouse && !n.orbiting && n.lmbGesture < 2) {
    vec3 ro, rd, hit; rayFromCursor(app, ro, rd);
    if (!groundHit(app, ro, rd, hit)) { float gy = app.terrain.height(app.camPos.x, app.camPos.z); hit = ro + rd * std::max(30.0f, (app.camPos.y - gy) * 2.0f); hit.y = gy; }
    n.panAnchor = hit; n.panPlaneY = hit.y; n.panning = true; n.flying = false;
  }
  if (n.panning) {
    if (!mmb) n.panning = false;
    else { vec3 ro, rd; rayFromCursor(app, ro, rd); navPanTo(app, n.panAnchor, n.panPlaneY, ro, rd); }
  }
  // ---- WASD / QE fly (Space up, Ctrl down), speed scales with the height above ground
  bool ctrl = kdown(w, GLFW_KEY_LEFT_CONTROL) || kdown(w, GLFW_KEY_RIGHT_CONTROL);
  bool ctrlCombo = false;   // Ctrl + a letter / digit is a shortcut (Ctrl+Z, Ctrl+S, Ctrl+1): no flying then
  if (ctrl) for (int k = GLFW_KEY_0; k <= GLFW_KEY_Z && !ctrlCombo; ++k) ctrlCombo = (k <= GLFW_KEY_9 || k >= GLFW_KEY_A) && kdown(w, k);
  if (!uiKey && !ctrlCombo) {
    vec3 f = camForward(app.camYaw, app.camPitch), r = glm::normalize(glm::cross(camForward(app.camYaw, 0), vec3(0, 1, 0)));
    vec3 mv(0);
    if (!ctrl) {
      if (kdown(w, GLFW_KEY_W)) mv += f;
      if (kdown(w, GLFW_KEY_S)) mv -= f;
      if (kdown(w, GLFW_KEY_D)) mv += r;
      if (kdown(w, GLFW_KEY_A)) mv -= r;
      if (kdown(w, GLFW_KEY_E)) mv.y += 1;
      if (kdown(w, GLFW_KEY_Q)) mv.y -= 1;
    }
    if (kdown(w, GLFW_KEY_SPACE)) mv.y += 1;
    if (ctrl) mv.y -= 1;
    if (glm::length(mv) > 0) {
      n.flying = false;
      float sp = navFlySpeed(app) * n.speedMult * (kdown(w, GLFW_KEY_LEFT_SHIFT) || kdown(w, GLFW_KEY_RIGHT_SHIFT) ? s.shiftMult : 1.0f);
      app.camPos += glm::normalize(mv) * sp * dt;
    }
    app.flySpeed = navFlySpeed(app) * n.speedMult;   // shown in the status bar
  }
  keepClear(app);
  // ---- keys: F frame, Home map, M overview, bookmarks
  if (!uiKey) {
    if (!ctrl && kedge(w, GLFW_KEY_F)) navFrameSelection(app);
    if (kedge(w, GLFW_KEY_HOME)) navFrameMap(app);
    if (!ctrl && kedge(w, GLFW_KEY_M)) { n.overview = !n.overview; }
    if (n.overview && kedge(w, GLFW_KEY_ESCAPE)) n.overview = false;
    for (int k = 1; k <= 9; ++k) {
      if (!kedge(w, GLFW_KEY_0 + k)) continue;
      if (ctrl) { navSaveBookmark(app, k); app.toast("Saved camera bookmark " + std::to_string(k) + " (press " + std::to_string(k) + " to return).", 2); }
      else navRecallBookmark(app, k);
    }
  }
}

const char* w3StatusHelp() {
  return "LMB drag orbit | click select | drag the selection to move it | Wheel zoom to cursor | + / - zoom | MMB drag pan | RMB drag orbit | WASD/QE fly, Space up, Ctrl down (Shift fast, Alt+wheel speed) | F frame | Home map | M overview | Ctrl+1-9 / 1-9 bookmarks | drag the soldier to explore | F5 explore from start";
}

// ------------------------------------------------------------------ V6: Select tool LMB gestures, centre zoom
void navZoomCentre(App& app, float steps) {
  navState().flying = false;
  navZoomAlongRay(app, steps, app.camPos, camForward(app.camYaw, app.camPitch));
}

EdGesture edSelectPointer(App& app, const EdPointer& p) {
  NavState& n = navState();
  const NavCamSettings& s = navSettings();
  bool press = p.lmb && !n.lmbPrev;
  n.lmbPrev = p.lmb;
  auto endOrbit = [&]() {
    app.looking = false;
    if (!app.selftest && app.win) glfwSetInputMode(app.win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  };
  if (press && n.lmbGesture == 0 && !p.overUi && !app.looking && !n.orbiting && !n.panning && !n.dropDrag) {
    float t; uint64_t id = app.pickInstance(p.ro, p.rd, t);
    vec3 gp; bool g = app.terrain.raycast(p.ro, p.rd, 20000, gp);
    bool onObj = id && (!g || t < glm::length(gp - p.ro) + 0.01f);
    n.pressId = onObj ? id : 0;
    n.pressOnSel = onObj && id == app.selected;
    n.pressPx = p.px; n.lastPx = p.px;
    n.pressGround = g; n.pressGroundPt = gp;
    n.lpivot = onObj ? p.ro + p.rd * t : (g && glm::length(gp - app.camPos) < 6000.0f ? gp : app.camPos);   // orbit about the pressed point
    n.lmbGesture = 1; app.dragPending = true;
  }
  if (n.lmbGesture == 1 && p.lmb && glm::length(p.px - n.pressPx) > 4.0f) {
    Instance* sel = n.pressOnSel ? app.doc.find(app.selected) : nullptr;
    if (sel) {   // press-drag on the selected piece: move it (one undo step; rejected moves revert on release)
      app.pushUndo(); sel = app.doc.find(app.selected);
      app.dragOrig = *sel; app.dragging = true;
      vec3 gp = n.pressGround ? n.pressGroundPt : sel->pos;
      app.dragOffset = sel->pos - gp; app.dragOffset.y = 0;
      n.moveYaw = sel->yaw; n.appliedYaw = sel->yaw;
      n.lmbGesture = 3;
    } else {     // anywhere else: orbit about the pressed point, never select
      n.lmbGesture = 2; n.flying = false; app.looking = true;
      if (!app.selftest && app.win) glfwSetInputMode(app.win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);   // virtual cursor continues from here
    }
  }
  if (n.lmbGesture == 2 && p.lmb) {
    vec2 d = p.px - n.lastPx; n.lastPx = p.px;
    if (d.x != 0 || d.y != 0) navOrbit(app, n.lpivot, -d.x * s.orbitDegPerPixel, -d.y * s.orbitDegPerPixel);
  }
  if (n.lmbGesture == 3 && p.lmb) {
    Instance* sel = app.doc.find(app.selected);
    vec3 gp;
    if (!sel) { n.lmbGesture = 0; app.dragging = false; }
    else if (app.terrain.raycast(p.ro, p.rd, 20000, gp)) {
      const AssetDef* a = app.assetFor(*sel);
      if (sel->yaw != n.appliedYaw) n.moveYaw = wrapDeg(n.moveYaw + sel->yaw - n.appliedYaw);   // wheel turns while moving
      vec3 np = gp + app.dragOffset;
      if (app.gridSnap) { np.x = std::round(np.x); np.z = std::round(np.z); }
      float ny = n.moveYaw;
      snapPlacement(app, a, np, ny, sel->id);   // sockets, sandbag runs, path ends (snap.cpp)
      PlaceCheck c = app.checkPlacement(a, np, ny, sel->id);
      app.lastCheck = c;
      sel->pos = vec3(c.snapXZ ? c.xz.x : np.x, c.ok ? c.y : sel->pos.y, c.snapXZ ? c.xz.y : np.z);
      sel->yaw = ny; n.appliedYaw = ny;
      app.bump(*sel);
    }
  }
  EdGesture out = n.lmbGesture == 1 ? EdGesture::Pending : n.lmbGesture == 2 ? EdGesture::Orbit : n.lmbGesture == 3 ? EdGesture::Move : EdGesture::None;
  if (!p.lmb && n.lmbGesture) {
    if (n.lmbGesture == 1) {   // a click: select what was under the cursor
      if (n.pressId) app.selected = n.pressId;
      else if (!app.editInteract) app.selected = 0;
    } else if (n.lmbGesture == 2) endOrbit();
    else if (n.lmbGesture == 3) {
      Instance* sel = app.doc.find(app.selected);
      if (sel && !app.lastCheck.ok) { *sel = app.dragOrig; app.bump(*sel); if (!app.undoStack.empty()) app.undoStack.pop_back(); app.toast("Move rejected: " + app.lastCheck.reason); }
      else if (sel) crushAndFlatten(app, sel->id);   // buildings crush growth and flatten at the new spot (same undo step)
    }
    n.lmbGesture = 0; app.dragging = false; app.dragPending = false;
  }
  return out;
}

// ------------------------------------------------------------------ overlays
void w3Overlay(App& app, Overlay& ov) {
  const Terrain& t = app.terrain;
  float X = t.buildHalfX, Z = t.buildHalfZ;
  vec4 bc(0.95f, 0.85f, 0.45f, 0.8f);
  float step = std::max(10.0f, std::max(X, Z) / 150.0f);
  // lifted with the camera height so the line never sinks into the ground at depth-buffer precision from far above
  float lift = 0.15f + 0.004f * std::max(0.0f, app.camPos.y - t.height(app.camPos.x, app.camPos.z));
  auto edge = [&](vec2 a, vec2 b) {
    float L = glm::length(b - a); int nseg = std::max(1, (int)std::ceil(L / step));
    for (int k = 0; k < nseg; ++k) {
      vec2 p0 = glm::mix(a, b, (float)k / nseg), p1 = glm::mix(a, b, (float)(k + 1) / nseg);
      ov.line(vec3(p0.x, t.height(p0.x, p0.y) + lift, p0.y), vec3(p1.x, t.height(p1.x, p1.y) + lift, p1.y), bc);
    }
  };
  edge({-X, -Z}, {X, -Z}); edge({X, -Z}, {X, Z}); edge({X, Z}, {-X, Z}); edge({-X, Z}, {-X, -Z});
  dropinOverlay(app, ov);
}

// ------------------------------------------------------------------ overview map
static void buildOverviewTexture(App& app) {
  NavState& n = navState();
  const Terrain& t = app.terrain;
  int S = std::max(64, navSettings().overviewTexture);
  int W = t.size >= t.sizeZ ? S : std::max(16, (int)(S * t.size / t.sizeZ)), H = t.size >= t.sizeZ ? std::max(16, (int)(S * t.sizeZ / t.size)) : S;
  std::vector<uint8_t> px((size_t)W * H * 4);
  vec3 sun = glm::normalize(vec3(-0.5f, 0.8f, -0.35f));
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      float wx = t.org.x + (x + 0.5f) / W * t.size, wz = t.org.y + (y + 0.5f) / H * t.sizeZ;
      float hh = t.height(wx, wz);
      vec3 nn = t.normal(wx, wz);
      float shade = std::clamp(0.55f + 0.8f * (glm::dot(nn, sun) - 0.75f) * 3.0f, 0.25f, 1.25f);
      vec3 c = hh < t.waterLevel + 0.05f ? vec3(0.18f, 0.32f, 0.45f) : glm::mix(vec3(0.24f, 0.36f, 0.15f), vec3(0.42f, 0.42f, 0.24f), std::clamp((hh + 1.0f) / 8.0f, 0.0f, 1.0f));
      float wear = t.wearAt(wx, wz); c = glm::mix(c, vec3(0.45f, 0.38f, 0.27f), wear * 0.8f);
      if (!t.inBuildArea(wx, wz)) c *= 0.6f;
      c *= shade;
      uint8_t* p = &px[((size_t)y * W + x) * 4];
      p[0] = (uint8_t)std::clamp(c.r * 255.0f, 0.0f, 255.0f); p[1] = (uint8_t)std::clamp(c.g * 255.0f, 0.0f, 255.0f); p[2] = (uint8_t)std::clamp(c.b * 255.0f, 0.0f, 255.0f); p[3] = 255;
    }
  if (n.ovTex && (n.ovTexW != W || n.ovTexH != H)) { glDeleteTextures(1, &n.ovTex); n.ovTex = 0; }
  if (!n.ovTex) {
    glCreateTextures(GL_TEXTURE_2D, 1, &n.ovTex);
    glTextureStorage2D(n.ovTex, 1, GL_RGBA8, W, H);
    glTextureParameteri(n.ovTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(n.ovTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(n.ovTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTextureParameteri(n.ovTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTextureSubImage2D(n.ovTex, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  n.ovTexW = W; n.ovTexH = H; n.ovTexGen = t.generation ^ (t.revision * 2654435761u) ^ (uint32_t)t.surfRev * 40503u;
}

void navOverviewJump(App& app, vec2 p) {
  // keep the camera's height above ground, heading and pitch; move so the view centre lands on p
  vec3 f = camForward(app.camYaw, app.camPitch), hit;
  float hag = std::max(5.0f, app.camPos.y - app.terrain.height(app.camPos.x, app.camPos.z));
  vec3 off;
  if (app.camPitch < -5.0f && app.terrain.raycast(app.camPos, f, 20000.0f, hit) && glm::length(hit - app.camPos) < 5000.0f) off = app.camPos - hit;
  else off = vec3(0, hag, 0) - camForward(app.camYaw, 0) * (hag * 0.8f);
  float gy = app.terrain.height(p.x, p.y);
  CamPose to{vec3(p.x, gy, p.y) + off, app.camYaw, app.camPitch};
  to.pos.y = std::max(to.pos.y, app.terrain.height(to.pos.x, to.pos.z) + navSettings().minClearance);
  navFlyTo(app, to);
}

static void drawOverview(App& app) {
  NavState& n = navState();
  const Terrain& t = app.terrain;
  ImGuiIO& io = ImGui::GetIO();
  uint32_t gen = t.generation ^ (t.revision * 2654435761u) ^ (uint32_t)t.surfRev * 40503u;
  if (!n.ovTex || n.ovTexGen != gen) buildOverviewTexture(app);
  float fr = navSettings().overviewFraction;
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * fr, io.DisplaySize.y * fr), ImGuiCond_Appearing);
  if (!ImGui::Begin("Overview map (M)", &n.overview, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) { ImGui::End(); return; }
  ImVec2 c0 = ImGui::GetCursorScreenPos(), av = ImGui::GetContentRegionAvail();
  av.y -= ImGui::GetTextLineHeightWithSpacing();
  av.x = std::max(av.x, 64.0f); av.y = std::max(av.y, 64.0f);
  ImGui::InvisibleButton("##ovcanvas", av, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
  bool hov = ImGui::IsItemHovered();
  vec2 cs(av.x, av.y), cc(c0.x + av.x * 0.5f, c0.y + av.y * 0.5f);
  if (n.ovScale <= 0) { n.ovScale = std::min(cs.x / t.size, cs.y / t.sizeZ) * 0.95f; n.ovCentre = vec2(t.org.x + t.size * 0.5f, t.org.y + t.sizeZ * 0.5f); }
  auto toS = [&](vec2 w) { return ImVec2(cc.x + (w.x - n.ovCentre.x) * n.ovScale, cc.y + (w.y - n.ovCentre.y) * n.ovScale); };
  auto toW = [&](ImVec2 s) { return vec2(n.ovCentre.x + (s.x - cc.x) / n.ovScale, n.ovCentre.y + (s.y - cc.y) / n.ovScale); };
  // input: wheel zoom about the cursor, drag pans, click jumps
  if (hov && io.MouseWheel != 0) {
    vec2 before = toW(io.MousePos);
    n.ovScale = std::clamp(n.ovScale * std::pow(1.2f, io.MouseWheel), 0.01f, 40.0f);
    vec2 after = toW(io.MousePos);
    n.ovCentre += before - after;
  }
  if (ImGui::IsItemActivated()) { n.ovPressPos = vec2(io.MousePos.x, io.MousePos.y); n.ovDragging = false; }
  if (ImGui::IsItemActive()) {
    if (glm::length(vec2(io.MousePos.x, io.MousePos.y) - n.ovPressPos) > 4) n.ovDragging = true;
    if (n.ovDragging) n.ovCentre -= vec2(io.MouseDelta.x, io.MouseDelta.y) / n.ovScale;
  }
  if (ImGui::IsItemDeactivated() && !n.ovDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    vec2 p = toW(io.MousePos);
    p = glm::clamp(p, t.org + vec2(1), t.org + vec2(t.size, t.sizeZ) - vec2(1));
    navOverviewJump(app, p);
    n.overview = false;
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(c0, ImVec2(c0.x + av.x, c0.y + av.y), true);
  dl->AddRectFilled(c0, ImVec2(c0.x + av.x, c0.y + av.y), IM_COL32(18, 20, 16, 255));
  dl->AddImage((ImTextureID)(intptr_t)n.ovTex, toS(t.org), toS(t.org + vec2(t.size, t.sizeZ)), ImVec2(0, 0), ImVec2(1, 1));
  // build area
  dl->AddRect(toS(vec2(-t.buildHalfX, -t.buildHalfZ)), toS(vec2(t.buildHalfX, t.buildHalfZ)), IM_COL32(240, 215, 115, 230), 0, 0, 2.0f);
  // placed structures (footprints)
  for (auto& i : app.doc.instances) {
    const AssetDef* a = app.assetFor(i);
    if (!a) continue;
    vec2 hf = a->footprint * 0.5f;
    if (hf.x * n.ovScale < 0.7f && hf.y * n.ovScale < 0.7f) { dl->AddRectFilled(toS(vec2(i.pos.x, i.pos.z) - vec2(0.6f / n.ovScale)), toS(vec2(i.pos.x, i.pos.z) + vec2(0.6f / n.ovScale)), a->tree.on ? IM_COL32(40, 90, 35, 220) : IM_COL32(190, 185, 170, 220)); continue; }
    quat q = yawQuat(i.yaw);
    ImVec2 p[4];
    vec2 cn[4] = {{-hf.x, -hf.y}, {hf.x, -hf.y}, {hf.x, hf.y}, {-hf.x, hf.y}};
    for (int k = 0; k < 4; ++k) { vec3 v = q * vec3(cn[k].x, 0, cn[k].y); p[k] = toS(vec2(i.pos.x + v.x, i.pos.z + v.z)); }
    ImU32 col = a->tree.on ? IM_COL32(40, 95, 35, 200) : i.id == app.selected ? IM_COL32(255, 170, 40, 240) : IM_COL32(200, 195, 180, 210);
    dl->AddConvexPolyFilled(p, 4, col);
  }
  // scenario: attack path, objectives, spawns (doc.extra["scenario"])
  if (app.doc.extra.is_object() && app.doc.extra.contains("scenario")) {
    Scenario sc;
    try { sc = Scenario::fromJson(app.doc.extra["scenario"]); } catch (...) {}
    if (!sc.objectives.empty()) {
      vec2 prev = sc.attackerStart();
      for (auto& o : sc.objectives) {
        vec2 p(o.pos.x, o.pos.z);
        dl->AddLine(toS(prev), toS(p), IM_COL32(255, 225, 100, 230), 2.5f);
        vec2 d = p - prev; float L = glm::length(d);
        if (L > 1e-3f) { d /= L; vec2 m = prev + d * (L * 0.55f), s2(-d.y, d.x); float a = 9.0f / n.ovScale; dl->AddTriangleFilled(toS(m + d * a), toS(m - d * a * 0.6f + s2 * a * 0.6f), toS(m - d * a * 0.6f - s2 * a * 0.6f), IM_COL32(255, 225, 100, 230)); }
        prev = p;
      }
    }
    for (auto& o : sc.objectives) {
      ImVec2 c = toS(vec2(o.pos.x, o.pos.z));
      if (o.shape == CaptureShape::Polygon && o.polygon.size() >= 3) {
        std::vector<ImVec2> pts; for (auto& q : o.polygon) pts.push_back(toS(q));
        dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(255, 200, 70, 240), ImDrawFlags_Closed, 2.0f);
      } else dl->AddCircle(c, std::max(4.0f, o.radius * n.ovScale), IM_COL32(255, 200, 70, 240), 48, 2.0f);
      char lb[96]; std::snprintf(lb, sizeof lb, "%c  %s", o.letter(), o.displayName().c_str());
      dl->AddCircleFilled(c, 9, IM_COL32(40, 30, 10, 230));
      char L[2] = {o.letter(), 0};
      dl->AddText(ImVec2(c.x - 4, c.y - 7), IM_COL32(255, 220, 120, 255), L);
      dl->AddText(ImVec2(c.x + 12, c.y - 7), IM_COL32(255, 235, 180, 255), lb + 3);
    }
    for (auto& s : sc.spawns) {
      ImVec2 c = toS(vec2(s.pos.x, s.pos.z));
      vec3 f = camForward(s.yaw, 0); vec2 d(f.x, f.z), sd(-d.y, d.x);
      ImU32 col = s.role == CombatRole::Attacker ? IM_COL32(235, 80, 60, 240) : IM_COL32(80, 150, 255, 240);
      auto at = [&](vec2 v) { return ImVec2(c.x + v.x * 9, c.y + v.y * 9); };
      dl->AddTriangleFilled(at(d), at(-d * 0.7f + sd * 0.7f), at(-d * 0.7f - sd * 0.7f), col);
      char ph[32]; std::snprintf(ph, sizeof ph, "%s", s.role == CombatRole::Attacker ? "ATK" : "DEF");
      dl->AddText(ImVec2(c.x + 10, c.y + 2), col, ph);
    }
  }
  // explore start and the camera (position + view wedge)
  if (app.doc.start.set) { ImVec2 c = toS(vec2(app.doc.start.pos.x, app.doc.start.pos.z)); dl->AddCircleFilled(c, 5, IM_COL32(90, 200, 255, 255)); dl->AddText(ImVec2(c.x + 7, c.y - 7), IM_COL32(150, 220, 255, 255), "start"); }
  {
    ImVec2 c = toS(vec2(app.camPos.x, app.camPos.z));
    float hw = 30.0f * navAspect(app);
    vec3 a = camForward(app.camYaw + hw, 0), b = camForward(app.camYaw - hw, 0);
    float L = 60;
    dl->AddTriangleFilled(c, ImVec2(c.x + a.x * L, c.y + a.z * L), ImVec2(c.x + b.x * L, c.y + b.z * L), IM_COL32(255, 255, 255, 50));
    dl->AddCircleFilled(c, 5, IM_COL32(255, 255, 255, 255));
  }
  if (hov) { vec2 w = toW(io.MousePos); char b[64]; std::snprintf(b, sizeof b, "%.0f, %.0f m", w.x, w.y); dl->AddText(ImVec2(io.MousePos.x + 14, io.MousePos.y + 10), IM_COL32(255, 255, 255, 230), b); }
  dl->PopClipRect();
  ImGui::TextDisabled("Map %.0f x %.0f m  |  wheel zoom  |  drag pan  |  click to move the camera there  |  objectives A-J, attack path, ATK / DEF spawns  |  M / Esc close", t.buildHalfX * 2, t.buildHalfZ * 2);
  ImGui::End();
}

// ------------------------------------------------------------------ New Map dialog
bool navNewMapSized(App& app, const TemplateInfo& t, const std::string& name, vec2 size) {
  navState().pendingSize = size == vec2(t.width, t.depth) ? size : mapSizeRules().clampSize(size);   // W5b: a template's own size is kept as authored
  bool ok = app.newMap(t, name);
  navState().pendingSize = vec2(0);
  // a larger map starts with the whole build area in view
  if (ok && (app.doc.width > 700 || app.doc.depth > 700)) { CamPose p = navHomePose(app, navAspect(app)); app.camPos = p.pos; app.camYaw = p.yaw; app.camPitch = p.pitch; }
  return ok;
}

static void newMapDialog(App& app) {
  static int W = 600, D = 600;
  if (app.showNewMap) { ImGui::OpenPopup("New map"); app.showNewMap = false; W = 600; D = 600; }
  ImGui::SetNextWindowSize(ImVec2(780, 0), ImGuiCond_Appearing);
  ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, ImGui::GetIO().DisplaySize.y - 40));   // never taller than the screen
  if (!ImGui::BeginPopupModal("New map", nullptr)) return;
  const MapSizeRules& r = mapSizeRules();
  ImGui::InputText("Map name", app.nameBuf, sizeof app.nameBuf);
  ImGui::Separator();
  bool createNow = false;
  // the template list scrolls (one 100 px thumbnail row each), so the size fields and the buttons below stay on screen
  const float rowH = 100 + ImGui::GetStyle().ItemSpacing.y;
  const float listH = std::min(rowH * (float)app.templates.size() + 8, std::max(200.0f, ImGui::GetIO().DisplaySize.y - 330.0f));
  ImGui::BeginChild("templates", ImVec2(0, listH), true);
  for (int k = 0; k < (int)app.templates.size(); ++k) {
    auto& t = app.templates[k];
    ImGui::PushID(k);
    ImGui::BeginDisabled(!t.available);
    bool sel = app.newTemplateIdx == k;
    if (t.thumbnail) ImGui::Image((ImTextureID)(intptr_t)t.thumbnail, ImVec2(160, 100), ImVec2(0, 0.8f), ImVec2(1, 0.2f));
    else ImGui::Dummy(ImVec2(160, 100));
    ImGui::SameLine();
    ImGui::BeginGroup();
    // NoAutoClosePopups: a Selectable inside a popup closes it by default, which shut this dialog on the first click
    // without creating anything. Double-click creates the map at once.
    if (ImGui::Selectable(t.name.c_str(), sel, ImGuiSelectableFlags_NoAutoClosePopups | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(560, 0))) {
      app.newTemplateIdx = k; if (t.id != "blank_slate") { W = (int)t.width; D = (int)t.depth; }
      if (t.available && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) createNow = true;
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 560);
    ImGui::TextDisabled("%s", t.description.c_str());
    ImGui::TextDisabled("%s %.0f x %.0f m", t.id == "blank_slate" ? "Default size" : "Size", t.width, t.depth);
    if (!t.available) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Not included in this build yet.");
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::EndDisabled();
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::Separator();
  bool valid = app.newTemplateIdx < (int)app.templates.size();
  const TemplateInfo* t = valid ? &app.templates[app.newTemplateIdx] : nullptr;
  bool sizable = t && t->id == "blank_slate";   // authored templates keep their own size
  ImGui::BeginDisabled(!sizable);
  ImGui::Text("Map size (buildable area)");
  ImGui::SetNextItemWidth(160); ImGui::InputInt("Width (m, x)", &W, (int)r.step, (int)r.step * 10); ImGui::SameLine();
  ImGui::SetNextItemWidth(160); ImGui::InputInt("Depth (m, z)", &D, (int)r.step, (int)r.step * 10);
  vec2 wd = r.clampSize(vec2((float)W, (float)D));
  if (!ImGui::IsItemActive()) { W = (int)wd.x; D = (int)wd.y; }
  ImGui::Text("Presets:");
  for (auto& p : r.presets) {
    ImGui::SameLine();
    char b[32]; std::snprintf(b, sizeof b, "%.0f x %.0f", p.x, p.y);
    if (ImGui::Button(b)) { W = (int)p.x; D = (int)p.y; }
  }
  ImGui::EndDisabled();
  if (!sizable && t) { wd = vec2(t->width, t->depth); }
  TerrainEstimate e = estimateTerrain(wd.x, wd.y, t ? t->margin : -1);
  ImGui::TextDisabled("%.0f x %.0f m = %.2f km2  |  terrain %d x %d vertices (%.2f m), %d chunks of %.0f m  |  ~%.0f MB (heights %.0f, colliders %.0f, streamed meshes %.0f, ground mask %.0f)",
                      wd.x, wd.y, wd.x * wd.y / 1e6, e.verticesX, e.verticesZ, Terrain::kCell, e.chunks, Terrain::kCell * Terrain::kChunkCells,
                      e.totalMB, e.cpuMB, e.physicsMB, e.gpuMB, e.surfaceMB);
  if (!e.comfortable)
    ImGui::TextColored(ImVec4(1, 0.65f, 0.3f, 1), "Large map: above %.1f km2 the build may not hold the frame-rate target on minimum-spec PCs (more terrain, longer saves, slower bot pathing).", r.comfortableKm2);
  ImGui::Separator();
  bool canCreate = t && t->available;
  ImGui::BeginDisabled(!canCreate);
  if (ImGui::Button("Create editable copy") || (createNow && canCreate)) { navNewMapSized(app, *t, app.nameBuf, sizable ? wd : vec2(t->width, t->depth)); ImGui::CloseCurrentPopup(); }
  ImGui::EndDisabled();
  ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

// V6: + / - zoom buttons at the bottom centre (above the status bar), toward the ground at the screen centre
static void zoomButtons(App& app) {
  if (app.mode != Mode::Creative) return;
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - ImGui::GetFrameHeightWithSpacing() - 14), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
  ImGui::SetNextWindowBgAlpha(0.55f);
  ImGui::Begin("##zoomButtons", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
  ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
  if (ImGui::Button(" + ", ImVec2(40, 32))) navZoomCentre(app, 1.0f);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom in toward the screen centre");
  ImGui::SameLine();
  if (ImGui::Button(" - ", ImVec2(40, 32))) navZoomCentre(app, -1.0f);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom out from the screen centre");
  ImGui::PopItemFlag();
  ImGui::End();
}

void w3EditorUI(App& app) {
  newMapDialog(app);
  if (navState().overview) drawOverview(app);
  dropinUI(app);
  zoomButtons(app);
}
