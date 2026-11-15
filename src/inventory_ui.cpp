// BR2 Battle Royale inventory screen (Tab / I), laid out like the PUBG reference: the item list on the left, the
// character with its helmet / vest / backpack slots in the middle, the gun slots and their attachments on the right,
// melee and throwables below them, the carry weight along the bottom. Clicks equip, use, take off or drop.
#include "loot.h"
#include "app.h"
#include "game.h"
#include <imgui.h>
#include <algorithm>

static const char* kSlotNames[4] = {"Sight", "Muzzle", "Magazine", "Stock"};
static const char* kAttIds[4][5] = {
  {"", "br_sight_reddot", "br_sight_acog", "br_sight_4x", "br_sight_10x"},
  {"", "br_suppressor", "br_muzzle_brake", "", ""},
  {"", "br_mag_ext", "", "", ""},
  {"", "br_recoil_pad", "", "", ""}};
static int slotOf(ItemKind k) { return k == ItemKind::Sight ? ATT_SIGHT : k == ItemKind::Muzzle ? ATT_MUZZLE : k == ItemKind::Mag ? ATT_MAG : ATT_STOCK; }

void drawInventoryScreen(App& app) {
  Soldier* L = gGame ? gGame->local() : nullptr;
  if (!L) return;
  Kit& k = *kitOf(L->slot);
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(io.DisplaySize); ImGui::SetNextWindowBgAlpha(0.72f);
  ImGui::Begin("##inventory", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
  float W = io.DisplaySize.x, H = io.DisplaySize.y, top = 60;
  ImGui::SetCursorPos(ImVec2(40, 20)); ImGui::Text("INVENTORY   (Tab / I to close)");
  std::string toast;

  // ---- left: items on the body (ammo, healing, loose attachments)
  ImGui::SetCursorPos(ImVec2(40, top));
  ImGui::BeginChild("items", ImVec2(W * 0.28f, H - top - 90), true);
  ImGui::TextDisabled("ITEMS");
  for (int t = 0; t < AM_COUNT; ++t) if (k.ammo[t] > 0) {
    ImGui::PushID(t);
    ImGui::Text("%-10s %4d", ammoName(t), k.ammo[t]); ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40);
    if (ImGui::SmallButton("Drop")) { int n = std::min(k.ammo[t], ammoBox(t)); k.ammo[t] -= n; lootDropItem(L->feet, "ammo:" + std::to_string(t), n); }
    ImGui::PopID();
  }
  for (auto it = k.items.begin(); it != k.items.end();) {
    const ItemDef* d = lootItem(it->first);
    std::string id = it->first; int n = it->second; ++it;
    if (!d || n <= 0) continue;
    ImGui::PushID(id.c_str());
    ImGui::Text("%-22s x%d", d->name.c_str(), n);
    float right = ImGui::GetContentRegionAvail().x;
    if (d->kind == ItemKind::Bandage || d->kind == ItemKind::Medkit) {
      ImGui::SameLine(right - 80);
      if (ImGui::SmallButton("Use")) { std::string why; if (healStart(k, d->kind == ItemKind::Bandage ? 1 : 2, L->hp, &why)) k.healShots = app.wstate[app.weaponDefs[app.activeWeapon]->id].shotIdx; else toast = why; }
    } else {   // a loose attachment: fit it to a gun that takes it
      for (int w : app.carried) {
        const WeaponDef& gd = *app.weaponDefs[w];
        if (!lootAttachFits(gd, *d)) continue;
        ImGui::SameLine(); ImGui::PushID(w);
        if (ImGui::SmallButton(("Fit " + gd.name.substr(0, std::min<size_t>(10, gd.name.size()))).c_str())) {
          WeaponState& s = app.wstate[gd.id]; int sl = slotOf(d->kind);
          if (s.att[sl]) { const char* old = kAttIds[sl][s.att[sl]]; if (*old) k.items[old]++; }
          s.att[sl] = d->value; if (--k.items[id] <= 0) k.items.erase(id);
        }
        ImGui::PopID();
      }
    }
    ImGui::SameLine(right - 36);
    if (ImGui::SmallButton("Drop")) { if (--k.items[id] <= 0) k.items.erase(id); lootDropItem(L->feet, id, 1); }
    ImGui::PopID();
  }
  ImGui::EndChild();

  // ---- middle: the character, armour and backpack slots beside it
  float mx0 = W * 0.33f, mx1 = W * 0.60f;
  ImGui::SetCursorPos(ImVec2(mx0, top));
  ImGui::BeginChild("body", ImVec2(mx1 - mx0, H - top - 90), false);
  ImVec2 p0 = ImGui::GetCursorScreenPos(), sz = ImGui::GetContentRegionAvail();
  drawInventoryCharacter(ImVec2(p0.x + sz.x * 0.25f, p0.y + 10), ImVec2(p0.x + sz.x * 0.75f, p0.y + sz.y - 20));
  auto piece = [&](const char* label, int& lvl, float* dur, const char* idPrefix, float y) {
    ImGui::SetCursorPos(ImVec2(0, y));
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", label);
    if (lvl > 0) {
      if (dur) ImGui::Text("Level %d  (%d)", lvl, (int)*dur); else ImGui::Text("Level %d", lvl);
      ImGui::PushID(label);
      if (ImGui::SmallButton("Drop")) {
        std::string id = idPrefix == std::string("bag") ? (lvl == 1 ? "br_backpack" : "br_backpack_mil") : lvl == 1 ? "br_vest_1" : std::string(idPrefix) + std::to_string(lvl);   // level 1 is the combined item
        lootDropItem(L->feet, id, dur ? (int)*dur : 1);
        lvl = 0; if (dur) *dur = 0;
        L->helmetLvl = k.helmet; L->vestLvl = k.vest; L->backpack = k.backpack;
      }
      ImGui::PopID();
    } else ImGui::TextDisabled("-");
    ImGui::EndGroup();
  };
  piece("HELMET", k.helmet, &k.helmetDur, "br_helmet_", 20);
  piece("VEST", k.vest, &k.vestDur, "br_vest_", 120);
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
    ImGui::Text("%d  %s%s", slotNo, d.name.c_str(), w == app.activeWeapon ? "   (in hand)" : "");
    int t = ammoTypeOf(d); ImGui::TextDisabled("%d in the gun, %s %d", s.loaded, ammoName(t), t >= 0 ? k.ammo[t] : 0);
    ImGui::EndGroup();
    for (int sl = 0; sl < 4; ++sl) {
      ImGui::PushID(sl);
      bool builtIn = sl == ATT_SIGHT && lootBuiltInAcog(d, s);   // the sniper's own scope shows as its ACOG; it does not come off
      const char* id = kAttIds[sl][std::clamp(builtIn ? 2 : s.att[sl], 0, 4)];
      const ItemDef* ad = *id ? lootItem(id) : nullptr;
      ImGui::Text("   %-9s %s", kSlotNames[sl], ad ? ad->name.c_str() : "-");
      if (ad && !builtIn) { ImGui::SameLine(); if (ImGui::SmallButton("Take off")) { k.items[id]++; s.att[sl] = 0; } }
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
    if (d.grenade.on) ImGui::Text("%s  x%d", d.name.c_str(), s.loaded + (int)s.containers.size()); else ImGui::Text("%s", d.name.c_str());
  }
  ImGui::EndChild();

  // ---- bottom: carry weight
  float wgt = kitWeight(&app, k, &app.carried), cap = kitCapacity(k);
  ImGui::SetCursorPos(ImVec2(40, H - 70));
  char b[64]; std::snprintf(b, sizeof b, "Weight %d / %d", (int)wgt, (int)cap);
  ImGui::ProgressBar(std::clamp(wgt / std::max(1.0f, cap), 0.0f, 1.0f), ImVec2(W - 80, 22), b);
  ImGui::End();
  if (!toast.empty()) app.toast(toast, 1.5);
}

