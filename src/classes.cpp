#include "classes.h"

namespace {
std::vector<ClassDef> gClasses;

std::vector<std::string> strs(const json& j, const char* k) {
  std::vector<std::string> v;
  if (j.contains(k) && j[k].is_array()) for (auto& e : j[k]) v.push_back(e.get<std::string>());
  return v;
}
ClassSide side(const json& j) {
  ClassSide s;
  if (!j.is_object()) return s;
  s.name = j.value("name", ""); s.primary = strs(j, "primary"); s.secondary = strs(j, "secondary"); s.pistol = j.value("pistol", "");
  return s;
}
}  // namespace

bool loadClasses(const fs::path& file, std::string& err) {
  gClasses.clear();
  json j;
  try { j = json::parse(readTextFile(file)); } catch (const std::exception& e) { err = file.string() + ": " + e.what(); return false; }
  if (!j.contains("classes") || !j["classes"].is_array()) { err = file.string() + ": no \"classes\" array"; return false; }
  for (auto& c : j["classes"]) {
    ClassDef d;
    d.id = c.value("id", "");
    if (d.id.empty() || classIndex(d.id) >= 0) { err = "class without id or duplicate id '" + d.id + "'"; gClasses.clear(); return false; }
    d.us = side(c.value("US", json())); d.de = side(c.value("DE", json()));
    d.frags = c.value("frags", 0); d.smokes = c.value("smokes", 0); d.packs = c.value("explosionPacks", 0);
    d.ability = c.value("ability", ""); d.builds = c.value("builds", false); d.drivesTanks = c.value("drivesTanks", false);
    d.hpMul = c.value("hpMul", 1.0f); d.staminaMul = c.value("staminaMul", 1.0f); d.botWeight = c.value("botWeight", 1.0f);
    gClasses.push_back(d);
  }
  return !gClasses.empty();
}

const std::vector<ClassDef>& classDefs() { return gClasses; }
int classIndex(const std::string& id) {
  for (size_t k = 0; k < gClasses.size(); ++k) if (gClasses[k].id == id) return (int)k;
  return -1;
}
const ClassDef* classById(const std::string& id) { int k = classIndex(id); return k < 0 ? nullptr : &gClasses[k]; }
