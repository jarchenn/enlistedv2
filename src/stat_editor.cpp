// ED2 Guns / Vehicles screens of the main menu (mainmenu.cpp draws the glass panel and calls these into its content rect).
// Every field comes from the item's own JSON (the shipped file, with its local-only patch), so new weapons and vehicles
// appear by themselves; geometry blocks (parts, materials, sights, sockets, model, rig, colliders) are left out. Guns
// also show their damage-table row (content/ai/damage.json) and their visual-recoil row (handling.json vmRecoil), the
// helicopter the shared flight tuning (settings/helicopter.json). An edit is stored as a JSON merge patch in the
// git-ignored overrides files (overrides.h); "Apply now" (or leaving the screen) reloads the definitions.
#include "app.h"
#include "overrides.h"
#include "damage_model.h"
#include "vm_anim.h"
#include "character.h"
#include "ui_flat.h"   // UI5: category tabs
#include <map>
#include <memory>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>

void heliTuneReset();      // heli.cpp
void brRailCacheClear();   // br_models.cpp
void zombieCfgReload();    // zombie.cpp

void App::reloadGameplayDefs() {
  animVmReset(); brRailCacheClear();
  buildWeaponMeshes(); makeWeaponSilhouettes(); layoutRackDisplays();
  damageTableReload(); heliTuneReset(); zombieCfgReload();   // UI3: enemy types with the AI overrides
  tanks.loadDefs(*this); tanks.sync(*this);
  resetLoadout();
  logf("ed2: gameplay definitions reloaded with %d override(s)", overrideCount());
}

