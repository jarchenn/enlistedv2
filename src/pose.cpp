// Procedural soldier poses on the supplied skeletons (no animation clips exist in the source GLBs).
// Each joint gets a rotation D_j expressed in rest model space about its rest head h_j; the model-space delta
// accumulates down the hierarchy: Delta_j = Delta_parent * T(h_j) * D_j * T(-h_j). With bind pose == rest pose the
// skinning matrix is Delta_j * restWorld_j * invBind_j. Arms use two-bone IK onto the weapon's grip sockets, legs
// two-bone IK onto planted / stepping feet (so crouching bends the knees), feet stay flat.
#include "character.h"
#include <cmath>
#include <algorithm>

namespace {
mat4 about(vec3 pivot, const quat& q) { return glm::translate(mat4(1), pivot) * glm::mat4_cast(q) * glm::translate(mat4(1), -pivot); }
quat between(vec3 a, vec3 b) {
  a = glm::normalize(a); b = glm::normalize(b);
  float d = glm::dot(a, b);
  if (d > 0.99999f) return quat(1, 0, 0, 0);
  if (d < -0.99999f) { vec3 ax = glm::cross(a, vec3(1, 0, 0)); if (glm::length(ax) < 1e-3f) ax = glm::cross(a, vec3(0, 1, 0)); return glm::angleAxis(3.14159265f, glm::normalize(ax)); }
  return glm::normalize(quat(1 + d, glm::cross(a, b)));
}
quat rotOf(const mat4& m) { return glm::normalize(glm::quat_cast(glm::mat3(m))); }
float ease(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3 - 2 * t); }
// elbow / knee position for a two-bone chain from A with lengths l1, l2 reaching T, bending toward pole
vec3 solve2(vec3 A, float l1, float l2, vec3 T, vec3 pole) {
  vec3 d = T - A; float L = glm::length(d);
  vec3 dir = L > 1e-5f ? d / L : vec3(0, -1, 0);
  L = std::clamp(L, std::fabs(l1 - l2) + 1e-3f, l1 + l2 - 1e-3f);
  float ca = std::clamp((l1 * l1 + L * L - l2 * l2) / (2 * l1 * L), -1.0f, 1.0f);
  vec3 perp = pole - dir * glm::dot(pole, dir);
  if (glm::length(perp) < 1e-4f) perp = glm::cross(dir, vec3(1, 0, 0));
  perp = glm::normalize(perp);
  return A + dir * (ca * l1) + perp * (std::sqrt(std::max(0.0f, 1 - ca * ca)) * l1);
}
}

