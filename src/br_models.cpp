// BR3 Battle Royale models (br_models.h): the marine body, worn gear, optics on the guns, the inventory preview.
#include "br_models.h"
#include "app.h"
#include "game.h"
#include "character.h"
#include <map>
#include <cstring>
#include <stb_image_write.h>

int brMarineModel(Game& G) {
  static int idx = -2;
  if (idx >= -1) return idx;
  idx = -1;
  if (G.modelIdx.count("br_marine")) return idx = G.modelIdx["br_marine"];
  auto cm = std::make_unique<CharModel>(); std::string e;
  if (!cm->load(contentDir() / "characters" / "br_marine.glb", e)) { logf("br: br_marine.glb not loaded (%s): the soldier body stays", e.c_str()); return idx; }
  idx = G.modelIdx["br_marine"] = (int)G.models.size(); G.models.push_back(std::move(cm));
  return idx;
}

static void addAsset(const AssetDef* a, const mat4& W, std::vector<ModelDraw>& md, std::vector<DrawItem>& boxes) {
  if (a->model) { for (int k = 0; k < (int)a->model->parts.size(); ++k) md.push_back({a->model.get(), k, W}); }
  else if (a->opaque.count) boxes.push_back({&a->opaque, W});   // the committed stand-in box
}

static const char* wornId(int slot, int lvl) {
  static const char* ids[3][4] = {{"", "br_helmet_1", "br_helmet_2", "br_helmet_3"}, {"", "br_vest_1", "br_vest_2", "br_vest_3"}, {"", "br_backpack", "br_backpack_mil", "br_backpack_mil"}};
  return ids[slot][std::clamp(lvl, 0, 3)];
}

void brWornDraws(App& app, Game& G, const Soldier& s, const mat4& world, std::vector<ModelDraw>& md, std::vector<DrawItem>& boxes) {
  if (s.model < 0 || s.model >= (int)G.models.size() || s.pose.delta.empty()) return;
  const CharModel& m = *G.models[s.model];
  int lv[3] = {s.helmetLvl, s.vestLvl, s.backpack};
  for (int slot = 0; slot < 3; ++slot) {
    if (lv[slot] <= 0) continue;
    const AssetDef* a = app.lib.find(wornId(slot, lv[slot]));
    if (!a || !a->raw.contains("worn")) continue;
    const json& w = a->raw["worn"];
    int cj = w.value("joint", "Spine2") == "Head" ? CJ_HEAD : CJ_SPINE2, j = m.semantic[cj];
    if (j < 0 || j >= (int)s.pose.delta.size()) continue;
    vec3 pos = jvec3(w["pos"], vec3(0)), sc = jvec3(w["scale"], vec3(1)), rest = w.contains("rest") ? jvec3(w["rest"]) : m.jointHead[j];
    pos += m.jointHead[j] - rest;   // fitted to the marine; another body carries it from its own joint
    addAsset(a, world * s.pose.delta[j] * glm::translate(mat4(1), pos) * glm::scale(mat4(1), sc), md, boxes);
  }
}

bool brHidePart(const Soldier& s, const std::string& partName) { return s.helmetLvl > 0 && partName.find("Hats") != std::string::npos; }

// ---------------------------------------------------------------- optics
static const char* sightId(int k) { static const char* ids[5] = {"", "br_sight_reddot", "br_sight_acog", "br_sight_4x", "br_sight_10x"}; return ids[std::clamp(k, 0, 4)]; }

struct Rail { vec3 p{0}; float scale = 1; };
static const Rail& railOf(const WeaponDef& d) {   // the gun JSON "rail" / "railScale" (read once); none: just under the rear sight
  static std::map<const WeaponDef*, Rail> cache;
  auto it = cache.find(&d);
  if (it != cache.end()) return it->second;
  Rail r; r.p = d.rear + vec3(0, -0.012f, 0.05f);
  try { json j = json::parse(readTextFile(d.file)); if (j.contains("rail")) r.p = jvec3(j["rail"], r.p); r.scale = j.value("railScale", 1.0f); } catch (...) {}
  return cache[&d] = r;
}

// weapon-space mount of optic k (asset space -> weapon space); scoped rifles: the optic's eye on their own scope's eye
static bool sightMount(const WeaponDef& d, int k, const AssetDef*& a, mat4& M, vec3& eyeW) {
  a = k > 0 ? gApp->lib.find(sightId(k)) : nullptr;
  if (!a || !a->raw.contains("sight")) return false;
  const Rail& r = railOf(d);
  vec3 eye = jvec3(a->raw["sight"]["eye"], vec3(0, 0.04f, 0)) * r.scale;
  vec3 base = d.hasScope ? d.scopeEye - eye : r.p;
  M = glm::translate(mat4(1), base) * glm::scale(mat4(1), vec3(r.scale));
  eyeW = base + eye;
  return true;
}

