// Pooled cosmetic effects. Never affect gameplay: impacts and damage come from the projectile path.
#include "combat.h"
#include "terrain.h"
#include <algorithm>

void Fx::smoke(vec3 p, vec3 v, float size, float life, vec4 col, float grow, float lift) {
  if (parts.size() >= maxParts) parts.erase(parts.begin());
  Particle q; q.p = p; q.v = v; q.size = size; q.life = q.maxLife = life; q.col = col; q.grow = grow; q.drag = 1.6f; q.lift = lift;
  parts.push_back(q);
}
void Fx::flash(vec3 p, float size, float life, vec4 col) {
  if (parts.size() >= maxParts) parts.erase(parts.begin());
  Particle q; q.p = p; q.size = size; q.life = q.maxLife = life; q.col = col; q.additive = true; q.grow = -size / life * 0.3f;
  parts.push_back(q);
}
void Fx::streak(vec3 a, vec3 b, float width, float life, vec4 col) {
  if (streaks.size() >= maxStreaks) streaks.erase(streaks.begin());
  streaks.push_back({a, b, life, life, width, col});
}

void Fx::explosion(vec3 p, vec3 n, const std::string& family, bool ground) {
  flash(p + n * 0.3f, 3.5f, 0.12f, vec4(9.0f, 6.0f, 2.5f, 1));
  flash(p + n * 0.2f, 1.6f, 0.25f, vec4(5.0f, 2.6f, 0.8f, 1));
  vec4 dust = family == "concrete" ? vec4(0.42f, 0.41f, 0.38f, 0.75f) : family == "timber" ? vec4(0.33f, 0.27f, 0.19f, 0.7f)
             : ground ? vec4(0.24f, 0.19f, 0.13f, 0.8f) : vec4(0.45f, 0.41f, 0.34f, 0.75f);
  for (int i = 0; i < 22; ++i) {
    vec3 d = glm::normalize(n * 1.3f + vec3(rnd() - 0.5f, rnd() * 0.8f, rnd() - 0.5f) * 1.6f);
    smoke(p + d * 0.3f, d * (2.0f + rnd() * 6.0f), 0.6f + rnd() * 0.8f, 2.5f + rnd() * 3.5f, dust * vec4(0.8f + rnd() * 0.3f, 0.8f + rnd() * 0.3f, 0.8f + rnd() * 0.3f, 1), 1.4f, 0.5f);
  }
  for (int i = 0; i < 10; ++i) smoke(p + vec3(0, 0.5f, 0), vec3(rnd() - 0.5f, 1.5f + rnd() * 2, rnd() - 0.5f), 1.2f, 5.0f + rnd() * 3, vec4(0.16f, 0.15f, 0.14f, 0.55f), 0.9f, 0.9f);
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
void Fx::muzzle(vec3 p, vec3 dir, float scale) {
  flash(p + dir * 0.25f * scale, 1.4f * scale, 0.06f, vec4(10.0f, 7.0f, 3.0f, 1));
  for (int s : {-1, 1}) {   // muzzle-brake side blast
    vec3 side = glm::normalize(glm::cross(dir, vec3(0, 1, 0))) * (float)s;
    flash(p + side * 0.5f * scale, 0.8f * scale, 0.05f, vec4(7, 4.5f, 2, 1));
    for (int i = 0; i < 6; ++i) smoke(p + side * 0.3f, (side * 4.0f + dir * 1.5f + vec3(0, 0.5f, 0)) * (0.6f + rnd()), 0.4f * scale, 2.5f + rnd() * 2, vec4(0.55f, 0.53f, 0.5f, 0.55f), 1.6f, 0.3f);
  }
  for (int i = 0; i < 8; ++i) smoke(p + dir * 0.6f * scale, dir * (3.0f + rnd() * 5) + vec3(rnd() - 0.5f, rnd(), rnd() - 0.5f), 0.5f * scale, 3 + rnd() * 2, vec4(0.6f, 0.58f, 0.55f, 0.5f), 1.5f, 0.35f);
}
void Fx::impact(vec3 p, vec3 n, const std::string& surface) {
  vec4 c = surface == "ground" ? vec4(0.25f, 0.2f, 0.14f, 0.7f) : surface == "wood" ? vec4(0.4f, 0.32f, 0.22f, 0.6f) : surface == "metal" ? vec4(0.5f, 0.5f, 0.5f, 0.4f) : vec4(0.55f, 0.52f, 0.46f, 0.65f);
  for (int i = 0; i < 5; ++i) smoke(p + n * 0.03f, (n * 2.0f + vec3(rnd() - 0.5f, rnd(), rnd() - 0.5f)) * (0.5f + rnd()), 0.08f + rnd() * 0.08f, 0.6f + rnd() * 0.8f, c, 0.8f, 0.2f);
  if (surface == "metal") flash(p + n * 0.02f, 0.18f, 0.05f, vec4(6, 5, 3, 1));
}

void Fx::update(float dt, const Terrain& t) {
  for (auto& q : parts) {
    q.life -= dt;
    q.v *= std::exp(-q.drag * dt);
    q.v.y += q.lift * dt;
    q.p += q.v * dt;
    q.size = std::max(0.02f, q.size + q.grow * dt);
  }
  parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Particle& q) { return q.life <= 0; }), parts.end());
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
  auto quad = [](std::vector<FxVert>& out, vec3 c, vec3 rx, vec3 ry, vec4 col) {
    vec3 p0 = c - rx - ry, p1 = c + rx - ry, p2 = c + rx + ry, p3 = c - rx + ry;
    out.push_back({p0, col, {0, 0}}); out.push_back({p1, col, {1, 0}}); out.push_back({p2, col, {1, 1}});
    out.push_back({p0, col, {0, 0}}); out.push_back({p2, col, {1, 1}}); out.push_back({p3, col, {0, 1}});
  };
  for (auto& q : parts) {
    float f = std::clamp(q.life / q.maxLife, 0.0f, 1.0f);
    vec4 c = q.col;
    if (q.additive) c *= f; else c.a *= std::min(1.0f, f * 2.0f) * std::min(1.0f, (1 - f) * 8.0f + 0.2f);
    quad(q.additive ? add : alpha, q.p, right * q.size, up * q.size, c);
  }
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
