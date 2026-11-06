// Crew-served stations (PaK 40, M2HB), projectiles and ammunition crates.
// All state here is an Explore session overlay: F5 starts from authored defaults, returning discards it.
#include "app.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>

static uint64_t coreUD(uint64_t id) { return id << 16; }
static float approach(float v, float t, float rate) { float d = t - v; float s = rate; return std::fabs(d) <= s ? t : v + (d > 0 ? s : -s); }

StationRT* App::station(uint64_t id) {
  auto it = rt.find(id);
  if (it == rt.end() || it->second.asset->station.kind.empty()) return nullptr;
  return &it->second.st;
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
  eye = vec3(e * vec4(s.optic - s.elevPivot, 1));
  fwd = glm::normalize(vec3(e * vec4(0, 0, 1, 0)));
  fov = s.sightFov;   // PaK 40 ZF 3x8: 8 deg real field fills the eyepiece circle (9); others from the asset
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
  occupied = 0; choiceFor = 0;
  crateStock.clear(); crateUnlimited.clear();
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end()) continue;
    auto& r = it->second;
    const AssetDef* a = r.asset;
    if (!a->station.kind.empty()) resetStation(i);
    if (a->crate.is_object() && !a->crate.empty()) {
      json o = i.extra.contains("crate") ? i.extra["crate"] : json::object();
      json sup = o.contains("supplies") ? o["supplies"] : a->crate.value("supplies", json::object());
      for (auto& [k, v] : sup.items()) crateStock[i.id][k] = v.get<int>();
      crateUnlimited[i.id] = o.value("unlimited", a->crate.value("unlimited", false));
    }
  }
}

bool App::occupyStation(uint64_t id, int pos) {
  Instance* inst = doc.find(id); StationRT* s = station(id);
  if (!inst || !s) return false;
  if (s->wrecked) { toast("The gun is destroyed."); return false; }
  if (occupied && occupied != id) { toast("You are already crewing another gun."); return false; }
  if (pos == ST_HANDLER && rt[id].asset->station.kind == "m2hb") { toast("The M2HB on its tripod is crewed from the spade grips only (Aim)."); return false; }
  if (pos == ST_HANDLER && rt[id].asset->station.fixed) { toast(rt[id].asset->station.label + ": a dug-in emplacement - crew it from the gunner's position (Aim); it is not moved."); return false; }
  auto& r = rt[id];
  vec3 anchor = vec3(stationMatrix(*inst, r) * vec4(pos == ST_HANDLER ? r.asset->station.handler : r.asset->station.gunner, 1));
  anchor.y = terrain.height(anchor.x, anchor.z);
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
  Instance* inst = doc.find(occupied); StationRT* s = station(occupied);
  if (inst && s) {
    auto& r = rt[occupied];
    mat4 M = stationMatrix(*inst, r);
    // valid dismount point: the position's anchor, else search around the carriage
    vec3 cands[] = {r.asset->station.gunner, r.asset->station.handler, vec3(-1.6f, 0, -0.5f), vec3(1.6f, 0, -0.5f), vec3(0, 0, -4.0f), vec3(-1.8f, 0, 1.0f), vec3(1.8f, 0, 1.0f)};
    for (auto& c : cands) {
      vec3 p = vec3(M * vec4(c, 1)); p.y = terrain.height(p.x, p.z);
      if (!Physics::capsuleBlocked(p + vec3(0, 0.1f, 0), Physics::standHeight() - 0.1f, Physics::radius())) { Physics::setCharacterPos(p); break; }
    }
    s->pos2 = ST_NONE; s->trans = 0; s->reload = 0; s->armed = false;
  }
  occupied = 0;
}

void App::switchStationPosition() {
  StationRT* s = station(occupied); if (!s) return;
  auto& r = rt[occupied];
  Instance* inst = doc.find(occupied);
  if (s->trans > 0) return;
  if (s->pos2 == ST_GUNNER) { s->pos2 = ST_TO_MOVE; s->trans = 1.2f; s->reload = 0; s->status = "Leaving the sight, lifting the trails..."; audio.play("gun_move", s->pos); }
  else if (s->pos2 == ST_HANDLER) {
    if (s->deployed) {   // to Aim
      vec3 a = vec3(stationMatrix(*inst, r) * vec4(r.asset->station.gunner, 1)); a.y = terrain.height(a.x, a.z);
      Physics::setCharacterPos(a);
      s->pos2 = ST_GUNNER; s->armed = false; s->status = "";
    } else {
      std::string why; vec3 p = s->pos;
      if (!carriageFits(*inst, r, p, s->carYaw, why)) { s->status = "Cannot deploy here: " + why; s->moveOk = false; return; }
      s->pos2 = ST_DEPLOYING; s->trans = 2.0f; s->status = "Deploying: spreading trails, setting spades..."; audio.play("gun_move", s->pos);
    }
  }
}

