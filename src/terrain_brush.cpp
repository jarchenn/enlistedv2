// V8 terrain sculpting (docs/terrain-tools.md): the Terrain tool's brushes (raise, lower, smooth, flatten, rolling
// hills, cliff / terrace), the sculpt layer they write (Terrain::sculpt, saved as MapDoc::terrainSculpt), placement
// re-seating, Generate hills (a seeded stamp on the base grid that avoids footprints, objectives and paths) and the
// editor UI for both. Beautify lives in beautify.cpp.
//
// Layers, bottom to top: base heightfield (template + MapDoc::terrainGen: outer ring, hill stamps; terrain.cpp)
// -> sculpt (fine 0.45 m tiles, absolute metres) -> terrain cuts (trenches lower the sculpted ground) -> crater deltas
// (MapDoc::terrainDeltas, relative to all of the above). Tile::base is everything below the craters, so persisted crater
// deltas and their disturbed-soil shading never include sculpting: sculpted ground, raised or lowered, shades as grass.
#include "terrain_tools.h"
#include "terrain_chunks.h"
#include "tank.h"
#include "scenario.h"
#include "editor_nav.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <algorithm>

float terrainFbm(float x, float z, uint32_t s);   // terrain.cpp

// ================================================================ sculpt layer (Terrain members)
float Terrain::sculptAt(float x, float z) const {
  if (sculpt.empty()) return 0;
  float ts = tileSize();
  int tx = (int)std::floor((x - org.x) / ts), tz = (int)std::floor((z - org.y) / ts);
  if (tx < 0 || tz < 0 || tx >= tilesX() || tz >= tilesZ()) return 0;
  auto it = sculpt.find(key(tx, tz));
  if (it == sculpt.end()) return 0;
  const auto& S = it->second;
  vec2 o = tileOrigin(tx, tz);
  float fs = fineSpacing();
  float fx = std::clamp((x - o.x) / fs, 0.0f, kFine - 0.0001f), fz = std::clamp((z - o.y) / fs, 0.0f, kFine - 0.0001f);
  int xi = (int)fx, zi = (int)fz; float u = fx - xi, v = fz - zi;
  float a = S[zi * kFineN + xi], b = S[zi * kFineN + xi + 1], c = S[(zi + 1) * kFineN + xi], d = S[(zi + 1) * kFineN + xi + 1];
  if (u >= v) return a + (b - a) * u + (d - b) * v;   // the triangle split of height()
  return a + (c - a) * v + (d - c) * u;
}

void Terrain::sculptChanged(const std::vector<int>& keys) {
  std::set<int> remesh;
  const float fs = fineSpacing();
  for (int k : keys) {
    if (k < 0 || k >= tilesX() * tilesZ()) continue;
    int tx = keyTx(k), tz = keyTz(k);
    auto it = tiles.find(k);
    if (it == tiles.end()) { if (activate(tx, tz)) remesh.insert(k); continue; }   // starts at the (sculpted, cut) base
    Tile& t = it->second;
    vec2 o = tileOrigin(tx, tz);
    t.cut = false;
    for (int j = 0; j < kFineN; ++j)
      for (int i = 0; i < kFineN; ++i) {
        float x = o.x + i * fs, z = o.y + j * fs, raw = groundBase(x, z);
        float nb = cuts.empty() ? raw : cutBase(x, z);
        size_t s = (size_t)j * kFineN + i;
        float crater = t.h[s] - t.base[s];   // craters ride on the new ground
        t.base[s] = nb; t.h[s] = nb + crater;
        t.cut |= nb < raw - 1e-5f;
      }
    physDirty.insert(k);
    remesh.insert(k);
  }
  std::set<int> all = remesh;   // neighbours share border normals
  for (int k : remesh) {
    int tx = keyTx(k), tz = keyTz(k);
    for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) if (tileAt(tx + dx, tz + dz)) all.insert(key(tx + dx, tz + dz));
  }
  for (int k : all) { auto it = tiles.find(k); if (it != tiles.end()) rebuildMesh(it->second); }
  ++revision;
}

