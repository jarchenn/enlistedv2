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
// FA2 grip profile (weapon space; content/settings/fps_arms.json "default" / "weapons"."<id>", docs/fps-arms.md): the held
// section is an ellipse round `axis` (the index finger's side) whose bottom (the palm side, opposite `up`) is `grip`
struct ArmGrip {
  vec3 grip{0}, axis{0, 0, 1}, up{0, 1, 0};
  vec2 section{0.022f, 0.022f};   // half width (toward the thumb side), half height (along up)
  float palm = 0;                 // deg: the heel's place round the section (+ = higher on the near side)
  float curl = 1, spread = 0;     // finger wrap (times the pose's curl), extra finger spread (0 = natural)
  float thumb = 0;                // deg: the thumb tip's place round the section (+ = higher)
  vec3 elbow{-0.55f, -1.0f, 0.1f};   // elbow pole (camera space, mirrored for the right hand)
  vec3 shoulder{0};               // shoulder offset from the default (camera space, mirrored)
  vec3 ads{0};                    // grip offset in ADS (weapon space, times the ADS blend)
  float slide = 0.08f;            // out of reach: how far the hand may slide back along the section
};
bool fpsArmsReady();   // FA2 (fps_arms.cpp): the generated first-person arm loaded (else the box arms draw)
struct GpuModel; bool fpsArmsIsModel(const GpuModel* g);   // a Renderer::viewModelTex draw of the arm (self-tests)
int fpsArmsFaction(const GpuModel* g);                      // the arm model's sleeve: 0 Allies (olive drab), 1 Axis (charcoal), -1 not the arm
struct WeaponDef; void fpsArmsGrip(const WeaponDef& d, ArmGrip& g);   // a weapon's grip profile (measured fore-end + fps_arms.json)
void fpsArmsLastFraming(vec2& handLo, vec2& handHi, vec2& sleeve15);   // the last drawn arm in NDC: hand bounds, 15 cm up the sleeve past the cuff (self-tests)
void fpsArmsLastJoints(vec3& shoulder, vec3& elbow, vec3& wrist, vec3& target);   // the last drawn arm's joints, camera space (self-tests)
// FA2: draw the one support arm (fps_arms.cpp) holding grip profile g for the weapon->camera transform weaponToCam;
// cam = camera->world. curl 1 = wrapped round the section, 0 relaxed; rightHand = the mirrored hand with its arm from
// the lower right; axis = the Axis (charcoal) sleeve. A new weapon gets its arm from a profile in fps_arms.json alone.
class App; bool fpsSupportHandDraw(App& app, const mat4& cam, const mat4& weaponToCam, const ArmGrip& g, float curl, bool rightHand, bool axis, vec3* wristOut = nullptr);

float smoothDamp(float cur, float target, float& vel, float smoothTime, float dt);

// Analytic two-bone IK. Shoulder S, target T, bone lengths a (upper) and b (fore), pole hint P.
// d = clamp(|T - S|, |a - b|, a + b); cos(alpha) = (a^2 + d^2 - b^2) / (2 a d)  (law of cosines at the shoulder)
// u = (T - S)/|T - S|; n = normalize(P - u (u.P)); E = S + u a cos(alpha) + n a sin(alpha); H = S + u d.
// Out of reach the arm straightens toward the target without stretching (H stops short of T).
void solveTwoBone(vec3 S, vec3 T, float a, float b, vec3 pole, vec3& elbow, vec3& hand);
