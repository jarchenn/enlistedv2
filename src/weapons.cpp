// Personal weapons: definitions (content/weapons/*.json), container-based ammunition accounting,
#include "smithy.h"
#include "loading.h"
#include "crumb.h"
// reload state machine, recoil springs, sights/scopes, first-person view model, crate resupply.
// HUD "loaded" counts every round in the weapon including the chambered one.
#include "app.h"
#include "fxdata.h"   // Battle Foundry 6 (fxrecoil)
#include "overrides.h"
#include "settings.h"
#include "sight_overlay.h"
#include "vm_anim.h"
#include "br_models.h"
#include "loot.h"   // BR2 Battle Royale loot
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>
#include <numeric>
#include <functional>
#include <set>
#include <tuple>
#include <map>
#include <stb_image.h>
#include <stb_image_write.h>

enum WPart { WP_BODY, WP_BOLT, WP_MAG, WP_COVER, WP_CLIP, WP_BIPOD, WP_SCOPE, WP_HANDLE, WP_COUNT };
static const char* kPartNames[WP_COUNT] = {"body", "bolt", "mag", "cover", "clip", "bipod", "scope", "handle"};

// AN1 step 2b: the grip and the handguard as capsules fitted to the model's vertices at the hand sockets. A slice 3 cm thick across
// the axis, within 6 cm of it: its centroid is the axis point, the median distance from the axis the radius (surface vertices);
// the grip's axis comes from the centroids of two slices 5 cm apart. Returns the fit radius (0: too few vertices).
static float fitCapsule(const std::vector<vec3>& P, vec3& c, vec3& a, bool fitAxis) {
  auto slice = [&](vec3 cc, vec3 aa, vec3& cen, std::vector<float>* dist) {
    vec3 s(0); int n = 0;
    for (auto& p : P) { vec3 d = p - cc; float t = glm::dot(d, aa); if (std::fabs(t) > 0.02f) continue; vec3 r = d - aa * t; if (glm::length(r) > 0.06f) continue; s += p - aa * t; ++n; }
    if (n < 4) return false;
    cen = s / (float)n;
    if (dist) for (auto& p : P) { vec3 d = p - cen; float t = glm::dot(d, aa); if (std::fabs(t) > 0.02f) continue; float r = glm::length(d - aa * t); if (r < 0.06f) dist->push_back(r); }
    return true;
  };
  for (int it = 0; it < 3; ++it) {
    vec3 cen; if (!slice(c, a, cen, nullptr)) return 0;
    c = cen;
    if (fitAxis) { vec3 c0, c1; if (slice(c - a * 0.025f, a, c0, nullptr) && slice(c + a * 0.025f, a, c1, nullptr) && glm::length(c1 - c0) > 0.01f) { vec3 na = glm::normalize(c1 - c0); if (glm::dot(na, a) > 0.5f) a = na; } }
  }
  std::vector<float> d; vec3 cen; if (!slice(c, a, cen, &d) || d.size() < 4) return 0;
  std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
  return d[d.size() / 2];
}
// AT7: a gun's own sights as their own parts (GpuModel::ironParts), so a fitted optic hides them whole (folded irons) instead
// of the window clip cutting them in half: the connected pieces (vertices welded by position) lying entirely within
// `radius` of the rear or front sight point and under 8 cm across, moved to the end of their part's index range as a new
// part. A piece welded to the receiver stays (the window clip covers it). JSON "ironsWithOptic": true (or a radius in m).
// AT8: the same split for a factory optic ("factoryOptic": {"box": [[x, y, z], [x, y, z]]}): the pieces wholly inside the box
static void splitPieces(GpuModel& g, std::function<bool(const vec3&)> inside, float maxDiag, std::vector<int>& out, const char* suffix, const std::string& id, const char* what) {
  if (!out.empty() || g.cpuIdx.empty()) return;
  const size_t nv = g.cpuPos.size(); std::vector<uint32_t> up(nv); for (size_t i = 0; i < nv; ++i) up[i] = (uint32_t)i;
  std::function<uint32_t(uint32_t)> find = [&](uint32_t x) { while (up[x] != x) { up[x] = up[up[x]]; x = up[x]; } return x; };
  auto join = [&](uint32_t a, uint32_t b) { a = find(a); b = find(b); if (a != b) up[a] = b; };
  { std::map<std::tuple<int, int, int>, uint32_t> weld;   // UV seams split a piece's vertices: weld them back by position (0.1 mm)
    for (size_t i = 0; i < nv; ++i) { vec3 p = g.cpuPos[i] * 1e4f; auto k = std::make_tuple((int)std::lround(p.x), (int)std::lround(p.y), (int)std::lround(p.z)); auto it = weld.find(k); if (it == weld.end()) weld.emplace(k, (uint32_t)i); else join((uint32_t)i, it->second); } }
  for (size_t q = 0; q + 2 < g.cpuIdx.size(); q += 3) { join(g.cpuIdx[q], g.cpuIdx[q + 1]); join(g.cpuIdx[q], g.cpuIdx[q + 2]); }
  std::map<uint32_t, std::pair<vec3, vec3>> box; std::map<uint32_t, bool> near;
  for (size_t i = 0; i < nv; ++i) {
    uint32_t r = find((uint32_t)i); auto it = box.find(r); const vec3& p = g.cpuPos[i];
    if (it == box.end()) { box[r] = {p, p}; near[r] = true; } else { it->second.first = glm::min(it->second.first, p); it->second.second = glm::max(it->second.second, p); }
    if (!inside(p)) near[r] = false;
  }
  std::set<uint32_t> irons; for (auto& [r, b] : box) if (near[r] && glm::length(b.second - b.first) < maxDiag) irons.insert(r);
  if (irons.empty()) { logf("weapon %s: no separate %s pieces found", id.c_str(), what); return; }
  const size_t np = g.parts.size(); int moved = 0;
  for (size_t pk = 0; pk < np; ++pk) {
    ModelPart& p = g.parts[pk]; std::vector<uint32_t> keep, iron;
    for (uint32_t q = p.first; q + 2 < p.first + p.count; q += 3) (irons.count(find(g.cpuIdx[q])) ? iron : keep).insert((irons.count(find(g.cpuIdx[q])) ? iron : keep).end(), {g.cpuIdx[q], g.cpuIdx[q + 1], g.cpuIdx[q + 2]});
    if (iron.empty()) continue;
    std::copy(keep.begin(), keep.end(), g.cpuIdx.begin() + p.first); std::copy(iron.begin(), iron.end(), g.cpuIdx.begin() + p.first + keep.size());
    ModelPart ip = p; ip.name = p.name + suffix; ip.first = p.first + (uint32_t)keep.size(); ip.count = (uint32_t)iron.size(); p.count = (uint32_t)keep.size();
    out.push_back((int)g.parts.size()); g.parts.push_back(ip); moved += (int)iron.size() / 3;
  }
  GLuint nb = 0; glCreateBuffers(1, &nb); glNamedBufferStorage(nb, g.cpuIdx.size() * 4, g.cpuIdx.data(), 0); glVertexArrayElementBuffer(g.vao, nb); glDeleteBuffers(1, &g.ebo); g.ebo = nb;
  logf("weapon %s: %d %s piece(s), %d triangles, split off: hidden with an optic fitted", id.c_str(), (int)irons.size(), what, moved);
}
static void splitIrons(GpuModel& g, const std::vector<vec3>& at, float radius, const std::string& id) {
  splitPieces(g, [&](const vec3& p) { for (const vec3& c : at) if (glm::length(p - c) <= radius) return true; return false; }, 0.08f, g.ironParts, "_irons", id, "sight");
}
// AT8: a gun's factory optic as parts of their own (GpuModel::opticParts): JSON "factoryOptic": {"names": [part names]} or
// {"box": [[lo], [hi]]} (weapon space: the pieces wholly inside); hidden whenever another sight is fitted
static void factoryOptic(GpuModel& g, const json& fo, const std::string& id) {
  if (!g.opticParts.empty()) return;
  for (auto& n : fo.value("names", json::array())) for (size_t k = 0; k < g.parts.size(); ++k) if (g.parts[k].name == n.get<std::string>()) g.opticParts.push_back((int)k);
  if (fo.contains("box")) { vec3 lo = jvec3(fo["box"][0]), hi = jvec3(fo["box"][1]);
    splitPieces(g, [=](const vec3& p) { return glm::all(glm::greaterThanEqual(p, lo)) && glm::all(glm::lessThanEqual(p, hi)); }, 1.0f, g.opticParts, "_optic", id, "factory optic"); }
}
static void measureHold(WeaponDef& d, const std::vector<vec3>& P) {
  if (P.empty()) return;
  vec3 c = d.gripL, a(0, 0, 1);
  if (float r = fitCapsule(P, c, a, false); r > 0) { d.guardC = c; d.guardRad = std::clamp(r, 0.014f, 0.05f); }   // a hand wraps at least a 28 mm handguard
  c = d.gripR; a = glm::normalize(vec3(0, -1, -0.25f));   // grips rake back
  if (float r = fitCapsule(P, c, a, true); r > 0) {
    if (a.z > 0.1f) { a = glm::normalize(vec3(0, -1, -0.25f)); c = d.gripR; r = fitCapsule(P, c, a, false); }   // a grip never rakes forward: the slices caught the trigger guard / receiver (MG 42)
    if (r > 0) { d.gripC = c; d.gripAx = a; d.gripRad = std::clamp(r, 0.013f, 0.035f); }
  }
  logf("[hold] %s grip (%.3f %.3f %.3f) ax (%.2f %.2f %.2f) r %.3f | guard (%.3f %.3f %.3f) r %.3f | sockets R (%.3f %.3f %.3f) L (%.3f %.3f %.3f)", d.id.c_str(),
       d.gripC.x, d.gripC.y, d.gripC.z, d.gripAx.x, d.gripAx.y, d.gripAx.z, d.gripRad, d.guardC.x, d.guardC.y, d.guardC.z, d.guardRad, d.gripR.x, d.gripR.y, d.gripR.z, d.gripL.x, d.gripL.y, d.gripL.z);
}

bool fireCurveAt(const WeaponDef& d, float t, float out[10]) {   // KF1
  const auto& c = d.fireCurve; if (c.empty()) { for (int k = 0; k < 10; ++k) out[k] = 0; return false; }
  float f = t * d.fireCurveFps; if (!(f >= 0.0f)) f = 0.0f; f = std::min(f, (float)c.size());   // FG1l: clamped before the int cast: the fifth-shot fold asks for t = 1e9 s, and 3e10 overflowed (int) to INT_MIN: c[i] read far out of bounds (crash at 750 rpm, FG 42 ADS)
  const int i = (int)f;
  if (i >= (int)c.size() - 1) { for (int k = 0; k < 10; ++k) out[k] = c.back()[k]; return false; }
  const float u = f - (float)i;
  for (int k = 0; k < 10; ++k) out[k] = c[i][k] + (c[i + 1][k] - c[i][k]) * u;
  return true;
}
void fireCurveSum(const WeaponDef& d, const WeaponState& s, float out[10], float& handle) {   // GR1
  for (int k = 0; k < 10; ++k) out[k] = 0;
  handle = 0;
  for (int n = 0; n < 4; ++n) {
    if (s.curveFolded[n]) continue;
    float row[10]; fireCurveAt(d, s.curveAge[n], row);
    for (int k = 0; k < 10; ++k) out[k] += s.curveGain[n] * row[k];
    handle = std::max(handle, row[9]);   // the rod cycles fully on every shot, hip or ADS
  }
}

mat4 kf2BoltLocal(const WeaponDef& d, float lift, float draw) {   // KF2: the bolt in weapon space: turned about the bore by lift x boltLift, then slid back draw x boltTravel
  return glm::translate(mat4(1), vec3(0, 0, -d.boltTravel * draw)) * glm::translate(mat4(1), d.boltPivot) * glm::mat4_cast(glm::angleAxis(radians(d.boltLift * lift), vec3(0, 0, 1))) * glm::translate(mat4(1), -d.boltPivot);
}

bool boltCurveAt(const WeaponDef& d, float boltLeft, float out[7]) {   // KF2
  for (int k = 0; k < 7; ++k) out[k] = 0;
  const auto& c = d.boltCurve;
  if (c.empty() || boltLeft <= 0 || boltLeft > d.boltTime) return false;
  const float f = std::max(0.0f, d.boltTime - boltLeft) * d.boltCurveFps; const int i = (int)f;
  if (i >= (int)c.size() - 1) { for (int k = 0; k < 7; ++k) out[k] = c.back()[k]; return true; }
  const float u = f - (float)i;
  for (int k = 0; k < 7; ++k) out[k] = c[i][k] + (c[i + 1][k] - c[i][k]) * u;
  return true;
}

