// --capture-materials <tag>: identical fixed cameras for before/after material comparisons.
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <cstdio>
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
    if (const char* set = std::getenv("NMK_CAPTURE_SET"); set && (std::string(set) == "gx6" || std::string(set) == "gx6street")) {
      // NMK_CAPTURE_SET=gx6: the GX6 houses, large houses and mil_stronghold (tools/catalog_gx_buildings.mjs),
      // furnished, with companions: front and rear three-quarter views and one view from every "look" step of the
      // tour. NMK_CAPTURE_SET=gx6street: the eleven houses in a furnished street; "perf_" cameras hold 150 frames and
      // write the mean frame / GPU time, triangles and draws to selftest_out/report_gx6_perf.txt.
      static std::deque<std::string> keep;
      static const char* ids[] = {"vil_colombage_auge", "vil_granite_longere", "vil_brick_townhouse", "vil_rendered_shutters", "vil_cafe_corner", "vil_bakery",
                                  "vil_relais_inn", "vil_merchant_house", "vil_notary_house", "vil_farm_courtyard", "vil_pension_hotel", "mil_stronghold"};
      cams.clear();
      bool street = std::string(set) == "gx6street";
      int n = 0; float sx = -150.0f;
      for (const char* aid : ids) {
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
  if (ci >= cams.size()) { finish(app); return; }
  Cam& c = cams[ci];
  app.camPos = c.pos;
  vec3 d = glm::normalize(c.target - c.pos);
  app.camYaw = degrees(std::atan2(-d.x, -d.z));
  app.camPitch = degrees(std::asin(std::clamp(d.y, -1.0f, 1.0f)));
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
  if (frames >= 5) { ++ci; frames = 0; }
}
