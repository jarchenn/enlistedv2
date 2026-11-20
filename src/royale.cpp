// BR1 Battle royale: the drop (bomber line, free fall, parachute), the storm (four shrinking circles), placement and
// the HUD. See royale.h for the hooks; numbers in content/settings/royale.json.
#include "smithy.h"
#include "royale.h"
#include "ui_glass.h"   // UI2: glass panel backgrounds
#include "app.h"
#include "game.h"
#include <DetourNavMeshQuery.h>
#include "minimap.h"
#include "enemy_types.h"
#include "mptest.h"
#include "loot.h"
#include "br_models.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <chrono>
#include <random>
#include <algorithm>

// ---------------------------------------------------------------- storm (pure)
void RoyaleStorm::build(vec2 half, uint32_t seed, bool (*walkable)(vec2)) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> u(0.0f, 1.0f);
  float R0 = glm::length(half);   // the whole play area
  circle[0] = {vec2(0), R0};
  float base = std::min(half.x, half.y) * 2.0f;   // circles 1..3 as a share of the area's shorter side (1 = the side)
  float r[5] = {R0, cfg.radiusFrac[0] * base, cfg.radiusFrac[1] * base, cfg.radiusFrac[2] * base, cfg.finalRadius};
  for (int k = 1; k <= 4; ++k) {
    r[k] = std::min(r[k], r[k - 1] * 0.95f);
    const RoyaleRing& p = circle[k - 1];
    float room = std::max(0.0f, p.r - r[k]);   // the new circle lies wholly inside the previous one
    vec2 best = p.c;
    for (int tries = 0; tries < 60; ++tries) {
      float a = u(rng) * 6.2831853f, d = room * std::sqrt(u(rng));
      vec2 c = p.c + vec2(std::cos(a), std::sin(a)) * d;
      best = c;
      if (std::fabs(c.x) + r[k] * 0.5f <= half.x && std::fabs(c.y) + r[k] * 0.5f <= half.y && (!walkable || walkable(c))) break;   // mostly over the map, centred on floor (BR11)
    }
    circle[k] = {best, r[k]};
  }
  // BR7: the drift of the final circle (enough legs for ~20 min), inside the area and (when known) over walkable ground
  drift.assign(1, circle[4].c);
  vec2 lim = glm::max(half - vec2(circle[4].r), vec2(0));
  for (int k = 0; k < 36; ++k) {
    vec2 from = drift.back(), to = from;
    for (int tries = 0; tries < 40; ++tries) {
      float a = u(rng) * 6.2831853f, d = cfg.driftMin + u(rng) * (cfg.driftMax - cfg.driftMin);
      vec2 c = glm::clamp(from + vec2(std::cos(a), std::sin(a)) * d, -lim, lim);
      if (glm::length(c - from) < cfg.driftMin * 0.8f) continue;   // pushed back by the border: try another bearing
      to = c;
      if (!walkable || walkable(c)) break;
    }
    drift.push_back(to);
  }
}

RoyaleStorm::At RoyaleStorm::at(float t) const {
  At a; float s = std::max(0.0f, t);
  for (int k = 0; k < 4; ++k) {
    a.stage = k; a.dps = cfg.dps[k]; a.next = circle[k + 1];
    if (s < cfg.wait[k]) { a.shrinking = false; a.left = cfg.wait[k] - s; a.now = circle[k]; return a; }
    s -= cfg.wait[k];
    if (s < cfg.shrink[k]) {
      float f = s / std::max(0.01f, cfg.shrink[k]);
      a.shrinking = true; a.left = cfg.shrink[k] - s;
      a.now = {glm::mix(circle[k].c, circle[k + 1].c, f), glm::mix(circle[k].r, circle[k + 1].r, f)};
      return a;
    }
    s -= cfg.shrink[k];
  }
  a.stage = 4; a.dps = cfg.dps[3]; a.now = a.next = circle[4];
  if (drift.size() < 2) { a.shrinking = false; a.left = 0; return a; }
  float cyc = cfg.driftPause + cfg.driftMove;   // BR7: pause, then glide to the next centre; the next ring is where it goes
  int k = std::min((int)(s / cyc), (int)drift.size() - 2); float w = s - k * cyc;
  a.leg = k; a.next = {drift[k + 1], circle[4].r};
  if (w < cfg.driftPause) { a.shrinking = false; a.left = cfg.driftPause - w; a.now = {drift[k], circle[4].r}; }
  else { float f = std::min(1.0f, (w - cfg.driftPause) / cfg.driftMove); f = f * f * (3 - 2 * f);
         a.shrinking = true; a.left = std::max(0.0f, cyc - w); a.now = {glm::mix(drift[k], drift[k + 1], f), circle[4].r}; }
  return a;
}

