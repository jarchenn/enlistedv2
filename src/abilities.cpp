// GM2 abilities (abilities.h, docs/abilities.md): explosion pack, artillery, suppression vignette
#include "abilities.h"
#include "app.h"
#include "game.h"
#include "minimap.h"
#include "mpai.h"
#include "net.h"
#include "physics.h"
#include "classes.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace {
struct Strike { vec2 target{0}; float t = 0; int fired = 0, owner = -1; uint32_t rng = 1; };
struct State {
  std::vector<LivePack> packs;
  std::vector<Strike> strikes;
  std::set<std::string> abilities;              // the local player's
  std::map<int, std::set<std::string>> slotAbilities;   // host: other soldiers' (GM1 grants them per class)
  std::map<int, double> slotArtyReady;          // host: when a slot may call again (Game::t / app.now)
  double localArtyReady = 0;                    // the local player's own cooldown (app.now)
  bool artyMap = false; bool haveTarget = false; vec2 target{0};
  float supp = 0, suppQuiet = 9;                // suppression level 0..1, s since the last near miss
  bool envGranted = false;
  Gm2Stats stats;
};
State gS;
ArtyConfig gArty;

float frand(uint32_t& r) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return (r % 100000u) / 100000.0f; }
double clockNow(App& app) { return gGame && gGame->active ? gGame->t : app.now; }
bool isClient() { return gNet && gNet->role == Net::CLIENT && gGame && gGame->active; }
bool isHost() { return gNet && gNet->role == Net::HOST && gGame && gGame->active; }

// one charge / shell: the shared explode() path (walls, craters, tanks, glass, props; soldiers through the mpai hook,
// attributed to the owner), the effect and the sound
void blast(App& app, vec3 p, vec3 n, float dmg, float radius, float crater, int owner) {
  mpaiSetBlastOwner(owner);
  app.blastPersonnel = 1;
  app.explode(p + n * 0.06f, -n, dmg, radius, crater, ~0ull, WALL_HE);
  mpaiSetBlastOwner(-1);
  app.fx.explosion(p, n, "ground", true);
  app.audio.play("explosion", p, 1.0f);
  gS.stats.lastBlast = p;
}

vec3 groundPoint(App& app, vec2 xz) {
  Physics::Hit h;
  vec3 top(xz.x, app.terrain.height(xz.x, xz.y) + 120.0f, xz.y);
  if (Physics::castRayEx(top, vec3(0, -1, 0), 400.0f, h)) return h.pos;   // roofs, bunkers: the first thing it meets
  return vec3(xz.x, app.terrain.height(xz.x, xz.y), xz.y);
}

void placeLocal(App& app, const LivePack& p) {
  gS.packs.push_back(p); ++gS.stats.packsPlaced;
  app.audio.play("click", p.pos, 0.7f);
  if (isHost() && !p.visualOnly) gNet->hostWorldEvent(json{{"t", "gm2pack"}, {"p", tojson(p.pos)}, {"n", tojson(p.nrm)}, {"f", p.fuse}, {"w", p.def}});
}
}   // namespace

Gm2Stats& gm2Stats() { return gS.stats; }
const std::vector<LivePack>& gm2Packs() { return gS.packs; }
const ArtyConfig& gm2Arty() { return gArty; }
bool gm2ArtyMapOpen() { return gS.artyMap; }
float gm2Suppression() { return gS.supp; }
bool gm2HasAbility(const std::string& id) { return gS.abilities.count(id) > 0; }

void gm2Reset(App& app) {
  gS.packs.clear(); gS.strikes.clear(); gS.abilities.clear(); gS.slotAbilities.clear(); gS.slotArtyReady.clear();
  gS.localArtyReady = 0; gS.artyMap = false; app.artyMapOpen = false; gS.haveTarget = false; gS.supp = 0; gS.suppQuiet = 9; gS.envGranted = false;
}

