// Battle Foundry rev 3 parameter data: content/settings/vfx/*.json (byte copies of docs/vfx-rev3/data, the spec's s10).
#include "fxdata.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {
json loadVfx(const char* name, int& files) {
  try { json j = json::parse(readTextFile(contentDir() / "settings" / "vfx" / name)); ++files; return j; }
  catch (std::exception& e) { logf("fxdata: settings/vfx/%s: %s (built-in defaults)", name, e.what()); return json::object(); }
}
float num(const json& j, const char* k, float d) { return j.contains(k) && j[k].is_number() ? j[k].get<float>() : d; }
FxData load() {
  FxData D; int& n = D.files;
  json bb = loadVfx("blackbody.json", n), w = loadVfx("weapons.json", n), m = loadVfx("munitions.json", n), s = loadVfx("surfaces.json", n);
  json g = loadVfx("guns_recoil.json", n), y = loadVfx("yref.json", n), t = loadVfx("tiers.json", n); loadVfx("materials.json", n);   // materials: the environment phases'
  for (const json& e : bb.value("lut", json::array())) {
    const json& c = e.value("rgb_linear_normalised", json::array({1, 1, 1}));
    D.bb.push_back({num(e, "T", 1000), vec3(c[0].get<float>(), c[1].get<float>(), c[2].get<float>()), num(e, "luminance_nits_eps1", 1)});
  }
  for (const json& e : w.value("weapons", json::array())) {
    FxWeaponRow r; r.id = e.value("id", ""); r.device = e.value("device", "none");
    r.boreM = num(e, "bore_mm", 7.62f) * 1e-3f; r.pMuzzle = num(e, "muzzle_pressure_Pa", r.pMuzzle); r.chargeKg = num(e, "charge_kg", r.chargeKg);
    r.xMach = num(e, "x_mach_m", r.xMach); r.envL = num(e, "envelope_length_m", r.envL); r.envR = num(e, "envelope_max_radius_m", r.envR);
    r.Tprim = num(e, "T_primary_K", r.Tprim); r.Tsec = num(e, "T_secondary_K", r.Tsec); r.epsSec = num(e, "emissivity_secondary", r.epsSec);
    r.tVis = num(e, "visible_flash_s", r.tVis); r.Ipeak = num(e, "peak_intensity_cd", r.Ipeak); r.Weq = num(e, "blast_W_eq_kg", r.Weq);
    r.blastR = num(e, "blast_sphere_m", r.blastR); r.dustR = num(e, "ground_dust_radius_m", r.dustR); r.smokeM3 = num(e, "smoke_m3", r.smokeM3);
    r.impulse = num(e, "recoil_impulse_Ns", r.impulse); r.embers = (int)num(e, "ember_count", (float)r.embers);
    D.weapons.push_back(r);
  }
  if (w.contains("device_modifiers")) for (auto& [k, e] : w["device_modifiers"].items()) {
    FxDeviceMod d; d.xMach = num(e, "x_mach", 1); d.envelope = num(e, "envelope", 1); d.dust = num(e, "dust", 1); d.secondary = num(e, "secondary_flash", 1);
    d.lobes = (int)num(e, "lateral_lobes", 0); d.smokeScale = num(e, "smoke_scale", 1); d.smokeDelay = num(e, "smoke_delay_s", 0); D.devices[k] = d;
  }
  for (const json& e : m.value("munitions", json::array())) {
    FxMunitionRow r; r.id = e.value("id", ""); r.W = num(e, "tnt_equiv_kg", r.W); r.Rfb = num(e, "fireball_radius_m", r.Rfb);
    r.tPeak = num(e, "t_peak_s", r.tPeak); r.tLum = num(e, "t_luminous_s", r.tLum);
    if (e.contains("crater")) { r.craterD = num(e["crater"], "diameter_m", r.craterD); r.craterDepth = num(e["crater"], "depth_m", r.craterDepth); r.craterLip = num(e["crater"], "lip_m", r.craterLip); }
    if (e.contains("column")) { r.apex = num(e["column"], "apex_m", r.apex); r.colLife = num(e["column"], "life_s", r.colLife); r.vCol = num(e["column"], "v_col_mps", r.vCol); }
    if (e.contains("fragment")) r.vFrag = num(e["fragment"], "v0_mps", r.vFrag);
    if (e.contains("temperature")) { r.T0 = num(e["temperature"], "T0_K", r.T0); r.tau = num(e["temperature"], "tau_s", r.tau); r.n = num(e["temperature"], "n", r.n); }
    D.munitions.push_back(r);
  }
  if (s.contains("surfaces")) for (auto& [k, e] : s["surfaces"].items()) {
    FxSurfaceRow r; r.id = k; const json& a = e.value("ejecta_albedo", json::array({0.1, 0.1, 0.1}));
    r.albedo = vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>()); r.mass = num(e, "ejecta_mass_scale", 1); r.dustToFire = num(e, "dust_to_fire", 1);
    r.craterDepth = num(e, "crater_depth_scale", 1); r.clodFrac = num(e, "clod_fraction", 0.5f); r.spark = num(e, "spark_rate", 0); r.wet = num(e, "wetness", 0);
    D.surfaces[k] = r;
  }
  if (s.contains("_soil_mu")) for (auto& [k, v] : s["_soil_mu"].items()) if (v.is_number()) D.soilMu[k] = v.get<float>();
  for (const json& e : g.value("guns", json::array())) {
    FxGunRow r; r.id = e.value("id", ""); r.L = num(e, "recoil_stroke_m", r.L); r.mRecoil = num(e, "recoiling_mass_kg", r.mRecoil);
    r.V0 = num(e, "free_recoil_velocity_mps", r.V0); r.impulse = num(e, "impulse_Ns", r.impulse); r.f = num(e, "counter_recoil_factor_f", r.f);
    r.tR = num(e, "t_recoil_s", r.tR); r.tCr = num(e, "t_counter_s", r.tCr); r.preExit = num(e, "pre_exit_travel_m", r.preExit);
    if (e.contains("platform")) { const json& p = e["platform"]; r.platform = p.value("name", ""); r.Mplat = num(p, "mass_kg", r.Mplat);
      r.h = num(p, "trunnion_height_above_cg_m", r.h); r.I = num(p, "pitch_inertia_kgm2", r.I); r.fn = num(p, "pitch_freq_Hz", r.fn);
      r.zeta = num(p, "damping_zeta", r.zeta); r.pitchDeg = num(p, "peak_pitch_deg", r.pitchDeg); }
    D.guns.push_back(r);
  }
  D.smallArms = g.value("small_arms_spring", json::object()); D.coupling = g.value("camera_coupling", json::object()); D.tiers = t;
  if (y.contains("illuminance_lux_by_condition")) for (auto& [k, v] : y["illuminance_lux_by_condition"].items()) if (v.is_number()) D.yrefLux[k] = v.get<float>();
  if (y.contains("peak_clamp_by_bloom_quality")) {
    const json& c = y["peak_clamp_by_bloom_quality"];
    auto cl = [&](const char* k, float d) { return c.contains(k) && c[k].contains("clamp") && c[k]["clamp"].is_number() ? c[k]["clamp"].get<float>() : d; };
    D.clampSingle = cl("unthresholded_single_gaussian", 60); D.clampThresh = cl("thresholded", 24); D.clampNone = cl("none", 16);
  }
  D.ok = D.bb.size() >= 2 && !D.weapons.empty() && !D.munitions.empty() && !D.surfaces.empty() && !D.guns.empty();
  logf("fxdata: %d files, %zu blackbody, %zu weapons, %zu munitions, %zu surfaces, %zu guns%s", n, D.bb.size(), D.weapons.size(), D.munitions.size(),
       D.surfaces.size(), D.guns.size(), D.ok ? "" : " (INCOMPLETE)");
  return D;
}
}

