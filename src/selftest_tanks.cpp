// --selftest-tanks (stage 2600): Phase 2 tanks end to end in the real Explore loop -- catalog / bake checks, entry through
// the E prompt, Ctrl hatch toggling, rate-limited turret and optics, the HE/AP state machine, the user's damage matrix
// with real projectiles (tank cannon, PaK 40, Flak-class shell, bazooka, Panzerschreck, small arms, splash), wrecks,
// driving and obstacles, the roof MG, safe / refused exits and persistence (Apply, undo, duplicate, save / reopen).
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <functional>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float tcur = 0;
uint64_t SH = 0, TG = 0, PAK = 0, TRAP = 0, TREE = 0, HOUSE = 0, WALLT = 0;
uint64_t HOUSE2 = 0, POLE1 = 0, POLE2 = 0, FENCE = 0, WIREF = 0, SWALL1 = 0, SWALL2 = 0;   // ramming lane
char b_[512]; bool check_ = true; float dt_ = 1.0f / 60.0f;
std::set<long> marks;   // one-shot time marks of the current step (robust to float accumulation of t)
bool once(float t, float mark) { long key = std::lround(mark * 1000.0f); if (t >= mark && !marks.count(key)) { marks.insert(key); return true; } return false; }
float wrap180_(float d) { d = std::fmod(d + 180.0f, 360.0f); if (d < 0) d += 360.0f; return d - 180.0f; }
fs::path outDirT() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
TankRT& T(App& a, uint64_t id) { return *a.tanks.get(id); }
void lookAtP(App& a, vec3 p) { vec3 e = a.eyePos(); a.eyeYaw = degrees(std::atan2(-(p.x - e.x), -(p.z - e.z))); a.eyePitch = degrees(std::atan2(p.y - e.y, glm::length(vec2(p.x - e.x, p.z - e.z)))); }
void aimWorld(App& a, vec3 dir) { a.eyeYaw = wrapDeg(degrees(std::atan2(-dir.x, -dir.z))); a.eyePitch = degrees(std::asin(std::clamp(dir.y, -1.0f, 1.0f))); }
void standAt(App& a, vec3 p) { Physics::setCharacterPos(vec3(p.x, a.terrain.height(p.x, p.z), p.z)); }
void place(App& a, uint64_t id, vec3 p, float yaw) { TankRT& k = T(a, id); k.pos = vec3(p.x, a.terrain.height(p.x, p.z), p.z); k.yaw = yaw; k.speed = 0; k.yawRate = 0; a.tanks.updateBody(a, k, true); }
std::string firstHit(App& a, vec3 o, vec3 d, float len) {
  Physics::Hit h; if (!Physics::castRayEx(o, d, len, h)) return "nothing";
  uint64_t low = h.ud & 0xFFFF, id = h.ud >> 16; char b[200];
  std::string what = h.ud == 0 || (h.ud >> 60) == 0xF ? "terrain" : low == 0xFFFF ? "wall body" : low == 0 ? "static shell" : "leaf";
  if (what == "leaf") { auto it = a.rt.find(id); if (it != a.rt.end() && low - 1 < it->second.asset->interactables.size()) { auto& d2 = it->second.asset->interactables[low - 1]; what += " " + d2.id + " (" + d2.kind + ")"; } }
  std::snprintf(b, sizeof b, "%s at %.2f m", what.c_str(), h.t); return b;
}
// fresh tank state for an isolated damage case (session overlay only)
void fresh(App& a, uint64_t id, float turretYaw = 0) {
  TankRT& t = T(a, id); t.hp = 100; t.wrecked = false; t.hitsFront = t.hitsSide = t.hitsIgnored = 0; t.tYaw = turretYaw; t.tYawV = 0; t.gElev = 0; t.burnT = 0;
  a.tanks.updateBody(a, t, true);
}
// world point / outward normal of a face of the struck component (hull or turret box), in that component's frame
void facePoint(App& a, uint64_t id, bool turret, vec3 localN, vec3 along, vec3& p, vec3& n) {
  TankRT& t = T(a, id); const TankDef& d = *t.def;
  mat4 h, tu, g, rc; a.tanks.rig(t, h, tu, g, rc);
  const TankBox& B = turret ? d.armourTurret : d.armourHull;
  vec3 lp = B.c + localN * B.h + along;
  mat4 M = turret ? tu : h;
  p = vec3(M * vec4(lp, 1)); n = glm::normalize(vec3(M * vec4(localN, 0)));
}
// a real projectile from 14 m out along the face normal (tank / AT / Flak shells are PROJ_HE, launchers PROJ_ROCKET)
void shoot(App& a, vec3 at, vec3 n, int kind, const std::string& weapon, bool ap = true) {
  vec3 from = at + n * 14.0f, dir = -n;
  if (kind == PROJ_ROCKET) {
    const WeaponDef& w = *a.weaponDefs[a.weaponIndex(weapon)];
    a.fireProjectile(from, dir, w.velocity, PROJ_ROCKET, w.rocketDmg, ~0ull, false, w.container);
    Projectile& rp = a.projectiles.back(); rp.blastDmg = w.rocketDmg; rp.blastR = w.rocketR; rp.craterScale = w.rocketCrater; rp.splashGun = w.rocketSplashGun; rp.atKill = true; rp.gravScale = 0;
  } else if (kind == PROJ_HE) {
    a.fireProjectile(from, dir, ap ? 790.0f : 600.0f, PROJ_HE, 120.0f, ~0ull, false, weapon);
    Projectile& rp = a.projectiles.back(); rp.at = ap; rp.atKill = true; rp.gravScale = 0;
  } else {
    a.fireProjectile(from, dir, 820.0f, PROJ_BULLET, 0, ~0ull, false, weapon);
    a.projectiles.back().mass = 0.01f; a.projectiles.back().gravScale = 0;
  }
}
}

