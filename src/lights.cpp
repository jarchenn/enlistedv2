// Light assets, interior lighting and lamp placement (L3, docs/lights.md).
//   * the "light" block of an asset (cached per asset) and the lights of the map gathered for the lighting engine;
//   * on / off: switched (extra.light.on), the prop broken or its glass shot out, the room's walls breached, time of day;
//   * interior lights generated with the room templates (one ceiling or table light per room, a fire on each hearth,
//     caged electric lamps in bunkers), never in rubble;
//   * fire particles (the wreck-flame recipe of tank_sim.cpp), Apply damage to map, lamp posts along routes, the
//     editor's Light section.
#include "interior.h"
#include "lights.h"
#include <unordered_set>
#include "app.h"
#include "autopath.h"
#include "settings.h"
#include "combat.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>
#include <map>
#include <cmath>

bool gAutoPathLamps = false;
std::string gLightsSkip;   // GX2 A/B timing: lights of this asset left out of the frame

namespace {
std::map<const AssetDef*, std::unique_ptr<LightAssetDef>> gDefs;
std::map<std::pair<const AssetDef*, int>, std::vector<int>> gRoomPanels;   // wall bays around each room (asset space)
std::map<uint64_t, float> gFxAcc;                                          // particle emission carry per instance
float gDaylight = -1.0f;

uint32_t hmix(uint32_t h) { h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16; return h; }
float h01(uint32_t a, uint32_t b = 0) { return (hmix(a * 0x9E3779B1u ^ hmix(b + 0x85ebca6bu)) & 0xFFFFFF) / 16777216.0f; }
vec3 v3(const json& j, vec3 def) {
  if (!j.is_array() || j.size() < 3) return def;
  return vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
}
bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
const json* extraLight(const Instance& i) {
  if (!i.extra.is_object()) return nullptr;
  auto it = i.extra.find("light");
  return it != i.extra.end() && it->is_object() ? &*it : nullptr;
}
// the generated room a piece belongs to: extra.furnish {host, slot "r<k>/..."}
bool furnishRoom(const Instance& i, uint64_t& host, int& room) {
  if (!i.extra.is_object()) return false;
  auto f = i.extra.find("furnish");
  if (f == i.extra.end() || !f->is_object()) return false;
  host = f->value("host", (uint64_t)0);
  std::string slot = f->value("slot", std::string());
  room = -1;
  if (slot.size() > 1 && slot[0] == 'r') room = std::atoi(slot.c_str() + 1);
  return host != 0;
}
float useScale(const std::string& use, float day) {
  if (use == "street") return std::clamp((0.72f - day) / 0.42f, 0.0f, 1.0f);   // on at dusk, off by day
  if (use == "interior") return glm::mix(1.0f, lightsDayScale(), std::clamp(day, 0.0f, 1.0f));
  if (use == "fire") return glm::mix(1.0f, 0.8f, std::clamp(day, 0.0f, 1.0f));
  return 1.0f;   // electric (bunkers)
}
}

// ============================================================ GX2: configuration, colour temperature
const json& interiorLightsConfig() {
  static const json j = [] {
    try { return json::parse(readTextFile(contentDir() / "settings" / "interior_lights.json")); }
    catch (...) { logf("lights: content/settings/interior_lights.json missing or invalid: the assets' own light blocks are used"); return json::object(); }
  }();
  return j;
}
float lightsDayScale() { static const float d = std::clamp(interiorLightsConfig().value("dayScale", kInteriorDay), 0.0f, 1.0f); return d; }
vec3 kelvinToRgb(float kelvin) {
  float t = std::clamp(kelvin, 1000.0f, 12000.0f) / 100.0f, r, g, b;
  r = t <= 66 ? 255.0f : 329.698727446f * std::pow(t - 60, -0.1332047592f);
  g = t <= 66 ? 99.4708025861f * std::log(t) - 161.1195681661f : 288.1221695283f * std::pow(t - 60, -0.0755148492f);
  b = t >= 66 ? 255.0f : (t <= 19 ? 0.0f : 138.5177312231f * std::log(t - 10) - 305.0447927307f);
  vec3 c = glm::clamp(vec3(r, g, b) / 255.0f, vec3(0), vec3(1));
  c = glm::pow(c, vec3(2.2f));   // sRGB-encoded fit -> linear
  return c / std::max({c.x, c.y, c.z, 1e-4f});
}

// ============================================================ GX2: dust motes in the sun shafts
int lightsMotes(App& app, const mat4& view, std::vector<FxVert>& add) {
  int q = std::clamp(settings().s.gfx.dustMotes, 0, 2);
  static const json M = interiorLightsConfig().value("dustMotes", json::object());
  int per = M.contains("perWindow") && M["perWindow"].is_array() && (int)M["perWindow"].size() > q ? M["perWindow"][q].get<int>() : (q == 0 ? 0 : q == 1 ? 18 : 40);
  if (per <= 0 || lightsDaylight(app) < 0.35f) return 0;
  vec3 sun = glm::normalize(app.R.sunDir);
  if (sun.y < 0.08f) return 0;
  const vec3 travel = -sun;
  const float range = M.value("range", 14.0f), depth = M.value("depth", 3.2f), size = M.value("size", 0.006f), bright = M.value("brightness", 1.4f);
  const int maxWin = M.value("maxWindows", 6);
  vec3 cam = app.mode == Mode::Explore ? app.eyePos() : app.camPos;
  struct W { vec3 c, n, u; float w, h, floor, d; uint64_t key; };
  std::vector<W> ws;
  for (auto& i : app.doc.instances) {
    if (std::fabs(i.pos.x - cam.x) > range + 40.0f || std::fabs(i.pos.z - cam.z) > range + 40.0f) continue;   // cheap reject first
    auto it = app.rt.find(i.id);
    if (it == app.rt.end() || !it->second.asset || !it->second.asset->enterable) continue;
    const AssetDef* a = it->second.asset;
    float reach = std::max(a->footprint.x, a->footprint.y) * 0.75f + range;
    if (glm::length(vec2(i.pos.x - cam.x, i.pos.z - cam.z)) > reach) continue;
    const RoomInfo* ri = app.interior.rooms(a);
    if (!ri || ri->storeys.empty()) continue;
    mat4 Mi = app.instanceMatrix(i); quat hq = yawQuat(i.yaw);
    vec3 mid = (a->bmin + a->bmax) * 0.5f;
    uint64_t wi = 0;
    for (const auto& d : a->interactables) {
      if (d.kind != "window") continue;
      ++wi;
      vec3 mn = d.apMin, mx = d.apMax;
      int na = (mx.x - mn.x) < (mx.z - mn.z) ? 0 : 2, la = 2 - na;
      float in = ((mn[na] + mx[na]) * 0.5f > mid[na]) ? -1.0f : 1.0f;   // toward the inside
      vec3 c = (mn + mx) * 0.5f; c[na] = in < 0 ? mn[na] : mx[na];
      vec3 n(0), u(0); n[na] = in; u[la] = 1;
      W w; w.c = vec3(Mi * vec4(c, 1)); w.n = hq * n; w.u = hq * u; w.w = mx[la] - mn[la]; w.h = mx.y - mn.y;
      if (glm::dot(travel, w.n) < 0.15f) continue;   // the sun is on the other side
      w.d = glm::length(w.c - cam);
      if (w.d > range) continue;
      float fl = ri->storeys[0].y;
      for (auto& S : ri->storeys) if (S.y <= mn.y + 0.05f) fl = S.y;
      w.floor = (Mi * vec4(0, fl, 0, 1)).y;
      w.key = i.id * 64 + wi;
      ws.push_back(w);
    }
  }
  std::sort(ws.begin(), ws.end(), [](const W& x, const W& y) { return x.d < y.d; });
  if ((int)ws.size() > maxWin) ws.resize(maxWin);
  // a window the sun cannot reach (another building, the eaves) has no shaft: a ray toward the sun, cached for a second
  static std::map<uint64_t, std::pair<double, bool>> lit;
  double now = glfwGetTime();
  mat4 inv = glm::inverse(view);
  vec3 right = vec3(inv[0]), up = vec3(inv[1]);
  float T = (float)std::fmod(now, 10000.0);
  int n = 0;
  for (auto& w : ws) {
    auto& L = lit[w.key];
    if (now - L.first > 1.0) { Physics::Hit h; L.second = !Physics::castRayEx(w.c - w.n * 0.4f + sun * 0.2f, sun, 60.0f, h); L.first = now; }
    if (!L.second) continue;
    for (int m = 0; m < per; ++m) {
      uint32_t hk = (uint32_t)(w.key * 977u) + (uint32_t)m * 131u;
      float ph = h01(hk, 1) * 6.2831f, period = 7.0f + 6.0f * h01(hk, 2);
      float f = std::fmod(T / period + h01(hk, 3), 1.0f);
      float alpha = std::pow(std::sin(f * 3.14159f), 2.0f);
      if (alpha < 0.02f) continue;
      vec3 p = w.c + w.u * ((h01(hk, 4) - 0.5f) * w.w) + vec3(0, (h01(hk, 5) - 0.5f) * w.h, 0) + travel * (0.15f + depth * h01(hk, 6));
      p += vec3(std::sin(T * 0.31f + ph), 0.6f * std::sin(T * 0.23f + ph * 1.7f) - f * 0.12f, std::cos(T * 0.27f + ph * 0.6f)) * 0.05f;
      if (p.y < w.floor + 0.08f) continue;
      float s = size * (0.6f + 0.8f * h01(hk, 7));
      vec4 col(vec3(1.0f, 0.93f, 0.8f) * bright * alpha, 1);
      vec3 rx = right * s, ry = up * s;
      vec3 p0 = p - rx - ry, p1 = p + rx - ry, p2 = p + rx + ry, p3 = p - rx + ry;
      add.push_back({p0, col, {0, 0}}); add.push_back({p1, col, {1, 0}}); add.push_back({p2, col, {1, 1}});
      add.push_back({p0, col, {0, 0}}); add.push_back({p2, col, {1, 1}}); add.push_back({p3, col, {0, 1}});
      ++n;
    }
  }
  return n;
}