void App::fireProjectile(vec3 from, vec3 dir, float speed, int kind, float dmg, uint64_t ignore, bool tracer, const std::string& ammo) {
  Projectile p; p.p = from; p.v = glm::normalize(dir) * speed; p.kind = kind; p.dmg = dmg; p.ignore = ignore; p.tracer = tracer; p.ammo = ammo;
  projectiles.push_back(p);
  ++shotsFired;
}

void App::stepProjectiles(float dt) {
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
      vec3 end = impact ? hit.pos : p.p + seg;
      // visible travel feedback along the actual path
      if (p.kind == PROJ_HE) fx.streak(p.p, end, 0.045f, 0.05f, vec4(2.2f, 1.6f, 1.0f, 1));
      else if (p.kind == PROJ_ROCKET) { fx.streak(p.p, end, 0.06f, 0.06f, vec4(6.0f, 3.0f, 1.0f, 1)); if (fx.rnd() < 0.5f) fx.smoke(p.p, vec3(0, 0.2f, 0), 0.12f, 1.4f, vec4(0.62f, 0.6f, 0.57f, 0.45f), 1.4f, 0.2f); }
      else if (p.tracer) fx.streak(p.p, end, 0.025f, 0.06f, vec4(9.0f, 2.2f, 0.8f, 1));
      else fx.streak(p.p, end, fx.rangeMode ? 0.012f : 0.007f, fx.rangeMode ? 0.05f : 0.025f, fx.rangeMode ? vec4(1.6f, 1.5f, 1.3f, 1) : vec4(0.7f, 0.68f, 0.62f, 1));
      if (impact) {
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
          auto ex = p.kind == PROJ_ROCKET ? explode(hit.pos, p.v, p.blastDmg, p.blastR, p.craterScale, p.ignore, WALL_ROCKET)
                  : p.at ? explode(hit.pos, p.v, 320.0f, 0.9f, 1.0f, p.ignore, WALL_AP) : explode(hit.pos, p.v, p.dmg, p.blastR > 0 ? p.blastR : 2.2f, p.craterScale > 0 ? p.craterScale : 1.0f, p.ignore, WALL_HE);
          blastAT = false; blastSplashGun = 40;
          fx.explosion(hit.pos, hit.normal, surface == "ground" ? "ground" : surface, ground || ex.crater);
          audio.play(p.kind == PROJ_ROCKET ? "explosion_rocket" : p.at ? "explosion_big" : "explosion", hit.pos, 1.0f);
          lastImpact = {hit.pos, surface, p.kind, (float)glfwGetTime(), ex.breached};
        } else {
          std::string surf2 = surface;
          bulletImpact(p, hit, surf2);   // glass / shutters / doors: damage, detach, maybe a penetrating continuation
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
  if (s->wrecked) { leaveStation(); return; }
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
    bool mg = d.kind == "m2hb";
    int full = mg ? 110 : 1;
    if (!mg && in.toggleAmmo && s->reserveATMax > 0) { s->selAT = !s->selAT; toast(d.label + ": next round " + (s->selAT ? d.atName : d.heName) + " - takes effect at the next reload", 3); }
    int pool = (!mg && s->selAT) ? s->reserveAT : s->reserve;
    if (in.reload && s->loaded < full && pool > 0 && s->reload <= 0 && (mg || s->loaded == 0)) { s->reload = d.reloadTime; audio.play(mg ? "belt" : "breech", vec3(M[3])); }
    if (s->reload > 0) {
      s->reload -= dt;
      if (s->reload <= 0) {   // moves rounds; never creates them. A partial .50 belt goes back to the emplacement stock.
        s->reload = 0;
        if (mg) { s->reserve += s->loaded; s->loaded = std::min(full, s->reserve); s->reserve -= s->loaded; }
        else if (s->selAT && s->reserveAT > 0) { s->loaded = 1; s->reserveAT -= 1; s->loadedAT = true; }
        else { s->loaded = 1; s->reserve -= 1; s->loadedAT = false; }
        audio.play(mg ? "belt" : "breech", vec3(M[3]));
      }
    }
    s->cool = std::max(0.0f, s->cool - dt);
    if (mg && in.fireMode) { s->semi = !s->semi; s->armed = false; toast(std::string("M2HB: ") + (s->semi ? "SEMI" : "AUTO"), 2); }
    if (mg) {
      if (fireHeld && s->armed && s->reload <= 0 && s->cool <= 0) {
        if (s->semi) s->armed = false;   // SEMI: one round per press
        if (s->loaded > 0) {
          vec3 dir; vec3 muzzle = stationMuzzle(*inst, r, dir);
          mat4 b, y, e, rc; stationParts(*inst, r, b, y, e, rc);
          fireProjectile(vec3(e * vec4(0, 0, 0, 1)), dir, 890.0f, PROJ_BULLET, 0, coreUD(inst->id), (s->shots % 5) == 0, d.ammo);
          s->loaded -= 1; s->shots++; s->cool = 60.0f / 500.0f; s->recoilT = 0;
          fx.flash(muzzle + dir * 0.08f, 0.25f, 0.04f, vec4(9, 6, 2.5f, 1));
          audio.play("shot_m2hb", muzzle, 1.0f);
          viewKickPitch = 0.35f;   // felt through the mount, not the shoulder
        } else if (s->armed) { audio.play("click", vec3(M[3]), 0.6f); s->armed = false; }
      }
      viewKickPitch *= 0.85f;
    }
    if (!mg && fireHeld && s->armed && s->deployed && s->trans <= 0) {
      s->armed = false;
      if (s->loaded > 0 && s->reload <= 0) {
        vec3 dir; vec3 muzzle = stationMuzzle(*inst, r, dir);
        mat4 b, y, e, rc; stationParts(*inst, r, b, y, e, rc);
        vec3 bore = d.muzzle - d.elevPivot;   // the bore axis at the trunnion plane (the Flak's bore sits above its trunnions)
        vec3 start = vec3(e * vec4(bore.x, bore.y, 0, 1));   // sweep from there along the bore: a wall at the muzzle stops the shell
        fireProjectile(start, dir, s->loadedAT ? d.atVelocity : d.heVelocity, PROJ_HE, d.heDamage, coreUD(inst->id), false, s->loadedAT ? d.ammoAT : d.ammo);
        if (!projectiles.empty()) { projectiles.back().at = s->loadedAT; projectiles.back().atKill = true; projectiles.back().blastR = d.heRadius; projectiles.back().craterScale = d.heCrater; }
        s->loaded -= 1; s->recoilT = 0; s->shots++;
        fx.muzzle(muzzle, dir, d.muzzleScale);
        audio.play(d.fireSound, muzzle, 1.0f);
      } else audio.play("click", vec3(M[3]), 0.6f);
    }
    vec3 a = vec3(M * vec4(d.gunner, 1)); a.y = terrain.height(a.x, a.z);
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
    vec3 a = vec3(M * vec4(d.handler, 1)); a.y = terrain.height(a.x, a.z);
    Physics::setCharacterPos(a);
  } else {
    vec3 a = vec3(M * vec4(s->pos2 == ST_GUNNER ? d.gunner : d.handler, 1)); a.y = terrain.height(a.x, a.z);
    Physics::setCharacterPos(a);
  }
  s->recoilT += dt;
}

