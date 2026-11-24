// Crew-served stations (PaK 40, M2HB), projectiles and ammunition crates.
// All state here is an Explore session overlay: F5 starts from authored defaults, returning discards it.
#include "app.h"
#include "fxdata.h"   // Battle Foundry 6 (fxrecoil)
#include "terrain_deform.h"
#include "crumb.h"
#include "mpai.h"
#include "net.h"
#include "water_query.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>

static uint64_t coreUD(uint64_t id) { return id << 16; }
// belt-fed machine-gun stations (M2HB tripod, MG 42 embrasure mount): continuous fire, belt reloads, no handler role
static bool mgKind(const std::string& k) { return k == "m2hb" || k == "mg42"; }
// CAN1: muzzle-loaders on wheeled carriages (18th-century cannon, mortar): no traverse mount (the gunner's yaw turns the
// carriage), aimed over the tube at a white dot, fired by a fuse, rolled back by the shot
static bool cannonKind(const std::string& k) { return k == "cannon"; }
static float approach(float v, float t, float rate);
// the carriage's speed along its +z after dt: the handler's push (tgt) builds slowly (it is heavy); otherwise the recoil's
// braking above walking pace, then rolling resistance
static float cannonSpeed(float v, float tgt, float push, float dt) {
  if (tgt != 0 && tgt * v >= 0 && std::fabs(v) <= std::fabs(tgt)) return approach(v, tgt, 0.45f * dt);
  return approach(v, 0.0f, (std::fabs(v) > push ? 5.0f : 1.2f) * dt);
}
// the shot's roll-back speed: rolls `recoil` m under cannonSpeed's two braking rates
static float cannonRecoilSpeed(float recoil, float push) { return -std::sqrt(2.0f * 5.0f * std::max(0.0f, recoil - push * push / 2.4f) + push * push); }
// MG 42 mount with the supplied GLB in its cradle (app.cpp draws it at elevM * translate(-bore)): the gunner aims as
// with the handheld MG 42 - the eye on the gun's own sight line (rear V-notch -> front post) behind the notch. The
// handheld draws the gun at its weapon FOV under its ADS world FOV; here the gun is part of the world, so the eye
// sits back by the focal-length ratio and the notch is drawn at the handheld's size (docs/bunkers.md).
// Out, relative to elevPivot in the elevation frame: eye, front post; and the world FOV. False without the GLB.
static bool mg42MountSight(const App& a, vec3& eye, vec3& front, float& fov) {
  int g = a.weaponIndex("mg42");
  if (g < 0 || !a.weaponDefs[g]->useModel()) return false;
  const WeaponDef& w = *a.weaponDefs[g];
  vec3 bore = w.muzzle; bore.z = 0.45f;   // as drawn (app.cpp)
  fov = w.adsWorldFov;
  float vmF = w.adsVmFov > 0 ? w.adsVmFov : a.vm.vmFov;
  float relief = w.eyeRelief * std::tan(radians(vmF) * 0.5f) / std::tan(radians(fov) * 0.5f);
  eye = w.rear - glm::normalize(w.front - w.rear) * relief - bore;
  front = w.front - bore;
  return true;
}
// standing height at a crew position: the floor under it when one is there (a bunker floor, docs/bunkers.md), else
// the terrain
static float crewFloor(float terrainY, vec3 a) {
  Physics::Hit h;
  if (Physics::castRayEx(vec3(a.x, std::max(a.y, terrainY) + 1.2f, a.z), vec3(0, -1, 0), 1.9f, h) && h.pos.y > terrainY + 0.02f) return h.pos.y;
  return terrainY;
}
static float approach(float v, float t, float rate) { float d = t - v; float s = rate; return std::fabs(d) <= s ? t : v + (d > 0 ? s : -s); }

StationRT* App::station(uint64_t id) {
  auto it = rt.find(id);
  if (it == rt.end() || it->second.asset->station.kind.empty()) return nullptr;
  return &it->second.st;
}

bool App::gunnerOptic() const {
  if (!occupied || mode != Mode::Explore) return false;
  auto it = rt.find(occupied);
  if (it == rt.end() || it->second.asset->station.kind.empty()) return false;
  return it->second.st.pos2 == ST_GUNNER && !mgKind(it->second.asset->station.kind) && !cannonKind(it->second.asset->station.kind);
}

mat4 App::stationMatrix(const Instance& i, const InstanceRT& r) const {
  vec3 p = r.st.active ? r.st.pos : i.pos; float y = r.st.active ? r.st.carYaw : i.yaw;
  return glm::translate(mat4(1), p) * glm::mat4_cast(yawQuat(y));
}

void App::stationParts(const Instance& i, const InstanceRT& r, mat4& base, mat4& yawM, mat4& elevM, mat4& recoilM, bool visual) const {
  const StationDef& s = r.asset->station;
  base = stationMatrix(i, r);
  yawM = base * glm::translate(mat4(1), s.yawPivot) * glm::mat4_cast(glm::angleAxis(radians(r.st.yaw), vec3(0, 1, 0)));
  elevM = yawM * glm::translate(mat4(1), s.elevPivot - s.yawPivot) * glm::mat4_cast(glm::angleAxis(radians(-r.st.elev), vec3(1, 0, 0)));
  float t = r.st.recoilT, off = 0;
  if (mgKind(s.kind) || cannonKind(s.kind)) t = 1.0f;   // mounted MGs: no recoil travel; a cannon's whole carriage rolls back (stepStation)
  if (gFxRecoil && !mgKind(s.kind) && !cannonKind(s.kind)) {   // fxrecoil (7.2 / 7.3): the brake's curve (Pak 40: 158 ms back, 788 ms home) on the rig's travel, and the carriage
    const FxGunRow& g = fxGunFor(s.kind);    // hops about its bedded spades (I_spade ~ 3 I: a third of the free 8.9 deg), wheels lifting, settling at f_n / zeta
    off = fxGunRecoilX(t + (gFxTier >= 2 ? fxGunPreTime(g) : 0.0f), g) * (s.recoil / std::max(g.L, 1e-3f));
    if (visual && g.platform == "towed" && t < 3.0f) {   // drawn only: the aim, the muzzle socket and the sight keep the bedded pose
      const float wn = 2.0f * 3.14159265f * g.fn, z = g.zeta, wd = wn * std::sqrt(1 - z * z), th = radians(g.pitchDeg / 3.0f) / fxSpringPeak(z) * std::exp(-z * wn * t) * std::sin(wd * t);
      const vec3 spade(0, 0, s.elevPivot.z - 2.4f);   // shortcut: the spade line ~2.4 m behind the trunnions (no spade marker in the station JSON yet)
      const mat4 hop = glm::translate(mat4(1), spade) * glm::mat4_cast(glm::angleAxis(-th, vec3(1, 0, 0))) * glm::translate(mat4(1), -spade);
      base = base * hop; yawM = base * glm::translate(mat4(1), s.yawPivot) * glm::mat4_cast(glm::angleAxis(radians(r.st.yaw), vec3(0, 1, 0)));
      elevM = yawM * glm::translate(mat4(1), s.elevPivot - s.yawPivot) * glm::mat4_cast(glm::angleAxis(radians(-r.st.elev), vec3(1, 0, 0)));
    }
  }
  else if (t < 0.06f) off = s.recoil * (t / 0.06f);
  else if (t < 0.7f) { float u = (t - 0.06f) / 0.64f; off = s.recoil * (1 - u * u * (3 - 2 * u)); }   // return to battery
  recoilM = elevM * glm::translate(mat4(1), vec3(0, 0, -off));
}

vec3 App::stationMuzzle(const Instance& i, const InstanceRT& r, vec3& dir) const {
  mat4 b, y, e, rc; stationParts(i, r, b, y, e, rc);
  dir = glm::normalize(vec3(e * vec4(0, 0, 1, 0)));
  return vec3(e * vec4(r.asset->station.muzzle - r.asset->station.elevPivot, 1));
}

bool App::stationCamera(vec3& eye, vec3& fwd, float& fov) const {
  if (!occupied) return false;
  auto it = rt.find(occupied); if (it == rt.end()) return false;
  const Instance* inst = nullptr; for (auto& i : doc.instances) if (i.id == occupied) inst = &i;
  if (!inst) return false;
  auto& r = it->second;
  if (r.st.pos2 != ST_GUNNER) return false;   // handler / transitions use the normal eye camera
  mat4 b, y, e, rc; stationParts(*inst, r, b, y, e, rc);
  const StationDef& s = r.asset->station;
  vec3 se = s.optic - s.elevPivot, sf; fov = s.sightFov;   // PaK 40 ZF 3x8: 8 deg real field fills the eyepiece circle (9); others from the asset
  if (s.kind == "mg42") mg42MountSight(*this, se, sf, fov);   // the handheld MG 42's ADS eye and FOV
  eye = vec3(e * vec4(se, 1));
  if (cannonKind(s.kind)) eye = vec3(b * vec4(s.optic, 1));   // CAN1: the gunner's eye stays behind the breech, the view along the tube
  fwd = glm::normalize(vec3(e * vec4(0, 0, 1, 0)));
  return true;
}

