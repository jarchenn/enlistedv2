// CAPE --capture-enemy [zombie.json type id] (default knight10; the user: "send an animation of a knight 10 in game"): one enemy of
// that type in the spawn-egg sandbox on the blank map, the editor camera beside it above the grass, no UI. Every other frame
// (the selftest's fixed 1/60 s: 30 fps) goes to selftest_out/enemy_<id>_NNNN.png for an mp4.
//   1 idle (the AI held) 3 s
//   2 the AI let go: it runs at a zombie 24 m off and strikes it (the zombie kept alive for three blows, then left to die)
//   3 a hit hard enough to stagger it (its hit reaction)
//   4 its death, and 4 s of the body
// The log lines "[cape] ..." say when each thing happened.
// SG3 probe: NMK_CAPE_PROBE=<still number> (e.g. 420): at that still the game, the AI and the animation clock stop, the camera
// holds, and the same view is rendered once per contributor switched off (12 frames each, so TAA / exposure settle), saved as
// selftest_out/probe_<id>_<NN>_<label>.png; then the run ends. NMK_CAPE_HOUR=<h> pins the hour (with NMK_WEATHER pinning the
// weather: one fixed light for every still).
#include "selftest.h"
#include "game.h"
#include "ai_core.h"
#include "enemy_types.h"
#include "scenario.h"

namespace {
int phase = 0, frame = 0, me = -1, z1 = -1, blows = 0; float t = 0, tGo = -1, tDown = -1, tHit = -1, tDie = -1; bool wasWind = false; fs::path pend; vec3 camP(0), look(0), deadAt(0);
std::string id = "knight10";
int probeAt = -2, probeI = -1, probeF = 0; float savedScale = 1.0f;
struct ProbeCfg { const char* label; void (*set)(App&, bool off); };
const ProbeCfg kProbe[] = {
  {"00_base", [](App&, bool) {}},
  {"01_no_sun_disc", [](App& a, bool off) { a.R.lt.sunDisc = off ? 0.0f : 1.0f; a.R.refreshReflections(); }},
  {"02_no_fog", [](App& a, bool off) { a.R.dbgOff = off ? (a.R.dbgOff | 2) : (a.R.dbgOff & ~2); }},
  {"04_no_bloom", [](App& a, bool off) { a.R.q.bloom = !off; }},
  {"05_no_shafts", [](App& a, bool off) { a.R.q.shafts = !off; }},
  {"06_no_taa", [](App& a, bool off) { a.R.q.taa = !off; a.R.resetTemporal(); }},
  {"07_no_froxel_fog", [](App& a, bool off) { a.R.perf.froxel = !off; }},
  {"08_no_effects", [](App& a, bool off) { a.R.dbgOff = off ? (a.R.dbgOff | 4) : (a.R.dbgOff & ~4); }},
  {"09_no_clouds", [](App& a, bool off) { a.R.perf.clouds = !off; }},
  {"10_no_ssr_ssao", [](App& a, bool off) { a.R.q.ssr = !off; a.R.q.ssao = !off; }},
  {"12_no_env_reflection", [](App& a, bool off) { a.R.dbgOff = off ? (a.R.dbgOff | 8) : (a.R.dbgOff & ~8); }},
  {"13_no_refl_cube", [](App& a, bool off) { a.R.perf.reflProbe = !off; }},
  {"15_no_moon", [](App& a, bool off) { a.R.dbgOff = off ? (a.R.dbgOff | 32) : (a.R.dbgOff & ~32); }},
  {"16_no_vsskin", [](App& a, bool off) { a.R.perf.vsSkin = !off; if (gGame) for (auto& s : gGame->sold) s.ciReady = false; }},   // instances are made skinned one way or the other at creation: all made again
  {"18_sky_first", [](App& a, bool off) { a.R.perf.skyLast = !off; }},   // the sky before the world (skylast off): characters then overwrite it whatever their depth
  {"19_no_local_lights", [](App& a, bool off) { a.R.dbgOff = off ? (a.R.dbgOff | 64) : (a.R.dbgOff & ~64); }},
  {"20_no_headlamp", [](App& a, bool off) { static bool was = false; if (off) { was = a.flashlight; a.flashlight = false; } else a.flashlight = was; }},
  {"21_no_particles", [](App& a, bool off) { if (off) { a.fx.parts.clear(); a.fx.streaks.clear(); a.fx.debris.clear(); } a.R.dbgOff = off ? (a.R.dbgOff | 4) : (a.R.dbgOff & ~4); }},
  {"22_no_zombie", [](App&, bool off) { if (gGame && z1 >= 0) gGame->sold[z1].dbgHide = off; }},
  {"23_no_knight", [](App&, bool off) { if (gGame && me >= 0) gGame->sold[me].dbgHide = off; }},
  {"17_no_sky_pass", [](App& a, bool off) { a.R.dbgOff = off ? (a.R.dbgOff | 16) : (a.R.dbgOff & ~16); }},
  {"11_native_res", [](App& a, bool off) { a.R.setResolutionScale(off ? 1.0f : savedScale); a.R.resetTemporal(); }},
};
fs::path probeOut(const char* label) { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p / ("probe_" + id + "_" + label + ".png"); }
fs::path out(int n) { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); char b[96]; std::snprintf(b, sizeof b, "enemy_%s_%04d.png", id.c_str(), n); return p / b; }
}

