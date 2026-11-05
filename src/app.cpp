#include "app.h"
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <stb_image_write.h>
#include <algorithm>

App* gApp = nullptr;
double gScrollY = 0;

static void scrollCb(GLFWwindow*, double, double y) { gScrollY += y; }

void App::toast(const std::string& s, double secs) { notice = s; noticeUntil = now + secs; logf("[notice] %s", s.c_str()); }

vec3 App::forward(float yawDeg, float pitchDeg) {
  float y = radians(yawDeg), p = radians(pitchDeg);
  return vec3(-std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p));
}

// ============================================================ runtime sync
const AssetDef* App::assetFor(const Instance& i) const {
  const AssetDef* a = lib.find(i.assetId);
  return a ? a : const_cast<AssetLibrary&>(lib).placeholder();
}
mat4 App::instanceMatrix(const Instance& i) const {
  return glm::translate(mat4(1), i.pos) * glm::mat4_cast(yawQuat(i.yaw));
}
bool App::authoredOpen(const Instance& i, const InteractDef& d) const {
  auto it = i.openDefaults.find(d.id);
  return it != i.openDefaults.end() ? it->second : d.defaultOpen;
}
float App::leafAngle(const InteractDef& d, float p) const {
  p = std::clamp(p, 0.0f, 1.0f);
  float e = p * p * (3 - 2 * p);
  return d.openAngle * e;
}
mat4 App::leafMatrix(const Instance& i, const Leaf& l, const InteractDef& d, float p) const {
  return instanceMatrix(i) * glm::translate(mat4(1), l.hinge) * glm::mat4_cast(glm::angleAxis(radians(l.sign * leafAngle(d, p)), d.axis));
}
void App::updateLeafBodies(const Instance& i, InstanceRT& r, int k) {
  auto& d = r.asset->interactables[k];
  for (size_t l = 0; l < d.leaves.size(); ++l) {
    auto& lf = d.leaves[l];
    vec3 pos = vec3(instanceMatrix(i) * vec4(lf.hinge, 1));
    quat q = yawQuat(i.yaw) * glm::angleAxis(radians(lf.sign * leafAngle(d, r.progress[k])), d.axis);
    Physics::setTransform(r.leafBodies[k][l], pos, q);
  }
}
void App::destroyRT(InstanceRT& r) {
  Physics::remove(r.staticBody); r.staticBody = 0xffffffffu;
  Physics::remove(r.wallBody); r.wallBody = 0xffffffffu;
  for (auto& v : r.leafBodies) for (auto b : v) Physics::remove(b);
  r.leafBodies.clear();
}
void App::buildRT(const Instance& i, InstanceRT& r) {
  destroyRT(r);
  r.id = i.id; r.rev = i.rev; r.asset = assetFor(i);
  uint64_t base = i.id << 16;
  if (r.asset->shape) r.staticBody = Physics::addStatic(r.asset->shape, i.pos, yawQuat(i.yaw), base);
  size_t n = r.asset->interactables.size();
  r.progress.assign(n, 0); r.dir.assign(n, 0); r.leafBodies.assign(n, {});
  r.hp.assign(r.asset->panels.size(), 0); r.state.assign(r.asset->panels.size(), PANEL_INTACT);
  for (size_t k = 0; k < r.asset->panels.size(); ++k) {
    auto& p = r.asset->panels[k];
    auto ov = i.panelHp.find(p.id);
    r.hp[k] = ov != i.panelHp.end() ? ov->second : p.hp;
    r.state[k] = panelStateFor(r.hp[k], p.hp);
  }
  rebuildWalls(i, r);
  for (size_t k = 0; k < n; ++k) {
    auto& d = r.asset->interactables[k];
    r.progress[k] = authoredOpen(i, d) ? 1.0f : 0.0f;
    for (auto& lf : d.leaves) {
      vec3 pos = vec3(instanceMatrix(i) * vec4(lf.hinge, 1));
      quat q = yawQuat(i.yaw) * glm::angleAxis(radians(lf.sign * leafAngle(d, r.progress[k])), d.axis);
      r.leafBodies[k].push_back(Physics::addKinematic(lf.shape, pos, q, base | (k + 1)));
    }
  }
}
void App::syncRuntime() {
  std::set<uint64_t> live;
  for (auto& i : doc.instances) {
    live.insert(i.id);
    auto& r = rt[i.id];
    if (r.rev != i.rev || r.id != i.id) buildRT(i, r);
  }
  for (auto it = rt.begin(); it != rt.end();) {
    if (!live.count(it->first)) { destroyRT(it->second); it = rt.erase(it); } else ++it;
  }
}
void App::clearRuntime() { for (auto& [id, r] : rt) destroyRT(r); rt.clear(); }
void App::resetInteractStates() {
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    auto& r = it->second;
    for (size_t k = 0; k < r.progress.size(); ++k) {
      r.progress[k] = authoredOpen(i, r.asset->interactables[k]) ? 1.0f : 0.0f;
      r.dir[k] = 0;
      updateLeafBodies(i, r, (int)k);
    }
  }
}