namespace {
struct Ed {
  int kind = 0;                 // 0 guns, 1 vehicles, 2 AI types (UI3: the Character screen's AI TYPES tab)
  std::string sel[3], search[3];
  int group[3] = {0, 0, 0};   // UI5: the category tab of each page
  bool raw = false; std::string rawText, rawErr; std::string rawFor;
  bool dirty = false; double resetArm = -10; std::string status;
  // preview
  GLuint tex = 0; int w = 0, h = 0; float yaw = 30; double next = 0; std::string pvFor;
} E;

const char* kHidden[] = {"id", "name", "side", "modern", "sights", "muzzle", "muzzleDir", "rail", "railScale", "sockets", "model", "animated", "parts", "materials",
                         "fallbackModel", "identity", "colliders", "rig", "historical", "credit", "base", "car", "heli", "faction", "posCorrection", "rotCorrection",
                         "modelOffset", "pivot", "muzzleOffset", "exits", "exhausts", "wheels", "seats3d"};
bool hidden(const std::string& k) {
  if (k.empty() || k[0] == '_' || k.rfind("note", 0) == 0 || k == "source") return true;
  for (const char* h : kHidden) if (k == h) return true;
  return false;
}
// "falloffStart_m" -> "Falloff start", "m"; units from the key's suffix or its meaning
std::string pretty(const std::string& k, std::string& unit) {
  std::string base = k; unit.clear();
  static const std::pair<const char*, const char*> suf[] = {{"_m", "m"}, {"_s", "s"}, {"_deg", "deg"}, {"_kg", "kg"}, {"Kmh", "km/h"}, {"_hp", "hp"}, {"Deg", "deg"}, {"Rpm", "rpm"}};
  for (auto& [s, u] : suf) { size_t n = std::strlen(s); if (base.size() > n && base.compare(base.size() - n, n, s) == 0) { base = base.substr(0, base.size() - n); unit = u; break; } }
  std::string lk = k; for (auto& c : lk) c = (char)std::tolower((unsigned char)c);
  if (unit.empty()) {
    if (lk == "rpm") unit = "rpm"; else if (lk.find("velocity") != std::string::npos || lk == "vel" || lk.find("speed") != std::string::npos) unit = "m/s";
    else if (lk.find("reload") != std::string::npos || lk.find("time") != std::string::npos || lk == "empty" || lk == "partial" || lk == "round") unit = "s";
    else if (lk.find("damage") != std::string::npos || lk == "near" || lk == "far" || lk.find("dmg") != std::string::npos || lk.find("blast") != std::string::npos) unit = "hp";
    else if (lk.find("rate") != std::string::npos || lk.find("yaw") != std::string::npos || lk.find("elevation") != std::string::npos || lk.find("pitch") != std::string::npos || lk.find("spread") != std::string::npos) unit = "deg";
    else if (lk.find("mass") != std::string::npos) unit = lk.find("bullet") != std::string::npos ? "g" : "kg";
    else if (lk.find("radius") != std::string::npos || lk.find("range") != std::string::npos || lk == "zero") unit = "m";
  }
  std::string out;
  for (size_t i = 0; i < base.size(); ++i) {
    char c = base[i];
    if (c == '_') { out += ' '; continue; }
    if (i > 0 && std::isupper((unsigned char)c) && !std::isupper((unsigned char)base[i - 1])) out += ' ';
    out += out.empty() ? (char)std::toupper((unsigned char)c) : (char)std::tolower((unsigned char)c);
  }
  return out;
}
// friendly section names (each machine gun listed by its own "name")
std::string sectionName(const std::string& k, const json& v) {
  if (v.is_object() && v.contains("name") && v["name"].is_string() && k != "gun") return v["name"].get<std::string>() + "  (" + k + ")";
  static const std::pair<const char*, const char*> m[] = {{"gun", "Main gun"}, {"ammunition", "Main gun ammunition"}, {"roofMg", "Roof machine gun"}, {"bowMg", "Bow / coax machine gun"},
    {"tow", "TOW missiles"}, {"mobility", "Mobility"}, {"durability", "Armour and durability"}, {"ramming", "Ramming"}, {"recoil", "Recoil (aim)"}, {"reload", "Reload"},
    {"ads", "Aiming down sights"}, {"flame", "Flame"}, {"scope", "Scope"}, {"_damage", "Damage (damage table)"}, {"_handling", "Visual recoil and flash"}, {"_helicopter", "Flight (all helicopters)"},
    {"chinGun", "Chin cannon"}, {"rockets", "Rockets"}, {"flight", "Flight"}};
  for (auto& [a, b] : m) if (k == a) return b;
  std::string u; return pretty(k, u);
}

json readJson(const fs::path& p) { try { return json::parse(readTextFile(p)); } catch (...) { return json::object(); } }
json readJsonQuiet(const fs::path& p) { std::error_code ec; return fs::exists(p, ec) ? readJson(p) : json::object(); }
// the shipped item without the user's overrides
json weaponBase(const WeaponDef& d) {
  json j = readJson(d.file);
  fs::path lp = d.file.parent_path() / "local" / d.file.filename();
  std::error_code ec; if (fs::exists(lp, ec)) j.merge_patch(readJson(lp));
  json dm = readJson(contentDir() / "ai" / "damage.json");
  j["_damage"] = dm.contains("weapons") && dm["weapons"].contains(d.id) ? dm["weapons"][d.id] : dm.value("default", json::object());
  json h = readJson(contentDir() / "weapons" / "handling.json"), vr = h.value("vmRecoil", json::object());
  json w = vr.value("weapons", json::object()).value(d.id, json::object());
  std::string kind = w.value("kind", d.wclass == WC_SNIPER ? "sniper" : d.wclass == WC_LMG ? "lmg" : d.wclass == WC_AUTO ? "assault" : "rifle");
  json r = vr.value("kinds", json::object()).value(kind, json::object()); r.merge_patch(w); r.erase("kind"); r.erase("suppressed");
  j["_handling"] = r;
  return j;
}
json vehicleBase(const std::string& id) {
  fs::path dir = contentDir() / "vehicles";
  json j = readJson(dir / (id + ".json"));
  if (j.contains("base") && j["base"].is_string()) { json b = readJson(dir / (j["base"].get<std::string>() + ".json")); b.erase("fallbackModel"); b.merge_patch(j); b.erase("base"); j = b; }
  return j;
}
// UI3 (h): the AI overrides keep enemy types under "types" and the soldier data under "_soldier" (overrides.h)
json& overrideTable(int kind, const std::string& id) {
  if (kind < 2) return kind == 0 ? weaponOverrides() : vehicleOverrides();
  json& a = aiOverrides(); if (id == "_soldier") return a;
  if (!a.contains("types") || !a["types"].is_object()) a["types"] = json::object();
  return a["types"];
}
json& itemOverride(int kind, const std::string& id) { json& o = overrideTable(kind, id); if (!o.contains(id) || !o[id].is_object()) o[id] = json::object(); return o[id]; }
// every enemy type (content/ai/zombie.json "types" plus the local-only zombie_local.json), shipped values
std::vector<json> aiTypeEntries() {
  std::vector<json> v;
  for (const char* f : {"zombie.json", "zombie_local.json"}) { json j = readJsonQuiet(contentDir() / "ai" / f); for (auto& e : j.value("types", json::array())) if (e.is_object()) v.push_back(e); }
  return v;
}
// a body model for the AI preview, in its bind pose (no clips needed; non-humanoid bodies load too)
struct AiBody { std::unique_ptr<CharModel> m; CharInstance ci; bool ok = false; };
AiBody* aiBody(const std::string& model) {
  static std::map<std::string, std::unique_ptr<AiBody>> cache;
  auto& b = cache[model];
  if (!b) {
    b = std::make_unique<AiBody>(); b->m = std::make_unique<CharModel>(); std::string e;
    const fs::path p = contentDir() / "characters" / (model + ".glb");
    if ((initSkinning(e) && (b->m->load(p, e) || b->m->load(p, e, 0, false))) && b->ci.create(*b->m)) { b->ci.skin(std::vector<mat4>(b->m->joints(), mat4(1))); b->ok = true; }
    else logf("ai editor: preview model %s: %s", model.c_str(), e.c_str());
  }
  return b->ok ? b.get() : nullptr;
}
bool hasPath(const json& j, const json::json_pointer& p) { try { return j.contains(p); } catch (...) { return false; } }
// remove a patch path; drop emptied parents
void erasePath(json& root, json::json_pointer p) {
  while (!p.empty()) {
    json::json_pointer parent = p.parent_pointer(); std::string last = p.back();
    if (!hasPath(root, parent)) return;
    json& par = root[parent];
    if (par.is_object()) par.erase(last);
    if (!par.empty() || parent.empty()) return;
    p = parent;
  }
}
// sane range: around the shipped value
void range(double base, bool integer, double& lo, double& hi) {
  double a = std::fabs(base);
  lo = base < 0 ? -std::max(10.0 * a, 1.0) : 0.0;
  hi = std::max(10.0 * a, base == 0 ? (integer ? 100.0 : 10.0) : 1.0);
}

// one value: base (shipped), cur (effective); returns true when changed; writes the patch at `path`
bool field(int kind, const std::string& id, const std::string& key, const json& base, const json& cur, const json::json_pointer& path) {
  json& ov = itemOverride(kind, id);
  const bool over = hasPath(ov, path);
  std::string unit; std::string label = pretty(key, unit);
  ImGui::PushID(path.to_string().c_str());
  ImGui::AlignTextToFramePadding();
  if (over) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.62f, 0.25f, 1));
  ImGui::TextUnformatted(label.c_str());
  if (over) ImGui::PopStyleColor();
  ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.46f);
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
  bool changed = false; json nv = cur;
  if (cur.is_boolean()) { bool b = cur.get<bool>(); if (ImGui::Checkbox("##v", &b)) { nv = b; changed = true; } }
  else if (cur.is_number_integer() || cur.is_number_unsigned()) {
    int v = cur.get<int>(); double lo, hi; range(base.is_number() ? base.get<double>() : (double)v, true, lo, hi);
    char fmt[48]; std::snprintf(fmt, sizeof fmt, "%%d %s", unit.c_str());
    if (ImGui::DragInt("##v", &v, std::max(1.0f, (float)(hi - lo) / 400.0f), (int)lo, (int)hi, fmt, ImGuiSliderFlags_AlwaysClamp)) { nv = v; changed = true; }
  } else if (cur.is_number()) {
    float v = cur.get<float>(); double lo, hi; range(base.is_number() ? base.get<double>() : (double)v, false, lo, hi);
    char fmt[48]; std::snprintf(fmt, sizeof fmt, "%%.4g %s", unit.c_str());
    if (ImGui::DragFloat("##v", &v, (float)std::max(1e-4, (hi - lo) / 1000.0), (float)lo, (float)hi, fmt, ImGuiSliderFlags_AlwaysClamp)) { nv = v; changed = true; }
  } else if (cur.is_string()) {
    char buf[160]; std::snprintf(buf, sizeof buf, "%s", cur.get<std::string>().c_str());
    bool sound = key.find("sound") != std::string::npos || key.find("Sound") != std::string::npos || key == "tail";
    if (sound) { if (ImGui::InputText("##v", buf, sizeof buf, ImGuiInputTextFlags_EnterReturnsTrue)) { nv = std::string(buf); changed = true; } }
    else ImGui::TextDisabled("%s", buf);
  } else if (cur.is_array() && !cur.empty() && cur.size() <= 4 && std::all_of(cur.begin(), cur.end(), [](const json& x) { return x.is_number(); })) {
    float v[4] = {0, 0, 0, 0}; int n = (int)cur.size(); for (int k = 0; k < n; ++k) v[k] = cur[k].get<float>();
    bool ch = n == 1 ? ImGui::DragFloat("##v", v, 0.01f) : n == 2 ? ImGui::DragFloat2("##v", v, 0.01f) : n == 3 ? ImGui::DragFloat3("##v", v, 0.01f) : ImGui::DragFloat4("##v", v, 0.01f);
    if (ch) { nv = json::array(); for (int k = 0; k < n; ++k) nv.push_back(v[k]); changed = true; }
  } else ImGui::TextDisabled("%s", cur.dump().substr(0, 60).c_str());
  if (changed) { ov[path] = nv; E.dirty = true; }
  if (ImGui::IsItemDeactivatedAfterEdit()) { std::string e; if (!saveOverrides(&e)) E.status = e; else E.status = "Saved. Applies when you leave this screen or press Apply now."; }
  if (over) { ImGui::SameLine(); if (ImGui::SmallButton("reset")) { erasePath(ov, path); saveOverrides(); E.dirty = true; } if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shipped value: %s", base.dump().c_str()); }
  ImGui::PopID();
  return changed;
}

