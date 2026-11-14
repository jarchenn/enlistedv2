#include "app.h"
#include "hlod.h"
#include "abilities.h"
#include "crumb.h"
#include "terrain_tools.h"   // V8 decor exception / draw (beautify.cpp)
#include "editor_nav.h"     // V8: navSyncCuts after an undo that regenerates the terrain
#include "settings.h"
#include "snap.h"
#include "lasso.h"
#include "ground_paint.h"
#include "thumbs.h"
#include "textures.h"
#include "selftest.h"
#include <chrono>
#include "mpai.h"
#include "w5.h"
#include "w3.h"
#include "nature.h"
#include "autopath.h"
#include "water_query.h"
#include "ai_hooks.h"
#include "lights.h"   // GX2: dust motes
#include "terrain_blend.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include "foundation.h"       // MA3 foundation dressing
#include "platform_gfx.h"   // GL 4.5 here, Metal in the normandy-mac fork
#include "classes.h"
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
  r.earthMesh.destroy(); r.earthKey = 0; r.earthGround = ~0u;
  r.lineMesh.destroy(); r.line.reset(); r.lineGround = ~0u;
  { extern void natureDestroyRT(InstanceRT&); natureDestroyRT(r); }
  for (auto& v : r.leafBodies) for (auto b : v) Physics::remove(b);
  r.leafBodies.clear();
  { extern void ivyDestroy(InstanceRT&); ivyDestroy(r); }
}
void App::buildRT(const Instance& i, InstanceRT& r) {
  destroyRT(r);
  r.id = i.id; r.rev = i.rev; r.asset = assetFor(i);
  uint64_t base = i.id << 16;
  if (ShapePtr s = snapShape(*this, i, r.asset) ? snapShape(*this, i, r.asset) : interior.staticShape(*this, i, r.asset)) r.staticBody = Physics::addStatic(s, i.pos, yawQuat(i.yaw), base);   // interiors: props, furnished copies
  if (r.asset->tree.on) buildTreeRT(i, r);
  r.defHp = r.asset->defense.hp; r.defDestroyed = false;
  r.secHp.assign(r.asset->defense.sections, r.asset->defense.sectionHp); r.secCut.assign(r.asset->defense.sections, 0);
  extern bool lineBuildRT(App&, const Instance&, InstanceRT&);
  if (lineBuildRT(*this, i, r)) rebuildSectionBodies(i, r);   // LN1: a line's mesh, bays (sections) and colliders
  else if (r.asset->defense.sections > 0) rebuildSectionBodies(i, r);
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
// every instance's id, revision and placement, hashed: the per-frame passes below skip their work while it holds.
// O(N) with no lookups; a revision bumped by hand (i.rev++) or a moved instance changes it as well.
uint64_t placementSig(const std::vector<Instance>& v, bool withPose) {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](uint64_t x) { h = (h ^ x) * 1099511628211ull; };
  for (auto& i : v) {
    mix(i.id * 0x9E3779B97F4A7C15ull + i.rev);
    if (withPose) { uint32_t b[4]; std::memcpy(b, &i.pos, 12); std::memcpy(&b[3], &i.yaw, 4); mix(((uint64_t)b[0] << 32) | b[2]); mix(((uint64_t)b[1] << 32) | b[3]); }
  }
  return h ^ v.size();
}
// found again whenever an instance appears, goes away or changes revision, or the runtime is rebuilt
const App::ActiveSet& App::activeSet() {
  ActiveSet& s = active_;
  uint64_t key = placementSig(doc.instances, false) ^ (syncSig_ * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)rt.size() << 40);
  if (key == s.key) return s;
  s.key = key; s.inter.clear(); s.objs.clear(); s.vehicles.clear(); s.lights.clear(); s.slowWire.clear(); s.rtOf.assign(doc.instances.size(), nullptr);
  for (size_t k = 0; k < doc.instances.size(); ++k) {
    auto it = rt.find(doc.instances[k].id); if (it == rt.end()) continue;
    s.rtOf[k] = &it->second;
    if (!it->second.asset) continue;
    const AssetDef* a = it->second.asset;
    if (!a->interactables.empty()) s.inter.push_back({k, &it->second});
    if (!a->station.kind.empty() || a->rack || (a->crate.is_object() && !a->crate.empty())) s.objs.push_back({k, &it->second});
    if (isVehicleAsset(a)) s.vehicles.push_back({k, &it->second});
    if (a->raw.is_object() && a->raw.contains("light")) s.lights.push_back({k, &it->second});   // lightDef (lights.cpp)
    if (a->defense.kind == "wire" && a->defense.traversal == "slow") s.slowWire.push_back({k, &it->second});   // traversalFactor (builder.cpp)
  }
  return s;
}
void App::syncRuntime() {
  uint64_t sig = placementSig(doc.instances, false);
  if (sig == syncSig_ && rt.size() == doc.instances.size()) return;   // nothing appeared, went away or changed revision
  { extern int lineSyncGates(App&); if (lineSyncGates(*this)) sig = placementSig(doc.instances, false); }   // LN2: line gates follow their spans
  syncSig_ = sig;   // taken before the pass: anything a rebuild changes is seen by the next call
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
  foundationDrawItems(*this, op, mode == Mode::Explore ? eyePos() : camPos);   // MA3: foundation plants + stones (foundation.cpp)
  interior.beginFrame();   // R2 room culling: per-building visibility once per frame
  if (mode == Mode::Explore) { grenadeDrawItems(); gm2DrawItems(*this, op); }   // GM2: placed packs
  lassoWaterDrawItems(*this);   // lasso water bodies into V1's water pass (lasso_water.cpp; nothing without water.h)
  R.wind = grass.wind ? 1.0f : 0.0f;
  lodPxPerM = projM[1][1] != 0.0f ? projM[1][1] * R.height * 0.5f : 0.0f; lodCount[0] = lodCount[1] = lodCount[2] = 0;
  leafMergeHit = leafMergeMiss = 0;
  static int dlFrameNo = 0; ++dlFrameNo; R.stats.occCulled = 0;   // GP1 occcull
  hlodFrame(*this);   // PF2 Phase B: far buildings merged per cell (hlod.cpp)
  const ActiveSet& act = activeSet();
  // P2 measurement (NMK_DLPROF=1): items per source category, logged every 240 frames
  static const bool dlProf = std::getenv("NMK_DLPROF") != nullptr; static int dlFrame = 0; static double dlCat[6][4] = {}; static double dlBand[3][4][2] = {}; static double dlLeafT[4] = {};
  // P2 dlcache: room-template furniture past InteriorSys::cullBig draws nothing (propDraw culls it, except the piece that
  // pushes its building's merged batch), so those pieces skip the whole per-instance path. The furnished flags are cached
  // until an edit, an add or a removal; the distance test runs every frame.
  static std::vector<uint8_t> furn; static uint64_t furnSig = 0;
  const vec3 dlEye = mode == Mode::Explore ? eyePos() : camPos;
  const float farFurn2 = (interior.curBig + 1.0f) * (interior.curBig + 1.0f);
  if (R.perf.dlCache) {
    uint64_t fs = ((uint64_t)revCounter << 32) ^ ((uint64_t)doc.instances.size() << 1) ^ (uint64_t)(uintptr_t)&doc;
    if (fs != furnSig || furn.size() != doc.instances.size()) { furnSig = fs; furn.resize(doc.instances.size()); for (size_t k = 0; k < furn.size(); ++k) furn[k] = isFurnishPiece(doc.instances[k]); }
  }
  for (size_t idx = 0; idx < doc.instances.size(); ++idx) {
    auto& i = doc.instances[idx];
    if (R.perf.dlCache && furn[idx]) { vec3 d = i.pos - dlEye; if (glm::dot(d, d) > farFurn2 && !interior.batchLeads.count(i.id)) continue; }
    struct DlTag { std::vector<DrawItem>& op; std::vector<DrawItem>& gl; std::vector<FoliageDraw>& fo; InstanceRT* r; size_t o, g, f; bool on; float dist; std::chrono::steady_clock::time_point t0 = on ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
      ~DlTag() { if (!on || !r || !r->asset) return; const AssetDef* a = r->asset;
        {   // DR1 audit: items and triangles per distance band (0-60, 60-150, 150-400, 400+ m), structures / trees apart
          int b = dist < 60 ? 0 : dist < 150 ? 1 : dist < 400 ? 2 : 3, k = a->tree.on ? 1 : 0;
          double tr = 0; for (size_t q = o; q < op.size(); ++q) if (op[q].mesh) tr += op[q].mesh->count / 3;
          dlBand[k][b][0] += op.size() - o; dlBand[k][b][1] += tr;
          for (size_t q = f; q < fo.size(); ++q) if (fo[q].tree) { dlBand[2][b][0] += 1; dlBand[2][b][1] += fo[q].tree->leafCards ? fo[q].tree->leafCards * 2.0 : fo[q].tree->cardVerts / 3.0; }
        }
        int c = a->tree.on ? 0 : a->nature.kind != NatureDef::NONE ? 1 : a->raw.is_object() && a->raw.contains("prop") ? 2 : a->enterable ? 3 : a->model ? 4 : 5;
        dlCat[c][0] += op.size() - o; dlCat[c][1] += gl.size() - g; dlCat[c][2] += fo.size() - f; dlCat[c][3] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); } }
      dlTag{op, gl, R.foliage, act.rtOf[idx], op.size(), gl.size(), R.foliage.size(), dlProf, dlProf ? glm::length(i.pos - dlEye) : 0.0f};
    if (hlodSkip(i)) {   // drawn by its active HLOD cell; its ivy is still drawn per copy (cards, not in the cell)
      InstanceRT* h = act.rtOf[idx];
      if (h && h->asset->ivy.mode != IvyDef::NONE) {
        float cd = glm::length(i.pos - (mode == Mode::Explore ? eyePos() : camPos));
        if (lodPxPerM >= 4.0f * cd) { extern void ivyDrawItem(App&, const Instance&, InstanceRT&, const mat4&, float); ivyDrawItem(*this, i, *h, instanceMatrix(i), cd); }
      }
      continue;
    }
    if (!act.rtOf[idx]) continue;
    auto& r = *act.rtOf[idx];
    mat4 M = instanceMatrix(i);
    if (beautifyDecorDraw(*this, i, r, M, op, gl)) continue;   // V8: border-ring decor, cheap at range (beautify.cpp)
    if (isVehicleAsset(r.asset)) { tanks.drawItems(*this, i, r); continue; }   // supplied tank GLB on its rig (tank_ui.cpp)
    if (mode == Mode::Explore) tanks.propMatrix(i, M);   // a prop a tank knocked over / crushed: drawn at its fallen pose (tank_ram.cpp)
    if (w5CullDraw(*this, i, r.asset)) {   // W5: small decorators beyond their draw distance (content/settings/maps.json)
      // a hedge past its cut still draws its bank (LOD2 at that range), darkened to its foliage: the field pattern reads from the air
      if (r.asset->tree.hedge && !r.treeFelled) {
        float cd = glm::length(i.pos - (mode == Mode::Explore ? eyePos() : camPos));
        const AssetDef* ha = r.asset;   // LOD2 = the bank alone (LOD1 would keep the pollard stems without their canopy)
        if (cd < settings().renderDist()) op.push_back({ha->lodMesh[1].count ? &ha->lodMesh[1] : &ha->opaque, M, vec4(0.6f, 0.75f, 0.5f, 1.0f)});
      }
      continue;
    }
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
    { extern bool lineDrawItems(App&, const Instance&, InstanceRT&, const mat4&, std::vector<DrawItem>&); if (lineDrawItems(*this, i, r, M, op)) continue; }   // LN1 lines
    if (r.asset->path.on) {   // draped on the current ground; re-draped after any terrain change or edit
      if (r.pathRev != i.rev) rebuildPathMesh(i, r);
      else if (r.pathGround != groundRev) {   // a crater only re-drapes the paths it reaches (100 AI throw a lot of frags)
        vec2 wl = pathSize(i, r.asset);
        if (groundChangedNear(r.pathGround, vec2(i.pos.x, i.pos.z), glm::length(wl) + 1.0f)) rebuildPathMesh(i, r); else r.pathGround = groundRev;
      }
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
    // PF2: a prop under ~1.5 px on screen is not drawn (it would only shimmer); buildings always are
    if (R.perf.assetLod && lodPxPerM > 0 && !r.asset->enterable && glm::length(r.asset->bmax - r.asset->bmin) * lodPxPerM < 1.5f * std::max(camDist, 1.0f)) continue;
    const SurfaceParams* surf = interior.surfaceFor(*this, i, r.asset); size_t opFirst = op.size();   // building finish parameters
    const size_t glFirst = gl.size();
    // GP1 occcull: a building whose whole box lies behind terrain (every corner's sight line blocked with 2.5 m to spare,
    // more than any far-LOD ring flattens a ridge) is not drawn in the camera pass. Re-tested when the eye has moved
    // 1.5 m or after 30 frames (cratering), a quarter of the buildings a frame. Buildings never occlude each other here:
    // they can be seen through (windows, doors, breaches).
    if (R.perf.occCull && camDist > 40.0f && (r.asset->enterable || !r.asset->panels.empty())) {
      const vec3 eye = mode == Mode::Explore ? eyePos() : camPos;
      if ((glm::distance(eye, r.occEye) > 1.5f || dlFrameNo - r.occFrame > 30) && ((i.id + dlFrameNo) & 3) == 0) {
        r.occEye = eye; r.occFrame = dlFrameNo;
        bool hid = true; const vec3 lo = r.asset->bmin, hi = r.asset->bmax;
        for (int c = 0; c < 9 && hid; ++c) {
          vec3 lp = c == 8 ? vec3((lo.x + hi.x) * 0.5f, hi.y, (lo.z + hi.z) * 0.5f) : vec3((c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z);
          vec3 p = vec3(M * vec4(lp, 1.0f)), d = p - eye; float len = glm::length(d); bool blocked = false;
          for (float s = 6.0f; s < len - 3.0f && !blocked; s += 3.0f) { vec3 q = eye + d * (s / len); blocked = terrain.height(q.x, q.z) > q.y + 2.5f; }
          hid = blocked;
        }
        r.occ = hid;
      }
    } else r.occ = false;
    if (r.occ) ++R.stats.occCulled;
    bool noFurn = interior.hideBuiltin(i) && r.asset->noFurnOpaque.count;   // furnished by room templates
    op.push_back({noFurn ? &r.asset->noFurnOpaque : &r.asset->opaque, M}); snapDraw(*this, i, op.back());   // V6: joined ends without caps (snap.cpp)
    if (op.back().mesh == &r.asset->opaque || op.back().mesh == &r.asset->noFurnOpaque) op.back().mesh = &assetLodMesh(r, r.asset, *op.back().mesh, camDist);   // PF2 asset LOD
    else r.lod = 0;
    blendDrawItems(*this, i, r, M, op);   // GX1: trench revetment fitted to the carve, retaining skirts (terrain_blend.cpp)
    if (!r.damaged) {   // DR1 walllod: an undamaged copy at asset LOD 1 / 2 draws the walls' LOD (a damaged one keeps its per-bay meshes)
      const AssetDef* wa = r.asset; const bool wl = R.perf.wallLod;
      // the trim (frames ~8 cm wide) goes below 5 px/m (~150 m at 1080p / 72 degrees: under half a pixel wide), or at LOD 2
      const bool noTrim = r.lod >= 2 || (r.lod >= 1 && lodPxPerM > 0 && lodPxPerM < 5.0f * camDist);
      op.push_back({wl && noTrim && wa->panelsLod[1].count ? &wa->panelsLod[1] : wl && r.lod >= 1 && wa->panelsLod[0].count ? &wa->panelsLod[0] : &wa->panelsIntact, M});
    }
    else for (size_t k = 0; k < r.asset->panels.size(); ++k) op.push_back({k < r.wf.size() && r.wf[k].active ? &r.pmesh[k] : &r.asset->panels[k].mesh[0], M});
    if (noFurn) { if (r.asset->noFurnGlass.count) gl.push_back({&r.asset->noFurnGlass, M}); } else if (r.asset->glass.count) gl.push_back({&r.asset->glass, M});
    const mat4 IM = r.asset->interactables.empty() ? mat4(1) : instanceMatrix(i);   // P2: once per copy (leafMatrix rebuilt it per leaf)
    // P2 leafmerge: every leaf closed, at rest and intact -> the asset's merged closed-leaf meshes (2 draws, not 2 per leaf);
    // a door opening, a shutter shot off or a pane broken puts this copy back on the per-leaf path the same frame
    // (one cached mesh pair per asset and open / closed combination, built on first use: buildLeafPose, assets.cpp)
    bool leavesMerged = R.perf.leafMerge && r.lod < 2 && !r.asset->interactables.empty() && r.progress.size() == r.asset->interactables.size();
    uint64_t poseKey = (uint64_t)(uintptr_t)r.asset * 0x9E3779B97F4A7C15ull;
    for (size_t k = 0; leavesMerged && k < r.progress.size(); ++k) {
      leavesMerged = (r.progress[k] == 0.0f || r.progress[k] == 1.0f) && (k >= r.dir.size() || r.dir[k] == 0);
      poseKey = (poseKey ^ (r.progress[k] == 1.0f ? 0x2545F4914F6CDD1Dull : 0x1ull)) * 0x100000001B3ull;
    }
    for (size_t k = 0; leavesMerged && k < r.leafSt.size(); ++k) for (uint8_t b : r.leafSt[k]) if (b) { leavesMerged = false; break; }
    if (leavesMerged) {
      static std::unordered_map<uint64_t, std::unique_ptr<LeafPose>> poses;
      auto& pp = poses[poseKey];
      if (!pp) {
        if (poses.size() > 2048) { for (auto& [k, q] : poses) if (q) q->destroy(); poses.clear(); }   // door toggling over a long session: start over
        auto& np = poses[poseKey]; np = std::make_unique<LeafPose>();
        std::vector<uint8_t> open(r.progress.size()); for (size_t k = 0; k < open.size(); ++k) open[k] = r.progress[k] == 1.0f;
        buildLeafPose(*r.asset, open, *np);
        if (np->opaque.count) op.push_back({&np->opaque, M});
        if (np->glass.count) gl.push_back({&np->glass, M});
      } else {
        if (pp->opaque.count) op.push_back({&pp->opaque, M});
        if (pp->glass.count) gl.push_back({&pp->glass, M});
      }
      if (dlProf && !op.empty() && op.back().mesh) dlLeafT[camDist < 60 ? 0 : camDist < 150 ? 1 : camDist < 400 ? 2 : 3] += op.back().mesh->count / 3;   // DR1 audit (the opaque pose mesh, when it has one)
      ++leafMergeHit;
    } else if (!r.asset->interactables.empty() && r.lod < 2) ++leafMergeMiss;
    for (size_t k = 0; !leavesMerged && k < r.asset->interactables.size() && r.lod < 2; ++k) {   // doors / shutters: not at LOD2 (a few pixels)
      auto& d = r.asset->interactables[k];
      for (size_t l = 0; l < d.leaves.size(); ++l) {
        auto& lf = d.leaves[l];
        uint8_t st = k < r.leafSt.size() && l < r.leafSt[k].size() ? r.leafSt[k][l] : 0;
        if (st & 2) continue;                                  // detached: drawn from its rigid body
        mat4 L = IM * glm::translate(mat4(1), lf.hinge) * glm::mat4_cast(glm::angleAxis(radians(lf.sign * leafAngle(d, r.progress[k])), d.axis));   // = leafMatrix
        op.push_back({&lf.opaque, L});
        if (lf.glass.count && !(st & 1)) gl.push_back({&lf.glass, L});
      }
    }
    // MA2: foundation weathering follows the ground under anything that stands on it (buildings, bunkers, garden walls)
    const vec4* contact = r.asset->footprint.x * r.asset->footprint.y >= 1.0f ? contactProfile(i, r) : nullptr;
    for (size_t k = opFirst; k < op.size(); ++k) { op[k].surf = surf; op[k].contact = contact; op[k].mainHidden |= r.occ; }
    for (size_t k = glFirst; k < gl.size(); ++k) gl[k].mainHidden |= r.occ;
    if (!R.perf.assetLod || lodPxPerM <= 0 || lodPxPerM >= 4.0f * camDist) { extern void ivyDrawItem(App&, const Instance&, InstanceRT&, const mat4&, float); ivyDrawItem(*this, i, r, M, camDist); }   // B1 ivy (ivy.cpp); PF2: not under 4 px/m
  }
  hlodDraw(op, gl);
  if (dlProf && ++dlFrame % 30 == 0) {
    static const char* nm[6] = {"trees", "nature", "props", "buildings", "models", "other"};
    std::string s; for (int c = 0; c < 6; ++c) { char b[96]; std::snprintf(b, sizeof b, " %s op %.0f gl %.0f fol %.0f %.2fms |", nm[c], dlCat[c][0] / 30, dlCat[c][1] / 30, dlCat[c][2] / 30, dlCat[c][3] / 30); s += b; }
    logf("[dlprof] per frame:%s total op %zu gl %zu foliage %zu worldModels %zu, draws %d (foliage %d, shadow %d), leaves merged %d / per-leaf %d buildings", s.c_str(), op.size(), gl.size(), R.foliage.size(), R.worldModels.size(), R.stats.draws, R.stats.foliageDraws, R.stats.shadowDraws, leafMergeHit, leafMergeMiss);
    std::memset(dlCat, 0, sizeof dlCat);
    std::string cp; for (int k = 0; k < GP_Count; ++k) if (R.cpuPassMs[k] / 30 > 0.05) { char b[64]; std::snprintf(b, sizeof b, " %s %.2f", gpuPassName(k), R.cpuPassMs[k] / 30); cp += b; }
    { static const char* bn[3] = {"structures+props", "tree trunks", "canopies (cards x2 tris at far LOD)"}; std::string bs;
      for (int k = 0; k < 3; ++k) { char b[200]; std::snprintf(b, sizeof b, " %s: 0-60 m %.0f items %.0fk tris, 60-150 %.0f / %.0fk, 150-400 %.0f / %.0fk, 400+ %.0f / %.0fk |", bn[k], dlBand[k][0][0] / 30, dlBand[k][0][1] / 30e3, dlBand[k][1][0] / 30, dlBand[k][1][1] / 30e3, dlBand[k][2][0] / 30, dlBand[k][2][1] / 30e3, dlBand[k][3][0] / 30, dlBand[k][3][1] / 30e3); bs += b; }
      logf("[dlprof] by distance:%s", bs.c_str()); std::memset(dlBand, 0, sizeof dlBand);
      logf("[dlprof] merged door / shutter / sash leaves (opaque tris, part of structures): 0-60 m %.0fk, 60-150 %.0fk, 150-400 %.0fk, 400+ %.0fk", dlLeafT[0] / 30e3, dlLeafT[1] / 30e3, dlLeafT[2] / 30e3, dlLeafT[3] / 30e3); std::memset(dlLeafT, 0, sizeof dlLeafT); }
    logf("[dlprof] folbatch: %zu cells, %zu trees, %.1f MB of instance buffers", R.folBatchCells, R.folBatchTrees, R.folBatchBytes / 1048576.0);
    logf("[dlprof] render CPU per pass (ms):%s", cp.c_str()); std::memset(R.cpuPassMs, 0, sizeof R.cpuPassMs);
  }
}

const GpuMesh& App::assetLodMesh(InstanceRT& r, const AssetDef* a, const GpuMesh& full, float dist) {
  if (!R.perf.assetLod || lodPxPerM <= 0 || (!a->lodMesh[0].count && !a->lodMesh[1].count)) { r.lod = 0; ++lodCount[0]; return full; }
  // LOD1 below 10 px/m (a 0.35 m part under ~3.5 px), LOD2 below 2.5 px/m (a 1.5 m volume under ~4 px); a finer level
  // comes back only 15 % past its threshold. 1080p at 72 deg: LOD1 from ~75 m, LOD2 from ~300 m (ADS zoom moves them out).
  const float t[2] = {10.0f, 2.5f};
  float ppm = lodPxPerM / std::max(dist, 1.0f);
  int lvl = std::min<int>(r.lod, 2);
  while (lvl > 0 && ppm >= t[lvl - 1] * 1.15f) --lvl;
  while (lvl < 2 && ppm < t[lvl]) ++lvl;
  r.lod = (uint8_t)lvl; ++lodCount[lvl];
  if (lvl == 2 && a->lodMesh[1].count) return a->lodMesh[1];
  if (lvl >= 1 && a->lodMesh[0].count) return a->lodMesh[0];
  return full;
}

// MA2: the real terrain under a pinned copy's walls, as a 4x4 grid of ground heights (asset space, relative to the
// copy's y) over its footprint grown by 0.4 m, so the samples on the box edges sit just outside the walls. Rebuilt when
// the copy or the ground changes (placement, moves, sculpting, craters). kMainFS reads it as the local ground line.
// shortcut: 4 x 4 samples (13 m apart on the 40 m chateau) miss ground detail finer than that; a perimeter list would not.
const vec4* App::contactProfile(const Instance& i, InstanceRT& r) {
  if (r.contactRev == i.rev && r.contactGround == groundRev) return r.contact;
  r.contactRev = i.rev; r.contactGround = groundRev;
  vec2 mn = vec2(r.asset->bmin.x, r.asset->bmin.z) - 0.4f, mx = vec2(r.asset->bmax.x, r.asset->bmax.z) + 0.4f;
  r.contact[0] = vec4(mn, mx);
  quat q = yawQuat(i.yaw);
  for (int z = 0; z < 4; ++z) for (int x = 0; x < 4; ++x) {
    vec2 a = glm::mix(mn, mx, vec2(x, z) / 3.0f);
    vec3 w = i.pos + q * vec3(a.x, 0, a.y);
    r.contact[1 + z][x] = terrain.height(w.x, w.z) - i.pos.y;
  }
  return r.contact;
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
  LoadStep ls("applyTemplate");
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
  docPath.clear(); navSidecar.clear(); dirty = false; selected = 0; undoStack.clear(); redoStack.clear();
  tool = Tool::Select; placing = nullptr;
  { LoadStep ls("newMap content"); w5NewMapContent(*this, t); }   // W5: authored template content (instances, scenario, routes; w5_maps.cpp)
  { extern int lineMigrateTrenches(App&); lineMigrateTrenches(*this); }   // LN1: trench pieces become trench lines
  { extern int lineMigratePieces(App&); lineMigratePieces(*this); }   // LN2: fence / wire / wall / hedge pieces become lines
  { LoadStep ls("autoFurnishAll"); interior.autoFurnishAll(*this); }   // V4: every building furnished (no undo step, not dirty; interior.cpp)
  toast("New map from template: " + t.name);
  return true;
}
bool App::openMap(const fs::path& p) {
  MapDoc d; std::string err;
  LoadStep lsAll("openMap");
  if (!loadMap(p, d, err)) { toast("Could not open map: " + err, 8); return false; }
  if (mode == Mode::Explore) exitExplore();
  clearRuntime();
  doc = std::move(d);
  { extern int lineMigrateTrenches(App&); lineMigrateTrenches(*this); }   // LN1: trench pieces become trench lines
  { extern int lineMigratePieces(App&); lineMigratePieces(*this); }   // LN2: fence / wire / wall / hedge pieces become lines
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
  navSidecar = navSidecarOf(p);          // PF1: its navmesh (a built-in map's is read-only)
  dirty = false; selected = 0; undoStack.clear(); redoStack.clear();
  std::snprintf(nameBuf, sizeof nameBuf, "%s", doc.name.c_str());
  { LoadStep ls("syncRuntime"); syncRuntime(); }
  { LoadStep ls("autoFurnishAll"); interior.autoFurnishAll(*this); }   // V4: buildings without extra.interior get furnished (no undo step, not dirty; interior.cpp)
  navSyncCuts(*this);   // GX1: cuts and terrain stamps on the opened ground now, not next frame
  Physics::optimizeBroadPhase();   // every body of the map was just added one by one
  toast("Opened " + doc.name + (isContent ? " (copy — Save As to keep)" : ""));
  return true;
}
bool App::saveMapTo(const fs::path& p) {
  if (!mpaiBeforeSave(*this)) return false;   // scenario check (mpui.cpp)
  doc.camera.set = true; doc.camera.pos = camPos; doc.camera.yaw = camYaw; doc.camera.pitch = camPitch;
  if (mode == Mode::Explore) { doc.camera.pos = savedCam.pos; doc.camera.yaw = savedCam.yaw; doc.camera.pitch = savedCam.pitch; }
  std::string err;
  if (!saveMap(p, doc, err)) { toast("Save failed: " + err, 8); return false; }
  aiNavSidecarSave(*this, navSidecar, navSidecarOf(p));   // PF1: the navmesh bakes that still match the map
  docPath = p; navSidecar = navSidecarOf(p); dirty = false;
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
  if (!sf.on && blendPinned(a)) {   // GX1: auto-levelling (terrain_blend.cpp): the median level, the pad and ring do the rest
    float ly = 0;
    if (!blendPlacement(*this, a, pos, yaw, ignoreId, ly, c.reason)) { c.ok = false; return c; }
    c.y = ly; grade = 0;
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
  vec3 hp = h->pos; float hy = h->yaw; int n = 0, k = 0;
  for (auto& c : a->raw["companions"]) {
    // B2 decor: "chance" keeps a companion on that share of placed copies (seeded by the host id and its index);
    // "ground" sets it on the terrain instead of the host's floor
    uint64_t s = (hostId + 0x9E3779B97F4A7C15ull * (uint64_t)++k) * 0xBF58476D1CE4E5B9ull; s ^= s >> 31;
    if (c.contains("chance") && (float)(s % 10000) / 10000.0f >= c["chance"].get<float>()) continue;
    const AssetDef* ca = lib.find(c.value("asset", ""));
    if (!ca || !c.contains("pos")) continue;
    Instance i; i.id = doc.newId(); i.assetId = ca->id; i.assetVersion = ca->version;
    i.pos = hp + yawQuat(hy) * vec3(c["pos"][0].get<float>(), c["pos"][1].get<float>(), c["pos"][2].get<float>());
    if (c.value("ground", false)) i.pos.y = terrain.height(i.pos.x, i.pos.z) + c["pos"][1].get<float>();
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
  }
  std::error_code ec; fs::create_directories(p.parent_path(), ec);
  stbi_write_png(p.string().c_str(), w, h, 4, flip.data(), w * 4);
  logf("screenshot %s", p.string().c_str());
}

// ============================================================ main loop
int App::run(int argc, char** argv) {
  bool runSelf = false, bench = false, legacyLook = false; std::vector<std::string> rqOff, rqOn; fs::path openPath, sheetsDir, previewGlb, previewOut, previewSpec, tankPreviewDir;
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
    else if (a == "--grass" && k + 1 < argc) { std::string q = argv[++k]; grass.quality = q == "off" ? GrassQuality::Off : q == "low" ? GrassQuality::Low : q == "high" ? GrassQuality::High : q == "ultra" ? GrassQuality::Ultra : GrassQuality::Medium; }
    else if (a == "--selftest-gore") runSelf = true;
    else if (a == "--selftest-modes") runSelf = true;   // G1 Solo / TDM modes (selftest_modes.cpp)   // W7 gore suite (selftest_gore.cpp)
    else if (a == "--selftest-nature") runSelf = true;   // natural ground dressing (selftest_nature.cpp)
    else if (a == "--selftest-settings") runSelf = true;   // V2 settings / presets / firing smoke (selftest_settings.cpp)
    else if (a == "--selftest-autopath") runSelf = true;   // V9 auto paths between houses (selftest_autopath.cpp)
    else if (a == "--selftest-snap") runSelf = true;     // V6: snapping (selftest_snap.cpp)
    else if (a == "--selftest-render") runSelf = true;   // V1 facades, walls, water (selftest_render.cpp)
    else if (a == "--selftest-lights") runSelf = true;   // L2 lighting engine (selftest_lights.cpp)
    else if (a == "--selftest-swim") runSelf = true;     // swimming, underwater view, breath (selftest_swim.cpp)
    else if (a == "--selftest-zombies") runSelf = true;
    else if (a == "--profile-map" && k + 3 < argc) { runSelf = true; k += 3; }   // PF3 CPU frame profile (selftest_perf.cpp)  // G2 zombie AI type, melee, knife v soldiers (selftest_zombies.cpp)
    else if (a == "--selftest-tanks") runSelf = true;   // tank suite (selftest_tanks.cpp)
    else if (a == "--selftest-interiors") runSelf = true;   // walls, interiors and furniture (selftest_interiors.cpp)
    else if (a == "--selftest-structures") runSelf = true;   // W4 structure assets (selftest_structures.cpp)
    else if (a == "--selftest-lines") runSelf = true;   // LN1 / LN2 line features (selftest_lines_ln2.cpp)
    else if (a == "--selftest-maps" || a == "--render-map-thumbs") runSelf = true;   // W5 default maps (selftest_maps.cpp)
    else if (a == "--selftest-grass" || a == "--build-range-map" || a == "--record-range" || a == "--selftest-fps" || a == "--selftest-env" || a == "--build-village-map" || a == "--selftest-arsenal" || a == "--selftest-move" || a == "--selftest-sights" || a == "--selftest-update" || a == "--selftest-entities" || a == "--selftest-editor") runSelf = true;
    else if (a == "--legacy-look") legacyLook = true;   // the pre-BSL-pass lighting / tonemap (A/B and fallback)
    else if (a == "--no-textures") tex::setEnabled(false);   // L4 A/B: procedural patterns only (textures.cpp)
    else if (a == "--selftest-textures") runSelf = true;   // L4 texture layer + missing-texture fallback (selftest_textures.cpp)
    else if (a == "--pack-texture" && k + 2 < argc) { fs::path s = argv[k + 1], d = argv[k + 2]; return tex::packSet(s, d); }   // tools/fetch_ambientcg.py
    else if (a == "--texture-placeholders" && k + 1 < argc) { std::vector<std::string> v; std::string l = argv[++k]; for (size_t b = 0, e; b <= l.size(); b = e + 1) { e = l.find(',', b); if (e == std::string::npos) e = l.size(); if (e > b) v.push_back(l.substr(b, e - b)); } return tex::writePlaceholders(k + 1 < argc ? fs::path(argv[k + 1]) : contentDir() / "textures", v); }
    else if (a == "--rq-off" && k + 1 < argc) rqOff.push_back(argv[++k]);   // switch one render feature off (A/B): taa ssr ssao bloom shafts cascade cloudshadow, or a Renderer::perf switch
    else if (a == "--rq-on" && k + 1 < argc) rqOn.push_back(argv[++k]);   // PF2: switch one Renderer::perf feature on (A/B)
    else if (a == "--benchmark-village") { runSelf = true; bench = true; if (k + 1 < argc && argv[k + 1][0] != '-') ++k; }
    else if (a == "--benchmark-stress") { runSelf = true; bench = true; }
    else if (a == "--bench-grass") { runSelf = true; bench = true; }   // G1: grass main + shadow per tier (selftest_grass.cpp)
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
  if (glfwRawMouseMotionSupported()) glfwSetInputMode(win, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);   // look ignores OS pointer acceleration

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
  for (auto& f : rqOff) { if (f == "taa") R.q.taa = false; else if (f == "ssr") R.q.ssr = false; else if (f == "ssao") R.q.ssao = false; else if (f == "bloom") R.q.bloom = false; else if (f == "shafts") R.q.shafts = false; else if (f == "cascade") R.q.farCascade = false; else if (f == "cloudshadow") R.q.cloudShadows = false; else if (f == "wind") grass.wind = false; else if (!R.perf.set(f, false)) logf("--rq-off: unknown feature %s", f.c_str()); }
  for (auto& f : rqOn) if (!R.perf.set(f, true)) logf("--rq-on: unknown feature %s", f.c_str());
  templates = loadTemplates(contentDir() / "templates");
  if (!lib.loadAll(contentDir() / "assets")) logf("warning: no assets found in %s", (contentDir() / "assets").string().c_str());
  makeThumbnails();
  {
    Material rm{"debris", vec3(0.30f, 0.27f, 0.21f), 0.95f, 0.0f, PAT_STONE, 1};
    Part p; p.center = vec3(0); p.size = vec3(1);
    MeshData m; appendPart(m, p, rm, mat4(1)); debrisMesh.upload(m);
    buildWeaponMeshes();
    makeWeaponSilhouettes();   // G4: the carried list (weapons.cpp)
    buildArmMeshes();
    loadBindings();
    loadMoveSettings();
    { std::string err; if (!loadClasses(contentDir() / "settings" / "classes.json", err)) logf("classes: %s", err.c_str());
      for (auto& c : classDefs()) for (const ClassSide* s : {&c.us, &c.de}) {   // a class naming a weapon that is not loaded is a content error
        std::vector<std::string> ids = s->primary; ids.insert(ids.end(), s->secondary.begin(), s->secondary.end()); if (!s->pistol.empty()) ids.push_back(s->pistol);
        for (auto& w : ids) if (weaponIndex(w) < 0) logf("classes: %s (%s) names unknown weapon %s", c.id.c_str(), s->name.c_str(), w.c_str()); }
      logf("classes: %zu loaded", classDefs().size()); }
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
  // NMK_FRAME_PROF=1: a frame over 150 ms logs where its time went: time per phase / crumb, summed, top 8. A mark names
  // the section that follows it (time to the next mark is charged to it), like crumb()
  auto mark = [&](const char* w) { if (gCrumbProf) crumbMark(w); };
  while (!quit) {
    if (gCrumbProf) crumbFrameEnd();
    mark("start");
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
    mark("update");
    if (mode == Mode::Creative) { updateCreative(frameDt); drawCreativeUI(); }
    else { updateExplore(frameDt); crumb("explore ui"); drawExploreUI(); }
    mark("ui+sync");
    mpaiUI(*this);   // Play menu, scenario panel, match HUD
    settingsWindow(*this);   // Settings window (Creative menu, pause menu, Loadout)
    crumb("syncRuntime");
    syncRuntime();
    crumb("tanks sync");
    tanks.sync(*this);
    crumb("interior sync");
    interior.sync(*this);   // generated furniture follows / leaves with its building
    crumb("foundation sync");
    foundationSync(*this);   // MA3: grass, weeds, stones and prop arrangements round building bases (foundation.cpp)

    if (confirmQuit) { ImGui::OpenPopup("Unsaved changes"); confirmQuit = false; }
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Save changes to \"%s\" before closing?", doc.name.c_str());
      if (ImGui::Button("Save")) { if (!docPath.empty()) { if (saveMapTo(docPath)) quit = true; } else { showSaveAs = true; } ImGui::CloseCurrentPopup(); }
      ImGui::SameLine(); if (ImGui::Button("Discard")) quit = true;
      ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }

    mark("drawlists");
    std::vector<DrawItem> op, gl, ghosts; Overlay ov;
    { auto t0 = std::chrono::steady_clock::now(); buildDrawLists(op, gl); R.opaqueStatic = op.size(); drawListMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }
    mark("view");
    op.insert(op.end(), worldBoxDraws.begin(), worldBoxDraws.end());
    if (mode == Mode::Explore) {
      dynLeafDrawItems(op);
      interior.fragDrawItems(op);
      builderGhosts(ghosts);
      crumb("view weapons");
      for (auto& d : fx.debris) op.push_back({&debrisMesh, glm::translate(mat4(1), d.p) * glm::mat4_cast(d.r) * glm::scale(mat4(1), d.size)});
      if (!tanks.occupied) weaponDrawItems(op);
    }
    crumb("view camera");
    int ww, wh; glfwGetWindowSize(win, &ww, &wh);
    // minimized windows report a 0x0 framebuffer: keep the aspect of the targets we actually render into
    float aspect = (fbw > 0 && fbh > 0) ? (float)fbw / fbh : (float)R.width / std::max(1, R.height);
    RenderView v;
    if (mode == Mode::Creative) {
      viewM = glm::lookAt(camPos, camPos + forward(camYaw, camPitch), vec3(0, 1, 0));
      projM = w3EditorProjection(*this, aspect);   // 60 deg; near / far follow height and map size (editor_nav.cpp)
      v.editor = !hideEditorOverlays; v.grid = gridOverlay; v.autoExposure = false; v.camPos = camPos;
      if (!hideEditorOverlays) drawCreativeOverlay(ov, ghosts);
      for (auto* p : {&gAnnotOverlay.lines, &gAnnotOverlay.linesTop, &gAnnotOverlay.tris}) {   // capture annotations
        auto& to = p == &gAnnotOverlay.lines ? ov.lines : p == &gAnnotOverlay.linesTop ? ov.linesTop : ov.tris;
        to.insert(to.end(), p->begin(), p->end());
      }
      audio.setListener(camPos, forward(camYaw, camPitch));
    } else {
      float shk = reducedMotion ? 0.0f : blastShake * blastShake * 1.6f;   // concussion shake (deg)
      vec3 e = eyePos(), fv = forward(eyeYaw + viewKickYaw + shk * std::sin((float)now * 41.0f), eyePitch + viewKickPitch + shk * std::sin((float)now * 53.0f + 1.3f));
      float fov = settings().fov();   // 72 unless changed in Settings
      if (!tanks.camera(*this, e, fv, fov) && !stationCamera(e, fv, fov)) weaponCamera(fov);
      mpaiCamera(*this, e, fv, fov);   // death / spectator camera during a match
      viewM = glm::lookAt(e, e + fv, viewUp(fv));   // swim.cpp: a slight roll with the stroke
      projM = glm::perspective(radians(fov), aspect, 0.03f, std::max(4000.0f, settings().renderDist() * 1.2f));   // PF2: Ultra render distance (5 km)
      worldFov = fov;
      v.vmProj = glm::perspective(radians(vmFovNow()), aspect, 0.01f, 6.0f);
      dofParams(v.dofFocus, v.dofAmount);
      if (scopeView()) { v.eyepieceR = kScopeEyepieceR; v.reticle = weaponDefs[activeWeapon]->reticle == "de_absehen1" ? 1 : 2; }
      else if (gunnerOptic()) { v.eyepieceR = kGunnerEyepieceR; v.reticle = 3; }
      v.editor = false; v.autoExposure = true; v.camPos = e;
      v.underwater = underwaterAt(*this, e); v.underFogCol = swimCfg.fogColor; v.underFogDens = swimCfg.fogDensity;   // swim.cpp
      audio.setListener(e, forward(eyeYaw, eyePitch));
    }
    audio.setUnderwater(v.underwater ? 1.0f : 0.0f);   // swim.cpp: muffled under water
    mark("mpai");
    mpaiFrame(*this, ov);   // soldiers, grenades, scenario overlay
    mark("fx");
    v.view = viewM; v.proj = projM; v.dt = frameDt;
    R.fxAlpha.clear(); R.fxAdd.clear();
    if (mode == Mode::Explore) fx.build(viewM, R.fxAlpha, R.fxAdd);
    lightsMotes(*this, viewM, R.fxAdd);   // GX2: dust in the sun shafts indoors (every mode)
    mark("w3");
    w3Frame(*this);   // terrain cuts, chunk physics sync, editor camera flight (editor_nav.cpp)
    mark("grass");
    updateGrass(v.camPos);
    mark("lights");
    R.grass = &grass;
    { extern void lightingAppFrame(App&); lightingAppFrame(*this); }   // L2: the frame's lights (src/lights.h) and room volumes
    mark("render");
    { extern void weatherAppFrame(App&); weatherAppFrame(*this); }   // WX1: the shared cloud clock, the host's weather on LAN clients
    R.render(v, terrain, op, gl, ghosts, ov);
    mark("imgui");
    if (selftest) selftest->update(*this, -1.0f);  // post-render hook (screenshots)
    if (!recCaption.empty()) {   // recording caption, over every other overlay
      ImDrawList* dl = ImGui::GetForegroundDrawList(); ImGuiIO& io = ImGui::GetIO();
      float fs = io.DisplaySize.y / 30.0f;
      ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0, recCaption.c_str());
      ImVec2 p(28, io.DisplaySize.y - fs * 2.6f);
      dl->AddRectFilled(ImVec2(p.x - 12, p.y - 8), ImVec2(p.x + ts.x + 12, p.y + ts.y + 8), IM_COL32(10, 10, 8, 200), 6);
      dl->AddText(ImGui::GetFont(), fs, p, IM_COL32(250, 240, 210, 255), recCaption.c_str());
    }
    if (!selftest) {   // FPS counter, top right (not in tests: their screenshots are compared)
      ImGuiIO& io = ImGui::GetIO(); char t[24]; std::snprintf(t, sizeof t, "%.0f FPS", io.Framerate);   // ImGui's 120-frame average
      float fs = std::max(12.0f, io.DisplaySize.y / 72.0f);
      ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0, t);
      ImGui::GetForegroundDrawList()->AddText(ImGui::GetFont(), fs, ImVec2(io.DisplaySize.x - ts.x - 10, 26), IM_COL32(235, 235, 225, 130), t);   // under the editor's menu bar
    }
    ImGui::Render();
    glViewport(0, 0, fbw, fbh);
    platformImGuiRender(ImGui::GetDrawData());
    R.gpuMark(GP_Count);   // end of the UI pass (render.cpp gpuMark)
    mark("present");
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
    mark("end");
    settingsEndFrame(*this);   // FPS cap
  }
  int code = selftest ? selftest->exitCode : 0;
  if (gCrumbProf) { extern json crumbProfReport(const char*); crumbProfReport("exit"); }
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
  // the full signature below walks every instance: only when an instance, the routes or the terrain changed
  static uint64_t gPlace = 0, gGen = ~0ull, gSurf = 0; static json gRoutes;
  const json noRoutes = json::array();
  const json& routesNow = doc.extra.is_object() && doc.extra.contains("routes") ? doc.extra["routes"] : noRoutes;
  uint64_t place = placementSig(doc.instances, true);
  if (gPlace == place && gGen == (uint64_t)terrain.generation && gSurf == terrain.surfSig && !terrain.surf.empty() && routesNow == gRoutes) return;
  gPlace = place; gGen = (uint64_t)terrain.generation; gRoutes = routesNow;
  // structures (not guns, crates, trees or defences) get a foundation soil band; routes come from the map
  std::vector<SurfaceFootprint> feet;
  uint64_t sig = 1469598103934665603ull;
  auto mix = [&](uint64_t v) { sig = (sig ^ v) * 1099511628211ull; };
  for (auto& i : doc.instances) {
    const AssetDef* a = assetFor(i);
    if (!a || a->station.kind.size() || (a->crate.is_object() && !a->crate.empty()) || a->tree.on || !a->defense.kind.empty() || a->rack || isVehicleAsset(a) || a->nature.kind != NatureDef::NONE) continue;
    { extern bool waterAsset(const AssetDef*); if (waterAsset(a)) continue; }   // V1: ponds / streams / the sea have no foundation band
    if (isFurnishPiece(i) || isDressingPiece(i)) continue;   // V4 / MA3: furniture and foundation dressing have no foundation band of their own
    if (a->raw.is_object() && a->raw.contains("earthwork")) continue;   // GX1 earth-covered: the berm's contact band hugs the walls (the bounds would draw a line across the banks)
    feet.push_back({vec2(i.pos.x, i.pos.z), i.yaw, vec2(a->bmin.x, a->bmin.z), vec2(a->bmax.x, a->bmax.z), 1.0f});
    uint32_t b[3]; std::memcpy(&b[0], &i.pos.x, 4); std::memcpy(&b[1], &i.pos.z, 4); std::memcpy(&b[2], &i.yaw, 4);
    mix(b[0]); mix(b[1]); mix(b[2]); mix(std::hash<std::string>{}(i.assetId));
  }
  json routes = doc.extra.is_object() && doc.extra.contains("routes") ? doc.extra["routes"] : json::array();
  mix(std::hash<std::string>{}(routes.dump()));
  mix(terrain.generation);
  gSurf = sig;
  if (sig == terrain.surfSig && !terrain.surf.empty()) return;
  terrain.buildSurface(routes, feet);
  terrain.surfSig = sig;
}

