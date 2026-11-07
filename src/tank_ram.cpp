// Tanks running into things (docs/tanks.md#ramming). One energy rule for everything in the way:
//   * every obstacle needs a break energy E (content/vehicles/ramming.json: props by asset id, wall bays per HP point
//     of their family, trees by trunk size);
//   * on contact the tank brings its kinetic energy  K = 1/2 M v^2;  while it keeps pushing (throttle held) the
//     engine adds  P = engine kW x push efficiency x |throttle|  every second;
//   * an obstacle gives way once the energy put into it reaches E; the tank keeps  v' = sqrt(v^2 - 2 E_left / M)
//     and then shares momentum with what it knocks over (v'' = v' M / (M + m)), so a fast tank barely notices a
//     telegraph pole while a slow one loses a good part of its speed; when K is not enough the tank stops against
//     the obstacle and the energy it did put in is kept (pushing on finishes the job: "hold W longer");
//   * concrete (bunkers), other tanks, guns, tank traps, sandbags, hedgerow banks and iron doors never give way.
// Knocked-over props fall as rigid bodies (instantly, from the hit side), settle, and are crushed flat when a tank
// drives over them; walls are pushed through with the shared impact-centred wall damage (wallfield.cpp), so the
// breach is ordinary wall damage (Apply / undo like any other). Props and walls reset with the Explore session.
#include "app.h"
#include <algorithm>

namespace {
constexpr uint64_t kDebrisLow = 0xFFF0u;   // loose debris (shared with detached shutters: "wood" surface for bullets)
uint64_t bodyUD(uint64_t id) { return id << 16; }
}

void TankSys::loadRam(App& app) {
  (void)app;
  ramProps.clear(); ramPanelKJperHp.clear();
  ramPanelKJperHp = {{"timber", 4.0f}, {"masonry", 22.0f}, {"concrete", -1.0f}};
  fs::path p = contentDir() / "vehicles" / "ramming.json";
  json j;
  try { j = json::parse(readTextFile(p)); } catch (std::exception& x) { logf("ramming.json: %s", x.what()); return; }
  const json props = j.value("props", json::object()), walls = j.value("wallKJperCm", json::object());   // named: items() of a temporary dangles
  for (auto& [id, v] : props.items()) {
    RamSpec s; s.mode = v.value("mode", "topple"); s.breakKJ = v.value("breakKJ", 20.0f); s.massKg = v.value("massKg", 200.0f);
    ramProps[id] = s;
  }
  for (auto& [fam, v] : walls.items()) ramPanelKJperHp[fam] = v.get<float>();
  logf("ramming: %zu props, %zu wall families", ramProps.size(), ramPanelKJperHp.size());
}

bool TankSys::propMatrix(const Instance& i, mat4& M) const {
  if (knocked.empty()) return false;
  auto it = knocked.find(i.id);
  if (it == knocked.end()) return false;
  const Knocked& k = it->second;
  if (k.dynamicBody) {
    vec3 p; quat q;
    if (!Physics::getTransform(k.body, p, q)) return false;
    M = glm::translate(mat4(1), p) * glm::mat4_cast(q);
  } else M = k.M;
  return true;
}

