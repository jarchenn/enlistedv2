// The Assets palette, factored out of the Creative browser so first-person build mode lists the same assets
// with the same categories, search and thumbnails (docs/builder.md).
#include "palette.h"
#include "app.h"
#include "thumbs.h"
#include "settings.h"
#include <imgui.h>
#include <algorithm>
#include <unordered_map>

static ImTextureID texId(GLuint t) { return (ImTextureID)(intptr_t)t; }

std::string assetFootText(const AssetDef* a) {
  char b[96]; std::snprintf(b, sizeof b, "%s | %d floor%s%s", footprintLabel(a->footprint).c_str(), a->floors, a->floors == 1 ? "" : "s", a->enterable ? " | enterable" : "");   // W2: one decimal (thumbs.cpp)
  return b;
}

// UI1: the palette's seven groups over the assets' own categories (unknown categories fall into Scenery)
std::string assetGroup(const std::string& c) {
  // MOD1: the modern set sorted into the same groups (its buildings by size: drawAssetPalette)
  if (c == "Modern: Vehicles") return "Vehicles";
  if (c == "Modern: Destruction") return "Ruins";
  if (c == "Modern: Military" || c == "Modern: Fortifications" || c == "Modern: Barriers") return "Bunkers";
  if (c == "Modern: Terrain" || c == "Modern: Road" || c == "Modern: Waterfront" || c == "Modern: Rail") return "Terrain";
  if (c == "Large buildings" || c == "Landmarks") return "Large Buildings";
  if (c == "Small buildings" || c == "Houses") return "Small Buildings";
  if (c == "Bunkers" || c == "Atlantic Wall" || c == "Defences" || c == "Field guns" || c == "Ordnance" || c == "Beach obstacles") return "Bunkers";
  if (c == "Ruins" || c == "Rubble") return "Ruins";
  if (c == "Vehicles" || c == "Landing craft") return "Vehicles";
  if (c == "Paths" || c == "Beach") return "Terrain";
  return "Scenery";
}

// TH1: themes for the palette's theme checkboxes. An asset's own "themes": ["wwii", "postapoc", "modern", "desert"] wins;
// otherwise derived from its category, id and wording, so nothing is untagged (plain 1944 content is WWII).
const char* const kThemeKeys[4] = {"wwii", "postapoc", "modern", "desert"};
static unsigned deriveThemes(const AssetDef* a) {
  const std::string& c = a->category; const std::string& id = a->id;
  auto pre = [&](const char* p) { return id.rfind(p, 0) == 0; };
  std::string d = id + " " + a->name + " " + a->description; std::transform(d.begin(), d.end(), d.begin(), ::tolower);
  auto says = [&](const char* w) { return d.find(w) != std::string::npos; };
  // ground, plants, water, caves, fires, benches: fit every setting
  for (const char* any : {"Nature", "Vegetation", "Trees", "Water", "Paths", "Ground pads", "Scenery", "Cave", "Cave roof", "Gameplay", "Lighting", "Lines"})
    if (c == any) return TH_ALL;
  unsigned t = 0;
  if (c.find("Desert") != std::string::npos || pre("sd_") || says("desert")) t |= TH_DESERT;   // sd_: the Sun Fort desert map's set
  if (pre("mod_") || pre("sd_") || pre("cl_") || pre("br_") || pre("brm_") || pre("rb1_") || pre("furn_mod_") || pre("smithing_table_tech") || pre("car_humvee") || pre("car_boxer") || pre("heli")
      || id == "rack_modern" || c.rfind("Modern", 0) == 0 || c == "Interior: Modern" || c.rfind("Industrial", 0) == 0 || c == "Battle Royale" || c == "Backrooms") t |= TH_MODERN;
  // Poly Haven household and yard props of no particular decade: both eras
  if (c == "Town clutter" || c == "Interior: Tableware & Decor" || c == "Interior: Decor & Lighting" || c == "Industrial: Yard") t |= TH_WWII | TH_MODERN;
  if (!(t & (TH_MODERN | TH_DESERT))) t |= TH_WWII;   // the 1944 catalogue
  if (c == "Ruins" || c == "Rubble" || c == "Modern: Destruction" || pre("rub_") || pre("ruin_") || pre("mod_debris_") || pre("mod_tool_")
      || says("_ruin") || says("ruined") || says("burnt") || says("burned") || says("wreck") || says("rubble") || says("derelict") || says("abandoned")
      || says("collapsed") || says("rusted") || says("rusty") || says("debris") || says("graffiti") || says("scrap") || says("damaged") || says("gutted")) t |= TH_POSTAPOC;
  return t;
}
unsigned assetThemes(const AssetDef* a) {
  static std::unordered_map<std::string, unsigned> memo;   // shortcut: by id for the session, so an edited "themes" shows after a restart
  auto it = memo.find(a->id); if (it != memo.end()) return it->second;
  unsigned t = 0;
  if (a->raw.is_object() && a->raw.contains("themes") && a->raw["themes"].is_array())
    for (auto& v : a->raw["themes"]) for (int k = 0; k < 4; ++k) if (v.is_string() && v.get<std::string>() == kThemeKeys[k]) t |= 1u << k;
  return memo[a->id] = t ? t : deriveThemes(a);
}