// ground support + swept overlap for a candidate carriage transform
bool App::carriageFits(const Instance& i, const InstanceRT& r, vec3& pos, float yaw, std::string& why) const {
  const StationDef& s = r.asset->station;
  quat q = yawQuat(yaw);
  auto w = [&](vec3 l) { return pos + q * l; };
  std::vector<vec3> wheels, spades;
  if (s.kind == "pak40") { wheels = {w({-0.9f, 0, 0}), w({0.9f, 0, 0})}; spades = {w({-0.98f, 0, -3.05f}), w({0.98f, 0, -3.05f})}; }
  else if (cannonKind(s.kind)) { wheels = {w({-0.85f * s.moveHalf.x, 0, 0}), w({0.85f * s.moveHalf.x, 0, 0})}; spades = {w({0, 0, -0.9f * s.moveHalf.z})}; }   // CAN1: the trail end
  else { wheels = {w({-0.4f, 0, -0.6f}), w({0.4f, 0, -0.6f})}; spades = {w({0, 0, 0.8f})}; }
  for (auto& p : wheels) if (!terrain.inBuildArea(p.x, p.z)) { why = "outside the build area"; return false; }
  float h0 = terrain.height(wheels[0].x, wheels[0].z), h1 = terrain.height(wheels[1].x, wheels[1].z);
  if (std::fabs(h0 - h1) > 0.3f) { why = "wheels on uneven ground"; return false; }
  for (auto& p : wheels) if (terrain.normal(p.x, p.z).y < std::cos(radians(20.0f))) { why = "too steep for the wheels (crater edge?)"; return false; }
  float yc = 0.5f * (h0 + h1);
  for (auto& p : spades) { float d = terrain.height(p.x, p.z) - yc; if (d < -0.55f || d > 0.45f) { why = "trail spades unsupported"; return false; } }
  for (auto& p : wheels) if (terrain.height(p.x, p.z) < terrain.waterLevel + 0.1f) { why = "in water"; return false; }
  // sweep covers the whole gun: spades to muzzle brake (the barrel must not be pushed into a wall either)
  vec3 c = vec3(pos.x, yc, pos.z) + q * vec3(0, s.moveHalf.y + 0.1f, s.kind == "pak40" ? 0.0f : 0.0f);
  vec3 he = s.kind == "pak40" ? vec3(s.moveHalf.x, s.moveHalf.y, 3.2f) : s.moveHalf;
  if (Physics::boxBlocked(c, he, q, coreUD(i.id), true)) { why = "blocked by a structure"; return false; }
  pos.y = yc;
  return true;
}

void App::resetStation(const Instance& i) {
  auto it = rt.find(i.id); if (it == rt.end()) return;
  auto& r = it->second; const AssetDef* a = r.asset;
  StationRT s;
  s.active = true; s.pos = i.pos; s.carYaw = i.yaw;
  json o = i.extra.contains("station") ? i.extra["station"] : json::object();
  s.loaded = o.value("loaded", a->station.loaded);
  s.reserve = o.value("reserve", a->station.reserve);
  s.reserveMax = std::max(a->station.reserve, s.reserve);
  s.reserveAT = o.value("reserveAT", a->station.reserveAT); s.reserveATMax = std::max(a->station.reserveAT, s.reserveAT);
  s.wrecked = o.value("wrecked", false);   // applied destruction persists; session wrecks reset with the overlay
  if (s.wrecked) { s.loaded = 0; s.status = "Destroyed"; }
  r.st = s;
  Physics::setTransform(r.staticBody, i.pos, yawQuat(i.yaw));
}

void App::resetCombat() {
  projectiles.clear(); fx.clear();
  // GM3: tanks and field guns the player drove or pushed keep that place: their pose goes into the document before the
  // reset (the next Explore or match starts them there). Not during a LAN session: the map hash (LAN identity) would move.
  if (!gNet || gNet->role == Net::NONE) {
    for (auto& i : doc.instances) {
      auto it = rt.find(i.id); if (it == rt.end() || !it->second.asset) continue;
      vec3 p = i.pos; float y = i.yaw;
      if (!it->second.asset->station.kind.empty() && it->second.st.active) { p = it->second.st.pos; y = it->second.st.carYaw; }
      else if (auto tk = tanks.tanks.find(i.id); tk != tanks.tanks.end() && tk->second.def) { p = tk->second.pos; y = tk->second.yaw; }
      if (glm::length(p - i.pos) > 0.05f || std::fabs(wrapDeg(y - i.yaw)) > 0.5f) { i.pos = p; i.yaw = wrapDeg(y); bump(i); }
    }
  }
  tanks.reset(*this);   // tank_sim.cpp: authored / applied tank state
  occupied = 0; choiceFor = 0;
  crateStock.clear(); crateUnlimited.clear();
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    auto& r = it->second;
    const AssetDef* a = r.asset;
    if (!a->station.kind.empty()) resetStation(i);
    resetCrate(i);
  }
}

void App::resetCrate(const Instance& i) {   // a crate's authored stock (also for crates placed during play)
  const AssetDef* a = assetFor(i);
  if (!a || !a->crate.is_object() || a->crate.empty()) return;
  json o = i.extra.contains("crate") ? i.extra["crate"] : json::object();
  json sup = o.contains("supplies") ? o["supplies"] : a->crate.value("supplies", json::object());
  for (auto& [k, v] : sup.items()) crateStock[i.id][k] = v.get<int>();
  crateUnlimited[i.id] = o.value("unlimited", a->crate.value("unlimited", false));
}

vec3 App::stationAnchor(uint64_t id, int pos) {
  Instance* inst = doc.find(id); auto& r = rt[id];
  vec3 a = vec3(stationMatrix(*inst, r) * vec4(pos == ST_HANDLER ? r.asset->station.handler : r.asset->station.gunner, 1));
  a.y = crewFloor(terrain.height(a.x, a.z), a);
  return a;
}

bool App::occupyStation(uint64_t id, int pos) {
  Instance* inst = doc.find(id); StationRT* s = station(id);
  if (!inst || !s) return false;
  if (mpaiStationRequest(*this, id, pos)) return false;   // LAN client: the host decides (net.cpp)
  if (s->wrecked) { toast("The gun is destroyed."); return false; }
  if (occupied && occupied != id) { toast("You are already crewing another gun."); return false; }
  if (pos == ST_HANDLER && mgKind(rt[id].asset->station.kind)) { toast("The " + rt[id].asset->name + " is crewed from the grips only (Aim)."); return false; }
  if (pos == ST_HANDLER && rt[id].asset->station.fixed) { toast(rt[id].asset->station.label + ": a dug-in emplacement - crew it from the gunner's position (Aim); it is not moved."); return false; }
  auto& r = rt[id];
  vec3 anchor = vec3(stationMatrix(*inst, r) * vec4(pos == ST_HANDLER ? r.asset->station.handler : r.asset->station.gunner, 1));
  anchor.y = crewFloor(terrain.height(anchor.x, anchor.z), anchor);
  if (Physics::capsuleBlocked(anchor + vec3(0, 0.1f, 0), Physics::standHeight() - 0.1f, Physics::radius())) { toast("Cannot take that position: it is obstructed."); return false; }
  occupied = id; choiceFor = 0;
  s->pos2 = pos; s->trans = 0; s->armed = false;
  if (pos == ST_HANDLER) s->status = s->deployed ? "Deployed - T for Aim, or W/S to start moving" : "Move";
  if (pos == ST_GUNNER && !s->deployed) { s->pos2 = ST_HANDLER; s->status = "Deploy the gun first (T)"; }
  Physics::setCharacterPos(anchor);
  return true;
}

void App::leaveStation() {
  if (!occupied) return;
  logf("station: left gun %llu (stage %s)", (unsigned long long)occupied, gCrumb ? gCrumb : "?");   // why a crew left (LAN tests)
  Instance* inst = doc.find(occupied); StationRT* s = station(occupied);
  if (inst && s) {
    auto& r = rt[occupied];
    mat4 M = stationMatrix(*inst, r);
    // valid dismount point: the position's anchor, else search around the carriage
    vec3 cands[] = {r.asset->station.gunner, r.asset->station.handler, vec3(-1.6f, 0, -0.5f), vec3(1.6f, 0, -0.5f), vec3(0, 0, -4.0f), vec3(-1.8f, 0, 1.0f), vec3(1.8f, 0, 1.0f)};
    for (auto& c : cands) {
      vec3 p = vec3(M * vec4(c, 1)); p.y = crewFloor(terrain.height(p.x, p.z), p);
      if (!Physics::capsuleBlocked(p + vec3(0, 0.1f, 0), Physics::standHeight() - 0.1f, Physics::radius())) { Physics::setCharacterPos(p); break; }
    }
    s->pos2 = ST_NONE; s->trans = 0; s->reload = 0; s->armed = false; s->fuse = -1; s->speed = 0; s->yawT = 0;
  }
  uint64_t was = occupied;
  occupied = 0;
  mpaiStationLeft(*this, was);
}