void TankSys::knockProp(App& app, Instance& i, InstanceRT& r, const RamSpec& s, vec3 at, vec3 dir, float speed) {
  Knocked k; k.id = i.id; k.rev = i.rev;
  Physics::remove(r.staticBody); r.staticBody = 0xffffffffu;
  vec3 h = glm::length(vec2(dir.x, dir.z)) > 1e-4f ? glm::normalize(vec3(dir.x, 0, dir.z)) : vec3(0, 0, 1);
  const AssetDef* a = r.asset;
  if (s.mode == "rubble") {
    // a dry-stone wall: the stretch collapses into a low spread heap (no collision; the tank and soldiers cross it)
    k.rubble = k.flat = true;
    float g = app.terrain.height(i.pos.x, i.pos.z);
    k.M = glm::translate(mat4(1), vec3(0, g, 0)) * glm::scale(mat4(1), vec3(1, 0.3f, 1)) * glm::translate(mat4(1), vec3(0, -g, 0)) *
          app.instanceMatrix(i) * glm::scale(mat4(1), vec3(1.0f, 1.0f, 1.25f));
    for (int n = 0; n < 18; ++n) {
      vec3 p = at + vec3(app.fx.rnd() - 0.5f, app.fx.rnd() * 0.5f, app.fx.rnd() - 0.5f) * 2.0f;
      app.fx.smoke(p, vec3(app.fx.rnd() - 0.5f, 0.5f + app.fx.rnd() * 0.6f, app.fx.rnd() - 0.5f) + h * 0.6f, 0.5f, 3.0f, vec4(0.5f, 0.47f, 0.42f, 0.45f), 1.8f, 0.2f);
      if (n % 3 == 0) app.fx.impact(p, -h, "stone");
    }
    app.audio.play("explosion", at, 0.35f);
  } else {
    // topple: the prop is torn from its footing and goes over at once, the way the tank pushes it: a rod hinged at
    // its foot (angular acceleration 3 g sin(a) / 2 L) starting with the spin the hit gives it (contact speed over
    // contact height); it lands where its top meets the ground and lies there as static debris
    float height = std::max(0.5f, a->bmax.y - a->bmin.y);
    float hc = std::clamp(at.y - i.pos.y, 0.5f, height);
    k.falling = true; k.M0 = k.M = app.instanceMatrix(i); k.dir = h;
    k.axis = glm::normalize(glm::cross(vec3(0, 1, 0), h));
    k.pivot = vec3(i.pos.x, app.terrain.height(i.pos.x, i.pos.z), i.pos.z);
    k.len = height; k.ang = 0.03f; k.angV = std::clamp(speed / hc, 0.6f, 3.0f);
    bool wood = true;
    for (auto& m : a->materials) if (m.name.find("stone") != std::string::npos || m.name.find("concrete") != std::string::npos) wood = false;
    app.audio.play(wood ? "tree_fall" : "explosion", at, wood ? 0.8f : 0.4f);
    app.audio.play("door_break", at, 0.7f);
    for (int n = 0; n < 6; ++n) app.fx.impact(at + vec3(app.fx.rnd() - 0.5f, app.fx.rnd() * 0.4f, app.fx.rnd() - 0.5f) * 0.5f, -h, wood ? "wood" : "stone");
    for (int n = 0; n < 6; ++n) app.fx.smoke(vec3(i.pos.x, at.y * 0.3f + i.pos.y * 0.7f, i.pos.z) + vec3(app.fx.rnd() - 0.5f, 0.1f, app.fx.rnd() - 0.5f), vec3(app.fx.rnd() - 0.5f, 0.5f, app.fx.rnd() - 0.5f), 0.35f, 1.8f, vec4(0.45f, 0.4f, 0.32f, 0.35f), 1.4f, 0.2f);
  }
  knocked[i.id] = k;
  ++knocksTotal;
}

void TankSys::breachWall(App& app, Instance& i, InstanceRT& r, const TankRT& t, vec3 at, vec3 n, vec3 dir) {
  const TankDef& d = *t.def;
  // a tank-sized opening: impact-centred AT-class stamps on the struck wall plane, across the hull width and up to the
  // turret roof, driven in along the direction of travel (the shared wall damage: ragged outline, debris, collision)
  if (glm::dot(n, dir) > 0) n = -n;
  vec3 u = glm::cross(vec3(0, 1, 0), n);
  if (glm::length(u) < 1e-3f) return;
  u = glm::normalize(u);
  vec3 cp = vec3(hullMatrix(t) * vec4(0, 0, 0, 1));
  cp = cp - n * glm::dot(cp - at, n);   // tank centre line projected on the wall plane
  float g = std::min(app.terrain.height(cp.x, cp.z), t.pos.y);
  float top = t.pos.y + std::min(d.model ? d.model->bmax.y : 3.0f, 3.2f) + 0.15f;
  float half = d.halfSize.x + 0.3f;
  quat q = yawQuat(i.yaw), qi = glm::inverse(q);
  if (r.wf.empty()) app.initWallFields(r);
  std::vector<std::string> hits; int breached = 0, stamps = 0;
  for (float y = g + 0.35f; y <= top; y += 0.55f)
    for (float x = -half; x <= half + 1e-3f; x += 0.6f) {
      vec3 p = cp + u * x; p.y = y;
      WallEvent e;
      e.p = qi * (p - i.pos); e.d = qi * dir; e.kind = WALL_AP; e.dmg = 320.0f;
      e.seed = (uint32_t)(i.id * 2654435761u) ^ (uint32_t)(r.wallEvents.size() * 40503u + 17u);
      bool fx = (stamps++ % 4) == 0;
      if (app.stampWall(r, e, fx, &hits, &breached) > 0) r.wallEvents.push_back(e);
    }
  app.rebuildWalls(i, r);
  for (int k = 0; k < 10; ++k) app.fx.smoke(at + vec3(app.fx.rnd() - 0.5f, app.fx.rnd(), app.fx.rnd() - 0.5f) * 1.5f, -n * 0.8f + vec3(0, 0.6f, 0), 0.7f, 4.0f, vec4(0.55f, 0.52f, 0.48f, 0.5f), 2.0f, 0.15f);
  app.audio.play("explosion", at, 0.5f);
  ++breachesTotal;
}