const FxData& fxData() { static FxData d = load(); return d; }

vec3 FxData::hue(float T) const {
  if (bb.empty()) return vec3(1, 0.3f, 0.05f);
  if (T <= bb.front().T) return bb.front().rgb;
  if (T >= bb.back().T) return bb.back().rgb;
  auto it = std::lower_bound(bb.begin(), bb.end(), T, [](const BB& b, float t) { return b.T < t; });
  const BB& b = *it; const BB& a = *(it - 1); return glm::mix(a.rgb, b.rgb, (T - a.T) / (b.T - a.T));
}
float FxData::lv(float T) const {
  if (bb.empty()) return 0;
  if (T <= bb.front().T) return bb.front().lv * std::pow(T / bb.front().T, 8.1f);   // the Wien tail (reference 05 s5.8)
  if (T >= bb.back().T) return bb.back().lv;
  auto it = std::lower_bound(bb.begin(), bb.end(), T, [](const BB& b, float t) { return b.T < t; });
  const BB& b = *it; const BB& a = *(it - 1); const float k = (T - a.T) / (b.T - a.T);
  return std::exp(glm::mix(std::log(std::max(a.lv, 1e-12f)), std::log(std::max(b.lv, 1e-12f)), k));
}
template <class T> static const T* byId(const std::vector<T>& v, const std::string& id) { for (const T& r : v) if (r.id == id) return &r; return nullptr; }
const FxWeaponRow* FxData::weapon(const std::string& id) const { return byId(weapons, id); }
const FxMunitionRow* FxData::munition(const std::string& id) const { return byId(munitions, id); }
const FxGunRow* FxData::gun(const std::string& id) const { return byId(guns, id); }
const FxSurfaceRow& FxData::surface(const std::string& id) const {
  static const FxSurfaceRow fallback; auto it = surfaces.find(id); if (it != surfaces.end()) return it->second;
  it = surfaces.find("dry_soil"); return it != surfaces.end() ? it->second : fallback;
}
FxDeviceMod FxData::device(const std::string& id) const { auto it = devices.find(id); return it != devices.end() ? it->second : FxDeviceMod{}; }

