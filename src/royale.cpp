// BR1 Battle royale: the drop (bomber line, free fall, parachute), the storm (four shrinking circles), placement and
// the HUD. See royale.h for the hooks; numbers in content/settings/royale.json.
#include "royale.h"
#include "app.h"
#include "game.h"
#include "minimap.h"
#include "enemy_types.h"
#include "mptest.h"
#include <imgui.h>
#include <chrono>
#include <random>

// ---------------------------------------------------------------- storm (pure)
void RoyaleStorm::build(vec2 half, uint32_t seed) {
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
      if (std::fabs(c.x) + r[k] * 0.5f <= half.x && std::fabs(c.y) + r[k] * 0.5f <= half.y) break;   // mostly over the map
    }
    circle[k] = {best, r[k]};
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
  a.stage = 4; a.shrinking = false; a.left = 0; a.now = a.next = circle[4]; a.dps = cfg.dps[3];
  return a;
}

// ---------------------------------------------------------------- settings and state
namespace {
struct Cfg {
  float planeAlt = 150, planeSpeed = 45, overrun = 150, jitter = 0.25f;
  float fallSpeed = 55, fallAcross = 25, fallAccel = 14;
  float chuteAt = 60, chuteDescent = 6, chuteAcross = 8, chuteAccel = 1.5f;
  float botJumpFrom = 0.08f, botJumpTo = 0.92f, botLandSpread = 110;
  RoyaleStormCfg storm;
};
Cfg gCfg;
void loadCfg() {
  gCfg = Cfg{};
  json j; try { j = json::parse(readTextFile(contentDir() / "settings" / "royale.json")); } catch (...) { logf("royale: content/settings/royale.json missing or bad, defaults"); return; }
  auto f = [&](const char* sec, const char* k, float& v) { if (j.contains(sec) && j[sec].contains(k)) v = j[sec][k].get<float>(); };
  f("plane", "altitude_m", gCfg.planeAlt); f("plane", "speed_mps", gCfg.planeSpeed); f("plane", "overrun_m", gCfg.overrun); f("plane", "endJitter", gCfg.jitter);
  f("fall", "speed_mps", gCfg.fallSpeed); f("fall", "across_mps", gCfg.fallAcross); f("fall", "accel_mps2", gCfg.fallAccel);
  f("chute", "autoOpen_m", gCfg.chuteAt); f("chute", "descent_mps", gCfg.chuteDescent); f("chute", "across_mps", gCfg.chuteAcross); f("chute", "accel_mps2", gCfg.chuteAccel);
  f("bots", "jumpFrom", gCfg.botJumpFrom); f("bots", "jumpTo", gCfg.botJumpTo); f("bots", "landSpread_m", gCfg.botLandSpread);
  if (j.contains("storm")) {
    const json& s = j["storm"];
    auto arr = [&](const char* k, float* v, int n) { if (s.contains(k) && s[k].is_array()) for (int i = 0; i < n && i < (int)s[k].size(); ++i) v[i] = s[k][i].get<float>(); };
    arr("wait_s", gCfg.storm.wait, 4); arr("shrink_s", gCfg.storm.shrink, 4); arr("dps", gCfg.storm.dps, 4); arr("radiusFrac", gCfg.storm.radiusFrac, 3);
    gCfg.storm.finalRadius = s.value("finalRadius_m", gCfg.storm.finalRadius);
  }
}

struct Air { int st = RA_GROUND; bool boarded = false; vec3 p{0}, v{0}; float jumpAt = 0; vec2 aim{0}; float stormAcc = 0; };
struct State {
  bool on = false;
  float time = 0;                 // s since the match start (plane and storm clock)
  vec3 a{0}, dir{0, 0, 1}; float len = 0, tIn = 0, tOut = 0;   // bomber line: start, unit direction, length; inside the area
  RoyaleStorm storm;
  std::vector<Air> air;
  bool jumpEdge = false;          // local player pressed Space since the last step
  uint32_t seed = 1;
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
bool planeFlying() { return S.time * gCfg.planeSpeed < S.len; }
float groundBelow(App& app, vec3 p) {
  float g = app.terrain.height(p.x, p.z);
  Physics::Hit h;
  if (Physics::castRayEx(p + vec3(0, 0.5f, 0), vec3(0, -1, 0), 800.0f, h)) g = std::max(g, h.pos.y);   // roofs count
  return std::max(g, app.terrain.waterLevel);
}
vec3 horizLook(App& app) { vec3 f = App::forward(app.eyeYaw, app.eyePitch); return vec3(f.x, 0, f.z); }   // length cos(pitch): straight down glides least
vec3 approachV(vec3 v, vec3 want, float rate) { vec3 d = want - v; float l = glm::length(d); return l <= rate ? want : v + d / l * rate; }
}

bool royaleActive() { return S.on && gGame && gGame->active && gGame->match.mode() == GameMode::BattleRoyale; }
int royaleAirState(int slot) { return royaleActive() && slot >= 0 && slot < (int)S.air.size() ? S.air[slot].st : RA_GROUND; }

void royaleBegin(App& app, Game& G) {
  S = State{};
  if (G.match.mode() != GameMode::BattleRoyale || G.isClient) return;
  loadCfg();
  S.on = true;
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
  S.storm.cfg = gCfg.storm; S.storm.build(vec2(hx, hz), S.seed * 2654435761u);
  S.air.assign(G.sold.size(), Air{});
  for (size_t i = 0; i < S.air.size(); ++i) {   // bots: a random point along the line, and a landing aim near below it
    Air& a = S.air[i];
    a.jumpAt = S.tIn + (gCfg.botJumpFrom + u(rng) * (gCfg.botJumpTo - gCfg.botJumpFrom)) * (S.tOut - S.tIn);
    vec3 q = planeAt(a.jumpAt); float ang = u(rng) * 6.2831853f, rr = gCfg.botLandSpread * std::sqrt(u(rng));
    a.aim = glm::clamp(vec2(q.x, q.z) + vec2(std::cos(ang), std::sin(ang)) * rr, vec2(-hx, -hz) * 0.92f, vec2(hx, hz) * 0.92f);
  }
  G.deployReady = true;   // no deploy screen: everyone boards the bomber
  // BR1b: named districts from the map file (scenario.districts: name, pos [x, y, z], radius); absent on most maps
  if (app.doc.extra.is_object() && app.doc.extra.contains("scenario") && app.doc.extra["scenario"].is_object())
    for (auto& d : app.doc.extra["scenario"].value("districts", json::array()))
      if (d.is_object() && d.contains("pos") && d["pos"].is_array() && d["pos"].size() == 3)
        S.districts.push_back({d.value("name", std::string("?")), vec3(d["pos"][0].get<float>(), d["pos"][1].get<float>(), d["pos"][2].get<float>())});
  logf("royale: %zu aboard, bomber (%.0f %.0f) -> heading %.0f deg at %.0f m, %.0f m line; storm circles r %.0f / %.0f / %.0f / %.0f m",
       S.air.size(), S.a.x, S.a.z, degrees(std::atan2(S.dir.x, S.dir.z)), y, S.len, S.storm.circle[1].r, S.storm.circle[2].r, S.storm.circle[3].r, S.storm.circle[4].r);
}

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
    if (!a.boarded) {   // first step alive: aboard while the bomber is still over the area, else straight into free fall
      a.boarded = true;
      if (S.time < S.tOut) { a.st = RA_ABOARD; a.p = P; }
      else { a.st = RA_FREEFALL; a.p = vec3(s.feet.x, P.y, s.feet.z); a.aim = vec2(s.feet.x, s.feet.z); }
      a.v = vec3(0);
      if (s.agent >= 0) { Physics::destroyAgent(s.agent); s.agent = -1; }   // no capsule until it lands
    }
    if (a.st == RA_GROUND) continue;
    bool local = s.localPlayer;
    if (a.st == RA_ABOARD) {
      a.p = P - vec3(0, 1.0f, 0);
      bool over = S.time >= S.tIn;
      if ((!local && !s.human && S.time >= a.jumpAt) || (local && jump && over) || S.time >= S.tOut) {
        a.st = RA_FREEFALL; a.v = S.dir * gCfg.planeSpeed * 0.3f;
        if (local) app.toast("Free fall: look where you want to go. Space opens the parachute (it opens itself at " + std::to_string((int)gCfg.chuteAt) + " m).", 5);
      } else if (local && jump) app.toast("Wait until the bomber is over the area.", 2);
    } else {
      bool chute = a.st == RA_CHUTE;
      float across = chute ? gCfg.chuteAcross : gCfg.fallAcross;
      vec3 want(0);
      if (local) want = horizLook(app) * across;   // where the mouse points; straight down glides least
      else { vec3 to(a.aim.x - a.p.x, 0, a.aim.y - a.p.z); float l = glm::length(to); if (l > 0.5f) want = to / l * std::min(across, l * 0.6f); }
      vec3 hv(a.v.x, 0, a.v.z);
      hv = approachV(hv, want, (chute ? gCfg.chuteAccel : gCfg.fallAccel) * dt);
      float vy = a.v.y, target = -(chute ? gCfg.chuteDescent : gCfg.fallSpeed);
      vy += std::clamp(target - vy, -20.0f * dt, 25.0f * dt);   // the canopy brakes hard; free fall builds up
      a.v = vec3(hv.x, vy, hv.z);
      vec3 np = a.p + a.v * dt;
      np.x = std::clamp(np.x, -hx * 1.05f, hx * 1.05f); np.z = std::clamp(np.z, -hz * 1.05f, hz * 1.05f);
      float g = groundBelow(app, np);
      if (!chute && (np.y - g <= gCfg.chuteAt || (local && jump))) { a.st = RA_CHUTE; if (local) app.toast("Parachute open. Look to steer; it is slow to answer.", 4); }
      if (np.y <= g + 0.02f) {   // landed: no fall damage (a placement, not a fall)
        vec3 feet(np.x, g, np.z);
        a.st = RA_GROUND; a.v = vec3(0);
        s.feet = feet; s.vel = vec3(0); s.speed = 0;
        if (local) Physics::setCharacterPos(feet);
        else {
          const EnemyType* et = s.zombie ? &enemyType(s.etype) : nullptr;
          s.agent = et && et->capsuleH > 0 ? Physics::createAgent(feet, et->capsuleH, et->capsuleR) : Physics::createAgent(feet);
          s.br.repathT = 0;
        }
        continue;
      }
      a.p = np;
    }
    s.feet = a.p; s.vel = a.v; s.speed = 0; s.posture = Physics::STAND;
    if (!local) { vec3 h(a.v.x, 0, a.v.z); if (glm::length(h) > 0.5f) s.yaw = degrees(std::atan2(-h.x, -h.z)); else if (a.st == RA_ABOARD) s.yaw = degrees(std::atan2(-S.dir.x, -S.dir.z)); }
    else Physics::setCharacterPos(a.p);   // the explore camera rides along
  }
  // storm damage outside the current ring, once a second while outside (landed soldiers only); armor never applies
  RoyaleStorm::At at = S.storm.at(S.time);
  for (auto& s : G.sold) {
    if (!s.alive) continue;
    Air& a = S.air[s.slot];
    if (a.st != RA_GROUND || glm::length(vec2(s.feet.x, s.feet.z) - at.now.c) <= at.now.r) { a.stormAcc = 0; continue; }
    a.stormAcc += dt;
    while (a.stormAcc >= 1.0f && s.alive) { a.stormAcc -= 1.0f; G.damage(s.slot, at.dps, -1, vec3(0, 1, 0), "storm"); }
  }
}

