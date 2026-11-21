#include <cstdlib>
// Pooled cosmetic effects. Never affect gameplay: impacts and damage come from the projectile path.
#include "combat.h"
#include "terrain.h"
#include "fxdata.h"
#include "terrain_deform.h"
#include <algorithm>
#include <cmath>

void Fx::smoke(vec3 p, vec3 v, float size, float life, vec4 col, float grow, float lift) {
  if (!admit()) return;
  Particle q; q.p = p; q.v = v; q.size = size; q.life = q.maxLife = life; q.col = col; q.grow = grow; q.drag = 1.6f; q.lift = lift;
  parts.push_back(q);
}
void Fx::flash(vec3 p, float size, float life, vec4 col) {
  if (!admit()) return;
  Particle q; q.p = p; q.size = size; q.life = q.maxLife = life; q.col = col; q.additive = true; q.grow = -size / life * 0.3f;
  parts.push_back(q);
}
void Fx::muzzleFlash(vec3 p, vec3 dir, float scale, bool suppressed, vec3 light, vec3 tint, const char* caliber, const char* device) {
  dir = glm::length(dir) > 1e-4f ? glm::normalize(dir) : vec3(0, 0, -1);
  if (evOn && tint.x < 0) {   // fxev: spec 5.1 small-arms fire (a blaster keeps its own coloured look)
    FxEvent e; e.id = nextId(-1); e.recipe = FXR_SMALL; e.pos = p; e.dir = dir; e.light = light; e.scale = scale;
    e.caliber = caliber ? caliber : ""; e.device = suppressed ? "suppressor" : device ? device : "none"; e.seed = (uint32_t)(serial * 2654435761u);
    emit(e); return;
  }
  vec3 up = std::fabs(dir.y) > 0.95f ? vec3(1, 0, 0) : vec3(0, 1, 0), s1 = glm::normalize(glm::cross(dir, up)), s2 = glm::cross(s1, dir);
  float life = 0.02f + 0.02f * rnd();
  auto put = [&](vec3 q, float size, vec4 col) {
    if (!admit()) return;
    Particle a; a.p = q; a.size = size; a.life = a.maxLife = life; a.col = col; a.additive = true; a.grow = size * 1.5f; parts.push_back(a);
  };
  FlashLook F = flashLook;
  if (tint.x >= 0) { F.core = tint * 6.0f + vec3(1.5f); F.petal = tint * 4.0f; F.edge = tint * 1.6f; F.light = tint; }   // SW1: a blaster's coloured flash (white-hot core)
  if (suppressed) { put(p + dir * 0.01f * scale, 0.025f * scale, vec4(F.core * 0.5f, 1)); return; }
  put(p + dir * 0.015f * scale, (0.05f + 0.025f * rnd()) * scale, vec4(F.core, 1));   // saturated yellow-orange core (not white)
  int n = 4 + (int)(rnd() * 3.0f);
  float spin = rnd() * 6.2831853f;
  for (int k = 0; k < n; ++k) {
    float along = (0.03f + 0.11f * rnd()) * scale;   // outer flame: forward along the bore, a few side petals
    float a = spin + k * 6.2831853f / n, sideK = k < 2 ? 0.0f : (0.02f + 0.03f * rnd()) * scale;
    vec3 q = p + dir * along + (s1 * std::cos(a) + s2 * std::sin(a)) * sideK;
    float sz = (0.045f - 0.15f * along / std::max(0.1f, scale) + 0.02f * rnd()) * scale;
    put(q, sz, vec4(F.petal * (0.85f + 0.3f * rnd()), 1));                         // orange flame
    put(q + dir * 0.25f * sz, sz * 1.35f, vec4(F.edge, 1));                         // a darker orange-red rim around it
  }
  if (!admitLight()) return;
  MuzzleLight L; L.p = light; L.life = life + 0.01f; L.k = scale; L.id = 0xF1A50100u + (++mlightSerial & 0xFF); if (tint.x >= 0) L.col = tint; mlights.push_back(L);
}

// DF1: the side view of a rifle shot: a pinpoint white-hot core at the crown, a big soft warm ball a ball's radius ahead, then
// a ragged orange-tan tongue of burning gas some three balls long, narrowing; all noise-eroded sprites (fireball sprite: flame
// id + 1000, its colour kept, the tongue's stretched along the bore on screen), gone in one or two frames, each shot rolled and
// sized afresh. Suppressed: a dim core only. The muzzle light as the standard flash.
void Fx::muzzleFireball(vec3 p, vec3 dir, float scale, bool suppressed, vec3 light, const char* caliber, const char* device) {
  dir = glm::length(dir) > 1e-4f ? glm::normalize(dir) : vec3(0, 0, -1);
  if (evOn) { muzzleFlash(p, dir, scale, suppressed, light, vec3(-1), caliber, device); return; }   // fxev: one physical flash for every gun
  vec3 up = std::fabs(dir.y) > 0.95f ? vec3(1, 0, 0) : vec3(0, 1, 0), s1 = glm::normalize(glm::cross(dir, up)), s2 = glm::cross(s1, dir);
  const float life = 0.022f + 0.018f * rnd(), D = (0.15f + 0.05f * rnd()) * scale;   // the ball's diameter (rifle ~17 cm)
  const float roll = rnd() * 6.2831853f;
  auto put = [&](vec3 q, float size, vec3 col, float stretch) {
    if (!admit()) return;
    Particle a; a.p = q; a.size = size; a.life = a.maxLife = life; a.col = vec4(col, 1); a.additive = true; a.grow = size * 0.8f;
    a.flame = 1000.0f + (float)(int)(rnd() * 255.0f); a.v = dir * (0.001f + stretch); a.drag = 0; parts.push_back(a);
  };
  if (suppressed) { put(p + dir * 0.01f * scale, 0.02f * scale, vec3(2.2f, 1.5f, 0.6f), 0); return; }
  put(p + dir * 0.006f * scale, 0.022f * scale, vec3(9.0f, 7.5f, 4.8f), 0);   // the crown: a pinpoint, white-hot
  vec3 ball = p + dir * (0.62f * D);
  put(ball, 0.66f * D, vec3(5.2f, 3.9f, 2.3f), 0);   // round and large against the tongue (the reference)   // the ball: pale yellow-tan, the brightest body
  for (int k = 0; k < 3; ++k) {   // its ragged rim: three lobes round it
    float a = roll + k * 2.094f; vec3 o = (s1 * std::cos(a) + s2 * std::sin(a)) * (0.22f * D * (0.7f + 0.6f * rnd()));
    put(ball + o + dir * (0.1f * D * (rnd() - 0.5f)), 0.42f * D * (0.8f + 0.4f * rnd()), vec3(3.2f, 2.0f, 0.95f), 0);
  }
  const float tongue = D * (2.6f + 1.2f * rnd());   // the tongue: from the ball's front some three balls on, narrowing, wandering off the bore
  const int n = 6 + (int)(rnd() * 3.0f);
  vec2 wob(0);
  for (int k = 0; k < n; ++k) {
    float u = (k + 0.5f) / n, along = 0.9f * D + tongue * u;
    wob += vec2(rnd() - 0.5f, rnd() - 0.5f) * (0.10f * D);
    vec3 q = p + dir * along + s1 * wob.x + s2 * wob.y;
    float sz = D * (0.48f - 0.20f * u) * (0.75f + 0.5f * rnd()) * (scale > 0.8f ? 1.15f : 1.0f);   // a rifle's tongue wider
    float heat = 1.0f - 0.55f * u;
    put(q, sz, vec3(3.0f, 1.75f, 0.70f) * heat * (0.8f + 0.4f * rnd()), 0.6f);
  }
  if (!admitLight()) return;
  MuzzleLight L; L.p = light + dir * (0.5f * D); L.life = life + 0.012f; L.k = scale * 1.15f; L.id = 0xF1A50100u + (++mlightSerial & 0xFF); mlights.push_back(L);
}

void Fx::streak(vec3 a, vec3 b, float width, float life, vec4 col) {
  if (streaks.size() >= maxStreaks) streaks.erase(streaks.begin());
  streaks.push_back({a, b, life, life, width, col});
}

// a round's flight segment a -> b (one frame). Tracer kinds (tracerKindFor): 1 a bright red bolt ~6 m long at the
// head of the segment, 2 a short white-yellow streak ~2 m; 0 the faint trail (range presentation: clearly visible).
void Fx::bullet(vec3 a, vec3 b, int kind) {
  if (kind == 0) { streak(a, b, rangeMode ? 0.012f : 0.007f, rangeMode ? 0.05f : 0.025f, rangeMode ? vec4(1.6f, 1.5f, 1.3f, 1) : vec4(0.7f, 0.68f, 0.62f, 1)); return; }
  if (kind >= 3) {   // SW1 blaster bolt: a 1.4 m white-hot core in a wider coloured glow at the head, a fading coloured trail behind it, a small travelling light
    vec3 c = blasterColour(kind), dv = b - a; float len = glm::length(dv);
    vec3 s = len > 1.4f ? b - dv * (1.4f / len) : a;
    streak(s, b, 0.022f, 0.03f, vec4(c * 4.0f + vec3(5.0f), 1));
    streak(s, b, 0.075f, 0.03f, vec4(c * 9.0f, 1));
    streak(a, b, 0.03f, 0.035f, vec4(c * 2.2f, 1));   // the trail fades within ~6 m
    if (!admitLight()) return;
    MuzzleLight L; L.p = b; L.life = 0.03f; L.k = 0.3f; L.col = c; L.id = 0xB0170000u + (++mlightSerial & 0xFF); mlights.push_back(L);
    return;
  }
  float L = kind == 1 ? 6.0f : 2.0f, len = glm::length(b - a);
  vec3 s = len > L ? b - (b - a) * (L / len) : a;
  if (kind == 1) streak(s, b, 0.03f, 0.035f, vec4(14.0f, 1.1f, 0.5f, 1));
  else streak(s, b, 0.012f, 0.03f, vec4(3.2f, 2.9f, 1.8f, 1));
}