std::string App::crateTransaction(uint64_t crateId) {
  Instance* c = doc.find(crateId);
  if (!c || !crateStock.count(crateId)) return "Not a supply crate.";
  auto& stock = crateStock[crateId];
  bool unlimited = crateUnlimited[crateId];
  float radius = assetFor(*c)->crate.value("serviceRadius", 6.0f);
  std::string msg;
  // 1) emplacements in reach that take an ammunition type this crate holds
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
    std::string name = r.asset->station.kind == "m2hb" ? "M2HB belts" : r.asset->station.label + " reserve";
    if (dist > radius) { if (msg.empty()) msg = name + ": gun is out of reach (" + std::to_string((int)dist) + " m > " + std::to_string((int)radius) + " m)."; continue; }
    int unit = r.asset->station.kind == "m2hb" ? 110 : 1;   // crate units: single 7.5 cm rounds, or 110-round .50 link increments
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
  // 2) personal weapon reserve (weapons.cpp)
  int before = 0; for (auto& [id, ws] : wstate) before += ws.reserveRounds();
  std::string personal = resupplyPersonal(crateId);
  int after = 0; for (auto& [id, ws] : wstate) after += ws.reserveRounds();
  if (after > before) return personal;                       // personal resupply happened
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
  if (s->pos2 == ST_GUNNER && d.kind == "m2hb") dl->AddCircleFilled(c, 2.5f, IM_COL32(240, 240, 230, 200));
  if (s->pos2 == ST_GUNNER && d.kind != "m2hb") {
    // eyepiece: dark surround, ZF 3x8 style reticle (central chevron, flanking chevrons, stadia)
    float R = io.DisplaySize.y * 0.44f;
    for (int k = 0; k < 64; ++k) {
      float a0 = k * 6.2831853f / 64, a1 = (k + 1) * 6.2831853f / 64;
      ImVec2 p0(c.x + std::cos(a0) * R, c.y + std::sin(a0) * R), p1(c.x + std::cos(a1) * R, c.y + std::sin(a1) * R);
      ImVec2 q0(c.x + std::cos(a0) * io.DisplaySize.x, c.y + std::sin(a0) * io.DisplaySize.x), q1(c.x + std::cos(a1) * io.DisplaySize.x, c.y + std::sin(a1) * io.DisplaySize.x);
      dl->AddQuadFilled(p0, q0, q1, p1, IM_COL32(4, 4, 3, 255));
    }
    dl->AddCircle(c, R, IM_COL32(20, 20, 18, 255), 96, 6);
    ImU32 rc = IM_COL32(15, 12, 8, 235);
    float u = R / 4.0f;   // one stadia unit (~1 deg at this field)
    auto chev = [&](float x, float y, float sz) {
      dl->AddLine(ImVec2(x - sz, y + sz * 1.4f), ImVec2(x, y), rc, 2.0f);
      dl->AddLine(ImVec2(x, y), ImVec2(x + sz, y + sz * 1.4f), rc, 2.0f);
    };
    chev(c.x, c.y, u * 0.45f);
    for (int k = 1; k <= 3; ++k) { chev(c.x - k * u, c.y, u * 0.28f); chev(c.x + k * u, c.y, u * 0.28f); }
    dl->AddLine(ImVec2(c.x - R, c.y + u * 0.7f), ImVec2(c.x - 3.3f * u, c.y + u * 0.7f), rc, 1.5f);
    dl->AddLine(ImVec2(c.x + 3.3f * u, c.y + u * 0.7f), ImVec2(c.x + R, c.y + u * 0.7f), rc, 1.5f);
    for (int k = 1; k <= 3; ++k) dl->AddLine(ImVec2(c.x - 4, c.y + k * u), ImVec2(c.x + 4, c.y + k * u), rc, 1.5f);
    char b[200];
    std::snprintf(b, sizeof b, "TRAVERSE %+5.1f (%.1f/%.1f)   ELEVATION %+5.1f (%.0f/%.0f)", s->yaw, d.yawMin, d.yawMax, s->elev, d.elevMin, d.elevMax);
    dl->AddText(ImVec2(c.x - R + 20, c.y + R - 44), IM_COL32(220, 210, 170, 255), b);
    bool blink = std::fmod(now, 0.6) < 0.35;
    if (s->atYawStop && blink) dl->AddText(ImVec2(c.x - 60, c.y - R + 30), IM_COL32(255, 120, 80, 255), "TRAVERSE STOP");
    if (s->atElevStop && blink) dl->AddText(ImVec2(c.x - 64, c.y - R + 48), IM_COL32(255, 120, 80, 255), "ELEVATION STOP");
  }
  // ammunition / position panel
  char b[256];
  const char* posName[] = {"", "GUNNER (Aim)", "PREPARING TO MOVE", "HANDLER (Move)", "DEPLOYING"};
  std::string cal = d.kind == "flak18" ? "8.8 cm" : "7.5 cm";
  std::string ammoName = d.kind != "m2hb" ? (s->loadedAT && s->loaded ? cal + " AT loaded" : cal + " HE") + " | next " + (s->selAT ? "AT" : "HE") + " (X) | AT reserve " + std::to_string(s->reserveAT) : ".50 belt";
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
