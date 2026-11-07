// Lasso tool (docs/lasso.md): region capture, group edits of placements, elevation with a falloff ring, water fill.
// Ground paint lives in ground_paint.cpp, move land in lasso_land.cpp.
#include "lasso.h"
#include "geom2d.h"
#include "snap.h"
#include "tank.h"
#include "terrain_tools.h"
#include <cfloat>

// ================================================================ region
const char* lassoModeName(int m) {
  static const char* n[] = {"Objects", "Elevation", "Paint", "Move land"};
  return m >= 0 && m < LM_COUNT ? n[m] : "?";
}
LassoParams& lassoParams() { static LassoParams p; return p; }
LassoRegion& lassoRegion() { static LassoRegion r; return r; }

bool LassoRegion::empty() const { for (auto& s : shapes) if (!s.subtract) return false; return true; }
bool LassoRegion::contains(vec2 p) const {
  bool in = false;   // inside any add shape and no subtract shape (the same set signedDist describes)
  for (auto& s : shapes) if (geom2d::inside(s.poly, p)) { if (s.subtract) return false; in = true; }
  return in;
}
float LassoRegion::signedDist(vec2 p) const {
  float d = -FLT_MAX;
  for (auto& s : shapes) {
    float sd = geom2d::signedDist(s.poly, p);
    d = s.subtract ? std::min(d, -sd) : std::max(d, sd);
  }
  return d == -FLT_MAX ? -1e30f : d;
}
bool LassoRegion::bounds(vec2& lo, vec2& hi) const {
  lo = vec2(FLT_MAX); hi = vec2(-FLT_MAX); bool any = false;
  for (auto& s : shapes) if (!s.subtract) { vec2 a, b; geom2d::bounds(s.poly, a, b); lo = glm::min(lo, a); hi = glm::max(hi, b); any = true; }
  return any;
}
void LassoRegion::add(std::vector<vec2> poly, bool subtract) {
  if (poly.size() < 3 || std::fabs(geom2d::signedArea(poly)) < 0.05f) return;
  shapes.push_back({std::move(poly), subtract});
}
LassoRegion LassoRegion::moved(vec2 delta, float yawDeg, vec2 pivot) const {
  LassoRegion r = *this;
  quat q = yawQuat(yawDeg);
  for (auto& s : r.shapes) for (auto& v : s.poly) { vec3 o = q * vec3(v.x - pivot.x, 0, v.y - pivot.y); v = pivot + vec2(o.x, o.z) + delta; }
  return r;
}

// ================================================================ objects
static bool furnishChild(const Instance& i) { return i.extra.is_object() && i.extra.contains("furnish"); }
static bool inSel(const App& app, uint64_t id) { return std::find(app.selection.begin(), app.selection.end(), id) != app.selection.end(); }

bool lassoInstanceIn(const App& app, const Instance& i, const LassoRegion& r, bool touching) {
  if (furnishChild(i)) return false;
  if (r.contains(vec2(i.pos.x, i.pos.z))) return true;
  if (!touching) return false;
  const AssetDef* a = app.assetFor(i);
  if (!a) return false;
  vec2 hf = a->footprint * 0.5f; quat q = yawQuat(i.yaw);
  for (vec3 l : {vec3(-hf.x, 0, -hf.y), vec3(hf.x, 0, -hf.y), vec3(hf.x, 0, hf.y), vec3(-hf.x, 0, hf.y)}) {
    vec3 w = i.pos + q * l;
    if (r.contains(vec2(w.x, w.z))) return true;
  }
  return false;
}

int lassoSelectObjects(App& app, const LassoRegion& r, int op, bool touching) {
  if (op == 0) app.selection.clear();
  for (auto& i : app.doc.instances) {
    if (!lassoInstanceIn(app, i, r, touching)) continue;
    auto it = std::find(app.selection.begin(), app.selection.end(), i.id);
    if (op == 2) { if (it != app.selection.end()) app.selection.erase(it); }
    else if (it == app.selection.end()) app.selection.push_back(i.id);
  }
  app.selected = app.selection.empty() ? 0 : app.selection.front();
  return (int)app.selection.size();
}