void App::switchStationPosition() {
  StationRT* s = station(occupied); if (!s) return;
  auto& r = rt[occupied];
  Instance* inst = doc.find(occupied);
  if (s->trans > 0) return;
  const bool can = cannonKind(r.asset->station.kind);
  if (s->pos2 == ST_GUNNER) { s->pos2 = ST_TO_MOVE; s->trans = can ? 0.5f : 1.2f; s->reload = 0; s->status = "Leaving the sight, lifting the trails..."; audio.play("gun_move", s->pos); }
  else if (s->pos2 == ST_HANDLER) {
    if (s->deployed) {   // to Aim
      vec3 a = vec3(stationMatrix(*inst, r) * vec4(r.asset->station.gunner, 1)); a.y = crewFloor(terrain.height(a.x, a.z), a);
      Physics::setCharacterPos(a);
      s->pos2 = ST_GUNNER; s->armed = false; s->status = "";
    } else {
      std::string why; vec3 p = s->pos;
      if (!carriageFits(*inst, r, p, s->carYaw, why)) { s->status = "Cannot deploy here: " + why; s->moveOk = false; return; }
      s->pos2 = ST_DEPLOYING; s->trans = can ? 0.5f : 2.0f; s->status = "Deploying: spreading trails, setting spades..."; audio.play("gun_move", s->pos);
    }
  }
}

// MG1: a small-arms shot: the round along dir, plus a shotgun's other pellets spread evenly over the cone's area
// (deterministic from seed, so host and replays agree). Only the first pellet carries the tracer.
// SND4: guns that eject brass (not launchers, grenades, flame, melee): the case lands 0.35-0.6 s after the shot, a bolt
// rifle's later (it comes out as the bolt opens), a shotgun's on the pump. Water: none; soft ground (terrain that is not
// a gravel / concrete path or stone / gravel / wood paint): quieter; buildings, props, vehicles, tunnels: full.
// Audio::casing limits the range (15 m), the rate per shooter and the number at once.
void App::shotCasing(const WeaponDef& d, vec3 feet, int shooter) {
  if (d.action != "auto" && d.action != "semi" && d.action != "bolt" && d.action != "heavy") return;
  if (!d.casings) return;   // MG12: the revolver keeps its cases, the sawn-off ejects them on reloading
  if (glm::length(feet - eyePos()) > 15.0f) return;   // before the ground query
  if (feet.y < waterAt(*this, feet.x, feet.z) - 0.05f) return;
  bool soft = true; Physics::Hit h;
  if (Physics::castRayEx(feet + vec3(0, 0.3f, 0), vec3(0, -1, 0), 1.0f, h) && h.ud != 0) soft = false;
  else {
    std::string ps = pathSurfaceAt(feet);
    extern const std::string& groundPaintStep(float, float); const std::string& gs = groundPaintStep(feet.x, feet.z);
    if (ps == "gravel" || ps == "concrete" || gs == "step_stone" || gs == "step_gravel" || gs == "step_wood") soft = false;
  }
  static uint32_t r = 977; r = r * 1664525u + 1013904223u;
  float delay = 0.35f + 0.25f * (r >> 8) / 16777216.0f + (d.action == "bolt" ? 0.25f : d.pellets > 1 ? 0.3f : 0.0f);
  audio.casing(feet, shooter, delay, soft);
}

void App::fireRounds(const WeaponDef& d, vec3 from, vec3 dir, int tracerKind, uint32_t seed, int owner) {
  vec3 rt = glm::normalize(glm::cross(dir, std::fabs(dir.y) > 0.99f ? vec3(1, 0, 0) : vec3(0, 1, 0))), up = glm::cross(rt, dir);
  float tc = std::tan(radians(d.spreadDeg));
  for (int k = 0; k < std::max(1, d.pellets); ++k) {
    vec3 v = dir;
    if (k > 0) {
      uint32_t h = (seed + 0x9E3779B9u * (uint32_t)k) * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
      float a = (h & 0xffff) / 65536.0f * 6.2831853f, r = std::sqrt(((h >> 16) & 0xffff) / 65536.0f) * tc;
      v = glm::normalize(dir + rt * std::cos(a) * r + up * std::sin(a) * r);
    }
    fireProjectile(from, v, d.velocity, PROJ_BULLET, 0, ~0ull, k == 0 ? tracerKind : 0, d.container);
    Projectile& p = projectiles.back(); p.mass = d.bulletMass * 0.001f; p.owner = owner; p.extraPellet = k > 0; p.mesh = d.projModel.get();
    if (d.blaster) { p.gravScale = 0; p.tracerKind = d.blaster; }   // SW1: a bolt flies straight, every pellet glows
  }
}

void App::fireProjectile(vec3 from, vec3 dir, float speed, int kind, float dmg, uint64_t ignore, int tracerKind, const std::string& ammo) {
  Projectile p; p.p = from; p.v = glm::normalize(dir) * speed; p.kind = kind; p.dmg = dmg; p.ignore = ignore; p.tracerKind = tracerKind; p.ammo = ammo;
  projectiles.push_back(p);
  ++shotsFired;
}

