// WP4 --capture-recoil: no suite, a look at the visual recoil and the muzzle flash (selftest_out/recoil_*.png). On a
// blank map, for a rifle, an SMG, a pistol, a shotgun and a scoped sniper: one shot from the hip and one aimed (ADS),
// each frame taken two fixed steps (~33 ms) after the shot, at the kick's peak with the flash up; the spring values
// logged. Then one Garand shot in a Tunnels corridor (the muzzle light on the walls).
#include "selftest.h"
#include "vm_anim.h"

namespace {
int phase = 0, idx = 0, sub = 0; float t = 0; int shotFrame = -1, frame = 0; fs::path pend;
const char* kW[] = {"m1_garand", "mp40", "modern_m4", "modern_mp7", "modern_pistol3", "modern_shotgun"};   // WP4c: rifle, SMG, rig weapons with optics, the Glock (ADS arm), a shotgun
std::vector<int> all; bool table = false;   // --recoil-table: every gun, hip and ADS, logged only
float z0 = 0, zMax = 0;
// the rear sight in view space (+z toward the eye): what the player sees move back
float sightZ(App& a) {
  const WeaponDef& d = *a.weaponDefs[a.activeWeapon]; vec3 w;
  if (!(d.anim && animVmPoint(a, d, 0, w))) w = vec3(a.viewModelMatrix(d, a.wstate[d.id]) * vec4(d.rear, 1));
  vec3 e = a.eyePos(), f = App::forward(a.eyeYaw + a.viewKickYaw, a.eyePitch + a.viewKickPitch);
  return (glm::lookAt(e, e + f, a.viewUp(f)) * vec4(w, 1)).z;
}
float peakK = 0, peakP = 0, peakY = 0;
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
  auto shotStep = [&](const std::string& name, bool ads) {   // false while running, true when done: frames before / at the peak / recovered
    if (t < dt * 1.5f) { in = ExploreInput{}; shotFrame = -1; a.eyePitch = 2; WeaponState& w0 = a.wstate[a.weaponDefs[a.activeWeapon]->id]; w0.recoilP = w0.recoilY = 0; peakK = peakP = peakY = 0; }
    in.aim = ads;
    if (!table && t > 1.6f && t < 1.6f + dt * 1.5f) a.uiShot = out(name + "_0before");
    if (t > 1.8f && shotFrame < 0) { in.fire = true; shotFrame = frame; z0 = sightZ(a); zMax = 0; return false; }
    in.fire = false;
    const WeaponDef& d = *a.weaponDefs[a.activeWeapon]; const WeaponState& s = a.wstate[d.id];
    if (shotFrame >= 0) { zMax = std::max(zMax, sightZ(a) - z0); peakK = std::max(peakK, s.kick); peakP = std::max(peakP, s.kickP); peakY = std::max(peakY, s.kickY); }
    if (!table && shotFrame >= 0 && frame == shotFrame + 1) a.uiShot = out(name + "_1peak");          // the render after one more step: ~33 ms after the shot, at the peak
    if (!table && shotFrame >= 0 && frame == shotFrame + 9) a.uiShot = out(name + "_2recovered");     // ~165 ms
    if (shotFrame >= 0 && frame == shotFrame + 12) logf("[recoil] %s: seen back %.3f m%s (spring %.3f m), muzzle up %.2f deg, left %.2f deg (omega %.0f: peak at %.0f ms); now %.4f m / %.3f deg", name.c_str(), zMax, zMax < 0.005f ? " ZERO" : "", peakK, peakP, peakY, d.vmOmega, 1000.0f / d.vmOmega, s.kick, s.kickP);
    return shotFrame >= 0 && frame > shotFrame + 14;
  };
  if (phase == 1) {
    if (idx == 0 && sub == 0 && t < dt * 1.5f && all.empty()) {
      for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--recoil-table") table = true;
      if (table) for (int w = 0; w < (int)a.weaponDefs.size(); ++w) { const WeaponDef& d = *a.weaponDefs[w]; if (!d.equipment() && !d.tool && !d.melee.on && !d.flame.on && d.id != "m2hb_carry") all.push_back(w); }   // the carried M2 fires only set up
    }
    int n = table ? (int)all.size() : (int)(sizeof kW / sizeof kW[0]);
    if (table && idx < n && t < dt * 1.5f) equip(a, a.weaponDefs[all[idx]]->id.c_str());
    if (table && idx < n) { if (shotStep(a.weaponDefs[all[idx]]->id + (sub ? "_ads" : "_hip"), sub == 1)) { t = 0; if (++sub == 2) { sub = 0; ++idx; } } return true; }
    if (idx >= n) { if (a.mode == Mode::Explore) a.exitExplore(); a.selftest->exitCode = 0; a.selftest->done = true; return true; }
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
