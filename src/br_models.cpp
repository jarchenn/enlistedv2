// BR3 Battle Royale models (br_models.h): the marine body, worn gear, optics on the guns, the inventory preview.
#include "br_models.h"
#include <stb_image.h>
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
  float vestOut = 0;   // BR8: a backpack goes on top of the vest: out by how far the vest's back stands off the body
  if (s.vestLvl > 0) if (const AssetDef* v = app.lib.find(wornId(1, s.vestLvl))) if (v->raw.contains("worn")) vestOut = v->raw["worn"].value("backOut", 0.0f);
  for (int slot = 0; slot < 3; ++slot) {
    if (lv[slot] <= 0) continue;
    const AssetDef* a = app.lib.find(wornId(slot, lv[slot]));
    if (!a || !a->raw.contains("worn")) continue;
    const json& w = a->raw["worn"];
    int cj = w.value("joint", "Spine2") == "Head" ? CJ_HEAD : CJ_SPINE2, j = m.semantic[cj];
    if (j < 0 || j >= (int)s.pose.delta.size()) continue;
    vec3 pos = jvec3(w["pos"], vec3(0)), sc = jvec3(w["scale"], vec3(1)), rest = w.contains("rest") ? jvec3(w["rest"]) : m.jointHead[j];
    pos += m.jointHead[j] - rest;   // fitted to the marine; another body carries it from its own joint
    if (slot == 2) pos.z -= vestOut;   // the back is -z (the body faces +z)
    // AN1 step 4: a hand-tuned correction on top of the baked fit ("wornFix": {"pos": [m], "rot": [deg x, y, z]}, about the item's base),
    // and a vest rides between the two upper spine joints so a bent torso does not drive its lower edge into the belly
    mat4 J = s.pose.delta[j];
    if (slot == 1 && m.semantic[CJ_SPINE1] >= 0 && m.semantic[CJ_SPINE1] != j) {
      const mat4& J1 = s.pose.delta[m.semantic[CJ_SPINE1]];
      quat q = glm::slerp(glm::normalize(glm::quat_cast(glm::mat3(J1))), glm::normalize(glm::quat_cast(glm::mat3(J))), 0.5f);
      vec3 piv = pos;   // blend the two deltas as they act on the vest's base point
      vec3 p = glm::mix(vec3(J1 * vec4(piv, 1)), vec3(J * vec4(piv, 1)), 0.5f);
      J = glm::translate(mat4(1), p) * glm::mat4_cast(q) * glm::translate(mat4(1), -piv);
    }
    mat4 fixR(1);
    if (a->raw.contains("wornFix")) { const json& f = a->raw["wornFix"]; pos += jvec3(f.value("pos", json()), vec3(0)); vec3 r = jvec3(f.value("rot", json()), vec3(0)); fixR = glm::mat4_cast(glm::quat(glm::radians(r))); sc *= jvec3(f.value("scale", json()), vec3(1)); }
    addAsset(a, world * J * glm::translate(mat4(1), pos) * fixR * glm::scale(mat4(1), sc), md, boxes);
  }
}

bool brHidePart(const Soldier& s, const std::string& partName) { return s.helmetLvl > 0 && partName.find("Hats") != std::string::npos; }

// ---------------------------------------------------------------- optics
static const char* sightId(int k) { static const char* ids[6] = {"", "br_sight_reddot", "br_sight_acog", "br_sight_4x", "br_sight_10x", "br_sight_holo"}; return ids[std::clamp(k, 0, 5)]; }   // AT1: 5 the holographic sight
// AT1: an optic's sight block: the local bake's own ("modelSight") when its model is loaded, else the committed stand-in's
static const json& sightOf(const AssetDef& a) { return a.model && a.raw.contains("modelSight") ? a.raw["modelSight"] : a.raw["sight"]; }

struct Rail { vec3 p{0}; float scale = 1; };
static std::map<const WeaponDef*, Rail> gRailCache;   // keyed by definition: cleared when the weapons reload (ED2)
void brRailCacheClear() { gRailCache.clear(); }
static const Rail& railOf(const WeaponDef& d) {   // the gun JSON "rail" / "railScale" (read once); none: just under the rear sight
  auto& cache = gRailCache;
  auto it = cache.find(&d);
  if (it != cache.end()) return it->second;
  Rail r; r.p = d.rear + vec3(0, -0.012f, 0.05f);
  try { json j = json::parse(readTextFile(d.file)); if (j.contains("rail")) r.p = jvec3(j["rail"], r.p); r.scale = j.value("railScale", 1.0f); } catch (...) {}
  return cache[&d] = r;
}

