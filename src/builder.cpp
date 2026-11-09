// Buildable defences (the guide's "BuilderController") and their ballistic behaviour.
//
// Placement (Explore, B): ray from the screen centre to D_max = 5 m on the terrain only; surface normal N;
//   slope  theta = acos(N . Y_up);  theta > 35 deg -> invalid (red hologram), else valid (green).
// Sandbag snapping: if the aim point is within R_snap = 0.6 m of a free socket of a placed sandbag, the new
//   piece's attaching socket is moved onto it and the piece is yawed so the tangents continue
//   (new socket tangent = -existing tangent), which chains straight and curved runs.
// Tank trap: HP 1500, small-arms multiplier 0, blocksVehicles footprint (no vehicle AI exists: flag + overlay).
// Sandbags: HP 300, stop every round; explosion damage to a sheltering character is multiplied by 0.1 when the
//   epicentre -> character ray crosses a sandbag collider.
// Placements made in Explore are a session overlay (Instance::extra "session"); Apply keeps them.
//
// First-person building (docs/builder.md): B opens build mode with the full Assets palette (palette.cpp: the
// editor's categories, search and thumbnails; Tab shows / hides it, picking an asset puts it in hand). The ghost
// sits where the view meets open ground (or a floor, for furniture) within 12 m, pushed forward if its footprint
// would contain the player. Wheel / Z / X / R rotate, LMB places through the editor's checkPlacement, Del removes
// the aimed placement, Ctrl+Z undoes the last place / remove, 1 / 2 / Q pick the quick defences, RMB or B leaves.
// In a session entered from Creative (drop-in, Explore from start) placements and removals are real map edits
// (placeInstance, the editor's undo stack, the map marked dirty); otherwise they stay a session overlay.
#include "app.h"
#include "editor_nav.h"
#include "palette.h"
#include "snap.h"
#include "mpai.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>

static const char* kBuildItems[3] = {"def_tank_trap", "def_sandbag_straight", "def_sandbag_curved"};
static constexpr float kBuildReach = 12.0f, kSnapR = 0.6f;

// Snapping for the asset in hand: the editor's one snap API (snap.cpp: sockets, sandbag runs, path ends).
static bool buildSnap(const App& app, const AssetDef* a, vec3& pos, float& yaw) {
  vec3 p = pos; float y = yaw;
  if (!snapPlacement(app, a, p, y, 0)) return false;
  pos = p; yaw = y; return true;   // snapped (a sandbag already at its socket still counts as snapped)
}
static bool isSession(const Instance& i) { return i.extra.is_object() && i.extra.value("session", false); }
static bool buildRemovable(const App& app, const Instance& i) {
  if (i.id == app.occupied || i.id == app.tanks.occupied) return false;
  return isSession(i) || app.build.realEdits;
}
// a real edit's undo snapshot never carries session overlay pieces (rack deployments) back into Creative
static void stripSessionFromUndoTop(App& app) {
  if (app.undoStack.empty()) return;
  auto& v = app.undoStack.back().inst;
  v.erase(std::remove_if(v.begin(), v.end(), isSession), v.end());
}

float App::groundSlopeDeg(vec3 p, vec3* nOut) const {
  const float e = 0.25f;
  vec3 n = glm::normalize(vec3(terrain.height(p.x - e, p.z) - terrain.height(p.x + e, p.z), 2 * e, terrain.height(p.x, p.z - e) - terrain.height(p.x, p.z + e)));
  if (nOut) *nOut = n;
  return degrees(std::acos(std::clamp(n.y, -1.0f, 1.0f)));
}

