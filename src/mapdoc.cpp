#include "mapdoc.h"
#include <fstream>
#include <cstring>
#include <set>

// TR1: the generated trees replaced by model trees (content/settings/tree_aliases.json: old id -> model variants). A map
// (template or saved) that places an old id loads each such tree as one of its variants, picked by the instance id
// (the same tree every load, a mix across a wood); the old ids stay hidden from the palette (palette.cpp)
// TR1 / VR1: generated tree and rock ids -> their model variants (content/settings/tree_aliases.json, rock_aliases.json);
// a rock id under "removed" is dropped on load (aliasRemoved)
static std::set<std::string> gAliasRemoved;
const std::vector<std::string>* treeAlias(const std::string& id) {
  static const std::map<std::string, std::vector<std::string>> table = [] {
    std::map<std::string, std::vector<std::string>> t;
    for (const char* f : {"tree_aliases.json", "rock_aliases.json"}) try {
      json j = json::parse(readTextFile(contentDir() / "settings" / f));
      for (auto& [k, v] : j.value("aliases", json::object()).items()) for (auto& x : v) t[k].push_back(x.get<std::string>());
      for (auto& x : j.value("removed", json::array())) gAliasRemoved.insert(x.get<std::string>());
    } catch (...) {}
    return t;
  }();
  auto it = table.find(id);
  return it == table.end() || it->second.empty() ? nullptr : &it->second;
}
bool aliasRemoved(const std::string& id) { treeAlias(id); return gAliasRemoved.count(id) > 0; }

// ground paint chunks: run-length (count, r, g, b, a) then base64 (docs/lasso.md#map-format)
static const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static std::string paintEncode(const std::vector<uint8_t>& t) {
  std::vector<uint8_t> r;
  for (size_t i = 0; i + 3 < t.size();) {
    size_t j = i + 4; int n = 1;
    while (j + 3 < t.size() && n < 255 && std::memcmp(&t[i], &t[j], 4) == 0) { j += 4; ++n; }
    r.push_back((uint8_t)n); r.insert(r.end(), t.begin() + (long)i, t.begin() + (long)i + 4);
    i = j;
  }
  std::string s;
  for (size_t i = 0; i < r.size(); i += 3) {
    uint32_t v = (uint32_t)r[i] << 16 | (i + 1 < r.size() ? (uint32_t)r[i + 1] << 8 : 0) | (i + 2 < r.size() ? r[i + 2] : 0);
    s += kB64[v >> 18 & 63]; s += kB64[v >> 12 & 63]; s += i + 1 < r.size() ? kB64[v >> 6 & 63] : '='; s += i + 2 < r.size() ? kB64[v & 63] : '=';
  }
  return s;
}
static bool paintDecode(const std::string& s, std::vector<uint8_t>& t, size_t texels) {
  std::vector<uint8_t> r; uint32_t acc = 0; int bits = 0;
  for (char c : s) {
    const char* p = c ? std::strchr(kB64, c) : nullptr;
    if (!p) continue;   // '=' padding
    acc = acc << 6 | (uint32_t)(p - kB64); bits += 6;
    if (bits >= 8) { bits -= 8; r.push_back((uint8_t)(acc >> bits & 255)); }
  }
  t.clear();
  for (size_t i = 0; i + 5 <= r.size(); i += 5) {
    for (int k = 0; k < r[i]; ++k) t.insert(t.end(), r.begin() + (long)i + 1, r.begin() + (long)i + 5);
  }
  return t.size() == texels * 4;
}