// ============================================================ definitions
const LightAssetDef* lightDef(const AssetDef* a) {
  if (!a || !a->raw.is_object()) return nullptr;
  auto lj = a->raw.find("light");
  if (lj == a->raw.end()) return nullptr;
  auto& slot = gDefs[a];
  if (slot) return slot.get();
  slot = std::make_unique<LightAssetDef>();
  json arr = lj->is_array() ? *lj : json::array({*lj});
  std::vector<std::string> groups;
  if (a->raw.contains("prop") && a->raw["prop"].is_object())
    for (auto& g : a->raw["prop"].value("groups", json::array())) groups.push_back(g.get<std::string>());
  for (auto& l : arr) {
    if (!l.is_object() || slot->lights.size() >= 16) continue;
    LightSpec s;
    s.color = v3(l.value("color", json()), s.color);
    s.intensity = l.value("intensity", 1.0f); s.radius = l.value("radius", 6.0f);
    s.offset = v3(l.value("offset", json()), vec3(0));
    vec3 d = v3(l.value("dir", json()), vec3(0, -1, 0));
    s.dir = glm::length(d) > 1e-4f ? glm::normalize(d) : vec3(0, -1, 0);
    if (l.contains("cone") && l["cone"].is_array() && l["cone"].size() >= 2) {
      s.cosInner = std::cos(radians(l["cone"][0].get<float>())); s.cosOuter = std::cos(radians(l["cone"][1].get<float>()));
    }
    s.flicker = std::clamp(l.value("flicker", 0.0f), 0.0f, 1.0f);
    s.shadow = l.contains("shadow") ? (l["shadow"].is_boolean() ? (l["shadow"].get<bool>() ? 1 : 0) : l["shadow"].get<int>()) : 0;
    if (l.contains("group")) {
      if (l["group"].is_number()) s.group = l["group"].get<int>();
      else if (l["group"].is_string()) { auto it = std::find(groups.begin(), groups.end(), l["group"].get<std::string>()); s.group = it == groups.end() ? -1 : (int)(it - groups.begin()); }
    }
    s.use = l.value("use", std::string(s.flicker >= 0.3f ? "fire" : "interior"));
    s.fx = l.value("fx", std::string()); s.fxSize = l.value("fxSize", 0.2f);
    slot->lights.push_back(s);
  }
  // GX2: interior_lights.json "fixtures" override the asset's colour (from its colour temperature), intensity and radius
  const json& F = interiorLightsConfig().contains("fixtures") ? interiorLightsConfig()["fixtures"] : json::object();
  if (F.is_object() && F.contains(a->id)) {
    const json& f = F[a->id];
    float sat = std::clamp(interiorLightsConfig().value("kelvinSaturation", 0.72f), 0.0f, 1.0f);
    for (auto& s : slot->lights) {
      if (f.contains("kelvin")) s.color = glm::mix(vec3(1), kelvinToRgb(f["kelvin"].get<float>()), sat);
      if (f.contains("intensity")) s.intensity = f["intensity"].get<float>();
      if (f.contains("radius")) s.radius = f["radius"].get<float>();
    }
  }
  return slot.get();
}

// ============================================================ which buildings are lit
bool lightsRuinedAsset(const AssetDef* a) {   // one shared list: V4 isRuinedAsset() (interior.cpp)
  return a && isRuinedAsset(a->id);
}
bool lightsOnlyHost(const AssetDef* a) {
  if (!a || !a->enterable || lightsRuinedAsset(a) || startsWith(a->id, "mil_")) return false;   // GX6: mil_ hosts are furnished (military rooms)
  return startsWith(a->id, "bunker_") || startsWith(a->id, "aw_") || a->category == "Bunkers" || a->category == "Atlantic Wall";
}
bool lightsLitBuilding(const AssetDef* a) {
  if (!a || !a->enterable || lightsRuinedAsset(a) || a->category == "Furniture") return false;
  if (startsWith(a->id, "bch_") || a->category == "Landing craft" || startsWith(a->category, "Beach")) return false;   // landing craft are not buildings
  return true;
}

// ============================================================ time of day
void lightsSetDaylight(float d) { gDaylight = d; }
float lightsDaylight(App& app) {
  if (gDaylight >= 0.0f) return std::clamp(gDaylight, 0.0f, 1.0f);
  float e = glm::normalize(app.R.sunDir).y;   // sun elevation: full day above ~15 deg, night below the horizon
  return std::clamp((e + 0.05f) / 0.31f, 0.0f, 1.0f);
}

// ============================================================ on / off
bool lightsRoomBreached(App& app, uint64_t hostId, int room) {
  auto it = app.rt.find(hostId);
  if (it == app.rt.end() || !it->second.damaged || it->second.state.empty()) return false;
  const InstanceRT& r = it->second;
  const AssetDef* a = r.asset;
  if (!a) return false;
  const RoomInfo* ri = app.interior.rooms(a);
  if (room < 0 || room >= (int)ri->rooms.size()) return false;
  auto key = std::make_pair(a, room);
  auto pit = gRoomPanels.find(key);
  if (pit == gRoomPanels.end()) {
    const auto& R = ri->rooms[room];
    std::vector<int> v;
    const float m = 0.45f;
    for (size_t k = 0; k < a->panels.size(); ++k) {
      const Panel& p = a->panels[k];
      if (p.trim >= 0) continue;   // window / door trim breaking is not a breach
      if (p.bmax.x < R.mn.x - m || p.bmin.x > R.mx.x + m || p.bmax.z < R.mn.y - m || p.bmin.z > R.mx.y + m) continue;
      if (p.bmax.y < R.y0 + 0.1f || p.bmin.y > R.y1 - 0.1f) continue;
      v.push_back((int)k);
    }
    pit = gRoomPanels.emplace(key, std::move(v)).first;
  }
  for (int k : pit->second) if (k < (int)r.state.size() && r.state[k] == PANEL_BREACHED) return true;
  return false;
}

