// --selftest-swim (docs/movement.md#swimming, docs/testing.md): swimming on the blank map through scripted ExploreInput.
//  * movement.json "swim" loads; on land the weapon mesh is drawn
//  * enter at chest depth, not below; hysteresis: still swimming in 1.15 m of water, out below 1.05 m once grounded
//  * floating in deep water: off the bed, the eye bobs inside its band over the surface; swim / sprint speeds
//  * no fire / aim / reload while swimming (LMB + RMB held), the weapon lowered, the first-person draw list holds the
//    arm boxes only (no weapon mesh) and the hands stroke
//  * Ctrl dives: the eye goes under, RenderView::underwater -> the world fog becomes the dense water fog, audio muffled,
//    a non-black blue-green screenshot; look pitch steers when submerged (down sinks, up rises)
//  * breath: no damage before 30 s under, drowning damage (drownDamage_hps) after; breath back on surfacing
//  * mantle out: hold Space facing a bank 0.3 m above the surface, ends standing on top, no longer swimming
//  * a bot agent dropped in deep water floats at the surface (Physics::updateAgent), not on the bed
//  * navmesh: a pond from the water query (not the global level) is forbidden: no walkable polygon in it, the path
//    across goes round it, a straight walk across is blocked; without the query the same ground is walkable
#include "selftest.h"
#include "mptest.h"
#include "nav.h"
#include "nav_geom.h"
#include "physics.h"
#include "water_query.h"
#include <cstdarg>
#include <functional>

namespace {
MpTest* T = nullptr;
int mode = -1; bool started = false;
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0;
char B[700];
bool chk(MpTest& t, bool ok, const char* f, ...) { va_list a; va_start(a, f); std::vsnprintf(B, sizeof B, f, a); va_end(a); return t.check(ok, B); }
#define CHK(ok, ...) chk(*T, ok, __VA_ARGS__)
void push(const std::string& n, std::function<bool(App&, float)> f) { seq.push_back({n, f}); }
fs::path outDir() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }

vec3 P0(0);             // test spot (feet on the ground)
float G0 = 0;           // ground height there
bool wantProbe = false; int probes = 0; vec3 probeMean(0); fs::path pendingShot;
uint32_t bankBody = 0xffffffffu;

void put(App& a, vec3 p) { Physics::setCharacterPos(vec3(p.x, a.terrain.height(p.x, p.z) + 0.02f, p.z)); }
void water(App& a, float level) { a.terrain.waterLevel = level; }
bool armsOnly(App& a, int& arms, int& other) {
  arms = other = 0;
  for (auto& d : a.R.viewModel) { bool arm = d.mesh == &a.armMesh[0] || d.mesh == &a.armMesh[1] || d.mesh == &a.armMesh[2]; (arm ? arms : other)++; }
  other += (int)a.R.viewModelTex.size();
  return arms > 0 && other == 0;
}

