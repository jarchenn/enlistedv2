// --selftest-maps (stages 4100-4199): W5 default maps (SPEC 3). For every authored template with map content
// (content/templates/*.json with "map": Omaha Easy Red; Mont-Castre's map file has no "scenario", so it runs the
// terrain-base case instead, terrainBaseChecks: size, relief, slopes, cliffs, walkable route, save / reopen, views):
//  * New Map "Create editable copy" loads the template: authored heightmap and water, every instance, the scenario;
//    the copy is an unsaved creative document that saves / reopens like any map, and edits like any map;
//  * the scenario validates with no errors; SPEC 2.1 distance bands for objectives and spawns; every spawn has its
//    team rack; every objective has linked structures that exist and >= 3 MG-nest defense slots;
//  * placed heights agree with the editor's placement rule; the map is in the Play "Default maps" list; the New Map
//    preview thumbnail exists and is not blank;
//  * docs/asset-usage-default-maps.md names every catalogue asset;
//  * screenshots from the A1 spawn (first person) and of every objective, with frame time, draws and memory.
// --render-map-thumbs renders each template's preview from its "thumbnailCamera" into selftest_out/<id>.thumb.png.
#include "selftest.h"
#include "editor_nav.h"
#include "scenario.h"
#include "w5.h"
#include "mpui.h"
#include <stb_image.h>
#include <stb_image_write.h>
#include <chrono>
#include <fstream>
#include <cstdarg>
#include <windows.h>
#include <psapi.h>

namespace {
fs::path outDirM() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
double wsMB() { PROCESS_MEMORY_COUNTERS pmc{}; GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc); return pmc.WorkingSetSize / 1048576.0; }
std::string fmt(const char* f, ...) { char b[600]; va_list a; va_start(a, f); std::vsnprintf(b, sizeof b, f, a); va_end(a); return b; }
double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::vector<int> mapTemplates(App& app) {
  std::vector<int> v;
  for (int k = 0; k < (int)app.templates.size(); ++k) if (app.templates[k].raw.contains("map")) v.push_back(k);
  return v;
}
struct View { std::string name; vec3 pos; float yaw, pitch; bool explore = false; bool gate = true; };   // gate: gameplay view held to the frame target
struct Perf { std::string name; bool gate = true; int frames = 0; double ms = 0, worst = 0, draws = 0, tris = 0; };

bool thumbOnly = false;
std::vector<int> sTpl; size_t sTi = 0;
std::vector<View> sViews; size_t sVi = 0; bool sViewInit = false;
std::vector<Perf> sPerf; double sLast = 0; double sLoadS = 0, sMemBefore = 0, sMemAfter = 0;
json sSum = json::object();