void App::buildWeaponMeshes() {
  weaponDefs.clear();
  fs::path dir = contentDir() / "weapons";
  if (!fs::exists(dir)) return;
  std::vector<fs::path> files;
  for (auto& e : fs::directory_iterator(dir)) if (e.path().extension() == ".json") files.push_back(e.path());
  std::sort(files.begin(), files.end());
  // handling.json: dispersion cones by class, stance factors, and each weapon's class
  handling = Handling{};
  json classes = json::object(), vmRecoil = json::object();
  try {
    json h = json::parse(readTextFile(dir / "handling.json"));
    static const char* cn[4] = {"rifle", "sniper", "auto", "lmg"};
    static const char* sn[STANCE_COUNT] = {"moving", "standing", "crouched", "prone", "mounted", "sprinting"};
    json hip = h.value("hip", json::object()), ads = h.value("ads", json::object()), st = h.value("stance", json::object()), rc = h.value("recoil", json::object());
    for (int k = 0; k < 4; ++k) { handling.hip[k] = hip.value(cn[k], handling.hip[k]); handling.ads[k] = ads.value(cn[k], handling.ads[k]); }
    for (int k = 0; k < STANCE_COUNT; ++k) { handling.stance[k] = st.value(sn[k], handling.stance[k]); handling.recoil[k] = rc.value(sn[k], handling.recoil[k]); }
    handling.movingYaw = rc.value("movingYaw", handling.movingYaw);
    classes = h.value("classes", json::object());
    vmRecoil = h.value("vmRecoil", json::object());   // WP4
    {   // WP4b: the muzzle flash's colours and light
      json mf = h.value("muzzleFlash", json::object()); Fx::FlashLook& F = fx.flashLook; F = Fx::FlashLook{};
      F.core = jvec3(mf.value("core", json()), F.core); F.petal = jvec3(mf.value("petal", json()), F.petal); F.edge = jvec3(mf.value("edge", json()), F.edge);
      F.light = jvec3(mf.value("lightColor", json()), F.light); F.radius = mf.value("lightRadius_m", F.radius); F.intensity = mf.value("lightIntensity", F.intensity);
    }
  } catch (std::exception& e) { logf("content/weapons/handling.json: %s (built-in handling)", e.what()); }
  size_t wk = 0;
  if (useImportedModels) prefetchModelTree(dir / "models");   // CACHE1: a cold texture cache fills in one parallel pass
  for (auto& f : files) {
    crumb("weapon defs");   // UI5
    if (gLoadingDepth) { char b[80]; std::snprintf(b, sizeof b, "Loading weapons %zu / %zu", ++wk, files.size()); loadingProgress(b, (float)wk / files.size()); }   // CACHE1: the splash shows the real count
    if (f.filename() == "handling.json" || f.filename() == "overrides_user.json") continue;   // ED2: the user's overrides patch the weapons below
    json j;
    try { j = json::parse(readTextFile(f)); } catch (...) { continue; }
    if (fs::path lp = f.parent_path() / "local" / f.filename(); fs::exists(lp) && !(f.stem() == "modern_mp5" && !R.perf.mp5New) && !(f.stem() == "sten" && !R.perf.sten2Rig) && !(f.stem() == "mg42" && !R.perf.mg42Rig) && !(f.stem() == "pkm" && !R.perf.pkmRig) && !((f.stem() == "kar98k" || f.stem() == "kar98k_zf39") && !R.perf.karRig) && !(f.stem() == "m1_garand" && !R.perf.garandRig)) {   // GR13 garandrig   // KR1 karrig   // FR5: sten2rig / mg42rig / pkmrig off keep the committed views   // XB1: mp5new off keeps the committed MP5   // SW1: a local-only override (git-ignored content/weapons/local/<file>): names, model, sights
      try { j.merge_patch(json::parse(readTextFile(lp))); } catch (std::exception& e) { logf("weapons: %s: %s (ignored)", lp.string().c_str(), e.what()); }
    }
    {   // ED2: the user's stat overrides (the menu's Guns screen): the weapon keys; _damage / _handling go to their tables
      json& ov = weaponOverrides(); auto it = ov.find(j.value("id", ""));
      if (it != ov.end() && it->is_object()) { json p = *it; p.erase("_damage"); p.erase("_handling"); j.merge_patch(p); }
    }
    auto d = std::make_unique<WeaponDef>();
    d->id = j.value("id", ""); d->name = j.value("name", d->id); d->side = j.value("side", "");
    d->tool = j.value("tool", std::string()) == "build"; d->locked = j.value("locked", false);   // GM3: the Engineer's hammer
    d->action = j.value("action", "semi"); d->feed = j.value("feed", "magazine"); d->magReload = j.value("reloadStyle", "") == "magazine"; d->container = j.value("container", "");
    d->caliber = j.value("caliber", ""); d->note = j.value("note", "");
    d->cap = j.value("cap", 1); d->loaded = j.value("loaded", 1); d->reserve = j.value("reserve", 0); d->containerSize = j.value("containerSize", 1);
    d->tracerEvery = j.value("tracerEvery", 0);
    d->defaultSight = std::clamp(j.value("defaultSight", 0), 0, 6);   // AT5
    d->sway = j.value("sway", 1.0f); d->breathe = j.value("breathe", true); d->rpm = j.value("rpm", 60.0f); d->velocity = j.value("velocity", 800.0f); d->zero = j.value("zero", 100.0f);
    if (j.contains("fireCurve") && j["fireCurve"].is_object()) {   // KF1
      const json& fc = j["fireCurve"]; d->fireCurveFps = std::max(1.0f, fc.value("fps", 30.0f));
      for (auto& r : fc.value("frames", json::array())) if (r.is_array() && r.size() >= 6) { std::array<float, 10> a{}; for (size_t k = 0; k < std::min<size_t>(10, r.size()); ++k) a[k] = r[k].get<float>(); d->fireCurve.push_back(a); d->fireCurveHandle |= a[9] != 0; }
      d->fireCurveAds = fc.value("ads", false);   // GR1
    }
    d->boltDelay = std::max(0.0f, j.value("boltDelay", 0.0f));
    if (j.contains("boltCurve") && j["boltCurve"].is_object()) {   // KF2
      const json& bc = j["boltCurve"]; d->boltCurveFps = std::max(1.0f, bc.value("fps", 30.0f));
      for (auto& r : bc.value("frames", json::array())) if (r.is_array() && r.size() >= 7) { std::array<float, 7> a; for (int k = 0; k < 7; ++k) a[k] = r[k].get<float>(); d->boltCurve.push_back(a); }
    }
    d->boltTravel = j.value("boltTravel", 0.09f);
    if (j.contains("boltKnob")) d->boltKnob = vec3(j["boltKnob"][0].get<float>(), j["boltKnob"][1].get<float>(), j["boltKnob"][2].get<float>());
    d->boltTime = j.value("boltTime", 0.0f); d->spinUp = std::max(0.0f, j.value("spinUp", 0.0f)); d->hipSpreadMul = std::max(0.1f, j.value("hipSpreadMul", 1.0f));   // MG13
    d->reloadEmpty = jkey(j, "reload").value("empty", 2.5f); d->reloadPartial = jkey(j, "reload").value("partial", d->reloadEmpty); d->reloadRound = jkey(j, "reload").value("round", 0.5f);
    d->rPitch = jkey(j, "recoil").value("pitch", 1.0f); d->rYaw = jkey(j, "recoil").value("yaw", 0.4f); d->rRecover = jkey(j, "recoil").value("recover", 8.0f);
    d->rClimb = jkey(j, "recoil").value("climb", 0.0f); d->rVisual = jkey(j, "recoil").value("visual", 0.05f); d->rSupported = jkey(j, "recoil").value("supported", 1.0f);
    d->rear = jvec3(j["sights"]["rear"]); d->front = jvec3(j["sights"]["front"]); d->muzzle = jvec3(j["muzzle"]);
    if (j.contains("muzzleDir")) d->muzzleDir = glm::normalize(jvec3(jkey(j, "muzzleDir"), vec3(0, 0, 1)));
    if (j.contains("ads")) {
      const json& a = j["ads"];
      d->eyeRelief = a.value("eyeRelief", 0.21f); d->adsVmFov = a.value("vmFov", 0.0f);
      d->adsPos = jvec3(a.value("posCorrection", json::array({0, 0, 0}))); d->adsRot = jvec3(a.value("rotCorrection", json::array({0, 0, 0})));
      if (a.contains("hipOffset")) d->hipOffset = jvec3(a["hipOffset"]);
      d->adsWorldFov = a.value("worldFov", 58.0f); d->sightLineOnly = a.value("virtualLine", false);
      d->adsSpeed = a.value("speed", 1.0f); d->adsZoom = a.value("zoom", 0.0f);
    }
    if (j.contains("flame")) {   // FT1 flamethrower tuning (data; docs/flamethrower.md)
      const json& f = j["flame"]; auto& fd = d->flame;
      fd.on = true; fd.fuelSeconds = f.value("fuelSeconds", fd.fuelSeconds); fd.range = f.value("range", fd.range); fd.speed = f.value("speed", fd.speed);
      fd.coneDeg = f.value("coneDeg", fd.coneDeg); fd.parcelsPerSec = f.value("parcelsPerSec", fd.parcelsPerSec); fd.parcelDamage = f.value("parcelDamage", fd.parcelDamage);
      fd.burnSeconds = f.value("burnSeconds", fd.burnSeconds); fd.burnDps = f.value("burnDps", fd.burnDps);
      fd.patchSeconds = f.value("patchSeconds", fd.patchSeconds); fd.patchRadius = f.value("patchRadius", fd.patchRadius); fd.patchDps = f.value("patchDps", fd.patchDps);
      fd.botRange = f.value("botRange", fd.botRange); fd.botBurst = f.value("botBurstSeconds", fd.botBurst); fd.botPause = f.value("botPauseSeconds", fd.botPause);
    }
    d->modern = j.value("modern", d->side == "modern");
    d->smoke = std::max(0.0f, j.value("smoke", d->modern || j.contains("blaster") || j.contains("flame") || j.value("action", std::string()) == "launcher" || j.value("caliber", std::string()).empty() ? 0.0f : 1.0f));   // WS1: period guns (not rockets, flamethrowers, or a weapon without a calibre) d->pellets = std::max(1, j.value("pellets", 1)); d->spreadDeg = j.value("spreadDeg", 0.0f);   // MG1
    d->casings = j.value("casings", true);
    { std::string bl = j.value("blaster", std::string()); d->blaster = bl == "red" ? 4 : bl.empty() ? 0 : 3; }   // SW1
    if (j.contains("hold")) { const json& h = j["hold"]; d->holdOffset = jvec3(h.value("offset", json()), vec3(0)); d->holdWeld = h.value("weld", 1.0f); std::string hk = h.value("kind", std::string()); d->holdKind = hk == "pistol" ? 0 : hk == "rifle" ? 1 : hk == "launcher" ? 2 : -1; d->leftHold = h.value("left", std::string()) == "magazine" ? 1 : h.value("left", std::string()) == "tube" ? 2 : 0;   // 2: a launcher tube: the support hand in a C over it beside the face
      if (h.contains("pose")) { const json& gp = h["pose"]; GripPose& G = d->gripPose; G.has = true;   // WS1: the approved hold (tools/grip_to_game.py); index 1 = the right hand
        auto jq = [](const json& a) { return a.is_array() && a.size() == 4 ? glm::normalize(quat(a[3].get<float>(), a[0].get<float>(), a[1].get<float>(), a[2].get<float>())) : quat(1, 0, 0, 0); };
        G.p[1] = jvec3(jkey(gp, "r").value("p", json()), vec3(0)); G.q[1] = jq(jkey(gp, "r").value("q", json())); G.p[0] = jvec3(jkey(gp, "l").value("p", json()), vec3(0)); G.q[0] = jq(jkey(gp, "l").value("q", json()));
        G.lMag = jkey(gp, "l").value("anchor", std::string()) != "grip"; G.gun = jvec3(gp.value("gun", json()), vec3(0)); G.scale = gp.value("scale", 1.0f); if (gp.contains("butt") && gp["butt"].size() >= 2) G.butt = vec3(gp["butt"][0].get<float>(), gp["butt"][1].get<float>(), 0);   // "mag" / "support": from the left socket (the magazine's centre with hold.left = magazine), "grip": from the rightGrip socket
        if (gp.contains("fingers")) for (auto it = gp["fingers"].begin(); it != gp["fingers"].end(); ++it) G.fingers.push_back({it.key(), jq(it.value())}); } }   // SW2
    d->dotCrosshair = j.value("crosshair", std::string()) == "dot";   // SW2
    d->category = j.value("category", std::string());   // UI11
    d->laterWar = j.value("laterWar", false); d->moveSpeed = j.value("moveSpeed", 1.0f); d->showArms = j.value("arms", true);
    if (j.contains("scope")) { d->hasScope = true; d->scopeMag = jkey(j, "scope").value("mag", 2.5f); d->reticle = jkey(j, "scope").value("reticle", ""); d->scopeEye = jvec3(j["scope"]["eye"]); d->scopeIrons = jkey(j, "scope").value("irons", false); d->scopeRelief = jkey(j, "scope").value("relief", 0.07f); }
    if (j.contains("boltPivot")) { d->boltPivot = vec3(j["boltPivot"][0].get<float>(), j["boltPivot"][1].get<float>(), 0); d->boltLift = j.value("boltLift", 70.0f); }
    {
      std::string c = classes.value(d->id, std::string());
      d->wclass = c == "sniper" ? WC_SNIPER : c == "lmg" ? WC_LMG : c == "auto" ? WC_AUTO : c == "rifle" ? WC_RIFLE
                : d->hasScope ? WC_SNIPER : d->feed == "belt" ? WC_LMG : d->action == "auto" ? WC_AUTO : WC_RIFLE;
    }
    {   // WP4 visual recoil: the weapon's kind (vmRecoil.weapons; else from its class), then the weapon's own overrides
      json kinds = vmRecoil.value("kinds", json::object()), ws = vmRecoil.value("weapons", json::object());
      json w = ws.value(d->id, json::object());
      std::string kind = w.value("kind", d->wclass == WC_SNIPER ? "sniper" : d->wclass == WC_LMG ? "lmg" : d->wclass == WC_AUTO ? "assault" : "rifle");
      json kd = kinds.value(kind, json::object());
      { json& ov = weaponOverrides(); auto it = ov.find(d->id); if (it != ov.end() && it->is_object() && it->contains("_handling")) w.merge_patch((*it)["_handling"]); }   // ED2
      for (const json* src : {&kd, &w}) {
        const json& k = *src;
        d->vmBack = k.value("back_m", d->vmBack); d->vmPitch = k.value("pitch_deg", d->vmPitch); d->vmYaw = k.value("left_deg", d->vmYaw);
        d->vmOmega = k.value("omega", d->vmOmega); d->vmBackMax = k.value("backMax_m", d->vmBackMax); d->flashScale = k.value("flash", d->flashScale); d->yawScale = k.value("yawScale", d->yawScale);
        d->suppressed = k.value("suppressed", d->suppressed);
      }
    }
    std::map<std::string, int> mi;
    d->mats.push_back(Material{"default"});
    for (auto& mj : j.value("materials", json::array())) {
      Material m; m.name = mj.value("name", ""); m.albedo = jvec3(jkey(mj, "albedo"), vec3(0.5f)); m.roughness = mj.value("roughness", 0.6f);
      m.metallic = mj.value("metallic", 0.0f); m.grainAxis = mj.value("grain", 1);
      static const std::map<std::string, int> pat = {{"plain", PAT_PLAIN}, {"wood", PAT_WOOD}, {"metal", PAT_METAL}, {"fabric", PAT_FABRIC}};
      auto it = pat.find(mj.value("pattern", "plain")); m.pattern = it == pat.end() ? PAT_PLAIN : it->second;
      mi[m.name] = (int)d->mats.size(); d->mats.push_back(m);
    }
    for (int p = 0; p < WP_COUNT; ++p) {
      MeshData m;
      for (auto& pj : j.value("parts", json::object()).value(kPartNames[p], json::array())) {   // a gun with no box "parts" (NG1 stand-ins) must not throw
        Part q;
        q.center = jvec3(pj["c"]); q.size = jvec3(jkey(pj, "s"), vec3(0.01f));
        if (pj.contains("r")) q.rot = glm::quat(glm::radians(jvec3(pj["r"])));
        auto it = mi.find(pj.value("m", "")); q.mat = it == mi.end() ? 0 : it->second;
        appendPart(m, q, d->mats[q.mat], mat4(1));
      }
      d->mesh[p].upload(m);
    }
    d->file = f;
    if (j.contains("rail")) { d->rail = jvec3(jkey(j, "rail"), d->rail); d->hasRail = true; } d->railScale = j.value("railScale", 1.0f);   // AT7: with the local override (railOf re-read only the committed file)
    d->bulletMass = j.value("bulletMass", 10.0f);
    for (auto& m : j.value("modes", json::array())) d->modes.push_back(m.get<std::string>());
    d->deploys = j.value("deploys", "");
    if (j.contains("rocket")) { auto& rj = j["rocket"]; d->rocketDmg = rj.value("damage", 260.0f); d->rocketR = rj.value("radius", 1.6f); d->rocketCrater = rj.value("crater", 0.6f); d->rocketSplashGun = rj.value("splashGun", 40.0f);
      d->rocketGrav = rj.value("gravity", 0.35f); d->rocketAtKill = rj.value("atKill", true); d->rocketBackblast = rj.value("backblast", true); d->rocketArmour = rj.value("armourMult", 1.0f); }
    loadEquipment(*d, j);
    if (j.contains("sockets")) { d->gripR = jvec3(j["sockets"]["rightGrip"], d->gripR); d->gripL = jvec3(j["sockets"]["leftSupport"], d->gripL); }
    if (j.contains("pivots")) { const json& pv = j["pivots"];   // MG9: the model's own hinges
      d->coverPivot = jvec3(pv.value("cover", json()), d->coverPivot); d->coverDeg = pv.value("coverDeg", d->coverDeg);
      d->bipodPivot = jvec3(pv.value("bipod", json()), d->bipodPivot); d->bipodDeg = pv.value("bipodDeg", d->bipodDeg); }
    if (j.contains("projectileModel") && useImportedModels) {   // XB7: the round's own flying mesh
      std::string pd = jkey(j, "projectileModel").value("dir", ""); auto it = modelCache.find(pd);
      if (it == modelCache.end()) { auto gm = std::make_shared<GpuModel>(); std::string err; if (!gm->load(dir / "models" / pd, err)) { logf("weapon %s: projectile model '%s' not loaded (%s); a trail instead", d->id.c_str(), pd.c_str(), err.c_str()); gm.reset(); } it = modelCache.emplace(pd, gm).first; }
      d->projModel = it->second;
    }
    if (j.contains("model") && useImportedModels) {
      std::string dirName = jkey(j, "model").value("dir", "");
      auto it = modelCache.find(dirName);
      if (it == modelCache.end()) {
        auto gm = std::make_shared<GpuModel>();
        std::string err; gm->keepPos = true;   // AN1 step 2b: the hold cross-sections (measureHold)
        if (!gm->load(dir / "models" / dirName, err)) { logf("weapon %s: model '%s' not loaded (%s); using box parts", d->id.c_str(), dirName.c_str(), err.c_str()); gm.reset(); }
        it = modelCache.emplace(dirName, gm).first;
      }
      d->model = it->second;
      auto roles = [&](const GpuModel& gm, std::vector<int>* parts) {
        json hn = jkey(j, "model").value("hideNames", json::array());   // AT5: parts by name never drawn (the SMG-9's own sight)
        for (size_t k = 0; k < gm.parts.size(); ++k) {
          if (std::find(hn.begin(), hn.end(), json(gm.parts[k].name)) != hn.end()) continue;
          const std::string& r = gm.parts[k].role;
          int role = WP_BODY;
          for (int p = 0; p < WP_COUNT; ++p) if (r == kPartNames[p]) role = p;
          parts[role].push_back((int)k);
        }
        for (auto& hp : jkey(j, "model").value("hideParts", json::array())) for (int p = 0; p < WP_COUNT; ++p) if (hp.get<std::string>() == kPartNames[p]) parts[p].clear();   // e.g. the Garand clip: never drawn
      };
      if (d->model) {
        measureHold(*d, d->model->cpuPos);
        const float ir = j.contains("ironsWithOptic") ? (j["ironsWithOptic"].is_number() ? j["ironsWithOptic"].get<float>() : 0.045f) : 0.0f;
        if (j.contains("factoryOptic")) factoryOptic(*d->model, j["factoryOptic"], d->id);   // AT8 (before the irons: the optic's pieces are not sights)
        if (ir > 0) splitIrons(*d->model, {d->rear, d->front}, ir, d->id);   // AT7
        roles(*d->model, d->modelParts);   // the split-off sights among them (brSightDraws drops them under an optic)
        fs::path ld = dir / "models" / dirName / "lod1";   // MG4: the reduced bake for third person and the world
        if (fs::exists(ld / "mesh.bin")) {
          auto lit = modelCache.find(dirName + "/lod1");
          if (lit == modelCache.end()) { auto gl = std::make_shared<GpuModel>(); gl->keepPos = true; std::string e2; if (!gl->load(ld, e2)) gl.reset(); lit = modelCache.emplace(dirName + "/lod1", gl).first; }
          d->modelLod = lit->second;
          if (d->modelLod && j.contains("factoryOptic")) factoryOptic(*d->modelLod, j["factoryOptic"], d->id + " (lod1)");
          if (d->modelLod) { if (j.contains("ironsWithOptic")) splitIrons(*d->modelLod, {d->rear, d->front}, j["ironsWithOptic"].is_number() ? j["ironsWithOptic"].get<float>() : 0.045f, d->id + " (lod1)");
            roles(*d->modelLod, d->modelPartsLod); }
        }
      }
    }
    if (j.contains("animated") && useImportedModels && !(d->id == "sten" && !R.perf.stenRig) && !(d->id == "stg44" && !R.perf.stg44Rig) && !(d->id == "mp40" && !R.perf.mp40Rig) && !(d->id == "m1918a2_bar" && !R.perf.barRig) && !(d->id == "m1_thompson" && !R.perf.thompsonRig) && !(d->id == "g43" && !R.perf.g43Rig) && !(d->id == "ppsh41" && !R.perf.ppshRig)) {   // FR1: likewise mp40rig / barrig / thompsonrig   // STR1: stenrig off keeps the generated arm; SG1: likewise stg44rig   // MG1: the source's own arms and clips (vm_anim.h); a failure keeps the static path
      const json& ab = j["animated"]; std::string err;
      d->anim = animVmLoad(dir / "models" / ab.value("dir", j.value("model", json::object()).value("dir", std::string())), ab, err);
      if (!d->anim) logf("weapon %s: animated viewmodel not used (%s): static model + generated arm", d->id.c_str(), err.c_str());
      else {
        d->showArms = false;   // the rig has its own arms
        float r = animVmClipDur(*d->anim, "reload"), re = animVmClipDur(*d->anim, "reload_empty"), fd = animVmClipDur(*d->anim, "fire");
        const float ri0 = animVmClipDur(*d->anim, "reload_insert");
        if (d->feed == "single" || (d->feed == "stripper" && ri0 > 0)) {   // one shell per insert clip; start / end around the loop when the rig has them   // KR13: a stripper rifle's top-off (single rounds) too, its empty reload the clip's own length
          float ri = ri0;
          if (ri > 0) { d->reloadRound = ri; d->reloadStart = std::max(0.0f, animVmClipDur(*d->anim, "reload_start")); d->reloadEnd = std::max(0.0f, animVmClipDur(*d->anim, "reload_end")); d->reloadInsert = ab.value("shellInsert", 0.7f); }
          else if (r > 0) d->reloadRound = r;
          if (d->feed == "stripper" && re > 0) d->reloadEmpty = re;
        }
        else if (r > 0) { d->reloadPartial = r; d->reloadEmpty = re > 0 ? re : r; d->reloadInsert = ab.value("insert", 0.65f); }
        if (ab.value("cycleInFire", false) && fd > 0) {   // the fire clip includes the pump / bolt stroke: the next shot waits for it
          if (d->action == "bolt") d->boltTime = std::max(d->boltTime, 0.9f * fd); else d->rpm = std::min(d->rpm, 60.0f / (0.9f * fd));
        }
        logf("weapon %s: animated viewmodel (reload %.2f / %.2f s, fire %.2f s)", d->id.c_str(), d->reloadPartial, d->reloadEmpty, fd);
      }
    }
    logf("weapon %-14s %s %s feed %-8s %d+%d", d->id.c_str(), d->action.c_str(), d->caliber.c_str(), d->feed.c_str(), d->loaded, d->reserve);
    weaponDefs.push_back(std::move(d));
  }
}

