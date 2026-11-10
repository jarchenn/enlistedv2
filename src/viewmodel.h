#pragma once
// First-person weapon handling (the guide's "WeaponHandler"): ADS alignment on the real sight line, spring
// interpolation, procedural sway / bob / breathing, IK arms, depth-of-field focus. See docs/fps-weapons.md.
#include "common.h"

struct ViewModelState {
  // spring-followed view-space offset of the weapon (critically damped, omega = kAdsOmega)
  vec3 pos{0}, vel{0};
  bool snap = true;                     // jump straight to the target (weapon switch / first frame)
  // sway: inverted, clamped mouse delta; smooth-damped back to centre
  vec3 swayP{0}, swayPv{0};             // position (m, view space)
  vec2 swayR{0}, swayRv{0};             // rotation (deg: yaw, pitch)
  // bob: Lissajous on horizontal speed
  float bobPhase = 0, speed = 0, bobAmp = 0;
  vec3 bob{0};
  // breathing (deg) and hold-breath
  float breathT = 0, breathHeld = 0, breathRecover = 0, breathPitch = 0, breathYaw = 0;
  bool holding = false;
  bool procedural = true;               // sway / bob / breathing on (legacy self-tests switch it off)
  bool showArms = true;
  float vmFov = 60.0f;                  // fixed first-person projection FOV (deg)
};

// constants shared with the self-tests
constexpr float kAdsOmega = 18.0f;        // ADS spring (1/s), critically damped
constexpr float kSwayClampPx = 6.0f;      // mouse delta clamp per frame (deg of look here: 6 * 0.1)
constexpr float kSwaySmooth = 0.12f;      // s
constexpr float kBreathAmpDeg = 0.08f;    // idle aim wander amplitude
constexpr float kBreathHz = 0.22f;
constexpr float kBreathHoldMax = 5.0f, kBreathRecover = 3.0f;
constexpr float kUpperArm = 0.32f, kForearm = 0.30f;
bool fpsArmsReady();   // FA1 (fps_arms.cpp): the rigged first-person arms loaded (else the box arms draw)
struct GpuModel; bool fpsArmsIsModel(const GpuModel* g);   // a Renderer::viewModelTex draw of the arm (self-tests)
int fpsArmsFaction(const GpuModel* g);                      // the arm model's sleeve: 0 Allies (olive drab), 1 Axis (charcoal), -1 not the arm
struct WeaponDef; void fpsArmsGrip(const WeaponDef& d, vec3& grip, float& radius);   // FA2: a weapon's support grip (weapon space) and section radius
// FA2: draw the one support arm (fps_arms.cpp) holding a section of radius "radius" whose bottom is at "grip" (weapon space,
// section axis = weapon +z) for the weapon->camera transform weaponToCam; cam = camera->world. curl 1 = wrapped round
// it, 0 relaxed; rightHand = the mirrored hand with its sleeve from the lower right; axis = the Axis (charcoal) sleeve.
// For a new weapon (a flamethrower's tube): call it from its viewmodel draw with its support grip and tube radius.
class App; bool fpsSupportHandDraw(App& app, const mat4& cam, const mat4& weaponToCam, vec3 grip, float radius, float curl, bool rightHand, bool axis, vec3* wristOut = nullptr);

float smoothDamp(float cur, float target, float& vel, float smoothTime, float dt);

// Analytic two-bone IK. Shoulder S, target T, bone lengths a (upper) and b (fore), pole hint P.
// d = clamp(|T - S|, |a - b|, a + b); cos(alpha) = (a^2 + d^2 - b^2) / (2 a d)  (law of cosines at the shoulder)
// u = (T - S)/|T - S|; n = normalize(P - u (u.P)); E = S + u a cos(alpha) + n a sin(alpha); H = S + u d.
// Out of reach the arm straightens toward the target without stretching (H stops short of T).
void solveTwoBone(vec3 S, vec3 T, float a, float b, vec3 pole, vec3& elbow, vec3& hand);