void TankSys::stepKnocked(App& app, float dt) {
  for (auto it = knocked.begin(); it != knocked.end();) {
    Knocked& k = it->second;
    Instance* i = app.doc.find(k.id);
    if (!i || i->rev != k.rev) { if (k.body != 0xffffffffu) Physics::remove(k.body); it = knocked.erase(it); continue; }
    if (k.falling) {
      k.age += dt;
      k.angV += 1.5f * 9.81f / k.len * std::sin(k.ang) * dt;
      k.ang += k.angV * dt;
      // landed: the top meets the ground (or lies flat)
      vec3 top = k.pivot + glm::angleAxis(std::min(k.ang, 1.5708f), k.axis) * vec3(0, k.len, 0);
      bool landed = k.ang >= 1.5708f || (k.ang > 0.6f && top.y <= app.terrain.height(top.x, top.z) + 0.12f);
      float a = std::min(k.ang, landed ? std::min(k.ang, 1.5708f) : k.ang);
      k.M = glm::translate(mat4(1), k.pivot) * glm::mat4_cast(glm::angleAxis(a, k.axis)) * glm::translate(mat4(1), -k.pivot) * k.M0;
      if (landed) {
        k.falling = false;
        auto r = app.rt.find(k.id);
        vec3 p = vec3(k.M[3]); quat q = glm::quat_cast(glm::mat3(k.M));
        k.body = r != app.rt.end() ? Physics::addStatic(r->second.asset->shape, p, q, bodyUD(k.id) | kDebrisLow) : 0xffffffffu;
        app.audio.play("tree_fall", top, 0.5f);
        for (int n = 0; n < 6; ++n) app.fx.smoke(top + vec3(app.fx.rnd() - 0.5f, 0.1f, app.fx.rnd() - 0.5f), vec3(app.fx.rnd() - 0.5f, 0.4f, app.fx.rnd() - 0.5f), 0.35f, 1.6f, vec4(0.45f, 0.4f, 0.32f, 0.35f), 1.4f, 0.2f);
      }
    }
    if (k.dynamicBody) {
      // settled (or after 8 s): the fallen prop becomes static debris lying where it came to rest
      k.age += dt;
      if ((k.age > 0.8f && !Physics::isActive(k.body)) || k.age > 8.0f) {
        vec3 p; quat q;
        if (Physics::getTransform(k.body, p, q)) {
          Physics::remove(k.body);
          auto r = app.rt.find(k.id);
          k.body = r != app.rt.end() ? Physics::addStatic(r->second.asset->shape, p, q, bodyUD(k.id) | kDebrisLow) : 0xffffffffu;
          k.M = glm::translate(mat4(1), p) * glm::mat4_cast(q);
          k.dynamicBody = false;
        }
      }
    }
    ++it;
  }
}