int WeaponState::reserveRounds() const { return std::accumulate(containers.begin(), containers.end(), 0); }

void App::resetWeapons() {
  wstate.clear();
  for (size_t k = 0; k < weaponDefs.size(); ++k) {
    auto& d = *weaponDefs[k];
    WeaponState s;
    s.loaded = d.loaded;
    for (int r = d.reserve; r > 0; r -= d.containerSize) s.containers.push_back(std::min(r, d.containerSize));
    s.reserveMax = d.loaded + d.reserve;   // allowance = the preset total (loaded rounds included)
    s.seed = 1234567u + (uint32_t)k * 7919u;   // deterministic shot-to-shot variation
    wstate[d.id] = s;
  }
  if (activeWeapon >= (int)weaponDefs.size()) activeWeapon = 0;
  adsT = 0; weaponMsg.clear();
  smithyRestoreFits(*this);   // SM3: parts fitted at a smithing table this match stay on (respawns, SD rounds)
  resetLoadout(); { ViewModelState keep = vm; vm = ViewModelState{}; vm.procedural = keep.procedural; vm.showArms = keep.showArms; vm.vmFov = keep.vmFov; }
  viewKickPitch = viewKickYaw = 0;
}

// Recoil never returns on its own: when a burst ends, the climb (and the horizontal wander) the pattern left is
// folded into the player's own aim. The sight line does not move on that frame; the aim simply stays where the
// recoil put it, and the player pulls down (mouse) to compensate. Also used on a weapon swap.
void App::settleRecoil(WeaponState& s) {
  if (s.recoilP == 0 && s.recoilY == 0) return;
  eyePitch = std::clamp(eyePitch + s.recoilP, -85.0f, 85.0f);
  eyeYaw = wrapDeg(eyeYaw + s.recoilY);
  s.recoilP = s.recoilY = 0;
}

static float wrand(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s % 100000u) / 100000.0f; }

// suppression flinch: a bullet hit on the local player moves the aim 0.6-1.2 deg in a random direction. Like settled
// recoil it stays in the aim (no automatic return); the player pulls back.
void App::hitFlinch(uint32_t seed) {
  seed |= 1u;   // xorshift needs a non-zero state
  float a = wrand(seed) * 6.2831853f, m = 0.6f + 0.6f * wrand(seed);
  eyePitch = std::clamp(eyePitch + m * std::sin(a), -85.0f, 85.0f);
  eyeYaw = wrapDeg(eyeYaw + m * std::cos(a));
  lastFlinch = m;
}

int App::weaponIndex(const std::string& id) const {
  for (size_t k = 0; k < weaponDefs.size(); ++k) if (weaponDefs[k]->id == id) return (int)k;
  // WR1 (user request): removed guns; old maps, racks, kits and saved loadouts naming them get the nearest kept one
  static const std::pair<const char*, const char*> kRemoved[] = {{"modern_m4", "modern_m4_irons"}, {"modern_saiga", "modern_shotgun"}};   // NG1: the Makarov is back
  for (auto& r : kRemoved) if (id == r.first) return weaponIndex(r.second);
  return -1;
}

void App::selectWeapon(int idx) {
  if (idx < 0 || idx >= (int)weaponDefs.size()) return;
  if (strictDDay && weaponDefs[idx]->laterWar) { toast(weaponDefs[idx]->name + " is hidden by the strict 6 June 1944 preset."); return; }
  WeaponState& cur = wstate[weaponDefs[activeWeapon]->id];
  if (idx != activeWeapon) weaponMsg.clear();   // NG2: the last gun's note ("4 rounds in the partial 50ae_mag7 kept") stayed up under the next gun
  settleRecoil(cur);   // the old weapon's climb stays in the aim (no snap back on the swap)
  if (cur.reload > 0) { if (cur.reloadKind != 4 && cur.reloadKind != 5) weaponMsg = "Reload interrupted (no rounds moved)"; cur.reload = 0; cur.reloadKind = 0; }   // MG1 kind 4: rounds already in   // nothing is committed until a reload completes
  if (cur.throwT >= 0 && weaponDefs[activeWeapon]->equipment()) { cur.throwT = -1; cur.struck = false; }   // wind-up cancelled: nothing thrown, nothing used
  activeWeapon = idx; adsT = 0; vm.snap = true;
  wstate[weaponDefs[idx]->id].triggerReleased = false;   // a trigger held through the swap never fires the new weapon
}