json mapToJson(const MapDoc& d) {
  json j = d.extra.is_object() ? d.extra : json::object();
  j["format"] = "normandy-mapmaker-map";
  j["schemaVersion"] = kMapSchemaVersion;
  j["name"] = d.name;
  j["template"] = {{"id", d.templateId}, {"version", d.templateVersion}, {"seed", d.seed}};
  if (d.width > 0 && d.depth > 0) j["mapSize"] = {{"width", d.width}, {"depth", d.depth}};
  j["nextId"] = d.nextId;
  json inst = json::array();
  for (auto& i : d.instances) {
    if (i.extra.is_object() && i.extra.contains("dressing")) continue;   // MA3 foundation dressing: derived on load, never stored
    json o = i.extra.is_object() ? i.extra : json::object();
    o["id"] = i.id;
    o["asset"] = {{"id", i.assetId}, {"version", i.assetVersion}};
    o["position"] = tojson(i.pos);
    o["yaw"] = i.yaw;
    json ov = json::object();
    for (auto& [k, v] : i.openDefaults) ov[k] = {{"startsOpen", v}};
    json pv = json::object();
    for (auto& [k, v] : i.panelHp) pv[k] = {{"hp", v}};
    o["overrides"] = {{"interactables", ov}};
    if (!pv.empty()) o["overrides"]["panels"] = pv;
    if (!i.leafState.empty()) { json lv = json::object(); for (auto& [k, v] : i.leafState) lv[k] = v == 2 ? "removed" : "glass_broken"; o["overrides"]["leaves"] = lv; }
    inst.push_back(o);
  }
  j["instances"] = inst;
  j["removedTemplateVegetation"] = json(std::vector<std::string>(d.removedVegetation.begin(), d.removedVegetation.end()));
  j["exploreStart"] = d.start.set ? json{{"position", tojson(d.start.pos)}, {"yaw", d.start.yaw}} : json(nullptr);
  if (!d.terrainDeltas.empty()) {
    json tiles = json::array();
    for (auto& [k, v] : d.terrainDeltas) {
      json a = json::array();
      for (float f : v) a.push_back(std::round(f * 1000.0f) / 1000.0f);
      tiles.push_back({{"key", k}, {"d", a}});
    }
    j["terrainDeltas"] = {{"templateVersion", d.terrainTemplateVersion}, {"fineN", 36}, {"tiles", tiles}};
  }
  if (!d.terrainSculpt.empty()) {   // V8: same tile layout as terrainDeltas; keys follow the grid (terrainGen ring)
    json tiles = json::array();
    for (auto& [k, v] : d.terrainSculpt) { json a = json::array(); for (float f : v) a.push_back(std::round(f * 1000.0f) / 1000.0f); tiles.push_back({{"key", k}, {"d", a}}); }
    j["terrainSculpt"] = {{"fineN", 36}, {"tiles", tiles}};
  }
  if (d.terrainGen.is_object() && !d.terrainGen.empty()) j["terrainGen"] = d.terrainGen;
  if (!d.waterBodies.empty()) {   // lasso water fill (docs/lasso.md), mm precision
    auto mm = [](float f) { return std::round(f * 1000.0f) / 1000.0f; };
    json wb = json::array();
    for (auto& w : d.waterBodies) {
      json o = json::array();
      for (vec2 p : w.outline) o.push_back(json::array({mm(p.x), mm(p.y)}));
      wb.push_back({{"id", w.id}, {"kind", w.kind}, {"level", mm(w.level)}, {"seed", json::array({mm(w.seed.x), mm(w.seed.y)})}, {"outline", o}});
    }
    j["waterBodies"] = wb;
  }
  if (!d.groundPaint.empty()) {   // lasso ground paint
    json ch = json::array();
    for (auto& [k, v] : d.groundPaint) ch.push_back({{"c", json::array({(int)(k >> 32), (int)(int32_t)(uint32_t)(k & 0xffffffffu)})}, {"d", paintEncode(v)}});
    j["groundPaint"] = {{"texel", 0.5}, {"chunk", 32}, {"chunks", ch}};
  }
  if (d.camera.set) j["editorCamera"] = {{"position", tojson(d.camera.pos)}, {"yaw", d.camera.yaw}, {"pitch", d.camera.pitch}};
  if (d.camera.set && d.cameraBookmarks.is_object() && !d.cameraBookmarks.empty()) j["editorCamera"]["bookmarks"] = d.cameraBookmarks;
  return j;
}