void TankSys::crushUnder(App& app, TankRT& t) {
  if (knocked.empty() || std::fabs(t.speed) < 0.1f) return;
  const TankDef& d = *t.def;
  mat4 H = hullMatrix(t);
  Physics::Overlap ov[16];
  vec3 he(d.halfSize.x, 0.3f, d.halfSize.z);
  int n = Physics::boxOverlaps(vec3(H * vec4(0, 0.3f, 0, 1)), he, glm::quat_cast(glm::mat3(H)), ov, 16, bodyUD(t.id), true);
  for (int h = 0; h < n; ++h) {
    auto kn = knocked.find(ov[h].ud >> 16);
    if (kn == knocked.end() || (ov[h].ud & 0xFFFF) != kDebrisLow) continue;
    Knocked& k = kn->second;
    if (k.flat || k.falling || k.dynamicBody) continue;
    float gy = app.terrain.height(ov[h].pos.x, ov[h].pos.z);
    k.M = glm::translate(mat4(1), vec3(0, gy, 0)) * glm::scale(mat4(1), vec3(1, 0.18f, 1)) * glm::translate(mat4(1), vec3(0, -gy, 0)) * k.M;
    Physics::remove(k.body); k.body = 0xffffffffu; k.flat = true;
    ++crushedTotal;
    app.audio.play("door_break", ov[h].pos, 0.5f);
    app.fx.impact(ov[h].pos, vec3(0, 1, 0), "wood");
  }
}

void TankSys::resetRams(App& app) {
  for (auto& [id, k] : knocked) {
    if (k.body != 0xffffffffu) Physics::remove(k.body);
    Instance* i = app.doc.find(id);
    auto r = app.rt.find(id);
    if (i && r != app.rt.end() && r->second.staticBody == 0xffffffffu && r->second.asset->shape)
      r->second.staticBody = Physics::addStatic(r->second.asset->shape, i->pos, yawQuat(i->yaw), bodyUD(id));
  }
  knocked.clear(); ramWorkDone.clear(); ramLastBreach.clear();
  for (auto& [id, t] : tanks) { t.ramKey = 0; t.ramWork = t.ramNeed = 0; t.ramName.clear(); }
}