void SelfTest::tanksUpdate(App& app, float dt) {
  if (stage == 2600) {
    reportName = "tanks";
    const TemplateInfo* t = app.findTemplate("blank_slate");
    app.newMap(*t, "Tank range");
    // ---------------------------------------------------------------- catalog and bake
    const AssetDef* as = app.lib.find("tank_sherman_firefly"); const AssetDef* at = app.lib.find("tank_tiger_i");
    check(as && at && as->category == "Vehicles" && at->category == "Vehicles", "both tanks are catalog entities (category Vehicles)");
    check(as && at && as->thumbnail && at->thumbnail, "catalog thumbnails rendered from the baked models");
    const TankDef* ds = app.tanks.defFor(as); const TankDef* dtg = app.tanks.defFor(at);
    check(ds && dtg && ds->model && dtg->model, "baked models load (content/vehicles/models; no .glb is read at runtime)");
    if (!ds || !dtg) { finish(app); return; }
    check(!ds->turretParts.empty() && !ds->gunParts.empty() && !ds->mgParts.empty(), "Sherman: separate hull / turret / gun / roof-MG parts from the supplied mesh (" + std::to_string(ds->model->parts.size()) + " parts)");
    check(!dtg->turretParts.empty() && !dtg->gunParts.empty(), "Tiger: gun + mantlet split from the turret mesh at bake (islands moved, nothing added)");
    vec3 se = ds->model->bmax - ds->model->bmin, te = dtg->model->bmax - dtg->model->bmin;
    float hullL = 0; for (int k : ds->hullParts) hullL = std::max(hullL, ds->model->parts[k].bmax.z - ds->model->parts[k].bmin.z);
    float tTur = 0; for (int k : dtg->turretParts) tTur = std::max(tTur, dtg->model->parts[k].bmax.y);
    std::snprintf(b_, sizeof b_, "plausible scale: Sherman %.2f m wide, hull %.2f m long; Tiger %.2f m wide, turret roof %.2f m (Tiger I: about 3.0 m high, 3.7 m wide)", se.x, hullL, te.x, tTur);
    check(se.x > 2.8f && se.x < 3.3f && hullL > 5.6f && hullL < 6.4f && te.x > 3.4f && te.x < 3.9f && tTur > 2.8f && tTur < 3.1f, b_);
    std::snprintf(b_, sizeof b_, "runtime cost bounded: Sherman %d tris, Tiger %d tris, %d + %d texture maps", ds->model->tris, dtg->model->tris, ds->model->textureCount, dtg->model->textureCount);
    check(ds->model->tris < 120000 && dtg->model->tris < 120000, b_);
    // ---------------------------------------------------------------- range
    SH = app.placeInstance(as, vec3(0, 0, 0), 0.0f);
    TG = app.placeInstance(at, vec3(0, 0, 45), 180.0f);
    PAK = app.placeInstance(app.lib.find("gun_pak40"), vec3(-14, 0, 2), 18.0f);
    TRAP = app.placeInstance(app.lib.find("def_tank_trap"), vec3(40, 0, 18), 0.0f);
    TREE = app.placeInstance(app.lib.find("tree_ash"), vec3(-40, 0, 14), 0.0f);
    HOUSE = app.placeInstance(app.lib.find("house_stone_cottage"), vec3(70, 0, -40), 0.0f);
    WALLT = app.placeInstance(app.lib.find("bunker_mg_casemate"), vec3(60, 0, 250), 200.0f);
    check(SH && TG && PAK && TRAP && TREE && HOUSE && WALLT, "range: Sherman, Tiger facing it 45 m away, PaK 40, tank trap, tree, cottage, casemate 257 m downrange placed from the catalog");
    // ramming lane (x -120 .. -160): telegraph poles, a post-and-wire fence with barbed wire behind it, two low stone walls
    POLE1 = app.placeInstance(app.lib.find("utility_pole"), vec3(-120, 0, -20), 0.0f);
    POLE2 = app.placeInstance(app.lib.find("utility_pole"), vec3(-130, 0, -20), 0.0f);
    FENCE = app.placeInstance(app.lib.find("fence_post_wire"), vec3(-140, 0, -20), 0.0f);
    WIREF = app.placeInstance(app.lib.find("wire_fence"), vec3(-140, 0, -14), 90.0f);
    SWALL1 = app.placeInstance(app.lib.find("stone_wall_low"), vec3(-150, 0, -20), 0.0f);
    SWALL2 = app.placeInstance(app.lib.find("stone_wall_low"), vec3(-162, 0, -20), 0.0f);
    HOUSE2 = app.placeInstance(app.lib.find("house_stone_cottage"), vec3(-150, 0, 60), 0.0f);   // pushed through (the T7 cottage stays intact)
    check(POLE1 && POLE2 && FENCE && WIREF && SWALL1 && SWALL2 && HOUSE2, "ramming lane: two telegraph poles, a post-and-wire fence, a barbed-wire fence, two low stone walls and a second cottage placed from the catalog");
    app.syncRuntime(); app.tanks.sync(app);
    app.camPos = vec3(-9, 4.5f, -8); app.camYaw = 225; app.camPitch = -12;
    app.doc.start = {true, vec3(-3.0f, 0, 1.0f), 90};
    app.doc.start.pos.y = app.terrain.height(-3, 1);
    app.doc.name = "Tank range";
    check(app.saveMapTo(outDirT() / "tank_range.nmap.json"), "clean tank range saved (shipped as content/examples/tank_range.nmap.json)");
    seq.clear(); cur = 0; tcur = 0;
    // ---------------------------------------------------------------- Creative: pick the tank in the browser, see the model ghost, place it
    seq.push_back({"creative placement", [this](App& a, float t) {
      static uint64_t placed; static int phase;
      const AssetDef* tg = a.lib.find("tank_tiger_i");
      if (phase < 3) { a.camPos = vec3(-100, 14, 82); a.camYaw = 0; a.camPitch = -38; }   // pinned: real mouse movement must not turn the view
      if (t == 0) {
        phase = 0; placed = 0;
        a.tool = Tool::Place; a.placing = tg; a.placeYaw = 30; a.selected = 0;
        a.looking = true;   // the placement ray goes through the screen centre
        return false;
      }
      if (phase == 0) {
        if (t < 0.3f) return false;
        int ghost = 0; for (auto& m : a.R.worldModels) ghost += m.tint.a < 0.999f && m.model == a.tanks.defFor(tg)->model.get();
        PlaceCheck c = a.checkPlacement(tg, a.hoverGround, a.placeYaw, 0);
        std::snprintf(b_, sizeof b_, "Creative: Tiger I picked from the Vehicles browser shows the tinted baked model as the placement ghost (%d parts at %.1f, %.1f; %s)", ghost, a.hoverGround.x, a.hoverGround.z, c.reason.c_str());
        check(a.hoverValid && ghost == (int)a.tanks.defFor(tg)->model->parts.size() && c.ok, b_);
        shotQueue.push_back("tanks_00_creative_place_ghost");
        phase = 1; return false;
      }
      if (phase == 1) {
        placed = a.placeInstance(tg, a.hoverGround, a.placeYaw);   // the same call a left click makes
        if (placed) a.selected = placed;
        a.syncRuntime(); a.tanks.sync(a);
        TankRT* k = a.tanks.get(placed);
        check(placed && k && std::fabs(wrap180_(k->yaw - 30)) < 0.01f, "click places a Tiger I instance at the cursor (yaw 30, own runtime state, collision on its body)");
        phase = 2; return false;
      }
      if (phase == 2) { if (t < 0.8f) return false; shotQueue.push_back("tanks_00b_creative_placed"); phase = 3; return false; }
      a.looking = false; a.tool = Tool::Select; a.placing = nullptr;
      a.deleteSelected();
      a.syncRuntime(); a.tanks.sync(a);
      check(!a.doc.find(placed) && !a.tanks.get(placed), "deleting it removes the instance and its tank state");
      return true; }});
    seq.push_back({"creative shots", [this](App& a, float t) {
      if (t == 0) { a.camPos = vec3(-9, 4.5f, -8); a.camYaw = 225; a.camPitch = -12; return false; }
      if (t < 0.05f) { shotQueue.push_back("tanks_01_creative_sherman"); return false; }
      if (t < 0.3f) return false;
      if (t < 0.35f) { a.camPos = vec3(8, 4.5f, 52); a.camYaw = 45; a.camPitch = -12; return false; }
      if (t < 0.4f) { shotQueue.push_back("tanks_02_creative_tiger"); return false; }
      return t > 0.6f; }});
    // ---------------------------------------------------------------- enter through the E prompt
    seq.push_back({"explore", [this](App& a, float t) {
      check(a.enterExplore(), "F5: Explore"); a.useScripted = true;
      standAt(a, vec3(-3.4f, 0, 0.6f));
      return true; }});
    seq.push_back({"prompt", [this](App& a, float t) {
      lookAtP(a, T(a, SH).pos + vec3(0, 1.2f, 0.6f));
      if (t < 0.3f) return false;
      check(a.tanks.target == SH, "aiming at any part of the Sherman within reach shows its Enter prompt (colliders resolve to the tank)");
      a.uiShot = outDirT() / "tanks_03_enter_prompt.png";
      a.scriptedInput.interact = true; return true; }});
    seq.push_back({"in gunner sight", [this](App& a, float t) {
      if (t < 0.2f) return false;
      vec3 e, f; float fov;
      bool cam = a.tanks.camera(a, e, f, fov);
      check(a.tanks.occupied == SH && T(a, SH).station == TS_GUNNER && cam && std::fabs(fov - T(a, SH).def->fov) < 0.01f, "E enters directly into the gunner sight (fov " + std::to_string((int)fov) + " deg)");
      check(Physics::character().posture == Physics::STAND, "infantry posture untouched while crewing");
      return true; }});
    // ---------------------------------------------------------------- Ctrl: one toggle per press, never crouches
    seq.push_back({"ctrl hold", [this](App& a, float t) {
      static int h0;
      if (t == 0) { h0 = a.tanks.hatchToggles; a.scriptedInput.crouch = true; }
      if (t < 0.6f) return false;
      a.scriptedInput.crouch = false;
      check(a.tanks.hatchToggles == h0 + 1 && T(a, SH).station == TS_HATCH, "Left Ctrl held for 0.6 s toggles once (gunner -> hatch / roof MG)");
      check(Physics::character().posture == Physics::STAND, "Ctrl in the tank does not crouch the infantry body");
      return true; }});
    seq.push_back({"hatch view", [this](App& a, float t) {
      if (t < 1.0f) return false;
      vec3 e, f; float fov; a.tanks.camera(a, e, f, fov);
      check(T(a, SH).hatchT >= 1.0f && e.y > T(a, SH).pos.y + 3.2f, "hatch camera rises smoothly to the commander's head-out position");
      aimWorld(a, glm::normalize(vec3(0.3f, -0.05f, 1)));
      a.uiShot = outDirT() / "tanks_04_sherman_hatch_mg.png";
      return true; }});
    seq.push_back({"roof mg", [this](App& a, float t) {
      static int s0, p0, m0;
      TankRT& k = T(a, SH);
      if (t == 0) { s0 = k.shots; m0 = k.mgLoaded; p0 = a.shotsFired; }
      if (t < 0.4f) return false;
      a.scriptedInput.fire = t < 1.0f;
      if (t < 1.4f) return false;
      check(k.mgLoaded < m0 && k.shots == s0 && a.shotsFired > p0, "hatch mode: LMB fires the roof MG only (" + std::to_string(m0 - k.mgLoaded) + " rounds, main gun untouched)");
      return true; }});
    // ---------------------------------------------------------------- hatch: Space works both MGs, RMB the main gun, belts change on their own
    seq.push_back({"hatch: Space fires roof + bow MG", [this](App& a, float t) {
      static int m0, b0, s0;
      TankRT& k = T(a, SH);
      if (t == 0) { m0 = k.mgLoaded; b0 = k.bowLoaded; s0 = k.shots; a.scriptedInput.jumpHeld = true; }
      if (t < 0.8f) return false;
      a.scriptedInput.jumpHeld = false;
      std::snprintf(b_, sizeof b_, "open hatch: Space fires the roof MG and the bow MG together (roof %d, bow %d rounds in 0.8 s; main gun untouched)", m0 - k.mgLoaded, b0 - k.bowLoaded);
      check(k.mgLoaded < m0 && k.bowLoaded < b0 && k.shots == s0, b_);
      return true; }});
    seq.push_back({"hatch: RMB fires the main gun", [this](App& a, float t) {
      static int s0; static bool ok0;
      TankRT& k = T(a, SH);
      if (t == 0) { s0 = k.shots; ok0 = k.loaded != SHELL_NONE; return false; }
      if (t < 0.1f) { a.scriptedInput.aim = true; return false; }
      a.scriptedInput.aim = false;
      if (t < 0.4f) return false;
      std::snprintf(b_, sizeof b_, "open hatch: RMB fires the main gun once (shots +%d; it was loaded: %d), no sight zoom from the hatch", k.shots - s0, (int)ok0);
      check(ok0 && k.shots == s0 + 1 && !a.tanks.zoom, b_);
      return true; }});
    seq.push_back({"roof MG belt changes automatically", [this](App& a, float t) {
      static int r0; static bool started; static float tEmpty;
      TankRT& k = T(a, SH); const TankDef& d = *k.def;
      if (t == 0) { k.mgLoaded = 3; k.mgReloadT = 0; r0 = k.mgReserve; started = false; tEmpty = -1; a.scriptedInput.fire = true; }
      if (k.mgLoaded == 0 && tEmpty < 0) tEmpty = t;
      if (k.mgReloadT > 0) started = true;
      if (t > 0.6f) a.scriptedInput.fire = false;   // released: the change still runs and finishes
      if (t < 0.6f + d.mgReload + 0.3f) return false;
      std::snprintf(b_, sizeof b_, "roof MG: an empty belt starts the change by itself (started %d) and the new %d-round belt is in after %.1f s (loaded %d, reserve %d -> %d)", (int)started, d.mgBelt, d.mgReload, k.mgLoaded, r0, k.mgReserve);
      check(started && k.mgLoaded == d.mgBelt && k.mgReserve == r0 - d.mgBelt, b_);
      return true; }});
    seq.push_back({"ctrl back", [this](App& a, float t) {
      static int h0, s0;
      if (t == 0) { h0 = a.tanks.hatchToggles; s0 = T(a, SH).shots; a.scriptedInput.crouch = true; a.scriptedInput.fire = true; }
      if (t < 0.15f) return false;
      a.scriptedInput.crouch = false;
      if (t < 1.2f) return false;   // fire still held through the switch: must not fire the cannon
      std::snprintf(b_, sizeof b_, "second Ctrl press returns to the sight; a fire button held through the switch does not fire the cannon (toggles +%d, station %d, cannon shots +%d)", a.tanks.hatchToggles - h0, T(a, SH).station, T(a, SH).shots - s0);
      check(a.tanks.hatchToggles == h0 + 1 && T(a, SH).station == TS_GUNNER && T(a, SH).shots == s0, b_);
      a.scriptedInput.fire = false;
      return true; }});
    // ---------------------------------------------------------------- gunner sight: Space works the bow MG (Browning M1918 type, BAR sound pack)
    seq.push_back({"sight: Space fires the bow MG", [this](App& a, float t) {
      static int b0, m0, s0, pc0; static std::string snd;
      TankRT& k = T(a, SH); const TankDef& d = *k.def;
      if (t == 0) { b0 = k.bowLoaded; m0 = k.mgLoaded; s0 = k.shots; pc0 = a.audio.playCount; snd.clear(); a.scriptedInput.jumpHeld = true; aimWorld(a, glm::normalize(yawQuat(k.yaw) * vec3(0.05f, -0.02f, 1))); }
      if (a.audio.lastPlayed.rfind("shot_", 0) == 0) snd = a.audio.lastPlayed;
      if (t > 0.5f && t < 0.55f) a.uiShot = outDirT() / "tanks_04b_sherman_bow_mg_ammo_strip.png";
      if (t < 1.0f) return false;
      a.scriptedInput.jumpHeld = false;
      vec3 bd; vec3 bm = a.tanks.bowMuzzleWorld(k, bd);
      vec3 hl = glm::inverse(glm::mat3(a.tanks.hullMatrix(k))) * (bm - k.pos);
      int n = b0 - k.bowLoaded;
      float expect = d.bowRpm / 60.0f;
      std::snprintf(b_, sizeof b_, "gunner sight: Space fires the %s from the glacis ball mount at hull (%.2f, %.2f, %.2f): %d rounds in 1 s at %.0f rpm, sound '%s'; main gun and roof MG untouched",
                    d.bowName.c_str(), hl.x, hl.y, hl.z, n, d.bowRpm, snd.c_str());
      check(d.bowOn && n >= expect - 2 && n <= expect + 2 && k.shots == s0 && k.mgLoaded == m0 && snd == d.bowSound && d.bowSound == "shot_m1918a2_bar" && hl.z > 2.0f && d.bowBelt == 50, b_);
      return true; }});
    seq.push_back({"bow MG belt changes automatically", [this](App& a, float t) {
      static int r0; static bool started;
      TankRT& k = T(a, SH); const TankDef& d = *k.def;
      if (t == 0) { k.bowLoaded = 2; k.bowReloadT = 0; r0 = k.bowReserve; started = false; a.scriptedInput.jumpHeld = true; }
      if (k.bowReloadT > 0) started = true;
      if (t > 0.4f) a.scriptedInput.jumpHeld = false;
      if (t < 0.4f + d.bowReload + 0.3f) return false;
      std::snprintf(b_, sizeof b_, "bow MG: the empty belt changes by itself (started %d) and a fresh 50-round belt is in after %.1f s (loaded %d, reserve %d -> %d)", (int)started, d.bowReload, k.bowLoaded, r0, k.bowReserve);
      check(started && k.bowLoaded == 50 && k.bowReserve == r0 - 50, b_);
      return true; }});
    // ---------------------------------------------------------------- turret: rate limits, lag, wrap-around, stops
    seq.push_back({"traverse lag", [this](App& a, float t) {
      TankRT& k = T(a, SH); const TankDef& d = *k.def;
      static float y0;
      if (t == 0) { y0 = k.tYaw; vec3 hd = yawQuat(k.yaw + 90) * vec3(0, 0, 1); aimWorld(a, hd); }
      if (t < 0.5f) return false;
      if (once(t, 0.5f)) {
        vec3 e, f; float fov; a.tanks.camera(a, e, f, fov);
        vec3 gd; a.tanks.muzzleWorld(k, gd);
        float moved = std::fabs(k.tYaw - y0);
        check(moved > 3 && moved <= d.traverseRate * 0.5f + 0.5f, "turret traverse is rate limited (" + std::to_string(moved).substr(0, 4) + " deg in 0.5 s, limit " + std::to_string((int)d.traverseRate) + " deg/s)");
        check(glm::dot(f, gd) > 0.9999f && glm::dot(f, App::forward(a.eyeYaw, a.eyePitch)) < 0.99f, "the sight follows the achieved gun line, not the instant mouse aim (desired-aim ring drawn separately)");
        a.uiShot = outDirT() / "tanks_05_sherman_sight_lag.png";
        return false;
      }
      if (std::fabs(k.tYaw - 90) > 0.3f) return t > 8;
      std::snprintf(b_, sizeof b_, "traverse settles on the target without overshoot (%.2f deg after %.1f s)", k.tYaw, t);
      check(t < 6.0f, b_);
      return true; }});
    seq.push_back({"wrap", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      if (t == 0) { k.tYaw = 170; k.tYawV = 0; vec3 hd = yawQuat(k.yaw - 170) * vec3(0, 0, 1); aimWorld(a, hd); }
      if (std::fabs(wrap180_(k.tYaw + 170)) > 0.3f) return t > 6;
      check(t < 2.5f, "yaw wrap-around takes the short way (170 -> -170 deg in " + std::to_string(t).substr(0, 4) + " s)");
      return true; }});
    seq.push_back({"elevation stop", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      if (t == 0) { vec3 hd = yawQuat(k.yaw + k.tYaw) * vec3(0, 0, 1); hd.y = 1.2f; aimWorld(a, glm::normalize(hd)); }
      if (t < 6.0f) return false;
      check(std::fabs(k.gElev - k.def->elevMax) < 0.01f, "elevation stops at the limit (" + std::to_string(k.gElev).substr(0, 4) + " / " + std::to_string((int)k.def->elevMax) + " deg)");
      return true; }});
    // ---------------------------------------------------------------- zero: the round crosses the sight line at the zero range
    seq.push_back({"zero", [this](App& a, float t) {
      TankRT& k = T(a, SH); const TankDef& d = *k.def;
      static vec3 sightHit; static float i0; static int phase;
      Instance* w = a.doc.find(WALLT);
      vec3 tgt = w->pos + vec3(0, 1.4f, 0);
      if (t == 0) { phase = 0; aimWorld(a, glm::normalize(tgt - (k.pos + vec3(0, 2.3f, 0)))); }
      if (phase == 0) {
        if (k.loaded == SHELL_NONE || std::fabs(k.tYawV) > 0.01f || std::fabs(k.gElevV) > 0.01f || t < 4.0f) return t > 15;
        vec3 e, f; float fov; a.tanks.camera(a, e, f, fov);
        Physics::Hit h; if (!Physics::castRayEx(e, f, 600.0f, h, SH << 16)) { check(false, "sight line reaches the casemate"); return true; }
        sightHit = h.pos; i0 = a.lastImpact.time; phase = 1;
        a.uiShot = outDirT() / "tanks_06_sherman_sight_casemate.png";
        return false;
      }
      if (phase == 1) { a.scriptedInput.fire = true; phase = 2; return false; }
      a.scriptedInput.fire = false;
      if (a.lastImpact.time <= i0) return t > 15;
      if (phase == 2) { phase = 3; a.uiShot = outDirT() / "tanks_06b_sherman_sight_impact.png"; }
      float range = glm::length(sightHit - k.pos);
      float err = glm::length(a.lastImpact.pos - sightHit);
      // expected offset at this range relative to the zero (game ballistics, no drag) plus the optic / bore parallax
      float va = d.velAP, aR = std::atan(9.81f * range / (2 * va * va)), a0 = std::atan(9.81f * d.zeroRange / (2 * va * va));
      float expect = range * std::fabs(std::tan(aR - a0)) + 0.35f;
      std::snprintf(b_, sizeof b_, "bore zero: AP impact %.2f m from the sight point at %.0f m (allowed %.2f m incl. parallax and drop vs the %.0f m zero)", err, range, expect, d.zeroRange);
      check(err <= expect, b_);
      return true; }});
    // ---------------------------------------------------------------- HE / AP state machine
    seq.push_back({"ammo: loaded + R", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      static int he0, ap0, s0, p0, l0, phase;
      if (t == 0) phase = 0;
      if (phase == 0) {
        if (k.reload > 0 || k.loaded == SHELL_NONE) return t > 10;   // let the crew finish the load after the zero shot
        he0 = k.reserveHE; ap0 = k.reserveAP; s0 = k.shots; p0 = a.shotsFired; l0 = k.loaded;
        k.selected = l0; a.scriptedInput.reload = true; phase = 1; return false;
      }
      if (phase == 1) { phase = 2; return false; }
      check(k.loaded == l0 && k.selected != l0 && k.reserveHE == he0 && k.reserveAP == ap0 && k.shots == s0 && a.shotsFired == p0,
            std::string("R with ") + (l0 == SHELL_HE ? "HE" : "AP") + " loaded only changes the NEXT choice; the chambered round, reserves and shot count are unchanged");
      return true; }});
    seq.push_back({"ammo: fire HE, R to AP while loading", [this](App& a, float t) {
      TankRT& k = T(a, SH); const TankDef& d = *k.def;
      static int phase, he0, ap0; static float tFire;
      if (t == 0) {
        phase = 0;
        k.loaded = SHELL_HE; k.selected = SHELL_HE; k.reload = 0;   // HE chambered, HE selected
        he0 = k.reserveHE; ap0 = k.reserveAP;
        a.scriptedInput.fire = true; return false;
      }
      a.scriptedInput.fire = false;
      if (phase == 0) { check(k.loaded == SHELL_NONE && k.reload > d.reloadTime - 0.05f && k.selected == SHELL_HE, "firing HE consumes exactly the chambered round and starts the next load automatically"); phase = 1; tFire = t; return false; }
      if (phase == 1 && t > tFire + 1.0f) { a.scriptedInput.reload = true; phase = 2; return false; }
      if (phase == 2) { check(k.selected == SHELL_AP && k.reload > 0 && k.reload < d.reloadTime - 0.9f, "R while loading switches the in-progress choice to AP without resetting the timer"); phase = 3; return false; }
      if (phase == 3) {
        if (k.reload > 0) return t > 12;
        std::snprintf(b_, sizeof b_, "the load completes after the original %.1f s (%.2f s): one AP chambered, AP %d -> %d, HE unchanged %d", d.reloadTime, t - tFire, ap0, k.reserveAP, k.reserveHE);
        check(k.loaded == SHELL_AP && k.reserveAP == ap0 - 1 && k.reserveHE == he0 && std::fabs((t - tFire) - d.reloadTime) < 0.1f, b_);
        return true;
      }
      return t > 12; }});
    seq.push_back({"ammo: repeated R while loading", [this](App& a, float t) {
      TankRT& k = T(a, SH); const TankDef& d = *k.def;
      static int phase, tot0, s0, p0, presses, sel0; static float tFire, lastR;
      if (t == 0) { phase = 0; presses = 0; tot0 = k.reserveAP + k.reserveHE; sel0 = k.selected; a.scriptedInput.fire = true; return false; }
      a.scriptedInput.fire = false;
      if (phase == 0) { tFire = t; s0 = k.shots; p0 = a.shotsFired; lastR = k.reload; phase = 1; check_ = true; return false; }
      if (phase == 1) {
        if (k.reload > 0) {
          check_ = check_ && k.reload <= lastR + 1e-4f; lastR = k.reload;   // never reset or extended
          if (presses < 7 && std::fmod(t, 0.3f) < dt_) { a.scriptedInput.reload = true; ++presses; }
          return t > 12;
        }
        int tot = k.reserveAP + k.reserveHE;
        int expect = presses % 2 ? (sel0 == SHELL_AP ? SHELL_HE : SHELL_AP) : sel0;
        std::snprintf(b_, sizeof b_, "%d R presses during one load: the timer never resets or jumps (%.2f s total), exactly one round chambered (%s), reserves %d -> %d, no shot fired",
                      presses, t - tFire, k.loaded == SHELL_HE ? "HE" : "AP", tot0, tot);
        check(check_ && presses >= 5 && k.loaded == expect && tot == tot0 - 1 && k.shots == s0 && a.shotsFired == p0 && std::fabs((t - tFire) - d.reloadTime) < 0.1f, b_);
        check_ = true;
        return true;
      }
      return t > 12; }});
    seq.push_back({"ammo: empty selected reserve", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      static int ap0;
      if (t == 0) { k.reserveHE = 0; k.selected = SHELL_HE; ap0 = k.reserveAP; a.scriptedInput.fire = true; return false; }
      a.scriptedInput.fire = false;
      if (k.reload > 0 || t < 0.2f) return t > 12;
      check(k.loaded == SHELL_AP && k.selected == SHELL_AP && k.reserveAP == ap0 - 1 && k.reserveHE == 0, "selected HE reserve empty: the crew loads AP instead (with feedback) and HE stays at 0");
      return true; }});
    seq.push_back({"ammo: switch / fire at completion", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      static int phase, s0;
      if (t == 0) { k.reserveHE = 3; k.loaded = SHELL_NONE; k.selected = SHELL_AP; k.reload = 0.5f / 60.0f; a.scriptedInput.reload = true; phase = 0; return false; }
      if (phase == 0) { check(k.loaded == SHELL_HE && k.reserveHE == 2, "R on the step the load completes: the toggle is applied first, so the new choice (HE) is chambered"); phase = 1;
        k.loaded = SHELL_NONE; k.reload = 0.5f / 60.0f; s0 = k.shots; a.scriptedInput.fire = true; return false; }
      if (phase == 1) { check(k.shots == s0 && k.loaded != SHELL_NONE, "LMB on the step the load completes does not fire (fire resolves before the load); the round is kept"); a.scriptedInput.fire = false; phase = 2; return false; }
      if (phase == 2) { a.scriptedInput.fire = true; phase = 3; return false; }
      a.scriptedInput.fire = false;
      check(k.shots == s0 + 1, "the next press fires it");
      return true; }});
    seq.push_back({"ammo: reload during hatch mode", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      static int l0;
      if (t == 0) { if (k.reload <= 0) k.loaded = SHELL_NONE; l0 = k.loadsCompleted; a.scriptedInput.crouch = true; return false; }
      if (t < 0.1f) return false;
      a.scriptedInput.crouch = false;
      if (k.reload > 0 || t < 0.3f) return t > 12;
      check(k.station == TS_HATCH && k.loadsCompleted == l0 + 1 && k.loaded != SHELL_NONE, "the load keeps running and completes while the player is at the hatch");
      return true; }});
    seq.push_back({"ammo: no reserves", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      static int phase, s0;
      if (t == 0) { a.scriptedInput.crouch = true; phase = 0; return false; }   // back to the sight
      a.scriptedInput.crouch = false;
      if (phase == 0) { if (t < 1.0f) return false; k.reserveAP = k.reserveHE = 0; k.loaded = SHELL_AP; k.reload = 0; s0 = k.shots; a.scriptedInput.fire = true; phase = 1; return false; }
      if (phase == 1) { a.scriptedInput.fire = false; phase = 2; return false; }
      if (phase == 2) { if (t < 1.5f) return false; a.scriptedInput.fire = true; phase = 3; return false; }
      a.scriptedInput.fire = false;
      check(k.station == TS_GUNNER && k.shots == s0 + 1 && k.loaded == SHELL_NONE && k.reload == 0 && k.reserveAP == 0 && k.reserveHE == 0, "no reserves: after the last round the gun stays empty, no load starts, a further click fires nothing");
      k.reserveAP = 20; k.reserveHE = 10;   // restock for the rest of the test
      return true; }});
    seq.push_back({"ammo: exit / re-entry", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      static int tot0, l0, phase;
      if (t == 0) phase = 0;
      if (phase == 0) { if (k.reload <= 0) return t > 10; tot0 = k.reserveAP + k.reserveHE; l0 = k.loadsCompleted; a.scriptedInput.interact = true; phase = 1; return false; }
      if (phase == 1) { check(a.tanks.occupied == 0, "E gets out (parked)"); phase = 2; return false; }
      if (phase == 2) { if (k.reload > 0) return t > 12; phase = 3; return false; }
      if (phase == 3) { lookAtP(a, k.pos + vec3(0, 1.2f, 0)); if (t < 7.5f) return false; a.scriptedInput.interact = true; phase = 4; return false; }
      if (t < 7.8f) return false;
      check(a.tanks.occupied == SH && k.loadsCompleted == l0 + 1 && k.reserveAP + k.reserveHE == tot0 - 1 && k.loaded != SHELL_NONE, "exit mid-load and re-entry: the crew finished that one load; no second round or timer reset appeared");
      return true; }});
    // ---------------------------------------------------------------- driving
    seq.push_back({"drive", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      static vec3 p0; static float y0, tReach;
      if (t == 0) { p0 = k.pos; y0 = k.yaw; tReach = -1; a.scriptedInput.move = vec2(0, 1); }
      if (tReach < 0 && k.speed > 5.0f) tReach = t;
      if (t < 4.0f) return false;
      if (once(t, 4.0f)) {
        std::snprintf(b_, sizeof b_, "W drives forward: %.1f m in 4 s, %.1f km/h (5 m/s after %.1f s)", glm::length(k.pos - p0), k.speed * 3.6f, tReach);
        check(glm::length(k.pos - p0) > 12 && k.speed > 5 && tReach > 0, b_);
        summary["sherman_5ms_s"] = tReach;
        a.scriptedInput.move = vec2(1, 0);   // pivot right
        y0 = k.yaw; return false;
      }
      if (t < 6.0f) return false;
      if (once(t, 6.0f)) { check(wrap180_(k.yaw - y0) < -10, "D turns the hull right on its tracks (" + std::to_string((int)wrap180_(k.yaw - y0)) + " deg; blocked '" + k.blockedBy + "')"); a.scriptedInput.move = vec2(0, -1); return false; }
      if (t < 8.0f) return false;
      check(k.speed < 0, "S brakes and reverses"); a.scriptedInput.move = vec2(0); return true; }});
    seq.push_back({"stop", [this](App& a, float t) { return std::fabs(T(a, SH).speed) < 0.01f || t > 6; }});
    seq.push_back({"moving exit refused", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      if (t == 0) { a.scriptedInput.move = vec2(0, 1); return false; }
      if (t < 1.5f) return false;
      if (once(t, 1.5f)) { a.scriptedInput.interact = true; return false; }
      if (t < 1.6f) return false;
      if (once(t, 1.6f)) check(a.tanks.occupied == SH && a.tanks.lastExitWhy == "moving", "E while moving is refused with feedback (slow down to get out)");
      a.scriptedInput.move = vec2(0);
      return std::fabs(k.speed) < 0.01f || t > 8; }});
    seq.push_back({"tank trap", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      Instance* tr = a.doc.find(TRAP);
      if (t == 0) { place(a, SH, tr->pos + vec3(0, 0, -9), 0); a.scriptedInput.move = vec2(0, 1); }
      if (t < 6.0f) return false;
      a.scriptedInput.move = vec2(0);
      float gap = tr->pos.z - k.pos.z;
      std::snprintf(b_, sizeof b_, "the tank trap physically stops the tank (front %.2f m short of the trap centre, blocked by '%s')", gap - k.def->halfSize.z, k.blockedBy.c_str());
      check(gap > k.def->halfSize.z && k.blockedBy.find("Tank trap") != std::string::npos, b_);
      return true; }});
    seq.push_back({"tree crushed", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      Instance* tr = a.doc.find(TREE);
      if (t == 0) { place(a, SH, tr->pos + vec3(0, 0, -8), 0); a.scriptedInput.move = vec2(0, 1); }
      if (t < 4.0f) return false;
      a.scriptedInput.move = vec2(0);
      check(a.rt[TREE].treeFelled && k.crushes >= 1, "a tree in the way is pushed over through the shared foliage damage path");
      return true; }});
    // ---------------------------------------------------------------- ramming: one energy rule (tank_ram.cpp)
    // a telegraph pole at speed: knocked over at once, falls the way the tank pushes it, costs a fast tank little speed
    seq.push_back({"ram: telegraph pole at speed", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      static float vPrev, vBefore, vAfter, tHit; static int phase, crushed0;
      Instance* p = a.doc.find(POLE1);
      if (t == 0) { place(a, SH, p->pos + vec3(0, 0, -50), 0); a.scriptedInput.move = vec2(0, 1); vPrev = 0; tHit = -1; phase = 0; crushed0 = a.tanks.crushedTotal; }
      if (tHit < 0 && a.tanks.knocked.count(POLE1)) { tHit = t; vBefore = vPrev; vAfter = k.speed; }
      vPrev = k.speed;
      if (tHit < 0) { if (t > 25) { check(false, "the tank never reached the telegraph pole (blocked '" + k.blockedBy + "')"); a.scriptedInput.move = vec2(0); return true; } return false; }
      if (phase == 0 && t >= tHit + 2.5f) {
        phase = 1;
        mat4 M = a.instanceMatrix(*p); a.tanks.propMatrix(*p, M);
        vec3 up = glm::normalize(vec3(M * vec4(0, 1, 0, 0)));
        vec3 top = vec3(M * vec4(0, 7, 0, 1));
        float loss = (vBefore - vAfter) / std::max(0.01f, vBefore);
        std::snprintf(b_, sizeof b_, "telegraph pole hit at %.1f km/h: knocked over at once and falling the way the tank pushes it (up axis y %.2f, top %.1f m ahead of the base 2.5 s later); the tank keeps %.1f km/h (-%.1f%%)",
                      vBefore * 3.6f, up.y, top.z - p->pos.z, vAfter * 3.6f, loss * 100);
        bool down = a.tanks.knocked[POLE1].flat || (up.y < 0.5f && top.z > p->pos.z + 2.0f);   // lying, or already crushed flat
        check(vBefore > 5.0f && loss > 0.0f && loss < 0.08f && down, b_);
        summary["ram_pole_fast"] = {{"before", vBefore}, {"after", vAfter}, {"loss", loss}};
        shotQueue.push_back("tanks_12_pole_knocked");
      }
      if (t < tHit + 6.0f) return false;
      a.scriptedInput.move = vec2(0);
      check(a.tanks.crushedTotal > crushed0, "driving on over the fallen pole crushes it flat under the tracks (no stop: blocked '" + k.blockedBy + "')");
      return true; }});
    // the same pole at walking pace: a much larger share of the speed goes into it
    seq.push_back({"ram: telegraph pole slowly", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      static float vPrev, vBefore, vAfter, tHit;
      Instance* p = a.doc.find(POLE2);
      if (t == 0) { place(a, SH, p->pos + vec3(0, 0, -9), 0); a.scriptedInput.move = vec2(0, 0.25f); vPrev = 0; tHit = -1; }
      if (tHit < 0 && a.tanks.knocked.count(POLE2)) { tHit = t; vBefore = vPrev; vAfter = k.speed; }
      vPrev = k.speed;
      if (tHit < 0) { if (t > 20) { check(false, "slow tank never reached the second pole"); a.scriptedInput.move = vec2(0); return true; } return false; }
      if (t < tHit + 0.5f) return false;
      a.scriptedInput.move = vec2(0);
      float loss = (vBefore - vAfter) / std::max(0.01f, vBefore);
      float fast = summary.contains("ram_pole_fast") ? summary["ram_pole_fast"].value("loss", 1.0f) : 1.0f;
      std::snprintf(b_, sizeof b_, "the same pole at %.1f km/h takes %.0f%% of the speed (%.1f%% at speed): v' = sqrt(v^2 - 2E/M), then momentum shared with the pole", vBefore * 3.6f, loss * 100, fast * 100);
      check(loss > 0.15f && loss > 4.0f * fast, b_);
      summary["ram_pole_slow"] = {{"before", vBefore}, {"after", vAfter}, {"loss", loss}};
      return true; }});
    seq.push_back({"ram: post-and-wire fence and barbed wire", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      Instance* f = a.doc.find(FENCE);
      if (t == 0) { place(a, SH, f->pos + vec3(0, 0, -11), 0); a.scriptedInput.move = vec2(0, 1); }
      if (t < 7.0f) return false;
      a.scriptedInput.move = vec2(0);
      Instance* w = a.doc.find(WIREF);
      std::snprintf(b_, sizeof b_, "a post-and-wire fence and a barbed-wire fence in the way are flattened and the tank drives on (fence knocked %d, wire cut %d, tank %.1f m past the wire, blocked '%s')",
                    (int)a.tanks.knocked.count(FENCE), (int)a.rt[WIREF].defDestroyed, k.pos.z - w->pos.z, k.blockedBy.c_str());
      check(a.tanks.knocked.count(FENCE) && a.rt[WIREF].defDestroyed && k.pos.z > w->pos.z + 2.0f, b_);
      return true; }});
    // a dry-stone wall: too much for a slow tank's momentum, it gives way to sustained pushing (hold W)
    seq.push_back({"ram: stone wall needs pushing", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      static float tStop, tBreak, work0; static int phase;
      Instance* s = a.doc.find(SWALL1);
      if (t == 0) { place(a, SH, s->pos + vec3(0, 0, -8.5f), 0); a.scriptedInput.move = vec2(0, 0.35f); tStop = tBreak = -1; phase = 0; }
      if (phase == 0 && k.blockedBy.rfind("pushing", 0) == 0) { phase = 1; tStop = t; work0 = k.ramWork; a.scriptedInput.move = vec2(0, 1); }
      if (phase == 1 && a.tanks.knocked.count(SWALL1)) { phase = 2; tBreak = t; }
      if (phase < 2) { if (t > 25) { check(false, "stone wall: phase " + std::to_string(phase) + ", blocked '" + k.blockedBy + "'"); a.scriptedInput.move = vec2(0); return true; } return false; }
      if (t < tBreak + 3.0f) return false;
      a.scriptedInput.move = vec2(0);
      const TankDef& d = *k.def;
      float need = a.tanks.ramProps["stone_wall_low"].breakKJ * 1000.0f;
      float expect = (need - work0) / (d.engineKW * 1000.0f * d.pushEff);
      std::snprintf(b_, sizeof b_, "low stone wall: the slow tank stops against it (%.0f kJ of %.0f from its momentum), holding W pushes it down in %.1f s (engine %.0f kW x %.2f: %.1f s expected), it collapses to rubble and the tank drives over (front %.1f m past it)",
                    work0 / 1000, need / 1000, tBreak - tStop, d.engineKW, d.pushEff, expect, k.pos.z + d.halfSize.z - s->pos.z);
      check(tStop > 0 && tBreak - tStop > 0.8f * expect && tBreak - tStop < expect + 1.0f && a.tanks.knocked[SWALL1].rubble && k.pos.z + d.halfSize.z > s->pos.z + 1.5f, b_);
      summary["ram_wall_push_s"] = tBreak - tStop;
      shotQueue.push_back("tanks_13_wall_rubble");
      return true; }});
    // the same wall hit by a heavy tank with momentum: through at once, at a cost in speed
    seq.push_back({"ram: Tiger at speed through a stone wall", [this](App& a, float t) {
      TankRT& g = T(a, TG);
      static float vPrev, vBefore, vAfter, tHit; static vec3 home; static float homeYaw;
      Instance* s = a.doc.find(SWALL2);
      if (t == 0) { home = g.pos; homeYaw = g.yaw; place(a, TG, s->pos + vec3(0, 0, -15), 0); g.speed = 8.0f; vPrev = 8; tHit = -1; }
      if (tHit < 0 && a.tanks.knocked.count(SWALL2)) { tHit = t; vBefore = vPrev; vAfter = g.speed; }
      vPrev = g.speed;
      if (tHit < 0 && t < 6) return false;
      std::snprintf(b_, sizeof b_, "a 57 t Tiger rolling at %.1f km/h smashes the same wall without stopping (1/2 M v^2 = %.0f kJ > 600 kJ) and keeps %.1f km/h",
                    vBefore * 3.6f, 0.5f * g.def->massKg * vBefore * vBefore / 1000, vAfter * 3.6f);
      check(tHit > 0 && vAfter > 3.0f && vAfter < vBefore - 1.0f, b_);
      place(a, TG, home, homeYaw);
      return true; }});
    seq.push_back({"house: push through the wall", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      Instance* h = a.doc.find(HOUSE2);
      const AssetDef* ha = a.rt[HOUSE2].asset;
      static float tPush, tBreach; static int br0;
      if (t == 0) { place(a, SH, h->pos + vec3(0, 0, ha->bmin.z - 8), 0); a.scriptedInput.move = vec2(0, 1); tPush = tBreach = -1; br0 = k.breaches; }
      if (tPush < 0 && k.blockedBy.rfind("pushing", 0) == 0) tPush = t;
      if (tBreach < 0 && k.breaches > br0) tBreach = t;
      bool through = k.pos.z + k.def->halfSize.z > h->pos.z + ha->bmin.z + 1.5f;
      if (!(through && tBreach > 0) && t < 30) return false;
      a.scriptedInput.move = vec2(0);
      int dmg = 0; for (auto st : a.rt[HOUSE2].state) dmg += st != PANEL_INTACT;
      std::snprintf(b_, sizeof b_, "masonry holds against the tank's momentum, then gives way to sustained pushing: stopped at %.1f s, breach after %.1f s of W, %d bays damaged, front %.1f m inside the wall line (blocked '%s')",
                    tPush, tBreach - tPush, dmg, k.pos.z + k.def->halfSize.z - (h->pos.z + ha->bmin.z), k.blockedBy.c_str());
      check(tPush > 0 && tBreach - tPush > 1.5f && dmg > 0 && through, b_);
      summary["ram_house_push_s"] = tBreach - tPush;
      shotQueue.push_back("tanks_14_house_breached");
      return true; }});
    seq.push_back({"bunker concrete holds", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      Instance* c = a.doc.find(WALLT);
      const AssetDef* ca = a.rt[WALLT].asset;
      static int br0, dmg0;
      if (t == 0) {
        br0 = k.breaches; dmg0 = 0; for (auto st : a.rt[WALLT].state) dmg0 += st != PANEL_INTACT;
        vec3 front = c->pos + yawQuat(c->yaw) * vec3(0, 0, ca->bmax.z + 7.0f);
        place(a, SH, front, wrapDeg(c->yaw + 180.0f));
        a.scriptedInput.move = vec2(0, 1);
      }
      if (t < 8.0f) return false;
      a.scriptedInput.move = vec2(0);
      int dmg = 0; for (auto st : a.rt[WALLT].state) dmg += st != PANEL_INTACT;
      std::snprintf(b_, sizeof b_, "a concrete casemate never gives way: 8 s of W, no breach (damaged bays %d before, %d after: the earlier zero shot), blocked by '%s'", dmg0, dmg, k.blockedBy.c_str());
      check(k.breaches == br0 && dmg == dmg0 && !k.blockedBy.empty() && k.blockedBy.rfind("pushing", 0) != 0, b_);
      return true; }});
    seq.push_back({"crater", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      static float pmax;
      if (t == 0) { place(a, SH, vec3(-60, 0, -62), 0); a.craterAt(vec3(-60, a.terrain.height(-60, -50), -50), 3.0f, 0.9f); pmax = 0; check_ = true; a.scriptedInput.move = vec2(0, 0.6f); }
      pmax = std::max(pmax, std::fabs(k.pitch));
      check_ = check_ && std::isfinite(k.pos.y) && k.pos.y > a.terrain.height(k.pos.x, k.pos.z) - 0.5f;
      if (t < 6.0f) return false;
      a.scriptedInput.move = vec2(0);
      check(check_ && pmax > 2.0f && k.pos.z > -46, "crossing a 3 m crater: the hull pitches with the ground (max " + std::to_string(pmax).substr(0, 4) + " deg), no tunnelling or NaN");
      check_ = true;
      return true; }});
    seq.push_back({"profiles", [this](App& a, float t) {
      const TankDef& d = *T(a, TG).def; const TankDef& s = *T(a, SH).def;
      check(d.accel < s.accel && d.traverseRate < s.traverseRate && d.maxFwd * d.offroadMul < s.maxFwd * s.offroadMul && d.maxFwd > s.maxFwd,
            "default gameplay profile: Sherman accelerates, traverses and runs off-road faster; the Tiger keeps its higher listed road speed");
      return true; }});
    // ---------------------------------------------------------------- mouse sensitivity matched to the view (explore.cpp)
    seq.push_back({"sensitivity: same on-screen speed in every view", [this](App& a, float t) {
      // screen distance near the crosshair per mouse count: (H / 2) / tan(fov / 2) x tan(0.1 deg x scale(fov))
      const float H = 1080.0f;
      auto px = [&](float fov) { return (H * 0.5f) / std::tan(radians(fov) * 0.5f) * std::tan(radians(0.1f * App::lookScaleFor(fov))); };
      float hip = px(72.0f), worst = 0;
      json tab = json::array();
      auto add = [&](const std::string& what, float fov) {
        float r = px(fov) / hip;
        worst = std::max(worst, std::fabs(r - 1.0f));
        tab.push_back({{"view", what}, {"fov", fov}, {"scale", App::lookScaleFor(fov)}, {"degPerCount", 0.1f * App::lookScaleFor(fov)}, {"screenRatio", r}});
      };
      add("hip (running around)", 72.0f);
      for (auto& w : a.weaponDefs) {
        if (w->action == "melee" || w->action == "thrown") continue;
        add(w->id + " ADS", w->adsWorldFov);
        if (w->hasScope) add(w->id + " scope " + std::to_string((int)std::lround(w->scopeMag)) + "x", 72.0f / w->scopeMag);
      }
      for (auto& [id, d] : a.tanks.defs) { add(id + " sight", d->fov); add(id + " sight zoom", d->fovZoom); add(id + " hatch", 70.0f); }
      summary["sensitivity"] = tab;
      std::snprintf(b_, sizeof b_, "mouse look scaled by tan(fov/2) / tan(36 deg) in every view: %d views (ADS, scopes, tank sights) move the image near the crosshair at the hip-fire speed (worst %.3f%%; ZF39 4x %.3f, Tiger sight %.3f / zoom %.3f, Sherman %.3f / %.3f)",
                    (int)tab.size(), worst * 100, App::lookScaleFor(18.0f), App::lookScaleFor(a.tanks.defs["tiger_i"]->fov), App::lookScaleFor(a.tanks.defs["tiger_i"]->fovZoom),
                    App::lookScaleFor(a.tanks.defs["sherman_firefly"]->fov), App::lookScaleFor(a.tanks.defs["sherman_firefly"]->fovZoom));
      check(worst < 0.002f && std::fabs(App::lookScaleFor(72.0f) - 1.0f) < 1e-6f && App::lookScaleFor(10.0f) < App::lookScaleFor(24.0f), b_);
      return true; }});
    seq.push_back({"exit beside a wall", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      Instance* h = a.doc.find(HOUSE);
      const AssetDef* ha = a.rt[HOUSE].asset;
      if (std::fabs(k.speed) > 0.01f && t < 3) return false;
      static float t0 = -1;
      if (t0 < 0 || t < t0) { t0 = t; place(a, SH, h->pos + vec3(ha->bmin.x - k.def->halfSize.x - 0.35f, 0, (ha->bmin.z + ha->bmax.z) * 0.5f), 0); return false; }
      if (t < t0 + 0.2f) return false;
      if (a.tanks.occupied && once(t, t0 + 0.2f)) { a.scriptedInput.interact = true; return false; }
      if (t < t0 + 0.4f) return false;
      t0 = -1;
      auto ch = Physics::character();
      vec3 lp = glm::inverse(yawQuat(h->yaw)) * (ch.pos - h->pos);
      bool insideHouse = lp.x > ha->bmin.x && lp.x < ha->bmax.x && lp.z > ha->bmin.z && lp.z < ha->bmax.z;
      std::snprintf(b_, sizeof b_, "parked against the cottage: the exit search skips the wall side and dismounts at a clear, supported spot (occupied %d, inside %d, why '%s')", (int)(a.tanks.occupied != 0), (int)insideHouse, a.tanks.lastExitWhy.c_str());
      check(a.tanks.occupied == 0 && !insideHouse && !Physics::capsuleBlocked(ch.pos + vec3(0, 0.08f, 0), 1.74f, 0.3f), b_);
      return true; }});
    // ---------------------------------------------------------------- damage matrix (real projectiles through stepProjectiles)
    auto caseStep = [this](const char* label, bool turret, vec3 localN, int kind, const char* weapon, int shots, int expectHp, bool expectWreck, float turretYaw, bool ap = true) {
      return Step{label, [=, this](App& a, float t) {
        static int fired, w0; static float lastT;
        TankRT& k = T(a, TG);
        if (t == 0) { fresh(a, TG, turretYaw); fired = 0; w0 = a.tanks.wrecksMade; lastT = 0; a.projectiles.clear(); }
        if (a.projectiles.empty() && fired < shots && t - lastT > 0.05f) {
          vec3 along = std::fabs(localN.y) > 0.5f ? vec3(0.4f, 0, 0.3f) : vec3(0, -0.15f, 0) + (std::fabs(localN.x) > 0.5f ? vec3(0, 0, 0.5f) : vec3(0.35f, 0, 0));
          vec3 p, n; facePoint(a, TG, turret, localN, along, p, n);
          shoot(a, p, n, kind, weapon, ap);
          ++fired; lastT = t; return false;
        }
        if (!a.projectiles.empty() || fired < shots || t - lastT < 0.15f) return t > 20;
        std::snprintf(b_, sizeof b_, "%s: %d hit(s) -> durability %d, %s (last: %s)", label, shots, k.hp, k.wrecked ? "DESTROYED" : "alive", a.tanks.lastHit.info.c_str());
        check(k.hp == expectHp && k.wrecked == expectWreck && a.tanks.wrecksMade == w0 + (expectWreck ? 1 : 0), b_);
        return true; }};
    };
    seq.push_back({"to damage range", [this](App& a, float t) {
      standAt(a, vec3(-30, 0, -20));
      return t > 0.2f; }});
    seq.push_back(caseStep("tank cannon AP x4 on the front hull", false, vec3(0, 0, 1), PROJ_HE, "17pdr_apcbc", 4, 20, false, 0));
    seq.push_back(caseStep("tank cannon AP x5 on the front hull", false, vec3(0, 0, 1), PROJ_HE, "17pdr_apcbc", 5, 0, true, 0));
    seq.push_back(caseStep("tank cannon HE x4 on the front turret", true, vec3(0, 0, 1), PROJ_HE, "17pdr_he", 4, 20, false, 0, false));
    seq.push_back(caseStep("tank cannon x1 on the left side (+x)", false, vec3(1, 0, 0), PROJ_HE, "17pdr_apcbc", 1, 50, false, 0));
    seq.push_back(caseStep("tank cannon x2 on the right side (-x)", false, vec3(-1, 0, 0), PROJ_HE, "17pdr_apcbc", 2, 0, true, 0));
    seq.push_back(caseStep("tank cannon x1 on the rear", false, vec3(0, 0, -1), PROJ_HE, "17pdr_apcbc", 1, 50, false, 0));
    seq.push_back(caseStep("tank cannon x2 on the rear", false, vec3(0, 0, -1), PROJ_HE, "17pdr_apcbc", 2, 0, true, 0));
    seq.push_back(caseStep("Flak-class 8.8 cm shell x2 on the side", false, vec3(1, 0, 0), PROJ_HE, "88mm_flak_he", 2, 0, true, 0, false));
    seq.push_back(caseStep("Flak-class shell x4 on the front", false, vec3(0, 0, 1), PROJ_HE, "88mm_flak_he", 4, 20, false, 0, false));
    seq.push_back(caseStep("bazooka x6 on the front hull", false, vec3(0, 0, 1), PROJ_ROCKET, "bazooka_m9", 6, 100, false, 0));
    seq.push_back(caseStep("Panzerschreck x6 on the front hull", false, vec3(0, 0, 1), PROJ_ROCKET, "panzerschreck", 6, 100, false, 0));
    seq.push_back(caseStep("Panzerschreck x4 on the front turret", true, vec3(0, 0, 1), PROJ_ROCKET, "panzerschreck", 4, 100, false, 0));
    seq.push_back(caseStep("bazooka x1 on the left side", false, vec3(1, 0, 0), PROJ_ROCKET, "bazooka_m9", 1, 50, false, 0));
    seq.push_back(caseStep("bazooka x2 on the right side", false, vec3(-1, 0, 0), PROJ_ROCKET, "bazooka_m9", 2, 0, true, 0));
    seq.push_back(caseStep("Panzerschreck x2 on the rear", false, vec3(0, 0, -1), PROJ_ROCKET, "panzerschreck", 2, 0, true, 0));
    seq.push_back(caseStep("Panzerschreck x1 on the left side", false, vec3(1, 0, 0), PROJ_ROCKET, "panzerschreck", 1, 50, false, 0));
    seq.push_back(caseStep("bazooka x1 on the rear", false, vec3(0, 0, -1), PROJ_ROCKET, "bazooka_m9", 1, 50, false, 0));
    seq.push_back(caseStep("launcher x3 on the roof", false, vec3(0, 1, 0), PROJ_ROCKET, "panzerschreck", 3, 100, false, 0));
    seq.push_back(caseStep("rifle x40 on the side", false, vec3(1, 0, 0), PROJ_BULLET, "30-06", 40, 100, false, 0));
    // turret traversed 90 deg: zones follow the turret frame, not the hull or the shooter's bearing
    seq.push_back(caseStep("turret at 90 deg: launcher from the hull front strikes the turret side", true, vec3(-1, 0, 0), PROJ_ROCKET, "panzerschreck", 1, 50, false, 90));
    seq.push_back(caseStep("turret at 90 deg: launcher from the hull side strikes the turret front", true, vec3(0, 0, 1), PROJ_ROCKET, "bazooka_m9", 3, 100, false, 90));
    seq.push_back(caseStep("turret at 90 deg: cannon on the turret front x4", true, vec3(0, 0, 1), PROJ_HE, "17pdr_apcbc", 4, 20, false, 90));
    seq.push_back({"turret-frame check", [this](App& a, float t) {
      TankRT& k = T(a, TG);
      fresh(a, TG, 90);
      vec3 p, n, p2, n2; facePoint(a, TG, true, vec3(-1, 0, 0), vec3(0), p, n); facePoint(a, TG, true, vec3(0, 0, 1), vec3(0), p2, n2);
      mat4 H = a.tanks.hullMatrix(k);
      vec3 hullFwd = vec3(H * vec4(0, 0, 1, 0)), hullLeft = vec3(H * vec4(1, 0, 0, 0));
      check(glm::dot(n, hullFwd) > 0.95f && glm::dot(n2, hullLeft) > 0.95f, "with the turret at 90 deg its side faces the hull front and its front faces the hull side (the cases above came from those bearings)");
      return true; }});
    seq.push_back({"zones: corners, underside", [this](App& a, float t) {
      TankRT& k = T(a, TG); fresh(a, TG, 0);
      mat4 H = a.tanks.hullMatrix(k); glm::mat3 R(H);
      vec3 c = vec3(H * vec4(k.def->armourHull.c + vec3(k.def->armourHull.h.x, 0, k.def->armourHull.h.z), 1));
      bool onT;
      int corner = a.tanks.classify(k, c, glm::normalize(R * vec3(1, 0, 1)), onT);
      int corner2 = a.tanks.classify(k, c, glm::normalize(R * vec3(1, 0, -1)), onT);
      vec3 under = vec3(H * vec4(0, 0.06f, 0, 1));
      int belly = a.tanks.classify(k, under, R * vec3(0, -1, 0), onT);
      check(corner == TZ_FRONT && corner2 == TZ_REAR, "45 deg corner: deterministic policy, front / rear wins the tie");
      TankHitResult r1 = a.tanks.applyHit(a, TG, TA_LAUNCHER, under, R * vec3(0, -1, 0), vec3(0, 1, 0));
      check(belly == TZ_BELLY && !r1.counted && k.hp == 100, "underside is classified as underside (never side); a launcher there does not count");
      TankHitResult r2 = a.tanks.applyHit(a, TG, TA_CANNON, under, R * vec3(0, -1, 0), vec3(0, 1, 0));
      check(r2.counted && k.hp == 50, "underside: a cannon hit counts as a side-class hit");
      return true; }});
    seq.push_back({"mixed hits", [this](App& a, float t) {
      TankRT& k = T(a, TG); fresh(a, TG, 0);
      mat4 H = a.tanks.hullMatrix(k); glm::mat3 R(H);
      vec3 fp = vec3(H * vec4(0, 1.0f, k.def->armourHull.h.z, 1)), sp = vec3(H * vec4(k.def->armourHull.h.x, 1.0f, 0, 1));
      a.tanks.applyHit(a, TG, TA_CANNON, sp, R * vec3(1, 0, 0), vec3(0));
      a.tanks.applyHit(a, TG, TA_CANNON, fp, R * vec3(0, 0, 1), vec3(0)); a.tanks.applyHit(a, TG, TA_CANNON, fp, R * vec3(0, 0, 1), vec3(0));
      bool alive = !k.wrecked && k.hp == 10;
      a.tanks.applyHit(a, TG, TA_CANNON, fp, R * vec3(0, 0, 1), vec3(0));
      check(alive && k.wrecked, "mixed: 1 side + 2 front hits leave 10 durability, a third front hit destroys (front 20, side 50 of 100)");
      return true; }});
    seq.push_back({"splash never damages tanks", [this](App& a, float t) {
      TankRT& k = T(a, TG); fresh(a, TG, 0);
      mat4 H = a.tanks.hullMatrix(k);
      vec3 beside = vec3(H * vec4(k.def->armourHull.h.x + 0.8f, 0.3f, 0, 1)); beside.y = a.terrain.height(beside.x, beside.z) + 0.1f;
      a.explode(beside, vec3(0, -1, 0), 320, 2.2f, 1.0f);
      a.blastAT = true; a.explode(beside + vec3(0, 0, 1.5f), vec3(0, -1, 0), 320, 0.9f, 1.0f, ~0ull, WALL_AP); a.blastAT = false;
      check(k.hp == 100 && !k.wrecked, "HE and AP blasts 0.8 m beside the hull: no tank damage (only direct hits count; there is no splash path)");
      return true; }});
    seq.push_back({"PaK 40 on the front", [this](App& a, float t) {
      // the real AT-gun path: crew the PaK, lay it on the Tiger's front plate, fire PzGr. 39
      static int phase;
      TankRT& k = T(a, TG);
      StationRT* s = a.station(PAK);
      if (t == 0) {
        fresh(a, TG, 0); phase = 0;
        if (a.tanks.occupied) a.tanks.exitTank(a, true);
        check(a.occupyStation(PAK, ST_GUNNER), "PaK 40 crewed through the station framework");
        s->loaded = 0; s->reload = 0; s->selAT = true; s->reserveAT = std::max(s->reserveAT, 2);
        a.scriptedInput.reload = true;
        return false;
      }
      if (phase == 0) { if (s->reload > 0 || t < 0.2f) return t > 10; phase = 1; }
      Instance* pi = a.doc.find(PAK);
      mat4 H = a.tanks.hullMatrix(k);
      vec3 tgt = vec3(H * vec4(0.3f, 1.0f, k.def->armourHull.h.z, 1));
      if (phase == 1) {
        vec3 dir; vec3 m = a.stationMuzzle(*pi, a.rt[PAK], dir);
        vec3 l = glm::inverse(yawQuat(s->carYaw)) * (tgt - m);
        float dist = glm::length(vec2(l.x, l.z)), tof = dist / 790.0f, drop = 0.5f * 9.81f * tof * tof;
        a.scriptedInput.dyaw = degrees(std::atan2(l.x, l.z)) - s->yawT; a.scriptedInput.dpitch = degrees(std::atan2(l.y + drop, dist)) - s->elevT;
        phase = 2; return false;
      }
      a.scriptedInput.dyaw = a.scriptedInput.dpitch = 0;
      if (phase == 2) { if (std::fabs(s->yaw - s->yawT) > 0.02f || std::fabs(s->elev - s->elevT) > 0.02f) return t > 15; a.scriptedInput.fire = true; phase = 3; return false; }
      a.scriptedInput.fire = false;
      if (phase == 3) { if (!a.projectiles.empty() || t < 0.5f) return t > 15; phase = 4; }
      std::snprintf(b_, sizeof b_, "PaK 40 PzGr. 39 on the Tiger's front (real station shot): counted, durability %d, no one-shot kill (%s)", k.hp, a.tanks.lastHit.info.c_str());
      check(k.hp == 80 && !k.wrecked, b_);
      a.leaveStation();
      return true; }});
    // ---------------------------------------------------------------- T7: walls and apertures with tank shells
    auto windowStep = [this](const char* label, int which, bool open, int kind) {
      return Step{label, [=, this](App& a, float t) {
        static vec3 c, n; static float i0; static int k, l; static std::string pathInfo;
        Instance* h = a.doc.find(HOUSE); auto& r = a.rt[HOUSE]; const AssetDef* ha = r.asset;
        if (t == 0) {
          // choose an unused glazed window and a point on one of its PANES (not a stile or glazing bar) whose straight
          // path from 14 m out is clear: the window's own leaf first when closed, nothing before 0.35 m inside when open
          static std::set<int> used;
          k = -1;
          for (size_t q = 0; q < ha->interactables.size() && k < 0; ++q) {
            auto& d = ha->interactables[q];
            bool glass = false; for (auto& lf : d.leaves) glass = glass || lf.glass.count > 0;
            if (d.kind != "window" || !glass || used.count((int)q)) continue;
            vec3 lc = (d.apMin + d.apMax) * 0.5f, ext = d.apMax - d.apMin, ac = (ha->bmin + ha->bmax) * 0.5f;
            vec3 ln(0), lu(0); if (ext.x < ext.z) { ln.x = lc.x > ac.x ? 1.0f : -1.0f; lu.z = 1; } else { ln.z = lc.z > ac.z ? 1.0f : -1.0f; lu.x = 1; }
            float eu = glm::dot(ext, lu);
            for (size_t o = 0; o < ha->interactables.size(); ++o) {   // the window closed / open, shutters in front of it open
              auto& od = ha->interactables[o];
              bool front = glm::length((od.apMin + od.apMax) * 0.5f - lc) < 0.7f;
              if ((int)o == (int)q) { r.progress[o] = open ? 1.0f : 0.0f; r.dir[o] = 0; a.updateLeafBodies(*h, r, (int)o); }
              else if (front) { r.progress[o] = 1.0f; r.dir[o] = 0; a.updateLeafBodies(*h, r, (int)o); }
            }
            const float off[4][2] = {{0.25f, 0.12f}, {-0.25f, 0.12f}, {0.25f, -0.2f}, {-0.25f, -0.2f}};
            for (auto& f : off) {
              vec3 lp = lc + lu * (f[0] * eu) + vec3(0, f[1] * ext.y, 0);
              vec3 cw = h->pos + yawQuat(h->yaw) * lp, nw = yawQuat(h->yaw) * ln;
              Physics::Hit hh; bool hit = Physics::castRayEx(cw + nw * 14.0f, -nw, 16.0f, hh);
              bool ok;
              if (open) ok = !hit || hh.t > 14.35f;
              else { int lf2 = -1; bool onG = false; ok = hit && hh.ud == ((HOUSE << 16) | (uint64_t)(q + 1)) && a.locateLeaf(*h, r, (int)q, hh.pos, lf2, onG) && onG; }
              if (ok) { k = (int)q; c = cw; n = nw; break; }
            }
          }
          if (k < 0) { check(false, std::string(label) + ": a glazed cottage window with a clear shot"); return true; }
          used.insert(k);
          pathInfo = firstHit(a, c + n * 14.0f, -n, 16.0f);
          i0 = a.lastImpact.time;
          if (kind == PROJ_ROCKET) shoot(a, c, n, PROJ_ROCKET, "panzerschreck"); else shoot(a, c, n, PROJ_HE, "17pdr_he", false);
          return false;
        }
        if (!a.projectiles.empty() || a.lastImpact.time <= i0) return t > 6;
        float depth = glm::dot(a.lastImpact.pos - c, -n);
        bool broken = false; for (auto s : r.leafSt[k]) broken = broken || (s & 1);
        std::snprintf(b_, sizeof b_, "%s: detonation %.2f m inside the facade on '%s'%s [window %s; first on the path before the shot: %s]", label, depth, a.lastImpact.surface.c_str(), open ? "" : (broken ? ", pane broken" : ", pane INTACT"), ha->interactables[k].id.c_str(), pathInfo.c_str());
        check(depth > 0.35f && (open || broken), b_);
        return true; }};
    };
    seq.push_back(windowStep("HE shell through a closed glazed window: the glass breaks and the shell detonates on the next solid surface inside", 0, false, PROJ_HE));
    seq.push_back(windowStep("Panzerschreck rocket through a closed glazed window", 1, false, PROJ_ROCKET));
    seq.push_back(windowStep("HE shell through an open window", 2, true, PROJ_HE));
    seq.push_back({"masonry: AP once, HE twice", [this](App& a, float t) {
      static int phase, pa, ph; static vec3 ca, na, ch, nh; static float i0;
      Instance* h = a.doc.find(HOUSE); auto& r = a.rt[HOUSE];
      if (t == 0) {
        phase = 0; pa = ph = -1;
        vec3 hc = (r.asset->bmin + r.asset->bmax) * 0.5f;
        auto wp = [&](int k, vec3& c, vec3& n) { auto& pn = r.asset->panels[k]; vec3 lc = (pn.bmin + pn.bmax) * 0.5f + vec3(0, -0.2f, 0); vec3 ln = pn.normal;
          if (glm::dot(ln, vec3(lc.x - hc.x, 0, lc.z - hc.z)) < 0) ln = -ln;   // outward
          lc += ln * 0.5f * glm::dot(pn.bmax - pn.bmin, glm::abs(ln));          // on the outer face
          c = h->pos + yawQuat(h->yaw) * lc; n = yawQuat(h->yaw) * ln; };
        // candidate bays, largest first: ground floor masonry whose path from 14 m out meets this house's wall body first
        std::vector<std::pair<float, int>> cand;
        for (size_t k = 0; k < r.asset->panels.size(); ++k) { auto& pn = r.asset->panels[k]; vec3 e = pn.bmax - pn.bmin; if (pn.bmin.y < 0.5f && pn.family == "masonry") cand.push_back({-std::max(e.x, e.z) * e.y, (int)k}); }
        std::sort(cand.begin(), cand.end());
        for (auto& [ar, k] : cand) {
          vec3 c, n; wp(k, c, n);
          Physics::Hit hh; if (!Physics::castRayEx(c + n * 14.0f, -n, 15.0f, hh) || hh.ud != ((HOUSE << 16) | 0xFFFF) || hh.t < 13.5f) continue;
          if (pa < 0) { pa = k; continue; }
          if (std::fabs(glm::dot(r.asset->panels[k].normal, r.asset->panels[pa].normal)) < 0.5f) { ph = k; break; }
        }
        if (pa < 0 || ph < 0) { check(false, "cottage has two wall bays to test"); return true; }
        wp(pa, ca, na); wp(ph, ch, nh);
        i0 = a.lastImpact.time; shoot(a, ca + na * 0.05f, na, PROJ_HE, "17pdr_apcbc", true); phase = 1; return false;
      }
      if (!a.projectiles.empty() || a.lastImpact.time <= i0) return t > 6;
      if (phase == 1) { check(r.state[pa] == PANEL_BREACHED, "tank AP shell on masonry: one hit breaches locally (canonical fortification rule, shared wall field) [panel " + r.asset->panels[pa].id + " " + r.asset->panels[pa].family + ", state " + std::to_string(r.state[pa]) + ", impact on " + a.lastImpact.surface + ", path: " + firstHit(a, ca + na * 14.0f, -na, 15.0f) + "]");
        i0 = a.lastImpact.time; shoot(a, ch + nh * 0.05f, nh, PROJ_HE, "17pdr_he", false); phase = 2; return false; }
      if (phase == 2) { check(r.state[ph] != PANEL_BREACHED, "tank HE shell on masonry: the first hit only craters / weakens");
        i0 = a.lastImpact.time; shoot(a, ch + nh * 0.05f, nh, PROJ_HE, "17pdr_he", false); phase = 3; return false; }
      check(r.state[ph] == PANEL_BREACHED, "a second tank HE hit at the same spot breaches");
      return true; }});
    // ---------------------------------------------------------------- the player's own tank knocked out
    seq.push_back({"wreck with occupant", [this](App& a, float t) {
      TankRT& k = T(a, SH);
      static int w0;
      if (t == 0) {
        k.hp = 100; k.wrecked = false; place(a, SH, vec3(10, 0, -10), 0);
        standAt(a, k.pos + vec3(-3.4f, 0, 0.5f)); lookAtP(a, k.pos + vec3(0, 1.2f, 0.5f));
        return false;
      }
      if (t < 0.3f) { lookAtP(a, k.pos + vec3(0, 1.2f, 0.5f)); return false; }
      if (once(t, 0.3f)) { a.scriptedInput.interact = true; return false; }
      if (t < 0.6f) return false;
      if (once(t, 0.6f)) {
        check(a.tanks.occupied == SH, "re-entered the Sherman (target " + std::to_string(a.tanks.target) + ", station " + std::to_string(a.occupied) + ")");
        w0 = a.tanks.wrecksMade; k.reload = 2.0f;
        vec3 p, n; facePoint(a, SH, false, vec3(1, 0, 0), vec3(0, -0.2f, 0.4f), p, n);
        shoot(a, p, n, PROJ_ROCKET, "panzerschreck"); return false;
      }
      if (t < 0.8f) return false;
      if (once(t, 0.8f)) { vec3 p, n; facePoint(a, SH, false, vec3(-1, 0, 0), vec3(0, -0.2f, -0.4f), p, n); shoot(a, p, n, PROJ_ROCKET, "panzerschreck"); return false; }
      if (!a.projectiles.empty() || t < 1.2f) return t > 6;
      auto ch = Physics::character();
      check(k.wrecked && a.tanks.wrecksMade == w0 + 1 && a.tanks.occupied == 0 && !Physics::capsuleBlocked(ch.pos + vec3(0, 0.08f, 0), 1.74f, 0.3f),
            "two side launcher hits knock out the occupied Sherman exactly once; the player bails out to a clear spot");
      check(k.reload == 0 && k.loaded == SHELL_NONE && k.engineLoop < 0 && k.trackLoop < 0, "the wreck stops loading, weapons and engine / track sounds");
      bool enter = a.tanks.enter(a, SH);
      check(!enter && a.tanks.occupied == 0, "a wreck cannot be entered");
      return true; }});
    seq.push_back({"wreck smoke", [this](App& a, float t) { if (t == 0) standAt(a, T(a, SH).pos + vec3(-9, 0, -9)); lookAtP(a, T(a, SH).pos + vec3(0, 1.5f, 0)); if (t < 2.0f) return false; shotQueue.push_back("tanks_07_wreck_smoke"); return true; }});
    // ---------------------------------------------------------------- Tiger: optics, exhaust, sound, hatch
    seq.push_back({"tiger sight", [this](App& a, float t) {
      TankRT& g = T(a, TG);
      if (t == 0) { g.hp = 100; g.wrecked = false; g.tYaw = 0; a.tanks.updateBody(a, g, true); standAt(a, g.pos + yawQuat(g.yaw) * vec3(3.9f, 0, 0.5f)); }
      lookAtP(a, g.pos + vec3(0, 1.2f, 0) + yawQuat(g.yaw) * vec3(0, 0, 0.5f));
      if (t < 0.3f) return false;
      if (once(t, 0.3f)) { a.scriptedInput.interact = true; return false; }
      if (t < 1.0f) return false;
      check(a.tanks.occupied == TG, "entered the Tiger through its prompt");
      vec3 hd = yawQuat(g.yaw) * vec3(0, 0, 1); aimWorld(a, hd);
      a.uiShot = outDirT() / "tanks_08_tiger_sight.png";
      return true; }});
    seq.push_back({"tiger zoom + exhaust", [this](App& a, float t) {
      TankRT& g = T(a, TG);
      if (t == 0) { a.scriptedInput.aim = true; a.scriptedInput.move = vec2(0, 0.6f); }
      if (t < 1.5f) return false;
      vec3 e, f; float fov; a.tanks.camera(a, e, f, fov);
      check(std::fabs(fov - g.def->fovZoom) < 0.01f, "RMB switches the gunner sight to its higher magnification");
      a.uiShot = outDirT() / "tanks_09_tiger_sight_zoom.png";
      a.scriptedInput.aim = false;
      int parts = 0; for (auto& p : a.fx.parts) parts += p.col.a < 0.4f && p.col.r < 0.4f && p.col.r > 0.25f;
      check(parts > 0 && g.rpm > 0.2f, "engine running: exhaust particles from the outlets, rpm follows the load (" + std::to_string(parts) + " particles)");
      check(!a.audio.ok() || a.audio.loopsActive() >= 1, std::string("engine / track loops playing (") + (a.audio.ok() ? std::to_string(a.audio.loopsActive()) + " voices)" : "no audio device)"));
      a.scriptedInput.move = vec2(0);
      return true; }});
    seq.push_back({"perf", [this](App& a, float t) {
      // indicative only (the GPU is shared with other programs): both tanks in view, the Tiger driving with exhaust
      static double cpu, gpu, step, stepMax; static int n, draws, tris;
      if (t == 0) { cpu = gpu = step = stepMax = 0; n = 0; a.tanks.stepMsMax = 0; a.scriptedInput.move = vec2(0.3f, 0.7f); aimWorld(a, glm::normalize(T(a, SH).pos - T(a, TG).pos)); }
      cpu += a.R.stats.cpuMs; gpu += a.R.stats.gpuMs; step += a.tanks.stepMs; ++n; draws = a.R.stats.draws; tris = a.R.stats.tris;
      if (t < 2.0f) return false;
      a.scriptedInput.move = vec2(0);
      summary["perf_render_cpu_ms"] = cpu / n; summary["perf_render_gpu_ms"] = gpu / n; summary["perf_tank_step_ms"] = step / n; summary["perf_tank_step_max_ms"] = a.tanks.stepMsMax;
      summary["perf_draws"] = draws; summary["perf_tris"] = tris;
      std::snprintf(b_, sizeof b_, "indicative cost, 2 tanks (one driving, exhaust, loops): tank simulation %.3f ms / step (max %.3f), render CPU %.2f ms, GPU %.2f ms, %d draws, %d tris", step / n, a.tanks.stepMsMax, cpu / n, gpu / n, draws, tris);
      check(step / n < 2.0, b_);
      return true; }});
    seq.push_back({"tiger hatch", [this](App& a, float t) {
      TankRT& g = T(a, TG);
      if (t == 0) { a.scriptedInput.crouch = true; }
      if (t < 0.1f) return false;
      a.scriptedInput.crouch = false;
      if (t < 1.2f) return false;
      aimWorld(a, glm::normalize(yawQuat(g.yaw) * vec3(-0.3f, 0.02f, 1)));
      if (t < 1.6f) return false;
      check(g.station == TS_HATCH, "Tiger hatch / cupola MG station");
      a.uiShot = outDirT() / "tanks_10_tiger_hatch_mg.png";
      return true; }});
    seq.push_back({"tiger mg", [this](App& a, float t) {
      TankRT& g = T(a, TG);
      static int m0;
      if (t == 0) m0 = g.mgLoaded;
      a.scriptedInput.fire = t < 0.6f;
      if (t < 0.8f) return false;
      vec3 md; vec3 mm = a.tanks.mgMuzzleWorld(g, md);
      std::snprintf(b_, sizeof b_, "Tiger cupola MG fires in hatch mode (%d rounds; mg yaw %.1f elev %.1f, muzzle y %.2f above hull origin, dir y %.2f, notice '%s')", m0 - g.mgLoaded, g.mgYaw, g.mgElev, mm.y - g.pos.y, md.y, a.notice.c_str());
      check(g.mgLoaded < m0, b_);
      return true; }});
    seq.push_back({"tiger: Space fires cupola + bow MG 42", [this](App& a, float t) {
      TankRT& g = T(a, TG);
      static int m0, b0; static std::set<std::string> snd;
      if (t == 0) { m0 = g.mgLoaded; b0 = g.bowLoaded; snd.clear(); a.scriptedInput.jumpHeld = true; }
      if (a.audio.lastPlayed.rfind("shot_", 0) == 0) snd.insert(a.audio.lastPlayed);
      if (t < 0.8f) return false;
      a.scriptedInput.jumpHeld = false;
      int nb = b0 - g.bowLoaded;
      std::snprintf(b_, sizeof b_, "Tiger hatch: Space fires the cupola MG and the MG 42 type bow MG together (%d / %d rounds in 0.8 s, bow at %.0f rpm, %d-round belts, sound '%s')",
                    m0 - g.mgLoaded, nb, g.def->bowRpm, g.def->bowBelt, snd.empty() ? "" : snd.begin()->c_str());
      check(g.mgLoaded < m0 && nb >= 13 && nb <= 18 && g.def->bowBelt == 50 && snd.count("shot_mg42") && g.def->bowSound == "shot_mg42", b_);
      return true; }});
    seq.push_back({"tiger out", [this](App& a, float t) {
      TankRT& g = T(a, TG);
      static float t0;
      if (t == 0) { a.scriptedInput.crouch = true; t0 = -1; return false; }
      a.scriptedInput.crouch = false;
      if (std::fabs(g.speed) > 0.01f || t < 1.2f) return t > 10;
      if (t0 < 0) { t0 = t; a.scriptedInput.interact = true; return false; }
      if (t < t0 + 0.2f) return false;
      check(a.tanks.occupied == 0, "out of the Tiger");
      return true; }});
    // ---------------------------------------------------------------- persistence: Apply, undo / redo, duplicate, save / reopen
    seq.push_back({"apply", [this](App& a, float t) {
      TankRT& g = T(a, TG);
      g.hp = 50; g.hitsSide = 1; g.wrecked = false; g.reserveAP = 7;
      a.applyDamageToMap(); a.exitExplore();
      bool poles = a.rt.count(POLE1) && a.rt[POLE1].staticBody != 0xffffffffu && a.tanks.knocked.empty() && a.tanks.ramWorkDone.empty();
      check(poles, "leaving Explore puts knocked-over props back (session overlay: static bodies restored, nothing left lying)");
      Instance* i = a.doc.find(TG);
      bool kept = i->extra.contains("tank") && i->extra["tank"].value("hp", 0) == 50 && i->extra["tank"].value("reserveAP", 0) == 7;
      check(kept, "Apply session changes keeps the Tiger's damage and ammunition in the map (one undo step)");
      Instance* s = a.doc.find(SH);
      check(s->extra.contains("tank") && s->extra["tank"].value("wrecked", false), "the Sherman wreck is applied too");
      a.undo();
      check(!a.doc.find(TG)->extra.contains("tank"), "undo restores the undamaged Tiger");
      a.redo();
      check(a.doc.find(TG)->extra.contains("tank") && a.doc.find(TG)->extra["tank"].value("hp", 0) == 50, "redo re-applies it");
      a.selected = SH;
      size_t n0 = a.doc.instances.size();
      a.duplicateSelected();
      Instance* dup = a.doc.instances.size() > n0 ? &a.doc.instances.back() : nullptr;
      check(dup && dup->assetId == "tank_sherman_firefly" && !(dup->extra.is_object() && dup->extra.contains("tank")), "duplicating the wrecked Sherman gives a fresh tank (no damage history, no occupant)");
      check(a.saveMapTo(outDirT() / "tanks_saved.nmap.json"), "map saved");
      return true; }});
    seq.push_back({"reopen", [this](App& a, float t) {
      if (t == 0) { a.openMap(outDirT() / "tanks_saved.nmap.json"); return false; }
      if (t < 0.2f) return false;
      a.syncRuntime(); a.tanks.sync(a);
      check(a.enterExplore(), "F5 on the reopened map");
      TankRT* s = a.tanks.get(SH); TankRT* g = a.tanks.get(TG);
      check(s && g && s->wrecked && g->hp == 50 && g->reserveAP == 7 && g->reload == 0 && g->loaded != SHELL_NONE,
            "reopened map: the wreck stays a wreck, the Tiger keeps durability 50 and 7 AP; nothing was reloaded for free");
      shotQueue.push_back("tanks_11_reopened");
      return true; }});
    seq.push_back({"back", [this](App& a, float t) { if (t < 0.2f) return false; a.exitExplore(); return true; }});
  }
  stage = 2601;
  if (cur >= seq.size()) { finish(app); return; }
  if (tcur == 0) logf("[selftest] step: %s", seq[cur].name.c_str());
  dt_ = dt;
  bool done2 = seq[cur].fn(app, tcur);
  tcur += dt;
  if (done2) { ++cur; tcur = 0; marks.clear(); }
}