bool enemyCapture(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-enemy") { mode = 1; if (k + 1 < __argc && __argv[k + 1][0] != '-') id = __argv[k + 1]; } }
  if (mode != 1) return false;
  if (dt < 0) { if (!pend.empty()) { a.screenshot(pend); pend.clear(); } return true; }
  auto fail = [&](const char* why) { logf("[cape] FAIL %s", why); a.selftest->exitCode = 1; a.selftest->done = true; return true; };
  if (phase == 0) { a.newMap(*a.findTemplate("blank_slate"), "Enemy capture"); a.syncRuntime(); phase = 1; t = 0; return true; }
  t += dt; ++frame;
  AiCore* A = aiCore();
  if (phase == 1) {
    if (t < 1.0f) return true;
    const int et = enemyTypeIndex(id); if (et < 0) return fail("no such enemy type");
    if (!gGame) gGame = new Game(a);
    std::string err; if (!gGame->sandboxBegin(err)) return fail(err.c_str());
    me = gGame->sandboxAdd(AiType::Zombies, et, 0, vec3(0, a.terrain.height(0, 0), 0), 180.0f);
    z1 = gGame->sandboxAdd(AiType::Zombies, 0, 1, vec3(0, a.terrain.height(0, 24), 24), 0.0f);
    if (me < 0 || z1 < 0) return fail("spawn");
    if (A) A->frozen = true;
    const Soldier& s = gGame->sold[me]; logf("[cape] %s: slot %d, model %s, %.0f health; zombie slot %d", id.c_str(), me, gGame->models[s.model]->id.c_str(), s.hp, z1);
    phase = 2; t = 0; frame = 0; return true;
  }
  Game& G = *gGame; if (!G.active) return fail("the sandbox ended");
  Soldier& s = G.sold[me]; Soldier& z = G.sold[z1]; const EnemyType& e = enemyType(s.etype);
  // the camera: ahead of it and to its side, at chest height (above the grass), near enough that it fills most of the frame
  const float H = e.height * e.scale; const vec3 at = (s.alive ? s.feet : deadAt) + vec3(0, 0.52f * H, 0);
  vec3 f = z.feet - s.feet; f.y = 0; f = glm::length(f) > 0.5f && tDown < 0 ? glm::normalize(f) : vec3(0, 0, 1); const vec3 r(-f.z, 0, f.x);
  const vec3 wantP = at + (r * 0.78f + f * 0.62f) * (1.75f * H) + vec3(0, 0.12f * H, 0);
  camP = frame < 3 ? wantP : glm::mix(camP, wantP, 0.05f); look = frame < 3 ? at : glm::mix(look, at, 0.12f);
  const vec3 d = glm::normalize(look - camP); a.camPos = camP; a.camYaw = degrees(std::atan2(-d.x, -d.z)); a.camPitch = degrees(std::asin(d.y));
  if (probeAt == -2) { const char* e = std::getenv("NMK_CAPE_PROBE"); probeAt = e ? std::atoi(e) : -1; }
  if (const char* e = std::getenv("NMK_CAPE_HOUR")) { a.R.lt.hour = (float)std::atof(e); a.R.lt.userHour = true; }
  if (probeAt >= 0 && frame / 2 >= probeAt) {   // SG3: the elimination stills
    G.paused = true; a.R.holdTime = true; if (A) A->frozen = true;
    const int n = (int)(sizeof kProbe / sizeof kProbe[0]);
    if (probeI < 0) { probeI = 0; probeF = 0; savedScale = a.R.resolutionScale(); logf("[cape] probe at still %d (hour %.2f, weather %d): %d views", probeAt, a.R.lt.hour, a.R.wx.want(), n); kProbe[0].set(a, true); }
    if (++probeF == 12) pend = probeOut(kProbe[probeI].label);
    if (probeI == 0 && probeF == 13) {   // SG6: locate the bright thing in the base still: the brightest 40 px block, and the point the user saw (1015, 455 of 1920 x 1080)
      std::vector<uint8_t> px; int w = 0, h = 0; a.R.readPixels(px, w, h);
      float best = -1; int bx = 0, by = 0;
      for (int y = 0; y + 40 <= h; y += 10) for (int x = 0; x + 40 <= w; x += 10) {
        float s = 0; for (int j = 0; j < 40; j += 4) for (int i = 0; i < 40; i += 4) { const uint8_t* q = &px[((size_t)(y + j) * w + x + i) * 4]; s += 0.2126f * q[0] + 0.7152f * q[1] + 0.0722f * q[2]; }
        if (s > best) { best = s; bx = x + 20; by = y + 20; }
      }
      const struct { const char* what; float u, v; } at[] = {{"brightest block", (bx + 0.5f) / w, (by + 0.5f) / h}, {"the reported point", 1015.5f / 1920.0f, 1.0f - 455.5f / 1080.0f}};
      for (auto& q : at) {
        float z = 0; const vec3 W = a.R.probeWorldAt(q.u, q.v, z);
        logf("[cape] probe locate %s: screen %.0f, %.0f (from the top) of %d x %d, mean sRGB %.0f, depth %.6f -> %s", q.what, q.u * w, (1.0f - q.v) * h, w, h, best / 100.0f, z,
             z <= 0.0f ? "the sky (nothing drawn there)" : (std::string("world (") + std::to_string(W.x) + ", " + std::to_string(W.y) + ", " + std::to_string(W.z) + "), " + std::to_string(glm::length(W - a.camPos)) + " m from the camera").c_str());
        if (z <= 0.0f) continue;
        for (auto& L : a.R.lights) if (glm::length(L.pos - W) < 3.0f) logf("[cape]   light id %u at %.2f m: radius %.1f intensity %.2f colour %.2f %.2f %.2f %s", L.id, glm::length(L.pos - W), L.radius, L.intensity, L.color.r, L.color.g, L.color.b, L.cosOuter > -1.0f ? "spot" : "point");
        int np = 0; float maxS = 0; for (auto& P : a.fx.parts) if (glm::length(P.p - W) < 3.0f) { ++np; maxS = std::max(maxS, P.size); }
        if (np) logf("[cape]   %d particles within 3 m (largest %.2f m)", np, maxS);
        for (size_t k = 0; k < G.sold.size(); ++k) if (G.sold[k].inUse) { float d = glm::length(G.sold[k].feet + vec3(0, 1.0f, 0) - W); if (d < 3.0f) logf("[cape]   soldier %zu (%s) at %.2f m, alive %d", k, (int)k == me ? "the knight" : (int)k == z1 ? "the zombie" : "other", d, (int)G.sold[k].alive); }
      }
      logf("[cape] probe locate: %zu lights this frame (headlamp %s), %zu particles, camera (%.2f, %.2f, %.2f)", a.R.lights.size(), a.flashlight ? "on" : "off", a.fx.parts.size(), a.camPos.x, a.camPos.y, a.camPos.z);
    }
    if (probeF > 12) {
      logf("[cape] probe %s -> %s", kProbe[probeI].label, probeOut(kProbe[probeI].label).string().c_str());
      kProbe[probeI].set(a, false);
      if (++probeI >= n) { logf("[cape] probe done"); a.selftest->exitCode = 0; a.selftest->done = true; return true; }
      kProbe[probeI].set(a, true); probeF = 0;
    }
    return true;
  }
  if (frame % 2 == 0) pend = out(frame / 2);
  if (s.alive) deadAt = s.feet;
  if (phase == 2) {   // 1: idle
    if (t > 3.0f) { if (A) A->frozen = false; tGo = t; phase = 3; logf("[cape] %.2f s: the AI let go (the zombie %.1f m off)", t, glm::length(z.feet - s.feet)); }
    return true;
  }
  if (phase == 3) {   // 2: the run and the blows
    s.hp = std::max(s.hp, 0.8f * e.hp);
    const bool wind = s.meleeWind >= 0; if (wasWind && !wind) { ++blows; logf("[cape] %.2f s: blow %d done (the zombie %.0f health)", t, blows, z.hp); } wasWind = wind;
    if (z.alive && blows < 3) z.hp = std::max(z.hp, 2000.0f); else if (z.alive && z.hp > 30.0f) z.hp = 30.0f;
    if ((!z.alive && tDown < 0) || t > tGo + 22.0f) { tDown = t; phase = 4; if (A) A->frozen = true; logf("[cape] %.2f s: the zombie is down after %d blows; the AI held", t, blows); }
    return true;
  }
  if (phase == 4) {   // 3: the hit reaction, 4: the death
    if (tHit < 0 && t > tDown + 1.2f) { tHit = t; const float dmg = std::max(e.staggerMin + 5.0f, 40.0f); s.hp = std::max(s.hp, dmg + 50.0f); G.damage(me, dmg, -1, vec3(0, 0, -1), "upper torso"); logf("[cape] %.2f s: hit for %.0f (staggers at %.0f): %s", t, dmg, e.staggerMin, G.t < s.staggerUntil ? "staggered" : "no stagger"); }
    if (tHit >= 0 && tDie < 0 && t > tHit + 2.2f) { tDie = t; G.damage(me, 100000.0f, -1, vec3(0, 0, -1), "upper torso"); logf("[cape] %.2f s: killed (alive %d)", t, (int)s.alive); }
    if (tDie >= 0 && t > tDie + 4.5f) { logf("[cape] done: %d frames (30 fps) as selftest_out/enemy_%s_NNNN.png", frame / 2, id.c_str()); a.selftest->exitCode = 0; a.selftest->done = true; }
  }
  return true;
}
