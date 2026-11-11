// Crew-served stations (PaK 40, M2HB), projectiles and ammunition crates.
// All state here is an Explore session overlay: F5 starts from authored defaults, returning discards it.
#include "app.h"
#include "crumb.h"
#include "mpai.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>

static uint64_t coreUD(uint64_t id) { return id << 16; }
// belt-fed machine-gun stations (M2HB tripod, MG 42 embrasure mount): continuous fire, belt reloads, no handler role
static bool mgKind(const std::string& k) { return k == "m2hb" || k == "mg42"; }
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
  return it->second.st.pos2 == ST_GUNNER && !mgKind(it->second.asset->station.kind);
}

mat4 App::stationMatrix(const Instance& i, const InstanceRT& r) const {
  vec3 p = r.st.active ? r.st.pos : i.pos; float y = r.st.active ? r.st.carYaw : i.yaw;
  return glm::translate(mat4(1), p) * glm::mat4_cast(yawQuat(y));
}

void App::stationParts(const Instance& i, const InstanceRT& r, mat4& base, mat4& yawM, mat4& elevM, mat4& recoilM) const {
  const StationDef& s = r.asset->station;
  base = stationMatrix(i, r);
  yawM = base * glm::translate(mat4(1), s.yawPivot) * glm::mat4_cast(glm::angleAxis(radians(r.st.yaw), vec3(0, 1, 0)));
  elevM = yawM * glm::translate(mat4(1), s.elevPivot - s.yawPivot) * glm::mat4_cast(glm::angleAxis(radians(-r.st.elev), vec3(1, 0, 0)));
  float t = r.st.recoilT, off = 0;
  if (t < 0.06f) off = s.recoil * (t / 0.06f);
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
    s->pos2 = ST_NONE; s->trans = 0; s->reload = 0; s->armed = false;
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
  if (s->pos2 == ST_GUNNER) { s->pos2 = ST_TO_MOVE; s->trans = 1.2f; s->reload = 0; s->status = "Leaving the sight, lifting the trails..."; audio.play("gun_move", s->pos); }
  else if (s->pos2 == ST_HANDLER) {
    if (s->deployed) {   // to Aim
      vec3 a = vec3(stationMatrix(*inst, r) * vec4(r.asset->station.gunner, 1)); a.y = crewFloor(terrain.height(a.x, a.z), a);
      Physics::setCharacterPos(a);
      s->pos2 = ST_GUNNER; s->armed = false; s->status = "";
    } else {
      std::string why; vec3 p = s->pos;
      if (!carriageFits(*inst, r, p, s->carYaw, why)) { s->status = "Cannot deploy here: " + why; s->moveOk = false; return; }
      s->pos2 = ST_DEPLOYING; s->trans = 2.0f; s->status = "Deploying: spreading trails, setting spades..."; audio.play("gun_move", s->pos);
    }
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
      if (mpaiProjectile(*this, p, seg, len, impact ? hit.t : len)) { projectiles.erase(projectiles.begin() + k); continue; }   // hit a soldier
      vec3 end = impact ? hit.pos : p.p + seg;
      // visible travel feedback along the actual path
      if (p.kind == PROJ_HE) fx.streak(p.p, end, 0.045f, 0.05f, vec4(2.2f, 1.6f, 1.0f, 1));
      else if (p.kind == PROJ_ROCKET) { fx.streak(p.p, end, 0.06f, 0.06f, vec4(6.0f, 3.0f, 1.0f, 1)); if (fx.rnd() < 0.5f) fx.smoke(p.p, vec3(0, 0.2f, 0), 0.12f, 1.4f, vec4(0.62f, 0.6f, 0.57f, 0.45f), 1.4f, 0.2f); }
      else fx.bullet(p.p, end, p.tracerKind);
      if (impact && (p.kind == PROJ_HE || p.kind == PROJ_ROCKET) && shellThroughGlass(*this, p, hit.pos, hit.ud)) continue;   // T7: thin glass breaks, the shell flies on
      if (impact) {
        // tanks (tank_sim.cpp): the damage matrix decides; small arms ricochet, a launcher on the front armour makes a
        // visible non-penetrating impact with no blast (no splash leak); counted hits detonate on the armour (no ground crater)
        TankHitResult th = tanks.projectileHit(*this, p, hit.pos, hit.normal, hit.ud);
        if (th.tank) {
          if (!th.consumed && p.kind != PROJ_BULLET) {   // detonation on the armour: the shared blast, but no crater under the tank
            blastAT = false; blastSplashGun = p.splashGun; blastPersonnel = p.kind == PROJ_ROCKET ? 1.6f : p.at ? 1.0f : 2.0f;
            auto ex = p.kind == PROJ_ROCKET ? explode(hit.pos, p.v, p.blastDmg, p.blastR, 0.0f, p.ignore, WALL_ROCKET)
                    : p.at ? explode(hit.pos, p.v, 320.0f, 0.9f, 0.0f, p.ignore, WALL_AP) : explode(hit.pos, p.v, p.dmg, 2.2f, 0.0f, p.ignore, WALL_HE);
            blastSplashGun = 40; blastPersonnel = 1;
            fx.explosion(hit.pos, hit.normal, "metal", false);
            audio.play("explosion", hit.pos, 1.0f);
            lastImpact = {hit.pos, "armour", p.kind, (float)glfwGetTime(), ex.breached};
          } else {
            if (p.kind == PROJ_BULLET) fx.impact(hit.pos, hit.normal, "metal");
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
        if (p.kind == PROJ_HE || p.kind == PROJ_ROCKET) {
          // explicit direct hit on an AT gun (its own body, not merely inside the blast): one-shot destruction
          uint64_t hitInst = (hit.ud >> 60) == 0xF || hit.ud == 0 ? 0 : (hit.ud >> 16);
          if (p.atKill && hitInst) { auto hr = rt.find(hitInst); if (hr != rt.end() && isAtGun(hr->second.asset) && !hr->second.st.wrecked) { wreckStation(hitInst, p.kind == PROJ_ROCKET ? "rocket direct hit" : "shell direct hit"); ++directHits; } }
          blastAT = p.at;   // AT: concentrated wall damage, deep crater (ground.cpp)
          blastSplashGun = p.splashGun;
          blastPersonnel = p.kind == PROJ_ROCKET ? 1.6f : p.at ? 1.0f : 2.0f;   // HE against soldiers: launchers +60 %, tank / AT gun shells +100 %
          const float gk = 0.5f;   // tank / AT gun shells dig half the crater (walls unchanged)
          auto ex = p.kind == PROJ_ROCKET ? explode(hit.pos, p.v, p.blastDmg, p.blastR, p.craterScale, p.ignore, WALL_ROCKET)
                  : p.at ? explode(hit.pos, p.v, 320.0f, 0.9f, 1.0f * gk, p.ignore, WALL_AP) : explode(hit.pos, p.v, p.dmg, p.blastR > 0 ? p.blastR : 2.2f, (p.craterScale > 0 ? p.craterScale : 1.0f) * gk, p.ignore, WALL_HE);
          blastAT = false; blastSplashGun = 40; blastPersonnel = 1;
          fx.explosion(hit.pos, hit.normal, surface == "ground" ? "ground" : surface, ground || ex.crater);
          audio.play(p.kind == PROJ_ROCKET ? "explosion_rocket" : p.at ? "explosion_big" : "explosion", hit.pos, 1.0f);
          lastImpact = {hit.pos, surface, p.kind, (float)glfwGetTime(), ex.breached};
        } else {
          std::string surf2 = surface;
          bulletImpact(p, hit, surf2);   // glass / shutters / doors: damage, detach, maybe a penetrating continuation
          mpaiImpact(*this, p, hit.pos, hit.normal, hit.ud, hit.t, surface);   // LAN: the same impact on every client
          fx.impact(hit.pos, hit.normal, surf2 == "concrete" || surf2 == "masonry" || surf2 == "gravel" ? "stone" : surf2 == "dirt" ? "ground" : surf2 == "wood_door" || surf2 == "wood_shutter" ? "wood" : surf2);
          {   // material impact sound at the contact
            std::string im = surf2 == "ground" || surf2 == "dirt" || surf2 == "gravel" || surf2 == "earth" ? "dirt" : surf2 == "steel" || surf2 == "metal" ? "metal"
                           : surf2 == "wood" || surf2 == "wood_door" || surf2 == "wood_shutter" || surf2 == "timber" ? "wood" : surf2 == "glass" ? "" : "concrete";
            if (!im.empty() && glm::length(hit.pos - eyePos()) < 120.0f) audio.play("impact_" + im, hit.pos, 0.5f);
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
  mat4 M = stationMatrix(*inst, r);
  if (s->pos2 == ST_GUNNER) {
    // bounded traverse/elevation in the deployed carriage frame
    s->yawT = std::clamp(s->yawT + in.dyaw, d.yawMin, d.yawMax);
    s->elevT = std::clamp(s->elevT + in.dpitch, d.elevMin, d.elevMax);
    s->atYawStop = (s->yawT <= d.yawMin + 1e-3f && in.dyaw < 0) || (s->yawT >= d.yawMax - 1e-3f && in.dyaw > 0) || s->yawT <= d.yawMin + 1e-3f || s->yawT >= d.yawMax - 1e-3f;
    s->atElevStop = s->elevT <= d.elevMin + 1e-3f || s->elevT >= d.elevMax - 1e-3f;
    s->yaw = approach(s->yaw, s->yawT, d.traverseRate * dt);
    s->elev = approach(s->elev, s->elevT, d.elevateRate * dt);
    bool mg = mgKind(d.kind), mg42 = d.kind == "mg42";
    int full = mg ? std::max(1, d.loaded) : 1;   // one belt: M2HB 110, MG 42 50
    if (!mg && in.toggleAmmo && s->reserveATMax > 0) { s->selAT = !s->selAT; toast(d.label + ": next round " + (s->selAT ? d.atName : d.heName) + " - takes effect at the next reload", 3); }
    int pool = (!mg && s->selAT) ? s->reserveAT : s->reserve;
    const char* loadSnd = mg42 && audio.hasEvent("reload_mg42") ? "reload_mg42" : mg ? "belt" : "breech";   // the handheld MG 42's belt foley
    if (in.reload && s->loaded < full && pool > 0 && s->reload <= 0 && (mg || s->loaded == 0)) { s->reload = d.reloadTime; audio.play(loadSnd, vec3(M[3])); }
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
          viewKickPitch = 0.35f;   // felt through the mount, not the shoulder
        } else if (s->armed) { audio.play("click", vec3(M[3]), 0.6f); s->armed = false; }
      }
      if (!fireHeld) { vec3 td; if (s->burst > 1 && mg42) audio.play("tail_mg42", stationMuzzle(*inst, r, td), 0.9f); s->burst = 0; }   // recorded tail once per release, as handheld
      viewKickPitch *= 0.85f;
    }
    if (!mg && fireHeld && s->armed && s->deployed && s->trans <= 0) {
      s->armed = false;
      if (s->loaded > 0 && s->reload <= 0) stationShoot(occupied, -1);
      else audio.play("click", vec3(M[3]), 0.6f);
    }
    vec3 a = vec3(M * vec4(d.gunner, 1)); a.y = crewFloor(terrain.height(a.x, a.z), a);
    Physics::setCharacterPos(a);
  } else if (s->pos2 == ST_HANDLER && !s->deployed && s->trans <= 0) {
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
    fx.flash(muzzle + dir * 0.08f, mg42 ? 0.18f : 0.25f, 0.04f, vec4(9, 6, 2.5f, 1));
    audio.play(mg42 ? "shot_mg42" : "shot_m2hb", muzzle, 1.0f);
  } else {
    vec3 bore = d.muzzle - d.elevPivot;   // the bore axis at the trunnion plane (the Flak's bore sits above its trunnions)
    start = vec3(e * vec4(bore.x, bore.y, 0, 1));   // sweep from there along the bore: a wall at the muzzle stops the shell
    fireProjectile(start, dir, s->loadedAT ? d.atVelocity : d.heVelocity, PROJ_HE, d.heDamage, coreUD(inst->id), false, s->loadedAT ? d.ammoAT : d.ammo);
    projectiles.back().at = s->loadedAT; projectiles.back().atKill = true; projectiles.back().blastR = d.heRadius; projectiles.back().craterScale = d.heCrater;
    fx.muzzle(muzzle, dir, d.muzzleScale);
    audio.play(d.fireSound, muzzle, 1.0f);
  }
  projectiles.back().owner = owner;
  s->loaded -= 1; s->shots++; s->recoilT = 0;
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
  if (s->pos2 == ST_GUNNER && mgKind(d.kind) && !irons) dl->AddCircleFilled(c, 2.5f, IM_COL32(240, 240, 230, 200));
  if (s->pos2 == ST_GUNNER && !mgKind(d.kind)) {
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
  std::string ammoName = d.kind == "mg42" ? "7.92 mm belt" : d.kind != "m2hb" ?(s->loadedAT && s->loaded ? cal + " AT loaded" : cal + " HE") + " | next " + (s->selAT ? "AT" : "HE") + " (X) | AT reserve " + std::to_string(s->reserveAT) : ".50 belt";
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
    ImVec2 sp[4]; bool ok = true;
    for (int k = 0; k < 4; ++k) {
      vec3 w = vec3(M * vec4(loc[k], 1)); w.y = terrain.height(w.x, w.z) + 0.05f;
      vec4 cc = vp * vec4(w, 1); if (cc.w < 0.1f) { ok = false; break; }
      sp[k] = ImVec2((cc.x / cc.w * 0.5f + 0.5f) * io.DisplaySize.x, (0.5f - cc.y / cc.w * 0.5f) * io.DisplaySize.y);
    }
    if (ok) for (int k = 0; k < 4; ++k) dl->AddLine(sp[k], sp[(k + 1) % 4], s->moveOk ? IM_COL32(90, 230, 120, 230) : IM_COL32(255, 80, 60, 240), 2.5f);
  }
}
