// Phase 2 tanks: definitions, Explore simulation (driving, turret, gunner sight, hatch MG, HE/AP loading),
// damage rules, wrecks, effects, sound and persistence. docs/tanks.md
#include "app.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <stb_image_write.h>
#include <algorithm>

bool isVehicleAsset(const AssetDef* a) { return a && a->vehicle; }
const char* tankZoneName(int z) { static const char* n[] = {"front", "side", "rear", "roof", "underside"}; return z >= 0 && z < 5 ? n[z] : "?"; }

namespace {
float approach(float v, float t, float step) { float d = t - v; return std::fabs(d) <= step ? t : v + (d > 0 ? step : -step); }
float wrap180(float d) { d = std::fmod(d + 180.0f, 360.0f); if (d < 0) d += 360.0f; return d - 180.0f; }
TankBox jbox(const json& j) { TankBox b; b.c = jvec3(j.value("c", json()), vec3(0)); b.h = jvec3(j.value("h", json()), vec3(0.5f)); return b; }
vec3 jv(const json& j, const char* k, vec3 d = vec3(0)) { return j.contains(k) ? jvec3(j[k], d) : d; }
}

// ============================================================ definitions
void TankSys::loadDefs(App& app) {
  defs.clear();
  fs::path dir = contentDir() / "vehicles";
  std::error_code ec;
  if (!fs::exists(dir, ec)) return;
  for (auto& e : fs::directory_iterator(dir, ec)) {
    if (e.path().extension() != ".json" || e.path().stem() == "ramming") continue;   // ramming.json: obstacle table (tank_ram.cpp)
    json j; try { j = json::parse(readTextFile(e.path())); } catch (std::exception& x) { logf("vehicle %s: %s", e.path().string().c_str(), x.what()); continue; }
    auto d = std::make_unique<TankDef>();
    d->raw = j; d->id = j.value("id", e.path().stem().string()); d->name = j.value("name", d->id);
    d->faction = j.value("faction", ""); d->identity = j.value("identity", "");
    // baked model
    std::string mdir = j.contains("model") ? j["model"].value("dir", "") : "";
    if (!mdir.empty()) {
      auto gm = std::make_shared<GpuModel>(); std::string err;
      if (gm->load(dir / "models" / mdir, err)) d->model = gm; else logf("vehicle %s: model not loaded (%s) - run --import-vehicle-models", d->id.c_str(), err.c_str());
    }
    if (d->model) {
      for (size_t k = 0; k < d->model->parts.size(); ++k) {
        const ModelPart& p = d->model->parts[k];
        int side = (p.bmin.x + p.bmax.x) * 0.5f < 0 ? 0 : 1;
        if (p.role == "turret") d->turretParts.push_back((int)k);
        else if (p.role == "gun") d->gunParts.push_back((int)k);
        else if (p.role == "hatch") d->hatchParts.push_back((int)k);
        else if (p.role == "mg") d->mgParts.push_back((int)k);
        else if (p.role == "track") d->trackParts[side].push_back((int)k);
        else if (p.role == "wheels") d->wheelParts[side].push_back((int)k);
        else d->hullParts.push_back((int)k);
      }
    }
    const json rg = j.value("rig", json::object());
    d->turretPivot = jv(rg, "turretPivot"); d->gunPivot = jv(rg, "gunPivot"); d->muzzle = jv(rg, "muzzle"); d->optic = jv(rg, "optic");
    d->hatchEye = jv(rg, "hatchEye"); d->mgPivot = jv(rg, "mgPivot"); d->mgMuzzle = jv(rg, "mgMuzzle"); d->engine = jv(rg, "engine");
    for (auto& x : rg.value("exhausts", json::array())) d->exhausts.push_back(jvec3(x));
    for (auto& x : rg.value("exits", json::array())) d->exits.push_back(jvec3(x));
    const json cl = j.value("colliders", json::object());
    for (auto& b : cl.value("hull", json::array())) d->hullBoxes.push_back(jbox(b));
    for (auto& b : cl.value("turret", json::array())) d->turretBoxes.push_back(jbox(b));
    if (cl.contains("armourHull")) d->armourHull = jbox(cl["armourHull"]);
    if (cl.contains("armourTurret")) d->armourTurret = jbox(cl["armourTurret"]);
    const json g = j.value("gun", json::object());
    d->traverseRate = g.value("traverseRate", d->traverseRate); d->traverseAccel = g.value("traverseAccel", d->traverseAccel);
    d->elevRate = g.value("elevationRate", d->elevRate); d->elevAccel = g.value("elevationAccel", d->elevAccel);
    if (g.contains("elevation")) { d->elevMin = g["elevation"][0]; d->elevMax = g["elevation"][1]; }
    d->recoil = g.value("recoil", d->recoil); d->fov = g.value("fov", d->fov); d->fovZoom = g.value("fovZoom", d->fovZoom);
    d->cannonName = g.value("name", ""); d->reticle = g.value("reticle", ""); d->zeroRange = g.value("zeroRange", d->zeroRange); d->muzzleScale = g.value("muzzleScale", d->muzzleScale);
    const json a = j.value("ammunition", json::object());
    d->reloadTime = a.value("reloadTime", d->reloadTime); d->reserveHE = a.value("reserveHE", d->reserveHE); d->reserveAP = a.value("reserveAP", d->reserveAP);
    d->startLoaded = a.value("startLoaded", d->startLoaded); d->ammoHE = a.value("he", ""); d->ammoAP = a.value("ap", "");
    d->velAP = a.value("velocityAP", d->velAP); d->velHE = a.value("velocityHE", d->velHE); d->dmgHE = a.value("blastHE", d->dmgHE);
    const json m = j.value("roofMg", json::object());
    d->mgWeapon = m.value("model", ""); d->mgName = m.value("name", ""); d->mgSound = m.value("sound", "shot_m2hb");
    d->mgModelOffset = jv(m, "modelOffset");
    if (m.contains("yaw")) { d->mgYawMin = m["yaw"][0]; d->mgYawMax = m["yaw"][1]; }
    if (m.contains("elevation")) { d->mgElevMin = m["elevation"][0]; d->mgElevMax = m["elevation"][1]; }
    d->mgRpm = m.value("rpm", d->mgRpm); d->mgVel = m.value("velocity", d->mgVel); d->mgSpread = m.value("spreadDeg", d->mgSpread);
    d->mgBelt = m.value("belt", d->mgBelt); d->mgReserve = m.value("reserve", d->mgReserve); d->mgReload = m.value("reload", d->mgReload); d->mgBulletMass = m.value("bulletMass", d->mgBulletMass);
    d->mgTail = m.value("tail", ""); d->mgReloadSound = m.value("reloadSound", "belt");
    const json bw = j.value("bowMg", json::object());
    d->bowOn = !bw.empty();
    d->bowName = bw.value("name", ""); d->bowShort = bw.value("short", "Bow MG");
    d->bowSound = bw.value("sound", "shot_mg42"); d->bowTail = bw.value("tail", ""); d->bowReloadSound = bw.value("reloadSound", "belt");
    d->bowPivot = jv(bw, "pivot"); d->bowMuzzle = jv(bw, "muzzle", d->bowPivot + vec3(0, 0, 0.35f));
    d->bowYaw = bw.value("yaw", d->bowYaw);
    if (bw.contains("elevation")) { d->bowElevMin = bw["elevation"][0]; d->bowElevMax = bw["elevation"][1]; }
    d->bowRpm = bw.value("rpm", d->bowRpm); d->bowVel = bw.value("velocity", d->bowVel); d->bowSpread = bw.value("spreadDeg", d->bowSpread);
    d->bowBelt = bw.value("belt", d->bowBelt); d->bowReserve = bw.value("reserve", d->bowReserve); d->bowReload = bw.value("reload", d->bowReload);
    d->bowBulletMass = bw.value("bulletMass", d->bowBulletMass);
    const json mo = j.value("mobility", json::object());
    d->maxFwd = mo.value("maxForward", d->maxFwd); d->maxRev = mo.value("maxReverse", d->maxRev); d->accel = mo.value("accel", d->accel);
    d->brake = mo.value("brake", d->brake); d->coast = mo.value("coast", d->coast); d->turnRate = mo.value("turnRate", d->turnRate);
    d->pivotRate = mo.value("pivotRate", d->pivotRate); d->maxSlopeDeg = mo.value("maxSlopeDeg", d->maxSlopeDeg); d->offroadMul = mo.value("offroadMul", d->offroadMul);
    d->halfSize = jv(mo, "halfSize", d->halfSize); d->clearance = mo.value("clearance", d->clearance); d->trackHalfGauge = mo.value("trackHalfGauge", d->trackHalfGauge); d->trackHalfLen = mo.value("trackHalfLength", d->trackHalfLen);
    const json hi = j.value("historical", json::object());
    d->histRoadKmh = hi.value("roadSpeedKmh", 0.0f); d->histSource = hi.value("source", "");
    const json du = j.value("durability", json::object());
    d->frontSurvive = du.value("frontHitsSurvived", d->frontSurvive); d->sideSurvive = du.value("sideHitsSurvived", d->sideSurvive);
    const json rm = j.value("ramming", json::object());
    d->massKg = rm.value("massKg", d->massKg); d->engineKW = rm.value("engineKW", d->engineKW); d->pushEff = rm.value("pushEfficiency", d->pushEff);
    logf("vehicle %s: %s, %zu model parts (hull %zu turret %zu gun %zu tracks %zu/%zu)", d->id.c_str(), d->name.c_str(), d->model ? d->model->parts.size() : 0,
         d->hullParts.size(), d->turretParts.size(), d->gunParts.size(), d->trackParts[0].size(), d->trackParts[1].size());
    std::string id = d->id;
    defs[id] = std::move(d);
  }
  // catalog entries: placement / pick bounds from the baked model (their collider parts are invisible)
  for (auto& a : app.lib.assets) if (const TankDef* d = defFor(a.get())) { a->bmin = d->model->bmin; a->bmax = d->model->bmax; a->bmax.y = std::min(a->bmax.y, 3.6f); }
  buildTankSounds(app.audio);
  loadRam(app);
}

