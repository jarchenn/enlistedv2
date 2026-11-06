// First-person weapon handling. View space here is the camera frame: +x right, +y up, -z forward.
// Weapon space: butt at z = 0, muzzle toward +z, top +y (the import pipeline and the box parts share it).
#include "app.h"
#include <algorithm>
#include <glm/gtx/quaternion.hpp>

float smoothDamp(float cur, float target, float& vel, float smoothTime, float dt) {
  // critically damped approach (Game Programming Gems 4, 1.10): exact for a constant target
  float omega = 2.0f / std::max(1e-4f, smoothTime), x = omega * dt;
  float e = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
  float change = cur - target, temp = (vel + omega * change) * dt;
  vel = (vel - omega * temp) * e;
  return target + (change + temp) * e;
}

void solveTwoBone(vec3 S, vec3 T, float a, float b, vec3 pole, vec3& elbow, vec3& hand) {
  vec3 st = T - S;
  float dist = glm::length(st);
  vec3 u = dist > 1e-6f ? st / dist : vec3(0, 0, -1);
  float d = std::clamp(dist, std::fabs(a - b) + 1e-4f, a + b - 1e-4f);
  float ca = std::clamp((a * a + d * d - b * b) / (2 * a * d), -1.0f, 1.0f), sa = std::sqrt(1 - ca * ca);
  vec3 n = pole - u * glm::dot(u, pole);
  n = glm::dot(n, n) > 1e-10f ? glm::normalize(n) : glm::normalize(glm::cross(u, vec3(1, 0, 0)));
  elbow = S + u * (a * ca) + n * (a * sa);
  hand = S + u * d;
}

bool App::vmScoped(const WeaponDef& d) const { return d.hasScope && !(d.scopeIrons && scopeIronsMode); }

// ADS alignment: rotate the weapon so its sight line L = normalize(F - R) lies on the camera's forward axis, then
// translate so the rear aperture R sits on that axis at the eye relief:
//   q    = rotation(L -> +z_weapon), Q = Ry(180) * q          (weapon +z -> view -z)
//   T_ads = (0, 0, -relief) - Q * R                             (R lands at view (0, 0, -relief))
//   T_hip = (0, 0, -0.21) - Q * R + hipOffset                     (default (0.12, -0.07, 0.06); independent of the ADS calibration)
// Per-weapon calibration (WeaponDef, JSON "ads"): relief = eyeRelief; C = euler(rotCorrection) is applied in view
// space about R and T_ads += posCorrection, so with both zero R and F sit exactly on the view axis.
static void sightFrame(const WeaponDef& d, bool scoped, quat& Q, vec3& Rp, vec3& Fp, float& relief) {
  Rp = scoped ? d.scopeEye : d.rear;
  Fp = scoped ? d.scopeEye + vec3(0, 0, 0.25f) : d.front;
  relief = scoped ? 0.07f : d.eyeRelief;
  vec3 L = glm::normalize(Fp - Rp);
  quat q = glm::rotation(L, vec3(0, 0, 1));
  Q = glm::angleAxis(3.14159265f, vec3(0, 1, 0)) * q;
  if (!scoped && d.adsRot != vec3(0)) Q = glm::quat(glm::radians(d.adsRot)) * Q;
}

vec3 App::vmTarget(const WeaponDef& d) const {
  quat Q; vec3 Rp, Fp; float relief;
  bool scoped = vmScoped(d);
  sightFrame(d, scoped, Q, Rp, Fp, relief);
  vec3 ads = vec3(0, 0, -relief) - Q * Rp + (scoped ? vec3(0) : d.adsPos);
  vec3 hip = vec3(0, 0, scoped ? -relief : -0.21f) - Q * Rp + d.hipOffset;
  float t = adsT * adsT * (3 - 2 * adsT);
  return glm::mix(hip, ads, t);
}

float App::vmFovNow() const {
  if (weaponDefs.empty()) return vm.vmFov;
  const WeaponDef& d = *weaponDefs[activeWeapon];
  if (d.adsVmFov <= 0 || vmScoped(d)) return vm.vmFov;
  float t = adsT * adsT * (3 - 2 * adsT);
  return glm::mix(vm.vmFov, d.adsVmFov, t);
}

void App::fpCamera(mat4& view, float& worldFovNow) const {
  // the camera the weapon is posed against this frame (viewModelMatrix), and the world FOV that goes with it
  vec3 e = eyePos(), f = forward(eyeYaw + viewKickYaw, eyePitch + viewKickPitch);
  view = glm::lookAt(e, e + f, vec3(0, 1, 0));
  worldFovNow = 72.0f;
  weaponCamera(worldFovNow);
}

