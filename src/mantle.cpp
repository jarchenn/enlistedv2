// Climb (docs/movement.md#mantle). Hold Space facing anything whose top is 0.4-2.2 m above the ground under the feet
// (mid-jump the jump's take-off floor), from a run or standing still. The climb puts the player ON the top, never over it
// (MV1, replacing the old vault-over): from there they walk or jump off either side themselves.
//   land   centred on a top narrower than 0.9 m (wall, parapet coping, fence post rail >= 5 cm: the body balances on it),
//          0.45 m in on a deeper one (or further in, where Physics::shapeFitsAt says the standing body fits), at most
//          0.6 m below / 0.3 m above the ledge;
//          too thin to stand on (< 5 cm): just past it, onto a surface at the ledge's height or within 0.4 m of the
//          start's level ground (a field fence); never down off the far side of a roof;
//   never  through uncut barbed wire (within 1 m of the crossing), never with no standing room, never chained.
//   2.2 m: the player is 1.8 m tall, trench lips stand 1.9-2.0 m over the floor and roof parapets reach ~1.2 m.
// minTop 5 cm: a 6 cm glass balustrade on a setback terrace (0.59 m, above jump height) must stay climbable.
// The standing body is swept up, across and down in 8 cm steps before anything moves.
// Motion (0.45 s for a knee-high hop .. 1.3 s for a 2.2 m pull-up): hands reach the ledge (0-15 %), the body is pulled
// up (15-60 %), rolls forward onto the top with the head low and the view pitched down (60-85 %), then stands.
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
    c.landUp = m.value("landAbove_m", c.landUp); c.landDown = m.value("landBelow_m", c.landDown); c.fenceDrop = m.value("fenceDrop_m", c.fenceDrop);
    c.minTop = m.value("minTop_m", c.minTop); c.pitchDip = m.value("pitchDip_deg", c.pitchDip); c.pitchUp = m.value("pitchUp_deg", c.pitchUp);
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
  const float probeY = ground + M.maxH + 0.6f, probeLen = probeY - (ground - M.fenceDrop - 2.5f);
  auto topAt = [&](float d, float& y) {   // a ray starting inside a solid reports it at once: y = probeY (too tall)
    Physics::Hit g; vec3 o = feet + f * d; o.y = probeY;
    if (!Physics::castRayEx(o, vec3(0, -1, 0), probeLen, g)) return false;
    y = g.pos.y; return true;
  };
  float ledge;
  if (!topAt(dFace + 0.015f, ledge)) { mantle.why = "no top above the face"; return false; }
  float hgt = ledge - ground;
  if (hgt < M.minH || hgt > M.maxH) { char b[96]; std::snprintf(b, sizeof b, "ledge %.2f m outside the climb range (%.2f-%.2f m)", hgt, M.minH, M.maxH); mantle.why = b; return false; }
  const vec3 lift(0, 0.06f, 0);
  // the top's depth along f: where it falls away (or something taller than the climb stands on it)
  float run = ledge, dEdge = dFace + 1.6f; bool blocked = false;
  for (float d = dFace + 0.015f; d <= dFace + 1.6f + 1e-4f; d += d < dFace + 0.15f ? 0.01f : 0.03f) {   // 1 cm near the face: thin tops measured to ~1 cm
    float y; bool got = topAt(d, y);
    if (got && y > ground + M.maxH) { dEdge = d; blocked = true; break; }
    if (!got || y < ledge - 0.4f) { dEdge = d; break; }
    run = std::max(run, y);
  }
  auto fits = [&](float d, float y) { vec3 p = feet + f * d; p.y = y; return Physics::shapeFitsAt(Physics::STAND, p + lift); };
  float dLand = -1, land = 0; bool over = false;
  // onto the top, never over it: centred on a narrow top (the body balances on a wall or parapet coping),
  // 0.45 m in on a deep one, or further in where the standing body fits
  float w = dEdge - dFace;
  if (w >= M.minTop)
    for (float d = dFace + std::min(w * 0.5f, 0.45f); d < dEdge - 1e-4f; d += 0.06f) {
      float y; if (topAt(d, y) && y >= ledge - M.landDown && y <= ledge + M.landUp && fits(d, y)) { dLand = d; land = y; break; }
      if (w < 0.9f) break;   // narrow: the centre or nothing
    }
  // too thin to stand on (a rail, a fence): just past it, only onto a surface at the ledge's height or the start's
  // level ground (a fence in a field); never down off the far side of a roof
  if (dLand < 0 && !blocked)
    for (float d = dEdge + 0.35f; d <= dEdge + 1.2f + 1e-4f; d += 0.06f) {
      float y; if (!topAt(d, y) || !fits(d, y)) continue;
      if (y >= ledge - M.landDown && y <= ledge + M.landUp) { dLand = d; land = y; break; }
      if (std::fabs(y - ground) <= M.fenceDrop) { dLand = d; land = y; over = true; break; }
    }
  if (dLand < 0) { char b[96]; std::snprintf(b, sizeof b, "no standing room on top (%.2f m deep%s)", w, blocked ? ", taller beyond" : ""); mantle.why = b; return false; }
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
  vec3 ledgePt = feet + f * dFace; ledgePt.y = ledge;
  return beginMantle(feet, f, dLand, std::max(run, land) + 0.08f, land, hgt, ledgePt, over);
}

