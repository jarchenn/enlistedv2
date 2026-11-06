// Deformable ground: craters on the canonical height field, foundation protection, physics patch sync,
// session reset and explicit "apply damage to map" persistence.
#include "app.h"
#include <algorithm>

static uint64_t tileUD(int key) { return (0xFull << 60) | (uint64_t)key; }

float App::groundWeight(float x, float z) const {
  // 0 inside / right next to a placed building's footprint, ramping to 1 at 1.8 m: foundations,
  // thresholds and steps stay supported at any rotation.
  float w = 1.0f;
  for (auto& i : doc.instances) {
    const AssetDef* a = assetFor(i);
    if (a->station.kind.size() || isVehicleAsset(a)) continue;   // field guns and tanks sit on the ground, they do not protect it
    if (a->path.on || a->defense.kind == "wire") continue;   // paths and wire lie on open ground: craters form under them
    vec2 hf = a->footprint * 0.5f;
    if (glm::length(vec2(x - i.pos.x, z - i.pos.z)) > glm::length(hf) + 2.0f) continue;
    vec3 l = glm::inverse(yawQuat(i.yaw)) * vec3(x - i.pos.x, 0, z - i.pos.z);
    vec2 d = glm::max(glm::abs(vec2(l.x, l.z)) - hf, vec2(0));
    float dist = glm::length(d);
    float t = std::clamp((dist - 0.25f) / 1.55f, 0.0f, 1.0f);
    w = std::min(w, t * t * (3 - 2 * t));
  }
  return w;
}

void App::syncGroundTiles(const std::vector<int>& keys) {
  for (int k : keys) {
    auto it = terrain.tiles.find(k); if (it == terrain.tiles.end()) continue;
    auto& t = it->second;
    Physics::remove(t.body);
    vec2 o = terrain.tileOrigin(t.tx, t.tz);
    t.body = Physics::addHeightPatch(t.h, Terrain::kFineN, vec3(o.x, 0, o.y), terrain.fineSpacing(), tileUD(k));
    Physics::setBaseTile(t.tx, t.tz, false);
  }
}

bool App::craterAt(vec3 p, float radius, float depth, bool irregular) {
  float gh = terrain.height(p.x, p.z);
  if (p.y - gh > 0.9f) return false;                         // airburst: no ground contact
  if (gh < terrain.waterLevel + 0.05f) return false;         // open water: nothing to deform
  auto touched = terrain.crater(vec3(p.x, gh, p.z), radius, depth, [this](float x, float z) { return groundWeight(x, z); }, 1.6f, irregular);
  syncGroundTiles(touched);
  if (!touched.empty()) ++groundRev;
  return !touched.empty();
}

bool App::deformGround(vec3 p, float scale) {
  // Crater displacement (guide formula):  Z_new = Z_old - D_max * (max(0, 1 - d^2 / R^2))^p,  p = 2
  //   standard shell (7.5 cm HE): D_max 0.62 m, R 2.0 m;  AT shell (PzGr. 39): D_max 1.5 m, R 3.9 m (was 0.5 / 1.5, 1.2 / 3.0)
  // AT is the guide's deep profile scaled to stay climbable (peak wall ~32 deg); both are game tuning, not
  // measurements of wartime craters.
  // Shell craters have an irregular oblong outline (Terrain::craterShape), never narrower than R.
  if (blastAT) return craterAt(p, kATCraterR, kATCraterD, true);
  return craterAt(p, kStdCraterR * scale, kStdCraterD * scale, true);
}

void App::removeGroundBodies() {
  for (auto& [k, t] : terrain.tiles) { Physics::remove(t.body); t.body = 0xffffffffu; Physics::setBaseTile(t.tx, t.tz, true); }
}

