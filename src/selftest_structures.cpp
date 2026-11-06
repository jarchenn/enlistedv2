// --selftest-structures (W4, SPEC 3.6): the new Atlantic Wall / beach / bocage / village catalogue assets.
//  * every catalogue asset carries "nav" metadata (the bunkers' is W2's: reported as pending until it lands);
//  * each W4 asset (an asset with a "tour") validates its nav block (effect, rooms <-> doors / entrances, area
//    outlines, terrain cuts) and its colliders (static shape, a free standing spot in every room, clear sight from
//    embrasure / MG / firestep firing points, cover points with something solid in front of them);
//  * each is placed on a Blank Slate at a rotation, its terrain cut applied (a stand-in for W3's terrain code),
//    companions / a joining trench piece placed, framed for a screenshot, then walked through its tour with the
//    real character controller (same step vocabulary as "walkthrough": goto / interact / look / shot);
//  * "climb" probes: the character is driven at a face with jumps and vault input and must stay below it (AT ditch).
#include "selftest.h"
#include "physics.h"

namespace {
std::vector<std::string> sIds;
size_t sIdx = 0;
uint64_t sJoin = 0;
size_t sClimb = 0;
float sFloor = 0;

bool inPoly(vec2 p, const std::vector<vec2>& poly) {
  bool in = false;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
    if (((poly[i].y > p.y) != (poly[j].y > p.y)) && p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) in = !in;
  return in;
}
float edgeDist(vec2 p, const std::vector<vec2>& poly) {
  float d = 1e9f;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    vec2 a = poly[j], b = poly[i], ab = b - a;
    float t = std::clamp(glm::dot(p - a, ab) / std::max(1e-6f, glm::dot(ab, ab)), 0.0f, 1.0f);
    d = std::min(d, glm::length(p - (a + ab * t)));
  }
  return d;
}
// Lowers the ground inside each cut outline to (instance y - depth), sloping over `bank` inside the edge. The real
// application is W3's terrain work; this stand-in only lets the suite walk below-grade assets.
struct CutProbe { vec3 p; float want; };
std::vector<CutProbe> applyCuts(App& app, const Instance& in, const AssetDef* a) {
  std::vector<CutProbe> probes;
  Terrain& T = app.terrain;
  quat q = yawQuat(in.yaw);
  float ts = T.tileSize(), fs = T.fineSpacing();
  for (auto& c : a->navMeta.terrainCuts) {
    std::vector<vec2> w; vec2 lo(1e9f), hi(-1e9f);
    for (auto& p : c.outline) { vec3 v = in.pos + q * vec3(p.x, 0, p.y); w.push_back(vec2(v.x, v.z)); lo = glm::min(lo, w.back()); hi = glm::max(hi, w.back()); }
    int tx0 = (int)std::floor((lo.x + T.size * 0.5f) / ts), tx1 = (int)std::floor((hi.x + T.size * 0.5f) / ts);
    int tz0 = (int)std::floor((lo.y + T.size * 0.5f) / ts), tz1 = (int)std::floor((hi.y + T.size * 0.5f) / ts);
    CutProbe best{vec3(0), 0}; float bestD = -1;
    for (int tz = tz0; tz <= tz1; ++tz)
      for (int tx = tx0; tx <= tx1; ++tx) {
        Terrain::Tile* t = T.activate(tx, tz);
        if (!t) continue;
        vec2 o = T.tileOrigin(tx, tz);
        for (int j = 0; j < Terrain::kFineN; ++j)
          for (int i = 0; i < Terrain::kFineN; ++i) {
            vec2 p(o.x + i * fs, o.y + j * fs);
            if (!inPoly(p, w)) continue;
            float d = edgeDist(p, w), k = c.bank > 1e-3f ? std::min(1.0f, d / c.bank) : 1.0f;
            float& h = t->h[j * Terrain::kFineN + i];
            h = std::min(h, in.pos.y - c.depth * k);
            if (d > bestD) { bestD = d; best = {vec3(p.x, 0, p.y), in.pos.y - c.depth}; }
          }
      }
    if (bestD > fs) probes.push_back(best);
  }
  return probes;
}
bool w2Owned(const std::string& id) { return id.rfind("bunker_", 0) == 0 || id == "gun_mg42_embrasure"; }
const char* effName(NavEffect e) { static const char* n[] = {"auto", "block", "carve", "area", "none"}; return n[(int)e]; }
float yawToCam(vec3 from, vec3 to) { vec3 d = to - from; return degrees(std::atan2(-d.x, -d.z)); }
vec3 facing(float yawDeg) { return vec3(std::sin(radians(yawDeg)), 0, std::cos(radians(yawDeg))); }   // asset yaw convention
}

