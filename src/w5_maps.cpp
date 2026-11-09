// W5 default maps runtime: authored template terrain, template map content, previews, the Play map picker and the
// decorator distance cull (see w5.h; the content is built by tools/mapgen, documented in tools/mapgen/README.md).
#include "settings.h"
#include "w5.h"
#include "app.h"
#include "editor_nav.h"
#include "mpui.h"
#include "ai_hooks.h"
#include <imgui.h>
#include <stb_image.h>
#include <algorithm>
#include <map>

namespace {
struct HeightImage { int w = 0, h = 0; std::vector<uint16_t> px; bool ok = false; };

const HeightImage& heightImage(const std::string& file) {
  static std::map<std::string, HeightImage> cache;
  auto it = cache.find(file);
  if (it != cache.end()) return it->second;
  HeightImage& im = cache[file];
  fs::path p = contentDir() / "templates" / file;
  int w = 0, h = 0, n = 0;
  stbi_us* d = stbi_load_16(p.string().c_str(), &w, &h, &n, 1);
  if (!d) { logf("w5: heightmap %s: %s", p.string().c_str(), stbi_failure_reason()); return im; }
  im.w = w; im.h = h; im.px.assign(d, d + (size_t)w * h); im.ok = true;
  stbi_image_free(d);
  return im;
}
}  // namespace

json w5MapSettings() {
  static json s = [] {
    json j = json::object();
    try { j = json::parse(readTextFile(contentDir() / "settings" / "maps.json")); } catch (...) {}
    return j;
  }();
  return s;
}

// ---------------------------------------------------------------- Terrain::generate
void w5TerrainBase(Terrain& t, const TemplateInfo& ti) {
  const json& r = ti.raw;
  if (r.contains("waterLevel") && r["waterLevel"].is_number()) t.waterLevel = r["waterLevel"].get<float>();
  if (!r.contains("heightmap") || !r["heightmap"].is_object()) return;
  const json& hm = r["heightmap"];
  const HeightImage& im = heightImage(hm.value("file", ""));
  if (!im.ok) return;
  float lo = hm.value("min", 0.0f), hi = hm.value("max", 1.0f), cell = hm.value("cell", Terrain::kCell);
  vec2 org = t.org;
  if (hm.contains("org") && hm["org"].is_array() && hm["org"].size() >= 2) org = vec2(hm["org"][0].get<float>(), hm["org"][1].get<float>());
  // the image covers its own grid (org, cell); every terrain vertex samples it bilinearly, so a map opened at the
  // template's size reads it 1:1 and other sizes still get the authored ground where they overlap it
  auto at = [&](int i, int j) { i = std::clamp(i, 0, im.w - 1); j = std::clamp(j, 0, im.h - 1); return lo + (hi - lo) * (im.px[(size_t)j * im.w + i] / 65535.0f); };
  for (int z = 0; z < t.nz; ++z)
    for (int x = 0; x < t.nx; ++x) {
      float fx = (t.org.x + x * Terrain::kCell - org.x) / cell, fz = (t.org.y + z * Terrain::kCell - org.y) / cell;
      if (fx < -1 || fz < -1 || fx > im.w || fz > im.h) continue;
      int xi = (int)std::floor(fx), zi = (int)std::floor(fz); float u = fx - xi, v = fz - zi;
      float a = at(xi, zi), b = at(xi + 1, zi), c = at(xi, zi + 1), d = at(xi + 1, zi + 1);
      t.h[(size_t)z * t.nx + x] = glm::mix(glm::mix(a, b, u), glm::mix(c, d, u), v);
    }
}

// ---------------------------------------------------------------- App::newMap
void w5NewMapContent(App& app, const TemplateInfo& t) {
  std::string mf = t.raw.value("map", "");
  if (mf.empty()) return;
  MapDoc d; std::string err;
  if (!loadMap(contentDir() / "templates" / mf, d, err)) { app.toast("Template content could not be loaded: " + err, 8); return; }
  d.name = app.doc.name; d.templateId = t.id; d.templateVersion = t.version; d.seed = t.seed;
  d.width = app.doc.width; d.depth = app.doc.depth;
  for (auto& i : d.instances) {
    i.rev = ++app.revCounter;
    if (const AssetDef* a = app.lib.find(i.assetId)) i.assetVersion = std::max(i.assetVersion, a->version);
  }
  app.doc = std::move(d);
  app.navSidecar = navSidecarOf(contentDir() / "templates" / mf);   // PF1: the template's pre-baked navmesh
  app.resetGround();
  app.syncRuntime();
  if (app.doc.camera.set) { app.camPos = app.doc.camera.pos; app.camYaw = app.doc.camera.yaw; app.camPitch = app.doc.camera.pitch; }
  app.dirty = false;
  gUi.rulesInit = false;   // the Play setup reads the new map's rule defaults
  logf("w5: %s: %zu instances from %s", t.id.c_str(), app.doc.instances.size(), mf.c_str());
}

