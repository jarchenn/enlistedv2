#include "grenade.h"
#include "physics.h"
#include "combat.h"
#include <cmath>
#include <algorithm>

void Grenades::addCloud(vec3 p, double start, uint32_t seed) {
  SmokeCloud c; c.id = nextId++; c.p = p + vec3(0, 1.2f, 0); c.start = start; c.seed = seed;
  c.radius = tun.smokeRadius; c.build = tun.smokeBuild; c.dense = tun.smokeDense; c.fade = tun.smokeFade;
  addCloud(c);   // cap overlapping cost: the oldest cloud gives way
}

void Grenades::expire(double now) {
  clouds.erase(std::remove_if(clouds.begin(), clouds.end(), [&](const SmokeCloud& c) { return now - c.start > cloudLife(c); }), clouds.end());
}

float Grenades::density(const SmokeCloud& c, double now) const {
  float a = (float)(now - c.start);
  if (a < 0) return 0;
  if (a < c.build) { float u = a / std::max(c.build, 1e-3f); return u * u * (3 - 2 * u); }
  a -= c.build;
  if (a < c.dense) return 1.0f;
  a -= c.dense;
  if (a < c.fade) { float u = 1.0f - a / std::max(c.fade, 1e-3f); return u * u; }
  return 0;
}

float Grenades::opticalDepth(vec3 a, vec3 b, double now) const {
  float tau = 0;
  vec3 d = b - a; float L = glm::length(d);
  if (L < 1e-4f) return 0;
  vec3 dn = d / L;
  for (auto& c : clouds) {
    float dens = density(c, now);
    if (dens <= 0) continue;
    // the cloud grows from a 2 m puff to its full radius during build-up; ground-hugging (squashed vertically)
    float R = c.radius * (0.4f + 0.6f * std::min(1.0f, dens * 1.5f));
    vec3 q = c.p;
    // chord of the segment through the sphere
    float tc = glm::dot(q - a, dn);
    vec3 closest = a + dn * std::clamp(tc, 0.0f, L);
    if (glm::length(closest - q) >= R) continue;
    float t0 = std::max(0.0f, tc - R), t1 = std::min(L, tc + R);
    const int N = 12;
    float h = (t1 - t0) / N, sum = 0;
    for (int k = 0; k < N; ++k) {
      vec3 p = a + dn * (t0 + (k + 0.5f) * h);
      vec3 r = p - q; r.y *= 1.6f;                       // flatter cloud
      float x = glm::dot(r, r) / (R * R);
      if (x < 1) sum += 1.0f - x;                        // core-weighted profile: thin at the edge, dense in the middle
    }
    tau += tun.smokeSigma * dens * sum * h;
  }
  return tau;
}

void Grenades::particles(Fx& fx, double now, float dt) const {
  for (auto& c : clouds) {
    float dens = density(c, now);
    if (dens <= 0.01f) continue;
    float R = c.radius * (0.4f + 0.6f * std::min(1.0f, dens * 1.5f));
    // keep roughly particlesPerCloud alive: each lives ~4 s
    float rate = tun.particlesPerCloud / 4.0f * dens;
    float n = rate * dt + fx.rnd();
    for (int k = 0; k < (int)n; ++k) {
      float u = fx.rnd() * 6.2831853f, r = std::sqrt(fx.rnd()) * R * 0.85f;
      vec3 p = c.p + vec3(std::cos(u) * r, (fx.rnd() - 0.4f) * R * 0.55f, std::sin(u) * r);
      float g = 0.62f + 0.08f * fx.rnd();
      fx.smoke(p, vec3(fx.rnd() - 0.5f, 0.15f, fx.rnd() - 0.5f) * 0.3f, R * 0.55f, 4.0f, vec4(g, g, g * 0.98f, 0.55f * std::min(1.0f, dens + 0.2f)), 0.25f, 0.05f);
    }
  }
}

bool grenadeVelocity(vec3 from, vec3 to, float speed, bool high, vec3& vel) {
  vec3 d = to - from; vec2 h(d.x, d.z); float x = glm::length(h), y = d.y, g = 9.81f, v2 = speed * speed;
  float disc = v2 * v2 - g * (g * x * x + 2 * y * v2);
  if (disc < 0 || x < 1e-3f) return false;
  float s = std::sqrt(disc);
  float ang = std::atan((v2 + (high ? s : -s)) / (g * x));
  vec2 hd = h / x;
  vel = vec3(hd.x * std::cos(ang), std::sin(ang), hd.y * std::cos(ang)) * speed;
  return true;
}
