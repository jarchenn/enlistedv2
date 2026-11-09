// --capture-materials <tag>: identical fixed cameras for before/after material comparisons.
#include "selftest.h"
#include "settings.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include "ground_paint.h"

namespace {
struct Cam { const char* name; vec3 pos, target; };
std::vector<Cam> cams;
size_t ci = 0; int frames = 0;
std::string tag = "now";
uint64_t FH = 0, CM = 0, WH = 0, PK = 0;
// grass set: per camera, mean GPU ms with grass on (Medium) then off, and the blades drawn
bool grassSet = false; double gOn = 0, gOff = 0; size_t gBlades = 0; int gN = 0; size_t gNear = 0, gShadowN = 0;
// interiors set: houses are furnished by the room templates (seeded by the instance id, so every run matches)
void captureHook(App& app, uint64_t id) {
  Instance* i = app.doc.find(id); if (!i) return;
  if (i->assetId.rfind("house_", 0) != 0) return;
  app.syncRuntime();
  app.interior.furnish(app, id, 0);
}
// The GX test scene (brief §0): the B3 chateau (timber-frame, hallways, furnished rooms), a line-up of a ruin, a villa
// and two bunkers (the observation post is the brief's "bunker on a mound"), an L-shaped trench run with a corner, and
// a clear 40 x 40 m grass field at x -20..20, z 40..80. Positions are fixed so every run and every stream matches.
void gxBuildScene(App& app) {
  app.newMap(*app.findTemplate("blank_slate"), "GX test scene");
  auto P = [&](const char* id, vec3 p, float yaw, bool comp = false) -> uint64_t {
    const AssetDef* a = app.lib.find(id);
    uint64_t iid = a ? app.placeInstance(a, p, yaw) : 0;
    if (!iid) logf("gx scene: %s at (%.1f, %.1f) not placed", id, p.x, p.z);
    else if (comp) app.placeCompanions(iid);
    return iid;
  };
  P("vil_chateau_grand", vec3(0, 0, 0), 0.0f, true);
  P("ruin_house_shelled", vec3(-52, 0, -80), 0.0f);
  P("vil_villa_deauville", vec3(-24, 0, -80), 0.0f, true);
  P("bunker_observation_post", vec3(8, 0, -80), 0.0f, true);
  P("bunker_field_shelter", vec3(26, 0, -80), 0.0f, true);
  // trench: entry, two straights and a corner along +X, then two straights along +Z (pieces join at their sockets)
  P("aw_trench_entry", vec3(53, 0, 0), 0.0f);
  P("aw_trench_straight", vec3(60, 0, 0), 0.0f);
  P("aw_trench_straight", vec3(68, 0, 0), 0.0f);
  P("aw_trench_corner", vec3(76, 0, 0), 0.0f);
  P("aw_trench_straight", vec3(76, 0, 8), -90.0f);
  P("aw_trench_straight", vec3(76, 0, 16), -90.0f);
  app.doc.camera.set = true; app.doc.camera.pos = vec3(10, 45, 120); app.doc.camera.yaw = 0; app.doc.camera.pitch = -22;
  app.doc.start.set = true; app.doc.start.pos = vec3(0, app.terrain.height(0, 36), 36); app.doc.start.yaw = 0;
  app.syncRuntime();
  app.interior.autoFurnishAll(app);
  MapDoc d = app.doc;   // the file holds the authored instances only (furniture is re-added on load)
  d.instances.erase(std::remove_if(d.instances.begin(), d.instances.end(), [](const Instance& i) { return isFurnishPiece(i); }), d.instances.end());
  std::string err;
  fs::path out = exeDir() / "selftest_out" / "gx_test_scene.nmap.json";
  logf("gx scene: %zu instances, saved %s: %s", d.instances.size(), out.string().c_str(), saveMap(out, d, err) ? "ok" : err.c_str());
}
}

