#pragma once
// The Assets palette (category, search, filters, thumbnail grid) shared by the Creative browser (editor.cpp) and
// first-person build mode (builder.cpp). Draws inside the current ImGui window.
#include <string>
struct App;
struct AssetDef;

struct AssetPalette {
  int category = 0;                 // 0 = All, then categories in library order
  char filter[64] = "";             // case-insensitive name search
  bool enterableOnly = false;
  int minFloors = 0;
  float thumb = 120;                // thumbnail edge (px)
  bool buildingFilters = true;      // show "Enterable only" / "Min floors"
};

// Returns the asset clicked this frame (nullptr if none); `active` is highlighted as the asset in hand.
const AssetDef* drawAssetPalette(App& app, AssetPalette& st, const AssetDef* active);
std::string assetFootText(const AssetDef* a);   // "6.0 x 8.0 m | 2 floors | enterable"