void App::buildDrawLists(std::vector<DrawItem>& op, std::vector<DrawItem>& gl) {
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    auto& r = it->second;
    mat4 M = instanceMatrix(i);
    // coarse distance cull
    if (glm::length(i.pos - (mode == Mode::Explore ? eyePos() : camPos)) > 1200) continue;
    op.push_back({&r.asset->opaque, M});
    if (!r.damaged) op.push_back({&r.asset->panelsIntact, M});
    else for (size_t k = 0; k < r.asset->panels.size(); ++k) op.push_back({&r.asset->panels[k].mesh[r.state[k]], M});
    if (r.asset->glass.count) gl.push_back({&r.asset->glass, M});
    for (size_t k = 0; k < r.asset->interactables.size(); ++k) {
      auto& d = r.asset->interactables[k];
      for (auto& lf : d.leaves) {
        mat4 L = leafMatrix(i, lf, d, r.progress[k]);
        op.push_back({&lf.opaque, L});
        if (lf.glass.count) gl.push_back({&lf.glass, L});
      }
    }
  }
}

// ============================================================ documents
const TemplateInfo* App::findTemplate(const std::string& id) const {
  for (auto& t : templates) if (t.id == id) return &t;
  return nullptr;
}
void App::applyTemplate(const std::string& id) {
  const TemplateInfo* t = findTemplate(id);
  if (!t) t = templates.empty() ? nullptr : &templates[0];
  TemplateInfo fallback; fallback.id = "blank_slate";
  if (!t) t = &fallback;
  removeGroundBodies();
  terrain.generate(*t);
  Physics::setTerrain(terrain.h, terrain.n, terrain.size);
}
bool App::newMap(const TemplateInfo& t, const std::string& name) {
  if (mode == Mode::Explore) exitExplore();
  clearRuntime();
  doc = MapDoc();
  doc.name = name; doc.templateId = t.id; doc.templateVersion = t.version; doc.seed = t.seed;
  applyTemplate(t.id);
  camPos = t.camPos; camYaw = t.camYaw; camPitch = t.camPitch;
  docPath.clear(); dirty = false; selected = 0; undoStack.clear(); redoStack.clear();
  tool = Tool::Select; placing = nullptr;
  toast("New map from template: " + t.name);
  return true;
}
bool App::openMap(const fs::path& p) {
  MapDoc d; std::string err;
  if (!loadMap(p, d, err)) { toast("Could not open map: " + err, 8); return false; }
  if (mode == Mode::Explore) exitExplore();
  clearRuntime();
  doc = std::move(d);
  for (auto& i : doc.instances) { i.rev = ++revCounter; if (!lib.find(i.assetId)) toast("Missing asset '" + i.assetId + "' shown as placeholder; its data is preserved.", 8); }
  applyTemplate(doc.templateId);
  resetGround();
  if (doc.camera.set) { camPos = doc.camera.pos; camYaw = doc.camera.yaw; camPitch = doc.camera.pitch; }
  bool isContent = p.string().find((contentDir()).string()) == 0;
  docPath = isContent ? fs::path() : p;  // built-in examples open as unsaved copies
  dirty = false; selected = 0; undoStack.clear(); redoStack.clear();
  std::snprintf(nameBuf, sizeof nameBuf, "%s", doc.name.c_str());
  syncRuntime();
  toast("Opened " + doc.name + (isContent ? " (copy — Save As to keep)" : ""));
  return true;
}
bool App::saveMapTo(const fs::path& p) {
  doc.camera.set = true; doc.camera.pos = camPos; doc.camera.yaw = camYaw; doc.camera.pitch = camPitch;
  if (mode == Mode::Explore) { doc.camera.pos = savedCam.pos; doc.camera.yaw = savedCam.yaw; doc.camera.pitch = savedCam.pitch; }
  std::string err;
  if (!saveMap(p, doc, err)) { toast("Save failed: " + err, 8); return false; }
  docPath = p; dirty = false;
  toast("Saved " + p.filename().string());
  return true;
}

