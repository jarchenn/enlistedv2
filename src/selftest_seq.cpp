// KR12 --capture-seq [--seq-weapon <id>] (the user: "ensure the animation you send is exactly how I see it in game"): the
// game itself plays one sequence on a blank map and every other frame (the selftest's fixed 1/60 s: 30 fps) is written to
// selftest_out/seq_<id>_<n>.png with the UI, for an mp4: idle 1 s, a hip shot, 1.9 s (the bolt cycle), the reload with two
// rounds out (the partial reload), the raise to the sights, a shot on the sights, 2 s, down, idle 1.2 s.
#include "selftest.h"
#include "loot.h"   // ATT_SIGHT

namespace {
int phase = 0, frame = 0; float t = 0, tReload = -1, tAim = -1, tDown = -1; bool fired1 = false, fired2 = false; fs::path pend;
std::string wid = "kar98k";
fs::path out(int n) { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); char b[64]; std::snprintf(b, sizeof b, "seq_%s_%04d.png", wid.c_str(), n); return p / b; }
}

bool seqCapture(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) {
    mode = 0;
    for (int k = 1; k < __argc; ++k) {
      if (std::string(__argv[k]) == "--capture-seq") mode = 1;
      if (std::string(__argv[k]) == "--seq-weapon" && k + 1 < __argc) wid = __argv[k + 1];
    }
  }
  if (mode != 1) return false;
  if (dt < 0) { if (!pend.empty()) { a.screenshot(pend); pend.clear(); } return true; }
  ExploreInput& in = a.scriptedInput;
  if (phase == 0) {
    a.newMap(*a.findTemplate("blank_slate"), "Sequence capture");
    a.syncRuntime(); a.enterExplore(); a.useScripted = true; a.eyeYaw = 0; a.eyePitch = 2;
    int w = a.weaponIndex(wid);
    if (w < 0) { logf("[seq] no weapon %s", wid.c_str()); a.selftest->exitCode = 1; a.selftest->done = true; return true; }
    a.carried = {w}; a.selectWeapon(w);
    WeaponState& s = a.wstate[wid]; s.loaded = a.weaponDefs[w]->cap; s.cooldown = 0; s.bolt = 0; s.reload = 0; s.att[ATT_SIGHT] = 0;
    phase = 1; t = 0; frame = 0; return true;
  }
  t += dt; ++frame;
  const WeaponDef& d = *a.weaponDefs[a.activeWeapon]; WeaponState& s = a.wstate[d.id];
  in = ExploreInput{};
  const float T0 = 1.5f;   // settle (the map and the rig load), then idle 1 s
  float tt = t - T0;
  if (tt >= 0 && frame % 2 == 0) a.uiShot = out(frame / 2);
  if (tt >= 1.0f && !fired1) { in.fire = true; fired1 = true; logf("[seq] %s hip shot at %.3f s", wid.c_str(), tt); }
  if (fired1 && tReload < 0 && tt >= 2.9f) { s.loaded = std::max(0, d.cap - 2); in.reload = true; tReload = tt; logf("[seq] reload (2 rounds out) at %.3f s", tt); }
  if (tReload >= 0 && tAim < 0 && tt > tReload + 0.3f && s.reload <= 0) { tAim = tt; logf("[seq] reload done, aim at %.3f s", tt); }
  if (tAim >= 0 && tDown < 0) in.aim = true;
  if (tAim >= 0 && !fired2 && tt >= tAim + 0.9f) { in.fire = true; fired2 = true; logf("[seq] aimed shot at %.3f s", tt); }
  if (fired2 && tDown < 0 && tt >= tAim + 0.9f + 2.0f) { tDown = tt; logf("[seq] down at %.3f s", tt); }
  if (tDown >= 0 && tt >= tDown + 1.2f) {
    logf("[seq] done: %d frames (30 fps) as selftest_out/seq_%s_NNNN.png", frame / 2, wid.c_str());
    if (a.mode == Mode::Explore) a.exitExplore(); a.selftest->exitCode = 0; a.selftest->done = true;
  }
  return true;
}
