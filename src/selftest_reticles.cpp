// --capture-reticles (AT2): every optic in ADS (the guns' own red dots, the fitted red dot / holographic / ACOG / 4x / 10x /
// thermal, the scoped rifles, a blaster) with a soldier 100 m ahead: selftest_out/ret_<tag>.png (with the HUD), no sway.
// Then the zero check: one real shot per case, its trajectory (start, direction, gravity) at 25 / 50 / 100 m projected
// with the world camera and compared with the drawn mark (the red dot / holographic mark: sight_overlay.cpp; the others:
// the screen centre). [ret] lines in the log.
#include "selftest.h"
#include "game.h"
#include "mpui.h"
#include "mptest.h"
#include "loot.h"
#include "sight_overlay.h"
#include "br_models.h"
#include "vm_anim.h"
#include <functional>
#include <set>
#include <GLFW/glfw3.h>
#include <stb_image_write.h>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0;
MpTest* T = nullptr; int mode = -1; bool started = false;
int bot = -1; vec3 botAt{0}; float aimYaw = 0, aimPitch = 0;
fs::path outDir() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
void push(const std::string& n, std::function<bool(App&, float)> f) { seq.push_back({n, f}); }

void holdBot() {
  if (bot < 0 || !gGame) return;
  Soldier& b = gGame->sold[bot];
  if (b.agent >= 0) Physics::setAgentPos(b.agent, botAt);
  b.feet = botAt; b.vel = vec3(0); b.hp = 1000; b.protectUntil = -1; b.weapon = -1;
  if (Soldier* L = gGame->local()) b.yaw = aimYaw + 180;
}

std::set<std::string>& skipped() { static std::set<std::string> s; return s; }
struct Case { const char* tag; const char* gun; int att; bool hip = false; };
const Case kCases[] = {
  {"ak12_own", "modern_ak12", 0}, {"glauncher_own", "modern_glauncher", 0}, {"mp7_own", "modern_mp7", 0},
  {"shotgun_own", "modern_shotgun", 0}, {"smg9_own", "modern_smg9", 0},
  {"ak_reddot", "modern_ak", 1}, {"ak_holo", "modern_ak", 5}, {"ak_acog", "modern_ak", 2}, {"ak_4x", "modern_ak", 3}, {"ak_10x", "modern_ak", 4},
  {"ak_thermal", "modern_ak", 6},
  {"sniper_bolt_mil", "modern_sniper_bolt", 0}, {"sniper_50_bmg", "modern_sniper_50", 0}, {"sniper_semi_acog", "modern_sniper_semi", 0},
  {"ar15_plus", "modern_ar15", 0}, {"kar98k_zf39", "kar98k_zf39", 0}, {"m1c", "m1c_garand", 0}, {"blaster_rifle", "blaster_rifle", 0},
  // AT2 sheet: each fitted optic on four guns (animated rigs and static models), and every red dot from the hip
  {"m4_reddot", "modern_m4_irons", 1}, {"scar_reddot", "modern_scar", 1}, {"kar98k_reddot", "kar98k", 1},
  {"m4_holo", "modern_m4_irons", 5}, {"scar_holo", "modern_scar", 5}, {"kar98k_holo", "kar98k", 5},
  {"m4_acog", "modern_m4_irons", 2}, {"scar_acog", "modern_scar", 2}, {"kar98k_acog", "kar98k", 2},
  {"m4_4x", "modern_m4_irons", 3}, {"scar_4x", "modern_scar", 3}, {"kar98k_4x", "kar98k", 3},
  {"m4_thermal", "modern_m4_irons", 6}, {"scar_thermal", "modern_scar", 6}, {"kar98k_thermal", "kar98k", 6},
  {"sniper_bolt_10x", "modern_sniper_bolt", 4}, {"sniper_semi_10x", "modern_sniper_semi", 4}, {"sniper_50_10x", "modern_sniper_50", 4}, {"vsk_10x", "modern_vsk", 4},
  {"ak12_own_hip", "modern_ak12", 0, true}, {"glauncher_own_hip", "modern_glauncher", 0, true}, {"mp7_own_hip", "modern_mp7", 0, true},
  {"shotgun_own_hip", "modern_shotgun", 0, true}, {"smg9_own_hip", "modern_smg9", 0, true}, {"ak_reddot_hip", "modern_ak", 1, true},
};