// ============================================================ undo
Snapshot App::snap() const { return Snapshot{doc.instances, doc.start, doc.removedVegetation, selected, doc.terrainDeltas}; }
void App::restore(const Snapshot& s) {
  bool groundChanged = doc.terrainDeltas != s.terrain;
  doc.instances = s.inst; doc.start = s.start; doc.removedVegetation = s.removedVeg; selected = s.selected;
  doc.terrainDeltas = s.terrain;
  if (groundChanged) resetGround();
  for (auto& i : doc.instances) i.rev = ++revCounter;  // force collider rebuild to the restored transforms
  dirty = true;
}
void App::pushUndo() { undoStack.push_back(snap()); if (undoStack.size() > 200) undoStack.erase(undoStack.begin()); redoStack.clear(); }
void App::undo() { if (undoStack.empty()) return; redoStack.push_back(snap()); restore(undoStack.back()); undoStack.pop_back(); }
void App::redo() { if (redoStack.empty()) return; undoStack.push_back(snap()); restore(redoStack.back()); redoStack.pop_back(); }

// ============================================================ placement
static bool obbOverlap2D(vec2 c1, vec2 h1, float a1, vec2 c2, vec2 h2, float a2) {
  vec2 ax[4] = {{std::cos(a1), -std::sin(a1)}, {std::sin(a1), std::cos(a1)}, {std::cos(a2), -std::sin(a2)}, {std::sin(a2), std::cos(a2)}};
  vec2 d = c2 - c1;
  for (auto& a : ax) {
    float r1 = h1.x * std::fabs(glm::dot(ax[0], a)) + h1.y * std::fabs(glm::dot(ax[1], a));
    float r2 = h2.x * std::fabs(glm::dot(ax[2], a)) + h2.y * std::fabs(glm::dot(ax[3], a));
    if (std::fabs(glm::dot(d, a)) > r1 + r2) return false;
  }
  return true;
}
PlaceCheck App::checkPlacement(const AssetDef* a, vec3 pos, float yaw, uint64_t ignoreId) const {
  PlaceCheck c;
  vec2 hf = a->footprint * 0.5f;
  quat q = yawQuat(yaw);
  struct S { vec3 local; const char* side; };
  const S samples[] = {{{0, 0, 0}, "center"}, {{-hf.x, 0, -hf.y}, "rear-left"}, {{hf.x, 0, -hf.y}, "rear-right"},
                       {{-hf.x, 0, hf.y}, "front-left"}, {{hf.x, 0, hf.y}, "front-right"}, {{0, 0, -hf.y}, "rear"},
                       {{0, 0, hf.y}, "front"}, {{-hf.x, 0, 0}, "left"}, {{hf.x, 0, 0}, "right"}};
  float top = -1e9f, low = 1e9f; const char* lowSide = "center";
  for (auto& s : samples) {
    vec3 w = pos + q * s.local;
    if (!terrain.inBuildArea(w.x, w.z)) { c.ok = false; c.reason = "Outside the build area."; return c; }
    float h = terrain.height(w.x, w.z);
    if (h < terrain.waterLevel + 0.2f) { c.ok = false; c.reason = std::string("The ") + s.side + " of the footprint is in water."; return c; }
    top = std::max(top, h);
    if (h < low) { low = h; lowSide = s.side; }
  }
  c.y = top;
  float grade = top - low;
  if (grade > std::min(a->maxGrade, a->plinthDepth)) {
    char buf[160];
    std::string side = lowSide; side[0] = (char)std::toupper(side[0]);
    std::snprintf(buf, sizeof buf, "%s foundation is unsupported (ground falls %.1f m; limit %.1f m).", side.c_str(), grade, std::min(a->maxGrade, a->plinthDepth));
    c.ok = false; c.reason = buf; return c;
  }
  for (auto& o : doc.instances) {
    if (o.id == ignoreId) continue;
    const AssetDef* b = assetFor(o);
    if (b->category == "Trees" || b->category == "Bushes") continue;
    if (obbOverlap2D(vec2(pos.x, pos.z), hf - vec2(0.05f), radians(yaw), vec2(o.pos.x, o.pos.z), b->footprint * 0.5f - vec2(0.05f), radians(o.yaw))) {
      c.ok = false; c.reason = "Overlaps " + b->name + " #" + std::to_string(o.id) + "."; return c;
    }
  }
  c.reason = "Valid placement.";
  return c;
}
uint64_t App::placeInstance(const AssetDef* a, vec3 pos, float yaw) {
  PlaceCheck c = checkPlacement(a, pos, yaw, 0);
  if (!c.ok) { toast(c.reason); return 0; }
  pushUndo();
  Instance i; i.id = doc.newId(); i.assetId = a->id; i.assetVersion = a->version;
  i.pos = vec3(pos.x, c.y, pos.z); i.yaw = wrapDeg(yaw);
  bump(i);
  doc.instances.push_back(i);
  return i.id;
}
bool App::duplicateSelected() {
  Instance* s = doc.find(selected); if (!s) return false;
  const AssetDef* a = assetFor(*s);
  vec3 off = yawQuat(s->yaw) * vec3(a->footprint.x + 2.0f, 0, 0);
  for (int tries = 0; tries < 8; ++tries) {
    vec3 p = s->pos + off * (1.0f + tries * 0.5f);
    PlaceCheck c = checkPlacement(a, p, s->yaw, 0);
    if (c.ok) {
      pushUndo();
      Instance n = *doc.find(selected);  // deep copy including authored overrides
      n.id = doc.newId(); n.pos = vec3(p.x, c.y, p.z);
      bump(n);
      doc.instances.push_back(n);
      selected = n.id;
      return true;
    }
  }
  toast("No free space next to the selection for a duplicate.");
  return false;
}
void App::deleteSelected() {
  auto it = std::find_if(doc.instances.begin(), doc.instances.end(), [&](auto& i) { return i.id == selected; });
  if (it == doc.instances.end()) return;
  pushUndo();
  doc.instances.erase(it); selected = 0; dirty = true;
}
uint64_t App::pickInstance(vec3 ro, vec3 rd, float& tBest) const {
  uint64_t best = 0; tBest = 1e30f;
  for (auto& i : doc.instances) {
    const AssetDef* a = assetFor(i);
    quat iq = glm::inverse(yawQuat(i.yaw));
    vec3 lo = iq * (ro - i.pos), ld = iq * rd;
    float t;
    vec3 bc = (a->bmin + a->bmax) * 0.5f, bh = (a->bmax - a->bmin) * 0.5f;
    if (!rayOBB(lo, ld, bc, bh, quat(1, 0, 0, 0), t) || t > tBest) continue;
    for (auto& p : a->parts) {
      if (!p.visible) continue;
      if (rayOBB(lo, ld, p.center, p.size * 0.5f, p.rot, t) && t < tBest) { tBest = t; best = i.id; }
    }
    for (auto& pn : a->panels)
      if (rayOBB(lo, ld, (pn.bmin + pn.bmax) * 0.5f, (pn.bmax - pn.bmin) * 0.5f, quat(1, 0, 0, 0), t) && t < tBest) { tBest = t; best = i.id; }
  }
  return best;
}
void App::cursorRay(vec3& ro, vec3& rd) const {
  double mx, my; glfwGetCursorPos(win, &mx, &my);
  int w, h; glfwGetWindowSize(win, &w, &h);
  vec2 ndc(2.0f * (float)mx / std::max(w, 1) - 1.0f, 1.0f - 2.0f * (float)my / std::max(h, 1));
  if (looking) ndc = vec2(0);
  mat4 inv = glm::inverse(projM * viewM);
  vec4 a = inv * vec4(ndc, -1, 1), b = inv * vec4(ndc, 1, 1);
  ro = vec3(a) / a.w; rd = glm::normalize(vec3(b) / b.w - ro);
}
bool App::startMarkerValid(vec3 pos) const { return !Physics::capsuleBlocked(pos + vec3(0, 0.08f, 0), Physics::standHeight() - 0.06f, Physics::radius()); }