void build(App& app) {
  const TemplateInfo* t = app.findTemplate("blank_slate");
  app.newMap(*t, "Swim range");
  P0 = vec3(0, 0, 0); G0 = app.terrain.height(0, 0); P0.y = G0;
  app.doc.start = {true, P0, 0};
  water(app, -1000);
  T->check(app.enterExplore(), "F5 into Explore on the blank map");
  app.useScripted = true; app.vm.procedural = false;
  seq.clear(); cur = 0; st = 0;

  push("settings", [](App& a, float t) {
    if (t < 0.6f) return false;
    const SwimSettings& s = a.swimCfg;
    int arms, other; armsOnly(a, arms, other);
    CHK(std::fabs(s.enterDepth - 1.25f) < 1e-3f && std::fabs(s.exitDepth - 1.05f) < 1e-3f && std::fabs(s.breath - 30.0f) < 1e-3f && std::fabs(s.speed - 2.2f) < 1e-3f && s.drownDps > 0,
        "movement.json swim: enter %.2f m, exit %.2f m, float %.2f m, %.1f / %.1f m/s, breath %.0f s, drowning %.0f HP/s every %.1f s, fog %.3f /m",
        s.enterDepth, s.exitDepth, s.floatDepth, s.speed, s.sprint, s.breath, s.drownDps, s.drownTick, s.fogDensity);
    CHK(other > 0 && !a.swim.active, "on land the first-person weapon is drawn (%d weapon items, %d arm boxes)", other, arms);
    CHK((bool)Physics::waterQuery, "the agents' water query is set (Physics::waterQuery -> waterAt)");
    return true; });

  push("enter / exit depth (hysteresis)", [](App& a, float t) {
    static int ph; static bool r[4];
    if (t == 0) { put(a, P0); a.eyeYaw = 0; a.eyePitch = 0; a.scriptedInput = ExploreInput{}; ph = 0; water(a, G0 + 1.15f); return false; }
    float g = a.terrain.height(P0.x, P0.z);
    if (ph == 0 && t > 0.6f) { r[0] = a.swim.active; water(a, g + 1.32f); ph = 1; return false; }
    if (ph == 1 && t > 1.2f) { r[1] = a.swim.active; water(a, g + 1.15f); ph = 2; return false; }
    if (ph == 2 && t > 1.8f) { r[2] = a.swim.active; water(a, g + 0.95f); ph = 3; return false; }
    if (ph == 3 && t > 2.4f) {
      r[3] = a.swim.active;
      CHK(!r[0] && r[1] && r[2] && !r[3], "1.15 m of water: walking (%d); 1.32 m: swimming (%d); back to 1.15 m: still swimming (%d); 0.95 m, grounded: walking (%d) [%d enters, %d exits, %s]",
          r[0], r[1], r[2], r[3], a.swim.enters, a.swim.exits, a.swim.why.c_str());
      return true;
    }
    return false; });

  push("float, surface bob", [](App& a, float t) {
    static float lo, hi, tUp; static int n, grounded, under;
    if (t == 0) { put(a, P0); water(a, G0 + 4.0f); a.scriptedInput = ExploreInput{}; lo = 1e9f; hi = -1e9f; n = grounded = under = 0; tUp = -1; return false; }
    const SwimSettings& s = a.swimCfg;
    float mid = a.move.eyeStand - s.floatDepth, band = s.bobAmp + 0.06f;
    float e = a.eyePos().y - a.terrain.waterLevel;
    if (tUp < 0) {   // buoyancy floats the swimmer up off the bed (0.45 m/s), then 1.5 s to settle
      if (std::fabs(e - mid) < 0.1f) tUp = t;
      return t > 12 ? (CHK(false, "floated up from the bed of 4 m of water (eye %.2f m over the surface)", e), true) : false;
    }
    if (t < tUp + 1.5f) return false;
    auto c = Physics::character();
    lo = std::min(lo, e); hi = std::max(hi, e); ++n; grounded += c.grounded; under += a.swim.headUnder;
    if (t < tUp + 5.5f) return false;
    CHK(a.swim.active && grounded == 0 && under == 0 && lo > mid - band && hi < mid + band && hi - lo > 0.02f,
        "4 m of water: floated up off the bed in %.1f s, then off the bed (%d grounded samples), eye %.3f..%.3f m over the surface for 4 s (band %.2f +- %.2f, bobbing %.3f m), head never under (%d)",
        tUp, grounded, lo, hi, mid, band, hi - lo, under);
    return true; });

  auto speed = [](const char* what, bool sprint, float want) {
    return [what, sprint, want](App& a, float t) {
      static vec3 p0; static float t0;
      if (t == 0) { a.eyeYaw = 0; a.eyePitch = 0; a.scriptedInput = ExploreInput{}; a.scriptedInput.move = vec2(0, 1); a.scriptedInput.sprint = sprint; return false; }
      if (t < 1.5f) { p0 = Physics::character().pos; t0 = t; return false; }
      if (t < 3.5f) return false;
      auto c = Physics::character();
      float v = glm::length(vec2(c.pos.x - p0.x, c.pos.z - p0.z)) / (t - t0);
      a.scriptedInput = ExploreInput{};
      CHK(std::fabs(v - want) / want < 0.08f && a.swim.active, "%s: %.2f m/s (want %.2f)", what, v, want);
      return true; };
  };
  push("swim speed", speed("swimming", false, 2.2f));
  push("sprint swim speed", speed("swimming with Shift", true, 3.2f));

  push("no fire, weapon lowered, hands-only arms", [](App& a, float t) {
    static int shots0, arms, other, frames, armsOk; static float ads, hmin, hmax;
    if (t == 0) { shots0 = a.shotsFired; ads = 0; frames = armsOk = 0; hmin = 1e9f; hmax = -1e9f; a.scriptedInput = ExploreInput{}; a.scriptedInput.move = vec2(0, 1); return false; }
    a.scriptedInput.fire = true; a.scriptedInput.aim = true; a.scriptedInput.move = vec2(0, 1);
    if (t > 0.3f) {
      ads = std::max(ads, a.adsT); ++frames; if (armsOnly(a, arms, other)) ++armsOk;
      hmin = std::min(hmin, a.lastHandR.z); hmax = std::max(hmax, a.lastHandR.z);
    }
    if (t < 2.5f) return false;
    a.scriptedInput = ExploreInput{};
    CHK(a.shotsFired == shots0 && ads < 0.01f && a.swimLower() > 0.99f, "LMB + RMB held 2.5 s while swimming: %d shots, ADS %.2f, weapon lowered %.2f", a.shotsFired - shots0, ads, a.swimLower());
    CHK(armsOk == frames && frames > 10, "first-person draw list: arm boxes only on %d / %d frames (last: %d arm boxes, %d weapon items)", armsOk, frames, arms, other);
    CHK(hmax - hmin > 0.12f, "breaststroke: the right hand travels %.2f m fore and aft (camera space)", hmax - hmin);
    return true; });

  push("dive: underwater view, fog, audio", [](App& a, float t) {
    static vec4 fogTop; static int ph; static float yTop;
    if (t == 0) { a.scriptedInput = ExploreInput{}; ph = 0; return false; }
    if (ph == 0 && t > 0.5f) { fogTop = a.R.fogNow; yTop = a.eyePos().y; ph = 1; a.scriptedInput.crouch = true; return false; }
    if (ph == 1 && t > 2.0f) { a.scriptedInput.crouch = false; ph = 2; return false; }
    if (ph == 2 && t > 2.3f) {
      vec4 f = a.R.fogNow; const SwimSettings& s = a.swimCfg;
      CHK(a.eyeUnderwater() && a.swim.headUnder && a.eyePos().y < a.terrain.waterLevel - 0.5f, "Ctrl dives: eye %.2f m below the surface (was %.2f m above)", a.terrain.waterLevel - a.eyePos().y, yTop - a.terrain.waterLevel);
      CHK(std::fabs(f.w - s.fogDensity) < 1e-4f && glm::length(vec3(f) - s.fogColor) < 1e-3f && std::fabs(fogTop.w - s.fogDensity) > 1e-3f,
          "fog: %.4f /m (%.2f %.2f %.2f) under water, %.4f /m (%.2f %.2f %.2f) at the surface", f.w, f.x, f.y, f.z, fogTop.w, fogTop.x, fogTop.y, fogTop.z);
      CHK(a.audio.underwater > 0.99f, "audio muffled under water (%.2f)", a.audio.underwater);
      wantProbe = true; probes = 0; pendingShot = outDir() / "swim_underwater.png"; ph = 3; return false;
    }
    if (ph == 3 && probes > 0) {
      CHK(probeMean.x + probeMean.y + probeMean.z > 30 && probeMean.y > probeMean.x && probeMean.z > probeMean.x,
          "underwater frame is not black and reads blue-green: mean RGB %.0f %.0f %.0f (swim_underwater.png)", probeMean.x, probeMean.y, probeMean.z);
      return true;
    }
    return t > 6; });

  push("look pitch steers under water", [](App& a, float t) {
    static float y0, y1; static int ph;
    if (t == 0) { a.eyePitch = -45; a.scriptedInput = ExploreInput{}; a.scriptedInput.move = vec2(0, 1); y0 = Physics::character().pos.y; ph = 0; return false; }
    if (ph == 0) { if (t < 0.8f) return false; y1 = Physics::character().pos.y; a.eyePitch = 45; ph = 1; return false; }
    if (t < 1.8f) return false;
    float y2 = Physics::character().pos.y;
    a.eyePitch = 0; a.scriptedInput = ExploreInput{};
    CHK(y1 < y0 - 0.25f && y2 > y1 + 0.4f, "submerged, W looking 45 deg down sinks %.2f m in 0.8 s; looking 45 deg up rises %.2f m in 1 s", y0 - y1, y2 - y1);
    return true; });

  push("breath and drowning", [](App& a, float t) {
    static float tUnder, dealtAt29; static int ticksAt29; static bool marked;
    if (t == 0) { a.swim.breath = a.swimCfg.breath; a.swim.drownDealt = 0; a.swim.drownTicks = 0; a.scriptedInput = ExploreInput{}; tUnder = -1; marked = false; return false; }
    a.scriptedInput.crouch = true;   // stay down (on the bed)
    if (tUnder < 0) { if (a.swim.headUnder) tUnder = t; return t > 5 ? (CHK(false, "the head went under"), true) : false; }
    float u = t - tUnder;
    if (!marked && u >= a.swimCfg.breath - 1.0f) { ticksAt29 = a.swim.drownTicks; dealtAt29 = a.swim.drownDealt; marked = true; }
    if (u < a.swimCfg.breath + 2.2f) return false;
    a.scriptedInput = ExploreInput{};
    CHK(ticksAt29 == 0 && dealtAt29 == 0 && a.swim.drownTicks >= 2 && a.swim.drownDealt >= 2 * a.swimCfg.drownDps * a.swimCfg.drownTick - 0.01f && a.swim.breath <= 0,
        "%.0f s of breath: no damage after %.0f s under (%d ticks), %d drowning ticks / %.0f HP after %.1f s",
        a.swimCfg.breath, a.swimCfg.breath - 1, ticksAt29, a.swim.drownTicks, a.swim.drownDealt, u);
    return true; });

  push("surface: breath back", [](App& a, float t) {
    if (t == 0) { a.scriptedInput = ExploreInput{}; return false; }
    a.scriptedInput.jumpHeld = true;   // Space: up
    if (t < 6.0f) return false;
    a.scriptedInput = ExploreInput{};
    CHK(!a.swim.headUnder && a.swim.breath >= a.swimCfg.breath - 0.01f && a.swim.active, "Space surfaces; breath back to %.1f s within 6 s, still swimming", a.swim.breath);
    return true; });

  push("mantle out onto a bank", [](App& a, float t) {
    static int ph, m0; static float top; static vec3 base;
    if (t == 0) {
      a.scriptedInput = ExploreInput{}; a.eyeYaw = 0; a.eyePitch = 0; ph = 0; m0 = a.swim.mantles;
      auto c = Physics::character();
      base = c.pos; top = a.terrain.waterLevel + 0.3f;
      float g = a.terrain.height(base.x, base.z - 4.0f), h = top - (g - 1.0f);
      Part p; p.center = vec3(0); p.size = vec3(6, h, 5);
      bankBody = Physics::addStatic(Physics::buildShape({p}), vec3(base.x, g - 1.0f + h * 0.5f, base.z - 1.6f - 2.5f), quat(1, 0, 0, 0), 0xEEEDull << 16);   // face 1.6 m ahead (-z)
      return false;
    }
    if (ph == 0) {
      a.scriptedInput.move = vec2(0, 1);
      if (t > 1.6f) { a.scriptedInput.jumpHeld = true; a.scriptedInput.jump = true; ph = 1; }
      return false;
    }
    if (ph == 1) {
      a.scriptedInput.move = vec2(0, 1); a.scriptedInput.jumpHeld = true;
      if (a.mantle.active) ph = 2;
      return t > 6 ? (CHK(false, "mantle out started (%s; swimming %d, depth %.2f m)", a.mantle.why.c_str(), a.swim.active, a.swim.depth), true) : false;
    }
    if (ph == 2) { a.scriptedInput.jumpHeld = true; if (a.mantle.active) return false; a.scriptedInput = ExploreInput{}; ph = 3; return false; }
    if (ph == 3 && t > 0.5f) {
      auto c = Physics::character();
      if (!a.mantle.active && c.grounded) {
        CHK(a.swim.mantles == m0 + 1 && !a.swim.active && std::fabs(c.pos.y - top) < 0.15f && c.pos.z < base.z - 1.6f,
            "hold Space at a bank 0.3 m above the surface: mantles out (%s), feet %.2f m above the surface on top, %.2f m in, not swimming",
            a.swim.why.c_str(), c.pos.y - a.terrain.waterLevel, -(c.pos.z - base.z) - 1.6f);
        return true;
      }
    }
    return t > 10 ? (CHK(false, "mantle out finished standing"), true) : false; });

  // a 25 degree slope out of the water: buoyancy keeps the feet ~1.3 m under the surface, so swimming up it alone never
  // reaches standing depth; swimming forward into it (W only, no Space) climbs out (App::tryClimbBank)
  push("swim forward up a sloped bank", [](App& a, float t) {
    static float surf, prev;
    if (t == 0) {
      prev = a.terrain.waterLevel;
      if (bankBody != 0xffffffffu) { Physics::remove(bankBody); bankBody = 0xffffffffu; }
      surf = G0 + 3.0f; water(a, surf);
      const float th = radians(25.0f), zc = P0.z - 2.5f;   // the slab's top crosses the surface 2.5 m ahead of P0 (-z)
      Part p; p.center = vec3(0); p.size = vec3(6, 1, 14);
      bankBody = Physics::addStatic(Physics::buildShape({p}), vec3(P0.x, surf - 0.5f * std::cos(th), zc - 0.5f * std::sin(th)), glm::angleAxis(th, vec3(1, 0, 0)), 0xEEEDull << 16);
      Physics::setCharacterPos(vec3(P0.x, surf - a.swimCfg.floatDepth, P0.z + 3.0f));
      a.scriptedInput = ExploreInput{}; a.eyeYaw = 0; a.eyePitch = 0;
      return false;
    }
    a.scriptedInput.move = vec2(0, t > 0.8f ? 1.0f : 0.0f);
    auto c = Physics::character();
    if (t > 1.5f && !a.swim.active && !a.mantle.active && c.grounded) {
      a.scriptedInput = ExploreInput{}; water(a, prev);
      CHK(c.pos.y > surf - a.swimCfg.exitDepth && c.pos.z < P0.z, "W into a 25 degree bank climbs out (%s, %s): feet %.2f m under the surface, standing", a.swim.why.c_str(), a.mantle.why.c_str(), surf - c.pos.y);
      return true;
    }
    if (t > 12) { a.scriptedInput = ExploreInput{}; water(a, prev); CHK(false, "W into a 25 degree bank climbs out (stuck: swimming %d, depth %.2f m, %s)", a.swim.active, a.swim.depth, a.mantle.why.c_str()); return true; }
    return false; });

  push("bot floats", [](App& a, float t) {
    if (t < 0.2f) return false;
    if (bankBody != 0xffffffffu) { Physics::remove(bankBody); bankBody = 0xffffffffu; }
    vec3 p(30, 0, 30); float g = a.terrain.height(p.x, p.z); p.y = g + 0.05f;
    int h = Physics::createAgent(p);
    int sw = 0;
    for (int k = 0; k < 240; ++k) { Physics::updateAgent(h, 1.0f / 60, vec3(0), false, Physics::STAND, 0); Physics::step(1.0f / 60); sw += Physics::agent(h).swim; }
    auto s = Physics::agent(h);
    float want = a.terrain.waterLevel - a.swimCfg.floatDepth;
    CHK(s.swim && sw > 200 && std::fabs(s.pos.y - want) < 0.2f && s.pos.y > g + 2.0f,
        "a bot agent on the bed of 4 m of water floats: feet %.2f m above the bed, %.2f m under the surface (float depth %.2f), swimming %d / 240 steps", s.pos.y - g, a.terrain.waterLevel - s.pos.y, a.swimCfg.floatDepth, sw);
    Physics::destroyAgent(h);
    return true; });

  push("navmesh refuses deep local water", [](App& a, float t) {
    if (t < 0.1f) return false;
    water(a, -1000);   // the global level is off: only the query's pond counts
    const vec2 C(100, 100); const float R = 10;
    auto pond = [&a, C, R](float x, float z) { return glm::length(vec2(x, z) - C) < R ? a.terrain.height(C.x, C.y) + 3.0f : -1e9f; };
    Nav* nav = new Nav();
    nav->classify = [](uint64_t) { return (int)NT_SOLID; };
    vec3 lo(C.x - 30, -100, C.y - 30), hi(C.x + 30, 100, C.y + 30);
    auto at = [&](float x, float z) { return vec3(x, a.terrain.height(x, z), z); };
    vec3 mid = at(C.x, C.y), A = at(C.x - 20, C.y), Bp = at(C.x + 20, C.y);
    nav->build(lo, hi, 2);
    int dryMid = nav->nearest(mid, 1.0f);
    nav->waterQuery = pond;
    nav->build(lo, hi, 2);
    int wetMid = nav->nearest(mid, 1.0f);
    std::vector<vec3> pts; float len = 0;
    bool path = nav->pathSync(A, Bp, pts, nullptr, nullptr, &len);
    float closest = 1e9f; for (auto& q : pts) closest = std::min(closest, glm::length(vec2(q.x, q.z) - C));
    for (size_t k = 1; k < pts.size(); ++k) {   // segments too: the closest approach of the polyline
      vec2 p0(pts[k - 1].x, pts[k - 1].z), p1(pts[k].x, pts[k].z), d = p1 - p0; float l2 = glm::dot(d, d);
      float u = l2 > 1e-6f ? std::clamp(glm::dot(C - p0, d) / l2, 0.0f, 1.0f) : 0.0f; closest = std::min(closest, glm::length(p0 + d * u - C));
    }
    float tr = 1; bool hitR = nav->raycastMesh(A, Bp, tr);
    CHK(dryMid == 0 && wetMid == -1, "pond from the water query (3 m deep, r %.0f m, global level off): no walkable polygon at its centre (%d; without the query %d)", R, wetMid, dryMid);
    CHK(path && closest > R - 1.0f && len > 41.0f, "path across goes round it: %zu points, %.1f m (straight 40 m), closest %.1f m from the centre", pts.size(), len, closest);
    CHK(!hitR || tr < 1.0f, "a straight walk across the pond is blocked (t %.2f)", tr);
    delete nav;
    water(a, G0 + 4.0f);
    return true; });

  push("reset", [](App& a, float) { water(a, -1000); a.exitExplore(); CHK(a.mode == Mode::Creative && a.audio.underwater == 0, "back to Creative, audio not muffled"); return true; });
}
}  // namespace