void poseCharacter(const CharModel& m, const PoseParams& p, PoseResult& out) {
  int nj = m.joints();
  out.skin.assign(nj, mat4(1)); out.delta.assign(nj, mat4(1));
  std::vector<quat> D(nj, quat(1, 0, 0, 0));
  std::vector<int> order(nj); for (int k = 0; k < nj; ++k) order[k] = k;
  auto depth = [&](int j) { int d = 0; while (j >= 0) { j = m.jointParent[j]; ++d; } return d; };
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return depth(a) < depth(b); });
  auto J = [&](int s) { return m.semantic[s]; };
  auto H = [&](int s) { return J(s) >= 0 ? m.jointHead[J(s)] : vec3(0); };
  bool dead = p.death >= 0;
  float prone = dead ? 0.0f : std::clamp(p.prone, 0.0f, 1.0f);
  float crouch = dead ? 0.0f : std::clamp(p.crouch, 0.0f, 1.0f) * (1 - prone);
  float walk = dead ? 0.0f : std::clamp(p.walkAmount, 0.0f, 1.0f) * (1 - prone);
  // ---- body root: hips drop for crouch, bob for walking
  float hipsY = H(CJ_HIPS).y;
  float drop = 0.36f * crouch + 0.025f * walk * std::fabs(std::cos(p.walkPhase));
  mat4 root = glm::translate(mat4(1), vec3(0, -drop, 0.06f * crouch));
  // ---- spine: crouch lean + aim pitch (looking up leans back a little) + upper-body twist
  float pitch = dead ? 0.0f : p.aimPitch;
  float lean = 0.22f * crouch + 0.10f * walk;
  // mantle: hands on the ledge (climbW), the chest rolls forward over it in the second half
  float climb = dead ? -1.0f : p.climb, climbW = climb >= 0 ? std::clamp(p.climbW, 0.0f, 1.0f) : 0.0f;
  float climbRoll = climb >= 0 ? std::max(0.0f, std::sin(3.14159265f * std::clamp((climb - 0.45f) / 0.5f, 0.0f, 1.0f))) : 0.0f;
  lean += 0.55f * climbRoll;
  for (int s : {CJ_SPINE, CJ_SPINE1, CJ_SPINE2}) if (J(s) >= 0)
    D[J(s)] = glm::angleAxis(p.aimYawOffset / 3.0f, vec3(0, 1, 0)) * glm::angleAxis(lean / 3.0f - pitch * 0.12f, vec3(1, 0, 0));
  if (J(CJ_NECK) >= 0) D[J(CJ_NECK)] = glm::angleAxis(-pitch * 0.25f - lean * 0.4f, vec3(1, 0, 0));
  if (J(CJ_HEAD) >= 0) D[J(CJ_HEAD)] = glm::angleAxis(-pitch * 0.25f - lean * 0.3f, vec3(1, 0, 0));
  // ---- evaluate in topological order; arms and legs are solved when their chain root is reached
  std::vector<mat4>& Dl = out.delta;
  std::vector<bool> done(nj, false);
  quat preArm[2], preFore[2]; bool armSolved[2] = {false, false};
  quat preLeg[2], preKnee[2]; bool legSolved[2] = {false, false};
  mat4 weapon(1);
  bool weaponSet = false;
  auto placeWeapon = [&](const mat4& chest) {
    // right shoulder pocket, following the chest; bore along the aim (lowered when at low ready / throwing)
    vec3 pocket = vec3(chest * vec4(H(CJ_RARM) * vec3(0.55f, 1.0f, 1.0f) + vec3(0, -0.07f, 0.06f), 1));
    float wp = pitch - 0.62f * std::clamp(p.lowReady, 0.0f, 1.0f);
    float yawIn = 0.06f;   // stock in the shoulder, muzzle slightly toward the centre line
    if (p.throwT >= 0) { wp = -1.0f; pocket = vec3(chest * vec4(0.10f, H(CJ_HIPS).y + 0.18f, 0.22f, 1)); yawIn = -0.5f; }
    quat q = glm::angleAxis(yawIn, vec3(0, 1, 0)) * glm::angleAxis(-wp, vec3(1, 0, 0));   // +z bore, pitch up = -x rotation
    weapon = glm::translate(mat4(1), pocket) * glm::mat4_cast(q);
    if (p.recoil > 0) weapon = weapon * glm::translate(mat4(1), vec3(0, 0, -0.035f * p.recoil)) * glm::mat4_cast(glm::angleAxis(-0.06f * p.recoil, vec3(1, 0, 0)));
    if (p.reload > 0 && p.reload < 1) weapon = weapon * glm::mat4_cast(glm::angleAxis(0.35f * std::sin(p.reload * 3.14159f), vec3(0, 0, 1)));
    weaponSet = true;
  };
  for (int j : order) {
    int par = m.jointParent[j];
    mat4 Pd = par >= 0 ? Dl[par] : root;
    int side = j == J(CJ_LARM) ? 0 : j == J(CJ_RARM) ? 1 : -1;
    if (side >= 0 && p.holdWeapon && !dead) {
      if (!weaponSet) placeWeapon(J(CJ_SPINE2) >= 0 ? Dl[J(CJ_SPINE2)] : Pd);
      int sA = side ? CJ_RARM : CJ_LARM, sF = side ? CJ_RFOREARM : CJ_LFOREARM, sH = side ? CJ_RHAND : CJ_LHAND;
      vec3 a0 = H(sA), b0 = H(sF), c0 = H(sH);
      vec3 A = vec3(Pd * vec4(a0, 1));
      vec3 grip = side ? p.gripR : p.gripL;
      vec3 T = vec3(weapon * vec4(grip, 1));
      if (side == 0 && p.reload > 0 && p.reload < 1) {   // left hand: grip -> magazine -> pouch -> magazine -> grip
        vec3 mag = vec3(weapon * vec4(p.magPos, 1)), pouch = vec3(Pd * vec4(0.14f, hipsY + 0.05f, 0.16f, 1));
        float r = p.reload;
        T = r < 0.25f ? glm::mix(T, mag, ease(r / 0.25f)) : r < 0.5f ? glm::mix(mag, pouch, ease((r - 0.25f) / 0.25f))
          : r < 0.8f ? glm::mix(pouch, mag, ease((r - 0.5f) / 0.3f)) : glm::mix(mag, T, ease((r - 0.8f) / 0.2f));
      }
      if (side == 1 && p.throwT >= 0) {   // right arm throws: wind-up behind the head, release forward-up, follow through
        mat4 C = J(CJ_SPINE2) >= 0 ? Dl[J(CJ_SPINE2)] : Pd;
        vec3 back = vec3(C * vec4(-0.22f, 1.78f - drop, -0.22f, 1)), rel = vec3(C * vec4(-0.12f, 1.70f - drop, 0.55f, 1)), fol = vec3(C * vec4(0.05f, 1.15f - drop, 0.45f, 1));
        float t = p.throwT;
        T = t < 0.45f ? glm::mix(vec3(weapon * vec4(p.gripR, 1)), back, ease(t / 0.45f)) : t < 0.7f ? glm::mix(back, rel, ease((t - 0.45f) / 0.25f)) : glm::mix(rel, fol, ease((t - 0.7f) / 0.3f));
      }
      if (climbW > 0) T = glm::mix(T, p.climbGrip + vec3(side ? -p.climbSpread : p.climbSpread, 0, 0), climbW);   // palms on the ledge (left is +x)
      // the wrist sits ~7 cm behind the palm socket, toward the elbow
      float l1 = glm::length(b0 - a0), l2 = glm::length(c0 - b0);
      vec3 pole = side ? vec3(-0.6f, -1, -0.2f) : vec3(0.7f, -1, -0.1f);
      vec3 E = solve2(A, l1, l2, T, pole);
      T = E + glm::normalize(T - E) * std::min(l2, glm::length(T - E));
      quat Rp = rotOf(Pd);
      preArm[side] = between(b0 - a0, glm::inverse(Rp) * (E - A));
      quat Ra = Rp * preArm[side];
      preFore[side] = between(c0 - b0, glm::inverse(Ra) * (T - E));
      armSolved[side] = true;
      D[j] = preArm[side];
    } else if (side < 0) {
      if (j == J(CJ_LFOREARM) && armSolved[0]) D[j] = preFore[0];
      if (j == J(CJ_RFOREARM) && armSolved[1]) D[j] = preFore[1];
    }
    if (side >= 0 && dead) D[j] = glm::angleAxis((side ? 1.0f : -1.0f) * 0.9f * ease(p.death), vec3(0, 0, 1));   // arms fall to the sides
    int leg = j == J(CJ_LUPLEG) ? 0 : j == J(CJ_RUPLEG) ? 1 : -1;
    if (leg >= 0 && !dead && prone < 0.5f) {
      int sU = leg ? CJ_RUPLEG : CJ_LUPLEG, sK = leg ? CJ_RLEG : CJ_LLEG, sF = leg ? CJ_RFOOT : CJ_LFOOT;
      vec3 a0 = H(sU), b0 = H(sK), c0 = H(sF);
      vec3 A = vec3(Pd * vec4(a0, 1));
      float ph = p.walkPhase + (leg ? 3.14159265f : 0.0f);
      vec3 T = c0 + vec3(0, std::max(0.0f, std::cos(ph)) * 0.11f * walk, 0.30f * walk * std::sin(ph) + 0.10f * crouch);
      if (crouch > 0.01f) T.x += (leg ? -0.04f : 0.04f) * crouch;
      if (climb >= 0 && leg) T += vec3(0, 0.55f, 0.30f) * climbRoll;   // the right knee comes up onto the ledge
      float l1 = glm::length(b0 - a0), l2 = glm::length(c0 - b0);
      vec3 K = solve2(A, l1, l2, T, vec3(0, 0.2f, 1));
      quat Rp = rotOf(Pd);
      preLeg[leg] = between(b0 - a0, glm::inverse(Rp) * (K - A));
      quat Ru = Rp * preLeg[leg];
      preKnee[leg] = between(c0 - b0, glm::inverse(Ru) * (T - K));
      legSolved[leg] = true;
      D[j] = preLeg[leg];
    }
    if (j == J(CJ_LLEG) && legSolved[0]) D[j] = preKnee[0];
    if (j == J(CJ_RLEG) && legSolved[1]) D[j] = preKnee[1];
    if ((j == J(CJ_LFOOT) || j == J(CJ_RFOOT)) && par >= 0) D[j] = glm::inverse(rotOf(Pd));   // keep the foot flat
    if (dead && (j == J(CJ_LLEG) || j == J(CJ_RLEG))) D[j] = glm::angleAxis(0.25f * ease(p.death), vec3(1, 0, 0));
    Dl[j] = Pd * about(m.jointHead[j], D[j]);
    done[j] = true;
  }
  if (!weaponSet) placeWeapon(J(CJ_SPINE2) >= 0 ? Dl[J(CJ_SPINE2)] : root);
  // ---- whole-body transforms: prone (lying forward, head at the capsule origin), death fall about the feet
  mat4 body(1);
  if (prone > 0) body = glm::translate(mat4(1), vec3(0, 0.16f * prone, -1.45f * prone)) * glm::mat4_cast(glm::angleAxis(1.5707963f * prone, vec3(1, 0, 0)));
  if (dead) {
    float e = ease(p.death);
    float s = p.deathStyle == 1 ? 1.0f : -1.0f;
    vec3 axis = p.deathStyle == 2 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    body = glm::translate(mat4(1), vec3(0, 0.13f * e, 0)) * glm::mat4_cast(glm::angleAxis(s * 1.5707963f * e, axis));
  }
  for (int j = 0; j < nj; ++j) {
    Dl[j] = body * Dl[j];
    out.skin[j] = Dl[j] * m.nodeWorld[m.jointNode[j]] * m.invBind[j];
  }
  out.weapon = body * weapon;
  for (int s = 0; s < CJ_COUNT; ++s) out.joint[s] = J(s) >= 0 ? vec3(Dl[J(s)] * vec4(H(s), 1)) : vec3(0);
}
