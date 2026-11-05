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
    if (a->station.kind.size()) continue;   // field guns sit on the ground, they do not protect it
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

bool App::craterAt(vec3 p, float radius, float depth) {
  float gh = terrain.height(p.x, p.z);
  if (p.y - gh > 0.9f) return false;                         // airburst: no ground contact
  if (gh < terrain.waterLevel + 0.05f) return false;         // open water: nothing to deform
  auto touched = terrain.crater(vec3(p.x, gh, p.z), radius, depth, [this](float x, float z) { return groundWeight(x, z); });
  syncGroundTiles(touched);
  return !touched.empty();
}

bool App::deformGround(vec3 p, float scale) {
  // shipped 7.5 cm HE profile (game tuning, not a measurement of wartime craters): ~3 m across, ~0.4 m deep
  return craterAt(p, 1.5f * scale, 0.42f * scale);
}

void App::removeGroundBodies() {
  for (auto& [k, t] : terrain.tiles) { Physics::remove(t.body); t.body = 0xffffffffu; Physics::setBaseTile(t.tx, t.tz, true); }
}

void App::resetGround() {
  // Explore damage is a session overlay: restore the map's authored ground (base + applied deltas).
  removeGroundBodies();
  terrain.clearTiles();
  if (!doc.terrainDeltas.empty()) {
    terrain.applyDeltas(doc.terrainDeltas);
    std::vector<int> keys;
    for (auto& [k, t] : terrain.tiles) keys.push_back(k);
    syncGroundTiles(keys);
  }
}

void App::applyDamageToMap() {
  // Explicit editor operation: transfer ONLY panel states and terrain deltas into the map, as one undo step.
  pushUndo();
  int panels = 0;
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    auto& r = it->second;
    bool changed = false;
    for (size_t k = 0; k < r.asset->panels.size(); ++k) {
      auto& p = r.asset->panels[k];
      float authored = i.panelHp.count(p.id) ? i.panelHp[p.id] : p.hp;
      if (std::fabs(r.hp[k] - authored) > 0.01f) { i.panelHp[p.id] = r.hp[k]; changed = true; ++panels; }
    }
    if (changed) { i.rev = ++revCounter; }
  }
  doc.terrainDeltas = terrain.deltas();
  doc.terrainTemplateVersion = doc.templateVersion;
  dirty = true;
  char b[160];
  std::snprintf(b, sizeof b, "Applied damage to map: %d wall panels, %zu ground tiles (Ctrl+Z to undo).", panels, doc.terrainDeltas.size());
  toast(b, 6);
}

void App::clearAppliedDamage() {
  pushUndo();
  for (auto& i : doc.instances) if (!i.panelHp.empty()) { i.panelHp.clear(); i.rev = ++revCounter; }
  doc.terrainDeltas.clear();
  dirty = true;
  resetGround();
  toast("Cleared applied damage (Ctrl+Z to undo).");
}