// TH1: one asset's palette group: four groups sorted by what the piece is (hung on a wall, debris, hand-sized,
// furniture) before its category's group; the asset files keep their categories
static std::string paletteGroup(const AssetDef* a) {
  const std::string& c = a->category; const std::string& id = a->id;
  auto pre = [&](const char* p) { return id.rfind(p, 0) == 0; };
  auto has = [&](const char* w) { return id.find(w) != std::string::npos; };
  if (a->raw.is_object() && a->raw.contains("group") && a->raw["group"].is_string()) return a->raw["group"].get<std::string>();   // PR10: an asset may name its group
  if (pre("mod_bld_")) return a->floors >= 3 || a->footprint.x * a->footprint.y >= 400.0f ? "Large Buildings" : "Small Buildings";   // modern buildings by size
  const json* pr = a->raw.is_object() && a->raw.contains("prop") ? &a->raw["prop"] : nullptr;
  bool hung = pr && pr->contains("mount") && (*pr)["mount"].is_number() && (*pr)["mount"].get<float>() > 0.0f;   // furnisher pieces hung at a height
  if (hung || has("_face_") || has("wall_clock") || has("hanging_picture") || has("sconce") || has("wall_lantern") || has("ornate_mirror") || pre("mod_ac_rack") || pre("mod_vent_grille"))
    return "Wall mountables";
  if (c == "Rubble" || c == "Modern: Destruction" || has("rubble") || pre("furn_debris") || has("glass_pile") || has("papers")) return "Debris";
  bool natural = false;
  for (const char* n : {"Nature", "Vegetation", "Trees", "Water", "Paths", "Ground pads", "Cave", "Cave roof", "Lines", "Lighting"}) natural |= c == n;
  float h = a->bmax.y - a->bmin.y;
  if (!a->enterable && !natural && std::max(a->footprint.x, a->footprint.y) <= 0.6f && h <= 0.6f) return "Small objects";   // tools, cans, books
  if (pre("mod_debris_")) return "Debris";
  if (c == "Furniture" || c.rfind("Interior", 0) == 0 || c == "Modern: Interior") return "Furniture";
  return assetGroup(c);
}