// ================================================================ brush
const char* brushModeName(int m) {
  static const char* n[] = {"Raise", "Lower", "Smooth", "Flatten", "Rolling hills", "Cliff / terrace"};
  return m >= 0 && m < BR_COUNT ? n[m] : "?";
}
static float smoothstep01(float e0, float e1, float x) { float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f); return t * t * (3 - 2 * t); }
// 1 inside the hard core, feathering to 0 at the rim
static float brushWeight(float r, float falloff) {
  if (r >= 1.0f) return 0.0f;
  float core = 1.0f - std::clamp(falloff, 0.0f, 1.0f);
  if (r <= core) return 1.0f;
  return 1.0f - smoothstep01(core, 1.0f, r);
}
static constexpr float kSculptMin = -60.0f, kSculptMax = 80.0f;

std::vector<int> terrainBrushDab(App& app, const BrushParams& p, vec2 c, float dt) {
  Terrain& T = app.terrain;
  std::vector<int> keys;
  if (T.h.empty() || dt <= 0) return keys;
  const float R = std::max(0.5f, p.radius), fs = T.fineSpacing(), ts = T.tileSize();
  const int F = Terrain::kFine, N = Terrain::kFineN;
  int tx0 = std::max(0, (int)std::floor((c.x - R - fs - T.org.x) / ts)), tx1 = std::min(T.tilesX() - 1, (int)std::floor((c.x + R + fs - T.org.x) / ts));
  int tz0 = std::max(0, (int)std::floor((c.y - R - fs - T.org.y) / ts)), tz1 = std::min(T.tilesZ() - 1, (int)std::floor((c.y + R + fs - T.org.y) / ts));
  if (tx0 > tx1 || tz0 > tz1) return keys;
  std::vector<SeatRec> before = terrainSeats(app, c, R);
  // read phase: every new value from the pre-dab ground, sampled at the global fine-grid position (a sample shared by
  // two tiles gets bit-identical inputs in both, so tile borders never crack)
  struct Pend { int key; std::vector<float> s; };
  std::vector<Pend> pend;
  const float k = std::clamp(R * 0.12f, fs, 4.0f);   // smoothing kernel
  const float blend = 1.0f - std::exp(-std::max(0.0f, p.strength) * dt * 1.5f);
  for (int tz = tz0; tz <= tz1; ++tz)
    for (int tx = tx0; tx <= tx1; ++tx) {
      int key = T.key(tx, tz);
      auto it = T.sculpt.find(key);
      std::vector<float> s = it != T.sculpt.end() ? it->second : std::vector<float>((size_t)N * N, 0.0f);
      bool changed = false;
      for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
          float x = T.org.x + (float)(tx * F + i) * fs, z = T.org.y + (float)(tz * F + j) * fs;
          float d = glm::length(vec2(x - c.x, z - c.y));
          if (d >= R) continue;
          float w = brushWeight(d / R, p.falloff);
          if (w <= 0) continue;
          float ds = 0;
          switch (p.mode) {
            case BR_RAISE: ds = p.strength * dt * w; break;
            case BR_LOWER: ds = -p.strength * dt * w; break;
            case BR_SMOOTH: {
              float cur = T.height(x, z), sum = 0;
              for (int a = 0; a < 8; ++a) { float an = a * 0.78539816f; sum += T.height(x + std::cos(an) * k, z + std::sin(an) * k); }
              ds = (sum / 8.0f - cur) * blend * w;
              break;
            }
            case BR_FLATTEN: ds = (p.target - T.height(x, z)) * blend * w; break;
            case BR_HILLS: ds = p.strength * dt * w * terrainFbm(x / std::max(2.0f, p.scale), z / std::max(2.0f, p.scale), p.seed + 977) * 1.6f; break;
            case BR_TERRACE: {   // stepped falloff: flat treads, short steep risers
              int q = std::clamp(p.steps, 1, 8);
              float tq = w * q, fl = std::floor(tq);
              float prof = (fl + smoothstep01(0.82f, 1.0f, tq - fl)) / q;
              ds = p.strength * dt * prof;
              break;
            }
            default: break;
          }
          if (ds == 0) continue;
          float& v = s[(size_t)j * N + i];
          float nv = std::clamp(v + ds, kSculptMin, kSculptMax);
          if (nv != v) { v = nv; changed = true; }
        }
      if (changed) pend.push_back({key, std::move(s)});
    }
  // write phase
  for (auto& pd : pend) {
    bool any = false; for (float v : pd.s) if (std::fabs(v) > 1e-5f) { any = true; break; }
    if (any) T.sculpt[pd.key] = std::move(pd.s); else T.sculpt.erase(pd.key);
    keys.push_back(pd.key);
  }
  if (keys.empty()) return keys;
  T.sculptChanged(keys);
  terrainReseat(app, before);
  return keys;
}