bool gm2Grant(App& app, const std::string& id, int count) {
  int w = app.weaponIndex(id);
  if (w < 0) { gS.abilities.insert(id); return true; }   // not a weapon: an ability id
  if (std::find(app.carried.begin(), app.carried.end(), w) == app.carried.end()) { app.carried.push_back(w); app.capCarried(app.carried); }
  WeaponState& s = app.wstate[id];
  s.loaded = count > 0 ? 1 : 0; s.containers.assign(std::max(0, count - 1), 1); s.throwT = -1; s.cooldown = 0;
  return std::find(app.carried.begin(), app.carried.end(), w) != app.carried.end();
}

// a class's GM2 items at spawn: its explosion packs (none: the pack leaves the stack) and its ability (only that one)
void gm2ApplyClass(App& app, const ClassDef& c) {
  gS.abilities.erase("artillery");
  if (!c.ability.empty()) gS.abilities.insert(c.ability);
  int w = app.weaponIndex("explosion_pack");
  if (w < 0) return;
  if (c.packs > 0) gm2Grant(app, "explosion_pack", c.packs);
  else { app.carried.erase(std::remove(app.carried.begin(), app.carried.end(), w), app.carried.end()); WeaponState& s = app.wstate["explosion_pack"]; s.loaded = 0; s.containers.clear(); }
}
void gm2ApplyClassSoldier(App& app, int slot, Soldier& s, const ClassDef& c) {
  gS.slotAbilities[slot].clear();
  if (!c.ability.empty()) gS.slotAbilities[slot].insert(c.ability);
  int w = app.weaponIndex("explosion_pack");
  if (w < 0) return;
  s.carried.erase(std::remove(s.carried.begin(), s.carried.end(), w), s.carried.end());
  WeaponState& ws = s.wsBy[w]; ws.loaded = 0; ws.containers.clear();
  if (c.packs > 0) { s.carried.push_back(w); ws.loaded = 1; ws.containers.assign(c.packs - 1, 1); }
}

// ---------------------------------------------------------------- explosion pack
bool gm2PlacePack(App& app, int def, vec3 eye, vec3 dir, int owner, std::string* why) {
  if (def < 0 || def >= (int)app.weaponDefs.size() || !app.weaponDefs[def]->pack.on) { if (why) *why = "not a pack"; return false; }
  const WeaponDef& d = *app.weaponDefs[def];
  dir = glm::normalize(dir);
  Physics::Hit h;
  if (!Physics::castRayEx(eye, dir, d.pack.reach, h)) { if (why) *why = "Nothing in reach to place it on."; return false; }
  if (owner == -1 && isClient()) {   // a LAN client: the host places it from our soldier (inventory and reach checked there)
    gNet->clientRequest(json{{"t", "pack"}, {"e", tojson(eye)}, {"d", tojson(dir)}, {"w", def}});
    return true;
  }
  LivePack p; p.def = def; p.owner = owner; p.fuse = d.pack.fuse;
  p.nrm = glm::dot(h.normal, h.normal) > 1e-6f ? glm::normalize(h.normal) : vec3(0, 1, 0);
  p.pos = h.pos + p.nrm * 0.03f;
  if (TankRT* t = app.tanks.get(h.ud)) {   // on a vehicle: it rides along
    mat4 H = app.tanks.hullMatrix(*t), Hi = glm::inverse(H);
    p.tank = h.ud; p.local = vec3(Hi * vec4(p.pos, 1)); p.localN = glm::normalize(glm::mat3(Hi) * p.nrm);
  }
  placeLocal(app, p);
  return true;
}

// ---------------------------------------------------------------- artillery
float gm2ArtyCooldownLeft(App& app) { return (float)std::max(0.0, gS.localArtyReady - app.now); }

