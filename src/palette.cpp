// The Assets palette, factored out of the Creative browser so first-person build mode lists the same assets
// with the same categories, search and thumbnails (docs/builder.md).
#include "palette.h"
#include "app.h"
#include "thumbs.h"
#include <imgui.h>
#include <algorithm>

static ImTextureID texId(GLuint t) { return (ImTextureID)(intptr_t)t; }

std::string assetFootText(const AssetDef* a) {
  char b[96]; std::snprintf(b, sizeof b, "%s | %d floor%s%s", footprintLabel(a->footprint).c_str(), a->floors, a->floors == 1 ? "" : "s", a->enterable ? " | enterable" : "");   // W2: one decimal (thumbs.cpp)
  return b;
}

const AssetDef* drawAssetPalette(App& app, AssetPalette& st, const AssetDef* active) {
  const AssetDef* picked = nullptr;
  std::vector<std::string> cats = {"All"};
  for (auto& a : app.lib.assets) if (std::find(cats.begin(), cats.end(), a->category) == cats.end()) cats.push_back(a->category);
  st.category = std::clamp(st.category, 0, (int)cats.size() - 1);
  if (ImGui::BeginCombo("Category", cats[st.category].c_str())) {
    for (int k = 0; k < (int)cats.size(); ++k) if (ImGui::Selectable(cats[k].c_str(), k == st.category)) st.category = k;
    ImGui::EndCombo();
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
  int n = 0;
  for (auto& a : app.lib.assets) {
    if (st.category > 0 && a->category != cats[st.category]) continue;
    if (st.buildingFilters && st.enterableOnly && !a->enterable) continue;
    if (st.buildingFilters && a->floors < st.minFloors) continue;
    std::string lname = a->name; std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
    if (!lf.empty() && lname.find(lf) == std::string::npos) continue;
    if (n % cols) ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushID(a->id.c_str());
    bool on = active == a.get();
    if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.6f, 0.2f, 1));
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