void terrainCommitSculpt(App& app, const std::vector<int>& keys) {
  for (int k : keys) {
    auto it = app.terrain.sculpt.find(k);
    if (it == app.terrain.sculpt.end()) app.doc.terrainSculpt.erase(k); else app.doc.terrainSculpt[k] = it->second;
  }
  app.dirty = true;
}

// ================================================================ seating
// ground a placement stands on: buildings and other footprint assets the highest of nine footprint samples (the
// editor's placement rule), everything that drapes or sinks into the ground the height under its pivot
static bool pivotSeated(const AssetDef* a) {
  return !a || a->nature.kind != NatureDef::NONE || a->category == "Nature" || a->tree.on || a->path.on || !a->defense.kind.empty() ||
         !a->station.kind.empty() || isVehicleAsset(a) || a->footprint.x * a->footprint.y < 6.0f;
}
static float seatHeight(const App& app, const Instance& i, const AssetDef* a) {
  const Terrain& T = app.terrain;
  if (pivotSeated(a)) return T.placementHeight(i.pos.x, i.pos.z, i.id);
  vec2 hf = a->footprint * 0.5f; quat q = yawQuat(i.yaw);
  const vec3 loc[] = {{0, 0, 0}, {-hf.x, 0, -hf.y}, {hf.x, 0, -hf.y}, {-hf.x, 0, hf.y}, {hf.x, 0, hf.y}, {0, 0, -hf.y}, {0, 0, hf.y}, {-hf.x, 0, 0}, {hf.x, 0, 0}};
  float top = -1e9f;
  for (auto& l : loc) { vec3 w = i.pos + q * l; top = std::max(top, T.placementHeight(w.x, w.z, i.id)); }
  return top;
}
static bool insideFootprint(const Instance& host, const AssetDef* a, vec3 p, float pad = 0.3f) {
  vec3 l = glm::inverse(yawQuat(host.yaw)) * (p - host.pos);
  return std::fabs(l.x) <= a->footprint.x * 0.5f + pad && std::fabs(l.z) <= a->footprint.y * 0.5f + pad;
}
std::vector<SeatRec> terrainSeats(const App& app, vec2 c, float radius) {
  std::vector<SeatRec> out;
  for (auto& i : app.doc.instances) {
    const AssetDef* a = app.assetFor(i);
    if (radius >= 0) {
      float reach = radius + (a ? glm::length(a->footprint) * 0.5f : 1.0f) + 0.5f;
      if (glm::length(vec2(i.pos.x, i.pos.z) - c) > reach) continue;
    }
    SeatRec r; r.id = i.id; r.h = seatHeight(app, i, a);
    if (a && app.interior.isProp(a)) {   // furniture rides with the building it stands in
      for (auto& h : app.doc.instances) {
        const AssetDef* ha = app.assetFor(h);
        if (&h == &i || !ha || app.interior.isProp(ha) || !ha->enterable) continue;
        if (insideFootprint(h, ha, i.pos)) { r.host = h.id; break; }
      }
    }
    out.push_back(r);
  }
  return out;
}
int terrainReseat(App& app, const std::vector<SeatRec>& before) {
  std::map<uint64_t, float> dy;
  int moved = 0;
  for (auto& b : before) {
    Instance* i = app.doc.find(b.id); if (!i) continue;
    if (b.host) continue;
    dy[b.id] = seatHeight(app, *i, app.assetFor(*i)) - b.h;
  }
  for (auto& b : before) {
    Instance* i = app.doc.find(b.id); if (!i) continue;
    float d = 0;
    if (b.host) { auto it = dy.find(b.host); d = it != dy.end() ? it->second : seatHeight(app, *i, app.assetFor(*i)) - b.h; }
    else d = dy[b.id];
    if (std::fabs(d) < 1e-4f) continue;
    i->pos.y += d; app.bump(*i); ++moved;
  }
  if (app.doc.start.set) {   // the explore start marker follows the ground it stands on
    float g = app.terrain.height(app.doc.start.pos.x, app.doc.start.pos.z);
    if (std::fabs(g - app.doc.start.pos.y) < 3.0f) app.doc.start.pos.y = g;
  }
  return moved;
}