bool App::snapDefense(const AssetDef* a, vec3& pos, float& yaw, int* matchedSocket) const {
  if (a->defense.kind != "sandbag" || a->defense.sockets.size() < 2) return false;
  float best = kSnapR; bool found = false; vec3 sp, st;
  for (auto& o : doc.instances) {
    const AssetDef* b = assetFor(o);
    if (b->defense.kind != "sandbag") continue;
    for (size_t k = 0; k < b->defense.sockets.size(); ++k) {
      vec3 wp = o.pos + yawQuat(o.yaw) * b->defense.sockets[k].p;
      vec3 wt = yawQuat(o.yaw) * b->defense.sockets[k].t;
      // occupied: another sandbag already has a socket here
      bool used = false;
      for (auto& o2 : doc.instances) {
        if (o2.id == o.id) continue;
        const AssetDef* c = assetFor(o2);
        if (c->defense.kind != "sandbag") continue;
        for (auto& s2 : c->defense.sockets) if (glm::length(o2.pos + yawQuat(o2.yaw) * s2.p - wp) < 0.05f) used = true;
      }
      if (used) continue;
      float d = glm::length(vec2(wp.x - pos.x, wp.z - pos.z));
      if (d < best) { best = d; sp = wp; st = wt; found = true; }
    }
  }
  if (!found) return false;
  // attach with the new piece's socket whose tangent can face the existing one: R(yaw) * t_new = -t_existing
  int use = 0;
  const DefenseSocket& ns = a->defense.sockets[use];
  float yt = std::atan2(-st.x, -st.z), yn = std::atan2(ns.t.x, ns.t.z);
  yaw = wrapDeg(degrees(yt - yn));
  pos = sp - yawQuat(yaw) * ns.p;
  pos.y = terrain.height(pos.x, pos.z);
  if (matchedSocket) *matchedSocket = use;
  return true;
}

