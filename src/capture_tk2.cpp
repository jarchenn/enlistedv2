// TK2 --capture-tk2 <tag>: close-ups of every tank from 3 m (front three-quarter, side, tracks; selftest_out/tk2_<tag>_<id>_<view>.png,
// scene only) and a frame-time sample on a medium map (chateau_town, 350 x 400 m): one tank 3 m from the camera, then
// five tanks in view at 30-100 m ([tk2] lines: average / p95 frame ms over 4 s after a 3 s warm-up). Run it before and
// after a re-bake with the GPU exclusive (tools/gpu_turn.sh).
#include "selftest.h"

namespace {
const char* kTanks[] = {"tank_tiger_i", "tank_panzer_iv", "tank_sherman_m4", "tank_sherman_firefly", "tank_abrams_m1a2"};
const int kN = sizeof kTanks / sizeof kTanks[0];
std::string tag = "x"; int phase = 0, idx = 0, view = 0; float t = 0; fs::path pend; std::vector<uint64_t> placed;
std::vector<float> frames, gpu; std::vector<int> tris;
fs::path out(const std::string& n) { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p / ("tk2_" + tag + "_" + n + ".png"); }
void look(App& a, vec3 eye, vec3 at) { vec3 d = at - eye; a.camPos = eye; a.camYaw = degrees(std::atan2(-d.x, -d.z)); a.camPitch = degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z)))); }
void clear(App& a) { for (uint64_t id : placed) a.doc.instances.erase(std::remove_if(a.doc.instances.begin(), a.doc.instances.end(), [&](const Instance& i) { return i.id == id; }), a.doc.instances.end()); placed.clear(); a.syncRuntime(); a.tanks.sync(a); }
void report(const char* what) {
  if (frames.empty()) return;
  auto stat = [](std::vector<float> s, double& avg, double& p95) { std::sort(s.begin(), s.end()); avg = 0; for (float f : s) avg += f; avg /= s.size(); p95 = s[(size_t)(s.size() * 0.95)]; };
  double ca, cp, ga, gp; stat(frames, ca, cp); stat(gpu, ga, gp);
  double tr = 0; for (int x : tris) tr += x; tr /= std::max<size_t>(1, tris.size());
  logf("[tk2] %s: GPU avg %.2f ms (p95 %.2f), CPU render avg %.2f ms (p95 %.2f), %.0f k triangles drawn (%zu frames)", what, ga, gp, ca, cp, tr / 1000, frames.size());
  frames.clear(); gpu.clear(); tris.clear();
}
}