void lassoSanitize(App& app) {
  if (app.selection.empty()) return;
  if (app.selected && !inSel(app, app.selected)) { app.selection.clear(); return; }   // a click elsewhere picked one piece
  app.selection.erase(std::remove_if(app.selection.begin(), app.selection.end(), [&](uint64_t id) { return !app.doc.find(id); }), app.selection.end());
  if (app.selection.empty()) app.selected = 0;
  else if (!app.selected || !inSel(app, app.selected)) app.selected = app.selection.front();
}

vec3 lassoGroupCentre(const App& app) {
  vec3 s(0); int n = 0;
  for (uint64_t id : app.selection) if (const Instance* i = const_cast<App&>(app).doc.find(id)) { s += i->pos; ++n; }
  return n ? s / (float)n : vec3(0);
}

namespace {
struct GroupState {
  bool active = false, ok = true;
  std::vector<Instance> orig;
  vec3 centre{0};
  std::string reason;
  size_t undoMark = 0;
} gG;
}
bool lassoGroupActive() { return gG.active; }
const std::string& lassoGroupReason() { return gG.reason; }

bool lassoGroupBegin(App& app) {
  lassoSanitize(app);
  if (app.selection.empty() || gG.active) return false;
  app.pushUndo();
  gG = GroupState{};
  gG.active = true; gG.undoMark = app.undoStack.size();
  for (uint64_t id : app.selection) if (Instance* i = app.doc.find(id)) gG.orig.push_back(*i);
  gG.centre = lassoGroupCentre(app);
  return true;
}

bool lassoGroupSet(App& app, vec2 delta, float yawDeg) {
  if (!gG.active) return false;
  quat q = yawQuat(yawDeg);
  // move every member first (props then check against their moved building), then seat and validate each
  for (auto& o : gG.orig) {
    Instance* i = app.doc.find(o.id); if (!i) continue;
    vec3 off = q * (o.pos - gG.centre);
    i->pos = vec3(gG.centre.x + off.x + delta.x, o.pos.y, gG.centre.z + off.z + delta.y);
    i->yaw = wrapDeg(o.yaw + yawDeg);
  }
  gG.ok = true; gG.reason.clear();
  app.placeIgnore = &app.selection;
  for (auto& o : gG.orig) {
    Instance* i = app.doc.find(o.id); if (!i) continue;
    const AssetDef* a = app.assetFor(*i);
    PlaceCheck c = app.checkPlacement(a, i->pos, i->yaw, i->id);
    if (c.ok) { i->pos.y = c.y; if (c.snapXZ) { i->pos.x = c.xz.x; i->pos.z = c.xz.y; } }
    else if (gG.ok) { gG.ok = false; gG.reason = (a ? a->name : i->assetId) + ": " + c.reason; }
    app.bump(*i);
  }
  app.placeIgnore = nullptr;
  app.lastCheck.ok = gG.ok; app.lastCheck.reason = gG.reason;
  return gG.ok;
}

bool lassoGroupEnd(App& app, bool commit) {
  if (!gG.active) return false;
  gG.active = false;
  bool moved = false;
  for (auto& o : gG.orig) if (Instance* i = app.doc.find(o.id)) if (i->pos != o.pos || i->yaw != o.yaw) moved = true;
  if (!commit || !gG.ok || !moved) {
    for (auto& o : gG.orig) if (Instance* i = app.doc.find(o.id)) { *i = o; app.bump(*i); }
    if (app.undoStack.size() == gG.undoMark && !app.undoStack.empty()) app.undoStack.pop_back();
    if (commit && !gG.ok) app.toast("Group move rejected: " + gG.reason);
    app.lastCheck.ok = true;
    return false;
  }
  std::vector<uint64_t> ids; for (auto& o : gG.orig) ids.push_back(o.id);
  for (uint64_t id : ids) if (app.doc.find(id)) crushAndFlatten(app, id);   // buildings crush growth at the new spot (same step)
  lassoSanitize(app);
  app.dirty = true;
  return true;
}

