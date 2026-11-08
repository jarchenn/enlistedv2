// Swimming (docs/movement.md#swimming). Water is waterAt() (water_query.h): the global sea level, V1 water assets and
// the lasso tool's water bodies, whichever is highest under the point.
//   enter   more than enterDepth (1.25 m, chest) of water over the feet; leave below exitDepth (1.05 m) once grounded
//           again (hysteresis: wading out up a bank never flickers between swimming and walking);
//   move    Physics::updateSwim: no gravity, Jolt collisions kept (banks, piers, wrecks block). Wish speed 2.2 m/s, 3.2
//           with Shift; Space rises, Ctrl dives; submerged (or looking down past diveSteerPitch) the look pitch steers
//           W / S. Buoyancy pulls the feet back to floatDepth (1.38 m) below the surface, gently from below and quickly
//           from above (a jump in); never above it + 0.12 m (Space at the surface only lifts the chin). Velocity follows
//           the wish with drag (4 /s), so a plunge from a pier slows in the water instead of stopping dead;
//   exit    Space, or swimming forward (W) for 0.3 s up to the edge, with the head up: the mantle onto a bank or a pier
//           (mantle.cpp, heights from the floating feet, so a lip 0.3 m above the surface is ~1.7 m and 1 m above it is
//           out of reach), else App::tryClimbBank up a sloped bank to the first spot shallow enough to stand;
//   look    weapon lowered, fire / aim / reload stripped (explore.cpp), hands-only breaststroke arms (viewmodel.cpp),
//           surface bob (bobAmp, ~0.55 Hz + the stroke) and a slight roll with the stroke;
//   under   the camera below waterAt() at the camera: RenderView::underwater (dense blue-green fog, a tint and
//           vignette in the tonemap, render.cpp) and muffled audio (Audio::setUnderwater);
//   breath  30 s with the eye under (also prone in shallow water), 7.5 s back per second above; then drownDps HP per
//           second through the rules damage path (rulesDrown: match, host / single player only), shown in a breath bar.
// Bots never path into deep water (nav.cpp marks it forbidden from waterAt); one pushed in floats (Physics::updateAgent).
#include "app.h"
#include "character.h"
#include "rules.h"
#include "water_query.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace {
float ease(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3 - 2 * t); }
constexpr float kTau = 6.2831853f;
}

void App::loadSwimSettings(const json& j) {
  swimCfg = SwimSettings{};
  SwimSettings& c = swimCfg;
  if (j.contains("swim") && j["swim"].is_object()) {
    const json& s = j["swim"];
    c.enterDepth = s.value("enterDepth_m", c.enterDepth); c.exitDepth = s.value("exitDepth_m", c.exitDepth); c.floatDepth = s.value("floatDepth_m", c.floatDepth);
    c.speed = s.value("speed_mps", c.speed); c.sprint = s.value("sprint_mps", c.sprint); c.rise = s.value("rise_mps", c.rise); c.dive = s.value("dive_mps", c.dive);
    c.buoyancy = s.value("buoyancy_mps", c.buoyancy); c.drag = s.value("drag_per_s", c.drag); c.diveSteerPitch = s.value("diveSteerPitch_deg", c.diveSteerPitch);
    c.bobAmp = s.value("bobAmp_m", c.bobAmp); c.bobHz = s.value("bobHz", c.bobHz); c.rollDeg = s.value("roll_deg", c.rollDeg); c.strokeHz = s.value("strokeHz", c.strokeHz);
    c.breath = s.value("breath_s", c.breath); c.breathRecover = s.value("breathRecover_per_s", c.breathRecover);
    c.drownDps = s.value("drownDamage_hps", c.drownDps); c.drownTick = std::max(0.05f, s.value("drownTick_s", c.drownTick));
    c.fogDensity = s.value("underwaterFog_per_m", c.fogDensity);
    if (s.contains("underwaterFogColor") && s["underwaterFogColor"].is_array() && s["underwaterFogColor"].size() == 3)
      c.fogColor = vec3(s["underwaterFogColor"][0].get<float>(), s["underwaterFogColor"][1].get<float>(), s["underwaterFogColor"][2].get<float>());
  }
  c.exitDepth = std::min(c.exitDepth, c.enterDepth - 0.05f);
  Physics::swimTuning.enter = c.enterDepth; Physics::swimTuning.exit = c.exitDepth; Physics::swimTuning.floatDepth = c.floatDepth;
  Physics::waterQuery = [this](float x, float z) { return waterAt(*this, x, z); };
  swim.breath = c.breath;
}

bool App::eyeUnderwater() const {
  if (mode != Mode::Explore || !Physics::hasCharacter() || occupied || tanks.occupied) return false;
  return underwaterAt(*this, eyePos());
}