// bore (m) from a weapons/*.json "caliber" text: "7.92x57 mm", "9x19 mm", ".45 ACP", ".30-06", "2.36 in rocket"; 0 = unknown
static float boreFromCaliber(const std::string& c, float* caseMm) {
  if (caseMm) *caseMm = 0;
  const char* s = c.c_str(); while (*s && !(*s == '.' || (*s >= '0' && *s <= '9'))) ++s;
  if (!*s) return 0;
  char* e = nullptr; const float a = std::strtof(s, &e);
  if (!(a > 0)) return 0;
  if (*s == '.' || (a < 1.0f && !(e && *e == 'x'))) return a * 0.0254f;   // .45 / .30-06: inches
  if (e && *e == 'x' && caseMm) *caseMm = std::strtof(e + 1, nullptr);
  if (c.find(" in") != std::string::npos) return a * 0.0254f;
  return a * 1e-3f;
}
FxWeaponRow fxWeaponFor(const std::string& vfxId, const std::string& caliber) {
  const FxData& D = fxData();
  if (const FxWeaponRow* r = D.weapon(vfxId)) return *r;
  float caseMm = 0; const float bore = boreFromCaliber(caliber, &caseMm);
  // the nearest small-arms row by class: pistol-case (< 30 mm or 9-11.5 mm bore), intermediate (7.62x39), full rifle, 5.56, HMG
  const char* id = bore <= 0 ? "rifle_7.62x51" : bore < 0.0065f ? "rifle_5.56x45" : bore > 0.0125f ? "hmg_12.7x99"
                 : (bore > 0.0085f || (caseMm > 0 && caseMm < 30)) ? "pistol_9x19" : (caseMm > 0 && caseMm < 45) ? "rifle_7.62x39" : "rifle_7.62x51";
  FxWeaponRow r = D.weapon(id) ? *D.weapon(id) : FxWeaponRow{};
  if (bore > 0 && r.boreM > 0) { const float k = bore / r.boreM; r.xMach *= k; r.envL *= k; r.envR *= k; r.boreM = bore; }   // x_Mach linear in D (reference 06 s6.2)
  return r;
}