void App::stepProjectiles(float dt) {
  flame.step(*this, dt);   // FT1: the flame stream, ground fire (flame.cpp)
  const int sub = 2;
  float h = dt / sub;
  for (int sI = 0; sI < sub; ++sI)
    for (size_t k = 0; k < projectiles.size();) {
      Projectile& p = projectiles[k];
      vec3 seg = p.v * h;
      float len = glm::length(seg);
      Physics::Hit hit;
      bool impact = len > 1e-5f && Physics::castRayEx(p.p, seg, len, hit, p.ignore);   // continuous swept collision
      // AT guns: swept test of the segment against each gun's direct-hit volume (the gun is mostly open frame,
      // so its collision hull alone would let rockets pass between shield, cradle and trails)
      if (p.atKill && len > 1e-5f) {
        float tMax = impact ? hit.t : len;
        for (auto& gi : doc.instances) {
          auto gr = rt.find(gi.id);
          if (gr == rt.end() || !isAtGun(gr->second.asset) || gr->second.st.wrecked || (p.ignore >> 16) == gi.id) continue;
          const AssetDef* ga = gr->second.asset;
          mat4 GM = stationMatrix(gi, gr->second);
          vec3 c = vec3(GM * vec4((ga->gunHitMin + ga->gunHitMax) * 0.5f, 1));
          float tt;
          if (rayOBB(p.p, seg / len, c, (ga->gunHitMax - ga->gunHitMin) * 0.5f, glm::quat_cast(glm::mat3(GM)), tt) && tt >= 0 && tt <= tMax) {
            impact = true; tMax = tt; hit.t = tt; hit.pos = p.p + seg / len * tt; hit.normal = -seg / len; hit.ud = gi.id << 16;
          }
        }
      }
      {   // GM3: a shell striking a soldier bursts on him (the direct hit kills; the blast below is the sphere round it)
        extern bool mpaiShellHit(App&, const Projectile&, vec3, float, float&);
        float ts = impact ? hit.t : len;
        if ((p.kind == PROJ_HE || p.kind == PROJ_ROCKET) && mpaiShellHit(*this, p, seg, len, ts)) { impact = true; hit.t = ts; hit.pos = p.p + seg / len * ts; hit.normal = -seg / len; hit.ud = 0; }
      }
      auto arrowHit = [&](vec3 at, float vol) { if (p.tracerKind < 0 && audio.hasEvent("archer_impact")) audio.play("archer_impact", at, vol, 0.96f + 0.08f * fx.rnd()); };   // AR1: an archer's arrow (pf3's sound)
      if (mpaiProjectile(*this, p, seg, len, impact ? hit.t : len)) { arrowHit(p.p + seg * 0.5f, 1.0f); projectiles.erase(projectiles.begin() + k); continue; }   // hit a soldier
      vec3 end = impact ? hit.pos : p.p + seg;
      // visible travel feedback along the actual path
      if (p.energy) {   // MT2: the bolt: a long glowing core along its path and a light at its head
        const vec3 dn = glm::length(p.v) > 1e-3f ? glm::normalize(p.v) : vec3(0, 0, 1);
        fx.streak(end - dn * std::min(p.boltLen, p.travelled + glm::length(end - p.p)), end, p.boltW, 0.03f, p.boltCol);
        fx.streak(end - dn * p.boltLen * 0.6f, end, p.boltW * 2.2f, 0.03f, p.boltCol * 0.25f);
        fx.flash(end, p.boltW * 5.0f, 0.03f, p.boltCol * 0.3f);
      }
      else if (p.kind == PROJ_HE && !p.mesh) fx.streak(p.p, end, 0.045f, 0.05f, vec4(2.2f, 1.6f, 1.0f, 1));   // CAN1: a round shot is drawn instead (below)
      else if (p.kind == PROJ_ROCKET) { fx.streak(p.p, end, 0.06f, 0.06f, vec4(6.0f, 3.0f, 1.0f, 1)); if (fx.rnd() < 0.5f) fx.smoke(p.p, vec3(0, 0.2f, 0), 0.12f, 1.4f, vec4(0.62f, 0.6f, 0.57f, 0.45f), 1.4f, 0.2f); }
      else if (p.mesh) p.visTrav += glm::length(end - p.p);   // XB7: the bolt itself is drawn (projectileDrawItems)
      else { float L = glm::length(end - p.p); if (p.visTrav + L > p.trailSkip) { float u0 = p.visTrav < p.trailSkip && L > 0 ? (p.trailSkip - p.visTrav) / L : 0.0f;
        fx.bullet(glm::mix(p.p, end, u0) + p.visOff * visFade(p.visTrav + u0 * L), end + p.visOff * visFade(p.visTrav + L), p.tracerKind); } p.visTrav += L; }   // AT4: drawn from the muzzle onto the path
      if (impact && !p.energy && (p.kind == PROJ_HE || p.kind == PROJ_ROCKET) && shellThroughGlass(*this, p, hit.pos, hit.ud)) continue;
      auto energyHit = [&](vec3 at, vec3 n) {   // MT2: soldiers in the blast (owner rules as a shell's), a flash and sparks; the world is untouched
        fx.flash(at + n * 0.3f, p.boltW * 12.0f, 0.14f, p.boltCol * 1.2f);
        fx.blasterImpact(at, n, p.boltCol.x >= p.boltCol.z ? 4 : 3);
        extern bool mechBoltBlast(App&, const Projectile&, vec3);   // tank_mech.cpp: soldiers in a match
        if (!mechBoltBlast(*this, p, at)) blastPlayer(at, p.blastDmg, p.blastR);
        audio.play(audio.hasEvent("blaster_hit") ? "blaster_hit" : "explosion", at, 1.0f, audio.hasEvent("blaster_hit") ? 0.5f : 1.0f);
        ++energyHits;
      };   // T7: thin glass breaks, the shell flies on
      if (impact) {
        arrowHit(hit.pos, hit.ud == 0 || (hit.ud >> 60) == 0xF ? 0.6f : 1.0f);   // AR1: a miss into the ground thuds quieter than a wall / wood
        // tanks (tank_sim.cpp): the damage matrix decides; small arms ricochet, a launcher on the front armour makes a
        // visible non-penetrating impact with no blast (no splash leak); counted hits detonate on the armour (no ground crater)
        TankHitResult th = tanks.projectileHit(*this, p, hit.pos, hit.normal, hit.ud);
        if (th.tank) {
          if (p.energy) { energyHit(hit.pos, hit.normal); lastImpact = {hit.pos, "armour", p.kind, (float)glfwGetTime(), 0}; }
          else if (!th.consumed && p.kind != PROJ_BULLET) {   // detonation on the armour: the shared blast, but no crater under the tank
            blastAT = false; blastSplashGun = p.splashGun; blastPersonnel = p.kind == PROJ_ROCKET ? 1.6f : p.at ? 1.0f : 2.0f;
            auto ex = p.kind == PROJ_ROCKET ? explode(hit.pos, p.v, p.blastDmg, p.blastR, 0.0f, p.ignore, WALL_ROCKET)
                    : p.at ? explode(hit.pos, p.v, 320.0f, 0.9f, 0.0f, p.ignore, WALL_AP) : explode(hit.pos, p.v, p.dmg, 2.2f, 0.0f, p.ignore, p.cannonBall ? WALL_CANNON : WALL_HE);
            blastSplashGun = 40; blastPersonnel = 1;
            if (p.lightAT > 0) { fx.flash(hit.pos + hit.normal * 0.1f, 0.5f, 0.06f, vec4(9, 6, 3, 1)); fx.impact(hit.pos, hit.normal, "metal"); }   // VH8: a 25 mm round on armour
            else fx.explosion(hit.pos, hit.normal, "metal", false);
            if (p.kind == PROJ_HE && !p.cannon30) audio.play("shell_hit", hit.pos, 1.0f, 1.0f, glm::length(hit.pos - eyePos()) / 343.0f);   // SND9: the shell bursting, heard after the sound's travel time
            else audio.play("explosion", hit.pos, 1.0f);
            lastImpact = {hit.pos, "armour", p.kind, (float)glfwGetTime(), ex.breached};
          } else {
            if (p.kind == PROJ_BULLET) fx.impact(hit.pos, hit.normal, "metal");
            if (p.cannon30) { fx.impact(hit.pos, hit.normal, "metal"); fx.flash(hit.pos + hit.normal * 0.1f, 0.7f, 0.07f, vec4(9, 6, 3, 1)); audio.play("impact_metal", hit.pos, 0.7f); }   // VH5: 30 mm bursts on armour
            else if (p.kind == PROJ_HE) { fx.flash(hit.pos + hit.normal * 0.1f, 1.2f, 0.08f, vec4(9, 6, 3, 1)); audio.play("shell_hit", hit.pos, 1.0f, 1.0f, glm::length(hit.pos - eyePos()) / 343.0f); }   // SND9: a shell the armour stopped still bursts (it was silent)
            lastImpact = {hit.pos, "armour", p.kind, (float)glfwGetTime(), 0};
          }
          projectiles.erase(projectiles.begin() + k);
          continue;
        }
        std::string surface = "masonry";
        bool ground = hit.ud == 0 || (hit.ud >> 60) == 0xF;
        if (ground) { surface = "ground"; std::string ps = pathSurfaceAt(hit.pos); if (!ps.empty()) surface = ps; }   // gravel / dirt / concrete paths
        else if ((hit.ud & 0xFFFF) == 0xFFFF) {   // a wall body: report the struck panel's family
          uint64_t iid = hit.ud >> 16;
          for (auto& i : doc.instances) if (i.id == iid) {
            auto& r = rt[iid];
            vec3 lp = glm::inverse(yawQuat(i.yaw)) * (hit.pos - i.pos);
            float best = 1e9f;
            for (auto& pn : r.asset->panels) { float d = glm::length(glm::clamp(lp, pn.bmin, pn.bmax) - lp); if (d < best) { best = d; surface = pn.family; } }
          }
        } else if ((hit.ud & 0xFFFF) != 0) surface = "wood";
        if (p.energy) { energyHit(hit.pos, hit.normal); lastImpact = {hit.pos, surface, p.kind, (float)glfwGetTime(), 0}; }
        else if (p.kind == PROJ_HE || p.kind == PROJ_ROCKET) {
          // explicit direct hit on an AT gun (its own body, not merely inside the blast): one-shot destruction
          uint64_t hitInst = (hit.ud >> 60) == 0xF || hit.ud == 0 ? 0 : (hit.ud >> 16);
          if (p.atKill && hitInst) { auto hr = rt.find(hitInst); if (hr != rt.end() && isAtGun(hr->second.asset) && !hr->second.st.wrecked) { wreckStation(hitInst, p.kind == PROJ_ROCKET ? "rocket direct hit" : "shell direct hit"); ++directHits; } }
          blastAT = p.at;   // AT: concentrated wall damage, deep crater (ground.cpp)
          blastShell = p.kind == PROJ_HE && !p.cannon30;   // SND9: LAN clients hear "shell_hit" for it
          blastSplashGun = p.splashGun;
          blastPersonnel = p.kind == PROJ_ROCKET ? 1.6f : p.at ? 1.0f : 2.0f;   // HE against soldiers: launchers +60 %, tank / AT gun shells +100 %
          const float gk = 0.5f;   // tank / AT gun shells dig half the crater (walls unchanged)
          auto ex = p.kind == PROJ_ROCKET ? explode(hit.pos, p.v, p.blastDmg, p.blastR, p.craterScale, p.ignore, WALL_ROCKET)
                  : p.at ? explode(hit.pos, p.v, 320.0f, 0.9f, 1.0f * gk, p.ignore, WALL_AP) : explode(hit.pos, p.v, p.dmg, p.blastR > 0 ? p.blastR : 2.2f, (p.craterScale > 0 ? p.craterScale : 1.0f) * gk, p.ignore, p.cannonBall ? WALL_CANNON : WALL_HE);
          blastAT = false; blastSplashGun = 40; blastPersonnel = 1; blastShell = false;
          if (p.lightAT > 0) {   // VH8: a 25 mm round: a modest burst, not a shell's explosion
            fx.flash(hit.pos + hit.normal * 0.1f, 0.5f, 0.06f, vec4(9, 6, 3, 1)); fx.impact(hit.pos, hit.normal, surface == "ground" ? "ground" : surface);
            for (int k = 0; k < 2; ++k) fx.smoke(hit.pos + hit.normal * 0.2f, hit.normal * 0.6f + vec3(0, 0.4f, 0), 0.25f, 1.1f, vec4(0.42f, 0.4f, 0.37f, 0.45f), 1.1f, 0.3f);
          } else fx.explosion(hit.pos, hit.normal, surface == "ground" ? "ground" : surface, ground || ex.crater, p.kind == PROJ_ROCKET ? "rpg7_PG7V" : p.at || p.cannon30 ? "40mm_HEDP_M433" : nullptr, p.v, p.kind == PROJ_ROCKET || p.at || p.cannon30 ? -1.0f : 0.7f);   // fxscale: launcher warhead / AP or 30 mm / a 75 mm HE shell (~0.7 kg TNT-eq)
          if (p.kind == PROJ_HE && !p.cannon30) audio.play("shell_hit", hit.pos, 1.0f, 1.0f, glm::length(hit.pos - eyePos()) / 343.0f);   // SND9: tank / AT / field-gun shells, on anything (ground, walls, soldiers, big enemies)
          else audio.play(p.kind == PROJ_ROCKET ? "explosion_rocket" : p.at ? "explosion_big" : "explosion", hit.pos, p.cannon30 ? 0.35f : 1.0f);   // VH5: 10 rounds/s of 30 mm
          lastImpact = {hit.pos, surface, p.kind, (float)glfwGetTime(), ex.breached};
        } else {
          std::string surf2 = surface;
          bulletImpact(p, hit, surf2);   // glass / shutters / doors: damage, detach, maybe a penetrating continuation
          mpaiImpact(*this, p, hit.pos, hit.normal, hit.ud, hit.t, surface);   // LAN: the same impact on every client
          if (p.tracerKind >= 3) {   // SW1: a blaster bolt: scorch, sparks and its own hit sound (no dust, no ricochet)
            fx.blasterImpact(hit.pos, hit.normal, p.tracerKind);
            if (glm::length(hit.pos - eyePos()) < 120.0f) audio.play("blaster_hit", hit.pos, 0.5f);
          } else {
          fx.impact(hit.pos, hit.normal, surf2 == "concrete" || surf2 == "masonry" || surf2 == "gravel" ? "stone" : surf2 == "dirt" ? "ground" : surf2 == "wood_door" || surf2 == "wood_shutter" ? "wood" : surf2);
          {   // material impact sound at the contact
            std::string im = surf2 == "ground" || surf2 == "dirt" || surf2 == "gravel" || surf2 == "earth" ? "dirt" : surf2 == "steel" || surf2 == "metal" ? "metal"
                           : surf2 == "wood" || surf2 == "wood_door" || surf2 == "wood_shutter" || surf2 == "timber" ? "wood" : surf2 == "glass" ? "" : "concrete";
            if (!im.empty() && glm::length(hit.pos - eyePos()) < 120.0f) {
              if (audio.hasEvent("bullet_hit")) {   // SND4: the supplied bullet hit on every surface (glass keeps its own break), a few at a time, pitch varied
                static double recent[6] = {-9, -9, -9, -9, -9, -9}; double now = glfwGetTime(); int o = 0; for (int q = 1; q < 6; ++q) if (recent[q] < recent[o]) o = q;
                if (now - recent[o] >= 0.2) { recent[o] = now; static uint32_t rs = 777; rs = rs * 1664525u + 1013904223u; audio.play("bullet_hit", hit.pos, 0.6f, 0.9f + 0.2f * (rs >> 8) / 16777216.0f); }
              } else audio.play("impact_" + im, hit.pos, 0.5f);
            }
          }
          }
          lastImpact = {hit.pos, surface, p.kind, (float)glfwGetTime(), 0};
        }
        projectiles.erase(projectiles.begin() + k);
        continue;
      }
      p.p = end;
      p.v.y -= 9.81f * p.gravScale * h;
      p.life += h;
      if (p.life > 9.0f || p.p.y < -100) { projectiles.erase(projectiles.begin() + k); continue; }
      ++k;
    }
  for (auto& c : spawnedProjectiles) projectiles.push_back(c);
  spawnedProjectiles.clear();
}