bool lassoGroupMove(App& app, vec2 delta, float yawDeg) {
  if (!lassoGroupBegin(app)) return false;
  lassoGroupSet(app, delta, yawDeg);
  return lassoGroupEnd(app, true);
}

int lassoDeleteGroup(App& app) {
  lassoSanitize(app);
  if (app.selection.empty()) return 0;
  app.pushUndo();
  std::vector<uint64_t> ids = app.selection;
  size_t n0 = app.doc.instances.size();
  app.doc.instances.erase(std::remove_if(app.doc.instances.begin(), app.doc.instances.end(), [&](const Instance& i) { return std::find(ids.begin(), ids.end(), i.id) != ids.end(); }), app.doc.instances.end());
  app.selection.clear(); app.selected = 0; app.dirty = true;
  int n = (int)(n0 - app.doc.instances.size());
  app.toast("Deleted " + std::to_string(n) + " objects (Ctrl+Z to undo).", 3);
  return n;
}

int lassoDuplicateGroup(App& app) {
  lassoSanitize(app);
  if (app.selection.empty()) return 0;
  std::vector<Instance> src;
  for (uint64_t id : app.selection) if (Instance* i = app.doc.find(id)) src.push_back(*i);
  vec2 lo(FLT_MAX), hi(-FLT_MAX);
  for (auto& i : src) {
    const AssetDef* a = app.assetFor(i); float r = a ? glm::length(a->footprint) * 0.5f : 1.0f;
    lo = glm::min(lo, vec2(i.pos.x, i.pos.z) - vec2(r)); hi = glm::max(hi, vec2(i.pos.x, i.pos.z) + vec2(r));
  }
  vec2 size = hi - lo;
  std::vector<vec2> tries;
  for (float s : {1.0f, 1.5f, 2.0f})
    for (vec2 d : {vec2(1, 0), vec2(-1, 0), vec2(0, 1), vec2(0, -1)}) tries.push_back(d * ((d.x != 0 ? size.x : size.y) * s + 2.0f));
  for (vec2 off : tries) {
    std::vector<float> ys; bool ok = true;
    for (auto& i : src) {
      PlaceCheck c = app.checkPlacement(app.assetFor(i), i.pos + vec3(off.x, 0, off.y), i.yaw, 0);
      if (!c.ok) { ok = false; break; }
      ys.push_back(c.y);
    }
    if (!ok) continue;
    app.pushUndo();
    std::vector<uint64_t> fresh;
    for (size_t k = 0; k < src.size(); ++k) {
      Instance n = src[k];
      n.id = app.doc.newId(); n.pos = vec3(n.pos.x + off.x, ys[k], n.pos.z + off.y);
      if (isVehicleAsset(app.assetFor(n))) TankSys::stripDamage(n.extra);   // a duplicated tank is a fresh tank
      app.bump(n);
      app.doc.instances.push_back(n);
      fresh.push_back(n.id);
    }
    app.selection = fresh; app.selected = fresh.front();
    return (int)fresh.size();
  }
  app.toast("No free space next to the group for a copy.");
  return 0;
}

// ================================================================ elevation
static float sstep(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3 - 2 * t); }
float lassoElevWeight(const ElevParams& p, float sd, float maxInside) {
  float F = std::max(0.0f, p.falloff);
  float t;
  if (p.profile == 1) t = (sd + F) / std::max(1e-3f, F + std::max(0.0f, maxInside));   // dome: rises all the way to the middle
  else t = F > 1e-3f ? (sd + F) / F : (sd >= 0 ? 1.0f : 0.0f);                        // plateau: full height inside
  t = std::clamp(t, 0.0f, 1.0f);
  return p.linearEdge ? t : sstep(t);
}

