// Mantle / climb (docs/movement.md#mantle). Hold Space facing a ledge that is too tall or too deep to vault:
//   ledge  0.9-2.1 m above the ground under the feet (mid-jump the jump's take-off floor, so a jump does not change
//          what counts as climbable), face within reach at shin / waist / chest height; below the vault's 1.35 m
//          only tops deeper than the vault's 1.1 m (platforms): low walls stay the vault's;
//   land   on top: the first spot 0.42-1.6 m past the face whose top is within +0.3 / -0.6 m of the ledge and where
//          the standing body fits (Physics::shapeFitsAt), crossing nothing taller than the range;
//          over:   when the top is narrower than 0.55 m (a garden wall), the far side at most 2.2 m below, like a vault;
//   never  through uncut barbed wire (within 1 m of the crossing), never with no standing room, never chained.
// The standing body is swept up, across and down in 8 cm steps before anything moves (as the vault does).
// Motion (0.9-1.3 s, longer for taller ledges): hands reach the ledge (0-15 %), the body is pulled up (15-60 %),
// rolls forward over the top with the head low and the view pitched down (60-85 %), then stands (85-100 %).
// The weapon is lowered out of view and the first-person arms reach for the ledge (viewmodel.cpp); the soldier
// body gets the same hands-on-ledge pose (pose.cpp). Bots keep their own navmesh mantle links (nav_links.cpp,
// ai_path.cpp; content/ai/navigation.json), whose ranges stay inside the player's (checked by --selftest-move).
#include "app.h"
#include "character.h"
#include <algorithm>

namespace {
constexpr float kReachEnd = 0.15f, kPullEnd = 0.6f, kRollEnd = 0.85f;
float ease(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3 - 2 * t); }
float arc(float u, float a, float b) { return (u <= a || u >= b) ? 0.0f : std::sin(3.14159265f * (u - a) / (b - a)); }
float ramp(float u, float a, float b) { return ease((u - a) / std::max(1e-4f, b - a)); }
}

void App::loadClimbSettings(const json& j) {
  vaultCfg = VaultSettings{}; mantleCfg = MantleSettings{};
  if (j.contains("vault") && j["vault"].is_object()) {
    const json& v = j["vault"];
    vaultCfg.hold = v.value("hold_s", vaultCfg.hold); vaultCfg.minH = v.value("minHeight_m", vaultCfg.minH); vaultCfg.maxH = v.value("maxHeight_m", vaultCfg.maxH);
    vaultCfg.reach = v.value("reach_m", vaultCfg.reach); vaultCfg.maxDepth = v.value("maxDepth_m", vaultCfg.maxDepth); vaultCfg.maxDrop = v.value("maxDrop_m", vaultCfg.maxDrop);
    vaultCfg.speed = v.value("speed", vaultCfg.speed);
  }
  if (j.contains("mantle") && j["mantle"].is_object()) {
    const json& m = j["mantle"];
    MantleSettings& c = mantleCfg;
    c.minH = m.value("minHeight_m", c.minH); c.maxH = m.value("maxHeight_m", c.maxH); c.reach = m.value("reach_m", c.reach);
    c.durMin = m.value("minDuration_s", c.durMin); c.durMax = m.value("maxDuration_s", c.durMax);
    c.landUp = m.value("landAbove_m", c.landUp); c.landDown = m.value("landBelow_m", c.landDown); c.maxDrop = m.value("maxDrop_m", c.maxDrop);
    c.narrow = m.value("narrowTop_m", c.narrow); c.pitchDip = m.value("pitchDip_deg", c.pitchDip); c.pitchUp = m.value("pitchUp_deg", c.pitchUp);
    c.handSpread = m.value("handSpread_m", c.handSpread);
  }
}