void build(App& app) {
  if (const char* dump = std::getenv("NMK_OPTIC_DUMP")) {   // AT7: every gun x optic it takes, its seated mount (weapon space): the offline audit's input
    json out = json::array();
    for (auto& dp : app.weaponDefs) {
      const WeaponDef& d = *dp; json g = {{"id", d.id}, {"file", d.file.string()}, {"rail", {d.rail.x, d.rail.y, d.rail.z}}, {"hasRail", d.hasRail}, {"railScale", d.railScale}, {"hasScope", d.hasScope}, {"useModel", d.useModel()}, {"muzzle", {d.muzzle.x, d.muzzle.y, d.muzzle.z}}, {"optics", json::object()}};
      for (int k = 1; k <= 7; ++k) {
        const ItemDef* it = lootItem(lootAttachItemId(0, k)); if (!it || !lootAttachFits(d, *it)) continue;
        WeaponState ws; ws.att[0] = k; vec3 c; float r; mat4 M;
        if (!brSightWindow(d, ws, c, r, &M)) continue;
        g["optics"][std::to_string(k)] = {{"base", {M[3].x, M[3].y, M[3].z}}, {"scale", glm::length(vec3(M[0]))}, {"audit", brOpticAudit(d, k)}};
      }
      out.push_back(g);
    }
    std::ofstream(dump) << out.dump(1); logf("[ret] optic dump: %s", dump);
  }
  if (const char* pv = std::getenv("NMK_OPTIC_PREVIEW")) {   // AT7: the smithing preview's draws (gun, parts, optic), side-on and from the rear quarter: preview_<gun>_<sight>.png
    push("previews", [l = std::string(pv)](App& a, float t) {
      if (t < 0.5f) return false;
      size_t p = 0;
      while (p < l.size()) {
        size_t q = l.find(',', p); if (q == std::string::npos) q = l.size(); std::string e = l.substr(p, q - p); p = q + 1;
        size_t c = e.find(':'); if (c == std::string::npos) continue;
        int w = a.weaponIndex(e.substr(0, c)), k = std::atoi(e.c_str() + c + 1); if (w < 0) continue;
        const WeaponDef& d = *a.weaponDefs[w]; WeaponState ws; ws.att[0] = k;
        for (int view = 0; view < 2; ++view) {
          std::vector<ModelDraw> md; std::vector<DrawItem> bx;
          vec3 lo0 = d.useModel() ? d.model->bmin : vec3(-0.1f, -0.1f, 0), hi0 = d.useModel() ? d.model->bmax : vec3(0.1f, 0.1f, 1);
          mat4 W = glm::rotate(mat4(1), glm::radians(view == 0 ? -90.0f : -140.0f), vec3(0, 1, 0)) * glm::translate(mat4(1), -(lo0 + hi0) * 0.5f);
          if (d.useModel()) for (int r = 0; r < 8; ++r) for (int pk : d.modelParts[r]) md.push_back({d.model.get(), pk, W});
          brSightDraws(a, d, ws, W, md, bx);
          float r = std::max(0.2f, glm::length(hi0 - lo0) * 0.5f); vec3 eye(0, r * 0.15f, r * 2.2f); const int PW = 960, PH = 400;
          std::vector<uint8_t> px;
          if (!a.R.renderModelSheet(md, bx, glm::lookAt(eye, vec3(0), vec3(0, 1, 0)), glm::perspective(glm::radians(24.0f), (float)PW / PH, 0.02f, r * 10), eye, PW, PH, px)) continue;
          fs::path f = outDir() / ("preview_" + d.id + "_" + std::to_string(k) + (view ? "_q" : "") + ".png");
          stbi_write_png(f.string().c_str(), PW, PH, 4, px.data(), PW * 4);
        }
      }
      return true; });
  }
  push("start", [](App& a, float t) {
    if (t == 0) {
      if (a.mode == Mode::Explore) a.exitExplore();
      a.openMap(contentDir() / "examples/village_assault_3.nmap.json");
      json scj = a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object();
      Scenario sc = Scenario::fromJson(scj);
      sc.rules.mode = GameMode::TDM; sc.rules.killCap = 999; sc.rules.teamPlayers[0] = 2; sc.rules.teamPlayers[1] = 1; sc.rules.respawnDelay = 2;
      sc.rules.humanTeam = 0; sc.rules.aiTypes[0] = aiBit(AiType::Soldiers); sc.rules.aiTypes[1] = aiBit(AiType::Soldiers);
      gUi.loaded = false; gUi.matchScenario = sc.toJson();
      if (!gGame) gGame = new Game(a);
      gGame->pendingStart = true; a.doc.start = {true, vec3(0, 0, 30), 180};
      T->check(a.enterExplore() && gGame->active, "TDM started (" + gGame->setupError + ")");
      a.useScripted = true; a.scriptedInput = ExploreInput{};
      return false;
    }
    Game& G = *gGame; Soldier* L = G.local();
    for (auto& s : G.sold) if (!s.alive) return t > 30 ? (T->check(false, "everyone spawns"), true) : false;
    if (!L) return false;
    for (auto& s : G.sold) if (!s.localPlayer && s.faction == L->faction) bot = s.slot;
    for (auto& s : G.sold) if (s.faction != L->faction) { s.hp = 1000; if (s.agent >= 0) Physics::setAgentPos(s.agent, L->feet + vec3(300, 0, 300)); s.feet = L->feet + vec3(300, 0, 300); }
    L->hp = 5000; L->protectUntil = -1;   // no spawn-protection ring over the sight picture
    // a clear 100 m line at eye height: the first yaw (15 deg steps) with nothing in the way and ground under the far end
    vec3 eye = a.eyePos(); bool found = false;
    for (int k = 0; k < 24 && !found; ++k) {
      float yaw = a.eyeYaw + 15.0f * k; vec3 f = App::forward(yaw, 0); Physics::Hit h;
      if (Physics::castRayEx(eye, f, 104.0f, h)) continue;
      vec3 far = eye + f * 100.0f; Physics::Hit g;
      if (!Physics::castRayEx(far + vec3(0, 30, 0), vec3(0, -1, 0), 60.0f, g)) continue;
      if (g.pos.y > eye.y - 0.8f || g.pos.y < eye.y - 4.0f) continue;   // standing ground about level with ours
      aimYaw = yaw; botAt = g.pos; found = true;
    }
    if (!found) { aimYaw = a.eyeYaw; botAt = eye + App::forward(aimYaw, 0) * 100.0f - vec3(0, 1.6f, 0); }
    aimPitch = degrees(std::atan2(botAt.y + 1.3f - eye.y, glm::length(vec2(botAt.x - eye.x, botAt.z - eye.z))));   // the chest
    logf("[ret] target 100 m: clear line %s, yaw %.1f pitch %.2f, frame %dx%d", found ? "yes" : "NO", aimYaw, aimPitch, a.R.width, a.R.height);
    return true; });
  if (std::getenv("NMK_ARMS_AUDIT")) push("arms audit", [](App& a, float t) {   // AT7: every gun in first person, hip then fully aimed: its hand parts (the rig's own arms or the generated one)
    static size_t k = 0; static int phase = 0, bad = 0; static float t0 = 0; static std::string line;
    auto count = [&](const WeaponDef& d, std::string& ms) {
      a.R.viewModelTex.clear(); a.R.viewModel.clear(); std::vector<DrawItem> tmp; a.weaponDrawItems(tmp);
      int n = 0; std::set<std::string> mats;
      for (auto& it : a.R.viewModelTex) {
        if (fpsArmsIsModel(it.model)) { ++n; mats.insert("generated"); continue; }
        if (!d.anim) continue;
        const auto& pt = it.model->parts[it.part]; std::string m = pt.mat >= 0 && pt.mat < (int)it.model->mats.size() ? it.model->mats[pt.mat].name : "?";
        std::string l = m; for (auto& c : l) c = (char)std::tolower((unsigned char)c);
        bool arm = false; for (const char* w : {"arm", "hand", "glove", "sleeve", "skin", "shirt", "cloth", "body_", "manny", "male", "gear", "mg1_mat"}) arm = arm || l.find(w) != std::string::npos;
        if (arm) { ++n; mats.insert(m); }
      }
      for (auto& x : mats) ms += x + " "; return n;
    };
    while (k < a.weaponDefs.size() && (a.weaponDefs[k]->equipment() || a.weaponDefs[k]->action == "heavy" || a.weaponDefs[k]->melee.on || a.weaponDefs[k]->tool)) ++k;
    if (k >= a.weaponDefs.size()) { a.scriptedInput = ExploreInput{}; T->check(bad == 0, "every gun draws hands at the hip (" + std::to_string(bad) + " without)"); return true; }
    const WeaponDef& d = *a.weaponDefs[k];
    if (phase == 0) { std::string m; a.equipWeapon((int)k, m); a.scriptedInput = ExploreInput{}; t0 = t; phase = 1; line.clear(); return false; }
    if (phase == 1) { if (t - t0 < 0.6f) return false; std::string ms; int n = count(d, ms); line = " hip " + std::to_string(n) + " (" + ms + ")"; if (n == 0) ++bad; a.scriptedInput.aim = true; t0 = t; phase = 2; return false; }
    if (a.adsT < 0.999f && t - t0 < 3.0f) return false;
    std::string ms; int n = count(d, ms); line += " | ADS " + std::to_string(n) + " (" + ms + ")";
    logf("[arms] %-20s %s%s", d.id.c_str(), d.anim ? "rig   " : "static", line.c_str());
    a.scriptedInput = ExploreInput{}; phase = 0; ++k; return false; });
  if (const char* only = std::getenv("NMK_RET_GUNS")) {   // NG2: just these guns' first-person views (hip, ADS, ADS 0.1 s after a shot): ret_view_<gun>_<view>.png
    std::string l = only; size_t p = 0;
    while (p <= l.size()) {
      size_t q = l.find(',', p); if (q == std::string::npos) q = l.size();
      std::string gun = l.substr(p, q - p); p = q + 1; if (gun.empty()) continue;
      int sight = 0; if (size_t at = gun.find('@'); at != std::string::npos) { sight = std::atoi(gun.c_str() + at + 1); gun = gun.substr(0, at); }   // AT7: gun@sight fits that optic
      for (int v = 0; v < 3; ++v) push("view " + gun, [gun, v, sight](App& a, float t) {
        holdBot();
        int w = a.weaponIndex(gun); if (w < 0) { T->check(false, gun + " exists"); return true; }
        if (t == 0) { std::string m; if (a.activeWeapon != w) a.equipWeapon(w, m); WeaponState& s = a.wstate[gun]; s.loaded = std::max(s.loaded, 5); s.att[0] = sight; a.vm.procedural = false; a.scriptedInput = ExploreInput{}; a.scriptedInput.aim = v > 0; }
        a.eyeYaw = aimYaw; a.eyePitch = aimPitch; a.viewKickYaw = a.viewKickPitch = 0;
        if (t < 1.8f || (v > 0 && a.adsT < 0.999f && t < 6.0f)) return false;
        static float shotAt = -1;
        if (v == 2) { if (shotAt < 0 || shotAt > t) { shotAt = t; a.scriptedInput.fire = true; return false; } a.scriptedInput.fire = false; if (t - shotAt < 0.1f) return false; shotAt = -1; }
        a.uiShot = outDir() / ("ret_view_" + gun + (sight ? "@" + std::to_string(sight) : std::string()) + (v == 0 ? "_hip" : v == 1 ? "_ads" : "_fire") + ".png");
        return true; });
    }
    push("done", [](App& a, float t) { if (t < 0.5f) return false; a.vm.procedural = true; a.scriptedInput = ExploreInput{}; if (a.mode == Mode::Explore) a.exitExplore(); return true; });
    return;
  }
  // the guns' own sights first (Battle Royale turns a sniper's own scope into its ACOG), then the fitted optics
  for (int pass = 0; pass < 2; ++pass) {
  if (pass == 1) push("battle royale", [](App&, float) { gGame->match.rules.mode = GameMode::BattleRoyale; lootBegin(*gGame); return true; });   // BR2
  // AT2: a fitted ACOG / 4x / 10x / thermal gives the scoped view at its zoom, outside Battle Royale (SM3's free fits) and in it
  for (const char* g : {"modern_ak", "modern_m4_irons", "modern_scar", "modern_fal", "modern_sniper_bolt"}) for (int k : {2, 3, 4, 6}) {
    std::string gun = g; int sight = k, ps = pass;
    push("scope " + gun, [gun, sight, ps](App& a, float t) {
      int w = a.weaponIndex(gun); const ItemDef* it = lootItem(lootAttachItemId(0, sight));
      if (w < 0 || !it || !lootAttachFits(*a.weaponDefs[w], *it)) return true;
      if (t == 0) { std::string m; if (a.activeWeapon != w) a.equipWeapon(w, m); a.wstate[gun].att[0] = sight; a.scriptedInput = ExploreInput{}; a.scriptedInput.aim = true; }
      if (a.activeWeapon != w) { std::string m; a.equipWeapon(w, m); a.wstate[gun].att[0] = sight; if (t > 4.0f) { T->check(false, gun + ": could not be equipped"); return true; } return false; }
      static float inHand = 0; if (t < 0.02f || inHand > t) inHand = t; if (a.adsT <= 0) inHand = std::max(inHand, t);
      if (t - inHand < 1.0f && t < 6.0f) return false;   // a second of aiming once the gun is in hand (6 s at most)
      mat4 view; float wf; a.fpCamera(view, wf);
      float mag = sight == 2 ? 2.0f : sight == 4 ? 10.0f : 4.0f;
      T->check(a.scopeView() && std::fabs(wf - 72.0f / mag) < 0.2f, gun + " sight " + std::to_string(sight) + (ps ? " (Battle Royale)" : " (outside Battle Royale)") + ": scoped view at " + std::to_string(72.0f / std::max(0.01f, wf)) + "x (" + std::to_string(mag) + "x wanted)");
      a.wstate[gun].att[0] = 0; a.scriptedInput = ExploreInput{};
      return true; });
  }
  if (pass == 1) push("holo scale", [](App& a, float t) {   // AT2: the holographic sight's mount is uniform on every gun it fits (its ring a true circle)
    static size_t i = 0; static int n = 0, bad = 0;
    const ItemDef* it = lootItem("br_sight_holo");
    while (i < a.weaponDefs.size() && (!it || a.weaponDefs[i]->hasScope || !lootAttachFits(*a.weaponDefs[i], *it))) ++i;
    if (i >= a.weaponDefs.size()) { T->check(n > 0 && bad == 0, "the holographic sight mounts at uniform scale on all " + std::to_string(n) + " guns it fits (" + std::to_string(bad) + " stretched)"); return true; }
    static float t0 = -1; if (t0 < 0 || t < t0) t0 = t;
    const WeaponDef& d = *a.weaponDefs[i]; WeaponState& ws = a.wstate[d.id];
    if (t - t0 < 0.05f) { std::string m; if (a.activeWeapon != (int)i) a.equipWeapon((int)i, m); ws.att[0] = 5; a.scriptedInput = ExploreInput{}; return false; }
    if (t - t0 < 0.5f) return false;
    vec3 c, lo, hi; float r; mat4 M, Wm;
    bool ok = brSightWindow(d, ws, c, r, &M, &lo, &hi) && (d.anim ? animVmWeaponMatrix(a, d, Wm) : (Wm = a.viewModelMatrix(d, ws), true));
    if (ok) {
      glm::mat3 L = glm::mat3(Wm * M); vec3 sc(glm::length(L[0]), glm::length(L[1]), glm::length(L[2]));
      float ratio = std::max({sc.x, sc.y, sc.z}) / std::max(1e-6f, std::min({sc.x, sc.y, sc.z}));
      float skew = std::max({std::fabs(glm::dot(L[0], L[1])) / (sc.x * sc.y), std::fabs(glm::dot(L[1], L[2])) / (sc.y * sc.z), std::fabs(glm::dot(L[0], L[2])) / (sc.x * sc.z)});
      vec3 dim = (hi - lo) * sc * 1000.0f, own = (hi - lo) * 1000.0f;
      ++n; if (ratio > 1.01f || skew > 0.01f) ++bad;
      logf("[holo] %-20s mounted %.1f x %.1f x %.1f mm (model %.1f x %.1f x %.1f), axis scales %.3f %.3f %.3f (ratio %.4f, skew %.4f)", d.id.c_str(), dim.x, dim.y, dim.z, own.x, own.y, own.z, sc.x, sc.y, sc.z, ratio, skew);
    } else logf("[holo] %s: no mount", d.id.c_str());
    ws.att[0] = 0; ++i; t0 = -1; return false; });
  for (const Case& c : kCases) {
    if ((c.att != 0) != (pass == 1)) continue;
    Case cc = c;
    push(std::string("aim ") + c.tag, [cc](App& a, float t) {
      holdBot();
      if (t == 0) {
        int w = a.weaponIndex(cc.gun); std::string msg;
        T->check(w >= 0, std::string(cc.gun) + " exists");
        if (w < 0) return true;
        if (cc.att && !lootSightFits(*a.weaponDefs[w], cc.att)) { logf("[ret] %s: sight %d does not fit %s: skipped", cc.tag, cc.att, cc.gun); skipped().insert(cc.tag); return true; }
        if (a.activeWeapon != w) a.equipWeapon(w, msg);
        WeaponState& s = a.wstate[a.weaponDefs[w]->id]; s.att[0] = cc.att; s.loaded = std::max(s.loaded, 5);
        a.vm.procedural = false;   // no sway, bob or breathing
        a.scriptedInput = ExploreInput{}; a.scriptedInput.aim = !cc.hip;
      }
      a.eyeYaw = aimYaw; a.eyePitch = aimPitch; a.viewKickYaw = a.viewKickPitch = 0;
      if (t < 1.8f || (!cc.hip && a.adsT < 0.999f && t < 6.0f)) return false;   // fully aimed (a bolt gun's bolt / cooldown resume when it is back in hand)
      a.uiShot = outDir() / (std::string("ret_") + cc.tag + ".png");
      return true; });
    push(std::string("zero ") + c.tag, [cc](App& a, float t) {
      holdBot();
      a.eyeYaw = aimYaw; a.eyePitch = aimPitch; a.viewKickYaw = a.viewKickPitch = 0;
      static vec2 mark; static float dotPx; static mat4 VP; static vec3 eye, fwd; static int frames;
      if (cc.hip || skipped().count(cc.tag)) return true;   // hip views: no aimed shot
      if (t == 0) { frames = 0; return false; }
      if (t < 0.3f) return false;   // the UI shot is written
      const WeaponDef& d = *a.weaponDefs[a.activeWeapon];
      if (frames == 0) {   // the mark and the camera this frame, then pull the trigger
        mat4 view; float wf; a.fpCamera(view, wf);
        mat4 P = glm::perspective(radians(wf), (float)a.R.width / std::max(1, a.R.height), 0.03f, 4000.0f); VP = P * view;
        eye = a.eyePos(); fwd = App::forward(a.eyeYaw, a.eyePitch);
        float pxMoa = P[1][1] * a.R.height * 0.5f * 2.908882e-4f;
        mark = vec2(a.R.width * 0.5f, a.R.height * 0.5f); dotPx = 1.0f;
#ifdef AT2_HUD_RETICLE
        ReflexReticle r = reflexReticle(a);
        if (r.ok) { mark = r.pos; dotPx = std::max(0.75f, (r.kind == 1 ? 1.0f : 0.5f) * r.pxMoa); }
#endif
        Physics::Hit eh; float hitD = Physics::castRayEx(eye, fwd, 600.0f, eh) ? eh.t : -1.0f;
        logf("[ret] %s: frame %dx%d, world FOV %.2f, %.3f px per MOA, the eye ray meets the world at %.1f m", cc.tag, a.R.width, a.R.height, wf, pxMoa, hitD);
        for (auto& x : a.handling.ads) x = 0;   // no dispersion: the sight, not the cone, is measured
        for (auto& x : a.handling.hip) x = 0;
        a.lastShotDir = vec3(0); a.scriptedInput.fire = true; ++frames; return false;
      }
      a.scriptedInput.fire = false;
      if (glm::length(a.lastShotDir) < 0.5f) return t > 2.0f ? (T->check(false, std::string(cc.tag) + ": a shot was fired"), true) : false;
      float v = d.velocity, g = d.action == "launcher" ? 9.81f * d.rocketGrav : (d.blaster ? 0.0f : 9.81f);
      std::string line; float worst = 0, atZero = 0;
      for (float D : {25.0f, 50.0f, 100.0f, d.zero}) {
        float tt = std::max(0.0f, glm::dot(eye + fwd * D - a.lastShotStart, fwd)) / std::max(1.0f, v * glm::dot(a.lastShotDir, fwd));
        vec3 p = a.lastShotStart + a.lastShotDir * v * tt - vec3(0, 0.5f * g * tt * tt, 0);
        vec4 q = VP * vec4(p, 1); vec2 px((q.x / q.w * 0.5f + 0.5f) * a.R.width, (0.5f - q.y / q.w * 0.5f) * a.R.height);
        float e = glm::length(px - mark);
        if (D == d.zero) atZero = e; else worst = std::max(worst, e);
        char b[96]; std::snprintf(b, sizeof b, "  %3.0f m%s: %+.2f, %+.2f px", D, D == d.zero ? " (zero)" : "", px.x - mark.x, px.y - mark.y); line += b;
      }
      logf("[ret] %s zero %.0f m: impact against the mark%s (mark radius %.2f px)", cc.tag, d.zero, line.c_str(), dotPx);
#ifdef AT2_HUD_RETICLE
      T->check(atZero <= 0.25f, std::string(cc.tag) + ": the mark sits on the impact at the zero (" + std::to_string(atZero) + " px; 25 / 50 / 100 m worst " + std::to_string(worst) + " px, dot radius " + std::to_string(dotPx) + " px)");
#endif
      a.scriptedInput = ExploreInput{}; a.scriptedInput.aim = true;
      return true; });
  }
  }
  push("done", [](App& a, float t) { if (t < 0.5f) return false; a.vm.procedural = true; a.scriptedInput = ExploreInput{}; if (a.mode == Mode::Explore) a.exitExplore(); return true; });
}
// ---- --check-sight-freeze: fit each optic on ten guns (animated rigs and static models), then press, tap, hold and release
// ADS at varied times over the first 2 s (also with ADS held through the fit, and right after a weapon switch); afterwards
// the gun must leave ADS, fire, reload and switch. Frames over 0.25 s are logged as hitches.
struct FzRun { std::string gun; int sight; int variant; };
std::vector<FzRun> fzRuns; size_t fzCur = 0; float fzT = 0, fzWorst = 0; int fzPhase = 0, fzShot0 = 0, fzFails = 0; float fzPhaseT = 0;
bool fzAim(int variant, int sight, float t) {   // the ADS key over the first 2 s
  if (variant == 1) return t < 0.6f || (t > 0.65f && t < 0.7f) || (t > 1.3f && t < 1.32f);
  uint32_t h = 2166136261u ^ (uint32_t)(variant * 7 + sight * 131);
  float x = 0; bool on = false;
  while (x < 2.0f) { h = h * 16777619u ^ 0x9e3779b9u; float len = 0.03f + 0.32f * ((h >> 8) % 1000) / 1000.0f; if (t >= x && t < x + len) return on; x += len; on = !on; }
  return false;
}
void buildFreeze(App&) {
  const char* guns[10] = {"modern_ak", "modern_m4_irons", "modern_fal", "modern_lmg", "modern_smg9", "modern_shotgun", "modern_sniper_bolt", "modern_scar", "kar98k", "modern_vsk"};
  push("start", [](App& a, float t) {
    if (t == 0) {
      if (a.mode == Mode::Explore) a.exitExplore();
      a.openMap(contentDir() / "examples/village_assault_3.nmap.json");
      json scj = a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object();
      Scenario sc = Scenario::fromJson(scj);
      sc.rules.mode = GameMode::TDM; sc.rules.killCap = 999; sc.rules.teamPlayers[0] = 1; sc.rules.teamPlayers[1] = 1; sc.rules.respawnDelay = 2;
      sc.rules.humanTeam = 0; sc.rules.aiTypes[0] = aiBit(AiType::Soldiers); sc.rules.aiTypes[1] = aiBit(AiType::Soldiers);
      gUi.loaded = false; gUi.matchScenario = sc.toJson();
      if (!gGame) gGame = new Game(a);
      gGame->pendingStart = true; a.doc.start = {true, vec3(0, 0, 30), 180};
      T->check(a.enterExplore() && gGame->active, "match started (" + gGame->setupError + ")");
      a.useScripted = true; a.scriptedInput = ExploreInput{};
      return false;
    }
    Game& G = *gGame; Soldier* L = G.local();
    if (!L || !L->alive) return t > 30 ? (T->check(false, "the player spawns"), true) : false;
    for (auto& s : G.sold) if (s.faction != L->faction) { s.hp = 100000; if (s.agent >= 0) Physics::setAgentPos(s.agent, L->feet + vec3(300, 0, 300)); s.feet = L->feet + vec3(300, 0, 300); }
    L->hp = 100000; L->protectUntil = -1;
    G.match.rules.mode = GameMode::BattleRoyale; lootBegin(G);
    return true; });
  const char* only = std::getenv("NMK_FZ_GUN");
  for (const char* g : guns) for (int k = 1; k <= 6; ++k) for (int v = 0; v < 4; ++v) if (!only || std::string(only) == g) fzRuns.push_back({g, k, v});
  push("fuzz", [](App& a, float dt) {
    static float last = -1; float fdt = last < 0 ? 0 : dt - last; last = dt; (void)fdt;
    while (fzCur < fzRuns.size()) {
      const FzRun& r = fzRuns[fzCur];
      int w = a.weaponIndex(r.gun); const ItemDef* it = lootItem(lootAttachItemId(0, r.sight));
      if (w >= 0 && it && lootAttachFits(*a.weaponDefs[w], *it) && !a.weaponDefs[w]->hasScope) break;
      if (w >= 0 && it && lootAttachFits(*a.weaponDefs[w], *it) && a.weaponDefs[w]->hasScope && r.sight != 2) break;
      ++fzCur;
    }
    return true; });
  push("runs", [](App& a, float t) {
    static float prevT = 0; float fdt = t - prevT; prevT = t;
    if (fzCur >= fzRuns.size()) { T->check(fzFails == 0, "after every fit and ADS pattern the gun leaves ADS, fires, reloads and switches (" + std::to_string(fzFails) + " runs failed)"); logf("[fz] worst frame %.3f s", fzWorst); return true; }
    const FzRun& r = fzRuns[fzCur];
    Game& G = *gGame; Soldier* L = G.local(); Kit* k = L ? kitOf(L->slot) : nullptr;
    int w = a.weaponIndex(r.gun);
    { static double wl = 0; double now = glfwGetTime(); if (wl > 0 && now - wl > 0.12 && fzPhase > 0) logf("[fz] %s sight %d v%d: a %.0f ms frame (phase %d at %.2f s, adsT %.2f, scope %d)", r.gun.c_str(), r.sight, r.variant, 1000 * (now - wl), fzPhase, t - fzPhaseT, a.adsT, a.scopeView() ? 1 : 0); wl = now; }
    if (fzPhase > 0 && fdt > 0.25f) { fzWorst = std::max(fzWorst, fdt); logf("[fz] %s sight %d v%d: a %.3f s frame (phase %d)", r.gun.c_str(), r.sight, r.variant, fdt, fzPhase); }
    if (fzPhase == 0) {   // set up: the gun out, sight off, loaded, ammo in the bag
      std::string m; if (a.activeWeapon != w) a.equipWeapon(w, m);
      WeaponState& s = a.wstate[r.gun]; s.att[0] = 0; s.loaded = lootMagSize(*a.weaponDefs[w], s);
      if (k) { for (int& x : k->ammo) x = 300; k->items[lootAttachItemId(0, r.sight)] = 1; }
      a.scriptedInput = ExploreInput{}; a.scriptedInput.aim = r.variant == 1;
      fzPhase = 1; fzPhaseT = t; return false;
    }
    float pt = t - fzPhaseT;
    if (fzPhase == 1) {   // settle (variant 1: aiming before the fit)
      if (pt < 0.6f) return false;
      if (r.variant == 2) { int other = a.weaponIndex(r.gun == "modern_ak" ? "modern_m4_irons" : "modern_ak"); if (k) { std::string why; lootFit(a, *k, w, lootAttachItemId(0, r.sight), &why); } a.selectWeapon(other); }
      else if (k) { std::string why; T->check(lootFit(a, *k, w, lootAttachItemId(0, r.sight), &why) || true, ""); if (!why.empty()) logf("[fz] fit %s sight %d: %s", r.gun.c_str(), r.sight, why.c_str()); }
      fzPhase = 2; fzPhaseT = t; return false;
    }
    if (fzPhase == 2) {   // variant 2: back to the sighted gun, then the presses
      if (r.variant == 2 && pt < 0.3f) return false;
      if (r.variant == 2 && a.activeWeapon != w) a.selectWeapon(w);
      fzPhase = 3; fzPhaseT = t; return false;
    }
    if (fzPhase == 3) {   // 2 s of ADS presses
      a.scriptedInput.aim = fzAim(r.variant, r.sight, pt);
      if (r.variant == 3) a.selftest->slowFrames = pt < 2.0f;   // variant 3: 10 fps (0.1 s frames, the main loop's cap) while the presses run
      if (pt < 2.0f) return false;
      a.selftest->slowFrames = false;
      a.scriptedInput = ExploreInput{}; fzPhase = 4; fzPhaseT = t; return false;
    }
    WeaponState& s = a.wstate[r.gun];
    auto fail = [&](const char* what) { ++fzFails; logf("[fz] FAIL %s sight %d v%d: %s (adsT %.2f, active %s, cooldown %.2f, bolt %.2f, reload %.2f, loaded %d)", r.gun.c_str(), r.sight, r.variant, what, a.adsT, a.weaponDefs[a.activeWeapon]->id.c_str(), s.cooldown, s.bolt, s.reload, s.loaded); fzPhase = 9; };
    if (fzPhase == 4) {   // out of ADS
      if (a.adsT <= 0.0f && a.activeWeapon == w) { fzShot0 = s.shotIdx; s.loaded = std::max(s.loaded, 3); fzPhase = 5; fzPhaseT = t; }
      else if (pt > 1.0f) fail("does not leave ADS / not in hand");
      return false;
    }
    if (fzPhase == 5) {   // fires
      a.scriptedInput.fire = pt < 0.3f;
      if (s.shotIdx > fzShot0) { a.scriptedInput.fire = false; s.loaded = 1; fzPhase = 6; fzPhaseT = t; }
      else if (pt > 2.5f) fail("does not fire");
      return false;
    }
    if (fzPhase == 6) {   // reloads
      a.scriptedInput.fire = false; a.scriptedInput.reload = pt < 0.1f;
      if (s.reload > 0) { a.scriptedInput.reload = false; fzPhase = 7; fzPhaseT = t; }
      else if (pt > 1.0f) fail("does not reload");
      return false;
    }
    if (fzPhase == 7) {   // the reload ends, then a switch
      if (s.reload > 0 && pt < 8.0f) return false;
      if (s.reload > 0) { fail("reload never ends"); return false; }
      a.selectWeapon(a.weaponIndex(r.gun == "modern_ak" ? "modern_m4_irons" : "modern_ak"));
      fzPhase = 8; fzPhaseT = t; return false;
    }
    if (fzPhase == 8) { if (a.activeWeapon == w && pt > 0.5f) { fail("does not switch away"); return false; } if (pt < 0.2f) return false; }
    logf("[fz] %s sight %d v%d: %s (vm %.3f %.3f %.3f)", r.gun.c_str(), r.sight, r.variant, fzPhase == 9 ? "FAILED" : "ok", a.vm.pos.x, a.vm.pos.y, a.vm.pos.z);
    a.wstate[r.gun].att[0] = 0;
    ++fzCur; fzPhase = 0;
    while (fzCur < fzRuns.size()) {   // the next run whose optic fits
      const FzRun& n = fzRuns[fzCur]; int nw = a.weaponIndex(n.gun); const ItemDef* nit = lootItem(lootAttachItemId(0, n.sight));
      if (nw >= 0 && nit && lootAttachFits(*a.weaponDefs[nw], *nit)) break;
      ++fzCur;
    }
    return false; });
  push("done", [](App& a, float t) { if (t < 0.5f) return false; a.scriptedInput = ExploreInput{}; if (a.mode == Mode::Explore) a.exitExplore(); return true; });
}
}

bool reticlesSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) { std::string s = __argv[k]; if (s == "--capture-reticles") mode = 1; else if (s == "--check-sight-freeze") mode = 2; } }
  if (mode != 1 && mode != 2) return false;
  if (dt < 0) return true;
  if (!started) { started = true; T = new MpTest(mode == 2 ? "sight_freeze" : "reticles"); if (mode == 2) buildFreeze(app); else build(app); }
  if (cur < seq.size()) {
    bool done = seq[cur].fn(app, st);
    st += dt; total += dt;
    if (done) { ++cur; st = 0; }
    if (total > (mode == 2 ? 1500 : 400)) { T->check(false, "timed out in step '" + seq[std::min(cur, seq.size() - 1)].name + "'"); cur = seq.size(); }
  }
  if (cur >= seq.size() && !app.selftest->done) { app.selftest->exitCode = T->finish(); app.selftest->done = true; }
  return true;
}
