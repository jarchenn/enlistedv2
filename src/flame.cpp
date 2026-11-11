// FT1 flamethrower: the stream of burning parcels, ground fire patches, the nozzle light, flames and smoke, the roar
// (synthesised: the project's audio banks hold no flame or burn sound). See flame.h and docs/flamethrower.md.
#include "flame.h"
#include "app.h"
#include "game.h"
#include "net.h"
#include <algorithm>
#include <cmath>

bool flameDrain(WeaponState& ws, const WeaponDef& d, float seconds) {
  ws.flameFrac += seconds * (float)d.cap / std::max(1.0f, d.flame.fuelSeconds);
  int n = (int)ws.flameFrac; ws.flameFrac -= (float)n; ws.loaded = std::max(0, ws.loaded - n);
  return ws.loaded > 0;
}

namespace {
bool hostSim() { return gGame && gGame->active && !gGame->isClient; }
// the roar: low-passed noise with a slow swell, a flutter and crackle; ~1.5 s, seamless at the loop point
std::vector<float> makeRoar() {
  const int sr = 44100, n = (int)(1.5f * sr);
  std::vector<float> b(n);
  uint32_t s = 12345u; auto r = [&] { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s % 20001u) / 10000.0f - 1.0f; };
  float lp1 = 0, lp2 = 0, lp3 = 0;
  for (int i = 0; i < n; ++i) {
    float t = (float)i / sr, x = r();
    lp1 += 0.10f * (x - lp1); lp2 += 0.02f * (x - lp2); lp3 += 0.4f * (x - lp3);
    float flutter = 0.75f + 0.25f * std::sin(6.2831853f * 11.0f * t) * std::sin(6.2831853f * 2.0f * t + 1.0f);
    float crackle = (r() > 0.985f ? r() * 0.5f : 0.0f);
    b[i] = (0.9f * lp1 + 1.8f * lp2 + 0.25f * lp3) * flutter + crackle;
  }
  const int f = (int)(0.12f * sr);   // crossfade the end into the start
  for (int i = 0; i < f; ++i) { float u = (float)i / f; b[i] = b[i] * u + b[n - f + i] * (1 - u); }
  b.resize(n - f);
  float mx = 1e-4f; for (float v : b) mx = std::max(mx, std::fabs(v));
  for (float& v : b) v = v / mx * 0.8f;
  return b;
}
vec3 diskDir(vec3 d, float coneRad, float a, float rr) {   // d + a random offset inside the cone
  vec3 rt = glm::cross(d, vec3(0, 1, 0)); rt = glm::length(rt) > 1e-4f ? glm::normalize(rt) : vec3(1, 0, 0);
  vec3 up = glm::cross(rt, d);
  float r = std::sqrt(rr) * std::tan(coneRad);
  return glm::normalize(d + rt * std::cos(a) * r + up * std::sin(a) * r);
}
float segDist(vec3 a, vec3 b, vec3 p0, vec3 p1) {   // closest distance between segments a-b and p0-p1 (soldier axis)
  vec3 d1 = b - a, d2 = p1 - p0, r = a - p0;
  float A = glm::dot(d1, d1), E = glm::dot(d2, d2), F = glm::dot(d2, r), s, t;
  if (A < 1e-9f && E < 1e-9f) return glm::length(r);
  if (A < 1e-9f) { s = 0; t = std::clamp(F / E, 0.0f, 1.0f); }
  else {
    float C = glm::dot(d1, r);
    if (E < 1e-9f) { t = 0; s = std::clamp(-C / A, 0.0f, 1.0f); }
    else {
      float B = glm::dot(d1, d2), den = A * E - B * B;
      s = den > 1e-9f ? std::clamp((B * F - C * E) / den, 0.0f, 1.0f) : 0.0f;
      t = (B * s + F) / E;
      if (t < 0) { t = 0; s = std::clamp(-C / A, 0.0f, 1.0f); } else if (t > 1) { t = 1; s = std::clamp((B - C) / A, 0.0f, 1.0f); }
    }
  }
  return glm::length((a + d1 * s) - (p0 + d2 * t));
}
}