RoyaleRing royaleRing(bool next) { if (!royaleActive()) return {}; RoyaleStorm::At a = S.storm.at(S.time); return next ? a.next : a.now; }

vec3 royaleSafePoint(const Soldier& s) {
  if (!royaleActive() || !gGame) return s.feet;
  RoyaleStorm::At a = S.storm.at(S.time);
  uint32_t h = (uint32_t)s.slot * 2654435761u ^ (uint32_t)(a.stage + 1) * 40503u;
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
  if (!L || !L->alive || royaleAirState(L->slot) == RA_GROUND) return;
  if (in.jump) S.jumpEdge = true;
  in.move = vec2(0); in.jump = in.jumpHeld = in.crouch = in.sprint = in.prone = false;
  in.fire = in.aim = in.reload = in.interact = in.build = in.quickGrenade = false;
  (void)app;
}

bool royaleCamera(App& app, vec3& eye, vec3& fwd, float& fov) {
  if (!royaleActive() || !gGame) return false;
  Soldier* L = gGame->local();
  if (!L || !L->alive || royaleAirState(L->slot) != RA_ABOARD) return false;
  vec3 P = planeAt(S.time), f = App::forward(app.eyeYaw, app.eyePitch);
  eye = P - f * 32.0f + vec3(0, 6, 0); fwd = glm::normalize(P - eye); fov = 70;
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
    if (!s.alive || royaleAirState(s.slot) != RA_CHUTE) continue;
    float yaw = s.localPlayer ? radians(app.eyeYaw) + 3.14159265f : radians(s.yaw) + 3.14159265f;
    mat4 M = glm::translate(mat4(1), s.feet) * glm::rotate(mat4(1), yaw, vec3(0, 1, 0));
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
  // the storm wall: the current ring as a translucent blue band; the whole view tinted while the camera is outside it
  mat4 vp = app.projM * app.viewM;
  auto proj = [&](vec3 w, ImVec2& o) { vec4 q = vp * vec4(w, 1); if (q.w < 0.1f) return false; o = ImVec2((q.x / q.w * 0.5f + 0.5f) * io.DisplaySize.x, (0.5f - q.y / q.w * 0.5f) * io.DisplaySize.y); return true; };
  float y0 = std::min(eye.y, app.terrain.height(eye.x, eye.z)) - 40, y1 = std::max(eye.y, app.terrain.height(eye.x, eye.z)) + 160;
  const int N = 128;
  for (int k = 0; k < N; ++k) {
    float a0 = k * 6.2831853f / N, a1 = (k + 1) * 6.2831853f / N;
    vec2 p0 = at.now.c + vec2(std::cos(a0), std::sin(a0)) * at.now.r, p1 = at.now.c + vec2(std::cos(a1), std::sin(a1)) * at.now.r;
    if (glm::length(p0 - vec2(eye.x, eye.z)) > 900) continue;
    ImVec2 q[4];
    if (!proj(vec3(p0.x, y0, p0.y), q[0]) || !proj(vec3(p1.x, y0, p1.y), q[1]) || !proj(vec3(p1.x, y1, p1.y), q[2]) || !proj(vec3(p0.x, y1, p0.y), q[3])) continue;
    bg->AddQuadFilled(q[0], q[1], q[2], q[3], IM_COL32(70, 110, 255, 46));
  }
  if (glm::length(vec2(eye.x, eye.z) - at.now.c) > at.now.r) bg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(40, 80, 255, 62));
  // banner: alive, storm timer
  int alive = 0; for (auto& s : G.sold) alive += s.alive;
  int stillIn = G.match.stillIn();
  char b[200], tm[32];
  int sec = (int)std::ceil(at.left); std::snprintf(tm, sizeof tm, "%d:%02d", sec / 60, sec % 60);
  if (at.stage >= 4) std::snprintf(b, sizeof b, "BATTLE ROYALE   |   Alive %d / %zu   |   Final circle", std::max(alive, stillIn), G.sold.size());
  else std::snprintf(b, sizeof b, "BATTLE ROYALE   |   Alive %d / %zu   |   %s %s   |   Circle %d / 4", std::max(alive, stillIn), G.sold.size(), at.shrinking ? "Closing" : "Storm closes in", tm, at.stage + 1);
  float cx = io.DisplaySize.x * 0.5f, y = 10;
  ImVec2 ts = ImGui::CalcTextSize(b);
  dl->AddRectFilled(ImVec2(cx - ts.x * 0.5f - 12, y), ImVec2(cx + ts.x * 0.5f + 12, y + 28), IM_COL32(12, 12, 20, 205), 5);
  dl->AddText(ImVec2(cx - ts.x * 0.5f, y + 6), at.shrinking ? IM_COL32(150, 190, 255, 255) : IM_COL32(235, 235, 220, 255), b);
  Soldier* L = G.local();
  if (L && L->alive && royaleAirborne(L->slot))   // BR1b: on the drop (aboard, falling, under the canopy) the districts are named in the view
    for (auto& d : S.districts) {
      ImVec2 sp; vec3 w = d.pos + vec3(0, 12, 0);
      if (!proj(w, sp) || sp.x < 0 || sp.y < 0 || sp.x > io.DisplaySize.x || sp.y > io.DisplaySize.y) continue;
      char t[96]; std::snprintf(t, sizeof t, "%s  %.0f m", d.name.c_str(), glm::length(w - eye));
      label(dl, sp, t, 17.0f, IM_COL32(255, 238, 190, 240));
    }
  auto centre = [&](const char* t, float yy, ImU32 col) { ImVec2 s2 = ImGui::CalcTextSize(t); dl->AddRectFilled(ImVec2(cx - s2.x * 0.5f - 12, yy - 6), ImVec2(cx + s2.x * 0.5f + 12, yy + s2.y + 6), IM_COL32(10, 10, 14, 200), 5); dl->AddText(ImVec2(cx - s2.x * 0.5f, yy), col, t); };
  if (L && L->alive) {
    int st = royaleAirState(L->slot);
    if (st == RA_ABOARD) centre(S.time < S.tIn ? "Aboard the bomber - Space to jump once it is over the area" : "Aboard the bomber - SPACE to jump", io.DisplaySize.y * 0.78f, IM_COL32(255, 230, 150, 255));
    else if (st == RA_FREEFALL) { char f[120]; std::snprintf(f, sizeof f, "Free fall  %.0f m  -  Space: parachute", L->feet.y - groundBelow(app, L->feet)); centre(f, io.DisplaySize.y * 0.78f, IM_COL32(255, 230, 150, 255)); }
    else if (st == RA_CHUTE) { char f[80]; std::snprintf(f, sizeof f, "Parachute  %.0f m", L->feet.y - groundBelow(app, L->feet)); centre(f, io.DisplaySize.y * 0.78f, IM_COL32(220, 230, 255, 255)); }
    else if (glm::length(vec2(L->feet.x, L->feet.z) - at.now.c) > at.now.r) { char f[80]; std::snprintf(f, sizeof f, "IN THE STORM  -%.0f hp/s", at.dps); centre(f, io.DisplaySize.y * 0.70f, IM_COL32(150, 180, 255, 255)); }
  }
  if (L && L->slot < (int)G.match.slots.size() && G.match.slots[L->slot].place > 0) {
    char f[80]; int pl = G.match.slots[L->slot].place;
    if (pl == 1) std::snprintf(f, sizeof f, "WINNER  -  #1 of %zu", G.sold.size());
    else std::snprintf(f, sizeof f, "You placed #%d of %zu", pl, G.sold.size());
    centre(f, io.DisplaySize.y * 0.32f, pl == 1 ? IM_COL32(150, 240, 150, 255) : IM_COL32(255, 200, 170, 255));
  }
}

void royaleMinimap(ImDrawList* dl, const MinimapView& v) {
  if (!royaleActive()) return;
  RoyaleStorm::At a = S.storm.at(S.time);
  auto ring = [&](const RoyaleRing& r, ImU32 col, float w) { vec2 c = v.toScreen(r.c); dl->AddCircle(ImVec2(c.x, c.y), r.r * v.scale, col, 96, w); };
  ring(a.now, IM_COL32(80, 130, 255, 230), 2.5f);
  if (a.stage < 4) ring(a.next, IM_COL32(255, 255, 255, 230), 1.5f);
  if (planeFlying()) { vec3 P = planeAt(S.time); vec2 p = v.toScreen(vec2(P.x, P.z)), q = v.toScreen(vec2(P.x + S.dir.x * 40, P.z + S.dir.z * 40)); dl->AddLine(ImVec2(p.x, p.y), ImVec2(q.x, q.y), IM_COL32(255, 220, 120, 240), 3); }
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
  RoyaleStorm::At fin = st.at(st.total() + 30);
  chk(fin.stage == 4 && std::fabs(fin.now.r - c.finalRadius) < 1e-3f, "after the last shrink the ring stays at the final circle");
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
