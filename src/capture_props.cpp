// --capture-props: no suite, close-up shots of props (PR1): each asset of NMK_PROPS (ids separated by ";"; an entry
// "id@x,y,z" aims at that asset-space point instead of the bounds' centre, e.g. one fuel dispenser of the petrol
// station) placed alone on a blank map by day, shot from 1.5 m and 4 m at standing eye height (1.62 m), 35 degrees off
// its +z face (NMK_PROPS_NEAR / NMK_PROPS_FAR: the two distances; NMK_PROPS_EYE: eye height above the piece; NMK_PROPS_NEAR_ONLY: no far shot; NMK_PROPS_GROUND: an asset laid under each piece), into selftest_out/prop_<k>_<n|f>.png; selftest_out/props.txt lists them (tools/props/sheet.py stitches
// the labelled contact sheet). The editor camera, no UI.
// NMK_PROPS_MAP=<template id> (PR5): load that shipped map instead; it furnishes on load as in a match. Three furnished
// buildings are picked (a house, an inn / cafe, the largest) and each one's biggest ground-floor room is shot from
// inside; selftest_out/map_counts.json counts the furniture drawn by an imported model against the in-house pieces.
#include "selftest.h"
#include "settings.h"
#include "interior.h"
#include <sstream>
#include <set>
#include <map>
#include <functional>
#include <fstream>

namespace {
struct Shot { std::string id; bool aim = false; vec3 at{0}; uint64_t inst = 0; float near = -1; };
bool modelBakedFor(const std::string& id) {   // a piece drawn by an imported model: furniture_models.json names one and its bake is on disk
  json f = furnitureModelFor(id); if (f.is_null()) return false;
  std::error_code ec; return fs::exists(contentDir() / "assets" / "models" / f.value("dir", id) / "mesh.bin", ec);
}
std::vector<Shot> gShots; int gK = 0; float gT = 0; bool gSetup = false;
fs::path out(const std::string& f) { std::error_code ec; fs::create_directories(exeDir() / "selftest_out", ec); return exeDir() / "selftest_out" / f; }
}