bool App::tryStartMantle(vec3 feet, float yawDeg, bool fromWater) {
  const MantleSettings& M = mantleCfg;
  vec3 f = forward(yawDeg, 0); f.y = 0; f = glm::normalize(f);
  // heights are measured from the ground under the feet (mid-jump: the floor the jump left)
  float ground = feet.y;
  // swimming (swim.cpp): the floating feet are the base, so a bank 0.3 m above the surface measures ~1.7 m
  if (!fromWater) { Physics::Hit g; if (Physics::castRayEx(feet + vec3(0, 0.05f, 0), vec3(0, -1, 0), 1.6f, g)) ground = g.pos.y; }
  Physics::Hit h; float dFace = 99;
  for (float hy : {0.35f, 0.75f, 1.15f}) if (Physics::castRayEx(feet + vec3(0, hy, 0), f, M.reach + 0.3f, h) && h.t < dFace) dFace = h.t;
  if (dFace > M.reach + 0.3f) { mantle.why = "no ledge in reach"; return false; }
  const float probeY = ground + M.maxH + 0.6f, probeLen = probeY - (ground - M.maxDrop - 0.5f);
  auto topAt = [&](float d, float& y) {   // a ray starting inside a solid reports it at once: y = probeY (too tall)
    Physics::Hit g; vec3 o = feet + f * d; o.y = probeY;
    if (!Physics::castRayEx(o, vec3(0, -1, 0), probeLen, g)) return false;
    y = g.pos.y; return true;
  };
  float ledge;
  if (!topAt(dFace + 0.12f, ledge)) { mantle.why = "no top above the face"; return false; }
  float hgt = ledge - ground;
  if (hgt < M.minH || hgt > M.maxH) { char b[96]; std::snprintf(b, sizeof b, "ledge %.2f m outside the mantle range (%.2f-%.2f m)", hgt, M.minH, M.maxH); mantle.why = b; return false; }
  if (hgt <= vaultCfg.maxH)   // vault height: only a top deeper than a vault crosses (a platform) is climbed onto
    for (float d = dFace + 0.12f; d <= dFace + vaultCfg.maxDepth + 0.2f; d += 0.08f) {
      float y; if (!topAt(d, y) || y < ledge - 0.4f) { mantle.why = "low obstacle: vault it, not mantle"; return false; }
    }
  const vec3 lift(0, 0.06f, 0);
  // walk across the top: the highest point crossed so far, where the top falls away, the first landing
  float run = ledge, dEdge = -1, dLand = -1, land = 0, runAtEdge = ledge; bool over = false;
  for (float d = dFace + 0.12f; d <= dFace + 1.6f + 1e-4f; d += 0.08f) {
    float y; bool got = topAt(d, y);
    if (got && y > ground + M.maxH) break;   // something taller beyond: no crossing past here
    if (dEdge < 0 && (!got || y < ledge - 0.4f)) { dEdge = d; runAtEdge = run; }
    if (got) run = std::max(run, y);
    if (got && d >= dFace + 0.42f && y >= ledge - M.landDown && y <= ledge + M.landUp) {
      vec3 p = feet + f * d; p.y = y;
      if (Physics::shapeFitsAt(Physics::STAND, p + lift)) { dLand = d; land = y; break; }
    }
  }
  if (dLand < 0 && dEdge > 0 && dEdge - dFace <= M.narrow + 0.12f) {   // a narrow wall: drop over to the far side
    for (float d = dEdge + 0.35f; d <= dEdge + 1.2f + 1e-4f; d += 0.08f) {
      float y; if (!topAt(d, y)) continue;
      if (y > ledge - 0.25f || ledge - y > M.maxDrop) continue;
      vec3 p = feet + f * d; p.y = y;
      if (Physics::shapeFitsAt(Physics::STAND, p + lift)) { dLand = d; land = y; over = true; run = runAtEdge; break; }
    }
  }
  if (dLand < 0) { mantle.why = "no standing room on top (or beyond a narrow wall)"; return false; }
  // unsafe obstacles (barbed wire) are never climbed: no uncut wire within 1 m of the crossing (face to landing)
  for (auto& i : doc.instances) {
    auto it = rt.find(i.id); if (it == rt.end() || !it->second.asset->defense.unsafeVault || it->second.defDestroyed) continue;
    quat iq = glm::inverse(yawQuat(i.yaw));
    vec2 lo(it->second.asset->bmin.x, it->second.asset->bmin.z), hi(it->second.asset->bmax.x, it->second.asset->bmax.z);
    for (float d = std::max(0.0f, dFace - 0.3f); d <= dLand + 0.5f; d += 0.1f) {
      vec3 lp = iq * (feet + f * d - i.pos); vec2 q(lp.x, lp.z);
      if (glm::length(glm::clamp(q, lo, hi) - q) < 1.0f) { mantle.why = "unsafe obstacle (wire)"; return false; }
    }
  }
  float yCross = std::max(run, land) + 0.08f;
  vec3 P0 = feet, P1(feet.x, yCross, feet.z), P2 = feet + f * dLand, P3;
  P2.y = yCross; P3 = vec3(P2.x, land, P2.z);
  auto sweep = [&](vec3 a, vec3 b) { int n = std::max(1, (int)std::ceil(glm::length(b - a) / 0.08f)); for (int k = 1; k <= n; ++k) if (!Physics::shapeFitsAt(Physics::STAND, glm::mix(a, b, (float)k / n) + lift)) return false; return true; };
  if (!sweep(P0, P1)) { mantle.why = "no room to pull up (overhang / ceiling)"; return false; }
  if (!sweep(P1, P2)) { mantle.why = "body does not clear the ledge (head room)"; return false; }
  if (!sweep(P2, P3)) { mantle.why = "landing blocked"; return false; }
  mantle.pts[0] = P0; mantle.pts[1] = P1; mantle.pts[2] = P2; mantle.pts[3] = P3;
  mantle.h = hgt; mantle.over = over; mantle.fwd = f;
  mantle.ledge = feet + f * dFace; mantle.ledge.y = ledge;
  float k = std::clamp((yCross - P0.y - 0.6f) / 1.6f, 0.0f, 1.0f);   // rise 0.6 m (mid-jump) .. 2.2 m (floor to a 2.1 m lip)
  mantle.dur = std::clamp(glm::mix(M.durMin, M.durMax, k) + (over ? 0.1f : 0.0f), M.durMin, M.durMax);
  mantle.t = 0; mantle.pitchOff = 0; mantle.eyeFrom = eyeHeight;
  mantle.active = true; mantle.why = over ? "mantle over" : "mantle"; ++mantle.count;
  riseT = riseDur = 0;
  return true;
}

