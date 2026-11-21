#pragma once
// Battle Foundry rev 3 parameter data (spec docs/vfx-rev3/BATTLE-FOUNDRY-SPEC.md s10). Loaded once from
// content/settings/vfx/*.json, byte-identical copies of docs/vfx-rev3/data/ (check --check-fxev compares them): the spec's
// numbers are read, never retyped here. Rows the game has no exact match for are scaled from the nearest row by the
// reference chapters' formulas (fxWeaponFor).
#include "common.h"

struct FxDeviceMod { float xMach = 1, envelope = 1, dust = 1, secondary = 1, smokeScale = 1, smokeDelay = 0; int lobes = 0; };
struct FxWeaponRow {   // weapons.json (reference 06 s6.6)
  std::string id, device = "none";
  float boreM = 0.00762f, pMuzzle = 5.5e7f, chargeKg = 0.003f, xMach = 0.12f, envL = 0.36f, envR = 0.054f;
  float Tprim = 1645, Tsec = 2500, epsSec = 0.25f, tVis = 0.0025f, Ipeak = 1.1e4f, Weq = 0.001f, blastR = 0.37f, dustR = 0.9f, smokeM3 = 0.08f, impulse = 11.4f;
  int embers = 8;
};
struct FxMunitionRow {   // munitions.json (reference 07)
  std::string id; float W = 0.2f, Rfb = 2.2f, tPeak = 0.008f, tLum = 0.059f, craterD = 0.95f, craterDepth = 0.38f, craterLip = 0.11f;
  float apex = 7.2f, colLife = 2.8f, vCol = 15, vFrag = 2000, T0 = 3200, tau = 0.02f, n = 0.85f;
};
struct FxSurfaceRow {  // surfaces.json (spec 6.5)
  std::string id; vec3 albedo{0.1f}; float mass = 1, dustToFire = 1, craterDepth = 1, clodFrac = 0.5f, spark = 0, wet = 0;
};
struct FxGunRow {      // guns_recoil.json (spec 7.2 / 7.3)
  std::string id; float L = 0.3f, mRecoil = 1000, V0 = 10, impulse = 1e4f, f = 0.04f, tR = 0.05f, tCr = 0.3f, preExit = 0.04f;
  std::string platform; float Mplat = 1e4f, h = 1, I = 1e5f, fn = 1.2f, zeta = 0.3f, pitchDeg = 0.8f;
};

struct FxData {
  bool ok = false; int files = 0;
  struct BB { float T; vec3 rgb; float lv; };   // blackbody.json: hue (max channel 1), luminance at eps 1 (nits)
  std::vector<BB> bb;
  std::vector<FxWeaponRow> weapons; std::map<std::string, FxDeviceMod> devices;
  std::vector<FxMunitionRow> munitions; std::map<std::string, FxSurfaceRow> surfaces; std::map<std::string, float> soilMu;
  std::vector<FxGunRow> guns; json smallArms, coupling, tiers;
  std::map<std::string, float> yrefLux;   // yref.json illuminance by condition
  float clampSingle = 60, clampThresh = 24, clampNone = 16;   // yref.json peak_clamp_by_bloom_quality (unthresholded multi-radius: none)
  vec3 hue(float T) const;    // LUT, linear in T
  float lv(float T) const;    // LUT, linear in log(L)
  const FxWeaponRow* weapon(const std::string& id) const;
  const FxMunitionRow* munition(const std::string& id) const;
  const FxSurfaceRow& surface(const std::string& id) const;   // unknown: dry_soil
  const FxGunRow* gun(const std::string& id) const;
  FxDeviceMod device(const std::string& id) const;
};
const FxData& fxData();
// a game weapon's flash row: weapons.json id, else from its "caliber" text (bore in mm, or inches) the nearest small-arms row,
// x_Mach / envelope rescaled by the bore ratio (x_Mach = 0.67 D sqrt(p/p_a): linear in D at the row's pressure)
FxWeaponRow fxWeaponFor(const std::string& vfxId, const std::string& caliber);
// emitted luminance (nits) of a grey body at T, eps; and Yref = 0.18 E / pi (nits) for illuminance E (lux)
inline float fxYrefNits(float lux) { return 0.18f * lux / 3.14159265f; }
int fxDataSelfCheck();   // --check-fxev (headless)