// ================================================================ whole-map layers
json terrainProtectShapes(const App& app) {
  json o = json::array(), c = json::array(), l = json::array();
  auto r2 = [](float v) { return std::round(v * 100.0f) / 100.0f; };
  for (auto& i : app.doc.instances) {
    const AssetDef* a = app.assetFor(i);
    if (!a) continue;
    // ground dressing re-seats instead (trees, shrubs, boulders, patches)
    if (a->nature.kind != NatureDef::NONE || a->tree.on || a->category == "Nature" || a->category == "Trees" || a->category == "Bushes") continue;
    if (a->category == "Vegetation" && a->footprint.x * a->footprint.y < 9.0f) continue;
    if (a->path.on) { vec3 f = yawQuat(i.yaw) * vec3(0, 0, a->footprint.y * 0.5f); l.push_back({r2(a->footprint.x + 1.0f), r2(i.pos.x - f.x), r2(i.pos.z - f.z), r2(i.pos.x + f.x), r2(i.pos.z + f.z)}); continue; }
    o.push_back({r2(i.pos.x), r2(i.pos.z), r2(a->footprint.x * 0.5f), r2(a->footprint.y * 0.5f), r2(i.yaw)});
  }
  if (app.doc.extra.is_object() && app.doc.extra.contains("scenario")) {
    Scenario s = Scenario::fromJson(app.doc.extra["scenario"]);
    for (auto& ob : s.objectives) {
      float r = ob.radius;
      if (ob.shape == CaptureShape::Polygon) for (auto& p : ob.polygon) r = std::max(r, glm::length(p - vec2(ob.pos.x, ob.pos.z)));
      c.push_back({r2(ob.pos.x), r2(ob.pos.z), r2(r)});
      for (auto& rt : ob.routes) { json line = {6.0f}; for (auto& p : rt.points) { line.push_back(r2(p.x)); line.push_back(r2(p.z)); } if (line.size() >= 3) l.push_back(line); }
    }
    for (auto& sp : s.spawns) c.push_back({r2(sp.pos.x), r2(sp.pos.z), 10.0f});
    // the attack axis: attackers' start through every objective in order
    json axis = {8.0f}; vec2 st = s.attackerStart(); axis.push_back(r2(st.x)); axis.push_back(r2(st.y));
    for (auto& ob : s.objectives) { axis.push_back(r2(ob.pos.x)); axis.push_back(r2(ob.pos.z)); }
    if (axis.size() >= 5) l.push_back(axis);
  }
  if (app.doc.extra.is_object() && app.doc.extra.contains("routes") && app.doc.extra["routes"].is_array())
    for (auto& r : app.doc.extra["routes"]) {
      json line = {r.value("width", 3.0f) + 1.0f};
      for (auto& p : r.value("pts", json::array())) if (p.is_array() && p.size() >= 2) { line.push_back(r2(p[0].get<float>())); line.push_back(r2(p[1].get<float>())); }
      if (line.size() >= 3) l.push_back(line);
    }
  if (app.doc.start.set) c.push_back({r2(app.doc.start.pos.x), r2(app.doc.start.pos.z), 3.0f});
  return json{{"o", o}, {"c", c}, {"l", l}};
}

void terrainRegenerate(App& app) {
  std::vector<SeatRec> before = terrainSeats(app, vec2(0), -1.0f);
  app.applyTemplate(app.doc.templateId);   // base grid + terrainGen layers (w3ApplyTerrain -> Terrain::generate)
  navSyncCuts(app);                        // generate() dropped the terrain cuts: re-cut now, not next frame, so seats compare like with like
  app.resetGround();                       // sculpt + crater deltas on the new grid
  terrainReseat(app, before);
  app.dirty = true;
}

void terrainGenerateHills(App& app, const HillsParams& p) {
  app.pushUndo();
  json st = {{"amp", p.amp}, {"scale", p.scale}, {"seed", p.seed}, {"pad", p.pad}, {"feather", p.feather}, {"protect", terrainProtectShapes(app)}};
  if (!app.doc.terrainGen.is_object()) app.doc.terrainGen = json::object();
  if (!app.doc.terrainGen.contains("hills") || !app.doc.terrainGen["hills"].is_array()) app.doc.terrainGen["hills"] = json::array();
  app.doc.terrainGen["hills"].push_back(st);
  terrainRegenerate(app);
  app.toast("Generated hills (Ctrl+Z to undo).", 4);
}
void terrainClearHills(App& app) {
  if (!app.doc.terrainGen.is_object() || !app.doc.terrainGen.contains("hills")) return;
  app.pushUndo();
  app.doc.terrainGen.erase("hills");
  terrainRegenerate(app);
  app.toast("Removed generated hills (Ctrl+Z to undo).", 4);
}