mat4 App::viewModelMatrix(const WeaponDef& d, const WeaponState& s) const {
  vec3 e = eyePos(), f = forward(eyeYaw + viewKickYaw, eyePitch + viewKickPitch);
  mat4 cam = glm::inverse(glm::lookAt(e, e + f, viewUp(f)));
  return cam * vmLocal(d, s);   // viewmodel.cpp: ADS alignment, spring, sway, bob, kick, reload dip
}

vec3 App::weaponMuzzleWorld(const WeaponDef& d, const WeaponState& s) const {
  vec3 w; if (d.anim && animVmPoint(*this, d, 2, w)) return w;   // MG1: the animated rig's muzzle
  return vec3(viewModelMatrix(d, s) * vec4(d.muzzle, 1));
}

bool App::weaponCamera(float& fov) const {
  if (occupied || weaponDefs.empty() || mode != Mode::Explore) return false;
  const WeaponDef& d = *weaponDefs[activeWeapon];
  if (adsT <= 0) return false;
  // One blend drives the world FOV, the weapon pose (vmTarget) and the weapon FOV (vmFovNow): t = smoothstep(adsT),
  // adsT moving at 1 / move.adsTime per second toward the held state, so entry, exit and a mid-way reversal are
  // continuous. The optic view (scope mask, real magnification) still switches at adsT 0.85 with the mask.
  float t = adsT * adsT * (3 - 2 * adsT);
  const float hip = settings().fov();   // 72 unless changed in Settings; optics keep their real magnification of the 72 deg view
  { float m; int rt; if (lootScope(*this, m, rt)) { fov = adsT > 0.85f ? 72.0f / m : glm::mix(hip, 58.0f, t); return true; } }   // BR2: a fitted optic
  if (d.hasScope && !(d.scopeIrons && scopeIronsMode)) { fov = adsT > 0.85f ? 72.0f / d.scopeMag : glm::mix(hip, 58.0f, t); return true; }
  fov = d.adsZoom > 0 ? hip * (1.0f - d.adsZoom * t) : glm::mix(hip, d.adsWorldFov, t);   // zoom-only weapons narrow the hip FOV by a fraction
  return true;
}

bool App::scopeView() const {
  if (occupied || tanks.occupied || weaponDefs.empty() || mode != Mode::Explore) return false;
  float m; int rt;
  return (vmScoped(*weaponDefs[activeWeapon]) || lootScope(*this, m, rt)) && adsT > 0.85f;   // the same switch as the scope magnification (weaponCamera); BR2 fitted optics too
}

void App::weaponDrawItems(std::vector<DrawItem>& op) {
  R.viewModel.clear(); R.viewModelTex.clear();
  if (occupied || weaponDefs.empty() || mode != Mode::Explore) return;
  const WeaponDef& d = *weaponDefs[activeWeapon];
  const WeaponState& s = wstate[d.id];
  if (scopeView()) return;   // GM1b: scoped, only the eyepiece shows (black outside the circle, no weapon)
  if (d.anim && animVmDraw(*this, d)) { lastArmDraws = 0; return; }   // MG1: the animated rig (its own arms; no static weapon, no generated arm)
  mat4 M = viewModelMatrix(d, s);
  if (swimLower() >= 0.999f) { armDrawItems(M, d, s); return; }   // swimming (swim.cpp): weapon slung, hands-only stroke
  bool tex = d.useModel();
  // reload progress drives an imported reload clip (Garand: clip seats, follower and operating rod cycle)
  float animU = 1.0f;
  if (s.reload > 0 && s.reloadKind && s.reloadTotal > 0) animU = std::clamp(1 - s.reload / s.reloadTotal, 0.0f, 1.0f);
  auto add = [&](int p, const mat4& m) {
    if (tex && (p != WP_SCOPE || !d.modelParts[p].empty())) {   // a model without a scope keeps the box-part scope (M1C)
      for (int k : d.modelParts[p]) {
        const ModelPart& mp = d.model->parts[k];
        if (p == WP_CLIP && s.loaded == 0 && s.reload <= 0) continue;   // en-bloc clip ejected
        R.viewModelTex.push_back({d.model.get(), k, mp.anim.empty() ? m : M * d.model->animAt(k, animU)});
      }
      return;
    }
    if (d.mesh[p].count) R.viewModel.push_back({&d.mesh[p], m});
  };
  add(WP_BODY, M); add(WP_SCOPE, M);
  brSightDraws(*this, d, s, M, R.viewModelTex, R.viewModel);   // BR3: the fitted optic on its rail
  lootAttachDraws(*this, d, s, M, R.viewModelTex, R.viewModel);   // BR10: muzzle device, extended magazine, recoil pad
  // bolt: bolt actions lift and draw the handle; automatics move with each shot
  if (d.action == "bolt") {
    float u = d.boltTime > 0 ? std::clamp(1 - s.bolt / d.boltTime, 0.0f, 1.0f) : 1;
    float phase = s.bolt > 0 ? std::sin(u * 3.14159f) : 0;
    if (s.reload > 0 && s.reloadKind) phase = 1;
    mat4 b = M * glm::translate(mat4(1), d.boltPivot) * glm::mat4_cast(glm::angleAxis(radians(d.boltLift * std::min(1.0f, phase * 2)), vec3(0, 0, 1))) * glm::translate(mat4(1), vec3(-d.boltPivot.x, -d.boltPivot.y, -0.09f * phase));
    if (!d.boltCurve.empty()) {   // KF2: the clip's lift, draw and push (the handle turns about the bore, then the bolt slides back)
      float row[7]; boltCurveAt(d, s.bolt, row);
      if (s.reload > 0 && s.reloadKind) { row[0] = 1; row[1] = 1; }
      b = M * kf2BoltLocal(d, row[0], row[1]);
    }
    add(WP_BOLT, b);
  } else {
    float cyc = std::clamp(s.cooldown * (d.rpm / 60.0f), 0.0f, 1.0f);
    add(WP_BOLT, M * glm::translate(mat4(1), vec3(0, 0, -0.05f * std::sin(cyc * 3.14159f))));
  }
  float hz = s.cooldown > 0 && d.action == "semi" ? 0.08f * std::sin(std::clamp(s.cooldown * (d.rpm / 60.0f), 0.0f, 1.0f) * 3.14159f) : 0;
  if (d.fireCurveHandle) { float row[10]; fireCurveSum(d, s, row, hz); }   // GR1: the measured rod stroke
  if (s.reload > 0 && d.feed == "enbloc" && !d.magReload) hz = 0.09f;
  if (d.action == "bolt" && !d.boltCurve.empty()) {   // KF2: the shroud / safety rides back with the bolt (it does not turn)
    float row[7]; boltCurveAt(d, s.bolt, row); hz = d.boltTravel * ((s.reload > 0 && s.reloadKind) ? 1.0f : row[1]);
  }
  add(WP_HANDLE, M * glm::translate(mat4(1), vec3(0, 0, -hz)));
  // magazine: drops out and a fresh one comes up during a magazine reload
  if (d.feed == "magazine") {
    float y = 0;
    if (s.reload > 0) { float u = 1 - s.reload / s.reloadTotal; y = u < 0.5f ? -0.35f * (u / 0.5f) : -0.35f * (1 - (u - 0.5f) / 0.5f); }
    if (!s.magOff || s.reload > 0) add(WP_MAG, M * glm::translate(mat4(1), vec3(0, y, 0)));
  }
  // MG 42 top cover opens for a belt change; bipod unfolds when supported
  if (d.feed == "belt") {
    float a = 0;
    if (s.reload > 0) { float u = 1 - s.reload / s.reloadTotal; a = u < 0.25f ? u / 0.25f : u > 0.75f ? (1 - u) / 0.25f : 1; }
    add(WP_COVER, M * glm::translate(mat4(1), d.coverPivot) * glm::mat4_cast(glm::angleAxis(radians(d.coverDeg * a), vec3(1, 0, 0))) * glm::translate(mat4(1), -d.coverPivot));
    float leg = supported ? 1.0f : 0.0f;
    add(WP_BIPOD, M * glm::translate(mat4(1), d.bipodPivot) * glm::mat4_cast(glm::angleAxis(radians(d.bipodDeg * leg), vec3(1, 0, 0))) * glm::translate(mat4(1), -d.bipodPivot));
    // MG9: a drum / belt box on the gun (imported "mag" parts) comes off and a fresh one goes on while the cover is open
    float my = 0, mx = 0;
    if (s.reload > 0) { float u = 1 - s.reload / s.reloadTotal; float k = u < 0.5f ? u / 0.5f : 1 - (u - 0.5f) / 0.5f; my = -0.30f * k; mx = 0.12f * k; }
    add(WP_MAG, M * glm::translate(mat4(1), vec3(mx, my, 0)));
  }
  // en-bloc / stripper clip visible while it is being loaded (imported en-bloc clip: always seated, animated)
  if (tex && d.feed == "enbloc") add(WP_CLIP, M);
  else if (tex && d.action == "launcher") { if (s.reload > 0) { float u = 1 - s.reload / std::max(0.01f, s.reloadTotal); add(WP_CLIP, M * glm::translate(mat4(1), vec3(0, 0.1f * (1 - u), -0.6f * (1 - u)))); } }
  else if (s.reload > 0 && (s.reloadKind == 1 || s.reloadKind == 2) && (d.feed == "enbloc" || d.feed == "stripper")) {
    float u = 1 - s.reload / s.reloadTotal;
    float y = u < 0.6f ? 0.12f * (1 - u / 0.6f) : 0;
    if (u < 0.85f) add(WP_CLIP, M * glm::translate(mat4(1), vec3(0, y, 0)));
  }
  if (!scopeView()) armDrawItems(M, d, s);   // scoped: no arms (the support arm read as a flat plank beside the eyepiece)
  if (vmDebugYaw != 0) {   // FA2 debug side view (self-test shots): the whole first-person rig, weapon and arm together, turned about a point ahead of the eye
    mat4 camW = M * glm::inverse(vmLocal(d, s)); vec3 piv(0.05f, -0.15f, -0.45f);
    mat4 X = camW * glm::translate(mat4(1), piv + vec3(-0.05f, 0.08f, -0.35f)) * glm::rotate(mat4(1), radians(vmDebugYaw), vec3(0, 1, 0)) * glm::translate(mat4(1), -piv) * glm::inverse(camW);
    for (auto& it : R.viewModel) it.model = X * it.model;
    for (auto& it : R.viewModelTex) it.m = X * it.m;
  }
  (void)op;
}

std::string App::resupplyPersonal(uint64_t crateId) {
  if (weaponDefs.empty()) return "";
  const WeaponDef& d = *weaponDefs[activeWeapon];
  return resupplyWeapon(crateId, d, wstate[d.id]);
}
std::string App::resupplyWeapon(uint64_t crateId, const WeaponDef& d, WeaponState& s) {
  auto& stock = crateStock[crateId];
  if (!stock.count(d.container)) return d.name + ": this crate holds no " + d.container + " (wrong feed type).";
  bool unlimited = crateUnlimited[crateId];
  int have = s.loaded + s.reserveRounds();
  int want = s.reserveMax - have;   // refill to the preset allowance: a full weapon plus a full reserve
  if (want <= 0) return d.name + " already full (" + std::to_string(s.loaded) + " / " + std::to_string(s.reserveRounds()) + "). Crate untouched.";
  int units = (want + d.containerSize - 1) / d.containerSize;   // the crate pays in whole containers
  if (!unlimited) units = std::min(units, stock[d.container]);
  if (units <= 0) return d.name + ": crate has no " + d.container + " left.";
  int total = std::min(s.reserveMax, have + units * d.containerSize);
  s.reload = 0;   // a reload in progress would move rounds again when it ends
  s.loaded = std::min(d.loaded, total);
  s.containers.clear();
  for (int r = total - s.loaded; r > 0; r -= d.containerSize) s.containers.push_back(std::min(r, d.containerSize));
  if (!unlimited) stock[d.container] -= units;
  audio.play("crate", doc.find(crateId)->pos);
  return "Refilled " + d.name + ": " + std::to_string(s.loaded) + " / " + std::to_string(s.reserveRounds()) + ".";
}