mat4 App::vmLocal(const WeaponDef& d, const WeaponState& s, bool withProcedural) const {
  quat Q; vec3 Rp, Fp; float relief;
  sightFrame(d, vmScoped(d), Q, Rp, Fp, relief);
  vec3 p = vm.snap ? vmTarget(d) : vm.pos;
  mat4 rot(1);
  if (withProcedural && vm.procedural) {
    p += vm.swayP + vm.bob;
    vec3 pivot = p + Q * Rp;    // rotate about the rear sight so ADS stays close to the line
    rot = glm::translate(mat4(1), pivot) *
          glm::mat4_cast(glm::angleAxis(radians(vm.swayR.x), vec3(0, 1, 0)) * glm::angleAxis(radians(vm.swayR.y), vec3(1, 0, 0))) *
          glm::translate(mat4(1), -pivot);
  }
  p.z += s.kick * (reducedMotion ? 0.3f : 1.0f);   // recoil push into the shoulder
  mat4 m = rot * glm::translate(mat4(1), p) * glm::mat4_cast(Q);
  // reload / switch dip
  if (s.reload > 0 && s.reloadKind != 3) {
    float u = 1 - s.reload / std::max(0.01f, s.reloadTotal);
    float dip = std::sin(u * 3.14159f);
    m = m * glm::translate(mat4(1), vec3(0, -0.06f * dip, 0.25f)) * glm::mat4_cast(glm::angleAxis(radians(-22.0f * dip), vec3(0, 0, 1))) * glm::translate(mat4(1), vec3(0, 0, -0.25f));
  }
  return m;
}

void App::sightLineWorld(const WeaponDef& d, const WeaponState& s, vec3& rear, vec3& front) const {
  quat Q; vec3 Rp, Fp; float relief;
  sightFrame(d, vmScoped(d), Q, Rp, Fp, relief);
  mat4 M = viewModelMatrix(d, s);
  rear = vec3(M * vec4(Rp, 1)); front = vec3(M * vec4(Fp, 1));
}

vec3 App::vmApparent(vec3 w) const {
  // a first-person point drawn with the fixed vmFov appears where the world projection puts
  //   x' = x * tan(worldFov / 2) / tan(vmFov / 2)   (same for y, same view depth)
  vec4 v = viewM * vec4(w, 1);
  float k = std::tan(radians(worldFov) * 0.5f) / std::tan(radians(vm.vmFov) * 0.5f);
  v.x *= k; v.y *= k;
  return vec3(glm::inverse(viewM) * v);
}

bool App::sightMarks(std::vector<SightMark>& out) const {
  // Calibration markers in framebuffer pixels (origin top-left): rear aperture / notch, front post tip and muzzle
  // through the weapon projection; the aim point (eye ray hit) and the flash / tracer origin through the world one.
  out.clear();
  if (occupied || weaponDefs.empty() || mode != Mode::Explore || R.width <= 0) return false;
  const WeaponDef& d = *weaponDefs[activeWeapon];
  auto it = wstate.find(d.id); if (it == wstate.end()) return false;
  const WeaponState& s = it->second;
  mat4 view; float wf; fpCamera(view, wf);
  float aspect = (float)R.width / std::max(1, R.height);
  mat4 vmVP = glm::perspective(radians(vmFovNow()), aspect, 0.01f, 6.0f) * view;
  mat4 wVP = glm::perspective(radians(wf), aspect, 0.03f, 4000.0f) * view;
  auto px = [&](const mat4& vp, vec3 w) { vec4 c = vp * vec4(w, 1); return vec2((c.x / c.w * 0.5f + 0.5f) * R.width, (0.5f - c.y / c.w * 0.5f) * R.height); };
  mat4 M = viewModelMatrix(d, s);
  vec3 Rw = vec3(M * vec4(d.rear, 1)), Fw = vec3(M * vec4(d.front, 1)), Mw = vec3(M * vec4(d.muzzle, 1));
  out.push_back({px(vmVP, Rw), 0xFFFFD000u, 0});   // rear: cyan ring (ABGR)
  out.push_back({px(vmVP, Fw), 0xFF30FF30u, 1});   // front tip: green cross
  out.push_back({px(vmVP, Mw), 0xFF0080FFu, 2});   // muzzle: orange square
  out.push_back({px(wVP, vmApparent(Mw)), 0xFFFFFFFFu, 3});   // flash / tracer origin (world projection): white dot
  vec3 e = eyePos(), f = forward(eyeYaw + viewKickYaw, eyePitch + viewKickPitch);
  Physics::Hit h;
  vec3 P = Physics::castRayEx(e, f, 600, h) ? h.pos : e + f * 600.0f;
  out.push_back({px(wVP, P), 0xFF2020FFu, 4});     // aim point: red ring
  return true;
}