bool capturePropsSelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-props") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  auto finish = [&](int code) { if (!a.selftest->done) { a.selftest->exitCode = code; a.selftest->done = true; } return true; };
  if (!gSetup && std::getenv("NMK_PROPS_MAP")) {   // PR5: a shipped map's furnished interiors
    gSetup = true;
    std::string mid = std::getenv("NMK_PROPS_MAP");
    const TemplateInfo* t = a.findTemplate(mid); if (!t) { logf("[capture-map] no template %s", mid.c_str()); return finish(1); }
    a.newMap(*t, "Map capture");
    std::map<uint64_t, std::vector<const Instance*>> kids; int pieces = 0, byModel = 0, hand = 0, handModel = 0, user = 0;
    std::map<std::string, int> userIds;
    for (auto& i : a.doc.instances) {
      bool fp = isFurnishPiece(i);
      if (!fp && i.assetId.rfind("furn_", 0) != 0) continue;
      bool m = modelBakedFor(i.assetId);
      if (fp) { ++pieces; byModel += m; kids[i.extra["furnish"].value("host", (uint64_t)0)].push_back(&i); } else { ++hand; handModel += m; }
      json f = furnitureModelFor(i.assetId);
      if (!f.is_null() && f.value("src", std::string()).rfind("frsrc_u_", 0) == 0) { ++user; ++userIds[i.assetId]; }
    }
    json cj = {{"map", mid}, {"furnished_buildings", kids.size()}, {"generated_pieces", pieces}, {"generated_by_model", byModel}, {"hand_placed_furn", hand}, {"hand_placed_by_model", handModel}, {"user_model_pieces", user}, {"user_model_ids", userIds}};
    logf("[capture-map] %s: %zu furnished buildings, %d generated pieces (%d drawn by an imported model, %d in-house), %d hand-placed furn_* (%d by a model), %d of the user's models", mid.c_str(), kids.size(), pieces, byModel, pieces - byModel, hand, handModel, user);
    // three buildings: a house, an inn / cafe, the largest (most pieces)
    auto has = [](const std::string& id, std::initializer_list<const char*> ws) { for (auto w : ws) if (id.find(w) != std::string::npos) return true; return false; };
    std::vector<std::pair<uint64_t, std::string>> pick; std::set<uint64_t> used;
    std::set<std::string> usedTypes;
    auto choose = [&](std::function<bool(const std::string&)> ok, const char* why) {   // intact buildings first; a ruined town (Newtown) has only ruins
      for (int ruins = 0; ruins < 2; ++ruins) {
        uint64_t best = 0; size_t bn = 0;
        for (auto& [hid, v] : kids) { Instance* h = a.doc.find(hid); if (!h || used.count(hid) || usedTypes.count(h->assetId) || !ok(h->assetId) || (!ruins && has(h->assetId, {"ruin"}))) continue; if (v.size() > bn) { bn = v.size(); best = hid; } }
        if (best) { used.insert(best); usedTypes.insert(a.doc.find(best)->assetId); pick.push_back({best, why}); return; }
      }
    };
    choose([&](const std::string& id) { return has(id, {"farmhouse", "cottage", "townhouse", "terrace", "villa_", "manor", "longhouse", "notary", "house_", "_house"}) && !has(id, {"school", "store", "ware"}); }, "house");
    choose([&](const std::string& id) { return has(id, {"inn", "cafe", "tavern", "diner", "bistro", "restaurant", "bakery", "shop"}); }, "inn / cafe / shop");
    choose([&](const std::string&) { return true; }, "largest");
    for (auto& [hid, why] : pick) {
      Instance* h = a.doc.find(hid); const AssetDef* ha = a.assetFor(*h); const RoomInfo* ri = a.interior.rooms(ha);
      if (!ri || ri->rooms.empty()) continue;
      const RoomInfo::Room* best = nullptr;
      for (auto& R : ri->rooms) if (R.storey == 0 && (!best || R.area > best->area)) best = &R;
      if (!best) best = &ri->rooms[0];
      Shot s; s.id = ha->id + " (" + why + ")"; s.inst = hid; s.aim = true;
      s.at = vec3((best->mn.x + best->mx.x) * 0.5f, best->y0 + 1.0f, (best->mn.y + best->mx.y) * 0.5f);
      s.near = std::max(0.6f, 0.42f * std::min(best->mx.x - best->mn.x, best->mx.y - best->mn.y));   // inside the room
      gShots.push_back(s);
    }
    { std::ofstream f(out("map_counts.json")); f << cj.dump(1); }
    a.syncRuntime();
    Settings& S = settings(); S.s.hour = 14.0f; S.apply(a);
    if (a.mode == Mode::Explore) a.exitExplore();
    a.hideUi = true; gT = 0;
    std::ofstream f(out("props.txt")); for (auto& s : gShots) f << s.id << "\n";
    return true;
  }
  if (!gSetup) {
    gSetup = true;
    a.newMap(*a.findTemplate("blank_slate"), "Prop capture");
    std::stringstream ss(std::getenv("NMK_PROPS") ? std::getenv("NMK_PROPS") : "mod_trash_bin");
    std::string tok; int n = 0;
    while (std::getline(ss, tok, ';')) {
      if (tok.empty()) continue;
      Shot s; size_t p = tok.find('@'); s.id = tok.substr(0, p);
      if (p != std::string::npos) { s.aim = true; std::sscanf(tok.c_str() + p + 1, "%f,%f,%f", &s.at.x, &s.at.y, &s.at.z); }
      const AssetDef* as = a.lib.find(s.id);
      if (!as) { logf("[capture-props] no asset %s", s.id.c_str()); continue; }
      // rows across the blank map, each piece its own footprint + 8 m apart (props ~ 20 m: up to ~400 in one run; PR2: whole buildings too)
      static float cx = -190.0f, cz = -190.0f, rowD = 0.0f;
      float w = std::max(12.0f, std::max(as->bmax.x - as->bmin.x, as->bmax.z - as->bmin.z) + 8.0f);
      if (cx + w > 190.0f) { cx = -190.0f; cz += rowD; rowD = 0.0f; }
      float x = cx + w * 0.5f, z = cz + w * 0.5f; cx += w; rowD = std::max(rowD, w); ++n;
      s.inst = a.placeInstance(as, vec3(x, a.terrain.height(x, z), z), 0);
      if (!s.inst && as->raw.is_object() && as->raw.contains("prop")) {   // PR2: a wall / ceiling piece with nothing to hang on: put it in the air at its height
        const json& pj = as->raw["prop"];
        Instance i; i.id = a.doc.newId(); i.assetId = as->id; i.assetVersion = as->version;
        i.pos = vec3(x, a.terrain.height(x, z) + (pj.value("hang", false) ? 2.6f : pj.value("mount", 1.2f)), z);
        a.doc.instances.push_back(i); s.inst = i.id;
      }
      if (!s.inst) { logf("[capture-props] %s could not be placed", s.id.c_str()); continue; }
      if (const char* gid = std::getenv("NMK_PROPS_GROUND")) if (const AssetDef* ga = a.lib.find(gid)) {   // PR4: a hard pad under the piece (scale reads on concrete / dirt, not in long grass)
        Instance g; g.id = a.doc.newId(); g.assetId = ga->id; g.assetVersion = ga->version; g.pos = vec3(x, a.terrain.height(x, z) - 0.01f, z);
        a.doc.instances.push_back(g);
      }
      gShots.push_back(s);
    }
    a.syncRuntime();
    Settings& S = settings(); S.s.hour = 14.0f; S.apply(a);
    if (a.mode == Mode::Explore) a.exitExplore();
    a.hideUi = true; gT = 0;
    std::ofstream f(out("props.txt")); for (auto& s : gShots) f << s.id << "\n";
    return true;
  }
  static const bool nearOnly = std::getenv("NMK_PROPS_NEAR_ONLY") != nullptr;   // PR2 audit: the near shot alone (half the GPU time)
  if (nearOnly && gK % 2) ++gK;
  if (gK >= (int)gShots.size() * 2) { a.hideUi = false; logf("[capture-props] %d shots", nearOnly ? gK / 2 : gK); return finish(0); }
  Shot& s = gShots[gK / 2]; bool far = gK % 2;
  Instance* inst = a.doc.find(s.inst); auto rt = a.rt.find(s.inst);
  if (inst && rt != a.rt.end() && rt->second.asset) {
    const AssetDef& as = *rt->second.asset;
    vec3 c = s.aim ? s.at : (as.bmin + as.bmax) * 0.5f;
    vec3 tgt = inst->pos + yawQuat(inst->yaw) * c;
    static const float nearD = std::getenv("NMK_PROPS_NEAR") ? (float)std::atof(std::getenv("NMK_PROPS_NEAR")) : 1.5f;   // PR2 audit: 1 m
    static const float farD = std::getenv("NMK_PROPS_FAR") ? (float)std::atof(std::getenv("NMK_PROPS_FAR")) : 4.0f;   // PR4: large clusters need ~10 m
    float d = s.near > 0 ? s.near : far ? farD : nearD, ang = glm::radians(35.0f);
    vec3 dir = yawQuat(inst->yaw) * vec3(std::sin(ang), 0, std::cos(ang));
    vec3 eye = tgt + dir * (d + (s.aim ? 0.0f : 0.5f * std::max(as.bmax.x - as.bmin.x, as.bmax.z - as.bmin.z) * 0.5f));
    static const char* eyeRel = std::getenv("NMK_PROPS_EYE");   // PR4: small pieces: the eye this far above the piece's middle (default standing height above the ground)
    eye.y = eyeRel ? tgt.y + (float)std::atof(eyeRel) : inst->pos.y + 1.62f;
    vec3 v = tgt - eye;
    a.camPos = eye; a.camYaw = glm::degrees(std::atan2(-v.x, -v.z)); a.camPitch = glm::degrees(std::atan2(v.y, glm::length(vec2(v.x, v.z))));
  }
  gT += dt;
  if (gT > 1.2f && gT < 1.2f + dt * 1.5f) a.uiShot = out("prop_" + std::to_string(gK / 2) + (far ? "_f" : "_n") + ".png");
  if (gT > 1.5f && a.uiShot.empty()) { ++gK; gT = 0; }   // PR2: wait while the shot is still pending (window minimised)
  return true;
}