// ---------------------------------------------------------------- settings and state
namespace {
struct Cfg {
  float planeAlt = 300, planeSpeed = 45, overrun = 150, jitter = 0.25f;
  // BR13 deployment (royale.json "exit", "freefall", "deploy", "canopy", "flare", "landing", "camera", "sound")
  float exitTime = 0.8f, exitDrop = 2.0f;
  float gravity = 9.81f, termFlat = 52, termDive = 88, termSpread = 44, diveLook = 60, attIn = 1.4f, attOut = 0.55f;
  float trackFlat = 32, trackDive = 16, driftFlat = 4, driftDive = 9, slide = 9, tauH = 2.2f, tauSpread = 1.2f, turnRate = 75;
  float autoOpen = 60, deployTime = 1.8f, unfold = 0.45f, maxDecel = 32, swingKick = 0.6f;
  float glide = 9, glideFast = 12.5f, glideBrake = 3.5f, desc = 5.5f, descFast = 7.5f, descBrake = 3.8f, descStall = 6.5f, brakeHold = 2.5f;
  float canopyTurn = 38, canopyTurnTau = 0.6f, canopyTauH = 1.3f, canopyTauV = 0.9f, lineLen = 6, swingDamp = 0.7f;
  float flareWindow = 7, flareTime = 1.1f, flareGlide = 2, flareDesc = 1.2f, flareTau = 0.3f;
  float landSoft = 3.0f, landSoftTime = 0.35f, landHardTime = 0.9f, landRunOut = 0.6f;
  float fovPerMps = 0.09f, fovMax = 7, fovTau = 0.6f, buffet = 0.25f, joltDrop = 0.35f, joltFreq = 9, joltDamp = 6, swingPitch = 0.5f, landDip = 0.25f;
  float windMin = 12, windFull = 70, windVol = 0.9f, softVol = 0.45f, planeVol = 0.7f, openVol = 1.0f;
  float botJumpFrom = 0.08f, botJumpTo = 0.92f, botLandSpread = 110;
  int vehicles = 8; float vehicleSpacing = 35;   // BR7: parked civilian cars made drivable
  RoyaleStormCfg storm;
};
Cfg gCfg;
void stormCfg(const json& s);
void loadCfg() {
  gCfg = Cfg{};
  json j; try { j = json::parse(readTextFile(contentDir() / "settings" / "royale.json")); } catch (...) { logf("royale: content/settings/royale.json missing or bad, defaults"); return; }
  auto f = [&](const char* sec, const char* k, float& v) { if (j.contains(sec) && j[sec].contains(k)) v = j[sec][k].get<float>(); };
  f("plane", "altitude_m", gCfg.planeAlt); f("plane", "speed_mps", gCfg.planeSpeed); f("plane", "overrun_m", gCfg.overrun); f("plane", "endJitter", gCfg.jitter);
  Cfg& c = gCfg;
  f("exit", "time_s", c.exitTime); f("exit", "dropBelow_m", c.exitDrop);
  f("freefall", "gravity_mps2", c.gravity); f("freefall", "terminalFlat_mps", c.termFlat); f("freefall", "terminalDive_mps", c.termDive); f("freefall", "terminalSpread_mps", c.termSpread);
  f("freefall", "diveLookPitch_deg", c.diveLook); f("freefall", "attitudeIn_per_s", c.attIn); f("freefall", "attitudeOut_per_s", c.attOut);
  f("freefall", "trackFlat_mps", c.trackFlat); f("freefall", "trackDive_mps", c.trackDive); f("freefall", "driftFlat_mps", c.driftFlat); f("freefall", "driftDive_mps", c.driftDive);
  f("freefall", "slide_mps", c.slide); f("freefall", "horizontalTau_s", c.tauH); f("freefall", "spreadTau_s", c.tauSpread); f("freefall", "turnRate_dps", c.turnRate);
  f("deploy", "autoOpen_m", c.autoOpen); f("deploy", "time_s", c.deployTime); f("deploy", "unfold_s", c.unfold); f("deploy", "maxDecel_mps2", c.maxDecel); f("deploy", "swingKick_radps", c.swingKick);
  f("canopy", "glide_mps", c.glide); f("canopy", "glideFast_mps", c.glideFast); f("canopy", "glideBrake_mps", c.glideBrake);
  f("canopy", "descent_mps", c.desc); f("canopy", "descentFast_mps", c.descFast); f("canopy", "descentBrake_mps", c.descBrake); f("canopy", "descentStall_mps", c.descStall); f("canopy", "brakeHold_s", c.brakeHold);
  f("canopy", "turnRate_dps", c.canopyTurn); f("canopy", "turnTau_s", c.canopyTurnTau); f("canopy", "horizontalTau_s", c.canopyTauH); f("canopy", "verticalTau_s", c.canopyTauV);
  f("canopy", "lineLength_m", c.lineLen); f("canopy", "swingDamping_per_s", c.swingDamp);
  f("flare", "window_m", c.flareWindow); f("flare", "time_s", c.flareTime); f("flare", "glide_mps", c.flareGlide); f("flare", "descent_mps", c.flareDesc); f("flare", "tau_s", c.flareTau);
  f("landing", "softSpeed_mps", c.landSoft); f("landing", "softTime_s", c.landSoftTime); f("landing", "hardTime_s", c.landHardTime); f("landing", "runOut", c.landRunOut);
  f("camera", "fovPerMps", c.fovPerMps); f("camera", "fovMax_deg", c.fovMax); f("camera", "fovTau_s", c.fovTau); f("camera", "buffet_deg", c.buffet);
  f("camera", "joltDrop_m", c.joltDrop); f("camera", "joltFreq_hz", c.joltFreq); f("camera", "joltDamping", c.joltDamp); f("camera", "swingPitch", c.swingPitch); f("camera", "landDip_m", c.landDip);
  f("sound", "windMin_mps", c.windMin); f("sound", "windFull_mps", c.windFull); f("sound", "windVolume", c.windVol); f("sound", "softWindVolume", c.softVol); f("sound", "planeVolume", c.planeVol); f("sound", "openVolume", c.openVol);
  f("bots", "jumpFrom", gCfg.botJumpFrom); f("bots", "jumpTo", gCfg.botJumpTo); f("bots", "landSpread_m", gCfg.botLandSpread);
  if (j.contains("vehicles")) { gCfg.vehicles = jkey(j, "vehicles").value("count", gCfg.vehicles); gCfg.vehicleSpacing = jkey(j, "vehicles").value("spacing_m", gCfg.vehicleSpacing); }
  if (j.contains("storm")) stormCfg(j["storm"]);
}
void stormCfg(const json& s) {   // royale.json "storm", then a map's own (NT3: extra.royale.storm, small maps)
  {
    auto arr = [&](const char* k, float* v, int n) { if (s.contains(k) && s[k].is_array()) for (int i = 0; i < n && i < (int)s[k].size(); ++i) v[i] = s[k][i].get<float>(); };
    arr("wait_s", gCfg.storm.wait, 4); arr("shrink_s", gCfg.storm.shrink, 4); arr("dps", gCfg.storm.dps, 4); arr("radiusFrac", gCfg.storm.radiusFrac, 3);
    gCfg.storm.finalRadius = s.value("finalRadius_m", gCfg.storm.finalRadius);
    gCfg.storm.driftPause = s.value("driftPause_s", gCfg.storm.driftPause); gCfg.storm.driftMove = s.value("driftMove_s", gCfg.storm.driftMove);
    gCfg.storm.driftMin = s.value("driftMin_m", gCfg.storm.driftMin); gCfg.storm.driftMax = s.value("driftMax_m", gCfg.storm.driftMax);
  }
}

struct Air {
  int st = RA_GROUND; bool boarded = false; vec3 p{0}, v{0}; float jumpAt = 0; vec2 aim{0}; float stormAcc = 0;
  // BR13 flight state: t = s in the current state; heading = body / canopy facing (deg, the game's yaw); att = body
  // attitude 0 flat .. 1 head down; turn = canopy turn rate; swing / roll = pendulum angles under the canopy (rad, +
  // forward / into the turn) with their rates; brakeT = s of brakes held; flareT = flare time left; landDur = landing
  float t = 0, heading = 0, att = 0, attTarget = 0, turn = 0, swing = 0, swingV = 0, roll = 0, rollV = 0, brakeT = 0, flareT = 0, landDur = 0;
  bool flared = false, opened = false; vec2 runOut{0};
};
struct State {
  bool on = false;
  float time = 0;                 // s since the match start (plane and storm clock)
  vec3 a{0}, dir{0, 0, 1}; float len = 0, tIn = 0, tOut = 0;   // bomber line: start, unit direction, length; inside the area
  RoyaleStorm storm;
  std::vector<Air> air;
  bool jumpEdge = false;          // local player pressed Space since the last step
  vec2 move{0};                   // BR5: the local player's WASD while airborne (y forward, x right)
  int flight = 0;                 // BR5: the local flight posture for the HUD: 0 belly, 1 glide, 2 dive, 3 brake
  bool freeLook = false;          // BR13: Left Alt held: the mouse looks around without steering
  bool brakeHeld = false;         // BR13: S or Space under the canopy (flare near the ground)
  float fovAdd = 0, joltY = 0, joltV = 0, buffetT = 0;   // BR13 camera: speed FOV, the opening jolt (spring), buffet clock
  int windLoop = -1, softLoop = -1, planeLoop = -1;     // BR13 sound loops
  uint32_t seed = 1;
  bool indoor = false;            // BR11: Tunnels / Backrooms: everyone starts on the ground inside, no bomber
  struct District { std::string name; vec3 pos{0}; };
  std::vector<District> districts;   // BR1b: the map's named landing districts (scenario.districts, e.g. Newtown)
} S;

// district names over a map / the drop view: dark backing, centred on the point
void label(ImDrawList* dl, ImVec2 at, const std::string& t, float size, ImU32 col) {
  ImFont* f = ImGui::GetFont();
  ImVec2 ts = f->CalcTextSizeA(size, 1e9f, 0, t.c_str());
  ImVec2 p(at.x - ts.x * 0.5f, at.y - ts.y * 0.5f);
  dl->AddRectFilled(ImVec2(p.x - 4, p.y - 2), ImVec2(p.x + ts.x + 4, p.y + ts.y + 2), IM_COL32(10, 12, 18, 150), 3);
  dl->AddText(f, size, p, col, t.c_str());
}

vec3 planeAt(float t) { return S.a + S.dir * (gCfg.planeSpeed * t); }
bool planeFlying() { return !S.indoor && S.time * gCfg.planeSpeed < S.len; }
float groundBelow(App& app, vec3 p) {
  float g = app.terrain.height(p.x, p.z);
  Physics::Hit h;
  if (Physics::castRayEx(p + vec3(0, 0.5f, 0), vec3(0, -1, 0), 800.0f, h)) g = std::max(g, h.pos.y);   // roofs count
  return std::max(g, app.terrain.waterLevel);
}
// frame-rate independent smoothing: x moves toward `to` with time constant tau (the same result at any step size)
float ease(float dt, float tau) { return tau <= 1e-4f ? 1.0f : 1.0f - std::exp(-dt / tau); }
float angDiff(float to, float from) { float d = std::fmod(to - from + 540.0f, 360.0f) - 180.0f; return d; }
vec3 headDir(float yawDeg) { float r = radians(yawDeg); return vec3(-std::sin(r), 0, -std::cos(r)); }   // the game's yaw: 0 = -z

// BR13 sounds, synthesized (no wind / cloth / aircraft recordings in content/audio): a bright rushing wind for free
// fall, a soft wind with fabric flutter under the canopy, a two-engine bomber drone, and the canopy opening crack
void synthRoyaleSounds(Audio& au) {
  static bool done = false; if (done) return; done = true;
  const int SR = 44100;
  std::mt19937 g(1313); std::uniform_real_distribution<float> U(-1, 1);
  auto norm = [](std::vector<float>& b, float peak) { float m = 1e-6f; for (float v : b) m = std::max(m, std::fabs(v)); for (float& v : b) v *= peak / m; };
  auto loopFix = [&](std::vector<float>& b) { size_t xf = SR / 10; for (size_t i = 0; i < xf; ++i) { float a = (float)i / xf; b[i] = b[i] * a + b[b.size() - xf + i] * (1 - a); } b.resize(b.size() - xf); };
  { std::vector<float> b((size_t)(4.1f * SR)); float l1 = 0, l2 = 0, h = 0;   // rush: broadband, buffeting
    for (size_t i = 0; i < b.size(); ++i) { float t = (float)i / SR, n = U(g); l1 += (n - l1) * 0.35f; l2 += (l1 - l2) * 0.35f; h += (n - h) * 0.08f;
      float buf = 0.75f + 0.25f * std::sin(t * 6.2832f * 0.9f) * std::sin(t * 6.2832f * 2.3f + 1.0f); b[i] = (l2 * 0.8f + (n - h) * 0.25f) * buf; }
    loopFix(b); norm(b, 0.8f); au.addSound("br_wind_rush", b); }
  { std::vector<float> b((size_t)(5.1f * SR)); float l1 = 0, l2 = 0, fl = 0;   // soft wind + fabric flutter
    for (size_t i = 0; i < b.size(); ++i) { float t = (float)i / SR, n = U(g); l1 += (n - l1) * 0.04f; l2 += (l1 - l2) * 0.04f;
      float flap = std::pow(0.5f + 0.5f * std::sin(t * 6.2832f * (7.0f + 2.0f * std::sin(t * 1.3f))), 8.0f); fl += (U(g) * flap - fl) * 0.3f;
      b[i] = l2 * 3.0f + fl * 0.35f; }
    loopFix(b); norm(b, 0.6f); au.addSound("br_wind_soft", b); }
  { std::vector<float> b((size_t)(2.05f * SR)); float lp = 0;   // bomber: two radial engines beating
    for (size_t i = 0; i < b.size(); ++i) { float t = (float)i / SR, s = 0;
      for (int k = 1; k <= 6; ++k) s += (std::sin(6.2832f * 48.0f * k * t) + std::sin(6.2832f * 50.0f * k * t + 0.4f)) / (float)k;
      lp += (U(g) - lp) * 0.05f; b[i] = s * 0.5f + lp * 1.5f; }
    loopFix(b); norm(b, 0.7f); au.addSound("br_plane_drone", b); }
  { std::vector<float> b((size_t)(1.3f * SR)); float lp = 0, lp2 = 0;   // opening: unfolding swish, the crack of the canopy catching air, the harness thump
    for (size_t i = 0; i < b.size(); ++i) { float t = (float)i / SR, n = U(g);
      float swish = t < 0.45f ? std::sin(t / 0.45f * 1.5708f) : std::exp(-(t - 0.45f) * 12.0f);
      lp += (n - lp) * (0.05f + 0.3f * swish); lp2 += (n - lp2) * 0.6f;
      float crack = t > 0.45f ? std::exp(-(t - 0.45f) * 40.0f) : 0, thump = t > 0.47f ? std::exp(-(t - 0.47f) * 14.0f) * std::sin(6.2832f * 70.0f * (t - 0.47f)) : 0;
      float flutter = t > 0.5f ? std::exp(-(t - 0.5f) * 4.0f) * std::pow(0.5f + 0.5f * std::sin(t * 6.2832f * 11.0f), 6.0f) * n : 0;
      b[i] = lp * swish * 0.6f + lp2 * crack * 1.2f + thump * 0.9f + flutter * 0.4f; }
    norm(b, 0.9f); au.addSound("br_chute_open", b); }
}
}