bool gm2CallArtillery(App& app, vec2 target, int owner, std::string* why) {
  const Terrain& t = app.terrain;
  if (!t.inTerrain(target.x, target.y)) { if (why) *why = "Off the map."; return false; }
  if (owner == -1) {
    if (!gm2HasAbility("artillery")) { if (why) *why = "You cannot call artillery."; ++gS.stats.refused; return false; }
    if (gm2ArtyCooldownLeft(app) > 0) { if (why) *why = "Artillery not ready."; ++gS.stats.refused; return false; }
    gS.localArtyReady = app.now + gArty.cooldown;
    if (isClient()) { gNet->clientRequest(json{{"t", "arty"}, {"x", target.x}, {"z", target.y}}); ++gS.stats.strikesCalled; return true; }
  }
  Strike s; s.target = target; s.owner = owner; s.rng = 0x9E3779B9u ^ (uint32_t)(target.x * 131.0f) ^ ((uint32_t)(target.y * 977.0f) << 7) ^ (uint32_t)gS.stats.strikesCalled;
  gS.strikes.push_back(s); ++gS.stats.strikesCalled;
  if (isHost()) gNet->hostWorldEvent(json{{"t", "gm2arty"}, {"x", target.x}, {"z", target.y}, {"dl", gArty.delay}});
  return true;
}

void gm2ToggleArtyMap(App& app) {
  if (!gS.artyMap && !gm2HasAbility("artillery")) { app.toast("No artillery available.", 2); return; }
  gS.artyMap = !gS.artyMap; app.artyMapOpen = gS.artyMap;
}

// ---------------------------------------------------------------- suppression
void gm2AddNearMiss(float closeness) {
  closeness = std::clamp(closeness, 0.0f, 1.0f);
  gS.supp = std::min(1.0f, gS.supp + 0.12f + 0.18f * closeness);   // a light vignette per round; a burst darkens it quickly
  gS.suppQuiet = 0; ++gS.stats.nearMisses;
}

// ---------------------------------------------------------------- per frame
void gm2Step(App& app, float dt) {
  if (!gS.envGranted) {   // debugging / tests: NMK_GM2_GRANT=explosion_pack,artillery
    gS.envGranted = true;
    if (const char* e = std::getenv("NMK_GM2_GRANT")) { std::string s = e, id; for (char c : s + ",") { if (c == ',') { if (!id.empty()) gm2Grant(app, id); id.clear(); } else id += c; } }
  }
  // packs: ride their vehicle, burn their fuse, go off once
  for (auto& p : gS.packs) {
    if (p.tank) { if (TankRT* t = app.tanks.get(p.tank)) { mat4 H = app.tanks.hullMatrix(*t); p.pos = vec3(H * vec4(p.local, 1)); p.nrm = glm::normalize(glm::mat3(H) * p.localN); } }
    p.fuse -= dt;
    if (p.fuse > 0 || p.visualOnly) continue;
    const WeaponDef& d = *app.weaponDefs[p.def];
    blast(app, p.pos, p.nrm, d.pack.damage, d.pack.radius, d.pack.crater, p.owner);
    ++gS.stats.packsExploded;
  }
  gS.packs.erase(std::remove_if(gS.packs.begin(), gS.packs.end(), [](const LivePack& p) { return p.fuse <= 0; }), gS.packs.end());
  // strikes (host / solo): one shell every `gap` s after the delay, scattered over the target
  for (auto& s : gS.strikes) {
    s.t += dt;
    while (s.fired < gArty.shells && s.t >= gArty.delay + s.fired * gArty.gap) {
      float a = frand(s.rng) * 6.2831853f, r = gArty.spread * std::sqrt(frand(s.rng));
      vec3 g = groundPoint(app, s.target + vec2(std::cos(a), std::sin(a)) * r);
      blast(app, g, vec3(0, 1, 0), gArty.damage, gArty.radius, gArty.crater, s.owner);
      ++s.fired; ++gS.stats.shellsLanded;
    }
  }
  gS.strikes.erase(std::remove_if(gS.strikes.begin(), gS.strikes.end(), [](const Strike& s) { return s.fired >= gArty.shells; }), gS.strikes.end());
  gS.suppQuiet += dt;
  if (gS.suppQuiet > 0.35f) gS.supp = std::max(0.0f, gS.supp - dt * 0.9f);   // fades fairly fast once the fire stops
}