namespace {
std::string offReason(App& app, const Instance& i, const AssetDef* a, const LightAssetDef* d, int k) {
  if (const json* L = extraLight(i)) if (!L->value("on", true)) return "switched off";
  // P2: the piece's host / room come from its extra.furnish JSON (string parse); memoised per instance revision. The lamp,
  // glass and breach state below are read live every call (damage must put a lamp out the frame it happens).
  struct FR { uint32_t rev = 0; const void* ex = nullptr; bool gen = false; uint64_t host = 0; int room = -1; };
  static std::unordered_map<uint64_t, FR> frc;
  if (frc.size() > 65536) frc.clear();
  FR& fc = frc[i.id];
  if (fc.rev != i.rev || fc.ex != &i.extra) { fc.rev = i.rev; fc.ex = &i.extra; fc.host = 0; fc.room = -1; fc.gen = furnishRoom(i, fc.host, fc.room); }
  uint64_t host = fc.host; int room = fc.room;
  bool gen = fc.gen;
  if (gen) {
    auto ht = app.rt.find(host);
    if (ht != app.rt.end() && lightsRuinedAsset(ht->second.asset)) return "ruin";
  }
  if (const PropDef* pd = app.interior.prop(a)) {
    auto st = app.interior.st.find(i.id);
    if (st != app.interior.st.end()) {
      const LightSpec& s = d->lights[std::clamp(k, 0, (int)d->lights.size() - 1)];
      bool kept = s.group >= 0 && std::find(pd->keep.begin(), pd->keep.end(), s.group) != pd->keep.end() && s.group != pd->glass;
      if (st->second.crushed) return "broken";
      if (st->second.broken && !kept) return "broken";
      if (st->second.glassBroken && s.group >= 0 && s.group == pd->glass) return "glass broken";
    }
  }
  if (gen && room >= 0 && lightsRoomBreached(app, host, room)) return "room breached";
  return "";
}
}

std::string lightOffReason(App& app, const Instance& i, int k) {
  const AssetDef* a = app.assetFor(i);
  const LightAssetDef* d = lightDef(a);
  if (!d || d->lights.empty()) return "no light";
  return offReason(app, i, a, d, k);
}

float lightsEmissiveScale(App& app, const Instance& i) {
  const AssetDef* a = app.assetFor(i);
  const LightAssetDef* d = lightDef(a);
  if (!d || d->lights.empty()) return 1.0f;
  if (!offReason(app, i, a, d, 0).empty()) return 0.0f;
  return useScale(d->lights[0].use, lightsDaylight(app));
}

void lightsGather(App& app, std::vector<LightDesc>& out) {
  float day = lightsDaylight(app);
  for (auto& [idx, rp] : app.activeSet().lights) {   // instances with a light asset only (app.cpp), not every instance
    const Instance& i = app.doc.instances[idx];
    const AssetDef* a = rp->asset;
    const LightAssetDef* d = lightDef(a);
    if (!d || d->lights.empty() || (!gLightsSkip.empty() && a->id == gLightsSkip)) continue;
    const json* L = extraLight(i);
    float gain = L ? std::max(0.0f, L->value("gain", 1.0f)) : 1.0f;
    bool tint = L && L->contains("color");
    vec3 col = tint ? v3((*L)["color"], vec3(1)) : vec3(1);
    mat4 M = app.instanceMatrix(i);
    quat q = yawQuat(i.yaw);
    for (size_t k = 0; k < d->lights.size(); ++k) {
      const LightSpec& s = d->lights[k];
      if (!offReason(app, i, a, d, (int)k).empty()) continue;
      float sc = useScale(s.use, day) * gain;
      if (sc <= 1e-3f) continue;
      LightDesc o;
      o.pos = vec3(M * vec4(s.offset, 1)); o.radius = s.radius;
      o.color = tint ? col : s.color; o.intensity = s.intensity * sc;
      o.dir = glm::normalize(q * s.dir); o.cosInner = s.cosInner; o.cosOuter = s.cosOuter;
      o.flicker = s.flicker; o.shadow = s.shadow;
      o.id = (uint32_t)(i.id * 16u + (uint64_t)k);
      out.push_back(o);
    }
  }
}