void App::swimBreath(float dt) {
  const SwimSettings& S = swimCfg;
  swim.headUnder = eyeUnderwater();
  if (swim.headUnder) swim.breath = std::max(0.0f, swim.breath - dt);
  else { swim.breath = std::min(S.breath, swim.breath + dt * S.breathRecover); swim.drownAcc = 0; }
  if (!swim.headUnder || swim.breath > 0) return;
  swim.drownAcc += dt;
  while (swim.drownAcc >= S.drownTick) {
    swim.drownAcc -= S.drownTick;
    float dmg = S.drownDps * S.drownTick;
    swim.drownDealt += dmg; ++swim.drownTicks;
    if (swim.drownTicks == 1) logf("swim: out of breath, drowning (eye %.2f m under water)", waterAt(*this, eyePos().x, eyePos().z) - eyePos().y);
    rulesDrown(*this, dmg);
  }
}

bool App::stepSwim(float dt, const ExploreInput& in, const Physics::CharState& before) {
  const SwimSettings& S = swimCfg;
  const vec3 feet = before.pos;
  swim.surface = waterAt(*this, feet.x, feet.z);
  swim.depth = waterNone(swim.surface) ? -1e9f : swim.surface - feet.y;
  if (!swim.active && swim.depth > S.enterDepth) {
    swim.active = true; ++swim.enters; swim.why = "enter"; proneToggle = false; riseT = riseDur = 0;
  } else if (swim.active && ((swim.depth < S.exitDepth && before.grounded) || swim.depth < 0.2f)) {
    swim.active = false; ++swim.exits; swim.why = swim.depth < 0.2f ? "exit (out of the water)" : "exit (grounded)";
  }
  swim.lower = std::clamp(swim.lower + (swim.active ? dt : -dt) * 3.5f, 0.0f, 1.0f);   // ~0.3 s either way
  if (!swim.active) { swim.bob = 0; swim.roll *= std::exp(-dt * 8.0f); return false; }
  rulesInWater(*this);   // the water broke any fall: no fall damage when the feet find ground again
  // ---- out onto a bank / pier: hold Space facing it with the head up (mantle.cpp, heights from the floating feet)
  bool nearSurface = swim.depth < S.floatDepth + 0.35f;
  swim.pushT = in.move.y > 0.5f ? swim.pushT + dt : 0;   // swimming forward: the edge ahead (mantle reach, bank scan 1.6 m) is tried
  bool wantOut = (in.jumpHeld && !vault.latch) || swim.pushT > 0.3f;
  if (wantOut && nearSurface && (tryStartMantle(feet, eyeYaw, true) || tryClimbBank(feet, eyeYaw, swim.surface))) {
    vault.latch = true; swim.active = false; ++swim.mantles; ++swim.exits; swim.why = "mantle out";
    return true;
  }
  // ---- movement
  postureWanted = Physics::STAND; proneToggle = false;   // Z / Ctrl do not lie down in the water (Ctrl dives)
  const float target = swim.surface - S.floatDepth;   // feet height when floating
  bool under = swim.depth > S.floatDepth + 0.3f;       // the body is under the surface (diving)
  vec3 f = forward(eyeYaw, 0), rgt(std::cos(radians(eyeYaw)), 0, -std::sin(radians(eyeYaw)));
  vec3 fw = (under || eyePitch < -S.diveSteerPitch) ? forward(eyeYaw, eyePitch) : f;   // submerged, the look steers
  vec3 wish = fw * in.move.y + rgt * in.move.x;
  if (glm::length(wish) > 1) wish = glm::normalize(wish);
  const bool fast = in.sprint && stamina > 1.0f;   // sprint-stroke drains stamina like a run
  tickStamina(dt, fast && (in.move.x != 0 || in.move.y != 0));
  wish *= (fast ? S.sprint : S.speed) * traversalFactor(feet);
  float vy = in.jumpHeld ? S.rise : in.crouch ? -S.dive : 0.0f;
  if (!in.jumpHeld && !in.crouch) {
    float dy = target - feet.y;
    if (dy < 0) vy = std::max(-1.5f, dy * 2.0f);                                    // above the floating height (a jump in): settle
    else if (!(under && in.move.y != 0)) vy = std::min(S.buoyancy, dy * 2.0f);      // below it: drift up (not while swimming on under water)
  }
  vec3 want(wish.x, wish.y + vy, wish.z);
  const float ceiling = target + 0.12f;   // Space at the surface lifts the chin, no more
  want.y = std::min(want.y, std::max((ceiling - feet.y) / std::max(dt, 1e-4f), -3.0f));
  vec3 v = before.vel + (want - before.vel) * (1 - std::exp(-S.drag * dt));
  if (!occupied) Physics::updateSwim(dt, v);
  auto c = Physics::character();
  vm.speed = glm::length(vec2(c.pos.x - before.pos.x, c.pos.z - before.pos.z)) / std::max(dt, 1e-4f);
  // ---- stroke, surface bob and roll
  float sp = glm::length(c.pos - before.pos) / std::max(dt, 1e-4f);
  float k = std::clamp(sp / std::max(0.1f, S.speed), 0.0f, 1.0f);
  float prevPhase = swim.phase;
  swim.phase += dt * S.strokeHz * (0.35f + 0.65f * k);
  float depthNow = swim.surface - c.pos.y;
  bool atSurface = depthNow < S.floatDepth + 0.25f;
  float red = reducedMotion ? 0.3f : 1.0f;
  swim.bobT += dt;
  float amp = atSurface ? S.bobAmp * red : 0.0f;
  swim.bob = amp * (0.6f * std::sin(kTau * S.bobHz * swim.bobT) + 0.4f * std::sin(kTau * 2.0f * swim.phase));
  float rollT = S.rollDeg * red * std::sin(kTau * swim.phase) * (atSurface ? 1.0f : 0.5f);
  swim.roll += (rollT - swim.roll) * std::min(1.0f, dt * 6.0f);
  float eyeT = move.eyeStand + swim.bob;
  eyeHeight += (eyeT - eyeHeight) * std::min(1.0f, dt * 8.0f);
  if (std::floor(swim.phase) != std::floor(prevPhase) && atSurface && k > 0.3f && audio.hasEvent("step_water")) audio.play("step_water", c.pos + vec3(0, S.floatDepth, 0), 0.35f);   // a splash per stroke
  return true;
}

