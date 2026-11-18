// SM1 smithing table (content/assets/smithing_table.json, "smithy": true): E at one opens the weapon-fitting screen.
// Left: the guns you carry; middle: the selected gun turning slowly with what is fitted; right: its four slots (sight,
// muzzle, magazine, stock) with the parts you carry that fit, numbered 1-9 (click or the key fits / removes); below: bare
// / now / after (the hovered change) for ADS time, recoil, spread, magazine and optic. The rules and the inventory
// changes are Battle Royale's own (loot.cpp lootFit / lootTakeOff / lootAttachFits), so what fits here fits in BR and
// the inventory's Fit / Take off buttons stay. Bots never use it. Battle Royale places a few per map (smithyBegin).
#include "smithy.h"
#include "app.h"
#include "game.h"
#include "loot.h"
#include "br_models.h"
#include "ui_flat.h"
#include "crumb.h"
#include <array>
#include <map>
#include <imgui.h>

namespace {
bool gOpen = false; uint64_t gId = 0; int gSel = 0;   // gSel: index into the carried guns
GLuint gTex = 0; int gTw = 0, gTh = 0; double gTexAt = -1; uint64_t gTexSig = 0;   // the preview, re-rendered every 50 ms or on a change
int gTestHover = -1;   // the capture: the "after" column for this action
Kit gNoKit;   // outside a match: nothing carried
Kit* gTestKit = nullptr;   // the headless check / capture: their own carried parts
std::vector<uint64_t> gPlaced;   // the tables Battle Royale placed this match
std::map<std::string, std::array<int, 4>> gFits;   // SM3: free fits outside Battle Royale, by weapon id: back on after a respawn / an SD round

Kit& localKit() { if (gTestKit) return *gTestKit; Soldier* L = gGame && gGame->active ? gGame->local() : nullptr; Kit* k = L ? kitOf(L->slot) : nullptr; return k ? *k : gNoKit; }
std::vector<int> guns(App& app) { std::vector<int> v; for (int w : app.carried) { const WeaponDef& d = *app.weaponDefs[w]; if (!d.equipment() && d.action != "melee" && d.action != "grenade" && d.action != "tool") v.push_back(w); } return v; }
// SM3: Battle Royale (and the checks' own kit) fits the parts you carry; everywhere else every fitting part is free
bool kitRule() { return gTestKit || (gGame && gGame->active && gGame->match.mode() == GameMode::BattleRoyale); }
const char* noFitWhy(const WeaponDef& d) {
  if (d.action == "launcher") return "launchers take no parts";
  if (d.flame.on) return "flamethrowers take no parts";
  if (d.blaster) return "energy weapons take no parts";
  if (ammoTypeOf(d) == AM_NONE) return "no parts are made for its calibre";
  return "takes no parts";
}

// the gun and its parts, turning (yaw from the clock), rendered off screen into gTex
void renderGun(App& app, const WeaponDef& d, const WeaponState& s, int w, int h) {
  uint64_t sig = std::hash<std::string>()(d.id); for (int a : s.att) sig = sig * 31 + (uint64_t)a;
  if (gTex && sig == gTexSig && ImGui::GetTime() - gTexAt < 0.05) return;   // ~20 Hz: a full read-back each time
  gTexSig = sig; gTexAt = ImGui::GetTime();
  std::vector<ModelDraw> md; std::vector<DrawItem> bx;
  float yaw = (float)ImGui::GetTime() * 25.0f;
  vec3 lo0 = d.useModel() ? d.model->bmin : vec3(-0.1f, -0.1f, 0), hi0 = d.useModel() ? d.model->bmax : vec3(0.1f, 0.1f, 1);
  mat4 W = glm::rotate(mat4(1), glm::radians(yaw), vec3(0, 1, 0)) * glm::translate(mat4(1), -(lo0 + hi0) * 0.5f);   // about the gun's own centre
  if (d.useModel()) for (int p = 0; p < 8; ++p) for (int k : d.modelParts[p]) md.push_back({d.model.get(), k, W});
  lootAttachDraws(app, d, s, W, md, bx);
  brSightDraws(app, d, s, W, md, bx);   // AT5: the optic (fitted, or the gun's default one) in the preview too
  if (md.empty() && bx.empty()) return;
  vec3 c(0); float r = std::max(0.2f, glm::length(hi0 - lo0) * 0.5f);
  vec3 eye = c + vec3(0, r * 0.35f, r * 2.4f);
  std::vector<uint8_t> px;
  if (!app.R.renderModelSheet(md, bx, glm::lookAt(eye, c, vec3(0, 1, 0)), glm::perspective(glm::radians(30.0f), (float)w / h, 0.02f, r * 10), eye, w, h, px, true)) return;   // transparent: over the page
  for (size_t i = 0; i < px.size(); i += 4) for (int k = 0; k < 3; ++k) px[i + k] = (uint8_t)std::min(255.0f, px[i + k] * 1.35f + 14.0f);   // lifted: black guns still read on the dark page
  if (!gTex || gTw != w || gTh != h) {
    if (gTex) glDeleteTextures(1, &gTex);
    glCreateTextures(GL_TEXTURE_2D, 1, &gTex); glTextureStorage2D(gTex, 1, GL_RGBA8, w, h);
    glTextureParameteri(gTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(gTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gTw = w; gTh = h;
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTextureSubImage2D(gTex, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
}
const char* sightName(int v) { static const char* n[] = {"iron sights", "red dot", "ACOG", "4x", "10x", "holographic", "thermal", "pistol red dot"}; return v >= 0 && v <= 7 ? n[v] : "?"; }
}

bool smithyOpen() { return gOpen; }
bool isSmithy(const AssetDef* a) { return a && a->raw.is_object() && a->raw.value("smithy", false); }
void smithyOpenAt(App& app, uint64_t id) {
  gOpen = true; gId = id; gSel = 0;
  std::vector<int> g = guns(app);
  for (size_t k = 0; k < g.size(); ++k) if (g[k] == app.activeWeapon) gSel = (int)k;   // the gun in hand first
  app.syncCursor();
}
void smithyClose(App& app) { gOpen = false; gId = 0; app.syncCursor(); }

// one fit / remove through the table (the screen, its number keys and the headless check)
bool smithyApply(App& app, int weapon, int slot, const std::string& itemId, std::string* why) {
  if (kitRule()) { Kit& k = localKit(); return itemId.empty() ? lootTakeOff(app, k, weapon, slot, why) : lootFit(app, k, weapon, itemId, why); }
  auto no = [&](const char* m) { if (why) *why = m; return false; };   // SM3: free: no bag
  if (weapon < 0 || weapon >= (int)app.weaponDefs.size() || slot < 0 || slot > 3) return no("no such weapon");
  const WeaponDef& d = *app.weaponDefs[weapon]; WeaponState& s = app.wstate[d.id];
  if (itemId.empty()) { if (!s.att[slot]) return no("nothing fitted"); s.att[slot] = 0; }
  else {
    const ItemDef* it = lootItem(itemId);
    if (!it || it->kind < ItemKind::Sight || it->kind > ItemKind::Stock) return no("not an attachment");
    if (!lootAttachFits(d, *it)) return no("does not fit this weapon");
    int sl = lootSlotOf(it->kind); if (s.att[sl] == it->value) return no("already fitted");
    s.att[sl] = it->value;
  }
  std::array<int, 4> a; for (int k = 0; k < 4; ++k) a[k] = s.att[k]; gFits[d.id] = a;
  return true;
}
void smithyRestoreFits(App& app) { for (auto& [id, a] : gFits) { auto it = app.wstate.find(id); if (it != app.wstate.end()) for (int k = 0; k < 4; ++k) it->second.att[k] = a[k]; } }
bool smithyFree() { return !kitRule(); }

void smithyUi(App& app) {
  if (!gOpen) return;
  const Instance* t = app.doc.find(gId);
  Soldier* L = gGame && gGame->active ? gGame->local() : nullptr;
  vec3 me = L ? L->feet : app.eyePos();
  if (!t || app.mode != Mode::Explore || app.pauseMenu || (L && !L->alive) || glm::length(vec2(t->pos.x - me.x, t->pos.z - me.z)) > 3.5f) { smithyClose(app); return; }
  app.uiCursor = true;
  Kit& k = localKit();
  std::vector<int> g = guns(app);
  gSel = g.empty() ? 0 : std::clamp(gSel, 0, (int)g.size() - 1);
  ImGuiIO& io = ImGui::GetIO();
  const float W = io.DisplaySize.x, H = io.DisplaySize.y, s = std::max(0.75f, H / 1080.0f);
  ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("##smithy", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  flat::backdrop(dl, ImVec2(0, 0), ImVec2(W, H));
  const float X0 = 60 * s, top = 52 * s;
  flat::text(dl, flat::titleFont(), 44 * s, ImVec2(X0, top), flat::ink(), "SMITHING TABLE", 0.06f);
  flat::text(dl, ImGui::GetFont(), 15 * s, ImVec2(X0, top + 52 * s), flat::grey(), "FIT OR REMOVE PARTS  //  CLICK OR 1-9  //  E OR ESC TO LEAVE", 0.1f);
  flat::rule(dl, X0, W - X0, top + 80 * s);
  const float y0 = top + 100 * s, colW = 300 * s, rightX = W - X0 - 420 * s;
  // ---- left: the guns
  ImGui::SetCursorScreenPos(ImVec2(X0, y0));
  ImGui::BeginChild("##guns", ImVec2(colW, H - y0 - 80 * s), false, ImGuiWindowFlags_NoBackground);
  flat::section("Weapons");
  if (g.empty()) flat::note("no guns carried");
  for (size_t i = 0; i < g.size(); ++i) {   // icon, name, and what it carries
    const WeaponDef& d = *app.weaponDefs[g[i]]; const WeaponState& gs = app.wstate[d.id];
    std::string parts;
    for (int sl = 0; sl < 4; ++sl) { const char* id = lootAttachItemId(sl, gs.att[sl]); if (*id) if (const ItemDef* it = lootItem(id)) parts += (parts.empty() ? "" : ", ") + it->name; }
    if (!lootGunTakesParts(d)) parts = std::string("no fittings: ") + noFitWhy(d); else if (parts.empty()) parts = "nothing fitted";
    ImGui::PushID((int)i);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    if (ImGui::Selectable("##gun", (int)i == gSel, 0, ImVec2(colW - 8 * s, 58 * s))) gSel = (int)i;
    auto si = app.weaponSilhouette.find(d.id);
    if (si != app.weaponSilhouette.end() && si->second) dl->AddImage((ImTextureID)(intptr_t)si->second, ImVec2(p0.x + 4 * s, p0.y + 8 * s), ImVec2(p0.x + 104 * s, p0.y + 36 * s));
    dl->AddText(ImGui::GetFont(), 17 * s, ImVec2(p0.x + 112 * s, p0.y + 6 * s), flat::ink(), d.name.c_str());
    dl->AddText(ImGui::GetFont(), 13 * s, ImVec2(p0.x + 112 * s, p0.y + 30 * s), flat::grey(), parts.c_str());
    ImGui::PopID();
  }
  ImGui::EndChild();
  if (g.empty()) { ImGui::End(); goto keys; }
  {
    const int w = g[gSel]; const WeaponDef& d = *app.weaponDefs[w]; WeaponState& ws = app.wstate[d.id];
    // ---- middle: the gun, turning
    const float mx0 = X0 + colW + 30 * s, mx1 = rightX - 30 * s, mh = (mx1 - mx0) * 9 / 16;
    {   // a dark studio: a soft gradient, a floor shadow under the gun, the gun over it (transparent render)
      dl->AddRectFilledMultiColor(ImVec2(mx0, y0), ImVec2(mx1, y0 + mh), IM_COL32(44, 47, 52, 210), IM_COL32(44, 47, 52, 210), IM_COL32(12, 13, 15, 230), IM_COL32(12, 13, 15, 230));
      ImVec2 sc((mx0 + mx1) * 0.5f, y0 + mh * 0.84f);
      for (int k = 0; k < 6; ++k) { float f = 1.0f - k / 6.0f; dl->AddEllipseFilled(sc, ImVec2((mx1 - mx0) * 0.36f * (0.5f + 0.5f * f), mh * 0.05f * (0.5f + 0.5f * f)), IM_COL32(0, 0, 0, 34)); }
    }
    renderGun(app, d, ws, 1536, 864);
    if (gTex) dl->AddImage((ImTextureID)(intptr_t)gTex, ImVec2(mx0, y0), ImVec2(mx1, y0 + mh));
    flat::text(dl, flat::titleFont(), 30 * s, ImVec2(mx0, y0 + mh + 12 * s), flat::ink(), d.name.c_str(), 0.04f);
    // ---- right: the slots; every action numbered (1-9) in reading order
    struct Act { int slot; std::string item; std::string label; };
    std::vector<Act> acts;
    int hover = -1;
    ImGui::SetCursorScreenPos(ImVec2(rightX, y0));
    ImGui::BeginChild("##slots", ImVec2(420 * s, H - y0 - 80 * s), false, ImGuiWindowFlags_NoBackground);
    if (!lootGunTakesParts(d)) flat::note((std::string("No fittings: ") + noFitWhy(d) + ". Guns with a known calibre take parts: sights on long guns, muzzles on all but shotguns, magazines on magazine-fed guns, the recoil pad on rifles.").c_str());
    else for (int sl = 0; sl < 4; ++sl) {
      flat::section(lootSlotName(sl));
      bool builtIn = sl == ATT_SIGHT && lootBuiltInAcog(d, ws);
      const char* fid = lootAttachItemId(sl, ws.att[sl]);
      const ItemDef* fd = *fid ? lootItem(fid) : nullptr;
      if (builtIn) ImGui::TextDisabled("its own scope (stays)");
      else if (fd) {
        acts.push_back({sl, "", "Remove " + fd->name});
        std::string b = "[" + std::to_string(acts.size()) + "]  Remove " + fd->name;
        if (ImGui::Button(b.c_str())) smithyApply(app, w, sl, "");
        if (ImGui::IsItemHovered()) hover = (int)acts.size() - 1;
      } else ImGui::TextDisabled("-  empty");
      bool any = false;
      std::map<std::string, int> freeParts;   // SM3: outside Battle Royale every part of this slot, free
      if (!kitRule()) for (int v = 1; v < 7; ++v) if (const char* fidv = lootAttachItemId(sl, v); *fidv) freeParts[fidv] = -1;
      for (auto& [id, n] : kitRule() ? k.items : freeParts) {
        const ItemDef* it = lootItem(id);
        if (!it || n == 0 || it->kind < ItemKind::Sight || it->kind > ItemKind::Stock || lootSlotOf(it->kind) != sl || !lootAttachFits(d, *it) || ws.att[sl] == it->value) continue;
        any = true;
        acts.push_back({sl, id, "Fit " + it->name});
        std::string b = "[" + std::to_string(acts.size()) + "]  Fit " + it->name + (n > 0 ? "  (x" + std::to_string(n) + ")" : std::string("  (free)"));
        if (ImGui::Button(b.c_str())) smithyApply(app, w, sl, id);
        if (ImGui::IsItemHovered()) hover = (int)acts.size() - 1;
      }
      if (!any && !builtIn) ImGui::TextDisabled(kitRule() ? "   no carried part fits" : "   no part fits this weapon");
    }
    ImGui::EndChild();
    for (int n = 0; n < 9 && n < (int)acts.size(); ++n) if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + n), false)) smithyApply(app, w, acts[n].slot, acts[n].item);
    // ---- the read-out: bare / now / after the hovered change
    WeaponState bare = ws; for (int& a : bare.att) a = 0;
    if (hover < 0 && gTestHover >= 0 && gTestHover < (int)acts.size()) hover = gTestHover;
    WeaponState after = ws; bool hasAfter = hover >= 0;
    if (hasAfter) { const Act& A = acts[hover]; if (A.item.empty()) after.att[A.slot] = 0; else if (const ItemDef* it = lootItem(A.item)) after.att[A.slot] = it->value; }
    GunStats b0 = lootGunStats(app, d, bare), b1 = lootGunStats(app, d, ws), b2 = lootGunStats(app, d, after);
    float ry = y0 + mh + 60 * s;
    // better: green, worse: red (the hovered change against now)
    auto row = [&](const char* name, const std::string& a, const std::string& b, const std::string& c, int better) {   // better: +1 / -1 / 0 same
      flat::text(dl, ImGui::GetFont(), 15 * s, ImVec2(mx0, ry), flat::grey(), name, 0.1f);
      flat::text(dl, ImGui::GetFont(), 17 * s, ImVec2(mx0 + 160 * s, ry), flat::grey(), a.c_str(), 0.0f);
      flat::text(dl, ImGui::GetFont(), 17 * s, ImVec2(mx0 + 300 * s, ry), flat::ink(), b.c_str(), 0.0f);
      ImU32 col = !hasAfter ? flat::grey(0.5f) : better > 0 ? IM_COL32(120, 220, 120, 255) : better < 0 ? IM_COL32(235, 105, 95, 255) : flat::grey();
      flat::text(dl, ImGui::GetFont(), 17 * s, ImVec2(mx0 + 440 * s, ry), col, hasAfter ? c.c_str() : "-", 0.0f);
      ry += 26 * s;
    };
    auto cmp = [](float now, float aft, bool lowerBetter) { return aft == now ? 0 : ((aft < now) == lowerBetter ? 1 : -1); };
    auto f2 = [](float v, const char* u) { char t[32]; std::snprintf(t, sizeof t, "%.2f%s", v, u); return std::string(t); };
    auto pc = [](float v) { char t[32]; std::snprintf(t, sizeof t, "%.0f %%", v * 100); return std::string(t); };
    flat::text(dl, ImGui::GetFont(), 13 * s, ImVec2(mx0 + 160 * s, ry), flat::grey(), "BARE", 0.12f);
    flat::text(dl, ImGui::GetFont(), 13 * s, ImVec2(mx0 + 300 * s, ry), flat::grey(), "NOW", 0.12f);
    flat::text(dl, ImGui::GetFont(), 13 * s, ImVec2(mx0 + 440 * s, ry), flat::grey(), hasAfter ? ("AFTER: " + acts[hover].label).c_str() : "AFTER (HOVER A PART)", 0.12f);
    ry += 26 * s;
    row("ADS TIME", f2(b0.adsTime, " s"), f2(b1.adsTime, " s"), f2(b2.adsTime, " s"), cmp(b1.adsTime, b2.adsTime, true));
    row("RECOIL", pc(b0.recoil), pc(b1.recoil), pc(b2.recoil), cmp(b1.recoil, b2.recoil, true));
    row("SPREAD", pc(b0.spread), pc(b1.spread), pc(b2.spread), cmp(b1.spread, b2.spread, true));
    row("MAGAZINE", std::to_string(b0.mag), std::to_string(b1.mag), std::to_string(b2.mag), cmp((float)b1.mag, (float)b2.mag, false));
    row("OPTIC", sightName(b0.sight), sightName(b1.sight), sightName(b2.sight), b2.sight == b1.sight ? 0 : b2.sight > b1.sight ? 1 : -1);
    row("SUPPRESSED", b0.quiet ? "yes" : "no", b1.quiet ? "yes" : "no", b2.quiet ? "yes" : "no", b2.quiet == b1.quiet ? 0 : b2.quiet ? 1 : -1);
    flat::text(dl, ImGui::GetFont(), 14 * s, ImVec2(mx0, ry + 8 * s), flat::grey(), !kitRule() ? "EVERY PART IS FREE HERE (BATTLE ROYALE: THE PARTS YOU CARRY)  //  FITS STAY ON FOR THE MATCH" : k.items.empty() ? "PARTS COME FROM BATTLE ROYALE LOOT" : "BATTLE ROYALE: THE PARTS YOU CARRY", 0.1f);
  }
  ImGui::End();
keys:
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) smithyClose(app);   // E: updateExplore (the press that opened it no longer closes it the same frame)
}

// ---------------------------------------------------------------- Battle Royale placement
// A handful per map in workshops, garages, barns and military buildings: the same buildings and spots on every peer
// (sorted ids, a fixed hash, the physics of the same map), so a LAN client places them too without a message.
// a table inside this building: the first free spot (room grid, two headings) with standing room in front; 0 if none
uint64_t smithyPlaceIn(App& app, const Instance& h) {
  const AssetDef* table = app.lib.find("smithing_table"); const AssetDef* a = app.assetFor(h);
  if (!table || !a) return 0;
  const vec3 half = (table->bmax - table->bmin) * 0.5f;
  std::vector<std::pair<vec3, vec3>> rooms; for (auto& r : a->navMeta.rooms) rooms.push_back({r.min, r.max});
  if (rooms.empty()) rooms.push_back({vec3(-a->footprint.x * 0.35f, 0, -a->footprint.y * 0.35f), vec3(a->footprint.x * 0.35f, 2.5f, a->footprint.y * 0.35f)});
  quat q = yawQuat(h.yaw);
  for (auto& [mn, mx] : rooms)
    for (int gx = 0; gx < 5; ++gx) for (int gz = 0; gz < 5; ++gz) for (int turn = 0; turn < 2; ++turn) {
      vec3 l(glm::mix(mn.x + 1.0f, mx.x - 1.0f, gx / 4.0f), mn.y + 1.0f, glm::mix(mn.z + 1.0f, mx.z - 1.0f, gz / 4.0f));
      vec3 w = h.pos + q * l; Physics::Hit fh;
      if (!Physics::castRayEx(w, vec3(0, -1, 0), 2.0f, fh)) continue;
      float yaw = h.yaw + turn * 90.0f; quat ty = yawQuat(yaw);
      vec3 base = fh.pos + vec3(0, 0.02f, 0), ctr = base + ty * ((table->bmin + table->bmax) * 0.5f);
      if (Physics::boxBlocked(ctr + vec3(0, 0.05f, 0), half * 0.95f, ty)) continue;
      if (Physics::boxBlocked(base + ty * vec3(0, 1.0f, half.z + 0.6f), vec3(0.45f, 0.8f, 0.45f), ty)) continue;   // standing room in front
      Instance n; n.id = app.doc.newId(); n.assetId = table->id; n.assetVersion = table->version; n.pos = base; n.yaw = wrapDeg(yaw);
      n.extra = json::object(); n.extra["session"] = true; n.extra["smithyBr"] = true;
      app.bump(n); app.doc.instances.push_back(n);
      return n.id;
    }
  return 0;
}
void smithyBegin(App& app, Game& G) {
  gPlaced.clear(); gFits.clear();
  if (G.match.mode() != GameMode::BattleRoyale) return;
  const AssetDef* table = app.lib.find("smithing_table");
  if (!table) return;
  static const char* kWords[] = {"workshop", "garage", "barn", "warehouse", "factory", "hangar", "depot", "stable", "forge", "smithy", "mil_", "barracks", "bunker_hq", "industrial"};
  std::vector<const Instance*> cands;
  for (auto& i : app.doc.instances) {
    const AssetDef* a = app.assetFor(i); if (!a || !a->enterable) continue;
    std::string id = a->id; bool hit = false; for (const char* w : kWords) hit |= id.find(w) != std::string::npos;
    if (hit) cands.push_back(&i);
  }
  std::sort(cands.begin(), cands.end(), [](const Instance* a, const Instance* b) { auto h = [](uint64_t x) { x ^= x >> 33; x *= 0xff51afd7ed558ccdull; x ^= x >> 33; return x; }; return h(a->id) < h(b->id); });
  const int want = std::clamp((int)cands.size() / 3, cands.empty() ? 0 : 2, 6);
  std::vector<uint64_t> hosts;
  for (const Instance* h : cands) {
    if ((int)hosts.size() >= want) break;
    if (uint64_t id = smithyPlaceIn(app, *h)) { gPlaced.push_back(id); hosts.push_back(h->id); }
  }
  if (!gPlaced.empty()) app.syncRuntime();
  logf("smithy: %zu smithing tables placed (of %zu candidate buildings)", gPlaced.size(), cands.size());
}
void smithyEnd(App& app) {
  if (gOpen) smithyClose(app);
  gFits.clear();
  if (gPlaced.empty()) return;
  auto& v = app.doc.instances;
  v.erase(std::remove_if(v.begin(), v.end(), [](const Instance& i) { return std::find(gPlaced.begin(), gPlaced.end(), i.id) != gPlaced.end(); }), v.end());
  gPlaced.clear(); app.syncRuntime();
}
const std::vector<uint64_t>& smithyPlaced() { return gPlaced; }
void smithyUseKit(Kit* k) { gTestKit = k; }
void smithyTestHover(int action) { gTestHover = action; }