const AssetDef* drawAssetPalette(App& app, AssetPalette& st, const AssetDef* active) {
  const AssetDef* picked = nullptr;
  const std::vector<std::string> cats = {"All", "Scenery", "Large Buildings", "Small Buildings", "Bunkers", "Ruins", "Vehicles", "Terrain", "Debris", "Small objects", "Furniture", "Wall mountables"};
  extern bool lineReplacedPiece(const std::string&);
  extern const std::vector<std::string>* treeAlias(const std::string&);   // TR1: generated trees, replaced by model trees
  auto hidden = [](const AssetDef* a) { return (a->raw.is_object() && a->raw.value("hidden", false)) || lineReplacedPiece(a->id) || treeAlias(a->id)   // LN2: pieces now drawn as lines   // LN1: lines and the pieces they replace
                                               || a->category == "Water"; };   // UI1: ponds, puddles and streams come from the water lasso
  st.category = std::clamp(st.category, 0, (int)cats.size() - 1);
  if (ImGui::BeginCombo("Category", cats[st.category].c_str())) {
    for (int k = 0; k < (int)cats.size(); ++k) if (ImGui::Selectable(cats[k].c_str(), k == st.category)) st.category = k;
    ImGui::EndCombo();
  }
  {   // TH1: theme checkboxes (an asset shows when one of its themes is ticked), kept in settings_user.json
    static const char* const kThemeLabels[4] = {"WWII", "Post-apocalyptic", "Modern", "Desert"};
    unsigned& th = settings().s.themes;
    for (int k = 0; k < 4; ++k) {
      bool on = th & (1u << k);
      if (k % 2) ImGui::SameLine(150);   // two per row: the Assets window is ~300 px wide
      if (ImGui::Checkbox(kThemeLabels[k], &on)) { th = on ? th | (1u << k) : th & ~(1u << k); settings().save(); }
    }
    static bool logged = false;
    if (!logged) {
      logged = true; int n[4] = {}, all = 0, shown = 0;
      for (auto& a : app.lib.assets) { if (hidden(a.get())) continue; unsigned t = assetThemes(a.get()); ++shown; all += t == TH_ALL; for (int k = 0; k < 4; ++k) n[k] += (t >> k) & 1; }
      logf("palette themes: %d listed assets: wwii %d, postapoc %d, modern %d, desert %d (all four %d)", shown, n[0], n[1], n[2], n[3], all);
      for (size_t g = 1; g < cats.size(); ++g) {
        int m = 0, q[4] = {};
        for (auto& a : app.lib.assets) if (!hidden(a.get()) && paletteGroup(a.get()) == cats[g]) { ++m; unsigned t = assetThemes(a.get()); for (int k = 0; k < 4; ++k) q[k] += (t >> k) & 1; }
        logf("palette group %s: %d (wwii %d, postapoc %d, modern %d, desert %d)", cats[g].c_str(), m, q[0], q[1], q[2], q[3]);
      }
    }
  }
  ImGui::InputTextWithHint("##search", "Search...", st.filter, sizeof st.filter);
  if (st.buildingFilters) {
    ImGui::Checkbox("Enterable only", &st.enterableOnly); ImGui::SameLine();
    ImGui::SetNextItemWidth(80); ImGui::SliderInt("Min floors", &st.minFloors, 0, 3);
  }
  ImGui::Separator();
  ImGui::BeginChild("##grid");
  float avail = ImGui::GetContentRegionAvail().x;
  int cols = std::max(1, (int)(avail / (st.thumb + 12)));
  std::string lf = st.filter; std::transform(lf.begin(), lf.end(), lf.begin(), ::tolower);
  int n = 0, thumbBudget = 2;   // MEM2: tiles rendered per frame (cached ones load freely)
  for (auto& a : app.lib.assets) {
    if (hidden(a.get())) continue;
    if (st.category > 0 && paletteGroup(a.get()) != cats[st.category]) continue;
    if (!(assetThemes(a.get()) & settings().s.themes)) continue;   // TH1
    if (st.buildingFilters && st.enterableOnly && !a->enterable) continue;
    if (st.buildingFilters && a->floors < st.minFloors) continue;
    std::string lname = a->name; std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
    if (!lf.empty() && lname.find(lf) == std::string::npos) continue;
    if (n % cols) ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushID(a->id.c_str());
    bool on = active == a.get();
    if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.6f, 0.2f, 1));
    if (!a->thumbnail && ImGui::IsRectVisible(ImVec2(st.thumb, st.thumb))) { extern void assetThumbOnDemand(App&, AssetDef&, int&); assetThumbOnDemand(app, *a, thumbBudget); }
    if (ImGui::ImageButton("##thumb", texId(a->thumbnail), ImVec2(st.thumb, st.thumb), ImVec2(0, 1), ImVec2(1, 0))) picked = a.get();
    if (on) ImGui::PopStyleColor();
    std::string foot = assetFootText(a.get());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s\n%s\n\n%s", a->name.c_str(), a->category.c_str(), foot.c_str(), a->description.c_str());
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + st.thumb + 4);
    ImGui::TextUnformatted(a->name.c_str());
    ImGui::TextDisabled("%s", a->category.c_str());
    ImGui::TextDisabled("%s", foot.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopID();
    ImGui::EndGroup();
    ++n;
  }
  if (n == 0) ImGui::TextDisabled("No assets match the filter.");
  ImGui::EndChild();
  return picked;
}
