// --selftest-entities: new entities of the complete update, part 4 (docs/entities.md):
//   E1 wire obstacles from the supplied GLB set (three distinct entities, budgets, LOD, editor round trip,
//   traversal blocking / slow mode, no vault, rounds through the gaps, per-stretch blast cutting, rendering).
#include "selftest.h"
#include "model.h"
#include <GLFW/glfw3.h>
#include <functional>
#include <fstream>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> eseq; size_t ecur = 0; float et = 0;
const char* kWires[3] = {"wire_concertina", "wire_fence", "wire_knife_rest"};
uint64_t W_ID[3] = {0, 0, 0};
vec3 W_POS[3] = {vec3(-12, 0, -10), vec3(0, 0, -10), vec3(12, 0, -10)};
fs::path outDirE() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
float yawToward(vec3 d) { float best = 0, bd = -2; d.y = 0; d = glm::normalize(d); for (int y = 0; y < 360; ++y) { float q = glm::dot(App::forward((float)y, 0), d); if (q > bd) { bd = q; best = (float)y; } } return best; }
float yawPlusX() { return App::forward(90, 0).x > 0.5f ? 90.0f : -90.0f; }   // face +x (across the obstacles, which run along z)
void feetAt(App& a, vec3 p) { Physics::setCharacterPos(vec3(p.x, a.terrain.height(p.x, p.z) + 0.02f, p.z)); }
}