namespace {
constexpr float kSculptMin = -60.0f, kSculptMax = 80.0f;   // as the brush (terrain_brush.cpp)
struct ElevSession {
  bool active = false;
  LassoRegion region;
  std::map<int, std::vector<float>> orig, sd;   // per tile: the sculpt before the edit, the signed distance per sample
  float margin = 0, maxInside = 0;
  std::vector<SeatRec> seats; std::map<uint64_t, float> seatY;
  bool startSet = false; float startY = 0;
  size_t undoMark = 0;
  ElevParams last;
} gE;

void elevBuildSd(App& app, float margin) {
  Terrain& T = app.terrain;
  vec2 lo, hi; if (!gE.region.bounds(lo, hi)) return;
  lo -= vec2(margin); hi += vec2(margin);
  const float fs = T.fineSpacing(), ts = T.tileSize(); const int F = Terrain::kFine, N = Terrain::kFineN;
  int tx0 = std::max(0, (int)std::floor((lo.x - T.org.x) / ts)), tx1 = std::min(T.tilesX() - 1, (int)std::floor((hi.x - T.org.x) / ts));
  int tz0 = std::max(0, (int)std::floor((lo.y - T.org.y) / ts)), tz1 = std::min(T.tilesZ() - 1, (int)std::floor((hi.y - T.org.y) / ts));
  gE.sd.clear(); gE.maxInside = 0;
  for (int tz = tz0; tz <= tz1; ++tz)
    for (int tx = tx0; tx <= tx1; ++tx) {
      int key = T.key(tx, tz);
      std::vector<float> d((size_t)N * N);
      bool near = false;
      for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
          float x = T.org.x + (float)(tx * F + i) * fs, z = T.org.y + (float)(tz * F + j) * fs;
          float s = gE.region.signedDist(vec2(x, z));
          d[(size_t)j * N + i] = s;
          if (s > -margin) near = true;
          gE.maxInside = std::max(gE.maxInside, s);
        }
      if (!near) continue;
      gE.sd[key] = std::move(d);
      if (!gE.orig.count(key)) {   // first time this tile joins the edit: it still holds its pre-edit sculpt
        auto it = T.sculpt.find(key);
        gE.orig[key] = it != T.sculpt.end() ? it->second : std::vector<float>((size_t)N * N, 0.0f);
      }
    }
  gE.margin = margin;
}

void elevWrite(App& app, const ElevParams* p) {   // p == nullptr: restore the originals
  Terrain& T = app.terrain;
  const int N = Terrain::kFineN;
  std::vector<int> keys;
  for (auto& [key, o] : gE.orig) {
    std::vector<float> s = o;
    auto sit = gE.sd.find(key);
    if (p && sit != gE.sd.end() && p->amount != 0)
      for (size_t k = 0; k < (size_t)N * N; ++k) {
        float w = lassoElevWeight(*p, sit->second[k], gE.maxInside);
        if (w > 0) s[k] = std::clamp(o[k] + p->amount * w, kSculptMin, kSculptMax);
      }
    auto cur = T.sculpt.find(key);
    bool any = false; for (float v : s) if (std::fabs(v) > 1e-5f) { any = true; break; }
    if (cur != T.sculpt.end() ? cur->second == s : !any) continue;   // unchanged since the last write
    if (any) T.sculpt[key] = std::move(s); else T.sculpt.erase(key);
    keys.push_back(key);
  }
  if (!keys.empty()) T.sculptChanged(keys);
  // re-seat from the pre-edit heights (a preview is re-applied from scratch, never on top of the last one)
  for (auto& [id, y] : gE.seatY) if (Instance* i = app.doc.find(id)) { i->pos.y = y; app.bump(*i); }
  if (gE.startSet) app.doc.start.pos.y = gE.startY;
  terrainReseat(app, gE.seats);
}
}   // namespace

bool lassoElevActive() { return gE.active; }

