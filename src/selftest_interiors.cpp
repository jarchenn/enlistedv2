// --selftest-interiors (stage 2800): walls, interiors and furniture (docs/interiors.md) --
//   materials retargeted to the finish patterns (and the schoolhouse's missing brick), the furniture family
//   (bevels, colliders, damage data), room detection and keep-clear volumes, seeded room templates (deterministic,
//   non-duplicating, overrides kept, deletions kept), manual placement rules (doors, stairs, walls, support), host
//   follow / duplicate / delete / undo, save / load, the furnished buildings' own walkthroughs (doors, stairs,
//   routes), reachability of every room, small arms, blast shielding by walls, a shell through an open window,
//   support loss (tables, floors), fragment budgets, reset / Apply, and frame time in a furnished room and a blast.
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <functional>
#include <chrono>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; float limit = 30; };
std::vector<Step> seq; size_t cur = 0; float tcur = 0;
uint64_t FH = 0, CT = 0, INN = 0, WS = 0, PLAIN = 0, CRATES[30] = {};
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
        int round = 0, cols = 0; for (auto& p : as->parts) { round += p.shape == Part::ROUND; cols += (!p.visible && p.collide); }
        if (round * 2 >= (int)as->parts.size() - cols || pd->flat()) ++bevelled;
        if (pd->blocking ? (cols >= 1 && cols <= 3 && as->shape) : (!as->shape && cols == 0)) ++blockingOk;
        cosmetic += !pd->blocking;
        frags += (int)pd->frags.size();
        int tris = (as->opaque.count + as->glass.count) / 3;
        if (tris > maxTris) { maxTris = tris; worst = as->id; }
        check(as->thumbnail != 0, as->id + ": catalogue thumbnail");
      }
      std::snprintf(b_, sizeof b_, "%d furniture assets in the Furniture category (tables, chairs, stool, hutches, armoire, commode, chest, bed, shelf, bench, fireplace, clock, small props, rugs)", n);
      check(n >= 25, b_);
      std::snprintf(b_, sizeof b_, "%d/%d built from bevelled rounded boxes (small edge radii, smooth normals) or flat sheets", bevelled, n); check(bevelled == n, b_);
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
      FH = a.placeInstance(a.lib.find("house_farmhouse_2story"), vec3(0, 0, 0), 0.0f);
      CT = a.placeInstance(a.lib.find("house_stone_cottage"), vec3(40, 0, 0), 90.0f);
      INN = a.placeInstance(a.lib.find("large_village_inn"), vec3(-45, 0, 0), 0.0f);
      WS = a.placeInstance(a.lib.find("small_carpenter_workshop"), vec3(0, 0, 40), 180.0f);
      PLAIN = a.placeInstance(a.lib.find("house_farmhouse_2story"), vec3(0, 0, -40), 0.0f);   // built-in furniture (baseline)
      check(FH && CT && INN && WS && PLAIN, "fixtures: farmhouse, cottage (turned 90 deg), inn, workshop (turned 180 deg), a plain farmhouse");
      a.syncRuntime();
      for (uint64_t h : {FH, CT, INN, WS}) {
        int n = a.interior.furnish(a, h, 0);
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
        Instance* c = childBy(a, FH, "chair"); Instance* hu = nullptr; for (auto* k : childrenOf(a, CT)) if (k->assetId.rfind("furn_hutch", 0) == 0) hu = k;
        if (!hu) for (auto* k : childrenOf(a, FH)) if (k->assetId == "furn_hutch_glazed") hu = k;
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
        const PropDef* pd = a.interior.prop(a.assetFor(*c)); if (!pd || !pd->blocking || pd->family == "masonry" || propBroken(a, c->id)) continue;
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
      vec3 inside = W(a, FH, vec3(l.x, 1.0f, 4.0f - 0.02f));
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