void App::stepViewModel(float dt, const ExploreInput& in, const WeaponDef& d, const WeaponState& s) {
  (void)s;
  float a = adsT * adsT * (3 - 2 * adsT);
  // ---- ADS spring: critically damped, x'' = w^2 (target - x) - 2 w x'
  vec3 tgt = vmTarget(d);
  if (vm.snap) { vm.pos = tgt; vm.vel = vec3(0); vm.snap = false; }
  else {
    vec3 acc = kAdsOmega * kAdsOmega * (tgt - vm.pos) - 2.0f * kAdsOmega * vm.vel;
    vm.vel += acc * dt; vm.pos += vm.vel * dt;
  }
  if (!vm.procedural) {
    vm.swayP = vm.swayPv = vm.bob = vec3(0); vm.swayR = vm.swayRv = vec2(0);
    vm.breathPitch = vm.breathYaw = 0; vm.holding = false;
    return;
  }
  float red = reducedMotion ? 0.3f : 1.0f, adsS = glm::mix(1.0f, 0.35f, a);
  // ---- sway: inverted, clamped look delta; smooth-damped back to centre
  float lim = kSwayClampPx * 0.1f;
  float dx = std::clamp(in.dyaw + in.lookYaw, -lim, lim), dy = std::clamp(in.dpitch + in.lookPitch, -lim, lim);
  vec3 tp = vec3(dx * 0.006f, -dy * 0.006f, 0) * adsS * red;
  vec2 tr = vec2(dx * 1.6f, -dy * 1.6f) * adsS * red;
  for (int k = 0; k < 2; ++k) {
    vm.swayP[k] = smoothDamp(vm.swayP[k], tp[k], vm.swayPv[k], kSwaySmooth, dt);
    vm.swayR[k] = smoothDamp(vm.swayR[k], tr[k], vm.swayRv[k], kSwaySmooth, dt);
  }
  // ---- bob: layered sines on horizontal speed; vertical at step rate, horizontal at half (weight shift)
  //   phi += pi * v / stride * dt;  y = Ay sin(2 phi), x = Ax sin(phi);  Ay = 12 mm, Ax = 8 mm at sprint speed
  auto ch = Physics::character();
  float v = vm.speed;
  vm.bobPhase += 3.14159265f * v / 1.5f * dt;
  float k = std::min(v / 4.6f, 1.0f) * (1 - a) * (ch.crouched ? 0.5f : 1.0f) * red * (ch.grounded ? 1.0f : 0.0f);
  vm.bobAmp = k;
  vm.bob = vec3(0.008f * k * std::sin(vm.bobPhase), 0.012f * k * std::sin(2 * vm.bobPhase), 0);
  // ---- breathing: slow vertical wander; Left Shift in ADS holds the breath (max 5 s, then 3 s recovery at 2x)
  bool want = a > 0.85f && in.sprint;
  vm.holding = want && vm.breathRecover <= 0 && vm.breathHeld < kBreathHoldMax;
  if (vm.holding) { vm.breathHeld += dt; if (vm.breathHeld >= kBreathHoldMax) { vm.breathRecover = kBreathRecover; vm.holding = false; } }
  else vm.breathHeld = std::max(0.0f, vm.breathHeld - dt * kBreathHoldMax / kBreathRecover);
  vm.breathRecover = std::max(0.0f, vm.breathRecover - dt);
  float amp = kBreathAmpDeg * (vm.breathRecover > 0 ? 2.0f : 1.0f) * (v > 0.3f ? 0.5f : 1.0f) * red;
  if (!vm.holding) vm.breathT += dt;
  vm.breathPitch = amp * std::sin(6.2831853f * kBreathHz * vm.breathT);
  vm.breathYaw = 0.5f * amp * std::sin(3.14159265f * kBreathHz * vm.breathT);
}

void App::dofParams(float& focus, float& amount) const {
  focus = 0; amount = 0;
  if (occupied || weaponDefs.empty() || mode != Mode::Explore) return;
  const WeaponDef& d = *weaponDefs[activeWeapon];
  if (vmScoped(d) || reducedMotion) return;
  quat Q; vec3 Rp, Fp; float relief;
  sightFrame(d, false, Q, Rp, Fp, relief);
  focus = relief + glm::length(Fp - Rp) + 0.03f;   // just beyond the front sight post
  amount = std::clamp((adsT - 0.6f) / 0.4f, 0.0f, 1.0f);
}