bool saveMap(const fs::path& path, const MapDoc& d, std::string& err) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  fs::path tmp = path; tmp += ".tmp";
  fs::path bak = path; bak += ".bak";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) { err = "Cannot write " + tmp.string(); return false; }
    out << mapToJson(d).dump(2);
    out.flush();
    if (!out) { err = "Write failed for " + tmp.string(); return false; }
  }
  // verify the temp file parses before replacing anything
  try { json::parse(readTextFile(tmp)); } catch (std::exception& e) { err = std::string("Verification failed: ") + e.what(); return false; }
  if (fs::exists(path)) {
    fs::remove(bak, ec);
    fs::rename(path, bak, ec);
    if (ec) { err = "Could not keep previous version: " + ec.message(); return false; }
  }
  fs::rename(tmp, path, ec);
  if (ec) { err = "Could not replace save: " + ec.message(); if (fs::exists(bak)) fs::rename(bak, path, ec); return false; }
  return true;
}

bool loadMap(const fs::path& path, MapDoc& d, std::string& err) {
  json j;
  try { j = json::parse(readTextFile(path)); } catch (std::exception& e) { err = std::string("Parse error: ") + e.what(); return false; }
  if (j.value("format", "") != "normandy-mapmaker-map") { err = "Not a Battle Foundry map"; return false; }
  int ver = j.value("schemaVersion", 0);
  if (ver > kMapSchemaVersion) { err = "Map was saved by a newer version (schema " + std::to_string(ver) + ")"; return false; }
  MapDoc n;
  n.extra = j;
  for (const char* k : {"format", "schemaVersion", "name", "template", "nextId", "instances", "removedTemplateVegetation", "exploreStart", "editorCamera", "mapSize"}) n.extra.erase(k);
  if (j.contains("terrainDeltas") && jkey(j, "terrainDeltas").value("templateVersion", -1) == jkey(j, "template").value("version", 1) && jkey(j, "terrainDeltas").value("fineN", 0) == 36) {
    n.terrainTemplateVersion = j["terrainDeltas"]["templateVersion"];
    for (auto& t : j["terrainDeltas"]["tiles"]) n.terrainDeltas[t["key"].get<int>()] = t["d"].get<std::vector<float>>();
    n.extra.erase("terrainDeltas");
  }   // otherwise kept verbatim in extra (deltas for another template version are preserved, not applied)
  if (j.contains("terrainSculpt") && jkey(j, "terrainSculpt").value("fineN", 0) == 36) {   // V8 sculpt layer
    for (auto& t : j["terrainSculpt"]["tiles"]) n.terrainSculpt[t["key"].get<int>()] = t["d"].get<std::vector<float>>();
    n.extra.erase("terrainSculpt");
  }
  if (j.contains("terrainGen") && j["terrainGen"].is_object()) { n.terrainGen = j["terrainGen"]; n.extra.erase("terrainGen"); }
  if (j.contains("waterBodies") && j["waterBodies"].is_array()) {   // lasso water fill
    for (auto& o : j["waterBodies"]) {
      WaterBody w;
      w.id = o.value("id", 0ull); w.kind = o.value("kind", std::string("pond")); w.level = o.value("level", 0.0f);
      if (o.contains("seed") && o["seed"].is_array() && o["seed"].size() >= 2) w.seed = vec2(o["seed"][0].get<float>(), o["seed"][1].get<float>());
      for (auto& p : o.value("outline", json::array())) if (p.is_array() && p.size() >= 2) w.outline.push_back(vec2(p[0].get<float>(), p[1].get<float>()));
      if (w.outline.size() >= 3) n.waterBodies.push_back(w);
    }
    n.extra.erase("waterBodies");
  }
  if (j.contains("groundPaint") && j["groundPaint"].is_object() && jkey(j, "groundPaint").value("chunk", 0) == 32) {   // lasso ground paint
    uint64_t sig = 1469598103934665603ull;
    for (auto& c : jkey(j, "groundPaint").value("chunks", json::array())) {
      if (!c.contains("c") || !c["c"].is_array() || c["c"].size() < 2) continue;
      std::vector<uint8_t> t;
      if (!paintDecode(c.value("d", std::string()), t, 32 * 32)) continue;
      int64_t key = (int64_t)((uint64_t)(uint32_t)c["c"][0].get<int>() << 32 | (uint32_t)c["c"][1].get<int>());
      for (uint8_t b : t) sig = (sig ^ b) * 1099511628211ull;
      n.groundPaint[key] = std::move(t);
    }
    n.paintSig = n.groundPaint.empty() ? 0 : sig | 1;
    n.extra.erase("groundPaint");
  }
  n.name = j.value("name", "Untitled map");
  n.templateId = jkey(j, "template").value("id", "blank_slate");
  n.templateVersion = jkey(j, "template").value("version", 1);
  n.seed = jkey(j, "template").value("seed", 1u);
  if (j.contains("mapSize") && j["mapSize"].is_object()) { n.width = jkey(j, "mapSize").value("width", 0.0f); n.depth = jkey(j, "mapSize").value("depth", 0.0f); }   // absent: pre-9.1 map, template size
  n.nextId = j.value("nextId", 1ull);
  for (auto& o : j.value("instances", json::array())) {
    Instance i;
    i.extra = o;
    for (const char* k : {"id", "asset", "position", "yaw", "overrides"}) i.extra.erase(k);
    i.id = o.value("id", 0ull);
    i.assetId = jkey(o, "asset").value("id", "");
    i.assetVersion = jkey(o, "asset").value("version", 1);
    if (aliasRemoved(i.assetId)) continue;   // VR1: a generated rock with no model rock yet
    if (const auto* al = treeAlias(i.assetId)) { uint64_t h = i.id * 0x9E3779B97F4A7C15ull; h ^= h >> 29; i.assetId = (*al)[(size_t)(h % al->size())]; i.assetVersion = 1; }   // TR1
    i.pos = jvec3(o["position"]);
    i.yaw = o.value("yaw", 0.0f);
    if (o.contains("overrides") && o["overrides"].contains("interactables"))
      for (auto& [k, v] : o["overrides"]["interactables"].items()) i.openDefaults[k] = v.value("startsOpen", false);
    if (o.contains("overrides") && o["overrides"].contains("panels"))
      for (auto& [k, v] : o["overrides"]["panels"].items()) i.panelHp[k] = v.value("hp", 0.0f);
    if (o.contains("overrides") && o["overrides"].contains("leaves"))
      for (auto& [k, v] : o["overrides"]["leaves"].items()) i.leafState[k] = v.is_string() && v.get<std::string>() == "removed" ? 2 : 1;
    n.nextId = std::max(n.nextId, i.id + 1);
    n.instances.push_back(i);
  }
  for (auto& s : j.value("removedTemplateVegetation", json::array())) n.removedVegetation.insert(s.get<std::string>());
  if (j.contains("exploreStart") && j["exploreStart"].is_object()) {
    n.start.set = true; n.start.pos = jvec3(j["exploreStart"]["position"]); n.start.yaw = jkey(j, "exploreStart").value("yaw", 0.0f);
  }
  if (j.contains("editorCamera")) {
    n.camera.set = true; n.camera.pos = jvec3(j["editorCamera"]["position"]);
    n.camera.yaw = jkey(j, "editorCamera").value("yaw", 0.0f); n.camera.pitch = jkey(j, "editorCamera").value("pitch", -20.0f);
    if (j["editorCamera"].contains("bookmarks") && j["editorCamera"]["bookmarks"].is_object()) n.cameraBookmarks = j["editorCamera"]["bookmarks"];
  }
  d = std::move(n);
  return true;
}