void App::stepStation(float dt, const ExploreInput& in) {
  StationRT* s = station(occupied);
  if (!s) { occupied = 0; return; }
  if (s->wrecked) { logf("station: leave, gun wrecked"); leaveStation(); return; }
  Instance* inst = doc.find(occupied);
  auto& r = rt[occupied];
  const StationDef& d = r.asset->station;
  bool focused = glfwGetWindowAttrib(win, GLFW_FOCUSED) || selftest;
  bool fireHeld = in.fire && focused;
  if (!fireHeld) s->armed = true;             // fresh press required after any transition
  if (in.switchPos) switchStationPosition();
  if (s->trans > 0) {
    s->trans -= dt;
    if (s->trans <= 0) {
      s->trans = 0;
      if (s->pos2 == ST_TO_MOVE) { s->pos2 = ST_HANDLER; s->deployed = false; s->status = "Move: W/S push, A/D turn, T deploy"; }
      else if (s->pos2 == ST_DEPLOYING) { s->pos2 = ST_HANDLER; s->deployed = true; s->status = "Deployed - T for Aim"; }
    }
  }
  if (cannonKind(d.kind) && R.perf.cannons) cannonStep(*inst, r, dt, in);
  mat4 M = stationMatrix(*inst, r);
  if (s->pos2 == ST_GUNNER) {
    // bounded traverse/elevation in the deployed carriage frame
    s->yawT = std::clamp(s->yawT + in.dyaw, d.yawMin, d.yawMax);
    s->elevT = std::clamp(s->elevT + in.dpitch, d.elevMin, d.elevMax);
    s->atYawStop = (s->yawT <= d.yawMin + 1e-3f && in.dyaw < 0) || (s->yawT >= d.yawMax - 1e-3f && in.dyaw > 0) || s->yawT <= d.yawMin + 1e-3f || s->yawT >= d.yawMax - 1e-3f;
    s->atElevStop = s->elevT <= d.elevMin + 1e-3f || s->elevT >= d.elevMax - 1e-3f;
    bool mg = mgKind(d.kind), mg42 = d.kind == "mg42";
    if (mg) { s->yaw = s->yawT; s->elev = s->elevT; }   // a mounted MG swings as freely as the carried gun: no traverse / elevation rate
    else { s->yaw = approach(s->yaw, s->yawT, d.traverseRate * dt); s->elev = approach(s->elev, s->elevT, d.elevateRate * dt); }
    if (cannonKind(d.kind)) s->yaw = 0;   // CAN1: yawT is the carriage turn still to make (below)
    int full = mg ? std::max(1, d.loaded) : 1;   // one belt: M2HB 110, MG 42 50
    if (!mg && in.toggleAmmo && s->reserveATMax > 0) { s->selAT = !s->selAT; toast(d.label + ": next round " + (s->selAT ? d.atName : d.heName) + " - takes effect at the next reload", 3); }
    int pool = (!mg && s->selAT) ? s->reserveAT : s->reserve;
    const char* loadSnd = mg42 && audio.hasEvent("reload_mg42") ? "reload_mg42" : mg ? "belt" : "breech";   // the handheld MG 42's belt foley
    if ((in.reload || (cannonKind(d.kind) && s->fuse < 0)) && s->loaded < full && pool > 0 && s->reload <= 0 && (mg || s->loaded == 0)) { s->reload = d.reloadTime; audio.play(loadSnd, vec3(M[3])); }   // CAN1: the cannon's crew reloads by itself
    if (s->reload > 0) {
      s->reload -= dt;
      if (s->reload <= 0) { stationFinishReload(*s, d); audio.play(loadSnd, vec3(M[3])); }
    }
    s->cool = std::max(0.0f, s->cool - dt);
    if (mg && in.fireMode) { s->semi = !s->semi; s->armed = false; toast(std::string(mg42 ? "MG 42: " : "M2HB: ") + (s->semi ? "SEMI" : "AUTO"), 2); }
    if (mg) {
      if (fireHeld && s->armed && s->reload <= 0 && s->cool <= 0) {
        if (s->semi) s->armed = false;   // SEMI: one round per press
        if (s->loaded > 0) {
          stationShoot(occupied, -1);
          s->cool = 60.0f / (mg42 ? 1200.0f : 500.0f);
          // no recoil on a mount (the only difference from the carried MG): no view kick, no gun travel (stationParts)
        } else if (s->armed) { audio.play("click", vec3(M[3]), 0.6f); s->armed = false; }
      }
      if (!fireHeld) { vec3 td; if (s->burst > 1 && mg42) audio.play("tail_mg42", stationMuzzle(*inst, r, td), 0.9f); s->burst = 0; }   // recorded tail once per release, as handheld
      viewKickPitch *= 0.85f;
    }
    if (!mg && fireHeld && s->armed && s->deployed && s->trans <= 0) {
      s->armed = false;
      if (s->loaded > 0 && s->reload <= 0 && cannonKind(d.kind) && R.perf.cannons) { if (s->fuse < 0) { s->fuse = 0; audio.play("bolt", vec3(M[3]), 0.4f, 1.6f); } }   // CAN1: the match to the fuse
      else if (s->loaded > 0 && s->reload <= 0) stationShoot(occupied, -1);
      else audio.play("click", vec3(M[3]), 0.6f);
    }
    vec3 a = vec3(M * vec4(d.gunner, 1)); a.y = crewFloor(terrain.height(a.x, a.z), a);
    Physics::setCharacterPos(a);
  } else if (s->pos2 == ST_HANDLER && !s->deployed && s->trans <= 0 && !(cannonKind(d.kind) && R.perf.cannons)) {
    // reposition: slow, short, swept against structures and checked for ground support
    float turn = -in.move.x * 12.0f * dt, push = in.move.y * 0.7f * dt;
    if (turn != 0 || push != 0) {
      float ny = wrapDeg(s->carYaw + turn);
      vec3 np = s->pos + yawQuat(ny) * vec3(0, 0, 1) * push;   // push toward the muzzle; handler is at the trails
      std::string why;
      if (carriageFits(*inst, r, np, ny, why)) {
        s->pos = np; s->carYaw = ny; s->moveOk = true; s->status = "Ground OK";
        Physics::setTransform(r.staticBody, s->pos, yawQuat(s->carYaw));
      } else { s->moveOk = false; s->status = "Blocked: " + why; }
    }
    M = stationMatrix(*inst, r);
    vec3 a = vec3(M * vec4(d.handler, 1)); a.y = crewFloor(terrain.height(a.x, a.z), a);
    Physics::setCharacterPos(a);
  } else {
    vec3 a = vec3(M * vec4(s->pos2 == ST_GUNNER ? d.gunner : d.handler, 1)); a.y = crewFloor(terrain.height(a.x, a.z), a);
    Physics::setCharacterPos(a);
  }
  s->recoilT += dt;
}

