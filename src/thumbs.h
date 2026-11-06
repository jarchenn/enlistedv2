#pragma once
// Build-menu thumbnails (W2, spec 9.4). See docs/thumbnails.md.
//  - framing: the camera frames the asset's rendered silhouette (box parts, panels, interactable leaves, imported
//    models including thin wire, tree canopies, glass) at a 3/4 view; when that view leaves the tile too empty (thin
//    or tall assets) the view with the largest filled share of the tile is used instead (kThumbMinFill); a
//    sparse silhouette that still fills under kThumbCropFill (a 10 m fence) is shown as its densest square window
//  - look: one light rig fixed to the camera (key from upper left, no shadows, no fog), flat neutral background
//  - cache: <exe>/cache/thumbs/<id>-<key>.png; the key hashes the asset JSON, its baked model files and
//    kThumbVersion, so editing or re-baking an asset regenerates its tile on the next start
#include "common.h"
#include <map>

class App;
struct AssetDef;

constexpr const char* kThumbVersion = "w2-thumbs-2";   // bump when the thumbnail look / framing changes
constexpr int kThumbSize = 256;
constexpr float kThumbMinFill = 0.15f;                 // 3/4 view kept when it fills at least this share of the tile
constexpr float kThumbCropFill = 0.07f;                // below this the tile is a detail crop of the silhouette (fences)

struct ThumbStats { int rendered = 0, loaded = 0, searched = 0, external = 0; };   // external: vehicles (TankSys tiles)
struct ThumbInfo { std::string key; bool cached = false; float fill = 0; float yaw = 0, pitch = 0; bool searched = false, cropped = false; };

std::string footprintLabel(vec2 fp);              // build-menu footprint: one decimal, "0.3 × 0.3 m"
std::string assetThumbKey(const AssetDef& a);     // 16 hex digits
fs::path thumbCacheDir();                         // <exe>/cache/thumbs, or $NMK_THUMB_CACHE
ThumbStats makeAssetThumbnails(App& app);         // every catalogue asset: cached tile or a fresh render
float thumbBackgroundShare(App& app, GLuint tex);  // share of the tile that shows only the background (0..1)
void saveThumbPng(GLuint tex, const fs::path& p);  // a tile as PNG (top row first)
bool sameThumb(GLuint a, GLuint b);               // pixel-identical tiles

extern std::map<std::string, ThumbInfo> gThumbInfo;     // last build, per asset id
extern std::map<std::string, std::string> gThumbSalt;   // self-test hook: extra key text per asset id ("asset changed")