void brSightDraws(App& app, const WeaponDef& d, const WeaponState& ws, const mat4& W, std::vector<ModelDraw>& md, std::vector<DrawItem>& boxes) {
  (void)app; const AssetDef* a; mat4 M; vec3 e;
  if (sightMount(d, ws.att[0], a, M, e)) addAsset(a, W * M, md, boxes);
}

bool brSightLine(const WeaponDef& d, const WeaponState& ws, vec3& R, vec3& F, float& relief) {
  const AssetDef* a; mat4 M; vec3 e;
  if (d.hasScope || !sightMount(d, ws.att[0], a, M, e)) return false;
  R = e; F = e + vec3(0, 0, 0.25f);
  relief = ws.att[0] == 1 ? 0.16f : 0.09f;   // red dot: held further back; magnified optics: at the eyepiece
  return true;
}

bool brSightLineActive(const WeaponDef& d, vec3& R, vec3& F, float& relief) {
  auto it = gApp->wstate.find(d.id);
  return it != gApp->wstate.end() && brSightLine(d, it->second, R, F, relief);
}

// ---------------------------------------------------------------- inventory preview
static CharInstance gInst; static const CharModel* gInstOf = nullptr;
static bool renderPreview(Soldier& L, float yaw, int w, int h, std::vector<uint8_t>& px) {   // RGBA, top row first
  App& app = *gApp; Game& G = *gGame;
  const CharModel& m = *G.models[L.model];
  if (gInstOf != &m) { if (gInstOf) gInst.destroy(); gInst.create(m); gInstOf = &m; }
  gInst.skin(L.pose.skin);
  std::vector<ModelDraw> md; std::vector<DrawItem> boxes;
  mat4 world = glm::rotate(mat4(1), glm::radians(yaw), vec3(0, 1, 0));
  for (int k = 0; k < (int)gInst.view.parts.size(); ++k) if (!brHidePart(L, gInst.view.parts[k].name)) md.push_back({&gInst.view, k, world});
  brWornDraws(app, G, L, world, md, boxes);
  vec3 c(0, 0.95f, 0), eye(0, 1.15f, 3.6f);
  mat4 view = glm::lookAt(eye, c, vec3(0, 1, 0)), proj = glm::perspective(glm::radians(34.0f), (float)w / h, 0.1f, 20.0f);
  return app.R.renderModelSheet(md, boxes, view, proj, eye, w, h, px);
}

bool brInventoryPreviewPng(const fs::path& out, float yaw) {   // --capture-br-models
  Soldier* L = gGame ? gGame->local() : nullptr;
  if (!L || L->model < 0 || L->pose.skin.empty()) return false;
  const int w = 360, h = 560; std::vector<uint8_t> px;
  if (!renderPreview(*L, yaw, w, h, px)) return false;
  std::vector<uint8_t> flip(px);   // renderModelSheet reads back top row first
  return stbi_write_png(out.string().c_str(), w, h, 4, flip.data(), w * 4) != 0;
}

void drawInventoryCharacter(ImVec2 mn, ImVec2 mx) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRect(mn, mx, IM_COL32(255, 255, 255, 40), 6);
  Soldier* L = gGame ? gGame->local() : nullptr;
  if (!L || L->model < 0 || L->model >= (int)gGame->models.size() || L->pose.skin.empty()) return;
  static GLuint tex = 0; static int tw = 0, th = 0;
  static float yaw = 20; static double nextAt = 0;   // starts facing the viewer
  // turn: drag inside the frame, else a slow spin
  ImVec2 keep = ImGui::GetCursorScreenPos();   // the caller's layout cursor stays where it was
  ImGui::SetCursorScreenPos(mn); ImGui::InvisibleButton("##brchar", ImVec2(std::max(1.0f, mx.x - mn.x), std::max(1.0f, mx.y - mn.y)));
  ImGui::SetCursorScreenPos(keep);
  if (ImGui::IsItemActive()) yaw += ImGui::GetIO().MouseDelta.x * 0.6f; else yaw += ImGui::GetIO().DeltaTime * 12.0f;
  int w = std::clamp((int)(mx.x - mn.x), 16, 512), h = std::clamp((int)(mx.y - mn.y), 16, 768);
  double now = ImGui::GetTime();
  if (now >= nextAt || !tex || w != tw || h != th) {   // renderModelSheet makes an FBO per call: ~15 refreshes a second
    nextAt = now + 0.066;
    std::vector<uint8_t> px;
    if (renderPreview(*L, yaw, w, h, px)) {
      if (!tex || w != tw || h != th) {
        if (tex) glDeleteTextures(1, &tex);
        glCreateTextures(GL_TEXTURE_2D, 1, &tex); glTextureStorage2D(tex, 1, GL_RGBA8, w, h);
        glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        tw = w; th = h;
      }
      glTextureSubImage2D(tex, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    }
  }
  if (tex) dl->AddImage((ImTextureID)(intptr_t)tex, mn, ImVec2(mn.x + tw, mn.y + th));
}
