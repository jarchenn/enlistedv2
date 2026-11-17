#pragma once
// SM1 smithing table: the weapon-fitting screen (smithy.cpp)
#include "common.h"
struct App; struct Game; struct AssetDef;
bool smithyOpen();                                    // the fitting screen is up (frees the cursor, look frozen)
bool isSmithy(const AssetDef* a);                     // asset JSON "smithy": true
void smithyOpenAt(App& app, uint64_t tableId);        // E at a table
void smithyClose(App& app);
void smithyUi(App& app);                              // each explore frame
bool smithyApply(App& app, int weapon, int slot, const std::string& itemId, std::string* why = nullptr);   // fit (itemId) or remove (empty)
void smithyBegin(App& app, Game& G);                  // a match starts: Battle Royale places a handful (every peer the same)
void smithyEnd(App& app);
struct Instance; uint64_t smithyPlaceIn(App& app, const Instance& building);   // a table inside it (0: no room)
const std::vector<uint64_t>& smithyPlaced();
struct Kit; void smithyUseKit(Kit* k);           // self-tests: the carried parts (null: the local soldier's)
