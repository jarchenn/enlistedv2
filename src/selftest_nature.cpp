// --selftest-nature: natural ground dressing (docs/nature.md).
//  * every nat_* catalogue asset loads with the right kind, category, nav effect and collision (boulders: convex hulls;
//    patches, ferns, nettles: none); thumbnails are not blank;
//  * patch scatter is deterministic (same asset / radius / density / seed = same layout, through rebuilds, undo and a
//    save / reopen), seeds change it, it stays inside its radius and is not a grid (nearest-neighbour spread);
//  * boulder collision matches the visual mesh (rays against the physics body vs. the render triangles) and stops rounds;
//  * the navmesh contract: boulders solid, bushes brush areas, flowers / tall grass / stones / ferns ignored;
//  * V9 ground cover: brush / dry grass / clover / meadow patches (species, pebbles, colour), wheat squares (tiling,
//    snap sockets, no pasture grass under the crop, variants), the grass preset share, wheat flattened by blasts and a
//    moving tank (amount, radial / along-track direction), reset by F5;
//  * shader compile errors stay at 0; screenshots of each family; draws and frame time with and without a dressing set
//    on the village and Mont-Castre maps (selftest_out/nature_perf.json).
#include "selftest.h"
#include "mptest.h"
#include "nature.h"
#include "physics.h"
#include <cstring>
#include "thumbs.h"
#include <cstdarg>
#include <functional>
#include <chrono>
#include <fstream>
#include <set>
#include <algorithm>