// ============================================================ thumbnails & screenshots
void App::makeThumbnails() {
  for (auto& a : lib.assets) {
    std::vector<DrawItem> op{{&a->opaque, mat4(1)}, {&a->panelsIntact, mat4(1)}}, gl;
    if (a->glass.count) gl.push_back({&a->glass, mat4(1)});
    for (auto& d : a->interactables)
      for (auto& lf : d.leaves) {
        mat4 L = glm::translate(mat4(1), lf.hinge) * glm::mat4_cast(glm::angleAxis(radians(lf.sign * (d.defaultOpen ? d.openAngle : 0)), d.axis));
        op.push_back({&lf.opaque, L});
        if (lf.glass.count) gl.push_back({&lf.glass, L});
      }
    a->thumbnail = R.renderThumbnail(op, gl, a->bmin, a->bmax);
  }
  for (auto& t : templates) {
    if (!t.available) continue;
    terrain.generate(t);
    mat4 v = glm::lookAt(t.camPos, t.camPos + forward(t.camYaw, t.camPitch), vec3(0, 1, 0));
    t.thumbnail = R.renderThumbnail({}, {}, vec3(-1), vec3(1), &terrain, &v);
  }
}
void App::screenshot(const fs::path& p) {
  std::vector<uint8_t> px; int w, h;
  R.readPixels(px, w, h);
  std::vector<uint8_t> flip(px.size());
  for (int y = 0; y < h; ++y) std::memcpy(&flip[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
  for (size_t k = 3; k < flip.size(); k += 4) flip[k] = 255;
  std::error_code ec; fs::create_directories(p.parent_path(), ec);
  stbi_write_png(p.string().c_str(), w, h, 4, flip.data(), w * 4);
  logf("screenshot %s", p.string().c_str());
}

// ============================================================ main loop
int App::run(int argc, char** argv) {
  bool runSelf = false, bench = false; fs::path openPath;
  for (int k = 1; k < argc; ++k) {
    std::string a = argv[k];
    if (a == "--selftest") runSelf = true;
    else if (a == "--benchmark") { runSelf = true; bench = true; }
    else if (a == "--selftest-reopen") { runSelf = true; ++k; }
    else if (a == "--selftest-damage") runSelf = true;
    else openPath = a;
  }
  if (!glfwInit()) { logf("glfwInit failed"); return 1; }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
  int W = runSelf ? 1920 : 1600, H = runSelf ? 1080 : 900;
  win = glfwCreateWindow(W, H, "Normandy Mapmaker", nullptr, nullptr);
  if (!win) { logf("Could not create an OpenGL 4.5 core window. Update your graphics driver."); glfwTerminate(); return 1; }
  glfwMakeContextCurrent(win);
  if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) { logf("gladLoadGL failed"); return 1; }
  logf("GL %s | %s | %s", glGetString(GL_VERSION), glGetString(GL_RENDERER), glGetString(GL_VENDOR));
  glfwSwapInterval(runSelf ? 0 : 1);
  glfwSetScrollCallback(win, scrollCb);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.FontGlobalScale = 1.0f;
  ImGui::StyleColorsDark();
  auto& st = ImGui::GetStyle();
  st.WindowRounding = 4; st.FrameRounding = 3; st.GrabRounding = 3;
  st.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.11f, 0.10f, 0.94f);
  st.Colors[ImGuiCol_Header] = ImVec4(0.32f, 0.36f, 0.26f, 0.8f);
  st.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.42f, 0.46f, 0.32f, 0.9f);
  st.Colors[ImGuiCol_Button] = ImVec4(0.27f, 0.30f, 0.22f, 1.0f);
  st.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.38f, 0.42f, 0.30f, 1.0f);
  st.Colors[ImGuiCol_ButtonActive] = ImVec4(0.48f, 0.52f, 0.36f, 1.0f);
  st.Colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.19f, 0.16f, 1.0f);
  st.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.22f, 0.25f, 0.18f, 1.0f);
  ImGui_ImplGlfw_InitForOpenGL(win, true);
  ImGui_ImplOpenGL3_Init("#version 450 core");

  Physics::init();
  audio.init();
  int fbw, fbh; glfwGetFramebufferSize(win, &fbw, &fbh);
  R.init(fbw, fbh);
  templates = loadTemplates(contentDir() / "templates");
  if (!lib.loadAll(contentDir() / "assets")) logf("warning: no assets found in %s", (contentDir() / "assets").string().c_str());
  makeThumbnails();
  {
    const TemplateInfo* t = findTemplate("blank_slate");
    TemplateInfo fb; fb.id = "blank_slate"; fb.name = "Blank Slate";
    newMap(t ? *t : fb, "My Normandy map");
  }
  if (!openPath.empty()) openMap(openPath);
  if (runSelf) selftest = new SelfTest(bench);

  double last = glfwGetTime();
  bool confirmQuit = false;
  while (!quit) {
    glfwPollEvents();
    if (glfwWindowShouldClose(win)) {
      if (dirty && !selftest) { confirmQuit = true; glfwSetWindowShouldClose(win, GLFW_FALSE); }
      else break;
    }
    double t = glfwGetTime();
    frameDt = (float)std::min(0.1, t - last); last = t; now = t;
    if (selftest) frameDt = 1.0f / 60.0f;  // deterministic simulation for the scripted test
    glfwGetFramebufferSize(win, &fbw, &fbh);
    if (fbw > 0 && fbh > 0) R.resize(fbw, fbh);

    ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
    if (selftest) { selftest->update(*this, frameDt); if (selftest->done) quit = true; }
    if (mode == Mode::Creative) { updateCreative(frameDt); drawCreativeUI(); }
    else { updateExplore(frameDt); drawExploreUI(); }
    syncRuntime();

    if (confirmQuit) { ImGui::OpenPopup("Unsaved changes"); confirmQuit = false; }
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Save changes to \"%s\" before closing?", doc.name.c_str());
      if (ImGui::Button("Save")) { if (!docPath.empty()) { if (saveMapTo(docPath)) quit = true; } else { showSaveAs = true; } ImGui::CloseCurrentPopup(); }
      ImGui::SameLine(); if (ImGui::Button("Discard")) quit = true;
      ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }

    std::vector<DrawItem> op, gl, ghosts; Overlay ov;
    buildDrawLists(op, gl);
    int ww, wh; glfwGetWindowSize(win, &ww, &wh);
    float aspect = fbh > 0 ? (float)fbw / fbh : 1.0f;
    RenderView v;
    if (mode == Mode::Creative) {
      viewM = glm::lookAt(camPos, camPos + forward(camYaw, camPitch), vec3(0, 1, 0));
      projM = glm::perspective(radians(60.0f), aspect, 0.1f, 4000.0f);
      v.editor = true; v.grid = gridOverlay; v.autoExposure = false; v.camPos = camPos;
      drawCreativeOverlay(ov, ghosts);
      audio.setListener(camPos, forward(camYaw, camPitch));
    } else {
      vec3 e = eyePos();
      viewM = glm::lookAt(e, e + forward(eyeYaw, eyePitch), vec3(0, 1, 0));
      projM = glm::perspective(radians(72.0f), aspect, 0.05f, 4000.0f);
      v.editor = false; v.autoExposure = true; v.camPos = e;
      audio.setListener(e, forward(eyeYaw, eyePitch));
    }
    v.view = viewM; v.proj = projM; v.dt = frameDt;
    R.render(v, terrain, op, gl, ghosts, ov);
    if (selftest) selftest->update(*this, -1.0f);  // post-render hook (screenshots)
    ImGui::Render();
    glViewport(0, 0, fbw, fbh);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (!uiShot.empty()) {
      std::vector<uint8_t> px((size_t)fbw * fbh * 4), fl(px.size());
      glReadBuffer(GL_BACK); glReadPixels(0, 0, fbw, fbh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
      for (int y = 0; y < fbh; ++y) std::memcpy(&fl[(size_t)y * fbw * 4], &px[(size_t)(fbh - 1 - y) * fbw * 4], (size_t)fbw * 4);
      for (size_t k = 3; k < fl.size(); k += 4) fl[k] = 255;
      stbi_write_png(uiShot.string().c_str(), fbw, fbh, 4, fl.data(), fbw * 4);
      logf("ui screenshot %s", uiShot.string().c_str()); uiShot.clear();
    }
    glfwSwapBuffers(win);
  }
  int code = selftest ? selftest->exitCode : 0;
  clearRuntime();
  Physics::destroyCharacter();
  audio.shutdown();
  ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
  Physics::shutdown();
  glfwDestroyWindow(win); glfwTerminate();
  return code;
}