// ================================================================ strokes (editor)
namespace {
struct ToolState {
  BrushParams brush;
  bool stroking = false; std::set<int> strokeKeys; float sinceRev = 0;
  bool showHills = false; HillsParams hills;
  bool showBeautify = false;
} gT;
}
void terrainStrokeBegin(App& app, BrushParams& p, vec3 at) {
  app.pushUndo();
  p.target = app.terrain.height(at.x, at.z);
  gT.strokeKeys.clear(); gT.stroking = true; gT.sinceRev = 0;
}
void terrainStrokeEnd(App& app) {
  if (!gT.stroking) return;
  gT.stroking = false;
  std::vector<int> keys(gT.strokeKeys.begin(), gT.strokeKeys.end());
  terrainCommitSculpt(app, keys);
  if (!app.undoStack.empty() && app.undoStack.back().sculpt == app.doc.terrainSculpt) app.undoStack.pop_back();   // nothing changed: no empty undo step
  ++app.groundRev;   // paths re-drape, nature patches re-check, the navmesh refreshes
  gT.strokeKeys.clear();
}
void terrainToolUpdate(App& app, float dt, bool uiMouse) {
  bool down = glfwGetMouseButton(app.win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  static bool prevDown = false;
  bool press = down && !prevDown; prevDown = down;
  if (!gT.stroking) {
    if (press && !uiMouse && !app.looking && app.hoverValid) terrainStrokeBegin(app, gT.brush, app.hoverGround);
    if (!gT.stroking) return;
  }
  if (!down) { terrainStrokeEnd(app); return; }
  if (!app.hoverValid) return;
  auto keys = terrainBrushDab(app, gT.brush, vec2(app.hoverGround.x, app.hoverGround.z), std::min(dt, 0.05f));
  gT.strokeKeys.insert(keys.begin(), keys.end());
  terrainCommitSculpt(app, keys);
  gT.sinceRev += dt;
  if (gT.sinceRev > 0.3f) { gT.sinceRev = 0; ++app.groundRev; }   // paths / patches follow while the stroke runs, not every frame
}

void terrainOverlay(App& app, Overlay& ov) {
  if (app.tool != Tool::Terrain || !app.hoverValid) return;
  const Terrain& T = app.terrain;
  vec3 c = app.hoverGround;
  float R = gT.brush.radius, core = R * (1.0f - std::clamp(gT.brush.falloff, 0.0f, 1.0f));
  vec4 col = gT.brush.mode == BR_LOWER ? vec4(1.0f, 0.55f, 0.3f, 1) : gT.brush.mode == BR_FLATTEN || gT.brush.mode == BR_SMOOTH ? vec4(0.4f, 0.8f, 1.0f, 1) : vec4(0.45f, 1.0f, 0.5f, 1);
  auto ring = [&](float r, vec4 cc, int n) {
    vec3 prev;
    for (int k = 0; k <= n; ++k) {
      float a = k * 6.2831853f / n;
      vec3 p(c.x + std::cos(a) * r, 0, c.z + std::sin(a) * r); p.y = T.height(p.x, p.z) + 0.12f;
      if (k) ov.line(prev, p, cc, true);
      prev = p;
    }
  };
  ring(R, col, 64);
  if (core > 0.3f && core < R - 0.2f) ring(core, vec4(vec3(col), 0.55f), 48);
  if (gT.brush.mode == BR_FLATTEN && gT.stroking) ring(std::min(R, 1.5f), vec4(1, 1, 1, 0.9f), 16);
  ov.line(c, c + vec3(0, 1.5f, 0), col, true);
}

// ================================================================ UI
void terrainToolbar(App& app) {
  bool active = app.tool == Tool::Terrain;
  if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.50f, 0.25f, 1));
  if (ImGui::Button("Terrain")) { if (active) app.tool = Tool::Select; else { app.tool = Tool::Terrain; app.placing = nullptr; app.selected = 0; } }
  if (active) ImGui::PopStyleColor();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sculpt the ground: raise, lower, smooth, flatten, rolling hills, cliffs");
  ImGui::SameLine();
}

