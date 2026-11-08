// Recast/Detour navigation mesh: tile pipeline, background rebuilds, commits and main-thread queries (nav.h).
#include "nav_internal.h"
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourCommon.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

static_assert(sizeof(dtPolyRef) == sizeof(NavRef), "build Detour with DT_POLYREF64 (cmake/recast.cmake)");

const char* navLinkName(int k) { static const char* n[] = {"drop", "mantle", "vault", "jump"}; return k >= 0 && k < LINK_KINDS ? n[k] : "?"; }

void NavConfig::load(const json& j) {
  if (!j.is_object()) return;
  tile = j.value("tile_m", tile); cs = j.value("cell_m", cs); ch = j.value("cellHeight_m", ch);
  radius = j.value("agentRadius_m", radius); height = j.value("agentHeight_m", height); crouchHeight = j.value("crouchHeight_m", crouchHeight);
  climb = j.value("maxStep_m", climb); slope = j.value("maxSlope_deg", slope);
  regionMin = j.value("regionMinSize", regionMin); regionMerge = j.value("regionMergeSize", regionMerge);
  edgeMaxLen = j.value("edgeMaxLen_m", edgeMaxLen); edgeMaxError = j.value("edgeMaxError", edgeMaxError);
  detailDist = j.value("detailSampleDist", detailDist); detailErr = j.value("detailSampleMaxError", detailErr);
  shallowWater = j.value("shallowWater_m", shallowWater); workers = j.value("rebuildWorkers", workers);
  if (j.contains("links")) {
    const json& l = j["links"];
    dropMin = l.value("dropMin_m", dropMin); dropMax = l.value("dropMax_m", dropMax); mantleMin = l.value("mantleMin_m", mantleMin); mantleMax = l.value("mantleMax_m", mantleMax);
    vaultMax = l.value("vaultMaxHeight_m", vaultMax); vaultThick = l.value("vaultMaxThickness_m", vaultThick); gapMax = l.value("gapMax_m", gapMax);
    linkSpacing = l.value("spacing_m", linkSpacing); linkBorder = l.value("probeBorder_m", linkBorder);
    if (l.contains("cost_m")) for (int k = 0; k < LINK_KINDS; ++k) linkCost[k] = l["cost_m"].value(navLinkName(k), linkCost[k]);
  }
  // Detour tiles must be a whole number of cells
  tile = std::round(tile / cs) * cs;
}

// ---------------------------------------------------------------- jobs and threads
struct NavBuildJob {
  int tx = 0, tz = 0; uint32_t gen = 0, idBase = 0;
  std::shared_ptr<NavBodySet> bodies; NavAreaMarks marks; float water = -1000;
  std::vector<float> wgrid; float wx0 = 0, wz0 = 0, wstep = 1; int wn = 0;   // sampled local water (Nav::waterQuery), empty = none
  float waterAt(float x, float z) const {
    if (wgrid.empty()) return water;
    int i = std::clamp((int)std::lround((x - wx0) / wstep), 0, wn - 1), k = std::clamp((int)std::lround((z - wz0) / wstep), 0, wn - 1);
    return std::max(water, wgrid[(size_t)k * wn + i]);
  }
  unsigned char* data = nullptr; int size = 0; std::vector<NavLink> links; double ms = 0; int polys = 0, tris = 0, kinds[4] = {0, 0, 0, 0};
  ~NavBuildJob() { if (data) dtFree(data); }
};

struct NavImpl {
  std::mutex qm; std::condition_variable cv;
  std::deque<std::unique_ptr<NavBuildJob>> queue;
  std::vector<std::unique_ptr<NavBuildJob>> done;
  std::vector<std::thread> threads; bool quit = false; std::atomic<int> running{0};
  using Key = std::pair<int, int>;
  std::map<Key, uint32_t> genIssued, genCommitted, stamps;
  std::map<Key, std::vector<NavLink>> tileLinks;
  std::map<uint32_t, NavLink> linkById;
  int tx0 = 0, tz0 = 0, tx1 = -1, tz1 = -1;
  dtQueryFilter filter;
  NavConfig cfg;
};