bool swimSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-swim") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) {   // post-render
    if (wantProbe) {
      std::vector<uint8_t> px; int w = 0, h = 0;
      if (app.R.readPixels(px, w, h) && w > 0 && h > 0) {
        double s[3] = {0, 0, 0}; size_t n = (size_t)w * h;
        for (size_t i = 0; i < n; ++i) for (int c = 0; c < 3; ++c) s[c] += px[i * 4 + c];
        probeMean = vec3((float)(s[0] / n), (float)(s[1] / n), (float)(s[2] / n)); ++probes;
      }
      wantProbe = false;
    }
    if (!pendingShot.empty()) { app.screenshot(pendingShot); pendingShot.clear(); }
    return true;
  }
  if (!started) { started = true; T = new MpTest("swim"); build(app); cur = 0; st = 0; }
  if (cur < seq.size()) {
    if (st == 0) logf("[selftest] swim step: %s", seq[cur].name.c_str());
    bool done = seq[cur].fn(app, st);
    st += dt; total += dt;
    if (done) { ++cur; st = 0; }
    if (total > 240) { T->check(false, "swim test timed out in step '" + seq[std::min(cur, seq.size() - 1)].name + "'"); cur = seq.size(); }
  }
  if (cur >= seq.size() && !app.selftest->done) { app.selftest->exitCode = T->finish(); app.selftest->done = true; }
  return true;
}
