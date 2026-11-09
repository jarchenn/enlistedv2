// --selftest-interiors (stage 2800): walls, interiors and furniture (docs/interiors.md) --
//   materials retargeted to the finish patterns (and the schoolhouse's missing brick), the furniture family
//   (bevels, colliders, damage data), room detection and keep-clear volumes, seeded room templates (deterministic,
//   non-duplicating, overrides kept, deletions kept), manual placement rules (doors, stairs, walls, support), host
//   follow / duplicate / delete / undo, save / load, the furnished buildings' own walkthroughs (doors, stairs,
//   routes), reachability of every room, small arms, blast shielding by walls, a shell through an open window,
//   support loss (tables, floors), fragment budgets, reset / Apply, and frame time in a furnished room and a blast.
#include "selftest.h"
#include "lights.h"   // L3 (docs/lights.md)
#include <set>
#include <unordered_map>
#include <GLFW/glfw3.h>
#include <functional>
#include <chrono>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; float limit = 30; };
std::vector<Step> seq; size_t cur = 0; float tcur = 0;
uint64_t FH = 0, CT = 0, INN = 0, WS = 0, PLAIN = 0, CRATES[30] = {}, GK = 0, TK = 0, CRUSH[3] = {}, SHOT[3] = {};
std::vector<std::string> firstLayout;
json perf = json::object();
std::vector<double> frameMs; std::chrono::high_resolution_clock::time_point lastT;
char b_[600]; float dt_ = 1.0f / 60.0f; std::string scratch_;
fs::path outDirI() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
vec3 W(App& a, uint64_t id, vec3 l) { Instance* i = a.doc.find(id); return i ? i->pos + yawQuat(i->yaw) * l : l; }
std::vector<Instance*> childrenOf(App& a, uint64_t host) {
  std::vector<Instance*> v;
  for (auto& i : a.doc.instances) if (i.extra.is_object() && i.extra.contains("furnish") && i.extra["furnish"].value("host", (uint64_t)0) == host) v.push_back(&i);
  return v;
}
std::vector<std::string> layoutOf(App& a, uint64_t host) {
  std::vector<std::string> v;
  for (auto* i : childrenOf(a, host)) {
    char b[200]; std::snprintf(b, sizeof b, "%s|%s|%.3f,%.3f,%.3f|%.1f", i->extra["furnish"].value("slot", std::string()).c_str(), i->assetId.c_str(), i->pos.x, i->pos.y, i->pos.z, i->yaw);
    v.push_back(b);
  }
  std::sort(v.begin(), v.end());
  return v;
}
Instance* childBy(App& a, uint64_t host, const std::string& slotPart, bool blocking = true) {
  for (auto* i : childrenOf(a, host)) {
    const PropDef* pd = a.interior.prop(a.assetFor(*i));
    if (pd && pd->blocking == blocking && i->extra["furnish"].value("slot", std::string()).find(slotPart) != std::string::npos) return i;
  }
  return nullptr;
}
bool propBroken(App& a, uint64_t id) { auto it = a.interior.st.find(id); return it != a.interior.st.end() && it->second.broken; }
int liveFrags(App& a) { int n = 0; for (auto& f : a.interior.frags) n += !f.frozen; return n; }
// grid reachability: cells of every room reachable from its doorways / neighbours with a 0.3 m body clearance
// around blocking furniture (host-local footprints dilated by the player radius)
std::string reachWhy;
float reachable(App& a, uint64_t host, int& worstRoom) {
  Instance* h = a.doc.find(host); const AssetDef* ha = a.assetFor(*h);
  const RoomInfo* ri = a.interior.rooms(ha);
  float worst = 1; worstRoom = -1;
  quat iq = glm::inverse(yawQuat(h->yaw));
  for (size_t s = 0; s < ri->storeys.size(); ++s) {
    const auto& S = ri->storeys[s];
    std::vector<uint8_t> blocked(S.id.size(), 0);
    for (auto* c : childrenOf(a, host)) {
      const PropDef* pd = a.interior.prop(a.assetFor(*c)); if (!pd || !pd->blocking || propBroken(a, c->id)) continue;
      vec3 lp = iq * (c->pos - h->pos);
      if (std::fabs(lp.y - S.y) > 0.5f) continue;
      quat q = yawQuat(c->yaw - h->yaw); vec3 mn(1e9f), mx(-1e9f);
      for (int k = 0; k < 8; ++k) { vec3 cc((k & 1) ? pd->bmax.x : pd->bmin.x, 0, (k & 4) ? pd->bmax.z : pd->bmin.z); vec3 w = lp + q * cc; mn = glm::min(mn, w); mx = glm::max(mx, w); }
      mn -= vec3(0.28f); mx += vec3(0.28f);
      for (int j = 0; j < S.nz; ++j) for (int i = 0; i < S.nx; ++i) {
        float x = S.o.x + (i + 0.5f) * ri->cell, z = S.o.y + (j + 0.5f) * ri->cell;
        if (x > mn.x && x < mx.x && z > mn.z && z < mx.z) blocked[(size_t)j * S.nx + i] = 1;
      }
    }
    // flood from the ways in (doorway cells, door swings, stair landings, the authored route), once over the empty
    // storey (the building's own access) and once around the furniture: furniture must not cut off what was reachable
    auto flood = [&](const std::vector<uint8_t>& blk) {
      std::vector<uint8_t> seen(S.id.size(), 0); std::vector<int> st;
      for (size_t c = 0; c < S.id.size(); ++c) if ((S.id[c] == RoomInfo::DOORWAY || (S.keep[c] && S.id[c] >= 0)) && !blk[c]) { seen[c] = 1; st.push_back((int)c); }
      while (!st.empty()) {
        int c = st.back(); st.pop_back(); int ci = c % S.nx, cj = c / S.nx;
        const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; ++d) {
          int ni = ci + di[d], nj = cj + dj[d]; if (ni < 0 || nj < 0 || ni >= S.nx || nj >= S.nz) continue;
          size_t n = (size_t)nj * S.nx + ni;
          if (seen[n] || blk[n] || (S.id[n] < 0 && S.id[n] != RoomInfo::DOORWAY)) continue;
          seen[n] = 1; st.push_back((int)n);
        }
      }
      return seen;
    };
    std::vector<uint8_t> none(S.id.size(), 0);
    std::vector<uint8_t> base = flood(none), seen = flood(blocked);
    for (size_t k = 0; k < ri->rooms.size(); ++k) {
      if (ri->rooms[k].storey != (int)s) continue;
      int tot = 0, ok = 0, free = 0;
      for (size_t c = 0; c < S.id.size(); ++c) if (S.id[c] == (int)k && base[c]) { ++tot; if (!blocked[c]) { ++free; ok += seen[c]; } }
      if (!tot) continue;   // closed off in the building itself (no doorway / route reaches it): not the furniture's doing
      float f = free ? (float)ok / free : 1.0f;
      if (f < worst) {
        worst = f; worstRoom = (int)k;
        int seeds = 0; for (size_t c = 0; c < S.id.size(); ++c) if (S.id[c] == (int)k && (S.keep[c])) ++seeds;
        char b[200]; std::snprintf(b, sizeof b, "%s room %zu (%s, storey %zu): %d free cells, %d reached, %d keep cells", ha->id.c_str(), k, roomTypeName(ri->rooms[k].type), s, free, ok, seeds);
        reachWhy = b;
      }
      (void)tot;
    }
  }
  return worst;
}
}