void App::stepWeapons(float dt, const ExploreInput& in) {
  if (weaponDefs.empty()) return;
  // G4 stack switching (arsenal.cpp): Q steps down the stack and an empty weapon slot falls to the knife; the wheel
  // steps either way over the filled rows; 1 / 2 weapon slots (empty: the knife), 3 the knife, 4 the grenades in turn
  bool pick = !(pauseMenu || showLoadout || choiceFor || rackOpen);
  int slotKey = !pick || (build.active && build.engineer) ? 0 : in.choose1 ? 1 : in.choose2 ? 2 : in.slot3 ? 3 : in.slot4 ? 4 : 0;   // GM3: digits pick the Engineer's item while he builds
  int wheelStep = pick && in.wheel != 0 ? (in.wheel > 0 ? -1 : 1) : 0;
  if (in.nextWeapon || slotKey || wheelStep) {
    if (carried.empty()) resetLoadout();
    std::vector<int> rows = stackRows();
    int n = (int)rows.size(), cur = -1, knife = -1, to = -1;
    for (int k = 0; k < n; ++k) { if (rows[k] == activeWeapon) cur = k; if (knife < 0 && rows[k] >= 0 && weaponDefs[rows[k]]->melee.on) knife = k; }
    auto usable = [&](int k) { return rows[k] >= 0 && !(strictDDay && weaponDefs[rows[k]]->laterWar); };
    if (in.nextWeapon) { to = (cur + 1) % n; if (!usable(to)) to = knife >= 0 ? knife : to; }
    else if (wheelStep) { to = cur < 0 ? 0 : cur; for (int k = 0; k < n; ++k) { to = (to + wheelStep + n) % n; if (usable(to)) break; } }
    else if (slotKey <= 2) to = usable(slotKey - 1) ? slotKey - 1 : knife;
    else if (slotKey == 3) to = knife;
    else for (int k = 1; k <= n; ++k) { int j = ((cur < 2 ? 1 : cur) + k) % n; if (j >= 2 && usable(j) && weaponDefs[rows[j]]->grenade.on) { to = j; break; } }
    if (to >= 0 && to < n && usable(to) && rows[to] != activeWeapon) selectWeapon(rows[to]);
    stackShownAt = now;
  }
  const WeaponDef& d = *weaponDefs[activeWeapon];
  WeaponState& s = wstate[d.id];
  bool modal = pauseMenu || showLoadout || choiceFor || rackOpen;
  if (in.fireMode && !modal) toggleFireMode(d, s);
  if (d.action == "heavy") {   // carried M2: no firing; LMB deploys it on its tripod; RMB brings the rear ring over the front post
    { const float as = d.adsSpeed * (wstate[d.id].att[ATT_SIGHT] == 6 ? 0.8f : 1.0f);   // TH1: a thermal scope is slower to the eye
    adsT = std::clamp(adsT + (in.aim && !modal ? as / std::max(0.05f, move.adsTime) : -as / std::max(0.05f, move.adsTime)) * dt, 0.0f, 1.0f); }
    stepViewModel(dt, in, d, s);
    if (in.fire && !modal && s.triggerReleased) { s.triggerReleased = false; deployHeavy(d, s); }
    if (!in.fire) s.triggerReleased = true;
    viewKickPitch = viewKickYaw = 0;
    return;
  }
  bool fireHeld = in.fire && !modal && (glfwGetWindowAttrib(win, GLFW_FOCUSED) || selftest);
  // H: throw the side's grenade from any weapon, then go back to it
  if (quickReturnT > 0) { quickReturnT -= dt; if (quickReturnT <= 0) { int back = quickReturn; quickReturn = -1; quickReturnT = -1; if (back >= 0) selectWeapon(back); return; } }
  if (in.quickGrenade && !modal && quickReturn < 0 && !d.grenade.on) {
    int g = weaponIndex(d.side == "DE" ? "m24_grenade" : "mk2_grenade");
    if (g >= 0) {
      WeaponState& gs = wstate[weaponDefs[g]->id];
      if (gs.loaded > 0 || !gs.containers.empty()) {
        quickReturn = activeWeapon; selectWeapon(g);
        if (gs.loaded == 0) { gs.containers.pop_back(); gs.loaded = 1; }
        gs.cooldown = 0; gs.throwT = 0; gs.triggerReleased = false;
        return;
      } else weaponMsg = weaponDefs[g]->name + ": none left";
    }
  }
  if (d.tool) {   // GM3: the Engineer's hammer never fires; it swings while a build is held (builder.cpp)
    adsT = 0;
    stepViewModel(dt, in, d, s);
    if (build.active && build.engineer && build.holdT > 0) { if (s.throwT < 0) s.throwT = 0; s.throwT += dt; if (s.throwT >= 0.55f) s.throwT = 0; }
    else s.throwT = -1;
    viewKickPitch = vm.breathPitch; viewKickYaw = vm.breathYaw;
    return;
  }
  if (d.equipment()) {   // grenades and the knife: no sights, no recoil, no reload key
    adsT = 0;
    stepViewModel(dt, in, d, s);
    s.cooldown = std::max(0.0f, s.cooldown - dt);
    if (!fireHeld) s.triggerReleased = true;
    stepEquipment(dt, in, d, s, fireHeld);
    if (d.anim) animVmStep(*this, d, s, dt); else animVmNotActive();   // MG7: the knife's own arms and slash
    viewKickPitch = vm.breathPitch; viewKickYaw = vm.breathYaw;
    return;
  }
  // bolt actions (Kar98k, ZF39): a shot drops out of ADS (adsT = 0 at the shot); aiming waits for the bolt and the
  // cooldown, then a held aim key re-enters
  const bool holdAds = (d.boltDelay > 0 && s.bolt > d.boltTime) || (!d.boltCurve.empty() && !d.anim && s.bolt > 0);   // KF1: shouldered until the bolt is worked; KF2: and through the cycle (the clip works the bolt on the sights); KR2: not on an animated rig (KR1 karrig: its hand draws the bolt to the eye; the rig comes off the sights for the cycle, as the user's clip)
  bool aimBlocked = d.action == "bolt" && !holdAds && (s.bolt > 0 || s.cooldown > 0);
  float adsRate = d.adsSpeed / std::max(0.05f, move.adsTime) * (s.att[ATT_STOCK] ? 1.0f / kPadAdsTime : 1.0f);   // the only writer of adsT while a weapon is out; BR2 recoil pad: ADS time -25 %
  adsT = std::clamp(adsT + (in.aim && !modal && !aimBlocked ? adsRate : -adsRate) * dt, 0.0f, 1.0f);
  stepViewModel(dt, in, d, s);
  if (d.anim) animVmStep(*this, d, s, dt); else animVmNotActive();   // MG1: clips, pose, rig placement
  auto ch = Physics::character();
  // bipod deployed: a bipod weapon (belt MG, or a "supported" recoil factor) rested while prone or crouched and still.
  // A folded bipod (standing / moving) gives no benefit.
  bool hasBipod = d.feed == "belt" || d.rSupported < 0.999f;
  bool moving = glm::length(in.move) >= 0.01f;
  supported = hasBipod && (ch.posture == Physics::PRONE || ch.crouched) && !moving;
  int stance = supported ? STANCE_MOUNTED : ch.posture == Physics::PRONE ? STANCE_PRONE : ch.crouched ? STANCE_CROUCHED
             : !moving ? STANCE_STANDING : in.sprint && adsT < 0.5f ? STANCE_SPRINTING : STANCE_MOVING;
  {   // AT2 watchdog: no weapon timer outlives its longest legitimate value (a stuck one would lock firing / aiming)
    auto wd = [&](float& v, float maxV, const char* what) { if (v > maxV) { logf("weapon watchdog: %s %s %.2f s > %.2f s: cleared", d.id.c_str(), what, v, maxV); v = 0; } };
    wd(s.cooldown, 60.0f / std::max(1.0f, d.rpm) + 2.0f, "cooldown"); wd(s.bolt, d.boltTime + 1.0f, "bolt");
    if (s.reload > 0) wd(s.reload, std::max(s.reloadTotal, 0.0f) + 1.0f, "reload");
    if (!std::isfinite(adsT)) { logf("weapon watchdog: %s ADS fraction not finite: reset", d.id.c_str()); adsT = 0; }
  }
  s.cooldown = std::max(0.0f, s.cooldown - dt);
  float boltPrev = s.bolt;
  s.bolt = std::max(0.0f, s.bolt - dt);
  if (d.action == "bolt" && d.boltTime > 0 && boltPrev > s.bolt && !audio.hasEvent("bolt_cycle_" + d.id)) {   // working the bolt: open, then close (bank foley; "bolt" fallback; SND4: the cycle recording plays at the shot instead)
    auto crossed = [&](float u) { float k = d.boltTime * (1 - u); return boltPrev > k && s.bolt <= k; };
    if (crossed(0.25f)) audio.play("bolt", eyePos(), 0.6f);
    if (crossed(0.7f)) audio.play("bolt_close", eyePos(), 0.6f);
  }
  // recoil: every shot adds aim displacement and nothing pulls it back (no climb ceiling, no automatic recovery).
  // While a burst runs the displacement is held in recoilP / recoilY; once it ends it becomes the player's aim
  // (settleRecoil), so the view stays elevated where the pattern ended. The player's own mouse look is separate,
  // so pulling down compensates, during the burst or after it.
  s.sinceShot += dt;
  for (float& a : s.curveAge) a += dt;   // KF1 / GR1
  float cycle = 60.0f / std::max(1.0f, d.rpm);
  if (s.sinceShot > cycle * 1.6f + 0.06f) settleRecoil(s);
  if (gFxRecoil) {   // fxrecoil (spec 7.5): the shoulder is a damped spring per weapon class; impulses add (the climb in a burst is emergent)
    const vec2 fz = fxSmallArmsSpring(d); const float w = 6.2831853f * fz.x, z = fz.y;
    if (s.boltT >= 0 && s.sinceShot >= s.boltT) { s.kickV -= s.boltK; s.boltT = -1; }   // the carrier hits the back of the receiver
    auto under = [&](float& x, float& v) { for (int k = 0; k < 4; ++k) { const float h = dt / 4; v += (-w * w * x - 2 * z * w * v) * h; x += v * h; } };
    under(s.kick, s.kickV); under(s.kickP, s.kickPV); under(s.kickY, s.kickYV);
    if (s.kick > d.vmBackMax) { s.kick = d.vmBackMax; s.kickV = std::min(s.kickV, 0.0f); }
  } else {   // WP4: the visual recoil springs (critically damped, exact step: a sharp kick, a controlled return, no bounce)
    float w = d.vmOmega, e = std::exp(-w * dt);
    auto spring = [&](float& x, float& v) { float c = v + w * x; x = (x + c * dt) * e; v = (v - w * c * dt) * e; };
    spring(s.kick, s.kickV); spring(s.kickP, s.kickPV); spring(s.kickY, s.kickYV);
    if (s.kick > d.vmBackMax) { s.kick = d.vmBackMax; s.kickV = std::min(s.kickV, 0.0f); }   // never into the camera
  }
  if (!fireHeld) {
    if (s.burst > 1 && d.action == "auto") audio.play("tail_" + d.id, weaponMuzzleWorld(d, s), 0.9f);   // recorded tail, once per release
    s.triggerReleased = true; s.burst = 0;
  }
  float cP = 0, cY = 0; viewKickRoll = 0;
  if (!d.fireCurve.empty()) {   // KF1: the camera rides the measured curve; each shot's last row goes into the aim once (GR1: shots overlap)
    for (int n = 0; n < 4; ++n) {
      float row[10];
      if (!s.curveFolded[n] && !fireCurveAt(d, s.curveAge[n], row)) { s.recoilP += s.curveGain[n] * row[4]; s.recoilY += s.curveGain[n] * row[5]; s.curveFolded[n] = true; }
    }
    float row[10], h; fireCurveSum(d, s, row, h); cP = row[4]; cY = row[5]; viewKickRoll = row[8];
  }
  viewKickPitch = s.recoilP + cP + vm.breathPitch; viewKickYaw = s.recoilY + cY + vm.breathYaw;   // breathing moves the whole sight picture
  if (d.flame.on) { stepFlameLocal(dt, in, d, s, fireHeld); return; }   // FT1: hold to spray; fuel by the second, no reload
  // ---- reload start
  if (in.reload && s.reload <= 0) {
    if (s.loaded >= d.cap) weaponMsg = "Already full";
    else if (s.reserveRounds() == 0) weaponMsg = "No reserve " + d.container + " - find a supply crate";
    else if (d.feed == "single" || (d.feed == "stripper" && s.loaded > 0)) { s.reloadKind = 3; s.reload = s.reloadTotal = d.reloadStart + d.reloadRound; audio.play(d.action == "launcher" ? "rocket_ready" : "bolt", eyePos(), 0.6f); }
    else { s.reloadKind = s.loaded == 0 ? 1 : 2; s.reload = s.reloadTotal = s.loaded == 0 ? d.reloadEmpty : d.reloadPartial; audio.play(audio.hasEvent("reload_" + d.id) ? "reload_" + d.id : d.feed == "belt" ? "belt" : d.feed == "magazine" ? "mag_out" : "bolt", eyePos(), 0.7f); }
  }
  // ---- reload progress / completion: rounds move between locations, never appear
  if (s.reload > 0) {
    if ((s.reloadKind == 3 || s.reloadKind == 5 || (s.reloadKind == 4 && d.feed == "single")) && fireHeld && s.loaded > 0) { s.reload = 0; s.reloadKind = 0; }   // fire interrupts single-round loading
    s.reload -= dt;
    if ((s.reloadKind == 1 || s.reloadKind == 2) && d.feed == "magazine" && s.reloadTotal > 0 && s.reload <= s.reloadTotal * 0.5f) s.magOff = true;   // WP5: mag-out (the old one is at the bottom of its drop)
    // MG2 shell loop: the round goes in at the insert moment of its segment (kind 5: the segment plays out), then the next
    // segment, or the end clip after the last round
    bool shellNow = s.reloadKind == 3 && d.feed == "single" && d.reloadInsert > 0 && s.reload <= d.reloadRound * (1 - d.reloadInsert);
    if (s.reloadKind == 5 && s.reload <= 0) {
      if (s.loaded < d.cap && s.reserveRounds() > 0) { s.reload = s.reloadTotal = d.reloadRound; s.reloadKind = 3; }
      else if (d.reloadEnd > 0) { s.reload = s.reloadTotal = d.reloadEnd; s.reloadKind = 4; }
      else { s.reload = 0; s.reloadKind = 0; }
    }
    bool insertNow = (s.reloadKind == 1 || s.reloadKind == 2) && d.reloadInsert > 0 && s.reloadTotal > 0 && s.reload <= s.reloadTotal * (1 - d.reloadInsert);   // MG1
    if (s.reloadKind == 4 && s.reload <= 0) { s.reload = 0; s.reloadKind = 0; }   // MG1: the clip after the insert has ended
    else if ((s.reload <= 0 || insertNow || shellNow) && s.reloadKind && s.reloadKind != 5) {
      float rest = std::max(0.0f, s.reload);
      auto fullest = [&]() { return std::max_element(s.containers.begin(), s.containers.end()); };
      if (s.reloadKind == 3) {
        auto it = std::min_element(s.containers.begin(), s.containers.end(), [](int a, int b) { return (a == 0 ? 1 << 30 : a) < (b == 0 ? 1 << 30 : b); });
        if (it != s.containers.end() && *it > 0) { --*it; ++s.loaded; }
        s.containers.erase(std::remove(s.containers.begin(), s.containers.end(), 0), s.containers.end());
        audio.play(d.action == "launcher" ? "rocket_in" : "clip_in", eyePos(), 0.5f);   // the banks have no rocket-loading foley: launchers keep the procedural clicks
        if (shellNow) s.reloadKind = 5;   // MG2: the segment finishes before the next round
        else if (s.loaded < d.cap && s.reserveRounds() > 0) { s.reload = s.reloadTotal = d.reloadRound; }   // keep loading singly
        else { s.reload = 0; s.reloadKind = 0; }
      } else if (d.feed == "stripper") {
        auto it = fullest(); int n = std::min(d.cap - s.loaded, *it); *it -= n; s.loaded += n;
        s.containers.erase(std::remove(s.containers.begin(), s.containers.end(), 0), s.containers.end());
        s.reload = 0; s.reloadKind = 0; s.bolt = 0.3f; audio.play(audio.hasEvent("clip_" + d.id) ? "clip_" + d.id : "clip_in", eyePos(), 0.7f);
      } else {
        // en-bloc clip / magazine / belt: the partial one in the gun is retained in reserve, the fullest goes in
        int retained = s.loaded;
        auto it = fullest(); int n = *it; s.containers.erase(it);
        if (retained > 0) { s.containers.push_back(retained); weaponMsg = std::to_string(retained) + (d.feed == "enbloc" ? " rounds in the ejected clip kept in reserve" : " rounds in the partial " + d.container + " kept"); }
        s.loaded = n; s.magOff = false;   // WP5: mag-in
        s.reload = 0; s.reloadKind = 0;
        if (rest > 0) { s.reload = rest; s.reloadKind = 4; }   // MG1: rounds in; the clip plays out before the next shot
        audio.play(audio.hasEvent("clip_" + d.id) ? "clip_" + d.id : d.feed == "enbloc" ? "clip_in" : d.feed == "belt" ? "belt" : "mag_in", eyePos(), 0.8f);
      }
    }
  }
  // ---- fire
  bool canCycle = s.cooldown <= 0 && s.bolt <= 0 && s.reload <= 0;
  if (d.spinUp > 0) {   // MG13: the barrels spin up before the first round (and down again off the trigger)
    s.spin = fireHeld && s.reload <= 0 ? std::min(d.spinUp, s.spin + dt) : std::max(0.0f, s.spin - 2 * dt);
    canCycle = canCycle && s.spin >= d.spinUp;
  }
  bool semi = d.action != "auto" || std::string(fireModeName(d, s)) == "SEMI";   // selector SEMI: one shot per press
  if (fireHeld && canCycle && (!semi || s.triggerReleased)) {
    s.triggerReleased = false;
    if (s.loaded <= 0) { if (!s.clicked) { audio.play("click", eyePos(), 0.6f); s.clicked = true; } }
    else {
      s.clicked = false;
      s.loaded -= 1; ++s.shotIdx; ++s.burst;
      s.cooldown = 60.0f / d.rpm;
      if (d.action == "bolt") { s.bolt = d.boltTime + d.boltDelay;   // KF1: the bolt waits boltDelay
        if (audio.hasEvent("bolt_cycle_" + d.id)) audio.play("bolt_cycle_" + d.id, eyePos(), 0.7f); }   // SND4: open-back, forward-close, twice
      vec3 eye = eyePos();
      vec3 aimDir = forward(eyeYaw + viewKickYaw, eyePitch + viewKickPitch);
      // dispersion: the class's hip / ADS cone (handling.json) x the stance (prone near laser-like, mounted near none)
      lastStance = stance; lastSpreadDeg = handling.cone(d.wclass, adsT > 0.85f && !(d.adsZoom > 0 && d.hipSpreadMul > 1), stance) * (adsT > 0.85f ? 1.0f : d.hipSpreadMul) * (s.att[ATT_MUZZLE] == 2 ? kBrakeSpread : 1.0f);   // BR2 muzzle brake: spread -30 %   // MG13d: a zoom-only gun with a wide hip cone (the minigun) aims with the plain class hip cone
      float spread = radians(lastSpreadDeg);
      vec3 rt = glm::normalize(glm::cross(aimDir, vec3(0, 1, 0))), up = glm::cross(rt, aimDir);
      float a = wrand(s.seed) * 6.2831853f, r = std::sqrt(wrand(s.seed)) * std::tan(spread);
      aimDir = glm::normalize(aimDir + rt * std::cos(a) * r + up * std::sin(a) * r);
      // aim from the eye: find the point under the sight line
      Physics::Hit h;
      vec3 P = Physics::castRayEx(eye, aimDir, 600, h) ? h.pos : eye + aimDir * 600.0f;
      // AT2: aimed through an optic (or a blaster's zoom) the bore crosses the line of sight at the zero, not at whatever the
      // eye ray meets far behind the target (soldiers are not in that ray): the reticle centre is then the impact at the zero
      if (adsT > 0.85f && (scopeView() || d.adsZoom > 0)) { float zr = std::max(5.0f, d.zero); if (glm::length(P - eye) > zr) P = eye + aimDir * zr; }
      // ...but sweep from the real muzzle: a wall beside the barrel stops the shot
      vec3 muzzle = weaponMuzzleWorld(d, s);
      vec3 start = muzzle;
      Physics::Hit hb;
      if (Physics::castRayEx(eye, muzzle - eye, glm::length(muzzle - eye), hb)) start = eye + glm::normalize(muzzle - eye) * std::max(0.0f, hb.t - 0.01f);
      vec3 dir = glm::normalize(P - start);
      if (glm::dot(dir, aimDir) < 0.5f) dir = aimDir;
      // Aimed fire: the round leaves the front sight tip F along the sight line V = normalize(F - R), where R is the
      // rear aperture, both in world space from the posed weapon (sway / breathing move the sights and the shot together).
      if (adsT > 0.85f && !vmScoped(d) && !d.sightLineOnly && d.adsZoom <= 0) {   // SW2: zoom aiming fires from the hip toward the dot
        vec3 Rw, Fw; vmNoRecoilRot = true; sightLineWorld(d, s, Rw, Fw); vmNoRecoilRot = false;   // WP4: the visual muzzle rise is not aim (the gameplay recoil is)
        // MG8: an animated rig's ADS puts both posed sights on the camera axis, so its line is the camera's; the axis itself
        // avoids the alignment's numeric residual (0.02 deg = 0.5 mrad on the M4 irons)
        // AT2: through a fitted magnified optic the reticle is the eyepiece's, drawn on the camera axis: fire along it too
        vec3 V = d.anim || scopeView() ? forward(eyeYaw + viewKickYaw, eyePitch + viewKickPitch) : glm::normalize(Fw - Rw);
        vec3 vr = glm::normalize(glm::cross(V, vec3(0, 1, 0))), vu = glm::cross(vr, V);
        V = glm::normalize(V + vr * std::cos(a) * r + vu * std::sin(a) * r);   // same dispersion draw as the hip path
        Physics::Hit hf;
        start = Fw;
        if (scopeView()) start = eye + forward(eyeYaw + viewKickYaw, eyePitch + viewKickPitch) * glm::dot(Fw - eye, forward(eyeYaw + viewKickYaw, eyePitch + viewKickPitch));   // AT2: the gun's own sight points sit under a fitted optic: its line of sight is the camera axis
        if (Physics::castRayEx(eye, start - eye, glm::length(start - eye), hf)) start = eye + glm::normalize(start - eye) * std::max(0.0f, hf.t - 0.01f);
        dir = V; lastSightR = Rw; lastSightF = Fw; lastSightV = glm::normalize(Fw - Rw);
      }
      // declared zero: tilt up by the drop at the zero range
      float g = d.action == "launcher" ? 9.81f * d.rocketGrav : 9.81f;   // rockets: reduced effective gravity (sustained motor); MG4 grenades: full
      float tz = d.zero / d.velocity, drop = d.blaster ? 0.0f : 0.5f * g * tz * tz;   // SW1: bolts do not drop
      dir = glm::normalize(dir + vec3(0, drop / d.zero, 0));
      int tracer = s.att[ATT_MUZZLE] == 1 ? 0 : d.tracerKind(s.shotIdx);   // BR2 suppressor: no tracers
      if (d.action == "launcher") {   // rocket: data-tuned blast through the shared destruction pipeline
        fireProjectile(start, dir, d.velocity, PROJ_ROCKET, d.rocketDmg, ~0ull, false, d.container);
        Projectile& rp = projectiles.back();
        rp.blastDmg = d.rocketDmg; rp.blastR = d.rocketR; rp.craterScale = d.rocketCrater; rp.splashGun = d.rocketSplashGun; rp.atKill = d.rocketAtKill; rp.gravScale = d.rocketGrav; rp.armourMult = d.rocketArmour;
        if (d.rocketBackblast) fx.backblast(weaponMuzzleWorld(d, s) - aimDir * 1.4f, -aimDir);   // content/settings/fx.json
        {   // RC2: the launch shoves the shooter: a backblast concussion shake and a roll of the view while the sight settles
          extern float gKickRoll;
          blastShake = std::max(blastShake, d.rocketBackblast ? 0.6f : 0.4f);
          gKickRoll = (d.rocketBackblast ? 2.2f : 1.4f) * (fx.rnd() < 0.5f ? -1.0f : 1.0f);
        }
      } else {
        size_t n0 = projectiles.size();
        fireRounds(d, start, dir, tracer, s.seed ^ (uint32_t)s.shotIdx * 7919u, -1);   // MG1: a shotgun's pellets
        { vec3 off = vmApparent(muzzle) - start; if (glm::length(off) < 1.5f) for (size_t i = n0; i < projectiles.size(); ++i) projectiles[i].visOff = off; }   // AT4: the rounds leave the drawn barrel; the ballistic start (the sight line when aimed) is unchanged
      }
      lastShotStart = start; lastShotDir = dir; lastShotMuzzle = muzzle;
      vec3 fxm = vmApparent(muzzle);   // effects are drawn with the world projection
      {   // WP4: along the drawn bore at the viewmodel's apparent muzzle (compact in ADS), the light at the world muzzle
        mat4 VM = viewModelMatrix(d, s); vec3 bore = glm::normalize(vec3(VM * vec4(d.muzzleDir, 0)));
        fx.muzzleFlash(fxm, bore, d.flashScale * (adsT > 0.85f ? 0.6f : 1.0f), d.suppressed || s.att[ATT_MUZZLE] == 1, muzzle, d.blaster ? blasterColour(d.blaster) : vec3(-1), d.caliber.c_str(), s.att[ATT_MUZZLE] == 2 ? "brake" : nullptr);
        if (R.perf.fxEv) R.exposureFreeze(0.15f);   // Battle Foundry 5.2: the own flash never moves the exposure (the lighting session's fxexpo)
      }
      fx.smoke(fxm, aimDir * 2.0f + vec3(0, 0.3f, 0), 0.05f, 0.7f + 0.5f * fx.rnd(), vec4(0.6f, 0.58f, 0.55f, 0.16f), 0.6f, 0.2f); fx.parts.back().fade = 1.6f;   // thin, short-lived, eroded wisp
      if (!(d.suppressed || s.att[ATT_MUZZLE] == 1)) fx.gunSmoke(fxm, aimDir, d.smoke, -1);   // WS1: a WW2 gun's lingering puff (the local player: gun -1)
      audio.play(lootSuppressedShot(d, s) && audio.hasEvent("shot_suppressed_rifle") ? std::string("shot_suppressed_rifle") : "shot_" + d.id + (d.action == "auto" && semi ? "_semi" : ""), muzzle, 1.0f);   // SND15; SND18: SEMI on an automatic plays its single (else falls back to shot_<id>)
      if (d.pellets > 1 && d.feed == "single") audio.play("shotgun_pump", eyePos(), 0.8f);   // MG12: tube-fed pumps only (not the break-action sawn-off)   // the pump cycling the next shell (the recording starts 0.3 s in)
      shotCasing(d, ch.pos, -1);   // SND4
      // recoil per shot = profile x stance (handling.json: crouched 0.85, prone 0.6) x 0.85 in ADS. Mounted keeps the
      // pre-overhaul values: the old posture factor (prone 0.20, crouched 0.70) x the weapon's own bipod factor
      // (recoil.supported; MG 42 prone 0.2 x 0.35 = 0.07), handling.json mounted 0.5 only for a bipod weapon without one.
      // Standing still kicks pitch only; moving adds movingYaw (25 %) of the yaw.
      float sc = stance != STANCE_MOUNTED ? handling.recoil[stance]
               : (ch.posture == Physics::PRONE ? 0.20f : 0.70f) * (d.rSupported < 0.999f ? d.rSupported : handling.recoil[STANCE_MOUNTED]);
      if (adsT > 0.85f) sc *= 0.85f;
      if (s.att[ATT_MUZZLE] == 2) sc *= kBrakeRecoil;   // BR2 muzzle brake: recoil -20 %
      float yawK = moving ? 1 + handling.movingYaw : 1.0f;
      lastRecoilScale = sc;
      // KF1: the measured curve drives aim and gun (no random recoil, no spring kick); GR1: an ADS-only curve takes the
      // aim blend's share of the shot, the random recoil and the spring the rest
      const float cw = d.fireCurve.empty() ? 0.0f : !d.fireCurveAds ? 1.0f : vmScoped(d) ? 0.0f : adsT * adsT * (3 - 2 * adsT);
      if (!d.fireCurve.empty()) {
        int& n = s.curveSlot;
        if (!s.curveFolded[n]) { float row[10]; fireCurveAt(d, 1e9f, row); s.recoilP += s.curveGain[n] * row[4]; s.recoilY += s.curveGain[n] * row[5]; }   // a fifth shot inside the curve: the oldest folds now
        s.curveAge[n] = 0; s.curveGain[n] = cw; s.curveFolded[n] = false; n = (n + 1) % 4; s.sinceShot = 0;
      }
      if (cw < 1) {
      const float pr = s.recoilP, py = s.recoilY;
      s.recoilP += sc * (d.rPitch * (0.85f + 0.3f * wrand(s.seed)) + d.rClimb * std::min(s.burst, 12));
      s.recoilP = std::min(s.recoilP, std::max(0.0f, 85.0f - eyePitch));   // only the camera's pitch bound (look clamp 85) limits the climb
      {   // WP4c: an erratic horizontal jerk every shot (+ = left): either side (slightly more often left), a wide spread of sizes
          // with an occasional big one; yawScale per kind (handling.json vmRecoil), the stance factor squared (crouched calmer,
          // prone / mounted small), moving worse
        float side = wrand(s.seed) < 0.55f ? 1.0f : -1.0f, mag = 0.3f + 0.9f * wrand(s.seed);
        if (wrand(s.seed) < 0.15f) mag *= 2.0f;
        s.recoilY = std::clamp(s.recoilY + sc * sc * yawK * d.yawScale * d.rYaw * side * mag + sc * d.rPitch * 0.04f, -8.0f, 8.0f);
      }
      s.recoilP = pr + (s.recoilP - pr) * (1 - cw); s.recoilY = py + (s.recoilY - py) * (1 - cw);   // GR1
      s.sinceShot = 0;
      {   // WP4: a shot's impulse into the springs (peak = the per-shot value after 1 / omega); bursts blend, never restart
        float k = 2.718282f * d.vmOmega * std::sqrt(sc);
        if (gFxRecoil) {   // fxrecoil: the same per-shot peaks (the weapon's tuning) from an underdamped spring: v0 = peak omega / firstPeak(zeta)
          const vec2 fz = fxSmallArmsSpring(d); k = 6.2831853f * fz.x / fxSpringPeak(fz.y) * std::sqrt(sc);
          if (gFxTier >= 3 && d.action != "bolt" && d.action != "pump") { s.boltT = 0.008f + 0.017f * wrand(s.seed); s.boltK = (0.15f + 0.2f * wrand(s.seed)) * d.vmBack * k; }
        }
        k *= 1 - cw;   // GR1
        s.kickV += d.vmBack * k; s.kickPV += d.vmPitch * k * (0.9f + 0.2f * wrand(s.seed)); s.kickYV += d.vmYaw * k * (0.6f + 0.8f * wrand(s.seed));
      }
      }
      if (d.action == "bolt" && d.boltDelay <= 0) adsT = 0;   // working the bolt takes the rifle off the shoulder (KF1: boltDelay keeps it there first)
      if (d.feed == "enbloc" && s.loaded == 0) { audio.play("ping", muzzle, 0.8f); weaponMsg = "Clip ejected (ping)"; s.clipPings++; }
    }
  }
}