bool royaleActive() { return S.on && gGame && gGame->active && gGame->match.mode() == GameMode::BattleRoyale; }
int royaleAirState(int slot) {
  if (!royaleActive() || slot < 0 || slot >= (int)S.air.size()) return RA_GROUND;
  if (S.indoor) return RA_GROUND;   // BR11: nobody flies in a tunnel
  if (!S.air[slot].boarded && slot < (int)gGame->sold.size() && gGame->sold[slot].alive) return RA_ABOARD;   // BR7: spawned, boards on the next step
  return S.air[slot].st;
}

void royaleBegin(App& app, Game& G) {
  S = State{};
  if (G.match.mode() != GameMode::BattleRoyale || G.isClient) return;
  loadCfg();
  if (app.doc.extra.is_object() && app.doc.extra.contains("royale") && app.doc.extra["royale"].is_object()) {   // NT3: a small map scales its match
    const json& r = app.doc.extra["royale"];
    if (r.contains("storm")) stormCfg(r["storm"]);
    gCfg.vehicles = r.value("vehicles", gCfg.vehicles); gCfg.vehicleSpacing = r.value("vehicleSpacing_m", gCfg.vehicleSpacing); gCfg.botLandSpread = r.value("landSpread_m", gCfg.botLandSpread);
  }
  S.on = true;
  S.indoor = lootIndoorMap(app);
  S.seed = (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count() | 1u;
  std::mt19937 rng(S.seed);
  std::uniform_real_distribution<float> u(0.0f, 1.0f);
  float hx = app.terrain.buildHalfX, hz = app.terrain.buildHalfZ;
  // bomber: a random corner to the opposite one, each end moved a little along the edges, overrunning the area
  int k = (int)(u(rng) * 4) & 3; vec2 sgn((k & 1) ? 1.0f : -1.0f, (k & 2) ? 1.0f : -1.0f);
  auto jit = [&](float h) { return (u(rng) * 2 - 1) * gCfg.jitter * h; };
  vec2 p0(sgn.x * hx + jit(hx), sgn.y * hz + jit(hz)), p1(-sgn.x * hx + jit(hx), -sgn.y * hz + jit(hz));
  p0 = glm::clamp(p0, vec2(-hx, -hz), vec2(hx, hz)); p1 = glm::clamp(p1, vec2(-hx, -hz), vec2(hx, hz));
  vec2 d = glm::normalize(p1 - p0);
  float top = -1e9f; for (int i = 0; i <= 64; ++i) { vec2 q = glm::mix(p0, p1, i / 64.0f); top = std::max(top, app.terrain.height(q.x, q.y)); }
  float y = std::max(top, app.terrain.waterLevel) + gCfg.planeAlt;
  S.dir = vec3(d.x, 0, d.y);
  S.a = vec3(p0.x, y, p0.y) - S.dir * gCfg.overrun;
  S.len = glm::length(p1 - p0) + 2 * gCfg.overrun;
  S.tIn = gCfg.overrun / gCfg.planeSpeed; S.tOut = (S.len - gCfg.overrun) / gCfg.planeSpeed;
  S.storm.cfg = gCfg.storm;
  S.storm.build(vec2(hx, hz), S.seed * 2654435761u, [](vec2 p) {   // BR7: a drift centre needs navmesh within 8 m (not a lake, roof or wall)
    NavRef r; vec3 on;   // BR11: underground the floor can be far below the surface: search the whole height
    return gGame && gGame->nav.closest(vec3(p.x, gGame->app.terrain.height(p.x, p.y), p.y), S.indoor ? vec3(8, 80, 8) : vec3(8), r, on); });
  S.air.assign(G.sold.size(), Air{});
  for (size_t i = 0; i < S.air.size(); ++i) {   // bots: a random point along the line, and a landing aim near below it
    Air& a = S.air[i];
    a.jumpAt = S.tIn + (gCfg.botJumpFrom + u(rng) * (gCfg.botJumpTo - gCfg.botJumpFrom)) * (S.tOut - S.tIn);
    vec3 q = planeAt(a.jumpAt); float ang = u(rng) * 6.2831853f, rr = gCfg.botLandSpread * std::sqrt(u(rng));
    a.aim = glm::clamp(vec2(q.x, q.z) + vec2(std::cos(ang), std::sin(ang)) * rr, vec2(-hx, -hz) * 0.92f, vec2(hx, hz) * 0.92f);
    { NavRef nr; vec3 on; vec3 p(a.aim.x, app.terrain.height(a.aim.x, a.aim.y), a.aim.y);   // BR7: aim at walkable ground (not the lake or a roof)
      (void)nr; for (float rad : {10.0f, 30.0f, 60.0f}) if (royaleSnap(G, p, rad, on)) { a.aim = vec2(on.x, on.z); break; } }
  }
  G.deployReady = true;   // no deploy screen: everyone boards the bomber
  {   // BR7: a few of the map's parked civilian cars become drivable (E; the tank framework's car mode). Bots do not drive.
    static const char* kinds[] = {"mod_sedan", "mod_sedan_red", "mod_suv", "mod_pickup", "mod_hatchback", "mod_delivery_van"};
    std::vector<const Instance*> cand;
    for (auto& i : app.doc.instances) {
      bool car = false; for (const char* k : kinds) car |= i.assetId == k;
      if (car && std::fabs(i.pos.y - app.terrain.height(i.pos.x, i.pos.z)) < 0.6f && app.terrain.inBuildArea(i.pos.x, i.pos.z)) cand.push_back(&i);   // on open ground (not a garage deck)
    }
    std::shuffle(cand.begin(), cand.end(), rng);
    std::vector<vec3> took;
    for (const Instance* i : cand) {
      if ((int)took.size() >= gCfg.vehicles) break;
      bool near = false; for (vec3 p : took) near |= glm::length(vec2(p.x - i->pos.x, p.z - i->pos.z)) < gCfg.vehicleSpacing;
      if (!near && app.tanks.adoptCar(app, i->id)) took.push_back(i->pos);
    }
    logf("royale: %zu of %zu parked cars drivable", took.size(), cand.size());
  }
  {   // BR5: everyone in the player's body (the BR marine when its bake is present), all of them soldiers
    int body = brMarineModel(G);
    if (body < 0) { Soldier* L = G.local(); body = L ? L->model : G.sold.empty() ? -1 : G.sold[0].model; }
    for (auto& s : G.sold) {
      if (body >= 0) s.model = body;
      s.zombie = false; s.etype = 0; s.evariant = 0; s.eScale = 1; s.eLean = 0; s.eBody = 1; s.eFallback = false;
    }
  }
  // BR1b: named districts from the map file (scenario.districts: name, pos [x, y, z], radius); absent on most maps
  if (app.doc.extra.is_object() && app.doc.extra.contains("scenario") && app.doc.extra["scenario"].is_object())
    for (auto& d : jkey(app.doc.extra, "scenario").value("districts", json::array()))
      if (d.is_object() && d.contains("pos") && d["pos"].is_array() && d["pos"].size() == 3)
        S.districts.push_back({d.value("name", std::string("?")), vec3(d["pos"][0].get<float>(), d["pos"][1].get<float>(), d["pos"][2].get<float>())});
  logf("royale: %zu aboard, bomber (%.0f %.0f) -> heading %.0f deg at %.0f m, %.0f m line; storm circles r %.0f / %.0f / %.0f / %.0f m",
       S.air.size(), S.a.x, S.a.z, degrees(std::atan2(S.dir.x, S.dir.z)), y, S.len, S.storm.circle[1].r, S.storm.circle[2].r, S.storm.circle[3].r, S.storm.circle[4].r);
}

bool royaleSnap(const Game& G, vec3 p, float r, vec3& out) {
  dtQueryFilter f = G.nav.defaultFilter(); f.setExcludeFlags(f.getExcludeFlags() | NF_TRAP);
  NavRef ref; return G.nav.closest(p, vec3(r, std::max(r, 2.0f), r), ref, out, &f);
}

void royaleEnd(App& app) {
  app.tanks.releaseAdopted(app); S.on = false;
  for (int* h : {&S.windLoop, &S.softLoop, &S.planeLoop}) if (*h >= 0) { app.audio.loopStop(*h); *h = -1; }   // BR13
}   // BR7: the parked cars back where they stood

bool royaleBareHands(App& app, Game& G, Soldier& s) {
  if (G.match.mode() != GameMode::BattleRoyale) return false;
  int knife = app.weaponIndex("knife_bayonet");
  s.cls = -1;   // no class kit (abilities, packs) in a battle royale: loot is the kit
  if (s.localPlayer) {
    app.resetWeapons();
    std::vector<int> pool; if (knife >= 0) pool.push_back(knife);
    app.carried = pool; if (knife >= 0) app.selectWeapon(knife);
    s.weapon = app.activeWeapon;
  } else { s.weapon = knife >= 0 ? knife : 0; s.carried.clear(); if (knife >= 0) s.carried.push_back(knife); }
  s.ws = WeaponState{}; s.wsBy.clear();
  s.frags = s.smokes = 0; s.reload = 0; s.throwT = -1; s.throwReleased = false;
  if (s.localPlayer) G.localGrenadeCounts(s, true);
  return true;
}

void royaleStep(App& app, Game& G, float dt) {
  if (!royaleActive()) return;
  S.time += dt;
  if (S.air.size() != G.sold.size()) S.air.resize(G.sold.size());
  vec3 P = planeAt(S.time);
  bool jump = S.jumpEdge; S.jumpEdge = false;
  float hx = app.terrain.buildHalfX, hz = app.terrain.buildHalfZ;
  for (auto& s : G.sold) {
    if (!s.alive) continue;
    Air& a = S.air[s.slot];
    if (!a.boarded && S.indoor) {   // BR11: already standing where the spawn put it (inside, far from the others)
      a.boarded = true; a.st = RA_GROUND;
      if (!s.localPlayer && !s.human) lootArmBot(G, s, 2);   // as on landing
      continue;
    }
    if (!a.boarded) {   // first step alive: aboard while the bomber is still over the area, else straight into free fall
      a.boarded = true;
      if (S.time < S.tOut) { a.st = RA_ABOARD; a.p = P; }
      else { a.st = RA_FREEFALL; a.p = vec3(s.feet.x, P.y, s.feet.z); a.aim = vec2(s.feet.x, s.feet.z); a.heading = s.yaw; }
      a.v = vec3(0);
      if (s.agent >= 0) { Physics::destroyAgent(s.agent); s.agent = -1; }   // no capsule until it lands
    }
    if (a.st == RA_GROUND) continue;
    bool local = s.localPlayer;
    if (a.st == RA_LANDING) { a.t += dt; if (a.t >= a.landDur) a.st = RA_GROUND; continue; }   // on its feet: royaleInput eases control back
    if (a.st == RA_ABOARD) {
      a.p = P - vec3(0, 1.0f, 0);
      bool over = S.time >= S.tIn;
      if ((!local && !s.human && S.time >= a.jumpAt) || (local && jump && over) || S.time >= S.tOut) {
        // exit: out of the door with the aircraft's own velocity, still level; drag and gravity take over from here
        a.st = RA_EXIT; a.t = 0; a.p = P - vec3(0, gCfg.exitDrop, 0) - S.dir * 3.0f; a.v = S.dir * gCfg.planeSpeed;
        a.heading = degrees(std::atan2(-S.dir.x, -S.dir.z)); a.att = a.attTarget = 0; a.opened = a.flared = false;
        if (local) app.toast("Out! Mouse: look and steer (look down to dive)   W track   S spread   A / D slide   Alt: free look   SPACE: parachute (opens itself at " + std::to_string((int)gCfg.autoOpen) + " m)", 7);
      } else if (local && jump) app.toast("Wait until the bomber is over the area.", 2);
    } else {
      const Cfg& c = gCfg;
      a.t += dt;
      float agl = a.p.y - groundBelow(app, a.p);
      // controls: forward / side (-1..1), the look's dive target, the heading to steer for, brakes
      float fwd = 0, side = 0, brake = 0; float wantHeading = a.heading;
      if (local) {
        fwd = S.move.y > 0.1f ? 1.0f : 0.0f; brake = S.move.y < -0.1f || (a.st == RA_CHUTE && S.brakeHeld) ? 1.0f : 0.0f; side = std::clamp(S.move.x, -1.0f, 1.0f);
        if (!S.freeLook) { a.attTarget = std::clamp(-app.eyePitch / c.diveLook, 0.0f, 1.0f); wantHeading = app.eyeYaw; }   // Alt: look without steering
        else wantHeading = a.heading;
      } else {   // bots: track flat toward the landing aim while far, dive when close, turn the canopy toward it, flare low
        vec2 to = a.aim - vec2(a.p.x, a.p.z); float d = glm::length(to);
        if (d > 1) wantHeading = degrees(std::atan2(-to.x, -to.y));
        fwd = d > 25 ? 1.0f : 0.0f; a.attTarget = d > 160 ? 0.0f : d > 40 ? 0.7f : 1.0f;
        if (a.st == RA_CHUTE) { side = std::clamp(angDiff(wantHeading, a.heading) / 30.0f, -1.0f, 1.0f); brake = agl < c.flareWindow * 0.6f ? 1.0f : 0.0f; fwd = d > 40 ? 1.0f : 0.0f; }
      }
      vec3 hv(a.v.x, 0, a.v.z); float vy = a.v.y; vec3 hvBefore = hv;
      if (a.st == RA_EXIT || a.st == RA_FREEFALL || (a.st == RA_DEPLOY && a.t < c.unfold)) {
        // free fall: gravity against quadratic drag toward the attitude's terminal speed; the body turns toward the
        // steering heading at a limited rate, and the horizontal velocity eases toward the body's drive, so a change of
        // direction curves the path. Pulling out of a dive (attitude down) is slower than tipping into one.
        bool exiting = a.st == RA_EXIT;
        float tgt = exiting || a.st == RA_DEPLOY ? 0.0f : brake > 0 ? 0.0f : a.attTarget;
        a.att += (tgt - a.att) * ease(dt, 1.0f / (tgt > a.att ? c.attIn : c.attOut));
        if (!exiting) a.heading += std::clamp(angDiff(wantHeading, a.heading), -c.turnRate * dt, c.turnRate * dt);
        float term = brake > 0 ? c.termSpread : glm::mix(c.termFlat, c.termDive, std::pow(a.att, 1.2f));
        vy += (-c.gravity + c.gravity * (vy * vy) / (term * term) * (vy < 0 ? 1.0f : -1.0f)) * dt;
        vec3 f = headDir(a.heading), r(-f.z, 0, f.x);
        vec3 drive = exiting ? vec3(0) : brake > 0 ? vec3(0)
                   : f * (fwd > 0 ? glm::mix(c.trackFlat, c.trackDive, a.att) : glm::mix(c.driftFlat, c.driftDive, a.att)) + r * (side * c.slide);
        hv = drive + (hv - drive) * (1.0f - ease(dt, brake > 0 ? c.tauSpread : c.tauH));
        S.flight = brake > 0 ? 3 : a.att > 0.5f ? 2 : fwd > 0 ? 1 : 0;
        if (a.st == RA_EXIT && a.t >= c.exitTime) { a.st = RA_FREEFALL; a.t = 0; }
        if (a.st == RA_FREEFALL && (agl <= c.autoOpen || (local && jump))) {   // pull: the canopy starts to unfold (auto at autoOpen as a safety)
          a.st = RA_DEPLOY; a.t = 0; a.swing = 0; a.swingV = 0; a.roll = a.rollV = 0; a.turn = 0;
          if (local) app.toast("Canopy opening...", 1.5);
        }
      } else if (a.st == RA_DEPLOY || a.st == RA_CHUTE) {
        // the canopy: a ram-air glide along its heading; A / D turn it (it leads, the body swings after), W runs faster
        // and a little steeper, S brakes (slower, a softer descent that fades: no hovering); near the ground the brakes flare
        bool deploying = a.st == RA_DEPLOY;
        if (deploying && !a.opened) { a.opened = true; a.swingV += c.swingKick;   // the canopy catches the air: the body swings forward under it; crack, harness tug, camera jolt
          if (local) { app.audio.play("br_chute_open", app.eyePos(), c.openVol); S.joltV = -c.joltDrop * c.joltFreq * 2.0f; } }
        a.turn += (side * c.canopyTurn - a.turn) * ease(dt, c.canopyTurnTau);
        a.heading += a.turn * dt;
        a.brakeT = brake > 0 ? a.brakeT + dt : std::max(0.0f, a.brakeT - dt * 2);
        if (!deploying && brake > 0 && !a.flared && agl < c.flareWindow) { a.flared = true; a.flareT = c.flareTime; }
        a.flareT = std::max(0.0f, a.flareT - dt);
        float soft = std::exp(-a.brakeT / std::max(0.1f, c.brakeHold));
        float speed = brake > 0 ? c.glideBrake : fwd > 0 ? c.glideFast : c.glide;
        float desc = brake > 0 ? glm::mix(c.descStall, c.descBrake, soft) : fwd > 0 ? c.descFast : c.desc;
        float tauH = c.canopyTauH, tauV = c.canopyTauV;
        if (a.flareT > 0) { speed = c.flareGlide; desc = c.flareDesc; tauH = tauV = c.flareTau; }
        vec3 want = headDir(a.heading) * speed;
        if (deploying) {   // inflating: strong deceleration over a short, felt interval, capped (no snap to the new speed)
          vec3 v3(hv.x, vy, hv.z), w3(want.x, -desc, want.z), d3 = w3 - v3; float l = glm::length(d3), m = c.maxDecel * dt;
          v3 = l <= m ? w3 : v3 + d3 / l * m; hv = vec3(v3.x, 0, v3.z); vy = v3.y;
          if (a.t >= c.deployTime) { a.st = RA_CHUTE; a.t = 0;
            if (local) app.toast("Canopy open.   A / D turn   W faster   S brake (flare near the ground)   mouse: free look", 5); }
        } else {
          hv = want + (hv - want) * (1.0f - ease(dt, tauH));
          vy += (-desc - vy) * ease(dt, tauV);
        }
        S.flight = a.flareT > 0 || brake > 0 ? 3 : fwd > 0 ? 1 : 0;
        // pendulum under the canopy: the body swings forward when the canopy slows (pitch) and outward-then-in on turns (roll)
        float w2 = c.gravity / std::max(1.0f, c.lineLen);
        vec3 acc = (hv - hvBefore) / std::max(dt, 1e-4f); float along = glm::dot(acc, headDir(a.heading));
        a.swingV += (-w2 * std::sin(a.swing) - c.swingDamp * a.swingV - along / c.lineLen) * dt; a.swing += a.swingV * dt;
        float lateral = radians(a.turn) * glm::length(hv);
        a.rollV += (-w2 * std::sin(a.roll) - c.swingDamp * a.rollV + lateral / c.lineLen) * dt; a.roll += a.rollV * dt;
        a.swing = std::clamp(a.swing, -0.7f, 0.7f); a.roll = std::clamp(a.roll, -0.6f, 0.6f);
      }
      a.v = vec3(hv.x, vy, hv.z);
      vec3 np = a.p + a.v * dt;
      np.x = std::clamp(np.x, -hx * 1.05f, hx * 1.05f); np.z = std::clamp(np.z, -hz * 1.05f, hz * 1.05f);
      float g = groundBelow(app, np);
      if (np.y <= g + 0.02f) {   // touchdown: no fall damage (a placement, not a fall); a well-timed flare lands soft
        vec3 feet(np.x, g, np.z);
        bool canopy = a.st == RA_CHUTE || a.st == RA_DEPLOY, soft = canopy && -a.v.y <= c.landSoft, hard = !canopy;   // a flare lands soft; no canopy lands hard
        if (!local) { NavRef nr; vec3 on;   // BR7: a bot that came down on a roof, in water or in kit stands on the nearest navmesh
          if (!royaleSnap(G, feet, 1.5f, on) || std::fabs(on.y - feet.y) > 1.0f) for (float rad : {10.0f, 30.0f, 60.0f}) if (royaleSnap(G, feet, rad, on)) { feet = on; break; } (void)nr; }
        s.feet = feet; s.vel = vec3(0); s.speed = 0;
        if (local) {
          a.st = RA_LANDING; a.t = 0; a.landDur = soft ? c.landSoftTime : hard ? c.landHardTime : (c.landSoftTime + c.landHardTime) * 0.5f;
          vec3 f = headDir(app.eyeYaw), r(-f.z, 0, f.x);   // the run-out: what is left of the glide, as walking input
          a.runOut = vec2(glm::dot(hv, r), glm::dot(hv, f)) / std::max(1.0f, c.glide) * c.landRunOut;
          S.joltV = -(soft ? 0.5f : hard ? 2.0f : 1.0f) * c.landDip * c.joltFreq * 2.0f;
          Physics::setCharacterPos(feet);
          if (soft) app.toast("Soft landing", 1.2); else if (hard) app.toast("Hard landing", 1.5);
        } else {
          a.st = RA_GROUND;
          const EnemyType* et = s.zombie ? &enemyType(s.etype) : nullptr;
          s.agent = et && et->capsuleH > 0 ? Physics::createAgent(feet, et->capsuleH, et->capsuleR) : Physics::createAgent(feet);
          s.br.repathT = 0;
          if (!s.human) lootArmBot(G, s, 2);   // BR5: bots land with two random guns from the loot table (the player loots)
        }
        a.v = vec3(0);
        continue;
      }
      a.p = np;
    }
    s.feet = a.p; s.vel = a.v; s.speed = 0; s.posture = Physics::STAND;
    if (!local) s.yaw = a.st == RA_ABOARD ? degrees(std::atan2(-S.dir.x, -S.dir.z)) : a.heading;
    else Physics::setCharacterPos(a.p);   // the explore camera rides along
  }
  // BR13 local effects: the camera jolt spring, the speed FOV, the wind / aircraft sounds
  if (Soldier* L = G.local()) {
    const Cfg& c = gCfg; int st = L->alive && L->slot < (int)S.air.size() ? royaleAirState(L->slot) : RA_GROUND;
    const Air* a = st != RA_GROUND ? &S.air[L->slot] : nullptr;
    float k = 6.2832f * c.joltFreq; S.joltV += (-k * k * S.joltY - 2.0f * c.joltDamp * S.joltV) * dt; S.joltY += S.joltV * dt;   // damped spring
    float speed = a ? glm::length(a->v) : 0;
    bool falling = st == RA_EXIT || st == RA_FREEFALL;
    S.fovAdd += ((falling ? std::min(c.fovMax, std::max(0.0f, speed - 20) * c.fovPerMps) : 0) - S.fovAdd) * ease(dt, c.fovTau);
    S.buffetT += dt;
    synthRoyaleSounds(app.audio);
    vec3 ear = app.eyePos();
    float rush = 0, soft = 0;
    if (falling) rush = glm::smoothstep(c.windMin, c.windFull, speed) * c.windVol;
    else if (st == RA_DEPLOY) { float u = std::min(1.0f, a->t / std::max(0.1f, c.deployTime)); rush = (1 - u) * glm::smoothstep(c.windMin, c.windFull, speed) * c.windVol; soft = u * c.softVol; }
    else if (st == RA_CHUTE) soft = c.softVol * (0.6f + 0.4f * std::min(1.0f, speed / std::max(1.0f, c.glideFast)));
    auto loop = [&](int& h, const char* name, vec3 at, float vol, float pitch) {
      if (vol > 0.01f) { if (h < 0) h = app.audio.loopStart(name, at, vol, pitch); else app.audio.loopSet(h, at, vol, pitch); }
      else if (h >= 0) { app.audio.loopStop(h); h = -1; }
    };
    loop(S.windLoop, "br_wind_rush", ear, rush, 0.8f + speed / 150.0f);
    loop(S.softLoop, "br_wind_soft", ear, soft, 1.0f);
    loop(S.planeLoop, "br_plane_drone", planeAt(S.time), planeFlying() ? c.planeVol : 0, 1.0f);   // spatial: fades as the jumper falls away
  }
  // storm damage outside the current ring, once a second while outside (landed soldiers only); armor never applies
  RoyaleStorm::At at = S.storm.at(S.time);
  for (auto& s : G.sold) {   // BR5: a bot with an empty gun (nothing loaded, nothing spare) takes its other gun
    if (!s.alive || s.localPlayer || s.human || S.air[s.slot].st != RA_GROUND || s.weapon < 0 || s.reload > 0) continue;
    if (s.ws.loaded > 0 || !s.ws.containers.empty()) continue;
    for (auto& [w, ws] : s.wsBy) if (ws.loaded > 0 || !ws.containers.empty()) {
      int was = s.weapon; WeaponState old = s.ws; s.weapon = w; s.ws = ws; s.wsBy.erase(w); s.wsBy[was] = old; break;
    }
  }
  for (auto& s : G.sold) {
    if (!s.alive) continue;
    Air& a = S.air[s.slot];
    if (a.st != RA_GROUND || glm::length(vec2(s.feet.x, s.feet.z) - at.now.c) <= at.now.r) { a.stormAcc = 0; continue; }
    a.stormAcc += dt;
    while (a.stormAcc >= 1.0f && s.alive) { a.stormAcc -= 1.0f; G.damage(s.slot, at.dps, -1, vec3(0, 1, 0), "storm"); }
  }
}

// BR12: the storm wall as a plain translucent blue curtain in the scene: the effects pass draws it alpha-blended,
// depth-tested against the world (hidden behind buildings, terrain and props) and without depth writes. uv (0.5, 0.5):
// the fx shader's radial fade is 1 there, so the colour is flat; alpha fades from the ground line up the wall.
void royaleFx(App& app, std::vector<FxVert>& out) {
  if (!royaleActive()) return;
  RoyaleStorm::At at = S.storm.at(S.time);
  vec3 eye = app.eyePos();
  const int N = std::clamp((int)(at.now.r * 0.5f), 96, 720);   // ~2 m chords
  const float H = 70.0f; const vec2 uv(0.5f);
  const vec4 base(0.22f, 0.45f, 1.0f, 0.55f), top(0.22f, 0.45f, 1.0f, 0.12f);
  for (int k = 0; k < N; ++k) {
    float a0 = k * 6.2831853f / N, a1 = (k + 1) * 6.2831853f / N;
    vec2 p0 = at.now.c + vec2(std::cos(a0), std::sin(a0)) * at.now.r, p1 = at.now.c + vec2(std::cos(a1), std::sin(a1)) * at.now.r;
    if (glm::length(p0 - vec2(eye.x, eye.z)) > 1200) continue;
    // from below the ground (and below a tunnel the eye is in) up the wall
    float g0 = std::min(app.terrain.height(p0.x, p0.y), eye.y - 3) - 4, g1 = std::min(app.terrain.height(p1.x, p1.y), eye.y - 3) - 4;
    vec3 b0(p0.x, g0, p0.y), b1(p1.x, g1, p1.y), t0(p0.x, std::max(g0, eye.y - 3) + H, p0.y), t1(p1.x, std::max(g1, eye.y - 3) + H, p1.y);
    out.push_back({b0, base, uv}); out.push_back({b1, base, uv}); out.push_back({t1, top, uv});
    out.push_back({b0, base, uv}); out.push_back({t1, top, uv}); out.push_back({t0, top, uv});
  }
}
void royaleSetTime(float t) { S.time = t; }

RoyaleRing royaleRing(bool next) { if (!royaleActive()) return {}; RoyaleStorm::At a = S.storm.at(S.time); return next ? a.next : a.now; }

vec3 royaleSafePoint(const Soldier& s, int salt) {
  if (!royaleActive() || !gGame) return s.feet;
  RoyaleStorm::At a = S.storm.at(S.time);
  uint32_t h = (uint32_t)s.slot * 2654435761u ^ (uint32_t)(a.stage + 1 + a.leg * 7) * 40503u ^ (uint32_t)salt * 2246822519u;   // a new point per stage / drift leg / salt
  float ang = (h % 6283u) / 1000.0f, rr = a.next.r * 0.6f * std::sqrt(((h >> 13) % 1000u) / 1000.0f);
  vec2 p = a.next.c + vec2(std::cos(ang), std::sin(ang)) * rr;
  return vec3(p.x, gGame->app.terrain.height(p.x, p.y), p.y);
}

float royaleStormSoon(const Soldier& s) {
  if (!royaleActive()) return 1e9f;
  RoyaleStorm::At a = S.storm.at(S.time);
  vec2 p(s.feet.x, s.feet.z);
  if (glm::length(p - a.now.c) > a.now.r) return 0;
  if (glm::length(p - a.next.c) <= a.next.r) return 1e9f;   // safe through this stage
  return a.shrinking ? a.left : a.left + S.storm.cfg.shrink[std::min(a.stage, 3)];   // outside the next ring by the end of the shrink
}

void royaleInput(App& app, ExploreInput& in) {
  if (!royaleActive() || !gGame) return;
  Soldier* L = gGame->local();
  int st = L && L->alive ? royaleAirState(L->slot) : RA_GROUND;
  if (st == RA_GROUND) return;
  if (st == RA_LANDING) {   // BR13: back on the feet: the glide's run-out fades into the player's own walking, no firing yet
    const Air& a = S.air[L->slot]; float u = std::clamp(a.t / std::max(0.05f, a.landDur), 0.0f, 1.0f);
    in.move = in.move * u + a.runOut * (1 - u); in.jump = in.jumpHeld = in.sprint = in.prone = false; in.fire = in.aim = false;
    return;
  }
  if (in.jump) S.jumpEdge = true;
  S.freeLook = app.win && glfwGetKey(app.win, GLFW_KEY_LEFT_ALT) == GLFW_PRESS;
  S.brakeHeld = in.jumpHeld;   // Space under the canopy: brakes / flare, like S
  S.move = in.move;   // BR5: WASD steers the free fall / canopy
  in.move = vec2(0); in.jump = in.jumpHeld = in.crouch = in.sprint = in.prone = false;
  in.fire = in.aim = in.reload = in.interact = in.build = in.quickGrenade = false;
  (void)app;
}

bool royaleCamera(App& app, vec3& eye, vec3& fwd, float& fov) {
  if (!royaleActive() || !gGame) return false;
  Soldier* L = gGame->local();
  int st = L && L->alive ? royaleAirState(L->slot) : RA_GROUND;
  if (st == RA_ABOARD) {
    vec3 P = planeAt(S.time), f = App::forward(app.eyeYaw, app.eyePitch);
    eye = P - f * 32.0f + vec3(0, 6, 0); fwd = glm::normalize(P - eye); fov = 70;
    return true;
  }
  if (st == RA_GROUND) return false;
  // BR13: first person, with restrained effects: a slightly wider FOV with speed, buffeting in free fall, the opening
  // jolt, the swing under the canopy, the dip of the landing
  const Air& a = S.air[L->slot]; const Cfg& c = gCfg;
  float yawJ = 0, pitchJ = 0;
  if (st == RA_EXIT || st == RA_FREEFALL) {
    float k = glm::smoothstep(c.windMin, c.windFull, glm::length(a.v)) * c.buffet;
    yawJ = k * (std::sin(S.buffetT * 23.0f) * 0.6f + std::sin(S.buffetT * 37.0f + 1.1f) * 0.4f);
    pitchJ = k * (std::sin(S.buffetT * 29.0f + 0.5f) * 0.6f + std::sin(S.buffetT * 41.0f + 2.0f) * 0.4f);
  }
  if (st == RA_DEPLOY || st == RA_CHUTE) { pitchJ += degrees(a.swing) * c.swingPitch; eye.y -= c.lineLen * (1 - std::cos(a.swing)) * 0.3f; eye += headDir(a.heading + 90) * (c.lineLen * std::sin(a.roll) * 0.15f); }
  eye.y += S.joltY;
  fwd = App::forward(app.eyeYaw + yawJ, std::clamp(app.eyePitch + pitchJ, -89.0f, 89.0f));
  fov += S.fovAdd;
  return true;
}

void royaleDraw(App& app) {
  if (!royaleActive() || !gGame) return;
  auto drawAsset = [&](const char* id, const mat4& M) {
    const AssetDef* a = app.lib.find(id);
    if (!a || !a->model) return false;
    for (int k = 0; k < (int)a->model->parts.size(); ++k) app.R.worldModels.push_back({a->model.get(), k, M});
    return true;
  };
  auto box = [&](const mat4& M, vec3 c, vec3 s) { app.worldBoxDraws.push_back({&app.debrisMesh, M * glm::translate(mat4(1), c) * glm::scale(mat4(1), s)}); };
  if (planeFlying()) {   // the bomber (stand-in boxes until the br_bomber asset exists)
    mat4 M = glm::translate(mat4(1), planeAt(S.time)) * glm::rotate(mat4(1), std::atan2(S.dir.x, S.dir.z), vec3(0, 1, 0));
    if (!drawAsset("br_bomber", M)) {
      box(M, vec3(0), vec3(3.2f, 3.0f, 22)); box(M, vec3(0, 0.3f, 1.5f), vec3(32, 0.5f, 4.5f));
      box(M, vec3(0, 0.6f, -9.5f), vec3(10, 0.4f, 2.6f)); box(M, vec3(0, 2.6f, -9.8f), vec3(0.4f, 4.2f, 3.0f));
      for (float x : {-11.0f, -5.5f, 5.5f, 11.0f}) box(M, vec3(x, 0, 3.4f), vec3(1.4f, 1.4f, 3.2f));   // engines
    }
  }
  for (auto& s : gGame->sold) {   // canopies over everyone under a parachute (stand-in until br_parachute exists)
    int st = s.alive ? royaleAirState(s.slot) : RA_GROUND;
    if (st != RA_CHUTE && st != RA_DEPLOY) continue;
    const Air& a = S.air[s.slot];
    float inflate = st == RA_CHUTE ? 1.0f : std::clamp((a.t - gCfg.unfold * 0.5f) / std::max(0.1f, gCfg.deployTime - gCfg.unfold * 0.5f), 0.05f, 1.0f);   // BR13: unfolding
    // the canopy leads: it banks with the turn and sits over the body, which hangs back by the swing
    mat4 M = glm::translate(mat4(1), s.feet) * glm::rotate(mat4(1), radians(a.heading) + 3.14159265f, vec3(0, 1, 0))
           * glm::rotate(mat4(1), -a.roll, vec3(0, 0, 1)) * glm::rotate(mat4(1), a.swing, vec3(1, 0, 0)) * glm::scale(mat4(1), vec3(inflate, 0.4f + 0.6f * inflate, inflate));
    if (drawAsset("br_parachute", M)) continue;
    box(M, vec3(0, 6.8f, 0), vec3(6.5f, 0.5f, 3.6f)); box(M, vec3(-3.4f, 6.3f, 0), vec3(1.2f, 0.4f, 3.4f)); box(M, vec3(3.4f, 6.3f, 0), vec3(1.2f, 0.4f, 3.4f));
    for (float x : {-2.6f, 2.6f}) box(M * glm::translate(mat4(1), vec3(x * 0.5f, 4.2f, 0)) * glm::rotate(mat4(1), x > 0 ? -0.55f : 0.55f, vec3(0, 0, 1)), vec3(0), vec3(0.04f, 4.6f, 0.04f));
  }
}

void royaleHud(App& app, Game& G) {
  if (!royaleActive()) return;
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* bg = ImGui::GetBackgroundDrawList();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  RoyaleStorm::At at = S.storm.at(S.time);
  vec3 eye = app.eyePos();
  // the whole view tinted while the camera is outside the ring (the wall itself is 3D: royaleFx)
  mat4 vp = app.projM * app.viewM;
  auto proj = [&](vec3 w, ImVec2& o) { vec4 q = vp * vec4(w, 1); if (q.w < 0.1f) return false; o = ImVec2((q.x / q.w * 0.5f + 0.5f) * io.DisplaySize.x, (0.5f - q.y / q.w * 0.5f) * io.DisplaySize.y); return true; };
  if (glm::length(vec2(eye.x, eye.z) - at.now.c) > at.now.r) bg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(40, 80, 255, 62));
  // banner: alive, storm timer
  int alive = 0; for (auto& s : G.sold) alive += s.alive;
  int stillIn = G.match.stillIn();
  char b[200], tm[32];
  int sec = (int)std::ceil(at.left); std::snprintf(tm, sizeof tm, "%d:%02d", sec / 60, sec % 60);
  if (at.stage >= 4) std::snprintf(b, sizeof b, "BATTLE ROYALE   |   Alive %d / %zu   |   %s %s", std::max(alive, stillIn), G.sold.size(), at.shrinking ? "Circle moving" : "Circle moves in", tm);   // BR7: the final circle drifts
  else std::snprintf(b, sizeof b, "BATTLE ROYALE   |   Alive %d / %zu   |   %s %s   |   Circle %d / 4", std::max(alive, stillIn), G.sold.size(), at.shrinking ? "Closing" : "Storm closes in", tm, at.stage + 1);
  float cx = io.DisplaySize.x * 0.5f, y = 10;
  ImVec2 ts = ImGui::CalcTextSize(b);
  uiGlassPanel(dl, ImVec2(cx - ts.x * 0.5f - 12, y), ImVec2(cx + ts.x * 0.5f + 12, y + 28), std::max(8.0f, (float)5));
  dl->AddText(ImVec2(cx - ts.x * 0.5f, y + 6), at.shrinking ? IM_COL32(150, 190, 255, 255) : IM_COL32(235, 235, 220, 255), b);
  Soldier* L = G.local();
  if (L && L->alive && royaleAirborne(L->slot))   // BR1b: on the drop (aboard, falling, under the canopy) the districts are named in the view
    for (auto& d : S.districts) {
      ImVec2 sp; vec3 w = d.pos + vec3(0, 12, 0);
      if (!proj(w, sp) || sp.x < 0 || sp.y < 0 || sp.x > io.DisplaySize.x || sp.y > io.DisplaySize.y) continue;
      char t[96]; std::snprintf(t, sizeof t, "%s  %.0f m", d.name.c_str(), glm::length(w - eye));
      label(dl, sp, t, 17.0f, IM_COL32(255, 238, 190, 240));
    }
  auto centre = [&](const char* t, float yy, ImU32 col) { ImVec2 s2 = ImGui::CalcTextSize(t); uiGlassPanel(dl, ImVec2(cx - s2.x * 0.5f - 12, yy - 6), ImVec2(cx + s2.x * 0.5f + 12, yy + s2.y + 6), std::max(8.0f, (float)5)); dl->AddText(ImVec2(cx - s2.x * 0.5f, yy), col, t); };
  if (L && L->alive) {
    int st = royaleAirState(L->slot);
    if (st == RA_ABOARD) centre(S.time < S.tIn ? "Aboard the bomber - Space to jump once it is over the area" : "Aboard the bomber - SPACE to jump", io.DisplaySize.y * 0.78f, IM_COL32(255, 230, 150, 255));
    else if (st == RA_EXIT || st == RA_FREEFALL || st == RA_DEPLOY || st == RA_CHUTE) {   // BR5: altitude, fall rate, ground speed, posture
      const Air& a = S.air[L->slot];
      static const char* posture[2][4] = {{"BELLY", "TRACK", "DIVE", "SPREAD"}, {"GLIDE", "RUN", "GLIDE", "BRAKES"}};
      bool ch = st == RA_CHUTE || st == RA_DEPLOY; float gs = glm::length(vec2(a.v.x, a.v.z));
      char f[200]; std::snprintf(f, sizeof f, "%s %s     ALT %4.0f m     FALL %3.0f m/s     SPEED %3.0f km/h", ch ? "PARACHUTE" : "FREE FALL", posture[ch][S.flight & 3], L->feet.y - groundBelow(app, L->feet), -a.v.y, gs * 3.6f);
      centre(f, io.DisplaySize.y * 0.78f, ch ? IM_COL32(220, 230, 255, 255) : IM_COL32(255, 230, 150, 255));
      const char* keys = ch ? "A / D turn   W faster   S or SPACE brake (flare near the ground)   mouse: free look"
                            : "mouse: look and steer (look down to dive)   W track   S spread   A / D slide   ALT free look   SPACE parachute";
      centre(keys, io.DisplaySize.y * 0.78f + 30, IM_COL32(210, 205, 190, 230));
    }
    else if (glm::length(vec2(L->feet.x, L->feet.z) - at.now.c) > at.now.r) { char f[80]; std::snprintf(f, sizeof f, "IN THE STORM  -%.0f hp/s", at.dps); centre(f, io.DisplaySize.y * 0.70f, IM_COL32(150, 180, 255, 255)); }
  }
  if (L && L->slot < (int)G.match.slots.size() && G.match.slots[L->slot].place > 0) {
    char f[80]; int pl = G.match.slots[L->slot].place;
    if (pl == 1) std::snprintf(f, sizeof f, "WINNER  -  #1 of %zu", G.sold.size());
    else std::snprintf(f, sizeof f, "You placed #%d of %zu", pl, G.sold.size());
    centre(f, io.DisplaySize.y * 0.32f, pl == 1 ? IM_COL32(150, 240, 150, 255) : IM_COL32(255, 200, 170, 255));
  }
}