// near misses: an enemy round whose path this step passes within 2.5 m of the eye (once per round: the closest
// approach must fall inside this step's segment). Called by the fixed step right after the rounds moved by dt.
void gm2NearMisses(App& app, float dt) {
  if (dt > 0) {
    vec3 e = app.eyePos();
    int me = gGame && gGame->active ? gGame->localSlot : -2;
    auto test = [&](vec3 p, vec3 v) {
      vec3 a = p - v * dt, ab = p - a; float L2 = glm::dot(ab, ab); if (L2 < 1e-6f) return;
      float u = glm::dot(e - a, ab) / L2; if (u <= 0 || u >= 1) return;
      float d = glm::length(a + ab * u - e);
      if (d < 2.5f && d > 0.05f) gm2AddNearMiss(1.0f - d / 2.5f);
    };
    static int dbg = 0;   // shortcut: diagnostics for the near-miss self-test (enemy rounds within 10 m only), remove once it passes in the combined batch
    for (auto& q : app.projectiles) if (q.owner != -1 && dbg < 30 && glm::length(q.p - e) < 10.0f) { ++dbg; logf("gm2 nm: owner %d kind %d at (%.2f %.2f %.2f) v (%.0f %.0f %.0f) eye (%.2f %.2f %.2f) me %d dt %.4f", q.owner, q.kind, q.p.x, q.p.y, q.p.z, q.v.x, q.v.y, q.v.z, e.x, e.y, e.z, me, dt); }
    for (auto& p : app.projectiles) if (p.kind == PROJ_BULLET && p.owner != -1 && p.owner != me) test(p.p, p.v);
    if (gGame && gGame->isClient) for (auto& g : gGame->ghosts) test(g.p, g.v);
  }
}

void gm2DrawItems(App& app, std::vector<DrawItem>& op) {
  for (auto& p : gS.packs) {
    if (p.def < 0 || p.def >= (int)app.weaponDefs.size()) continue;
    const WeaponDef& d = *app.weaponDefs[p.def];
    vec3 y = p.nrm, x = glm::normalize(glm::abs(y.y) < 0.9f ? glm::cross(vec3(0, 1, 0), y) : glm::cross(vec3(1, 0, 0), y)), z = glm::cross(x, y);
    mat4 M(vec4(x, 0), vec4(y, 0), vec4(z, 0), vec4(p.pos, 1));
    for (int k = 0; k < 8; ++k) if (d.mesh[k].count) op.push_back({&d.mesh[k], M});
  }
}

void gm2Hud(App& app) {
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  // suppression vignette: dark bands from every edge, deeper as the level rises
  if (gS.supp > 0.01f) {
    float W = io.DisplaySize.x, H = io.DisplaySize.y, band = std::min(W, H) * (0.18f + 0.22f * gS.supp);
    int a = (int)std::clamp(gS.supp * 215.0f, 0.0f, 215.0f);
    ImU32 dark = IM_COL32(0, 0, 0, a), clear = IM_COL32(0, 0, 0, 0);
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, band), dark, dark, clear, clear);
    dl->AddRectFilledMultiColor(ImVec2(0, H - band), ImVec2(W, H), clear, clear, dark, dark);
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(band, H), dark, clear, clear, dark);
    dl->AddRectFilledMultiColor(ImVec2(W - band, 0), ImVec2(W, H), clear, dark, dark, clear);
  }
  // the artillery map (key 7): click a spot, then Call in
  if (!gS.artyMap) return;
  float s = std::min(io.DisplaySize.x, io.DisplaySize.y) * 0.8f;
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(s, s + 70), ImGuiCond_Always);
  bool open = true;
  if (ImGui::Begin("Artillery (7)", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar)) {
    ImVec2 c0 = ImGui::GetCursorScreenPos(), av = ImGui::GetContentRegionAvail(); av.y -= 34;
    ImGui::InvisibleButton("##artymap", av);
    std::vector<MinimapMarker> mk;
    if (gS.haveTarget) {
      mk.push_back({gS.target, IM_COL32(255, 70, 50, 255), 3, 9, 0, "Target"});
      mk.push_back({gS.target, IM_COL32(255, 90, 60, 200), 4, gArty.spread, 0, ""});
    }
    MinimapView v = drawMinimap(app, nullptr, vec2(c0.x, c0.y), vec2(c0.x + av.x, c0.y + av.y), vec2(0), 0);   // the mapping only
    for (auto& m : mk) if (m.shape == 4) m.size = std::max(6.0f, gArty.spread * v.scale);   // the barrage's spread on this map
    drawMinimap(app, ImGui::GetWindowDrawList(), vec2(c0.x, c0.y), vec2(c0.x + av.x, c0.y + av.y), vec2(0), 0, MM_ALL, mk);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) { gS.target = v.toWorld(vec2(io.MousePos.x, io.MousePos.y)); gS.haveTarget = true; }
    float cd = gm2ArtyCooldownLeft(app);
    ImGui::BeginDisabled(!gS.haveTarget || cd > 0);
    if (ImGui::Button("Call in", ImVec2(140, 0))) {
      std::string why;
      if (gm2CallArtillery(app, gS.target, -1, &why)) { app.toast("Artillery on the way.", 3); gS.artyMap = false; app.artyMapOpen = false; }
      else app.toast(why, 3);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (cd > 0) ImGui::Text("Ready in %.0f s", cd);
    else ImGui::TextUnformatted(gS.haveTarget ? "Target set: Call in fires the barrage." : "Click the map to set the target.");
  }
  ImGui::End();
  if (!open) { gS.artyMap = false; app.artyMapOpen = false; }
}