void FlameSys::spray(App& app, int key, int weapon, vec3 origin, vec3 dir, float dt, bool damage, vec3 bore) {
  if (weapon < 0 || weapon >= (int)app.weaponDefs.size() || glm::length(dir) < 0.5f) return;
  const WeaponDef& d = *app.weaponDefs[weapon];
  if (!d.flame.on) return;
  dir = glm::normalize(dir);
  FlameNozzle& n = nz_[key];
  n.o = origin; n.d = dir; n.lastOn = t_; n.weapon = weapon; n.bore = glm::length(bore) > 0.5f ? glm::normalize(bore) : vec3(0);
  int owner = key < 0 ? (gGame && gGame->active ? gGame->localSlot : -1) : key;
  n.acc += dt * d.flame.parcelsPerSec;
  float cone = glm::radians(d.flame.coneDeg);
  while (n.acc >= 1.0f && parcels_.size() < 220) {
    n.acc -= 1.0f;
    FlameParcel q;
    q.v = diskDir(dir, cone, rnd() * 6.2831853f, rnd()) * (d.flame.speed * (0.94f + 0.12f * rnd()));
    q.p = q.o = origin + q.v * (rnd() * std::min(dt, 0.12f));   // spread over the frame: a continuous stream
    q.life = d.flame.range / glm::length(q.v); q.owner = owner; q.weapon = weapon; q.dmg = damage;
    parcels_.push_back(q); ++emitted;
  }
  if (damage && gNet && gNet->role == Net::HOST) {   // the host tells the others where this nozzle points (10 Hz)
    n.netT += dt;
    if (n.netT >= 0.1f) { gNet->hostWorldEvent(json{{"t", "flame"}, {"by", owner}, {"w", weapon}, {"p", tojson(origin)}, {"d", tojson(dir)}, {"dt", n.netT}}); n.netT = 0; }
  }
}

void FlameSys::clear(App* app) {
  if (app) for (auto& [k, n] : nz_) if (n.loop >= 0) app->audio.loopStop(n.loop);
  nz_.clear(); parcels_.clear(); patches_.clear();
  farthest = 0; emitted = wallStops = groundPatches = soldierHits = 0;
}

