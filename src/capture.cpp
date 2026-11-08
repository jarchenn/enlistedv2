// --capture-materials <tag>: identical fixed cameras for before/after material comparisons.
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <deque>

namespace {
struct Cam { const char* name; vec3 pos, target; };
std::vector<Cam> cams;
size_t ci = 0; int frames = 0;
std::string tag = "now";
uint64_t FH = 0, CM = 0, WH = 0, PK = 0;
// interiors set: houses are furnished by the room templates (seeded by the instance id, so every run matches)
void captureHook(App& app, uint64_t id) {
  Instance* i = app.doc.find(id); if (!i) return;
  if (i->assetId.rfind("house_", 0) != 0) return;
  app.syncRuntime();
  app.interior.furnish(app, id, 0);
}
}

void SelfTest::captureUpdate(App& app, float) {
  if (stage == 800) {
    reportName = "capture";
    for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-materials" && k + 1 < __argc) tag = __argv[k + 1];
    const TemplateInfo* t = app.findTemplate("blank_slate");
    app.newMap(*t, "Material captures");
    if (const char* set = std::getenv("NMK_CAPTURE_SET"); set && std::string(set) == "bunkers") {
      // NMK_CAPTURE_SET=bunkers: every bunker from fixed asset-space cameras (docs/bunkers.md); names are
      // <bunker>_<view>. The large casemate's "reference" view matches the Atlantic Wall reference photo's angle.
      struct V { const char* name; vec3 pos, tgt; };
      struct B { const char* id; std::vector<V> views; };
      const B bs[] = {
        {"bunker_mg_casemate", {{"front_corner", vec3(10.5f, 2.4f, 9.5f), vec3(1.5f, 1.4f, 1.0f)}, {"front", vec3(8.5f, 4.2f, 11.5f), vec3(0, 1.0f, 0)},
                                {"rear", vec3(-7.5f, 3.8f, -11.0f), vec3(0, 1.0f, -1.5f)}, {"view_out", vec3(1.9f, 1.92f, 1.15f), vec3(1.9f, 1.92f, 16.0f)},
                                {"view_out_corner", vec3(1.9f, 1.92f, 1.15f), vec3(12.0f, 1.92f, 4.0f)}, {"interior", vec3(2.3f, 2.1f, -1.2f), vec3(-1.0f, 0.9f, 1.6f)},
                                {"mg42_mount", vec3(0.9f, 2.0f, -0.2f), vec3(-0.1f, 1.7f, 1.6f)}}},
        {"bunker_observation_post", {{"front", vec3(6.8f, 3.4f, 9.2f), vec3(0, 1.0f, 0)}, {"interior", vec3(1.15f, 1.85f, -1.15f), vec3(-0.6f, 0.9f, 1.2f)}}},
        {"bunker_field_shelter", {{"front", vec3(6.8f, 3.4f, 9.2f), vec3(0, 1.0f, 0)}, {"interior", vec3(-1.9f, 1.8f, -1.35f), vec3(1.6f, 0.8f, 1.0f)}}},
        {"bunker_command_shelter", {{"front", vec3(11.0f, 4.8f, 14.0f), vec3(0, 1.0f, 0)}, {"rear", vec3(-9.5f, 4.4f, -13.5f), vec3(0, 1.0f, -2.0f)},
                                    {"view_out", vec3(2.2f, 1.92f, 1.85f), vec3(2.2f, 1.92f, 16.0f)}, {"interior", vec3(3.3f, 1.95f, -2.3f), vec3(-0.6f, 0.8f, 2.0f)}}},
        {"bunker_large_casemate", {{"reference", vec3(-6.5f, 1.6f, 21.0f), vec3(3.0f, 2.9f, -1.0f)}, {"front", vec3(9.0f, 5.0f, 20.0f), vec3(-1.0f, 2.0f, 0.0f)},
                                   {"rear", vec3(14.0f, 5.5f, -17.0f), vec3(1.0f, 2.0f, -3.0f)}, {"view_out", vec3(-3.2f, 2.12f, 2.35f), vec3(-3.2f, 2.12f, 20.0f)},
                                   {"interior", vec3(0.4f, 2.0f, 0.2f), vec3(-5.0f, 1.0f, 2.6f)}}},
        {"bunker_pak_casemate", {{"front", vec3(9.0f, 3.2f, 14.0f), vec3(0, 1.2f, 1.0f)}, {"rear", vec3(-7.0f, 3.4f, -12.0f), vec3(0, 1.2f, -2.0f)},
                                 {"gun_room", vec3(2.4f, 2.1f, -2.6f), vec3(-0.2f, 0.9f, 1.6f)}, {"view_out", vec3(2.3f, 1.64f, 1.4f), vec3(2.3f, 1.64f, 16.0f)}}},
      };
      cams.clear();
      float x = 0;
      static std::deque<std::string> keep;   // stable storage for the camera names
      for (auto& b : bs) {
        const AssetDef* a = app.lib.find(b.id);
        if (!a) continue;
        uint64_t id = app.placeInstance(a, vec3(x, 0, 0), 0.0f);
        app.placeCompanions(id);
        vec3 o = app.doc.find(id)->pos;
        std::string n = std::string(b.id).substr(7);   // drop "bunker_"
        for (auto& v : b.views) { keep.push_back(n + "_" + v.name); cams.push_back({keep.back().c_str(), o + v.pos, o + v.tgt}); }
        x += 50;
      }
      app.syncRuntime();
      app.hideEditorOverlays = true;
      app.grassBudgetMs = -1;
      ci = 0; frames = 0; stage = 801;
      return;
    }
    if (const char* set = std::getenv("NMK_CAPTURE_SET"); set && std::string(set) == "b3") {
      // NMK_CAPTURE_SET=b3: the B3 chateau and houses (tools/catalog_chateau.mjs), furnished: a front and a rear
      // three-quarter view, then one view from every "look" step of the tour (from the last goto, at the last
      // expected floor height). Names are <asset>_<view>.
      static std::deque<std::string> keep;
      static const char* ids[] = {"vil_chateau_grand", "vil_longhouse_thatched", "vil_townhouse_jettied", "vil_villa_deauville", "vil_manor_turret", "vil_corner_shop",
                                  "vil_gatehouse_chapel", "vil_mill_house", "vil_terrace_pair", "vil_cottage_barn", "vil_rectory"};
      cams.clear();
      int n = 0;
      for (const char* aid : ids) {
        const AssetDef* a = app.lib.find(aid);
        if (!a) continue;
        vec3 at3(-135.0f + (n % 4) * 90.0f, 0, -90.0f + (n / 4) * 90.0f);   // a 4 x 3 grid inside the Blank Slate
        ++n;
        uint64_t id = app.placeInstance(a, at3, 0.0f);
        if (!id) continue;
        app.placeCompanions(id);
        app.syncRuntime();
        app.interior.furnish(app, id, 0);
        vec3 o = app.doc.find(id)->pos;
        float fx = a->footprint.x * 0.5f, fz = a->footprint.y * 0.5f, H = a->bmax.y - a->bmin.y;
        keep.push_back(a->id + "_ext_front"); cams.push_back({keep.back().c_str(), o + vec3(fx * 0.9f + 4, 1.7f + H * 0.25f, fz * 1.6f + 6), o + vec3(0, H * 0.35f, 0)});
        keep.push_back(a->id + "_ext_rear"); cams.push_back({keep.back().c_str(), o + vec3(-fx * 0.9f - 4, 1.7f + H * 0.3f, -fz * 1.6f - 6), o + vec3(0, H * 0.35f, 0)});
        vec2 at(0); float y = 0; int k = 0;
        for (auto& st : a->raw["tour"].value("steps", json::array())) {
          if (st.contains("goto")) { at = vec2(st["goto"][0].get<float>(), st["goto"][1].get<float>()); if (st.contains("expectY")) y = st["expectY"].get<float>(); }
          if (st.contains("look")) { keep.push_back(a->id + "_int" + std::to_string(++k)); cams.push_back({keep.back().c_str(), o + vec3(at.x, y + 1.6f, at.y), o + jvec3(st["look"])}); }
        }
      }
      app.syncRuntime();
      app.hideEditorOverlays = true;
      app.grassBudgetMs = -1;
      ci = 0; frames = 0; stage = 801;
      return;
    }
    if (const char* set = std::getenv("NMK_CAPTURE_SET"); set && std::string(set) == "interiors") {
      // NMK_CAPTURE_SET=interiors: walls, interiors and furniture (docs/interiors.md). The representative house is the
      // farmhouse (exterior wall, windows, doorway, two stairs, a dirt road and pasture in front); the inn shows the
      // rendered facade of the reference screenshot, the schoolhouse the brick, the cottage a second interior.
      struct V { const char* name; vec3 pos, tgt; };
      struct B { const char* id; vec3 at; std::vector<V> views; };
      const B bs[] = {
        {"house_farmhouse_2story", vec3(0, 0, 0), {{"ext_close", vec3(-3.0f, 1.75f, 7.6f), vec3(-3.0f, 1.7f, 4.5f)}, {"ext_street", vec3(10.5f, 1.7f, 15.0f), vec3(0.0f, 2.6f, 3.0f)},
                                                     {"int_kitchen", vec3(-0.9f, 1.65f, 3.3f), vec3(-4.6f, 0.75f, -2.6f)}, {"furniture_close", vec3(-1.9f, 1.45f, 2.0f), vec3(-3.3f, 0.75f, 0.0f)},
                                                     {"int_parlour", vec3(1.6f, 1.65f, 3.3f), vec3(5.0f, 0.8f, -1.0f)}, {"int_bedroom", vec3(-0.9f, 4.95f, 3.3f), vec3(-4.6f, 3.9f, -2.6f)}}},
        {"large_village_inn", vec3(60, 0, 0), {{"render_wall", vec3(0.0f, 1.75f, 11.5f), vec3(0.0f, 2.2f, 0.0f)}}},
        {"large_village_schoolhouse", vec3(-60, 0, 0), {{"brick_wall", vec3(4.0f, 1.75f, 13.0f), vec3(0.0f, 2.5f, 0.0f)}}},
        {"house_stone_cottage", vec3(0, 0, -40), {{"cottage_ext", vec3(6.5f, 1.7f, 8.5f), vec3(0.0f, 1.6f, 0.0f)}, {"cottage_int", vec3(1.4f, 1.6f, 1.9f), vec3(-2.6f, 0.7f, -1.6f)}}},
      };
      cams.clear();
      static std::deque<std::string> keep;
      for (auto& b : bs) {
        const AssetDef* a = app.lib.find(b.id);
        if (!a) continue;
        uint64_t id = app.placeInstance(a, b.at, 0.0f);
        if (!id) continue;
        if (std::string(b.id) == "house_farmhouse_2story") {   // the road in front of the representative house
          if (const AssetDef* road = app.lib.find("path_dirt")) app.placeInstance(road, b.at + vec3(0, 0, 11.0f), 90.0f);
        }
        captureHook(app, id);   // interiors: furnish (no-op before the room templates existed)
        vec3 o = app.doc.find(id)->pos;
        for (auto& v : b.views) { keep.push_back(v.name); cams.push_back({keep.back().c_str(), o + v.pos, o + v.tgt}); }
      }
      app.syncRuntime();
      app.hideEditorOverlays = true;
      app.grassBudgetMs = -1;
      ci = 0; frames = 0; stage = 801;
      return;
    }
    FH =app.placeInstance(app.lib.find("house_farmhouse_2story"), vec3(0, 0, 0), 0.0f);
    CM = app.placeInstance(app.lib.find("bunker_mg_casemate"), vec3(0, 0, 40), 0.0f);
    WH = app.placeInstance(app.lib.find("large_stone_warehouse"), vec3(45, 0, 0), 0.0f);
    PK = app.placeInstance(app.lib.find("gun_pak40"), vec3(8, 0, 58), 200.0f);
    app.syncRuntime();
    app.hideEditorOverlays = true;
    app.grassBudgetMs = -1;   // grass fully built before each shot
    auto W = [&](uint64_t id, vec3 l) { Instance* i = app.doc.find(id); return i->pos + yawQuat(i->yaw) * l; };
    cams = {
      {"farmhouse_table", W(FH, vec3(-1.9f, 1.75f, 2.4f)), W(FH, vec3(-3.4f, 0.85f, 0.3f))},
      {"steel_shutter_indoor", W(CM, vec3(0.2f, 1.55f, 0.4f)), W(CM, vec3(0.0f, 1.45f, 1.97f))},
      {"steel_outdoor_pak_shield", W(PK, vec3(2.2f, 1.6f, 3.2f)), W(PK, vec3(0.0f, 0.9f, 0.4f))},
      {"long_masonry_wall", W(WH, vec3(-11.0f, 1.7f, 9.5f)), W(WH, vec3(4.0f, 2.5f, 4.5f))},
      {"pasture_to_bunker", W(CM, vec3(3.0f, 1.7f, 30.0f)), W(CM, vec3(0.0f, 1.2f, 3.0f))},
    };
    ci = 0; frames = 0; stage = 801;
  }
  if (ci >= cams.size()) { finish(app); return; }
  Cam& c = cams[ci];
  app.camPos = c.pos;
  vec3 d = glm::normalize(c.target - c.pos);
  app.camYaw = degrees(std::atan2(-d.x, -d.z));
  app.camPitch = degrees(std::asin(std::clamp(d.y, -1.0f, 1.0f)));
  if (++frames == 4) shotQueue.push_back("mat_" + tag + "_" + c.name);
  if (frames >= 5) { ++ci; frames = 0; }
}