void Fx::explosion(vec3 p, vec3 n, const std::string& family, bool ground, const char* munition, vec3 vImpact, float W) {
  if (scaleOn) { explosionScaled(p, n, family, munition ? munition : "", vImpact, W); return; }
  flash(p + n * 0.3f, 3.5f, 0.12f, vec4(9.0f, 6.0f, 2.5f, 1));
  flash(p + n * 0.2f, 1.6f, 0.25f, vec4(5.0f, 2.6f, 0.8f, 1));
  vec4 dust = family == "concrete" ? vec4(0.42f, 0.41f, 0.38f, 0.75f) : family == "timber" ? vec4(0.33f, 0.27f, 0.19f, 0.7f)
             : ground ? vec4(0.35f, 0.29f, 0.21f, 0.8f) : vec4(0.45f, 0.41f, 0.34f, 0.75f);   // W6: kicked-up dry soil is lighter than the ground it came from (it was 0.24 / 0.19 / 0.13: a hole in fog once lit)
  for (int i = 0; i < 22; ++i) {
    vec3 d = glm::normalize(n * 1.3f + vec3(rnd() - 0.5f, rnd() * 0.8f, rnd() - 0.5f) * 1.6f);
    smoke(p + d * 0.3f, d * (2.0f + rnd() * 6.0f), 0.6f + rnd() * 0.8f, 2.5f + rnd() * 3.5f, dust * vec4(0.8f + rnd() * 0.3f, 0.8f + rnd() * 0.3f, 0.8f + rnd() * 0.3f, 1), 1.4f, 0.5f);
  }
  for (int i = 0; i < 10; ++i) smoke(p + vec3(0, 0.5f, 0), vec3(rnd() - 0.5f, 1.5f + rnd() * 2, rnd() - 0.5f), 1.2f, 5.0f + rnd() * 3, vec4(0.28f, 0.27f, 0.25f, 0.55f), 0.9f, 0.9f);   // W6: was 0.16
  int nd = ground ? 10 : 16;
  for (int i = 0; i < nd; ++i) {
    if (debris.size() >= maxDebris) debris.erase(debris.begin());
    Debris d;
    d.p = p + n * 0.15f;
    d.v = glm::normalize(n + vec3(rnd() - 0.5f, rnd() * 0.6f, rnd() - 0.5f) * 1.5f) * (4.0f + rnd() * 9.0f);
    d.w = vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f) * 14.0f;
    float s = 0.05f + rnd() * 0.16f;
    d.size = vec3(s, s * (0.5f + rnd() * 0.6f), s * (0.6f + rnd() * 0.8f));
    d.life = 7.0f + rnd() * 5;
    debris.push_back(d);
  }
}
// ---- firing smoke (content/settings/fx.json): read once; the defaults below match the shipped file
namespace {
struct MuzzleFx {
  float flashSize = 1.4f, flashLife = 0.06f, sideFlashSize = 0.8f, sideFlashLife = 0.05f;
  int sidePuffs = 2, forePuffs = 4;
  float sideSize = 0.4f, sideSpeed = 4.0f, foreSize = 0.5f; vec2 foreSpeed{3, 8};
  float alpha = 0.28f; vec2 life{1.2f, 2.4f}; float grow = 2.2f, lift = 0.35f, drag = 2.2f, fadePow = 1.6f; vec3 col{0.6f, 0.58f, 0.55f};
};
struct BackblastFx { int puffs = 5; float offset = 1.4f, size = 0.25f; vec2 speed{3, 5}; float alpha = 0.4f; vec2 life{1.0f, 1.5f}; float grow = 1.8f, lift = 0.3f, fadePow = 1.3f; vec3 col{0.55f, 0.53f, 0.5f}; };
vec2 jv2(const json& j, const char* k, vec2 d) { return j.contains(k) && j[k].is_array() && j[k].size() >= 2 ? vec2(j[k][0].get<float>(), j[k][1].get<float>()) : d; }
struct FxSettings {
  MuzzleFx m; BackblastFx b;
  FxSettings() {
    json j; try { j = json::parse(readTextFile(contentDir() / "settings" / "fx.json")); } catch (std::exception& e) { logf("fx: settings/fx.json: %s (defaults)", e.what()); return; }
    if (j.contains("muzzle")) {
      const json& q = j["muzzle"];
      m.flashSize = q.value("flashSize", m.flashSize); m.flashLife = q.value("flashLife", m.flashLife);
      m.sideFlashSize = q.value("sideFlashSize", m.sideFlashSize); m.sideFlashLife = q.value("sideFlashLife", m.sideFlashLife);
      m.sidePuffs = q.value("sidePuffsPerSide", m.sidePuffs); m.forePuffs = q.value("forePuffs", m.forePuffs);
      m.sideSize = q.value("sideSize", m.sideSize); m.sideSpeed = q.value("sideSpeed", m.sideSpeed); m.foreSize = q.value("foreSize", m.foreSize);
      m.foreSpeed = jv2(q, "foreSpeed", m.foreSpeed); m.alpha = q.value("alpha", m.alpha); m.life = jv2(q, "life", m.life);
      m.grow = q.value("grow", m.grow); m.lift = q.value("lift", m.lift); m.drag = q.value("drag", m.drag); m.fadePow = q.value("fadePow", m.fadePow);
      m.col = jvec3(q.value("colour", json()), m.col);
    }
    if (j.contains("backblast")) {
      const json& q = j["backblast"];
      b.puffs = q.value("puffs", b.puffs); b.offset = q.value("offset", b.offset); b.size = q.value("size", b.size); b.speed = jv2(q, "speed", b.speed);
      b.alpha = q.value("alpha", b.alpha); b.life = jv2(q, "life", b.life); b.grow = q.value("grow", b.grow); b.lift = q.value("lift", b.lift);
      b.fadePow = q.value("fadePow", b.fadePow); b.col = jvec3(q.value("colour", json()), b.col);
    }
    logf("fx: firing smoke %d puffs, alpha %.2f, life %.1f-%.1f s", m.sidePuffs * 2 + m.forePuffs, m.alpha, m.life.x, m.life.y);
  }
};
const FxSettings& fxSettings() { static FxSettings s; return s; }
}

void Fx::setBudget(float k) {
  k = std::clamp(k, 0.25f, 4.0f);
  maxParts = (size_t)(900 * k); maxStreaks = (size_t)(160 * k); maxDebris = (size_t)(72 * k);
}

void Fx::muzzle(vec3 p, vec3 dir, float scale, const char* gun) {
  const MuzzleFx& m = fxSettings().m;
  if (evOn) {   // fxev: spec 5.1 heavy discharge: the physical flash, the ground dust disc, the bore evacuator; the firing smoke below stays
    FxEvent e; e.id = nextId(-1); e.recipe = FXR_HEAVY; e.pos = p; e.dir = glm::length(dir) > 1e-4f ? glm::normalize(dir) : vec3(0, 0, 1); e.light = p; e.scale = scale;
    e.weapon = gun ? gun : ""; e.seed = (uint32_t)(serial * 2654435761u); emit(e);
  } else
  flash(p + dir * 0.25f * scale, m.flashSize * scale, m.flashLife, vec4(10.0f, 7.0f, 3.0f, 1));
  auto puff = [&](vec3 at, vec3 v, float size) {
    smoke(at, v, size, glm::mix(m.life.x, m.life.y, rnd()), vec4(m.col, m.alpha), m.grow, m.lift);
    parts.back().fade = m.fadePow; parts.back().drag = m.drag;
  };
  for (int s : {-1, 1}) {   // muzzle-brake side blast
    vec3 side = glm::normalize(glm::cross(dir, vec3(0, 1, 0))) * (float)s;
    if (!evOn) flash(p + side * 0.5f * scale, m.sideFlashSize * scale, m.sideFlashLife, vec4(7, 4.5f, 2, 1));
    for (int i = 0; i < m.sidePuffs; ++i) puff(p + side * 0.3f, (side * m.sideSpeed + dir * 1.5f + vec3(0, 0.5f, 0)) * (0.6f + rnd()), m.sideSize * scale);
  }
  for (int i = 0; i < m.forePuffs; ++i) puff(p + dir * 0.6f * scale, dir * glm::mix(m.foreSpeed.x, m.foreSpeed.y, rnd()) + vec3(rnd() - 0.5f, rnd(), rnd() - 0.5f), m.foreSize * scale);
}
void Fx::backblast(vec3 p, vec3 back) {
  const BackblastFx& b = fxSettings().b;
  for (int n = 0; n < b.puffs; ++n) {
    smoke(p, back * glm::mix(b.speed.x, b.speed.y, rnd()) + vec3(rnd() - 0.5f, rnd() * 0.5f, rnd() - 0.5f), b.size, glm::mix(b.life.x, b.life.y, rnd()), vec4(b.col, b.alpha), b.grow, b.lift);
    parts.back().fade = b.fadePow;
  }
}
void Fx::blasterImpact(vec3 p, vec3 n, int kind) {   // SW1
  vec3 c = blasterColour(kind);
  flash(p + n * 0.03f, 0.35f, 0.09f, vec4(c * 8.0f + vec3(2.0f), 1));
  flash(p + n * 0.02f, 0.14f, 0.16f, vec4(c * 3.0f + vec3(3.0f, 2.0f, 1.0f), 1));
  for (int i = 0; i < 7; ++i) {   // sparks: short additive specks thrown off the surface
    if (!admit()) break;
    Particle q; q.p = p + n * 0.02f; q.v = glm::normalize(n * 1.2f + vec3(rnd() - 0.5f, rnd() - 0.3f, rnd() - 0.5f)) * (2.0f + rnd() * 4.0f);
    q.size = 0.025f; q.life = q.maxLife = 0.15f + rnd() * 0.25f; q.col = vec4(c * 3.0f + vec3(4.0f, 3.0f, 2.0f), 1); q.additive = true; q.drag = 2.5f; q.lift = -6.0f; parts.push_back(q);
  }
  for (int i = 0; i < 2; ++i) smoke(p + n * 0.03f, n * (0.4f + rnd() * 0.4f), 0.07f, 0.9f + rnd() * 0.6f, vec4(0.12f, 0.11f, 0.1f, 0.55f), 1.0f, 0.25f);   // scorch smoke
  if (!admitLight()) return;
  MuzzleLight L; L.p = p + n * 0.1f; L.life = 0.12f; L.k = 0.45f; L.col = c; L.id = 0xB0171000u + (++mlightSerial & 0xFF); mlights.push_back(L);
}

