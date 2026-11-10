#pragma once
// MA3 foundation dressing (docs/foundation.md): grass, weeds, moss, leaf litter, small stones and a few authored prop
// arrangements around building bases. Deterministic per building (instance id + asset + placement + map seed), derived
// at run time and never saved: plants are instance buffers drawn through the nature-patch grass path, props are
// ordinary instances tagged extra["dressing"] = {host, sig} with ids derived from the host id (identical on every
// machine), skipped by the map file and the map hash. foundationSync rebuilds a building's dressing when it is placed,
// moved, deleted or loaded, and prunes props next to wall panels that are breached (restored by a reset).
// A host's optional extra["dressingStyle"] ("maintained" | "neglected" | "damaged" | "bunker" | "coastal" | "none")
// overrides the style chosen from the asset, the map and the seed.
#include "common.h"
#include <vector>

class App; struct Instance; struct DrawItem;

enum FoundationStyle : int { FS_MAINTAINED = 0, FS_NEGLECTED, FS_DAMAGED, FS_BUNKER, FS_COASTAL, FS_COUNT };
const char* foundationStyleName(int s);

bool isDressingPiece(const Instance& i);
void foundationSync(App& app);                                     // once per frame (all modes)
void foundationDrawItems(App& app, std::vector<DrawItem>& op, vec3 eye);   // plant patches (R.natPatches) + stones
void foundationReset(App& app);                                    // drop every record and dressing prop (tests: a "reload")

struct FoundationStats { int hosts = 0, props = 0, plants = 0, stones = 0, rebuilds = 0, pruned = 0; double lastMs = 0; };
FoundationStats foundationStats();
// tests: one host's dressing (world plant roots, stone centres, style); false = no record
bool foundationHostInfo(uint64_t host, std::vector<vec3>& plants, std::vector<vec3>& stones, int& style);