bool writeThumb(App& app, const fs::path& p) {
  std::vector<uint8_t> px; int w = 0, h = 0;
  if (!app.R.readPixels(px, w, h) || w < 64 || h < 64) return false;
  // crop to 16:10 about the centre, box-filter to 320 x 200, place in the middle 60 % band of a square-ish canvas
  int cw = std::min(w, (int)(h * 1.6f)), ch = (int)(cw / 1.6f), x0 = (w - cw) / 2, y0 = (h - ch) / 2;
  const int TW = 320, TH = 200, CH = 334, off = (CH - TH) / 2;
  std::vector<uint8_t> out((size_t)TW * CH * 4, 0);
  for (size_t k = 3; k < out.size(); k += 4) out[k] = 255;
  for (int y = 0; y < TH; ++y)
    for (int x = 0; x < TW; ++x) {
      int sx0 = x0 + x * cw / TW, sx1 = x0 + (x + 1) * cw / TW, sy0 = y0 + y * ch / TH, sy1 = y0 + (y + 1) * ch / TH;
      double acc[3] = {0, 0, 0}; int n = 0;
      for (int sy = sy0; sy < std::max(sy1, sy0 + 1); ++sy)
        for (int sx = sx0; sx < std::max(sx1, sx0 + 1); ++sx) { const uint8_t* q = &px[((size_t)(h - 1 - sy) * w + sx) * 4]; for (int c = 0; c < 3; ++c) acc[c] += q[c]; ++n; }
      uint8_t* o = &out[((size_t)(y + off) * TW + x) * 4];
      for (int c = 0; c < 3; ++c) o[c] = (uint8_t)(acc[c] / std::max(1, n));
    }
  // top / bottom bands: repeat the edge rows, darkened (they are cropped away by the dialog anyway)
  for (int y = 0; y < CH; ++y) {
    if (y >= off && y < off + TH) continue;
    int src = y < off ? off : off + TH - 1;
    for (int x = 0; x < TW; ++x) for (int c = 0; c < 3; ++c) out[((size_t)y * TW + x) * 4 + c] = out[((size_t)src * TW + x) * 4 + c] / 2;
  }
  return stbi_write_png(p.string().c_str(), TW, CH, 4, out.data(), TW * 4) != 0;
}
double blankFraction(const fs::path& p) {
  int w = 0, h = 0, n = 0;
  stbi_uc* d = stbi_load(p.string().c_str(), &w, &h, &n, 3);
  if (!d) return 1.0;
  int y0 = (int)(h * 0.2), y1 = (int)(h * 0.8);
  long long same = 0, tot = 0; const stbi_uc* r = d + ((size_t)y0 * w) * 3;
  for (int y = y0; y < y1; ++y) for (int x = 0; x < w; ++x, ++tot) { const stbi_uc* q = d + ((size_t)y * w + x) * 3; same += std::abs(q[0] - r[0]) + std::abs(q[1] - r[1]) + std::abs(q[2] - r[2]) < 12; }
  stbi_image_free(d);
  return tot ? (double)same / tot : 1.0;
}
// Views authored in the template ("views": [{name, pos, yaw, pitch, gameplay}]), e.g. the terrain base's cliff shots.
void templateViews(const TemplateInfo& t) {
  for (auto& v : t.raw.value("views", json::array()))
    sViews.push_back({v.value("name", std::string("view")), jvec3(v["pos"], vec3(0)), v.value("yaw", 0.0f), v.value("pitch", 0.0f), false, v.value("gameplay", false)});
}
}  // namespace

