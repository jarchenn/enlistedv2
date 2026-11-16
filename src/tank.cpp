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
  // VH1 variants: "base": "<id>" takes that file and applies the variant's keys over it (JSON merge patch)
  std::map<std::string, json> files;
  for (auto& e : fs::directory_iterator(dir, ec)) {
    if (e.path().extension() != ".json" || e.path().stem() == "ramming") continue;   // ramming.json: obstacle table (tank_ram.cpp)
    try { files[e.path().stem().string()] = json::parse(readTextFile(e.path())); } catch (std::exception& x) { logf("vehicle %s: %s", e.path().string().c_str(), x.what()); }
  }
  std::map<std::string, std::shared_ptr<GpuModel>> loaded;   // variants share their base's GPU model
  for (auto& [stem, j0] : files) {
    json j = j0;
    if (j0.contains("base")) {
      auto b = files.find(j0["base"].get<std::string>());
      if (b == files.end()) { logf("vehicle %s: base %s not found", stem.c_str(), j0["base"].get<std::string>().c_str()); continue; }
      j = b->second; j.erase("fallbackModel"); j.merge_patch(j0); j.erase("base");
    }
    auto d = std::make_unique<TankDef>();
    d->raw = j; d->id = j.value("id", stem); d->name = j.value("name", d->id);
    d->faction = j.value("faction", ""); d->identity = j.value("identity", "");
    // baked model (a car whose supplied model is local-only falls back to its committed part-built stand-in)
    for (const char* key : {"dir", "fallbackDir"}) {
      std::string mdir = j.contains("model") ? j["model"].value(key, "") : "";
      if (mdir.empty() || d->model) continue;
      if (loaded.count(mdir)) { d->model = loaded[mdir]; continue; }
      auto gm = std::make_shared<GpuModel>(); std::string err;
      if (gm->load(dir / "models" / mdir, err)) { d->model = loaded[mdir] = gm; if (std::string(key) == "fallbackDir") logf("vehicle %s: supplied model missing, part-built fallback %s", d->id.c_str(), mdir.c_str()); }
      else logf("vehicle %s: model %s not loaded (%s) - run --import-vehicle-models", d->id.c_str(), mdir.c_str(), err.c_str());
    }
    d->car = j.value("car", false);
    if (d->model) {
      static const char* wheelRoles[TankDef::MAX_WHEELS] = {"wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr", "wheel_f2l", "wheel_f2r", "wheel_r2l", "wheel_r2r"};
      vec3 wlo[TankDef::MAX_WHEELS], whi[TankDef::MAX_WHEELS]; for (int w = 0; w < TankDef::MAX_WHEELS; ++w) { wlo[w] = vec3(1e9f); whi[w] = vec3(-1e9f); }
      vec3 slo(1e9f), shi(-1e9f);
      for (size_t k = 0; k < d->model->parts.size(); ++k) {
        const ModelPart& p = d->model->parts[k];
        int side = (p.bmin.x + p.bmax.x) * 0.5f < 0 ? 0 : 1;
        int wheel = -1; for (int w = 0; w < TankDef::MAX_WHEELS; ++w) if (p.role == wheelRoles[w]) wheel = w;
        if (d->car && wheel >= 0) { d->wheelPart[wheel].push_back((int)k); wlo[wheel] = glm::min(wlo[wheel], p.bmin); whi[wheel] = glm::max(whi[wheel], p.bmax); }
        else if (d->car && p.role == "steer") { d->steerParts.push_back((int)k); slo = glm::min(slo, p.bmin); shi = glm::max(shi, p.bmax); }
        else if (d->car && p.role == "windshield") d->windshieldParts.push_back((int)k);
        else if (d->car && p.role == "stowage") d->stowageParts.push_back((int)k);
        else if (d->car && p.role == "pintle") d->pintleParts.push_back((int)k);
        else if (d->car && p.role == "rws") d->rwsParts.push_back((int)k);
        else if (p.role == "rotor") d->rotorParts.push_back((int)k);          // VH5 helicopter
        else if (p.role == "tailrotor") d->tailRotorParts.push_back((int)k);
        else if (p.role == "turret") d->turretParts.push_back((int)k);
        else if (p.role == "gun") d->gunParts.push_back((int)k);
        else if (p.role == "hatch") d->hatchParts.push_back((int)k);
        else if (p.role == "mg") d->mgParts.push_back((int)k);
        else if (p.role == "track") d->trackParts[side].push_back((int)k);
        else if (p.role == "wheels") d->wheelParts[side].push_back((int)k);
        else d->hullParts.push_back((int)k);
      }
      if (d->car) {
        for (int w = 0; w < TankDef::MAX_WHEELS; ++w) if (!d->wheelPart[w].empty()) { d->wheelC[w] = (wlo[w] + whi[w]) * 0.5f; d->wheelR = (whi[w].y - wlo[w].y) * 0.5f; }
        if (!d->wheelPart[4].empty()) d->nWheels = 8;
        if (!d->steerParts.empty()) d->steerC = (slo + shi) * 0.5f;
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
    d->heli = j.value("heli", false);
    if (d->heli) {   // VH5 helicopter: flight, rotors, chin gun, rockets; damage as an armoured car
      d->rotorHub = jv(rg, "rotorHub"); d->tailHub = jv(rg, "tailRotorHub");
      for (auto& x : rg.value("rocketPods", json::array())) d->pods.push_back(jvec3(x));
      if (cl.contains("rotorDisc")) d->rotorDisc = jbox(cl["rotorDisc"]);
      const json fl = j.value("flight", json::object());
      d->rotorRps = fl.value("rotorRps", d->rotorRps); d->tailRps = fl.value("tailRps", d->tailRps);
      if (fl.contains("pitchLimits")) { d->hPitchMin = fl["pitchLimits"][0]; d->hPitchMax = fl["pitchLimits"][1]; }
      const json cg = j.value("chinGun", json::object());
      d->gun30Name = cg.value("name", "Chin gun"); d->gun30Sound = cg.value("sound", "shot_m2hb");
      d->gun30Rpm = cg.value("rpm", d->gun30Rpm); d->gun30Vel = cg.value("velocity", d->gun30Vel); d->gun30Dmg = cg.value("damage", d->gun30Dmg); d->gun30R = cg.value("blastRadius", d->gun30R);
      d->gun30Ammo = cg.value("ammo", d->gun30Ammo); d->gun30Heat = cg.value("heatPerShot", d->gun30Heat); d->gun30Cool = cg.value("coolPerSec", d->gun30Cool); d->gun30Resume = cg.value("resumeHeat", d->gun30Resume);
      d->gun30Spread = cg.value("spreadDeg", d->gun30Spread);
      if (cg.contains("yaw")) { d->gun30YawMin = cg["yaw"][0]; d->gun30YawMax = cg["yaw"][1]; }
      if (cg.contains("elevation")) { d->gun30ElevMin = cg["elevation"][0]; d->gun30ElevMax = cg["elevation"][1]; }
      const json rk = j.value("rockets", json::object());
      d->rocketCount = rk.value("count", d->rocketCount); d->rocketVel = rk.value("velocity", d->rocketVel); d->rocketDmg = rk.value("damage", d->rocketDmg); d->rocketR = rk.value("radius", d->rocketR);
      d->rocketCrater = rk.value("crater", d->rocketCrater); d->rocketSplashGun = rk.value("splashGun", d->rocketSplashGun); d->rocketGrav = rk.value("gravScale", d->rocketGrav); d->rocketCooldown = rk.value("cooldown", d->rocketCooldown);
      d->smallArmsDmg = du.value("smallArmsDamage", 0); d->heavyBulletJ = du.value("heavyBulletJ", 0.0f); d->heavyBulletDmg = du.value("heavyBulletDamage", 0);
      d->launcherDmg = du.value("launcherDamage", 50); d->cannonDmg = du.value("cannonDamage", 100);
      logf("vehicle %s: helicopter, rotor parts %zu, tail rotor parts %zu, %zu rocket pods", d->id.c_str(), d->rotorParts.size(), d->tailRotorParts.size(), d->pods.size());
    }
    if (d->car) {
      d->driverEye = jv(rg, "driverEye", d->hatchEye); if (!rg.contains("hatchEye")) d->hatchEye = d->driverEye;   // VH2: the exits are checked from the driver's head
      d->steerAxis = glm::normalize(jv(rg, "steerAxis", d->steerAxis)); d->windshieldPivot = jv(rg, "windshieldPivot");
      // the driver's eye relative to the steering wheel: holds for the supplied model and for its part-built fallback alike
      if (rg.contains("driverEyeFromSteer")) { d->driverEye = d->steerC + jv(rg, "driverEyeFromSteer"); if (!rg.contains("hatchEye")) d->hatchEye = d->driverEye; }
      const json cd = j.value("carDrive", json::object());
      d->maxSteerDeg = cd.value("maxSteerDeg", d->maxSteerDeg); d->steerRate = cd.value("steerRate", d->steerRate); d->steerTurns = cd.value("steerTurns", d->steerTurns);
      d->handbrakeDecel = cd.value("handbrakeDecel", d->handbrakeDecel); d->suspTravel = cd.value("suspensionTravel", d->suspTravel); d->runOverDmg = cd.value("runOverDamage", d->runOverDmg);
      const json va = j.value("variant", json::object());
      d->windshieldFold = va.value("windshieldFoldDeg", 0.0f); d->stowage = va.value("stowage", true); d->pintle = va.value("pintle", false);
      d->smallArmsDmg = du.value("smallArmsDamage", 0);
      d->heavyBulletJ = du.value("heavyBulletJ", 0.0f); d->heavyBulletDmg = du.value("heavyBulletDamage", 0);
      d->launcherDmg = du.value("launcherDamage", 100); d->cannonDmg = du.value("cannonDamage", 100);
      d->grip = cd.value("gripMps2", d->grip);
      if (!d->wheelPart[0].empty()) { d->trackHalfGauge = 0.5f * (d->wheelC[0].x - d->wheelC[1].x); d->trackHalfLen = 0.5f * (d->wheelC[0].z - d->wheelC[2].z); }
      // steering base: front axle to the unsteered line (8x8: the two front axles steer about the middle of the rear two)
      d->steerPivotZ = d->nWheels == 8 ? 0.5f * (d->wheelC[2].z + d->wheelC[6].z) : d->wheelC[2].z;
      d->carL = d->wheelPart[0].empty() ? 2 * d->trackHalfLen : std::max(0.5f, d->wheelC[0].z - d->steerPivotZ);
      d->rwsSight = m.value("remoteSight", false); d->mgOptic = jv(rg, "mgOptic", d->mgPivot);
      d->rwsFov = m.value("fov", d->rwsFov); d->rwsFovZoom = m.value("fovZoom", d->rwsFovZoom);
    }
    logf("vehicle %s: %s, %zu model parts (hull %zu turret %zu gun %zu tracks %zu/%zu)", d->id.c_str(), d->name.c_str(), d->model ? d->model->parts.size() : 0,
         d->hullParts.size(), d->turretParts.size(), d->gunParts.size(), d->trackParts[0].size(), d->trackParts[1].size());
    std::string id = d->id;
    defs[id] = std::move(d);
  }
  // catalog entries: placement / pick bounds from the baked model (their collider parts are invisible)
  for (auto& a : app.lib.assets) if (const TankDef* d = defFor(a.get())) { a->bmin = d->model->bmin; a->bmax = d->model->bmax; a->bmax.y = std::min(a->bmax.y, 3.6f); }
  for (auto& [id, d] : defs) if (d->car) logf("vehicle %s: car, wheels r %.3f track %.2f wheelbase %.2f, steering wheel (%.2f %.2f %.2f)", id.c_str(), d->wheelR, 2 * d->trackHalfGauge, 2 * d->trackHalfLen, d->steerC.x, d->steerC.y, d->steerC.z);
  buildTankSounds(app.audio);
  loadRam(app);
}

const TankDef* TankSys::defFor(const AssetDef* a) const {
  if (!isVehicleAsset(a)) return nullptr;
  auto it = defs.find(a->raw["vehicle"].get<std::string>());
  return it == defs.end() || !it->second->model ? nullptr : it->second.get();
}

// ============================================================ BR7 adopted part-built cars
bool TankSys::adoptCar(App& app, uint64_t id) {
  Instance* inst = app.doc.find(id); auto rt = app.rt.find(id);
  if (!inst || rt == app.rt.end() || !rt->second.asset || tanks.count(id)) return false;
  const AssetDef* a = rt->second.asset;
  auto& dp = adoptedDefs[a->id];
  if (!dp) {
    vec3 rlo(1e9f), rhi(-1e9f), dash(0, 1.0f, 0.6f), blo(1e9f), bhi(-1e9f); float wr = 0.33f; bool rims = false;
    for (auto& p : a->parts) {
      const std::string& mn = p.mat >= 0 && p.mat < (int)a->materials.size() ? a->materials[p.mat].name : std::string();
      blo = glm::min(blo, p.center - p.size * 0.5f); bhi = glm::max(bhi, p.center + p.size * 0.5f);
      if (mn == "rim") { rims = true; rlo = glm::min(rlo, p.center); rhi = glm::max(rhi, p.center); wr = std::max(wr, p.center.y); }
      if (mn == "dash") dash = p.center;
    }
    if (!rims || bhi.x <= blo.x) return false;   // not a car
    auto d = std::make_unique<TankDef>();
    d->id = a->id; d->name = a->name; d->faction = ""; d->car = true; d->partsAsset = a;
    float hx = 0.5f * (bhi.x - blo.x), hz = 0.5f * (bhi.z - blo.z), top = bhi.y, clr = 0.3f;
    TankBox body; body.c = vec3(0, clr + 0.5f * (top - clr), 0.5f * (blo.z + bhi.z)); body.h = vec3(hx * 0.95f, 0.5f * (top - clr), hz * 0.97f);
    d->hullBoxes = {body}; d->armourHull = body; d->halfSize = body.h; d->clearance = clr;
    d->trackHalfGauge = std::max(0.5f, std::max(std::fabs(rlo.x), std::fabs(rhi.x)));
    d->trackHalfLen = std::max(0.8f, 0.5f * (rhi.z - rlo.z)); d->carL = 2 * d->trackHalfLen;
    d->wheelR = wr;
    d->driverEye = vec3(-0.37f, dash.y + 0.38f, dash.z - 0.75f); d->hatchEye = d->driverEye;   // the generic kit seats the driver at x -0.37
    d->engine = vec3(0, 0.8f, bhi.z - 0.6f); d->exhausts = {vec3(0.4f, 0.3f, blo.z - 0.05f)};
    float sx = hx + 1.1f; d->exits = {vec3(-sx, 0, 0), vec3(sx, 0, 0), vec3(-sx, 0, blo.z * 0.6f), vec3(sx, 0, blo.z * 0.6f), vec3(0, 0, blo.z - 1.5f), vec3(0, 0, bhi.z + 1.5f)};
    // arcade civilian handling (bigger = slower): ~80 km/h on roads, 45 off-road
    float big = std::clamp((hz * 2 - 4.2f) / 2.0f, 0.0f, 1.0f);
    d->maxFwd = glm::mix(22.0f, 17.0f, big); d->maxRev = 5; d->accel = glm::mix(4.5f, 3.0f, big); d->brake = 9; d->coast = 0.8f; d->offroadMul = 0.55f; d->maxSlopeDeg = 28;
    d->maxSteerDeg = 33; d->steerRate = 2.2f; d->steerTurns = 2.6f; d->handbrakeDecel = 8; d->suspTravel = 0.08f; d->runOverDmg = 10;
    d->frontSurvive = 1; d->sideSurvive = 1; d->smallArmsDmg = 2; d->massKg = glm::mix(1400.0f, 2800.0f, big); d->engineKW = 90; d->pushEff = 0.5f;
    dp = std::move(d);
  }
  TankRT t; t.id = id; t.def = dp.get(); t.adopted = true; t.body = rt->second.staticBody; t.shapeYaw = 0;
  t.pos = inst->pos; t.yaw = inst->yaw;
  float y, p, r, s; groundPose(app, *t.def, t.pos, t.yaw, y, p, r, s); t.pos.y = std::max(inst->pos.y, y); t.pitch = p; t.roll = r;
  tanks[id] = t;
  return true;
}

void TankSys::releaseAdopted(App& app) {
  for (auto it = tanks.begin(); it != tanks.end();) {
    if (!it->second.adopted) { ++it; continue; }
    TankRT& t = it->second;
    for (int* h : {&t.engineLoop, &t.trackLoop, &t.turretLoop}) { app.audio.loopStop(*h); *h = -1; }
    if (occupied == it->first) occupied = 0;
    if (Instance* i = app.doc.find(it->first)) if (t.body != 0xffffffffu) Physics::setTransform(t.body, i->pos, yawQuat(i->yaw));   // its body back where it was parked
    it = tanks.erase(it);
  }
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