extern App* gApp;
void royaleMinimap(ImDrawList* dl, const MinimapView& v) {
  if (!royaleActive()) return;
  RoyaleStorm::At a = S.storm.at(S.time);
  auto ring = [&](const RoyaleRing& r, ImU32 col, float w) { vec2 c = v.toScreen(r.c); dl->AddCircle(ImVec2(c.x, c.y), r.r * v.scale, col, 96, w); };
  ring(a.now, IM_COL32(80, 130, 255, 230), 2.5f);
  if (a.stage < 4 || a.next.c != a.now.c) ring(a.next, IM_COL32(255, 255, 255, 230), 1.5f);   // BR7: also where the final circle drifts to
  if (planeFlying()) { vec3 P = planeAt(S.time); vec2 p = v.toScreen(vec2(P.x, P.z)), q = v.toScreen(vec2(P.x + S.dir.x * 40, P.z + S.dir.z * 40)); dl->AddLine(ImVec2(p.x, p.y), ImVec2(q.x, q.y), IM_COL32(255, 220, 120, 240), 3); }
  for (uint64_t id : smithyPlaced()) if (const Instance* t = gApp->doc.find(id)) {   // SM1: smithing tables: a small anvil
    vec2 p = v.toScreen(vec2(t->pos.x, t->pos.z)); if (!v.contains(p)) continue;
    float a = std::clamp(v.scale * 1.6f, 4.0f, 8.0f); ImU32 c = IM_COL32(235, 190, 120, 240), k = IM_COL32(20, 16, 12, 220);
    ImVec2 top[4] = {ImVec2(p.x - a * 1.2f, p.y - a * 0.6f), ImVec2(p.x + a * 1.3f, p.y - a * 0.6f), ImVec2(p.x + a * 0.5f, p.y), ImVec2(p.x - a * 0.6f, p.y)};
    dl->AddRectFilled(ImVec2(p.x - a * 0.9f, p.y + a * 0.55f), ImVec2(p.x + a * 0.9f, p.y + a * 0.85f), k);   // the foot, shadowed
    dl->AddConvexPolyFilled(top, 4, c); dl->AddRectFilled(ImVec2(p.x - a * 0.35f, p.y), ImVec2(p.x + a * 0.35f, p.y + a * 0.6f), c);
    dl->AddRectFilled(ImVec2(p.x - a * 0.8f, p.y + a * 0.6f), ImVec2(p.x + a * 0.8f, p.y + a * 0.8f), c);
  }
  // BR1b: district names (the big map at full size; the corner map, ~200 m across, small)
  float size = std::clamp(v.scale * 7.0f, 10.0f, 17.0f);
  for (auto& d : S.districts) {
    vec2 s = v.toScreen(vec2(d.pos.x, d.pos.z));
    if (!v.contains(s)) continue;
    label(dl, ImVec2(s.x, s.y), d.name, size, IM_COL32(255, 238, 190, 235));
  }
}