void FlameSys::step(App& app, float dt) {
  t_ += dt;
  Fx& fx = app.fx;
  const bool host = hostSim();
  Game* G = gGame;
  const bool roomForFlame = fx.parts.size() < fx.maxParts * 0.8f, roomForSmoke = fx.parts.size() < fx.maxParts * 0.9f;
  // ---- nozzles: flare, light is gathered separately, the roar follows the nozzle
  static bool roarReady = false;
  if (!roarReady && !nz_.empty()) { roarReady = true; app.audio.addSound("flame_roar", makeRoar()); }
  int voices = 0;
  for (auto& [key, n] : nz_) {
    bool on = n.lastOn >= t_ - 0.12;
    if (on && roomForFlame) {
      float k = key < 0 ? 0.6f : 1.0f;   // the player's own nozzle is a metre from the eye: smaller sprites
      vec3 axis = n.d;   // the flame leaves the nozzle straight along the stream (it used to follow the raised bore and arc over to the crosshair)
      for (int i = 0; i < 3; ++i) {   // the flame tongue billowing forward of the nozzle: hot white-yellow at the lip, orange and dark red further out
        float t = fx.rnd() * fx.rnd() * 1.9f;   // more sprites near the nozzle
        Particle q; q.p = n.o + axis * (0.05f + t) + vec3(fx.rnd() - 0.5f, fx.rnd() - 0.5f, fx.rnd() - 0.5f) * (0.04f + 0.1f * t);
        q.v = axis * (3.0f + 4.0f * fx.rnd()) + vec3(fx.rnd() - 0.5f, fx.rnd() - 0.5f, fx.rnd() - 0.5f) * (0.6f + t);
        q.size = (0.1f + 0.24f * t) * k * (0.8f + 0.5f * fx.rnd()); q.life = q.maxLife = 0.16f + 0.12f * fx.rnd() + 0.05f * t;
        q.col = vec4(3.2f, 0, 0, 1); q.additive = true; q.flame = std::floor(fx.rnd() * 255.0f); q.grow = q.size * 1.2f; q.drag = 0.6f; q.lift = 0.1f;
        fx.parts.push_back(q);
      }
      if (fx.rnd() < 0.4f) {   // the soft outer body of the tongue: translucent orange-red
        float t = 0.3f + 1.2f * fx.rnd();
        Particle q; q.p = n.o + axis * t; q.v = axis * 3.0f + vec3(fx.rnd() - 0.5f, fx.rnd() - 0.5f, fx.rnd() - 0.5f) * 0.8f; q.size = (0.25f + 0.3f * t) * k; q.life = q.maxLife = 0.28f + 0.2f * fx.rnd();
        q.col = vec4(0.8f, 0.22f, 0.04f, 0.5f); q.grow = q.size * 0.8f; q.drag = 0.8f; q.lift = 0.15f; fx.parts.push_back(q);
      }
      if (roomForSmoke && fx.rnd() < 0.18f) {   // grey smoke curling off the tip of the tongue
        vec3 tip = n.o + axis * (1.2f + 0.8f * fx.rnd());
        fx.smoke(tip, vec3(fx.rnd() - 0.5f, 0.25f + 0.4f * fx.rnd(), fx.rnd() - 0.5f) * 1.4f + axis * 2.0f, (0.35f + 0.25f * fx.rnd()) * k, 1.5f + fx.rnd(), vec4(0.2f, 0.19f, 0.18f, 0.42f), 0.8f, 0.45f);
      }
    }
    if (on && host && G && n.weapon >= 0) {   // bots near the stream are suppressed, as by a near miss (4 times a second)
      n.fxT += dt;
      if (n.fxT >= 0.25f) {
        n.fxT = 0;
        int owner = key < 0 ? G->localSlot : key;
        vec3 far = n.o + n.d * app.weaponDefs[n.weapon]->flame.range;
        for (auto& s : G->sold) {
          if (!s.alive || s.human || s.zombie || s.slot == owner || !(owner >= 0 && owner < (int)G->sold.size() && Game::hostile(G->sold[owner], s))) continue;
          if (segDist(n.o, far, s.feet, s.feet + vec3(0, 1.7f, 0)) < 3.5f) s.br.suppressed = std::min(1.0f, s.br.suppressed + 0.2f);
        }
      }
    }
    if (on && voices < 4) {
      ++voices;
      if (n.loop < 0) n.loop = app.audio.loopStart("flame_roar", n.o, key < 0 ? 0.55f : 0.8f, 1.0f);
      else app.audio.loopSet(n.loop, n.o, key < 0 ? 0.55f : 0.8f, 0.97f + 0.06f * fx.rnd());
    } else if (n.loop >= 0) { app.audio.loopStop(n.loop); n.loop = -1; }
  }
  for (auto it = nz_.begin(); it != nz_.end();) { if (it->second.lastOn < t_ - 2.0 && it->second.loop < 0) it = nz_.erase(it); else ++it; }
  // ---- parcels
  for (size_t k = 0; k < parcels_.size();) {
    FlameParcel& q = parcels_[k];
    q.age += dt;
    q.v.y -= 3.9f * dt;                      // a heavy fuel stream: 0.4 g
    vec3 seg = q.v * dt; float len = glm::length(seg);
    bool dead = q.age >= q.life, stopped = false; vec3 end = q.p + seg, nrm(0, 1, 0);
    Physics::Hit h;
    if (len > 1e-5f && Physics::castRayEx(q.p, seg, len, h)) { end = h.pos; nrm = h.normal; stopped = true; ++wallStops; }
    if (q.dmg && host && G) {
      float travelled = glm::length(end - q.o), rad = 0.3f + 0.05f * travelled;
      const WeaponDef::FlameDef& fd = app.weaponDefs[q.weapon]->flame;
      for (auto& s : G->sold) {
        if (!s.alive || s.slot == q.owner || s.slot >= 256 || (q.seen[s.slot >> 5] >> (s.slot & 31)) & 1u) continue;
        if (segDist(q.p, end, s.feet + vec3(0, 0.1f, 0), s.feet + vec3(0, 1.65f, 0)) > rad + 0.3f) continue;
        q.seen[s.slot >> 5] |= 1u << (s.slot & 31);
        if (G->flameHit(s.slot, q.owner, fd.parcelDamage, glm::normalize(q.v), fd.burnSeconds, fd.burnDps)) ++soldierHits;
      }
    }
    farthest = std::max(farthest, glm::length(end - q.o));
    float u = std::clamp(q.age / q.life, 0.0f, 1.0f);
    float eyeD = glm::length(end - app.eyePos());   // particles beside the player's own eye would fill the screen: fade them in with distance
    float near = (q.owner >= 0 && q.owner == (G ? G->localSlot : -2)) || q.owner < 0 ? std::clamp((eyeD - 1.2f) / 6.0f, 0.15f, 1.0f) : 1.0f;
    if (roomForFlame && eyeD > 1.2f && fx.rnd() < 0.35f) {   // the burning stream: flame sprites, white-yellow -> orange -> dark red, noise-eroded, widening
      Particle f; f.p = end; f.v = q.v * 0.1f + vec3(fx.rnd() - 0.5f, fx.rnd() - 0.3f, fx.rnd() - 0.5f) * 0.9f; f.v.y += 0.4f;
      f.size = (0.2f + 0.5f * u) * near; f.life = f.maxLife = 0.26f + 0.26f * fx.rnd();
      f.col = vec4(2.6f * glm::mix(0.4f, 1.0f, near), 0, 0, 1); f.additive = true; f.flame = std::floor(fx.rnd() * 255.0f); f.grow = f.size * 0.7f; f.drag = 0.8f; f.lift = 0.8f;
      fx.parts.push_back(f);
    }
    if (roomForFlame && eyeD > 1.2f && fx.rnd() < 0.12f) {   // the soft body round the core
      Particle f; f.p = end; f.v = q.v * 0.08f + vec3(fx.rnd() - 0.5f, fx.rnd() - 0.2f, fx.rnd() - 0.5f) * 0.7f; f.size = (0.35f + 0.7f * u) * near; f.life = f.maxLife = 0.35f + 0.3f * fx.rnd();
      f.col = vec4(0.8f, 0.24f, 0.05f, 0.4f); f.grow = f.size * 0.5f; f.drag = 0.9f; f.lift = 0.7f; fx.parts.push_back(f);
    }
    if (roomForSmoke && u > 0.3f && eyeD > 1.2f && fx.rnd() < 0.07f)   // smoke wisps curling off the stream
      fx.smoke(end, q.v * 0.06f + vec3(fx.rnd() - 0.5f, 0.5f + 0.7f * fx.rnd(), fx.rnd() - 0.5f) * 1.2f, (0.4f + 0.4f * fx.rnd()) * near, 1.6f + 0.8f * fx.rnd(), vec4(0.19f, 0.18f, 0.17f, 0.4f), 0.9f, 0.8f);
    if (stopped) {
      if (nrm.y > 0.45f && q.age > 0.03f) {   // landed on ground: a short fire patch (merged with a nearby one)
        FlamePatch* near = nullptr; float nd = 1e9f;
        for (auto& pt : patches_) { float dd = glm::length(vec2(pt.p.x - end.x, pt.p.z - end.z)); if (dd < 0.9f && std::fabs(pt.p.y - end.y) < 0.8f && dd < nd) { nd = dd; near = &pt; } }
        const WeaponDef::FlameDef& fd = app.weaponDefs[q.weapon]->flame;
        if (near) near->t = std::max(near->t, fd.patchSeconds);
        else if (patches_.size() < 24) {
          FlamePatch pt; pt.p = end + nrm * 0.05f; pt.t = fd.patchSeconds; pt.radius = fd.patchRadius; pt.dps = fd.patchDps; pt.burnSeconds = fd.burnSeconds; pt.burnDps = fd.burnDps;
          pt.owner = q.owner; pt.dmg = q.dmg; patches_.push_back(pt); ++groundPatches;
        }
      }
      if (roomForFlame) for (int i = 0; i < 3; ++i) {   // a splash against what stopped it
        Particle f; f.p = end + nrm * 0.1f; f.v = glm::normalize(nrm + vec3(fx.rnd() - 0.5f, fx.rnd() - 0.5f, fx.rnd() - 0.5f)) * (1.5f + 2.0f * fx.rnd());
        f.size = 0.5f; f.life = f.maxLife = 0.25f + 0.2f * fx.rnd(); f.col = vec4(7, 3, 0.8f, 1); f.additive = true; f.grow = -1.0f; f.drag = 1.5f;
        fx.parts.push_back(f);
      }
      dead = true;
    }
    q.p = end;
    if (dead) { parcels_[k] = parcels_.back(); parcels_.pop_back(); } else ++k;
  }
  // ---- ground fire patches
  for (size_t k = 0; k < patches_.size();) {
    FlamePatch& pt = patches_[k];
    pt.t -= dt; pt.tick += dt;
    if (roomForFlame && fx.rnd() < 0.6f) {
      Particle f; f.p = pt.p + vec3(fx.rnd() - 0.5f, 0, fx.rnd() - 0.5f) * (pt.radius * 1.6f); f.v = vec3(fx.rnd() - 0.5f, 1.2f + 1.6f * fx.rnd(), fx.rnd() - 0.5f) * 0.6f; f.v.y += 0.8f;
      f.size = 0.4f + 0.3f * fx.rnd(); f.life = f.maxLife = 0.3f + 0.3f * fx.rnd();
      f.col = vec4(6.5f + 2.5f * fx.rnd(), 2.4f + 1.2f * fx.rnd(), 0.5f, 1); f.additive = true; f.grow = -f.size * 0.9f; f.drag = 0.8f; f.lift = 2.0f;
      fx.parts.push_back(f);
    }
    if (roomForSmoke && fx.rnd() < 0.05f) fx.smoke(pt.p + vec3(0, 0.6f, 0), vec3(0.2f, 1.0f, 0.1f), 0.6f, 1.8f, vec4(0.2f, 0.19f, 0.18f, 0.35f), 0.9f, 0.8f);
    if (pt.tick >= 0.25f) {
      pt.tick = 0;
      if (pt.dmg && host && G) for (auto& s : G->sold) {
        if (!s.alive) continue;   // ground fire burns whoever walks through it, its owner too
        if (glm::length(vec2(s.feet.x - pt.p.x, s.feet.z - pt.p.z)) > pt.radius || std::fabs(s.feet.y - pt.p.y) > 1.5f) continue;
        if (G->flameHit(s.slot, pt.owner, pt.dps * 0.25f, vec3(0, 1, 0), pt.burnSeconds, pt.burnDps)) ++soldierHits;
      }
    }
    if (pt.t <= 0) { patches_[k] = patches_.back(); patches_.pop_back(); } else ++k;
  }
}