void Fx::impact(vec3 p, vec3 n, const std::string& surface) {
  vec4 c = surface == "ground" ? vec4(0.25f, 0.2f, 0.14f, 0.7f) : surface == "wood" ? vec4(0.4f, 0.32f, 0.22f, 0.6f) : surface == "metal" ? vec4(0.5f, 0.5f, 0.5f, 0.4f) : vec4(0.55f, 0.52f, 0.46f, 0.65f);
  for (int i = 0; i < 5; ++i) smoke(p + n * 0.03f, (n * 2.0f + vec3(rnd() - 0.5f, rnd(), rnd() - 0.5f)) * (0.5f + rnd()), 0.08f + rnd() * 0.08f, 0.6f + rnd() * 0.8f, c, 0.8f, 0.2f);
  if (surface == "metal") flash(p + n * 0.02f, 0.18f, 0.05f, vec4(6, 5, 3, 1));
}

void Fx::update(float dt, const Terrain& t) {
  clock += dt; presentT = clock; ground = &t;
  for (size_t k = 0; k < delayed.size();) {
    if (delayed[k].t <= clock) { if (admit()) parts.push_back(delayed[k].q); delayed[k] = delayed.back(); delayed.pop_back(); } else ++k;
  }
  flashes.erase(std::remove_if(flashes.begin(), flashes.end(), [&](const FlashEl& f) { return f.t0 + f.life < clock - 0.25; }), flashes.end());
  for (auto& q : parts) if (q.ret >= 0) q.ret -= dt;
  for (auto& L : mlights) L.age += dt;   // WP4 muzzle lights
  mlights.erase(std::remove_if(mlights.begin(), mlights.end(), [](const MuzzleLight& L) { return L.age >= L.life; }), mlights.end());
  for (auto& q : parts) {
    q.life -= dt;
    { const vec3 w = q.additive ? vec3(0) : vec3(wind.x, 0, wind.y); q.v = w + (q.v - w) * std::exp(-q.drag * dt); }   // weather: smoke / dust settle into the wind (0: still air)
    q.v.y += q.lift * dt;
    q.p += q.v * dt;
    q.size = std::max(0.02f, q.size + q.grow * dt);
    if (q.grain && q.p.y < t.height(q.p.x, q.p.z)) q.life = 0;   // fxscale: a ballistic grain ends where it meets the ground (8.9 Medium: the heightfield)
  }
  parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Particle& q) { return q.life <= 0 || (q.ret < 0.0f && q.ret > -0.5f); }), parts.end());
  for (auto& s : streaks) s.life -= dt;
  streaks.erase(std::remove_if(streaks.begin(), streaks.end(), [](const Streak& s) { return s.life <= 0; }), streaks.end());
  for (auto& d : debris) {
    d.life -= dt;
    if (d.resting) continue;
    d.v.y -= 9.81f * dt;
    d.p += d.v * dt;
    float angle = glm::length(d.w) * dt;
    if (angle > 1e-5f) d.r = glm::angleAxis(angle, glm::normalize(d.w)) * d.r;
    float g = t.height(d.p.x, d.p.z) + d.size.y * 0.5f;
    if (d.p.y < g) {
      d.p.y = g;
      d.v = vec3(d.v.x * 0.45f, -d.v.y * 0.25f, d.v.z * 0.45f);
      d.w *= 0.5f;
      if (glm::length(d.v) < 0.6f) d.resting = true;   // recycled on expiry; never restores the original obstacle
    }
  }
  debris.erase(std::remove_if(debris.begin(), debris.end(), [](const Debris& d) { return d.life <= 0; }), debris.end());
}

void Fx::build(const mat4& view, std::vector<FxVert>& alpha, std::vector<FxVert>& add) const {
  mat4 inv = glm::inverse(view);
  vec3 right = vec3(inv[0]), up = vec3(inv[1]), eye = vec3(inv[3]);
  auto quad = [](std::vector<FxVert>& out, vec3 c, vec3 rx, vec3 ry, vec4 col, vec2 o = vec2(0)) {   // o: uv offset (the shader reads firing-smoke id / flag from it)
    vec3 p0 = c - rx - ry, p1 = c + rx - ry, p2 = c + rx + ry, p3 = c - rx + ry;
    const float r = std::min(glm::length(rx), glm::length(ry));   // W6: the soft fade's radius
    out.push_back({p0, col, o + vec2(0, 0), r}); out.push_back({p1, col, o + vec2(1, 0), r}); out.push_back({p2, col, o + vec2(1, 1), r});
    out.push_back({p0, col, o + vec2(0, 0), r}); out.push_back({p2, col, o + vec2(1, 1), r}); out.push_back({p3, col, o + vec2(0, 1), r});
  };
  const float tanHalf = 0.70f;   // shortcut: the 70 deg vertical view (build has no projection); a zoomed sight caps a little early
  for (const Particle& q0 : parts) {
    float f = std::clamp(q0.life / q0.maxLife, 0.0f, 1.0f);
    vec4 c = q0.col;
    // weather corpus 7.19 / 7.22: a sprite never covers more than about half the view height (camera inside smoke: tens of
    // full-screen layers of overdraw), and an alpha sprite fades out near the eye (d0 = r/2 + 0.1 m .. d0 + r) like the
    // additive ones below; the inside look of a smoke cloud comes from the fog term instead (Renderer::smokeSigma)
    Particle q = q0; float nearA = 1.0f;
    if (nearFade) {
      const float d = glm::length(q.p - eye);
      q.size = std::min(q.size, 0.5f * std::max(d, 0.05f) * tanHalf);
      if (!q.additive) { const float d0 = 0.5f * q0.size + 0.1f; nearA = glm::smoothstep(d0, d0 + q0.size, d); if (nearA * c.a < 1.0f / 255.0f) continue; }
    }
    c.a *= nearA;
    if (q0.ret >= 0) { const float rk = std::clamp(q0.ret / 0.3f, 0.0f, 1.0f); c.a *= rk; if (q0.additive) c *= rk; }   // fxcap: retiring over 0.3 s
    // CV1 fix: an additive sprite at the eye covers the whole screen at full strength (standing in a fire, a light
    // shaft's motes, a flash beside the camera) and stacks to white: fade it out inside ~2 of its own sizes
    static const bool noFade = std::getenv("NMK_FX_NONEARFADE") != nullptr;   // dev A/B: the old behaviour
    const float nearK = q.additive && !noFade ? glm::smoothstep(0.6f * q.size, 2.2f * q.size + 0.15f, glm::length(q.p - eye)) : 1.0f;
    if (nearK <= 0.001f) continue;
    if (q.flame >= 1000 && q.additive) {   // DF1 fireball sprite: its own colour, fading over its life; the tongue's stretched along its travel on screen
      float u = 1.0f - f;
      vec4 cc = vec4(vec3(q.col) * std::clamp(f * 1.6f, 0.0f, 1.0f) * nearK, 200.0f + std::floor(q.flame - 1000.0f) + 0.99f * u);
      float a = glm::dot(q.v, right), b = glm::dot(q.v, up), l = std::sqrt(a * a + b * b);
      vec3 ax = l > 1e-4f ? (right * a + up * b) / l : right, pr = l > 1e-4f ? (up * a - right * b) / l : up;
      float st = 1.0f + std::min(0.8f, glm::length(q.v)) * (l / std::max(1e-4f, glm::length(q.v)));
      quad(add, q.p, ax * (q.size * st), pr * q.size, cc);
      continue;
    }
    if (q.flame >= 0 && q.additive) {   // flame sprite: white-yellow -> orange -> dark red along its life; a >= 100 tells the shader (id + age)
      float u = 1.0f - f;
      vec3 g = u < 0.25f ? glm::mix(vec3(1.0f, 0.96f, 0.75f), vec3(1.0f, 0.72f, 0.2f), u / 0.25f)
             : u < 0.6f ? glm::mix(vec3(1.0f, 0.72f, 0.2f), vec3(0.9f, 0.28f, 0.04f), (u - 0.25f) / 0.35f)
                        : glm::mix(vec3(0.9f, 0.28f, 0.04f), vec3(0.3f, 0.03f, 0.0f), (u - 0.6f) / 0.4f);
      float fadeOut = std::clamp(f / 0.18f, 0.0f, 1.0f);
      c = vec4(g * q.col.x * fadeOut * nearK, 100.0f + std::floor(q.flame) + 0.99f * u);
      quad(add, q.p, right * q.size, up * q.size, c);
      continue;
    }
    if (q.additive) c *= f * nearK;
    else if (q.fade > 0) {   // firing smoke: thins out from the start; an eroded wisp stretched along its screen-space drift (muzzle direction, then wind)
      c.a *= std::pow(f, q.fade) * std::min(1.0f, (1 - f) * 10.0f + 0.25f);
      float a = glm::dot(q.v, right), b = glm::dot(q.v, up), l = std::sqrt(a * a + b * b);
      vec3 ax = l > 0.05f ? (right * a + up * b) / l : right, pr = l > 0.05f ? (up * a - right * b) / l : up;
      float st = 1.0f + 0.7f * std::min(1.0f, l / 3.0f);
      if (c.a < 1.0f / 255.0f) continue;
      int id = (int)(std::fmod(q.maxLife * 97.13f, 1.0f) * 64.0f);
      quad(alpha, q.p, ax * (q.size * st), pr * (q.size * 0.85f), c, vec2(2.0f * id, 4.0f));
      continue;
    }
    else c.a *= std::min(1.0f, f * 2.0f) * std::min(1.0f, (1 - f) * 8.0f + 0.2f);
    if (!q.additive && c.a < 1.0f / 255.0f) continue;   // an invisible quad still costs its overdraw
    quad(q.additive ? add : alpha, q.p, right * q.size, up * q.size, c);
  }
  buildFlashes(eye, add);
  for (auto& s : streaks) {
    vec3 d = s.b - s.a;
    if (glm::length(d) < 1e-4f) continue;
    vec3 mid = (s.a + s.b) * 0.5f;
    vec3 side = glm::normalize(glm::cross(d, mid - eye)) * s.width;
    float f = std::clamp(s.life / s.maxLife, 0.0f, 1.0f);
    vec4 c = s.col * f;
    // streak uses uv.x = 0.5 band so the fragment shader softens it across its width
    vec3 p0 = s.a - side, p1 = s.a + side, p2 = s.b + side, p3 = s.b - side;
    add.push_back({p0, c, {0, 0.5f}}); add.push_back({p1, c, {1, 0.5f}}); add.push_back({p2, c, {1, 0.5f}});
    add.push_back({p0, c, {0, 0.5f}}); add.push_back({p2, c, {1, 0.5f}}); add.push_back({p3, c, {0, 0.5f}});
  }
}

