// BR2 Battle Royale inventory screen (Tab / I), laid out like the PUBG reference: the item list on the left, the
// character with its helmet / vest / backpack slots in the middle, the gun slots and their attachments on the right,
// melee and throwables below them, the carry weight along the bottom. Clicks equip, use, take off or drop.
// BR6: a row clicked is selected (Shift ticks several); a click outside the panels, or "Drop selected", drops them.
#include "loot.h"
#include "app.h"
#include "game.h"
#include <imgui.h>
#include <algorithm>
#include <set>

static const char* kSlotNames[4] = {"Sight", "Muzzle", "Magazine", "Stock"};
static const char* kAttIds[4][5] = {
  {"", "br_sight_reddot", "br_sight_acog", "br_sight_4x", "br_sight_10x"},
  {"", "br_suppressor", "br_muzzle_brake", "", ""},
  {"", "br_mag_ext", "", "", ""},
  {"", "br_recoil_pad", "", "", ""}};
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
    row("ammo:" + std::to_string(t), lb); ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40);
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
    row("item:" + id, lb);
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
    row("gear:" + std::to_string(u), lootItemName(app, *it).c_str());
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
      const char* id = kAttIds[sl][std::clamp(s.att[sl], 0, 4)];
      const ItemDef* ad = *id ? lootItem(id) : nullptr;
      ImGui::Text("   %-9s %s", kSlotNames[sl], ad ? ad->name.c_str() : "-");
      if (ad) { ImGui::SameLine(); if (ImGui::SmallButton("Take off")) { k.items[id]++; s.att[sl] = 0; } }
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
  for (int sl = 0; sl < 4; ++sl) { const char* id = kAttIds[sl][std::clamp(s.att[sl], 0, 4)]; if (const ItemDef* d = *id ? lootItem(id) : nullptr) out += (out.empty() ? "" : ", ") + d->name; }
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