// the climb from `feet` up to yCross, forward dLand along f, down onto `land`: swept for the standing body, then started
bool App::beginMantle(vec3 feet, vec3 f, float dLand, float yCross, float land, float hgt, vec3 ledgePt, bool over) {
  const MantleSettings& M = mantleCfg;
  const vec3 lift(0, 0.06f, 0);
  vec3 P0 = feet, P1(feet.x, yCross, feet.z), P2 = feet + f * dLand, P3;
  P2.y = yCross; P3 = vec3(P2.x, land, P2.z);
  auto sweep = [&](vec3 a, vec3 b) { int n = std::max(1, (int)std::ceil(glm::length(b - a) / 0.08f)); for (int k = 1; k <= n; ++k) if (!Physics::shapeFitsAt(Physics::STAND, glm::mix(a, b, (float)k / n) + lift)) return false; return true; };
  if (!sweep(P0, P1)) { mantle.why = "no room to pull up (overhang / ceiling)"; return false; }
  if (!sweep(P1, P2)) { mantle.why = "body does not clear the ledge (head room)"; return false; }
  if (!sweep(P2, P3)) { mantle.why = "landing blocked"; return false; }
  mantle.pts[0] = P0; mantle.pts[1] = P1; mantle.pts[2] = P2; mantle.pts[3] = P3;
  mantle.h = hgt; mantle.over = over; mantle.fwd = f;
  mantle.ledge = ledgePt;
  float k = std::clamp((hgt - M.minH) / std::max(0.1f, M.maxH - M.minH), 0.0f, 1.0f);   // a knee-high hop is quick, a 2 m pull-up slow
  mantle.dur = std::clamp(glm::mix(M.durMin, M.durMax, k) + (over ? 0.1f : 0.0f), M.durMin, M.durMax);
  mantle.t = 0; mantle.pitchOff = 0; mantle.eyeFrom = eyeHeight;
  mantle.active = true; mantle.why = over ? "mantle over" : "mantle"; ++mantle.count;
  riseT = riseDur = 0;
  return true;
}

// out of the water up a sloped or stepped bank, where the mantle finds no face: the first spot ahead (0.4-1.6 m) shallow
// enough to stand in (or dry, at most 0.8 m above the surface) where the standing body fits
bool App::tryClimbBank(vec3 feet, float yawDeg, float surface) {
  vec3 f = forward(yawDeg, 0); f.y = 0; f = glm::normalize(f);
  for (float d = 0.4f; d <= 1.6f + 1e-4f; d += 0.1f) {
    Physics::Hit g; vec3 o = feet + f * d; o.y = surface + 1.2f;
    if (!Physics::castRayEx(o, vec3(0, -1, 0), 4.0f, g)) continue;
    if (g.pos.y > surface + 0.8f) { mantle.why = "bank too high to climb"; return false; }
    if (surface - g.pos.y > swimCfg.exitDepth) continue;   // still too deep to stand: swim on
    if (!Physics::shapeFitsAt(Physics::STAND, g.pos + vec3(0, 0.06f, 0))) continue;
    return beginMantle(feet, f, d, std::max(g.pos.y, feet.y) + 0.08f, g.pos.y, g.pos.y - feet.y, g.pos, false);
  }
  mantle.why = "no bank in reach";
  return false;
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