// weapon-space mount of optic k (asset space -> weapon space)
static bool sightMount(const WeaponDef& d, int k, const AssetDef*& a, mat4& M, vec3& eyeW) {
  if (d.hasScope) return false;   // scoped rifles keep their own model: a fitted optic changes only the magnification and reticle (loot.cpp lootScope)
  a = k > 0 ? gApp->lib.find(sightId(k)) : nullptr;
  if (!a || !a->raw.contains("sight")) return false;
  const Rail& r = railOf(d);
  vec3 eye = jvec3(sightOf(*a)["eye"], vec3(0, 0.04f, 0)) * r.scale;
  vec3 base = r.p;
  M = glm::translate(mat4(1), base) * glm::scale(mat4(1), vec3(r.scale));
  eyeW = base + eye;
  return true;
}

// the red dot's mark: a small emissive disc in the window on the eye line, facing the eye (asset space)
static const GpuMesh* redDotMark(const AssetDef& a) {
  static GpuMesh mesh; static bool built = false;
  if (!built) {
    built = true;
    vec3 eye = jvec3(a.raw["sight"]["eye"], vec3(0, 0.035f, -0.04f)); float L = a.raw["sight"].value("length", 0.085f);
    vec3 col(1, 0.03f, 0.015f);
    Material m{"br_red_dot"}; m.albedo = col; m.emissive = col * 40.0f;
    Part p; p.shape = Part::CYL; p.size = vec3(0.0016f, 0.0002f, 0.0016f); p.collide = false; p.seg = 16;
    p.rot = glm::angleAxis(1.5707963f, vec3(1, 0, 0));   // the disc axis (y) along the sight line (z)
    p.center = vec3(eye.x, eye.y, eye.z + 0.6f * L);
    MeshData md; appendPart(md, p, m, mat4(1)); mesh.upload(md);
  }
  return &mesh;
}

// AT1: the holographic sight's reticle is the model's own (the user's rule: no recreated reticle): the image on its window plate
// (modelSight.reticle: the plate's centre and size, the image copied beside the local bake), its lit texels drawn as emissive quads
// on the plate, a little brighter than the red dot's mark (the renderer has no emissive textures). Without the local model the
// stand-in is the red dot's frame, and the red dot's mark is its one dot
static const GpuMesh* holoReticle(const AssetDef& a) {
  static GpuMesh mesh[2]; static bool built[2] = {false, false};
  int k = a.model ? 1 : 0;
  if (!built[k]) {
    built[k] = true;
    const json& s = sightOf(a);
    MeshData md;
    int w = 0, h = 0, n = 0; stbi_uc* px = nullptr;
    if (k && s.contains("reticle")) {
      const json& r = s["reticle"];
      fs::path img = contentDir() / "assets" / "models" / a.raw["model"].value("dir", std::string()) / r.value("image", std::string("reticle.png"));
      px = stbi_load(img.string().c_str(), &w, &h, &n, 3);
      if (px) {
        vec3 c = jvec3(r["center"], vec3(0)); vec2 sz(r["size"][0].get<float>(), r["size"][1].get<float>());
        const int N = 96; vec3 col(1, 0.04f, 0.02f);
        for (int j = 0; j < N; ++j) for (int i = 0; i < N; ++i) {
          float lit = 0;   // the cell's brightest red over the rest (the plate's black is nothing)
          for (int y = j * h / N; y < (j + 1) * h / N; ++y) for (int x = i * w / N; x < (i + 1) * w / N; ++x) {
            const stbi_uc* q = px + (y * w + x) * 3; lit = std::max(lit, (q[0] - 0.5f * (q[1] + q[2])) / 255.0f);
          }
          if (lit < 0.2f) continue;
          Material m{"br_holo_reticle"}; m.albedo = col; m.emissive = col * (85.0f * std::min(1.0f, lit * 1.4f));   // readable against a bright sky
          Part p; p.shape = Part::BOX; p.collide = false; p.size = vec3(sz.x / N * 1.05f, sz.y / N * 1.05f, 0.0002f);
          p.center = c + vec3(((i + 0.5f) / N - 0.5f) * sz.x, (0.5f - (j + 0.5f) / N) * sz.y, 0);
          appendPart(md, p, m, mat4(1));
        }
        stbi_image_free(px);
      } else logf("AT1: the holographic sight's reticle image %s is missing: no reticle", img.string().c_str());
    } else {   // the stand-in (the red dot's frame): the red dot's mark
      vec3 eye = jvec3(s["eye"], vec3(0, 0.035f, -0.04f)); float L = s.value("length", 0.085f);
      vec3 col(1, 0.03f, 0.015f); Material m{"br_red_dot"}; m.albedo = col; m.emissive = col * 40.0f;
      Part p; p.shape = Part::CYL; p.size = vec3(0.0016f, 0.0002f, 0.0016f); p.collide = false; p.seg = 16;
      p.rot = glm::angleAxis(1.5707963f, vec3(1, 0, 0)); p.center = vec3(eye.x, eye.y, eye.z + 0.6f * L);
      appendPart(md, p, m, mat4(1));
    }
    mesh[k].upload(md);
  }
  return &mesh[k];
}