static void buildTile(const NavConfig& c, NavBuildJob& j) {
  auto t0 = std::chrono::steady_clock::now();
  const float T = c.tile;
  float tminx = j.tx * T, tminz = j.tz * T;
  rcConfig rc{};
  rc.cs = c.cs; rc.ch = c.ch; rc.walkableSlopeAngle = c.slope;
  rc.walkableHeight = (int)std::ceil(c.crouchHeight / c.ch);
  rc.walkableClimb = (int)std::floor(c.climb / c.ch + 1e-3f);
  rc.walkableRadius = (int)std::ceil(c.radius / c.cs - 1e-3f);
  rc.maxEdgeLen = (int)(c.edgeMaxLen / c.cs); rc.maxSimplificationError = c.edgeMaxError;
  rc.minRegionArea = (int)(c.regionMin * c.regionMin); rc.mergeRegionArea = (int)(c.regionMerge * c.regionMerge);
  rc.maxVertsPerPoly = 6;
  rc.tileSize = (int)std::lround(T / c.cs);
  rc.borderSize = rc.walkableRadius + 3 + (int)std::ceil(c.linkBorder / c.cs);
  rc.width = rc.tileSize + rc.borderSize * 2; rc.height = rc.width;
  rc.detailSampleDist = c.detailDist < 0.9f ? 0 : c.cs * c.detailDist;
  rc.detailSampleMaxError = c.ch * c.detailErr;
  float pad = rc.borderSize * c.cs;
  rc.bmin[0] = tminx - pad; rc.bmin[2] = tminz - pad; rc.bmax[0] = tminx + T + pad; rc.bmax[2] = tminz + T + pad;
  // geometry
  NavTris tris;
  navTriangles(*j.bodies, vec3(rc.bmin[0], -2000, rc.bmin[2]), vec3(rc.bmax[0], 2000, rc.bmax[2]), tris);
  int nt = (int)tris.kind.size(), nv = (int)tris.v.size() / 3;
  j.tris = nt; for (uint8_t k : tris.kind) j.kinds[k & 3]++;
  if (nt == 0) { j.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); return; }
  float ymin = 1e9f, ymax = -1e9f;
  for (int k = 0; k < nv; ++k) { ymin = std::min(ymin, tris.v[k * 3 + 1]); ymax = std::max(ymax, tris.v[k * 3 + 1]); }
  ymin = std::max(ymin, -500.0f); ymax = std::min(ymax + c.height + 1, ymin + 400.0f);   // 13-bit span heights
  rc.bmin[1] = ymin - 1; rc.bmax[1] = ymax;
  rcContext ctx(false);
  rcHeightfield* hf = rcAllocHeightfield();
  rcCompactHeightfield* chf = nullptr; rcContourSet* cset = nullptr; rcPolyMesh* pm = nullptr; rcPolyMeshDetail* dm = nullptr;
  auto cleanup = [&] { rcFreeHeightField(hf); rcFreeCompactHeightfield(chf); rcFreeContourSet(cset); rcFreePolyMesh(pm); rcFreePolyMeshDetail(dm); };
  if (!rcCreateHeightfield(&ctx, *hf, rc.width, rc.height, rc.bmin, rc.bmax, rc.cs, rc.ch)) { cleanup(); return; }
  std::vector<unsigned char> ta(nt, 0);
  rcMarkWalkableTriangles(&ctx, rc.walkableSlopeAngle, tris.v.data(), nv, tris.t.data(), nt, ta.data());
  const uint8_t wireArea = navarea::fromNav(NavArea::Wire);
  for (int k = 0; k < nt; ++k) {
    if (tris.kind[k] == NT_WIRE) ta[k] = ta[k] ? wireArea : 0;      // wire: solid, its top forbidden
    else if (tris.kind[k] == NT_CARVE) ta[k] = 0;                   // carved obstacle: walked around, never over
    else if (ta[k]) ta[k] = navarea::fromNav(NavArea::Default);
  }
  if (!rcRasterizeTriangles(&ctx, tris.v.data(), nv, tris.t.data(), ta.data(), nt, *hf, rc.walkableClimb)) { cleanup(); return; }
  NavColumns cols; cols.fromHeightfield(*hf);
  rcFilterLowHangingWalkableObstacles(&ctx, rc.walkableClimb, *hf);
  rcFilterLedgeSpans(&ctx, rc.walkableHeight, rc.walkableClimb, *hf);
  rcFilterWalkableLowHeightSpans(&ctx, rc.walkableHeight, *hf);
  chf = rcAllocCompactHeightfield();
  if (!rcBuildCompactHeightfield(&ctx, rc.walkableHeight, rc.walkableClimb, *hf, *chf)) { cleanup(); return; }
  if (!rcErodeWalkableArea(&ctx, rc.walkableRadius, *chf)) { cleanup(); return; }
  // asset areas (marsh, road, trench, minefield, ...), then water, then crouch headroom
  for (auto& m : j.marks.marks) {
    if (m.poly.size() < 3) continue;
    std::vector<float> pv; for (auto& p : m.poly) { pv.push_back(p.x); pv.push_back(0); pv.push_back(p.y); }
    rcMarkConvexPolyArea(&ctx, pv.data(), (int)m.poly.size(), m.hmin, m.hmax, m.nullify ? RC_NULL_AREA : m.area, *chf);
  }
  const int standCells = (int)std::ceil(c.height / c.ch);
  const uint8_t forbidWater = navarea::fromNav(NavArea::Minefield);   // deep water: forbidden like a minefield (never walked)
  for (int z = 0; z < chf->height; ++z) for (int x = 0; x < chf->width; ++x) {
    const rcCompactCell& cell = chf->cells[x + z * chf->width];
    for (int i = (int)cell.index, ni = (int)(cell.index + cell.count); i < ni; ++i) {
      unsigned char& a = chf->areas[i];
      if (a == RC_NULL_AREA) continue;
      float y = chf->bmin[1] + chf->spans[i].y * chf->ch;
      float wl = j.wgrid.empty() ? j.water : j.waterAt(chf->bmin[0] + (x + 0.5f) * chf->cs, chf->bmin[2] + (z + 0.5f) * chf->cs);   // swim.cpp: lakes and ponds too
      if (y < wl) a = (wl - y > c.shallowWater) ? forbidWater : navarea::fromNav(NavArea::Water);
      if (chf->spans[i].h < standCells && a < navarea::kLinkDrop && !(a & navarea::kCrouchBit)) a = (unsigned char)(a | navarea::kCrouchBit);
    }
  }
  {   // lasso ground paint: marsh and mud are slow ground (their NavArea), on plain ground spans only
    extern int groundPaintNavArea(float, float);
    const unsigned char plain = navarea::fromNav(NavArea::Default), grass = navarea::fromNav(NavArea::Grass);
    for (int z = 0; z < chf->height; ++z) for (int x = 0; x < chf->width; ++x) {
      const rcCompactCell& cell = chf->cells[x + z * chf->width];
      if (!cell.count) continue;
      int pa = groundPaintNavArea(chf->bmin[0] + (x + 0.5f) * chf->cs, chf->bmin[2] + (z + 0.5f) * chf->cs);
      if (pa < 0) continue;
      for (int i = (int)cell.index, ni = (int)(cell.index + cell.count); i < ni; ++i) {
        unsigned char& a = chf->areas[i]; unsigned char base = (unsigned char)(a & ~navarea::kCrouchBit);
        if (base == plain || base == grass) a = (unsigned char)(navarea::fromNav((NavArea)pa) | (a & navarea::kCrouchBit));
      }
    }
  }
  if (!rcBuildDistanceField(&ctx, *chf) || !rcBuildRegions(&ctx, *chf, rc.borderSize, rc.minRegionArea, rc.mergeRegionArea)) { cleanup(); return; }
  cset = rcAllocContourSet();
  if (!rcBuildContours(&ctx, *chf, rc.maxSimplificationError, rc.maxEdgeLen, *cset) || cset->nconts == 0) { cleanup(); j.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); return; }
  pm = rcAllocPolyMesh();
  if (!rcBuildPolyMesh(&ctx, *cset, rc.maxVertsPerPoly, *pm)) { cleanup(); return; }
  dm = rcAllocPolyMeshDetail();
  if (!rcBuildPolyMeshDetail(&ctx, *pm, *chf, rc.detailSampleDist, rc.detailSampleMaxError, *dm)) { cleanup(); return; }
  if (pm->npolys == 0) { cleanup(); return; }
  for (int i = 0; i < pm->npolys; ++i) {
    uint8_t a = pm->areas[i];
    NavArea na = navarea::toNav(a);
    pm->flags[i] = navAreaCost(na) < 0 ? NF_FORBID : (navarea::crouch(a) ? (NF_WALK | NF_CROUCH) : NF_WALK);
  }
  navGenerateLinks(c, cols, *pm, tminx, tminz, T, j.idBase, j.links);
  // both ends must be on walkable, unmarked voxels of the final field (links come from the raw columns, before area
  // marks such as hedgerow footprints, forbidden areas and erosion)
  auto onWalk = [&](vec3 p) {
    int cx = (int)std::floor((p.x - chf->bmin[0]) / chf->cs), cz = (int)std::floor((p.z - chf->bmin[2]) / chf->cs);
    for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
      int x = cx + dx, z = cz + dz; if (x < 0 || z < 0 || x >= chf->width || z >= chf->height) continue;
      const rcCompactCell& cell = chf->cells[x + z * chf->width];
      for (int i = (int)cell.index, ni = (int)(cell.index + cell.count); i < ni; ++i) {
        uint8_t a = chf->areas[i]; if (a == RC_NULL_AREA || navAreaCost(navarea::toNav(a)) < 0) continue;
        if (std::fabs(chf->bmin[1] + chf->spans[i].y * chf->ch - p.y) < 0.6f) return true;
      }
    }
    return false; };
  // ...and never pass over a blocked or forbidden mark (hedgerow footprints, wire, minefields)
  auto inTri = [](vec2 p, const std::vector<vec2>& t) {
    auto cr = [](vec2 a, vec2 b, vec2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
    float d1 = cr(t[0], t[1], p), d2 = cr(t[1], t[2], p), d3 = cr(t[2], t[0], p);
    return !((d1 < 0 || d2 < 0 || d3 < 0) && (d1 > 0 || d2 > 0 || d3 > 0)); };
  auto crossesMark = [&](const NavLink& l) {
    for (auto& mk : j.marks.marks) {
      if (mk.poly.size() != 3 || !(mk.nullify || navAreaCost(navarea::toNav(mk.area)) < 0)) continue;
      for (int k = 0; k <= 20; ++k) { vec3 q = glm::mix(l.a, l.b, k / 20.0f); if (q.y >= mk.hmin && q.y <= mk.hmax && inTri(vec2(q.x, q.z), mk.poly)) return true; }
    }
    return false; };
  j.links.erase(std::remove_if(j.links.begin(), j.links.end(), [&](const NavLink& l) { return !onWalk(l.a) || !onWalk(l.b) || crossesMark(l); }), j.links.end());
  std::vector<float> lv; std::vector<float> lr; std::vector<unsigned char> ld, la; std::vector<unsigned short> lf; std::vector<unsigned int> lid;
  for (auto& l : j.links) {
    lv.insert(lv.end(), {l.a.x, l.a.y, l.a.z, l.b.x, l.b.y, l.b.z});
    lr.push_back(c.radius + 0.25f); ld.push_back(l.bidir ? DT_OFFMESH_CON_BIDIR : 0);
    la.push_back((unsigned char)(navarea::kLinkDrop + l.kind)); lf.push_back(NF_LINK); lid.push_back(l.id);
  }
  dtNavMeshCreateParams p{};
  p.verts = pm->verts; p.vertCount = pm->nverts; p.polys = pm->polys; p.polyAreas = pm->areas; p.polyFlags = pm->flags; p.polyCount = pm->npolys; p.nvp = pm->nvp;
  p.detailMeshes = dm->meshes; p.detailVerts = dm->verts; p.detailVertsCount = dm->nverts; p.detailTris = dm->tris; p.detailTriCount = dm->ntris;
  p.offMeshConVerts = lv.data(); p.offMeshConRad = lr.data(); p.offMeshConDir = ld.data(); p.offMeshConAreas = la.data(); p.offMeshConFlags = lf.data(); p.offMeshConUserID = lid.data();
  p.offMeshConCount = (int)j.links.size();
  p.walkableHeight = c.crouchHeight; p.walkableRadius = c.radius; p.walkableClimb = c.climb;
  p.tileX = j.tx; p.tileY = j.tz; p.tileLayer = 0;
  rcVcopy(p.bmin, pm->bmin); rcVcopy(p.bmax, pm->bmax);
  p.cs = rc.cs; p.ch = rc.ch; p.buildBvTree = true;
  if (!dtCreateNavMeshData(&p, &j.data, &j.size)) { j.data = nullptr; j.size = 0; }
  j.polys = pm->npolys;
  cleanup();
  j.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void NavColumns::fromHeightfield(const rcHeightfield& hf) {
  w = hf.width; h = hf.height; bx = hf.bmin[0]; by = hf.bmin[1]; bz = hf.bmin[2]; cs = hf.cs; ch = hf.ch;
  start.assign((size_t)w * h + 1, 0); floors.clear(); floors.reserve((size_t)w * h * 2);
  for (int i = 0; i < w * h; ++i) {
    start[i] = (int)floors.size();
    for (const rcSpan* s = hf.spans[i]; s; s = s->next) {
      float top = by + s->smax * ch;
      float nxt = s->next ? by + s->next->smin * ch : 1e9f;
      floors.push_back({top, nxt - top, s->area != RC_NULL_AREA && s->area != navarea::fromNav(NavArea::Wire), s->area == navarea::fromNav(NavArea::Wire)});
    }
  }
  start[(size_t)w * h] = (int)floors.size();
}

// ---------------------------------------------------------------- Nav
Nav::Nav() : impl_(std::make_unique<NavImpl>()) {}
Nav::~Nav() {
  { std::lock_guard<std::mutex> l(impl_->qm); impl_->quit = true; impl_->queue.clear(); }
  impl_->cv.notify_all();
  for (auto& t : impl_->threads) t.join();
  if (query_) dtFreeNavMeshQuery(query_);
  if (mesh_) dtFreeNavMesh(mesh_);
}

const dtQueryFilter& Nav::defaultFilter() const { return impl_->filter; }

bool Nav::busy() const { std::lock_guard<std::mutex> l(impl_->qm); return !impl_->queue.empty() || impl_->running.load() > 0 || !impl_->done.empty(); }

void Nav::clear() {
  {
    std::unique_lock<std::mutex> l(impl_->qm);
    impl_->queue.clear();
    impl_->cv.wait(l, [&] { return impl_->running.load() == 0; });   // a worker may still finish one tile
    impl_->done.clear();
  }
  std::unique_lock<std::shared_mutex> wl(mtx_);
  if (query_) { dtFreeNavMeshQuery(query_); query_ = nullptr; }
  if (mesh_) { dtFreeNavMesh(mesh_); mesh_ = nullptr; }
  impl_->genIssued.clear(); impl_->genCommitted.clear(); impl_->stamps.clear(); impl_->tileLinks.clear(); impl_->linkById.clear();
  usedNodes = links = tilesBuilt = rebuilds = queued = 0; for (int& k : linkCount) k = 0;
  buildMs = lastRebuildMs = rebuildMsMax = 0; changed.clear();
}

void Nav::initMesh() {
  mesh_ = dtAllocNavMesh();
  dtNavMeshParams p{};
  p.orig[0] = 0; p.orig[1] = 0; p.orig[2] = 0;
  p.tileWidth = cfg.tile; p.tileHeight = cfg.tile;
  int n = (impl_->tx1 - impl_->tx0 + 1) * (impl_->tz1 - impl_->tz0 + 1);
  int pw = 1; while (pw < n + 4) pw <<= 1;
  p.maxTiles = pw; p.maxPolys = 1 << 16;
  mesh_->init(&p);
  query_ = dtAllocNavMeshQuery();
  query_->init(mesh_, 4096);
  dtQueryFilter& f = impl_->filter;
  f.setIncludeFlags(NF_WALK | NF_CROUCH | NF_LINK); f.setExcludeFlags(NF_FORBID | NF_TRAPLINK | NF_BADLINK);
  for (int a = 0; a < DT_MAX_AREAS; ++a) f.setAreaCost(a, 1.0f);
  for (int k = 0; k < (int)NavArea::Count; ++k) {
    float cst = std::max(0.1f, navAreaCost((NavArea)k));
    f.setAreaCost(navarea::fromNav((NavArea)k), cst);
    f.setAreaCost(navarea::fromNav((NavArea)k, true), cst * 1.6f);
  }
  for (int k = 0; k < LINK_KINDS; ++k) f.setAreaCost(navarea::kLinkDrop + k, 1.0f + cfg.linkCost[k]);
}

static void workerLoop(NavImpl* I) {
  for (;;) {
    std::unique_ptr<NavBuildJob> j;
    {
      std::unique_lock<std::mutex> l(I->qm);
      I->cv.wait(l, [&] { return I->quit || !I->queue.empty(); });
      if (I->quit) return;
      j = std::move(I->queue.front()); I->queue.pop_front();
      I->running++;
    }
    buildTile(I->cfg, *j);
    {
      std::lock_guard<std::mutex> l(I->qm);
      I->done.push_back(std::move(j));
      I->running--;
    }
    I->cv.notify_all();
  }
}

static uint32_t linkIdBase(const NavImpl& I, int tx, int tz) {
  int w = I.tx1 - I.tx0 + 1;
  uint32_t serial = (uint32_t)((tz - I.tz0) * w + (tx - I.tx0)) & 0xFFFFFu;
  return (serial << 12) + 1;   // 0 is "no link"
}

// local water over a tile and its border, 1 m samples taken here (the main thread), so the workers never touch the map
static void sampleWater(const Nav& nav, NavBuildJob& j, float pad) {
  if (!nav.waterQuery) return;
  const float T = nav.cfg.tile, P = pad + 3.0f, st = 1.0f;
  j.wx0 = j.tx * T - P; j.wz0 = j.tz * T - P; j.wstep = st; j.wn = (int)std::ceil((T + 2 * P) / st) + 1;
  j.wgrid.assign((size_t)j.wn * j.wn, -1e9f);
  bool any = false;
  for (int k = 0; k < j.wn; ++k) for (int i = 0; i < j.wn; ++i) {
    float w = nav.waterQuery(j.wx0 + i * st, j.wz0 + k * st);
    j.wgrid[(size_t)k * j.wn + i] = w; any = any || (w > -999.0f && w > j.water);
  }
  if (!any) j.wgrid.clear();
}

void Nav::build(vec3 blo, vec3 bhi, int threads) {
  clear();
  auto t0 = std::chrono::steady_clock::now();
  impl_->cfg = cfg;
  lo = blo; hi = bhi;
  impl_->tx0 = tileOf(blo.x); impl_->tx1 = tileOf(bhi.x); impl_->tz0 = tileOf(blo.z); impl_->tz1 = tileOf(bhi.z);
  initMesh();
  std::vector<std::unique_ptr<NavBuildJob>> jobs;
  float pad = cfg.linkBorder + cfg.radius + 1.0f;
  for (int tz = impl_->tz0; tz <= impl_->tz1; ++tz) for (int tx = impl_->tx0; tx <= impl_->tx1; ++tx) {
    auto j = std::make_unique<NavBuildJob>();
    j->tx = tx; j->tz = tz; j->gen = ++impl_->genIssued[{tx, tz}]; j->idBase = linkIdBase(*impl_, tx, tz); j->water = waterLevel;
    sampleWater(*this, *j, pad);
    vec3 a(tx * cfg.tile - pad, -2000, tz * cfg.tile - pad), b((tx + 1) * cfg.tile + pad, 2000, (tz + 1) * cfg.tile + pad);
    j->bodies = navCollectBodies(a, b, classify);
    if (areaMarks) areaMarks(tx, tz, j->marks);
    jobs.push_back(std::move(j));
  }
  if (threads <= 0) threads = std::max(1, (int)std::thread::hardware_concurrency() - 1);
  std::atomic<size_t> next{0};
  std::vector<std::thread> T;
  for (int k = 0; k < threads; ++k) T.emplace_back([&] { for (size_t i; (i = next++) < jobs.size();) buildTile(cfg, *jobs[i]); });
  for (auto& t : T) t.join();
  int bodies = 0, tri = 0, kinds[4] = {0, 0, 0, 0}, polys = 0;
  for (auto& j : jobs) { bodies += navBodyCount(*j->bodies); tri += j->tris; for (int k = 0; k < 4; ++k) kinds[k] += j->kinds[k]; polys += j->polys; }
  logf("nav: input %d body refs, %d triangles (solid %d, wire %d, skip %d, carve %d), %d polygons", bodies, tri, kinds[0], kinds[1], kinds[2], kinds[3], polys);
  for (auto& j : jobs) commit(*j);
  changed.clear();
  buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  // background rebuild workers (started once, kept for the session)
  if (impl_->threads.empty()) for (int k = 0; k < std::max(1, cfg.workers); ++k) impl_->threads.emplace_back(workerLoop, impl_.get());
  logf("nav: %d tiles (%.0f m), %d polygons, %d links (drop %d, mantle %d, vault %d, jump %d), built in %.0f ms on %d threads",
       tilesBuilt, cfg.tile, usedNodes, links, linkCount[0], linkCount[1], linkCount[2], linkCount[3], buildMs, threads);
}

void Nav::rebuildArea(vec3 a, vec3 b) {
  if (!mesh_) return;
  int x0 = std::max(impl_->tx0, tileOf(a.x) - 1), x1 = std::min(impl_->tx1, tileOf(b.x) + 1);
  int z0 = std::max(impl_->tz0, tileOf(a.z) - 1), z1 = std::min(impl_->tz1, tileOf(b.z) + 1);
  float pad = cfg.linkBorder + cfg.radius + 1.0f;
  for (int tz = z0; tz <= z1; ++tz) for (int tx = x0; tx <= x1; ++tx) {
    auto j = std::make_unique<NavBuildJob>();
    j->tx = tx; j->tz = tz; j->gen = ++impl_->genIssued[{tx, tz}]; j->idBase = linkIdBase(*impl_, tx, tz); j->water = waterLevel;
    sampleWater(*this, *j, pad);
    vec3 lo2(tx * cfg.tile - pad, -2000, tz * cfg.tile - pad), hi2((tx + 1) * cfg.tile + pad, 2000, (tz + 1) * cfg.tile + pad);
    j->bodies = navCollectBodies(lo2, hi2, classify);
    if (areaMarks) areaMarks(tx, tz, j->marks);
    std::lock_guard<std::mutex> l(impl_->qm);
    // a queued (not started) job for the same tile is superseded by this newer snapshot
    impl_->queue.erase(std::remove_if(impl_->queue.begin(), impl_->queue.end(), [&](const std::unique_ptr<NavBuildJob>& q) { return q->tx == tx && q->tz == tz; }), impl_->queue.end());
    impl_->queue.push_back(std::move(j));
    ++queued;
  }
  impl_->cv.notify_all();
}

void Nav::update() {
  changed.clear();
  std::vector<std::unique_ptr<NavBuildJob>> fin;
  { std::lock_guard<std::mutex> l(impl_->qm); fin.swap(impl_->done); }
  for (auto& j : fin) {
    if (!mesh_) break;
    if (j->gen <= impl_->genCommitted[{j->tx, j->tz}]) continue;   // an even newer build already landed
    commit(*j);
    ++rebuilds; lastRebuildMs = j->ms; rebuildMsMax = std::max(rebuildMsMax, j->ms);
  }
}

void Nav::commit(NavBuildJob& j) {
  NavImpl& I = *impl_;
  auto key = std::make_pair(j.tx, j.tz);
  {
    std::unique_lock<std::shared_mutex> wl(mtx_);   // path workers pause between slices
    dtTileRef old = mesh_->getTileRefAt(j.tx, j.tz, 0);
    if (old) {
      const dtMeshTile* t = mesh_->getTileByRef(old);
      if (t && t->header) { usedNodes -= t->header->polyCount - t->header->offMeshConCount; --tilesBuilt; }
      mesh_->removeTile(old, nullptr, nullptr);
    }
    if (j.data) {
      dtTileRef ref = 0;
      if (dtStatusSucceed(mesh_->addTile(j.data, j.size, DT_TILE_FREE_DATA, 0, &ref))) {
        j.data = nullptr;   // owned by the mesh now
        const dtMeshTile* t = mesh_->getTileByRef(ref);
        if (t && t->header) { usedNodes += t->header->polyCount - t->header->offMeshConCount; ++tilesBuilt; }
      }
    }
  }
  for (auto& l : I.tileLinks[key]) { --links; --linkCount[l.kind]; I.linkById.erase(l.id); }
  I.tileLinks[key] = j.links;
  for (auto& l : j.links) { ++links; ++linkCount[l.kind]; I.linkById[l.id] = l; }
  I.genCommitted[key] = j.gen;
  ++I.stamps[key]; ++stamp_;
  changed.push_back(key);
}

uint32_t Nav::tileStamp(int tx, int tz) const { auto it = impl_->stamps.find({tx, tz}); return it == impl_->stamps.end() ? 0 : it->second; }

void Nav::forEachTile(const std::function<void(int, int)>& fn) const {
  for (auto& [k, s] : impl_->stamps) fn(k.first, k.second);
}

std::vector<NavLink> Nav::linksNear(vec3 p, float r) const {
  std::vector<NavLink> out;
  for (int tz = tileOf(p.z - r); tz <= tileOf(p.z + r); ++tz) for (int tx = tileOf(p.x - r); tx <= tileOf(p.x + r); ++tx) {
    auto it = impl_->tileLinks.find({tx, tz});
    if (it == impl_->tileLinks.end()) continue;
    for (auto& l : it->second) if (glm::length(l.a - p) < r || (l.bidir && glm::length(l.b - p) < r)) out.push_back(l);
  }
  return out;
}

const NavLink* Nav::linkById(uint32_t id) const { auto it = impl_->linkById.find(id); return it == impl_->linkById.end() ? nullptr : &it->second; }

// ---------------------------------------------------------------- queries
bool Nav::closest(vec3 p, vec3 ext, NavRef& ref, vec3& on, const dtQueryFilter* f) const {
  ref = 0;
  if (!query_) return false;
  float c[3] = {p.x, p.y, p.z}, e[3] = {ext.x, ext.y, ext.z}, r[3];
  dtPolyRef pr = 0;
  if (dtStatusFailed(query_->findNearestPoly(c, e, f ? f : &impl_->filter, &pr, r)) || !pr) return false;
  ref = pr; on = vec3(r[0], r[1], r[2]);
  return true;
}

int Nav::nearest(vec3 p, float maxDist) const {
  NavRef r; vec3 on;
  if (!closest(p + vec3(0, 0.3f, 0), vec3(maxDist, std::max(1.5f, maxDist), maxDist), r, on)) return -1;
  return glm::length(vec2(on.x - p.x, on.z - p.z)) <= maxDist + 0.25f ? 0 : -1;
}

uint8_t Nav::areaAt(vec3 p, float r) const {
  NavRef ref; vec3 on;
  dtQueryFilter all; all.setIncludeFlags(0xffff); all.setExcludeFlags(0);
  if (!closest(p + vec3(0, 0.3f, 0), vec3(r, 1.5f, r), ref, on, &all)) return 0;
  unsigned char a = 0; mesh_->getPolyArea(ref, &a);
  return a;
}

bool Nav::pathSync(vec3 a, vec3 b, std::vector<vec3>& pts, std::vector<NavRef>* polysOut, const dtQueryFilter* f, float* lenOut) const {
  pts.clear(); if (polysOut) polysOut->clear();
  if (!query_) return false;
  const dtQueryFilter* fl = f ? f : &impl_->filter;
  NavRef ra, rb; vec3 pa, pb;
  if (!closest(a + vec3(0, 0.3f, 0), vec3(2, 2.5f, 2), ra, pa, fl) || !closest(b + vec3(0, 0.3f, 0), vec3(3, 3, 3), rb, pb, fl)) return false;
  static thread_local std::vector<dtPolyRef> polys(4096);
  int n = 0;
  float sa[3] = {pa.x, pa.y, pa.z}, sb[3] = {pb.x, pb.y, pb.z};
  dtStatus st = query_->findPath(ra, rb, sa, sb, fl, polys.data(), &n, (int)polys.size());
  if (dtStatusFailed(st) || n == 0) return false;
  if (polys[n - 1] != rb) return false;   // partial: goal not reached
  std::vector<float> sp(4096 * 3); std::vector<unsigned char> sf(4096); std::vector<dtPolyRef> sr(4096); int ns = 0;
  query_->findStraightPath(sa, sb, polys.data(), n, sp.data(), sf.data(), sr.data(), &ns, 4096, DT_STRAIGHTPATH_ALL_CROSSINGS);
  float L = 0;
  for (int k = 0; k < ns; ++k) { pts.push_back(vec3(sp[k * 3], sp[k * 3 + 1], sp[k * 3 + 2])); if (k) L += glm::length(pts[k] - pts[k - 1]); }
  if (polysOut) polysOut->assign(polys.begin(), polys.begin() + n);
  if (lenOut) *lenOut = L;
  return !pts.empty();
}

bool Nav::raycastMesh(vec3 a, vec3 b, float& t) const {
  t = 0;
  NavRef ra; vec3 pa;
  if (!closest(a + vec3(0, 0.3f, 0), vec3(1, 2, 1), ra, pa)) return false;
  float s[3] = {pa.x, pa.y, pa.z}, e[3] = {b.x, b.y, b.z}, hn[3]; float tt = 0;
  dtPolyRef path[64]; int np = 0;
  if (dtStatusFailed(query_->raycast(ra, s, e, &impl_->filter, &tt, hn, path, &np, 64))) return false;
  t = tt > 1.0f ? 1.0f : tt;
  return true;
}

std::vector<NavEdge> Nav::tileEdges(int tx, int tz) const {
  std::vector<NavEdge> out;
  if (!mesh_) return out;
  const dtMeshTile* t = mesh_->getTileAt(tx, tz, 0);
  if (!t || !t->header) return out;
  for (int i = 0; i < t->header->polyCount; ++i) {
    const dtPoly& p = t->polys[i];
    if (p.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;
    if (p.flags & NF_FORBID) continue;
    vec3 c(0); for (int k = 0; k < p.vertCount; ++k) { const float* v = &t->verts[p.verts[k] * 3]; c += vec3(v[0], v[1], v[2]); }
    c /= (float)p.vertCount;
    for (int k = 0; k < p.vertCount; ++k) {
      if (p.neis[k] != 0) continue;                       // shared with a polygon or a tile portal
      const float* va = &t->verts[p.verts[k] * 3]; const float* vb = &t->verts[p.verts[(k + 1) % p.vertCount] * 3];
      NavEdge e; e.a = vec3(va[0], va[1], va[2]); e.b = vec3(vb[0], vb[1], vb[2]); e.area = p.getArea();
      vec3 d = e.b - e.a; d.y = 0; if (glm::length(d) < 0.05f) continue;
      vec3 n = glm::normalize(vec3(d.z, 0, -d.x));
      if (glm::dot(n, (e.a + e.b) * 0.5f - c) < 0) n = -n;
      e.n = n; out.push_back(e);
    }
  }
  return out;
}

int Nav::markTraps(const std::vector<vec3>& anchors) {
  if (!mesh_) return 0;
  std::unique_lock<std::shared_mutex> wl(mtx_);
  dtNavMesh* M = mesh_;
  // reverse adjacency over every link (polygon neighbours, tile portals, off-mesh connections)
  std::unordered_map<dtPolyRef, std::vector<dtPolyRef>> rev;
  std::vector<dtPolyRef> all;
  for (int i = 0; i < M->getMaxTiles(); ++i) {
    const dtMeshTile* t = static_cast<const dtNavMesh*>(M)->getTile(i);
    if (!t || !t->header) continue;
    dtPolyRef base = M->getPolyRefBase(t);
    for (int j = 0; j < t->header->polyCount; ++j) {
      const dtPoly& p = t->polys[j]; dtPolyRef ref = base | (dtPolyRef)j;
      unsigned short fl = p.flags & ~(NF_TRAP | NF_TRAPLINK);
      M->setPolyFlags(ref, fl);
      all.push_back(ref);
      if (fl & NF_FORBID) continue;
      for (unsigned int k = p.firstLink; k != DT_NULL_LINK; k = t->links[k].next) if (t->links[k].ref) rev[t->links[k].ref].push_back(ref);
    }
  }
  // flood backwards from the anchors: every polygon that can reach one
  std::unordered_map<dtPolyRef, char> ok; std::vector<dtPolyRef> q;
  dtQueryFilter f; f.setIncludeFlags(0xffff); f.setExcludeFlags(NF_FORBID);
  for (auto& a : anchors) {
    dtPolyRef r = 0; float c[3] = {a.x, a.y + 0.3f, a.z}, e[3] = {4, 6, 4}, n[3];
    if (dtStatusSucceed(query_->findNearestPoly(c, e, &f, &r, n)) && r && !ok.count(r)) { ok[r] = 1; q.push_back(r); }
  }
  if (q.empty()) return 0;
  while (!q.empty()) {
    dtPolyRef r = q.back(); q.pop_back();
    auto it = rev.find(r); if (it == rev.end()) continue;
    for (dtPolyRef s : it->second) if (!ok.count(s)) { ok[s] = 1; q.push_back(s); }
  }
  int traps = 0;
  for (dtPolyRef ref : all) {
    if (ok.count(ref)) continue;
    const dtMeshTile* t = nullptr; const dtPoly* p = nullptr;
    if (dtStatusFailed(M->getTileAndPolyByRef(ref, &t, &p)) || (p->flags & NF_FORBID)) continue;
    if (p->getType() == DT_POLYTYPE_OFFMESH_CONNECTION) M->setPolyFlags(ref, p->flags | NF_TRAPLINK);   // leads only into a pocket
    else { M->setPolyFlags(ref, p->flags | NF_TRAP); ++traps; }
  }
  return traps;
}

bool Nav::isTrap(vec3 p, float r) const {
  NavRef ref; vec3 on;
  dtQueryFilter all; all.setIncludeFlags(0xffff); all.setExcludeFlags(NF_FORBID);
  if (!closest(p + vec3(0, 0.3f, 0), vec3(r, 1.5f, r), ref, on, &all)) return false;
  unsigned short fl = 0; mesh_->getPolyFlags(ref, &fl);
  return (fl & NF_TRAP) != 0;
}