const TankDef* TankSys::defFor(const AssetDef* a) const {
  if (!isVehicleAsset(a)) return nullptr;
  auto it = defs.find(a->raw["vehicle"].get<std::string>());
  return it == defs.end() || !it->second->model ? nullptr : it->second.get();
}

// ============================================================ preview (--tank-preview <dir>)
bool TankSys::writePreview(App& app, const fs::path& outDir) {
  std::error_code ec; fs::create_directories(outDir, ec);
  for (auto& [id, dp] : defs) {
    const TankDef& d = *dp;
    if (!d.model) continue;
    for (int roles = 0; roles < 2; ++roles) {   // roles 1: only the moving parts (gun, roof MG, hatch) over an empty hull
      std::vector<ModelDraw> md;
      auto add = [&](const std::vector<int>& ps) { for (int k : ps) md.push_back({d.model.get(), k, mat4(1)}); };
      if (!roles) { add(d.hullParts); add(d.turretParts); for (int s = 0; s < 2; ++s) { add(d.trackParts[s]); add(d.wheelParts[s]); } }
      add(d.gunParts); add(d.hatchParts); add(d.mgParts);
      vec3 bmin = d.model->bmin, bmax = d.model->bmax, c = (bmin + bmax) * 0.5f;
      float R = glm::length(bmax - bmin) * 0.5f + 0.3f;
      struct View { const char* tag; vec3 dir, up; bool persp; };
      View views[] = {{"side", vec3(-1, 0, 0), vec3(0, 1, 0), false}, {"front", vec3(0, 0, 1), vec3(0, 1, 0), false}, {"top", vec3(0, 1, 0), vec3(0, 0, 1), false},
                      {"rear", vec3(0, 0, -1), vec3(0, 1, 0), false}, {"34", glm::normalize(vec3(-1, 0.6f, 1.2f)), vec3(0, 1, 0), true}};
      for (auto& v : views) {
        int W = 1600, H = 1000;
        vec3 eye = c + v.dir * (R * 3.0f);
        mat4 view = glm::lookAt(eye, c, v.up);
        float hw = R * 1.05f, hh = hw * H / W;
        mat4 proj = v.persp ? glm::perspective(radians(38.0f), (float)W / H, 0.1f, 100.0f) : glm::ortho(-hw, hw, -hh, hh, 0.1f, 100.0f);
        std::vector<uint8_t> px;
        app.R.renderModelSheet(md, {}, view, proj, eye, W, H, px);
        mat4 vp = proj * view;
        auto put = [&](int x, int y, uint32_t col, float a) {
          if (x < 0 || y < 0 || x >= W || y >= H) return;
          uint8_t* q = &px[((size_t)y * W + x) * 4];
          for (int k = 0; k < 3; ++k) q[k] = (uint8_t)(q[k] * (1 - a) + ((col >> (8 * k)) & 255) * a);
        };
        auto proj2 = [&](vec3 w, int& x, int& y) { vec4 cc = vp * vec4(w, 1); if (cc.w <= 0) return false; x = (int)std::lround((cc.x / cc.w * 0.5f + 0.5f) * W); y = (int)std::lround((0.5f - cc.y / cc.w * 0.5f) * H); return true; };
        auto line = [&](vec3 a, vec3 b, uint32_t col) { int n = 300; for (int k = 0; k <= n; ++k) { int x, y; if (proj2(glm::mix(a, b, k / (float)n), x, y)) put(x, y, col, 0.9f); } };
        if (!v.persp) {   // 0.5 m grid in the view plane through the origin (red / green: the axes)
          vec3 ax = glm::normalize(glm::cross(v.up, v.dir)), ay = v.up;
          for (int k = -16; k <= 16; ++k) {
            float s = k * 0.5f; float a = k == 0 ? 0.8f : k % 2 == 0 ? 0.35f : 0.15f;
            for (int q = 0; q < 2; ++q) {
              vec3 o = (q ? ax : ay) * s, dd = (q ? ay : ax) * 8.0f;
              int x0, y0, x1, y1; if (!proj2(o - dd, x0, y0) || !proj2(o + dd, x1, y1)) continue;
              int n = std::max(std::abs(x1 - x0), std::abs(y1 - y0)) + 1;
              for (int t = 0; t <= n; ++t) put(x0 + (x1 - x0) * t / n, y0 + (y1 - y0) * t / n, k == 0 ? (q ? 0xff00c000u : 0xff0000e0u) : 0xff000000u, a);
            }
          }
        }
        // colliders (cyan hull, orange turret) and sockets
        auto box = [&](const TankBox& b, uint32_t col) {
          vec3 p[8]; for (int k = 0; k < 8; ++k) p[k] = b.c + b.h * vec3(k & 1 ? 1 : -1, k & 2 ? 1 : -1, k & 4 ? 1 : -1);
          int e[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
          for (auto& q : e) line(p[q[0]], p[q[1]], col);
        };
        for (auto& b : d.hullBoxes) box(b, 0xffffff00u);
        for (auto& b : d.turretBoxes) box(b, 0xff0080ffu);
        struct Mk { vec3 p; uint32_t col; };
        std::vector<Mk> mk = {{d.turretPivot, 0xff0000ffu}, {d.gunPivot, 0xffff0000u}, {d.muzzle, 0xffff00ffu}, {d.optic, 0xff00ff00u}, {d.hatchEye, 0xff00ffffu}, {d.mgPivot, 0xffffffffu}, {d.mgMuzzle, 0xffffff80u}, {d.engine, 0xff4040ffu}};
        for (auto& e : d.exhausts) mk.push_back({e, 0xff808080u});
        for (auto& e : d.exits) mk.push_back({e, 0xff00c0ffu});
        for (auto& m : mk) { int x, y; if (!proj2(m.p, x, y)) continue; for (int k = -10; k <= 10; ++k) { put(x + k, y, m.col, 1); put(x, y + k, m.col, 1); put(x + k, y + 1, m.col, 1); put(x + 1, y + k, m.col, 1); } }
        fs::path p = outDir / (id + "_" + v.tag + (roles ? "_moving" : "") + ".png");
        stbi_write_png(p.string().c_str(), W, H, 4, px.data(), W * 4);
      }
    }
    logf("tank preview %s written", id.c_str());
  }
  return true;
}
