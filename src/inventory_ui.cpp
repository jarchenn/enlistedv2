// BR2 Battle Royale inventory screen (Tab / I), laid out like the PUBG reference: the item list on the left, the
// character with its helmet / vest / backpack slots in the middle, the gun slots and their attachments on the right,
// melee and throwables below them, the carry weight along the bottom. Clicks equip, use, take off or drop.
// BR6: a row clicked is selected (Shift ticks several); a click outside the panels, or "Drop selected", drops them.
#include "loot.h"
#include "app.h"
#include "game.h"
#include <imgui.h>
#include "render.h"
#include "assets.h"
#include "model.h"
#include <algorithm>
#include <set>
#include <map>

// ---- BR9: item icons. Guns and throwables: the white weapon silhouettes (makeWeaponSilhouettes). Everything else: a
// small 3/4 render of the item's model (the same renderModelSheet path, background keyed out to alpha), made the first
// time it is shown and kept; a drawn glyph where the item has no model. Ammo icons carry a colour band per type.
static GLuint itemIconTex(App& app, const std::string& id) {
  static std::map<std::string, GLuint> cache;
  auto found = cache.find(id); if (found != cache.end()) return found->second;
  GLuint& tex = cache[id];
  std::vector<ModelDraw> md; std::vector<DrawItem> bx; vec3 bmin(0), bmax(0);
  const AssetDef* a = app.lib.find(lootAssetId(id));
  if (a && a->model) { for (int k = 0; k < (int)a->model->parts.size(); ++k) md.push_back({a->model.get(), k, mat4(1)}); bmin = a->model->bmin; bmax = a->model->bmax; }
  else if (a && a->opaque.count) { bx.push_back({&a->opaque, mat4(1)}); bmin = a->opaque.bmin; bmax = a->opaque.bmax; }
  else return tex = 0;
  const int S = 96;
  vec3 c = (bmin + bmax) * 0.5f; float r = std::max(0.02f, glm::length(bmax - bmin) * 0.5f);
  vec3 eye = c + glm::normalize(vec3(0.9f, 0.75f, 1.2f)) * r * 4.0f;
  std::vector<uint8_t> px;
  if (!app.R.renderModelSheet(md, bx, glm::lookAt(eye, c, vec3(0, 1, 0)), glm::ortho(-r, r, -r, r, 0.01f, r * 8), eye, S, S, px) || px.size() < (size_t)S * S * 4) return tex = 0;
  const uint8_t bg[3] = {px[0], px[1], px[2]};   // the corner: the clear colour after the sheet's tone curve
  for (size_t k = 0; k < (size_t)S * S; ++k) {
    int diff = 0; for (int c2 = 0; c2 < 3; ++c2) diff = std::max(diff, std::abs((int)px[k * 4 + c2] - (int)bg[c2]));
    px[k * 4 + 3] = (uint8_t)std::clamp((diff - 4) * 12, 0, 255);
  }
  glCreateTextures(GL_TEXTURE_2D, 1, &tex); glTextureStorage2D(tex, 1, GL_RGBA8, S, S);
  glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTextureSubImage2D(tex, 0, 0, 0, S, S, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  return tex;
}
static ImU32 ammoColour(int t) {
  static const ImU32 c[AM_COUNT] = {IM_COL32(240, 200, 60, 255), IM_COL32(240, 130, 40, 255), IM_COL32(90, 200, 90, 255), IM_COL32(220, 70, 60, 255),
                                    IM_COL32(170, 90, 220, 255), IM_COL32(160, 160, 160, 255), IM_COL32(70, 140, 230, 255)};
  return t >= 0 && t < AM_COUNT ? c[t] : IM_COL32(200, 200, 200, 255);
}
void lootDrawIcon(App& app, ImDrawList* dl, ImVec2 p, float s, const FloorItem& f) {   // a 2s x s slot at p
  auto silhouette = [&](const std::string& weaponId) {
    auto it = app.weaponSilhouette.find(weaponId); if (it == app.weaponSilhouette.end() || !it->second) return false;
    dl->AddImage((ImTextureID)(intptr_t)it->second, ImVec2(p.x, p.y + s * 0.25f), ImVec2(p.x + 2 * s, p.y + s * 0.75f));   // 4:1
    return true;
  };
  if (f.weapon >= 0) { silhouette(app.weaponDefs[f.weapon]->id); return; }
  bool ammo = f.id.rfind("ammo:", 0) == 0;
  const ItemDef* d = ammo ? nullptr : lootItem(f.id);
  if (d && d->kind == ItemKind::Throwable && silhouette(d->weapon)) return;
  ImVec2 q(p.x + s * 0.5f, p.y), q1(q.x + s, q.y + s);   // the square in the middle of the slot
  if (GLuint t = itemIconTex(app, f.id)) { dl->AddRectFilled(q, q1, IM_COL32(205, 205, 198, 70), 3); dl->AddImage((ImTextureID)(intptr_t)t, q, q1); }   // a light backdrop: dark parts on a dark panel
  else if (d && d->kind == ItemKind::Medkit) {   // glyphs: no model for these
    dl->AddRectFilled(ImVec2(q.x + 1, q.y + s * 0.2f), ImVec2(q1.x - 1, q1.y - s * 0.1f), IM_COL32(235, 235, 230, 255), 3);
    float cx = (q.x + q1.x) * 0.5f, cy = q.y + s * 0.55f, a = s * 0.22f, b = s * 0.07f;
    dl->AddRectFilled(ImVec2(cx - a, cy - b), ImVec2(cx + a, cy + b), IM_COL32(200, 30, 30, 255)); dl->AddRectFilled(ImVec2(cx - b, cy - a), ImVec2(cx + b, cy + a), IM_COL32(200, 30, 30, 255));
  } else if (d && d->kind == ItemKind::Bandage) {
    dl->AddCircleFilled(ImVec2((q.x + q1.x) * 0.5f, q.y + s * 0.5f), s * 0.38f, IM_COL32(225, 215, 190, 255));
    dl->AddCircle(ImVec2((q.x + q1.x) * 0.5f, q.y + s * 0.5f), s * 0.18f, IM_COL32(150, 140, 120, 255), 0, 1.5f);
  } else {
    dl->AddRectFilled(ImVec2(q.x + 2, q.y + 2), ImVec2(q1.x - 2, q1.y - 2), IM_COL32(90, 90, 85, 255), 3);
    std::string n = d ? d->name.substr(0, 1) : ammo ? std::string(ammoName(std::atoi(f.id.c_str() + 5))).substr(0, 2) : "?";
    ImVec2 ts = ImGui::CalcTextSize(n.c_str()); dl->AddText(ImVec2((q.x + q1.x - ts.x) * 0.5f, (q.y + q1.y - ts.y) * 0.5f), IM_COL32(240, 240, 235, 255), n.c_str());
  }
  if (ammo) dl->AddRectFilled(ImVec2(q.x - 4, q.y + 2), ImVec2(q.x - 1, q1.y - 2), ammoColour(std::atoi(f.id.c_str() + 5)));   // the type's colour band
}
void lootIcon(App& app, const FloorItem& f, float s) {
  ImVec2 p = ImGui::GetCursorScreenPos();
  lootDrawIcon(app, ImGui::GetWindowDrawList(), p, s, f);
  ImGui::Dummy(ImVec2(2 * s, s)); ImGui::SameLine();
}
void lootIcon(App& app, const std::string& itemId, float s) { FloorItem f; f.id = itemId; lootIcon(app, f, s); }

static const char* kSlotNames[4] = {"Sight", "Muzzle", "Magazine", "Stock"};
static const char* kAttIds[4][6] = {
  {"", "br_sight_reddot", "br_sight_acog", "br_sight_4x", "br_sight_10x", "br_sight_holo"},   // AT1: 5 the holographic sight
  {"", "br_suppressor", "br_muzzle_brake", "", "", ""},
  {"", "br_mag_ext", "", "", "", ""},
  {"", "br_recoil_pad", "", "", "", ""}};
static int slotOf(ItemKind k) { return k == ItemKind::Sight ? ATT_SIGHT : k == ItemKind::Muzzle ? ATT_MUZZLE : k == ItemKind::Mag ? ATT_MAG : ATT_STOCK; }

static std::set<std::string> gSel;   // selected rows (lootDiscard keys)
// a selectable row; the buttons drawn after it on the same line stay clickable
static void row(const std::string& key, const char* label) {
  bool on = gSel.count(key) > 0;
  if (ImGui::Selectable((label + std::string("##") + key).c_str(), on, ImGuiSelectableFlags_AllowOverlap)) {
    if (ImGui::GetIO().KeyShift) { if (on) gSel.erase(key); else gSel.insert(key); }
    else { gSel.clear(); if (!on) gSel.insert(key); }
  }
}

void drawInventoryScreen(App& app) {
  Soldier* L = gGame ? gGame->local() : nullptr;
  if (!L) return;
  Kit& k = *kitOf(L->slot);
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(io.DisplaySize); ImGui::SetNextWindowBgAlpha(0.72f);
  ImGui::Begin("##inventory", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
  float W = io.DisplaySize.x, H = io.DisplaySize.y, top = 60;
  ImGui::SetCursorPos(ImVec2(40, 20)); ImGui::Text("INVENTORY   (Tab / I / Esc to close)");
  ImGui::SameLine(); ImGui::TextDisabled("   click a row, then outside the panels to drop it; Shift: several");
  std::string toast;
  char lb[160];

  // ---- left: items in the bag (ammo, healing, loose attachments, spare armour)
  ImGui::SetCursorPos(ImVec2(40, top));
  ImGui::BeginChild("items", ImVec2(W * 0.28f, H - top - 90), true);
  ImGui::TextDisabled("ITEMS");
  for (int t = 0; t < AM_COUNT; ++t) if (k.ammo[t] > 0) {
    ImGui::PushID(t);
    std::snprintf(lb, sizeof lb, "%-10s %4d", ammoName(t), k.ammo[t]);
    lootIcon(app, "ammo:" + std::to_string(t)); row("ammo:" + std::to_string(t), lb); ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40);
    if (ImGui::SmallButton("Drop")) { int n = std::min(k.ammo[t], ammoBox(t)); k.ammo[t] -= n; lootDropItem(L->feet, "ammo:" + std::to_string(t), n); }
    ImGui::PopID();
  }
  for (auto it = k.items.begin(); it != k.items.end();) {
    const ItemDef* d = lootItem(it->first);
    std::string id = it->first; int n = it->second; ++it;
    if (!d || n <= 0) continue;
    ImGui::PushID(id.c_str());
    std::snprintf(lb, sizeof lb, "%-22s x%d", d->name.c_str(), n);
    float right = ImGui::GetContentRegionAvail().x;
    lootIcon(app, id); row("item:" + id, lb);
    if (d->kind == ItemKind::Bandage || d->kind == ItemKind::Medkit) {
      ImGui::SameLine(right - 80);
      if (ImGui::SmallButton("Use")) { std::string why; if (healStart(k, d->kind == ItemKind::Bandage ? 1 : 2, L->hp, &why)) k.healShots = app.wstate[app.weaponDefs[app.activeWeapon]->id].shotIdx; else toast = why; }
    } else {   // a loose attachment: fit it to a gun that takes it (a muzzle piece only where the muzzle is free)
      for (int w : app.carried) {
        const WeaponDef& gd = *app.weaponDefs[w];
        if (!lootAttachFits(gd, *d)) continue;
        WeaponState& s = app.wstate[gd.id]; int sl = slotOf(d->kind);
        std::string gn = gd.name.substr(0, std::min<size_t>(10, gd.name.size()));
        ImGui::SameLine(); ImGui::PushID(w);
        if (sl == ATT_MUZZLE && s.att[sl]) ImGui::TextDisabled("(%s: take its muzzle off first)", gn.c_str());
        else if (ImGui::SmallButton(("Fit " + gn).c_str())) {
          if (s.att[sl]) { const char* old = kAttIds[sl][s.att[sl]]; if (*old) k.items[old]++; }   // a sight swap: the old one into the bag
          s.att[sl] = d->value; if (--k.items[id] <= 0) k.items.erase(id);
        }
        ImGui::PopID();
      }
    }
    ImGui::SameLine(right - 36);
    if (ImGui::SmallButton("Drop")) { if (--k.items[id] <= 0) k.items.erase(id); lootDropItem(L->feet, id, 1); }
    ImGui::PopID();
  }
  std::vector<uint32_t> gearUids; for (auto& g : k.gear) gearUids.push_back(g.uid);   // Wear changes the list
  for (uint32_t u : gearUids) {
    auto it = std::find_if(k.gear.begin(), k.gear.end(), [&](const FloorItem& g) { return g.uid == u; }); if (it == k.gear.end()) continue;
    ImGui::PushID((int)u);
    float right = ImGui::GetContentRegionAvail().x;
    lootIcon(app, *it); row("gear:" + std::to_string(u), lootItemName(app, *it).c_str());
    ImGui::SameLine(right - 80); bool wearIt = ImGui::SmallButton("Wear");
    ImGui::SameLine(right - 36); bool dropIt = ImGui::SmallButton("Drop");
    ImGui::PopID();
    if (wearIt) lootWearGear(app, u); else if (dropIt) lootDiscard(app, "gear:" + std::to_string(u));
  }
  ImGui::EndChild();

  // ---- middle: the character, armour and backpack slots beside it
  float mx0 = W * 0.33f, mx1 = W * 0.60f;
  ImGui::SetCursorPos(ImVec2(mx0, top));
  ImGui::BeginChild("body", ImVec2(mx1 - mx0, H - top - 90), false);
  ImVec2 p0 = ImGui::GetCursorScreenPos(), sz = ImGui::GetContentRegionAvail();
  drawInventoryCharacter(ImVec2(p0.x + sz.x * 0.25f, p0.y + 10), ImVec2(p0.x + sz.x * 0.75f, p0.y + sz.y - 20));
  auto piece = [&](const char* label, int lvl, float* dur, const char* key, float y) {
    ImGui::SetCursorPos(ImVec2(0, y));
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", label);
    if (lvl > 0) {
      std::string pid = key[0] == 'b' ? (lvl == 1 ? "br_backpack" : "br_backpack_mil") : lootPieceId(key[0] == 'h', lvl);
      lootIcon(app, pid, 30); ImGui::NewLine();
      if (dur) std::snprintf(lb, sizeof lb, "Level %d  (%d)", lvl, (int)*dur); else std::snprintf(lb, sizeof lb, "Level %d", lvl);
      ImGui::PushID(label);
      ImGui::PushItemWidth(150); row(std::string("worn:") + key, lb); ImGui::PopItemWidth();
      if (ImGui::SmallButton("Drop")) lootDiscard(app, std::string("worn:") + key);
      ImGui::PopID();
    } else ImGui::TextDisabled("-");
    ImGui::EndGroup();
  };
  piece("HELMET", k.helmet, &k.helmetDur, "helmet", 20);
  piece("VEST", k.vest, &k.vestDur, "vest", 120);
  piece("BACKPACK", k.backpack, nullptr, "bag", 220);
  ImGui::EndChild();

  // ---- right: gun slots 1-3 with their attachment slots, melee and throwables below
  float rx = W * 0.63f;
  ImGui::SetCursorPos(ImVec2(rx, top));
  ImGui::BeginChild("guns", ImVec2(W - rx - 40, H - top - 90), true);
  std::vector<int> rows = app.stackRows();
  int slotNo = 0;
  for (int w : rows) {
    if (w < 0 || app.weaponDefs[w]->equipment()) { if (w < 0 && slotNo < 3) { ++slotNo; ImGui::TextDisabled("%d  (empty)", slotNo); ImGui::Separator(); } continue; }
    const WeaponDef& d = *app.weaponDefs[w];
    WeaponState& s = app.wstate[d.id];
    ++slotNo;
    ImGui::PushID(w);
    auto it = app.weaponSilhouette.find(d.id);
    if (it != app.weaponSilhouette.end() && it->second) ImGui::Image((ImTextureID)(intptr_t)it->second, ImVec2(180, 50));
    else ImGui::Dummy(ImVec2(180, 50));
    ImGui::SameLine(); ImGui::BeginGroup();
    std::snprintf(lb, sizeof lb, "%d  %s%s", slotNo, d.name.c_str(), w == app.activeWeapon ? "   (in hand)" : "");
    row("gun:" + std::to_string(w), lb);
    int t = ammoTypeOf(d); ImGui::TextDisabled("%d in the gun, %s %d", s.loaded, ammoName(t), t >= 0 ? k.ammo[t] : 0);
    ImGui::EndGroup();
    for (int sl = 0; sl < 4; ++sl) {
      ImGui::PushID(sl);
      bool builtIn = sl == ATT_SIGHT && lootBuiltInAcog(d, s);   // the sniper's own scope shows as its ACOG; it does not come off
      const char* id = kAttIds[sl][std::clamp(builtIn ? 2 : s.att[sl], 0, 5)];
      const ItemDef* ad = *id ? lootItem(id) : nullptr;
      ImGui::Text("   %-9s", kSlotNames[sl]); ImGui::SameLine();
      if (ad) { lootIcon(app, id, 16); ImGui::Text("%s", ad->name.c_str()); if (!builtIn) { ImGui::SameLine(); if (ImGui::SmallButton("Take off")) { k.items[id]++; s.att[sl] = 0; } } }   // a sniper's own scope does not come off
      else ImGui::TextDisabled("-");
      ImGui::PopID();
    }
    if (ImGui::SmallButton("Drop gun")) { lootDropGun(app, w); ImGui::PopID(); break; }
    ImGui::Separator();
    ImGui::PopID();
  }
  ImGui::TextDisabled("MELEE AND THROWABLES");
  for (int w : app.carried) {
    const WeaponDef& d = *app.weaponDefs[w];
    if (!d.equipment()) continue;
    const WeaponState& s = app.wstate[d.id];
    FloorItem gi; gi.weapon = w; lootIcon(app, gi);
    if (d.grenade.on) { std::snprintf(lb, sizeof lb, "%s  x%d", d.name.c_str(), s.loaded + (int)s.containers.size()); ImGui::PushID(w); row("throw:" + std::to_string(w), lb); ImGui::PopID(); }
    else ImGui::Text("%s", d.name.c_str());
  }
  ImGui::EndChild();

  // ---- discarding: a click on the background (outside every panel) or the button drops the selected rows
  bool dropSel = !gSel.empty() && ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered();   // the parent window only: not over a panel
  ImGui::SetCursorPos(ImVec2(40, H - 100));
  if (!gSel.empty()) { char db[48]; std::snprintf(db, sizeof db, "Drop selected (%d)", (int)gSel.size()); dropSel |= ImGui::SmallButton(db); }
  if (dropSel) { std::set<std::string> s = gSel; gSel.clear(); for (auto& key : s) lootDiscard(app, key); }

  // ---- bottom: carry weight
  float wgt = kitWeight(&app, k, &app.carried), cap = kitCapacity(k);
  ImGui::SetCursorPos(ImVec2(40, H - 70));
  char b[64]; std::snprintf(b, sizeof b, "Weight %d / %d", (int)wgt, (int)cap);
  ImGui::ProgressBar(std::clamp(wgt / std::max(1.0f, cap), 0.0f, 1.0f), ImVec2(W - 80, 22), b);
  ImGui::End();
  if (!toast.empty()) app.toast(toast, 1.5);
}

// ---- BR6: a dead combatant's crate: a list of what it holds; a click takes an item (the floor pick-up rules),
// "Take all" takes what fits without swapping guns; what is left stays for whoever comes next.
static std::string attList(const WeaponState& s) {
  std::string out;
  for (int sl = 0; sl < 4; ++sl) { const char* id = kAttIds[sl][std::clamp(s.att[sl], 0, 5)]; if (const ItemDef* d = *id ? lootItem(id) : nullptr) out += (out.empty() ? "" : ", ") + d->name; }
  return out;
}
void drawCaseScreen(App& app, LootCase& c) {
  Soldier* L = gGame ? gGame->local() : nullptr;
  if (!L) return;
  Kit& k = *kitOf(L->slot);
  ImGuiIO& io = ImGui::GetIO();
  float w = std::min(560.0f, io.DisplaySize.x - 40);
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f + 60, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, io.DisplaySize.y - 80)); ImGui::SetNextWindowBgAlpha(0.86f);
  ImGui::Begin("##crate", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::Text("%s's crate", c.owner.c_str()); ImGui::SameLine(w - 100);
  std::string toast;
  auto take = [&](uint32_t uid, bool swap) { std::string msg; lootTakeFromCase(app, uid, swap, msg); if (!msg.empty()) toast = msg; };
  std::vector<uint32_t> uids; for (auto& f : c.items) uids.push_back(f.uid);   // the takes below change the list
  if (ImGui::SmallButton("Take all")) for (uint32_t u : uids) take(u, false);   // no swaps: a full gun slot keeps its gun
  ImGui::TextDisabled("click an item to take it   (E / Esc to close)");
  ImGui::Separator();
  for (uint32_t u : uids) {
    auto it = std::find_if(c.items.begin(), c.items.end(), [&](const FloorItem& x) { return x.uid == u; }); if (it == c.items.end()) continue;
    const FloorItem& f = *it;
    std::string label = lootItemName(app, f);
    if (f.weapon >= 0) { std::string a = attList(f.ws); label += "   " + std::to_string(f.ws.loaded) + " in the gun" + (a.empty() ? "" : "   [" + a + "]"); }
    ImGui::PushID((int)u);
    lootIcon(app, f);
    bool clicked = ImGui::Selectable(label.c_str());
    ImGui::PopID();
    if (clicked) take(u, true);
  }
  if (c.items.empty()) ImGui::TextDisabled("(empty)");
  ImGui::Separator();
  float wgt = kitWeight(&app, k, &app.carried), cap = kitCapacity(k);
  char b[64]; std::snprintf(b, sizeof b, "Your weight %d / %d", (int)wgt, (int)cap);
  ImGui::ProgressBar(std::clamp(wgt / std::max(1.0f, cap), 0.0f, 1.0f), ImVec2(-1, 18), b);
  ImGui::End();
  if (!toast.empty()) app.toast(toast, 1.5);
}
