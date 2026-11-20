// ED2 user stat overrides: see overrides.h
#include "overrides.h"
#include <fstream>

namespace {
bool gLoaded = false;
json gW = json::object(), gV = json::object(), gA = json::object();
// beside the exe, not in content/ (a build's content/ may be a link to the source tree: CMake NMK_CONTENT_LINK); files an
// older build left in content/<kind>/overrides_user.json are still read until the first save
fs::path userPath(const char* kind) { return exeDir() / "overrides" / (std::string(kind) + ".json"); }
fs::path legacyPath(const char* kind) { return contentDir() / kind / "overrides_user.json"; }
fs::path wPath() { return userPath("weapons"); }
fs::path vPath() { return userPath("vehicles"); }
fs::path aPath() { return userPath("ai"); }   // UI3 (h)
json readObj(const fs::path& p) {
  std::error_code ec;
  if (!fs::exists(p, ec)) return json::object();
  try { json j = json::parse(readTextFile(p)); if (j.is_object()) return j; logf("overrides: %s is not an object (ignored)", p.string().c_str()); }
  catch (std::exception& e) { logf("overrides: %s: %s (ignored)", p.string().c_str(), e.what()); }
  return json::object();
}
json readKind(const char* kind) { std::error_code ec; return readObj(fs::exists(userPath(kind), ec) ? userPath(kind) : legacyPath(kind)); }
void ensure() { if (!gLoaded) { gLoaded = true; gW = readKind("weapons"); gV = readKind("vehicles"); gA = readKind("ai"); } }
bool writeObj(const fs::path& p, const json& j, std::string* err) {
  std::error_code ec;
  if (j.empty()) { fs::remove(p, ec); return true; }   // nothing overridden: no file
  fs::create_directories(p.parent_path(), ec);
  std::ofstream f(p, std::ios::binary);
  if (!f) { if (err) *err = "cannot write " + p.string(); return false; }
  f << j.dump(1);
  return (bool)f;
}
}

json& weaponOverrides() { ensure(); return gW; }
json& vehicleOverrides() { ensure(); return gV; }
json& aiOverrides() { ensure(); return gA; }
void reloadOverrides() { gLoaded = false; ensure(); }
bool saveOverrides(std::string* err) {
  ensure();
  for (json* j : {&gW, &gV}) for (auto it = j->begin(); it != j->end();) { if (it->is_object() && it->empty()) it = j->erase(it); else ++it; }   // drop emptied items
  if (gA.contains("types") && gA["types"].is_object()) { json& t = gA["types"]; for (auto it = t.begin(); it != t.end();) { if (it->is_object() && it->empty()) it = t.erase(it); else ++it; } if (t.empty()) gA.erase("types"); }
  if (gA.contains("_soldier") && gA["_soldier"].is_object() && gA["_soldier"].empty()) gA.erase("_soldier");
  bool ok = writeObj(wPath(), gW, err) && writeObj(vPath(), gV, err) && writeObj(aPath(), gA, err);
  std::error_code ec; if (ok) for (const char* k : {"weapons", "vehicles", "ai"}) fs::remove(legacyPath(k), ec);   // moved beside the exe
  return ok;
}
int overrideCount() {
  ensure(); int n = 0; for (json* j : {&gW, &gV}) for (auto& [k, v] : j->items()) if (!v.is_object() || !v.empty()) ++n;
  if (gA.contains("types")) for (auto& [k, v] : jkey(gA, "types").items()) if (!v.is_object() || !v.empty()) ++n;
  if (gA.contains("_soldier") && !gA["_soldier"].empty()) ++n;
  return n;
}   // items actually changed