// CAN1: a cannon's fuse and carriage. The fuse burns from its tip into the vent (a spark and a wisp along it), then the
// shot. The carriage rolls along its +z: pushed by the handler (W/S, slow to get going: it is heavy), turned by the handler
// (A/D) or by the gunner's aim (yawT: the turn still to make, at traverseRate), rolled back by the shot (s->speed < 0) and
// braked by the ground; each side's wheels turn by that side's travel (app.cpp draws them).
void App::cannonStep(const Instance& inst, InstanceRT& r, float dt, const ExploreInput& in) {
  StationRT* s = &r.st; const StationDef& d = r.asset->station;
  if (s->fuse >= 0) {
    s->fuse += dt;
    mat4 b, y, e, rc; stationParts(inst, r, b, y, e, rc);
    const vec3 sp = vec3(e * vec4(glm::mix(d.vent, d.fuseTip, std::max(0.0f, 1.0f - s->fuse / d.fuseTime)) - d.elevPivot, 1));
    fx.flash(sp, 0.07f, 0.04f, vec4(9, 5, 1.5f, 1));
    if (fx.rnd() < 0.4f) fx.smoke(sp, vec3(0, 0.3f, 0), 0.03f, 0.9f, vec4(0.72f, 0.7f, 0.66f, 0.5f), 1.8f, 0.3f);
    if (s->fuse >= d.fuseTime) { s->fuse = -1; if (s->loaded > 0) stationShoot(inst.id, -1); }
  }
  float tgt = 0, turn = 0;
  if (s->pos2 == ST_HANDLER && !s->deployed && s->trans <= 0) { tgt = in.move.y * d.pushSpeed; turn = -in.move.x * 9.0f * dt; }
  if (s->pos2 == ST_GUNNER) { turn = std::clamp(s->yawT, -d.traverseRate * dt, d.traverseRate * dt); s->yawT -= turn; }
  s->speed = cannonSpeed(s->speed, tgt, d.pushSpeed, dt);
  if (s->speed == 0 && turn == 0) return;
  const float ny = wrapDeg(s->carYaw + turn); vec3 np = s->pos + yawQuat(ny) * vec3(0, 0, 1) * (s->speed * dt);
  std::string why;
  if (!carriageFits(inst, r, np, ny, why)) { s->speed = 0; s->yawT = 0; s->moveOk = false; if (s->pos2 == ST_HANDLER) s->status = "Blocked: " + why; return; }
  const float half = 0.85f * d.moveHalf.x, da = radians(turn);   // a turn about the centre: the -x side runs forward for a positive yaw
  s->rollL += s->speed * dt + da * half; s->rollR += s->speed * dt - da * half;
  s->pos = np; s->carYaw = ny; s->moveOk = true; if (s->pos2 == ST_HANDLER) s->status = "Ground OK";
  Physics::setTransform(r.staticBody, s->pos, yawQuat(s->carYaw));
}

// one round: the projectile along the moving parts' aim, ammunition, recoil, flash and sound. owner -1 = the local gunner.
void App::stationShoot(uint64_t id, int owner) {
  Instance* inst = doc.find(id); auto& r = rt[id]; StationRT* s = &r.st;
  const StationDef& d = r.asset->station;
  bool mg42 = d.kind == "mg42";
  vec3 dir; vec3 muzzle = stationMuzzle(*inst, r, dir);
  mat4 b, y, e, rc; stationParts(*inst, r, b, y, e, rc);
  vec3 start; int tracer = 0;
  if (mgKind(d.kind)) {
    start = vec3(e * vec4(0, 0, 0, 1)); vec3 se, sf; float sfov;
    if (mg42 && mg42MountSight(*this, se, sf, sfov)) {   // the handheld's aimed fire: from the front post along the sight line, zeroed
      const WeaponDef& w = *weaponDefs[weaponIndex("mg42")];
      vec3 E = vec3(e * vec4(se, 1)), F = vec3(e * vec4(sf, 1)); Physics::Hit hf;
      start = Physics::castRayEx(E, F - E, glm::length(F - E), hf, coreUD(inst->id)) ? E + glm::normalize(F - E) * std::max(0.0f, hf.t - 0.01f) : F;
      float tz = w.zero / w.velocity; dir = glm::normalize(glm::normalize(F - E) + vec3(0, 0.5f * 9.81f * tz * tz / w.zero, 0));
    }
    tracer = tracerKindFor(3, s->shots + 1);   // MG 42 / M2HB: a red tracer every 3rd round, the others a short streak
    fireProjectile(start, dir, mg42 ? 755.0f : 890.0f, PROJ_BULLET, 0, coreUD(inst->id), tracer, d.ammo);
    s->burst++;
    fx.muzzleFlash(muzzle + dir * 0.03f, dir, mg42 ? 1.5f : 1.9f, false, muzzle, vec3(-1), mg42 ? "7.92x57 mm" : "12.7x99 mm");   // WP4
    audio.play(mg42 ? "shot_mg42" : "shot_m2hb", muzzle, 1.0f);
  } else {
    vec3 bore = d.muzzle - d.elevPivot;   // the bore axis at the trunnion plane (the Flak's bore sits above its trunnions)
    start = vec3(e * vec4(bore.x, bore.y, 0, 1));   // sweep from there along the bore: a wall at the muzzle stops the shell
    fireProjectile(start, dir, s->loadedAT ? d.atVelocity : d.heVelocity, PROJ_HE, d.heDamage, coreUD(inst->id), false, s->loadedAT ? d.ammoAT : d.ammo);
    projectiles.back().at = s->loadedAT; projectiles.back().atKill = true; projectiles.back().blastR = d.heRadius; projectiles.back().craterScale = d.heCrater;
    projectiles.back().gravScale = d.heGravity;
    projectiles.back().cannonBall = cannonKind(d.kind);   // CW4: its wall hits are WALL_CANNON (the only hit that breaches a rampart)
    if (cannonKind(d.kind) && R.perf.cannons) {   // CAN1: the ball drawn, the carriage rolled back recoil m, a black-powder cloud
      projectiles.back().mesh = d.ballModel.get(); s->speed = cannonRecoilSpeed(d.recoil, d.pushSpeed);
      for (int k = 0; k < 12; ++k) { vec3 j(fx.rnd() - 0.5f, fx.rnd() - 0.3f, fx.rnd() - 0.5f); fx.smoke(muzzle + dir * (0.2f + 0.3f * k), dir * (5.0f - 0.35f * k) + j * 1.2f, 0.35f + 0.08f * k, 5.0f + 3.0f * fx.rnd(), vec4(0.84f, 0.83f, 0.8f, 0.8f), 2.4f, 0.25f); }
      const vec3 vent = vec3(e * vec4(d.vent - d.elevPivot, 1)); fx.flash(vent, 0.25f, 0.06f, vec4(9, 6, 3, 1)); fx.smoke(vent, vec3(0, 1.2f, 0), 0.15f, 2.5f, vec4(0.8f, 0.79f, 0.76f, 0.6f), 2.0f, 0.5f);
    }
    fx.muzzle(muzzle, dir, d.muzzleScale, d.kind.c_str());   // fxev: pak40 / flak18 -> their weapons.json row (fxHeavyFor)
    audio.play(d.fireSound, muzzle, 1.0f);
  }
  projectiles.back().owner = owner;
  s->loaded -= 1; s->shots++; s->recoilT = 0;
  if (gFxRecoil && !mgKind(d.kind) && !cannonKind(d.kind) && fxGunFor(d.kind).platform == "towed") {   // fxrecoil (7.3): the spades dig in, a pair of trenches deepening with the shots
    const float dep = std::min(0.15f, 0.04f * (1.0f + 0.5f * (float)s->shots));
    const vec3 back = glm::normalize(vec3(b * vec4(0, 0, -1, 0)));
    for (float x : {-0.75f, 0.75f}) { const vec3 sp = vec3(b * vec4(x, 0, d.elevPivot.z - 2.4f, 1)); deformStamp(sp, sp + back * 0.4f, 0.18f, dep, 1.0f); }
  }
  mpaiStationShot(*this, id, start, dir, tracer, owner);
}