void App::setBuildPalette(bool open) {
  if (build.palette == open) return;
  build.palette = open;
  if (!selftest && win && mode == Mode::Explore && !pauseMenu) glfwSetInputMode(win, GLFW_CURSOR, open ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
}

void App::builderKeys(ExploreInput& in) {
  static std::map<int, bool> prev;
  auto edge = [&](int k) { bool d = glfwGetKey(win, k) == GLFW_PRESS, p = prev[k]; prev[k] = d; return d && !p; };
  bool z = edge(GLFW_KEY_Z), x = edge(GLFW_KEY_X), del = edge(GLFW_KEY_DELETE), tab = edge(GLFW_KEY_TAB);
  if (!build.active) return;
  if (build.palette && ImGui::GetIO().WantCaptureKeyboard) {   // typing in the palette search: no keys reach the player
    in.move = vec2(0); in.jump = in.jumpHeld = in.crouch = in.sprint = in.interact = in.reload = in.build = in.nextWeapon = false;
    in.choose1 = in.choose2 = in.prone = in.toggleAmmo = in.fireMode = in.packUp = in.quickGrenade = in.switchPos = false;
    return;
  }
  bool ctrl = glfwGetKey(win, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
  if (z && ctrl) build.pendUndo = true; else if (z) build.pendRot -= 15.0f;
  if (x) build.pendRot += 15.0f;
  if (del) build.pendDel = true;
  if (tab) build.pendPalette = true;
  if (binds.prone == GLFW_KEY_Z) in.prone = false;   // Z / X turn the ghost while building (not prone / ammo type)
  in.toggleAmmo = false;
  if (build.palette) in.fire = false;
}

bool App::buildRemoveAimed() {
  if (mpaiBuildClient(*this)) { toast("Removing placements is host-only in LAN matches.", 3); return false; }
  Instance* t = doc.find(build.aimId);
  if (!t || !buildRemovable(*this, *t)) { toast(build.realEdits ? "Nothing to remove under the crosshair." : "Only pieces built in this session can be removed (drop in from Creative to edit the map).", 3); return false; }
  BuildAction act; act.real = !isSession(*t);
  bool wasDirty = dirty;
  if (act.real) { pushUndo(); stripSessionFromUndoTop(*this); act.undoDepth = undoStack.size(); }
  act.removed.push_back(*t);
  uint64_t id = t->id;
  if (selected == id) selected = 0;
  auto furnOf = [&](const Instance& i) { return isFurnishPiece(i) && i.extra["furnish"].value("host", (uint64_t)0) == id; };   // V4: its furniture goes with it
  for (auto& i : doc.instances) if (furnOf(i)) act.removed.push_back(i);
  doc.instances.erase(std::remove_if(doc.instances.begin(), doc.instances.end(), [&](const Instance& i) { return i.id == id || furnOf(i); }), doc.instances.end());
  syncRuntime();
  dirty = act.real ? true : wasDirty;
  mpaiBuildChanged(*this, {}, {id});   // LAN host: every PC removes it
  build.history.push_back(std::move(act)); ++build.removed; build.aimId = 0;
  audio.play("crate", build.pos, 0.5f);
  return true;
}

bool App::buildUndo() {
  if (build.history.empty()) { toast("Nothing to undo in build mode.", 2); return false; }
  BuildAction act = std::move(build.history.back()); build.history.pop_back();
  bool wasDirty = dirty;
  for (uint64_t id : act.placed) doc.instances.erase(std::remove_if(doc.instances.begin(), doc.instances.end(), [&](const Instance& i) { return i.id == id; }), doc.instances.end());
  for (auto& i : act.removed) { Instance c = i; bump(c); doc.instances.push_back(c); }
  if (act.real && !undoStack.empty() && undoStack.size() == act.undoDepth) undoStack.pop_back();   // the editor's matching undo entry
  syncRuntime();
  if (!act.real) { std::vector<uint64_t> back; for (auto& i : act.removed) back.push_back(i.id); mpaiBuildChanged(*this, back, act.placed); }   // LAN host
  dirty = act.real ? true : wasDirty;
  ++build.undone;
  return true;
}

void App::stepBuilder(const ExploreInput& in) {
  if (in.build) {
    build.active = !build.active;
    if (build.active) {
      build.realEdits = navState().creativeSession;
      build.pendDel = build.pendUndo = build.pendPalette = false; build.pendRot = 0;
      setBuildPalette(!useScripted);
      toast(std::string(build.realEdits ? "Build mode (map edits)" : "Build mode") + ": Tab assets, 1 tank trap, 2 sandbags, wheel / Z / X rotate, LMB place, Del remove, Ctrl+Z undo, RMB or B leave", 5);
    } else setBuildPalette(false);
  }
  if (!build.active) { build.aimId = 0; return; }
  if (in.aim && !build.palette) { build.active = false; build.forceAsset.clear(); setBuildPalette(false); return; }
  if (build.pendPalette) { build.pendPalette = false; setBuildPalette(!build.palette); }
  bool quick = in.choose1 || in.choose2 || in.nextWeapon;
  if (in.choose1) build.item = 0;
  if (in.choose2) build.item = build.item == 1 ? 2 : 1;
  if (in.nextWeapon) build.item = (build.item + 1) % 3;
  if (quick) { build.pick.clear(); build.forceAsset.clear(); }
  if (in.reload) build.yaw += 15.0f;
  build.yaw += in.wheel * 15.0f + build.pendRot; build.pendRot = 0;
  if (build.pendUndo) { build.pendUndo = false; buildUndo(); }
  vec3 eye = eyePos(), f = forward(eyeYaw, eyePitch);
  build.aimId = 0;   // Del target: the instance under the crosshair
  { Physics::Hit h; if (Physics::castRayEx(eye, f, kBuildReach, h) && h.ud != 0 && (h.ud >> 60) != 0xF) if (const Instance* t = doc.find(h.ud >> 16); t && buildRemovable(*this, *t)) build.aimId = t->id; }
  if (build.pendDel) { build.pendDel = false; buildRemoveAimed(); }
  // the asset in hand: a rack deployment, the palette pick, or a quick defence
  const AssetDef* a = lib.find(!build.forceAsset.empty() ? build.forceAsset : !build.pick.empty() ? build.pick : std::string(kBuildItems[build.item]));
  build.asset = a;
  build.valid = false; build.snapped = false;
  if (!a) { build.reason = "missing asset"; return; }
  bool defence = !a->defense.kind.empty() || !a->station.kind.empty(), prop = interior.isProp(a);
  Physics::Hit h;
  bool hit = Physics::castRayEx(eye, f, kBuildReach, h);
  bool ground = hit && (h.ud == 0 || (h.ud >> 60) == 0xF);
  vec3 fp;   // furniture lands on real floors and table tops, as in the editor
  if (prop && interior.hoverPoint(*this, eye, f, fp, 0) && glm::length(fp - eye) <= kBuildReach) { hit = ground = true; h.pos = fp; h.normal = vec3(0, 1, 0); }
  if (!hit || !ground) { build.reason = hit ? "Aim at open ground" : "Out of reach (12 m)"; build.pos = eye + f * kBuildReach; build.pos.y = terrain.height(build.pos.x, build.pos.z); build.ghostYaw = eyeYaw + build.yaw; return; }
  build.pos = h.pos; build.ghostYaw = eyeYaw + build.yaw;
  // theta = acos(N . Y_up) from the hit normal
  build.slopeDeg = degrees(std::acos(std::clamp(glm::dot(glm::normalize(h.normal), vec3(0, 1, 0)), -1.0f, 1.0f)));
  vec3 p = build.pos; float y = build.ghostYaw;
  if (buildSnap(*this, a, p, y)) { build.pos = p; build.ghostYaw = y; build.snapped = true; }
  if (defence) {
    float maxSlope = a->defense.kind.empty() ? 20.0f : a->defense.maxSlopeDeg;   // stations: carriage limit
    if (build.slopeDeg > maxSlope) {
      char b[96]; std::snprintf(b, sizeof b, "Too steep: %.0f deg > %.0f deg", build.slopeDeg, maxSlope);
      build.reason = b; return;
    }
  }
  if (!prop && !build.snapped) {   // never build round the player: push the ghost along the view until its footprint is clear
    vec3 feet = Physics::character().pos;
    vec2 hf = a->footprint * 0.5f + vec2(Physics::radius() + 0.1f);
    auto around = [&](vec3 c) { vec3 lp = glm::inverse(yawQuat(build.ghostYaw)) * (feet - c); return std::fabs(lp.x) < hf.x && std::fabs(lp.z) < hf.y; };
    if (around(build.pos)) {
      vec3 fh = glm::normalize(vec3(f.x, 0, f.z) + vec3(1e-6f, 0, 0));
      bool clear = false;
      for (int k = 1; k <= 120 && !clear; ++k) { vec3 q = h.pos + fh * (0.25f * k); q.y = terrain.height(q.x, q.z); if (!around(q)) { build.pos = q; clear = true; } }
      if (!clear) { build.reason = "Too close: you would be inside it"; return; }
    }
  }
  if (std::string why = buildCheckAt(a, build.pos, build.ghostYaw, build.snapped, vec3(1e9f)); !why.empty()) { build.reason = why; return; }
  build.valid = true; build.reason = build.snapped ? (a->defense.kind == "sandbag" ? "Snaps to the sandbag run" : "Snaps to the joint") : "Valid";
  bool firePress = in.fire && !build.fireHeld;
  build.fireHeld = in.fire;
  if (!firePress || build.palette) return;
  if (mpaiBuildRequest(*this, a, build.pos, build.ghostYaw)) {   // LAN client: the host re-checks and places it on every PC
    if (!build.forceAsset.empty()) { build.active = false; build.forceAsset.clear(); setBuildPalette(false); }
    return;
  }
  BuildAction act; act.real = build.realEdits && build.forceAsset.empty();   // rack deployments stay a session overlay
  uint64_t id = 0;
  if (act.real) {
    size_t n0 = doc.instances.size();
    skipSlopeRule = true; id = placeInstance(a, build.pos, build.ghostYaw); skipSlopeRule = false;   // the editor's own path (undo, furnishing hooks)
    if (!id) return;   // placeInstance toasted why
    stripSessionFromUndoTop(*this);
    act.undoDepth = undoStack.size();
    placeCompanions(id);
    for (size_t k = n0; k < doc.instances.size(); ++k) act.placed.push_back(doc.instances[k].id);   // the placement, its furniture (V4) and companions
    dirty = true;
  } else {
    id = buildSessionPiece(a, build.pos, build.ghostYaw, build.forceAsset.empty(), &act)[0];
    if (!act.real) mpaiBuildChanged(*this, act.placed, {});   // LAN host: every PC creates the same pieces
  }
  build.history.push_back(std::move(act));
  ++buildPlaced;
  if (!a->station.kind.empty()) { syncRuntime(); if (Instance* si = doc.find(id)) resetStation(*si); if (!build.forceAsset.empty()) { build.active = false; build.forceAsset.clear(); setBuildPalette(false); } }
  audio.play("crate", build.pos, 0.8f);
}

// the placement rules once the aim is known: the editor's checkPlacement (slope judged by the caller), then for
// defences a clear footprint. builderFeet: never build round that soldier (a LAN client's, re-checked on the host).
std::string App::buildCheckAt(const AssetDef* a, vec3& pos, float yaw, bool snapped, vec3 builderFeet) {
  bool defence = !a->defense.kind.empty() || !a->station.kind.empty();
  vec3 lp = glm::inverse(yawQuat(yaw)) * (builderFeet - pos); vec2 hf = a->footprint * 0.5f + vec2(Physics::radius() + 0.1f);
  if (!snapped && !interior.isProp(a) && std::fabs(lp.x) < hf.x && std::fabs(lp.z) < hf.y && std::fabs(lp.y) < 3) return "Too close: you would be inside it";
  skipSlopeRule = true;
  PlaceCheck c = checkPlacement(a, pos, yaw, 0);
  skipSlopeRule = false;
  if (!c.ok) return c.reason;
  pos.y = c.y;
  if (c.snapXZ) { pos.x = c.xz.x; pos.z = c.xz.y; }
  if (defence) {
    vec3 half = (a->bmax - a->bmin) * 0.5f * 0.9f, ctr = pos + yawQuat(yaw) * ((a->bmin + a->bmax) * 0.5f);
    if (Physics::boxBlocked(ctr + vec3(0, 0.05f, 0), half, yawQuat(yaw), ~0ull, true) && !snapped) return "Blocked by something here";
  }
  return "";
}

// a placement kept as a session overlay (Instance::extra "session"); returns its id first, then any companions
std::vector<uint64_t> App::buildSessionPiece(const AssetDef* a, vec3 pos, float yaw, bool companions, BuildAction* act) {
  bool wasDirty = dirty;
  std::vector<uint64_t> ids;
  Instance i; i.id = doc.newId(); i.assetId = a->id; i.assetVersion = a->version;
  i.pos = pos; i.yaw = wrapDeg(yaw);
  i.extra = json::object(); i.extra["session"] = true;
  if (a->defense.blocksVehicles) i.extra["blocksVehicles"] = true;
  if (!a->station.kind.empty()) i.extra["deployedBy"] = "player";
  bump(i);
  doc.instances.push_back(i);
  ids.push_back(i.id);
  if (companions) {   // a bunker's crew stations / ammunition come with it, also as session pieces
    size_t n0 = doc.instances.size(); placeCompanions(i.id);
    for (size_t k = n0; k < doc.instances.size(); ++k) { auto& ci = doc.instances[k]; if (!ci.extra.is_object()) ci.extra = json::object(); ci.extra["session"] = true; ids.push_back(ci.id); }
  }
  dirty = wasDirty;
  syncRuntime();
  for (uint64_t id : ids) if (Instance* si = doc.find(id)) { if (!assetFor(*si)->station.kind.empty()) resetStation(*si); resetCrate(*si); }
  if (act) act->placed.insert(act->placed.end(), ids.begin(), ids.end());
  return ids;
}

void App::builderGhosts(std::vector<DrawItem>& ghosts) {
  if (!build.active) return;
  if (const Instance* t = doc.find(build.aimId)) if (const AssetDef* ta = assetFor(*t))   // the Del target, tinted
    ghosts.push_back({&ta->opaque, instanceMatrix(*t), vec4(1.0f, 0.55f, 0.15f, 0.18f)});
  if (!build.asset) return;
  const AssetDef* a = build.asset;
  mat4 M = glm::translate(mat4(1), build.pos) * glm::mat4_cast(yawQuat(build.ghostYaw));
  vec4 tint = build.valid ? vec4(0.25f, 0.95f, 0.4f, 0.45f) : vec4(1.0f, 0.25f, 0.2f, 0.45f);
  ghosts.push_back({&a->opaque, M, tint});   // same preview as the editor's Place tool
  ghosts.push_back({&a->panelsIntact, M, tint});
  if (a->model) {
    for (size_t k = 0; k < a->model->parts.size(); ++k) R.worldModels.push_back({a->model.get(), (int)k, M});
    ghosts.push_back({&a->colliderGhost, M, vec4(vec3(tint), 0.22f)});
  }
  if (isVehicleAsset(a)) tanks.ghostItems(*this, a, M, tint);
  for (auto& d : a->interactables)
    for (auto& lf : d.leaves)
      ghosts.push_back({&lf.opaque, M * glm::translate(mat4(1), lf.hinge) * glm::mat4_cast(glm::angleAxis(radians(lf.sign * (d.defaultOpen ? d.openAngle : 0)), d.axis)), tint});
}

void App::removeSessionPlacements() {
  size_t n0 = doc.instances.size();
  doc.instances.erase(std::remove_if(doc.instances.begin(), doc.instances.end(), isSession), doc.instances.end());
  if (doc.instances.size() != n0) syncRuntime();
  build = BuildState{};
}

// ---------------------------------------------------------------- defences under fire
bool App::defenseBulletHit(uint64_t iid, vec3 at) {
  auto it = rt.find(iid);
  if (it == rt.end() || it->second.asset->defense.kind.empty()) return false;
  InstanceRT& r = it->second;
  const DefenseDef& d = r.asset->defense;
  if (!r.defDestroyed) {
    r.defHp -= 1.0f * d.smallArms;   // tank trap: multiplier 0 (immune to small arms)
    if (r.defHp <= 0) { Instance* i = doc.find(iid); if (i) destroyDefense(*i, r); }
  }
  lastPen = {d.kind, 0, 0, 0, false, at, at};
  return true;   // always stops the round
}

// Wire obstacles: the supplied model is baked in `sections` slabs along asset z (model part "section"), and the
// collider parts carry the same "sec" index. A cut section hides its slab and drops its colliders (solid posts /
// frame and the player-only traversal volume); the rest of the obstacle stays. All sections cut = destroyed.
void App::rebuildSectionBodies(const Instance& i, InstanceRT& r) {
  Physics::remove(r.staticBody); r.staticBody = 0xffffffffu;
  Physics::remove(r.playerBody); r.playerBody = 0xffffffffu;
  std::vector<Part> solid, player;
  for (auto& p : r.asset->parts) {
    if (!p.collide) continue;
    if (p.sec >= 0 && p.sec < (int)r.secCut.size() && r.secCut[p.sec]) continue;
    (p.playerOnly ? player : solid).push_back(p);
  }
  uint64_t base = i.id << 16;
  if (!solid.empty()) r.staticBody = Physics::addStatic(Physics::buildShape(solid), i.pos, yawQuat(i.yaw), base);
  if (!player.empty() && r.asset->defense.traversal != "slow") r.playerBody = Physics::addPlayerOnly(Physics::buildShape(player), i.pos, yawQuat(i.yaw), base);
}

float App::traversalFactor(vec3 feet) const {
  float f = 1.0f;
  for (auto& [idx, rp] : const_cast<App&>(*this).activeSet().slowWire) {   // slow wire only (app.cpp), not every instance
    const Instance& i = doc.instances[idx]; const InstanceRT& r = *rp; const DefenseDef& d = r.asset->defense;
    if (r.defDestroyed) continue;
    vec3 lp = glm::inverse(yawQuat(i.yaw)) * (feet - i.pos);
    for (auto& p : r.asset->parts) {   // inside any uncut player-only volume (axis-aligned in asset space)
      if (!p.playerOnly || (p.sec >= 0 && p.sec < (int)r.secCut.size() && r.secCut[p.sec])) continue;
      vec3 h = p.size * 0.5f;
      if (glm::all(glm::lessThanEqual(glm::abs(lp - p.center), h + vec3(0.1f, 0.0f, 0.1f)))) f = std::min(f, d.slowFactor);
    }
  }
  return f;
}

void App::destroyDefense(Instance& i, InstanceRT& r) {
  if (r.defDestroyed) return;
  if (r.asset->defense.kind == "wire") {   // every stretch cut
    r.defDestroyed = true;
    std::fill(r.secCut.begin(), r.secCut.end(), 1);
    rebuildSectionBodies(i, r);
    return;
  }
  r.defDestroyed = true;
  Physics::remove(r.staticBody);
  r.staticBody = Physics::addStatic(r.asset->defBrokenShape ? r.asset->defBrokenShape : r.asset->shape, i.pos, yawQuat(i.yaw), i.id << 16);
  audio.play("explosion", i.pos, 0.5f);
}

void App::blastDefenses(vec3 p, float dmg, float radius) {
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    InstanceRT& r = it->second;
    if (r.asset->defense.kind.empty() || r.defDestroyed) continue;
    vec3 lp = glm::inverse(yawQuat(i.yaw)) * (p - i.pos);
    float dist = glm::length(glm::clamp(lp, r.asset->bmin, r.asset->bmax) - lp);
    if (dist > radius) continue;
    if (r.asset->defense.kind == "wire" && !r.secHp.empty()) {   // each stretch takes the blast by its own distance
      int n = (int)r.secHp.size(); bool any = false, all = true;
      float z0 = r.asset->bmin.z, L = std::max(1e-3f, r.asset->bmax.z - z0);
      for (int s = 0; s < n; ++s) {
        if (r.secCut[s]) continue;
        vec3 lo(r.asset->bmin.x, r.asset->bmin.y, z0 + L * s / n), hi(r.asset->bmax.x, r.asset->bmax.y, z0 + L * (s + 1) / n);
        float ds = glm::length(glm::clamp(lp, lo, hi) - lp);
        if (ds > radius) continue;
        r.secHp[s] -= dmg * (1.0f - ds / radius);
        if (r.secHp[s] <= 0) { r.secCut[s] = 1; any = true; fx.impact(vec3(instanceMatrix(i) * vec4((lo + hi) * 0.5f, 1)), vec3(0, 1, 0), "metal"); }
      }
      for (int s = 0; s < n; ++s) if (!r.secCut[s]) all = false;
      if (any) { rebuildSectionBodies(i, r); audio.play("ping", i.pos, 0.5f); }   // metallic snap (stand-in until the audio mapping, A1)
      if (all) r.defDestroyed = true;
      continue;
    }
    r.defHp -= dmg * (1.0f - dist / radius);
    if (r.defHp <= 0) destroyDefense(i, r);
  }
}

float App::blastOcclusion(vec3 from, vec3 to) const {
  // epicentre -> target ray: a sandbag collider in between multiplies the explosive damage by its occlusion (0.1)
  vec3 d = to - from; float len = glm::length(d);
  if (len < 1e-3f) return 1.0f;
  Physics::Hit h;
  if (!Physics::castRayEx(from, d, len - 0.05f, h)) return 1.0f;
  uint64_t iid = h.ud >> 16;
  if (h.ud != 0 && (h.ud >> 60) != 0xF) {
    const Instance* i = nullptr; for (auto& o : doc.instances) if (o.id == iid) i = &o;
    if (i) { const AssetDef* a = assetFor(*i); if (a->defense.kind == "sandbag") return a->defense.occlusion; }
  }
  return 0.1f;   // any other solid cover (walls, ground) shelters the same way
}

void App::blastPlayer(vec3 p, float dmg, float radius) {
  if (mode != Mode::Explore) return;
  float R = radius * 3.0f;   // personnel exposure radius for the readout (game tuning)
  vec3 feet = Physics::character().pos;
  vec3 chest = feet + vec3(0, Physics::character().crouched ? 0.75f : 1.25f, 0), head = eyePos();
  float d = glm::length(chest - p);
  if (d > R) return;
  float raw = dmg * (1.0f - d / R);
  float m = 0.5f * blastOcclusion(p, chest) + 0.5f * blastOcclusion(p, head);
  lastBlast = {raw, m, raw * m, (float)now};
  blastShake = std::max(blastShake, std::min(1.0f, raw * m / 60.0f));
  if (raw * m > 15.0f) audio.play("tinnitus", eyePos(), std::min(1.0f, raw * m / 80.0f));
}

void App::drawBuilderUI() {
  ImDrawList* dl = ImGui::GetForegroundDrawList(); ImGuiIO& io = ImGui::GetIO();
  if (!build.active && build.palette) setBuildPalette(false);   // left build mode some other way (tank, mode change)
  if (build.active && build.palette && !pauseMenu) {   // the editor's Assets palette (palette.cpp)
    if (!selftest && glfwGetInputMode(win, GLFW_CURSOR) != GLFW_CURSOR_NORMAL) glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);   // e.g. after the pause menu
    ImGui::SetNextWindowPos(ImVec2(10, 54), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, std::max(300.0f, io.DisplaySize.y - 140)), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Build: assets", nullptr, ImGuiWindowFlags_NoCollapse)) {
      ImGui::TextDisabled("%s", build.realEdits ? "Placements are map edits (Ctrl+Z undoes)." : "Placements are a playtest overlay (this session).");
      ImGui::TextDisabled("Pick an asset to hold it. Tab / Esc: back to the view.");
      static AssetPalette pal; pal.thumb = 96;
      if (const AssetDef* pk = drawAssetPalette(*this, pal, build.asset)) { build.pick = pk->id; build.forceAsset.clear(); setBuildPalette(false); }
    }
    ImGui::End();
  }
  if (build.active && build.asset) {
    char b[320]; std::snprintf(b, sizeof b, "BUILD%s: %s  |  %s  |  slope %.0f deg  |  Tab assets, 1/2/Q defences, wheel/Z/X rotate, LMB place, Del remove%s, Ctrl+Z undo, RMB leave",
                               build.realEdits ? " (map)" : "", build.asset->name.c_str(), build.reason.c_str(), build.slopeDeg, build.aimId ? " (aimed)" : "");
    ImVec2 ts = ImGui::CalcTextSize(b);
    ImVec2 pp(io.DisplaySize.x * 0.5f - ts.x * 0.5f, io.DisplaySize.y * 0.78f);
    dl->AddRectFilled(ImVec2(pp.x - 10, pp.y - 6), ImVec2(pp.x + ts.x + 10, pp.y + ts.y + 6), IM_COL32(10, 10, 8, 190), 5);
    dl->AddText(pp, build.valid ? IM_COL32(120, 240, 140, 255) : IM_COL32(255, 120, 100, 255), b);
  }
  if (now - lastBlast.time < 4.0 && lastBlast.raw > 0) {
    char b[160]; std::snprintf(b, sizeof b, "Blast exposure %.0f (raw %.0f x %.2f%s)", lastBlast.value, lastBlast.raw, lastBlast.mult, lastBlast.mult < 0.99f ? " sheltered" : "");
    dl->AddText(ImVec2(14, io.DisplaySize.y * 0.12f), IM_COL32(255, 210, 160, 230), b);
  }
}