// V4 furniture breaks (interior.cpp): small fast chips that fall under gravity (wood splinters, faience chips, glass), and
// a slow low dust cloud when a piece is blown apart. Both respect maxParts like every other effect.
void Fx::splinters(vec3 p, vec3 v, int n, vec4 col, float size) {
  for (int k = 0; k < n; ++k) {
    if (!admit()) return;
    Particle q;
    vec3 r(rnd() - 0.5f, rnd() * 0.9f + 0.1f, rnd() - 0.5f);
    q.p = p + r * 0.25f; q.v = v * (0.4f + 0.8f * rnd()) + r * (2.5f + 3.5f * rnd());
    q.size = size * (0.6f + 0.9f * rnd()); q.life = q.maxLife = 0.6f + 0.8f * rnd();
    q.col = vec4(vec3(col) * (0.75f + 0.5f * rnd()), col.a); q.grow = 0; q.drag = 0.9f; q.lift = -9.0f;
    parts.push_back(q);
  }
}
void Fx::dust(vec3 p, float radius, vec4 col) {
  int n = 6 + (int)(radius * 6);
  for (int k = 0; k < n; ++k) {
    vec3 r(rnd() - 0.5f, rnd() * 0.5f, rnd() - 0.5f);
    smoke(p + r * radius, r * 0.8f + vec3(0, 0.15f, 0), 0.25f + 0.35f * radius * rnd(), 2.2f + 2.0f * rnd(), col, 0.5f + 0.3f * rnd(), 0.05f);
  }
}