// ---------------------------------------------------------------- G4: carried list + ammo readout (lower right)
// The reference (screens/refs/inventory_ref.png): right-aligned rows of white weapon silhouettes with the name across
// their lower part, "loaded/reserve" to the right, a key column at the far right, the selected row in a thin
// gold-brown box; white text with a soft shadow. The list shows while switching and fades 1.5 s later; the large
// "loaded / reserve" and the fire mode under it stay.
std::string stackAmmoText(App& a, int w) {
  if (w < 0) return "";
  const WeaponDef& d = *a.weaponDefs[w]; const WeaponState& s = a.wstate[d.id];
  if (d.melee.on) return "";
  if (d.grenade.on || d.pack.on) return std::to_string(s.loaded + (int)s.containers.size());
  if (d.flame.on) return std::to_string(s.loaded) + " %";   // fuel: the tank in percent
  return std::to_string(s.loaded) + "/" + std::to_string(s.reserveRounds());
}
static std::string readoutModeName(App& a, const WeaponDef& d, const WeaponState& s) {
  if (d.melee.on) return "Melee";
  if (d.grenade.on) return "Thrown";
  if (d.pack.on) return "Placed (10 s fuse)";
  std::string m = a.fireModeName(d, s);
  if (m == "FLAME") return "Flame";
  if (m == "AUTO") return "Full-auto";
  if (m == "SINGLE") return "Single-shot";
  if (m == "CARRY") return "Carried (deploy)";
  return d.action == "bolt" ? "Bolt-action" : "Semi-auto";
}
static void shadowText(ImDrawList* dl, ImFont* f, float sz, ImVec2 p, ImU32 col, const char* t, float a) {
  for (ImVec2 o : {ImVec2(1.5f, 1.5f), ImVec2(0, 1.5f), ImVec2(1.5f, 0)}) dl->AddText(f, sz, ImVec2(p.x + o.x, p.y + o.y), IM_COL32(0, 0, 0, (int)(110 * a)), t);
  dl->AddText(f, sz, p, (col & 0x00FFFFFF) | ((ImU32)(((col >> 24) & 0xFF) * a) << 24), t);
}
void App::drawStackUI(ImDrawList* dl, ImVec2 ds) {
  const WeaponDef& d = *weaponDefs[activeWeapon];
  const WeaponState& s = wstate[d.id];
  ImFont* f = ImGui::GetFont(); float fs = ImGui::GetFontSize();
  const float sc = std::clamp(ds.y / 1080.0f, 0.75f, 2.0f), right = ds.x - 28 * sc;
  // ---- readout: large "loaded / reserve" (grenades: the count; the knife: none), the fire mode under it
  std::string big = d.melee.on ? "" : d.grenade.on || d.flame.on || d.pack.on ? stackAmmoText(*this, activeWeapon) : std::to_string(s.loaded) + " / " + std::to_string(s.reserveRounds());
  float bigSz = fs * 2.1f * sc, midSz = fs * 1.15f * sc, smSz = fs * 1.0f * sc;
  std::string mode = readoutModeName(*this, d, s);
  ImVec2 mt = f->CalcTextSizeA(midSz, 1e9f, 0, mode.c_str());
  float yMode = ds.y - 26 * sc - mt.y;
  shadowText(dl, f, midSz, ImVec2(right - mt.x, yMode), IM_COL32(235, 235, 230, 255), mode.c_str(), 1);
  float yBig = yMode;
  if (!big.empty()) {
    ImVec2 bt = f->CalcTextSizeA(bigSz, 1e9f, 0, big.c_str());
    yBig = yMode - bt.y + 2 * sc;
    shadowText(dl, f, bigSz, ImVec2(right - bt.x, yBig), IM_COL32(245, 245, 240, 255), big.c_str(), 1);
  }
  lastReadout = big + " | " + mode;
  std::string st = s.reload > 0 ? (s.reloadKind == 3 || s.reloadKind == 5 ? "Loading rounds" : "Reloading") : s.bolt > 0 ? "Working the bolt" : (!d.equipment() && s.loaded == 0) ? (d.flame.on ? "Out of fuel - refill at an ammo crate" : "Empty - R to reload") : "";
  if (d.feed == "belt") st += std::string(st.empty() ? "" : "   ") + (supported ? "Bipod supported" : "Unsupported: crouch and stand still");
  float yInfo = yBig - smSz - 4 * sc;
  if (!st.empty()) { ImVec2 t = f->CalcTextSizeA(smSz, 1e9f, 0, st.c_str()); shadowText(dl, f, smSz, ImVec2(right - t.x, yInfo), IM_COL32(255, 205, 130, 255), st.c_str(), 1); yInfo -= smSz + 2 * sc; }
  if (!weaponMsg.empty()) { ImVec2 t = f->CalcTextSizeA(smSz, 1e9f, 0, weaponMsg.c_str()); shadowText(dl, f, smSz, ImVec2(right - t.x, yInfo), IM_COL32(255, 210, 130, 230), weaponMsg.c_str(), 1); yInfo -= smSz + 2 * sc; }
  // ---- the list: always shown unless hidden in Settings > Gameplay
  float a = showWeaponList && !smithyOpen() ? 1.0f : 0.0f;   // SM1: not over the fitting screen
  lastStackAlpha = a;
  if (a <= 0) return;
  std::vector<int> rows = lootActive() ? std::vector<int>{activeWeapon} : stackRows();   // BR2: in play only the gun in hand shows
  const float rowH = 62 * sc, silW = 190 * sc, silH = 46 * sc, keyW = 30 * sc, gap = 14 * sc;
  float y = yInfo - 10 * sc - rowH * rows.size();
  for (size_t k = 0; k < rows.size(); ++k, y += rowH) {
    int w = rows[k];
    float kx = right - keyW, x1 = kx - gap, x0 = x1 - silW - 80 * sc;
    // key column: a small rounded tag with the slot key (the grenades share 4)
    char key[2] = {(char)('1' + std::min<size_t>(k, 3)), 0};
    ImVec2 kc(kx + keyW * 0.5f, y + rowH * 0.45f);
    dl->AddRect(ImVec2(kc.x - 11 * sc, kc.y - 11 * sc), ImVec2(kc.x + 11 * sc, kc.y + 11 * sc), IM_COL32(230, 230, 225, (int)(200 * a)), 4 * sc, 0, 1.5f * sc);
    ImVec2 ks = f->CalcTextSizeA(smSz, 1e9f, 0, key);
    shadowText(dl, f, smSz, ImVec2(kc.x - ks.x * 0.5f, kc.y - ks.y * 0.5f), IM_COL32(240, 240, 235, 255), key, a);
    if (w < 0) continue;   // an empty weapon slot: just its key
    const WeaponDef& wd = *weaponDefs[w];
    // silhouette (white side view), the name across its lower part, the ammo to its right
    auto it = weaponSilhouette.find(wd.id);
    float sx0 = x0, sy0 = y + 4 * sc;
    if (it != weaponSilhouette.end() && it->second)
      dl->AddImage((ImTextureID)(intptr_t)it->second, ImVec2(sx0, sy0), ImVec2(sx0 + silW, sy0 + silH), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(235, 235, 232, (int)(225 * a)));
    shadowText(dl, f, midSz, ImVec2(sx0 + 6 * sc, sy0 + silH * 0.55f), IM_COL32(250, 250, 248, 255), wd.name.c_str(), a);
    std::string am = stackAmmoText(*this, w);
    if (!am.empty()) { ImVec2 t = f->CalcTextSizeA(midSz, 1e9f, 0, am.c_str()); shadowText(dl, f, midSz, ImVec2(x1 - t.x, sy0 + 2 * sc), IM_COL32(245, 245, 240, 255), am.c_str(), a); }
    if (w == activeWeapon) dl->AddRect(ImVec2(x0 - 6 * sc, y), ImVec2(x1 + 6 * sc, y + rowH - 4 * sc), IM_COL32(176, 138, 72, (int)(235 * a)), 2 * sc, 0, 1.6f * sc);
  }
}