// ---------------------------------------------------------------- App::makeThumbnails
bool w5TemplateThumbnail(TemplateInfo& t) {
  std::string f = t.raw.value("thumbnail", "");
  if (f.empty()) return false;
  int w = 0, h = 0, n = 0;
  stbi_uc* d = stbi_load((contentDir() / "templates" / f).string().c_str(), &w, &h, &n, 4);
  if (!d) return false;
  // The New Map dialog samples template tiles with v 0.8 -> 0.2 (the middle 60 % of a square generated thumbnail),
  // so pad the stored 16:10 image into a texture 1 / 0.6 as tall with the picture in that band (edge rows repeated
  // into the padding so filtering at the band edge shows no seam). Stored top row first; upload bottom row first.
  const int H = (int)std::lround(h / 0.6), off = (H - h) / 2;
  std::vector<uint8_t> flip((size_t)w * H * 4);
  for (int y = 0; y < H; ++y) {
    int src = std::clamp(H - 1 - y - off, 0, h - 1);
    std::memcpy(&flip[(size_t)y * w * 4], d + (size_t)src * w * 4, (size_t)w * 4);
  }
  h = H;
  stbi_image_free(d);
  GLuint tex = 0;
  glCreateTextures(GL_TEXTURE_2D, 1, &tex);
  glTextureStorage2D(tex, 1, GL_RGBA8, w, h);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTextureSubImage2D(tex, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, flip.data());
  glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  t.thumbnail = tex;
  return true;
}

// ---------------------------------------------------------------- MpUi::setupWindow
void w5PlayMapPicker(App& app) {
  static int pending = -1;
  std::vector<int> maps;
  for (int k = 0; k < (int)app.templates.size(); ++k) {
    const TemplateInfo& t = app.templates[k];
    if (t.available && t.raw.value("playable", false) && t.raw.contains("map")) maps.push_back(k);
  }
  if (maps.empty()) return;
  ImGui::SetNextItemWidth(330);
  if (ImGui::BeginCombo("Default maps", "Load a ready-to-play map...")) {
    for (int k : maps) {
      const TemplateInfo& t = app.templates[k];
      if (ImGui::Selectable(t.name.c_str(), app.doc.templateId == t.id)) pending = k;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t.description.c_str());
    }
    ImGui::EndCombo();
  }
  if (pending >= 0 && app.dirty && !ImGui::IsPopupOpen("Discard unsaved changes?")) ImGui::OpenPopup("Discard unsaved changes?");
  bool go = pending >= 0 && !app.dirty;
  if (ImGui::BeginPopupModal("Discard unsaved changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("The open map has unsaved changes. Load the default map anyway?");
    if (ImGui::Button("Load default map")) { go = true; ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) { pending = -1; ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
  }
  if (go && pending >= 0 && pending < (int)app.templates.size()) {
    const TemplateInfo& t = app.templates[pending];
    pending = -1;
    navNewMapSized(app, t, t.name, vec2(t.width, t.depth));
  }
}

// ---------------------------------------------------------------- App::buildDrawLists
bool w5CullDraw(const App& app, const Instance& i, const AssetDef* a) {
  struct Rule { float perMetre = 0, minD = 0, maxD = 0; };
  static std::map<std::string, Rule> rules = [] {
    std::map<std::string, Rule> m;
    json c = w5MapSettings().value("decorCull", json::object());
    if (!c.value("enabled", true)) return m;
    json cats = c.value("categories", json::object());   // keep the object alive while iterating
    for (auto it = cats.begin(); it != cats.end(); ++it)
      m[it.key()] = Rule{it->value("perMetre", 60.0f), it->value("min", 60.0f), it->value("max", 1200.0f)};
    json ids = c.value("assets", json::object());          // per-asset overrides (keyed "#<asset id>")
    for (auto it = ids.begin(); it != ids.end(); ++it)
      m["#" + it.key()] = Rule{it->value("perMetre", 60.0f), it->value("min", 60.0f), it->value("max", 1200.0f)};
    logf("w5: decor cull rules for %zu categories / assets", m.size());
    return m;
  }();
  if (!a || rules.empty()) return false;
  static std::unordered_map<const AssetDef*, const Rule*> ruleOf;   // per instance per frame: no string keys (assets never move)
  auto ro = ruleOf.find(a);
  if (ro == ruleOf.end()) {
    auto it = rules.find("#" + a->id);
    if (it == rules.end()) it = rules.find(a->category);
    ro = ruleOf.emplace(a, it == rules.end() ? nullptr : &it->second).first;
  }
  if (!ro->second) return false;
  const Rule& rule = *ro->second;
  vec3 eye = app.mode == Mode::Explore ? app.eyePos() : app.camPos;
  float d2 = glm::dot(i.pos - eye, i.pos - eye);
  vec3 ext = a->bmax - a->bmin;
  float size = std::max({ext.x, ext.y, ext.z, 0.3f});
  float cut = std::clamp(rule.perMetre * size, rule.minD, rule.maxD) * settings().drawDist();   // Settings draw distance (rules stay cached)
  return d2 > cut * cut;
}
