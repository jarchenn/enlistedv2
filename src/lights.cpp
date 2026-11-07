// Light assets, interior lighting and lamp placement (L3, docs/lights.md).
//   * the "light" block of an asset (cached per asset) and the lights of the map gathered for the lighting engine;
//   * on / off: switched (extra.light.on), the prop broken or its glass shot out, the room's walls breached, time of day;
//   * interior lights generated with the room templates (one ceiling or table light per room, a fire on each hearth,
//     caged electric lamps in bunkers), never in rubble;
//   * fire particles (the wreck-flame recipe of tank_sim.cpp), Apply damage to map, lamp posts along routes, the
//     editor's Light section.
#include "lights.h"
#include "app.h"
#include "autopath.h"
#include <imgui.h>
#include <algorithm>
#include <map>
#include <cmath>

bool gAutoPathLamps = false;

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
  if (use == "interior") return glm::mix(1.0f, kInteriorDay, std::clamp(day, 0.0f, 1.0f));
  if (use == "fire") return glm::mix(1.0f, 0.8f, std::clamp(day, 0.0f, 1.0f));
  return 1.0f;   // electric (bunkers)
}
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
  return slot.get();
}

// ============================================================ which buildings are lit
bool lightsRuinedAsset(const AssetDef* a) {   // the shared exclusion list (V4 isRuinedAsset() replaces this once it lands)
  if (!a) return false;
  const std::string& id = a->id;
  return startsWith(id, "ruin_") || startsWith(id, "rub_") || id == "vil_church_ruined" || id == "vil_chateau_keep" || id == "aw_r612_unfinished" || id == "bch_lcvp_wrecked";
}
bool lightsOnlyHost(const AssetDef* a) {
  if (!a || !a->enterable || lightsRuinedAsset(a)) return false;
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
  uint64_t host = 0; int room = -1;
  bool gen = furnishRoom(i, host, room);
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
  for (auto& i : app.doc.instances) {
    auto it = app.rt.find(i.id);
    if (it == app.rt.end() || !it->second.asset) continue;
    const AssetDef* a = it->second.asset;
    const LightAssetDef* d = lightDef(a);
    if (!d || d->lights.empty()) continue;
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

void lightsPlanRooms(App& app, InteriorSys& sys, const Instance& host, const AssetDef* ha, const RoomInfo& ri, uint32_t seed, std::vector<LightPlanItem>& items) {
  if (!lightsLitBuilding(ha)) return;
  bool bunker = lightsOnlyHost(ha);
  quat hq = yawQuat(host.yaw), iq = glm::inverse(hq);
  const size_t planned = items.size();
  for (size_t k = 0; k < ri.rooms.size(); ++k) {
    const auto& R = ri.rooms[k];
    if (R.type == ROOM_RUIN) continue;
    std::string r = "r" + std::to_string(k) + "/";
    auto inRoom = [&](const LightPlanItem& it) { return it.slot.rfind(r, 0) == 0; };
    bool lit = false;
    for (size_t q = 0; q < planned; ++q) if (inRoom(items[q]) && lightDef(app.lib.find(items[q].asset))) lit = true;   // an oil lamp or candles on a table already
    // ---- ceiling light (by type and area)
    const char* ceilingAsset = nullptr; const char* tableAsset = nullptr;
    float pick = h01(seed, (uint32_t)k * 31u + 7u);
    if (bunker) ceilingAsset = "furn_lamp_caged";
    else switch (R.type) {
      case ROOM_KITCHEN: ceilingAsset = "furn_lamp_ceiling"; tableAsset = "furn_lamp"; break;
      case ROOM_SITTING: if (R.area >= 12.0f) ceilingAsset = "furn_lamp_ceiling"; tableAsset = pick < 0.6f ? "furn_lamp_table" : "furn_lamp"; break;
      case ROOM_BEDROOM: tableAsset = pick < 0.5f ? "furn_candle" : "furn_lamp"; break;
      case ROOM_UTILITY: if (R.area >= 6.0f) ceilingAsset = "lamp_hanging_lantern"; tableAsset = "furn_candle"; break;
      case ROOM_WORKSHOP: case ROOM_BARN: ceilingAsset = "lamp_hanging_lantern"; break;
      case ROOM_HALL: ceilingAsset = "furn_lamp_ceiling"; tableAsset = "furn_candle"; break;
      case ROOM_CHURCH: ceilingAsset = "lamp_hanging_lantern"; tableAsset = "furn_candelabra"; break;
      case ROOM_CLASS: case ROOM_OFFICE: case ROOM_TAVERN: ceilingAsset = "furn_lamp_ceiling"; tableAsset = "furn_lamp"; break;
      default: ceilingAsset = "furn_lamp_ceiling"; break;
    }
    // a table light where the room has a table / dresser and no lamp yet (the church: the candelabra on the altar)
    bool tablePlaced = false;
    if (tableAsset && (!lit || R.type == ROOM_CHURCH) && app.lib.find(tableAsset)) {
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
        const LightPlanItem& base = items[best];
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
    // the ceiling light; a room left without a lamp gets one hung from its ceiling as well
    bool wantCeiling = ceilingAsset && (R.type != ROOM_BEDROOM);
    if (!wantCeiling && !tablePlaced && !lit) ceilingAsset = "furn_lamp_ceiling", wantCeiling = true;
    if (R.type == ROOM_UTILITY && !ceilingAsset && !tablePlaced && !lit) ceilingAsset = "lamp_hanging_lantern", wantCeiling = true;
    if (wantCeiling && !app.lib.find(ceilingAsset)) wantCeiling = false;
    if (wantCeiling) {
      const AssetDef* ca = app.lib.find(ceilingAsset);
      const PropDef* cp = sys.prop(ca);
      float cyaw = (R.mx.x - R.mn.x) >= (R.mx.y - R.mn.y) ? 0.0f : 90.0f;
      Spot sp = ceilingSpot(app, host, ri, (int)k, cp, cyaw);
      float drop = cp ? -cp->bmin.y : 0.5f;   // hung from its origin (the top): parts reach down to bmin.y
      if (sp.ok && sp.ceil - drop < R.y0 + 2.05f && !bunker && std::string(ceilingAsset) != "furn_lamp_caged") {   // a low ceiling: a flush caged lamp instead
        if (const AssetDef* fl = app.lib.find("furn_lamp_caged")) { ca = fl; cp = sys.prop(fl); drop = cp ? -cp->bmin.y : 0.2f; }
      }
      if (sp.ok && sp.ceil - drop >= R.y0 + 1.75f) {
        items.push_back({r + "light", ca->id, vec3(sp.p.x, sp.ceil - 0.004f, sp.p.z), cyaw});
        lit = true;
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
        for (auto& p : placed) if (glm::length(p - c) < spacing) { near = true; break; }
        if (near) continue;
        for (float sd : {side, -side}) {
          vec2 w = c + nrm * sd * (L.width * 0.5f + 0.75f);
          if (!T.inBuildArea(w.x, w.y)) continue;
          float y = T.height(w.x, w.y);
          if (y < T.waterLevel + 0.15f) continue;
          if (std::fabs(T.height(w.x + 0.5f, w.y) - T.height(w.x - 0.5f, w.y)) > 0.45f || std::fabs(T.height(w.x, w.y + 0.5f) - T.height(w.x, w.y - 0.5f)) > 0.45f) continue;   // steep
          if (!app.pathSurfaceAt(vec3(w.x, y, w.y)).empty()) continue;   // never on another path
          vec2 face = -nrm * sd;   // the lantern arm reaches over the route
          float yaw = degrees(std::atan2(face.x, face.y));
          quat q = yawQuat(yaw);
          vec3 cen = vec3(w.x, y, w.y) + q * ((bmn + bmx) * 0.5f) + vec3(0, 0.15f, 0);
          if (Physics::boxBlocked(cen, glm::max((bmx - bmn) * 0.5f, vec3(0.3f, H * 0.5f - 0.2f, 0.3f)), q)) continue;   // a wall, a tree, a fence
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
    ImGui::TextDisabled("%d interior lights, %d lit (daylight %.2f: lamps at %.0f %% by day)", n, on, lightsDaylight(app), kInteriorDay * 100.0f);
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
