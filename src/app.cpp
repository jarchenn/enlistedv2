#include "app.h"
#include "terrain_tools.h"   // V8 decor exception / draw (beautify.cpp)
#include "editor_nav.h"     // V8: navSyncCuts after an undo that regenerates the terrain
#include "settings.h"
#include "snap.h"
#include "lasso.h"
#include "ground_paint.h"
#include "thumbs.h"
#include "textures.h"
#include "selftest.h"
#include "mpai.h"
#include "w5.h"
#include "w3.h"
#include "nature.h"
#include "autopath.h"
#include "water_query.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include "platform_gfx.h"   // GL 4.5 here, Metal in the normandy-mac fork
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
  for (auto& m : r.pmesh) m.destroy();
  r.pmesh.clear(); r.pshape.clear(); r.wf.clear();
  Physics::remove(r.trunkBody); r.trunkBody = 0xffffffffu;
  Physics::remove(r.playerBody); r.playerBody = 0xffffffffu;
  r.pathMesh.destroy(); r.pathGround = ~0u;
  { extern void natureDestroyRT(InstanceRT&); natureDestroyRT(r); }
  for (auto& v : r.leafBodies) for (auto b : v) Physics::remove(b);
  r.leafBodies.clear();
}
void App::buildRT(const Instance& i, InstanceRT& r) {
  destroyRT(r);
  r.id = i.id; r.rev = i.rev; r.asset = assetFor(i);
  uint64_t base = i.id << 16;
  if (ShapePtr s = snapShape(*this, i, r.asset) ? snapShape(*this, i, r.asset) : interior.staticShape(*this, i, r.asset)) r.staticBody = Physics::addStatic(s, i.pos, yawQuat(i.yaw), base);   // interiors: props, furnished copies
  if (r.asset->tree.on) buildTreeRT(i, r);
  r.defHp = r.asset->defense.hp; r.defDestroyed = false;
  r.secHp.assign(r.asset->defense.sections, r.asset->defense.sectionHp); r.secCut.assign(r.asset->defense.sections, 0);
  if (r.asset->defense.sections > 0) rebuildSectionBodies(i, r);
  else if (r.asset->playerShape && r.asset->defense.traversal != "slow") r.playerBody = Physics::addPlayerOnly(r.asset->playerShape, i.pos, yawQuat(i.yaw), base);
  size_t n = r.asset->interactables.size();
  r.progress.assign(n, 0); r.dir.assign(n, 0); r.leafBodies.assign(n, {});
  r.leafSt.assign(n, {}); r.leafHits.assign(n, {});
  for (size_t k = 0; k < n; ++k) {
    auto& d = r.asset->interactables[k];
    r.leafSt[k].assign(d.leaves.size(), 0); r.leafHits[k].assign(d.leaves.size(), 0.0f);
    for (size_t l = 0; l < d.leaves.size(); ++l) {
      auto s = i.leafState.find(d.id + "/" + std::to_string(l));
      if (s != i.leafState.end()) r.leafSt[k][l] = s->second == 2 ? 3 : 1;   // applied damage: glass broken / leaf gone
    }
  }
  r.hp.assign(r.asset->panels.size(), 0); r.state.assign(r.asset->panels.size(), PANEL_INTACT);
  replayWallDamage(i, r);   // authored wall damage (events; old maps' panel HP converted)
  rebuildWalls(i, r);
  for (size_t k = 0; k < n; ++k) {
    auto& d = r.asset->interactables[k];
    r.progress[k] = authoredOpen(i, d) ? 1.0f : 0.0f;
    for (size_t l = 0; l < d.leaves.size(); ++l) {
      auto& lf = d.leaves[l];
      if (r.leafSt[k][l] & 2) { r.leafBodies[k].push_back(0xffffffffu); continue; }   // leaf gone (applied damage)
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
  R.foliage.clear(); R.worldModels.clear(); worldBoxDraws.clear(); R.natPatches.clear();
  if (mode == Mode::Explore) grenadeDrawItems();
  lassoWaterDrawItems(*this);   // lasso water bodies into V1's water pass (lasso_water.cpp; nothing without water.h)
  R.wind = grass.wind ? 1.0f : 0.0f;
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    auto& r = it->second;
    mat4 M = instanceMatrix(i);
    if (beautifyDecorDraw(*this, i, r, M, op, gl)) continue;   // V8: border-ring decor, cheap at range (beautify.cpp)
    if (isVehicleAsset(r.asset)) { tanks.drawItems(*this, i, r); continue; }   // supplied tank GLB on its rig (tank_ui.cpp)
    if (mode == Mode::Explore) tanks.propMatrix(i, M);   // a prop a tank knocked over / crushed: drawn at its fallen pose (tank_ram.cpp)
    if (w5CullDraw(*this, i, r.asset)) continue;   // W5: small decorators beyond their draw distance (content/settings/maps.json)
    if (r.asset->nature.kind != NatureDef::NONE) {   // boulders, stone / flower / tall-grass patches (nature.cpp)
      float cd = glm::length(i.pos - (mode == Mode::Explore ? eyePos() : camPos));
      if (cd < 1200 * settings().drawDist()) natureDrawItems(*this, i, r, op, cd);
      continue;
    }
    if (r.asset->tree.on) {
      if (glm::length(i.pos - (mode == Mode::Explore ? eyePos() : camPos)) < 900 * settings().drawDist()) treeDrawItems(i, r, op);
      continue;
    }
    if (r.asset->rack) { op.push_back({&r.asset->opaque, M}); rackDrawItems(i, r); continue; }
    if (!r.asset->station.kind.empty()) {   // station: base + yaw / elevation / recoil children
      mat4 b, y, e, rc; stationParts(i, r, b, y, e, rc);
      if (r.asset->model && !r.asset->station.modelRole.empty()) {
        const StationDef& sd = r.asset->station; const GpuModel* gm = r.asset->model.get();
        vec4 tint = r.st.wrecked ? vec4(0.22f, 0.2f, 0.18f, 1) : vec4(1);
        mat4 slump = r.st.wrecked ? glm::rotate(mat4(1), radians(-14.0f), vec3(1, 0, 0)) * glm::rotate(mat4(1), radians(9.0f), vec3(0, 0, 1)) : mat4(1);
        mat4 L[4] = {b, y * glm::translate(mat4(1), -sd.yawPivot), e * slump * glm::translate(mat4(1), -sd.elevPivot), rc * slump * glm::translate(mat4(1), -sd.elevPivot)};
        for (size_t k = 0; k < gm->parts.size() && k < sd.modelRole.size(); ++k) R.worldModels.push_back({gm, (int)k, L[sd.modelRole[k]], tint});
        continue;
      }
      if (r.st.wrecked) {   // wreck: blackened, barrel slumped and slewed off its line
        vec4 burnt(0.22f, 0.2f, 0.18f, 1);
        mat4 slump = glm::rotate(mat4(1), radians(-14.0f), vec3(1, 0, 0)) * glm::rotate(mat4(1), radians(9.0f), vec3(0, 0, 1));
        op.push_back({&r.asset->opaque, b, burnt});
        op.push_back({&r.asset->station.yawMesh, y, burnt});
        op.push_back({&r.asset->station.elevMesh, e * slump, burnt});
        op.push_back({&r.asset->station.recoilMesh, rc * slump, burnt});
        continue;
      }
      int m2w = r.asset->station.kind == "m2hb" ? weaponIndex("m2hb_carry") : -1;
      int g42 = r.asset->station.kind == "mg42" ? weaponIndex("mg42") : -1;
      if (g42 >= 0 && weaponDefs[g42]->useModel()) {   // the supplied MG 42 GLB in the embrasure mount's cradle (docs/bunkers.md)
        op.push_back({&r.asset->opaque, b});
        op.push_back({&r.asset->station.yawMesh, y});
        op.push_back({&r.asset->station.elevMesh, e});
        const GpuModel* gm = weaponDefs[g42]->model.get();
        vec3 bore = weaponDefs[g42]->muzzle; bore.z = 0.45f;   // receiver on the trunnion, bore on the elevation axis
        mat4 gun = rc * glm::translate(mat4(1), -bore);
        for (size_t k = 0; k < gm->parts.size(); ++k) R.worldModels.push_back({gm, (int)k, gun});
        continue;
      }
      if (m2w >= 0 && weaponDefs[m2w]->useModel()) {   // the supplied M2 GLB on the station: tripod on the base, gun on the cradle
        const GpuModel* gm = weaponDefs[m2w]->model.get();
        mat4 tri = b * glm::translate(mat4(1), vec3(0, 0.311f, -0.665f)), gun = rc * glm::translate(mat4(1), vec3(0, -0.181f, -0.665f));
        for (size_t k = 0; k < gm->parts.size(); ++k) {
          float cy = (gm->parts[k].bmin.y + gm->parts[k].bmax.y) * 0.5f;
          R.worldModels.push_back({gm, (int)k, cy < 0.10f ? tri : gun});
        }
        continue;
      }
      op.push_back({&r.asset->opaque, b});
      op.push_back({&r.asset->station.yawMesh, y});
      op.push_back({&r.asset->station.elevMesh, e});
      op.push_back({&r.asset->station.recoilMesh, rc});
      continue;
    }
    if (r.asset->path.on) {   // draped on the current ground; re-draped after any terrain change or edit
      if (r.pathGround != groundRev || r.pathRev != i.rev) rebuildPathMesh(i, r);
      op.push_back({&r.pathMesh, M}); snapDraw(*this, i, op.back());   // V6: world-space pattern across joined ends
      continue;
    }
    { extern bool waterDrawItems(App&, const Instance&, const InstanceRT&, const mat4&, std::vector<DrawItem>&); if (waterDrawItems(*this, i, r, M, op)) continue; }   // V1 water surfaces (water.cpp)
    // coarse distance cull
    float camDist = glm::length(i.pos - (mode == Mode::Explore ? eyePos() : camPos));
    if (camDist > 1200 * settings().drawDist()) continue;   // x the Settings draw distance (Low 0.6, High 1.4)
    if (r.asset->model) {   // supplied model: LOD by distance, cut wire sections hidden; collider boxes are not drawn
      if (camDist > r.asset->modelCull) continue;
      const GpuModel* gm = (r.asset->modelLod && camDist > r.asset->modelLodDist) ? r.asset->modelLod.get() : r.asset->model.get();
      for (size_t k = 0; k < gm->parts.size(); ++k) {
        int s = gm->parts[k].section;
        if (s >= 0 && s < (int)r.secCut.size() && r.secCut[s]) continue;
        R.worldModels.push_back({gm, (int)k, M, vec4(1), r.asset->modelThinPx});
      }
      op.push_back({&r.asset->opaque, M});   // any visible box parts (none for the wire obstacles)
      continue;
    }
    if (r.defDestroyed) { op.push_back({&r.asset->defBrokenMesh, M}); continue; }
    if (interior.propDraw(*this, i, r, M, op, gl)) continue;   // furniture: intact / glass gone / broken (interior.cpp)
    const SurfaceParams* surf = interior.surfaceFor(*this, i, r.asset); size_t opFirst = op.size();   // building finish parameters
    bool noFurn = interior.hideBuiltin(i) && r.asset->noFurnOpaque.count;   // furnished by room templates
    op.push_back({noFurn ? &r.asset->noFurnOpaque : &r.asset->opaque, M}); snapDraw(*this, i, op.back());   // V6: joined ends without caps (snap.cpp)
    if (!r.damaged) op.push_back({&r.asset->panelsIntact, M});
    else for (size_t k = 0; k < r.asset->panels.size(); ++k) op.push_back({k < r.wf.size() && r.wf[k].active ? &r.pmesh[k] : &r.asset->panels[k].mesh[0], M});
    if (noFurn) { if (r.asset->noFurnGlass.count) gl.push_back({&r.asset->noFurnGlass, M}); } else if (r.asset->glass.count) gl.push_back({&r.asset->glass, M});
    for (size_t k = 0; k < r.asset->interactables.size(); ++k) {
      auto& d = r.asset->interactables[k];
      for (size_t l = 0; l < d.leaves.size(); ++l) {
        auto& lf = d.leaves[l];
        uint8_t st = k < r.leafSt.size() && l < r.leafSt[k].size() ? r.leafSt[k][l] : 0;
        if (st & 2) continue;                                  // detached: drawn from its rigid body
        mat4 L = leafMatrix(i, lf, d, r.progress[k]);
        op.push_back({&lf.opaque, L});
        if (lf.glass.count && !(st & 1)) gl.push_back({&lf.glass, L});
      }
    }
    for (size_t k = opFirst; k < op.size(); ++k) op[k].surf = surf;
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
  w3ApplyTerrain(*this, *t);   // map size + chunked terrain + chunk colliders (editor_nav.cpp)
  ++groundRev;
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
  w5NewMapContent(*this, t);   // W5: authored template content (instances, scenario, routes; w5_maps.cpp)
  interior.autoFurnishAll(*this);   // V4: every building furnished (no undo step, not dirty; interior.cpp)
  toast("New map from template: " + t.name);
  return true;
}
bool App::openMap(const fs::path& p) {
  MapDoc d; std::string err;
  if (!loadMap(p, d, err)) { toast("Could not open map: " + err, 8); return false; }
  if (mode == Mode::Explore) exitExplore();
  clearRuntime();
  doc = std::move(d);
  int migrated = 0;
  for (auto& i : doc.instances) {
    i.rev = ++revCounter;
    const AssetDef* a = lib.find(i.assetId);
    if (!a) { toast("Missing asset '" + i.assetId + "' shown as placeholder; its data is preserved.", 8); continue; }
    // an instance saved against an older asset version adopts the current definition (e.g. the tank trap, 1.25x
    // larger in v2): geometry, collision and bounds come from the asset and instances carry no scale, so it is
    // applied exactly once; position (ground pivot) and yaw are kept
    if (i.assetVersion < a->version) { logf("map: %s #%llu asset v%d -> v%d", i.assetId.c_str(), (unsigned long long)i.id, i.assetVersion, a->version); i.assetVersion = a->version; ++migrated; }
  }
  if (migrated) toast(std::to_string(migrated) + " placement(s) updated to the current asset versions.", 6);
  applyTemplate(doc.templateId);
  // applied ground damage is stored against the template version it was cut into; a changed template
  // would misplace it, so it is dropped with a notice rather than misapplied
  if (!doc.terrainDeltas.empty()) {
    const TemplateInfo* t = findTemplate(doc.templateId);
    if (t && doc.terrainTemplateVersion != t->version) {
      toast("Applied ground damage was made for template version " + std::to_string(doc.terrainTemplateVersion) +
            " (now " + std::to_string(t->version) + "); it was not applied.", 10);
      doc.terrainDeltas.clear();
    }
  }
  resetGround();
  if (doc.camera.set) { camPos = doc.camera.pos; camYaw = doc.camera.yaw; camPitch = doc.camera.pitch; }
  bool isContent = p.string().find((contentDir()).string()) == 0;
  docPath = isContent ? fs::path() : p;  // built-in examples open as unsaved copies
  dirty = false; selected = 0; undoStack.clear(); redoStack.clear();
  std::snprintf(nameBuf, sizeof nameBuf, "%s", doc.name.c_str());
  syncRuntime();
  interior.autoFurnishAll(*this);   // V4: buildings without extra.interior get furnished (no undo step, not dirty; interior.cpp)
  toast("Opened " + doc.name + (isContent ? " (copy — Save As to keep)" : ""));
  return true;
}
bool App::saveMapTo(const fs::path& p) {
  if (!mpaiBeforeSave(*this)) return false;   // scenario check (mpui.cpp)
  doc.camera.set = true; doc.camera.pos = camPos; doc.camera.yaw = camYaw; doc.camera.pitch = camPitch;
  if (mode == Mode::Explore) { doc.camera.pos = savedCam.pos; doc.camera.yaw = savedCam.yaw; doc.camera.pitch = savedCam.pitch; }
  std::string err;
  if (!saveMap(p, doc, err)) { toast("Save failed: " + err, 8); return false; }
  docPath = p; dirty = false;
  toast("Saved " + p.filename().string());
  return true;
}

// ============================================================ undo
Snapshot App::snap() const { return Snapshot{doc.instances, doc.start, doc.removedVegetation, selected, doc.terrainDeltas, doc.extra.is_object() && doc.extra.contains("routes") ? doc.extra["routes"] : json(), doc.terrainSculpt, doc.terrainGen, doc.waterBodies, selection, doc.groundPaint, doc.paintSig}; }
void App::restore(const Snapshot& s) {
  bool groundChanged = doc.terrainDeltas != s.terrain || doc.terrainSculpt != s.sculpt;
  bool genChanged = doc.terrainGen != s.terrainGen;   // V8: outer ring / generated hills: the terrain is regenerated
  doc.instances = s.inst; doc.start = s.start; doc.removedVegetation = s.removedVeg; selected = s.selected;
  doc.terrainDeltas = s.terrain; doc.terrainSculpt = s.sculpt; doc.terrainGen = s.terrainGen;
  doc.waterBodies = s.water;   // lasso water fill
  selection = s.selection;     // lasso group
  doc.groundPaint = s.paint; doc.paintSig = s.paintSig;   // lasso ground paint
  if (doc.extra.is_object() || !s.routes.is_null()) { if (!doc.extra.is_object()) doc.extra = json::object(); if (s.routes.is_null()) doc.extra.erase("routes"); else doc.extra["routes"] = s.routes; }   // ground-wear routes (auto paths)
  if (genChanged) { applyTemplate(doc.templateId); navSyncCuts(*this); }   // generate() drops the terrain cuts: re-cut now, not next frame
  if (groundChanged || genChanged) resetGround();
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
  if (a->nature.kind == NatureDef::SCATTER) {   // patches: only the centre must be on dry ground in the build area (items skip water)
    if (!terrain.inBuildArea(pos.x, pos.z) && !decorMayLeaveBorder(*this, ignoreId, pos)) { c.ok = false; c.reason = "Outside the build area."; return c; }   // V8: decor (beautify.cpp)
    c.y = terrain.height(pos.x, pos.z);
    if (c.y < terrain.waterLevel + 0.05f && a->nature.scatter != NatureDef::STONES) { c.ok = false; c.reason = "The patch centre is in water."; return c; }
    c.reason = "Valid placement."; return c;
  }
  vec2 hf = a->footprint * 0.5f;
  quat q = yawQuat(yaw);
  struct S { vec3 local; const char* side; };
  const S samples[] = {{{0, 0, 0}, "center"}, {{-hf.x, 0, -hf.y}, "rear-left"}, {{hf.x, 0, -hf.y}, "rear-right"},
                       {{-hf.x, 0, hf.y}, "front-left"}, {{hf.x, 0, hf.y}, "front-right"}, {{0, 0, -hf.y}, "rear"},
                       {{0, 0, hf.y}, "front"}, {{-hf.x, 0, 0}, "left"}, {{hf.x, 0, 0}, "right"}};
  float top = -1e9f, low = 1e9f; const char* lowSide = "center";
  for (auto& s : samples) {
    vec3 w = pos + q * s.local;
    if (!terrain.inBuildArea(w.x, w.z) && !decorMayLeaveBorder(*this, ignoreId, w)) { c.ok = false; c.reason = "Outside the build area."; return c; }   // V8: decor (beautify.cpp)
    if (a->path.on) continue;   // paths drape on any ground and may cross water's edge, other paths and footprints
    float h = terrain.placementHeight(w.x, w.z, ignoreId);   // ignores the instance's own terrain cut
    if (h < terrain.waterLevel + 0.2f) { c.ok = false; c.reason = std::string("The ") + s.side + " of the footprint is in water."; return c; }
    top = std::max(top, h);
    if (h < low) { low = h; lowSide = s.side; }
  }
  SnapFit sf = snapFit(*this, a, pos, yaw, ignoreId);   // V6: on neighbours' sockets: the chain's base height, their cuts ignored (snap.cpp)
  if (sf.on) { top = sf.top; low = sf.low; }
  c.y = sf.on ? sf.y : top;
  if (a->path.on) { c.y = terrain.height(pos.x, pos.z); return c; }
  if (a->nature.kind != NatureDef::NONE || a->category == "Nature") {   // ground dressing: sits into any slope, overlaps anything
    c.y = a->nature.kind == NatureDef::ROCK ? (low + terrain.height(pos.x, pos.z)) * 0.5f : terrain.height(pos.x, pos.z);   // boulders: half way down to the lowest edge (the sunk base)
    c.reason = "Valid placement."; return c;
  }
  float grade = top - low;
  if (interior.isProp(a)) return const_cast<InteriorSys&>(interior).checkPlacement(const_cast<App&>(*this), a, pos, yaw, ignoreId);   // furniture (lazy caches): floors, walls, keep-clear volumes
  if (!a->defense.kind.empty()) {   // defences: terrain-conforming, limited by slope (theta = acos(N . up)) instead of grade
    float sl = skipSlopeRule ? 0.0f : groundSlopeDeg(pos);
    if (sl > a->defense.maxSlopeDeg) { char b[96]; std::snprintf(b, sizeof b, "Too steep: %.0f deg slope (limit %.0f deg).", sl, a->defense.maxSlopeDeg); c.ok = false; c.reason = b; return c; }
    c.y = terrain.height(pos.x, pos.z);
    grade = 0;
  }
  if (grade > (a->plinthDepth > 0 ? std::min(a->maxGrade, a->plinthDepth) : a->maxGrade)) {
    char buf[160];
    std::string side = lowSide; side[0] = (char)std::toupper(side[0]);
    std::snprintf(buf, sizeof buf, "%s foundation is unsupported (ground falls %.1f m; limit %.1f m).", side.c_str(), grade, (a->plinthDepth > 0 ? std::min(a->maxGrade, a->plinthDepth) : a->maxGrade));
    c.ok = false; c.reason = buf; return c;
  }
  if (crushBlocked(*this, a, pos, yaw, ignoreId, c.reason)) { c.ok = false; return c; }   // V6: growth taller than the crush limit
  for (auto& o : doc.instances) {
    if (o.id == ignoreId || std::find(sf.neighbours.begin(), sf.neighbours.end(), o.id) != sf.neighbours.end()) continue;   // snapped neighbours touch
    if (placeIgnore && std::find(placeIgnore->begin(), placeIgnore->end(), o.id) != placeIgnore->end()) continue;   // lasso group: members move together
    const AssetDef* b = assetFor(o);
    if (b->category == "Trees" || b->category == "Bushes" || b->category == "Nature" || b->nature.kind != NatureDef::NONE) continue;
    if (crushBuilding(a) && crushVegetation(b)) continue;   // V6: low growth is crushed, taller growth blocked above (crushBlocked)
    if (a->defense.kind == "sandbag" && b->defense.kind == "sandbag") continue;   // sandbag runs chain end to end
    if (interior.isProp(b)) continue;   // furniture is checked by its own placement rules
    if (obbOverlap2D(vec2(pos.x, pos.z), hf - vec2(0.05f), radians(yaw), vec2(o.pos.x, o.pos.z), b->footprint * 0.5f - vec2(0.05f), radians(o.yaw))) {
      c.ok = false; c.reason = "Overlaps " + b->name + " #" + std::to_string(o.id) + "."; return c;
    }
  }
  c.reason = "Valid placement.";
  return c;
}
// Companions: crew stations and ammunition an asset ships with ("companions": [{asset, pos, yaw}] in asset space),
// placed as ordinary instances on the host's floor when the host is placed from the palette (docs/bunkers.md).
int App::placeCompanions(uint64_t hostId) {
  Instance* h = doc.find(hostId); if (!h) return 0;
  const AssetDef* a = assetFor(*h);
  if (!a || !a->raw.contains("companions")) return 0;
  vec3 hp = h->pos; float hy = h->yaw; int n = 0;
  for (auto& c : a->raw["companions"]) {
    const AssetDef* ca = lib.find(c.value("asset", ""));
    if (!ca || !c.contains("pos")) continue;
    Instance i; i.id = doc.newId(); i.assetId = ca->id; i.assetVersion = ca->version;
    i.pos = hp + yawQuat(hy) * vec3(c["pos"][0].get<float>(), c["pos"][1].get<float>(), c["pos"][2].get<float>());
    i.yaw = wrapDeg(hy + c.value("yaw", 0.0f));
    bump(i);
    doc.instances.push_back(i);
    ++n;
  }
  return n;
}
uint64_t App::placeInstance(const AssetDef* a, vec3 pos, float yaw) {
  PlaceCheck c = checkPlacement(a, pos, yaw, 0);
  if (!c.ok) { toast(c.reason); return 0; }
  pushUndo();
  Instance i; i.id = doc.newId(); i.assetId = a->id; i.assetVersion = a->version;
  i.pos = c.snapXZ ? vec3(c.xz.x, c.y, c.xz.y) : vec3(pos.x, c.y, pos.z); i.yaw = wrapDeg(yaw);
  if (a->defense.blocksVehicles) { if (!i.extra.is_object()) i.extra = json::object(); i.extra["blocksVehicles"] = true; }
  bump(i);
  doc.instances.push_back(i);
  autopathOnPlaced(*this, i.id);   // "Auto paths" on: a new house gets its aprons and a path to its neighbours (autopath.cpp)
  if (interior.autoOnPlace) interior.autoFurnish(*this, i.id);   // V4: a new building comes furnished (interior.cpp)
  return i.id;
}
bool App::duplicateSelected() {
  if (selection.size() > 1) return lassoDuplicateGroup(*this) > 0;   // a lasso group (lasso.cpp)
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
      if (isVehicleAsset(a)) TankSys::stripDamage(n.extra);   // a duplicated tank is a fresh tank (spawn config only)
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
  if (selection.size() > 1) { lassoDeleteGroup(*this); return; }   // a lasso group (lasso.cpp)
  auto it = std::find_if(doc.instances.begin(), doc.instances.end(), [&](auto& i) { return i.id == selected; });
  if (it == doc.instances.end()) return;
  pushUndo();
  uint64_t gone = it->id;
  doc.instances.erase(it); selected = 0; dirty = true;
  interior.dropPieces(*this, gone);   // V4: its room-template furniture goes with it (interior.cpp)
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
    if (isVehicleAsset(a)) { tBest = t; best = i.id; continue; }   // tanks: the model bounds (collider parts are invisible)
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
  makeAssetThumbnails(*this);   // thumbs.cpp (W2): silhouette framing, models and wire, disk cache
  for (auto& t : templates) {
    if (!t.available) continue;
    if (w5TemplateThumbnail(t)) continue;   // W5: pre-rendered default-map preview
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
  std::vector<SightMark> mk;
  if (sightMarkers && sightMarks(mk)) {
    auto put = [&](int x, int y, uint32_t c) { if (x < 0 || y < 0 || x >= w || y >= h) return; uint8_t* p = &flip[((size_t)y * w + x) * 4]; p[0] = c & 255; p[1] = (c >> 8) & 255; p[2] = (c >> 16) & 255; };
    float sc = std::max(1.0f, h / 1080.0f);
    for (auto& m : mk) {
      int cx = (int)std::floor(m.px.x), cy = (int)std::floor(m.px.y);
      int r = (int)(6 * sc);
      if (m.kind == 0 || m.kind == 4) for (int a = 0; a < 96; ++a) { float t = a * 6.2831853f / 96; put(cx + (int)std::lround(std::cos(t) * r), cy + (int)std::lround(std::sin(t) * r), m.col); }
      else if (m.kind == 1) for (int k = -r; k <= r; ++k) { if (std::abs(k) > 1) { put(cx + k, cy, m.col); put(cx, cy + k, m.col); } }
      else if (m.kind == 2) for (int k = -r / 2; k <= r / 2; ++k) { put(cx + k, cy - r / 2, m.col); put(cx + k, cy + r / 2, m.col); put(cx - r / 2, cy + k, m.col); put(cx + r / 2, cy + k, m.col); }
      else for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) put(cx + x, cy + y, m.col);
    }
    // the HUD centre dot (drawn by ImGui, so not otherwise in the scene capture)
    for (int y = -2; y <= 2; ++y) for (int x = -2; x <= 2; ++x) if (x * x + y * y <= 5) put(w / 2 + x, h / 2 + y, 0xFFE6F0F0u);
  }
  std::error_code ec; fs::create_directories(p.parent_path(), ec);
  stbi_write_png(p.string().c_str(), w, h, 4, flip.data(), w * 4);
  logf("screenshot %s", p.string().c_str());
}

// ============================================================ main loop
int App::run(int argc, char** argv) {
  bool runSelf = false, bench = false, legacyLook = false; std::vector<std::string> rqOff; fs::path openPath, sheetsDir, previewGlb, previewOut, previewSpec, tankPreviewDir;
  for (int k = 1; k < argc; ++k) {
    std::string a = argv[k];
    if (mpaiArg(*this, argc, argv, k, runSelf)) { if (mpaiExit >= 0) return mpaiExit; continue; }   // multiplayer + AI flags (mpai.cpp)
    else if (a == "--selftest") runSelf = true;
    else if (a == "--benchmark") { runSelf = true; bench = true; }
    else if (a == "--selftest-terrain") runSelf = true;   // V8 sculpt, hills, Beautify (selftest_terrain.cpp)
    else if (a == "--selftest-lasso") runSelf = true;     // lasso tool (selftest_lasso.cpp)
    else if (a == "--selftest-reopen") { runSelf = true; ++k; }
    else if (a == "--selftest-damage" || a == "--selftest-pak" || a == "--selftest-weapons") runSelf = true;
    else if (a == "--capture-materials") { runSelf = true; ++k; }
    else if (a == "--grass" && k + 1 < argc) { std::string q = argv[++k]; grass.quality = q == "off" ? GrassQuality::Off : q == "low" ? GrassQuality::Low : q == "high" ? GrassQuality::High : GrassQuality::Medium; }
    else if (a == "--selftest-gore") runSelf = true;
    else if (a == "--selftest-modes") runSelf = true;   // G1 Solo / TDM modes (selftest_modes.cpp)   // W7 gore suite (selftest_gore.cpp)
    else if (a == "--selftest-nature") runSelf = true;   // natural ground dressing (selftest_nature.cpp)
    else if (a == "--selftest-settings") runSelf = true;   // V2 settings / presets / firing smoke (selftest_settings.cpp)
    else if (a == "--selftest-autopath") runSelf = true;   // V9 auto paths between houses (selftest_autopath.cpp)
    else if (a == "--selftest-snap") runSelf = true;     // V6: snapping (selftest_snap.cpp)
    else if (a == "--selftest-render") runSelf = true;   // V1 facades, walls, water (selftest_render.cpp)
    else if (a == "--selftest-lights") runSelf = true;   // L2 lighting engine (selftest_lights.cpp)
    else if (a == "--selftest-swim") runSelf = true;     // swimming, underwater view, breath (selftest_swim.cpp)
    else if (a == "--selftest-tanks") runSelf = true;   // tank suite (selftest_tanks.cpp)
    else if (a == "--selftest-interiors") runSelf = true;   // walls, interiors and furniture (selftest_interiors.cpp)
    else if (a == "--selftest-structures") runSelf = true;   // W4 structure assets (selftest_structures.cpp)
    else if (a == "--selftest-maps" || a == "--render-map-thumbs") runSelf = true;   // W5 default maps (selftest_maps.cpp)
    else if (a == "--selftest-grass" || a == "--build-range-map" || a == "--record-range" || a == "--selftest-fps" || a == "--selftest-env" || a == "--build-village-map" || a == "--selftest-arsenal" || a == "--selftest-move" || a == "--selftest-sights" || a == "--selftest-update" || a == "--selftest-entities" || a == "--selftest-editor") runSelf = true;
    else if (a == "--legacy-look") legacyLook = true;   // the pre-BSL-pass lighting / tonemap (A/B and fallback)
    else if (a == "--no-textures") tex::setEnabled(false);   // L4 A/B: procedural patterns only (textures.cpp)
    else if (a == "--selftest-textures") runSelf = true;   // L4 texture layer + missing-texture fallback (selftest_textures.cpp)
    else if (a == "--pack-texture" && k + 2 < argc) { fs::path s = argv[k + 1], d = argv[k + 2]; return tex::packSet(s, d); }   // tools/fetch_ambientcg.py
    else if (a == "--texture-placeholders" && k + 1 < argc) { std::vector<std::string> v; std::string l = argv[++k]; for (size_t b = 0, e; b <= l.size(); b = e + 1) { e = l.find(',', b); if (e == std::string::npos) e = l.size(); if (e > b) v.push_back(l.substr(b, e - b)); } return tex::writePlaceholders(k + 1 < argc ? fs::path(argv[k + 1]) : contentDir() / "textures", v); }
    else if (a == "--rq-off" && k + 1 < argc) rqOff.push_back(argv[++k]);   // switch one render feature off (A/B): taa ssr ssao bloom shafts cascade cloudshadow
    else if (a == "--benchmark-village") { runSelf = true; bench = true; if (k + 1 < argc && argv[k + 1][0] != '-') ++k; }
    else if (a == "--benchmark-stress") { runSelf = true; bench = true; }
    else if (a == "--selftest-visuals") runSelf = true;   // W2: thumbnails, footprint labels, bunker pass (selftest_visuals.cpp)
    else if (a == "--box-weapons") useImportedModels = false;
    else if (a == "--weapon-sheets" && k + 1 < argc) sheetsDir = argv[++k];
    else if (a == "--model-preview" && k + 2 < argc) { previewGlb = argv[++k]; previewOut = argv[++k]; if (k + 1 < argc && argv[k + 1][0] != '-') previewSpec = argv[++k]; }
    else if (a == "--tank-preview" && k + 1 < argc) tankPreviewDir = argv[++k];   // tank.cpp: views of the baked tanks + rig sockets
    else if (a == "--import-vehicle-models" && k + 1 < argc) {   // tanks (tank_import.cpp): <srcDir> [content root]
      fs::path src = argv[++k], out = k + 1 < argc ? fs::path(argv[++k]) : contentDir();
      return importVehicleModels(src, out) ? 0 : 1;
    }
    else if (a == "--import-weapon-models" && k + 1 < argc) {
      // offline bake: <srcDir with .glb files> [content root to write into, default: the running content/]
      fs::path src = argv[++k], out = k + 1 < argc ? fs::path(argv[++k]) : contentDir();
      return importWeaponModels(src, out) ? 0 : 1;
    }
    else openPath = a;
  }
  if (!glfwInit()) { logf("glfwInit failed"); return 1; }
  platformWindowHints();
  int W = runSelf ? 1920 : 1600, H = runSelf ? 1080 : 900;
  win = glfwCreateWindow(W, H, "Normandy Mapmaker", nullptr, nullptr);
  if (!win) { logf("Could not create an OpenGL 4.5 core window. Update your graphics driver."); glfwTerminate(); return 1; }
  if (!platformInitContext(win)) { logf("graphics context init failed"); return 1; }
  logf("GL %s | %s | %s", glGetString(GL_VERSION), glGetString(GL_RENDERER), glGetString(GL_VENDOR));
  platformSwapInterval(runSelf ? 0 : 1);
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
  platformImGuiInit(win);

  Physics::init();
  audio.init();
  audio.loadEvents(contentDir() / "audio");
  int fbw, fbh; glfwGetFramebufferSize(win, &fbw, &fbh);
  R.init(fbw, fbh);
  settingsInit(*this, runSelf);   // settings_user.json: presets, gameplay, audio, keys, UI (settings.cpp)
  R.q.legacy = legacyLook;
  for (auto& f : rqOff) { if (f == "taa") R.q.taa = false; else if (f == "ssr") R.q.ssr = false; else if (f == "ssao") R.q.ssao = false; else if (f == "bloom") R.q.bloom = false; else if (f == "shafts") R.q.shafts = false; else if (f == "cascade") R.q.farCascade = false; else if (f == "cloudshadow") R.q.cloudShadows = false; else if (f == "wind") grass.wind = false; }
  templates = loadTemplates(contentDir() / "templates");
  if (!lib.loadAll(contentDir() / "assets")) logf("warning: no assets found in %s", (contentDir() / "assets").string().c_str());
  makeThumbnails();
  {
    Material rm{"debris", vec3(0.30f, 0.27f, 0.21f), 0.95f, 0.0f, PAT_STONE, 1};
    Part p; p.center = vec3(0); p.size = vec3(1);
    MeshData m; appendPart(m, p, rm, mat4(1)); debrisMesh.upload(m);
    buildWeaponMeshes();
    buildArmMeshes();
    loadBindings();
    loadMoveSettings();
    layoutRackDisplays();
    resetLoadout();
  }
  if (!sheetsDir.empty()) { writeWeaponSheets(sheetsDir); glfwTerminate(); return 0; }
  if (!previewGlb.empty()) { bool ok = writeModelPreview(previewGlb, previewOut, previewSpec); glfwTerminate(); return ok ? 0 : 1; }
  tanks.loadDefs(*this); tanks.makeThumbnails(*this);
  if (!tankPreviewDir.empty()) { tanks.writePreview(*this, tankPreviewDir); glfwTerminate(); return 0; }
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
    if (selftest && !selftest->realTime) frameDt = selftest->slowFrames ? 0.1f : 1.0f / 60.0f;  // deterministic simulation for the scripted test
    glfwGetFramebufferSize(win, &fbw, &fbh);
    if (fbw > 0 && fbh > 0) R.resize(fbw, fbh);

    platformImGuiNewFrame(); ImGui::NewFrame();
    if (selftest) { selftest->update(*this, frameDt); if (selftest->done) quit = true; }
    if (mode == Mode::Creative) { updateCreative(frameDt); drawCreativeUI(); }
    else { updateExplore(frameDt); drawExploreUI(); }
    mpaiUI(*this);   // Play menu, scenario panel, match HUD
    settingsWindow(*this);   // Settings window (Creative menu, pause menu, Loadout)
    syncRuntime();
    tanks.sync(*this);
    interior.sync(*this);   // generated furniture follows / leaves with its building

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
    op.insert(op.end(), worldBoxDraws.begin(), worldBoxDraws.end());
    if (mode == Mode::Explore) {
      dynLeafDrawItems(op);
      interior.fragDrawItems(op);
      builderGhosts(ghosts);
      for (auto& d : fx.debris) op.push_back({&debrisMesh, glm::translate(mat4(1), d.p) * glm::mat4_cast(d.r) * glm::scale(mat4(1), d.size)});
      if (!tanks.occupied) weaponDrawItems(op);
    }
    int ww, wh; glfwGetWindowSize(win, &ww, &wh);
    // minimized windows report a 0x0 framebuffer: keep the aspect of the targets we actually render into
    float aspect = (fbw > 0 && fbh > 0) ? (float)fbw / fbh : (float)R.width / std::max(1, R.height);
    RenderView v;
    if (mode == Mode::Creative) {
      viewM = glm::lookAt(camPos, camPos + forward(camYaw, camPitch), vec3(0, 1, 0));
      projM = w3EditorProjection(*this, aspect);   // 60 deg; near / far follow height and map size (editor_nav.cpp)
      v.editor = !hideEditorOverlays; v.grid = gridOverlay; v.autoExposure = false; v.camPos = camPos;
      if (!hideEditorOverlays) drawCreativeOverlay(ov, ghosts);
      audio.setListener(camPos, forward(camYaw, camPitch));
    } else {
      float shk = reducedMotion ? 0.0f : blastShake * blastShake * 1.6f;   // concussion shake (deg)
      vec3 e = eyePos(), fv = forward(eyeYaw + viewKickYaw + shk * std::sin((float)now * 41.0f), eyePitch + viewKickPitch + shk * std::sin((float)now * 53.0f + 1.3f));
      float fov = settings().fov();   // 72 unless changed in Settings
      if (!tanks.camera(*this, e, fv, fov) && !stationCamera(e, fv, fov)) weaponCamera(fov);
      mpaiCamera(*this, e, fv, fov);   // death / spectator camera during a match
      viewM = glm::lookAt(e, e + fv, viewUp(fv));   // swim.cpp: a slight roll with the stroke
      projM = glm::perspective(radians(fov), aspect, 0.03f, 4000.0f);
      worldFov = fov;
      v.vmProj = glm::perspective(radians(vmFovNow()), aspect, 0.01f, 6.0f);
      dofParams(v.dofFocus, v.dofAmount);
      v.editor = false; v.autoExposure = true; v.camPos = e;
      v.underwater = underwaterAt(*this, e); v.underFogCol = swimCfg.fogColor; v.underFogDens = swimCfg.fogDensity;   // swim.cpp
      audio.setListener(e, forward(eyeYaw, eyePitch));
    }
    audio.setUnderwater(v.underwater ? 1.0f : 0.0f);   // swim.cpp: muffled under water
    mpaiFrame(*this, ov);   // soldiers, grenades, scenario overlay
    v.view = viewM; v.proj = projM; v.dt = frameDt;
    R.fxAlpha.clear(); R.fxAdd.clear();
    if (mode == Mode::Explore) fx.build(viewM, R.fxAlpha, R.fxAdd);
    w3Frame(*this);   // terrain cuts, chunk physics sync, editor camera flight (editor_nav.cpp)
    updateGrass(v.camPos);
    R.grass = &grass;
    { extern void lightingAppFrame(App&); lightingAppFrame(*this); }   // L2: the frame's lights (src/lights.h) and room volumes
    R.render(v, terrain, op, gl, ghosts, ov);
    if (selftest) selftest->update(*this, -1.0f);  // post-render hook (screenshots)
    if (!recCaption.empty()) {   // recording caption, over every other overlay
      ImDrawList* dl = ImGui::GetForegroundDrawList(); ImGuiIO& io = ImGui::GetIO();
      float fs = io.DisplaySize.y / 30.0f;
      ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0, recCaption.c_str());
      ImVec2 p(28, io.DisplaySize.y - fs * 2.6f);
      dl->AddRectFilled(ImVec2(p.x - 12, p.y - 8), ImVec2(p.x + ts.x + 12, p.y + ts.y + 8), IM_COL32(10, 10, 8, 200), 6);
      dl->AddText(ImGui::GetFont(), fs, p, IM_COL32(250, 240, 210, 255), recCaption.c_str());
    }
    ImGui::Render();
    glViewport(0, 0, fbw, fbh);
    platformImGuiRender(ImGui::GetDrawData());
    if (rec.active() && (++recTick & 1)) {   // 30 fps from the fixed 60 Hz simulation, HUD included
      recPx.resize((size_t)fbw * fbh * 4);
      glReadBuffer(GL_BACK); glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, fbw, fbh, GL_RGBA, GL_UNSIGNED_BYTE, recPx.data());
      rec.frame(recPx.data(), fbw, fbh);
    }
    if (!uiShot.empty()) {
      std::vector<uint8_t> px((size_t)fbw * fbh * 4), fl(px.size());
      glReadBuffer(GL_BACK); glReadPixels(0, 0, fbw, fbh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
      for (int y = 0; y < fbh; ++y) std::memcpy(&fl[(size_t)y * fbw * 4], &px[(size_t)(fbh - 1 - y) * fbw * 4], (size_t)fbw * 4);
      for (size_t k = 3; k < fl.size(); k += 4) fl[k] = 255;
      stbi_write_png(uiShot.string().c_str(), fbw, fbh, 4, fl.data(), fbw * 4);
      logf("ui screenshot %s", uiShot.string().c_str()); uiShot.clear();
    }
    platformPresent(win);
    settingsEndFrame(*this);   // FPS cap
  }
  int code = selftest ? selftest->exitCode : 0;
  clearRuntime();
  Physics::destroyCharacter();
  audio.shutdown();
  platformImGuiShutdown(); ImGui::DestroyContext();
  Physics::shutdown();
  glfwDestroyWindow(win); glfwTerminate();
  return code;
}