// ---------------------------------------------------------------- LAN
bool gm2HostRequest(App& app, Game& g, Soldier& s, int slot, const json& m, std::string& why) {
  std::string t = m.value("t", "");
  if (t == "pack") {
    int def = m.value("w", -1);
    if (def < 0 || def >= (int)app.weaponDefs.size() || !app.weaponDefs[def]->pack.on) { why = "pack: not a pack"; return false; }
    if (std::find(s.carried.begin(), s.carried.end(), def) == s.carried.end()) { why = "pack: not carried"; return false; }
    WeaponState& ws = s.wsBy[def];
    if (ws.loaded <= 0 && ws.containers.empty()) { why = "pack: none left"; return false; }
    vec3 eye = g.eye(s), e = jvec3(m["e"], eye), d = jvec3(m["d"], vec3(0, 0, -1));
    if (glm::length(e - eye) > 1.0f) e = eye;   // the host's own eye for this soldier, give or take a step
    if (!gm2PlacePack(app, def, e, d, slot, &why)) return false;
    if (ws.loaded > 0) --ws.loaded; else ws.containers.pop_back();
    return true;
  }
  if (t == "arty") {
    auto it = gS.slotAbilities.find(slot);
    if (it == gS.slotAbilities.end() || !it->second.count("artillery")) { why = "arty: no ability"; return false; }
    double& ready = gS.slotArtyReady[slot];
    if (g.t < ready) { why = "arty: cooldown"; return false; }
    if (!gm2CallArtillery(app, vec2(m.value("x", 0.0f), m.value("z", 0.0f)), slot, &why)) return false;
    ready = g.t + gArty.cooldown;
    return true;
  }
  why = "unknown"; return false;
}

void gm2GrantSlot(int slot, const std::string& ability) { gS.slotAbilities[slot].insert(ability); }

bool gm2ClientEvent(App& app, const json& ev) {
  std::string t = ev.value("t", "");
  if (t == "gm2pack") {   // a pack placed on the host: drawn here, its blast arrives as an "ex" event
    LivePack p; p.pos = jvec3(ev["p"]); p.nrm = jvec3(ev["n"], vec3(0, 1, 0)); p.fuse = ev.value("f", 10.0f); p.def = ev.value("w", -1); p.visualOnly = true;
    gS.packs.push_back(p); app.audio.play("click", p.pos, 0.6f);
    return true;
  }
  if (t == "gm2arty") { app.toast("Artillery incoming.", 3); return true; }
  return false;
}
