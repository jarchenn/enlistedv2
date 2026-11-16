// --capture-props: no suite, close-up shots of props (PR1): each asset of NMK_PROPS (ids separated by ";"; an entry
// "id@x,y,z" aims at that asset-space point instead of the bounds' centre, e.g. one fuel dispenser of the petrol
// station) placed alone on a blank map by day, shot from 1.5 m and 4 m at standing eye height (1.62 m), 35 degrees off
// its +z face, into selftest_out/prop_<k>_<n|f>.png; selftest_out/props.txt lists them (tools/props/sheet.py stitches
// the labelled contact sheet). The editor camera, no UI.
#include "selftest.h"
#include "settings.h"
#include <sstream>
#include <fstream>

namespace {
struct Shot { std::string id; bool aim = false; vec3 at{0}; uint64_t inst = 0; };
std::vector<Shot> gShots; int gK = 0; float gT = 0; bool gSetup = false;
fs::path out(const std::string& f) { return exeDir() / "selftest_out" / f; }
}

bool capturePropsSelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-props") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  auto finish = [&](int code) { if (!a.selftest->done) { a.selftest->exitCode = code; a.selftest->done = true; } return true; };
  if (!gSetup) {
    gSetup = true;
    a.newMap(*a.findTemplate("blank_slate"), "Prop capture");
    std::stringstream ss(std::getenv("NMK_PROPS") ? std::getenv("NMK_PROPS") : "mod_trash_bin");
    std::string tok; int n = 0;
    while (std::getline(ss, tok, ';')) {
      if (tok.empty()) continue;
      Shot s; size_t p = tok.find('@'); s.id = tok.substr(0, p);
      if (p != std::string::npos) { s.aim = true; std::sscanf(tok.c_str() + p + 1, "%f,%f,%f", &s.at.x, &s.at.y, &s.at.z); }
      const AssetDef* as = a.lib.find(s.id);
      if (!as) { logf("[capture-props] no asset %s", s.id.c_str()); continue; }
      float x = -175.0f + (n % 8) * 50.0f, z = -175.0f + (n / 8) * 50.0f; ++n;   // a grid inside the blank map
      s.inst = a.placeInstance(as, vec3(x, a.terrain.height(x, z), z), 0);
      if (!s.inst) { logf("[capture-props] %s could not be placed", s.id.c_str()); continue; }
      gShots.push_back(s);
    }
    a.syncRuntime();
    Settings& S = settings(); S.s.hour = 14.0f; S.apply(a);
    if (a.mode == Mode::Explore) a.exitExplore();
    a.hideUi = true; gT = 0;
    std::ofstream f(out("props.txt")); for (auto& s : gShots) f << s.id << "\n";
    return true;
  }
  if (gK >= (int)gShots.size() * 2) { a.hideUi = false; logf("[capture-props] %d shots", gK); return finish(0); }
  Shot& s = gShots[gK / 2]; bool far = gK % 2;
  Instance* inst = a.doc.find(s.inst); auto rt = a.rt.find(s.inst);
  if (inst && rt != a.rt.end() && rt->second.asset) {
    const AssetDef& as = *rt->second.asset;
    vec3 c = s.aim ? s.at : (as.bmin + as.bmax) * 0.5f;
    vec3 tgt = inst->pos + yawQuat(inst->yaw) * c;
    float d = far ? 4.0f : 1.5f, ang = glm::radians(35.0f);
    vec3 dir = yawQuat(inst->yaw) * vec3(std::sin(ang), 0, std::cos(ang));
    vec3 eye = tgt + dir * (d + (s.aim ? 0.0f : 0.5f * std::max(as.bmax.x - as.bmin.x, as.bmax.z - as.bmin.z) * 0.5f));
    eye.y = inst->pos.y + 1.62f;
    vec3 v = tgt - eye;
    a.camPos = eye; a.camYaw = glm::degrees(std::atan2(-v.x, -v.z)); a.camPitch = glm::degrees(std::atan2(v.y, glm::length(vec2(v.x, v.z))));
  }
  gT += dt;
  if (gT > 1.2f && gT < 1.2f + dt * 1.5f) a.uiShot = out("prop_" + std::to_string(gK / 2) + (far ? "_f" : "_n") + ".png");
  if (gT > 1.5f) { ++gK; gT = 0; }
  return true;
}
