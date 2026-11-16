// WP4 --capture-recoil: no suite, a look at the visual recoil and the muzzle flash (selftest_out/recoil_*.png). On a
// blank map, for a rifle, an SMG, a pistol, a shotgun and a scoped sniper: one shot from the hip and one aimed (ADS),
// each frame taken two fixed steps (~33 ms) after the shot, at the kick's peak with the flash up; the spring values
// logged. Then one Garand shot in a Tunnels corridor (the muzzle light on the walls).
#include "selftest.h"

namespace {
int phase = 0, idx = 0, sub = 0; float t = 0; int shotFrame = -1, frame = 0; fs::path pend;
const char* kW[] = {"m1_garand", "mp40", "m1911a1", "modern_shotgun", "kar98k_zf39"};
fs::path out(const std::string& n) { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p / ("recoil_" + n + ".png"); }
void equip(App& a, const char* id) {
  int w = a.weaponIndex(id); if (w < 0) return;
  a.carried = {w}; a.selectWeapon(w);
  WeaponState& s = a.wstate[a.weaponDefs[w]->id]; s.loaded = a.weaponDefs[w]->cap; s.cooldown = 0; s.bolt = 0; s.reload = 0;
}
}

bool recoilCapture(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-recoil") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) { if (!pend.empty()) { a.screenshot(pend); pend.clear(); } return true; }
  t += dt; ++frame;
  ExploreInput& in = a.scriptedInput;
  if (phase == 0) {
    a.newMap(*a.findTemplate("blank_slate"), "Recoil capture");
    if (const AssetDef* w = a.lib.find("brm_wall_p")) a.placeInstance(w, vec3(0, a.terrain.height(0, -14), -14), 0);
    a.syncRuntime(); a.enterExplore(); a.useScripted = true; a.eyeYaw = 0; a.eyePitch = 2;
    phase = 1; t = 0; idx = 0; sub = 0; return true;
  }
  auto shotStep = [&](const std::string& name, bool ads) {   // false while running, true when done
    if (t < dt * 1.5f) { in = ExploreInput{}; shotFrame = -1; a.eyePitch = 2; WeaponState& w0 = a.wstate[a.weaponDefs[a.activeWeapon]->id]; w0.recoilP = w0.recoilY = 0; }
    in.aim = ads;
    if (t > 1.8f && shotFrame < 0) { in.fire = true; shotFrame = frame; a.uiShot = out(name); return false; }   // after any bolt cycle and the ADS raise; the frame of the shot (~17 ms in: the flash up, the kick ~2/3 of its peak)
    in.fire = false;
    if (shotFrame >= 0 && frame == shotFrame + 1) {
      const WeaponDef& d = *a.weaponDefs[a.activeWeapon]; const WeaponState& s = a.wstate[d.id];
      logf("[recoil] %s: back %.3f m, muzzle up %.2f deg, left %.2f deg (omega %.0f), flash parts %zu, muzzle lights %zu", name.c_str(), s.kick, s.kickP, s.kickY, d.vmOmega, a.fx.parts.size(), a.fx.mlights.size());
    }
    return shotFrame >= 0 && frame > shotFrame + 20;
  };
  if (phase == 1) {
    if (idx >= (int)(sizeof kW / sizeof kW[0])) { phase = 2; t = 0; return true; }
    if (t < dt * 1.5f && sub == 0) equip(a, kW[idx]);
    if (shotStep(std::string(kW[idx]) + (sub ? "_ads" : "_hip"), sub == 1)) { t = 0; if (++sub == 2) { sub = 0; ++idx; } }
    return true;
  }
  if (phase == 2) {   // a dark corridor
    if (a.mode == Mode::Explore) a.exitExplore();
    a.openMap(contentDir() / "templates/tunnels.nmap.json"); a.enterExplore(); a.useScripted = true;
    equip(a, "m1_garand"); phase = 3; t = 0; return true;
  }
  if (phase == 3) {
    static bool settled = false;
    if (!settled) { in = ExploreInput{}; if (t < 2.0f) return true; settled = true; equip(a, "m1_garand"); a.uiShot = out("tunnels_before"); t = 0; return true; }   // let the map settle
    if (shotStep("tunnels_flash", false) || t > 8) { if (a.mode == Mode::Explore) a.exitExplore(); a.selftest->exitCode = 0; a.selftest->done = true; }
    return true;
  }
  return true;
}