// ---- BR5: a dead combatant's case. Its contents on the left (Take, Take all, or drag onto your side), what you
// carry on the right; what you leave stays in the case for whoever comes next.
static std::string attList(const WeaponState& s) {
  std::string out;
  for (int sl = 0; sl < 4; ++sl) { const char* id = kAttIds[sl][std::clamp(s.att[sl], 0, 4)]; if (const ItemDef* d = *id ? lootItem(id) : nullptr) out += (out.empty() ? "" : ", ") + d->name; }
  return out;
}
void drawCaseScreen(App& app, LootCase& c) {
  Soldier* L = gGame ? gGame->local() : nullptr;
  if (!L) return;
  Kit& k = *kitOf(L->slot);
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(io.DisplaySize); ImGui::SetNextWindowBgAlpha(0.72f);
  ImGui::Begin("##lootcase", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
  float W = io.DisplaySize.x, H = io.DisplaySize.y, top = 60, paneW = (W - 120) * 0.5f;
  ImGui::SetCursorPos(ImVec2(40, 20)); ImGui::Text("LOOT   %s   (E / Tab to close)", c.owner.c_str());
  std::string toast;
  auto take = [&](uint32_t uid, bool swap) { std::string msg; lootTakeFromCase(app, uid, swap, msg); if (!msg.empty()) toast = msg; };

  // ---- left: the case
  ImGui::SetCursorPos(ImVec2(40, top));
  ImGui::BeginChild("case", ImVec2(paneW, H - top - 90), true);
  ImGui::TextDisabled("CASE"); ImGui::SameLine(paneW - 90);
  std::vector<uint32_t> uids; for (auto& f : c.items) uids.push_back(f.uid);   // the takes below change the list
  if (ImGui::SmallButton("Take all")) for (uint32_t u : uids) take(u, false);   // no swaps: a full gun slot keeps its gun
  ImGui::Separator();
  for (uint32_t u : uids) {
    auto it = std::find_if(c.items.begin(), c.items.end(), [&](const FloorItem& x) { return x.uid == u; }); if (it == c.items.end()) continue;
    const FloorItem& f = *it;
    std::string label = lootItemName(app, f);
    if (f.weapon >= 0) { std::string a = attList(f.ws); label += "   " + std::to_string(f.ws.loaded) + " in the gun" + (a.empty() ? "" : "   [" + a + "]"); }
    ImGui::PushID((int)u);
    ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowOverlap, ImVec2(paneW - 90, 0));
    if (ImGui::BeginDragDropSource()) { ImGui::SetDragDropPayload("BR_LOOT", &u, sizeof u); ImGui::Text("%s", label.c_str()); ImGui::EndDragDropSource(); }
    ImGui::SameLine(paneW - 60);
    bool clicked = ImGui::SmallButton("Take");
    ImGui::PopID();
    if (clicked) take(u, true);
  }
  if (c.items.empty()) ImGui::TextDisabled("(empty)");
  ImGui::EndChild();

  // ---- right: what you carry (a drop target for dragged items)
  ImGui::SetCursorPos(ImVec2(80 + paneW, top));
  ImGui::BeginChild("mine", ImVec2(paneW, H - top - 90), true);
  ImGui::TextDisabled("YOUR INVENTORY");
  ImGui::Separator();
  int slotNo = 0;
  for (int w : app.stackRows()) {
    if (w < 0 || app.weaponDefs[w]->equipment()) { if (w < 0 && slotNo < 3) { ++slotNo; ImGui::TextDisabled("%d  (empty gun slot)", slotNo); } continue; }
    const WeaponDef& d = *app.weaponDefs[w]; const WeaponState& s = app.wstate[d.id];
    std::string a = attList(s); int t = ammoTypeOf(d);
    ImGui::Text("%d  %s   %d in the gun, %s %d", ++slotNo, d.name.c_str(), s.loaded, ammoName(t), t >= 0 ? k.ammo[t] : 0);
    if (!a.empty()) ImGui::TextDisabled("     %s", a.c_str());
  }
  ImGui::Spacing();
  ImGui::Text("Helmet %s   Vest %s   Backpack %s", k.helmet ? std::to_string(k.helmet).c_str() : "-", k.vest ? std::to_string(k.vest).c_str() : "-", k.backpack ? std::to_string(k.backpack).c_str() : "-");
  ImGui::Spacing();
  for (int t = 0; t < AM_COUNT; ++t) if (k.ammo[t] > 0) ImGui::Text("%-10s %4d", ammoName(t), k.ammo[t]);
  for (auto& [id, n] : k.items) if (const ItemDef* d = lootItem(id); d && n > 0) ImGui::Text("%-22s x%d", d->name.c_str(), n);
  for (int w : app.carried) { const WeaponDef& d = *app.weaponDefs[w]; if (d.grenade.on) { const WeaponState& s = app.wstate[d.id]; ImGui::Text("%-22s x%d", d.name.c_str(), s.loaded + (int)s.containers.size()); } }
  ImGui::EndChild();
  if (ImGui::BeginDragDropTarget()) {   // the whole right pane
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("BR_LOOT")) take(*(const uint32_t*)p->Data, true);
    ImGui::EndDragDropTarget();
  }

  // ---- bottom: carry weight
  float wgt = kitWeight(&app, k, &app.carried), cap = kitCapacity(k);
  ImGui::SetCursorPos(ImVec2(40, H - 70));
  char b[64]; std::snprintf(b, sizeof b, "Weight %d / %d", (int)wgt, (int)cap);
  ImGui::ProgressBar(std::clamp(wgt / std::max(1.0f, cap), 0.0f, 1.0f), ImVec2(W - 80, 22), b);
  ImGui::End();
  if (!toast.empty()) app.toast(toast, 1.5);
}