// W5b terrain base (Normandy Forest - Mont-Castre): size, relief and slope statistics, cliffs steeper than 60 deg, a
// walkable route from the attackers' edge to the ridge top, no NaNs, an empty editable document that saves / reopens.
bool SelfTest::terrainBaseChecks(App& app, const TemplateInfo& t, const json& mj) {
  const std::string id = t.id;
  const Terrain& T = app.terrain;
  check(app.doc.instances.empty() && mj.value("instances", json::array()).empty(), id + ": terrain base: no authored instances (the user places everything)");
  check(std::fabs(app.doc.width - 850) < 1 && std::fabs(T.buildHalfZ * 2 - 1700) < 1 && std::fabs(t.width - 850) < 1 && std::fabs(t.depth - 1700) < 1,
        fmt("%s: map size %.0f x %.0f m (half of Omaha's area, 1:2)", id.c_str(), app.doc.width, T.buildHalfZ * 2));
  // vertices inside the build area
  const float c = Terrain::kCell;
  auto inB = [&](int i, int j) { float x = T.org.x + i * c, z = T.org.y + j * c; return std::fabs(x) <= T.buildHalfX && std::fabs(z) <= T.buildHalfZ; };
  auto H = [&](int i, int j) { return T.h[(size_t)j * T.nx + i]; };
  int nan = 0; float lo = 1e9f, hi = -1e9f, maxS = 0; long n60 = 0, n50 = 0, nAll = 0;
  for (int j = 1; j < T.nz - 1; ++j)
    for (int i = 1; i < T.nx - 1; ++i) {
      if (!std::isfinite(H(i, j))) { ++nan; continue; }
      if (!inB(i, j)) continue;
      lo = std::min(lo, H(i, j)); hi = std::max(hi, H(i, j));
      float gx = (H(i + 1, j) - H(i - 1, j)) / (2 * c), gz = (H(i, j + 1) - H(i, j - 1)) / (2 * c);
      float sdeg = degrees(std::atan(std::sqrt(gx * gx + gz * gz)));
      maxS = std::max(maxS, sdeg); n60 += sdeg > 60; n50 += sdeg > 50; ++nAll;
    }
  check(nan == 0, fmt("%s: heightmap has no NaN / inf heights (%d)", id.c_str(), nan));
  check(hi - lo > 60 && hi - lo < 140, fmt("%s: height range %.1f .. %.1f m (relief %.1f m)", id.c_str(), lo, hi, hi - lo));
  check(maxS > 60 && n60 >= 40, fmt("%s: cliffs: steepest slope %.1f deg; %ld vertices over 60 deg, %ld over 50 deg (of %ld)", id.c_str(), maxS, n60, n50, nAll));
  // walkable route: 4-neighbour flood fill from the attackers' edge over steps no steeper than 45 deg (player limit 50)
  std::vector<uint8_t> seen((size_t)T.nx * T.nz, 0); std::vector<int> q;
  const float maxStep = std::tan(radians(45.0f)) * c;
  for (int j = 0; j < T.nz; ++j) for (int i = 0; i < T.nx; ++i)
    if (inB(i, j) && T.org.y + j * c < -T.buildHalfZ + 12) { seen[(size_t)j * T.nx + i] = 1; q.push_back(j * T.nx + i); }
  float reachHi = -1e9f; size_t walk = 0;
  for (size_t k = 0; k < q.size(); ++k) {
    int i = q[k] % T.nx, j = q[k] / T.nx; reachHi = std::max(reachHi, H(i, j)); ++walk;
    const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto& e : d) {
      int a = i + e[0], b = j + e[1];
      if (a < 0 || b < 0 || a >= T.nx || b >= T.nz || !inB(a, b) || seen[(size_t)b * T.nx + a]) continue;
      if (std::fabs(H(a, b) - H(i, j)) > maxStep) continue;
      seen[(size_t)b * T.nx + a] = 1; q.push_back(b * T.nx + a);
    }
  }
  float lowland = 1e9f;
  for (int i = 0; i < T.nx; ++i) for (int j = 0; j < T.nz; ++j) if (inB(i, j) && T.org.y + j * c < -T.buildHalfZ + 300) lowland = std::min(lowland, H(i, j));
  check(reachHi > hi - 20 && reachHi - lowland > 60, fmt("%s: walkable route from the attackers' edge to the ridge top: highest reachable ground %.1f m (%.0f m above the lowland, top %.1f m); %.0f %% of the area reachable on foot",
                                                          id.c_str(), reachHi, reachHi - lowland, hi, 100.0 * walk / std::max<long>(1, nAll)));
  Scenario s = Scenario::fromJson(app.doc.extra.value("scenario", json()));
  check(s.objectives.empty() && s.spawns.empty(), id + ": no scenario: no objectives, spawns or racks");
  // the empty creative copy saves and reopens with the authored ground
  float h0 = T.height(120, 400), h1 = T.height(-200, -600);
  fs::path p = userMapsDir() / ("selftest_" + id + ".nmap.json");
  bool ok = app.saveMapTo(p) && app.openMap(p);
  check(ok && std::fabs(app.doc.width - 850) < 1 && std::fabs(app.terrain.buildHalfZ * 2 - 1700) < 1, fmt("%s: reopened at its authored size %.0f x %.0f m", id.c_str(), app.terrain.buildHalfX * 2, app.terrain.buildHalfZ * 2));
  check(ok && app.doc.templateId == id && std::fabs(app.terrain.height(120, 400) - h0) < 0.01f && std::fabs(app.terrain.height(-200, -600) - h1) < 0.01f,
        fmt("%s: the editable copy saves and reopens on the authored terrain (%.2f / %.2f m)", id.c_str(), app.terrain.height(120, 400), app.terrain.height(-200, -600)));
  check(app.R.shaderErrors == 0, fmt("all shaders compiled, terrain slope material included (%d errors)", app.R.shaderErrors));
  sViews.clear(); templateViews(t);
  sViews.push_back({"overview", t.camPos, t.camYaw, t.camPitch, false, false});
  sVi = 0; sPerf.clear(); sViewInit = false;
  sSum[id] = {{"heights", {lo, hi}}, {"maxSlopeDeg", maxS}, {"over60", n60}, {"over50", n50}, {"reachableTop", reachHi}, {"workingSetMB", sMemAfter}};
  check(sViews.size() >= 4, fmt("%s: %zu screenshot views", id.c_str(), sViews.size()));
  return true;
}