void App::drawWeaponUI() {
  if (occupied || weaponDefs.empty()) return;
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const WeaponDef& d = *weaponDefs[activeWeapon];
  const WeaponState& s = wstate[d.id];
  ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  // scoped: the eyepiece, its surround and the reticle are drawn by the renderer (render.cpp eyepiece pass)
  std::vector<SightMark> mk;
  if (sightMarkers && sightMarks(mk)) {
    float sx = io.DisplaySize.x / std::max(1, R.width), sy = io.DisplaySize.y / std::max(1, R.height);
    const char* nm[5] = {"rear", "front", "muzzle", "flash", "aim"};
    for (auto& m : mk) {
      ImVec2 q(m.px.x * sx, m.px.y * sy);
      if (m.kind == 0 || m.kind == 4) dl->AddCircle(q, 6, m.col, 24, 1.5f);
      else if (m.kind == 1) { dl->AddLine(ImVec2(q.x - 7, q.y), ImVec2(q.x + 7, q.y), m.col, 1.5f); dl->AddLine(ImVec2(q.x, q.y - 7), ImVec2(q.x, q.y + 7), m.col, 1.5f); }
      else if (m.kind == 2) dl->AddRect(ImVec2(q.x - 3, q.y - 3), ImVec2(q.x + 3, q.y + 3), m.col, 0, 0, 1.5f);
      else dl->AddCircleFilled(q, 2, m.col);
      dl->AddText(ImVec2(q.x + 8, q.y - 16 + 10 * (m.kind % 3)), m.col, nm[m.kind]);
    }
    char cb[200]; vec2 ctr(R.width * 0.5f, R.height * 0.5f);
    std::snprintf(cb, sizeof cb, "%s  rear %.1f px  front %.1f px from the dot  | relief %.3f m  weapon FOV %.1f", d.id.c_str(), glm::length(mk[0].px - ctr), glm::length(mk[1].px - ctr), d.eyeRelief, vmFovNow());
    dl->AddText(ImVec2(20, io.DisplaySize.y - 60), IM_COL32(255, 255, 255, 230), cb);
  }
  drawReflexReticle(*this, dl, io.DisplaySize.x / std::max(1, R.width), io.DisplaySize.y / std::max(1, R.height));   // AT2: red dot / holographic mark
  drawSightOverlay(*this, dl, io.DisplaySize.x / std::max(1, R.width), io.DisplaySize.y / std::max(1, R.height));   // W2: centre, sight points, roll
  drawStackUI(dl, io.DisplaySize);   // G4: the carried list (while switching) and the ammo / fire-mode readout
  if (showLoadout) {
    ImGui::SetNextWindowPos(c, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Loadout / range (L)", &showLoadout, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::Checkbox("Strict 6 June 1944 preset (hides later-war M1C)", &strictDDay);
    ImGui::Checkbox("Range presentation: show every trajectory", &fx.rangeMode);
    // graphics presets, render quality, grass, gore, reduced motion, arms and sight markers: the Settings window
    if (ImGui::Button("Settings...")) settings().open = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Graphics (presets, render quality, grass), gameplay (sensitivity, gore, reduced motion, arms, sight markers), audio, controls, interface");
    if (ImGui::CollapsingHeader("Look development (docs/rendering.md)")) {   // tuning values outside the presets
      auto& q = R.q;
      ImGui::Checkbox("Legacy look (fallback)", &q.legacy);
      ImGui::BeginDisabled(q.legacy);
      ImGui::SetNextItemWidth(160); ImGui::SliderFloat("Shadow softness", &q.shadowSoft, 0.5f, 4.0f, "%.1f texels");
      ImGui::SetNextItemWidth(160); ImGui::SliderFloat("Haze density", &q.hazeDensity, 0.0f, 0.003f, "%.4f /m");
      ImGui::SetNextItemWidth(160); ImGui::SliderFloat("Cloud cover", &R.cloudCover, 0.0f, 1.0f, "%.2f");
      ImGui::EndDisabled();
    }
    ImGui::Separator();
    for (size_t k = 0; k < weaponDefs.size(); ++k) {
      auto& w = *weaponDefs[k];
      if ((strictDDay && w.laterWar) || w.modern) continue;   // MG1: the modern guns come only from their case
      auto& ws = wstate[w.id];
      char lbl[200];
      if (w.flame.on) std::snprintf(lbl, sizeof lbl, "%s%s  (fuel %d %%)", (int)k == activeWeapon ? "> " : "  ", w.name.c_str(), ws.loaded);
      else std::snprintf(lbl, sizeof lbl, "%s%s  (%d + %d)", (int)k == activeWeapon ? "> " : "  ", w.name.c_str(), ws.loaded, ws.reserveRounds());
      if (ImGui::Selectable(lbl, (int)k == activeWeapon)) { std::string m; equipWeapon((int)k, m); toast(m, 3); }   // G4: the 2-slot rule
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", w.note.c_str());
    }
    if (ImGui::Selectable("  Browning M2HB (mounted) - go to a placed gun")) {
      uint64_t best = 0; float bd = 1e9f;
      for (auto& i : doc.instances) if (assetFor(i)->station.kind == "m2hb") { float dd = glm::length(i.pos - eyePos()); if (dd < bd) { bd = dd; best = i.id; } }
      if (!best) toast("Place a Browning M2HB on the map to crew it.");
      else { showLoadout = false; occupyStation(best, ST_GUNNER); }
    }
    ImGui::End();
  }
}

// --weapon-sheets <dir>: orthographic side and rear views of every weapon on a centimetre grid, with the
// configured sights (rear red, front green, muzzle blue) and IK sockets (right yellow, left cyan) marked.
// --model-preview <glb> <outDir> [spec.json]: bake the GLB with an import spec (default: ground pivot, native scale)
// into <outDir>/bake and render top / front / side / three-quarter views with a 10 cm grid, for inspecting supplied
// models (which groups form complete objects, scale, axes) before writing their asset or weapon entries.
bool App::writeModelPreview(const fs::path& glb, const fs::path& outDir, const fs::path& specJson) {
  json sj = json::object();
  if (!specJson.empty()) { try { sj = json::parse(readTextFile(specJson)); } catch (...) { logf("preview: bad spec %s", specJson.string().c_str()); return false; } }
  if (!sj.contains("pivot")) sj["pivot"] = "ground";
  if (!sj.contains("scale") && !sj.contains("realLength")) sj["scale"] = 1.0;
  if (!sj.contains("maxTex")) sj["maxTex"] = 512;
  ImportSpec s = importSpecFromJson(sj);
  std::error_code ec; fs::create_directories(outDir, ec);
  std::string err, sum;
  if (!bakeGlbModel(glb, s, outDir / "bake", err, sum)) { logf("preview: bake failed: %s", err.c_str()); return false; }
  logf("preview: %s", sum.c_str());
  GpuModel gm; if (!gm.load(outDir / "bake", err)) { logf("preview: load failed: %s", err.c_str()); return false; }
  std::vector<ModelDraw> md; for (size_t k = 0; k < gm.parts.size(); ++k) md.push_back({&gm, (int)k, mat4(1)});
  vec3 lo = gm.bmin, hi = gm.bmax, c = (lo + hi) * 0.5f, ext = hi - lo;
  float R0 = glm::length(ext) * 0.5f + 0.05f;
  auto shot = [&](const char* tag, vec3 dir, vec3 up, bool persp) {
    int W = 1600, H = 1000; float asp = (float)W / H;
    vec3 eye = c - glm::normalize(dir) * (R0 * 3.0f);
    mat4 view = glm::lookAt(eye, c, up);
    mat4 proj = persp ? glm::perspective(radians(30.0f), asp, 0.01f, R0 * 10) : glm::ortho(-R0 * asp, R0 * asp, -R0, R0, 0.01f, R0 * 10);
    std::vector<uint8_t> px; R.renderModelSheet(md, {}, view, proj, eye, W, H, px);
    if (!persp) {   // 10 cm grid in the view plane through the centre
      vec3 rt = glm::normalize(glm::cross(glm::normalize(dir), up)), u2 = glm::cross(rt, glm::normalize(dir));
      for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        float hx = (x + 0.5f) / W * 2 * R0 * asp - R0 * asp + glm::dot(c, rt), hy = (1 - (y + 0.5f) / H) * 2 * R0 - R0 + glm::dot(c, u2);
        float px1 = 2 * R0 * asp / W;
        bool m1 = std::fabs(hx - std::round(hx)) < px1 * 0.6f || std::fabs(hy - std::round(hy)) < px1 * 0.6f;
        bool m01 = std::fabs(hx * 10 - std::round(hx * 10)) < px1 * 3 || std::fabs(hy * 10 - std::round(hy * 10)) < px1 * 3;
        if (m1 || m01) { uint8_t* q = &px[((size_t)y * W + x) * 4]; float a = m1 ? 0.6f : 0.18f; for (int k = 0; k < 3; ++k) q[k] = (uint8_t)(q[k] * (1 - a)); }
      }
    }
    stbi_write_png((outDir / (std::string(tag) + ".png")).string().c_str(), W, H, 4, px.data(), W * 4);
  };
  shot("top", vec3(0, -1, 0), vec3(0, 0, -1), false);
  shot("front", vec3(0, 0, -1), vec3(0, 1, 0), false);
  shot("side", vec3(1, 0, 0), vec3(0, 1, 0), false);
  shot("persp", glm::normalize(vec3(-1, -0.7f, -1.2f)), vec3(0, 1, 0), true);
  logf("preview: bounds (%.3f %.3f %.3f)-(%.3f %.3f %.3f), %d parts, %d tris", lo.x, lo.y, lo.z, hi.x, hi.y, hi.z, (int)gm.parts.size(), gm.tris);
  for (auto& p : gm.parts) logf("  part %-40s tris %d  (%.3f %.3f %.3f)-(%.3f %.3f %.3f)", p.name.c_str(), (int)(p.count / 3), p.bmin.x, p.bmin.y, p.bmin.z, p.bmax.x, p.bmax.y, p.bmax.z);
  gm.destroy();
  return true;
}

