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
#include "app.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>

static const char* kBuildItems[3] = {"def_tank_trap", "def_sandbag_straight", "def_sandbag_curved"};
static constexpr float kBuildReach = 5.0f, kSnapR = 0.6f;

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

void App::stepBuilder(const ExploreInput& in) {
  if (in.build) { build.active = !build.active; if (build.active) toast("Build mode: 1 tank trap, 2 sandbags, Q cycle, R / wheel rotate, LMB place, RMB or B cancel", 5); }
  if (!build.active) return;
  if (in.aim) { build.active = false; build.forceAsset.clear(); return; }
  if (in.choose1) build.item = 0;
  if (in.choose2) build.item = build.item == 1 ? 2 : 1;
  if (in.nextWeapon) build.item = (build.item + 1) % 3;
  if (in.reload) build.yaw += 15.0f;
  build.yaw += in.wheel * 15.0f;
  const AssetDef* a = lib.find(build.forceAsset.empty() ? kBuildItems[build.item] : build.forceAsset.c_str());
  if (!build.forceAsset.empty() && (in.choose1 || in.choose2 || in.nextWeapon)) { build.forceAsset.clear(); a = lib.find(kBuildItems[build.item]); }
  build.asset = a;
  build.valid = false; build.snapped = false;
  if (!a) { build.reason = "missing asset"; return; }
  vec3 eye = eyePos(), f = forward(eyeYaw, eyePitch);
  Physics::Hit h;
  bool hit = Physics::castRayEx(eye, f, kBuildReach, h);
  bool ground = hit && (h.ud == 0 || (h.ud >> 60) == 0xF);
  if (!hit || !ground) { build.reason = hit ? "Aim at open ground" : "Out of reach (5 m)"; build.pos = eye + f * kBuildReach; build.pos.y = terrain.height(build.pos.x, build.pos.z); build.ghostYaw = eyeYaw + build.yaw; return; }
  build.pos = h.pos; build.ghostYaw = eyeYaw + build.yaw;
  // theta = acos(N . Y_up) from the hit normal
  build.slopeDeg = degrees(std::acos(std::clamp(glm::dot(glm::normalize(h.normal), vec3(0, 1, 0)), -1.0f, 1.0f)));
  vec3 p = build.pos; float y = build.ghostYaw;
  if (snapDefense(a, p, y)) { build.pos = p; build.ghostYaw = y; build.snapped = true; }
  float maxSlope = a->defense.kind.empty() ? 20.0f : a->defense.maxSlopeDeg;   // stations: carriage limit
  if (build.slopeDeg > maxSlope) {
    char b[96]; std::snprintf(b, sizeof b, "Too steep: %.0f deg > %.0f deg", build.slopeDeg, maxSlope);
    build.reason = b; return;
  }
  skipSlopeRule = true;   // theta already judged from the hit normal above
  PlaceCheck c = checkPlacement(a, build.pos, build.ghostYaw, 0);
  skipSlopeRule = false;
  if (!c.ok) { build.reason = c.reason; return; }
  build.pos.y = c.y;
  vec3 half = (a->bmax - a->bmin) * 0.5f * 0.9f, ctr = build.pos + yawQuat(build.ghostYaw) * ((a->bmin + a->bmax) * 0.5f);
  if (Physics::boxBlocked(ctr + vec3(0, 0.05f, 0), half, yawQuat(build.ghostYaw), ~0ull, true) && !build.snapped) { build.reason = "Blocked by something here"; return; }
  build.valid = true; build.reason = build.snapped ? "Snaps to the sandbag run" : "Valid";
  bool firePress = in.fire && !build.fireHeld;
  build.fireHeld = in.fire;
  if (firePress) {
    Instance i; i.id = doc.newId(); i.assetId = a->id; i.assetVersion = a->version;
    i.pos = build.pos; i.yaw = wrapDeg(build.ghostYaw);
    i.extra = json::object(); i.extra["session"] = true;
    if (a->defense.blocksVehicles) i.extra["blocksVehicles"] = true;
    if (!a->station.kind.empty()) i.extra["deployedBy"] = "player";
    bump(i); dirty = false;
    doc.instances.push_back(i);
    ++buildPlaced;
    if (!a->station.kind.empty()) { syncRuntime(); resetStation(doc.instances.back()); build.active = false; build.forceAsset.clear(); }
    audio.play("crate", build.pos, 0.8f);
  }
}

void App::builderGhosts(std::vector<DrawItem>& ghosts) const {
  if (!build.active || !build.asset) return;
  mat4 M = glm::translate(mat4(1), build.pos) * glm::mat4_cast(yawQuat(build.ghostYaw));
  vec4 tint = build.valid ? vec4(0.25f, 0.95f, 0.4f, 0.45f) : vec4(1.0f, 0.25f, 0.2f, 0.45f);
  ghosts.push_back({&build.asset->opaque, M, tint});
}

void App::removeSessionPlacements() {
  size_t n0 = doc.instances.size();
  doc.instances.erase(std::remove_if(doc.instances.begin(), doc.instances.end(), [](const Instance& i) { return i.extra.is_object() && i.extra.value("session", false); }), doc.instances.end());
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
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    const InstanceRT& r = it->second; const DefenseDef& d = r.asset->defense;
    if (d.kind != "wire" || d.traversal != "slow" || r.defDestroyed) continue;
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
  if (build.active && build.asset) {
    char b[200]; std::snprintf(b, sizeof b, "BUILD: %s  |  %s  |  slope %.0f deg  |  1/2/Q item, R rotate, LMB place, RMB cancel", build.asset->name.c_str(), build.reason.c_str(), build.slopeDeg);
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