void SelfTest::interiorsUpdate(App& app, float dt) {
  auto push = [&](const std::string& n, std::function<bool(App&, float)> f, float limit = 30) { seq.push_back({n, f, limit}); };
  if (stage == 2800) {
    reportName = "interiors";
    const TemplateInfo* t = app.findTemplate("blank_slate");
    app.newMap(*t, "Interior checks");
    seq.clear(); cur = 0; tcur = 0;

    // ================================================================ materials
    push("materials: finish patterns on the buildings", [this](App& a, float) {
      auto pat = [&](const char* asset, const char* mat) { const AssetDef* as = a.lib.find(asset); if (!as) return -1; for (auto& m : as->materials) if (m.name == mat) return m.pattern; return -2; };
      check(pat("house_farmhouse_2story", "plaster") == PAT_INTERIOR && pat("house_farmhouse_2story", "floor_x") == PAT_FLOORFIN && pat("house_farmhouse_2story", "terracotta") == PAT_FLOORFIN,
            "farmhouse: interior plaster -> per-room finish (20), floors -> per-room floor finish (21)");
      check(pat("house_farmhouse_2story", "stone") == PAT_RUBBLE && pat("house_farmhouse_2story", "stone_dressed") == PAT_ASHLAR, "rubble walls -> muted rubble (22), quoins and dressings -> ashlar (19)");
      check(pat("large_village_inn", "render") == PAT_LIMEWASH, "inn / office render -> lime render over rubble (17)");
      int sb = pat("large_village_schoolhouse", "brick");
      check(sb == PAT_BRICKWORK, "schoolhouse: the walls' `brick` material now exists (was undefined: walls fell back to the default grey) -> brickwork (18)");
      // the interior finish is the plaster skin of every wall bay, so a breach removes paper / paint / wainscot with the wall
      const AssetDef* fh = a.lib.find("house_farmhouse_2story"); int skins = 0, bays = 0;
      for (auto& pn : fh->panels) { bool sk = false; for (auto& p : pn.parts[0]) if (fh->materials[p.mat].pattern == PAT_INTERIOR) sk = true; skins += sk; ++bays; }
      std::snprintf(b_, sizeof b_, "interior finishes live in the wall bays' plaster layer (%d of %d farmhouse bays): holes remove them with the wall", skins, bays);
      check(skins >= 20, b_);
      check(a.R.shaderErrors == 0, "main shader with patterns 17-28 compiled");
      return true; });

    // ================================================================ furniture family
    push("furniture: catalogue, bevels, colliders, damage data", [this](App& a, float) {
      int n = 0, bevelled = 0, blockingOk = 0, cosmetic = 0, frags = 0, maxTris = 0; std::string worst;
      for (auto& as : a.lib.assets) {
        if (as->category != "Furniture") continue;
        ++n;
        const PropDef* pd = a.interior.prop(as.get());
        if (!pd) { check(false, as->id + ": no prop block"); continue; }
        int round = 0, cols = 0;   // P1: chamfered boxes and the B1 curved parts (turnings, casks, arches, wedges) count as soft-edged
        for (auto& p : as->parts) { round += p.shape == Part::ROUND || (p.shape == Part::BOX && p.bv > 0) || p.shape == Part::CYL || p.shape == Part::ARCH || p.shape == Part::WEDGE; cols += (!p.visible && p.collide); }
        if (round * 2 >= (int)as->parts.size() - cols || pd->flat()) ++bevelled;
        if (pd->blocking ? (cols >= 1 && cols <= 3 && as->shape) : ((!as->shape && cols == 0) || (lightDef(as.get()) && cols <= 1))) ++blockingOk;   // L3: a lamp keeps one invisible box so rounds can shoot it out
        cosmetic += !pd->blocking;
        frags += (int)pd->frags.size();
        int tris = (as->opaque.count + as->glass.count) / 3;
        if (tris > maxTris) { maxTris = tris; worst = as->id; }
        check(as->thumbnail != 0, as->id + ": catalogue thumbnail");
      }
      std::snprintf(b_, sizeof b_, "%d furniture assets in the Furniture category (tables, chairs, stool, hutches, armoire, commode, chest, bed, shelf, bench, fireplace, clock, small props, rugs)", n);
      check(n >= 25, b_);
      std::snprintf(b_, sizeof b_, "%d/%d built from bevelled / rounded boxes and turned or curved parts (smooth normals) or flat sheets", bevelled, n); check(bevelled == n, b_);
      std::snprintf(b_, sizeof b_, "%d/%d: blocking pieces have 1-3 simple collider boxes, %d cosmetic pieces have no blocking collision", blockingOk, n, cosmetic); check(blockingOk == n, b_);
      std::snprintf(b_, sizeof b_, "%d rigid fragment pieces defined (at most 4 per item); heaviest mesh %s %d tris", frags, worst.c_str(), maxTris); check(frags >= 20 && maxTris < 40000, b_);
      const PropDef* fp = a.interior.prop(a.lib.find("furn_fireplace"));
      check(fp && fp->family == "masonry" && fp->rounds == 0 && fp->blast >= 300, "fireplace surround is masonry: rounds stop on it, only a very close blast brings its mantel down");
      const PropDef* gh = a.interior.prop(a.lib.find("furn_hutch_glazed"));
      check(gh && gh->glass >= 0 && a.lib.find("furn_hutch_glazed")->glass.count > 0, "glazed hutch: its panes break on their own (first hit)");
      summary["furniture"] = {{"assets", n}, {"fragments", frags}, {"maxTris", maxTris}, {"maxTrisAsset", worst}};
      return true; });

    // ================================================================ rooms
    push("rooms: detection, keep-clear volumes, sills", [this](App& a, float) {
      json rs = json::object();
      for (auto& as : a.lib.assets) {
        if (!as->enterable || as->panels.empty() || !(as->id.rfind("house_", 0) == 0 || as->id.rfind("small_", 0) == 0 || as->id.rfind("large_", 0) == 0)) continue;
        const RoomInfo* ri = a.interior.rooms(as.get());
        int doors = 0, stairs = 0, wins = 0; for (auto& b : ri->keep) { doors += b.kind == 0; stairs += b.kind == 1; wins += b.kind == 2; }
        std::string types; for (auto& r : ri->rooms) { types += roomTypeName(r.type); types += r.hearth ? "*, " : ", "; }
        std::snprintf(b_, sizeof b_, "%s: %zu storeys, %zu rooms (%s), keep-clear: %d doors, %d stairs, %d windows; %zu sills", as->id.c_str(), ri->storeys.size(), ri->rooms.size(), types.c_str(), doors, stairs, wins, ri->sills.size());
        check(!ri->rooms.empty() && doors >= 1 && (as->floors < 2 || stairs >= 1), b_);
        rs[as->id] = {{"rooms", ri->rooms.size()}, {"doors", doors}, {"stairs", stairs}, {"windows", wins}};
      }
      const RoomInfo* fr = a.interior.rooms(a.lib.find("house_farmhouse_2story"));
      bool kitchen = false, bed = false; for (auto& r : fr->rooms) { kitchen |= r.type == ROOM_KITCHEN && r.hearth; bed |= r.type == ROOM_BEDROOM; }
      check(kitchen && bed, "farmhouse: the hearth room is the kitchen, the upper rooms are bedrooms");
      int stairs = 0; for (auto& b : fr->keep) stairs += b.kind == 1;
      check(stairs == 2, "farmhouse: both staircases (with landings) are keep-clear volumes");
      summary["rooms"] = rs;
      return true; });

    // ================================================================ templates
    push("templates: furnish four buildings", [this](App& a, float) {
      size_t undo0 = a.undoStack.size();
      FH = a.placeInstance(a.lib.find("house_farmhouse_2story"), vec3(0, 0, 0), 0.0f);
      CT = a.placeInstance(a.lib.find("house_stone_cottage"), vec3(40, 0, 0), 90.0f);
      INN = a.placeInstance(a.lib.find("large_village_inn"), vec3(-45, 0, 0), 0.0f);
      WS = a.placeInstance(a.lib.find("small_carpenter_workshop"), vec3(0, 0, 40), 180.0f);
      // V4 auto-furnish on placement: each building arrives furnished, with one undo step (the placement itself; the furnishing adds none)
      int autoN = 0; bool allAuto = true;
      for (uint64_t h : {FH, CT, INN, WS}) { int n = (int)childrenOf(a, h).size(); autoN += n; allAuto &= n > 0 && a.interior.hideBuiltin(*a.doc.find(h)); }
      std::snprintf(b_, sizeof b_, "auto-furnish on placement: the four buildings arrive furnished (%d pieces); %zu undo steps for 4 placements", autoN, a.undoStack.size() - undo0);
      check(allAuto && a.undoStack.size() - undo0 == 4, b_);
      a.interior.autoOnPlace = false;
      PLAIN = a.placeInstance(a.lib.find("house_farmhouse_2story"), vec3(0, 0, -40), 0.0f);   // built-in furniture (baseline)
      a.interior.autoOnPlace = true;
      a.interior.unfurnish(a, PLAIN);   // built-in furniture on purpose: interior.on = false, never auto-furnished (save / reopen)
      check(FH && CT && INN && WS && PLAIN && childrenOf(a, PLAIN).empty(), "fixtures: farmhouse, cottage (turned 90 deg), inn, workshop (turned 180 deg), a plain farmhouse (auto-furnish off)");
      a.syncRuntime();
      for (uint64_t h : {FH, CT, INN, WS}) {
        size_t had = childrenOf(a, h).size();
        int n = a.interior.furnish(a, h, 0);
        check((size_t)n == had, a.doc.find(h)->assetId + ": regenerating the auto-furnished layout gives the same " + std::to_string(had) + " pieces");
        Instance* hi = a.doc.find(h);
        std::set<std::string> kinds; for (auto* c : childrenOf(a, h)) kinds.insert(c->assetId);
        std::snprintf(b_, sizeof b_, "%s: %d pieces of %zu kinds in %.0f ms; built-in box furniture hidden", hi->assetId.c_str(), n, kinds.size(), a.interior.lastFurnishMs);
        check(n >= 6 && a.interior.hideBuiltin(*hi), b_);
        summary["furnish"][hi->assetId] = {{"pieces", n}, {"kinds", kinds.size()}, {"ms", a.interior.lastFurnishMs}};
      }
      firstLayout = layoutOf(a, FH);
      // distinct layouts: the farmhouse's two bedrooms and the cottage do not repeat one arrangement
      std::set<std::string> rooms;
      for (auto* c : childrenOf(a, FH)) { std::string s = c->extra["furnish"].value("slot", std::string()); rooms.insert(s.substr(0, s.find('/'))); }
      check(rooms.size() >= 4, "farmhouse: furniture in " + std::to_string(rooms.size()) + " rooms");
      // V4 density gate: at least 3x the pieces of the first templates (measured on master 0e8479f with these four fixtures:
      // farmhouse 22, cottage 8, inn 9, workshop 6 = 45)
      const std::map<std::string, int> before = {{"house_farmhouse_2story", 22}, {"house_stone_cottage", 8}, {"large_village_inn", 9}, {"small_carpenter_workshop", 6}};
      int now = 0, was = 0; std::string per;
      for (uint64_t h : {FH, CT, INN, WS}) {
        Instance* hi = a.doc.find(h); int n = (int)childrenOf(a, h).size(), b = before.at(hi->assetId);
        now += n; was += b;
        char q[120]; std::snprintf(q, sizeof q, "%s%s %d (x%.1f)", per.empty() ? "" : ", ", hi->assetId.c_str(), n, n / (float)b); per += q;
      }
      std::snprintf(b_, sizeof b_, "density: %d pieces in the four fixtures vs %d before = x%.2f (gate x3): %s", now, was, now / (float)was, per.c_str());
      check(now >= 3 * was, b_);
      summary["density"] = {{"before", was}, {"now", now}, {"ratio", now / (float)was}};
      return true; });
    push("templates: every piece valid (floors, walls, doors, stairs, windows)", [this](App& a, float) {
      int bad = 0, n = 0, rugs = 0, rugOk = 0; std::string why;
      for (uint64_t h : {FH, CT, INN, WS})
        for (auto* c : childrenOf(a, h)) {
          const AssetDef* ca = a.assetFor(*c); const PropDef* pd = a.interior.prop(ca);
          if (!pd || c->extra.value("propBroken", false)) continue;
          ++n;
          std::string slot = c->extra["furnish"].value("slot", std::string());
          if (pd->flat() && c->assetId.rfind("furn_rug", 0) == 0) {   // rugs lie 2 mm above the floor of their storey: no z-fighting, no floating
            ++rugs; Instance* hi = a.doc.find(h); const RoomInfo* ri = a.interior.rooms(a.assetFor(*hi));
            float ly = (glm::inverse(yawQuat(hi->yaw)) * (c->pos - hi->pos)).y, fl = 1e9f;
            for (auto& S : ri->storeys) if (std::fabs(S.y - ly) < std::fabs(fl - ly)) fl = S.y;
            float gap = (ly + pd->bmin.y) - fl;
            if (gap > 0.0009f && gap < 0.006f) ++rugOk; else why += " rug gap " + std::to_string(gap);
          }
          if (slot.find("/top") != std::string::npos) {   // on a furniture top: resting on the parent's surface
            std::string ps = slot.substr(0, slot.find("/top")); Instance* par = nullptr;
            for (auto* k : childrenOf(a, h)) if (k->extra["furnish"].value("slot", std::string()) == ps) par = k;
            const PropDef* pp = par ? a.interior.prop(a.assetFor(*par)) : nullptr; bool on = false;
            if (pp) for (float sy : pp->surfY) on |= std::fabs(c->pos.y - (par->pos.y + sy)) < 0.003f;
            if (!on) { ++bad; why += " " + c->assetId + " not on its parent's top"; }
            continue;
          }
          if (pd->mount > 0 || c->assetId == "furn_curtains") continue;   // wall-hung: checked by its support below
          vec3 at = c->pos; PlaceCheck pc = a.interior.checkPlacement(a, ca, at, c->yaw, c->id);
          if (!pc.ok || std::fabs(pc.y - c->pos.y) > 0.03f) { ++bad; if (why.size() < 300) why += " " + c->assetId + ":" + pc.reason + " dy " + std::to_string(pc.y - c->pos.y); }
        }
      std::snprintf(b_, sizeof b_, "%d generated pieces re-checked with the manual placement rules: %d invalid%s", n, bad, why.c_str());
      check(bad == 0, b_);
      std::snprintf(b_, sizeof b_, "%d/%d rugs flat on the floor (1-6 mm gap)", rugOk, rugs); check(rugs >= 2 && rugOk == rugs, b_);
      int unsup = 0; std::string un; for (uint64_t h : {FH, CT, INN, WS}) for (auto* c : childrenOf(a, h)) if (!c->extra.value("propBroken", false) && !a.interior.supported(a, *c, a.assetFor(*c))) { ++unsup; if (un.size() < 200) un += " " + c->assetId + "@" + c->extra["furnish"].value("slot", std::string()); }
      check(unsup == 0, "nothing floats: every piece stands on a floor or a furniture top, wall-hung pieces have a wall behind (" + std::to_string(unsup) + " unsupported" + un + ")");
      int worstRoom = -1; float f = 1;
      std::string why2;
      for (uint64_t h : {FH, CT, INN, WS}) { int wr; float r = reachable(a, h, wr); if (r < f) { f = r; worstRoom = wr; why2 = reachWhy; } }
      reachWhy = why2;
      std::snprintf(b_, sizeof b_, "every room stays walkable: worst room has %.0f%% of its free floor reachable from its doors / landings with a 0.3 m body clearance (%s)", f * 100, reachWhy.c_str());
      check(f > 0.85f, b_);
      return true; });
    push("templates: deterministic, non-duplicating, overrides and deletions kept", [this](App& a, float) {
      size_t n0 = childrenOf(a, FH).size(), all0 = a.doc.instances.size();
      a.interior.furnish(a, FH, 0);
      check(layoutOf(a, FH) == firstLayout && a.doc.instances.size() == all0, "regenerate with the same seed: identical layout, no duplicates (" + std::to_string(n0) + " pieces)");
      // a piece moved by hand becomes an override; a deleted piece stays deleted
      Instance* mv = childBy(a, FH, "chair"); Instance* del = childBy(a, FH, "commode");
      check(mv && del, "fixtures: a generated chair and commode");
      if (!mv || !del) return true;
      uint64_t mvId = mv->id; std::string delSlot = del->extra["furnish"]["slot"];
      a.pushUndo();
      mv = a.doc.find(mvId); mv->pos += yawQuat(mv->yaw) * vec3(0.12f, 0, 0.0f); a.bump(*mv); vec3 moved = mv->pos;
      a.selected = del->id; a.deleteSelected();
      a.interior.sync(a);
      mv = a.doc.find(mvId);
      check(mv && mv->extra["furnish"].value("manual", false), "a generated piece moved by hand is marked as an override");
      a.interior.furnish(a, FH, 0);
      mv = a.doc.find(mvId);
      bool delBack = false; for (auto* c : childrenOf(a, FH)) if (c->extra["furnish"].value("slot", std::string()) == delSlot) delBack = true;
      check(mv && glm::length(mv->pos - moved) < 1e-3f, "regenerate keeps the override where it was moved");
      check(!delBack && childrenOf(a, FH).size() <= n0 - 1, "regenerate leaves the deleted piece (and what stood on it) out (" + std::to_string(childrenOf(a, FH).size()) + " of " + std::to_string(n0) + " pieces)");
      a.interior.furnish(a, FH, 1);
      auto l2 = layoutOf(a, FH);
      check(l2 != firstLayout, "New layout (reroll): a different arrangement from a new seed");
      a.interior.furnish(a, FH, 2);
      a.interior.furnish(a, FH, 2);
      std::set<std::string> slots; bool dup = false; for (auto* c : childrenOf(a, FH)) dup |= !slots.insert(c->extra["furnish"].value("slot", std::string())).second;
      check(!dup, "reset twice: one piece per slot, no duplicates");
      return true; });
    push("templates: pieces follow the building; duplicate, delete, undo", [this](App& a, float) {
      Instance* h = a.doc.find(WS); size_t n = childrenOf(a, WS).size();
      if (!n) { check(false, "workshop has no furniture"); return true; }
      Instance* c0 = childrenOf(a, WS).front(); uint64_t cid = c0->id;
      vec3 l0 = glm::inverse(yawQuat(h->yaw)) * (c0->pos - h->pos); float y0 = c0->yaw - h->yaw;
      a.pushUndo(); h = a.doc.find(WS); h->pos += vec3(6, 0, 3); h->yaw = wrapDeg(h->yaw + 90); a.bump(*h);
      a.interior.sync(a); a.syncRuntime();
      h = a.doc.find(WS); Instance* c1 = a.doc.find(cid);
      vec3 l1 = glm::inverse(yawQuat(h->yaw)) * (c1->pos - h->pos);
      check(glm::length(l1 - l0) < 1e-3f && std::fabs(wrapDeg(c1->yaw - h->yaw - y0 + 180) - 180) < 0.05f && !c1->extra["furnish"].value("manual", false),
            "moving / turning the workshop carries its furniture (same local pose, not an override)");
      a.undo(); a.interior.sync(a);
      h = a.doc.find(WS); c1 = a.doc.find(cid);
      check(c1 && glm::length(glm::inverse(yawQuat(h->yaw)) * (c1->pos - h->pos) - l0) < 1e-3f, "undo puts building and furniture back together");
      a.selected = WS; bool dup = a.duplicateSelected(); uint64_t copy = a.selected;
      a.interior.sync(a); a.syncRuntime();
      check(dup && copy != WS && childrenOf(a, copy).size() >= n * 0.8f && childrenOf(a, WS).size() == n, "a duplicated building is furnished on its own (" + std::to_string(childrenOf(a, copy).size()) + " pieces); the original keeps its " + std::to_string(n));
      a.selected = copy; a.deleteSelected(); a.interior.sync(a); a.syncRuntime();
      check(childrenOf(a, copy).empty(), "deleting a building removes its template furniture");
      return true; });
    push("placement: manual rules (doors, stairs, walls, support, wall-hung)", [this](App& a, float) {
      const AssetDef* ar = a.lib.find("furn_armoire"); const AssetDef* pic = a.lib.find("furn_picture"); const AssetDef* rug = a.lib.find("furn_rug_kilim");
      // plain farmhouse (built-in furniture): the front door is at local (1.5, 4.0), the front stair at x 0.2, z -1.7..2.9
      PlaceCheck door = a.checkPlacement(ar, W(a, PLAIN, vec3(1.5f, 0.3f, 3.2f)), 0, 0);
      check(!door.ok && door.reason.find("door") != std::string::npos, "an armoire in the front door's swing is refused: \"" + door.reason + "\"");
      PlaceCheck stair = a.checkPlacement(ar, W(a, PLAIN, vec3(0.2f, 0.31f, 3.4f)), 90, 0);
      check(!stair.ok, "an armoire at the stair foot is refused: \"" + stair.reason + "\"");
      PlaceCheck wall = a.checkPlacement(ar, W(a, PLAIN, vec3(-5.4f, 0.3f, 1.0f)), 90, 0);
      check(!wall.ok && wall.reason.find("ntersect") != std::string::npos, "an armoire pushed into the gable wall is refused: \"" + wall.reason + "\"");
      PlaceCheck hole = a.checkPlacement(a.lib.find("furn_chest"), W(a, PLAIN, vec3(0.2f, 3.3f, 0.6f)), 0, 0);
      check(!hole.ok, "a chest over the stair opening upstairs is refused: \"" + hole.reason + "\"");
      PlaceCheck rugDoor = a.checkPlacement(rug, W(a, PLAIN, vec3(1.9f, 0.3f, 3.2f)), 90, 0);
      check(rugDoor.ok, "a rug may lie in a door swing (cosmetic, no collision): \"" + rugDoor.reason + "\"");
      PlaceCheck air = a.checkPlacement(pic, W(a, PLAIN, vec3(-2.0f, 0.3f, 0.5f)), 0, 0);
      check(!air.ok, "a picture with no wall behind it is refused: \"" + air.reason + "\"");
      // a valid manual placement on the upper floor of the furnished farmhouse, through the real cursor ray
      Instance* h = a.doc.find(FH); const RoomInfo* ri = a.interior.rooms(a.assetFor(*h));
      int ok = 0, tried = 0;
      for (auto& r : ri->rooms) {
        if (r.storey != 1) continue;
        vec2 c = (r.mn + r.mx) * 0.5f; ++tried;
        vec3 hp; vec3 ro = W(a, FH, vec3(c.x, r.y0 + 2.4f, c.y + 0.3f)), rd = glm::normalize(W(a, FH, vec3(c.x, r.y0, c.y)) - ro);
        if (!a.interior.hoverPoint(a, ro, rd, hp, 0)) continue;
        PlaceCheck pc = a.checkPlacement(a.lib.find("furn_stool"), hp, 0, 0);
        if (pc.ok && std::fabs(pc.y - W(a, FH, vec3(0, r.y0, 0)).y) < 0.05f) ++ok;
      }
      check(ok >= 1, "a stool placed through the cursor ray lands on an upper floor (" + std::to_string(ok) + " of " + std::to_string(tried) + " bedrooms)");
      return true; });
    push("save / load: furniture, overrides and seeds round-trip without duplicates", [this](App& a, float) {
      fs::path p = outDirI() / "interiors_roundtrip.nmap.json";
      auto before = layoutOf(a, FH); size_t n = a.doc.instances.size();
      json in0 = a.doc.find(FH)->extra["interior"];
      bool saved = a.saveMapTo(p), opened = a.openMap(p);
      a.syncRuntime(); a.interior.sync(a); a.syncRuntime();
      check(saved && opened && a.doc.instances.size() == n && layoutOf(a, FH) == before && a.doc.find(FH)->extra["interior"] == in0, "save + reopen: same pieces, poses, seeds and slot lists; sync after load adds nothing");
      a.interior.furnish(a, FH, 0);
      check(a.doc.instances.size() == n && layoutOf(a, FH) == before, "regenerate after load reproduces the saved layout");
      return true; });

    // ================================================================ routes in Explore: the buildings' own walkthroughs
    for (const char* which : {"farmhouse", "cottage"}) {
      std::string w = which;
      push("walkthrough of the furnished " + w + " (doors, stairs, rooms)", [this, w](App& a, float t) {
        uint64_t id = w == "farmhouse" ? FH : CT;
        if (t == 0) {
          const AssetDef* as = a.assetFor(*a.doc.find(id));
          json st = as->walkthrough["start"];
          houseId = id;
          a.doc.start = {true, toWorld(a, jvec3(st["pos"])), worldYaw(a, st.value("yaw", 0.0f))};
          a.doc.start.pos.y = a.terrain.height(a.doc.start.pos.x, a.doc.start.pos.z);
          check(a.enterExplore(), "F5 into Explore at the " + w + "'s door");
          a.useScripted = true;
          json steps = json::array();
          for (auto& s : as->walkthrough["steps"]) if (!s.contains("shot")) steps.push_back(s);
          beginCustom(a, as->id, id, steps);
          return false;
        }
        if (runWalkthrough(a, dt_)) { a.exitExplore(); return true; }
        return false; }, 300);
    }

    // ================================================================ damage
    push("damage: Explore fixtures", [this](App& a, float t) {
      if (t == 0) {
        // the kitchen windows of the furnished farmhouse open, so a shell can pass through them
        Instance* h = a.doc.find(FH); h->openDefaults["kitchen_casement"] = true; h->openDefaults["kitchen_shutters"] = true; a.bump(*h);
        for (int k = 0; k < 30; ++k) CRATES[k] = a.placeInstance(a.lib.find("furn_crate"), vec3(30.0f + (k % 6) * 0.75f, 0, 30.0f + (k / 6) * 0.75f), (float)(k * 13 % 90));
        // V4: a second farmhouse for the grenade, three free-standing pieces for direct shell hits, a tank and the
        // furniture in its path
        GK = a.placeInstance(a.lib.find("house_farmhouse_2story"), vec3(75, 0, -20), 0.0f);
        const char* shotIds[3] = {"furn_armoire", "furn_dresser", "furn_sideboard"};
        for (int k = 0; k < 3; ++k) SHOT[k] = a.placeInstance(a.lib.find(shotIds[k]), vec3(-30.0f + k * 12.0f, 0, 70.0f), 0.0f);
        TK = a.placeInstance(a.lib.find("tank_sherman_firefly"), vec3(110, 0, -75), 0.0f);
        const char* crushIds[3] = {"furn_table_dining", "furn_dresser", "furn_armoire"};
        for (int k = 0; k < 3; ++k) CRUSH[k] = a.placeInstance(a.lib.find(crushIds[k]), vec3(110.0f + (k - 1) * 0.4f, 0, -64.0f + k * 4.0f), k == 0 ? 90.0f : 0.0f);
        check(GK && SHOT[0] && SHOT[1] && SHOT[2] && TK && CRUSH[0] && CRUSH[1] && CRUSH[2], "V4 fixtures: a furnished farmhouse for a grenade, three pieces in the open for shells, a Sherman and three pieces in its path");
        int placed = 0; for (auto c : CRATES) placed += c != 0;
        check(placed == 30, "30 crates for the fragment-budget test (" + std::to_string(placed) + ")");
        a.doc.start = {true, vec3(10, 0, 20), 0}; a.doc.start.pos.y = a.terrain.height(10, 20);
        a.syncRuntime();
        check(a.enterExplore(), "F5 into Explore");
        a.useScripted = true;
        return false;
      }
      return t > 0.5f; });
    push("damage: small arms break wood, glass first, masonry stops rounds", [this](App& a, float t) {
      static uint64_t chair = 0, hutch = 0, fire = 0; static int fired = 0, stopped = 0;
      if (t == 0) {
        Instance* c = childBy(a, FH, "chair"); Instance* hu = nullptr;
        for (uint64_t hst : {CT, FH, INN, WS}) if (!hu) for (auto* k : childrenOf(a, hst)) if (k->assetId == "furn_hutch_glazed") hu = k;   // GX2: the seeded pick may give a fixture the plain hutch
        chair = c ? c->id : 0; hutch = hu ? hu->id : 0; fired = 0; stopped = 0;
        fire = a.placeInstance(a.lib.find("furn_fireplace"), vec3(20, 0, 30), 0);
        a.syncRuntime();
        check(chair && fire, "fixtures: a generated chair, a fireplace surround in the open");
        return false;
      }
      const PropDef* cp = chair ? a.interior.prop(a.assetFor(*a.doc.find(chair))) : nullptr;
      if (chair && !propBroken(a, chair) && fired < 40 && (int)(t * 60) % 6 == 0) {
        Instance* c = a.doc.find(chair);
        vec3 tgt = c->pos + vec3(0, 0.3f, 0), from = tgt + vec3(0.3f, 0.5f, 2.5f);
        Physics::Hit hh; bool clear = Physics::castRayEx(from, glm::normalize(tgt - from), glm::length(tgt - from) + 0.3f, hh) && (hh.ud >> 16) == chair;
        a.fireProjectile(from, glm::normalize(tgt - from), 820, PROJ_BULLET, 0, ~0ull, false, "kar98k");
        a.projectiles.back().mass = 0.01f; a.projectiles.back().gravScale = 0;
        if (!clear) a.interior.bulletHit(a, chair, tgt, glm::normalize(tgt - from), scratch_);   // line of fire blocked by the table: hit it directly
        ++fired;
      }
      if (t > 3.0f) {
        auto it = a.interior.st.find(chair);
        std::snprintf(b_, sizeof b_, "a chair breaks after %d rounds (its rating: %d), into %d rigid pieces", fired, cp ? cp->rounds : -1, cp ? (int)cp->frags.size() : 0);
        check(chair && propBroken(a, chair) && it != a.interior.st.end() && cp && fired >= cp->rounds && fired <= cp->rounds + 6, b_);
        int stops = 0; for (int k = 0; k < 20; ++k) stops += a.interior.bulletHit(a, fire, a.doc.find(fire)->pos + vec3(0.6f, 0.6f, 0.25f), vec3(0, 0, -1), scratch_) == 1;
        check(stops == 20 && !propBroken(a, fire), "20 rounds on the masonry fireplace: every one stops, it does not break");
        if (hutch) {
          std::string s; a.interior.bulletHit(a, hutch, a.doc.find(hutch)->pos + vec3(0, 1.4f, 0), vec3(0, 0, -1), s);
          check(a.interior.st[hutch].glassBroken && !propBroken(a, hutch), "the first round into a glazed hutch breaks its panes, the hutch stands");
        }
        return true;
      }
      return false; }, 10);
    push("damage: walls shield furniture; the same blast inside breaks it", [this](App& a, float t) {
      if (t > 0) return true;
      // the generated piece nearest the farmhouse front wall inside the ground floor
      Instance* h = a.doc.find(FH); quat iq = glm::inverse(yawQuat(h->yaw));
      Instance* best = nullptr; float bz = -1e9f;
      for (auto* c : childrenOf(a, FH)) {
        const PropDef* pd = a.interior.prop(a.assetFor(*c)); if (!pd || !pd->blocking || pd->mount > 0 || pd->family == "masonry" || pd->family == "metal" || pd->blast > 60 || propBroken(a, c->id)) continue;
        vec3 l = iq * (c->pos - h->pos); if (l.y > 1.0f || std::fabs(l.x - 1.5f) < 1.2f) continue;
        if (l.z > bz) { bz = l.z; best = c; }
      }
      check(best != nullptr, "fixture: a ground-floor piece near the front wall");
      if (!best) return true;
      uint64_t id = best->id; vec3 l = iq * (best->pos - h->pos);
      int sh0 = a.interior.shielded;
      vec3 outside = W(a, FH, vec3(l.x, 1.0f, 4.5f + 0.02f));
      auto ex = a.explode(outside, yawQuat(h->yaw) * vec3(0, 0, -1), 120, 2.2f, 0.0f, ~0ull, WALL_HE);
      std::snprintf(b_, sizeof b_, "HE on the outside of the front wall, %.2f m from %s: wall cratered (%zu bays), the piece behind it intact (shielded checks %d)", glm::length(outside - best->pos), best->assetId.c_str(), ex.hits.size(), a.interior.shielded - sh0);
      check(!propBroken(a, id) && a.interior.shielded > sh0, b_);
      const PropDef* bpd = a.interior.prop(a.assetFor(*best));
      vec3 lc = l + glm::inverse(yawQuat(h->yaw)) * (yawQuat(best->yaw) * ((bpd->bmin + bpd->bmax) * 0.5f));   // the piece's centre (host-local)
      vec3 inside = W(a, FH, vec3(lc.x, lc.y, std::min(4.0f - 0.02f, lc.z + 0.45f)));   // the same charge inside the room, between the piece and that wall
      a.explode(inside, yawQuat(h->yaw) * vec3(0, 0, 1), 120, 2.2f, 0.0f, ~0ull, WALL_HE);
      check(propBroken(a, id), "the same HE blast inside the room breaks it: " + a.interior.lastBreak);
      return true; });
    push("damage: a shell through the open kitchen window explodes inside", [this](App& a, float t) {
      static int b0 = 0; static vec3 from, aim;
      if (t == 0) {
        b0 = a.interior.breaks;
        aim = W(a, FH, vec3(-3.0f, 1.3f, -2.0f)); from = W(a, FH, vec3(-3.0f, 1.95f, 18.0f));
        a.fireProjectile(from, glm::normalize(aim - from), 600, PROJ_HE, 120.0f, ~0ull, false, "pak40");
        a.projectiles.back().gravScale = 0;
        a.lastExplosion = App::Explosion{};
        return false;
      }
      if (t < 1.0f) return false;
      vec3 e = glm::inverse(yawQuat(a.doc.find(FH)->yaw)) * (a.lastExplosion.pos - a.doc.find(FH)->pos);
      std::snprintf(b_, sizeof b_, "the shell passed the open window and exploded at the next real surface inside (local %.2f, %.2f, %.2f), %d pieces broke", e.x, e.y, e.z, a.interior.breaks - b0);
      check(e.z < 4.0f && e.z > -4.1f && e.x < 0.0f && a.interior.breaks > b0, b_);
      return true; }, 5);
    push("damage: support loss (things on a table, a breached floor)", [this](App& a, float t) {
      if (t > 0) return true;
      // a prop standing on a generated table falls when the table breaks
      Instance* top = nullptr; Instance* table = nullptr;
      for (uint64_t h : {CT, INN, FH})
        for (auto* c : childrenOf(a, h)) {
          std::string s = c->extra["furnish"].value("slot", std::string());
          if (s.find("table/top") != std::string::npos && !propBroken(a, c->id)) {
            std::string base = s.substr(0, s.find("/top"));
            for (auto* k : childrenOf(a, h)) if (k->extra["furnish"].value("slot", std::string()) == base && !propBroken(a, k->id)) { top = c; table = k; }
          }
          if (top) break;
        }
      check(top && table, "fixture: an intact table with something on it");
      if (top && table) {
        uint64_t tid = top->id;
        a.interior.breakProp(a, *table, a.rt[table->id], vec3(0, 0, 2), "test");
        check(propBroken(a, tid), "what stood on the table falls and breaks with it (" + top->assetId + ")");
      }
      // an AP shot up through the upper floor under a small piece: the piece does not float over the hole
      Instance* h = a.doc.find(FH); quat iq = glm::inverse(yawQuat(h->yaw));
      Instance* up = nullptr; float area = 1e9f;
      for (auto* c : childrenOf(a, FH)) {
        const PropDef* pd = a.interior.prop(a.assetFor(*c)); if (!pd || !pd->blocking || propBroken(a, c->id)) continue;
        vec3 l = iq * (c->pos - h->pos); if (l.y < 2.0f) continue;
        float ar = (pd->bmax.x - pd->bmin.x) * (pd->bmax.z - pd->bmin.z);
        if (ar < area) { area = ar; up = c; }
      }
      check(up != nullptr, "fixture: a small piece upstairs");
      if (up) {
        uint64_t uid = up->id; vec3 under = up->pos + yawQuat(up->yaw) * vec3((a.interior.prop(a.assetFor(*up))->bmin.x + a.interior.prop(a.assetFor(*up))->bmax.x) * 0.5f, 0, (a.interior.prop(a.assetFor(*up))->bmin.z + a.interior.prop(a.assetFor(*up))->bmax.z) * 0.5f) - vec3(0, 0.21f, 0);
        a.explode(under, vec3(0, 1, 0), 320, 2.2f, 0.0f, ~0ull, WALL_AP);
        int floating = 0; for (auto* c : childrenOf(a, FH)) if (!propBroken(a, c->id) && !a.interior.supported(a, *c, a.assetFor(*c))) ++floating;
        check(propBroken(a, uid) && floating == 0, "AP up through the floor under " + up->assetId + ": it falls and breaks; nothing in the house floats (" + std::to_string(floating) + ")");
      }
      return true; });
    push("damage: fragment budget (30 crates in one blast), sleep and retire", [this](App& a, float t) {
      static int maxLive = 0, b0 = 0, bodies0 = 0;
      if (t == 0) {
        maxLive = 0; b0 = a.interior.breaks; bodies0 = Physics::bodyCount();
        a.explode(vec3(31.9f, a.terrain.height(31.9f, 31.5f) + 0.3f, 31.5f), vec3(0, -1, 0), 400, 3.5f, 0.0f, ~0ull, WALL_HE);
        return false;
      }
      maxLive = std::max(maxLive, liveFrags(a));
      static float vmax = 0, lowest = 1e9f;
      if (t < 0.05f) { vmax = 0; lowest = 1e9f; }
      for (auto& f : a.interior.frags) if (!f.frozen) {
        if (t < 0.1f) vmax = std::max(vmax, glm::length(Physics::velocity(f.body)));
        vec3 p; quat q; if (Physics::getTransform(f.body, p, q)) lowest = std::min(lowest, p.y - a.terrain.height(p.x, p.z));
      }
      if (t < 9.0f) return false;
      int crates = a.interior.breaks - b0;
      std::snprintf(b_, sizeof b_, "fragments thrown at most %.1f m/s (cap 7) and none sank into the ground (lowest centre %.2f m above it)", vmax, lowest);
      check(vmax <= 7.05f && lowest > -0.3f, b_);
      std::snprintf(b_, sizeof b_, "%d crates broke; live rigid fragments peaked at %d (budget %zu), %zu kept (cap %zu)", crates, maxLive, a.interior.maxLive, a.interior.frags.size(), a.interior.maxKept);
      check(crates >= 20 && maxLive <= (int)a.interior.maxLive && a.interior.frags.size() <= a.interior.maxKept, b_);
      std::snprintf(b_, sizeof b_, "9 s later every fragment sleeps as a visual with no collision (%d live, %d bodies vs %d before)", liveFrags(a), Physics::bodyCount(), bodies0);
      check(liveFrags(a) == 0 && Physics::bodyCount() <= bodies0, b_);
      summary["fragments"] = {{"crates", crates}, {"peakLive", maxLive}, {"kept", a.interior.frags.size()}};
      return true; }, 15);
    push("damage: a grenade in a kitchen blows the furniture apart", [this](App& a, float t) {
      static int inRoom = 0, broke = 0, fr0 = 0, bursts0 = 0; static size_t parts0 = 0; static float ms = 0;
      if (t == 0) {
        Instance* h = a.doc.find(GK); const RoomInfo* ri = a.interior.rooms(a.assetFor(*h));
        int kr = -1; for (size_t k = 0; k < ri->rooms.size(); ++k) if (ri->rooms[k].type == ROOM_KITCHEN) kr = (int)k;
        check(kr >= 0, "fixture: the farmhouse kitchen");
        if (kr < 0) return true;
        const auto& R = ri->rooms[kr]; vec2 c = (R.mn + R.mx) * 0.5f;
        vec3 gp = W(a, GK, vec3(c.x + 0.7f, R.y0 + 0.08f, c.y + 0.5f));
        quat iq = glm::inverse(yawQuat(h->yaw));
        std::vector<uint64_t> ids;
        for (auto* p : childrenOf(a, GK)) {
          const PropDef* pd = a.interior.prop(a.assetFor(*p)); if (!pd || propBroken(a, p->id)) continue;
          vec3 l = iq * (p->pos - h->pos);
          if (l.x < R.mn.x - 0.1f || l.x > R.mx.x + 0.1f || l.z < R.mn.y - 0.1f || l.z > R.mx.y + 0.1f || std::fabs(l.y - R.y0) > 0.4f) continue;
          if (glm::length(p->pos - gp) > 4.0f || !pd->blocking) continue;
          ids.push_back(p->id);
        }
        inRoom = (int)ids.size(); fr0 = a.interior.fragsSpawned; bursts0 = a.interior.splinterBursts; parts0 = a.fx.parts.size();
        a.explode(gp, vec3(0, -1, 0), 85.0f, 4.5f, 0.3f, ~0ull, WALL_GRENADE);   // the M24's blast values
        ms = a.interior.lastBlastMs;
        broke = 0; for (uint64_t id : ids) broke += propBroken(a, id);
        return false;
      }
      if (t < 0.3f) return false;
      std::snprintf(b_, sizeof b_, "a grenade on the kitchen floor: %d of the %d pieces within 4 m blown apart (%d broke in all, %d rigid fragments, %d splinter bursts, +%zu particles; furniture pass %.1f ms)",
                    broke, inRoom, a.interior.lastBlastBreaks, a.interior.fragsSpawned - fr0, a.interior.splinterBursts - bursts0, a.fx.parts.size() - parts0, ms);
      check(inRoom >= 4 && broke * 10 >= inRoom * 7 && a.interior.fragsSpawned - fr0 >= 8 && a.interior.splinterBursts - bursts0 >= broke, b_);
      summary["grenade"] = {{"near", inRoom}, {"broke", broke}, {"allBroke", a.interior.lastBlastBreaks}, {"ms", ms}};
      return true; }, 5);
    push("damage: tank HE, AT and rocket direct hits blow pieces apart", [this](App& a, float t) {
      static int d0 = 0;
      if (t == 0) {
        d0 = a.interior.directHits;
        for (int k = 0; k < 3; ++k) {
          Instance* p = a.doc.find(SHOT[k]); const PropDef* pd = a.interior.prop(a.assetFor(*p));
          vec3 c = p->pos + vec3(0, (pd->bmin.y + pd->bmax.y) * 0.5f, 0), from = c + vec3(0, 0.4f, 30.0f);
          vec3 d = glm::normalize(c - from);
          if (k == 0) a.fireProjectile(from, d, 620, PROJ_HE, 150.0f, ~0ull, false, "sherman_he");                          // 75 mm tank HE
          if (k == 1) { a.fireProjectile(from, d, 790, PROJ_HE, 0.0f, ~0ull, false, "pak40"); a.projectiles.back().at = true; a.projectiles.back().atKill = true; }   // PaK 40 AP
          if (k == 2) { a.fireProjectile(from, d, 110, PROJ_ROCKET, 0.0f, ~0ull, false, "panzerschreck"); a.projectiles.back().blastDmg = 320; a.projectiles.back().blastR = 1.3f; }
          a.projectiles.back().gravScale = 0;
        }
        return false;
      }
      if (t < 1.5f) return false;
      int ok = 0; for (int k = 0; k < 3; ++k) ok += propBroken(a, SHOT[k]);
      std::snprintf(b_, sizeof b_, "a tank HE shell, a PaK AP shot and a rocket each strike a piece in the open: %d of 3 blown apart, %d direct hits", ok, a.interior.directHits - d0);
      check(ok == 3 && a.interior.directHits - d0 >= 3, b_);
      return true; }, 6);
    push("damage: a tank drives through furniture", [this](App& a, float t) {
      TankRT* k = a.tanks.get(TK);
      if (!k) { check(false, "fixture: the Sherman"); return true; }
      static int c0 = 0;
      if (t == 0) {
        c0 = a.interior.crushed;
        k->pos = vec3(110, a.terrain.height(110, -75), -75); k->yaw = 0; k->speed = 0; k->yawRate = 0; a.tanks.updateBody(a, *k, true);
        check(a.tanks.enter(a, TK), "into the Sherman");
        a.scriptedInput.move = vec2(0, 1);
        return false;
      }
      if (t < 9.0f && k->pos.z < -46.0f) return false;
      a.scriptedInput.move = vec2(0);
      int n = 0; for (uint64_t id : CRUSH) { auto it = a.interior.st.find(id); n += it != a.interior.st.end() && it->second.crushed; }
      std::snprintf(b_, sizeof b_, "a Sherman driving at a table, a dresser and an armoire crushes all of them (%d of 3, %d crushed in all) and drives on to z %.1f (blocked by '%s')", n, a.interior.crushed - c0, k->pos.z, k->blockedBy.c_str());
      check(n == 3 && k->pos.z > -50.0f, b_);
      return true; }, 12);
    push("damage: out of the tank", [this](App& a, float t) {
      TankRT* k = a.tanks.get(TK);
      a.scriptedInput.move = vec2(0); a.scriptedInput.interact = false;
      if (!k || !a.tanks.occupied) return true;
      if (std::fabs(k->speed) < 0.05f && std::fmod(t, 1.0f) < 0.02f) a.scriptedInput.interact = true;
      return t > 8.0f; }, 10);
    push("persistence: reset on F5, Apply, Clear applied damage", [this](App& a, float t) {
      if (t > 0) return true;
      int broken = 0; for (auto& [id, s] : a.interior.st) broken += s.broken;
      uint64_t c = 0; for (auto* k : childrenOf(a, FH)) if (propBroken(a, k->id) && !k->extra.value("propBroken", false)) c = k->id;
      a.applyDamageToMap();
      check(c && a.doc.find(c)->extra.value("propBroken", false), "Apply session changes stores broken furniture (extra.propBroken)");
      a.exitExplore();
      int after = 0; for (auto& [id, s] : a.interior.st) after += s.broken && !s.applied;
      check(after == 0 && a.interior.frags.empty(), "back to Creative: session breaks and fragments are reset (" + std::to_string(broken) + " were broken)");
      check(propBroken(a, c), "an applied break stays broken");
      a.clearAppliedDamage(); a.syncRuntime();
      check(!propBroken(a, c) && !a.doc.find(c)->extra.contains("propBroken"), "Clear applied damage restores it");
      a.undo(); a.syncRuntime();
      check(propBroken(a, c), "undo brings the applied break back");
      return true; });

    // ================================================================ every building
    push("all buildings: auto-furnish every enterable asset, skip rules, pews, density", [this](App& a, float) {
      // pieces per building with the first templates (master 0e8479f, default density 0.7): the 3x gate is on the total
      const std::map<std::string, int> before = {{"house_farmhouse_2story", 22}, {"house_stone_cottage", 10}, {"large_municipal_building", 22},
        {"large_stone_warehouse", 13}, {"large_village_inn", 11}, {"large_village_schoolhouse", 27}, {"small_carpenter_workshop", 5}, {"small_roadside_office", 2},
        {"small_stone_dairy", 4}, {"small_storehouse", 4}, {"vil_church", 5}, {"vil_norman_farmhouse", 31}};   // vil_chateau_keep (7) is left bare now
      json all = json::object(); int k = 0, total = 0, was = 0, skippedOk = 0, skipped = 0, furnished = 0, eligible = 0, vilRooms = 0, vils = 0, pews = 0, fixed = 0;
      std::string bad, lowest; float worst = 1e9f;
      for (auto& as : a.lib.assets) {
        if (!as->enterable || as->category == "Furniture") continue;
        vec3 at(-245.0f + (k % 8) * 70.0f, 0, (k / 8 % 2 ? -1.0f : 1.0f) * (110.0f + (k / 16) * 65.0f)); ++k;
        uint64_t id = a.placeInstance(as.get(), at, 0.0f);
        if (!id) { all[as->id] = {{"placed", false}}; bad += " " + as->id + "(not placed)"; continue; }
        const RoomInfo* ri = a.interior.rooms(as.get());
        int n = (int)childrenOf(a, id).size();
        bool skip = as->id.rfind("bunker_", 0) == 0 || as->id.rfind("aw_", 0) == 0 || as->id.rfind("bch_", 0) == 0 || isRuinedAsset(as->id);
        if (as->id.rfind("vil_", 0) == 0) { ++vils; vilRooms += !ri->rooms.empty(); if (ri->rooms.empty()) bad += " " + as->id + "(no rooms)"; }
        int lamps = 0; for (auto* c : childrenOf(a, id)) lamps += lightDef(a.assetFor(*c)) != nullptr;   // L3: bunkers get caged lamps only
        if (skip) { ++skipped; skippedOk += n - lamps == 0 && !a.interior.hideBuiltin(*a.doc.find(id)); }
        else if (!ri->rooms.empty()) { ++eligible; furnished += n > 0 && a.interior.hideBuiltin(*a.doc.find(id)); if (n == 0) bad += " " + as->id + "(empty)"; }
        if (as->id.find("church") != std::string::npos) for (auto* c : childrenOf(a, id)) pews += c->assetId == "furn_pew";
        auto bf = before.find(as->id);
        if (bf != before.end()) { total += n; was += bf->second; float r = n / (float)bf->second; if (r < worst) { worst = r; lowest = as->id; } }
        if (as->id == "vil_stone_barn") fixed += n > 0;
        all[as->id] = {{"pieces", n}, {"rooms", ri->rooms.size()}};
        logf("[interiors] %s: %zu rooms, %d pieces", as->id.c_str(), ri->rooms.size(), n);
      }
      std::snprintf(b_, sizeof b_, "auto-furnish: %d of %d enterable houses, barns, the church and civic buildings furnished on placement;%s", furnished, eligible, bad.empty() ? " none missed" : bad.c_str());
      check(eligible >= 12 && furnished == eligible, b_);
      std::snprintf(b_, sizeof b_, "skip rules: %d of %d bunkers / Atlantic Wall / landing craft / ruins (isRuinedAsset: ruin_*, rub_*, the ruined church, the chateau keep) left as authored", skippedOk, skipped);
      check(skipped >= 12 && skippedOk == skipped, b_);
      std::snprintf(b_, sizeof b_, "room detection: %d of %d vil_* buildings have rooms (earth and dressed-stone floors count: the barn is furnished now (%d), the ruined church has rooms but stays bare)", vilRooms, vils, fixed);
      check(vils >= 5 && vilRooms == vils && fixed == 1, b_);
      std::snprintf(b_, sizeof b_, "the church gets pews: %d pews", pews);
      check(pews >= 6, b_);
      std::snprintf(b_, sizeof b_, "density over every building: %d pieces vs %d with the first templates = x%.2f (gate x3); lowest %s x%.1f", total, was, total / (float)std::max(1, was), lowest.c_str(), worst);
      check(total >= 3 * was, b_);
      // a decorative border building (V8, extra.decor) and a building whose furniture the user removed stay as they are
      a.interior.autoOnPlace = false;
      uint64_t dec = a.placeInstance(a.lib.find("house_stone_cottage"), vec3(-245.0f, 0, 45.0f), 0.0f);   // GX2: clear of the grid (the B3 manor stands at -245, -240)
      a.interior.autoOnPlace = true;
      if (Instance* d = a.doc.find(dec)) { d->extra = json::object(); d->extra["decor"] = true; }
      int dn = a.interior.autoFurnish(a, dec);
      uint64_t rem = a.placeInstance(a.lib.find("house_stone_cottage"), vec3(-175.0f, 0, 45.0f), 0.0f);
      a.interior.unfurnish(a, rem);
      int rn = a.interior.autoFurnish(a, rem);
      check(dec && dn == 0 && childrenOf(a, dec).empty() && rem && rn == 0 && childrenOf(a, rem).empty(), "extra.decor buildings and buildings with their template furniture removed are not auto-furnished");
      summary["allBuildings"] = all; summary["allBuildingsDensity"] = {{"before", was}, {"now", total}, {"ratio", total / (float)std::max(1, was)}};
      return true; });
    // ================================================================ GX2 (graphics brief sections 2-3): dressing and interior lighting
    push("GX2: hallway pendants between windows, a dressed big room, no bare walls, every room furnished and lit", [this](App& a, float) {
      lightsSetDaylight(0.0f);
      // the new props: loaded, with a furnishing envelope; the fixtures carry warm (2700-3200 K) light
      int props = 0, envOk = 0, fix = 0, warm = 0;
      for (const char* id : {"furn_bed_iron", "furn_gramophone", "furn_radio", "furn_sconce", "furn_sconce_candle", "furn_lamp_wall_oil", "furn_candlestick", "furn_lamp_pendant_glass",
                             "furn_coat_stand", "furn_coat_hooks", "furn_suitcases", "furn_curtains_half", "furn_pan_rail", "furn_debris_glass", "furn_debris_plaster", "furn_rug_runner",
                             "furn_frame_landscape", "furn_frame_seascape", "furn_frame_dusk", "furn_frame_portrait", "furn_frame_small"}) {
        const AssetDef* as = a.lib.find(id); if (!as) continue;
        ++props;
        const PropDef* pd = a.interior.prop(as);
        envOk += pd && as->raw["prop"].contains("bounds") && pd->bmax.x > pd->bmin.x && pd->bmax.y > pd->bmin.y;
        if (const LightAssetDef* ld = lightDef(as)) { ++fix; const vec3 c = ld->lights[0].color; warm += c.r >= c.g && c.g > c.b && c.b < 0.8f; }
      }
      std::snprintf(b_, sizeof b_, "GX2 props: %d / 21 loaded, %d with a prop.bounds envelope; %d light fixtures, %d warm (Kelvin from interior_lights.json)", props, envOk, fix, warm);
      check(props == 21 && envOk == props && fix >= 4 && warm == fix, b_);
      vec3 k27 = kelvinToRgb(2700.0f), k32 = kelvinToRgb(3200.0f), k65 = kelvinToRgb(6500.0f);
      std::snprintf(b_, sizeof b_, "colour temperature: 2700 K = (%.2f %.2f %.2f), 3200 K = (%.2f %.2f %.2f), 6500 K ~ white (%.2f %.2f %.2f)", k27.r, k27.g, k27.b, k32.r, k32.g, k32.b, k65.r, k65.g, k65.b);
      check(k27.r == 1.0f && k27.g < k32.g && k27.b < k32.b && k65.g > 0.9f && k65.b > 0.85f, b_);
      // the chateau (furnished by the step before): its rear corridor is a hall lit by pendants between the windows
      Instance* ch = nullptr; for (auto& i : a.doc.instances) if (i.assetId == "vil_chateau_grand") ch = &i;   // the copy the step before placed
      if (!ch) { check(false, "fixture: the grand chateau"); lightsSetDaylight(-1); return true; }
      uint64_t cid = ch->id;
      a.interior.furnish(a, cid, 0);
      std::vector<InteriorSys::RoomStat> st = a.interior.lastStats;
      const AssetDef* ca = a.lib.find("vil_chateau_grand"); const RoomInfo* ri = a.interior.rooms(ca);
      int hk = -1; float hl = 0;
      for (size_t k = 0; k < ri->rooms.size(); ++k) { const auto& R = ri->rooms[k]; vec2 e = R.mx - R.mn; if (R.storey == 0 && R.type == ROOM_HALL && std::max(e.x, e.y) > hl) { hl = std::max(e.x, e.y); hk = (int)k; } }
      std::vector<float> pend; int runners = 0, pics = 0; std::string pr = "r" + std::to_string(hk) + "/";
      for (auto* c : childrenOf(a, cid)) {
        std::string slot = c->extra["furnish"].value("slot", std::string());
        if (slot.rfind(pr, 0) != 0) continue;
        vec3 l = glm::inverse(yawQuat(ch->yaw)) * (c->pos - ch->pos);
        if (slot.find("pendant") != std::string::npos) pend.push_back(l.x);
        runners += c->assetId == "furn_rug_runner"; pics += slot.find("hallpic") != std::string::npos;
      }
      std::sort(pend.begin(), pend.end());
      float gmin = 99, gmax = 0, nearWin = 99;
      for (size_t q = 1; q < pend.size(); ++q) { gmin = std::min(gmin, pend[q] - pend[q - 1]); gmax = std::max(gmax, pend[q] - pend[q - 1]); }
      if (hk >= 0) for (auto& b : ri->keep) if (b.kind == 2 && b.mn.y < ri->rooms[hk].y1 && b.mn.y > ri->rooms[hk].y0 - 0.3f && b.mn.z < ri->rooms[hk].mx.y + 0.9f && b.mx.z > ri->rooms[hk].mn.y - 0.9f)
        for (float x : pend) nearWin = std::min(nearWin, std::fabs(x - (b.mn.x + b.mx.x) * 0.5f));
      std::snprintf(b_, sizeof b_, "chateau corridor (%.1f m, room %d): %zu pendants %.1f-%.1f m apart, nearest %.2f m from a window's centre line; %d runner rugs, %d pictures",
                    hl, hk, pend.size(), gmin, gmax, nearWin, runners, pics);
      check(hk >= 0 && hl >= 20.0f && pend.size() >= 5 && gmin >= 4.0f && gmax <= 6.6f && nearWin >= 0.6f && runners >= 3 && pics >= 5, b_);
      // the current_02 room: the chateau's largest ground-floor room
      const InteriorSys::RoomStat* big = nullptr;
      for (auto& s : st) if (ri->rooms[s.room].storey == 0 && s.type != ROOM_HALL && (!big || s.area > big->area)) big = &s;
      if (big) {
        std::snprintf(b_, sizeof b_, "chateau's largest ground-floor room (%s, %.0f m2): %d pieces (>= 25), %d wall-mounted (>= 6), %d lights, longest bare wall %.1f m (<= 4)",
                      roomTypeName(big->type), big->area, big->pieces, big->wallItems, big->lights, big->bare);
        check(big->pieces >= 25 && big->wallItems >= 6 && big->bare <= 4.0f, b_);
        summary["gx2BigRoom"] = {{"pieces", big->pieces}, {"wallItems", big->wallItems}, {"lights", big->lights}, {"bare", big->bare}, {"area", big->area}};
      }
      // every furnished building: every room furnished and lit, bare walls
      int rooms = 0, dressed = 0, litR = 0, bareOver = 0, roomsWall = 0; std::string empty, dark, bare;
      std::set<std::string> done;
      std::vector<uint64_t> hosts;   // the copies the step before placed (the latest of each building), never the fixtures
      for (auto it = a.doc.instances.rbegin(); it != a.doc.instances.rend(); ++it) {
        const AssetDef* as = a.lib.find(it->assetId);
        if (!as || !as->enterable || as->category == "Furniture" || done.count(as->id) || lightsOnlyHost(as) || isRuinedAsset(as->id)) continue;
        if (!it->extra.is_object() || !it->extra.contains("interior") || !it->extra["interior"].value("on", false)) continue;
        done.insert(as->id); hosts.push_back(it->id);
      }
      for (uint64_t hid : hosts) {
        const AssetDef* as = a.assetFor(*a.doc.find(hid));
        a.interior.furnish(a, hid, 0);
        for (auto& s : a.interior.lastStats) {
          if (s.type == ROOM_RUIN || s.area < 4.0f) continue;
          ++rooms; dressed += s.pieces > 0; litR += s.lights > 0;
          if (!s.pieces && empty.size() < 300) empty += " " + as->id + "/r" + std::to_string(s.room);
          if (!s.lights && dark.size() < 300) dark += " " + as->id + "/r" + std::to_string(s.room);
          if (s.type != ROOM_CHURCH && s.type != ROOM_BARN) { ++roomsWall; if (s.bare > 4.0f) { ++bareOver; if (bare.size() < 400) { char t[96]; std::snprintf(t, sizeof t, " %s/r%d %.1fm", as->id.c_str(), s.room, s.bare); bare += t; } } }
        }
      }
      std::snprintf(b_, sizeof b_, "every building (%zu types): %d / %d rooms furnished%s; %d / %d with their own light%s", done.size(), dressed, rooms, empty.c_str(), litR, rooms, dark.c_str());
      check(rooms > 100 && dressed == rooms && litR == rooms, b_);
      std::snprintf(b_, sizeof b_, "bare walls: %d / %d rooms have a wall over 4 m with nothing on it%s", bareOver, roomsWall, bare.c_str());
      check(bareOver * 10 <= roomsWall, b_);
      summary["gx2Rooms"] = {{"rooms", rooms}, {"furnished", dressed}, {"lit", litR}, {"bareOver4m", bareOver}};
      lightsSetDaylight(-1);
      return true; });
    // ================================================================ L3 lights (docs/lights.md)
    push("lights: every intact building lit, ruins dark, broken lamps out, breach, day / night, switch, apply", [this](App& a, float) {
      lightsSetDaylight(0.0f);   // night: every light at full output
      std::unordered_map<uint64_t, const Instance*> idx; for (auto& i : a.doc.instances) idx[i.id] = &i;
      auto gather = [&]() { std::vector<LightDesc> L; lightsGather(a, L); return L; };
      auto litIds = [&](const std::vector<LightDesc>& L) { std::set<uint64_t> s; for (auto& l : L) s.insert(l.id / 16); return s; };
      std::vector<LightDesc> L = gather();
      std::map<uint64_t, int> lightsOf; std::map<uint64_t, std::set<int>> roomsLit; std::set<uint32_t> ids; int badDesc = 0;
      for (auto& l : L) {
        ids.insert(l.id); badDesc += !(l.intensity > 0 && l.radius > 0 && glm::length(l.color) > 0);
        auto it = idx.find(l.id / 16); if (it == idx.end()) continue;
        const Instance& i = *it->second;
        if (!i.extra.is_object() || !i.extra.contains("furnish")) continue;
        uint64_t h = i.extra["furnish"].value("host", (uint64_t)0); std::string slot = i.extra["furnish"].value("slot", std::string());
        ++lightsOf[h]; if (slot.size() > 1 && slot[0] == 'r') roomsLit[h].insert(std::atoi(slot.c_str() + 1));
      }
      int eligible = 0, lit = 0, ruins = 0, ruinsDark = 0, bunkers = 0, bunkersOk = 0, rooms = 0, roomsWith = 0, crafts = 0, craftsDark = 0;
      std::string bad, ruinBad, open; int openLit = 0; json per = json::object();
      for (auto& i : a.doc.instances) {
        const AssetDef* as = a.lib.find(i.assetId); if (!as || !as->enterable || as->category == "Furniture") continue;
        int n = lightsOf[i.id];
        if (lightsRuinedAsset(as)) { ++ruins; ruinsDark += n == 0; if (n) ruinBad += " " + as->id; continue; }
        if (as->id.rfind("bch_", 0) == 0) { ++crafts; craftsDark += n == 0; continue; }
        bool gen = i.extra.is_object() && i.extra.contains("interior") && i.extra["interior"].value("on", false);
        const RoomInfo* ri = a.interior.rooms(as);
        if (!gen || !lightsLitBuilding(as) || ri->rooms.empty()) continue;   // authored bare (auto-furnish off, decor, removed)
        if (!lightsRoofed(a, i)) { if (open.find(as->id) == std::string::npos) open += " " + as->id; openLit += n > 0; continue; }   // open-topped pits: nothing to hang a lamp from
        ++eligible; lit += n > 0;
        if (!n) {   // why: the first light of the building
          std::string why = "no light pieces";
          for (auto* c : childrenOf(a, i.id)) if (lightDef(a.assetFor(*c))) { why = c->assetId + " " + lightOffReason(a, *c); break; }
          bad += " " + as->id + " #" + std::to_string(i.id) + " (" + why + ")";
        }
        if (lightsOnlyHost(as)) { ++bunkers; bunkersOk += n > 0 && !a.interior.hideBuiltin(i); }
        int nr = 0; for (auto& R : ri->rooms) nr += R.type != ROOM_RUIN;
        rooms += nr; roomsWith += (int)roomsLit[i.id].size();
        if (!per.contains(as->id)) per[as->id] = {{"lights", n}, {"rooms", nr}, {"roomsLit", roomsLit[i.id].size()}};
      }
      std::snprintf(b_, sizeof b_, "interior lights: %d of %d intact furnished buildings lit (%d lights at night, ids unique %d, bad %d);%s", lit, eligible, (int)L.size(), (int)(ids.size() == L.size()), badDesc, bad.empty() ? " none dark" : bad.c_str());
      check(eligible >= 20 && lit == eligible && ids.size() == L.size() && badDesc == 0, b_);
      std::snprintf(b_, sizeof b_, "open-topped emplacements (no ceiling over any room) get no lamp:%s (%d lit)", open.empty() ? " none" : open.c_str(), openLit);
      check(openLit == 0 && open.find("bunker_") == std::string::npos && open.find("house") == std::string::npos, b_);
      std::snprintf(b_, sizeof b_, "rooms lit: %d of %d rooms of those buildings have their own light (gate 80 %%)", roomsWith, rooms);
      check(rooms > 0 && roomsWith >= rooms * 0.8f, b_);
      std::snprintf(b_, sizeof b_, "ruins stay dark: %d of %d ruin / rubble / unfinished buildings without a light%s; %d of %d landing craft dark", ruinsDark, ruins, ruinBad.c_str(), craftsDark, crafts);
      check(ruins >= 3 && ruinsDark == ruins && craftsDark == crafts, b_);
      std::snprintf(b_, sizeof b_, "bunkers and casemates: %d of %d lit with caged electric lamps, their own furniture kept", bunkersOk, bunkers);
      check(bunkers >= 5 && bunkersOk == bunkers, b_);
      summary["lightsPerBuilding"] = per;
      // ---- a hung ceiling lamp: held from above, high enough to walk under
      Instance* cl = nullptr; Instance* tl = nullptr;
      for (auto* c : childrenOf(a, FH)) { if (c->assetId == "furn_lamp_ceiling" && !cl) cl = c; if ((c->assetId == "furn_lamp" || c->assetId == "furn_lamp_table" || c->assetId == "furn_candle") && !tl) tl = c; }
      if (!cl) for (auto* c : childrenOf(a, INN)) if (c->assetId == "furn_lamp_ceiling") { cl = c; break; }
      check(cl != nullptr && tl != nullptr, std::string("farmhouse: a ceiling lamp and a table light (") + (cl ? cl->assetId : "-") + ", " + (tl ? tl->assetId : "-") + ")");
      if (!cl || !tl) { lightsSetDaylight(-1); return true; }
      const PropDef* cpd = a.interior.prop(a.assetFor(*cl));
      {
        uint64_t h = cl->extra["furnish"].value("host", (uint64_t)0); std::string slot = cl->extra["furnish"].value("slot", std::string());
        const RoomInfo* ri = a.interior.rooms(a.assetFor(*a.doc.find(h))); int k = std::atoi(slot.c_str() + 1);
        float ly = cl->extra["furnish"]["loc"][1].get<float>(), clear = ly + cpd->bmin.y - ri->rooms[k].y0;
        std::snprintf(b_, sizeof b_, "ceiling lamp hangs from the ceiling (support from above %d) with %.2f m clear under it (>= 1.75)", (int)a.interior.supported(a, *cl, a.assetFor(*cl)), clear);
        check(a.interior.supported(a, *cl, a.assetFor(*cl)) && clear >= 1.75f, b_);
      }
      uint64_t CL = cl->id, TL = tl->id;
      // ---- day: interior lamps dim to kInteriorDay; a street lamp is off by day, on at night
      const AssetDef* gas = a.lib.find("lamp_gas_street");
      uint64_t SL = 0, SL2 = 0;
      for (int k = 0; k < 40 && (!SL || !SL2) && gas; ++k) {
        vec3 p(-250.0f + 12.0f * k, 0, -285.0f);
        if (!a.checkPlacement(gas, p, 0, 0).ok) continue;
        uint64_t id = a.placeInstance(gas, p, 0); if (!SL) SL = id; else SL2 = id;
      }
      a.syncRuntime();
      idx.clear(); for (auto& i : a.doc.instances) idx[i.id] = &i;   // placing reallocates the instance list
      cl = a.doc.find(CL); tl = a.doc.find(TL);
      check(SL && SL2, "two gas street lamps placed in the open");
      L = gather();
      float nightI = 0; bool streetNight = false; for (auto& l : L) { if (l.id / 16 == CL) nightI = l.intensity; if (l.id / 16 == SL) streetNight = true; }
      lightsSetDaylight(1.0f); L = gather();
      float dayI = 0; bool streetDay = false; for (auto& l : L) { if (l.id / 16 == CL) dayI = l.intensity; if (l.id / 16 == SL) streetDay = true; }
      std::snprintf(b_, sizeof b_, "time of day: street lamp lit at night %d, by day %d; ceiling lamp %.2f at night, %.2f by day (x%.2f, want %.2f)", (int)streetNight, (int)streetDay, nightI, dayI, nightI > 0 ? dayI / nightI : 0.0f, lightsDayScale());
      check(streetNight && !streetDay && nightI > 0 && std::fabs(dayI / nightI - lightsDayScale()) < 0.02f, b_);
      lightsSetDaylight(0.0f);
      // ---- rounds: the iron post rings off, a round in the panes puts the lamp out; a shot table lamp; a blast
      {
        Instance* s = a.doc.find(SL); const PropDef* spd = a.interior.prop(a.assetFor(*s));
        vec3 gc = s->pos + yawQuat(s->yaw) * ((spd->gmin + spd->gmax) * 0.5f);
        int post = a.interior.bulletHit(a, SL, s->pos + vec3(0, 1.4f, 0), vec3(0, 0, 1), scratch_);
        bool stillLit = litIds(gather()).count(SL) > 0;
        int pane = a.interior.bulletHit(a, SL, gc, vec3(0, 0, 1), scratch_);
        std::string why = lightOffReason(a, *a.doc.find(SL));
        std::snprintf(b_, sizeof b_, "street lamp: a round on the post stops (%d) and it stays lit (%d); a round in the panes passes (%d) and puts it out (\"%s\")", post, (int)stillLit, pane, why.c_str());
        check(post == 1 && stillLit && pane == 2 && why == "glass broken" && !litIds(gather()).count(SL), b_);
      }
      {
        const PropDef* tpd = a.interior.prop(a.assetFor(*tl));
        vec3 c = tl->pos + yawQuat(tl->yaw) * ((tpd->bmin + tpd->bmax) * 0.5f);
        int r = a.interior.bulletHit(a, TL, c, vec3(0, -0.3f, 1), scratch_);
        std::string why = lightOffReason(a, *a.doc.find(TL));
        std::snprintf(b_, sizeof b_, "%s shot on the table: hit %d, its light gone from lightsGather (\"%s\")", tl->assetId.c_str(), r, why.c_str());
        check(r == 2 && !why.empty() && !litIds(gather()).count(TL), b_);
      }
      {
        Instance* c = a.doc.find(CL); vec3 gc = c->pos + yawQuat(c->yaw) * ((cpd->gmin + cpd->gmax) * 0.5f);
        bool hit = a.interior.bulletHit(a, CL, gc, vec3(0, 1, 0), scratch_) == 2;
        check(hit && lightOffReason(a, *a.doc.find(CL)) == "glass broken" && !litIds(gather()).count(CL), "ceiling lamp: a round through the bulb puts it out");
        if (Instance* s2 = a.doc.find(SL2)) a.interior.breakProp(a, *s2, a.rt[SL2], vec3(2, 0, 0), "test blast", 1);
        check(propBroken(a, SL2) && !litIds(gather()).count(SL2), "a street lamp blown over: its lantern falls, the light goes out");
      }
      // ---- Apply damage to map: the lights put out this session are saved off; Clear restores
      {
        int n = lightsApplyToMap(a);
        Instance* s = a.doc.find(SL);
        bool saved = s && s->extra.contains("light") && !s->extra["light"].value("on", true) && s->extra["light"].value("applied", false);
        lightsClearApplied(a); s = a.doc.find(SL);
        std::snprintf(b_, sizeof b_, "Apply damage: %d lights saved off (extra.light {on: false, applied: true}); Clear applied damage removes them (%d)", n, (int)(s && !s->extra.contains("light")));
        check(n >= 4 && saved && s && !s->extra.contains("light"), b_);
      }
      // ---- a breached wall bay around the room puts its lamp out; a bay elsewhere does not
      {
        uint64_t lamp = 0, host = 0; int room = -1;
        for (auto& l : gather()) {
          auto it = idx.find(l.id / 16); if (it == idx.end() || !it->second->extra.contains("furnish")) continue;
          const json& f = it->second->extra["furnish"]; if (f.value("host", (uint64_t)0) != FH) continue;
          std::string sl = f.value("slot", std::string()); if (sl.size() < 2 || sl[0] != 'r') continue;
          lamp = l.id / 16; host = FH; room = std::atoi(sl.c_str() + 1); break;
        }
        InstanceRT& hr = a.rt[host];
        const AssetDef* ha = hr.asset; const RoomInfo* ri = a.interior.rooms(ha);
        std::vector<uint8_t> st0 = hr.state; bool dm0 = hr.damaged;
        int near = -1, far = -1;
        if (lamp && room >= 0) {
          const auto& R = ri->rooms[room];
          for (size_t k = 0; k < ha->panels.size(); ++k) {
            const Panel& p = ha->panels[k]; if (p.trim >= 0) continue;
            bool ov = !(p.bmax.x < R.mn.x - 0.45f || p.bmin.x > R.mx.x + 0.45f || p.bmax.z < R.mn.y - 0.45f || p.bmin.z > R.mx.y + 0.45f) && !(p.bmax.y < R.y0 + 0.1f || p.bmin.y > R.y1 - 0.1f);
            bool away = p.bmin.x > R.mx.x + 1.5f || p.bmax.x < R.mn.x - 1.5f || p.bmin.z > R.mx.y + 1.5f || p.bmax.z < R.mn.y - 1.5f || p.bmin.y > R.y1 + 0.5f || p.bmax.y < R.y0 - 0.5f;
            if (ov && near < 0) near = (int)k;
            if (away && far < 0) far = (int)k;
          }
        }
        bool okFar = true, okNear = false;
        if (near >= 0) {
          if (hr.state.size() < ha->panels.size()) hr.state.resize(ha->panels.size(), PANEL_INTACT);
          if (far >= 0) { hr.state[far] = PANEL_BREACHED; hr.damaged = true; okFar = lightOffReason(a, *a.doc.find(lamp)).empty(); hr.state[far] = PANEL_INTACT; }
          hr.state[near] = PANEL_BREACHED; hr.damaged = true;
          okNear = lightOffReason(a, *a.doc.find(lamp)) == "room breached" && !litIds(gather()).count(lamp);
        }
        hr.state = st0; hr.damaged = dm0;
        std::snprintf(b_, sizeof b_, "breach: a breached bay of its room (panel %d) puts the farmhouse room's lamp out (%d); a bay of another part of the house (panel %d) leaves it lit (%d)", near, (int)okNear, far, (int)okFar);
        check(near >= 0 && okNear && okFar, b_);
      }
      // ---- the editor switch (extra.light.on) and the emissive scale
      {
        uint64_t sw = 0; for (auto& l : gather()) if (l.id / 16 != CL && l.id / 16 != TL) { sw = l.id / 16; break; }
        Instance* w = a.doc.find(sw);
        float e0 = w ? lightsEmissiveScale(a, *w) : 0;
        if (w) { w->extra["light"]["on"] = false; }
        bool off = w && !litIds(gather()).count(sw) && lightsEmissiveScale(a, *a.doc.find(sw)) == 0.0f;
        if ((w = a.doc.find(sw))) { w->extra.erase("light"); }
        bool back = w && litIds(gather()).count(sw);
        std::snprintf(b_, sizeof b_, "switch: extra.light.on = false darkens %s (emissive %.2f -> 0), removing it lights it again (%d)", w ? w->assetId.c_str() : "-", e0, (int)back);
        check(sw && off && back && e0 > 0, b_);
      }
      a.interior.reset(a); a.syncRuntime();
      check(litIds(gather()).count(CL) > 0, "reset: the shot ceiling lamp is whole and lit again");
      for (uint64_t id : {SL, SL2}) { a.selected = id; a.deleteSelected(); }
      a.syncRuntime();
      lightsSetDaylight(-1.0f);
      return true; });
    // ================================================================ performance
    push("performance: furnished vs built-in room, a blast in the room", [this](App& a, float t) {
      static int phase = 0; static float t0 = 0;
      auto stand = [&](uint64_t h) { Physics::setCharacterPos(W(a, h, vec3(-1.0f, 0.32f, 3.0f))); vec3 l = W(a, h, vec3(-4.6f, 1.0f, -2.6f)), e = W(a, h, vec3(-1.0f, 1.95f, 3.0f)); a.eyeYaw = degrees(std::atan2(-(l.x - e.x), -(l.z - e.z))); a.eyePitch = -10; };
      auto rec = [&](const char* key) {
        double cpu = 0, gpu = 0; int draws = 0, tris = 0, n = (int)frameMs.size();
        (void)cpu; (void)gpu;
        std::vector<double> s = frameMs; std::sort(s.begin(), s.end());
        double mean = 0; for (double v : s) mean += v; mean /= std::max(1, n);
        draws = a.R.stats.draws; tris = a.R.stats.tris;
        perf[key] = {{"frames", n}, {"meanMs", mean}, {"p95Ms", n ? s[(size_t)(n * 0.95)] : 0}, {"draws", draws}, {"tris", tris}, {"gpuMs", a.R.stats.gpuMs}, {"cpuMs", a.R.stats.cpuMs}};
        frameMs.clear();
      };
      auto tick = [&]() { auto now = std::chrono::high_resolution_clock::now(); if (lastT.time_since_epoch().count()) frameMs.push_back(std::chrono::duration<double, std::milli>(now - lastT).count()); lastT = now; };
      if (t == 0) { phase = 0; t0 = 0; if (a.mode != Mode::Explore) { a.doc.start = {true, W(a, FH, vec3(1.5f, 0, 6.6f)), 0}; a.enterExplore(); a.useScripted = true; } stand(PLAIN); frameMs.clear(); lastT = {}; return false; }
      tick();
      if (phase == 0 && t - t0 > 2.0f) { rec("builtinKitchen"); phase = 1; t0 = t; stand(FH); }
      else if (phase == 1 && t - t0 > 2.0f) { rec("furnishedKitchen"); phase = 2; t0 = t; a.explode(W(a, FH, vec3(-3.0f, 0.32f, -0.5f)), vec3(0, -1, 0), 120, 2.2f, 0.0f, ~0ull, WALL_HE); }
      else if (phase == 2 && t - t0 > 2.0f) { rec("furnishedKitchenBlast"); phase = 3; }
      if (phase < 3) return false;
      std::snprintf(b_, sizeof b_, "kitchen view, 1920x1080 class window: built-in %.1f ms / %d draws / %d tris, furnished %.1f ms / %d draws / %d tris, during a room blast %.1f ms (p95 %.1f) -- wall-clock, GPU shared with other processes",
                    perf["builtinKitchen"]["meanMs"].get<double>(), perf["builtinKitchen"]["draws"].get<int>(), perf["builtinKitchen"]["tris"].get<int>(),
                    perf["furnishedKitchen"]["meanMs"].get<double>(), perf["furnishedKitchen"]["draws"].get<int>(), perf["furnishedKitchen"]["tris"].get<int>(),
                    perf["furnishedKitchenBlast"]["meanMs"].get<double>(), perf["furnishedKitchenBlast"]["p95Ms"].get<double>());
      check(perf["furnishedKitchen"]["meanMs"].get<double>() < 100.0, b_);
      summary["performance"] = perf;
      a.exitExplore();
      return true; }, 12);
    push("auto-furnish: open map and new map (no undo step, not dirty)", [this](App& a, float) {
      // a saved map with one bare building: opening it furnishes the building without an undo step or the dirty flag
      fs::path p = outDirI() / "interiors_autofurnish.nmap.json";
      a.newMap(*a.findTemplate("blank_slate"), "Auto-furnish check");
      a.interior.autoOnPlace = false;
      uint64_t h = a.placeInstance(a.lib.find("vil_norman_farmhouse"), vec3(0, 0, 0), 0.0f);
      a.interior.autoOnPlace = true;
      bool saved = a.saveMapTo(p), opened = a.openMap(p);
      int n = (int)childrenOf(a, h).size();
      std::snprintf(b_, sizeof b_, "open a map with a bare Norman farmhouse: furnished with %d pieces in %.0f ms; undo stack %zu, dirty %d", n, a.interior.lastAutoMs, a.undoStack.size(), (int)a.dirty);
      check(saved && opened && n > 0 && a.undoStack.empty() && !a.dirty, b_);
      // a new map from the Normandy Village Assault template: every building furnished, nothing to undo, not dirty
      const TemplateInfo* bt = a.findTemplate("battle1");
      check(bt != nullptr, "template: battle1 (Normandy Village Assault)");
      if (bt) {
        a.newMap(*bt, "Village check");
        int hosts = 0, done = 0, pieces = 0;
        for (auto& i : a.doc.instances) {
          const AssetDef* as = a.lib.find(i.assetId); if (!as || !as->enterable || a.interior.isProp(as)) continue;
          bool skip = as->id.rfind("bunker_", 0) == 0 || as->id.rfind("aw_", 0) == 0 || as->id.rfind("bch_", 0) == 0 || isRuinedAsset(as->id) || a.interior.rooms(as)->rooms.empty();
          if (skip) continue;
          ++hosts; done += a.interior.hideBuiltin(i);
        }
        for (auto& i : a.doc.instances) pieces += i.extra.is_object() && i.extra.contains("furnish");
        std::snprintf(b_, sizeof b_, "new map from battle1: %d of %d buildings furnished (%d pieces, %.0f ms); undo stack %zu, dirty %d", done, hosts, pieces, a.interior.lastAutoMs, a.undoStack.size(), (int)a.dirty);
        check(hosts > 0 && done == hosts && a.undoStack.empty() && !a.dirty, b_);
        summary["battle1"] = {{"buildings", hosts}, {"pieces", pieces}, {"ms", a.interior.lastAutoMs}};
      }
      return true; });

    if (getenv("NMK_INTERIORS_QUICK")) {   // tuning: only the furnishing steps (no Explore, no frame times)
      std::vector<Step> q; for (auto& s : seq) if (s.name.rfind("templates: furnish", 0) == 0 || s.name.rfind("all buildings", 0) == 0) q.push_back(s);
      seq.swap(q);
    }
  }
  stage = 2801;
  if (cur >= seq.size()) { check(app.R.shaderErrors == 0, "all shaders compiled"); finish(app); return; }
  if (tcur == 0) logf("[selftest] step: %s", seq[cur].name.c_str());
  dt_ = dt;
  bool d2 = seq[cur].fn(app, tcur);
  tcur += dt;
  if (tcur > seq[cur].limit) { check(false, "step timed out: " + seq[cur].name); d2 = true; }
  if (d2) { ++cur; tcur = 0; }
}