// G4: one white silhouette per weapon for the carried list: an orthographic side view (muzzle to the right) of the
// model (or its procedural parts), the background keyed out to alpha. Cached as PNG beside the exe
// (cache/weapon_silhouettes/<id>[_m<parts>_<bounds>].png, delete to rebuild) and uploaded once at start.
void App::makeWeaponSilhouettes() {
  const int W = 512, H = 128;
  fs::path dir = exeDir() / "cache" / "weapon_silhouettes";
  std::error_code ec; fs::create_directories(dir, ec);
  for (auto& dp : weaponDefs) {
    crumb("weapon silhouettes");   // UI5
    const WeaponDef& d = *dp;
    std::string key = d.id;   // MG10: an imported model's part count and bounds in the name, so a re-baked model draws a fresh icon
    if (d.useModel()) { char b[96]; std::snprintf(b, sizeof b, "_m%zu_%.3f_%.3f_%.3f", d.model->parts.size(), d.model->bmax.x, d.model->bmax.y, d.model->bmax.z); key += b; }
    fs::path p = dir / (key + ".png");
    std::vector<uint8_t> px;
    int w = 0, h = 0, ch = 0;
    if (stbi_uc* img = stbi_load(p.string().c_str(), &w, &h, &ch, 4)) {
      if (w == W && h == H) px.assign(img, img + (size_t)W * H * 4);
      stbi_image_free(img);
    }
    if (px.empty()) {
      std::vector<ModelDraw> md; std::vector<DrawItem> bx;
      vec3 bmin(-0.05f, -0.1f, 0), bmax(0.05f, 0.1f, 1.0f);
      if (d.useModel()) { for (int q = 0; q < WP_COUNT; ++q) for (int k : d.modelParts[q]) md.push_back({d.model.get(), k, mat4(1)}); bmin = d.model->bmin; bmax = d.model->bmax; }
      else for (int q = 0; q < WP_COUNT; ++q) if (d.mesh[q].count) bx.push_back({&d.mesh[q], mat4(1)});
      if (d.hasScope && !d.useModel()) bx.push_back({&d.mesh[WP_SCOPE], mat4(1)});
      vec3 c = (bmin + bmax) * 0.5f, ext = bmax - bmin;
      float hw = std::max(ext.z, 0.05f) * 0.52f, hh = hw * H / W;
      if (ext.y * 0.55f > hh) { hh = ext.y * 0.55f; hw = hh * W / H; }
      float D = glm::length(ext) + 1.0f;
      vec3 eye = c - vec3(D, 0, 0);   // looking +x: screen right is +z, the muzzle
      std::vector<uint8_t> rgba;
      R.renderModelSheet(md, bx, glm::lookAt(eye, c, vec3(0, 1, 0)), glm::ortho(-hw, hw, -hh, hh, 0.01f, D * 2), eye, W, H, rgba);
      px.assign((size_t)W * H * 4, 255);
      const uint8_t* bg = rgba.data();   // the corner: the clear colour after the sheet's tone curve
      for (size_t k = 0; k < (size_t)W * H; ++k) {
        int diff = 0; for (int c2 = 0; c2 < 3; ++c2) diff = std::max(diff, std::abs((int)rgba[k * 4 + c2] - (int)bg[c2]));
        px[k * 4 + 3] = (uint8_t)std::clamp((diff - 4) * 12, 0, 255);
      }
      stbi_write_png(p.string().c_str(), W, H, 4, px.data(), W * 4);
    }
    GLuint tex = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &tex); glTextureStorage2D(tex, 1, GL_RGBA8, W, H);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTextureSubImage2D(tex, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());   // first row = the top (ImGui v = 0)
    weaponSilhouette[d.id] = tex;
  }
}

void App::writeWeaponSheets(const fs::path& outDir) {
  std::error_code ec; fs::create_directories(outDir, ec);
  for (auto& dp : weaponDefs) {
    const WeaponDef& d = *dp;
    std::vector<ModelDraw> md; std::vector<DrawItem> bx;
    float L = 1.0f; vec3 bmin(-0.1f, -0.15f, 0), bmax(0.1f, 0.15f, 1.0f);
    if (d.useModel()) {
      for (int p = 0; p < WP_COUNT; ++p) for (int k : d.modelParts[p]) md.push_back({d.model.get(), k, mat4(1)});
      bmin = d.model->bmin; bmax = d.model->bmax;
    } else {
      for (int p = 0; p < WP_COUNT; ++p) if (d.mesh[p].count) bx.push_back({&d.mesh[p], mat4(1)});
    }
    if (d.hasScope) bx.push_back({&d.mesh[WP_SCOPE], mat4(1)});
    L = bmax.z;
    struct Mark { vec3 p; uint32_t c; };
    std::vector<Mark> marks = {{d.rear, 0xff2020ffu}, {d.front, 0xff20c020u}, {d.muzzle, 0xffff4020u}, {d.gripR, 0xff20e0ffu}, {d.gripL, 0xffffe020u}};
    auto sheet = [&](const char* tag, int W, int H, vec3 eye, vec3 at, vec3 up, float x0, float x1, float y0, float y1, int axH, int axV, float h0, float v0, float hs = 1.0f) {
      mat4 view = glm::lookAt(eye, at, up);
      mat4 proj = glm::ortho(x0, x1, y0, y1, 0.01f, 10.0f);
      std::vector<uint8_t> px;
      R.renderModelSheet(md, bx, view, proj, eye, W, H, px);
      float sx = W / (x1 - x0), sy = H / (y1 - y0);
      auto put = [&](int x, int y, uint32_t c, float a) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        uint8_t* q = &px[((size_t)y * W + x) * 4];
        for (int k = 0; k < 3; ++k) q[k] = (uint8_t)(q[k] * (1 - a) + ((c >> (8 * k)) & 255) * a);
      };
      for (int cm = -100; cm <= 200; ++cm) {   // vertical lines: horizontal axis value h0 + cm/100
        float v = cm / 100.0f;
        int x = (int)std::lround((v - h0 - x0) * sx);
        float a = cm % 10 == 0 ? 0.55f : cm % 5 == 0 ? 0.3f : 0.12f;
        if (x >= 0 && x < W) for (int y = 0; y < H; ++y) put(x, y, 0xff000000u, a);
        int y = (int)std::lround(H - 1 - (v - v0 - y0) * sy);
        if (y >= 0 && y < H) for (int xx = 0; xx < W; ++xx) put(xx, y, 0xff000000u, a);
      }
      for (auto& m : marks) {
        float h = m.p[axH] * hs, v = m.p[axV];
        int cx = (int)std::lround((h - h0 - x0) * sx), cy = (int)std::lround(H - 1 - (v - v0 - y0) * sy);
        for (int k = -9; k <= 9; ++k) { put(cx + k, cy, m.c, 1); put(cx, cy + k, m.c, 1); }
      }
      fs::path p = outDir / (d.id + "_" + tag + ".png");
      stbi_write_png(p.string().c_str(), W, H, 4, px.data(), W * 4);
    };
    // side: camera on -x looking +x, +z to the right, +y up
    float pad = 0.04f, cy = (bmin.y + bmax.y) * 0.5f;
    float spanZ = L + 2 * pad, scale = 2400.0f / spanZ, halfY = 450.0f / scale;
    sheet("side", 2400, 900, vec3(-2, cy, L * 0.5f), vec3(0, cy, L * 0.5f), vec3(0, 1, 0), -spanZ * 0.5f, spanZ * 0.5f, -halfY, halfY, 2, 1, L * 0.5f, cy);
    // rear: camera behind the butt looking +z; screen right = -x
    sheet("rear", 800, 800, vec3(0, cy, -2), vec3(0, cy, 0), vec3(0, 1, 0), -0.16f, 0.16f, -0.16f, 0.16f, 0, 1, 0, cy, -1.0f);
    logf("sheet %s: bounds (%.3f %.3f %.3f)-(%.3f %.3f %.3f)", d.id.c_str(), bmin.x, bmin.y, bmin.z, bmax.x, bmax.y, bmax.z);
  }
}