bool crushStep(App& a, MpTest& t);   // V6 (selftest_snap.cpp)
namespace {
MpTest* T = nullptr;
int mode = -1; bool started = false;
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0;
fs::path pendingShot;
char B[700];
bool chk(bool ok, const char* f, ...) { va_list a; va_start(a, f); std::vsnprintf(B, sizeof B, f, a); va_end(a); return T->check(ok, B); }
void note(const char* f, ...) { va_list a; va_start(a, f); std::vsnprintf(B, sizeof B, f, a); va_end(a); T->note(B); logf("[nature] %s", B); }
void push(const std::string& n, std::function<bool(App&, float)> f) { seq.push_back({n, f}); }
fs::path outDir() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
float yawToward(vec3 d) { return degrees(std::atan2(-d.x, -d.z)); }
void look(App& a, vec3 from, vec3 at) { a.camPos = from; vec3 d = at - from; a.camYaw = yawToward(d); a.camPitch = degrees(std::atan2(d.y, glm::length(vec2(d.x, d.z)))); }
json perf = json::object();
int gMaxNatDraws = 0; size_t gMaxNatTufts = 0;

std::vector<const AssetDef*> natAssets(App& a) { std::vector<const AssetDef*> v; for (auto& x : a.lib.assets) if (x->id.rfind("nat_", 0) == 0) v.push_back(x.get()); return v; }
uint64_t place(App& a, const std::string& id, vec3 p, float yaw = 0) {
  const AssetDef* d = a.lib.find(id);
  if (!d) { chk(false, "asset %s exists", id.c_str()); return 0; }
  uint64_t r = a.placeInstance(d, p, yaw);
  if (!r) chk(false, "%s placed at (%.1f, %.1f)", id.c_str(), p.x, p.z);
  return r;
}
InstanceRT* rtOf(App& a, uint64_t id) { auto it = a.rt.find(id); return it == a.rt.end() ? nullptr : &it->second; }
void buildPatch(App& a, uint64_t id) {   // what the frame does: natureDrawItems rebuilds a stale patch near the camera
  Instance* i = a.doc.find(id); InstanceRT* r = rtOf(a, id); if (!i || !r) return;
  std::vector<DrawItem> op; natureDrawItems(a, *i, *r, op, 0.0f); a.R.natPatches.clear();
}
// Moller-Trumbore against the asset's render triangles (asset space)
bool rayTris(const std::vector<vec3>& tr, vec3 o, vec3 d, float& t) {
  t = 1e9f; bool hit = false;
  for (size_t k = 0; k + 2 < tr.size(); k += 3) {
    vec3 e1 = tr[k + 1] - tr[k], e2 = tr[k + 2] - tr[k], p = glm::cross(d, e2);
    float det = glm::dot(e1, p); if (std::fabs(det) < 1e-12f) continue;
    float inv = 1.0f / det; vec3 s = o - tr[k];
    float u = glm::dot(s, p) * inv; if (u < 0 || u > 1) continue;
    vec3 q = glm::cross(s, e1); float v = glm::dot(d, q) * inv; if (v < 0 || u + v > 1) continue;
    float tt = glm::dot(e2, q) * inv; if (tt > 1e-4f && tt < t) { t = tt; hit = true; }
  }
  return hit;
}
// frame-time window: average GPU / CPU ms, draws and triangles over n frames
struct Win { int n = 0; double gpu = 0, cpu = 0, draws = 0, tris = 0, wall = 0; double last = 0; };
double nowS() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
bool measure(App& a, Win& w, float t, int frames) {
  if (t < 1.0f) { w = Win{}; w.last = nowS(); return false; }   // settle (streaming, TAA, exposure)
  double n = nowS();
  w.gpu += a.R.stats.gpuMs; w.cpu += a.R.stats.cpuMs; w.draws += a.R.stats.draws; w.tris += a.R.stats.tris; w.wall += (n - w.last) * 1000.0; w.last = n;
  return ++w.n >= frames;
}
json winJson(const Win& w) { double k = 1.0 / std::max(1, w.n); return {{"frames", w.n}, {"gpu_ms", w.gpu * k}, {"cpu_ms", w.cpu * k}, {"frame_ms", w.wall * k}, {"draws", w.draws * k}, {"tris", w.tris * k}}; }

// a dressing set around a focus point: boulders, outcrops, stones, flowers, tall grass, shrubs and ferns (33 instances)
int dress(App& a, vec3 f, float yaw0) {
  static const char* rocks[] = {"nat_boulder_granite_l", "nat_boulder_limestone_m", "nat_boulder_schist_slab", "nat_boulder_granite_s", "nat_boulder_limestone_l", "nat_boulder_schist_m", "nat_outcrop_limestone", "nat_outcrop_schist"};
  static const char* patches[] = {"nat_stones_patch", "nat_scree_patch", "nat_stones_granite_patch", "nat_flowers_meadow", "nat_flowers_poppy", "nat_flowers_daisy", "nat_flowers_buttercup", "nat_flowers_cornflower", "nat_flowers_cowparsley", "nat_flowers_foxglove", "nat_flowers_clover", "nat_tallgrass_patch", "nat_tallgrass_verge", "nat_tallgrass_patch", "nat_flowers_meadow", "nat_stones_patch"};
  static const char* plants[] = {"nat_hawthorn_bush", "nat_bramble_thicket", "nat_blackthorn_low", "nat_gorse_shrub", "nat_bracken_clump", "nat_fern_clump", "nat_nettle_clump", "nat_sapling_ash", "nat_sapling_oak"};
  int n = 0; quat q = yawQuat(yaw0);
  auto at = [&](float x, float z) { vec3 w = f + q * vec3(x, 0, z); w.y = a.terrain.height(w.x, w.z); return w; };
  for (int k = 0; k < 8; ++k) n += place(a, rocks[k], at(-14.0f + 4.0f * k, -6.0f - 3.0f * (k & 1)), 37.0f * k) != 0;
  for (int k = 0; k < 16; ++k) n += place(a, patches[k], at(-18.0f + 2.4f * k + ((k * 7) % 3), 2.0f + 3.5f * (k % 3)), 11.0f * k) != 0;
  for (int k = 0; k < 9; ++k) n += place(a, plants[k], at(-15.0f + 3.6f * k, 12.0f + 2.0f * (k & 1)), 53.0f * k) != 0;
  a.syncRuntime();
  return n;
}

void build(App& app) {
  push("setup: Blank Slate", [](App& a, float t) {
    if (t == 0) {
      const TemplateInfo* tp = a.findTemplate("blank_slate");
      chk(tp && a.newMap(*tp, "Nature test"), "Blank Slate map");
      a.grass.quality = GrassQuality::Medium;
      chk(a.R.shaderErrors == 0, "shader compile errors at start: %d (grass species 5-12, rock pattern 29, fern / nettle cards)", a.R.shaderErrors);
    }
    return t > 0.3f;
  });
  // ---------------------------------------------------------------- assets
  push("assets load", [](App& a, float) {
    auto v = natAssets(a);
    int rocks = 0, patches = 0, plants = 0;
    for (auto* d : v) {
      NavEffect ne = resolveNavEffect(d->navMeta, d->enterable, d->bmin, d->bmax);
      if (d->nature.kind == NatureDef::ROCK) {
        ++rocks; vec3 s = d->bmax - d->bmin;
        chk(d->category == "Nature" && d->shape && d->opaque.count > 0 && !d->nature.tris.empty() && ne == NavEffect::Block && s.y > 0.3f && s.y < 3.2f && std::max(s.x, s.z) < 6.0f,
            "%s: boulder / outcrop, %zu pieces, %zu tris, %.1f x %.1f x %.1f m, convex-hull collider, nav block", d->id.c_str(), d->nature.pieces.size(), d->nature.tris.size() / 3, s.x, s.y, s.z);
      } else if (d->nature.kind == NatureDef::SCATTER) {
        ++patches;
        bool stones = d->nature.scatter == NatureDef::STONES;
        chk(!d->shape && ne == NavEffect::None && d->opaque.count > 0 && (stones ? d->nature.sizeMin >= 0.03f && d->nature.sizeMax <= 0.41f : !d->nature.mix.empty()),
            "%s: %s patch, radius %.1f m, %.1f / m^2, no collider, nav none%s", d->id.c_str(), stones ? "stone" : d->nature.scatter == NatureDef::TALLGRASS ? "tall-grass" : "flower",
            d->nature.radius, d->nature.density, stones ? "" : (", " + std::to_string(d->nature.mix.size()) + " species").c_str());
      } else if (d->tree.on) {
        ++plants; float h = d->bmax.y;
        bool soft = ne == NavEffect::None || ne == NavEffect::Area;
        chk(d->category == "Vegetation" && d->tree.cardVerts > 0 && h < 3.2f && soft && (ne != NavEffect::Area || d->navMeta.area == NavArea::Brush),
            "%s: small vegetation, %d card verts, %.1f m, nav %s", d->id.c_str(), d->tree.cardVerts, h, ne == NavEffect::Area ? "brush area" : ne == NavEffect::None ? "none" : "?");
      } else chk(false, "%s: unknown nature asset kind", d->id.c_str());
    }
    chk(rocks >= 8 && patches >= 12 && plants >= 9, "catalogue: %d boulders / outcrops, %d patches, %d small plants (%zu nat_* assets)", rocks, patches, plants, v.size());
    // the patch species cover the brief
    std::set<int> sp; for (auto* d : v) for (auto& m : d->nature.mix) sp.insert(m.first);
    chk(sp.size() == 16, "patch species: %zu of 16 (pasture turf, meadow grass, seed grass, weeds, straw; tall meadow grass, poppy, ox-eye daisy, buttercup, cornflower, cow parsley, foxglove, clover; wheat, brush, stubble)", sp.size());
    return true;
  });
  push("thumbnails", [](App& a, float) {
    int bad = 0; float worst = 0; std::string wid;
    fs::path d = outDir() / "nature_thumbs"; std::error_code ec; fs::create_directories(d, ec);
    for (auto* as : natAssets(a)) {
      float bg = thumbBackgroundShare(a, as->thumbnail); if (bg > worst) { worst = bg; wid = as->id; }
      bad += bg > 0.95f; saveThumbPng(as->thumbnail, d / (as->id + ".png"));
    }
    chk(bad == 0, "catalogue tiles: none over 95 %% background (emptiest %s %.1f %%); tiles in selftest_out/nature_thumbs/", wid.c_str(), worst * 100);
    return true;
  });
  // ---------------------------------------------------------------- scatter determinism
  push("scatter layout determinism", [](App& a, float) {
    for (auto* d : natAssets(a)) {
      if (d->nature.kind != NatureDef::SCATTER) continue;
      NatPatchParams p; p.radius = d->nature.radius; p.density = d->nature.density; p.seed = 7;
      std::vector<NatScatterPt> A, Bv, C; natureLayout(d->nature, d->id, p, A); natureLayout(d->nature, d->id, p, Bv);
      NatPatchParams p2 = p; p2.seed = 8; natureLayout(d->nature, d->id, p2, C);
      float maxR = 0; for (auto& q : A) maxR = d->nature.square ? std::max(maxR, std::max(std::fabs(q.p.x), std::fabs(q.p.y))) : std::max(maxR, glm::length(q.p));   // squares: half side
      // not a grid: nearest-neighbour distances spread (coefficient of variation), and no shared lattice phase
      std::vector<float> nn; for (size_t i = 0; i < A.size(); ++i) { float b = 1e9f; for (size_t j = 0; j < A.size(); ++j) if (i != j) b = std::min(b, glm::length(A[i].p - A[j].p)); nn.push_back(b); }
      double m = 0, s2 = 0; for (float x : nn) m += x; m /= std::max<size_t>(1, nn.size()); for (float x : nn) s2 += (x - m) * (x - m); double cv = std::sqrt(s2 / std::max<size_t>(1, nn.size())) / std::max(1e-6, m);
      double expect = d->nature.density * (d->nature.square ? 4.0 : 3.14159) * p.radius * p.radius;
      chk(natureLayoutHash(A) == natureLayoutHash(Bv) && natureLayoutHash(A) != natureLayoutHash(C) && maxR <= p.radius + 1e-3f && cv > 0.12 && A.size() > expect * 0.35,
          "%s: %zu items (target %.0f) inside r %.1f (max %.2f), same seed = same layout, new seed differs, nearest-neighbour CV %.2f (a grid is ~0)",
          d->id.c_str(), A.size(), expect, p.radius, maxR, cv);
    }
    return true;
  });
  static uint64_t P1 = 0, P2 = 0, S1 = 0; static uint64_t h1 = 0, h2 = 0, hs = 0;
  push("placed patches: rebuild, undo, save / reopen", [](App& a, float t) {
    if (t == 0) {
      P1 = place(a, "nat_flowers_meadow", vec3(10, 0, 10), 30); P2 = place(a, "nat_tallgrass_patch", vec3(-10, 0, 10), 0); S1 = place(a, "nat_scree_patch", vec3(0, 0, -12), 75);
      a.syncRuntime(); buildPatch(a, P1); buildPatch(a, P2); buildPatch(a, S1);
      h1 = naturePatchHash(*rtOf(a, P1)); h2 = naturePatchHash(*rtOf(a, P2)); hs = naturePatchHash(*rtOf(a, S1));
      chk(rtOf(a, P1)->natCount > 200 && rtOf(a, P2)->natCount > 200 && rtOf(a, S1)->natCount > 200, "patches built: meadow %d plants, tall grass %d, scree %d stones",
          rtOf(a, P1)->natCount, rtOf(a, P2)->natCount, rtOf(a, S1)->natCount);
      chk(rtOf(a, P1)->staticBody == 0xffffffffu && rtOf(a, P2)->staticBody == 0xffffffffu && rtOf(a, S1)->staticBody == 0xffffffffu, "patches have no physics body (visual only, never in the navmesh)");
      // forced rebuild (terrain / footprint change path) gives the same instances
      a.groundRev++; buildPatch(a, P1); buildPatch(a, S1);
      chk(naturePatchHash(*rtOf(a, P1)) == h1 && naturePatchHash(*rtOf(a, S1)) == hs, "rebuild after a ground change: identical instances");
      // edit the radius, then undo: back to the same layout
      a.pushUndo(); Instance* i = a.doc.find(P1); NatPatchParams pp = naturePatchParams(*i, *a.assetFor(*i)); pp.radius = 3.0f; setNaturePatchParams(*i, *a.assetFor(*i), pp); a.bump(*i);
      a.syncRuntime(); buildPatch(a, P1); uint64_t hr = naturePatchHash(*rtOf(a, P1));
      a.undo(); a.syncRuntime(); buildPatch(a, P1);
      chk(hr != h1 && naturePatchHash(*rtOf(a, P1)) == h1, "radius edit changes the patch; undo restores the identical layout");
      // a house placed into the patch clears the plants under it
      size_t before = rtOf(a, P1)->natCount;
      uint64_t H = place(a, "house_stone_cottage", vec3(12, 0, 12), 0); a.syncRuntime(); buildPatch(a, P1);
      size_t after = rtOf(a, P1)->natCount;
      chk(H && after < before, "a structure placed on the patch clears it under its footprint (%zu -> %zu plants)", before, after);
      if (H) { a.undo(); a.syncRuntime(); buildPatch(a, P1); }   // undo, not delete: placing a building also flattens its ground (V6), which only undo restores
      chk(naturePatchHash(*rtOf(a, P1)) == h1, "structure removed: the patch is back exactly");
      fs::path f = userMapsDir() / "selftest_nature.nmap.json";
      chk(a.saveMapTo(f), "map saved");
      chk(a.openMap(f), "map reopened"); a.syncRuntime();
      buildPatch(a, P1); buildPatch(a, P2); buildPatch(a, S1);
      chk(rtOf(a, P1) && naturePatchHash(*rtOf(a, P1)) == h1 && naturePatchHash(*rtOf(a, P2)) == h2 && naturePatchHash(*rtOf(a, S1)) == hs, "save / reopen: all three patches bit-identical");
    }
    return t > 0.2f;
  });
  // ---------------------------------------------------------------- V9 ground cover and wheat (docs/nature.md#ground-cover)
  static uint64_t WA = 0, WB = 0, WP = 0, TK = 0;
  push("V9 ground cover patches", [](App& a, float t) {
    static std::vector<std::pair<uint64_t, std::string>> placed;
    if (t == 0) {
      const TemplateInfo* tp = a.findTemplate("blank_slate"); a.newMap(*tp, "Nature ground cover");
      a.grass.quality = GrassQuality::Medium;
      static const char* ids[] = {"nat_brush_patchy", "nat_brush_scrub", "nat_brush_rank", "nat_drygrass_tufts", "nat_drygrass_tall", "nat_drygrass_sparse", "nat_clover_tufts", "nat_green_tufts", "nat_meadow_mixed"};
      placed.clear();
      for (int k = 0; k < 9; ++k) placed.push_back({place(a, ids[k], vec3(-36.0f + 9.0f * k, 0, 30), 17.0f * k), ids[k]});
      a.syncRuntime();
      for (auto& [id, name] : placed) {
        buildPatch(a, id); InstanceRT* r = rtOf(a, id); const AssetDef* d = r ? r->asset : nullptr;
        if (!r || !d) { chk(false, "%s placed", name.c_str()); continue; }
        bool peb = d->nature.pebDensity > 0;
        std::set<int> sp; for (auto& g : r->natInst) { uint32_t u; std::memcpy(&u, &g.shape.z, 4); sp.insert((int)((u >> 24) & 0xff) - 0x38); }
        chk(r->natInst.size() > 100 && sp.size() >= 3 && (!peb || (r->natPos.size() > 5 && r->natMesh.count > 0)),
            "%s: %zu plants of %zu species%s", name.c_str(), r->natInst.size(), sp.size(), peb ? (", " + std::to_string(r->natPos.size()) + " pebbles under them").c_str() : "");
      }
      // dry grass is straw coloured, clover / green tufts are green (mean albedo of the instance colours)
      auto meanAlb = [&](const std::string& id) { vec3 s(0); size_t n = 0; for (auto& [pid, nm] : placed) if (nm == id) for (auto& g : rtOf(a, pid)->natInst) { uint32_t u; std::memcpy(&u, &g.shape.z, 4); s += vec3(u & 0xff, (u >> 8) & 0xff, (u >> 16) & 0xff); ++n; } return n ? s / (float)n : s; };
      vec3 dry = meanAlb("nat_drygrass_tufts"), grn = meanAlb("nat_green_tufts");
      chk(dry.r / std::max(1.0f, dry.g) > 0.95f && grn.g / std::max(1.0f, grn.r) > 1.3f, "dry grass reads straw (r/g %.2f), green tufts read green (g/r %.2f)", dry.r / std::max(1.0f, dry.g), grn.g / std::max(1.0f, grn.r));
      look(a, vec3(0, 4.5f, 44), vec3(0, 0.3f, 30)); a.selected = 0; a.R.resetTemporal();
    }
    if (t > 1.5f) { pendingShot = outDir() / "nature_groundcover.png"; note("screenshot selftest_out/nature_groundcover.png"); return true; }
    return false;
  });
  push("wheat tiles", [](App& a, float t) {
    if (t == 0) {
      WA = place(a, "nat_wheat_field", vec3(0, 0, 0), 0); WB = place(a, "nat_wheat_field", vec3(10, 0, 0), 0);
      WP = place(a, "nat_flowers_poppy", vec3(-3, 0, -3), 0);
      a.syncRuntime(); buildPatch(a, WA); buildPatch(a, WB); buildPatch(a, WP);
      InstanceRT* ra = rtOf(a, WA); InstanceRT* rb = rtOf(a, WB);
      if (!ra || !rb) { chk(false, "wheat tiles placed"); return true; }
      const AssetDef* d = ra->asset;
      chk(ra->natInst.size() > 2500 && ra->natMesh.count > 0 && d->nature.square && std::fabs(d->nature.minR - 5) < 1e-3f && std::fabs(d->nature.maxR - 5) < 1e-3f,
          "a 10 m wheat square: %zu stalks, a soil / straw layer (%d indices), size locked to 10 m so tiles stay aligned", ra->natInst.size(), ra->natMesh.count);
      // seam: both tiles grow right up to the shared edge, and their layouts differ (no visible repeat)
      float aMax = -1e9f, bMin = 1e9f; for (auto& g : ra->natInst) aMax = std::max(aMax, g.posYaw.x); for (auto& g : rb->natInst) bMin = std::min(bMin, g.posYaw.x);
      chk(aMax > 4.75f && aMax <= 5.0f && bMin < 5.25f && bMin >= 5.0f && naturePatchHash(*ra) != naturePatchHash(*rb), "tiles meet at the seam x = 5: left reaches %.2f, right starts %.2f; layouts differ", aMax, bMin);
      // V6 snap sockets on the four edges
      const json& sn = d->raw.contains("snap") ? d->raw["snap"] : json();
      int ok = 0; if (sn.is_array()) for (auto& s : sn) { vec3 p = jvec3(s["p"]), dr = jvec3(s["dir"]); ok += s.value("kind", "") == "wheat" && std::fabs(glm::length(p) - 5) < 1e-3f && glm::dot(glm::normalize(p), dr) > 0.999f; }
      chk(ok == 4, "wheat squares carry 4 edge sockets {p, dir, kind: wheat} for snapping (%d valid)", ok);
      // pasture grass does not grow under the crop; other patches still do
      auto fp = a.grassFootprints();
      bool under = a.grass.density(1.0f, 1.0f, a.terrain, fp), outside = a.grass.density(0.0f, -20.0f, a.terrain, fp);
      chk(!under && rtOf(a, WP)->natInst.size() > 20, "no pasture grass under the wheat (grows outside: %s); a poppy patch inside the field keeps %zu plants", outside ? "yes" : "no", rtOf(a, WP)->natInst.size());
      // variants
      uint64_t T1 = place(a, "nat_wheat_trampled", vec3(0, 0, 14), 0), T2 = place(a, "nat_wheat_stubble", vec3(10, 0, 14), 0);
      a.syncRuntime(); buildPatch(a, T1); buildPatch(a, T2);
      float ripe = naturePatchFlatShare(*ra), tr = naturePatchFlatShare(*rtOf(a, T1)), stb = naturePatchFlatShare(*rtOf(a, T2));
      chk(ripe < 0.12f && tr > 0.2f && stb > 0.08f && stb < 0.4f && rtOf(a, T2)->natPos.size() > 5, "variants: ripe %.0f %% laid (lodged patches), trampled %.0f %%, stubble %.0f %% lying straw with %zu stones",
          ripe * 100, tr * 100, stb * 100, rtOf(a, T2)->natPos.size());
      look(a, vec3(-6, 1.7f, -9), vec3(3, 0.6f, 2)); a.selected = 0; a.R.resetTemporal();
    }
    if (t > 1.5f) { pendingShot = outDir() / "nature_wheat.png"; note("screenshot selftest_out/nature_wheat.png"); return true; }
    return false;
  });
  push("wheat follows the grass quality preset", [](App& a, float t) {
    static size_t tufts[4]; static int q = 0; static float t0 = 0;
    if (t == 0) { q = 0; t0 = 0; a.grass.quality = GrassQuality::Off; look(a, vec3(5, 6, -14), vec3(5, 0, 0)); }
    if (t - t0 > 0.35f) {
      tufts[q] = a.R.natTufts;
      if (++q == 4) {
        a.grass.quality = GrassQuality::Medium;
        chk(tufts[0] == 0 && tufts[1] < tufts[2] && tufts[2] < tufts[3], "patch plants drawn follow the grass preset: Off %zu, Low %zu, Medium %zu, High %zu", tufts[0], tufts[1], tufts[2], tufts[3]);
        return true;
      }
      a.grass.quality = (GrassQuality)q; t0 = t;
    }
    return false;
  });
  auto flatIn = [](App& a, uint64_t id, std::function<bool(vec2)> in, float atLeast, float* meanDir = nullptr) {
    InstanceRT* r = rtOf(a, id); size_t n = 0, f = 0; double sx = 0, sz = 0;
    for (auto& g : r->natInst) {
      uint32_t u; std::memcpy(&u, &g.shape.z, 4); if ((int)((u >> 24) & 0xff) - 0x38 != NS_WHEAT) continue;
      if (!in(vec2(g.posYaw.x, g.posYaw.z))) continue;
      ++n; float fl = std::floor(g.shape.y) / 63.0f; f += fl >= atLeast;
      float dir = (g.shape.y - std::floor(g.shape.y)) / 0.98f * 6.2831853f; if (fl >= atLeast) { sx += std::sin(dir); sz += std::cos(dir); }
    }
    if (meanDir) *meanDir = (float)std::atan2(sx, sz);
    return n ? (float)f / n : -1.0f;
  };
  push("wheat flattening: blasts", [flatIn](App& a, float t) {
    if (t == 0) {
      size_t s0 = natureFlattenStamps();
      vec3 p(2, a.terrain.height(2, 2) + 0.3f, 2);
      a.blastPaths(p, 110, 4.0f);   // the App::explode hook (paths.cpp -> natureBlast)
      buildPatch(a, WA); buildPatch(a, WB);
      float nearS = flatIn(a, WA, [](vec2 q) { return glm::length(q - vec2(2, 2)) < 1.5f; }, 0.6f);
      float farS = flatIn(a, WA, [](vec2 q) { return glm::length(q - vec2(2, 2)) > 7.0f; }, 0.6f);
      // radial: stalks east of the burst lie toward +x
      float dirE = 0; float eastS = flatIn(a, WA, [](vec2 q) { return q.x > 3.0f && q.x < 4.5f && std::fabs(q.y - 2) < 0.5f; }, 0.4f, &dirE);
      chk(natureFlattenStamps() == s0 + 1 && nearS > 0.9f && farS < 0.12f && eastS > 0.8f && std::fabs(degrees(dirE) - 90) < 25,
          "a shell burst lays the wheat flat: %.0f %% within 1.5 m, %.0f %% beyond 7 m (the crop's own lodging), east of it laid toward %.0f deg (radial = 90)", nearS * 100, farS * 100, degrees(dirE));
      size_t s1 = natureFlattenStamps(); natureBlast(a, vec3(60, a.terrain.height(60, 60), 60), 110, 4.0f);
      chk(natureFlattenStamps() == s1, "a burst away from any wheat records nothing");
    }
    return t > 0.2f;
  });
  push("wheat flattening: a tank drives through", [flatIn](App& a, float t) {
    if (t == 0) {
      const AssetDef* as = a.lib.find("tank_sherman_firefly");
      TK = as ? a.placeInstance(as, vec3(0, 0, -25), 0.0f) : 0; a.syncRuntime();
      if (!chk(TK && a.enterExplore(), "a Sherman by the field; F5 Explore")) return true;
      buildPatch(a, WA);
      chk(natureFlattenStamps() == 0 && flatIn(a, WA, [](vec2 q) { return glm::length(q - vec2(2, 2)) < 1.5f; }, 0.6f) < 0.15f, "entering Explore resets the session: the burst's wheat stands again");
      a.tanks.sync(a); auto it = a.tanks.tanks.find(TK);
      if (!chk(it != a.tanks.tanks.end(), "the Sherman is a live tank")) return true;
      TankRT& tk = it->second; vec3 keep = tk.pos;
      for (float z = -12; z <= 12.01f; z += 0.5f) { tk.pos = vec3(0, a.terrain.height(0, z), z); tk.yaw = 0; a.now += 0.05; natureStepFlatten(a); }
      tk.pos = keep;
      buildPatch(a, WA);
      float hw = tk.def->halfSize.x;
      float mean = 0, under = flatIn(a, WA, [hw](vec2 q) { return std::fabs(q.x) < hw - 0.1f && std::fabs(q.y) < 4.0f; }, 0.9f, &mean);
      float side = flatIn(a, WA, [hw](vec2 q) { return std::fabs(q.x) > hw + 1.2f && std::fabs(q.y) < 4.0f; }, 0.6f);
      chk(under > 0.95f && side < 0.12f && std::fabs(degrees(mean)) < 20, "a tank swath: %.0f %% flat under the %.1f m hull, %.0f %% beside it, laid along the track (%.0f deg; 0 = its heading), %zu stamps",
          under * 100, 2 * hw, side * 100, degrees(mean), natureFlattenStamps());
      a.resetDamage(); buildPatch(a, WA);
      chk(natureFlattenStamps() == 0 && flatIn(a, WA, [hw](vec2 q) { return std::fabs(q.x) < hw - 0.1f && std::fabs(q.y) < 4.0f; }, 0.9f) < 0.15f, "reset (F5 / Return to start): the wheat stands again");
      a.exitExplore();
      // a swath for the screenshot (Creative keeps the session stamps until the next reset)
      for (float z = -6; z <= 6; z += 0.5f) natureTrample(a, vec2(-1.5f, z), 10.0f, vec2(1.6f, 3.2f));
      a.blastPaths(vec3(3, a.terrain.height(3, -2) + 0.2f, -2), 110, 3.5f);
      look(a, vec3(-4, 2.4f, -11), vec3(0, 0.3f, 0)); a.selected = 0; a.R.resetTemporal();
    }
    if (t > 1.5f) { pendingShot = outDir() / "nature_wheat_flattened.png"; note("screenshot selftest_out/nature_wheat_flattened.png"); natureResetFlatten(); return true; }
    return false;
  });
  // ---------------------------------------------------------------- collision vs visuals
  static std::vector<std::pair<uint64_t, std::string>> rocksPlaced;
  push("boulder collision matches the mesh", [](App& a, float t) {
    if (t == 0) {
      const TemplateInfo* tp = a.findTemplate("blank_slate"); a.newMap(*tp, "Nature rocks");
      rocksPlaced.clear(); int k = 0;
      for (auto* d : natAssets(a)) if (d->nature.kind == NatureDef::ROCK) { rocksPlaced.push_back({place(a, d->id, vec3(-36.0f + 9.0f * k, 0, -30), 23.0f * k), d->id}); ++k; }
      a.syncRuntime();
      return false;
    }
    if (t < 0.3f) return false;
    for (auto& [id, name] : rocksPlaced) {
      Instance* i = a.doc.find(id); InstanceRT* r = rtOf(a, id);
      if (!i || !r) { chk(false, "%s placed", name.c_str()); continue; }
      const AssetDef& d = *r->asset; mat4 M = a.instanceMatrix(*i), Mi = glm::inverse(M);
      vec3 c = vec3(M * vec4((d.bmin + d.bmax) * 0.5f, 1)); float ext = glm::length(d.bmax - d.bmin) * 0.5f;
      int rays = 0, agree = 0, leak = 0; float worst = 0, sumE = 0;
      for (int k = 0; k < 48; ++k) {
        float az = k * 0.5236f + (k / 12) * 0.26f, el = (k / 12) == 3 ? 1.2f : 0.06f + (k / 12) * 0.25f;
        float hgt = (d.bmax.y - std::max(0.0f, d.bmin.y)) * (0.2f + 0.2f * (k % 3));
        vec3 dir = -vec3(std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az));
        vec3 tgt = vec3(c.x, i->pos.y + hgt, c.z) + vec3(std::sin(az * 3.1f), 0, std::cos(az * 1.7f)) * ext * 0.25f;
        vec3 o = tgt - dir * (ext + 6.0f);
        float tm; bool mh = rayTris(d.nature.tris, vec3(Mi * vec4(o, 1)), glm::normalize(vec3(Mi * vec4(dir, 0))), tm);
        Physics::Hit h; bool any = Physics::castRayEx(o, dir, ext * 2 + 12.0f, h), ph = any && (h.ud >> 16) == id;
        if (any && !ph && (!mh || h.t < tm - 0.02f)) continue;   // the ground (or a neighbour) in front of the rock: occluded ray
        if (!mh && !ph) continue;
        ++rays;
        if (mh && !ph) { ++leak; continue; }
        if (!mh) continue;   // grazing the hull where the mesh has a slight concavity: counted as a ray, not as agreement
        float e = std::fabs(h.t - tm); worst = std::max(worst, e); sumE += e;
        agree += e <= 0.05f + 0.04f * ext;
      }
      chk(rays >= 20 && agree >= rays * 0.85f && leak == 0, "%s: %d / %d rays hit the hull within %.2f m of the visible surface (mean %.3f m, worst %.2f m), %d rays through visible rock",
          name.c_str(), agree, rays, 0.05f + 0.04f * ext, sumE / std::max(1, agree), worst, leak);
      // a rifle round stops on it
      vec3 o = c + vec3(0, 0.1f, ext + 5.0f), dir(0, 0, -1);
      Physics::Hit h;
      if (Physics::castRayEx(o, dir, ext + 10.0f, h) && (h.ud >> 16) == id) {
        Projectile p; p.p = o; p.v = dir * 820.0f; p.kind = PROJ_BULLET; p.dmg = 50; p.mass = 0.0098f;
        std::string surf; size_t sp0 = a.spawnedProjectiles.size();
        bool pen = a.bulletImpact(p, h, surf);
        chk(!pen && a.spawnedProjectiles.size() == sp0, "%s: a 7.92 mm round stops on the boulder (no penetration)", name.c_str());
      } else chk(false, "%s: the boulder blocks a rifle line", name.c_str());
    }
    return true;
  });
  push("navmesh contract", [](App& a, float) {
    // AiCore::classify (ai_core.cpp): solid bodies are carved / blocked by their triangles, Area / None are skipped
    int ok = 0, n = 0;
    for (auto* d : natAssets(a)) {
      NavEffect ne = resolveNavEffect(d->navMeta, d->enterable, d->bmin, d->bmax); ++n;
      bool hasBody = d->shape != nullptr || (d->tree.on && !d->parts.empty());
      if (d->nature.kind == NatureDef::ROCK) ok += ne == NavEffect::Block && d->shape;
      else if (d->nature.kind == NatureDef::SCATTER) ok += ne == NavEffect::None && !hasBody;
      else if (d->tree.on) ok += (ne == NavEffect::Area && d->navMeta.area == NavArea::Brush && d->bmax.y > 0.8f) || (ne == NavEffect::None && (d->bmax.y < 1.3f || d->tree.trunkR < 0.05f));
    }
    chk(ok == n, "%d / %d assets: boulders solid (block), shrubs brush areas, flowers / tall grass / stones / ferns / nettles / saplings ignored", ok, n);
    return true;
  });
  // ---------------------------------------------------------------- screenshots (Blank Slate, one family per shot)
  struct Shot { std::string name; vec3 from, at; std::function<void(App&)> setup; };
  static std::vector<Shot> shots;
  static size_t si = 0;
  shots = {
    {"nature_boulders", vec3(-41, 2.6f, -21.5f), vec3(-22, 0.4f, -30), nullptr},
    {"nature_boulders_close", vec3(-6.5f, 1.6f, -25.5f), vec3(-9, 0.5f, -30), nullptr},
    {"nature_outcrops", vec3(19, 3.4f, -21), vec3(30, 0.8f, -30), nullptr},
    {"nature_stones", vec3(0, 1.9f, 4.5f), vec3(0, 0, -1.5f), [](App& a) {
      place(a, "nat_stones_patch", vec3(-4, 0, 0), 0); place(a, "nat_scree_patch", vec3(3, 0, -1), 0); place(a, "nat_stones_granite_patch", vec3(0, 0, -6), 0); }},
    {"nature_flowers", vec3(0, 1.3f, 26), vec3(0, 0.2f, 16), [](App& a) {
      const char* f[] = {"nat_flowers_poppy", "nat_flowers_daisy", "nat_flowers_buttercup", "nat_flowers_cornflower", "nat_flowers_cowparsley", "nat_flowers_foxglove", "nat_flowers_clover", "nat_flowers_meadow"};
      for (int k = 0; k < 8; ++k) { uint64_t id = place(a, f[k], vec3(-10.5f + 3.0f * k, 0, 17 - (k & 1) * 2.5f), 0); if (Instance* i = a.doc.find(id)) { NatPatchParams p = naturePatchParams(*i, *a.assetFor(*i)); p.radius = 1.6f; setNaturePatchParams(*i, *a.assetFor(*i), p); a.bump(*i); } } }},
    {"nature_flowers_close", vec3(-6.2f, 0.55f, 18.6f), vec3(-5.0f, 0.25f, 16.0f), nullptr},
    {"nature_tallgrass_crouch", vec3(-30, 1.0f, 22), vec3(-30, 0.5f, 14), [](App& a) {
      place(a, "nat_tallgrass_patch", vec3(-30, 0, 15), 0); place(a, "nat_tallgrass_verge", vec3(-25, 0, 13), 0); place(a, "nat_tallgrass_patch", vec3(-34, 0, 12), 0);
      place(a, "def_sandbag_straight", vec3(-30, 0, 15.5f), 0); }},   // a low sandbag run standing in for a crouched / prone soldier
    {"nature_shrubs", vec3(30, 1.9f, 21.5f), vec3(30, 0.6f, 13), [](App& a) {
      const char* p[] = {"nat_hawthorn_bush", "nat_bramble_thicket", "nat_blackthorn_low", "nat_gorse_shrub", "nat_bracken_clump", "nat_fern_clump", "nat_nettle_clump", "nat_sapling_ash", "nat_sapling_oak"};
      for (int k = 0; k < 9; ++k) place(a, p[k], vec3(19.0f + 2.8f * k, 0, 14 - (k % 2) * 2.5f), 30.0f * k); }},
  };
  push("V6: a house on shrubs crushes them and flattens the ground", [](App& a, float) { return crushStep(a, *T); });   // selftest_snap.cpp
  push("screenshots", [](App& a, float t) {
    static float t0 = 0; static bool armed = false;
    if (t == 0) { si = 0; t0 = 0; armed = false; }
    if (si >= shots.size()) return true;
    if (!armed) {
      if (shots[si].setup) { shots[si].setup(a); a.syncRuntime(); }
      look(a, shots[si].from, shots[si].at); a.selected = 0; a.R.resetTemporal(); t0 = t; armed = true;
      return false;
    }
    gMaxNatDraws = std::max(gMaxNatDraws, a.R.natDraws); gMaxNatTufts = std::max(gMaxNatTufts, a.R.natTufts);
    if (t - t0 > 1.4f) { pendingShot = outDir() / (shots[si].name + ".png"); note("screenshot selftest_out/%s.png", shots[si].name.c_str()); ++si; armed = false; }
    return false;
  });
  push("shader errors", [](App& a, float t) {
    if (t < 0.2f) return false;
    chk(a.R.shaderErrors == 0 && a.R.fboErrors == 0, "shader compile errors %d, incomplete framebuffers %d (after drawing every family)", a.R.shaderErrors, a.R.fboErrors);
    chk(gMaxNatDraws > 0 && gMaxNatTufts > 0, "plant patches drawn through the grass program: up to %d instanced draws, %zu plants in one frame", gMaxNatDraws, gMaxNatTufts);
    return true;
  });
  // ---------------------------------------------------------------- performance: village, Mont-Castre
  static Win w0, w1, w2; static int rb0 = 0, rb1 = 0; static vec3 F; static float Y = 0; static int placed = 0; static std::string mapName;
  auto perfMap = [](const std::string& tag, std::function<bool(App&)> open, std::function<void(App&)> camera) {
    push(tag + ": open", [open, camera, tag](App& a, float t) {
      if (t == 0) { chk(open(a), "%s map opened", tag.c_str()); a.grass.quality = GrassQuality::Medium; camera(a); a.selected = 0; }
      return t > 2.0f;
    });
    // interleaved A/B (the GPU is shared with other processes): three rounds of 60 frames without / with the dressing
    // set, medians per state; the set is toggled by swapping the instance list (ids and revs kept)
    push(tag + ": A/B", [tag](App& a, float t) {
      static std::vector<Instance> off, on; static int phase = 0; static float t0 = 0; static int frames = 0;
      static std::vector<double> g[2], c[2], f[2], d[2]; static double last = 0;
      if (t == 0) {
        off = a.doc.instances; placed = dress(a, F, Y); on = a.doc.instances;
        for (int k = 0; k < 2; ++k) { g[k].clear(); c[k].clear(); f[k].clear(); d[k].clear(); }
        phase = 0; t0 = 0; frames = -1; rb0 = gNatRebuilds;
        a.doc.instances = off; a.syncRuntime(); a.R.resetTemporal();
        return false;
      }
      int st_ = phase & 1;
      if (frames < 0) { if (t - t0 > 1.2f) { frames = 0; last = nowS(); } return false; }   // settle after a toggle
      double n = nowS();
      g[st_].push_back(a.R.stats.gpuMs); c[st_].push_back(a.R.stats.cpuMs); f[st_].push_back((n - last) * 1000.0); d[st_].push_back(a.R.stats.draws); last = n;
      if (++frames < 60) return false;
      if (++phase < 6) {
        if (phase == 5) rb1 = gNatRebuilds;   // the last dressed window: no rebuilds expected (rb counted below)
        a.doc.instances = (phase & 1) ? on : off; a.syncRuntime(); a.R.resetTemporal(); t0 = t; frames = -1;
        if (phase & 1) rb0 = gNatRebuilds - 0;   // rebuilding the dressed set's patches happens at each toggle: count from the settle
        return false;
      }
      auto med = [](std::vector<double> v) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
      auto mean = [](const std::vector<double>& v) { double s2 = 0; for (double x : v) s2 += x; return v.empty() ? 0.0 : s2 / v.size(); };
      json j[2];
      for (int k = 0; k < 2; ++k) j[k] = {{"frames", g[k].size()}, {"gpu_ms_median", med(g[k])}, {"cpu_ms_median", med(c[k])}, {"frame_ms_median", med(f[k])}, {"draws", mean(d[k])}};
      perf[tag] = {{"without", j[0]}, {"with", j[1]}, {"instances", placed}};
      note("%s: without %.0f draws, GPU %.2f ms, CPU %.2f ms, frame %.1f ms | with the 33-item set %.0f draws, GPU %.2f ms, CPU %.2f ms, frame %.1f ms (medians of 3 x 60 frames each, interleaved)",
           tag.c_str(), j[0]["draws"].get<double>(), j[0]["gpu_ms_median"].get<double>(), j[0]["cpu_ms_median"].get<double>(), j[0]["frame_ms_median"].get<double>(),
           j[1]["draws"].get<double>(), j[1]["gpu_ms_median"].get<double>(), j[1]["cpu_ms_median"].get<double>(), j[1]["frame_ms_median"].get<double>());
      double dd = j[1]["draws"].get<double>() - j[0]["draws"].get<double>();
      chk(placed >= 30 && dd < 160, "%s: the 33-item dressing set adds %.0f draw calls (patches: one instanced draw each; solids also in the shadow passes)", tag.c_str(), dd);
      chk(a.R.shaderErrors == 0, "%s: shader errors 0", tag.c_str());
      a.doc.instances = on; a.syncRuntime();
      pendingShot = outDir() / ("nature_perf_" + tag + ".png");
      return true;
    });
    push(tag + ": still view", [tag](App& a, float t) {   // dressed and still: patches are built once, never per frame
      if (t < 1.0f) { rb0 = gNatRebuilds; return false; }
      if (t < 3.0f) return false;
      chk(gNatRebuilds == rb0, "%s: no patch rebuilds over 2 s of a still view (%d)", tag.c_str(), gNatRebuilds - rb0);
      return true;
    });
  };
  perfMap("village", [](App& a) { return a.openMap(contentDir() / "examples" / "village.nmap.json"); }, [](App& a) {
    vec3 c = a.doc.start.set ? a.doc.start.pos : vec3(0, 0, 30);
    F = c + vec3(0, 0, -22); F.y = a.terrain.height(F.x, F.z); Y = 0;
    look(a, c + vec3(0, 2.0f, 0), F + vec3(0, 0.5f, 0));
  });
  perfMap("montcastre", [](App& a) {
    const TemplateInfo* tp = a.findTemplate("normandy_forest");
    if (!tp || !a.newMap(*tp, "Mont-Castre dressing")) return false;
    // a cliff foot: the steepest sample on a coarse grid, then a spot 14 m out from it on gentler ground
    const Terrain& t = a.terrain; float best = 1; vec3 cliff(0);
    for (float z = t.org.y + 60; z < t.org.y + t.sizeZ - 60; z += 12) for (float x = t.org.x + 60; x < t.org.x + t.size - 60; x += 12) {
      vec3 n = t.normal(x, z); if (n.y < best && t.height(x, z) > t.waterLevel + 3) { best = n.y; cliff = vec3(x, t.height(x, z), z); }
    }
    vec3 dn = t.normal(cliff.x, cliff.z); vec2 out = glm::normalize(vec2(dn.x, dn.z) + vec2(1e-4f));
    F = cliff + vec3(out.x, 0, out.y) * 16.0f; F.y = t.height(F.x, F.z); Y = yawToward(vec3(-out.x, 0, -out.y));
    mapName = "normandy_forest";
    note("Mont-Castre: steepest face at (%.0f, %.0f), slope %.0f deg; dressing its foot", cliff.x, cliff.z, degrees(std::acos(std::clamp(best, -1.0f, 1.0f))));
    return true;
  }, [](App& a) {
    quat q = yawQuat(Y);
    vec3 from = F + q * vec3(0, 0, 26.0f); from.y = a.terrain.height(from.x, from.z) + 2.2f;
    look(a, from, F + vec3(0, 1.0f, 0));
  });
  push("finish", [](App& a, float t) {
    if (t < 0.3f) return false;
    std::ofstream(outDir() / "nature_perf.json") << perf.dump(2);
    chk(a.R.shaderErrors == 0, "shader errors at the end: %d", a.R.shaderErrors);
    return true;
  });
}
}  // namespace

bool natureSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-nature") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) { if (!pendingShot.empty()) { app.screenshot(pendingShot); pendingShot.clear(); } return true; }
  if (!started) { started = true; T = new MpTest("nature"); build(app); cur = 0; st = 0; }
  if (cur < seq.size()) {
    bool done = seq[cur].fn(app, st);
    st += dt; total += dt;
    if (done) { ++cur; st = 0; }
    if (total > 400) { T->check(false, "nature test timed out in step '" + seq[std::min(cur, seq.size() - 1)].name + "'"); cur = seq.size(); }
  }
  if (cur >= seq.size() && !app.selftest->done) {
    int code = T->finish();
    app.selftest->exitCode = code; app.selftest->done = true;
  }
  return true;
}
