// --selftest-visuals (W2, MP & Creative update): build-menu thumbnails (spec 9.4), footprint labels, and the bunker
// pass (spec 10: Observation Post / Field Shelter treatment, ammunition in every bunker prefab, nav annotations).
//   thumbnails: cold build into an empty cache, every catalogue tile <= 95 % background (the five known blanks named),
//   warm build loads every tile from the cache pixel-identical, a changed asset re-renders only itself and replaces its
//   stale file; before / after tiles of the known blanks in selftest_out/thumbs/.
#include "selftest.h"
#include "thumbs.h"
#include "navmeta.h"
#include <functional>
#include <cstdlib>
#include <algorithm>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> vseq; size_t vcur = 0; float vt = 0;
fs::path outDirV() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
const char* kKnownBlank[5] = {"utility_pole", "wire_concertina", "wire_fence", "wire_knife_rest", "gun_flak18"};
std::map<std::string, GLuint> coldTiles;
int countTiles(const fs::path& dir) { int n = 0; std::error_code ec; for (auto& e : fs::directory_iterator(dir, ec)) n += e.path().extension() == ".png"; return n; }
void setCache(const fs::path& p) { _putenv_s("NMK_THUMB_CACHE", p.string().c_str()); }
}

void SelfTest::visualsUpdate(App& app, float dt) {
  (void)dt;
  auto push = [&](const std::string& n, std::function<bool(App&, float)> f) { vseq.push_back({n, f}); };
  if (stage == 3250) {
    reportName = "visuals";
    vseq.clear(); vcur = 0; vt = 0;
    static fs::path cache = outDirV() / "thumb_cache_test";
    static fs::path shots = outDirV() / "thumbs";

    // ================================================================ thumbnails
    push("thumbs: before (legacy renderer) for the known blanks", [this](App& a, float) {
      std::error_code ec; fs::create_directories(shots, ec);
      for (const char* id : kKnownBlank) {
        AssetDef* as = const_cast<AssetDef*>(a.lib.find(id));
        if (!as) { check(false, std::string(id) + " missing from the catalogue"); continue; }
        // the pre-W2 path: fixed 3/4 sphere framing, sky background, imported models listed but never drawn
        std::vector<DrawItem> op{{&as->opaque, mat4(1)}, {&as->panelsIntact, mat4(1)}}, gl;
        GLuint t = a.R.renderThumbnail(op, gl, as->bmin, as->bmax);
        saveThumbPng(t, shots / (std::string("before_") + id + ".png"));
        glDeleteTextures(1, &t);
      }
      check(true, "legacy tiles of the five known blanks written to selftest_out/thumbs/before_*.png");
      return true; });
    push("thumbs: cold build into an empty cache", [this](App& a, float) {
      std::error_code ec; fs::remove_all(cache, ec); setCache(cache);
      ThumbStats s = makeAssetThumbnails(a);
      int n = (int)a.lib.assets.size() - s.external, files = countTiles(cache);
      char b[240]; std::snprintf(b, sizeof b, "cold cache: %d of %d tiles rendered (%d with a view search), %d loaded, %d PNG files written; %d vehicle tiles drawn by TankSys", s.rendered, n, s.searched, s.loaded, files, s.external);
      check(s.rendered == n && s.loaded == 0 && files == n && s.external == 2, b);
      summary["thumbs"]["cold"] = {{"rendered", s.rendered}, {"searched", s.searched}, {"files", files}};
      for (auto& as : a.lib.assets) {   // keep copies of the cold tiles for the warm comparison
        if (as->raw.is_object() && as->raw.contains("vehicle")) continue;
        GLuint c; glCreateTextures(GL_TEXTURE_2D, 1, &c); glTextureStorage2D(c, 1, GL_RGBA8, kThumbSize, kThumbSize);
        glCopyImageSubData(as->thumbnail, GL_TEXTURE_2D, 0, 0, 0, 0, c, GL_TEXTURE_2D, 0, 0, 0, 0, kThumbSize, kThumbSize, 1);
        coldTiles[as->id] = c;
      }
      return true; });
    push("thumbs: no blank tile (<= 95 % background)", [this](App& a, float) {
      int bad = 0; float worst = 0; std::string worstId;
      for (auto& as : a.lib.assets) {
        float bg = thumbBackgroundShare(a, as->thumbnail);
        const ThumbInfo& ti = gThumbInfo[as->id];
        bool known = false; for (const char* k : kKnownBlank) known |= as->id == k;
        if (bg > worst) { worst = bg; worstId = as->id; }
        summary["thumbs"]["tiles"][as->id] = {{"background", bg}, {"fill", ti.fill}, {"yaw", ti.yaw}, {"pitch", ti.pitch}, {"searched", ti.searched}, {"cropped", ti.cropped}};
        saveThumbPng(as->thumbnail, shots / ((known ? "after_" : "tile_") + as->id + ".png"));
        if (bg > 0.95f) ++bad;
        if (known || bg > 0.95f) {
          char b[280]; std::snprintf(b, sizeof b, "%s%s: %.1f%% background, silhouette %.1f%% of the tile, view yaw %.0f pitch %.0f%s", as->id.c_str(), known ? " (was blank)" : "", bg * 100, ti.fill * 100, ti.yaw, ti.pitch, ti.cropped ? " (long / sparse: detail crop)" : ti.searched ? " (thin / tall: view search)" : "");
          check(bg <= 0.95f, b);
        }
      }
      char b[200]; std::snprintf(b, sizeof b, "%zu catalogue tiles, %d over 95%% background; emptiest %s at %.1f%%", a.lib.assets.size(), bad, worstId.c_str(), worst * 100);
      check(bad == 0, b);
      return true; });
    push("thumbs: warm build loads every tile from the cache", [this](App& a, float) {
      ThumbStats s = makeAssetThumbnails(a);
      int same = 0, n = (int)a.lib.assets.size() - s.external;
      for (auto& as : a.lib.assets) if (coldTiles.count(as->id)) same += sameThumb(as->thumbnail, coldTiles[as->id]);
      char b[200]; std::snprintf(b, sizeof b, "warm cache: %d loaded, %d rendered; %d of %d tiles pixel-identical to the cold render", s.loaded, s.rendered, same, n);
      check(s.loaded == n && s.rendered == 0 && same == n, b);
      return true; });
    push("thumbs: a changed asset regenerates only its own tile", [this](App& a, float) {
      const AssetDef* pole = a.lib.find("utility_pole");
      std::string k0 = pole ? assetThumbKey(*pole) : "";
      gThumbSalt["utility_pole"] = "edited";
      std::string k1 = pole ? assetThumbKey(*pole) : "";
      ThumbStats s = makeAssetThumbnails(a);
      bool oldGone = !fs::exists(cache / ("utility_pole-" + k0 + ".png")), newThere = fs::exists(cache / ("utility_pole-" + k1 + ".png"));
      char b[260]; std::snprintf(b, sizeof b, "utility_pole changed: key %s -> %s, %d rendered, %d loaded; stale file removed %s, new file %s, %d files", k0.c_str(), k1.c_str(), s.rendered, s.loaded, oldGone ? "yes" : "NO", newThere ? "yes" : "NO", countTiles(cache));
      int n = (int)a.lib.assets.size() - s.external;
      check(k0 != k1 && s.rendered == 1 && s.loaded == n - 1 && oldGone && newThere && countTiles(cache) == n, b);
      gThumbSalt.clear();
      return true; });
    push("thumbs: restore the default cache", [this](App& a, float) {
      _putenv_s("NMK_THUMB_CACHE", "");
      for (auto& [id, t] : coldTiles) glDeleteTextures(1, &t);
      coldTiles.clear();
      ThumbStats s = makeAssetThumbnails(a);
      check(true, "default cache " + thumbCacheDir().string() + ": " + std::to_string(s.loaded) + " loaded, " + std::to_string(s.rendered) + " rendered");
      return true; });

    // ================================================================ footprint labels
    push("footprint labels: one decimal", [this](App& a, float) {
      std::string l = footprintLabel(vec2(0.3f, 0.3f));
      check(l == "0.3 \xC3\x97 0.3 m", "footprintLabel(0.3, 0.3) = \"" + l + "\" (want \"0.3 x 0.3 m\" with the multiplication sign)");
      int zero = 0; std::string list;
      for (auto& as : a.lib.assets) {
        std::string s = footprintLabel(as->footprint);
        if (s.rfind("0.0 ", 0) == 0 || s.find(" 0.0 m") != std::string::npos) { ++zero; list += " " + as->id; }
      }
      const AssetDef* pole = a.lib.find("utility_pole");
      check(zero == 0 && pole, "no catalogue label reads 0.0 (thin assets were \"0x0 m\" / \"0x5 m\"); telegraph pole: " + (pole ? footprintLabel(pole->footprint) : std::string("?")) + (zero ? "; zero:" + list : ""));
      return true; });

    // ================================================================ bunkers (spec 10)
    // visible authored parts (static and furniture) by material name, from the asset file: the loader moves large
    // boxes into destructible panels, so the loaded part list alone undercounts walls
    auto countMat = [](const AssetDef* d, std::initializer_list<const char*> names) {
      int n = 0;
      auto scan = [&](const json& ps) { for (auto& p : ps) { if (p.value("v", 1) == 0) continue; std::string m = p.value("m", ""); for (const char* x : names) if (m == x) { ++n; break; } } };
      scan(d->raw.value("parts", json::array()));
      for (auto& fg : d->raw.value("furniture", json::array())) scan(fg.value("parts", json::array()));
      return n;
    };
    push("bunkers: ammunition in every bunker prefab", [this, countMat](App& a, float) {
      int bunkers = 0;
      for (auto& as : a.lib.assets) {
        const AssetDef* d = as.get();
        bool gun = d->id == "bunker_pak_casemate" || d->id == "gun_flak18";
        if (d->category != "Bunkers" && !gun) continue;
        bunkers += d->category == "Bunkers";
        if (d->id.rfind("mil_", 0) == 0) continue;   // mil_ complexes are furnished: their ammunition comes from the mil_ammo / mil_fighting room recipes (furn_mil_ammo_rack, furn_mil_crates)
        int small = countMat(d, {"ammo_steel_de", "ammo_steel_us"}), crates = countMat(d, {"ammo_wood", "ammo_wood_dark"}), band = countMat(d, {"ammo_canvas"});
        int shells = countMat(d, {"ammo_wicker"}), brass = countMat(d, {"ammo_brass"});
        bool ok = gun ? (shells >= 2 && brass >= 2 && crates >= 8) : (small >= 2 && crates >= 1 && band >= 1);
        char b[260]; std::snprintf(b, sizeof b, "%s: %s (MG boxes %d, crate parts %d, bandolier parts %d, wicker carriers %d, loose-round parts %d)", d->id.c_str(), gun ? "gun ammunition: shell crates, wicker carriers, loose rounds" : "small-arms cache: 7.92 mm boxes, rifle crate, bandoliers", small, crates, band, shells, brass);
        check(ok, b);
        summary["bunkers"][d->id]["ammo"] = {{"mgBoxes", small}, {"crateParts", crates}, {"carriers", shells}};
      }
      check(bunkers == 7, std::to_string(bunkers) + " bunker prefabs in the catalogue (MG, OP, field shelter, command, large, PaK, command stronghold)");
      // decorative: resupply stays on the existing crates (crate_pak40_he companion of the PaK casemate)
      const AssetDef* pak = a.lib.find("bunker_pak_casemate");
      bool he = false; if (pak) for (auto& c : pak->raw.value("companions", json::array())) he |= c.value("asset", "") == "crate_pak40_he";
      check(he, "PaK casemate still brings its crate_pak40_he (the resupply interaction); the boxes themselves are dressing");
      return true; });
    push("bunkers: nav annotations", [this](App& a, float) {
      for (auto& as : a.lib.assets) {
        const AssetDef* d = as.get();
        if (d->category != "Bunkers") continue;
        const NavMeta& n = d->navMeta;
        int inside = 0, outside = 0, roomsOk = 0;
        auto inRoom = [&](vec3 p) { for (auto& r : n.rooms) if (p.x >= r.min.x - 0.05f && p.x <= r.max.x + 0.05f && p.z >= r.min.z - 0.05f && p.z <= r.max.z + 0.05f) return true; return false; };
        for (auto& r : n.rooms) roomsOk += glm::all(glm::lessThan(r.min, r.max)) && glm::all(glm::greaterThanEqual(r.min, d->bmin - vec3(0.01f))) && glm::all(glm::lessThanEqual(r.max, d->bmax + vec3(0.01f)));
        for (auto& c : n.corners) inside += inRoom(c.p);
        for (auto& f : n.firingPoints) inside += inRoom(f.p);
        for (auto& e : n.entrances) outside += !inRoom(e.p) && glm::length(e.dir) > 0.5f;
        bool doors = !n.doors.empty(); for (auto& dr : n.doors) doors &= dr.b == "outside" || std::any_of(n.rooms.begin(), n.rooms.end(), [&](const NavRoom& r) { return r.id == dr.b; });
        bool ok = n.authored && n.effect == NavEffect::Block && !n.rooms.empty() && roomsOk == (int)n.rooms.size() && doors && !n.entrances.empty() && outside == (int)n.entrances.size()
                  && n.corners.size() >= 4 * n.rooms.size() && !n.firingPoints.empty() && inside == (int)(n.corners.size() + n.firingPoints.size());
        char b[300]; std::snprintf(b, sizeof b, "%s nav: effect block, %zu rooms, %zu doors, %zu entrances (outside, pointing in), %zu corners and %zu firing points inside the rooms", d->id.c_str(), n.rooms.size(), n.doors.size(), n.entrances.size(), n.corners.size(), n.firingPoints.size());
        check(ok, b);
        summary["bunkers"][d->id]["nav"] = navMetaToJson(n);
      }
      return true; });
    // MA2 (brief section 8): weathering follows the real ground. A garden wall (no pad) across a 15 deg ramp; debug view
    // 3 paints white up to 0.5 m above the local ground (groundH, App::contactProfile). At both wall ends, scan up the
    // face: the white top must sit 0.5 m above the terrain there, though the ends stand ~0.9 m apart in height.
    push("MA2: foundation weathering follows the ground on a 15 deg slope", [this](App& a, float t) {
      static vec3 pos; static float halfX = 2.0f, faceZ = 0.35f, topY = 1.0f; static bool measured = false;
      const float slope = std::tan(glm::radians(15.0f));
      if (t == 0) {
        const TemplateInfo* tb = a.findTemplate("blank_slate");
        if (tb) a.newMap(*tb, "MA2 slope");
        Terrain& T = a.terrain;
        std::map<int, std::vector<float>> d;
        const float ts = T.tileSize(), fs = ts / Terrain::kFine;
        for (int tz = 0; tz < T.tilesZ(); ++tz) for (int tx = 0; tx < T.tilesX(); ++tx) {
          float x0 = T.org.x + tx * ts, z0 = T.org.y + tz * ts;
          if (x0 > 30 || x0 + ts < -30 || z0 > 30 || z0 + ts < -30) continue;
          std::vector<float> v((size_t)Terrain::kFineN * Terrain::kFineN);
          for (int j = 0; j < Terrain::kFineN; ++j) for (int i = 0; i < Terrain::kFineN; ++i) v[(size_t)j * Terrain::kFineN + i] = slope * std::clamp(x0 + i * fs + 30.0f, 0.0f, 60.0f);
          d[T.key(tx, tz)] = v;
        }
        a.doc.terrainDeltas = d; a.resetGround();
        const AssetDef* w = a.lib.find("drs_garden_wall");
        if (!w) { check(false, "MA2: drs_garden_wall missing"); return true; }
        Instance in; in.id = a.doc.newId(); in.assetId = w->id; in.assetVersion = w->version;
        pos = vec3(0, a.terrain.height(0, 0), 0); in.pos = pos; in.yaw = 0;
        a.bump(in); a.doc.instances.push_back(in);
        halfX = std::max(w->bmax.x, -w->bmin.x); faceZ = w->bmax.z; topY = pos.y + w->bmax.y - 0.08f;
        a.camPos = pos + vec3(0, 1.0f, faceZ + 6.5f); a.camYaw = 0; a.camPitch = -4;
        a.hideEditorOverlays = true; a.R.gxDebug = 3; measured = false;
        return false;
      }
      if (measured) {   // the same view, normally shaded: selftest_out/ma2_slope.png
        if (t < 2.4f) return false;
        a.screenshot(outDirV() / "ma2_slope.png"); a.hideEditorOverlays = false;
        return true;
      }
      if (t < 1.2f) return false;   // TAA settles
      std::vector<uint8_t> px; int W = 0, H = 0;
      bool got = a.R.readPixels(px, W, H);
      mat4 vp = a.projM * a.viewM;
      auto white = [&](vec3 p) {
        vec4 c = vp * vec4(p, 1); if (c.w <= 0) return -1;
        int x = (int)((c.x / c.w * 0.5f + 0.5f) * W), y = (int)((c.y / c.w * 0.5f + 0.5f) * H);
        if (x < 0 || y < 0 || x >= W || y >= H) return -1;
        const uint8_t* q = &px[((size_t)y * W + x) * 4];
        return q[0] > 150 && q[1] > 150 && q[2] > 150 ? 1 : 0;   // neutral white only (sunlit grass tips are not)
      };
      float rel[2] = {-9, -9}, g[2] = {0, 0};
      for (int e = 0; e < 2; ++e) {
        float x = (e == 0 ? -1.0f : 1.0f) * (halfX - 0.45f);
        g[e] = a.terrain.height(x, faceZ + 0.4f);
        float top = -1e9f; bool seen = false;
        for (float y = g[e] - 0.2f; y < std::min(g[e] + 1.6f, topY) && got; y += 0.01f) {   // on the face only (sky above is white too)
          int wv = white(vec3(x, y, faceZ + 0.01f));
          if (wv == 1) { top = y; seen = true; }   // the highest white sample: grass blades crossing the scan are ignored
        }
        if (seen) rel[e] = top - g[e];
      }
      char b[260]; std::snprintf(b, sizeof b, "MA2: on a 15 deg slope the foundation line stays %.2f / %.2f m above the local ground at the wall's two ends (want 0.5 +- 0.15; the ends' ground differs by %.2f m)", rel[0], rel[1], g[1] - g[0]);
      check(got && std::fabs(rel[0] - 0.5f) < 0.15f && std::fabs(rel[1] - 0.5f) < 0.15f && std::fabs(g[1] - g[0]) > 0.6f, b);
      summary["ma2"] = {{"relLow", rel[0]}, {"relHigh", rel[1]}, {"groundDelta", g[1] - g[0]}};
      a.screenshot(outDirV() / "ma2_slope_debug.png");
      a.R.gxDebug = 0; measured = true;
      return false; });
    push("bunkers: OP and field shelter treatment", [this, countMat](App& a, float) {
      for (const char* id : {"bunker_observation_post", "bunker_field_shelter"}) {
        const AssetDef* d = a.lib.find(id);
        if (!d) { check(false, std::string(id) + " missing"); continue; }
        int turf = countMat(d, {"turf"}), earth = countMat(d, {"earth_raw", "earth_dark"}), grass = countMat(d, {"grass_a", "grass_b", "grass_c"});
        int net = countMat(d, {"camo_net"}), bags = countMat(d, {"hessian", "hessian_dk"}), moss = countMat(d, {"moss"});
        int concrete = 0; for (auto& m : d->raw.value("materials", json::array())) if (m.value("pattern", "") == "bunker") concrete += countMat(d, {m.value("name", "").c_str()});
        char b[300]; std::snprintf(b, sizeof b, "%s: flat turf wedges %d (was the whole cover), earth shoulder %d and grassed slope %d facets, camouflage net %d, sandbags %d, moss %d, board-formed concrete %d parts", id, turf, earth, grass, net, bags, moss, concrete);
        check(turf == 0 && earth >= 20 && grass >= 20 && net >= 2 && bags >= 4 && moss >= 4 && concrete >= 3, b);
        summary["bunkers"][id]["treatment"] = {{"earth", earth}, {"grass", grass}, {"net", net}, {"sandbags", bags}, {"moss", moss}, {"bunkerConcrete", concrete}};
      }
      return true; });
  }
  stage = 3251;
  if (vcur >= vseq.size()) { check(app.R.shaderErrors == 0, "all shaders compiled"); finish(app); return; }
  if (vt == 0) logf("[selftest] step: %s", vseq[vcur].name.c_str());
  bool d2 = vseq[vcur].fn(app, vt);
  vt += 1.0f / 60.0f;
  if (vt > 60) { check(false, "step timed out: " + vseq[vcur].name); d2 = true; }
  if (d2) { ++vcur; vt = 0; }
}