// ---------------------------------------------------------------- IK arms
void App::buildArmMeshes() {
  Material m[3] = {{"sleeve_us", vec3(0.25f, 0.23f, 0.14f), 0.92f, 0.0f, PAT_FABRIC, 1},
                   {"sleeve_de", vec3(0.29f, 0.30f, 0.27f), 0.92f, 0.0f, PAT_FABRIC, 1},
                   {"skin", vec3(0.56f, 0.41f, 0.33f), 0.62f, 0.0f, PAT_PLAIN, 1}};
  for (int k = 0; k < 3; ++k) {
    Part p; p.center = vec3(0, 0, 0.5f); p.size = vec3(1);
    MeshData md; appendPart(md, p, m[k], mat4(1)); armMesh[k].upload(md);
  }
}

void App::armDrawItems(const mat4& weaponWorld, const WeaponDef& d, const WeaponState& s) {
  if (!vm.showArms || !armMesh[0].count) return;
  mat4 cam = weaponWorld * glm::inverse(vmLocal(d, s));   // camera (view -> world)
  mat4 Mv = vmLocal(d, s);
  vec3 tR = vec3(Mv * vec4(d.gripR, 1)), tL = vec3(Mv * vec4(d.gripL, 1));
  // during a magazine / clip reload the support hand goes to the feed
  if (s.reload > 0 && s.reloadKind != 3) {
    float u = 1 - s.reload / std::max(0.01f, s.reloadTotal);
    vec3 feed = d.feed == "enbloc" || d.feed == "stripper" ? (d.rear + vec3(0, 0.04f, 0.08f)) : vec3(0, -0.12f, (d.rear.z + d.front.z) * 0.5f);
    float w = std::sin(std::clamp(u, 0.0f, 1.0f) * 3.14159f);
    tL = glm::mix(tL, vec3(Mv * vec4(feed, 1)), w);
  }
  vec3 SR(0.17f, -0.24f, 0.02f), SL(-0.17f, -0.24f, 0.02f);
  vec3 eR, hR, eL, hL;
  solveTwoBone(SR, tR, kUpperArm, kForearm, vec3(0.4f, -1, 0), eR, hR);
  solveTwoBone(SL, tL, kUpperArm, kForearm, vec3(-0.4f, -1, 0), eL, hL);
  lastHandErr = vec3(glm::length(hR - tR), glm::length(hL - tL), 0);
  lastElbowR = eR; lastElbowL = eL; lastHandR = hR; lastHandL = hL; lastShoulderR = SR; lastShoulderL = SL;
  int sleeve = d.side == "US" ? 0 : 1;
  auto seg = [&](vec3 a, vec3 b, float w, float h, int mesh) {
    vec3 z = b - a; float len = glm::length(z); if (len < 1e-4f) return;
    z /= len;
    vec3 x = glm::cross(vec3(0, 1, 0), z); if (glm::dot(x, x) < 1e-6f) x = vec3(1, 0, 0); x = glm::normalize(x);
    vec3 y = glm::cross(z, x);
    mat4 m(vec4(x * w, 0), vec4(y * h, 0), vec4(z * len, 0), vec4(a, 1));
    R.viewModel.push_back({&armMesh[mesh], cam * m});
  };
  auto arm = [&](vec3 S, vec3 E, vec3 H) {
    seg(S, E, 0.095f, 0.095f, sleeve);
    vec3 cuff = glm::mix(E, H, 0.9f);
    seg(E, cuff, 0.078f, 0.075f, sleeve);
    seg(cuff, H, 0.055f, 0.05f, 2);   // wrist
  };
  arm(SR, eR, hR);
  arm(SL, eL, hL);
  // hands: blocks wrapped round the grip / under the handguard, aligned with the weapon so they stay below the
  // sight line in ADS (weapon space: box spans x,y [-0.5, 0.5] and z [0, 1] before scaling)
  auto hand = [&](vec3 socket, vec3 size) {
    mat4 m = glm::translate(mat4(1), socket + vec3(0, -size.y * 0.3f, -size.z * 0.5f)) * glm::scale(mat4(1), size);
    R.viewModel.push_back({&armMesh[2], weaponWorld * m});
  };
  hand(d.gripR, vec3(0.05f, 0.06f, 0.085f));
  hand(vec3(glm::inverse(Mv) * vec4(tL, 1)), vec3(0.065f, 0.035f, 0.09f));
}