bool lassoElevBegin(App& app, const LassoRegion& r) {
  if (gE.active) lassoElevCancel(app);
  if (r.empty() || app.terrain.h.empty()) return false;
  app.pushUndo();
  gE = ElevSession{};
  gE.active = true; gE.region = r; gE.undoMark = app.undoStack.size();
  vec2 lo, hi; r.bounds(lo, hi);
  gE.seats = terrainSeats(app, (lo + hi) * 0.5f, glm::length(hi - lo) * 0.5f + 64.0f);   // falloff up to 60 m
  for (auto& s : gE.seats) if (Instance* i = app.doc.find(s.id)) gE.seatY[s.id] = i->pos.y;
  gE.startSet = app.doc.start.set; gE.startY = app.doc.start.pos.y;
  elevBuildSd(app, 12.0f);
  return true;
}

void lassoElevPreview(App& app, const ElevParams& p) {
  if (!gE.active) return;
  if (p.falloff > gE.margin - 0.5f) elevBuildSd(app, std::max(12.0f, std::min(64.0f, p.falloff * 1.5f + 2.0f)));
  gE.last = p;
  elevWrite(app, &p);
  ++app.groundRev;
}

bool lassoElevApply(App& app, bool fillWater, WaterFill* fill) {
  if (!gE.active) return false;
  bool changed = gE.last.amount != 0;
  std::vector<int> keys; for (auto& [k, v] : gE.orig) keys.push_back(k);
  gE.active = false;
  if (!changed) { if (app.undoStack.size() == gE.undoMark && !app.undoStack.empty()) app.undoStack.pop_back(); return false; }
  terrainCommitSculpt(app, keys);
  ++app.groundRev;
  if (fillWater) {
    WaterFill f = lassoFillWater(app, gE.region, false, gE.last.falloff + 1.0f);   // the same undo step as the lowering; its ring is the bank
    if (!f.ok) app.toast("No water: " + f.why, 4);
    if (fill) *fill = f;
  }
  app.dirty = true;
  return true;
}

void lassoElevCancel(App& app) {
  if (!gE.active) return;
  elevWrite(app, nullptr);
  gE.active = false;
  if (app.undoStack.size() == gE.undoMark && !app.undoStack.empty()) app.undoStack.pop_back();
  ++app.groundRev;
}

bool lassoElevate(App& app, const LassoRegion& r, const ElevParams& p, bool fillWater, WaterFill* fill) {
  if (!lassoElevBegin(app, r)) return false;
  lassoElevPreview(app, p);
  return lassoElevApply(app, fillWater, fill);
}

// ================================================================ water
WaterFill lassoFillWater(App& app, const LassoRegion& r, bool ownUndo, float grow) {
  std::vector<vec2> outer;   // the flood seeds inside the union of the add shapes
  WaterFill best; best.why = "draw a region first";
  for (auto& s : r.shapes) {
    if (s.subtract) continue;
    WaterFill f = lassoComputeFill(app, s.poly, 80.0f, grow);
    if (f.ok && (!best.ok || f.level < best.level)) best = f;
    else if (!best.ok) best = f;
  }
  if (!best.ok) return best;
  if (ownUndo) app.pushUndo();
  // a new fill replaces the bodies it covers (refilling the same hollow)
  auto& wb = app.doc.waterBodies;
  wb.erase(std::remove_if(wb.begin(), wb.end(), [&](const WaterBody& w) { return geom2d::inside(best.outline, w.seed); }), wb.end());
  WaterBody w; w.id = app.doc.newId(); w.outline = best.outline; w.level = best.level; w.seed = best.seed;
  wb.push_back(w);
  ++app.groundRev; app.dirty = true;
  return best;
}

int lassoRemoveWater(App& app, const LassoRegion& r) {
  auto& wb = app.doc.waterBodies;
  int n = 0; for (auto& w : wb) if (r.contains(w.seed)) ++n;
  if (!n) return 0;
  app.pushUndo();
  wb.erase(std::remove_if(wb.begin(), wb.end(), [&](const WaterBody& w) { return r.contains(w.seed); }), wb.end());
  ++app.groundRev; app.dirty = true;
  return n;
}