void SelfTest::captureUpdate(App& app, float dt) {
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
    if (const char* set = std::getenv("NMK_CAPTURE_SET"); set && std::string(set) == "gx") {
      // NMK_CAPTURE_SET=gx (docs/graphics-overhaul.md): the graphics-overhaul test scene, content/templates/gx_test_scene,
      // from fixed cameras that match the brief's current_01..07 framings. NMK_GX_REBUILD=1 rebuilds the scene from the
      // Blank Slate in code and writes selftest_out/gx_test_scene.nmap.json (copy it to content/templates/).
      const TemplateInfo* gt = app.findTemplate("gx_test_scene");
      if (gt && !std::getenv("NMK_GX_REBUILD") && fs::exists(contentDir() / "templates" / gt->raw.value("map", std::string("-")))) app.newMap(*gt, "GX test scene");
      else gxBuildScene(app);
      auto I = [&](const char* id) -> Instance* { for (auto& i : app.doc.instances) if (i.assetId == id) return &i; return nullptr; };
      auto W = [&](Instance* i, vec3 l) { return i->pos + yawQuat(i->yaw) * l; };
      cams.clear();
      Instance* ch = I("vil_chateau_grand"); Instance* op = I("bunker_observation_post"); Instance* tc = I("aw_trench_corner");
      if (ch) {
        const AssetDef* a = app.lib.find(ch->assetId);
        float fx = a->footprint.x * 0.5f, fz = a->footprint.y * 0.5f, H = a->bmax.y - a->bmin.y;
        cams.push_back({"current_01_hallway", W(ch, vec3(-17.5f, 5.5f, -8.8f)), W(ch, vec3(-14.5f, 5.1f, -8.8f))});   // level-1 corridor (B3 tour look 2)
        cams.push_back({"current_02_room", W(ch, vec3(0.0f, 1.9f, 9.0f)), W(ch, vec3(0.0f, 1.5f, -8.2f))});           // entrance hall (B3 tour look 1)
        if (std::getenv("NMK_GX5_CLOSE")) {   // GX5 close-ups for judging the wall detail: corridor wall at 1.3 m, hall wall corner
          cams.push_back({"gx5_corridor_wall", W(ch, vec3(-17.5f, 5.3f, -8.8f)), W(ch, vec3(-17.5f, 5.1f, -6.8f))});
          cams.push_back({"gx5_corridor_wall2", W(ch, vec3(-17.5f, 5.3f, -8.8f)), W(ch, vec3(-17.5f, 5.1f, -10.8f))});
          cams.push_back({"gx5_hall_corner", W(ch, vec3(-2.0f, 1.7f, 8.0f)), W(ch, vec3(-6.0f, 1.2f, 3.0f))});
        }
        cams.push_back({"current_03_exterior", W(ch, vec3(-fx * 0.9f - 4, 1.7f + H * 0.3f, -fz * 1.6f - 6)), W(ch, vec3(0, H * 0.35f, 0))});
      }
      if (op) cams.push_back({"current_04_lineup", W(op, vec3(-16.0f, 1.7f, 46.0f)), W(op, vec3(-16.0f, 3.2f, 0.0f))});
      cams.push_back({"current_05_field", vec3(0.0f, 1.65f, 38.0f), vec3(0.0f, 1.2f, 140.0f)});
      if (op) cams.push_back({"current_06_bunker", W(op, vec3(-6.4f, 1.7f, 6.2f)), W(op, vec3(0.6f, 1.7f, -0.4f))});
      if (tc) cams.push_back({"current_07_trench", W(tc, vec3(-3.8f, 2.9f, 3.8f)), W(tc, vec3(1.6f, -0.6f, -1.6f))});
      // standing on the trench floor (1.62 m down, eye 1.65 m) 9 m back along the corner's first leg, looking along it
      // to the corner: the floor, both revetment walls and the lip against the sky
      if (tc) cams.push_back({"gx_trench_floor", W(tc, vec3(-9.0f, 0.03f, 0.05f)), W(tc, vec3(0.0f, 0.25f, 0.0f))});
      app.syncRuntime();
      app.hideEditorOverlays = true;
      app.grassBudgetMs = -1;
      ci = 0; frames = 0; stage = 801;
      return;
    }
    if (const char* set = std::getenv("NMK_CAPTURE_SET"); set && std::string(set).rfind("ids:", 0) == 0) {
      // NMK_CAPTURE_SET=ids:a,b,c (D1 model pass): each asset from a front-corner and a rear-corner camera sized to
      // its footprint; names are <id>_front / <id>_rear
      std::string s = std::string(set).substr(4) + ",";
      cams.clear();
      static std::deque<std::string> keep;
      int k = 0;
      for (size_t p = 0, q; (q = s.find(',', p)) != std::string::npos; p = q + 1) {
        std::string id = s.substr(p, q - p);
        const AssetDef* a = id.empty() ? nullptr : app.lib.find(id);
        if (!a) continue;
        uint64_t iid = app.placeInstance(a, vec3(-125.0f + 50.0f * (k % 6), 0, -125.0f + 50.0f * (k / 6)), 0.0f);
        ++k;
        if (!iid) continue;
        vec3 o = app.doc.find(iid)->pos;
        float r = std::max(1.6f, 0.5f * std::max(a->footprint.x, a->footprint.y)) * 1.3f;
        vec3 t = o + vec3(0, std::clamp(0.2f * r, 0.9f, 1.2f), 0);
        keep.push_back(id + "_front"); cams.push_back({keep.back().c_str(), t + vec3(0.75f * r, 0.45f * r, 1.0f * r), t});
        keep.push_back(id + "_rear"); cams.push_back({keep.back().c_str(), t + vec3(-0.8f * r, 0.55f * r, -0.9f * r), t});
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
        {   // the interiors suite's re-check ("every piece valid"): each generated floor piece against the manual placement rules
          int pieces = 0, bad = 0; std::string why;
          for (auto& c : app.doc.instances) {
            if (!c.extra.is_object() || !c.extra.contains("furnish") || c.extra["furnish"].value("host", (uint64_t)0) != id) continue;
            ++pieces;
            const AssetDef* ca = app.assetFor(c); const PropDef* pd = app.interior.prop(ca);
            if (!pd || pd->mount > 0 || c.assetId == "furn_curtains" || c.extra["furnish"].value("slot", std::string()).find("/top") != std::string::npos) continue;
            vec3 p = c.pos; PlaceCheck pc = app.interior.checkPlacement(app, ca, p, c.yaw, c.id);
            if (!pc.ok || std::fabs(pc.y - c.pos.y) > 0.03f) { ++bad; if (why.size() < 400) { vec3 l = glm::inverse(yawQuat(app.doc.find(id)->yaw)) * (c.pos - app.doc.find(id)->pos); char lp[64]; std::snprintf(lp, sizeof lp, "@(%.2f,%.2f,%.2f)", l.x, l.y, l.z); why += " " + c.assetId + lp + ":" + pc.reason; } }
          }
          std::printf("b3 recheck %s: %zu rooms, %d pieces, %d invalid%s\n", a->id.c_str(), app.interior.rooms(a)->rooms.size(), pieces, bad, why.c_str());
        }
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
    if (const char* set = std::getenv("NMK_CAPTURE_SET"); set && (std::string(set) == "gx6" || std::string(set) == "gx6street" || std::string(set) == "b3street")) {
      // NMK_CAPTURE_SET=gx6: the GX6 houses, large houses and mil_stronghold (tools/catalog_gx_buildings.mjs),
      // furnished, with companions: front and rear three-quarter views and one view from every "look" step of the
      // tour. NMK_CAPTURE_SET=gx6street: the eleven houses in a furnished street; "perf_" cameras hold 150 frames and
      // write the mean frame / GPU time, triangles and draws to selftest_out/report_gx6_perf.txt.
      static std::deque<std::string> keep;
      static const char* ids[] = {"vil_colombage_auge", "vil_granite_longere", "vil_brick_townhouse", "vil_rendered_shutters", "vil_cafe_corner", "vil_bakery",
                                  "vil_relais_inn", "vil_merchant_house", "vil_notary_house", "vil_farm_courtyard", "vil_pension_hotel", "mil_stronghold"};
      static const char* b3ids[] = {"vil_longhouse_thatched", "vil_townhouse_jettied", "vil_villa_deauville", "vil_manor_turret", "vil_corner_shop", "vil_gatehouse_chapel",
                                    "vil_mill_house", "vil_terrace_pair", "vil_cottage_barn", "vil_rectory", "vil_rectory", "mil_stronghold"};   // b3street: the same street of B3 houses (baseline)
      cams.clear();
      bool street = std::string(set) != "gx6";
      int n = 0; float sx = -150.0f;
      for (const char* aid : std::string(set) == "b3street" ? b3ids : ids) {
        const AssetDef* a = app.lib.find(aid);
        if (!a || (street && std::string(aid).rfind("mil_", 0) == 0)) continue;
        float fx = a->footprint.x * 0.5f, fz = a->footprint.y * 0.5f, H = a->bmax.y - a->bmin.y;
        vec3 at3 = street ? vec3(sx + fx, 0, -fz) : vec3(-160.0f + (n % 4) * 105.0f, 0, -110.0f + (n / 4) * 105.0f);   // street: fronts on z = 0
        if (street) sx += 2.0f * fx + 1.0f;
        ++n;
        uint64_t id = app.placeInstance(a, at3, 0.0f);
        if (!id) continue;
        app.placeCompanions(id);
        app.syncRuntime();
        app.interior.furnish(app, id, 0);
        if (street) continue;
        vec3 o = app.doc.find(id)->pos;
        keep.push_back(a->id + "_ext_front"); cams.push_back({keep.back().c_str(), o + vec3(fx * 0.9f + 4, 1.7f + H * 0.25f, fz * 1.6f + 6), o + vec3(0, H * 0.35f, 0)});
        keep.push_back(a->id + "_ext_rear"); cams.push_back({keep.back().c_str(), o + vec3(-fx * 0.9f - 4, 1.7f + H * 0.3f, -fz * 1.6f - 6), o + vec3(0, H * 0.35f, 0)});
        keep.push_back(a->id + "_ext_eye"); cams.push_back({keep.back().c_str(), o + vec3(fx * 0.55f, 1.7f, fz + 7.0f), o + vec3(-fx * 0.2f, 1.6f + H * 0.18f, 0)});
        vec2 at(0); float y = 0; int k = 0;
        for (auto& st : a->raw["tour"].value("steps", json::array())) {
          if (st.contains("goto")) { at = vec2(st["goto"][0].get<float>(), st["goto"][1].get<float>()); if (st.contains("expectY")) y = st["expectY"].get<float>(); }
          if (st.contains("look")) { keep.push_back(a->id + "_int" + std::to_string(++k)); cams.push_back({keep.back().c_str(), o + vec3(at.x, y + 1.6f, at.y), o + jvec3(st["look"])}); }
        }
      }
      if (street) {
        float x0 = -150.0f, x1 = sx;
        cams.push_back({"street_eye", vec3(x0 - 2.0f, 1.7f, 7.0f), vec3(x0 + 40.0f, 3.0f, -2.0f)});
        cams.push_back({"street_mid", vec3((x0 + x1) * 0.5f, 1.7f, 9.0f), vec3((x0 + x1) * 0.5f + 25.0f, 3.0f, -4.0f)});
        cams.push_back({"perf_street_eye", vec3(x0 - 2.0f, 1.7f, 7.0f), vec3(x0 + 40.0f, 3.0f, -2.0f)});
        cams.push_back({"perf_street_mid", vec3((x0 + x1) * 0.5f, 1.7f, 9.0f), vec3((x0 + x1) * 0.5f + 25.0f, 3.0f, -4.0f)});
        cams.push_back({"perf_street_far", vec3((x0 + x1) * 0.5f, 25.0f, 220.0f), vec3((x0 + x1) * 0.5f, 2.0f, -6.0f)});
        cams.push_back({"perf_empty_view", vec3((x0 + x1) * 0.5f, 1.7f, 9.0f), vec3((x0 + x1) * 0.5f, 3.0f, 60.0f)});   // the same spot facing away: the base cost
        cams.push_back({"perf_street_eye_warm", vec3(x0 - 2.0f, 1.7f, 7.0f), vec3(x0 + 40.0f, 3.0f, -2.0f)});   // the eye view again, after warm-up
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
    if (const char* set = std::getenv("NMK_CAPTURE_SET"); set && std::string(set) == "grass") {
      // NMK_CAPTURE_SET=grass (GX3): a 40 x 40 m pasture with a painted dirt path, a dry patch and a bunker, from
      // fixed cameras; each camera also logs GPU ms with grass on and off (GPU_TURN_EXCLUSIVE=1 for real numbers)
      auto Y = [&](float x, float z, float h) { return vec3(x, app.terrain.height(x, z) + h, z); };
      std::vector<vec2> path, dry;
      for (int k = 0; k <= 20; ++k) { float x = -24.0f + 2.4f * k; path.push_back({x, 4.0f + 2.5f * std::sin(x * 0.15f) - 0.8f}); }
      for (int k = 20; k >= 0; --k) { float x = -24.0f + 2.4f * k; path.push_back({x, 4.0f + 2.5f * std::sin(x * 0.15f) + 0.8f}); }
      for (int k = 0; k < 24; ++k) { float a = k * 6.2831853f / 24; dry.push_back({-9.0f + std::cos(a) * (5.0f + std::sin(a * 3) * 1.2f), -9.0f + std::sin(a) * 4.0f}); }
      LassoRegion rp, rd; rp.add(path); rd.add(dry);
      groundPaintRegion(app, rp, 2, 1.0f, 0.6f);   // dirt
      groundPaintRegion(app, rd, 8, 1.0f, 1.5f);   // dry grass
      CM = app.placeInstance(app.lib.find("bunker_mg_casemate"), vec3(14, 0, -16), 200.0f);
      app.syncRuntime();
      app.hideEditorOverlays = true;
      app.grassBudgetMs = -1;
      app.grass.quality = GrassQuality::Medium;
      cams = {
        {"grass_wide", Y(0, 22, 1.7f), Y(0, -40, 0.9f)},
        {"grass_close", Y(2.2f, -2.0f, 1.25f), Y(0.4f, -4.4f, 0.0f)},
        {"grass_low", Y(-3, 10, 0.55f), Y(-3, -10, 0.35f)},
        {"grass_high", Y(-18, 26, 9.0f), Y(6, -30, 0.0f)},
        {"grass_path", Y(-14, 13, 1.7f), Y(0, 3, 0.0f)},
        {"grass_bunker", Y(5, -3, 1.7f), Y(13, -14, 0.6f)},
      };
      grassSet = true;
      ci = 0; frames = 0; stage = 801;
      return;
    }
    if (const char* set = std::getenv("NMK_CAPTURE_SET"); set && std::string(set).rfind("gx4zf:", 0) == 0) {
      // NMK_CAPTURE_SET=gx4zf:id,id (GX4 z-fighting close-ups): per building a stairwell (from the top tread of the
      // highest flight down it) and an interior door header (from 1.3 m inside the door up at its lintel)
      static std::deque<std::string> keep;
      std::string s = std::string(set).substr(6) + ",";
      cams.clear(); int k = 0;
      for (size_t p = 0, q; (q = s.find(',', p)) != std::string::npos; p = q + 1) {
        std::string id = s.substr(p, q - p); const AssetDef* a = id.empty() ? nullptr : app.lib.find(id);
        if (!a) continue;
        uint64_t iid = app.placeInstance(a, vec3(-200.0f + 70.0f * k++, 0, 0), 0.0f);
        if (!iid) continue;
        vec3 o = app.doc.find(iid)->pos;
        vec3 hi(0, -1e9f, 0), lo(0, 1e9f, 0);   // the highest tread and the lowest tread under it (same flight: within 4 m)
        for (auto& pj : a->raw.value("parts", json::array())) if (pj.value("m", std::string()) == "tread_x" && !pj.contains("r")) { vec3 c = jvec3(pj["c"]); if (c.y > hi.y) hi = c; }
        for (auto& pj : a->raw.value("parts", json::array())) if (pj.value("m", std::string()) == "tread_x" && !pj.contains("r")) { vec3 c = jvec3(pj["c"]); if (c.y < lo.y && c.y > hi.y - 3.2f && glm::length(vec2(c.x - hi.x, c.z - hi.z)) < 5.0f) lo = c; }
        if (hi.y > -1e8f) { keep.push_back(id + "_stair"); cams.push_back({keep.back().c_str(), o + hi + vec3(0, 1.65f, 0) + (hi - lo) * vec3(0.25f, 0, 0.25f), o + lo + vec3(0, 0.6f, 0)}); }
        for (auto& it : a->raw.value("interactables", json::array())) {
          if (it.value("kind", std::string()) != "door" || !it.contains("aperture")) continue;
          vec3 mn = jvec3(it["aperture"]["min"]), mx = jvec3(it["aperture"]["max"]), c = (mn + mx) * 0.5f;
          bool alongX = mx.x - mn.x > mx.z - mn.z;
          vec3 in = alongX ? vec3(0, 0, c.z > 0 ? -1.0f : 1.0f) : vec3(c.x > 0 ? -1.0f : 1.0f, 0, 0);   // toward the building centre
          keep.push_back(id + "_door"); cams.push_back({keep.back().c_str(), o + vec3(c.x, 1.55f, c.z) + in * 1.3f, o + vec3(c.x, mx.y + 0.05f, c.z)});
          break;
        }
      }
      app.syncRuntime(); app.hideEditorOverlays = true; app.grassBudgetMs = -1;
      ci = 0; frames = 0; stage = 801;
      return;
    }
    if (const char* set = std::getenv("NMK_CAPTURE_SET"); set && std::string(set).rfind("gx4", 0) == 0) {
      // NMK_CAPTURE_SET=gx4[:id,id]: GX4 exteriors (docs/exterior-references.md), unfurnished, no companions: a front
      // three-quarter view matching the GLB reference renders (tools/gx4_refs.py), a street-level corner, an eaves / roof
      // close-up and a rear three-quarter. Names are <asset>_<view>.
      static std::deque<std::string> keep;
      static const char* all[] = {"vil_chateau_grand", "vil_longhouse_thatched", "vil_townhouse_jettied", "vil_villa_deauville", "vil_manor_turret", "vil_corner_shop",
                                  "vil_gatehouse_chapel", "vil_mill_house", "vil_terrace_pair", "vil_cottage_barn", "vil_rectory", "house_farmhouse_2story",
                                  "house_stone_cottage", "small_carpenter_workshop", "small_stone_dairy", "small_storehouse", "small_roadside_office",
                                  "large_village_schoolhouse", "large_village_inn", "large_stone_warehouse", "large_municipal_building", "vil_norman_farmhouse",
                                  "vil_stone_barn", "vil_church"};
      std::vector<std::string> ids;
      std::string s = set; size_t c = s.find(':');
      if (c == std::string::npos) ids.assign(std::begin(all), std::end(all));
      else for (size_t p = c + 1, q; p <= s.size(); p = q + 1) { q = s.find(',', p); if (q == std::string::npos) q = s.size(); if (q > p) ids.push_back(s.substr(p, q - p)); }
      cams.clear();
      int n = 0;
      for (auto& aid : ids) {
        const AssetDef* a = app.lib.find(aid);
        if (!a) { logf("gx4 capture: no asset %s", aid.c_str()); continue; }
        uint64_t id = app.placeInstance(a, vec3(-240.0f + 80.0f * (n % 7), 0, -220.0f + 80.0f * (n / 7)), 0.0f); ++n;
        if (!id) { logf("gx4 capture: %s not placed", aid.c_str()); continue; }
        vec3 o = app.doc.find(id)->pos;
        float fx = 0.5f * (a->bmax.x - a->bmin.x), fz = 0.5f * (a->bmax.z - a->bmin.z), H = a->bmax.y, R = std::max({2 * fx, 2 * fz, H});
        vec3 ctr = o + vec3(0.5f * (a->bmin.x + a->bmax.x), 0.45f * H, 0.5f * (a->bmin.z + a->bmax.z));
        auto add = [&](const char* v, vec3 p, vec3 t) { keep.push_back(aid + "_" + v); cams.push_back({keep.back().c_str(), p, t}); };
        add("34", ctr + vec3(-0.62f * R, 0.3f * R, 0.9f * R), ctr);
        add("eye", o + vec3(fx + std::max(5.0f, 0.3f * fx), 1.7f, fz + std::max(5.0f, 0.3f * fz)), o + vec3(0, 0.4f * H, 0));
        add("roof", o + vec3(0.35f * fx, 0.62f * H + 2.5f, fz + 5.5f), o + vec3(0.1f * fx, 0.62f * H, 0.3f * fz));
        add("rear", ctr + vec3(0.62f * R, 0.3f * R, -0.9f * R), ctr);
      }
      app.syncRuntime();
      app.hideEditorOverlays = true;
      app.grassBudgetMs = -1;
      ci = 0; frames = 0; stage = 801;
      return;
    }
    if (const char* set = std::getenv("NMK_CAPTURE_SET"); set && std::string(set) == "b2") {
      // NMK_CAPTURE_SET=b2: the B2 buildings with their companions (front corner and rear corner, framed from the
      // footprint) and every drs_* dressing piece close up (tools/catalog_houses2.mjs, catalog_dressing.mjs)
      static std::deque<std::string> keep;
      cams.clear();
      std::vector<const AssetDef*> houses, dress;
      for (auto& a : app.lib.assets) {
        if (a->id.rfind("drs_", 0) == 0) dress.push_back(a.get());
        else if (a->raw.contains("companions") && a->version >= 2 && a->enterable && a->category != "Bunkers") houses.push_back(a.get());
      }
      int n = 0;
      auto frame = [&](const AssetDef* a, vec3 at, const char* view, vec3 dir, float k) {
        float r = std::max({a->footprint.x, a->footprint.y, a->bmax.y}) * k + 2.0f;
        keep.push_back(a->id + "_" + view);
        vec3 t = at + vec3(0, a->bmax.y * 0.35f, 0);
        cams.push_back({keep.back().c_str(), t + glm::normalize(dir) * r, t});
      };
      for (auto* a : houses) {
        uint64_t id = app.placeInstance(a, vec3(-240.0f + 90.0f * (n % 6), 0, -220.0f + 80.0f * (n / 6)), 0.0f); ++n;   // inside the 300 m build area
        if (!id) { logf("b2 capture: %s not placed", a->id.c_str()); continue; }
        app.placeCompanions(id);
        vec3 o = app.doc.find(id)->pos;
        frame(a, o, "front", vec3(0.75f, 0.42f, 1.0f), 1.15f);
        frame(a, o, "rear", vec3(-0.8f, 0.5f, -1.0f), 1.15f);
        // interiors: furnish, log rooms and pieces per detected storey, one view per storey from its largest room
        app.syncRuntime();
        int nf = app.interior.furnish(app, id, 0);
        const RoomInfo* ri = app.interior.rooms(a);
        std::string rep = a->id + ": " + std::to_string(nf) + " pieces;";
        for (size_t s = 0; s < ri->storeys.size(); ++s) {
          auto& S = ri->storeys[s];
          int rooms = 0, pieces = 0; const RoomInfo::Room* big = nullptr;
          for (auto& R : ri->rooms) if (R.storey == (int)s) { ++rooms; if (!big || R.area > big->area) big = &R; }
          for (auto& i : app.doc.instances) if (isFurnishPiece(i) && i.extra["furnish"].value("host", (uint64_t)0) == id && i.pos.y - o.y > S.y - 0.3f && i.pos.y - o.y < S.ceil) ++pieces;
          rep += " storey y=" + std::to_string(S.y).substr(0, 4) + " rooms " + std::to_string(rooms) + " pieces " + std::to_string(pieces) + ";";
          if (!big) continue;
          keep.push_back(a->id + "_int" + std::to_string(s));   // from one corner of the room across to the other
          cams.push_back({keep.back().c_str(), o + vec3(big->mn.x + 0.35f, S.y + 1.7f, big->mn.y + 0.35f), o + vec3(big->mx.x - 0.3f, S.y + 0.4f, big->mx.y - 0.3f)});
        }
        logf("b2 interiors %s", rep.c_str());
      }
      n = 0;
      for (auto* a : dress) {
        uint64_t id = app.placeInstance(a, vec3(-250.0f + 45.0f * (n % 10), 0, 60.0f + 50.0f * (n / 10)), 0.0f); ++n;
        if (!id) { logf("b2 capture: %s not placed", a->id.c_str()); continue; }
        frame(a, app.doc.find(id)->pos, "close", vec3(0.7f, 0.45f, 1.0f), 1.0f);
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
  if (const char* pose = std::getenv("NMK_CAPTURE_POSE")) {   // D1: every station traversed / elevated / recoiling
    float y = 0, e = 0, t = 1;
    if (std::sscanf(pose, "%f,%f,%f", &y, &e, &t) >= 2)
      for (auto& [id, r] : app.rt) if (r.asset && !r.asset->station.kind.empty()) { r.st.yaw = y; r.st.elev = e; r.st.recoilT = t; }
  }
  { static bool presetDone = false;   // NMK_CAPTURE_PRESET=Low|Medium|High (GX5: the High-only material extras)
    if (!presetDone) { presetDone = true; if (const char* ps = std::getenv("NMK_CAPTURE_PRESET")) { std::string q = ps; settings().applyPreset(app, q == "Low" ? GfxPreset::Low : q == "High" ? GfxPreset::High : GfxPreset::Medium); } } }
  if (ci >= cams.size()) { finish(app); return; }
  Cam& c = cams[ci];
  app.camPos = c.pos;
  vec3 d = glm::normalize(c.target - c.pos);
  app.camYaw = degrees(std::atan2(-d.x, -d.z));
  app.camPitch = degrees(std::asin(std::clamp(d.y, -1.0f, 1.0f)));
  if (grassSet) {   // frames 1-30 settle (TAA), 30 shot, 40-159 grass on, 160 off, 180-299 timed off
    ++frames;
    if (frames == 30) shotQueue.push_back(tag + "_" + c.name);
    if (frames == 40) { gOn = gOff = 0; gBlades = 0; gN = 0; }
    if (frames > 40 && frames <= 160) { gOn += app.R.stats.gpuMs; gBlades += app.grass.lastDrawnBlades; ++gN; }
    if (frames == 160) {   // resident clumps within 15 m of the eye, before grass is switched off
      gShadowN = app.grass.lastShadowClumps; gNear = 0; app.grass.forEachInstance([&](const GrassInstance& i) { if (glm::length(vec2(i.posYaw.x - c.pos.x, i.posYaw.z - c.pos.z)) < 15.0f) ++gNear; });
      app.grass.quality = GrassQuality::Off;
    }
    if (frames > 180 && frames <= 300) gOff += app.R.stats.gpuMs;
    if (frames == 300) {
      app.grass.quality = GrassQuality::Medium;
      double on = gOn / gN, off = gOff / 120;
      // drawn blades / cards per m^2 within 15 m of the eye: resident clumps there x the preset's drawn share x cards per clump
      size_t near = gNear;
      double nearD = near * (grassPreset(GrassQuality::Medium).density / Grass::kMaxDensity) * Grass::kBlades / (3.14159265 * 225.0);
      summary[c.name] = {{"gpu_ms_grass_on", on}, {"gpu_ms_grass_off", off}, {"grass_ms", on - off}, {"blades_drawn", gBlades / gN}, {"near15_blades_per_m2", nearD}, {"shadow_clumps", gShadowN}};
      logf("grass capture %s: gpu %.3f ms on, %.3f off, grass %.3f ms, %zu blades, %.0f blades/m2 within 15 m, %zu shadow clumps", c.name, on, off, on - off, gBlades / gN, nearD, gShadowN);
      ++ci; frames = 0;
    }
    return;
  }
  if (std::string(c.name).rfind("perf_", 0) == 0) {   // GX6: hold the view, average the last 120 frames
    static double t0 = 0, gpu = 0, cpu = 0, tris = 0, draws = 0; static int nf = 0;
    double now = glfwGetTime();
    if (++frames == 30) { t0 = now; gpu = cpu = tris = draws = 0; nf = 0; }
    else if (frames > 30) { gpu += app.R.stats.gpuMs; cpu += app.R.stats.cpuMs; tris += app.R.stats.tris; draws += app.R.stats.draws; ++nf; }
    if (frames >= 150) {
      std::string line = std::string(c.name) + ": frame " + std::to_string((now - t0) * 1000.0 / std::max(1, nf)) + " ms, gpu " + std::to_string(gpu / std::max(1, nf)) + " ms, render cpu " +
        std::to_string(cpu / std::max(1, nf)) + " ms, tris " + std::to_string((long long)(tris / std::max(1, nf))) + ", draws " + std::to_string((long long)(draws / std::max(1, nf)));
      logf("gx6 perf %s", line.c_str());
      if (FILE* f = std::fopen("selftest_out/report_gx6_perf.txt", "a")) { std::fprintf(f, "%s\n", line.c_str()); std::fclose(f); }
      ++ci; frames = 0;
    }
    return;
  }
  if (++frames == 4) shotQueue.push_back("mat_" + tag + "_" + c.name);
  // NMK_CAPTURE_PERF=<frames>: hold each camera that long and log its mean GPU / CPU frame time (after 60 warm-up frames)
  static const int hold = std::getenv("NMK_CAPTURE_PERF") ? std::max(90, std::atoi(std::getenv("NMK_CAPTURE_PERF"))) : 5;
  static double gpuSum = 0, dtSum = 0, cpuSum = 0; static int n = 0;
  // NMK_CAPTURE_AB=<Renderer::perf switch> (GX5): interleaved A/B while the camera is held: 24-frame blocks alternate
  // the switch on / off, the first 8 frames of a block skipped (GPU timestamps are read 2-3 frames late); logs both
  // sides' mean GPU ms. Other load on a shared GPU falls on both sides alike.
  // NMK_CAPTURE_AB may list several switches (a,b,c): each gets 192 frames (8 blocks), so hold >= 60 + 192 x count
  static std::vector<std::string> abList = [] { std::vector<std::string> v; if (const char* e = std::getenv("NMK_CAPTURE_AB")) { std::string s = e; for (size_t b = 0, q; b <= s.size(); b = q + 1) { q = s.find(',', b); if (q == std::string::npos) q = s.size(); if (q > b) v.push_back(s.substr(b, q - b)); } } return v; }();
  static double abSum[8][2] = {}; static int abN[8][2] = {};
  // wall-clock frame time (the GPU timestamp ring drops frames under load: stats.gpuMs went stale): selftest swaps without vsync,
  // so a GPU-bound frame takes as long as the GPU needs
  static double lastT = 0; double nowT = glfwGetTime(), wallMs = lastT > 0 ? (nowT - lastT) * 1000.0 : 0.0; lastT = nowT;
  if (!abList.empty() && hold > 5 && frames > 60) {
    int rel = frames - 61, sub = rel / 192;
    if (sub < (int)abList.size() && sub < 8) {
      int blk = (rel % 192) / 24, side = blk & 1, in = rel % 24;
      if (in == 0) app.R.perf.set(abList[sub], side == 0);
      if (in >= 8) { abSum[sub][side] += wallMs; ++abN[sub][side]; }
    }
  }
  if (hold > 5 && frames > 60) { gpuSum += app.R.stats.gpuMs; dtSum += dt * 1000.0; cpuSum += app.R.stats.cpuMs; ++n; }
  if (frames >= hold) {
    if (n) logf("capture perf %s: gpu %.3f ms, cpu %.3f ms, frame %.3f ms (%d frames), %d draws, %d tris, %zu terrain tiles", c.name, gpuSum / n, cpuSum / n, dtSum / n, n, app.R.stats.draws, app.R.stats.tris, app.terrain.tiles.size());
    for (size_t j = 0; j < abList.size() && j < 8; ++j) {
      if (abN[j][0] && abN[j][1]) {
        double on = abSum[j][0] / abN[j][0], off = abSum[j][1] / abN[j][1];
        logf("capture ab %s %s: on %.3f ms, off %.3f ms (wall clock), %+.1f %% (%d / %d frames)", c.name, abList[j].c_str(), on, off, off > 0 ? (on / off - 1) * 100 : 0.0, abN[j][0], abN[j][1]);
      }
      app.R.perf.set(abList[j], true);
      abSum[j][0] = abSum[j][1] = 0; abN[j][0] = abN[j][1] = 0;
    }
    gpuSum = dtSum = cpuSum = 0; n = 0;
    ++ci; frames = 0;
  }
}