void terrainMenus(App& app) {
  if (ImGui::BeginMenu("Terrain")) {
    if (ImGui::MenuItem("Sculpt brush", nullptr, app.tool == Tool::Terrain)) { app.tool = app.tool == Tool::Terrain ? Tool::Select : Tool::Terrain; app.placing = nullptr; }
    if (ImGui::MenuItem("Generate hills...")) gT.showHills = true;
    bool hasHills = app.doc.terrainGen.is_object() && app.doc.terrainGen.contains("hills");
    if (ImGui::MenuItem("Remove generated hills", nullptr, false, hasHills)) terrainClearHills(app);
    if (ImGui::MenuItem("Clear sculpting", nullptr, false, !app.doc.terrainSculpt.empty())) {
      app.pushUndo(); app.doc.terrainSculpt.clear();
      std::vector<SeatRec> before = terrainSeats(app, vec2(0), -1.0f);
      app.resetGround(); terrainReseat(app, before); app.dirty = true;
      app.toast("Cleared sculpted ground (Ctrl+Z to undo).");
    }
    ImGui::EndMenu();
  }
  if (ImGui::MenuItem("Beautify...")) gT.showBeautify = true;
}

void beautifyDialog(App& app, bool& open);   // beautify.cpp

void terrainEditorUI(App& app) {
  ImGuiIO& io = ImGui::GetIO();
  if (app.tool == Tool::Terrain) {
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 330, 70), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(310, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Terrain brush");
    BrushParams& b = gT.brush;
    for (int m = 0; m < BR_COUNT; ++m) { if (m % 3) ImGui::SameLine(); if (ImGui::RadioButton(brushModeName(m), b.mode == m)) b.mode = m; }
    ImGui::SliderFloat("Radius (m)", &b.radius, 1.0f, 80.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat(b.mode == BR_SMOOTH || b.mode == BR_FLATTEN ? "Rate" : "Strength (m/s)", &b.strength, 0.1f, 12.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Falloff", &b.falloff, 0.0f, 1.0f, "%.2f");
    if (b.mode == BR_HILLS) { ImGui::SliderFloat("Hill size (m)", &b.scale, 6.0f, 120.0f, "%.0f"); int s = (int)b.seed; if (ImGui::InputInt("Seed", &s)) b.seed = (uint32_t)std::max(0, s); }
    if (b.mode == BR_TERRACE) ImGui::SliderInt("Steps", &b.steps, 1, 6);
    ImGui::TextDisabled("LMB paint (one undo step per stroke)\nFlatten levels to the height you click.\nPlacements in the stroke re-seat on the ground.");
    ImGui::TextDisabled("%zu sculpted tiles", app.doc.terrainSculpt.size());
    if (ImGui::Button("Generate hills...")) gT.showHills = true;
    ImGui::SameLine(); if (ImGui::Button("Done")) app.tool = Tool::Select;
    ImGui::End();
  }
  if (gT.showHills) {
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Appearing);
    if (ImGui::Begin("Generate hills", &gT.showHills, ImGuiWindowFlags_AlwaysAutoResize)) {
      HillsParams& h = gT.hills;
      ImGui::TextWrapped("Rolling hills over the whole map. Building footprints, paths, routes, objectives, spawns and the attack axis keep their ground; trees, shrubs and boulders re-seat.");
      ImGui::SliderFloat("Amplitude (m)", &h.amp, 0.5f, 20.0f, "%.1f");
      ImGui::SliderFloat("Scale (m)", &h.scale, 30.0f, 400.0f, "%.0f");
      int s = (int)h.seed; if (ImGui::InputInt("Seed", &s)) h.seed = (uint32_t)std::max(0, s);
      ImGui::SliderFloat("Keep-flat margin (m)", &h.pad, 0.0f, 20.0f, "%.1f");
      ImGui::SliderFloat("Feather (m)", &h.feather, 2.0f, 60.0f, "%.0f");
      if (ImGui::Button("Generate")) { terrainGenerateHills(app, h); gT.showHills = false; }
      ImGui::SameLine(); if (ImGui::Button("Cancel")) gT.showHills = false;
    }
    ImGui::End();
  }
  if (gT.showBeautify) beautifyDialog(app, gT.showBeautify);
}