void SelfTest::structuresUpdate(App& app, float dt) {
  auto go = [&](int s) { stage = s; stageTime = 0; };
  switch (stage) {
  case 3800: {
    reportName = "structures";
    const TemplateInfo* t = app.findTemplate("blank_slate");
    if (!t) { check(false, "Blank Slate template"); finish(app); return; }
    app.newMap(*t, "Structures (W4)");
    // ---- catalogue-wide: nav metadata on every asset
    int authored = 0, pending = 0;
    for (auto& up : app.lib.assets) {
      const AssetDef* a = up.get();
      if (a->navMeta.authored) {
        ++authored;
        check(true, a->id + ": nav " + effName(a->navMeta.effect) + (a->navMeta.area != NavArea::Default ? std::string(" / ") + navAreaName(a->navMeta.area) : "") +
                    (a->navMeta.hasAnnotations() ? " + annotations" : "") + (a->navMeta.terrainCuts.empty() ? "" : " + terrain cut"));
      } else if (w2Owned(a->id)) { ++pending; check(true, a->id + ": nav pending (bunker metadata is W2's)"); }
      else check(false, a->id + ": has no nav metadata");
      if (a->raw.contains("tour")) sIds.push_back(a->id);
    }
    summary["catalogue"] = {{"assets", app.lib.assets.size()}, {"navAuthored", authored}, {"navPendingW2", pending}, {"w4Assets", sIds.size()}};
    check(sIds.size() >= 30, "W4 structure assets found: " + std::to_string(sIds.size()));
    // ---- per W4 asset: nav block and collider sanity (asset space)
    for (auto& id : sIds) {
      const AssetDef* a = app.lib.find(id);
      const NavMeta& m = a->navMeta;
      const json& tour = a->raw["tour"];
      bool ok = m.effect != NavEffect::Auto;
      check(ok, id + ": explicit nav effect (" + effName(m.effect) + ")");
      if (m.effect == NavEffect::Area) check(m.area != NavArea::Default || !m.areas.empty(), id + ": area effect names an area type");
      std::set<std::string> rooms;
      bool roomsOk = true;
      for (auto& r : m.rooms) { rooms.insert(r.id); roomsOk &= r.max.x > r.min.x + 0.5f && r.max.y > r.min.y + 1.0f && r.max.z > r.min.z + 0.5f; }
      if (a->enterable) check(!m.rooms.empty() && !m.entrances.empty() && !m.doors.empty() && !m.corners.empty(), id + ": enterable: rooms, doors, entrances and corners annotated");
      bool linksOk = true;
      for (auto& d : m.doors) linksOk &= rooms.count(d.a) && (d.b == "outside" || rooms.count(d.b));
      for (auto& e : m.entrances) linksOk &= rooms.count(e.room) > 0;
      for (auto& c : m.corners) linksOk &= rooms.count(c.room) > 0;
      if (!m.rooms.empty()) check(roomsOk && linksOk, id + ": " + std::to_string(m.rooms.size()) + " rooms, doors / entrances / corners reference them");
      bool polyOk = true; for (auto& p : m.areas) polyOk &= p.outline.size() >= 3; for (auto& c : m.terrainCuts) polyOk &= c.outline.size() >= 3 && c.depth > 0.1f;
      check(polyOk, id + ": area outlines and terrain cuts well formed (" + std::to_string(m.areas.size()) + " areas, " + std::to_string(m.terrainCuts.size()) + " cuts)");
      if (tour.value("cut", false)) check(!m.terrainCuts.empty(), id + ": below grade: carries a terrainCut");
      vec3 ext = a->bmax - a->bmin;
      bool bOk = std::isfinite(ext.x) && std::isfinite(ext.y) && std::isfinite(ext.z) && ext.x > 0.1f && ext.y > 0.05f && ext.z > 0.1f && std::max({ext.x, ext.y, ext.z}) < 60.0f;
      int solidParts = 0; for (auto& p : a->raw.value("parts", json::array())) solidParts += p.value("k", 1) != 0;
      bool shapeOk = a->shape != nullptr || a->tree.stumpShape != nullptr || a->tree.trunkShape != nullptr || (solidParts == 0 && (m.effect == NavEffect::Area || m.effect == NavEffect::None));
      check(shapeOk && bOk, id + ": " + (solidParts ? "static collision shape" : "no colliders by design (walk-through area)") + ", bounds " + std::to_string(ext.x).substr(0, 5) + " x " + std::to_string(ext.y).substr(0, 5) + " x " + std::to_string(ext.z).substr(0, 5) + " m");
    }
    sIdx = 0;
    go(3801);
    break;
  }
  case 3801: {   // place the next asset (plus join / companions / cut) and run its in-world collider checks
    if (sIdx >= sIds.size()) { finish(app); return; }
    const AssetDef* a = app.lib.find(sIds[sIdx]);
    const json& tour = a->raw["tour"];
    size_t k = sIdx;
    float yaw = wrapDeg(23.0f + 67.0f * (float)k);
    vec3 pos(-260.0f + 52.0f * (float)(k % 11), 0, -240.0f + 64.0f * (float)(k / 11));
    houseId = app.placeInstance(a, pos, yaw);
    check(houseId != 0, a->id + ": placed at yaw " + std::to_string((int)yaw));
    if (!houseId) { ++sIdx; break; }
    curAsset = a->id; pass = 0; sJoin = 0;
    Instance in = *app.doc.find(houseId);
    std::vector<std::pair<Instance, const AssetDef*>> cutList{{in, a}};
    if (tour.contains("join")) {
      const json& jn = tour["join"];
      const AssetDef* ja = app.lib.find(jn.value("asset", ""));
      vec3 jp = toWorld(app, vec3(jn["pos"][0].get<float>(), 0, jn["pos"][1].get<float>()));
      sJoin = ja ? app.placeInstance(ja, jp, worldYaw(app, jn.value("yaw", 0.0f))) : 0;
      check(sJoin != 0, a->id + ": joining " + jn.value("asset", "") + " placed end to end");
      if (sJoin) { app.doc.find(sJoin)->pos.y = in.pos.y; app.bump(*app.doc.find(sJoin)); cutList.push_back({*app.doc.find(sJoin), ja}); }
    }
    if (tour.value("companions", false)) { int n = app.placeCompanions(houseId); check(n > 0, a->id + ": " + std::to_string(n) + " companion(s) placed"); }
    if (tour.value("cut", false)) {
      std::vector<CutProbe> probes;
      for (auto& [ci, ca] : cutList) { auto p = applyCuts(app, ci, ca); probes.insert(probes.end(), p.begin(), p.end()); }
      app.doc.terrainDeltas = app.terrain.deltas();
      app.resetGround();
      bool ok = !probes.empty();
      float worst = 0;
      for (auto& p : probes) { float e = std::fabs(app.terrain.height(p.p.x, p.p.z) - p.want); worst = std::max(worst, e); ok &= e < 0.12f; }
      check(ok, a->id + ": terrain cut applied (" + std::to_string(probes.size()) + " cuts, worst depth error " + std::to_string(worst).substr(0, 4) + " m)");
    }
    app.syncRuntime();
    // rooms: at least one spot where a standing man fits
    quat q = yawQuat(in.yaw);
    for (auto& r : a->navMeta.rooms) {
      bool free = false;
      for (float x = r.min.x + 0.35f; x <= r.max.x - 0.35f && !free; x += 0.25f)
        for (float z = r.min.z + 0.35f; z <= r.max.z - 0.35f && !free; z += 0.25f)
          free = !Physics::capsuleBlocked(in.pos + q * vec3(x, r.min.y + 0.06f, z), std::min(1.75f, r.max.y - r.min.y - 0.05f), 0.28f);
      check(free, a->id + ": room '" + r.id + "' has free standing room");
    }
    // firing points: a clear line of sight 4 m out at eye height (embrasure / mg / firestep / unglazed openings;
    // glazed windows have casements, mortar positions fire indirect)
    const json& rawFp = a->raw["nav"].value("firingPoints", json::array());
    for (size_t f = 0; f < a->navMeta.firingPoints.size(); ++f) {
      auto& fp = a->navMeta.firingPoints[f];
      bool unglazed = f < rawFp.size() && rawFp[f].value("unglazed", false);
      if ((fp.kind == "window" && !unglazed) || (f < rawFp.size() && rawFp[f].value("indirect", false))) continue;
      float eye = fp.kind == "mg" || fp.kind == "embrasure" || fp.kind == "firestep" ? 1.6f : 1.5f;
      vec3 o = in.pos + q * (fp.p + vec3(0, eye, 0)), d = q * facing(fp.yaw);
      Physics::Hit h;
      bool hit = Physics::castRayEx(o, d, 4.0f, h);
      check(!hit, a->id + ": " + fp.kind + " firing point " + std::to_string(f) + " sees out" + (hit ? " (blocked at " + std::to_string(h.t).substr(0, 4) + " m)" : ""));
    }
    // cover points: standing ground under them and something solid toward the threat within 2.5 m
    for (size_t c = 0; c < a->navMeta.cover.size(); ++c) {
      auto& cv = a->navMeta.cover[c];
      vec3 p = in.pos + q * cv.p, d = q * facing(cv.yaw);
      Physics::Hit g, h;
      bool ground = Physics::castRayEx(p + vec3(0, 1.0f, 0), vec3(0, -1, 0), 2.2f, g);
      bool solid = Physics::castRayEx(p + vec3(0, cv.high ? 1.45f : 0.75f, 0), d, 2.5f, h);
      check(ground && solid, a->id + ": cover point " + std::to_string(c) + (cv.high ? " (high)" : " (low)") + (ground ? "" : " has no ground") + (solid ? "" : " has nothing in front"));
    }
    // frame the asset for its screenshot
    float R = std::max({a->footprint.x, a->footprint.y, a->bmax.y - a->bmin.y, 4.0f});
    vec3 ctr = in.pos + q * ((a->bmin + a->bmax) * 0.5f);
    app.camPos = in.pos + q * vec3(R * 0.7f, 0, R * 0.95f) + vec3(0, R * 0.45f + 2.0f, 0);
    app.camYaw = yawToCam(app.camPos, ctr);
    app.camPitch = degrees(std::atan2(ctr.y - app.camPos.y, glm::length(vec2(ctr.x - app.camPos.x, ctr.z - app.camPos.z))));
    app.selected = 0;
    summary["assets"][a->id] = {{"name", a->name}, {"category", a->category}, {"screenshot", "selftest_out/mpu_" + a->id + ".png"}, {"enterable", a->enterable},
                                {"tourSteps", tour.value("steps", json::array()).size()}, {"cut", tour.value("cut", false)}};
    go(3802);
    break;
  }
  case 3802: {
    if (stageTime > 0.6f && stageTime < 0.6f + dt * 1.5f) shotQueue.push_back("mpu_" + curAsset);
    if (stageTime < 0.9f) break;
    const AssetDef* a = app.lib.find(curAsset);
    const json& tour = a->raw["tour"];
    if (!tour.contains("steps") || tour["steps"].empty()) { ++sIdx; go(3801); break; }
    json st = tour["start"];
    app.doc.start = {true, toWorld(app, jvec3(st["pos"])), worldYaw(app, st.value("yaw", 0.0f))};
    app.doc.start.pos.y = app.terrain.height(app.doc.start.pos.x, app.doc.start.pos.z);
    check(app.enterExplore(), curAsset + ": F5 enters Explore at the tour start"); app.useScripted = true;
    steps.clear(); for (auto& s : tour["steps"]) steps.push_back(s);
    stepIdx = 0; stepTime = 0; ++pass;
    go(3803);
    break;
  }
  case 3803:
    if (runWalkthrough(app, dt)) {
      const json& tour = app.lib.find(curAsset)->raw["tour"];
      if (tour.contains("climb")) { sClimb = 0; go(3804); break; }
      app.exitExplore(); ++sIdx; go(3801);
    }
    break;
  case 3804: {   // climb probes: from a floor point, drive at the face with sprint, jumps and vault input
    const json& cl = app.lib.find(curAsset)->raw["tour"]["climb"];
    if (sClimb >= cl.size()) { app.exitExplore(); ++sIdx; go(3801); break; }
    const json& c = cl[sClimb];
    Instance* in = app.doc.find(houseId);
    sFloor = c.value("floorY", 0.0f);
    vec3 tgt = toWorld(app, vec3(c["to"][0].get<float>(), 0, c["to"][1].get<float>()));
    if (stageTime <= dt * 1.5f) Physics::setCharacterPos(toWorld(app, vec3(c["from"][0].get<float>(), 0, c["from"][1].get<float>())) + vec3(0, sFloor + 0.05f, 0));
    auto ch = Physics::character();
    app.scriptedInput = ExploreInput{};
    app.eyeYaw = yawToCam(ch.pos, tgt); app.eyePitch = 0;
    app.scriptedInput.move = vec2(0, 1); app.scriptedInput.sprint = true;
    int fr = (int)(stageTime * 60.0f);
    app.scriptedInput.jump = fr % 40 < 2; app.scriptedInput.jumpHeld = fr % 40 < 20;
    if (stageTime > 6.0f) {
      float ly = ch.pos.y - in->pos.y;
      char b[200]; std::snprintf(b, sizeof b, "%s: %s: still in the cut after 6 s of sprint / jump / vault at the face (feet %.2f m, face top 0.0 m)", curAsset.c_str(), c.value("note", std::string("face")).c_str(), ly);
      check(ly < sFloor + 1.4f, b);
      ++sClimb; stageTime = 0;
    }
    break;
  }
  }
}