// ============================================================ grass
std::vector<GrassFootprint> App::grassFootprints() const {
  // every placed structure clears grass inside its geometric bounds (+ margin: steps, thresholds, berms);
  // field guns and crates sit in the grass and are not excluded.
  std::vector<GrassFootprint> fp;
  for (auto& i : doc.instances) {
    const AssetDef* a = assetFor(i);
    if (!a || a->station.kind.size() || !a->crate.empty() || isVehicleAsset(a)) continue;   // tanks drive away: no fixed grass hole
    if (isFurnishPiece(i)) continue;   // V4: room-template furniture stands inside its building's footprint
    if (a->nature.kind == NatureDef::SCATTER) { GrassFootprint f; if (natureGrassFootprint(i, *a, f)) fp.push_back(f); continue; }   // flowers / tall grass / stones grow in the grass; a wheat tile clears it
    if (a->nature.kind == NatureDef::ROCK) { GrassFootprint f{vec2(i.pos.x, i.pos.z), i.yaw, vec2(a->bmin.x, a->bmin.z) * 0.78f, vec2(a->bmax.x, a->bmax.z) * 0.78f}; f.margin = 0.0f; fp.push_back(f); continue; }   // grass up to the boulder's flanks
    if (a->tree.on && !a->tree.hedge) { float t = a->tree.trunkR + 0.1f; fp.push_back({vec2(i.pos.x, i.pos.z), i.yaw, vec2(-t), vec2(t)}); continue; }   // grass grows up to the trunk
    if (a->path.on) { vec2 mn, mx; pathLocalBounds(*this, i, a, mn, mx); GrassFootprint f{vec2(i.pos.x, i.pos.z), i.yaw, mn + vec2(0.06f, 0), mx - vec2(0.06f, 0)}; f.margin = 0.05f; fp.push_back(f); continue; }   // the trodden surface only, verges keep their grass
    if (a->tree.on && a->tree.hedge) { vec2 h = a->footprint * 0.5f; fp.push_back({vec2(i.pos.x, i.pos.z), i.yaw, vec2(-h.x, -h.y * 0.6f), vec2(h.x, h.y * 0.6f)}); continue; }
    fp.push_back({vec2(i.pos.x, i.pos.z), i.yaw, vec2(a->bmin.x, a->bmin.z), vec2(a->bmax.x, a->bmax.z)});
  }
  return fp;
}