float App::swimLower() const { return ease(swim.lower); }

vec3 App::viewUp(vec3 fwd) const {
  if (std::fabs(swim.roll) < 1e-3f || glm::length(fwd) < 1e-4f) return vec3(0, 1, 0);
  return glm::angleAxis(radians(swim.roll), glm::normalize(fwd)) * vec3(0, 1, 0);
}

bool App::swimHands(vec3& left, vec3& right, float& w) const {
  w = swimLower();
  if (w <= 0) return false;
  // breaststroke in camera space (+x right, +y up, -z forward), right hand; the left mirrors x:
  //   chest -> reach forward (glide) -> sweep out -> sweep in under the chin -> chest
  static const vec3 K[5] = {{0.10f, -0.27f, -0.22f}, {0.08f, -0.15f, -0.52f}, {0.09f, -0.13f, -0.56f}, {0.36f, -0.18f, -0.42f}, {0.20f, -0.31f, -0.27f}};
  static const float T[6] = {0.0f, 0.30f, 0.42f, 0.66f, 0.86f, 1.0f};
  float u = swim.phase - std::floor(swim.phase);
  int s = 0; while (s < 4 && u >= T[s + 1]) ++s;
  vec3 a = K[s], b = K[(s + 1) % 5];
  vec3 p = glm::mix(a, b, ease((u - T[s]) / std::max(1e-4f, T[s + 1] - T[s])));
  right = p; left = vec3(-p.x, p.y, p.z);
  return true;
}

void App::swimPose(PoseParams& pp) const {
  pp.swim = swimLower();
  pp.swimPhase = swim.phase * kTau;
}

void App::drawSwimUI() {
  const SwimSettings& S = swimCfg;
  if (swim.breath >= S.breath - 0.01f && !swim.headUnder) return;
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  float W = 240, H = 10, x = io.DisplaySize.x * 0.5f - W * 0.5f, y = io.DisplaySize.y - 132;
  float f = std::clamp(swim.breath / std::max(0.01f, S.breath), 0.0f, 1.0f);
  bool drowning = swim.breath <= 0 && swim.headUnder;
  ImU32 col = drowning ? ((int)(now * 4) % 2 ? IM_COL32(230, 60, 50, 240) : IM_COL32(120, 30, 25, 240)) : f < 0.25f ? IM_COL32(230, 120, 60, 230) : IM_COL32(110, 190, 235, 230);
  dl->AddRectFilled(ImVec2(x - 2, y - 2), ImVec2(x + W + 2, y + H + 2), IM_COL32(10, 14, 18, 170), 3);
  dl->AddRectFilled(ImVec2(x, y), ImVec2(x + W * f, y + H), col, 2);
  const char* t = drowning ? "Drowning: surface!" : "Breath";
  ImVec2 ts = ImGui::CalcTextSize(t);
  dl->AddText(ImVec2(io.DisplaySize.x * 0.5f - ts.x * 0.5f, y - ts.y - 4), IM_COL32(225, 235, 240, 220), t);
}
