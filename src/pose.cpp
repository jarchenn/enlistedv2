// Procedural soldier poses on the supplied skeletons (no animation clips exist in the source GLBs).
// Each joint gets a rotation D_j expressed in rest model space about its rest head h_j; the model-space delta
// accumulates down the hierarchy: Delta_j = Delta_parent * T(h_j) * D_j * T(-h_j). With bind pose == rest pose the
// skinning matrix is Delta_j * restWorld_j * invBind_j. Arms use two-bone IK onto the weapon's grip sockets, legs
// two-bone IK onto planted / stepping feet (so crouching bends the knees), feet stay flat or roll heel to toe.
// AN1 postures: kneel (right knee down, left foot forward, rifle shouldered), carry (rifle low across the body,
// muzzle down to the support side) and moveAim (knees bent, level glide, rifle at eye level).
#include "character.h"
#include <cmath>
#include <algorithm>

namespace {
const float kPi = 3.14159265f;
mat4 about(vec3 pivot, const quat& q) { return glm::translate(mat4(1), pivot) * glm::mat4_cast(q) * glm::translate(mat4(1), -pivot); }
quat between(vec3 a, vec3 b) {
  a = glm::normalize(a); b = glm::normalize(b);
  float d = glm::dot(a, b);
  if (d > 0.99999f) return quat(1, 0, 0, 0);
  if (d < -0.99999f) { vec3 ax = glm::cross(a, vec3(1, 0, 0)); if (glm::length(ax) < 1e-3f) ax = glm::cross(a, vec3(0, 1, 0)); return glm::angleAxis(kPi, glm::normalize(ax)); }
  return glm::normalize(quat(1 + d, glm::cross(a, b)));
}
quat rotOf(const mat4& m) { return glm::normalize(glm::quat_cast(glm::mat3(m))); }
float ease(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3 - 2 * t); }
float c01(float t) { return std::clamp(t, 0.0f, 1.0f); }
vec3 xf(const mat4& m, vec3 p) { return vec3(m * vec4(p, 1)); }
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
  // swimming: the body tilts forward about the waterline, arms breaststroke, legs frog-kick, the weapon rides slung
  float swim = dead ? 0.0f : c01(p.swim), sph = p.swimPhase;
  float prone = dead ? 0.0f : c01(p.prone) * (1 - swim);
  float kneel = dead ? 0.0f : c01(p.kneel) * (1 - prone) * (1 - swim);
  float crouch = dead ? 0.0f : c01(p.crouch) * (1 - prone) * (1 - kneel);
  float walk = dead ? 0.0f : c01(p.walkAmount) * (1 - prone);
  float climb = dead ? -1.0f : p.climb, climbW = climb >= 0 ? c01(p.climbW) : 0.0f;
  float carry = dead || climb >= 0 ? 0.0f : c01(p.carry) * (1 - swim) * (1 - prone);
  float mAim = dead ? 0.0f : c01(p.moveAim) * (1 - swim) * (1 - prone);
  bool gait = p.gait && !dead;
  float pitch = dead ? 0.0f : p.aimPitch;
  float lowR = c01(p.lowReady);
  // weapon kind by where the firing hand sits: pistols (and knives) at the butt, launchers far along the tube
  int kind = p.holdKind >= 0 ? p.holdKind : p.gripR.z < 0.12f ? 0 : p.gripR.z > 0.6f ? 2 : 1;   // SW2: a weapon may name its hold kind
  // ---- leg targets (rest model space; feet on the ground at y = 0, +z forward, left = +x)
  vec3 legT[2]; float footPitch[2] = {0, 0}; float l1L[2], l2L[2]; vec3 a0L[2];
  float climbRoll = climb >= 0 ? std::max(0.0f, std::sin(kPi * c01((climb - 0.45f) / 0.5f))) : 0.0f;
  float A = gait ? std::max(0.0f, p.stride) : 0.0f, sA = std::min(1.0f, A / 0.2f);
  auto gnd = [&](vec3 q) { return p.slope.x * q.x + p.slope.y * q.z; };   // the ground's height at a model point (the plane through the feet origin)
  for (int leg = 0; leg < 2; ++leg) {
    int sU = leg ? CJ_RUPLEG : CJ_LUPLEG, sK = leg ? CJ_RLEG : CJ_LLEG, sF = leg ? CJ_RFOOT : CJ_LFOOT;
    vec3 a0 = H(sU), b0 = H(sK), c0 = H(sF);
    a0L[leg] = a0; l1L[leg] = glm::length(b0 - a0); l2L[leg] = glm::length(c0 - b0);
    float ph = p.walkPhase + (leg ? kPi : 0.0f);
    vec3 T;
    if (gait) {   // stance: the planted foot slides back linearly at the body speed; swing: it lifts and comes forward
      float u = ph / (2 * kPi); u -= std::floor(u);
      float du = std::clamp(p.duty, 0.2f, 0.8f), z, y = 0, fp;
      float lift = (0.05f + 0.10f * walk) * sA;
      if (u < du) { float s = u / du; z = A * (1 - 2 * s); fp = s < 0.15f ? -0.22f * (1 - s / 0.15f) : s > 0.65f ? 0.55f * (s - 0.65f) / 0.35f : 0.0f; }
      else { float s = (u - du) / (1 - du); z = -A + 2 * A * ease(s); y = lift * std::sin(kPi * s); fp = s < 0.4f ? 0.55f * (1 - ease(s / 0.4f)) : -0.22f * ease((s - 0.6f) / 0.4f); }
      fp *= sA * std::clamp(p.moveDir.z, -1.0f, 1.0f);   // heel to toe along the travel (walking backwards rolls the other way)
      footPitch[leg] = fp;
      if (fp > 0) y += 0.13f * std::sin(fp);   // the heel comes up about the ball of the foot
      vec3 off = p.moveDir * (z - 0.04f * sA);
      T = c0 + vec3(off.x, y, off.z + 0.10f * crouch);
    } else
      T = c0 + vec3(0, std::max(0.0f, std::cos(ph)) * 0.11f * walk, 0.30f * walk * std::sin(ph) + 0.10f * crouch);
    if (gait && kind == 1 && p.holdWeapon) { float st = 1 - sA; T.z += (leg ? -0.07f : 0.11f) * st; T.x += (leg ? -0.02f : 0.03f) * st; }   // AN1 step 2: still with a rifle: support foot forward
    if (crouch > 0.01f) T.x += (leg ? -0.04f : 0.04f) * crouch;
    if (mAim > 0) T.x += (leg ? -0.03f : 0.03f) * mAim;   // a slightly wider base for the glide
    if (climb >= 0 && leg) T += vec3(0, 0.55f, 0.30f) * climbRoll;   // the right knee comes up onto the ledge
    if (swim > 0) {   // frog kick: heels draw up and out, then snap back together (half a cycle behind the arms)
      float k = 0.5f + 0.5f * std::sin(sph - 1.2f);
      T = glm::mix(T, c0 + vec3((leg ? -0.16f : 0.16f) * k, 0.38f * k, -0.22f * k), swim);
    }
    if (!dead && prone < 0.5f) T.y += gnd(T) * (1 - swim);
    legT[leg] = T;
  }
  // ---- body root: hips drop for crouch / glide / kneel, and as far as the stepping feet need to stay reachable
  float drop = 0.36f * crouch + 0.07f * mAim + 0.04f * c01(p.hunch) * (1 - crouch);   // AI5: the hunch bends the knees
  if (!gait) drop += 0.025f * walk * std::fabs(std::cos(p.walkPhase));
  else {
    float need = 0, needMax = 0;
    for (int leg = 0; leg < 2; ++leg) {
      float L = 0.975f * (l1L[leg] + l2L[leg]);
      vec3 d = legT[leg] - a0L[leg];
      need = std::max(need, a0L[leg].y - (legT[leg].y + std::sqrt(std::max(0.0f, L * L - d.x * d.x - d.z * d.z))));
      float dz = A + 0.06f;   // the worst case over the cycle: the glide keeps the upper body level at that height
      needMax = std::max(needMax, a0L[leg].y - (H(leg ? CJ_RFOOT : CJ_LFOOT).y - std::fabs(gnd(vec3(0, 0, dz))) + std::sqrt(std::max(0.0f, L * L - d.x * d.x - dz * dz))));
    }
    drop = std::max(drop, glm::mix(need, needMax, mAim) + 0.015f * mAim);
  }
  // kneel: the right knee on the ground under the hip, the right thigh near vertical
  vec3 kneeK(a0L[1].x, 0, a0L[1].z - 0.02f); kneeK.y = 0.065f + gnd(kneeK);
  if (kneel > 0) {
    float dropK = a0L[1].y - (kneeK.y + 0.965f * l1L[1]);
    drop = glm::mix(drop, dropK, kneel);
  }
  mat4 root = glm::translate(mat4(1), vec3(0, -drop, 0.06f * crouch));
  if (prone > 0) footPitch[0] = footPitch[1] = 1.1f * prone;   // AN1 step 2: lying down, insteps on the ground
  if (kneel > 0) {   // left foot forward and planted, shin near vertical; right shin back along the ground, toes tucked
    vec3 cl = H(CJ_LFOOT), cr = H(CJ_RFOOT);
    vec3 Tl(cl.x + 0.05f, 0, a0L[0].z + 0.40f); Tl.y = cl.y + gnd(Tl);
    float dy = std::clamp(cr.y + 0.05f + gnd(kneeK - vec3(0, 0, l2L[1])) - kneeK.y, 0.04f, 0.5f * l2L[1]);   // the ankle a little above its standing height
    vec3 Tr = kneeK + vec3(0, dy, -std::sqrt(l2L[1] * l2L[1] - dy * dy));
    legT[0] = glm::mix(legT[0], Tl, kneel); legT[1] = glm::mix(legT[1], Tr, kneel);
    footPitch[0] *= 1 - kneel; footPitch[1] = glm::mix(footPitch[1], 1.25f, kneel);
  }
  // ---- spine: lean + aim pitch (looking up leans back a little) + upper-body twist (aim, a bladed stance, run counter-swing)
  float lean = 0.22f * crouch + 0.03f * kneel;
  lean += gait ? 0.05f * walk + 0.13f * mAim + 0.06f * carry * walk : 0.10f * walk;
  lean += 0.20f * c01(p.hunch) * (1 - crouch) * (1 - kneel);   // AI5: moving ~22 deg, standing aim ~16 deg
  lean += 0.55f * climbRoll;   // mantle: the chest rolls forward over the ledge in the second half
  float aimW = (1 - carry) * (1 - 0.7f * lowR);   // shouldered and looking down the sights
  float blade = (kind == 1 ? -0.20f : kind == 2 ? -0.12f : 0.0f) * (1 - carry) - 0.10f * carry;   // AN1 step 2: squarer to the target
  lean += (kind == 1 ? 0.08f : 0.0f) * aimW * (1 - 0.6f * kneel);   // into the rifle (kneeling: torso nearly upright)
  float twist = p.aimYawOffset + blade + 0.08f * std::sin(p.walkPhase) * walk * carry;
  if (p.throwT >= 0 && !dead) {   // AN1 step 2: the throw turns the chest right on the wind-up, then through to the left, weight onto the front foot
    float t = p.throwT;
    twist += t < 0.45f ? -0.45f * ease(t / 0.45f) : t < 0.7f ? glm::mix(-0.45f, 0.35f, ease((t - 0.45f) / 0.25f)) : glm::mix(0.35f, 0.15f, ease((t - 0.7f) / 0.3f));
    lean += t < 0.45f ? -0.08f * ease(t / 0.45f) : t < 0.7f ? glm::mix(-0.08f, 0.22f, ease((t - 0.45f) / 0.25f)) : 0.22f;
  }
  lean *= 1 - prone;
  float side = -c01(std::fabs(p.lean)) * (p.lean < 0 ? -1.0f : 1.0f) * p.leanBend;   // MV3: the sideways bend through the waist and spine (hips stay)
  for (int s : {CJ_SPINE, CJ_SPINE1, CJ_SPINE2}) if (J(s) >= 0)
    D[J(s)] = glm::angleAxis(twist / 3.0f, vec3(0, 1, 0)) * glm::angleAxis(side / 3.0f, vec3(0, 0, 1)) * glm::angleAxis(lean / 3.0f - pitch * 0.20f, vec3(1, 0, 0));   // AN1 step 2: the spine takes 60 % of the look
  // shoulders protract to reach the handguard (left clavicle forward, the right a little)
  if (p.holdWeapon && !dead) {
    float pr = (kind == 1 ? 1.0f : 0.6f) * (1 - swim);
    if (J(CJ_LSHOULDER) >= 0) D[J(CJ_LSHOULDER)] = glm::angleAxis(-0.42f * pr, vec3(0, 1, 0)) * glm::angleAxis(0.10f * pr, vec3(0, 0, 1));
    if (J(CJ_RSHOULDER) >= 0) D[J(CJ_RSHOULDER)] = glm::angleAxis(0.15f * pr, vec3(0, 1, 0));
  }
  float headYaw = -(twist - p.aimYawOffset) * 0.5f;   // the head stays on the aim line while the chest blades / swings
  float weld = kind == 1 ? aimW * p.holdWeld : 0.0f;   // cheek on the stock (SW2: a weapon held out in front welds less)
  if (J(CJ_NECK) >= 0) D[J(CJ_NECK)] = glm::angleAxis(headYaw, vec3(0, 1, 0)) * glm::angleAxis(-pitch * 0.15f - lean * 0.4f + 0.16f * weld - 0.55f * prone, vec3(1, 0, 0));   // prone: head up
  if (J(CJ_HEAD) >= 0) D[J(CJ_HEAD)] = glm::angleAxis(headYaw, vec3(0, 1, 0)) * glm::angleAxis(0.20f * weld, vec3(0, 0, 1)) * glm::angleAxis(-pitch * 0.15f - lean * 0.3f + 0.12f * weld - 0.40f * prone, vec3(1, 0, 0));
  // ---- evaluate in topological order; arms and legs are solved when their chain root is reached
  std::vector<mat4>& Dl = out.delta;
  quat preArm[2], preFore[2]; bool armSolved[2] = {false, false};
  vec3 handDir[2], palmDir[2]; float handW[2] = {0, 0};   // the hand along the weapon (fingers round the grip / handguard) and its weight
  quat preLeg[2], preKnee[2]; bool legSolved[2] = {false, false};
  mat4 weapon(1);
  bool weaponSet = false;
  auto placeWeapon = [&](const mat4& chest) {
    vec3 sh = H(CJ_RARM);
    float wp = pitch - 0.62f * lowR;
    // aimed: rifles with the butt in the right shoulder pocket, pistols out in both hands, launchers on the shoulder
    float yawIn = kind == 1 ? 0.05f : 0.0f;   // stock in the shoulder, muzzle slightly toward the centre line
    quat qa = glm::angleAxis(p.aimYawOffset + yawIn, vec3(0, 1, 0)) * glm::angleAxis(-wp, vec3(1, 0, 0));   // +z bore, pitch up = -x rotation
    vec3 pa;
    if (kind == 1) pa = xf(chest, vec3(sh.x * 0.5f, sh.y + 0.08f, sh.z + 0.05f) + p.holdOffset);   // SW2: held out (blasters)
    else if (kind == 0) pa = xf(chest, vec3(sh.x * 0.2f, sh.y - 0.06f, sh.z + 0.44f)) - qa * p.gripR;
    else pa = xf(chest, vec3(sh.x * 0.8f, sh.y + 0.10f, sh.z)) - qa * vec3(0, 0, p.gripR.z - 0.30f);
    vec3 pos = pa; quat q = qa;
    if (carry > 0) {   // low ready across the body: firing hand at the right hip, muzzle down toward the left foot, swinging a little
      float ph = p.walkPhase;
      vec3 swing(0, 0.012f * std::fabs(std::cos(ph)) * walk, 0.03f * std::sin(ph) * walk);
      quat qc; vec3 pc;
      if (kind == 1) {
        qc = glm::angleAxis(0.5f * p.aimYawOffset + 0.85f, vec3(0, 1, 0)) * glm::angleAxis(0.50f, vec3(1, 0, 0)) * glm::angleAxis(0.30f, vec3(0, 0, 1));
        pc = xf(chest, vec3(sh.x * 0.7f, H(CJ_HIPS).y + 0.18f, sh.z + 0.20f) + swing + vec3(0, 0, p.holdOffset.z * 0.5f)) - qc * p.gripR;
      } else if (kind == 0) {   // pistol: compressed low ready in front of the chest
        qc = glm::angleAxis(0.5f * p.aimYawOffset, vec3(0, 1, 0)) * glm::angleAxis(0.95f, vec3(1, 0, 0));
        pc = xf(chest, vec3(sh.x * 0.3f, sh.y - 0.32f, sh.z + 0.26f) + swing) - qc * p.gripR;
      } else {   // launcher: stays on the shoulder, muzzle tipped down
        qc = glm::angleAxis(0.5f * p.aimYawOffset, vec3(0, 1, 0)) * glm::angleAxis(0.30f, vec3(1, 0, 0));
        pc = xf(chest, vec3(sh.x * 0.8f, sh.y + 0.10f, sh.z) + swing) - qc * vec3(0, 0, p.gripR.z - 0.30f);
      }
      pos = glm::mix(pa, pc, carry); q = glm::slerp(qa, qc, carry);
    }
    if (prone > 0) {   // AN1 step 2: in the upright frame before the body tips forward (+y becomes forward, +z down): the bore along +y, butt in the shoulder, raised on the elbows
      quat qp = glm::angleAxis(-(p.aimYawOffset + yawIn), vec3(0, 0, 1)) * glm::angleAxis(-0.5f * kPi - pitch, vec3(1, 0, 0));
      vec3 pp = kind == 1 ? xf(chest, vec3(sh.x * 0.5f, sh.y + 0.04f, sh.z - 0.10f))
              : kind == 0 ? xf(chest, vec3(sh.x * 0.2f, sh.y + 0.40f, sh.z - 0.06f)) - qp * p.gripR
              : xf(chest, vec3(sh.x * 0.8f, sh.y, sh.z - 0.14f)) - qp * vec3(0, 0, p.gripR.z - 0.30f);
      pos = glm::mix(pos, pp, prone); q = glm::slerp(q, qp, prone);
    }
    if (p.throwT >= 0) {   // AN1 step 2: the rifle in the left hand at the chest, muzzle up past the left shoulder
      q = glm::angleAxis(0.35f, vec3(0, 1, 0)) * glm::angleAxis(-1.15f, vec3(1, 0, 0));
      pos = xf(chest, vec3(0.16f, sh.y - 0.22f, sh.z + 0.28f)) - q * p.gripL;
    }
    weapon = glm::translate(mat4(1), pos) * glm::mat4_cast(q);
    if (p.recoil > 0) weapon = weapon * glm::translate(mat4(1), vec3(0, 0, -0.035f * p.recoil)) * glm::mat4_cast(glm::angleAxis(-0.06f * p.recoil, vec3(1, 0, 0)));
    if (p.reload > 0 && p.reload < 1) weapon = weapon * glm::mat4_cast(glm::angleAxis(0.35f * std::sin(p.reload * kPi), vec3(0, 0, 1)));
    if (swim > 0.5f)   // slung across the back, muzzle up over the left shoulder
      weapon = glm::translate(mat4(1), xf(chest, vec3(0.0f, H(CJ_HIPS).y + 0.30f, -0.17f))) * glm::mat4_cast(rotOf(chest) * glm::angleAxis(0.5f, vec3(0, 0, 1)) * glm::angleAxis(-1.35f, vec3(1, 0, 0)) * glm::angleAxis(kPi, vec3(0, 1, 0))) * glm::translate(mat4(1), vec3(0, 0, -0.45f));
    weaponSet = true;
  };
  float hipsY = H(CJ_HIPS).y;
  // fingers (any joints under a hand): curled toward the palm round the grip / handguard
  auto handOf = [&](int j) { for (int k = 0, a = m.jointParent[j]; k < 4 && a >= 0; ++k, a = m.jointParent[a]) { if (a == J(CJ_LHAND)) return 0; if (a == J(CJ_RHAND)) return 1; } return -1; };
  vec3 curlAx[2];
  for (int h = 0; h < 2; ++h) {
    vec3 rd = glm::normalize(H(h ? CJ_RHAND : CJ_LHAND) - H(h ? CJ_RFOREARM : CJ_LFOREARM)), rp = m.palmN[h] - rd * glm::dot(m.palmN[h], rd);   // AN1 step 2: the rig's own rest palm
    curlAx[h] = glm::length(rp) > 1e-3f ? glm::normalize(glm::cross(rd, glm::normalize(rp))) : vec3(0, 0, 1);
  }
  // AN1 step 2: per-finger curls (rad per segment: proximal, middle, distal). Firing hand: middle / ring / little wrapped round the grip,
  // the thumb round the other side, the index on the trigger when aimed (else straight along the frame); support hand: fingers up the
  // side of the handguard, thumb along the other side; a free hand rests half closed, a throwing hand closes on the grenade
  float trig = std::max(c01(p.recoil * 4), (1 - carry) * (1 - lowR) * (p.throwT >= 0 ? 0.0f : 1.0f));
  auto fingerCurl = [&](int h, int f, int seg) -> float {
    int s = std::clamp(seg, 1, 3) - 1;
    if (seg > 3) return 0.0f;
    static const float relaxed[5][3] = {{0.15f, 0.20f, 0.10f}, {0.25f, 0.35f, 0.20f}, {0.30f, 0.40f, 0.25f}, {0.35f, 0.45f, 0.25f}, {0.40f, 0.50f, 0.30f}};
    static const float fist[5][3] = {{0.45f, 0.50f, 0.40f}, {0.95f, 1.10f, 0.60f}, {1.05f, 1.15f, 0.65f}, {1.10f, 1.15f, 0.65f}, {1.15f, 1.10f, 0.60f}};
    static const float fireG[5][3] = {{0.35f, 0.45f, 0.35f}, {0.15f, 0.10f, 0.05f}, {1.05f, 1.15f, 0.65f}, {1.10f, 1.15f, 0.65f}, {1.15f, 1.10f, 0.60f}};
    static const float support[5][3] = {{0.20f, 0.25f, 0.15f}, {0.45f, 0.50f, 0.30f}, {0.50f, 0.55f, 0.30f}, {0.55f, 0.55f, 0.30f}, {0.60f, 0.55f, 0.30f}};   // up the far side of a slim handguard, tips at its top
    static const float cup[5][3] = {{0.20f, 0.25f, 0.15f}, {1.00f, 1.10f, 0.60f}, {1.00f, 1.10f, 0.60f}, {1.05f, 1.10f, 0.60f}, {1.10f, 1.05f, 0.60f}};
    if (dead || !armSolved[h]) return relaxed[f][s];
    if (h == 1 && p.throwT >= 0) return fist[f][s];
    float held = h == 1 ? (f == 1 ? glm::mix(fireG[1][s], s == 0 ? 0.75f : s == 1 ? 0.85f : 0.40f, trig) : fireG[f][s]) : kind == 0 ? cup[f][s] : support[f][s];
    float free = h == 0 && p.reload > 0 && p.reload < 1 ? fist[f][s] * 0.7f : relaxed[f][s];   // reloading: the hand closes on the magazine
    return glm::mix(free, held, handW[h]);
  };
  quat footTilt = dead || prone >= 0.5f ? quat(1, 0, 0, 0) : between(vec3(0, 1, 0), vec3(-p.slope.x, 1, -p.slope.y));   // soles along the slope
  for (int j : order) {
    int par = m.jointParent[j];
    mat4 Pd = par >= 0 ? Dl[par] : root;
    int side = j == J(CJ_LARM) ? 0 : j == J(CJ_RARM) ? 1 : -1;
    if (side >= 0 && p.holdWeapon && !dead) {
      if (!weaponSet) placeWeapon(J(CJ_SPINE2) >= 0 ? Dl[J(CJ_SPINE2)] : Pd);
      int sA = side ? CJ_RARM : CJ_LARM, sF = side ? CJ_RFOREARM : CJ_LFOREARM, sH = side ? CJ_RHAND : CJ_LHAND;
      vec3 a0 = H(sA), b0 = H(sF), c0 = H(sH);
      vec3 Ash = xf(Pd, a0);
      float l1 = glm::length(b0 - a0), l2 = glm::length(c0 - b0);
      vec3 grip = side ? p.gripR : p.gripL;
      vec3 T = xf(weapon, grip);
      if (side == 0 && kind != 0) {   // support hand: slide back along the handguard rather than lock the arm straight
        vec3 bore = glm::normalize(vec3(weapon[2])), wx = glm::normalize(vec3(weapon[0])), wy = glm::normalize(vec3(weapon[1])), wz = bore;
        float room = std::max(0.0f, p.gripL.z - p.gripR.z - 0.14f), slid = 0;
        for (int it = 0; it < 3; ++it) {
          float ex = glm::length(T - 0.075f * glm::normalize(wx * -0.30f + wy * -0.15f + wz * 0.94f) - Ash) - 0.985f * (l1 + l2);   // the wrist (palm minus 7.5 cm)
          if (ex <= 0) break;
          float s = std::min(ex, room - slid); if (s <= 0) break;
          T -= bore * s; slid += s;
        }
      }
      if (side == 0 && p.reload > 0 && p.reload < 1) {   // left hand: grip -> magazine -> pouch -> magazine -> grip
        vec3 mag = xf(weapon, p.magPos), pouch = xf(Pd, vec3(0.14f, hipsY + 0.05f, 0.16f));
        float r = p.reload;
        T = r < 0.25f ? glm::mix(T, mag, ease(r / 0.25f)) : r < 0.5f ? glm::mix(mag, pouch, ease((r - 0.25f) / 0.25f))
          : r < 0.8f ? glm::mix(pouch, mag, ease((r - 0.5f) / 0.3f)) : glm::mix(mag, T, ease((r - 0.8f) / 0.2f));
      }
      if (side == 1 && p.throwT >= 0) {   // right arm throws: wind-up behind the head, release forward-up, follow through
        mat4 C = J(CJ_SPINE2) >= 0 ? Dl[J(CJ_SPINE2)] : Pd;
        vec3 back = xf(C, vec3(-0.40f, 1.60f - drop, -0.26f)), rel = xf(C, vec3(-0.18f, 1.76f - drop, 0.45f)), fol = xf(C, vec3(0.12f, 1.10f - drop, 0.42f));   // AN1 step 2: wind-up beside and behind the shoulder
        float t = p.throwT;
        T = t < 0.45f ? glm::mix(xf(weapon, p.gripR), back, ease(t / 0.45f)) : t < 0.7f ? glm::mix(back, rel, ease((t - 0.45f) / 0.25f)) : glm::mix(rel, fol, ease((t - 0.7f) / 0.3f));
      }
      if (climbW > 0) T = glm::mix(T, p.climbGrip + vec3(side ? -p.climbSpread : p.climbSpread, 0, 0), climbW);   // palms on the ledge (left is +x)
      if (swim > 0) {   // breaststroke in the upright model frame (+y becomes forward once the body tilts): reach, sweep out, in
        float u = std::fmod(sph / 6.2831853f, 1.0f); if (u < 0) u += 1;
        float sx = side ? -1.0f : 1.0f, shy = H(CJ_RARM).y;   // left is +x
        vec3 chestP(0.12f * sx, shy - 0.20f, 0.30f), reach(0.10f * sx, shy + 0.55f, 0.18f), wide(0.55f * sx, shy + 0.30f, 0.10f);
        vec3 S = u < 0.35f ? glm::mix(chestP, reach, ease(u / 0.35f)) : u < 0.65f ? glm::mix(reach, wide, ease((u - 0.35f) / 0.3f)) : glm::mix(wide, chestP, ease((u - 0.65f) / 0.35f));
        T = glm::mix(T, xf(root, S), swim);
      }
      // the palm on the socket: the hand lies along the weapon and the wrist sits ~7.5 cm back from the socket
      {
        vec3 wx = glm::normalize(vec3(weapon[0])), wy = glm::normalize(vec3(weapon[1])), wz = glm::normalize(vec3(weapon[2]));
        vec3 hd = side ? wy * -0.72f + wz * 0.69f : kind == 0 ? wx * -0.55f + wy * -0.45f + wz * 0.70f : wx * -0.30f + wy * -0.15f + wz * 0.94f;
        float w = (1 - climbW) * (1 - swim) * (side == 1 && p.throwT >= 0 ? 0.0f : 1.0f) * (side == 0 && p.reload > 0 && p.reload < 1 ? 0.0f : 1.0f);
        // palms: the firing hand against the grip's right side, the support palm up under the handguard (pistols: cupping the right hand)
        vec3 pn = side ? wx : kind == 0 ? -wx * 0.8f + wy * 0.3f : wy * 0.75f - wx * 0.65f;
        handDir[side] = glm::normalize(hd); palmDir[side] = glm::normalize(pn); handW[side] = w;
        T -= (handDir[side] * 0.075f + palmDir[side] * (side ? 0.03f : 0.04f)) * w;   // the palm's surface (not the bones) against the weapon
      }
      // elbows hang down and in (not winged out)
      vec3 pole = side ? vec3(-0.25f, -1, -0.15f) : vec3(0.12f, -1, -0.05f);   // AN1 step 2: the support elbow under the handguard
      if (side == 0 && kneel > 0) pole = glm::mix(pole, vec3(0.10f, -1, 0.35f), kneel);   // toward the raised left knee
      if (prone > 0) pole = glm::mix(pole, side ? vec3(-0.5f, 0.3f, 1) : vec3(0.5f, 0.3f, 1), prone);   // elbows planted (upright +z is down)
      vec3 E = solve2(Ash, l1, l2, T, pole);
      T = E + glm::normalize(T - E) * std::min(l2, glm::length(T - E));
      quat Rp = rotOf(Pd);
      preArm[side] = between(b0 - a0, glm::inverse(Rp) * (E - Ash));
      quat Ra = Rp * preArm[side];
      preFore[side] = between(c0 - b0, glm::inverse(Ra) * (T - E));
      armSolved[side] = true;
      D[j] = preArm[side];
    } else if (side < 0) {
      if (j == J(CJ_LFOREARM) && armSolved[0]) D[j] = preFore[0];
      if (j == J(CJ_RFOREARM) && armSolved[1]) D[j] = preFore[1];
      int hs = j == J(CJ_LHAND) ? 0 : j == J(CJ_RHAND) ? 1 : -1;
      if (int fh = hs < 0 ? handOf(j) : -1; fh >= 0) {
        int f = j < (int)m.finger.size() ? m.finger[j] : -1;
        if (f >= 0) D[j] = glm::angleAxis(fingerCurl(fh, f, m.fingerSeg[j]), m.fingerAx[j]);
        else if (armSolved[fh] && handW[fh] > 0) D[j] = glm::angleAxis(0.4f * handW[fh], curlAx[fh]);   // unnamed finger joints: one curl
      }
      if (hs >= 0 && armSolved[hs] && handW[hs] > 0 && par >= 0) {
        vec3 rest = H(hs ? CJ_RHAND : CJ_LHAND) - H(hs ? CJ_RFOREARM : CJ_LFOREARM);
        // turn the fingers onto handDir, then roll the rest palm (the rig's own, AN1 step 2) about them onto palmDir
        vec3 rd = glm::normalize(rest), rp = m.palmN[hs] - rd * glm::dot(m.palmN[hs], rd);
        quat q = between(rd, handDir[hs]);
        if (glm::length(rp) > 1e-3f) {
          vec3 hd = handDir[hs], a = q * glm::normalize(rp), b = palmDir[hs] - hd * glm::dot(palmDir[hs], hd);
          if (glm::length(b) > 1e-3f) { b = glm::normalize(b); q = glm::angleAxis(std::atan2(glm::dot(glm::cross(a, b), hd), glm::dot(a, b)), hd) * q; }
        }
        D[j] = glm::slerp(quat(1, 0, 0, 0), glm::inverse(rotOf(Pd)) * q, handW[hs]);
      }
    }
    if (side >= 0 && dead) D[j] = glm::angleAxis((side ? 1.0f : -1.0f) * 0.9f * ease(p.death), vec3(0, 0, 1));   // arms fall to the sides
    int leg = j == J(CJ_LUPLEG) ? 0 : j == J(CJ_RUPLEG) ? 1 : -1;
    if (leg >= 0 && !dead && prone < 0.5f) {
      int sK = leg ? CJ_RLEG : CJ_LLEG, sF = leg ? CJ_RFOOT : CJ_LFOOT;
      vec3 a0 = a0L[leg], b0 = H(sK), c0 = H(sF);
      vec3 Ah = xf(Pd, a0), T = legT[leg];
      float l1 = l1L[leg], l2 = l2L[leg];
      vec3 K = solve2(Ah, l1, l2, T, vec3(0, 0.2f, 1));
      if (leg == 1 && kneel > 0) K = glm::mix(K, kneeK, kneel);   // the right knee on the ground
      quat Rp = rotOf(Pd);
      preLeg[leg] = between(b0 - a0, glm::inverse(Rp) * (K - Ah));
      quat Ru = Rp * preLeg[leg];
      preKnee[leg] = between(c0 - b0, glm::inverse(Ru) * (T - K));
      legSolved[leg] = true;
      D[j] = preLeg[leg];
    }
    if (leg >= 0 && !dead && prone >= 0.5f) D[j] = glm::angleAxis(leg ? -0.18f : 0.18f, vec3(0, 0, 1));   // AN1 step 2: prone legs a little apart
    if (j == J(CJ_LLEG) && legSolved[0]) D[j] = preKnee[0];
    if (j == J(CJ_RLEG) && legSolved[1]) D[j] = preKnee[1];
    if ((j == J(CJ_LFOOT) || j == J(CJ_RFOOT)) && par >= 0)   // the foot flat, or rolled heel to toe (+ = toes down)
      D[j] = glm::inverse(rotOf(Pd)) * footTilt * glm::angleAxis(dead ? 0.0f : footPitch[j == J(CJ_RFOOT)], vec3(1, 0, 0));
    if (dead && (j == J(CJ_LLEG) || j == J(CJ_RLEG))) D[j] = glm::angleAxis(0.25f * ease(p.death), vec3(1, 0, 0));
    if (kneel > 0 && j == J(CJ_RTOE)) D[j] = glm::angleAxis(-1.0f * kneel, vec3(1, 0, 0));   // toes bent flat on the ground
    Dl[j] = Pd * about(m.jointHead[j], D[j]);
  }
  if (!weaponSet) placeWeapon(J(CJ_SPINE2) >= 0 ? Dl[J(CJ_SPINE2)] : root);
  // ---- whole-body transforms: prone (lying forward, head at the capsule origin), death fall about the feet
  mat4 body(1);
  if (prone > 0) body = glm::translate(mat4(1), vec3(0, 0.16f * prone, -1.45f * prone)) * glm::mat4_cast(glm::angleAxis(1.5707963f * prone, vec3(1, 0, 0)));
  if (swim > 0) {   // about the waterline (the feet float ~1.38 m under it): 70 deg forward, a small roll with the stroke
    const vec3 pivot(0, 1.32f, 0);
    body = about(pivot, glm::angleAxis(1.22f * swim, vec3(1, 0, 0)) * glm::angleAxis(0.06f * swim * std::sin(sph), vec3(0, 1, 0)));
  }
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