bool TankSys::ramObstacles(App& app, TankRT& t, float dt, const mat4& H, vec3 c, vec3 he, quat hq, std::string& why) {
  const TankDef& d = *t.def;
  Physics::Overlap ov[48];
  int n = Physics::boxOverlaps(c, he, hq, ov, 48, bodyUD(t.id), true);
  vec3 dir = vec3(H * vec4(0, 0, t.speed >= 0 ? 1.0f : -1.0f, 0));
  float v = std::fabs(t.speed), M = d.massKg;
  enum { O_PROP, O_TREE, O_WIRE, O_LEAF, O_WALL };
  // a building wall is pushed as a whole plane (piers, bays and lintels of one face share the effort): key = instance,
  // normal axis and the plane's offset (25 cm); break energy = kJ per cm of the wall's thickness for its family
  auto wallKey = [](uint64_t iid, const Panel& pn) {
    int ax = std::fabs(pn.normal.x) > 0.5f ? 0 : std::fabs(pn.normal.y) > 0.5f ? 1 : 2;
    int off = (int)std::lround(0.5f * (pn.bmin[ax] + pn.bmax[ax]) * 4.0f) & 0x3FF;
    return (iid << 16) | 0x8000u | ((uint64_t)ax << 12) | (uint64_t)off;
  };
  auto wallNeed = [&](const Panel& pn, float per) {
    int ax = std::fabs(pn.normal.x) > 0.5f ? 0 : std::fabs(pn.normal.y) > 0.5f ? 1 : 2;
    float cm = std::max(5.0f, (pn.bmax[ax] - pn.bmin[ax]) * 100.0f);
    return per * cm * 1000.0f;
  };
  struct Obs { int kind; Instance* i; InstanceRT* r; RamSpec spec; uint64_t key; float need; vec3 at, nrm; int k = -1, l = -1; std::string name; float mass = 0; };
  std::vector<Obs> obs;
  std::string blocker; char dbg[200] = ""; uint64_t holdKey = 0; std::string holdName;
  for (int h = 0; h < n; ++h) {
    uint64_t ud = ov[h].ud, low = ud & 0xFFFF;
    if (ud == 0 || (ud >> 60) == 0xF) continue;
    uint64_t iid = ud >> 16;
    if (tanks.count(iid) && low == 0) { blocker = tanks[iid].def ? tanks[iid].def->name : "a tank"; continue; }
    Instance* i = app.doc.find(iid);
    auto rit = app.rt.find(iid);
    if (!i || rit == app.rt.end()) continue;
    InstanceRT& r = rit->second;
    const AssetDef* a = r.asset;
    // knocked props: driven over (a settled one is crushed flat under the tracks)
    auto kn = knocked.find(iid);
    if (kn != knocked.end()) {
      Knocked& k = kn->second;
      if (k.falling) continue;   // still going over in front of the hull
      vec3 p; quat q;
      if (!k.flat && k.dynamicBody && Physics::getTransform(k.body, p, q) && (q * vec3(0, 1, 0)).y > 0.5f) {
        // still more upright than 60 deg: the hull keeps shoving it over (never flattens a standing pole)
        vec3 axis = glm::normalize(glm::cross(vec3(0, 1, 0), vec3(dir.x, 0, dir.z)));
        float lever = std::max(0.5f, (r.asset->bmax.y - r.asset->bmin.y) * 0.5f);
        Physics::setVelocity(k.body, vec3(dir.x, 0, dir.z) * std::min(v, 1.5f), axis * std::max(1.5f, (v + 1.0f) / lever));
        continue;
      }
      if (!k.flat && (!k.dynamicBody || k.age > 0.6f)) {
        if (k.dynamicBody && Physics::getTransform(k.body, p, q)) k.M = glm::translate(mat4(1), p) * glm::mat4_cast(q);
        float gy = app.terrain.height(ov[h].pos.x, ov[h].pos.z);
        k.M = glm::translate(mat4(1), vec3(0, gy, 0)) * glm::scale(mat4(1), vec3(1, 0.18f, 1)) * glm::translate(mat4(1), vec3(0, -gy, 0)) * k.M;
        Physics::remove(k.body); k.body = 0xffffffffu; k.dynamicBody = false; k.flat = true;
        ++crushedTotal;
        app.audio.play("door_break", ov[h].pos, 0.5f);
      }
      continue;
    }
    if (low == kDebrisLow || low == 0xFFF1u) continue;   // shutter / door debris, felled trunks: driven over
    if (app.interior.isProp(a)) { if (low == 0) app.interior.tankContact(app, *i, r, ov[h].pos, dir, v); continue; }   // V4: furniture is crushed and its fragments driven over, never blocks (interior.cpp)
    if (low == 0xFFFFu) {   // a building's wall bays: push through
      quat q = yawQuat(i->yaw);
      vec3 lp = glm::inverse(q) * (ov[h].pos - i->pos);
      int best = -1; float bd = 1e9f;
      for (size_t k = 0; k < a->panels.size(); ++k) {
        const Panel& pn = a->panels[k];
        float dd = glm::length(glm::clamp(lp, pn.bmin, pn.bmax) - lp);
        if (dd < bd) { bd = dd; best = (int)k; }
      }
      if (best < 0) { blocker = a->name; continue; }
      const Panel& pn = a->panels[best];
      auto f = ramPanelKJperHp.find(pn.family);
      float per = f != ramPanelKJperHp.end() ? f->second : 22.0f;
      if (per < 0) { blocker = a->name + " (" + pn.family + ")"; continue; }
      uint64_t key = wallKey(iid, pn);
      auto lb = ramLastBreach.find(key);
      if (lb != ramLastBreach.end() && app.now - lb->second < 0.35) { holdKey = key; holdName = a->name; continue; }   // just breached: the edges crumble next pass
      Obs o{O_WALL, i, &r, {}, key, wallNeed(pn, per), ov[h].pos, q * pn.normal};
      o.k = best; o.name = a->name; o.mass = 0;
      obs.push_back(o);
      continue;
    }
    if (low != 0) {   // a door / shutter leaf: wooden ones are smashed in, iron doors and steel shutters hold
      int k = (int)low - 1;
      if (k < 0 || k >= (int)a->interactables.size()) { blocker = a->name; continue; }
      int l = -1;
      for (size_t m = 0; m < r.leafBodies[k].size(); ++m) if (r.leafBodies[k][m] == ov[h].body) l = (int)m;
      if (l < 0 || a->interactables[k].sound == "steel") { blocker = a->name + " (steel door)"; continue; }
      Obs o{O_LEAF, i, &r, {}, (iid << 16) | (uint64_t)(0x4000 + ((k * 16 + l) & 0x3FFF)), 15000.0f, ov[h].pos, -dir};
      o.k = k; o.l = l; o.name = "door"; o.mass = 40;
      obs.push_back(o);
      continue;
    }
    // the instance's own static body
    if (a->tree.on) {
      if (a->tree.hedge) { blocker = a->name + " (earth bank)"; continue; }
      if (r.treeFelled) continue;
      float rr = a->tree.trunkR / 0.4f;
      Obs o{O_TREE, i, &r, {}, iid << 16, std::clamp(900000.0f * rr * rr, 60000.0f, 1500000.0f), ov[h].pos, -dir};
      o.name = a->name; o.mass = a->tree.mass;
      obs.push_back(o);
      continue;
    }
    if (!a->defense.kind.empty()) {
      if (r.defDestroyed) continue;
      if (a->defense.blocksVehicles || a->defense.kind == "sandbag") { blocker = a->name; continue; }
      auto sp = ramProps.find(a->id);
      Obs o{O_WIRE, i, &r, {}, iid << 16, (sp != ramProps.end() ? sp->second.breakKJ : 60.0f) * 1000.0f, ov[h].pos, -dir};
      o.name = a->name; o.mass = sp != ramProps.end() ? sp->second.massKg : 80.0f;
      obs.push_back(o);
      continue;
    }
    auto sp = ramProps.find(a->id);
    RamSpec s;
    if (sp != ramProps.end()) s = sp->second;
    else if (a->category == "Dressing" && a->panels.empty() && !a->enterable && a->station.kind.empty() && !isVehicleAsset(a) &&
             (a->bmax.x - a->bmin.x) * (a->bmax.z - a->bmin.z) < 2.5f) { s.mode = "topple"; s.breakKJ = 10; s.massKg = 100; }   // small dressing
    else s.mode = "block";
    if (s.mode == "block" && !a->panels.empty()) {
      // a building's solid core where it meets a wall bay (window / door frames, sills, lintels): it goes with that bay,
      // and once the bay is pushed through the tank passes the frame pieces left standing in the opening
      quat q = yawQuat(i->yaw);
      vec3 lp = glm::inverse(q) * (ov[h].pos - i->pos);
      int best = -1; float bd = 1e9f;
      for (size_t k = 0; k < a->panels.size(); ++k) {
        const Panel& pn = a->panels[k];
        float dd = glm::length(glm::clamp(lp, pn.bmin - vec3(0.35f), pn.bmax + vec3(0.35f)) - lp);
        if (dd < bd) { bd = dd; best = (int)k; }
      }
      if (best >= 0 && bd < 1e-3f) {
        const Panel& pn = a->panels[best];
        uint64_t key = wallKey(iid, pn);
        auto f = ramPanelKJperHp.find(pn.family);
        float per = f != ramPanelKJperHp.end() ? f->second : 22.0f;
        if (per < 0) { blocker = a->name + " (" + pn.family + ")"; continue; }
        float need = wallNeed(pn, per);
        if (ramWorkDone[key] >= need) continue;   // that bay is already pushed through
        auto lb = ramLastBreach.find(key);
        if (lb != ramLastBreach.end() && app.now - lb->second < 0.35) { holdKey = key; holdName = a->name; continue; }
        Obs o{O_WALL, i, &r, {}, key, need, ov[h].pos, q * pn.normal};
        o.k = best; o.name = a->name;
        obs.push_back(o);
        continue;
      }
    }
    if (s.mode == "block" || !a->shape) {
      blocker = a->name;
      vec3 lp = glm::inverse(yawQuat(i->yaw)) * (ov[h].pos - i->pos);
      std::snprintf(dbg, sizeof dbg, "ram: %s core contact at local (%.2f, %.2f, %.2f), depth %.2f", a->id.c_str(), lp.x, lp.y, lp.z, ov[h].depth);
      continue;
    }
    Obs o{O_PROP, i, &r, s, iid << 16, s.breakKJ * 1000.0f, ov[h].pos, -dir};
    o.name = a->name; o.mass = s.mode == "rubble" ? 0.0f : s.massKg;
    obs.push_back(o);
  }
  if (!blocker.empty()) { if (blocker != t.blockedBy && dbg[0]) logf("%s", dbg); why = blocker; t.ramKey = 0; return false; }
  if (obs.empty() && holdKey) { why = "pushing " + holdName; return false; }
  if (obs.empty()) { t.ramKey = 0; return true; }   // only debris / felled trunks / wrecked wire in the way: driven over
  // energy available this step: the kinetic energy at contact plus the engine pushing
  float thr = std::fabs(t.throttleIn);
  bool pushing = thr > 0.05f && (t.throttleIn > 0) == (t.speed >= 0);
  float E = 0.5f * M * v * v + (pushing ? d.engineKW * 1000.0f * d.pushEff * thr * dt : 0.0f);
  float Ein = E;
  std::sort(obs.begin(), obs.end(), [&](const Obs& a, const Obs& b) {
    return a.need - ramWorkDone[a.key] < b.need - ramWorkDone[b.key];
  });
  float used = 0, massOut = 0; bool allGave = true;
  const Obs* holding = nullptr;
  for (auto& o : obs) {
    float left = std::max(0.0f, o.need - ramWorkDone[o.key]);
    if (E >= left) {
      E -= left; used += left; ramWorkDone[o.key] = o.need;
      massOut += o.mass;
      vec3 at = o.at;
      switch (o.kind) {
        case O_PROP: knockProp(app, *o.i, *o.r, o.spec, at, dir, v); break;
        case O_TREE: app.fellTree(*o.i, *o.r, at, dir * 4.0f); ++knocksTotal; break;
        case O_WIRE: app.destroyDefense(*o.i, *o.r); ++knocksTotal; app.audio.play("door_break", at, 0.6f); break;
        case O_LEAF:
          if (app.leafBreakable(o.r->asset->interactables[o.k])) app.detachLeaf(*o.i, *o.r, o.k, o.l, dir * (o.mass * std::max(2.0f, v)), at, true);
          else {   // a window sash (weapons only break its glass): the tank smashes the whole casement in
            app.breakGlass(*o.i, *o.r, o.k, o.l, at);
            o.r->leafSt[o.k][o.l] |= 3;
            Physics::remove(o.r->leafBodies[o.k][o.l]); o.r->leafBodies[o.k][o.l] = 0xffffffffu;
            app.audio.play("door_break", at, 0.6f);
          }
          ++knocksTotal; break;
        case O_WALL: breachWall(app, *o.i, *o.r, t, at, o.nrm, dir); ramLastBreach[o.key] = app.now; break;
      }
      lastRam = "broke " + o.name;
      ++t.knocks; ++t.crushes;
      if (o.kind == O_WALL) ++t.breaches;
    } else {
      ramWorkDone[o.key] += E; used += E; E = 0;
      allGave = false; holding = &o;
      break;
    }
  }
  if (!allGave) {
    // stopped against it: the energy put in so far is kept; pushing on (throttle held) finishes the job
    t.ramKey = holding->key; t.ramWork = ramWorkDone[holding->key]; t.ramNeed = holding->need; t.ramName = holding->name;
    why = "pushing " + holding->name;
    lastRam = why;
    if (pushing && std::fmod(t.ramWork, 120000.0f) < d.engineKW * 1000.0f * d.pushEff * dt * 1.01f) {   // crumbling dust while pushing
      app.fx.impact(holding->at, holding->nrm, holding->kind == O_WALL ? "masonry" : "wood");
      app.fx.smoke(holding->at, vec3(0, 0.6f, 0) - dir * 0.3f, 0.4f, 2.0f, vec4(0.52f, 0.49f, 0.44f, 0.4f), 1.6f, 0.2f);
    }
    return false;
  }
  t.ramKey = 0; t.ramWork = t.ramNeed = 0; t.ramName.clear();
  // what is left of the kinetic energy, then momentum shared with whatever was knocked over
  float vk2 = std::max(0.0f, v * v - 2.0f * std::max(0.0f, used - (Ein - 0.5f * M * v * v)) / M);
  float vn = std::sqrt(vk2) * M / (M + massOut);
  t.speed = (t.speed >= 0 ? 1.0f : -1.0f) * vn;
  why = "broke through";
  return true;
}