// a reload's end: moves rounds, never creates them. A partial MG belt goes back to the emplacement stock.
void App::stationFinishReload(StationRT& s, const StationDef& d) {
  s.reload = 0;
  if (mgKind(d.kind)) { int full = std::max(1, d.loaded); s.reserve += s.loaded; s.loaded = std::min(full, s.reserve); s.reserve -= s.loaded; }
  else if (s.selAT && s.reserveAT > 0) { s.loaded = 1; s.reserveAT -= 1; s.loadedAT = true; }
  else { s.loaded = 1; s.reserve -= 1; s.loadedAT = false; }
}

std::string App::crateTransaction(uint64_t crateId, WeaponState* who, int whoWeapon) {
  Instance* c = doc.find(crateId);
  if (!c || !crateStock.count(crateId)) return "Not a supply crate.";
  if ((who ? who->reload : (activeWeapon >= 0 && activeWeapon < (int)weaponDefs.size() ? wstate[weaponDefs[activeWeapon]->id].reload : 0.0f)) > 0) return "Finish reloading first.";   // GM1: no resupply mid-reload
  if (!who && mpaiCrateRequest(*this, crateId)) return "Asking the host for ammunition...";   // LAN client: the host moves the rounds
  auto& stock = crateStock[crateId];
  struct OnChange { App& a; uint64_t id; std::map<std::string, int> was; ~OnChange() { if (a.crateStock[id] != was) mpaiCrateChanged(a, id); } } onChange{*this, crateId, stock};   // LAN host: the stock replicates
  bool unlimited = crateUnlimited[crateId];
  float radius = assetFor(*c)->crate.value("serviceRadius", 6.0f);
  std::string msg;
  // 1) the held weapon, refilled to a full magazine plus a full reserve (weapons.cpp); guns in reach on the next press
  auto rounds = [&] { int n = 0; if (who) return who->loaded + who->reserveRounds(); for (auto& [id, ws] : wstate) n += ws.loaded + ws.reserveRounds(); return n; };
  int before = rounds();
  std::string personal = who ? resupplyWeapon(crateId, *weaponDefs[whoWeapon], *who) : resupplyPersonal(crateId);
  if (rounds() > before) return personal;
  // 2) emplacements in reach that take an ammunition type this crate holds
  for (auto& i : doc.instances) {
    StationRT* s = station(i.id);
    if (!s) continue;
    auto& r = rt[i.id];
    const std::string& ammo = r.asset->station.ammo;
    float dist = glm::length(vec2(s->pos.x - c->pos.x, s->pos.z - c->pos.z));
    // AT rounds (PaK 40) come from their own crate line into their own reserve
    const std::string& ammoAT = r.asset->station.ammoAT;
    std::string atMsg;
    if (!ammoAT.empty() && stock.count(ammoAT) && s->reserveAT < s->reserveATMax && glm::length(vec2(s->pos.x - c->pos.x, s->pos.z - c->pos.z)) <= radius) {
      int n = std::min(s->reserveATMax - s->reserveAT, unlimited ? 1 << 20 : stock[ammoAT]);
      if (n > 0) { s->reserveAT += n; if (!unlimited) stock[ammoAT] -= n; atMsg = " AT +" + std::to_string(n) + " (" + std::to_string(s->reserveAT) + "/" + std::to_string(s->reserveATMax) + ")."; }
    }
    if (!stock.count(ammo)) { if (!atMsg.empty()) { audio.play("crate", c->pos); return "PaK 40" + atMsg; } continue; }
    std::string name = r.asset->station.kind == "m2hb" ? "M2HB belts" : r.asset->station.kind == "mg42" ? "MG 42 belts" : r.asset->station.label + " reserve";
    if (dist > radius) { if (msg.empty()) msg = name + ": gun is out of reach (" + std::to_string((int)dist) + " m > " + std::to_string((int)radius) + " m)."; continue; }
    int unit = mgKind(r.asset->station.kind) ? std::max(1, r.asset->station.loaded) : 1;   // crate units: single 7.5 cm rounds, or one belt (.50 110, 7.92 mm 50)
    int need = (s->reserveMax - s->reserve) / unit;
    if (need <= 0) { if (!atMsg.empty()) { audio.play("crate", c->pos); return name + " HE full." + atMsg; } if (msg.empty()) msg = name + " already full (" + std::to_string(s->reserve) + "/" + std::to_string(s->reserveMax) + ")."; continue; }
    int have = unlimited ? need : stock[ammo];
    int n = std::min(need, have);
    if (n <= 0) { msg = name + ": crate has no " + ammo + " left."; continue; }
    s->reserve += n * unit;
    if (!unlimited) stock[ammo] -= n;
    audio.play("crate", c->pos);
    return name + " +" + std::to_string(n * unit) + " (now " + std::to_string(s->reserve) + "/" + std::to_string(s->reserveMax) + "). Crate " + ammo + ": " + (unlimited ? std::string("unlimited") : std::to_string(stock[ammo]) + " left") + ". Reload with R at the gun." + atMsg;
  }
  // nothing moved: the emplacement reason (out of reach, empty, full) leads, the personal refusal follows
  if (!msg.empty()) return personal.empty() ? msg : msg + "  " + personal;
  return personal.empty() ? "Nothing here fits your weapon or a gun in reach." : personal;
}