void SelfTest::mapsUpdate(App& app, float dt) {
  (void)dt;
  auto go = [&](int s) { stage = s; stageTime = 0; };
  switch (stage) {
  case 4100: {   // ---------------------------------------------------------------- setup
    reportName = "maps";
    gUi.showOverlay = false;   // screenshots and previews without the scenario overlay
    for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--render-map-thumbs") thumbOnly = true;
    sTpl = mapTemplates(app); sTi = 0;
    check(!sTpl.empty(), fmt("authored default-map templates found: %zu", sTpl.size()));
    if (sTpl.empty()) { finish(app); return; }
    go(4101);
    break;
  }
  case 4101: {   // ---------------------------------------------------------------- load one template + static checks
    if (sTi >= sTpl.size()) { go(4190); break; }
    const TemplateInfo& t = app.templates[sTpl[sTi]];
    if (thumbOnly) {
      navNewMapSized(app, t, t.name, vec2(t.width, t.depth));
      json c = t.raw.value("thumbnailCamera", t.raw.value("camera", json::object()));
      app.camPos = jvec3(c["pos"], t.camPos); app.camYaw = c.value("yaw", t.camYaw); app.camPitch = c.value("pitch", t.camPitch);
      go(4180); break;
    }
    const std::string id = t.id;
    // "userMap": a map the user built in the editor and published as a template (e.g. battle1). It gets the generic
    // checks (loads, editable copy, catalogue, validator, save / reopen, views); the SPEC 2.1 / 3.5 layout rules of the
    // shipped default maps, the authored heightmap and the placed-height rule (free-hand placement) don't apply, and
    // its frame time is reported, not gated.
    const bool userMap = t.raw.value("userMap", false);
    if (userMap) check(t.available, id + ": user map template is available (generated terrain from its seed)");
    else check(t.available && t.raw.contains("heightmap") && t.raw.contains("waterLevel"), id + ": template is available with an authored heightmap and water level");
    // Play / multiplayer listing (same filter as w5PlayMapPicker)
    // preview thumbnail
    fs::path th = contentDir() / "templates" / t.raw.value("thumbnail", std::string("-"));
    double bl = fs::exists(th) ? blankFraction(th) : 1.0;
    check(t.thumbnail != 0 && fs::exists(th) && bl < 0.95, fmt("%s: New Map preview rendered from the fixed camera (%s, %.0f %% background)", id.c_str(), th.filename().string().c_str(), bl * 100));
    // file content vs loaded document
    json mj; try { mj = json::parse(readTextFile(contentDir() / "templates" / t.raw.value("map", std::string()))); } catch (...) {}
    size_t fileN = mj.value("instances", json::array()).size();
    const bool terrainBase = !mj.contains("scenario");   // W5b: a terrain-only starting map (Mont-Castre): the user authors the rest
    if (terrainBase) check(!t.raw.value("playable", false), id + ": terrain base (no scenario): kept out of the Play > Default maps list");
    else check(t.raw.value("playable", false), id + ": listed in Play > setup > Default maps (playable)");
    sMemBefore = wsMB();
    double t0 = now();
    navNewMapSized(app, t, "Selftest " + t.name, vec2(t.width, t.depth));   // exactly the New Map "Create editable copy" path
    sLoadS = now() - t0; sMemAfter = wsMB();
    check(app.doc.templateId == id && app.docPath.empty(), id + ": Create editable copy gives an unsaved creative document from the template");
    if (terrainBase) { if (terrainBaseChecks(app, t, mj)) go(4102); else go(4101); break; }
    check(app.doc.instances.size() == fileN && fileN > (userMap ? 0u : 1000u), fmt("%s: all %zu authored instances loaded (%.2f s, working set %+.0f MB)", id.c_str(), fileN, sLoadS, sMemAfter - sMemBefore));
    check(std::fabs(app.doc.width - t.width) < 1 && std::fabs(app.terrain.buildHalfZ * 2 - t.depth) < 1, fmt("%s: map size %.0f x %.0f m", id.c_str(), app.doc.width, app.terrain.buildHalfZ * 2));
    size_t missing = 0; for (auto& i : app.doc.instances) if (!app.lib.find(i.assetId)) ++missing;
    check(missing == 0, fmt("%s: every instance names a catalogue asset (%zu missing)", id.c_str(), missing));
    // scenario
    Scenario s = Scenario::fromJson(app.doc.extra.value("scenario", json()));
    auto issues = validateScenario(s, true);
    int errs = 0, warns = 0; std::string firstErr;
    for (auto& i : issues) { if (i.error) { ++errs; if (firstErr.empty()) firstErr = i.what; } else ++warns; }
    check(errs == 0, fmt("%s: scenario validator: %d errors, %d warnings%s", id.c_str(), errs, warns, firstErr.empty() ? "" : (" (" + firstErr + ")").c_str()));
    auto xz = [](vec3 p) { return vec2(p.x, p.z); };
    if (userMap) {
      int att = 0, def = 0; for (auto& sp : s.spawns) (sp.role == CombatRole::Attacker ? att : def)++;
      check(!s.objectives.empty() && att > 0 && def > 0, fmt("%s: user map has a playable scenario: %zu objectives, %d attacker / %d defender spawns", id.c_str(), s.objectives.size(), att, def));
    } else {
    check(s.objectives.size() == 5, fmt("%s: %zu objectives A-E in order", id.c_str(), s.objectives.size()));
    check(s.rules.defendingFaction == Faction::Axis, id + ": Allies attack, Germans defend");
    }
    for (size_t k = 0; k < (userMap ? 0 : s.objectives.size()); ++k) {
      const Objective& o = s.objectives[k];
      std::string L(1, o.letter());
      if (k > 0) { float d = glm::length(xz(o.pos) - xz(s.objectives[k - 1].pos)); check(d >= 450 && d <= 600, fmt("%s: %s-%s objective spacing %.0f m (SPEC 2.1: 450-600)", id.c_str(), std::string(1, s.objectives[k - 1].letter()).c_str(), L.c_str(), d)); }
      vec3 fwd = attackForward(o.attackYaw);
      if (k > 0) check(glm::dot(o.pos - s.objectives[k - 1].pos, vec3(0, 0, 1)) > 0, id + ": " + L + " lies further along the attack axis than its predecessor");
      (void)fwd;
      for (auto* sp : s.spawnsFor(CombatRole::Attacker, (int)k)) {
        float d = glm::length(xz(sp->pos) - xz(o.pos));
        bool ok = k == 0 ? d >= 300 : (d >= 300 && d <= 450);
        check(ok, fmt("%s: attacker spawn %s is %.0f m before %s (SPEC 2.1: 300-450%s)", id.c_str(), sp->label.c_str(), d, L.c_str(), k == 0 ? ", the beach spawn may be further" : ""));
      }
      for (auto* sp : s.spawnsFor(CombatRole::Defender, (int)k)) {
        float d = glm::length(xz(sp->pos) - xz(o.pos));
        check(d >= 250 && d <= 500 && glm::dot(sp->pos - o.pos, vec3(0, 0, 1)) > 0, fmt("%s: defender spawn %s is %.0f m behind %s (SPEC 2.1: 250-500)", id.c_str(), sp->label.c_str(), d, L.c_str()));
      }
      int mg = 0; for (auto& d : o.defenseSlots) mg += d.kind == SlotKind::MgNest;
      size_t gone = 0; for (auto lid : o.linkedStructures) gone += app.doc.find(lid) == nullptr;
      check(mg >= 3, fmt("%s: %s %s: %d MG-nest slots (SPEC 3.5: >= 3), %zu defense slots in all", id.c_str(), L.c_str(), o.label.c_str(), mg, o.defenseSlots.size()));
      check(!o.linkedStructures.empty() && gone == 0, fmt("%s: %s: %zu linked structures, all present", id.c_str(), L.c_str(), o.linkedStructures.size()));
    }
    int racks = 0;
    for (auto& sp : s.spawns) { const Instance* r = app.doc.find(sp.rack); const AssetDef* a = r ? app.lib.find(r->assetId) : nullptr; racks += a && a->rack && glm::length(xz(r->pos) - xz(sp.pos)) < 12; }
    if (!userMap) check(racks == (int)s.spawns.size() && s.spawns.size() == 10, fmt("%s: %d / %zu spawns have their team weapons rack beside them", id.c_str(), racks, s.spawns.size()));
    // terrain: sea, shingle, bluff and draw from the authored heightmap
    if (id == "omaha_easy_red") {
      auto H = [&](float x, float sAx) { return app.terrain.height(x, sAx - t.depth * 0.5f); };
      check(H(0, 20) < app.terrain.waterLevel - 0.3f && H(0, 300) > app.terrain.waterLevel, fmt("Omaha: sea at s 20 (%.1f m) below the water level, tidal flat above it", H(0, 20)));
      float rise = H(300, 860) - H(300, 560);
      check(rise > 30 && rise < 50, fmt("Omaha: bluff rises %.1f m (SPEC: 30-50 m)", rise));
      check(H(300, 700) - H(-25, 700) > 10, fmt("Omaha: the E-1 draw cuts the bluff (%.1f m below the bluff face at s 700)", H(300, 700) - H(-25, 700)));
    }
    // placed heights against the editor's placement rule (instances placed free-hand by the author are skipped)
    int tested = 0, agree = 0; float worst = 0;
    for (size_t k = 0; k < app.doc.instances.size(); k += 7) {
      const Instance& i = app.doc.instances[k];
      const AssetDef* a = app.lib.find(i.assetId);
      if (!a || a->path.on || a->rack || a->category == "Furniture" || i.assetId == "env_sea_shallows" || !a->navMeta.terrainCuts.empty()) continue;
      PlaceCheck c = app.checkPlacement(a, i.pos, i.yaw, i.id);
      if (!c.ok && (c.reason.find("water") != std::string::npos || c.reason.find("Outside") != std::string::npos)) continue;
      ++tested; float e = std::fabs(c.y - i.pos.y); worst = std::max(worst, e); agree += e < 0.25f;
    }
    if (!userMap) check(tested > 300 && agree >= tested * 0.9, fmt("%s: placed heights match the editor rule for %d / %d sampled instances (worst %.2f m)", id.c_str(), agree, tested, worst));
    // asset-usage doc covers the catalogue
    std::string doc; try { doc = readTextFile(fs::path(W5_REPO_DIR) / "docs" / "asset-usage-default-maps.md"); } catch (...) {}
    int nIds = 0, covered = 0; std::string miss;
    for (auto& a : app.lib.assets) { ++nIds; if (doc.find("`" + a->id + "`") != std::string::npos) ++covered; else if (miss.size() < 200) miss += " " + a->id; }
    check(!doc.empty() && covered == nIds, fmt("docs/asset-usage-default-maps.md covers %d / %d catalogue assets%s", covered, nIds, miss.c_str()));
    // views: A1 first person, then each objective from the attackers' side, then the overview
    sViews.clear();
    for (auto& sp : s.spawns) if (sp.role == CombatRole::Attacker && sp.activeIn(0)) { sViews.push_back({"A1_spawn", sp.pos, 180.0f, 2.0f, true}); break; }
    for (auto& o : s.objectives) {
      vec3 back = -attackForward(o.attackYaw);
      vec3 p = o.pos + back * 60.0f; p.y = std::max(app.terrain.height(p.x, p.z), o.pos.y) + 24.0f;   // above hedge crowns
      vec3 d = o.pos + vec3(0, 3, 0) - p;
      sViews.push_back({std::string("obj_") + o.letter(), p, degrees(std::atan2(-d.x, -d.z)), degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z)))), false, false});
      vec3 e = o.pos + back * 45.0f; e.y = app.terrain.height(e.x, e.z) + 1.7f;   // eye level on the attackers' approach
      sViews.push_back({std::string("obj_") + o.letter() + "_eye", e, o.attackYaw, 0.0f, false, true});
    }
    sViews.push_back({"overview", t.camPos, t.camYaw, t.camPitch, false, false});
    templateViews(t);
    if (id == "omaha_easy_red") sViews.push_back({"bluff_top_to_beach", vec3(60, app.terrain.height(60, 820 - t.depth * 0.5f) + 3, 820 - t.depth * 0.5f), 0.0f, -6.0f, false});
    if (id == "omaha_easy_red") sViews.push_back({"plateau_bocage", vec3(-120, app.terrain.height(-120, 1300 - t.depth * 0.5f) + 2, 1300 - t.depth * 0.5f), 200.0f, -2.0f, false});
    sVi = 0; sPerf.clear(); sViewInit = false;
    sSum[id] = {{"instances", fileN}, {"loadSeconds", sLoadS}, {"workingSetMB", sMemAfter}, {"workingSetDeltaMB", sMemAfter - sMemBefore}, {"validatorWarnings", warns}};
    // editable like any map: move an instance (undoable), save, reopen
    {
      Instance* r = nullptr; for (auto& i : app.doc.instances) if (i.assetId == "def_tank_trap") { r = &i; break; }
      if (r) { app.pushUndo(); r = app.doc.find(r->id); vec3 o = r->pos; r->pos.x += 3; app.bump(*r); check(app.dirty && app.doc.find(r->id)->pos.x == o.x + 3, id + ": authored instances are ordinary editable placements"); }
      fs::path p = userMapsDir() / ("selftest_" + id + ".nmap.json");
      size_t n = app.doc.instances.size();
      bool ok = app.saveMapTo(p);
      ok = ok && app.openMap(p);
      Scenario s2 = Scenario::fromJson(app.doc.extra.value("scenario", json()));
      check(ok && app.doc.instances.size() == n && s2.objectives.size() == s.objectives.size() && s2.spawns.size() == s.spawns.size(),
            fmt("%s: the editable copy saves and reopens (%zu instances, %zu objectives)", id.c_str(), app.doc.instances.size(), s2.objectives.size()));
    }
    go(4102);
    break;
  }
  case 4102: {   // ---------------------------------------------------------------- views: settle, measure, shoot
    if (sVi >= sViews.size()) {
      if (app.mode == Mode::Explore) app.exitExplore();
      json pj = json::array(); double worstAvg = 0, worstEd = 0;
      for (auto& p : sPerf) { pj.push_back({{"view", p.name}, {"gameplay", p.gate}, {"frames", p.frames}, {"avgMs", p.ms}, {"worstMs", p.worst}, {"draws", p.draws}, {"tris", p.tris}}); (p.gate ? worstAvg : worstEd) = std::max(p.gate ? worstAvg : worstEd, p.ms); }
      const std::string id = app.templates[sTpl[sTi]].id;
      sSum[id]["views"] = pj; sSum[id]["workingSetEndMB"] = wsMB();
      if (app.templates[sTpl[sTi]].raw.value("userMap", false))   // user-built map: frame time reported, not gated
        check(true, fmt("%s: user map frame time (reported only): worst eye-level view %.2f ms, worst editor view %.2f ms", id.c_str(), worstAvg, worstEd));
      else check(worstAvg < 16.7, fmt("%s: every gameplay (eye-level) view holds the 60 FPS target: worst view average %.2f ms (elevated editor views, reported only: worst %.2f ms)", id.c_str(), worstAvg, worstEd));
      ++sTi; go(4101); break;
    }
    View& v = sViews[sVi];
    if (!sViewInit) {
      sViewInit = true;
      if (v.explore) {
        app.doc.start = {true, v.pos, v.yaw};
        bool ok = app.enterExplore();
        check(ok && app.mode == Mode::Explore, "first person at the A1 spawn (" + std::string(ok ? "entered" : "refused") + ")");
      } else {
        if (app.mode == Mode::Explore) app.exitExplore();
        app.camPos = v.pos; app.camYaw = v.yaw; app.camPitch = v.pitch;
      }
      sPerf.push_back({v.name, v.gate}); sLast = 0;
      logf("maps view %s at (%.1f %.1f %.1f) yaw %.1f pitch %.1f", v.name.c_str(), v.pos.x, v.pos.y, v.pos.z, v.yaw, v.pitch);
    }
    if (v.explore && app.mode == Mode::Explore) { app.useScripted = true; }
    double tnow = now();
    if (stageTime > 2.0f && stageTime < 4.0f && sLast > 0) {   // streaming settled: measure wall-clock frame time
      Perf& p = sPerf.back(); double ms = (tnow - sLast) * 1000.0;
      p.ms = (p.ms * p.frames + ms) / (p.frames + 1); p.worst = std::max(p.worst, ms);
      p.draws = (p.draws * p.frames + app.R.stats.draws) / (p.frames + 1); p.tris = (p.tris * p.frames + app.R.stats.tris) / (p.frames + 1); ++p.frames;
    }
    sLast = tnow;
    if (stageTime >= 4.0f) {
      std::string nm = "maps_" + app.templates[sTpl[sTi]].id + "_" + v.name;
      shotQueue.push_back(nm);
      Perf& p = sPerf.back();
      check(p.frames > 10, fmt("%s view %s: %.2f ms avg (worst %.1f) over %d frames, %.0f draws, %.1f M tris -> selftest_out/%s.png", v.gate ? "gameplay" : "editor", v.name.c_str(), p.ms, p.worst, p.frames, p.draws, p.tris / 1e6, nm.c_str()));
      ++sVi; stageTime = 0; sViewInit = false;
    }
    break;
  }
  case 4180: {   // ---------------------------------------------------------------- --render-map-thumbs
    if (stageTime > 3.0f) {
      const TemplateInfo& t = app.templates[sTpl[sTi]];
      fs::path p = outDirM() / (t.id + ".thumb.png");
      check(writeThumb(app, p), "preview written: selftest_out/" + p.filename().string() + " (copy to content/templates/)");
      ++sTi; go(4101);
    }
    break;
  }
  case 4190: {
    std::ofstream(outDirM() / "summary_maps.json") << sSum.dump(2);
    finish(app);
    break;
  }
  default: finish(app); break;
  }
}