bool captureTk2SelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-tk2") { mode = 1; if (k + 1 < __argc) tag = __argv[k + 1]; } }
  if (mode != 1) return false;
  if (dt < 0) { if (!pend.empty()) { a.screenshot(pend); pend.clear(); } return true; }
  t += dt;
  auto done = [&]() { a.selftest->exitCode = 0; a.selftest->done = true; return true; };
  if (phase == 0) { a.newMap(*a.findTemplate("blank_slate"), "TK2 close-ups"); phase = 1; idx = 0; view = 0; t = 0; return true; }
  if (phase == 1) {   // one tank at a time, three views
    if (idx >= kN) { clear(a); phase = 7; t = 0; return true; }
    if (placed.empty()) {
      const AssetDef* as = a.lib.find(kTanks[idx]);
      if (!as) { logf("[tk2] no asset %s", kTanks[idx]); ++idx; return true; }
      placed.push_back(a.placeInstance(as, vec3(0, a.terrain.height(0, 0), 0), 0)); a.syncRuntime(); a.tanks.sync(a); t = 0; view = 0;
    }
    const TankRT* tk = a.tanks.get(placed[0]);
    if (!tk) { clear(a); ++idx; return true; }
    float hx = 0, hz = 0, top = 0; for (auto& b : tk->def->hullBoxes) { hx = std::max(hx, std::fabs(b.c.x) + b.h.x); hz = std::max(hz, std::fabs(b.c.z) + b.h.z); top = std::max(top, b.c.y + b.h.y); }
    vec3 c = tk->pos + vec3(0, top * 0.6f, 0);
    if (view == 0) look(a, tk->pos + vec3(hx + 2.1f, top + 0.9f, hz + 2.1f), c);                      // front three-quarter, ~3 m off the corner
    else if (view == 1) look(a, tk->pos + vec3(hx + 3.0f, top * 0.7f, 0), c);                          // side
    else look(a, tk->pos + vec3(hx + 1.8f, 1.9f, -hz * 0.35f), tk->pos + vec3(hx * 0.85f, 0.55f, hz * 0.25f));   // the running gear: a high three-quarter view along the track
    if (t > 0.6f && pend.empty()) {
      static const char* vn[3] = {"front34", "side", "tracks"};
      pend = out(std::string(tk->def->id) + "_" + vn[view]);
      if (view == 2) { logf("[tk2] %s tracks: %zu grass boxes, first (%.1f %.1f %.2f %.2f | %.2f %.2f), tank at (%.1f %.1f)", tk->def->id.c_str(), a.R.grassVeh.size() / 2, a.R.grassVeh.empty() ? 0.f : a.R.grassVeh[0].x, a.R.grassVeh.empty() ? 0.f : a.R.grassVeh[0].y, a.R.grassVeh.empty() ? 0.f : a.R.grassVeh[0].z, a.R.grassVeh.empty() ? 0.f : a.R.grassVeh[0].w, a.R.grassVeh.size() > 1 ? a.R.grassVeh[1].x : 0.f, a.R.grassVeh.size() > 1 ? a.R.grassVeh[1].y : 0.f, tk->pos.x, tk->pos.z);
}
      if (++view == 3) { clear(a); ++idx; }
      t = 0;
    }
    return true;
  }
  if (phase == 7) {   // TK3: the same Tiger from the player's own eye in Explore, 3 m from its tracks
    static uint64_t tg = 0;
    if (t < dt * 1.5f) { tg = a.placeInstance(a.lib.find("tank_tiger_i"), vec3(0, a.terrain.height(0, 0), 0), 0); a.syncRuntime(); a.tanks.sync(a); a.enterExplore(); a.useScripted = true; a.scriptedInput = ExploreInput{}; }
    if (t > 0.5f && t < 0.5f + dt * 1.5f) { vec3 p(4.9f, 0, 1.5f); p.y = a.terrain.height(p.x, p.z); Physics::setCharacterPos(p); vec3 to = vec3(1.6f, 0.6f, 0) - (p + vec3(0, 1.62f, 0)); a.eyeYaw = degrees(std::atan2(-to.x, -to.z)); a.eyePitch = degrees(std::atan2(to.y, glm::length(vec2(to.x, to.z)))); }
    if (t > 2.0f && t < 2.0f + dt * 1.5f) a.uiShot = out("explore_tiger_tracks");
    if (t > 2.4f) { if (a.mode == Mode::Explore) a.exitExplore(); clear(a); (void)tg; phase = 2; t = 0; }
    return true;
  }
  if (phase == 2) {   // the medium map, one tank close
    const TemplateInfo* tp = a.findTemplate("chateau_town");
    if (!tp) { logf("[tk2] no chateau_town"); return done(); }
    a.newMap(*tp, "TK2 frame time"); a.camPos = vec3(0, a.terrain.height(0, 60) + 3, 60); a.camYaw = 0; a.camPitch = -5;   // the map centre, looking along -z
    vec3 f = App::forward(a.camYaw, 0), r = glm::normalize(glm::cross(f, vec3(0, 1, 0)));
    vec3 p = a.camPos + f * 7.0f; p.y = a.terrain.height(p.x, p.z);
    placed.push_back(a.placeInstance(a.lib.find(kTanks[1]), p, 0)); a.syncRuntime(); a.tanks.sync(a);
    if (const TankRT* tk = a.tanks.get(placed.back())) look(a, tk->pos + r * 4.5f + vec3(0, 2.2f, 0), tk->pos + vec3(0, 1.2f, 0));
    phase = 3; t = 0; return true;
  }
  auto sample = [&]() { frames.push_back((float)a.R.stats.cpuMs); gpu.push_back((float)a.R.stats.gpuMs); tris.push_back(a.R.stats.tris); };
  if (phase == 3) { if (t > 3.0f) sample(); if (t > 7.0f) { report("one Panzer IV at 3 m"); pend = out("frame_close"); phase = 4; t = 0; } return true; }
  if (phase == 4) {   // five tanks at 30-100 m in front of the camera
    clear(a);
    a.camPos = vec3(0, a.terrain.height(0, 60) + 3, 60); a.camYaw = 0; a.camPitch = -3;
    vec3 f = App::forward(a.camYaw, 0), r = glm::normalize(glm::cross(f, vec3(0, 1, 0)));
    for (int k = 0; k < 5; ++k) {
      const AssetDef* as = a.lib.find(kTanks[k]); if (!as) continue;
      for (int tr = 0; tr < 24; ++tr) {   // the first free spot along that distance band, either side of the view line
        float d = 30.0f + k * 17.5f + (tr / 8) * 4.0f, side = (tr % 8 < 4 ? 1.0f : -1.0f) * (3.0f + (tr % 4) * 6.0f);
        vec3 p = a.camPos + f * d + r * side; p.y = a.terrain.height(p.x, p.z);
        if (!a.checkPlacement(as, p, 30.0f * k, 0).ok) continue;
        if (uint64_t id = a.placeInstance(as, p, 30.0f * k)) { placed.push_back(id); break; }
      }
    }
    a.syncRuntime(); a.tanks.sync(a);
    logf("[tk2] %zu tanks placed for the far sample", placed.size());
    phase = 5; t = 0; return true;
  }
  if (phase == 5) { if (t > 3.0f) sample(); if (t > 7.0f) { report("five tanks at 30-100 m"); pend = out("frame_far"); phase = 6; t = 0; } return true; }
  if (t > 0.3f) return done();
  return true;
}
