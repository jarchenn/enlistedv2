#pragma once
// HB1b sandbags (src/sandbags.cpp): sack-material boxes drawn as the supplied bag meshes, instanced
#include "assets.h"
struct App;
int sandbagKind(const std::string& mat, vec3 size);   // 0 not a sandbag, 1 a bag, 2 a tie / ear / seam, 3 a course block
void sandbagBoxes(const Part& p, uint32_t seed, App& app, std::vector<BagXf>& out, vec3 tint = vec3(1));   // the bags for one part (asset space)
void sandbagDirt(std::vector<BagXf>& bags, size_t from);   // the bags from `from` on darken toward their piece's ground (y)
const std::vector<BagXf>& sandbagXforms(App& app, const AssetDef& a, bool broken);
void sandbagDraw(App& app, const AssetDef& a, const mat4& M, float camDist, bool broken);
void sandbagDrawList(App& app, const std::vector<BagXf>& bags, const std::vector<uint8_t>& secCut, const mat4& M, float camDist);
