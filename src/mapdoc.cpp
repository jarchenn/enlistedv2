#include "mapdoc.h"
#include <fstream>

json mapToJson(const MapDoc& d) {
  json j = d.extra.is_object() ? d.extra : json::object();
  j["format"] = "normandy-mapmaker-map";
  j["schemaVersion"] = kMapSchemaVersion;
  j["name"] = d.name;
  j["template"] = {{"id", d.templateId}, {"version", d.templateVersion}, {"seed", d.seed}};
  j["nextId"] = d.nextId;
  json inst = json::array();
  for (auto& i : d.instances) {
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
  if (d.camera.set) j["editorCamera"] = {{"position", tojson(d.camera.pos)}, {"yaw", d.camera.yaw}, {"pitch", d.camera.pitch}};
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
  if (j.value("format", "") != "normandy-mapmaker-map") { err = "Not a Normandy Mapmaker map"; return false; }
  int ver = j.value("schemaVersion", 0);
  if (ver > kMapSchemaVersion) { err = "Map was saved by a newer version (schema " + std::to_string(ver) + ")"; return false; }
  MapDoc n;
  n.extra = j;
  for (const char* k : {"format", "schemaVersion", "name", "template", "nextId", "instances", "removedTemplateVegetation", "exploreStart", "editorCamera"}) n.extra.erase(k);
  if (j.contains("terrainDeltas") && j["terrainDeltas"].value("templateVersion", -1) == j["template"].value("version", 1) && j["terrainDeltas"].value("fineN", 0) == 36) {
    n.terrainTemplateVersion = j["terrainDeltas"]["templateVersion"];
    for (auto& t : j["terrainDeltas"]["tiles"]) n.terrainDeltas[t["key"].get<int>()] = t["d"].get<std::vector<float>>();
    n.extra.erase("terrainDeltas");
  }   // otherwise kept verbatim in extra (deltas for another template version are preserved, not applied)
  n.name = j.value("name", "Untitled map");
  n.templateId = j["template"].value("id", "blank_slate");
  n.templateVersion = j["template"].value("version", 1);
  n.seed = j["template"].value("seed", 1u);
  n.nextId = j.value("nextId", 1ull);
  for (auto& o : j.value("instances", json::array())) {
    Instance i;
    i.extra = o;
    for (const char* k : {"id", "asset", "position", "yaw", "overrides"}) i.extra.erase(k);
    i.id = o.value("id", 0ull);
    i.assetId = o["asset"].value("id", "");
    i.assetVersion = o["asset"].value("version", 1);
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
    n.start.set = true; n.start.pos = jvec3(j["exploreStart"]["position"]); n.start.yaw = j["exploreStart"].value("yaw", 0.0f);
  }
  if (j.contains("editorCamera")) {
    n.camera.set = true; n.camera.pos = jvec3(j["editorCamera"]["position"]);
    n.camera.yaw = j["editorCamera"].value("yaw", 0.0f); n.camera.pitch = j["editorCamera"].value("pitch", -20.0f);
  }
  d = std::move(n);
  return true;
}