void App::updateSurface() {
  groundPaintSync(*this);   // lasso ground paint: the GPU copy follows the map (ground_paint.cpp)
  // structures (not guns, crates, trees or defences) get a foundation soil band; routes come from the map
  std::vector<SurfaceFootprint> feet;
  uint64_t sig = 1469598103934665603ull;
  auto mix = [&](uint64_t v) { sig = (sig ^ v) * 1099511628211ull; };
  for (auto& i : doc.instances) {
    const AssetDef* a = assetFor(i);
    if (!a || a->station.kind.size() || (a->crate.is_object() && !a->crate.empty()) || a->tree.on || !a->defense.kind.empty() || a->rack || isVehicleAsset(a) || a->nature.kind != NatureDef::NONE) continue;
    { extern bool waterAsset(const AssetDef*); if (waterAsset(a)) continue; }   // V1: ponds / streams / the sea have no foundation band
    if (isFurnishPiece(i)) continue;   // V4: furniture inside a building has no foundation band of its own
    feet.push_back({vec2(i.pos.x, i.pos.z), i.yaw, vec2(a->bmin.x, a->bmin.z), vec2(a->bmax.x, a->bmax.z), 1.0f});
    uint32_t b[3]; std::memcpy(&b[0], &i.pos.x, 4); std::memcpy(&b[1], &i.pos.z, 4); std::memcpy(&b[2], &i.yaw, 4);
    mix(b[0]); mix(b[1]); mix(b[2]); mix(std::hash<std::string>{}(i.assetId));
  }
  json routes = doc.extra.is_object() && doc.extra.contains("routes") ? doc.extra["routes"] : json::array();
  mix(std::hash<std::string>{}(routes.dump()));
  mix(terrain.generation);
  if (sig == terrain.surfSig && !terrain.surf.empty()) return;
  terrain.buildSurface(routes, feet);
  terrain.surfSig = sig;
}

void App::updateGrass(vec3 cam) {
  updateSurface();
  uint32_t seed = doc.seed * 2654435761u + (uint32_t)doc.templateVersion * 40503u + (uint32_t)std::hash<std::string>{}(doc.templateId);
  grass.update(terrain, seed, grassFootprints(), cam, grassBudgetMs);
}