// ================================================================ Battle Foundry phase 4: events, flashes, caps (fxev / fxcap)
// spec 5.1-5.8 (docs/vfx-rev3/BATTLE-FOUNDRY-SPEC.md), data content/settings/vfx (fxdata.h). Every number that has a row is read.
bool Fx::admit(bool cue) {
  if (!capOn) { if (parts.size() >= maxParts) parts.pop_front(); return true; }
  if (parts.size() < maxParts) return true;
  if (!cue) return false;   // caps apply to new spawns: refused, nothing live despawns
  for (auto& q : parts) if (q.ret < 0) { q.ret = 0.3f; return true; }   // a cue retires the oldest live sprite over 0.3 s instead
  return false;
}
bool Fx::admitLight() {
  if (!capOn) { if (mlights.size() >= 8) mlights.erase(mlights.begin()); return true; }
  return (int)mlights.size() < std::max(1, caps.flashLights);   // spec 8.1: a flash light survives at every tier
}
void Fx::setCaps(const Caps& c) {
  caps = c; caps.flashLights = std::max(1, c.flashLights); caps.dustPerBurst = std::max(1, c.dustPerBurst);   // the two floors (8.1 items 4 / 5)
  if (c.soft > 0) maxParts = (size_t)c.soft;
  if (!capOn) return;
  size_t live = 0; for (auto& q : parts) live += q.ret < 0;
  for (auto& q : parts) { if (live <= maxParts) break; if (q.ret < 0) { q.ret = 0.3f; --live; } }   // oldest first, faded, never snapped
  for (size_t k = 0; k + (size_t)caps.flashLights < mlights.size(); ++k) mlights[k].life = std::min(mlights[k].life, mlights[k].age + 0.3f);
}
vec3 Fx::emission(float T, float eps) const {
  const FxData& D = fxData();
  const float y = std::max(yrefNits, 1e-4f), mult = eps * D.lv(T) / y;
  return D.hue(T) * (peakClamp > 0 ? std::min(mult, peakClamp) : mult) * y * kNitsToScene;
}
float Fx::envelopeAvg(double t0, float life, double a, double b) {
  if (life <= 0) return 0;
  auto F = [&](double t) { double u = std::clamp((t - t0) / life, 0.0, 1.0); return u - u * u + u * u * u / 3.0; };   // antiderivative of (1-u)^2
  if (b - a < 1e-6) { double u = (b - t0) / life; return u < 0 || u > 1 ? 0.0f : (float)((1 - u) * (1 - u)); }
  return (float)std::min(1.0, (F(b) - F(a)) * life / (b - a));   // the interval's mean, never above the peak
}
static uint32_t hashU(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
static float hash01(uint32_t a, uint32_t b) { return (hashU(a * 0x9E3779B9u ^ hashU(b)) & 0xFFFFFF) / 16777216.0f; }

// heavy guns by vehicle / station name: the nearest weapons.json row and the real bore (x_Mach rescaled linearly)
static FxWeaponRow fxHeavyFor(const std::string& name, float scale) {
  const FxData& D = fxData();
  if (const FxWeaponRow* r = D.weapon(name)) return *r;
  auto has = [&](const char* k) { return name.find(k) != std::string::npos; };
  const char* id = "atgun_76mm_zis3"; float bore = 0;
  if (has("abrams")) id = "tankgun_120mm_m256";
  else if (has("bradley")) { id = "autocannon_20mm"; bore = 25; }
  else if (has("tiger") || has("flak")) bore = 88;
  else if (has("firefly")) bore = 76.2f;
  else if (has("sherman") || has("panzer") || has("pak")) bore = 75;
  else if (name.empty()) id = scale < 0.7f ? "autocannon_20mm" : scale > 1.45f ? "tankgun_120mm_m256" : "atgun_76mm_zis3";
  FxWeaponRow r = D.weapon(id) ? *D.weapon(id) : FxWeaponRow{};
  if (bore > 0) { const float k = bore * 1e-3f / r.boreM; r.xMach *= k; r.envL *= k; r.envR *= k; r.boreM = bore * 1e-3f; r.dustR *= std::cbrt(k * k * k); }
  return r;
}

bool Fx::emit(const FxEvent& e0) {
  FxEvent e = e0; if (e.visTime < 0) e.visTime = clock;
  if (e.id) {   // spec 9.2 EVENTS: no duplicate activation per id
    if (seenIds.count(e.id)) return false;
    seenIds.insert(e.id); seenOrder.push_back(e.id);
    if (seenOrder.size() > 2048) { seenIds.erase(seenOrder.front()); seenOrder.pop_front(); }
  }
  const float age = (float)(clock - e.visTime);
  const bool heavy = e.recipe == FXR_HEAVY;
  const FxWeaponRow W = heavy ? fxHeavyFor(e.weapon, e.scale) : fxWeaponFor(e.weapon, e.caliber);
  const FxDeviceMod dev = fxData().device(e.device);
  const float smokeLife = heavy ? 20.0f : 5.0f;
  if (age > smokeLife) return false;   // absolute-age skip: everything this event made is over
  vec3 dir = glm::length(e.dir) > 1e-4f ? glm::normalize(e.dir) : vec3(0, 0, 1);
  const uint32_t sd = e.seed ? e.seed : (uint32_t)e.id;
  const float sc = 1.0f + (hash01(sd, 7) - 0.5f) * 0.24f;   // spec 5.6: +-12 % at most
  const float xM = W.xMach * dev.xMach * sc, L = W.envL * dev.envelope * sc, R = W.envR * dev.envelope * sc;
  // presentation life (5.6): small 12-35 ms, heavy core 35-85 ms; the brightness is integrated over it (5.5), so energy is frame-rate free
  const float life = heavy ? std::clamp(2.0f * W.tVis, 0.035f, 0.085f) : std::clamp(6.0f * W.tVis, 0.012f, 0.035f);
  if (age < life * (heavy ? 1.6f : 1.0f)) {
    FlashEl f; f.t0 = e.visTime; f.life = life; f.p = e.pos; f.dir = dir; f.roll = hash01(sd, 1) * 6.2831853f; f.xM = xM; f.L = L; f.R = R;
    f.half = e.device == "brake" ? 30.0f : e.device == "flash_hider" ? 10.0f : 15.0f;
    f.emPrim = emission(W.Tprim, heavy ? 0.5f : 0.12f); f.emDisc = emission(heavy ? std::max(W.Tprim, 2000.0f) : 1800.0f, heavy ? 0.5f : 0.20f);
    f.emSec = emission(W.Tsec, W.epsSec) * dev.secondary;
    const int coreTris[5] = {0, 0, 60, 120, 200};   // spec 8.3 flash core form: billboard, billboard, swept mesh 60 / 120 / 200
    const int t = std::clamp(tier, 0, 4);
    if (coreTris[t] > 0) { f.kind = 0; f.segs = coreTris[t]; flashes.push_back(f); }
    else { FlashEl b = f; b.kind = 2; b.emSec = f.emDisc; b.R = 0.6f * L; b.p = e.pos + dir * (0.4f * L); flashes.push_back(b); }
    if (t >= 2) { FlashEl d = f; d.kind = 1; d.R = 0.4f * xM; d.p = e.pos + dir * xM; flashes.push_back(d); }   // E2 intermediate disc (Medium+)
    if (dev.secondary > 0.01f) {   // E3: the secondary-flash vortex, 3-6 x_Mach out (heavy: the longer dim edge envelope)
      const int nSec[5] = {1, 1, 2, 3, 4};
      for (int k = 0; k < nSec[t]; ++k) {
        FlashEl v = f; v.kind = 2; v.life = life * (heavy ? 1.6f : 1.0f); v.roll = hash01(sd, 20 + k) * 255.0f;
        const float along = xM * (3.0f + 3.0f * hash01(sd, 30 + k)) * dev.envelope;
        vec3 up = std::fabs(dir.y) > 0.95f ? vec3(1, 0, 0) : vec3(0, 1, 0), s1 = glm::normalize(glm::cross(dir, up)), s2 = glm::cross(s1, dir);
        const float a = hash01(sd, 40 + k) * 6.2831853f;
        v.p = e.pos + dir * std::min(along, L) + (s1 * std::cos(a) + s2 * std::sin(a)) * (0.3f * R);
        v.R = xM * (0.8f + 0.4f * hash01(sd, 50 + k)) * (heavy ? 0.7f : 1.0f); flashes.push_back(v);
      }
    }
  }
  // the flash light (the lighting session's combustion light replaces this; until then the WP4 light, its envelope as before)
  if (brightOn && age < 0.06f) {   // fxbright: one combustion light from T, eps and size (spec 5.6), its envelope the flash's
    CombSrc c; c.p = glm::length(e.light) > 0 ? e.light : e.pos; c.t0 = e.visTime; c.life = life * (heavy ? 1.6f : 1.0f);
    c.T = heavy ? 2400.0f : 2200.0f; c.eps = (heavy ? 0.55f : 0.2f) * (e.device == "suppressor" ? 0.1f : 1.0f); c.d = 2.8f * W.envR * dev.envelope; combSrc.push_back(c);
  } else if (age < 0.06f && admitLight()) {
    MuzzleLight Lt; Lt.p = glm::length(e.light) > 0 ? e.light : e.pos; Lt.life = (heavy ? 0.12f : 0.035f) * (e.device == "suppressor" ? 0.3f : 1.0f);
    Lt.k = heavy ? 3.0f * e.scale : e.scale * (e.device == "suppressor" ? 0.2f : 1.0f); Lt.id = 0xF1A50100u + (++mlightSerial & 0xFF);
    Lt.col = fxData().hue(2200); Lt.age = age; mlights.push_back(Lt);
  }
  auto spawn = [&](Particle q, float delay, bool cue) {   // world space: detached from the gun from the start
    if (delay > age) { Delayed d; d.t = e.visTime + delay; d.q = q; delayed.push_back(d); return; }
    q.life -= age - delay; if (q.life <= 0 || !admit(cue)) return; parts.push_back(q);
  };
  // suppressor: more smoke, released late (5.7 / 5.8)
  if (dev.smokeScale > 1.01f) for (int k = 0; k < 2; ++k) {
    Particle q; q.p = e.pos + dir * 0.03f; q.v = dir * (0.3f + 0.3f * hash01(sd, 60 + k)) + vec3(0, 0.15f, 0);
    q.size = 0.05f * dev.smokeScale; q.life = q.maxLife = 2.0f + 2.0f * hash01(sd, 62 + k); q.col = vec4(0.62f, 0.6f, 0.57f, 0.22f); q.grow = 0.35f; q.drag = 2.0f; q.lift = 0.15f; q.fade = 1.4f;
    spawn(q, dev.smokeDelay, false);
  }
  // E4 ground dust disc (5.7): a muzzle low over loose ground; asymmetric annulus expanding as R_max (1 - exp(-t / 0.055))
  const float tierDust[5] = {0, 8, 16, 28, 40};
  int nDust = std::min((int)tierDust[std::clamp(tier, 0, 4)], caps.groundDust > 0 ? caps.groundDust : 999);
  if (tier == 1 && !heavy) nDust = 0;   // Low: large calibre only
  const vec3 mw = glm::length(e.light) > 0 ? e.light : e.pos;
  const float gy = e.groundY > -1e8f ? e.groundY : ground ? ground->height(mw.x, mw.z) : -1e9f;
  const float h = mw.y - gy, trig = 3.0f * W.boreM * std::sqrt(W.pMuzzle / 101325.0f) * 0.5f;
  const float loose = e.surface == "grass" || e.surface == "concrete" || e.surface == "water" || e.surface == "wood" ? 0.0f
                    : wxSnow > 0.02f ? 0.65f : wxWet < 0.2f ? 0.65f : wxWet < 0.6f ? 0.15f : 0.0f;   // dry 0.5-0.8, damp 0.1-0.2, wet 0
  if (nDust > 0 && h > -0.2f && h < trig && loose > 0) {
    const float Rmax = W.dustR * dev.dust;
    vec3 fw = glm::normalize(vec3(dir.x, 0, dir.z) + vec3(1e-4f, 0, 0)), sw = vec3(-fw.z, 0, fw.x);
    const vec3 o = vec3(mw.x, gy, mw.z) + fw * (xM * (0.3f + 0.5f * hash01(sd, 70)));
    for (int k = 0; k < nDust; ++k) {
      const float phi = (k + hash01(sd, 100 + k)) * 6.2831853f / nDust, c = std::cos(phi);
      float rk = e.device == "brake" ? 1.0f * (0.7f + 0.3f * std::fabs(std::sin(phi))) : c >= 0 ? 0.55f + 0.45f * c : 0.55f + 0.30f * c;   // 1 fwd / 0.55 side / 0.25 back
      const float frac = 0.55f + 0.45f * hash01(sd, 200 + k);   // between the inner (0.55) and outer edge
      vec3 rd = fw * c + sw * std::sin(phi);
      Particle q; q.p = o + rd * 0.05f + vec3(0, 0.15f + 0.3f * hash01(sd, 300 + k), 0);
      q.drag = 1.0f / 0.055f; q.v = rd * (Rmax * rk * frac * q.drag);   // x(t) = v0 / k (1 - exp(-k t)) = R (1 - exp(-t / 0.055))
      q.size = std::max(0.15f, 0.22f * Rmax * rk); q.grow = 0.08f * Rmax; q.lift = 0.05f;
      q.life = q.maxLife = 3.0f + 5.0f * hash01(sd, 400 + k); q.col = vec4(0.47f, 0.42f, 0.34f, loose);
      spawn(q, 0.0f, k == 0);   // at least one puff is a cue (8.1 item 5)
    }
  }
  // bore evacuator (5.7): 0.4-1.5 s after the shot, a smaller puff at the bulge and the muzzle (Medium+, guns that have one)
  const bool evac = e.evacuator || W.id == "tankgun_105mm_m68" || W.id == "tankgun_120mm_m256" || W.id == "tankgun_125mm_2a46";
  if (heavy && evac && tier >= 2) {
    const float delay = 0.4f + 1.1f * hash01(sd, 80);
    for (int k = 0; k < 4; ++k) {
      Particle q; q.p = e.pos - dir * (k < 2 ? 1.8f : 0.0f) + vec3(0, 0.05f, 0); q.v = (k < 2 ? vec3(0, 0.6f, 0) : dir * 1.2f) + vec3(hash01(sd, 90 + k) - 0.5f, 0.2f, hash01(sd, 95 + k) - 0.5f) * 0.4f;
      q.size = 0.35f; q.grow = 0.5f; q.life = q.maxLife = 3.0f + 2.0f * hash01(sd, 85 + k); q.col = vec4(0.6f, 0.58f, 0.55f, 0.3f); q.drag = 1.5f; q.lift = 0.25f; q.fade = 1.3f;
      spawn(q, delay, false);
    }
  }
  return true;
}

// the flash elements, once per drawn frame: each element's (1-u)^2 envelope averaged over the interval since the last draw
void Fx::buildFlashes(vec3 eye, std::vector<FxVert>& add) const {
  double b = presentT, a = lastPresent;
  if (a < 0 || b - a <= 1e-6 || b - a > 0.25) a = b - 1.0 / 60.0;   // first draw / a repeat at the same instant / a long stall
  lastPresent = b; presentA = a;
  for (const FlashEl& f : flashes) {
    if (f.kind == 3) {   // the cooling fireball: emission of T(t) averaged over the drawn interval (6 samples), radius growing to R_fb
      const double s0 = std::max(a, f.t0), s1 = std::min(b, f.t0 + (double)f.life);
      if (s1 <= s0) continue;
      vec3 em(0);
      for (int j = 0; j < 6; ++j) { const double tt = s0 + (s1 - s0) * (j + 0.5) / 6.0 - f.t0; em += emission(300.0f + (f.T0 - 300.0f) * (float)std::pow(1.0 + tt / f.tau, -(double)f.n), f.eps); }
      em *= (float)((s1 - s0) / (b - a)) / 6.0f * f.k;
      const float u = (float)std::clamp((b - f.t0) / f.life, 0.0, 0.99), Rt = f.R * std::max(0.15f, 1.0f - std::exp(-2.6f * u / 0.19f));
      const vec3 rt = glm::normalize(glm::cross(f.p - eye, vec3(0, 1, 0)) + vec3(1e-5f)), upv = glm::normalize(glm::cross(rt, f.p - eye));
      const vec4 c(em, 200.0f + std::floor(std::fmod(f.roll, 255.0f)) + 0.99f * u);
      const vec3 rx = rt * Rt, ry = upv * Rt, p0 = f.p - rx - ry, p1 = f.p + rx - ry, p2 = f.p + rx + ry, p3 = f.p - rx + ry;
      add.push_back({p0, c, {0, 0}, 0}); add.push_back({p1, c, {1, 0}, 0}); add.push_back({p2, c, {1, 1}, 0});
      add.push_back({p0, c, {0, 0}, 0}); add.push_back({p2, c, {1, 1}, 0}); add.push_back({p3, c, {0, 1}, 0});
      continue;
    }
    const float k = envelopeAvg(f.t0, f.life, a, b);
    if (k <= 1e-4f) continue;
    vec3 up = std::fabs(f.dir.y) > 0.95f ? vec3(1, 0, 0) : vec3(0, 1, 0), s1 = glm::normalize(glm::cross(f.dir, up)), s2 = glm::cross(s1, f.dir);
    { const float cr = std::cos(f.roll), sr = std::sin(f.roll); vec3 t1 = s1 * cr + s2 * sr; s2 = glm::cross(t1, f.dir); s1 = t1; }
    auto V = [&](vec3 p, vec3 c) { add.push_back({p, vec4(c, 1), vec2(0.5f), 0}); };   // uv at the sprite centre: the vertex colour as given
    if (f.kind == 0) {   // E1 swept core: r(x) = R (x / x0)^0.45 exp(-1.2 (x / x0 - 1)^2), x0 = 0.6 x_Mach; soft at the silhouette
      const int M = f.segs >= 200 ? 10 : f.segs >= 120 ? 10 : 6, S = std::max(2, f.segs / (2 * M));
      const float x0 = 0.6f * f.xM, len = std::min(f.L, 3.2f * x0);
      auto ring = [&](int i, int j, vec3& p, vec3& c) {
        const float x = len * i / S, u = x / x0, r = i == 0 ? 0.0f : f.R * std::pow(u, 0.45f) * std::exp(-1.2f * (u - 1) * (u - 1));
        const float ang = j * 6.2831853f / M; const vec3 n = s1 * std::cos(ang) + s2 * std::sin(ang);
        const float rr = std::min(r, x * std::tan(glm::radians(f.half)) + 0.25f * f.R);   // the half-angle at the muzzle
        p = f.p + f.dir * x + n * rr;
        const vec3 vd = glm::normalize(eye - p); const float facing = std::pow(std::fabs(glm::dot(n, vd)), 1.5f);
        c = glm::mix(f.emPrim, f.emDisc, std::clamp(u / 1.67f, 0.0f, 1.0f)) * (k * facing * (1.0f - (float)i / S * 0.5f));
      };
      for (int i = 0; i < S; ++i) for (int j = 0; j < M; ++j) {
        vec3 p00, p01, p10, p11, c00, c01, c10, c11;
        ring(i, j, p00, c00); ring(i, j + 1, p01, c01); ring(i + 1, j, p10, c10); ring(i + 1, j + 1, p11, c11);
        V(p00, c00); V(p10, c10); V(p11, c11); V(p00, c00); V(p11, c11); V(p01, c01);
      }
    } else if (f.kind == 1) {   // E2 dished disc at x_Mach, normal to the bore (80 deg half-angle cone), brightest at its inner edge: pow(1 - r/R, 0.6)
      const int M = 8; const float dish = f.R * 0.176f;
      for (int j = 0; j < M; ++j) for (int ringI = 0; ringI < 2; ++ringI) {
        auto P = [&](float rf, int jj) { const float ang = jj * 6.2831853f / M; return f.p - f.dir * (dish * rf) + (s1 * std::cos(ang) + s2 * std::sin(ang)) * (f.R * rf); };
        auto C = [&](float rf) { return f.emDisc * (k * std::pow(1.0f - rf, 0.6f)); };
        const float r0 = ringI * 0.5f, r1 = r0 + 0.5f;
        if (ringI == 0) { V(f.p, C(0)); V(P(r1, j), C(r1)); V(P(r1, j + 1), C(r1)); }
        else { V(P(r0, j), C(r0)); V(P(r1, j), C(r1)); V(P(r1, j + 1), C(r1)); V(P(r0, j), C(r0)); V(P(r1, j + 1), C(r1)); V(P(r0, j + 1), C(r0)); }
      }
    } else {   // E3 / low-tier core: a noise-ragged billboard (the DF1 fireball sprite: alpha carries 200 + id + age)
      const vec3 rt = glm::normalize(glm::cross(f.p - eye, vec3(0, 1, 0)) + vec3(1e-5f)), upv = glm::normalize(glm::cross(rt, f.p - eye));
      const float u = (float)std::clamp((b - f.t0) / f.life, 0.0, 0.99);
      const vec4 c(f.emSec * k, 200.0f + std::floor(std::fmod(f.roll, 255.0f)) + 0.99f * u);
      const vec3 rx = rt * f.R, ry = upv * f.R, p0 = f.p - rx - ry, p1 = f.p + rx - ry, p2 = f.p + rx + ry, p3 = f.p - rx + ry;
      add.push_back({p0, c, {0, 0}, 0}); add.push_back({p1, c, {1, 0}, 0}); add.push_back({p2, c, {1, 1}, 0});
      add.push_back({p0, c, {0, 0}, 0}); add.push_back({p2, c, {1, 1}, 0}); add.push_back({p3, c, {0, 1}, 0});
    }
  }
}

// ---- --check-fxev, the event and brightness assertions (spec 9.2 EVENTS / BRIGHTNESS), headless
int fxEventSelfCheck(int& fails) {
  auto check = [&](bool ok, const std::string& what) { logf("[fxev] %s %s", ok ? "PASS" : "FAIL", what.c_str()); if (!ok) ++fails; };
  // envelope: energy over a 30 ms flash equal at 30 / 60 / 120 presentation rates (within one presentation frame's rounding)
  for (float life : {0.015f, 0.035f, 0.085f}) {
    float E[3]; const float rates[3] = {30, 60, 120};
    for (int r = 0; r < 3; ++r) { double dt = 1.0 / rates[r], e = 0; for (int i = 0; i < 40; ++i) { double a = 0.0037 + i * dt; e += Fx::envelopeAvg(0.0037 + 0.004, life, a, a + dt) * dt; } E[r] = (float)e; }
    const float ref = life / 3.0f;
    check(std::fabs(E[0] / ref - 1) < 0.01f && std::fabs(E[1] / ref - 1) < 0.01f && std::fabs(E[2] / ref - 1) < 0.01f,
          "flash energy at 30 / 60 / 120 fps = life / 3 (life " + std::to_string((int)(life * 1000)) + " ms): " + std::to_string(E[0] / ref) + " " + std::to_string(E[1] / ref) + " " + std::to_string(E[2] / ref));
  }
  check(Fx::envelopeAvg(1.0, 0.02f, 0.97, 0.9999) == 0.0f && Fx::envelopeAvg(1.0, 0.02f, 1.0201, 1.05) == 0.0f && Fx::envelopeAvg(1.0, 0.02f, 0.99, 1.0001) > 0.0f
        && Fx::envelopeAvg(1.0, 0.02f, 1.0199, 1.0201) > 0.0f, "envelope zero just before start and just after end, nonzero across both endpoints");
  check(Fx::envelopeAvg(0.0, 0.02f, 0.0, 0.0) == 1.0f && Fx::envelopeAvg(0.0, 0.02f, -0.001, 0.0005) <= 1.0f, "envelope peaks at 1 at the start, an average never above the peak");
  // emission: the same authored values, invisible in sun, dominant at night (spec 5.4 / 9.2)
  Fx fx; fx.peakClamp = 60;
  fx.yrefNits = fxYrefNits(100000); const vec3 primSun = fx.emission(1645, 0.12f) / (fx.yrefNits * Fx::kNitsToScene);
  fx.yrefNits = fxYrefNits(0.25f); const vec3 primMoon = fx.emission(1645, 0.12f) / (fx.yrefNits * Fx::kNitsToScene);
  check(primSun.r < 0.3f && primMoon.r >= 59.0f, "rifle primary flash: " + std::to_string(primSun.r) + " x Yref in full sun (invisible), " + std::to_string(primMoon.r) + " x at full moon (clamped 60)");
  check(fx.emission(1645, 0.12f).g / std::max(1e-9f, fx.emission(1645, 0.12f).r) < 0.25f && fx.emission(2500, 0.25f).b < fx.emission(2500, 0.25f).g,
        "flash palette from the blackbody LUT: orange (g/r 0.19 at 1645 K), no authored white");
  // events: duplicate ids, absolute age, world-space smoke, pools
  Fx f; f.tier = 3; f.wxWet = 0; f.clock = f.presentT = 100.0; FxEvent e; e.id = 42; e.recipe = FXR_HEAVY; e.weapon = "tankgun_120mm_m256"; e.pos = vec3(0, 1.5f, 0); e.dir = vec3(0, 0, 1); e.groundY = 0;
  check(f.emit(e) && !f.emit(e), "no duplicate activation per event id");
  const size_t nf = f.flashes.size(), np = f.parts.size(), nd = f.delayed.size();
  check(nf >= 3 && np >= 16 && nd == 4, "120 mm discharge: core + disc + secondary (" + std::to_string(nf) + "), ground dust disc " + std::to_string(np) + " sprites, evacuator puffs queued " + std::to_string(nd));
  FxEvent old = e; old.id = 43; old.visTime = f.clock - 30.0; check(!f.emit(old), "an event older than its longest phase is skipped by absolute age");
  FxEvent late = e; late.id = 44; late.visTime = f.clock - 1.0; const size_t fl0 = f.flashes.size(); f.emit(late);
  check(f.flashes.size() == fl0, "a late event (1 s) shows no flash, only what continues at its age");
  if (f.parts.empty()) { check(false, "no particles to test world space"); return fails; }
  vec3 p0 = f.parts.front().p; e.pos += vec3(0, 0, 5);   // the gun traverses on: the dust and smoke stay where they were born
  check(glm::length(f.parts.front().p - p0) < 1e-6f, "puffs are world space: a later muzzle move does not move them");
  f.maxParts = 20; f.capOn = true; const size_t before = f.parts.size(); FxEvent e2 = e; e2.id = 45; f.emit(e2);
  check(f.parts.size() <= std::max(before, (size_t)20) + 1, "pool cap enforced: at a cap of 20 only the cue sprite gets in (" + std::to_string(before) + " -> " + std::to_string(f.parts.size()) + ")");
  Fx::Caps c = f.caps; c.soft = 5; f.setCaps(c); int ret = 0; for (auto& q : f.parts) ret += q.ret >= 0;
  check(ret == (int)f.parts.size() - 5, "a lowered cap retires the oldest over 0.3 s (" + std::to_string(ret) + " fading, none removed at once)");
  c.flashLights = 0; c.dustPerBurst = 0; f.setCaps(c); check(f.caps.flashLights == 1 && f.caps.dustPerBurst == 1, "floors: a flash light and a dust puff survive at every tier");
  // SCALE (spec 9.2 / phase 5): grenade, 105 mm, 155 mm side by side differ in size and in rate; 2 x W -> x1.26 radius and times
  struct Burst { float life = 0, rMean = 0, plumeH = 0; int dust = 0, grains = 0, clods = 0; float fireK = 0; };
  auto burst = [&](const char* mun, const char* surf) {
    Fx x; x.tier = 3; x.clock = x.presentT = 50.0; x.maxParts = 100000; x.maxDebris = 1000; x.maxStreaks = 1000; x.caps.grains = 1 << 20; x.caps.clods = 1000; x.caps.dustPerBurst = 1000;
    const std::string sid = surf; x.surfaceOf = [sid](vec3, const std::string&) { return sid; };
    x.explosion(vec3(0), vec3(0, 1, 0), "ground", true, mun);
    Burst b; int nf = 0;
    for (auto& fl : x.flashes) if (fl.kind == 3) { b.life = fl.life; b.rMean += fl.R; b.fireK = fl.k; ++nf; }
    if (nf) b.rMean /= nf;
    for (auto& q : x.parts) (q.grain ? b.grains : b.dust)++;
    b.clods = (int)x.debris.size();
    for (auto& d : x.delayed) b.plumeH = std::max(b.plumeH, d.q.v.y / std::max(1e-3f, d.q.drag));
    return b;
  };
  const Burst g = burst("grenade_M67", "dry_soil"), h105 = burst("he_105mm_M1", "dry_soil"), h155 = burst("he_155mm_M107", "dry_soil");
  const FxMunitionRow Mg = fxMunitionFor("grenade_M67", -1), M1 = fxMunitionFor("he_105mm_M1", -1), M5 = fxMunitionFor("he_155mm_M107", -1);
  char buf[400];
  std::snprintf(buf, sizeof buf, "grenade / 105 / 155: fireball life %.0f / %.0f / %.0f ms, mean sprite radius %.2f / %.2f / %.2f m, plume to %.1f / %.1f / %.1f m",
                g.life * 1000, h105.life * 1000, h155.life * 1000, g.rMean, h105.rMean, h155.rMean, g.plumeH, h105.plumeH, h155.plumeH);
  check(g.life < h105.life && h105.life < h155.life && g.plumeH < h105.plumeH && h105.plumeH < h155.plumeH && std::fabs(h155.life / g.life / std::cbrt(M5.W / Mg.W) - 1) < 0.03f, buf);
  check(std::fabs(g.plumeH / (0.9f * Mg.apex) - 1) < 0.05f && std::fabs(h155.plumeH / (0.9f * M5.apex) - 1) < 0.05f && M1.apex / M1.craterD > 7 && M1.apex / M1.craterD < 9,
        "the plume tops out at 0.9 x the column apex, apex ~ 8 x crater diameter (grenade " + std::to_string(Mg.apex / Mg.craterD) + ", 155 mm " + std::to_string(M5.apex / M5.craterD) + ")");
  const FxMunitionRow w1 = fxMunitionFor("", 1.0f), w2 = fxMunitionFor("", 2.0f);
  check(std::fabs(w2.Rfb / w1.Rfb - 1.26f) < 0.005f && std::fabs(w2.tLum / w1.tLum - 1.26f) < 0.005f && std::fabs(w2.tPeak / w1.tPeak - 1.26f) < 0.005f,
        "doubling the charge: fireball radius x" + std::to_string(w2.Rfb / w1.Rfb) + ", times x" + std::to_string(w2.tLum / w1.tLum));
  const Burst sand = burst("he_105mm_M1", "dry_sand"), wat = burst("he_105mm_M1", "water_shallow");
  std::snprintf(buf, sizeof buf, "dry sand vs shallow water (105 mm): dust %d / %d, grains %d / %d, clods %d / %d, fireball x%.2f / x%.2f",
                sand.dust, wat.dust, sand.grains, wat.grains, sand.clods, wat.clods, sand.fireK, wat.fireK);
  check(sand.dust > 10 * std::max(1, wat.dust - 0) && wat.dust >= 1 && sand.grains > 0 && wat.grains == 0 && wat.clods == 0 && wat.fireK < sand.fireK, buf);
  return fails;
}

// ================================================================ Battle Foundry phase 5: explosions from the charge (fxscale)
// spec 6.1-6.5, reference 07 / 08. Lengths and times scale as W^(1/3) (velocities do not): k = W^(1/3) / 1.876 against the
// 155 mm timeline. The surface row decides the mix; an oblique impact biases the ejecta downrange.
void Fx::explosionScaled(vec3 p, vec3 n, const std::string& family, const std::string& munition, vec3 vImpact, float Wkg) {
  const FxData& D = fxData();
  const FxMunitionRow M = fxMunitionFor(munition.empty() ? (family == "grenade" ? "grenade_M67" : "mortar_81mm_M374") : munition, Wkg);
  const std::string sid = surfaceOf ? surfaceOf(p, family) : family == "grenade" || family == "ground" ? "dry_soil" : "concrete";
  const FxSurfaceRow& S = D.surface(sid);
  const bool water = sid == "water_shallow" || sid == "water_deep", air = sid == "airburst";
  FxEvent e; e.id = nextId(-2); e.recipe = FXR_IMPACT; e.pos = p; e.normal = n; e.surface = sid; e.seed = (uint32_t)(serial * 2246822519u);
  if (seenIds.count(e.id)) return; seenIds.insert(e.id); seenOrder.push_back(e.id); if (seenOrder.size() > 2048) { seenIds.erase(seenOrder.front()); seenOrder.pop_front(); }
  const uint32_t sd = e.seed; int salt = 0; auto R = [&]() { return hash01(sd, (uint32_t)++salt); };
  const float w3 = std::cbrt(M.W), k = w3 / 1.876f; const int t = std::clamp(tier, 0, 4);
  n = glm::length(n) > 1e-4f ? glm::normalize(n) : vec3(0, 1, 0);
  // the ejecta axis and the downrange azimuth (6.5): axis = mix(n, reflect(v, n), 0.55); density 1 + 0.9 cos(phi - phi_d)
  vec3 axis = n, down(0); bool oblique = false;
  if (glm::length(vImpact) > 1.0f) { const vec3 vi = glm::normalize(vImpact); axis = glm::normalize(glm::mix(n, glm::reflect(vi, n), 0.55f));
    down = vi - n * glm::dot(vi, n); oblique = glm::length(down) > 0.2f; if (oblique) down = glm::normalize(down); }
  vec3 t1 = glm::normalize(std::fabs(n.y) > 0.9f ? glm::cross(n, vec3(1, 0, 0)) : glm::cross(n, vec3(0, 1, 0))), t2 = glm::cross(n, t1);
  if (oblique) { t1 = down; t2 = glm::cross(n, t1); }
  auto azimuth = [&]() { for (int g = 0; g < 8; ++g) { float ph = R() * 6.2831853f; if (!oblique || R() * 1.9f < 1.0f + 0.9f * std::cos(ph)) return ph; } return 0.0f; };
  // S1 / S2: the cooling fireball (T0 3200 K, tau 0.02 s, n 0.85; L_v ~ T^8.1), sprites on a 0.35 R_fb shell, lognormal radii (median 0.45 R_fb)
  {
    const int nFb[5] = {1, 2, 4, 6, 8};
    const float fireK = water ? 0.15f : air ? 1.3f : std::clamp(1.6f / (0.6f + S.dustToFire), 0.35f, 1.3f);   // dust hides the ball; water suppresses it
    for (int i = 0; i < nFb[t]; ++i) {
      FlashEl f; f.kind = 3; f.t0 = clock; f.life = M.tLum; f.T0 = M.T0; f.tau = M.tau * k; f.n = M.n; f.eps = 0.9f; f.k = fireK; f.roll = R() * 255.0f;
      const float th = R() * 6.2831853f, ph = std::acos(2 * R() - 1); const vec3 o(std::sin(ph) * std::cos(th), std::fabs(std::cos(ph)), std::sin(ph) * std::sin(th));
      f.p = p + (air ? vec3(0) : n * (0.3f * M.Rfb)) + o * (0.35f * M.Rfb); f.R = M.Rfb * 0.45f * std::exp(0.4f * (R() + R() + R() - 1.5f) * 1.414f);
      flashes.push_back(f);
    }
    if (brightOn) { CombSrc c; c.p = p + n * (0.5f * M.Rfb); c.t0 = clock; c.life = 2.0f * M.tLum; c.cool = true; c.T0 = M.T0; c.tau = M.tau * k; c.n = M.n; c.eps = 0.9f * fireK; c.Rfb = M.Rfb; combSrc.push_back(c); }
    else if (admitLight()) { MuzzleLight L; L.p = p + n * (0.5f * M.Rfb); L.life = std::max(0.06f, 2.0f * M.tLum); L.k = 2.5f * w3 * fireK; L.col = D.hue(2800); L.id = 0xE7B00000u + (++mlightSerial & 0xFFF); mlights.push_back(L); }
  }
  const float Rc = 0.5f * M.craterD * std::max(0.3f, S.craterDepth), vMin = std::sqrt(9.81f * std::max(Rc, 0.1f)), vMax = 2.5f * 20.0f * std::pow(M.W, 0.18f), mu = fxSoilMu(sid);
  const vec3 albedo = water ? vec3(0.75f) : S.albedo;
  if (!air && !water) {   // 6.7 residue into the ground deformation buffer (tdeform): small craters as a bowl, every crater's 1.4 R vegetation-kill ring;
    // the big bowls are the gameplay craters (App::explode / craterAt). Oblique: an ellipse, semi-axes 1 / sin(elevation) clamped [1, 3.5]
    const float Dc = M.craterD * S.craterDepth, Rr = 0.5f * Dc;
    float ell = 1; if (oblique) { const float el = std::asin(std::clamp(-glm::dot(glm::normalize(vImpact), n), 0.05f, 1.0f)); ell = std::clamp(1.0f / std::sin(el), 1.0f, 3.5f); }
    const vec3 half = oblique ? down * (Rr * (ell - 1)) : vec3(0);
    if (Dc > 0.05f) {
      if (Dc < 0.9f) deformStamp(p - half, p + half, Rr, std::min(0.4f * Dc, 0.35f), 1.0f);
      deformStamp(p - half * 1.4f, p + half * 1.4f, 1.4f * Rr, 0.0f, 1.0f);
    }
  }
  const vec3 dustCol = glm::min(albedo * 1.3f, vec3(0.85f));   // fines scatter more than the bulk (8.6)
  // A. suspended dust (D < 0.2 mm): no arcs; out with the blast wind (tau 0.12 s), then the ambient wind; at least one per burst (8.1)
  if (!air) {
    const int nDust[5] = {3, 8, 20, 45, 90}, life[5] = {3, 6, 12, 20, 30};
    int nd = water ? 1 : std::max(1, (int)std::lround(nDust[t] * std::min(2.0f, S.dustToFire / 2.0f)));
    nd = std::min(nd, std::max(1, caps.dustPerBurst * 2));
    for (int i = 0; i < nd; ++i) {
      const float ph = azimuth(), r = R() * 0.6f * Rc; const vec3 rad = t1 * std::cos(ph) + t2 * std::sin(ph);
      Particle q; q.p = p + rad * r + n * 0.2f; q.v = rad * (6.0f + 12.0f * R()) * std::sqrt(k) + n * (2.0f + 6.0f * R()) * std::sqrt(k);
      q.drag = 1.0f / (0.12f * std::max(0.3f, k)); q.size = (0.8f + 3.2f * R()) * std::max(0.25f, k); q.grow = 0.25f * q.size;
      q.life = q.maxLife = std::min((float)life[t], (8.0f + 17.0f * R()) * std::max(0.4f, k)); q.col = vec4(dustCol * (0.85f + 0.3f * R()), water ? 0.35f : 0.55f);
      if (!admit(i == 0)) break; parts.push_back(q);
    }
  }
  // B. ballistic grains (0.2-20 mm): the power law v = v_min u^(-1/(3 mu)), theta = 52 - 22 sqrt(u) deg; drag; killed at the ground
  if (!air && !water) {
    const int nGr[5] = {0, 40, 200, 800, 2500};
    const int ng = std::min((int)(nGr[t] * std::min(1.0f, S.mass)), caps.grains > 0 ? caps.grains : 1 << 20);
    for (int i = 0; i < ng; ++i) {
      const float u = std::max(1e-3f, R()), v = std::min(vMax, vMin * std::pow(u, -1.0f / (3.0f * mu))), xi = std::sqrt(R()), th = glm::radians(52.0f - 22.0f * xi), ph = azimuth();
      const vec3 rad = t1 * std::cos(ph) + t2 * std::sin(ph), dir = glm::normalize(rad * std::cos(th) + axis * std::sin(th));
      Particle q; q.p = p + rad * (xi * Rc) + n * 0.05f; q.v = dir * v; q.drag = std::clamp(v / 40.0f, 0.2f, 3.0f); q.lift = -9.81f; q.grain = true;
      q.size = 0.02f + 0.03f * R(); q.life = q.maxLife = 6.0f; q.col = vec4(albedo * (0.8f + 0.38f * R()), 0.9f);
      if (!admit()) break; parts.push_back(q);
    }
  }
  // C. clods (> 20 mm): mesh debris, the same launch law biased to low xi, ground contact and rest (Fx::update)
  if (!air && !water) {
    const int nCl[5] = {4, 8, 16, 28, 40};
    const int nc = std::min((int)std::lround(nCl[t] * S.clodFrac), caps.clods > 0 ? caps.clods : 1 << 20);
    for (int i = 0; i < nc; ++i) {
      if (debris.size() >= maxDebris) { if (capOn) break; debris.erase(debris.begin()); }
      const float u = std::max(1e-3f, R()), v = std::min(std::min(vMax, 35.0f), vMin * std::pow(u, -1.0f / (3.0f * mu))), xi = 0.6f * std::sqrt(R()), th = glm::radians(52.0f - 22.0f * xi), ph = azimuth();
      const vec3 rad = t1 * std::cos(ph) + t2 * std::sin(ph);
      Debris d; d.p = p + n * 0.15f + rad * (xi * Rc); d.v = glm::normalize(rad * std::cos(th) + axis * std::sin(th)) * v;
      d.w = vec3(R() - 0.5f, R() - 0.5f, R() - 0.5f) * 14.0f; const float s = (0.04f + 0.12f * R()) * std::clamp(k, 0.5f, 1.5f);
      d.size = vec3(s, s * (0.5f + 0.6f * R()), s * (0.6f + 0.8f * R())); d.life = 7.0f + 5.0f * R(); debris.push_back(d);
    }
  }
  // D. the ground dust ring: an annulus out to 2.5 R_fb (tau 0.055 s), loose ground only
  if (!air && !water && S.dustToFire >= 1.0f) {
    static const int ringN[5] = {0, 6, 14, 26, 40};
    const int nRing = std::min(caps.groundDust > 0 ? caps.groundDust : 40, ringN[t]);
    for (int i = 0; i < nRing; ++i) {
      const float ph = (i + R()) * 6.2831853f / std::max(1, nRing); const vec3 rad = t1 * std::cos(ph) + t2 * std::sin(ph);
      Particle q; q.drag = 1.0f / 0.055f; q.p = p + rad * 0.3f + n * (0.3f + 0.9f * R()); q.v = rad * (2.5f * M.Rfb * (0.55f + 0.45f * R()) * q.drag);
      q.size = 0.25f * M.Rfb; q.grow = 0.1f * M.Rfb; q.life = q.maxLife = 3.0f + 4.0f * R(); q.col = vec4(dustCol, 0.45f);
      if (!admit()) break; parts.push_back(q);
    }
  }
  // the column / residual plume (5.1 event 4): delayed by 0.10-0.24 s x k, rising toward the column apex, lit soft sprites (8.3: 1-2 / 4 / 8 / 13 / 18)
  {
    const int nPl[5] = {2, 4, 8, 13, 18}; const float lifeT[5] = {2, 4, 8, 15, 25};
    const int np = std::min(nPl[t], caps.plume > 0 ? caps.plume : 18);
    const float H = air ? 0.0f : 0.9f * M.apex, kd = 1.0f / std::max(0.2f, 0.25f * M.colLife);
    const vec3 smokeCol = water ? vec3(0.8f) : glm::mix(vec3(0.06f), dustCol, std::clamp(S.dustToFire / 4.0f, 0.15f, 0.7f));   // soot darkened, soil-coloured where dust dominates
    for (int i = 0; i < np; ++i) {
      Particle q; const float hk = (float)(i + 1) / np;   // a column, not a ball: each sprite aims at its own height
      q.p = p + n * (0.3f * M.Rfb) + vec3(R() - 0.5f, 0, R() - 0.5f) * (0.5f * M.Rfb);
      q.drag = kd; q.v = (air ? vec3(R() - 0.5f, 0.2f, R() - 0.5f) * 2.0f : (axis * (H * hk * kd))) + vec3(R() - 0.5f, 0, R() - 0.5f) * 1.5f;
      q.size = 0.45f * M.Rfb * (0.7f + 0.6f * R()); q.grow = 0.25f * H * kd * 0.5f + 0.1f; q.lift = 0;
      q.life = q.maxLife = std::min(lifeT[t], std::max(2.0f, 3.0f * M.colLife * (0.8f + 0.4f * R()))); q.col = vec4(smokeCol * (0.85f + 0.3f * R()), water ? 0.45f : 0.7f);
      Delayed dl; dl.t = clock + (0.10f + 0.14f * R()) * k; dl.q = q; delayed.push_back(dl);
    }
  }
  // fragments (6.3): an equatorial belt about the shell axis (75 % within +-25 deg), dim motion streaks of length v dt
  {
    const int nFr[5] = {0, 0, 40, 120, 300};
    const int nf = std::min(nFr[t], caps.fragments >= 0 ? caps.fragments : 300);
    const vec3 ax = glm::length(vImpact) > 1.0f ? glm::normalize(vImpact) : vec3(0, 1, 0);
    vec3 b1 = glm::normalize(std::fabs(ax.y) > 0.9f ? glm::cross(ax, vec3(1, 0, 0)) : glm::cross(ax, vec3(0, 1, 0))), b2 = glm::cross(ax, b1);
    const float vf = std::min(M.vFrag, 1500.0f), len = vf / 60.0f;
    for (int i = 0; i < nf && (int)streaks.size() < (int)maxStreaks; ++i) {
      const float ph = R() * 6.2831853f, el = glm::radians((R() < 0.75f ? 25.0f : 80.0f) * (2 * R() - 1));
      const vec3 d = glm::normalize((b1 * std::cos(ph) + b2 * std::sin(ph)) * std::cos(el) + ax * std::sin(el));
      if (!air && glm::dot(d, n) < -0.05f) continue;   // into the ground: spent there
      const float r0 = M.Rfb * (0.3f + 2.0f * R());
      streaks.push_back({p + d * r0, p + d * (r0 + len), 0.03f, 0.03f, 0.012f, vec4(vec3(0.5f, 0.46f, 0.4f) * (1.6f / len), 1)});   // alpha ~ L_ref / (v dt)
    }
  }
}