// a JSON object's fields, its sub-objects as collapsible sections
void fields(int kind, const std::string& id, const json& base, const json& cur, const json::json_pointer& at, int depth) {
  std::vector<std::string> sections;
  // UI5: the plain fields flow in two columns (half the scrolling); sub-objects follow as full-width sections
  const bool tbl = ImGui::BeginTable("##fcols", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX);
  for (auto& [k, v] : cur.items()) {
    if (depth == 0 && hidden(k) && k != "_damage" && k != "_handling" && k != "_helicopter") continue;
    if (depth > 0 && hidden(k)) continue;
    if (v.is_object()) { sections.push_back(k); continue; }
    if (v.is_array() && (v.empty() || !v[0].is_number() || v.size() > 4)) continue;   // lists (modes, part tables): JSON view
    if (tbl) ImGui::TableNextColumn();
    field(kind, id, k, base.is_object() && base.contains(k) ? base[k] : json(), v, at / k);
  }
  if (tbl) ImGui::EndTable();
  for (auto& k : sections) {
    const json& v = cur[k];
    bool any = false; for (auto& [k2, v2] : v.items()) if (!hidden(k2) && (v2.is_number() || v2.is_boolean() || v2.is_string() || v2.is_object() || v2.is_array())) any = true;
    if (!any) continue;
    ImGui::PushID(k.c_str());
    if (ImGui::CollapsingHeader(sectionName(k, v).c_str(), depth == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
      ImGui::Indent(8);
      fields(kind, id, base.is_object() && base.contains(k) ? base[k] : json::object(), v, at / k, depth + 1);
      ImGui::Unindent(8);
    }
    ImGui::PopID();
  }
}

int gunGroup(const WeaponDef& d) { return d.side == "blaster" ? 2 : (d.side == "modern" || d.side == "Modern") ? 1 : 0; }
bool isGun(const WeaponDef& d) { return !d.equipment() && !d.tool && !d.melee.on; }
int vehGroup(const TankDef& d) {
  if (d.heli) return 3;
  if (!d.car) return 2;
  std::string id = d.id;
  return id.find("truck") != std::string::npos || id.find("ural") != std::string::npos || id.find("boxer") != std::string::npos ? 1 : 0;
}

void preview(App& app, ImVec2 mn, ImVec2 mx, const std::vector<ModelDraw>& md, const std::vector<DrawItem>& boxes, vec3 bmin, vec3 bmax, const std::string& key) {
  ImGui::SetCursorScreenPos(mn); ImGui::InvisibleButton("##turn", ImVec2(std::max(1.0f, mx.x - mn.x), std::max(1.0f, mx.y - mn.y)));
  E.yaw += ImGui::IsItemActive() ? ImGui::GetIO().MouseDelta.x * 0.6f : ImGui::GetIO().DeltaTime * 18.0f;
  const int w = std::clamp((int)(mx.x - mn.x), 16, 1200), h = std::clamp((int)(mx.y - mn.y), 16, 800);
  if (ImGui::GetTime() >= E.next || w != E.w || h != E.h || key != E.pvFor) {
    E.next = ImGui::GetTime() + 0.05; E.pvFor = key;
    vec3 c = 0.5f * (bmin + bmax); vec3 ext = bmax - bmin; float r = std::max(0.05f, 0.5f * std::max({ext.x, ext.y, ext.z}));   // frame the longest side
    mat4 world = glm::rotate(mat4(1), glm::radians(E.yaw), vec3(0, 1, 0)) * glm::translate(mat4(1), -c);
    std::vector<ModelDraw> m2 = md; for (auto& x : m2) x.m = world * x.m;
    std::vector<DrawItem> b2 = boxes; for (auto& x : b2) x.model = world * x.model;
    float fov = 30.0f, dist = r / std::tan(glm::radians(fov * 0.5f)) * std::max(1.0f, (float)h / w) * 1.05f;
    vec3 eye = vec3(0, r * 0.35f, dist);
    std::vector<uint8_t> px;
    if (app.R.renderModelSheet(m2, b2, glm::lookAt(eye, vec3(0), vec3(0, 1, 0)), glm::perspective(glm::radians(fov), (float)w / h, 0.02f, dist * 4 + r * 4), eye, w, h, px, true)) {
      if (!E.tex || w != E.w || h != E.h) {
        if (E.tex) glDeleteTextures(1, &E.tex);
        glCreateTextures(GL_TEXTURE_2D, 1, &E.tex); glTextureStorage2D(E.tex, 1, GL_RGBA8, w, h);
        glTextureParameteri(E.tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(E.tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        E.w = w; E.h = h;
      }
      glTextureSubImage2D(E.tex, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    }
  }
  if (E.tex) ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)E.tex, mn, ImVec2(mn.x + E.w, mn.y + E.h));
}
}  // namespace

bool statEditorDirty() { return E.dirty; }
// leaving the screen (or Apply now): the definitions reload with the overrides
void statEditorFlush(App& app) { if (E.dirty) { E.dirty = false; app.reloadGameplayDefs(); E.status = "Applied."; } }

void statEditorPage(App& app, int kind, ImVec2 mn, ImVec2 mx, float alpha) {
  E.kind = kind;
  ImGui::SetNextWindowPos(mn); ImGui::SetNextWindowSize(ImVec2(mx.x - mn.x, mx.y - mn.y)); ImGui::SetNextWindowBgAlpha(0);
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(1, 1, 1, 0.07f)); ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.10f)); ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.94f, 0.46f, 0.13f, 0.35f));
  if (ImGui::Begin(kind == 0 ? "##menu_guns" : kind == 1 ? "##menu_vehicles" : "##menu_ai", nullptr, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus)) {
    const float W = mx.x - mn.x, H = mx.y - mn.y, listW = W * 0.27f;
    // ---- left: global state, search, the grouped list
    ImGui::BeginChild("##list", ImVec2(listW, H), 0, ImGuiWindowFlags_NoBackground);
    int n = overrideCount();
    if (n) ImGui::TextColored(ImVec4(0.94f, 0.62f, 0.25f, 1), "OVERRIDES ACTIVE: %d", n); else ImGui::TextDisabled("Shipped values");
    if (ImGui::Button("Apply now")) statEditorFlush(app);
    ImGui::SameLine();
    bool armed = ImGui::GetTime() - E.resetArm < 3.0;
    if (ImGui::Button(armed ? "Confirm reset all" : "Reset all")) {
      if (armed) { weaponOverrides() = json::object(); vehicleOverrides() = json::object(); aiOverrides() = json::object(); saveOverrides(); E.dirty = true; E.resetArm = -10; E.status = "Every override removed."; }
      else E.resetArm = ImGui::GetTime();
    }
    char sb[96]; std::snprintf(sb, sizeof sb, "%s", E.search[kind].c_str());
    ImGui::SetNextItemWidth(-1); if (ImGui::InputTextWithHint("##search", "Search", sb, sizeof sb)) E.search[kind] = sb;
    std::string q = E.search[kind]; for (auto& c : q) c = (char)std::tolower((unsigned char)c);
    auto match = [&](const std::string& s) { std::string l = s; for (auto& c : l) c = (char)std::tolower((unsigned char)c); return q.empty() || l.find(q) != std::string::npos; };
    static const char* gG[3] = {"WWII", "Modern", "Blasters"}; static const char* gV[4] = {"Cars", "Trucks", "Tanks", "Aircraft"}; static const char* gA[2] = {"Soldiers", "Enemy types"};
    const int nG = kind == 0 ? 3 : kind == 1 ? 4 : 2;
    {   // UI5: one category at a time, picked by "//" tabs (a search looks through all of them)
      ImGui::SetWindowFontScale(0.78f); flat::tabs(kind == 0 ? gG : kind == 1 ? gV : gA, nG, E.group[kind]); ImGui::SetWindowFontScale(1.0f);
      E.group[kind] = std::clamp(E.group[kind], 0, nG - 1);
    }
    ImGui::BeginChild("##items", ImVec2(0, 0), 0, ImGuiWindowFlags_NoBackground);
    ImGui::SetWindowFontScale(1.15f); ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 7));   // UI5: larger, readable rows
    for (int g = 0; g < nG; ++g) {
      if (q.empty() && g != E.group[kind]) continue;
      if (!q.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(flat::gold())); ImGui::TextUnformatted(kind == 0 ? gG[g] : kind == 1 ? gV[g] : gA[g]); ImGui::PopStyleColor(); }
      if (kind == 2) {   // UI3 (h): every AI type
        auto item = [&](const std::string& id, const std::string& name) {
          json& tb = overrideTable(2, id); bool ov = tb.contains(id) && tb[id].is_object() && !tb[id].empty();
          if (ImGui::Selectable(((ov ? "* " : "  ") + name + "##" + id).c_str(), E.sel[2] == id)) { E.sel[2] = id; E.raw = false; }
          if (E.sel[2].empty()) E.sel[2] = id; };
        if (g == 0) { if (match("soldiers") || match("operators") || match("troopers")) item("_soldier", "Soldiers (every soldier AI)"); }
        else for (const json& e : aiTypeEntries()) { std::string id = e.value("id", std::string("zombie")), nm = e.value("name", id); if (match(nm) || match(id)) item(id, nm); }
      } else if (kind == 0) {
        for (auto& dp : app.weaponDefs) { const WeaponDef& d = *dp; if (!isGun(d) || gunGroup(d) != g || !(match(d.name) || match(d.id))) continue;
          bool ov = weaponOverrides().contains(d.id) && !weaponOverrides()[d.id].empty();
          if (ImGui::Selectable(((ov ? "* " : "  ") + d.name + "##" + d.id).c_str(), E.sel[0] == d.id)) { E.sel[0] = d.id; E.raw = false; }
          if (E.sel[0].empty()) E.sel[0] = d.id; }
      } else {
        for (auto& [vid, dp] : app.tanks.defs) { const TankDef& d = *dp; if (vehGroup(d) != g || !(match(d.name) || match(vid))) continue;
          bool ov = vehicleOverrides().contains(vid) && !vehicleOverrides()[vid].empty();
          if (ImGui::Selectable(((ov ? "* " : "  ") + d.name + "##" + vid).c_str(), E.sel[1] == vid)) { E.sel[1] = vid; E.raw = false; }
          if (E.sel[1].empty()) E.sel[1] = vid; }
      }
      ImGui::Spacing();
    }
    ImGui::PopStyleVar(); ImGui::SetWindowFontScale(1.0f);
    ImGui::EndChild();
    ImGui::EndChild();
    // ---- right: the item
    ImGui::SameLine();
    ImGui::BeginChild("##item", ImVec2(0, H), 0, ImGuiWindowFlags_NoBackground);
    const std::string id = E.sel[kind];
    json base, cur; std::vector<ModelDraw> md; std::vector<DrawItem> boxes; vec3 bmin(-0.5f), bmax(0.5f); std::string title;
    if (kind == 0) {
      int wi = app.weaponIndex(id);
      if (wi >= 0) {
        const WeaponDef& d = *app.weaponDefs[wi]; title = d.name + "   " + d.caliber;
        base = weaponBase(d);
        if (d.useModel() && d.worldModel()) { bmin = vec3(1e9f); bmax = vec3(-1e9f); for (int p = 0; p < 8; ++p) for (int k : d.worldParts()[p]) { md.push_back({d.worldModel(), k, mat4(1)}); const ModelPart& mp = d.worldModel()->parts[k]; bmin = glm::min(bmin, mp.bmin); bmax = glm::max(bmax, mp.bmax); } if (bmin.x > bmax.x) { bmin = vec3(-0.5f); bmax = vec3(0.5f); } }   // frame what is drawn
        else { for (int p = 0; p < 8; ++p) if (d.mesh[p].count) boxes.push_back({&d.mesh[p], mat4(1)}); bmin = vec3(-0.05f, -0.1f, 0); bmax = vec3(0.05f, 0.1f, 1.0f); }
      }
    } else if (kind == 2) {   // UI3 (h): a soldier patch on combat.json, or an enemy type's entry
      std::string model = "allied_ranger_0";
      if (id == "_soldier") { title = "Soldiers"; base = readJson(contentDir() / "ai" / "combat.json"); }
      else for (const json& e : aiTypeEntries()) if (e.value("id", std::string("zombie")) == id) {
        base = e; title = e.value("name", id);
        model = e.contains("models") && e["models"].is_array() && !e["models"].empty() && e["models"][0].is_string() ? e["models"][0].get<std::string>() : e.value("model", std::string("zombie"));
      }
      if (AiBody* b = aiBody(model)) { for (size_t k = 0; k < b->ci.view.parts.size(); ++k) md.push_back({&b->ci.view, (int)k, mat4(1)}); bmin = b->m->bmin; bmax = b->m->bmax; if (!(bmax.y > bmin.y)) { bmin = vec3(-0.5f, 0, -0.5f); bmax = vec3(0.5f, 1.9f, 0.5f); } }   // framed by its own bounds: big ones fit too
    } else {
      auto it = app.tanks.defs.find(id);
      if (it != app.tanks.defs.end()) {
        const TankDef& d = *it->second; title = d.name;
        base = vehicleBase(id);
        if (d.heli) base["_helicopter"] = readJson(contentDir() / "settings" / "helicopter.json");
        if (d.model) { for (size_t k = 0; k < d.model->parts.size(); ++k) md.push_back({d.model.get(), (int)k, mat4(1)}); bmin = d.model->bmin; bmax = d.model->bmax; }
      }
    }
    if (base.is_object() && !base.empty()) {
      json& ov = itemOverride(kind, id);
      cur = base; cur.merge_patch(ov);
      if (kind == 1 && vehicleOverrides().contains("_helicopter") && cur.contains("_helicopter")) cur["_helicopter"].merge_patch(vehicleOverrides()["_helicopter"]);
      ImGui::SetWindowFontScale(1.25f); ImGui::TextUnformatted(title.c_str()); ImGui::SetWindowFontScale(1.0f);
      ImGui::SameLine(ImGui::GetContentRegionAvail().x - 250);
      if (ImGui::Button(E.raw ? "Fields" : "JSON")) { E.raw = !E.raw; E.rawFor.clear(); }
      ImGui::SameLine();
      if (ImGui::Button("Reset to default")) { overrideTable(kind, id).erase(id); saveOverrides(); E.dirty = true; E.status = title + ": shipped values"; }
      ImVec2 p0 = ImGui::GetCursorScreenPos(); float pw = ImGui::GetContentRegionAvail().x, ph = std::min(220.0f, H * 0.28f);
      preview(app, p0, ImVec2(p0.x + pw, p0.y + ph), md, boxes, bmin, bmax, std::to_string(kind) + id);
      ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + ph + 6));
      if (!E.status.empty()) ImGui::TextColored(ImVec4(0.75f, 0.85f, 0.7f, 1), "%s", E.status.c_str());
      ImGui::TextDisabled(kind == 0 ? "LAN: the host's values rule damage; each player's own file sets fire rate, recoil and handling."
                          : kind == 1 ? "Vehicles are not replicated over LAN: each player's own values apply in their game."
                                      : "The AI runs on the host: in a LAN match the host's values apply. Soldier changes take effect at the next match start.");
      ImGui::BeginChild("##fields", ImVec2(0, 0), 0, ImGuiWindowFlags_NoBackground);
      ImGui::SetWindowFontScale(1.08f); ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));   // UI5
      if (E.raw) {   // the item's override patch as JSON: anything without a control
        if (E.rawFor != id) { E.rawFor = id; json o = ov; if (kind == 1 && vehicleOverrides().contains("_helicopter") && app.tanks.defs.at(id)->heli) o["_helicopter"] = vehicleOverrides()["_helicopter"]; E.rawText = o.dump(2); E.rawErr.clear(); }
        static char buf[65536]; std::snprintf(buf, sizeof buf, "%s", E.rawText.c_str());
        if (ImGui::InputTextMultiline("##raw", buf, sizeof buf, ImVec2(-1, ImGui::GetContentRegionAvail().y - 60))) E.rawText = buf;
        if (ImGui::Button("Apply JSON")) {
          try {
            json j = json::parse(E.rawText);
            if (!j.is_object()) throw std::runtime_error("the override must be a JSON object { ... }");
            json test = base; test.merge_patch(j);   // must merge cleanly
            if (j.contains("_helicopter")) { vehicleOverrides()["_helicopter"] = j["_helicopter"]; j.erase("_helicopter"); }
            overrideTable(kind, id)[id] = j;
            std::string e; if (!saveOverrides(&e)) throw std::runtime_error(e);
            E.rawErr.clear(); E.status = "JSON applied and saved."; E.dirty = true;
          } catch (std::exception& e) { E.rawErr = std::string("Not applied: ") + e.what(); }
        }
        ImGui::SameLine(); ImGui::TextDisabled("Only the changes from the shipped file (a JSON merge patch). null removes a key's override.");
        if (!E.rawErr.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.35f, 1), "%s", E.rawErr.c_str());
      } else {
        if (kind == 1 && cur.contains("_helicopter")) {   // the shared flight tuning lives in its own override key
          json h = cur["_helicopter"]; cur.erase("_helicopter"); json hb = base["_helicopter"];
          fields(kind, id, base, cur, json::json_pointer(), 0);
          if (ImGui::CollapsingHeader("Flight (all helicopters)")) {
            json& hov = vehicleOverrides()["_helicopter"]; if (!hov.is_object()) hov = json::object();
            json tmp = vehicleOverrides(); (void)tmp;
            ImGui::Indent(8);
            for (auto& [k, v] : h.items()) if (v.is_object()) { if (ImGui::TreeNode(sectionName(k, v).c_str())) {
              for (auto& [k2, v2] : v.items()) if (!hidden(k2) && !v2.is_object()) {   // writes vehicleOverrides()["_helicopter"]/<k>/<k2>
                json::json_pointer p = json::json_pointer("/_helicopter") / k / k2;
                json& vo = vehicleOverrides(); bool over = hasPath(vo, p);
                std::string unit; std::string label = pretty(k2, unit);
                ImGui::PushID(p.to_string().c_str()); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(label.c_str()); ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.46f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
                if (v2.is_number()) { float f = v2.get<float>(); double lo, hi; range(hb[k][k2].is_number() ? hb[k][k2].get<double>() : f, false, lo, hi);
                  if (ImGui::DragFloat("##v", &f, (float)std::max(1e-4, (hi - lo) / 1000.0), (float)lo, (float)hi, ("%.4g " + unit).c_str(), ImGuiSliderFlags_AlwaysClamp)) { vo[p] = f; E.dirty = true; }
                  if (ImGui::IsItemDeactivatedAfterEdit()) saveOverrides(); }
                else ImGui::TextDisabled("%s", v2.dump().substr(0, 40).c_str());
                if (over) { ImGui::SameLine(); if (ImGui::SmallButton("reset")) { erasePath(vo, p); saveOverrides(); E.dirty = true; } }
                ImGui::PopID();
              }
              ImGui::TreePop(); } }
            ImGui::Unindent(8);
          }
        } else fields(kind, id, base, cur, json::json_pointer(), 0);
      }
      ImGui::PopStyleVar(); ImGui::SetWindowFontScale(1.0f);
      ImGui::EndChild();
    } else ImGui::TextDisabled("Pick an item on the left.");
    ImGui::EndChild();
  }
  ImGui::End();
  ImGui::PopStyleColor(3); ImGui::PopStyleVar();
}