void App::drawCombatUI() {
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  if (choiceFor) {
    const char* t = "[1] Aim - gunner        [2] Move - handler        [E] cancel";
    ImVec2 ts = ImGui::CalcTextSize(t);
    dl->AddRectFilled(ImVec2(c.x - ts.x / 2 - 14, c.y + 40), ImVec2(c.x + ts.x / 2 + 14, c.y + 70), IM_COL32(15, 15, 12, 220), 5);
    dl->AddText(ImVec2(c.x - ts.x / 2, c.y + 47), IM_COL32(245, 235, 200, 255), t);
  }
  StationRT* s = station(occupied);
  if (!s) return;
  auto& r = rt[occupied];
  const StationDef& d = r.asset->station;
  vec3 ie, ifr; float ifov;
  bool irons = d.kind == "mg42" && mg42MountSight(*this, ie, ifr, ifov);   // aimed over the gun's own sights, as handheld: no dot
  if (s->pos2 == ST_GUNNER && (mgKind(d.kind) || cannonKind(d.kind)) && !irons) dl->AddCircleFilled(c, 2.5f, IM_COL32(240, 240, 230, 200));   // CAN1: the cannon's aim: the small white dot
  if (s->pos2 == ST_GUNNER && cannonKind(d.kind)) { char e[64]; std::snprintf(e, sizeof e, "Elevation %+.0f\xC2\xB0%s", s->elev, s->fuse >= 0 ? "   FUSE LIT" : ""); dl->AddText(ImVec2(c.x + 14, c.y + 12), IM_COL32(240, 240, 230, 220), e); }
  if (s->pos2 == ST_GUNNER && !mgKind(d.kind) && !cannonKind(d.kind)) {
    // TZF-style optic: the eyepiece, its soft black surround and the reticle are drawn by the renderer (render.cpp
    // eyepiece pass); the shell / traverse / elevation readout sits at the bottom right of the eyepiece
    float R = io.DisplaySize.y * kGunnerEyepieceR;
    const char* shell = s->loaded > 0 ? (s->loadedAT ? "AP" : "HE") : (s->selAT ? "AP (next)" : "HE (next)");
    char b[200];
    std::snprintf(b, sizeof b, "Shell %s  \xC2\xB7  Traverse %+.1f\xC2\xB0  \xC2\xB7  Elevation %+.0f mil", shell, s->yaw, s->elev * 6400.0f / 360.0f);   // German Strich: 6400 per turn
    float fs = std::max(16.0f, io.DisplaySize.y / 48.0f);
    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0, b);
    ImVec2 tp(c.x + R * 0.98f - ts.x, c.y + R * 0.80f);
    tp.x = std::min(tp.x, io.DisplaySize.x - ts.x - 20);
    dl->AddText(ImGui::GetFont(), fs, tp, IM_COL32(226, 200, 130, 235), b);
    bool blink = std::fmod(now, 0.6) < 0.35;
    if (s->atYawStop && blink) dl->AddText(ImVec2(c.x - 60, c.y - R + 30), IM_COL32(255, 120, 80, 255), "TRAVERSE STOP");
    if (s->atElevStop && blink) dl->AddText(ImVec2(c.x - 64, c.y - R + 48), IM_COL32(255, 120, 80, 255), "ELEVATION STOP");
  }
  // ammunition / position panel
  char b[256];
  const char* posName[] = {"", "GUNNER (Aim)", "PREPARING TO MOVE", "HANDLER (Move)", "DEPLOYING"};
  std::string cal = d.kind == "flak18" ? "8.8 cm" : "7.5 cm";
  std::string ammoName = cannonKind(d.kind) ? std::string("Round shot") : d.kind == "mg42" ? "7.92 mm belt" : d.kind != "m2hb" ?(s->loadedAT && s->loaded ? cal + " AT loaded" : cal + " HE") + " | next " + (s->selAT ? "AT" : "HE") + " (X) | AT reserve " + std::to_string(s->reserveAT) : ".50 belt";
  std::string state = s->reload > 0 ? "RELOADING " + std::to_string(s->reload).substr(0, 3) + " s" : s->loaded > 0 ? "READY" : "EMPTY - R to reload";
  std::snprintf(b, sizeof b, "%s | %s: %d loaded, %d reserve (max %d) | %s\nT switch position   E leave gun   %s", posName[s->pos2], ammoName.c_str(), s->loaded, s->reserve, s->reserveMax,
                s->pos2 == ST_GUNNER ? state.c_str() : (s->deployed ? "deployed" : "not deployed - no firing"), s->status.c_str());
  ImVec2 ts = ImGui::CalcTextSize(b);
  ImVec2 p(io.DisplaySize.x - ts.x - 30, io.DisplaySize.y - ts.y - 30);
  dl->AddRectFilled(ImVec2(p.x - 10, p.y - 8), ImVec2(p.x + ts.x + 10, p.y + ts.y + 8), IM_COL32(12, 12, 10, 200), 4);
  dl->AddText(p, s->moveOk ? IM_COL32(235, 225, 190, 255) : IM_COL32(255, 140, 110, 255), b);
  // deployment footprint while moving (gameplay indicator, not an editor overlay)
  if (s->pos2 == ST_HANDLER || s->pos2 == ST_DEPLOYING) {
    Instance* inst = doc.find(occupied);
    mat4 M = stationMatrix(*inst, r), vp = projM * viewM;
    vec3 loc[4] = {{-1.0f, 0.05f, 0.5f}, {1.0f, 0.05f, 0.5f}, {1.0f, 0.05f, -3.15f}, {-1.0f, 0.05f, -3.15f}};
    if (cannonKind(d.kind)) { const vec3 h = d.moveHalf; loc[0] = {-h.x, 0.05f, h.z}; loc[1] = {h.x, 0.05f, h.z}; loc[2] = {h.x, 0.05f, -h.z}; loc[3] = {-h.x, 0.05f, -h.z}; }
    ImVec2 sp[4]; bool ok = true;
    for (int k = 0; k < 4; ++k) {
      vec3 w = vec3(M * vec4(loc[k], 1)); w.y = terrain.height(w.x, w.z) + 0.05f;
      vec4 cc = vp * vec4(w, 1); if (cc.w < 0.1f) { ok = false; break; }
      sp[k] = ImVec2((cc.x / cc.w * 0.5f + 0.5f) * io.DisplaySize.x, (0.5f - cc.y / cc.w * 0.5f) * io.DisplaySize.y);
    }
    if (ok) for (int k = 0; k < 4; ++k) dl->AddLine(sp[k], sp[(k + 1) % 4], s->moveOk ? IM_COL32(90, 230, 120, 230) : IM_COL32(255, 80, 60, 240), 2.5f);
  }
}

// CAN1 --check-can1 (headless): both cannon assets parse as asked (elevation -20..+60, ten rounds, 5 s a round, a fuse, four
// wheels, a ball model); the ball lands inside a projectile's 9 s life at every elevation; the shot rolls the carriage back
// its recoil; a push takes 1.5-3 s to reach walking pace
int cannonSelfCheck() {
  int fails = 0; auto ok = [&](bool c, const std::string& m) { std::printf("[can1] %s %s\n", c ? "PASS" : "FAIL", m.c_str()); if (!c) ++fails; };
  for (const char* id : {"gun_cannon18", "gun_cannon_mortar"}) {
    json j; try { j = json::parse(readTextFile(contentDir() / "assets" / (std::string(id) + ".json"))); } catch (...) { ok(false, std::string(id) + " parses"); continue; }
    const json& st = j["station"]; const std::string n = id;
    ok(st.value("kind", "") == "cannon", n + " kind cannon");
    ok(st["elevation"][0].get<float>() == -20 && st["elevation"][1].get<float>() == 60, n + " elevation -20..+60");
    ok(st.value("loaded", 0) + st.value("reserve", 0) == 10 && st.value("reloadTime", 0.0f) == 5.0f, n + " ten rounds, 5 s each");
    const fs::path md = contentDir() / "assets" / "models";
    ok(fs::exists(md / st.value("ball", std::string("?")) / "mesh.bin"), n + " ball model baked");
    int wheels = 0, fuse = 0, tube = 0;
    try { json m = json::parse(readTextFile(md / j["model"]["dir"].get<std::string>() / "model.json"));
      for (auto& p : m["parts"]) { std::string pn = p.value("name", ""); wheels += pn.rfind("wheel_", 0) == 0; fuse += pn == "fuse"; tube += pn == "tube"; } } catch (...) {}
    ok(wheels == 4 && fuse == 1 && tube == 1, n + " model: 4 wheels, a fuse, a tube (" + std::to_string(wheels) + ", " + std::to_string(fuse) + ", " + std::to_string(tube) + ")");
    const json& he = st["shellHE"]; const float v0 = he.value("velocity", 0.0f), g = 9.81f * he.value("gravity", 1.0f), y0 = st["muzzle"][1].get<float>();
    float worst = 0, best = 0;
    for (float el = -20; el <= 60; el += 5) {   // the round flown as stepProjectiles flies it
      vec3 p(0, y0, 0), v(0, v0 * std::sin(radians(el)), v0 * std::cos(radians(el))); float t = 0;
      while (p.y > 0 && t < 30) { const float h = 1.0f / 120; p += v * h; v.y -= g * h; t += h; }
      worst = std::max(worst, t); best = std::max(best, p.z);
    }
    ok(worst < 9.0f, n + " lands inside the 9 s life (longest flight " + std::to_string(worst).substr(0, 4) + " s, range up to " + std::to_string((int)best) + " m)");
    const float rc = st.value("recoil", 0.0f), push = st.value("pushSpeed", 0.9f);
    float v = cannonRecoilSpeed(rc, push), x = 0; for (int k = 0; k < 600; ++k) { v = cannonSpeed(v, 0, push, 1.0f / 120); x += v / 120; }
    ok(std::fabs(-x - rc) < 0.05f * rc + 0.01f, n + " rolls back " + std::to_string(-x).substr(0, 4) + " m (recoil " + std::to_string(rc).substr(0, 4) + ")");
    v = 0; float t = 0; while (v < push - 1e-3f && t < 10) { v = cannonSpeed(v, push, push, 1.0f / 120); t += 1.0f / 120; }
    ok(t >= 1.5f && t <= 3.0f, n + " a push reaches walking pace in " + std::to_string(t).substr(0, 4) + " s");
  }
  std::printf("[can1] %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
  return fails ? 1 : 0;
}