void App::stepMantle(float dt) {
  MantleState& m = mantle;
  m.t += dt;
  float u = std::clamp(m.t / std::max(1e-3f, m.dur), 0.0f, 1.0f);
  vec3 p;
  if (u < kReachEnd) p = glm::mix(m.pts[0], m.pts[1], 0.04f * ramp(u, 0, kReachEnd));   // the hands find the ledge
  else if (u < kPullEnd) p = glm::mix(m.pts[0], m.pts[1], 0.04f + 0.96f * ramp(u, kReachEnd, kPullEnd));   // pull up
  else if (u < kRollEnd) p = glm::mix(m.pts[1], m.pts[2], ramp(u, kPullEnd, kRollEnd));   // roll forward over the top
  else p = glm::mix(m.pts[2], m.pts[3], ramp(u, kRollEnd, 1.0f));                          // stand / drop
  auto finish = [&](const char* why) {
    eyePitch = std::clamp(eyePitch - m.pitchOff, -85.0f, 85.0f); m.pitchOff = 0;
    m.active = false; if (why) m.why = why;
  };
  // the ledge or the landing may change mid-climb (destroyed, someone in the way): stop where the body still fits
  if (!Physics::shapeFitsAt(Physics::STAND, p + vec3(0, 0.06f, 0))) { finish("mantle interrupted (path blocked)"); return; }
  Physics::setCharacterPos(p);
  // camera: rises with the pull to ~0.7 m over the ledge, dips forward over it, then stands
  float yL = m.ledge.y, y0 = m.pts[0].y + m.eyeFrom, yA = yL + 0.72f, yB = (m.over ? m.pts[2].y : yL) + 0.6f, yEnd = m.pts[3].y + move.eyeStand;
  float ye = u < kPullEnd ? glm::mix(y0, yA, ramp(u, 0, kPullEnd)) : u < kRollEnd ? glm::mix(yA, yB, ramp(u, kPullEnd, kRollEnd)) : glm::mix(yB, yEnd, ramp(u, kRollEnd, 1.0f));
  eyeHeight = std::clamp(ye - p.y, 0.25f, move.eyeStand);
  float off = mantleCfg.pitchUp * arc(u, 0.05f, kPullEnd) - mantleCfg.pitchDip * arc(u, kPullEnd - 0.05f, 1.0f);
  if (reducedMotion) off *= 0.3f;
  eyePitch = std::clamp(eyePitch + off - m.pitchOff, -85.0f, 85.0f); m.pitchOff = off;
  if (u >= 1.0f) { eyeHeight = move.eyeStand; finish(nullptr); }
}

float App::mantleLower() const {
  if (!mantle.active) return 0;
  float u = std::clamp(mantle.t / std::max(1e-3f, mantle.dur), 0.0f, 1.0f);
  return ramp(u, 0, 0.12f) * (1 - ramp(u, 0.88f, 1.0f));
}

bool App::mantleGrips(vec3& left, vec3& right, float& w) const {
  if (!mantle.active) return false;
  float u = std::clamp(mantle.t / std::max(1e-3f, mantle.dur), 0.0f, 1.0f);
  w = ramp(u, 0, kReachEnd) * (1 - ramp(u, 0.72f, kRollEnd + 0.05f));
  vec3 rgt = glm::normalize(glm::cross(mantle.fwd, vec3(0, 1, 0)));
  vec3 c = mantle.ledge + mantle.fwd * 0.08f + vec3(0, 0.03f, 0);   // palms just past the lip
  left = c - rgt * mantleCfg.handSpread; right = c + rgt * mantleCfg.handSpread;
  return w > 0;
}

void App::mantlePose(vec3 feet, PoseParams& pp) const {
  if (!mantle.active) return;
  vec3 l, r; float w = 0;
  mantleGrips(l, r, w);
  pp.climb = std::clamp(mantle.t / std::max(1e-3f, mantle.dur), 0.0f, 1.0f);
  pp.climbW = w; pp.climbSpread = mantleCfg.handSpread; pp.lowReady = 1;
  // model space: feet at the origin, +z forward (the body is drawn yawed by eyeYaw + 180 deg)
  quat inv = glm::inverse(glm::angleAxis(radians(eyeYaw) + 3.14159265f, vec3(0, 1, 0)));
  pp.climbGrip = inv * ((l + r) * 0.5f - feet);
}