// ---------------------------------------------------------------- --selftest-royale (headless)
void royaleSelfTest(MpTest& suite) {
  auto chk = [&](bool ok, const std::string& what) { suite.check(ok, what); };
  loadCfg();
  // 1. circles nest and shrink (50 seeds, a 350 x 400 m area)
  bool nest = true, shrink = true; vec2 half(175, 200);
  for (uint32_t seed = 1; seed <= 50; ++seed) {
    RoyaleStorm st; st.cfg = gCfg.storm; st.build(half, seed * 7919u);
    for (int k = 1; k <= 4; ++k) {
      if (glm::length(st.circle[k].c - st.circle[k - 1].c) + st.circle[k].r > st.circle[k - 1].r + 1e-3f) nest = false;
      if (!(st.circle[k].r < st.circle[k - 1].r)) shrink = false;
    }
  }
  chk(nest, "every circle lies wholly inside the one before (50 seeds)");
  chk(shrink, "every circle is smaller than the one before; the last is " + std::to_string((int)gCfg.storm.finalRadius) + " m");
  // 2. schedule
  RoyaleStorm st; st.cfg = gCfg.storm; st.build(half, 12345);
  const RoyaleStormCfg& c = st.cfg;
  float t = 0; bool sched = true;
  for (int k = 0; k < 4; ++k) {
    RoyaleStorm::At w = st.at(t + c.wait[k] * 0.5f), sh = st.at(t + c.wait[k] + c.shrink[k] * 0.5f);
    if (w.stage != k || w.shrinking || std::fabs(w.now.r - st.circle[k].r) > 1e-3f || w.dps != c.dps[k]) sched = false;
    float mid = (st.circle[k].r + st.circle[k + 1].r) * 0.5f;
    if (sh.stage != k || !sh.shrinking || std::fabs(sh.now.r - mid) > 0.01f * st.circle[k].r) sched = false;
    t += c.wait[k] + c.shrink[k];
    RoyaleStorm::At e = st.at(t + 0.01f);
    if (std::fabs(e.now.r - st.circle[k + 1].r) > 0.1f) sched = false;
  }
  char b[200]; std::snprintf(b, sizeof b, "wait / shrink on schedule: %.0f/%.0f, %.0f/%.0f, %.0f/%.0f, %.0f/%.0f s (%.0f s in all), damage %.0f/%.0f/%.0f/%.0f per s",
                             c.wait[0], c.shrink[0], c.wait[1], c.shrink[1], c.wait[2], c.shrink[2], c.wait[3], c.shrink[3], st.total(), c.dps[0], c.dps[1], c.dps[2], c.dps[3]);
  chk(sched, b);
  RoyaleStorm::At fin = st.at(st.total() + 5);
  chk(fin.stage == 4 && std::fabs(fin.now.r - c.finalRadius) < 1e-3f && !fin.shrinking, "after the last shrink the ring is the final circle (" + std::to_string((int)c.finalRadius) + " m), first pausing");
  {   // BR7: the final circle drifts: each leg moves driftMin..driftMax, stays inside the area, glides then pauses
    bool legs = true; float cyc = c.driftPause + c.driftMove; vec2 lim = half - vec2(c.finalRadius);
    for (int k = 0; k < 10; ++k) {
      RoyaleStorm::At p0 = st.at(st.total() + k * cyc + 1), mv = st.at(st.total() + k * cyc + c.driftPause + c.driftMove * 0.5f), p1 = st.at(st.total() + (k + 1) * cyc + 1);
      float d = glm::length(p1.now.c - p0.now.c);
      if (p0.shrinking || !mv.shrinking || d < c.driftMin * 0.8f - 0.01f || d > c.driftMax + 0.01f || glm::length(p0.next.c - p1.now.c) > 0.01f) legs = false;
      if (std::fabs(p1.now.c.x) > lim.x + 0.01f || std::fabs(p1.now.c.y) > lim.y + 0.01f) legs = false;
    }
    char d[200]; std::snprintf(d, sizeof d, "the final circle drifts: %.0f s pause, %.0f s glide of %.0f..%.0f m to the shown next ring, inside the area (10 legs)", c.driftPause, c.driftMove, c.driftMin, c.driftMax);
    chk(legs, d);
  }
  // 3. damage only outside
  float tt = c.wait[0] + c.shrink[0] + 1; RoyaleStorm::At a = st.at(tt);
  vec2 in = a.now.c + vec2(a.now.r * 0.9f, 0), out = a.now.c + vec2(a.now.r * 1.1f, 0);
  chk(!st.outside(in, tt) && st.outside(out, tt), "a point inside the ring is safe, one just outside is in the storm");
  // 4. a 10-bot match on the real Match: one life each, bots walk to their safe point, the storm and fights thin them out
  Scenario sc; sc.rules.mode = GameMode::BattleRoyale; sc.rules.royalePlayers = 10;
  Match m; std::string err;
  bool ok = m.setup(sc, 0, err);
  chk(ok && m.slots.size() == 10, "battle royale roster: 10 combatants, one life each (" + err + ")");
  if (!ok) return;
  m.start();
  std::mt19937 rng(7); std::uniform_real_distribution<float> u(0.0f, 1.0f);
  struct B { vec2 p; float hp = 100, spd, acc = 0; };
  std::vector<B> bots(10);
  for (auto& x : bots) { x.p = vec2((u(rng) * 2 - 1) * 170, (u(rng) * 2 - 1) * 195); x.spd = 1.0f + u(rng) * 4.0f; }
  for (auto& s : m.slots) m.onSpawned(s.id);
  float dt = 0.1f, T = 0; bool respawned = false;
  while (m.phase == MatchPhase::Running && T < 2000) {
    T += dt;
    RoyaleStorm::At now = st.at(T);
    for (int i = 0; i < 10; ++i) {
      if (!m.slots[i].alive) continue;
      B& x = bots[i];
      vec2 goal = now.next.c + vec2(std::cos(i * 2.4f), std::sin(i * 2.4f)) * now.next.r * 0.5f;
      vec2 d = goal - x.p; float l = glm::length(d); if (l > 0.1f) x.p += d / l * std::min(l, x.spd * dt);
      if (glm::length(x.p - now.now.c) > now.now.r) { x.acc += dt; while (x.acc >= 1) { x.acc -= 1; x.hp -= now.dps; } } else x.acc = 0;
      for (int j = 0; j < 10; ++j) if (j != i && m.slots[j].alive && glm::length(bots[j].p - x.p) < 12 && u(rng) < 0.02f) bots[j].hp -= 34;   // a fight
    }
    for (int i = 0; i < 10; ++i) if (m.slots[i].alive && bots[i].hp <= 0) m.onDeath(i, -1);
    m.update(dt);
    if (!m.spawnQueue().empty()) respawned = true;
  }
  int winners = 0; std::set<int> places; for (auto& s : m.slots) { winners += s.place == 1; places.insert(s.place); }
  std::snprintf(b, sizeof b, "a 10-bot match ends with one winner after %.0f s (%s); placements #1..#10 each once", T, m.result.c_str());
  chk(m.phase == MatchPhase::Ended && m.winnerSlot >= 0 && winners == 1 && places.size() == 10 && *places.begin() == 1 && *places.rbegin() == 10, b);
  chk(!respawned, "nobody respawns");
}