void App::updateGrass(vec3 cam) {
  crumb("grass surface");
  updateSurface();
  crumb("grass footprints");
  uint32_t seed = doc.seed * 2654435761u + (uint32_t)doc.templateVersion * 40503u + (uint32_t)std::hash<std::string>{}(doc.templateId);
  // rebuilt only when an instance appeared, went away, moved or changed revision, or the ground changed (path drape)
  static std::vector<GrassFootprint> fp; static uint64_t fpSig = 0; static uint32_t fpGround = ~0u;
  uint64_t place = placementSig(doc.instances, true);
  if (fpSig != place || fpGround != groundRev) { fp = grassFootprints(); fpSig = place; fpGround = groundRev; }
  crumb("grass update");
  grass.update(terrain, seed, fp, cam, grassBudgetMs);
}

// ---- NMK_FRAME_PROF: crumbs as profile marks (crumb.h)
//   NMK_FRAME_PROF_MS=<ms>       a frame slower than this logs its breakdown (default 150)
//   NMK_FRAME_PROF_EVERY=<s>     per-section average / p95 CPU ms per frame logged every <s> seconds (default 10, 0 = only at exit)
bool gCrumbProf = std::getenv("NMK_FRAME_PROF") != nullptr;
static std::vector<std::pair<const char*, double>> gMarks;
static const double kSlowFrame = std::getenv("NMK_FRAME_PROF_MS") ? std::atof(std::getenv("NMK_FRAME_PROF_MS")) / 1000.0 : 0.15;
static const double kProfEvery = std::getenv("NMK_FRAME_PROF_EVERY") ? std::atof(std::getenv("NMK_FRAME_PROF_EVERY")) : 10.0;
static std::map<std::string, std::vector<float>> gSecMs;   // section -> ms per frame (0 when it did not run) since the last report
static std::vector<float> gFrameMs;
static double gProfSince = -1;
void crumbMark(const char* s) { gMarks.push_back({s, glfwGetTime()}); }
LoadStep::LoadStep(const char* w) : what(w), t0(gCrumbProf ? glfwGetTime() : 0) {}
LoadStep::~LoadStep() { if (gCrumbProf) logf("[loadprof] %s %.1f ms", what, (glfwGetTime() - t0) * 1000); }
json crumbProfReport(const char* tag) {
  auto stat = [](std::vector<float> v) { double s = 0; for (float x : v) s += x; std::sort(v.begin(), v.end()); return std::pair<double, double>(v.empty() ? 0 : s / v.size(), v.empty() ? 0 : v[std::min(v.size() - 1, (size_t)(0.95 * v.size()))]); };
  json out = json::object();
  if (gFrameMs.empty()) return out;
  auto [fa, fp] = stat(gFrameMs);
  out["frames"] = gFrameMs.size(); out["frameMs"] = fa; out["frameP95"] = fp;
  std::vector<std::tuple<double, double, std::string>> v;
  for (auto& [n, s] : gSecMs) { s.resize(gFrameMs.size(), 0.0f); auto [a, p] = stat(s); v.push_back({a, p, n}); out["sections"][n] = {{"avg", a}, {"p95", p}}; }
  std::sort(v.rbegin(), v.rend());
  std::string b; char x[96];
  for (size_t k = 0; k < v.size() && k < 14; ++k) { std::snprintf(x, sizeof x, " [%s] %.2f/%.2f", std::get<2>(v[k]).c_str(), std::get<0>(v[k]), std::get<1>(v[k])); b += x; }
  logf("[frameprof] %s %zu frames, avg %.2f ms p95 %.2f ms; section avg/p95 ms:%s", tag, gFrameMs.size(), fa, fp, b.c_str());
  gSecMs.clear(); gFrameMs.clear(); gProfSince = glfwGetTime();
  return out;
}
void crumbFrameEnd() {
  if (gMarks.size() > 1) {
    double total = gMarks.back().second - gMarks.front().second;
    size_t f = gFrameMs.size();
    for (size_t k = 0; k + 1 < gMarks.size(); ++k) {   // time from a mark to the next, charged to the mark
      auto& s = gSecMs[gMarks[k].first];
      if (s.size() < f + 1) s.resize(f + 1, 0.0f);
      s[f] += (float)((gMarks[k + 1].second - gMarks[k].second) * 1000);
    }
    gFrameMs.push_back((float)(total * 1000));
    if (total > kSlowFrame) {
      std::map<std::string, double> per;
      for (size_t k = 0; k + 1 < gMarks.size(); ++k) per[gMarks[k].first] += gMarks[k + 1].second - gMarks[k].second;
      std::vector<std::pair<double, std::string>> v; for (auto& [n, t] : per) v.push_back({t, n});
      std::sort(v.rbegin(), v.rend());
      std::string b; char x[96];
      for (size_t k = 0; k < v.size() && k < 8; ++k) { std::snprintf(x, sizeof x, " [%s] %.1f", v[k].second.c_str(), v[k].first * 1000); b += x; }
      logf("[frameprof] %.0f ms:%s", total * 1000, b.c_str());
    }
    if (gProfSince < 0) gProfSince = gMarks.front().second;
    if (kProfEvery > 0 && gMarks.back().second - gProfSince > kProfEvery) crumbProfReport("periodic");
  }
  gMarks.clear();
}