// ---- --check-fxev: the data copies match the spec, and the formulas hold on the loaded rows (EVENTS / BRIGHTNESS live in fx.cpp)
extern int fxEventSelfCheck(int& fails);
int fxDataSelfCheck() {
  int fails = 0;
  auto check = [&](bool ok, const std::string& what) { logf("[fxev] %s %s", ok ? "PASS" : "FAIL", what.c_str()); if (!ok) ++fails; };
  const FxData& D = fxData();
  check(D.ok && D.files == 8, "content/settings/vfx: 8 files loaded (" + std::to_string(D.files) + ")");
  // byte-identical to the spec's copies (the spec stays the source of truth); docs/ is next to content/ in a source tree
  fs::path docs; std::error_code ec;
  if (const char* src = std::getenv("NMK_SRC")) docs = fs::path(src) / "docs" / "vfx-rev3" / "data";
  if (docs.empty() || !fs::exists(docs)) docs = fs::weakly_canonical(contentDir(), ec).parent_path() / "docs" / "vfx-rev3" / "data";
  if (!fs::exists(docs)) logf("[fxev] SKIP data copies vs docs/vfx-rev3/data: no source tree here (set NMK_SRC)");
  else for (const char* f : {"blackbody.json", "guns_recoil.json", "materials.json", "munitions.json", "surfaces.json", "tiers.json", "weapons.json", "yref.json"}) {
    std::string a, b; try { a = readTextFile(contentDir() / "settings" / "vfx" / f); b = readTextFile(docs / f); } catch (...) {}
    check(!a.empty() && a == b, std::string("settings/vfx/") + f + " byte-identical to docs/vfx-rev3/data");
  }
  // blackbody: the spec's palette (5.3) from the LUT
  auto near3 = [](vec3 a, vec3 b, float t) { return glm::all(glm::lessThanEqual(glm::abs(a - b), vec3(t))); };
  check(near3(D.hue(1645), vec3(1.0f, 0.193f, 0.0f), 0.02f), "1645 K primary flash hue (1.000, 0.193, 0.000)");
  check(near3(D.hue(2500), vec3(1.0f, 0.380f, 0.068f), 0.02f), "2500 K secondary flash hue (1.000, 0.380, 0.068)");
  check(std::fabs(D.lv(1645) / 1.04e4f - 1) < 0.1f && std::fabs(D.lv(2500) / 1.79e6f - 1) < 0.1f, "luminance at eps 1: 1645 K 1.04e4, 2500 K 1.79e6 nits");
  // brightness convention (5.4): rifle primary 0.22 x Yref in full sun, secondary 78 x
  const float ysun = fxYrefNits(100000.0f);
  check(std::fabs(D.lv(1645) * 0.12f / ysun - 0.22f) < 0.03f, "rifle primary flash 0.22 x Yref in full sun (invisible)");
  check(std::fabs(D.lv(2500) * 0.25f / ysun - 78.0f) < 8.0f, "rifle secondary flash ~78 x Yref in full sun");
  // weapon geometry: x_Mach = 0.67 D sqrt(p/p_a) for every row
  bool xm = true; for (auto& r : D.weapons) xm = xm && std::fabs(0.67f * r.boreM * std::sqrt(r.pMuzzle / 101325.0f) / r.xMach - 1) < 0.02f;
  check(xm, "x_Mach = 0.67 D sqrt(p_muzzle / p_ambient) for all " + std::to_string(D.weapons.size()) + " weapons");
  { FxWeaponRow k = fxWeaponFor("", "7.92x57 mm"), p = fxWeaponFor("", ".45 ACP"), c = fxWeaponFor("", "7.62x39 mm");
    check(k.id == "rifle_7.62x51" && std::fabs(k.boreM - 0.00792f) < 1e-5f && p.id == "pistol_9x19" && c.id == "rifle_7.62x39", "game calibres map to rows (7.92x57 rifle, .45 pistol, 7.62x39)"); }
  // SCALE (spec 9.2): apex / crater diameter ~ 8 for every munition; doubling W multiplies R_fb and times by 2^(1/3)
  bool ap = true; for (auto& r : D.munitions) ap = ap && r.apex / r.craterD > 6.5f && r.apex / r.craterD < 9.5f;
  check(ap, "column apex / crater diameter ~ 8 for all " + std::to_string(D.munitions.size()) + " munitions");
  bool rc = true; for (auto& r : D.guns) rc = rc && r.tCr / r.tR >= 4 && r.tCr / r.tR <= 7;
  check(rc, "recoil counter / recoil time between 4 and 7 for all " + std::to_string(D.guns.size()) + " guns");
  fxEventSelfCheck(fails);
  logf("[fxev] %s (%d failed)", fails ? "FAIL" : "PASS", fails);
  return fails ? 1 : 0;
}

FxMunitionRow fxMunitionFor(const std::string& id, float W) {
  const FxData& D = fxData();
  if (W <= 0) { if (const FxMunitionRow* r = D.munition(id)) return *r; W = 1.09f; }
  FxMunitionRow r; r.id = "W"; r.W = W; const float w3 = std::cbrt(W);
  r.Rfb = 3.77f * w3; r.tPeak = 0.0139f * w3; r.tLum = 0.10f * w3; r.craterD = 1.6f * w3; r.craterDepth = 0.4f * r.craterD; r.craterLip = 0.12f * r.craterD;
  r.vCol = 20.0f * std::pow(W, 0.18f);
  // apex / D and the column life: log-W interpolation between the two rows around W (clamped at the ends)
  const FxMunitionRow* lo = nullptr; const FxMunitionRow* hi = nullptr;
  for (const FxMunitionRow& m : D.munitions) { if (m.W <= W && (!lo || m.W > lo->W)) lo = &m; if (m.W >= W && (!hi || m.W < hi->W)) hi = &m; }
  if (!lo) lo = hi; if (!hi) hi = lo;
  if (lo) {
    const float k = hi->W > lo->W ? (std::log(W) - std::log(lo->W)) / (std::log(hi->W) - std::log(lo->W)) : 0.0f;
    r.apex = r.craterD * glm::mix(lo->apex / lo->craterD, hi->apex / hi->craterD, k);
    r.colLife = std::exp(glm::mix(std::log(lo->colLife), std::log(hi->colLife), k));
    r.T0 = lo->T0; r.tau = lo->tau; r.n = lo->n; r.vFrag = glm::mix(lo->vFrag, hi->vFrag, k);
  } else r.apex = 8.0f * r.craterD;
  return r;
}
float fxSoilMu(const std::string& s) {
  const FxData& D = fxData();
  auto mu = [&](const char* k, float d) { auto it = D.soilMu.find(k); return it != D.soilMu.end() ? it->second : d; };
  if (s == "dry_sand" || s == "snow") return mu("dry_sand", 0.41f);
  if (s == "wet_soil" || s == "mud") return mu("damp_soil", 0.45f);
  if (s == "rock" || s == "concrete" || s == "asphalt") return mu("competent_rock", 0.55f);
  return mu("cohesive_soil", 0.50f);
}
