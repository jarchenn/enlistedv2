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
struct Shot { std::string id; bool aim = false; vec3 at{0}; uint64_t inst = 0; float near = -1; bool fixed = false; vec3 eye{0}, look{0}; };
void turnedBounds(vec3 bmin, vec3 bmax, float yaw, vec3& mn, vec3& mx) {   // the box round a piece's bounds turned by yaw
  quat q = yawQuat(yaw); mn = vec3(1e9f); mx = vec3(-1e9f);
  for (int k = 0; k < 8; ++k) { vec3 p = q * vec3(k & 1 ? bmax.x : bmin.x, k & 2 ? bmax.y : bmin.y, k & 4 ? bmax.z : bmin.z); mn = glm::min(mn, p); mx = glm::max(mx, p); }
}
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
    std::map<std::string, int> userIds, inHouse;   // inHouse: generated pieces still drawn in-house, by id (what to model next)
    for (auto& i : a.doc.instances) {
      bool fp = isFurnishPiece(i);
      if (!fp && i.assetId.rfind("furn_", 0) != 0) continue;
      bool m = modelBakedFor(i.assetId);
      if (fp) { ++pieces; byModel += m; kids[i.extra["furnish"].value("host", (uint64_t)0)].push_back(&i); if (!m) ++inHouse[i.assetId]; } else { ++hand; handModel += m; }
      json f = furnitureModelFor(i.assetId);
      if (!f.is_null() && f.value("src", std::string()).rfind("frsrc_u_", 0) == 0) { ++user; ++userIds[i.assetId]; }
    }
    json cj = {{"map", mid}, {"furnished_buildings", kids.size()}, {"generated_pieces", pieces}, {"generated_by_model", byModel}, {"hand_placed_furn", hand}, {"hand_placed_by_model", handModel}, {"user_model_pieces", user}, {"user_model_ids", userIds}, {"in_house_ids", inHouse}};
    logf("[capture-map] %s: %zu furnished buildings, %d generated pieces (%d drawn by an imported model, %d in-house), %d hand-placed furn_* (%d by a model), %d of the user's models", mid.c_str(), kids.size(), pieces, byModel, pieces - byModel, hand, handModel, user);
    a.syncRuntime();
    // three rooms (PR5 follow-up): a bedroom with one of the user's beds, a sitting room with a sofa / armchair / piano,
    // one more room with a replaced piece; intact buildings first, each room from a different building where possible
    auto has = [](const std::string& id, std::initializer_list<const char*> ws) { for (auto w : ws) if (id.find(w) != std::string::npos) return true; return false; };
    auto isUser = [&](const std::string& id) { json f = furnitureModelFor(id); return !f.is_null() && f.value("src", std::string()).rfind("frsrc_u_", 0) == 0; };
    std::map<std::pair<uint64_t, int>, std::vector<const Instance*>> rooms;
    for (auto& [hid, v] : kids) for (const Instance* p : v) {
      std::string sl = p->extra["furnish"].value("slot", std::string());
      if (sl.size() < 3 || sl[0] != 'r') continue;
      rooms[{hid, std::atoi(sl.c_str() + 1)}].push_back(p);
    }
    std::set<uint64_t> usedHost;
    std::pair<uint64_t, int> curKey{0, 0};   // the room being scored (for type filters)
    auto pickRoom = [&](std::function<int(const std::vector<const Instance*>&)> score, const char* why, std::function<bool(const std::string&)> focus) {
      std::pair<uint64_t, int> best{0, 0}; int bs = 0;
      for (int pass = 0; pass < 2 && !bs; ++pass)
        for (auto& [key, v] : rooms) {
          Instance* h = a.doc.find(key.first); if (!h || (!pass && (usedHost.count(key.first) || has(h->assetId, {"ruin"})))) continue;
          curKey = key; int sc = score(v); if (sc > bs) { bs = sc; best = key; }
        }
      if (!bs) return;
      usedHost.insert(best.first);
      Instance* h = a.doc.find(best.first); const AssetDef* ha = a.assetFor(*h); const RoomInfo* ri = a.interior.rooms(ha);
      if (!ri || best.second >= (int)ri->rooms.size()) return;
      const auto& R = ri->rooms[best.second]; const auto& S = ri->storeys[R.storey];
      quat hq = yawQuat(h->yaw), iq = glm::inverse(hq);
      // the subject: the piece the room was picked for (a user bed, the seating / piano), else the modelled pieces' centre
      std::vector<vec3> pts, mod; std::vector<std::pair<vec3, vec3>> boxes;
      for (const Instance* p : rooms[best]) {
        vec3 l = iq * (p->pos - h->pos);
        if (focus(p->assetId)) pts.push_back(l); if (modelBakedFor(p->assetId)) mod.push_back(l);
        const AssetDef* pa = a.assetFor(*p); const PropDef* pd = pa ? a.interior.prop(pa) : nullptr;
        if (pd && !pd->flat()) { vec3 mn, mx; turnedBounds(pd->bmin, pd->bmax, wrapDeg(p->yaw - h->yaw), mn, mx); boxes.push_back({mn + l, mx + l}); }
      }
      if (pts.empty()) pts = mod; if (pts.empty()) return;
      vec3 c(0); for (auto& q : pts) c += q; c /= (float)pts.size();
      // a standing point on the room's own floor: 0.5 m clear of its walls and 0.6 m from any piece, as far from the
      // furniture as the room allows (looking across it, never into a wall)
      const int rr = (int)std::ceil(0.5f / ri->cell);
      vec2 cam(0); float bd = -1e9f;
      for (int j0 = 0; j0 < S.nz; ++j0) for (int i0 = 0; i0 < S.nx; ++i0) {
        if (S.id[(size_t)j0 * S.nx + i0] != best.second) continue;
        bool clear = true;
        for (int dj = -rr; dj <= rr && clear; ++dj) for (int di = -rr; di <= rr && clear; ++di) {
          int i = i0 + di, j = j0 + dj; if (i < 0 || j < 0 || i >= S.nx || j >= S.nz || S.id[(size_t)j * S.nx + i] != best.second) clear = false;
        }
        if (!clear) continue;
        vec2 q(S.o.x + (i0 + 0.5f) * ri->cell, S.o.y + (j0 + 0.5f) * ri->cell);
        for (auto& b : boxes) if (q.x > b.first.x - 0.35f && q.x < b.second.x + 0.35f && q.y > b.first.z - 0.35f && q.y < b.second.z + 0.35f) clear = false;   // clear of every piece's footprint
        if (!clear) continue;
        float d = glm::length(q - vec2(c.x, c.z));
        if (d < 1.4f) continue;
        // a clear line of sight from standing height to the subject (nothing tall in between), about 3 m away
        vec3 e = h->pos + hq * vec3(q.x, R.y0 + 1.6f, q.y), t = h->pos + hq * vec3(c.x, R.y0 + 0.7f, c.z);
        Physics::Hit ph; float L = glm::length(t - e);
        bool seen = !Physics::castRayEx(e, (t - e) / L, L - 0.6f, ph);
        float sc = (seen ? 10.0f : 0.0f) - std::fabs(d - 3.0f);
        if (sc > bd) { bd = sc; cam = q; }
      }
      if (bd < -1e8f) return;
      Shot s; s.id = ha->id + " " + roomTypeKey(R.type) + " (" + why + ")"; s.inst = best.first; s.fixed = true;
      s.eye = h->pos + hq * vec3(cam.x, R.y0 + 1.6f, cam.y); s.look = h->pos + hq * vec3(c.x, R.y0 + 0.65f, c.z);
      gShots.push_back(s);
    };
    auto count = [&](const std::vector<const Instance*>& v, std::function<bool(const std::string&)> f) { int n = 0; for (auto* p : v) n += f(p->assetId); return n; };
    auto typeOf = [&](std::pair<uint64_t, int> k) -> std::string {
      Instance* h = a.doc.find(k.first); const AssetDef* ha = h ? a.assetFor(*h) : nullptr; const RoomInfo* ri = ha ? a.interior.rooms(ha) : nullptr;
      return ri && k.second < (int)ri->rooms.size() ? roomTypeKey(ri->rooms[k.second].type) : "";
    };
    // NMK_PROPS_ROOMS="tavern|dining,kitchen,shop": one room per group, by room type, the most furnished one; the camera
    // looks at the centre of its furniture (lamps, rugs and curtains left out)
    if (const char* rs = std::getenv("NMK_PROPS_ROOMS")) {
      std::stringstream ss(rs); std::string grp;
      auto big = [&](const std::string& id) { return !has(id, {"lamp", "rug", "curtain", "light", "sconce", "pendant", "candle", "debris", "frame_", "picture"}); };
      while (std::getline(ss, grp, ',')) {
        std::vector<std::string> alts; std::stringstream gs(grp); std::string w; while (std::getline(gs, w, '|')) alts.push_back(w);
        pickRoom([&](const std::vector<const Instance*>& v) {
          std::string t = typeOf(curKey); bool ok = false; for (auto& al : alts) ok |= t.rfind(al, 0) == 0;
          if (Instance* hh = a.doc.find(curKey.first); hh && has(hh->assetId, {"boathouse"})) ok = false;   // its room is mostly open water
          return ok ? count(v, big) : 0; }, grp.c_str(), big);
      }
    } else {
    pickRoom([&](const std::vector<const Instance*>& v) {   // a bedroom with one of the user's beds (else a modelled bed)
      int ub = count(v, [&](const std::string& id) { return has(id, {"_bed", "bunk"}) && isUser(id); }), mb = count(v, [&](const std::string& id) { return has(id, {"_bed", "bunk"}) && modelBakedFor(id); });
      return ub * 100 + mb * 10 + (ub + mb ? count(v, modelBakedFor) : 0); }, "user bed", [&](const std::string& id) { return has(id, {"_bed", "bunk"}) && modelBakedFor(id); });
    pickRoom([&](const std::vector<const Instance*>& v) {   // a sitting room with a sofa / armchair / piano / TV by a model
      int u = count(v, [&](const std::string& id) { return has(id, {"sofa", "armchair", "chair_", "piano", "settee", "chesterfield", "_tv", "synth"}) && isUser(id); });
      int m = count(v, [&](const std::string& id) { return has(id, {"sofa", "armchair", "piano", "settee", "chesterfield", "_tv"}) && modelBakedFor(id); });
      return u * 100 + m * 10 + (u + m ? count(v, modelBakedFor) : 0); }, "seating, piano", [&](const std::string& id) { return has(id, {"sofa", "armchair", "chair_", "piano", "settee", "chesterfield", "_tv", "synth"}) && modelBakedFor(id); });
    pickRoom([&](const std::vector<const Instance*>& v) { return count(v, modelBakedFor); }, "most modelled pieces", [&](const std::string& id) { return modelBakedFor(id); });
    }
    a.hideEditorOverlays = true;   // no gizmos, markers or nav lines in the shots
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
      Shot s; size_t p = tok.find('@'), hsh = tok.find('#'); s.id = tok.substr(0, std::min(p, hsh));
      if (hsh != std::string::npos) { s.fixed = std::sscanf(tok.c_str() + hsh + 1, "%f,%f,%f>%f,%f,%f", &s.eye.x, &s.eye.y, &s.eye.z, &s.look.x, &s.look.y, &s.look.z) == 6; }   // PR10: "id#eye>look" in asset space (a shot from inside)
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
      if (Instance* hi = a.doc.find(s.inst)) { if (s.fixed) { s.eye += hi->pos; s.look += hi->pos; } a.placeCompanions(s.inst); }   // PR10: a building's authored furniture
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
  if (s.fixed) {   // PR5: a camera placed inside a room, looking at its furniture
    vec3 v = s.look - s.eye;
    a.camPos = s.eye; a.camYaw = glm::degrees(std::atan2(-v.x, -v.z)); a.camPitch = glm::degrees(std::atan2(v.y, glm::length(vec2(v.x, v.z))));
  } else if (inst && rt != a.rt.end() && rt->second.asset) {
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