void FlameSys::gatherLights(std::vector<LightDesc>& out) const {
  int n = 0;
  for (auto& [key, z] : nz_) {
    if (z.lastOn < t_ - 0.12 || n >= 3) continue;
    ++n;
    LightDesc l; l.pos = z.o + z.d * 2.5f; l.radius = 14.0f; l.color = vec3(1.0f, 0.55f, 0.2f); l.intensity = 26.0f; l.flicker = 0.8f; l.id = 0xF1A00000u + (uint32_t)(key + 1);
    out.push_back(l);
  }
  int p = 0;
  for (auto& pt : patches_) {   // the two freshest ground fires glow too
    if (p >= 2) break;
    if (pt.t > 1.0f) { ++p; LightDesc l; l.pos = pt.p + vec3(0, 0.6f, 0); l.radius = 6.0f; l.color = vec3(1.0f, 0.5f, 0.18f); l.intensity = 6.0f; l.flicker = 0.9f; l.id = 0xF1B00000u + (uint32_t)p; out.push_back(l); }
  }
}

// FT1 flamethrower, the local player: hold to spray. The stream starts at the nozzle as the world projection shows it and
// bends toward the crosshair's target (up to 20 m); the tank drains by the second (100 % = flame.fuelSeconds). Damage is
// the host's (flame.cpp / Game::flameHit); a LAN client sprays for the look and asks the host at 10 Hz.
void App::stepFlameLocal(float dt, const ExploreInput& in, const WeaponDef& d, WeaponState& s, bool fireHeld) {
  if (in.reload && !pauseMenu && !showLoadout) weaponMsg = "Fuel is not reloaded: refill at an ammo crate";
  if (!fireHeld) { s.triggerReleased = true; s.clicked = false; flameNetT = 0; return; }
  s.triggerReleased = false;
  if (s.loaded <= 0) { if (!s.clicked) { audio.play("click", eyePos(), 0.6f); s.clicked = true; } return; }
  s.clicked = false;
  vec3 eye = eyePos(), aimDir = forward(eyeYaw + viewKickYaw, eyePitch + viewKickPitch);
  vec3 muzzle = weaponMuzzleWorld(d, s), start = muzzle;
  Physics::Hit hb;
  if (Physics::castRayEx(eye, muzzle - eye, glm::length(muzzle - eye), hb)) start = eye + glm::normalize(muzzle - eye) * std::max(0.0f, hb.t - 0.01f);
  vec3 fxs = vmApparent(start);   // the nozzle as the world projection draws it
  Physics::Hit h; float reach = Physics::castRayEx(eye, aimDir, 20.0f, h) ? h.t : 20.0f;
  vec3 dir = reach > 3.0f ? glm::normalize(eye + aimDir * reach - fxs) : aimDir;
  vec3 boreDir = glm::normalize(vmApparent(vec3(viewModelMatrix(d, s) * vec4(d.muzzle + d.muzzleDir, 1))) - fxs);   // the bore through the world projection (the gun is drawn with its own FOV)
  bool host = gGame && gGame->active && !gGame->isClient;
  flame.spray(*this, -1, activeWeapon, fxs, dir, dt, host, boreDir);
  flameDrain(s, d, dt);
  s.sinceShot = 0; s.kick = std::max(s.kick, d.rVisual);
  if (host && gGame->localSlot >= 0 && (++s.burst % 24) == 1) gGame->hearSound(fxs, (int)gGame->sold[gGame->localSlot].role, 70, gGame->localSlot, 0);
  if (gNet && gNet->role == Net::CLIENT && gGame && gGame->active) { flameNetT += dt; if (flameNetT >= 0.1f) { gNet->clientFlame(fxs, dir, activeWeapon, flameNetT); flameNetT = 0; } }
}