void SelfTest::entitiesUpdate(App& app, float dt) {
  auto push = [&](const std::string& n, std::function<bool(App&, float)> f) { eseq.push_back({n, f}); };
  if (stage == 1900) {
    reportName = "entities";
    const TemplateInfo* t = app.findTemplate("blank_slate");
    app.newMap(*t, "Entity checks");
    eseq.clear(); ecur = 0; et = 0;

    // ================================================================ E1 wire obstacles
    push("wire: three distinct catalogue entities", [this](App& a, float) {
      int n = 0; std::set<std::string> ids, models;
      for (auto& as : a.lib.assets) if (as->defense.kind == "wire") { ++n; ids.insert(as->id); }
      check(n == 3 && ids.size() == 3, "exactly three wire-obstacle entities in the catalogue (" + std::to_string(n) + ")");
      for (const char* id : kWires) {
        const AssetDef* as = a.lib.find(id);
        if (!as) { check(false, std::string(id) + " missing"); continue; }
        const GpuModel* m = as->model.get();
        bool ok = m && as->modelLod && as->thumbnail && as->category == "Defences" && as->defense.unsafeVault && as->defense.sections >= 2;
        char b[300];
        std::snprintf(b, sizeof b, "%s \"%s\": model %d tris (LOD %d), %d sections, ground pivot y %.3f, bounds %.2f x %.2f x %.2f m, thumbnail %s",
                      id, as->name.c_str(), m ? m->tris : 0, as->modelLod ? as->modelLod->tris : 0, as->defense.sections, m ? m->bmin.y : -1.0f,
                      m ? m->bmax.x - m->bmin.x : 0, m ? m->bmax.y - m->bmin.y : 0, m ? m->bmax.z - m->bmin.z : 0, as->thumbnail ? "yes" : "NO");
        check(ok && m->tris <= 50000 && as->modelLod->tris < m->tris / 2 && std::fabs(m->bmin.y) < 0.01f, b);
        if (m) { models.insert(std::to_string(m->tris)); summary["wire"][id] = {{"tris", m->tris}, {"lodTris", as->modelLod ? as->modelLod->tris : 0}, {"size", {m->bmax.x - m->bmin.x, m->bmax.y - m->bmin.y, m->bmax.z - m->bmin.z}}}; }
      }
      check(models.size() == 3, "the three use three different source assemblies (not one set copied)");
      return true; });
    push("wire: place, rotate, duplicate, delete, undo / redo, save / load", [this](App& a, float) {
      for (int k = 0; k < 3; ++k) W_ID[k] = a.placeInstance(a.lib.find(kWires[k]), W_POS[k], 0.0f);
      check(W_ID[0] && W_ID[1] && W_ID[2], "each wire obstacle placed on its own");
      size_t n0 = a.doc.instances.size();
      a.selected = W_ID[1]; a.pushUndo(); a.doc.find(W_ID[1])->yaw = 30; a.doc.find(W_ID[1])->rev++;
      bool dup = a.duplicateSelected();
      size_t n1 = a.doc.instances.size();
      a.deleteSelected();
      size_t n2 = a.doc.instances.size();
      a.undo(); size_t n3 = a.doc.instances.size();
      a.redo(); size_t n4 = a.doc.instances.size();
      a.undo();
      char b[200]; std::snprintf(b, sizeof b, "rotate, duplicate (%zu -> %zu), delete (%zu), undo (%zu), redo (%zu) act on one obstacle only", n0, n1, n2, n3, n4);
      check(dup && n1 == n0 + 1 && n2 == n0 && n3 == n0 + 1 && n4 == n0, b);
      Instance* f = a.doc.find(W_ID[1]); if (f) { f->yaw = 0; f->rev++; }
      // keep only the three originals
      std::vector<Instance> keep; for (auto& i : a.doc.instances) if (i.id == W_ID[0] || i.id == W_ID[1] || i.id == W_ID[2]) keep.push_back(i);
      a.doc.instances = keep; a.syncRuntime();
      fs::path p = outDirE() / "entities_wire.nmap.json";
      bool saved = a.saveMapTo(p), opened = a.openMap(p);
      int found = 0; for (auto& i : a.doc.instances) for (const char* id : kWires) if (i.assetId == id) ++found;
      check(saved && opened && found == 3, "save / load keeps all three (" + std::to_string(found) + ")");
      for (int k = 0; k < 3; ++k) for (auto& i : a.doc.instances) if (i.assetId == kWires[k]) W_ID[k] = i.id;
      a.doc.start = {true, vec3(0, a.terrain.height(0, 0), 0), 0};
      check(a.enterExplore(), "F5 into Explore");
      a.useScripted = true;
      return true; });
    push("wire: drawn with LOD and distance cull", [this](App& a, float t) {
      if (t < 0.3f) return false;
      std::vector<DrawItem> op, gl;
      feetAt(a, vec3(0, 0, 0)); a.buildDrawLists(op, gl);
      size_t nearN = a.R.worldModels.size(); int lodNear = 0;
      for (auto& m : a.R.worldModels) if (m.model == a.lib.find("wire_fence")->model.get()) ++lodNear;
      feetAt(a, vec3(0, 0, 80)); a.eyeYaw = 180;
      a.buildDrawLists(op, gl); int lodFar = 0;
      for (auto& m : a.R.worldModels) if (m.model == a.lib.find("wire_fence")->modelLod.get()) ++lodFar;
      feetAt(a, vec3(0, 0, 500)); a.buildDrawLists(op, gl); size_t culled = a.R.worldModels.size();
      char b[200]; std::snprintf(b, sizeof b, "drawn: %zu model parts near (fence full detail %d parts), fence LOD at 90 m (%d parts), nothing beyond the cull distance (%zu)", nearN, lodNear, lodFar, culled);
      check(nearN > 0 && lodNear > 0 && lodFar > 0 && culled == 0, b);
      feetAt(a, vec3(0, 0, 0));
      return true; });
    for (int k = 0; k < 3; ++k) {
      push(std::string("wire: ") + kWires[k] + " blocks walking through", [this, k](App& a, float t) {
        static float x0;
        vec3 p = W_POS[k];
        if (t == 0) { feetAt(a, p + vec3(-2.5f, 0, 0.3f)); a.eyeYaw = yawPlusX(); a.eyePitch = 0; a.scriptedInput = ExploreInput{}; x0 = Physics::character().pos.x; return false; }
        a.scriptedInput.move = vec2(0, 1);
        if (t < 4.0f) return false;
        a.scriptedInput = ExploreInput{};
        float x = Physics::character().pos.x;
        char b[200]; std::snprintf(b, sizeof b, "%s: walking across it for 4 s stops at x %.2f (obstacle centre %.2f, started %.2f)", kWires[k], x, p.x, x0);
        check(x > x0 + 0.5f && x < p.x - 0.1f, b);
        return true; });
      push(std::string("wire: ") + kWires[k] + " cannot be vaulted", [this, k](App& a, float) {
        vec3 feet = Physics::character().pos;
        bool v = a.tryStartVault(feet, yawPlusX());
        check(!v, std::string(kWires[k]) + ": hold-Space vault refused (" + a.vault.why + ")");
        a.vault = App::VaultState{};
        return true; });
    }
    push("wire: a vaultable wall next to wire is not vaulted (unsafe-wire rule)", [this](App& a, float t) {
      static uint64_t wall; static bool withWire, without; static std::string why1, why2;
      if (t == 0) {
        wall = a.placeInstance(a.lib.find("stone_wall_low"), W_POS[0] + vec3(-1.0f, 0, 0), 90.0f);   // runs along z, 0.37 m from the coil
        a.syncRuntime();
        feetAt(a, W_POS[0] + vec3(-2.2f, 0, 0.3f)); a.eyeYaw = yawPlusX();
        withWire = a.tryStartVault(Physics::character().pos, a.eyeYaw); why1 = a.vault.why; a.vault = App::VaultState{};
        Instance* c = a.doc.find(W_ID[0]); vec3 keep = c->pos; c->pos.z += 40; c->rev++; a.syncRuntime();   // move the coil away
        without = a.tryStartVault(Physics::character().pos, a.eyeYaw); why2 = a.vault.why; a.vault = App::VaultState{};
        c->pos = keep; c->rev++;
        a.doc.instances.erase(std::remove_if(a.doc.instances.begin(), a.doc.instances.end(), [](const Instance& i) { return i.id == wall; }), a.doc.instances.end());
        a.syncRuntime();
        check(!withWire && why1 == "unsafe obstacle (wire)" && without, "low wall beside the coil: vault refused (" + why1 + "); the same wall with the coil moved away vaults (" + (without ? std::string("yes") : "NO: " + why2) + ")");
      }
      return true; });
    push("wire: rounds pass through the gaps, posts stop them", [this](App& a, float) {
      Physics::Hit h;
      vec3 c = W_POS[0] + vec3(0, a.terrain.height(W_POS[0].x, W_POS[0].z) + 0.45f, 0);
      bool coil = Physics::castRayEx(c + vec3(-3, 0, 0.3f), vec3(1, 0, 0), 6, h) && (h.ud >> 16) == W_ID[0];
      vec3 f = W_POS[1] + vec3(0, a.terrain.height(W_POS[1].x, W_POS[1].z) + 0.55f, 0);
      bool gap = Physics::castRayEx(f + vec3(-3, 0, 0.55f), vec3(1, 0, 0), 6, h) && (h.ud >> 16) == W_ID[1];
      bool post = Physics::castRayEx(f + vec3(-3, 0, -0.06f), vec3(1, 0, 0), 6, h) && (h.ud >> 16) == W_ID[1];
      check(!coil && !gap && post, std::string("ray through the coil ") + (coil ? "BLOCKED" : "passes") + ", between fence posts " + (gap ? "BLOCKED" : "passes") + ", at a post " + (post ? "hits the post" : "MISSES"));
      return true; });
    push("wire: a blast cuts only the stretches it reaches", [this](App& a, float t) {
      if (t == 0) {
        Instance* i = a.doc.find(W_ID[1]); auto& r = a.rt[W_ID[1]];
        vec3 end = i->pos + vec3(0, 0.3f, -2.2f);   // the -z end of the fence
        a.explode(end, vec3(0, -1, 0), 80, 2.0f, 0.0f, ~0ull, WALL_GRENADE);
        int cut = 0; for (auto c : r.secCut) cut += c;
        char b[200]; std::snprintf(b, sizeof b, "grenade-sized blast at one end of the fence cuts %d of %zu stretches (near ones), the rest stand", cut, r.secCut.size());
        check(cut >= 1 && cut < (int)r.secCut.size() && r.secCut[0] && !r.secCut.back(), b);
        std::vector<DrawItem> op, gl; a.buildDrawLists(op, gl);
        int hidden = 0, shown = 0; const GpuModel* gm = a.lib.find("wire_fence")->model.get();
        for (size_t k = 0; k < gm->parts.size(); ++k) { int s = gm->parts[k].section; bool drawn = false; for (auto& m : a.R.worldModels) if (m.model == gm && m.part == (int)k) drawn = true; if (s >= 0 && r.secCut[s]) hidden += !drawn; else shown += drawn; }
        check(hidden > 0 && shown > 0, "cut stretches are no longer drawn (" + std::to_string(hidden) + " parts hidden, " + std::to_string(shown) + " still drawn)");
        bool through = Physics::shapeFitsAt(Physics::STAND, i->pos + vec3(0, a.terrain.height(i->pos.x, i->pos.z - 2.0f) - i->pos.y, -2.0f));
        bool blocked = !Physics::shapeFitsAt(Physics::STAND, i->pos + vec3(0, a.terrain.height(i->pos.x, i->pos.z + 2.0f) - i->pos.y, 2.0f));
        check(through && blocked, std::string("the cut stretch can be walked through (") + (through ? "yes" : "NO") + "), the standing one still blocks (" + (blocked ? "yes" : "NO") + ")");
        summary["wire"]["blastCut"] = cut;
        return false;
      }
      if (t < 0.05f) { feetAt(a, vec3(-3.0f, 0, -9.0f)); a.eyeYaw = yawToward(vec3(3.0f, 0, -1.0f)); a.eyePitch = -15; }
      if (t < 0.5f) return false;
      shotQueue.push_back("entities_wire_after_blast");
      return t > 0.7f; });
    push("wire: small arms do not cut wire", [this](App& a, float) {
      auto& r = a.rt[W_ID[0]]; float hp0 = 0; for (float h : r.secHp) hp0 += h;
      for (int k = 0; k < 100; ++k) a.defenseBulletHit(W_ID[0], a.doc.find(W_ID[0])->pos);
      float hp1 = 0; for (float h : r.secHp) hp1 += h;
      check(hp1 == hp0 && !r.defDestroyed, "100 rifle hits leave the coil uncut");
      return true; });
    push("wire: traversal 'slow' mode (data option)", [this](App& a, float t) {
      static float vFree, vWire; static vec3 p0;
      AssetDef* as = const_cast<AssetDef*>(a.lib.find("wire_concertina"));
      Instance* i = a.doc.find(W_ID[0]);
      if (t == 0) { as->defense.traversal = "slow"; i->rev++; a.syncRuntime(); feetAt(a, i->pos + vec3(-3.0f, 0, 0.3f)); a.eyeYaw = yawPlusX(); a.scriptedInput = ExploreInput{}; vFree = vWire = 0; return false; }
      a.scriptedInput.move = vec2(0, 1);
      float z = -(Physics::character().pos.x - i->pos.x);   // distance still to go across (> 0 before, < 0 after)
      if (t > 0.6f && z > 0.8f) vFree = std::max(vFree, a.vm.speed);
      if (std::fabs(z) < 0.15f) vWire = std::max(vWire, a.vm.speed);
      if (t < 8.0f && z > -1.2f) return false;
      a.scriptedInput = ExploreInput{};
      char b[200]; std::snprintf(b, sizeof b, "traversal \"slow\": crosses the coil at %.2f m/s against %.2f m/s outside (factor %.2f); default \"block\"", vWire, vFree, as->defense.slowFactor);
      check(z < -1.0f && vWire > 0 && vWire < vFree * 0.5f, b);
      as->defense.traversal = "block"; i->rev++; a.syncRuntime();
      return true; });
    push("wire: screenshot", [this](App& a, float t) {
      if (t == 0) { feetAt(a, vec3(-7.0f, 0, -3.5f)); a.eyeYaw = yawToward(vec3(3.5f, 0, -6.5f)); a.eyePitch = -12; a.scriptedInput = ExploreInput{}; a.selectWeapon(a.weaponIndex("m1_garand")); }
      if (t > 0.6f && t < 0.62f) shotQueue.push_back("entities_wire");
      return t > 0.8f; });
    push("reset", [this](App& a, float) { a.exitExplore(); check(a.mode == Mode::Creative, "back to Creative"); return true; });

    // ================================================================ E2 tank trap x1.25
    push("tank trap: 1.25x larger, collider and bounds match", [this](App& a, float) {
      const AssetDef* tt = a.lib.find("def_tank_trap");
      float h = tt->bmax.y - tt->bmin.y, w = tt->bmax.x - tt->bmin.x;
      const Part* hull = nullptr; for (auto& p : tt->parts) if (!p.visible && p.collide) hull = &p;
      char b[240]; std::snprintf(b, sizeof b, "def_tank_trap v%d: %.2f m wide x %.2f m high (v1: 1.21 x 0.87 m, factor %.2f), collision hull %.2f x %.2f m, footprint %.2f m",
                                 tt->version, w, h, h / 0.868f, hull ? hull->size.x : 0, hull ? hull->size.y : 0, tt->footprint.x);
      check(tt->version == 2 && std::fabs(h / 0.868f - 1.25f) < 0.02f && hull && std::fabs(hull->size.x - 1.375f) < 0.01f && std::fabs(hull->size.y - h) < 0.02f && tt->footprint.x == 1.75f, b);
      summary["tankTrap"] = {{"height", h}, {"width", w}, {"factor", h / 0.868f}};
      return true; });
    push("tank trap: a version-1 map adopts the new size once", [this](App& a, float) {
      fs::path p = outDirE() / "entities_trap_v1.nmap.json";
      json m; try { m = json::parse(readTextFile(fs::path(contentDir() / "examples" / "village.nmap.json"))); } catch (...) {}
      json keep = json::array(); int v1 = 0;
      for (auto& o : m["instances"]) if (o["asset"].value("id", "") == "def_tank_trap") { o["asset"]["version"] = 1; keep.push_back(o); ++v1; }
      m["instances"] = keep; m.erase("terrainDeltas");
      { std::ofstream f(p); f << m.dump(); }
      bool ok = a.openMap(p);
      auto height = [&](const Instance& i) { Physics::Hit h; vec3 top = i.pos + vec3(0, 5, 0); return Physics::castRayEx(top, vec3(0, -1, 0), 10, h) ? h.pos.y - i.pos.y : -1.0f; };
      a.syncRuntime();
      int up = 0; float h1 = 0; vec3 p1(0);
      for (auto& i : a.doc.instances) if (i.assetId == "def_tank_trap") { up += i.assetVersion == 2; h1 = height(i); p1 = i.pos; }
      bool saved = a.saveMapTo(p), re = a.openMap(p); a.syncRuntime();
      float h2 = 0; vec3 p2(0); int again = 0;
      for (auto& i : a.doc.instances) if (i.assetId == "def_tank_trap") { again += i.assetVersion == 2; h2 = height(i); p2 = i.pos; }
      char b[260]; std::snprintf(b, sizeof b, "village traps saved as v1 (%d): loaded as v2 (%d), top of the hull %.2f m above the pivot, position kept; saved and reloaded: %.2f m (no second growth)", v1, up, h1, h2);
      check(ok && v1 > 0 && up == v1 && std::fabs(h1 - 1.086f) < 0.03f && saved && re && again == v1 && std::fabs(h2 - h1) < 1e-3f && glm::length(p2 - p1) < 1e-4f, b);
      uint64_t n = a.placeInstance(a.lib.find("def_tank_trap"), p1 + vec3(5, 0, 0), 0.0f);
      Instance* ni = a.doc.find(n);
      check(ni && ni->assetVersion == 2, "a new placement is version 2");
      a.undo(); check(!a.doc.find(n), "undo removes the new placement");
      return true; });

    // ================================================================ E3 grenades (and the E5 knife, same equipment path)
    static uint64_t G_HOUSE = 0, G_COIL = 0;
    push("grenades: Mk 2 (US) and M24 (DE) in the roster", [this](App& a, float) {
      int mk = a.weaponIndex("mk2_grenade"), m24 = a.weaponIndex("m24_grenade"), kn = a.weaponIndex("knife_bayonet");
      bool ok = mk >= 0 && m24 >= 0 && kn >= 0;
      if (ok) {
        const WeaponDef &A = *a.weaponDefs[mk], &B = *a.weaponDefs[m24], &K = *a.weaponDefs[kn];
        char b[300]; std::snprintf(b, sizeof b, "Mk 2 side %s, %s model %d tris, fuse %.1f s, throw %.1f m/s; M24 side %s, model %d tris, throw %.1f m/s; knife model %d tris, reach %.1f m",
                                   A.side.c_str(), A.useModel() ? "supplied" : "NO", A.useModel() ? A.model->tris : 0, A.grenade.fuse, A.grenade.speed, B.side.c_str(), B.useModel() ? B.model->tris : 0, B.grenade.speed, K.useModel() ? K.model->tris : 0, K.melee.reach);
        check(A.side == "US" && B.side == "DE" && A.useModel() && B.useModel() && K.useModel() && A.grenade.on && B.grenade.on && K.melee.on && B.grenade.speed > A.grenade.speed, b);
      } else check(false, "grenade / knife definitions missing");
      const TemplateInfo* t = a.findTemplate("blank_slate");
      a.newMap(*t, "Grenade checks");
      G_HOUSE = a.placeInstance(a.lib.find("house_farmhouse_2story"), vec3(30, 0, 0), 0.0f);
      G_COIL = a.placeInstance(a.lib.find("wire_concertina"), vec3(-20, 0, -14), 90.0f);
      a.doc.start = {true, vec3(0, a.terrain.height(0, 0), 0), 0};
      check(G_HOUSE && G_COIL && a.enterExplore(), "fixtures: farmhouse, concertina coil; F5 into Explore");
      a.useScripted = true;
      int carriedMk = 0; for (int c : a.carried) carriedMk += c == mk || c == m24 || c == kn;
      check(carriedMk == 3, "the sandbox loadout carries both grenades and the knife alongside the firearms (" + std::to_string(a.carried.size()) + " items)");
      auto& s = a.wstate["mk2_grenade"];
      check(s.loaded + s.reserveRounds() == 4, "Mk 2: 4 grenades (1 in hand + 3), counted like ammunition (" + std::to_string(s.loaded) + " + " + std::to_string(s.reserveRounds()) + ")");
      return true; });
    auto throwStep = [this, &push](const std::string& id, const std::string& tag, int pose) {
      push("grenade: throw " + id + " " + tag, [this, id, tag, pose](App& a, float t) {
        static int thrown0, exploded0; static float tRel, rest = -1, vMin; static vec3 relPos, restPos; static bool once;
        if (t == 0) {
          feetAt(a, vec3(0, 0, 0)); a.eyeYaw = yawToward(vec3(0, 0, -1)); a.eyePitch = 8; a.scriptedInput = ExploreInput{};
          a.selectWeapon(a.weaponIndex(id)); thrown0 = a.grenadesThrown; exploded0 = a.grenadesExploded; tRel = -1; rest = -1; vMin = 99; once = false;
          a.scriptedInput.crouch = pose == 1; a.proneToggle = pose == 2;
          return false;
        }
        if (t < 1.8f) return false;   // posture settles
        if (tRel < 0) {
          a.scriptedInput.fire = true;   // held: one throw per press
          if (a.grenadesThrown > thrown0) { tRel = t; relPos = a.lastGrenadeSpawn; }
          return t > 6;
        }
        a.scriptedInput.fire = t - tRel < 1.5f;   // keep holding for a while: no second throw
        if (!a.grenades.empty()) { float v = glm::length(Physics::velocity(a.grenades[0].body)); if (t - tRel > 2.0f) vMin = std::min(vMin, v); restPos = a.grenades[0].pos; }
        if (a.grenadesExploded == exploded0) return t - tRel > 8;
        float fuseSeen = t - tRel;
        const WeaponDef& d = *a.weaponDefs[a.weaponIndex(id)];
        float dist = glm::length(vec2(a.lastGrenadeBlast.x, a.lastGrenadeBlast.z));
        char b[300]; std::snprintf(b, sizeof b, "%s %s: one grenade per press (%d thrown), went off once %.2f s after release (fuse %.1f s), %.1f m away, resting before it went off (%.2f m/s)",
                                   id.c_str(), tag.c_str(), a.grenadesThrown - thrown0, fuseSeen, d.grenade.fuse, dist, vMin);
        check(a.grenadesThrown - thrown0 == 1 && a.grenadesExploded - exploded0 == 1 && std::fabs(fuseSeen - d.grenade.fuse) < 0.05f && a.grenades.empty() && vMin < 0.3f && dist > 3.0f, b);
        summary["grenade"][id + "_" + tag] = {{"distance", dist}, {"fuse", fuseSeen}};
        a.scriptedInput = ExploreInput{}; a.proneToggle = false;
        return true; });
    };
    throwStep("mk2_grenade", "standing", 0);
    throwStep("m24_grenade", "standing", 0);
    throwStep("mk2_grenade", "crouched", 1);
    throwStep("mk2_grenade", "prone", 2);
    push("grenade: handling differs, posture shortens the throw", [this](App& a, float t) {
      if (t < 1.2f) return false;   // stand up again
      auto g = [&](const char* k) { return summary["grenade"].contains(k) ? summary["grenade"][k].value("distance", 0.0f) : 0.0f; };
      float mk = g("mk2_grenade_standing"), m24 = g("m24_grenade_standing"), cr = g("mk2_grenade_crouched"), pr = g("mk2_grenade_prone");
      char b[200]; std::snprintf(b, sizeof b, "thrown distance: Mk 2 %.1f m, M24 %.1f m (stick carries farther); Mk 2 crouched %.1f m, prone %.1f m", mk, m24, cr, pr);
      check(m24 > mk && pr < mk && pr > 2.0f, b);
      return true; });
    push("grenade: count goes down, the pouch refills the hand", [this](App& a, float) {
      auto& s = a.wstate["mk2_grenade"];
      check(s.loaded + s.reserveRounds() == 1, "Mk 2: 4 -> 1 left after three throws (" + std::to_string(s.loaded) + " + " + std::to_string(s.reserveRounds()) + ")");
      return true; });
    push("grenade: a swap during the wind-up cancels the throw", [this](App& a, float t) {
      static int thrown0, left0;
      if (t == 0) { a.selectWeapon(a.weaponIndex("m24_grenade")); a.scriptedInput = ExploreInput{}; return false; }
      auto& s = a.wstate["m24_grenade"];
      if (t < 1.2f) { thrown0 = a.grenadesThrown; left0 = s.loaded + s.reserveRounds(); return false; }
      if (t < 1.25f) { a.scriptedInput.fire = true; return false; }
      if (s.throwT >= 0 && t < 1.4f) { a.scriptedInput.fire = false; a.selectWeapon(a.weaponIndex("m1_garand")); return false; }
      if (t < 2.5f) return false;
      check(a.grenadesThrown == thrown0 && s.loaded + s.reserveRounds() == left0 && s.throwT < 0, "M24 wind-up interrupted by a weapon swap: nothing thrown, count unchanged (" + std::to_string(left0) + ")");
      return true; });
    push("grenade: H throws from the rifle and returns to it", [this](App& a, float t) {
      static int thrown0;
      if (t == 0) { a.selectWeapon(a.weaponIndex("m1_garand")); feetAt(a, vec3(0, 0, 0)); a.eyeYaw = yawToward(vec3(0, 0, -1)); a.eyePitch = 5; thrown0 = a.grenadesThrown; return false; }
      if (t < 0.5f) return false;
      if (t < 0.52f) { a.scriptedInput.quickGrenade = true; return false; }
      if (t < 2.2f) return false;
      const WeaponDef& d = *a.weaponDefs[a.activeWeapon];
      check(a.grenadesThrown == thrown0 + 1 && d.id == "m1_garand", "quick throw (H) from the M1 Garand: Mk 2 thrown (" + std::to_string(a.grenadesThrown - thrown0) + "), back on " + d.id);
      return true; });
    push("grenade: wait for it", [](App& a, float t) { return a.grenades.empty() || t > 6; });
    push("grenade: thrown at a wall from 0.3 m stays on the near side", [this](App& a, float t) {
      static vec3 wallP, n; static int thrown0, exploded0; static bool found;
      Instance* h = a.doc.find(G_HOUSE);
      if (t == 0) {
        found = false;
        for (float x = -3.0f; x <= 3.0f && !found; x += 0.25f) {   // a solid stretch of the front wall (asset z +4)
          vec3 from = h->pos + vec3(x, 0, 4.6f); from.y = a.terrain.height(from.x, from.z) + 1.62f;
          Physics::Hit hit; if (Physics::castRayEx(from, vec3(0, 0, -1), 1.0f, hit) && (hit.ud & 0xFFFF) == 0xFFFF && (hit.ud >> 16) == G_HOUSE) { wallP = hit.pos; n = hit.normal; found = true; }
        }
        if (!found) { check(false, "no solid front-wall stretch found"); return true; }
        feetAt(a, wallP + n * 0.3f - vec3(0, 1.62f, 0)); a.eyeYaw = yawToward(-n); a.eyePitch = 0;
        a.selectWeapon(a.weaponIndex("m24_grenade")); a.wstate["m24_grenade"].containers.push_back(1);
        thrown0 = a.grenadesThrown; exploded0 = a.grenadesExploded; a.scriptedInput = ExploreInput{};
        return false;
      }
      if (t < 1.0f) return false;
      a.scriptedInput.fire = t < 1.1f;
      bool inside = false;
      for (auto& g : a.grenades) if (glm::dot(g.pos - wallP, n) < -0.02f) inside = true;
      if (inside) { char b[200]; std::snprintf(b, sizeof b, "grenade passed into / through the wall (spawned %.2f m in front, now %.2f)", glm::dot(a.lastGrenadeSpawn - wallP, n), glm::dot(a.grenades[0].pos - wallP, n)); check(false, b); return true; }
      if (a.grenadesExploded == exploded0) return t > 9;
      float side = glm::dot(a.lastGrenadeBlast - wallP, n), spawn = glm::dot(a.lastGrenadeSpawn - wallP, n);
      char b[200]; std::snprintf(b, sizeof b, "face-to-wall throw: spawned %.2f m in front of the wall (wanted %.2f), went off %.2f m in front of it, never inside", spawn, glm::dot(a.lastGrenadeWant - wallP, n), side);
      check(a.grenadesThrown == thrown0 + 1 && spawn > 0.0f && side > 0.0f, b);
      return true; });
    push("grenade: blast effects are grenade-sized", [this](App& a, float) {
      Instance* c = a.doc.find(G_COIL); auto& r = a.rt[G_COIL];
      vec3 p = c->pos + vec3(0, 0.1f, 0); float h0 = a.terrain.height(p.x + 3, p.z), hc0 = a.terrain.height(p.x, p.z);
      int cut0 = 0; for (auto x : r.secCut) cut0 += x;
      const WeaponDef& d = *a.weaponDefs[a.weaponIndex("mk2_grenade")];
      // the same detonation a thrown Mk 2 makes, 3 m from the coil and on open ground
      a.explode(p, vec3(0, -1, 0), d.grenade.damage, d.grenade.radius, d.grenade.crater, ~0ull, WALL_GRENADE);
      int cut1 = 0; for (auto x : r.secCut) cut1 += x;
      float depth = hc0 - a.terrain.height(p.x, p.z), dh = a.terrain.height(p.x + 3, p.z) - h0;
      Instance* h = a.doc.find(G_HOUSE); auto& hr = a.rt[G_HOUSE];
      vec3 wallHit = h->pos + vec3(0.0f, 1.0f, 4.15f);
      // structural damage where it lands: the Bazooka's stamp radius (0.29 m), up to 0.36 m deep (docs/destruction.md)
      auto damagedCells = [&](float& maxRm) { int n = 0; maxRm = 0; for (auto& w : hr.wf) for (size_t q = 0; q < w.rf.size(); ++q) { float rm = w.rf[q] + w.rb[q]; n += rm > 0.02f; maxRm = std::max(maxRm, rm); } return n; };
      float rm0, rm1; int n0 = damagedCells(rm0);
      auto ex = a.explode(wallHit, vec3(0, 0, -1), d.grenade.damage, d.grenade.radius, d.grenade.crater, ~0ull, WALL_GRENADE);
      int n1 = damagedCells(rm1);
      float area = (n1 - n0) * a.destruction.cell * a.destruction.cell;
      char b[360]; std::snprintf(b, sizeof b, "Mk 2 blast: cuts %d coil stretch(es) on the near side, crater %.2f m deep at the centre, ground 3 m away moved %.3f m; against the farmhouse wall: %d bay hit(s), %.2f m^2 broken up to %.2f m deep, breached %s (structural damage, Bazooka-sized stamp)",
                                 cut1 - cut0, depth, dh, (int)ex.hits.size(), area, rm1, ex.breached ? "yes" : "no");
      check(cut1 > cut0 && depth > 0.02f && depth < 0.25f && !ex.hits.empty() && area > 0.1f && area < 0.7f && rm1 > 0.2f, b);
      summary["grenade"]["craterDepth"] = depth;
      (void)hr;
      return true; });
    // ---- knife (E5)
    push("knife: strikes glass within reach, one hit per target, walls stop it", [this](App& a, float t) {
      static vec3 stand; static int swings0, hits0;
      Instance* h = a.doc.find(G_HOUSE); auto& r = a.rt[G_HOUSE];
      const AssetDef* as = r.asset; int k = as->findInteract("kitchen_casement");
      if (t == 0) {
        if (k < 0) { check(false, "farmhouse kitchen casement not found"); return true; }
        const InteractDef& w = as->interactables[k];
        vec3 c = (w.apMin + w.apMax) * 0.5f;
        stand = h->pos + vec3(c.x, 0, w.apMax.z + 0.9f);
        feetAt(a, stand); a.eyeYaw = yawToward(vec3(0, 0, -1)); a.eyePitch = glm::degrees(std::atan2((h->pos.y + c.y) - (a.terrain.height(stand.x, stand.z) + 1.62f), 0.95f));
        a.selectWeapon(a.weaponIndex("knife_bayonet")); a.scriptedInput = ExploreInput{};
        swings0 = a.meleeSwings; hits0 = a.meleeHitCount;
        return false;
      }
      if (t < 0.8f) return false;
      a.scriptedInput.fire = t < 1.3f;   // held: one swing
      if (t < 2.0f) return false;
      a.scriptedInput.fire = false;
      bool broken = false; for (auto st : r.leafSt[k]) broken |= (st & 1) != 0;
      int swings = a.meleeSwings - swings0, hits = a.meleeHitCount - hits0;
      char b[200]; std::snprintf(b, sizeof b, "knife at the kitchen window from 0.9 m: %d swing for a held button, %d target hit(s) of %d rays, glass broken: %s",
                                 swings, hits, a.weaponDefs[a.activeWeapon]->melee.rays, broken ? "yes" : "NO");
      check(swings == 1 && hits >= 1 && hits < a.weaponDefs[a.activeWeapon]->melee.rays && broken, b);
      return true; });
    push("knife: nothing beyond its reach", [this](App& a, float t) {
      static int hits0;
      if (t == 0) { feetAt(a, vec3(0, 0, 0)); a.eyeYaw = yawToward(vec3(0, 0, -1)); a.eyePitch = 0; hits0 = a.meleeHitCount; return false; }
      a.scriptedInput.fire = t > 0.5f && t < 0.6f;
      if (t < 1.4f) return false;
      check(a.meleeHitCount == hits0, "a swing at open air (nothing within 1.7 m) hits nothing");
      return true; });
    push("equipment: screenshot", [this](App& a, float t) {
      if (t == 0) { a.selectWeapon(a.weaponIndex("mk2_grenade")); a.wstate["mk2_grenade"].loaded = 1; feetAt(a, vec3(0, 0, 0)); a.eyeYaw = yawToward(vec3(0, 0, -1)); a.eyePitch = -5; a.scriptedInput = ExploreInput{}; }
      if (t > 0.6f && t < 0.62f) shotQueue.push_back("entities_grenade_held");
      if (t > 0.7f && t < 0.72f) a.selectWeapon(a.weaponIndex("knife_bayonet"));
      if (t > 1.3f && t < 1.32f) shotQueue.push_back("entities_knife_held");
      return t > 1.5f; });
    push("reset 2", [this](App& a, float) { a.exitExplore(); check(a.mode == Mode::Creative && a.grenades.empty(), "back to Creative, no live grenades left"); return true; });

    // ================================================================ E4 path entities
    static uint64_t P_ID[3] = {0, 0, 0};
    static const char* kPaths[3] = {"path_gravel", "path_dirt", "path_concrete"};
    // largest gap between the draped surface and the ground under it (+ above, - below), sampled on the mesh grid
    auto drapeGap = [](App& a, uint64_t id, float& lo, float& hi) {
      Instance* i = a.doc.find(id); const AssetDef* as = a.assetFor(*i);
      MeshData m; extern void buildPathMeshData(MeshData&, const PathDef&, vec2, vec3, float, const std::function<float(float, float)>&, const std::vector<uint8_t>*);
      buildPathMeshData(m, as->path, a.pathSize(*i, as), i->pos, i->yaw, [&a](float x, float z) { return a.terrain.height(x, z); }, nullptr);
      lo = 1e9f; hi = -1e9f;
      for (auto& v : m.v) { vec3 w = i->pos + yawQuat(i->yaw) * vec3(v.pos.x, 0, v.pos.z); float g = v.pos.y + i->pos.y - a.terrain.height(w.x, w.z); lo = std::min(lo, g); hi = std::max(hi, g); }
    };
    push("paths: three editable path entities", [this](App& a, float) {
      std::set<std::string> kinds; int n = 0;
      for (auto& as : a.lib.assets) if (as->path.on) { ++n; kinds.insert(as->path.kind); check(as->thumbnail != 0 && as->category == "Paths", as->id + ": " + as->name + " (" + as->path.kind + "), thumbnail, default " + std::to_string(as->path.width).substr(0, 4) + " x " + std::to_string(as->path.length).substr(0, 4) + " m"); }
      check(n == 3 && kinds.size() == 3, "gravel, dirt and concrete are three separate entities (" + std::to_string(n) + ")");
      const TemplateInfo* t = a.findTemplate("rolling_bocage") ? a.findTemplate("rolling_bocage") : a.findTemplate("blank_slate");
      a.newMap(*t, "Path checks");
      for (int k = 0; k < 3; ++k) P_ID[k] = a.placeInstance(a.lib.find(kPaths[k]), vec3(-8.0f + 8.0f * k, 0, 20), 10.0f * k);
      check(P_ID[0] && P_ID[1] && P_ID[2], "placed: gravel, dirt, concrete");
      return true; });
    push("paths: size per instance, undo, save / load", [this, drapeGap](App& a, float) {
      Instance* g = a.doc.find(P_ID[0]);
      a.selected = P_ID[0]; a.pushUndo(); a.setPathSize(*a.doc.find(P_ID[0]), vec2(2.6f, 14.0f));
      g = a.doc.find(P_ID[0]); vec2 wl = a.pathSize(*g, a.assetFor(*g));
      a.undo(); g = a.doc.find(P_ID[0]); vec2 back = a.pathSize(*g, a.assetFor(*g));
      a.redo(); g = a.doc.find(P_ID[0]); vec2 again = a.pathSize(*g, a.assetFor(*g));
      check(std::fabs(wl.y - 14) < 1e-4f && std::fabs(back.y - 8) < 1e-4f && std::fabs(again.y - 14) < 1e-4f, "gravel resized to 2.6 x 14 m; undo -> 8 m, redo -> 14 m");
      a.setPathSize(*a.doc.find(P_ID[0]), vec2(50, 0.1f)); g = a.doc.find(P_ID[0]); vec2 cl = a.pathSize(*g, a.assetFor(*g));
      check(cl.x == 8.0f && cl.y == 1.0f, "sizes clamp to the asset limits (" + std::to_string(cl.x).substr(0, 4) + " x " + std::to_string(cl.y).substr(0, 4) + ")");
      a.setPathSize(*a.doc.find(P_ID[0]), vec2(2.6f, 14.0f));
      fs::path p = outDirE() / "entities_paths.nmap.json";
      bool ok = a.saveMapTo(p) && a.openMap(p);
      int found = 0; float L = 0;
      for (auto& i : a.doc.instances) for (int k = 0; k < 3; ++k) if (i.assetId == kPaths[k]) { ++found; P_ID[k] = i.id; if (k == 0) L = a.pathSize(i, a.assetFor(i)).y; }
      check(ok && found == 3 && std::fabs(L - 14) < 1e-4f, "save / load keeps the three paths and the 14 m length");
      float lo, hi; drapeGap(a, P_ID[0], lo, hi);
      char b[160]; std::snprintf(b, sizeof b, "draped on the ground: surface %.3f..%.3f m above it everywhere (no floating edges, no buried patches)", lo, hi);
      check(lo > 0.005f && hi < 0.06f, b);
      return true; });
    push("paths: grass cleared inside, verges keep theirs", [this](App& a, float) {
      Instance* i = a.doc.find(P_ID[1]); auto fp = a.grassFootprints(); vec2 wl = a.pathSize(*i, a.assetFor(*i));
      int inside = 0, outside = 0, outsideTotal = 0;
      for (int k = -4; k <= 4; ++k) {
        vec3 c = i->pos + yawQuat(i->yaw) * vec3(0.0f, 0, k * 1.0f); inside += a.grass.density(c.x, c.z, a.terrain, fp);
        vec3 o = i->pos + yawQuat(i->yaw) * vec3(wl.x * 0.5f + 0.6f, 0, k * 1.0f); outside += a.grass.density(o.x, o.z, a.terrain, fp); ++outsideTotal;
      }
      check(inside == 0 && outside > outsideTotal / 2, "dirt track: no grass on the track (" + std::to_string(inside) + " of 9), grass 0.6 m beyond the edge (" + std::to_string(outside) + " of 9)");
      return true; });
    push("paths: end-to-end snapping", [this](App& a, float) {
      Instance* g = a.doc.find(P_ID[0]); vec2 wl = a.pathSize(*g, a.assetFor(*g));
      vec3 end = g->pos + yawQuat(g->yaw) * vec3(0, 0, wl.y * 0.5f);
      vec3 hp = end + vec3(0.5f, 0, 0.6f); float yaw = 77;
      bool s = a.snapPath(a.lib.find("path_gravel"), hp, yaw);
      vec3 want = end + yawQuat(g->yaw) * vec3(0, 0, a.lib.find("path_gravel")->path.length * 0.5f);
      char b[160]; std::snprintf(b, sizeof b, "a new gravel path dropped 0.8 m off the end snaps in line: offset %.3f m, yaw %.1f (placed path %.1f)", glm::length(vec2(hp.x - want.x, hp.z - want.z)), yaw, g->yaw);
      check(s && glm::length(vec2(hp.x - want.x, hp.z - want.z)) < 1e-3f && std::fabs(yaw - g->yaw) < 1e-3f, b);
      return true; });
    push("paths: craters re-drape them; concrete breaks by the slab", [this, drapeGap](App& a, float t) {
      if (t == 0) { a.doc.start = {true, a.doc.find(P_ID[0])->pos + vec3(0, 0, -12), 0}; check(a.enterExplore(), "F5 into Explore"); a.useScripted = true; return false; }
      if (t < 0.3f) return false;
      Instance* g = a.doc.find(P_ID[0]);
      vec3 c = g->pos; float before = a.terrain.height(c.x, c.z);
      a.explode(vec3(c.x, before, c.z), vec3(0, -1, 0), 120, 2.2f, 1.0f, ~0ull, WALL_HE);
      float after = a.terrain.height(c.x, c.z);
      std::vector<DrawItem> op, gl; a.buildDrawLists(op, gl);
      float lo, hi; drapeGap(a, P_ID[0], lo, hi);
      auto& rg = a.rt[P_ID[0]];
      char b[220]; std::snprintf(b, sizeof b, "HE on the gravel path: ground %.2f m lower at the centre, path re-draped (rev %u = ground %u), surface %.3f..%.3f m above the new ground (follows the crater, never restores it)", before - after, rg.pathGround, a.groundRev, lo, hi);
      check(before - after > 0.3f && rg.pathGround == a.groundRev && lo > 0.005f && hi < 0.06f, b);
      Instance* cc = a.doc.find(P_ID[2]); auto& rc = a.rt[P_ID[2]]; vec2 wl = a.pathSize(*cc, a.assetFor(*cc));
      vec3 slab0 = cc->pos + yawQuat(cc->yaw) * vec3(0, 0, -wl.y * 0.5f + 1.0f);
      a.explode(vec3(slab0.x, a.terrain.height(slab0.x, slab0.z) + 0.05f, slab0.z), vec3(0, -1, 0), 120, 2.2f, 1.0f, ~0ull, WALL_HE);
      int cut = 0; for (auto s : rc.slabCut) cut += s;
      vec3 farEnd = cc->pos + yawQuat(cc->yaw) * vec3(0, 0, wl.y * 0.5f - 0.5f);
      std::string sNear = a.pathSurfaceAt(slab0), sFar = a.pathSurfaceAt(farEnd);
      std::snprintf(b, sizeof b, "HE on the concrete path's first slab: %d of %zu slabs broken (the ground shows there: \"%s\"), the far slab stands (\"%s\")", cut, rc.slabCut.size(), sNear.c_str(), sFar.c_str());
      check(cut >= 1 && cut < (int)rc.slabCut.size() && rc.slabCut[0] && !rc.slabCut.back() && sNear.empty() && sFar == "concrete", b);
      a.buildDrawLists(op, gl);
      return true; });
    push("paths: walked across freely, footsteps by surface", [this](App& a, float t) {
      static float x0;
      Instance* d = a.doc.find(P_ID[1]);
      if (t == 0) { feetAt(a, d->pos + yawQuat(d->yaw) * vec3(-3.0f, 0, 1.0f)); a.eyeYaw = yawToward(yawQuat(d->yaw) * vec3(1, 0, 0)); a.scriptedInput = ExploreInput{}; x0 = 0; return false; }
      a.scriptedInput.move = vec2(0, 1);
      if (t < 2.5f) return false;
      a.scriptedInput = ExploreInput{};
      vec3 lp = glm::inverse(yawQuat(d->yaw)) * (Physics::character().pos - d->pos);
      std::string s0 = a.pathSurfaceAt(a.doc.find(P_ID[0])->pos + vec3(0, 0, 3)), s1 = a.pathSurfaceAt(d->pos), s2 = a.pathSurfaceAt(d->pos + vec3(30, 0, 0));
      check(lp.x > 2.0f, "walked straight across the dirt track (no step, no collider): now " + std::to_string(lp.x).substr(0, 4) + " m past its centre");
      check(s0 == "gravel" && s1 == "dirt" && s2.empty(), "surface lookup for footsteps / impacts: gravel, dirt, nothing off the path");
      return true; });
    push("paths: screenshot", [this](App& a, float t) {
      Instance* d = a.doc.find(P_ID[1]);
      if (t == 0) { feetAt(a, d->pos + vec3(-2.0f, 0, -9.0f)); a.eyeYaw = yawToward(vec3(0.3f, 0, 1)); a.eyePitch = -22; a.selectWeapon(a.weaponIndex("knife_bayonet")); }
      if (t > 0.8f && t < 0.82f) shotQueue.push_back("entities_paths");
      return t > 1.0f; });
    push("reset 3", [this](App& a, float) { a.exitExplore(); check(a.mode == Mode::Creative, "back to Creative"); return true; });

    // ================================================================ E5 Flak 18 emplacement
    static uint64_t F_ID = 0;
    push("flak: supplied model split onto the station transforms", [this](App& a, float) {
      const AssetDef* f = a.lib.find("gun_flak18");
      if (!f || !f->model) { check(false, "gun_flak18 or its model missing"); return true; }
      int cnt[4] = {0, 0, 0, 0}; for (int r : f->station.modelRole) if (r >= 0 && r < 4) ++cnt[r];
      char b[260]; std::snprintf(b, sizeof b, "Flak 18: %d tris, parts base %d / traverse %d / elevation %d / recoil %d; fixed emplacement %s, %s, traverse %.0f..%.0f, elevation %.0f..%.0f deg",
                                 f->model->tris, cnt[0], cnt[1], cnt[2], cnt[3], f->station.fixed ? "yes" : "NO", f->faction.c_str(), f->station.yawMin, f->station.yawMax, f->station.elevMin, f->station.elevMax);
      check(cnt[0] > 0 && cnt[1] > 0 && cnt[2] > 0 && cnt[3] > 0 && cnt[0] + cnt[1] + cnt[2] + cnt[3] == (int)f->model->parts.size() && f->station.fixed && f->faction == "DE" && f->station.elevMax >= 80 && f->thumbnail, b);
      const TemplateInfo* t = a.findTemplate("blank_slate");
      a.newMap(*t, "Flak checks");
      F_ID = a.placeInstance(f, vec3(0, 0, -25), 0.0f);
      vec3 g = f->station.gunner; a.doc.start = {true, vec3(g.x, 0, -25 + g.z), 0}; a.doc.start.pos.y = a.terrain.height(a.doc.start.pos.x, a.doc.start.pos.z);
      check(F_ID && a.enterExplore(), "placed; F5 into Explore beside it");
      a.useScripted = true;
      return true; });
    push("flak: crewed from the gunner's position only", [this](App& a, float t) {
      auto& in = a.scriptedInput;
      Instance* i = a.doc.find(F_ID);
      vec3 g = i->pos + vec3(0, 1.2f, -0.5f), e = a.eyePos();
      a.eyeYaw = degrees(std::atan2(-(g.x - e.x), -(g.z - e.z))); a.eyePitch = degrees(std::atan2(g.y - e.y, glm::length(vec2(g.x - e.x, g.z - e.z))));
      static int phase = 0;
      if (t == 0) phase = 0;
      if (t < 0.3f) return false;
      if (phase == 0) {
        check(a.target.valid && a.target.inst == F_ID, "the Flak shows its crew prompt");
        bool moved = a.occupyStation(F_ID, ST_HANDLER);
        check(!moved && a.occupied == 0, "Move is refused: the emplacement is not repositioned");
        in.interact = true; phase = 1; return false;
      }
      if (phase == 1) { if (t < 0.6f) return false; in.choose1 = true; phase = 2; return false; }
      if (t < 1.5f) return false;
      vec3 ce, cf; float fov; bool cam = a.stationCamera(ce, cf, fov);
      check(a.occupied == F_ID && a.station(F_ID)->pos2 == ST_GUNNER && cam && fov < 20, "Aim: gunner at the direct-fire sight (fov " + std::to_string((int)fov) + " deg)");
      return true; });
    push("flak: traverses all round, elevates -3..+85, base stays put", [this](App& a, float t) {
      auto& in = a.scriptedInput; StationRT* s = a.station(F_ID);
      static mat4 base0, top0;
      auto mats = [&](mat4& base, mat4& top) {
        std::vector<DrawItem> op, gl; a.buildDrawLists(op, gl);
        const AssetDef* f = a.lib.find("gun_flak18"); int kb = -1, kt = -1;
        for (size_t k = 0; k < f->station.modelRole.size(); ++k) { if (f->station.modelRole[k] == 0 && kb < 0) kb = (int)k; if (f->station.modelRole[k] == 1 && kt < 0) kt = (int)k; }
        for (auto& m : a.R.worldModels) if (m.model == f->model.get()) { if (m.part == kb) base = m.m; if (m.part == kt) top = m.m; }
      };
      if (t == 0) { mats(base0, top0); in.dyaw = 150.0f - s->yawT; in.dpitch = 200.0f; return false; }
      in.dyaw = in.dpitch = 0;
      if (t < 12.0f && (std::fabs(s->yaw - s->yawT) > 0.05f || std::fabs(s->elev - s->elevT) > 0.05f)) return false;
      mat4 base1, top1; mats(base1, top1);
      float baseMove = 0, topMove = 0; for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) { baseMove = std::max(baseMove, std::fabs(base1[c][r] - base0[c][r])); topMove = std::max(topMove, std::fabs(top1[c][r] - top0[c][r])); }
      char b[220]; std::snprintf(b, sizeof b, "traversed to %.1f deg (past the PaK's 32.5), elevation clamps at %.1f deg; cruciform base unchanged (%.4f), carriage turned (%.2f)", s->yaw, s->elev, baseMove, topMove);
      check(s->yaw > 149 && std::fabs(s->elev - 85) < 0.1f && baseMove < 1e-4f && topMove > 0.5f, b);
      in.dpitch = -200.0f; return true; });
    push("flak: depression limit", [this](App& a, float t) {
      auto& in = a.scriptedInput; StationRT* s = a.station(F_ID);
      in.dpitch = 0;
      if (t < 14.0f && std::fabs(s->elev - s->elevT) > 0.05f) return false;
      check(std::fabs(s->elev + 3) < 0.1f, "depressed to its -3 deg stop (" + std::to_string(s->elev).substr(0, 5) + ")");
      in.dyaw = -s->yawT; in.dpitch = 3.0f - s->elevT;   // back to the front, a little above the horizon
      return true; });
    push("flak: fires HE from the muzzle, recoils, reloads; X selects AP", [this](App& a, float t) {
      auto& in = a.scriptedInput; StationRT* s = a.station(F_ID);
      static int shots0, phase; static float tFire;
      in.dyaw = in.dpitch = 0;
      if (t == 0) { phase = 0; shots0 = a.shotsFired; }
      if (phase == 0) { if (std::fabs(s->yaw - s->yawT) > 0.05f || std::fabs(s->elev - s->elevT) > 0.05f) return t > 14; phase = 1; tFire = t; in.fire = true; return false; }
      if (phase == 1) {
        if (t < tFire + 0.05f) return false;
        in.fire = false;
        const AssetDef* f = a.lib.find("gun_flak18");
        vec3 dir; vec3 mz = a.stationMuzzle(*a.doc.find(F_ID), a.rt[F_ID], dir);
        float off = a.projectiles.empty() ? 99.0f : glm::length(glm::cross(a.projectiles.back().p - mz, dir));
        char b[220]; std::snprintf(b, sizeof b, "shot fired (%d), 8.8 cm HE at %.0f m/s along the bore (%.2f m off the muzzle line), barrel recoiling (%.2f s in)", a.shotsFired - shots0, f->station.heVelocity, off, s->recoilT);
        check(a.shotsFired == shots0 + 1 && s->loaded == 0 && off < 0.05f && s->recoilT < 0.3f, b);
        in.toggleAmmo = true; phase = 2; return false;
      }
      if (phase == 2) { check(s->selAT, "X: next round AP"); in.reload = true; phase = 3; tFire = t; return false; }
      if (t < tFire + a.lib.find("gun_flak18")->station.reloadTime + 0.3f) return false;
      check(s->loaded == 1 && s->loadedAT, "reloaded with AP after " + std::to_string(a.lib.find("gun_flak18")->station.reloadTime).substr(0, 3) + " s");
      shotQueue.push_back("entities_flak_sight");
      return true; });
    push("flak: a direct AT hit wrecks it", [this](App& a, float t) {
      if (t == 0) { a.leaveStation(); a.wreckStation(F_ID, "test direct hit"); }
      if (t < 0.3f) return false;
      auto& r = a.rt[F_ID];
      std::vector<DrawItem> op, gl; a.buildDrawLists(op, gl); int dark = 0;
      for (auto& m : a.R.worldModels) if (m.model == a.lib.find("gun_flak18")->model.get() && m.tint.r < 0.5f) ++dark;
      check(r.st.wrecked && a.isAtGun(r.asset) && dark > 0, "AT-gun rules apply (one-shot direct hit): wrecked, drawn blackened with the barrel slumped (" + std::to_string(dark) + " parts)");
      return true; });
    push("flak: screenshot", [this](App& a, float t) {
      if (t == 0) { feetAt(a, vec3(-7, 0, -18)); a.eyeYaw = yawToward(vec3(7, 0, -7)); a.eyePitch = -8; }
      if (t > 0.5f && t < 0.52f) shotQueue.push_back("entities_flak");
      return t > 0.7f; });

    // ================================================================ A1 audio from the supplied banks
    push("audio: events from the supplied banks", [this](App& a, float) {
      if (a.audio.eventsLoaded == 0) { check(true, "audio: no device or no extracted samples (tools/fsb5.py extract) - procedural sounds only; mapping checks skipped"); return true; }
      std::string missing; std::map<std::string, std::string> firstOf; int shared = 0;
      for (auto& dp : a.weaponDefs) {
        const WeaponDef& d = *dp;
        if (d.equipment() || d.action == "heavy") continue;
        std::string e = "shot_" + d.id;
        if (!a.audio.hasEvent(e)) missing += " " + d.id;
      }
      for (const char* s : {"shot_m2hb", "pak_fire", "flak_fire", "explosion", "explosion_grenade", "explosion_rocket", "explosion_big", "impact_concrete", "impact_dirt", "impact_wood", "impact_metal", "knife_wood"})
        if (!a.audio.hasEvent(s)) missing += std::string(" ") + s;
      // distinct guns never share one recording (M1 / M1C Garand share by design: same rifle)
      json m; try { m = json::parse(readTextFile(contentDir() / "audio" / "manifest.json")); } catch (...) {}
      std::map<std::string, std::string> owner;
      for (auto& [ev, e] : m["events"].items()) {
        if (e.value("kind", "") != "shot" || ev == "shot_m1c_garand") continue;
        for (auto& v : e["variants"]) { std::string key = v.value("bank", "") + "#" + std::to_string(v.value("index", 0)); if (owner.count(key) && owner[key] != ev) ++shared; owner[key] = ev; }
      }
      char b[300]; std::snprintf(b, sizeof b, "%d events / %d samples from the banks; every firearm, launcher and gun station has its own firing event (missing:%s); %d recordings shared between distinct guns",
                                 a.audio.eventsLoaded, a.audio.samplesLoaded, missing.empty() ? " none" : missing.c_str(), shared);
      check(missing.empty() && shared == 0 && a.audio.samplesMissing == 0, b);
      return true; });
    push("audio: automatic fire - one body per round, no immediate repeat, one tail", [this](App& a, float t) {
      static int phase, shots0, tails0, repeats, last; static std::vector<int> seen;
      if (a.audio.eventsLoaded == 0) return true;
      if (t == 0) {
        a.doc.start = {true, vec3(0, a.terrain.height(0, 0), 0), 0};
        if (a.mode != Mode::Explore) { a.enterExplore(); a.useScripted = true; }
        a.selectWeapon(a.weaponIndex("mg42")); a.wstate["mg42"].loaded = 50; a.wstate["mg42"].mode = 0;
        phase = 0; repeats = 0; last = -1; seen.clear(); shots0 = a.audio.eventPlays["shot_mg42"]; tails0 = a.audio.eventPlays["tail_mg42"]; a.scriptedInput = ExploreInput{};
        return false;
      }
      if (t < 0.5f) return false;
      if (phase == 0) {
        a.scriptedInput.fire = true;
        int v = a.audio.eventVariant("shot_mg42");
        if (a.audio.eventPlays["shot_mg42"] - shots0 > (int)seen.size()) { if (v == last) ++repeats; last = v; seen.push_back(v); }
        if (t < 1.5f) return false;
        a.scriptedInput.fire = false; phase = 1; return false;
      }
      if (t < 2.5f) return false;
      int shots = a.audio.eventPlays["shot_mg42"] - shots0, tails = a.audio.eventPlays["tail_mg42"] - tails0;
      std::set<int> distinct(seen.begin(), seen.end());
      char b[240]; std::snprintf(b, sizeof b, "MG 42 1 s burst: %d rounds -> %d shot sounds (%zu variations, %d immediate repeats), %d tail on release", a.shotsFired > 0 ? shots : 0, shots, distinct.size(), repeats, tails);
      check(shots >= 15 && repeats == 0 && distinct.size() >= 4 && tails == 1, b);
      return true; });
    push("audio: reload foley, explosions, far layer", [this](App& a, float t) {
      if (a.audio.eventsLoaded == 0) return true;
      static int phase;
      if (t == 0) { phase = 0; a.selectWeapon(a.weaponIndex("m1_garand")); a.wstate["m1_garand"].loaded = 0; a.scriptedInput = ExploreInput{}; return false; }
      if (phase == 0) { a.scriptedInput.reload = true; phase = 1; return false; }
      if (phase == 1) { if (t < 0.2f) return false; check(a.audio.lastEvent == "reload_m1_garand", "Garand reload start: " + a.audio.lastEvent); phase = 2; return false; }
      auto& s = a.wstate["m1_garand"];
      if (phase == 2) { if (s.reload > 0) return t > 6; check(a.audio.lastEvent == "clip_m1_garand", "Garand clip seats: " + a.audio.lastEvent); phase = 3; return false; }
      vec3 e = a.eyePos();
      a.audio.play("flak_fire", e + vec3(0, 0, -200), 1.0f); std::string far = a.audio.lastEvent;
      a.audio.play("flak_fire", e + vec3(0, 0, -10), 1.0f); std::string near = a.audio.lastEvent;
      a.audio.play("explosion_grenade", e + vec3(0, 0, -15), 0.0f); std::string gr = a.audio.lastEvent;
      a.audio.play("explosion_nonexistent", e, 0.0f); std::string fb = a.audio.lastEvent;
      check(far == "flak_fire_far" && near == "flak_fire" && gr == "explosion_grenade" && fb == "explosion",
            "Flak at 200 m uses its far layer (" + far + "), at 10 m the near one (" + near + "); grenade blast " + gr + "; an unknown variant falls back to " + fb);
      return true; });
    push("reset 5", [this](App& a, float) { if (a.mode == Mode::Explore) a.exitExplore(); check(a.mode == Mode::Creative, "back to Creative"); return true; });
  }
  stage = 1901;
  if (ecur >= eseq.size()) { check(app.R.shaderErrors == 0, "all shaders compiled"); finish(app); return; }
  if (et == 0) logf("[selftest] step: %s", eseq[ecur].name.c_str());
  bool d2 = eseq[ecur].fn(app, et);
  et += dt;
  if (et > 30) { check(false, "step timed out: " + eseq[ecur].name); d2 = true; }
  if (d2) { ++ecur; et = 0; }
}