// ============================================================ interior plan (with the room templates)
namespace {
struct Spot { vec3 p{0}; float ceil = 0; bool ok = false; };
// the room cell nearest its centre (L-shaped rooms) with the ceiling or beam above it: the highest of a few samples, so a
// lamp never hangs under a bunk or a shelf
Spot ceilingSpot(App& app, const Instance& host, const RoomInfo& ri, int k, const PropDef* pd = nullptr, float yaw = 0) {   // pd: the lamp must hang clear of beams
  const auto& R = ri.rooms[k]; const auto& S = ri.storeys[R.storey];
  vec2 cc = (R.mn + R.mx) * 0.5f;
  quat hq = yawQuat(host.yaw);
  std::vector<std::pair<float, vec2>> cells;
  for (int j = 0; j < S.nz; j += 2) for (int i = 0; i < S.nx; i += 2) {
    if (S.id[(size_t)j * S.nx + i] != k || S.keep[(size_t)j * S.nx + i]) continue;
    vec2 c(S.o.x + (i + 0.5f) * ri.cell, S.o.y + (j + 0.5f) * ri.cell);
    cells.push_back({glm::length(c - cc), c});
  }
  std::sort(cells.begin(), cells.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  Spot best;
  for (size_t n = 0; n < cells.size() && n < 14; ++n) {
    vec2 c = cells[n].second;
    vec3 o = host.pos + hq * vec3(c.x, R.y0 + 1.25f, c.y);
    Physics::Hit h;
    if (!Physics::castRayEx(o, vec3(0, 1, 0), std::max(1.0f, R.y1 - R.y0 + 1.5f), h)) continue;
    float cy = R.y0 + 1.25f + h.t;
    if (cy < R.y0 + 1.9f) continue;   // a shelf or a bunk, not the ceiling
    if (pd) {   // the editor's solid check for a hung piece: no beam or wall inside it
      quat q = yawQuat(host.yaw + yaw);
      vec3 he = glm::max((pd->bmax - pd->bmin) * 0.5f - vec3(0.02f, 0.04f, 0.02f), vec3(0.01f)) * 0.5f;
      vec3 top = host.pos + hq * vec3(c.x, cy - 0.004f, c.y);
      if (Physics::boxBlocked(top + q * ((pd->bmin + pd->bmax) * 0.5f) + vec3(0, 0.02f, 0), he, q)) continue;
      // the keep-clear test of InteriorSys::checkPlacement on the lamp's whole box: the keep grid above only marks cell
      // centres, so a lamp beside a stair box (keep extends 2.2 m above the flight) passed here and failed the re-check
      quat lq = yawQuat(yaw); vec3 mn(1e9f), mx(-1e9f);
      for (int k2 = 0; k2 < 8; ++k2) {
        vec3 cc((k2 & 1) ? pd->bmax.x : pd->bmin.x, (k2 & 2) ? pd->bmax.y : pd->bmin.y, (k2 & 4) ? pd->bmax.z : pd->bmin.z);
        vec3 lp = vec3(c.x, cy - 0.004f, c.y) + lq * cc; mn = glm::min(mn, lp); mx = glm::max(mx, lp);
      }
      mn += vec3(0.03f); mx -= vec3(0.03f);
      bool keepHit = false;
      for (auto& b : ri.keep) keepHit |= (pd->blocking || b.kind == 1) && mn.x < b.mx.x && mx.x > b.mn.x && mn.y < b.mx.y && mx.y > b.mn.y && mn.z < b.mx.z && mx.z > b.mn.z;
      if (keepHit) continue;
    }
    if (!best.ok || cy > best.ceil + 0.05f) { best.ok = true; best.ceil = cy; best.p = vec3(c.x, cy, c.y); }
  }
  return best;
}
bool hasSurface(InteriorSys& sys, const AssetDef* a) { const PropDef* pd = a ? sys.prop(a) : nullptr; return pd && !pd->surfaces.empty() && pd->mount <= 0; }
int surfaceRank(const std::string& id) {   // tables first, then dressers and chests; a wall shelf last
  if (id.find("table") != std::string::npos || id.find("desk") != std::string::npos) return 0;
  if (id.find("commode") != std::string::npos || id.find("dresser") != std::string::npos || id.find("sideboard") != std::string::npos || id.find("washstand") != std::string::npos) return 1;
  if (id.find("chest") != std::string::npos || id.find("side") != std::string::npos) return 2;
  return 3;
}
}

namespace {
// a ceiling point above (c.x, c.y) host-local: the ceiling or beam straight above, clear of beams for the lamp's box
Spot ceilingAt(App& app, const Instance& host, const RoomInfo::Room& R, vec2 c, const PropDef* pd, float yaw) {
  quat hq = yawQuat(host.yaw);
  Spot best;
  for (float d : {0.0f, 0.25f, -0.25f, 0.5f, -0.5f}) {
    for (int ax = 0; ax < 2; ++ax) {
      vec2 q = c + (ax == 0 ? vec2(d, 0) : vec2(0, d));
      vec3 o = host.pos + hq * vec3(q.x, R.y0 + 1.25f, q.y);
      Physics::Hit h;
      if (!Physics::castRayEx(o, vec3(0, 1, 0), std::max(1.0f, R.y1 - R.y0 + 1.5f), h)) continue;
      float cy = R.y0 + 1.25f + h.t;
      if (cy < R.y0 + 1.9f) continue;
      if (pd) {
        quat qq = yawQuat(host.yaw + yaw);
        vec3 he = glm::max((pd->bmax - pd->bmin) * 0.5f - vec3(0.02f, 0.04f, 0.02f), vec3(0.01f)) * 0.5f;
        vec3 top = host.pos + hq * vec3(q.x, cy - 0.004f, q.y);
        if (Physics::boxBlocked(top + qq * ((pd->bmin + pd->bmax) * 0.5f) + vec3(0, 0.02f, 0), he, qq)) continue;
      }
      best.ok = true; best.ceil = cy; best.p = vec3(q.x, cy, q.y);
      return best;
    }
  }
  return best;
}
// the middle of a room across axis ax (0: the room runs along x) at a point along it (L-shaped halls)
float acrossCentre(const RoomInfo& ri, int k, int ax, float at) {
  const auto& R = ri.rooms[k]; const auto& S = ri.storeys[R.storey]; const float C = ri.cell;
  float lo = 1e9f, hi = -1e9f;
  if (ax == 0) { int i = (int)std::floor((at - S.o.x) / C); if (i >= 0 && i < S.nx) for (int j = 0; j < S.nz; ++j) if (S.id[(size_t)j * S.nx + i] == k) { lo = std::min(lo, S.o.y + j * C); hi = std::max(hi, S.o.y + (j + 1) * C); } }
  else { int j = (int)std::floor((at - S.o.y) / C); if (j >= 0 && j < S.nz) for (int i = 0; i < S.nx; ++i) if (S.id[(size_t)j * S.nx + i] == k) { lo = std::min(lo, S.o.x + i * C); hi = std::max(hi, S.o.x + (i + 1) * C); } }
  return lo < hi ? (lo + hi) * 0.5f : (ax == 0 ? (R.mn.y + R.mx.y) * 0.5f : (R.mn.x + R.mx.x) * 0.5f);
}
// hallway pendant positions along the long axis: the midpoints between consecutive windows (a pool of lamp light
// between two of window light), thinned to at least minS apart, gaps over maxS filled evenly; without windows, evenly
// `spacing` apart
std::vector<float> hallStops(float lo, float hi, const std::vector<float>& win, float spacing, float minS, float maxS, float clear) {
  std::vector<float> c, pos;
  for (size_t w = 0; w + 1 < win.size(); ++w) { float m = (win[w] + win[w + 1]) * 0.5f; if (m >= lo - 0.3f && m <= hi + 0.3f && win[w + 1] - win[w] > 2 * clear) c.push_back(m); }
  if (c.empty()) {
    if (hi - lo < minS * 0.6f) { pos.push_back((lo + hi) * 0.5f); return pos; }
    int n = std::max(2, (int)std::round((hi - lo) / spacing) + 1);
    while (n > 2 && (hi - lo) / (n - 1) < minS) --n;
    while ((hi - lo) / (n - 1) > maxS) ++n;
    for (int i = 0; i < n; ++i) c.push_back(lo + (hi - lo) * i / (n - 1));
  }
  for (float x : c) if (pos.empty() || x - pos.back() >= minS * 0.8f) pos.push_back(x);
  auto nearWin = [&](float x) { for (float w : win) if (std::fabs(x - w) < clear) return true; return false; };
  // the ends beyond the first / last stop, and long gaps: evenly spaced extra stops, slid off the windows
  std::vector<float> ext{lo - 1e-3f}; ext.insert(ext.end(), pos.begin(), pos.end()); ext.push_back(hi + 1e-3f);
  std::vector<float> out;
  for (size_t i = 0; i + 1 < ext.size(); ++i) {
    float a = ext[i], b = ext[i + 1];
    bool first = i == 0, last = i + 2 == ext.size();
    if (i > 0) out.push_back(a);
    float gap = b - a;
    if (first || last) {   // from the outer stop toward the hall's end
      if (gap <= maxS * 0.55f) continue;
      int add = (int)std::ceil((gap - maxS * 0.55f) / spacing);
      for (int k = 1; k <= add; ++k) {
        float x = first ? b - spacing * k : a + spacing * k;
        if (x < lo - 0.01f || x > hi + 0.01f) continue;
        for (float d : {0.0f, clear + 0.3f, -(clear + 0.3f)}) if (!nearWin(x + d)) { x += d; break; }
        out.push_back(x);
      }
      continue;
    }
    if (gap <= maxS) continue;
    int add = (int)std::ceil(gap / maxS) - 1;
    for (int k = 1; k <= add; ++k) {
      float x = a + gap * k / (add + 1);
      for (float d : {0.0f, clear + 0.3f, -(clear + 0.3f)}) if (!nearWin(x + d)) { x += d; break; }
      out.push_back(x);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}
std::string pickOne(const json& list, uint32_t h) {   // "a|b" -> one of them
  if (!list.is_array() || list.empty() || !list[0].is_string()) return std::string();
  std::string e = list[0].get<std::string>(); std::vector<std::string> alt; size_t a = 0;
  for (size_t b; (b = e.find('|', a)) != std::string::npos; a = b + 1) alt.push_back(e.substr(a, b - a));
  alt.push_back(e.substr(a));
  return alt[(size_t)(h01(h, 5) * alt.size()) % alt.size()];
}
}

void lightsPlanRooms(App& app, InteriorSys& sys, const Instance& host, const AssetDef* ha, const RoomInfo& ri, uint32_t seed, std::vector<LightPlanItem>& items, const LightWallFn& wallFix) {
  if (!lightsLitBuilding(ha)) return;
  bool bunker = lightsOnlyHost(ha) || startsWith(ha->id, "mil_");   // caged electric lamps
  quat hq = yawQuat(host.yaw), iq = glm::inverse(hq);
  const size_t planned = items.size();
  const json& C = interiorLightsConfig();
  const json noRules = json::object();
  const json& RR = C.contains("rooms") ? C["rooms"] : noRules;
  const std::string lowCeil = C.value("lowCeiling", std::string("furn_lamp_caged"));
  for (size_t k = 0; k < ri.rooms.size(); ++k) {
    const auto& R = ri.rooms[k];
    if (R.type == ROOM_RUIN) continue;
    std::string r = "r" + std::to_string(k) + "/";
    auto inRoom = [&](const LightPlanItem& it) { return it.slot.rfind(r, 0) == 0; };
    bool lit = false;
    for (size_t q = 0; q < planned; ++q) if (inRoom(items[q]) && lightDef(app.lib.find(items[q].asset))) lit = true;   // an oil lamp or candles on a table already
    // ---- the room type's ceiling and table lights (interior_lights.json "rooms")
    std::string ceilingAsset, tableAsset;
    if (bunker) ceilingAsset = "furn_lamp_caged";
    else {
      const json& rule = RR.contains(roomTypeKey(R.type)) ? RR[roomTypeKey(R.type)] : RR.contains("default") ? RR["default"] : noRules;
      if (R.area >= rule.value("ceilingMinArea", 0.0f)) ceilingAsset = rule.value("ceiling", std::string());
      tableAsset = pickOne(rule.value("table", json::array()), seed + (uint32_t)k * 31u + 7u);
    }
    // a table light where the room has a table / dresser and no lamp yet (the church: the candelabra on the altar)
    bool tablePlaced = false;
    if (!tableAsset.empty() && (!lit || R.type == ROOM_CHURCH) && app.lib.find(tableAsset)) {
      int bestRank = 99; int best = -1;
      for (size_t q = 0; q < planned; ++q) {
        if (!inRoom(items[q]) || items[q].slot.find("/top") != std::string::npos) continue;
        const AssetDef* ba = app.lib.find(items[q].asset);
        if (!hasSurface(sys, ba)) continue;
        int rk = surfaceRank(ba->id);
        if (R.type == ROOM_CHURCH) rk = ba->id.find("table") != std::string::npos ? 0 : 5;   // the altar
        if (rk < bestRank) { bestRank = rk; best = (int)q; }
      }
      if (best >= 0 && bestRank < 3) {   // tables, dressers, commodes, chests: never a shelf board or a hutch top
        const LightPlanItem base = items[best];
        const PropDef* bp = sys.prop(app.lib.find(base.asset));
        const PropDef* lp = sys.prop(app.lib.find(tableAsset));
        vec4 s = bp->surfaces[0]; float sy = bp->surfY[0];
        for (size_t si = 1; si < bp->surfaces.size(); ++si) if (bp->surfY[si] > sy) { s = bp->surfaces[si]; sy = bp->surfY[si]; }   // the top, not a shelf below it
        vec3 he = lp ? (lp->bmax - lp->bmin) * 0.5f : vec3(0.06f);
        quat bq = yawQuat(base.yaw);
        // corners and centre of the top, away from what already stands on it
        std::vector<vec2> cand = {vec2(s.x + he.x + 0.04f, s.y + he.z + 0.04f), vec2(s.z - he.x - 0.04f, s.y + he.z + 0.04f), vec2((s.x + s.z) * 0.5f, (s.y + s.w) * 0.5f),
                                  vec2(s.x + he.x + 0.04f, s.w - he.z - 0.04f), vec2(s.z - he.x - 0.04f, s.w - he.z - 0.04f)};
        for (auto& c : cand) {
          if (c.x < s.x || c.x > s.z || c.y < s.y || c.y > s.w) continue;
          vec3 p = base.p + bq * vec3(c.x, sy, c.y);
          bool clash = false;
          for (auto& o : items) if (o.slot.rfind(base.slot + "/top", 0) == 0 && glm::length(vec2(o.p.x - p.x, o.p.z - p.z)) < std::max(he.x, he.z) + 0.11f) clash = true;
          if (clash) continue;
          items.push_back({base.slot + "/light", tableAsset, p, wrapDeg(base.yaw + (h01(seed, (uint32_t)k + 91u) - 0.5f) * 40.0f)});
          tablePlaced = true; break;
        }
      }
    }
    // ---- hallways: pendants every 5-6 m along the long axis, between the windows (interior_lights.json "hallway")
    const json& HW = C.contains("hallway") ? C["hallway"] : noRules;
    vec2 ext = R.mx - R.mn; int hax = ext.x >= ext.y ? 0 : 1; float hlen = hax == 0 ? ext.x : ext.y;
    bool hallway = !bunker && R.type == ROOM_HALL && hlen >= HW.value("minLength", 4.5f) && app.lib.find(HW.value("fixture", std::string()));
    if (hallway) {
      float inset = HW.value("endInset", 1.2f);
      float lo = (hax == 0 ? R.mn.x : R.mn.y) + inset, hi = (hax == 0 ? R.mx.x : R.mx.y) - inset;
      std::vector<float> win;
      for (auto& b : ri.keep) {
        if (b.kind != 2 || b.mn.y < R.y0 - 0.3f || b.mn.y > R.y1) continue;
        if (b.mx.x < R.mn.x - 0.9f || b.mn.x > R.mx.x + 0.9f || b.mx.z < R.mn.y - 0.9f || b.mn.z > R.mx.y + 0.9f) continue;
        win.push_back(hax == 0 ? (b.mn.x + b.mx.x) * 0.5f : (b.mn.z + b.mx.z) * 0.5f);
      }
      std::sort(win.begin(), win.end());
      std::vector<float> stops = hallStops(lo, hi, win, HW.value("spacing", 5.5f), HW.value("minSpacing", 5.0f), HW.value("maxSpacing", 6.0f), HW.value("windowClear", 0.7f));
      int n = 0;
      for (float x : stops) {
        std::string fx = HW.value("fixture", std::string());
        const AssetDef* ca = app.lib.find(fx); const PropDef* cp = sys.prop(ca);
        float cyaw = hax == 0 ? 0.0f : 90.0f;
        vec2 c = hax == 0 ? vec2(x, acrossCentre(ri, (int)k, 0, x)) : vec2(acrossCentre(ri, (int)k, 1, x), x);
        Spot sp = ceilingAt(app, host, R, c, cp, cyaw);
        float drop = cp ? -cp->bmin.y : 0.5f;
        if (sp.ok && sp.ceil - drop < R.y0 + 2.05f) { if (const AssetDef* fl = app.lib.find(lowCeil)) { ca = fl; cp = sys.prop(fl); drop = cp ? -cp->bmin.y : 0.2f; } }
        if (!sp.ok || sp.ceil - drop < R.y0 + 1.75f) continue;
        items.push_back({r + "pendant" + std::to_string(n++), ca->id, vec3(sp.p.x, sp.ceil - 0.004f, sp.p.z), cyaw});
        lit = true;
      }
    }
    // the ceiling light; a room left without a lamp gets one hung from its ceiling as well
    bool bigRoom = R.area >= C.value("ceilingAlwaysArea", 20.0f) && !bunker && !hallway;   // GX2: a large room always has a ceiling light
    if (bigRoom && ceilingAsset.empty()) ceilingAsset = RR.contains("default") ? RR["default"].value("ceiling", std::string("furn_lamp_ceiling")) : "furn_lamp_ceiling";
    bool wantCeiling = !ceilingAsset.empty() && (R.type != ROOM_BEDROOM || bigRoom) && !hallway;
    if (!wantCeiling && !tablePlaced && !lit) ceilingAsset = R.type == ROOM_UTILITY ? "lamp_hanging_lantern" : "furn_lamp_ceiling", wantCeiling = true;
    if (wantCeiling && !app.lib.find(ceilingAsset)) wantCeiling = false;
    if (wantCeiling) {
      const AssetDef* ca0 = app.lib.find(ceilingAsset);
      const PropDef* cp0 = sys.prop(ca0);
      float cyaw = (R.mx.x - R.mn.x) >= (R.mx.y - R.mn.y) ? 0.0f : 90.0f;
      // GX2: an oversized room gets one lamp per 4-5 m zone (the zones its furniture is grouped in), else one central
      std::vector<vec2> at;
      vec2 e = R.mx - R.mn; float zl = C.value("zoneLength", 4.5f), maxSide = C.value("zoneOver", 8.0f);
      if (std::max(e.x, e.y) > maxSide && !bunker) {
        int ax = e.x >= e.y ? 0 : 1; float Lz = ax == 0 ? e.x : e.y, Wd = ax == 0 ? e.y : e.x;
        int nz = (int)std::ceil(Lz / zl), nw = Wd > maxSide * 1.5f ? 2 : 1;   // one row down the middle unless very wide
        for (int a = 0; a < nz; ++a) for (int b = 0; b < nw; ++b) {
          float u = (a + 0.5f) / nz, w = (b + 0.5f) / nw;
          at.push_back(ax == 0 ? vec2(R.mn.x + e.x * u, R.mn.y + e.y * w) : vec2(R.mn.x + e.x * w, R.mn.y + e.y * u));
        }
      }
      int n = 0;
      auto hangAt = [&](Spot sp, const std::string& slot) {
        const AssetDef* ca = ca0; const PropDef* cp = cp0;
        float drop = cp ? -cp->bmin.y : 0.5f;   // hung from its origin (the top): parts reach down to bmin.y
        if (sp.ok && sp.ceil - drop < R.y0 + 2.05f && !bunker && ceilingAsset != lowCeil) {   // a low ceiling: a flush caged lamp instead
          if (const AssetDef* fl = app.lib.find(lowCeil)) { ca = fl; cp = sys.prop(fl); drop = cp ? -cp->bmin.y : 0.2f; }
        }
        if (sp.ok && sp.ceil - drop >= R.y0 + 1.75f) { items.push_back({slot, ca->id, vec3(sp.p.x, sp.ceil - 0.004f, sp.p.z), cyaw}); lit = true; ++n; }
      };
      for (size_t q = 0; q < at.size(); ++q) {
        int qi = (int)std::floor((at[q].x - ri.storeys[R.storey].o.x) / ri.cell), qj = (int)std::floor((at[q].y - ri.storeys[R.storey].o.y) / ri.cell);
        const auto& S = ri.storeys[R.storey];
        if (qi < 0 || qj < 0 || qi >= S.nx || qj >= S.nz || S.id[(size_t)qj * S.nx + qi] != (int16_t)k) continue;   // over this room's floor
        hangAt(ceilingAt(app, host, R, at[q], cp0, cyaw), q == 0 ? r + "light" : r + "light" + std::to_string(q));
      }
      if (!n) hangAt(ceilingSpot(app, host, ri, (int)k, cp0, cyaw), r + "light");
    }
    // ---- wall lights either side of the doorways of larger rooms and halls (interior_lights.json "sconces")
    const json& SC = C.contains("sconces") ? C["sconces"] : noRules;
    if (wallFix && !bunker && R.type != ROOM_BARN && R.type != ROOM_CHURCH && (R.area >= SC.value("minArea", 16.0f) || (R.type == ROOM_HALL && SC.value("halls", true)))) {
      bool electric = false;
      for (auto& p : SC.value("electricIds", json::array())) if (startsWith(ha->id, p.get<std::string>().c_str())) electric = true;
      std::vector<std::string> rustic; for (auto& x : SC.value("rustic", json::array())) rustic.push_back(x.get<std::string>());
      std::string fx = electric || rustic.empty() ? SC.value("electric", std::string("furn_sconce")) : rustic[(size_t)(h01(seed + (uint32_t)k * 13u, 6) * rustic.size()) % rustic.size()];
      const AssetDef* sa = app.lib.find(fx); const PropDef* spd = sa ? sys.prop(sa) : nullptr;
      // doorway cells next to this room, grouped into openings
      const auto& S = ri.storeys[R.storey]; const float Cc = ri.cell;
      std::vector<uint8_t> seen((size_t)S.nx * S.nz, 0);
      int placed = 0, perRoom = SC.value("perRoom", 2);
      float fromDoor = SC.value("fromDoor", 0.35f), halfW = spd ? std::max(spd->bmax.x - spd->bmin.x, 0.1f) * 0.5f : 0.08f;
      auto id = [&](int i, int j) { return i < 0 || j < 0 || i >= S.nx || j >= S.nz ? (int16_t)RoomInfo::WALL : S.id[(size_t)j * S.nx + i]; };
      for (int j = 0; j < S.nz && spd && placed < perRoom; ++j) for (int i = 0; i < S.nx && placed < perRoom; ++i) {
        if (id(i, j) != RoomInfo::DOORWAY || seen[(size_t)j * S.nx + i]) continue;
        int i0 = i, i1 = i, j0 = j, j1 = j; bool touches = false;
        std::vector<int> st{j * S.nx + i}; seen[(size_t)j * S.nx + i] = 1;
        while (!st.empty()) {
          int c = st.back(); st.pop_back(); int ci = c % S.nx, cj = c / S.nx;
          i0 = std::min(i0, ci); i1 = std::max(i1, ci); j0 = std::min(j0, cj); j1 = std::max(j1, cj);
          const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
          for (int d = 0; d < 4; ++d) {
            int ni = ci + di[d], nj = cj + dj[d];
            if (id(ni, nj) == (int16_t)k) touches = true;
            if (id(ni, nj) == RoomInfo::DOORWAY && !seen[(size_t)nj * S.nx + ni]) { seen[(size_t)nj * S.nx + ni] = 1; st.push_back(nj * S.nx + ni); }
          }
        }
        if (!touches || ((i1 - i0 + 1) * Cc > 2.6f && (j1 - j0 + 1) * Cc > 2.6f)) continue;   // an open-plan gap, not a doorway
        vec2 mn(S.o.x + i0 * Cc, S.o.y + j0 * Cc), mx(S.o.x + (i1 + 1) * Cc, S.o.y + (j1 + 1) * Cc);
        bool thinX = (mx.x - mn.x) < (mx.y - mn.y);   // the opening runs along z: the wall is along z, crossed along x
        for (float sg : {1.0f, -1.0f}) {
          vec2 q = (mn + mx) * 0.5f;
          if (thinX) q.x = sg > 0 ? mx.x + 0.25f : mn.x - 0.25f; else q.y = sg > 0 ? mx.y + 0.25f : mn.y - 0.25f;
          int qi = (int)std::floor((q.x - S.o.x) / Cc), qj = (int)std::floor((q.y - S.o.y) / Cc);
          if (id(qi, qj) != (int16_t)k) continue;
          int d = thinX ? (sg > 0 ? 1 : 3) : (sg > 0 ? 0 : 2);
          float a0 = thinX ? mn.y : mn.x, a1 = thinX ? mx.y : mx.x;
          for (float side : {-1.0f, 1.0f}) {
            if (placed >= perRoom) break;
            float along = side > 0 ? a1 + fromDoor + halfW : a0 - fromDoor - halfW;
            LightPlanItem got;
            if (wallFix((int)k, fx, r + "sconce" + std::to_string(placed), d, along, got)) { items.push_back(got); ++placed; lit = true; }
          }
        }
      }
    }
    // ---- a room still without a light (no ceiling within reach: a tower base, a stairwell): a lamp on a wall
    if (!lit && wallFix && !bunker) {
      vec2 cc = (R.mn + R.mx) * 0.5f;
      std::vector<std::string> rustic; for (auto& x : SC.value("rustic", json::array())) rustic.push_back(x.get<std::string>());
      std::string fx = rustic.empty() ? std::string("furn_lamp_wall_oil") : rustic.back();
      for (int d = 0; d < 4 && !lit; ++d) for (float off : {0.0f, 0.6f, -0.6f, 1.2f, -1.2f}) {
        LightPlanItem got;
        float along = (d == 0 || d == 2 ? cc.x : cc.y) + off;
        if (wallFix((int)k, fx, r + "walllamp", d, along, got)) { items.push_back(got); lit = true; break; }
      }
    }
    // ---- a fire on the hearth: in front of the sooted firebox back, on the hearthstone
    if (R.hearth && !bunker && app.lib.find("furn_fire_hearth")) {
      vec3 hp = R.hearthPos; vec2 inward(0, 0); bool back = false;
      vec2 cc = (R.mn + R.mx) * 0.5f;
      for (size_t pi = 0; pi < ha->furnFirst && pi < ha->parts.size(); ++pi) {
        const Part& p = ha->parts[pi];
        if (p.mat < 0 || p.mat >= (int)ha->materials.size() || ha->materials[p.mat].name != "soot") continue;
        if (glm::length(vec2(p.center.x - hp.x, p.center.z - hp.z)) > 1.6f || p.size.y < 0.5f) continue;   // the back of the firebox, not the throat
        if (p.size.x < p.size.z * 0.2f) { inward = vec2(cc.x > p.center.x ? 1.0f : -1.0f, 0); hp = vec3(p.center.x + inward.x * 0.32f, hp.y, p.center.z); back = true; break; }
        if (p.size.z < p.size.x * 0.2f) { inward = vec2(0, cc.y > p.center.z ? 1.0f : -1.0f); hp = vec3(p.center.x, hp.y, p.center.z + inward.y * 0.32f); back = true; break; }
      }
      if (!back) { vec2 d = cc - vec2(hp.x, hp.z); inward = glm::length(d) > 1e-3f ? glm::normalize(d) : vec2(0, 1); }
      // the floor of the firebox under it: stepped out of the fireplace's own collider (a solid block in some houses)
      // until it stands free on the hearth, as the editor's placement rules would want it
      float yaw = degrees(std::atan2(inward.x, inward.y));
      const AssetDef* fa = app.lib.find("furn_fire_hearth"); const PropDef* fp = sys.prop(fa);
      vec3 he = fp ? glm::max((fp->bmax - fp->bmin) * 0.5f - vec3(0.02f, 0.04f, 0.02f), vec3(0.01f)) * 0.5f : vec3(0.1f);
      vec3 fc = fp ? (fp->bmin + fp->bmax) * 0.5f : vec3(0, 0.1f, 0);
      quat fq = yawQuat(host.yaw + yaw);
      for (float step = 0; step <= 0.9f; step += 0.06f) {
        vec3 c(hp.x + inward.x * step, 0, hp.z + inward.y * step);
        vec3 o = host.pos + hq * vec3(c.x, R.y0 + 0.6f, c.z);
        Physics::Hit h;
        if (!Physics::castRayEx(o, vec3(0, -1, 0), 1.2f, h) || h.t < 0.05f) continue;   // started inside the chimney breast
        float fy = (iq * (h.pos - host.pos)).y;
        if (fy > R.y0 + 0.45f) continue;   // a mantel or a ledge, not the hearth
        if (Physics::boxBlocked(host.pos + hq * vec3(c.x, fy, c.z) + fq * fc + vec3(0, 0.02f, 0), he, fq)) continue;
        items.push_back({r + "hearthfire", "furn_fire_hearth", vec3(c.x, fy + 0.002f, c.z), wrapDeg(yaw)});
        break;
      }
    }
  }
}

bool lightsRoofed(App& app, const Instance& host) {
  const AssetDef* a = app.assetFor(host);
  const RoomInfo* ri = a ? app.interior.rooms(a) : nullptr;
  if (!ri) return false;
  for (size_t k = 0; k < ri->rooms.size(); ++k) if (ceilingSpot(app, host, *ri, (int)k).ok) return true;
  return false;
}

// ============================================================ fire particles (wreck-flame recipe, tank_sim.cpp)
void lightsStep(App& app, float dt) {
  vec3 cam = app.mode == Mode::Explore ? app.eyePos() : app.camPos;
  int fires = 0;
  for (auto& i : app.doc.instances) {
    if (glm::length(i.pos - cam) > 40.0f) continue;
    auto it = app.rt.find(i.id);
    if (it == app.rt.end() || !it->second.asset) continue;
    const LightAssetDef* d = lightDef(it->second.asset);
    if (!d) continue;
    mat4 M(1); bool haveM = false;
    for (size_t k = 0; k < d->lights.size(); ++k) {
      const LightSpec& s = d->lights[k];
      if (s.fx.empty() || fires >= 16) continue;
      if (!offReason(app, i, it->second.asset, d, (int)k).empty()) continue;
      ++fires;
      if (!haveM) { M = app.instanceMatrix(i); haveM = true; }
      vec3 base = vec3(M * vec4(s.offset - vec3(0, s.fxSize * 0.6f, 0), 1));
      float sz = s.fxSize, k2 = sz / 0.55f;
      float& acc = gFxAcc[i.id * 16 + k];
      if (s.fx == "smoke_column") {   // D-day: a burning wreck's dark column, drifting downwind (no flames of its own)
        acc += dt * 2.2f * std::max(1.0f, k2);
        while (acc >= 1.0f) {
          acc -= 1.0f;
          float g = 0.09f + 0.1f * app.fx.rnd();
          app.fx.smoke(base + vec3(app.fx.rnd() - 0.5f, 0, app.fx.rnd() - 0.5f) * sz, vec3(0.45f + 0.3f * app.fx.rnd(), 1.5f + 0.9f * app.fx.rnd(), 0.15f * app.fx.rnd()),
                       sz * (1.3f + app.fx.rnd()), 9.0f + 5.0f * app.fx.rnd(), vec4(g, g, g * 0.95f, 0.5f), 0.9f, 0.7f);
        }
        continue;
      }
      acc += dt * 30.0f * std::clamp(k2 * 1.6f, 0.35f, 1.0f);
      while (acc >= 1.0f) {
        acc -= 1.0f;
        if (app.fx.parts.size() >= app.fx.maxParts) app.fx.parts.erase(app.fx.parts.begin());
        Particle q; q.p = base + vec3(app.fx.rnd() - 0.5f, 0, app.fx.rnd() - 0.5f) * (sz * 0.55f);
        q.v = vec3(app.fx.rnd() * 0.5f - 0.25f, 1.6f + 1.4f * app.fx.rnd(), app.fx.rnd() * 0.5f - 0.25f) * k2;
        q.size = sz * (0.7f + 0.6f * app.fx.rnd()); q.life = q.maxLife = (0.35f + 0.45f * app.fx.rnd()) * std::clamp(std::sqrt(k2), 0.5f, 1.0f);
        float h = app.fx.rnd();
        q.col = vec4(6.0f + 3.0f * h, 2.2f + 1.6f * h, 0.5f + 0.4f * h, 1); q.additive = true; q.grow = -q.size * 0.9f; q.drag = 0.8f; q.lift = 2.5f * k2;
        app.fx.parts.push_back(q);
        if (app.fx.rnd() < 0.02f) app.fx.flash(base + vec3(0, sz * 0.5f, 0), sz * 2.2f, 0.12f, vec4(7, 3, 1, 1));
        if (s.fx == "flame_smoke" && app.fx.rnd() < 0.06f) {
          float g = 0.22f + 0.15f * app.fx.rnd();
          app.fx.smoke(base + vec3(0, sz * 1.5f, 0), vec3(app.fx.rnd() * 0.4f - 0.2f + 0.2f, 0.8f + 0.4f * app.fx.rnd(), app.fx.rnd() * 0.4f - 0.2f),
                       sz * (1.4f + app.fx.rnd()), 2.5f + 1.5f * app.fx.rnd(), vec4(g, g, g * 0.95f, 0.35f), 0.6f, 0.5f);
        }
      }
    }
  }
}

// ============================================================ Apply damage to map
int lightsApplyToMap(App& app) {
  int n = 0;
  for (auto& i : app.doc.instances) {
    const AssetDef* a = app.assetFor(i);
    const LightAssetDef* d = lightDef(a);
    if (!d || d->lights.empty()) continue;
    std::string why = offReason(app, i, a, d, 0);
    if (why.empty() || why == "switched off" || why == "ruin") continue;
    if (!i.extra.is_object()) i.extra = json::object();
    json& L = i.extra["light"]; if (!L.is_object()) L = json::object();
    L["on"] = false; L["applied"] = true;   // rev not bumped: the prop keeps its session look (shot-out glass) until reload
    app.dirty = true; ++n;
  }
  return n;
}

void lightsClearApplied(App& app) {
  for (auto& i : app.doc.instances) {
    if (!i.extra.is_object() || !i.extra.contains("light") || !i.extra["light"].is_object()) continue;
    json& L = i.extra["light"];
    if (!L.value("applied", false)) continue;
    L.erase("applied"); L.erase("on");
    if (L.empty()) i.extra.erase("light");
    app.dirty = true;
  }
}

// ============================================================ lamp posts along routes
std::vector<LampLine> lightsRoutesOf(const json& routes) {
  std::vector<LampLine> v;
  if (!routes.is_array()) return v;
  for (auto& r : routes) {
    if (!r.is_object()) continue;
    LampLine l; l.width = r.value("width", 2.0f);
    for (auto& p : r.value("pts", json::array())) if (p.is_array() && p.size() >= 2) l.pts.push_back(vec2(p[0].get<float>(), p[1].get<float>()));
    if (l.pts.size() >= 2) v.push_back(l);
  }
  return v;
}

std::vector<LampLine> lightsPathLines(App& app) {
  std::vector<LampLine> v;
  for (auto& i : app.doc.instances) {
    const AssetDef* a = app.assetFor(i);
    if (!a || !a->path.on) continue;
    vec2 wl = app.pathSize(i, a);
    PathShape sh = pathShape(i);
    LampLine l; l.width = wl.x;
    quat q = yawQuat(i.yaw);
    int n = std::max(2, (int)std::ceil(wl.y / 2.0f) + 1);
    for (int k = 0; k < n; ++k) {
      vec2 c = pathCentre(wl.y, sh.bend, -wl.y * 0.5f + wl.y * k / (n - 1));
      vec3 w = i.pos + q * vec3(c.x, 0, c.y);
      l.pts.push_back(vec2(w.x, w.z));
    }
    v.push_back(l);
  }
  if (app.doc.extra.is_object() && app.doc.extra.contains("routes")) for (auto& l : lightsRoutesOf(app.doc.extra["routes"])) v.push_back(l);
  return v;
}

int lightsLineRoutes(App& app, const std::vector<LampLine>& lines, const std::string& asset, float spacing, const std::string& tag) {
  const AssetDef* la = app.lib.find(asset);
  if (!la || lines.empty()) return 0;
  const PropDef* pd = app.interior.prop(la);
  vec3 bmn = pd ? pd->bmin : vec3(-0.2f, 0, -0.2f), bmx = pd ? pd->bmax : vec3(0.2f, 3, 0.2f);
  float H = bmx.y - bmn.y;
  std::vector<vec2> placed;   // lamp posts already on the map (any street lamp) keep their spacing too
  for (auto& i : app.doc.instances) { const LightAssetDef* d = lightDef(app.assetFor(i)); if (d && !d->lights.empty() && d->lights[0].use == "street") placed.push_back(vec2(i.pos.x, i.pos.z)); }
  // bodies a post may overlap: auto pieces (paths, other posts) and instances already removed from the doc whose bodies
  // stay in physics until the next sync, so a re-run lays out the same posts as the first run
  std::unordered_set<uint64_t> pass, live;
  for (auto& i : app.doc.instances) { live.insert(i.id); if (i.extra.is_object() && i.extra.contains("auto") && i.extra["auto"].is_string()) pass.insert(i.id); }
  for (auto& r : app.rt) if (!live.count(r.first)) pass.insert(r.first);
  std::vector<size_t> order(lines.size());
  for (size_t k = 0; k < order.size(); ++k) order[k] = k;
  auto lenOf = [&](const LampLine& l) { float s = 0; for (size_t k = 0; k + 1 < l.pts.size(); ++k) s += glm::length(l.pts[k + 1] - l.pts[k]); return s; };
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return lenOf(lines[a]) > lenOf(lines[b]); });
  int n = 0;
  const Terrain& T = app.terrain;
  for (size_t li : order) {
    const LampLine& L = lines[li];
    float side = (li % 2) ? 1.0f : -1.0f, run = 0, total = lenOf(L);
    for (size_t k = 0; k + 1 < L.pts.size(); ++k) {
      vec2 a = L.pts[k], b = L.pts[k + 1]; float sl = glm::length(b - a);
      if (sl < 1e-3f) continue;
      vec2 t = (b - a) / sl, nrm(-t.y, t.x);
      for (float s = 0; s < sl; s += 2.0f) {
        float along = run + s;
        if (total > 10.0f && (along < 3.5f || along > total - 3.5f)) continue;   // not at the doors the route joins
        vec2 c = a + t * s;
        bool near = false;
        for (auto& p : placed) if (glm::length(p - c) < spacing - L.width - 2.0f) { near = true; break; }   // quick reject; the post itself is checked below
        if (near) continue;
        for (float sd : {side, -side}) {
          vec2 w = c + nrm * sd * (L.width * 0.5f + 0.75f);
          bool close = false;
          for (auto& p : placed) if (glm::length(p - w) < spacing) { close = true; break; }   // spacing between the posts, not the route points
          if (close) continue;
          if (!T.inBuildArea(w.x, w.y)) continue;
          float y = T.height(w.x, w.y);
          if (y < T.waterLevel + 0.15f) continue;
          if (std::fabs(T.height(w.x + 0.5f, w.y) - T.height(w.x - 0.5f, w.y)) > 0.45f || std::fabs(T.height(w.x, w.y + 0.5f) - T.height(w.x, w.y - 0.5f)) > 0.45f) continue;   // steep
          if (!app.pathSurfaceAt(vec3(w.x, y, w.y)).empty()) continue;   // never on another path
          vec2 face = -nrm * sd;   // the lantern arm reaches over the route
          float yaw = degrees(std::atan2(face.x, face.y));
          quat q = yawQuat(yaw);
          vec3 cen = vec3(w.x, y, w.y) + q * ((bmn + bmx) * 0.5f) + vec3(0, 0.15f, 0);
          Physics::Overlap ov[16];
          int no = Physics::boxOverlaps(cen, glm::max((bmx - bmn) * 0.5f, vec3(0.3f, H * 0.5f - 0.2f, 0.3f)), q, ov, 16);
          bool blocked = false;   // a wall, a tree, a fence
          for (int o = 0; o < no && !blocked; ++o) blocked = !pass.count(ov[o].ud >> 16);
          if (blocked) continue;
          Instance in; in.id = app.doc.newId(); in.assetId = la->id; in.assetVersion = la->version;
          in.pos = vec3(w.x, y, w.y); in.yaw = wrapDeg(yaw);
          in.extra = json::object(); in.extra["auto"] = tag;
          app.bump(in); app.doc.instances.push_back(in);
          placed.push_back(w); ++n;
          side = -sd;   // the next one across the route (staggered)
          break;
        }
      }
      run += sl;
    }
  }
  if (n) app.dirty = true;
  return n;
}

// ============================================================ editor
void lightsEditorPanel(App& app, Instance& s) {
  const AssetDef* a = app.assetFor(s);
  const LightAssetDef* d = lightDef(a);
  uint64_t sid = s.id;
  if (!d) {   // a building: its generated lights
    if (!a || !a->enterable) return;
    int n = 0, on = 0;
    for (auto& i : app.doc.instances) {
      uint64_t h = 0; int room = -1;
      if (!furnishRoom(i, h, room) || h != sid) continue;
      const LightAssetDef* ld = lightDef(app.assetFor(i)); if (!ld) continue;
      ++n; on += lightOffReason(app, i).empty();
    }
    if (!n || !ImGui::CollapsingHeader("Lights")) return;
    ImGui::TextDisabled("%d interior lights, %d lit (daylight %.2f: lamps at %.0f %% by day)", n, on, lightsDaylight(app), lightsDayScale() * 100.0f);
    int set = -1;
    if (ImGui::Button("All lights on")) set = 1;
    ImGui::SameLine(); if (ImGui::Button("All lights off")) set = 0;
    if (set >= 0) {
      app.pushUndo();
      for (auto& i : app.doc.instances) {
        uint64_t h = 0; int room = -1;
        if (!furnishRoom(i, h, room) || h != sid || !lightDef(app.assetFor(i))) continue;
        if (!i.extra.is_object()) i.extra = json::object();
        if (set) { if (i.extra.contains("light") && i.extra["light"].is_object()) { i.extra["light"].erase("on"); if (i.extra["light"].empty()) i.extra.erase("light"); } }
        else i.extra["light"]["on"] = false;
        i.extra["furnish"]["manual"] = true;   // kept on regenerate
        app.bump(i);
      }
    }
    return;
  }
  if (d->lights.empty() || !ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) return;
  const LightSpec& s0 = d->lights[0];
  json L = extraLight(s) ? *extraLight(s) : json::object();
  bool on = L.value("on", true);
  vec3 col = L.contains("color") ? v3(L["color"], s0.color) : s0.color;
  float gain = L.value("gain", 1.0f);
  std::string why = lightOffReason(app, s);
  float sc = useScale(s0.use, lightsDaylight(app));
  ImGui::TextDisabled("%zu light%s, %s, radius %.1f m%s%s", d->lights.size(), d->lights.size() == 1 ? "" : "s", s0.use.c_str(), s0.radius,
                      s0.cosOuter > -1.0f ? ", spot" : "", s0.flicker > 0 ? ", flickers" : "");
  if (why.empty()) ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.5f, 1), "Lit: %.0f %% of its output at this time of day", sc * 100.0f);
  else ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Dark: %s", why.c_str());
  bool act = false, ed = false, toggled = false;
  if (ImGui::Checkbox("On", &on)) toggled = true;
  float c[3] = {col.x, col.y, col.z};
  ImGui::ColorEdit3("Colour", c, ImGuiColorEditFlags_Float); act |= ImGui::IsItemActivated(); ed |= ImGui::IsItemEdited();
  float shown = gain * s0.intensity;
  ImGui::SliderFloat("Intensity", &shown, 0.0f, std::max(4.0f, s0.intensity * 4.0f), "%.2f"); act |= ImGui::IsItemActivated(); ed |= ImGui::IsItemEdited();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("1 = a 60 W bulb seen at 1 m. The asset's own value is %.2f.", s0.intensity);
  bool reset = (L.contains("color") || L.contains("gain") || !on) && ImGui::Button("Reset light");
  if (toggled || act || reset) app.pushUndo();
  Instance* sp = app.doc.find(sid);
  if (!sp || !(toggled || ed || reset)) return;
  if (!sp->extra.is_object()) sp->extra = json::object();
  json& W = sp->extra["light"]; if (!W.is_object()) W = json::object();
  if (reset) sp->extra.erase("light");
  else {
    if (toggled) { if (on) W.erase("on"); else W["on"] = false; W.erase("applied"); }
    if (ed) { W["color"] = {c[0], c[1], c[2]}; W["gain"] = s0.intensity > 1e-4f ? shown / s0.intensity : 1.0f; }
  }
  if (sp->extra.contains("furnish") && sp->extra["furnish"].is_object()) sp->extra["furnish"]["manual"] = true;   // kept on regenerate
  app.bump(*sp);
}