void App::resetGround() {
  // Explore damage is a session overlay: restore the map's authored ground (base + applied deltas).
  removeGroundBodies();
  terrain.clearTiles();
  ++groundRev;
  if (!doc.terrainDeltas.empty()) {
    terrain.applyDeltas(doc.terrainDeltas);
    std::vector<int> keys;
    for (auto& [k, t] : terrain.tiles) keys.push_back(k);
    syncGroundTiles(keys);
  }
}

void App::applyDamageToMap() {
  // Explicit editor operation: transfer the session overlay (panels, openings, trees, built defences, terrain
  // deltas) into the map, as one undo step. The undo snapshot never contains unapplied session placements.
  pushUndo();
  if (!undoStack.empty()) { auto& v = undoStack.back().inst; v.erase(std::remove_if(v.begin(), v.end(), [](const Instance& i) { return i.extra.is_object() && i.extra.value("session", false); }), v.end()); }
  int panels = 0, leaves = 0, placed = 0;
  tanks.applyToMap(*this);   // tank damage / wreck / ammunition (tank_sim.cpp)
  for (auto& i : doc.instances) {
    bool changed = false;
    if (i.extra.is_object() && i.extra.value("session", false)) { i.extra.erase("session"); changed = true; ++placed; }
    auto it = rt.find(i.id); if (it == rt.end()) { if (changed) i.rev = ++revCounter; continue; }
    auto& r = it->second;
    if (!r.asset->panels.empty()) {   // wall damage is stored as its replayable events (old per-panel HP migrated)
      std::vector<WallEvent> authored; authoredWallEvents(i, r.asset, authored);
      if (r.wallEvents.size() != authored.size() || !i.panelHp.empty()) {
        if (!i.extra.is_object()) i.extra = json::object();
        i.extra["wallDamage"] = wallEventsJson(r); i.panelHp.clear();
        changed = true; panels += (int)(r.wallEvents.size() - std::min(r.wallEvents.size(), authored.size()));
      }
    }
    for (size_t k = 0; k < r.leafSt.size(); ++k)
      for (size_t l = 0; l < r.leafSt[k].size(); ++l) {
        if (!r.leafSt[k][l]) continue;
        std::string key = r.asset->interactables[k].id + "/" + std::to_string(l);
        int v = (r.leafSt[k][l] & 2) ? 2 : 1;
        if (!i.leafState.count(key) || i.leafState[key] != v) { i.leafState[key] = v; changed = true; ++leaves; }
      }
    if (!r.asset->station.kind.empty() && r.st.wrecked && !(i.extra.is_object() && i.extra.contains("station") && i.extra["station"].value("wrecked", false))) {
      if (!i.extra.is_object()) i.extra = json::object();
      i.extra["station"]["wrecked"] = true; changed = true; ++leaves;
    }
    if (r.asset->tree.on && r.treeFelled && !r.trunkApplied) { json ts = treeApplyState(i, r); if (!ts.is_null()) { i.extra["tree"] = ts; changed = true; ++leaves; } }
    if (changed) { i.rev = ++revCounter; }
  }
  doc.terrainDeltas = terrain.deltas();
  doc.terrainTemplateVersion = doc.templateVersion;
  dirty = true;
  char b[160];
  std::snprintf(b, sizeof b, "Applied session changes to map: %d wall panels, %d openings / trees, %d defences, %zu ground tiles (Ctrl+Z to undo).", panels, leaves, placed, doc.terrainDeltas.size());
  toast(b, 6);
}

void App::clearAppliedDamage() {
  pushUndo();
  for (auto& i : doc.instances) if (!i.panelHp.empty() || !i.leafState.empty() || i.extra.contains("tree") || i.extra.contains("wallDamage")) { i.panelHp.clear(); i.leafState.clear(); i.extra.erase("tree"); i.extra.erase("wallDamage"); i.rev = ++revCounter; }
  doc.terrainDeltas.clear();
  dirty = true;
  resetGround();
  toast("Cleared applied damage (Ctrl+Z to undo).");
}