void brSightDraws(App& app, const WeaponDef& d, const WeaponState& ws, const mat4& W, std::vector<ModelDraw>& md, std::vector<DrawItem>& boxes) {
  (void)app; const AssetDef* a; mat4 M; vec3 e;
  if (!sightMount(d, ws.att[0], a, M, e)) return;
  addAsset(a, W * M, md, boxes);
  if (ws.att[0] == 1) boxes.push_back({redDotMark(*a), W * M});   // the reticle: the window itself is open
  if (ws.att[0] == 5) boxes.push_back({holoReticle(*a), W * M});   // AT1: the holographic sight: the model's own reticle
}

bool brSightLine(const WeaponDef& d, const WeaponState& ws, vec3& R, vec3& F, float& relief) {
  const AssetDef* a; mat4 M; vec3 e;
  if (!sightMount(d, ws.att[0], a, M, e)) return false;
  R = e; F = e + vec3(0, 0, 0.25f);
  relief = ws.att[0] == 1 || ws.att[0] == 5 ? 0.16f : 0.09f;   // red dot / holographic: held further back; magnified optics: at the eyepiece
  return true;
}

bool brSightLineActive(const WeaponDef& d, vec3& R, vec3& F, float& relief) {
  auto it = gApp->wstate.find(d.id);
  return it != gApp->wstate.end() && brSightLine(d, it->second, R, F, relief);
}

// ---------------------------------------------------------------- inventory preview
static CharInstance gInst; static const CharModel* gInstOf = nullptr;
// RGBA, top row first; cy / halfH > 0: a close-up centred at that height showing +-halfH (the worn-gear captures)
static bool renderPreview(Soldier& L, float yaw, int w, int h, std::vector<uint8_t>& px, float cy = 0, float halfH = 0) {
  App& app = *gApp; Game& G = *gGame;
  const CharModel& m = *G.models[L.model];
  if (gInstOf != &m) { if (gInstOf) gInst.destroy(); gInst.create(m); gInstOf = &m; }
  gInst.skin(L.pose.skin);
  std::vector<ModelDraw> md; std::vector<DrawItem> boxes;
  mat4 world = glm::rotate(mat4(1), glm::radians(yaw), vec3(0, 1, 0));
  for (int k = 0; k < (int)gInst.view.parts.size(); ++k) if (!brHidePart(L, gInst.view.parts[k].name)) md.push_back({&gInst.view, k, world});
  brWornDraws(app, G, L, world, md, boxes);
  // far enough for 2.1 m of height and 0.85 m of width (a narrow frame may crop the hands of a held rifle while turning)
  float tv = std::tan(glm::radians(17.0f)), dist = std::max(1.05f / tv, 0.42f / (tv * (float)w / h)) + 0.4f;
  vec3 c(0, 0.95f, 0), eye(0, 1.1f, dist);
  if (halfH > 0) { dist = halfH / tv + 0.3f; c = vec3(0, cy, 0); eye = vec3(0, cy + 0.05f, dist); }
  mat4 view = glm::lookAt(eye, c, vec3(0, 1, 0)), proj = glm::perspective(glm::radians(34.0f), (float)w / h, 0.1f, 40.0f);
  return app.R.renderModelSheet(md, boxes, view, proj, eye, w, h, px);
}

bool brInventoryPreviewPng(const fs::path& out, float yaw, float cy, float halfH) {   // --capture-br-models / -worn
  Soldier* L = gGame ? gGame->local() : nullptr;
  if (!L || L->model < 0 || L->pose.skin.empty()) return false;
  const int w = halfH > 0 ? 420 : 360, h = halfH > 0 ? 420 : 560; std::vector<uint8_t> px;
  if (!renderPreview(*L, yaw, w, h, px, cy, halfH)) return false;
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
  int w = std::clamp((int)(mx.x - mn.x), 16, 768), h = std::clamp((int)(mx.y - mn.y), 16, 1200);
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
