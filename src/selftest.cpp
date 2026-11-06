// Scripted milestone test. Phase A (--selftest): new Blank Slate map, place the farmhouse at 37°,
// set the start marker, walk the house through the real controller, move it, save, exit.
// Phase B (--selftest-reopen <map>): relaunch, reopen, verify, walk again, duplicate at an
// arbitrary rotation, check instance independence, mode cycling and undo correctness.
#include "selftest.h"
#include <GLFW/glfw3.h>
#include <fstream>
#include <windows.h>
#include <psapi.h>

static fs::path outDir() { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); return p; }
static const char* kHouse = "house_farmhouse_2story";

void SelfTest::check(bool ok, const std::string& what) {
  results.push_back(std::string(ok ? "PASS  " : "FAIL  ") + what);
  logf("[selftest] %s %s", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) ++failures;
}
vec3 SelfTest::toWorld(App& app, vec3 l) const {
  Instance* i = app.doc.find(houseId);
  return i ? i->pos + yawQuat(i->yaw) * l : l;
}
float SelfTest::worldYaw(App& app, float ly) const { Instance* i = app.doc.find(houseId); return wrapDeg((i ? i->yaw : 0) + ly); }

void SelfTest::beginWalkthrough(App& app) {
  const AssetDef* a = app.lib.find(curAsset);
  steps.clear(); stepIdx = 0; stepTime = 0;
  for (auto& s : a->walkthrough["steps"]) steps.push_back(s);
  ++pass;
  logf("[selftest] walkthrough pass %d: %zu steps (house #%llu at %.1f,%.1f yaw %.1f)", pass, steps.size(),
       (unsigned long long)houseId, app.doc.find(houseId)->pos.x, app.doc.find(houseId)->pos.z, app.doc.find(houseId)->yaw);
}

static float yawTo(vec3 from, vec3 to) { vec3 d = to - from; return degrees(std::atan2(-d.x, -d.z)); }

bool SelfTest::runWalkthrough(App& app, float dt) {
  if (stepIdx >= steps.size()) return true;
  json& s = steps[stepIdx];
  stepTime += dt;
  auto ch = Physics::character();
  Instance* house = app.doc.find(houseId);
  app.scriptedInput = ExploreInput{};
  auto next = [&]() { ++stepIdx; stepTime = 0; };
  std::string label = curAsset.substr(6) + " pass " + std::to_string(pass) + " step " + std::to_string(stepIdx + 1) + " ";
  if (s.contains("goto")) {
    vec3 tgt = toWorld(app, vec3(s["goto"][0].get<float>(), 0, s["goto"][1].get<float>()));
    vec2 d(tgt.x - ch.pos.x, tgt.z - ch.pos.z);
    if (glm::length(d) < 0.22f) {
      if (s.contains("expectY")) {
        float ly = ch.pos.y - house->pos.y, ey = s["expectY"];
        check(std::fabs(ly - ey) < 0.22f, label + s.value("note", std::string("goto")) + ": floor height " + std::to_string(ly).substr(0, 5) + " m (expected " + std::to_string(ey).substr(0, 4) + ")");
      }
      next(); return false;
    }
    app.eyeYaw = yawTo(ch.pos, tgt); app.eyePitch = s.value("pitch", -5.0f);
    app.scriptedInput.move = vec2(0, glm::length(d) < 0.6f ? 0.5f : 1.0f);
    app.scriptedInput.crouch = s.value("crouch", false);   // e.g. through a shell hole lower than a standing man
    if (stepTime > 25) {
      char b[200]; std::snprintf(b, sizeof b, "%s%s: could not reach target (stuck at local %.2f,%.2f,%.2f)", label.c_str(), s.value("note", std::string("goto")).c_str(),
        (glm::inverse(yawQuat(house->yaw)) * (ch.pos - house->pos)).x, ch.pos.y - house->pos.y, (glm::inverse(yawQuat(house->yaw)) * (ch.pos - house->pos)).z);
      // what is in front of the capsule: forward rays at three heights (body user data, distance)
      std::string why;
      for (float hy : {0.06f, 0.15f, 0.25f, 0.7f, 1.1f, 1.25f, 1.38f}) {
        Physics::Hit fh; vec3 o = ch.pos + vec3(0, hy, 0), fd = glm::normalize(vec3(d.x, 0, d.y));
        char w[96];
        if (Physics::castRayEx(o, fd, 1.0f, fh)) std::snprintf(w, sizeof w, " | y+%.2f: ud %llx at %.2f m", hy, (unsigned long long)fh.ud, fh.t);
        else std::snprintf(w, sizeof w, " | y+%.2f: clear", hy);
        why += w;
      }
      check(false, b + why); Physics::setCharacterPos(tgt + vec3(0, s.value("expectY", 0.0f), 0)); next();
    }
    return false;
  }
  if (s.contains("interact")) {
    std::string id = s["interact"];
    vec3 aim = toWorld(app, jvec3(s["aim"]));
    vec3 eye = app.eyePos();
    app.eyeYaw = yawTo(eye, aim);
    app.eyePitch = degrees(std::atan2(aim.y - eye.y, glm::length(vec2(aim.x - eye.x, aim.z - eye.z))));
    const AssetDef* a = app.lib.find(curAsset);
    int k = a->findInteract(id);
    auto& r = app.rt[houseId];
    if (stepTime < 0.1f) return false;
    if (s.value("_sent", false) == false) {
      bool okT = app.target.valid && app.target.inst == houseId && app.target.idx == k;
      check(okT, label + "aim at '" + id + "' shows prompt" + (okT ? "" : (app.target.valid ? " (got " + a->interactables[app.target.idx].id + ")" : " (no target)")));
      s["_before"] = r.progress[k];
      if (okT) app.scriptedInput.interact = true; else app.toggleInteract(houseId, k);
      s["_sent"] = true; return false;
    }
    if (r.dir[k] != 0 && stepTime < 5) return false;
    bool open = r.progress[k] > 0.5f;
    bool want = s.value("expectOpen", !(s["_before"].get<float>() > 0.5f));
    check(open == want, label + "'" + id + "' is now " + (open ? "open" : "closed"));
    next(); return false;
  }
  if (s.contains("look")) {
    vec3 aim = toWorld(app, jvec3(s["look"]));
    vec3 eye = app.eyePos();
    app.eyeYaw = yawTo(eye, aim);
    app.eyePitch = degrees(std::atan2(aim.y - eye.y, glm::length(vec2(aim.x - eye.x, aim.z - eye.z))));
    if (stepTime > s.value("hold", 0.6f)) next();
    return false;
  }
  if (s.contains("shot")) {
    if (pass == 1 || s.value("everyPass", false)) shotQueue.push_back(std::string(s["shot"]) + (pass > 1 ? "_pass" + std::to_string(pass) : ""));
    next(); return false;
  }
  if (s.contains("wait")) { if (stepTime > s["wait"].get<float>()) next(); return false; }
  if (s.contains("checkGround")) {   // feet rest on the canonical ground height (walkable crater, no invisible floor)
    if (stepTime < 0.4f) return false;
    float gh = app.terrain.height(ch.pos.x, ch.pos.z);
    char b[160]; std::snprintf(b, sizeof b, "%sfeet on deformed ground (feet %.3f, ground %.3f, grounded %d)", label.c_str(), ch.pos.y, gh, (int)ch.grounded);
    check(std::fabs(ch.pos.y - gh) < 0.08f && ch.grounded, b);
    next(); return false;
  }
  next(); return false;
}

void SelfTest::finish(App& app) {
  std::string phase = app.docPath.empty() ? "A" : "B";
  std::ofstream f(outDir() / ("report_" + (reportName.size() ? reportName : std::string(bench ? "bench" : stage >= 100 ? "B" : "A")) + ".txt"));
  if (!summary.empty()) std::ofstream(outDir() / ("summary_" + reportName + ".json")) << summary.dump(2);
  f << "Normandy Mapmaker self-test\n";
  f << "GL: " << (const char*)glGetString(GL_RENDERER) << " | " << (const char*)glGetString(GL_VERSION) << "\n\n";
  for (auto& r : results) f << r << "\n";
  f << "\n" << (results.size() - failures) << " passed, " << failures << " failed\n";
  if (bench) { std::ofstream b(outDir() / (reportName.size() ? "benchmark_" + reportName + ".json" : std::string("benchmark.json"))); b << benchOut.dump(2); }
  logf("[selftest] done: %d failures", failures);
  exitCode = failures ? 1 : 0;
  done = true;
}

void SelfTest::update(App& app, float dt) {
  if (dt < 0) {  // post-render: capture queued screenshots (scene only, no UI)
    for (auto& s : shotQueue) app.screenshot(outDir() / (s + ".png"));
    shotQueue.clear();
    if (stage >= 1600 && stage < 1700) sightsUpdate(app, dt);
    return;
  }
  stageTime += dt;
  if (stage >= 2000 && stage < 2100) { aiUpdate(app, dt); return; }   // multiplayer + AI (selftest_ai.cpp)
  if (stage >= 2100 && stage < 2200) { netUpdate(app, dt); return; }  // LAN (selftest_net.cpp)
  if (stage >= 500) { damageUpdate(app, dt); return; }
  static int frame = 0; ++frame;
  static fs::path reopenPath;
  static json expected;
  auto go = [&](int s) { stage = s; stageTime = 0; };
  switch (stage) {
  // ======================= Phase A
  case 0: {
    int argc = __argc; char** argv = __argv;
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--selftest-reopen" && k + 1 < argc) reopenPath = argv[k + 1];
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--selftest-damage") { go(500); return; }
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--selftest-pak") { go(600); return; }
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--selftest-weapons") { go(700); return; }
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--capture-materials") { go(800); return; }
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--selftest-grass") { go(900); return; }
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--benchmark-stress") { go(950); return; }
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--build-range-map") { go(980); return; }
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--record-range") { go(990); return; }
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--selftest-fps") { go(1100); return; }
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--selftest-env") { go(1200); return; }
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--build-village-map") { go(1300); return; }
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--selftest-arsenal") { go(1400); return; }
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--selftest-move") { go(1500); return; }
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--selftest-sights") { go(1600); return; }
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--selftest-update") { go(1700); return; }
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--selftest-entities") { go(1900); return; }
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--selftest-tanks") { go(2600); return; }
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--selftest-ai" || std::string(argv[k]) == "--benchmark-match") { go(2000); return; }
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--selftest-net") { go(2100); return; }
    for (int k = 1; k < argc; ++k) if (std::string(argv[k]) == "--benchmark-village") { extern void setVillageTag(const std::string&); setVillageTag(k + 1 < argc ? argv[k + 1] : "run"); go(1350); return; }
    if (!reopenPath.empty()) { go(100); break; }
    if (bench) { go(200); break; }
    const TemplateInfo* t = app.findTemplate("blank_slate");
    check(t != nullptr, "Blank Slate template is available");
    app.newMap(*t, "Farmhouse regression");
    check(app.doc.instances.empty(), "new Blank Slate map starts with zero placed structures");
    {  // F5 on a fresh map with no start marker must still enter Explore
      bool ok = app.enterExplore();
      check(ok && app.mode == Mode::Explore && app.doc.start.set, "F5 with no start marker enters Explore at the view centre");
      app.exitExplore();
      app.doc.start = StartMarker{};
    }
    const AssetDef* a = app.lib.find(kHouse);
    check(a != nullptr, "farmhouse asset definition loaded");
    if (!a) { finish(app); break; }
    houseId = app.placeInstance(a, vec3(24, 0, -16), 37.0f);
    check(houseId != 0, "farmhouse placed on Blank Slate grass at yaw 37");
    Instance* h = app.doc.find(houseId);
    // corner of the build area must accept a building too
    PlaceCheck corner = app.checkPlacement(a, vec3(app.terrain.buildHalf - 8, 0, app.terrain.buildHalf - 6), 0, 0);
    check(corner.ok, "build-area corner accepts a structure (" + corner.reason + ")");
    PlaceCheck overl = app.checkPlacement(a, h->pos + vec3(3, 0, 0), 37, 0);
    check(!overl.ok, "overlapping placement is rejected with a reason: \"" + overl.reason + "\"");
    json st = a->walkthrough["start"];
    app.doc.start = {true, toWorld(app, jvec3(st["pos"])), worldYaw(app, st.value("yaw", 0.0f))};
    app.doc.start.pos.y = app.terrain.height(app.doc.start.pos.x, app.doc.start.pos.z);
    check(app.startMarkerValid(app.doc.start.pos), "start marker outside the front door is valid");
    app.camPos = toWorld(app, vec3(14, 9, 20)); app.camYaw = yawTo(app.camPos, h->pos + vec3(0, 3, 0)); app.camPitch = -14;
    app.selected = houseId;
    go(1); break;
  }
  case 1:
    if (stageTime > 0.5f && stageTime < 0.5f + dt * 1.5f) shotQueue.push_back("01_creative_farmhouse");
    if (stageTime > 0.8f) { app.selected = 0; check(app.enterExplore(), "F5 enters Explore at the marker"); app.useScripted = true;
      vec3 e = app.eyePos(); check(std::fabs(e.y - app.doc.start.pos.y - 1.62f) < 0.15f, "camera is at eye level (" + std::to_string(e.y - app.doc.start.pos.y).substr(0, 4) + " m)");
      beginWalkthrough(app); go(2); }
    break;
  case 2:
    if (runWalkthrough(app, dt)) {
      app.exitExplore();
      check(app.mode == Mode::Creative, "Esc/Return to Creative restores the editor");
      // move the house (one undoable edit) and re-set the marker at its front door
      Instance* h = app.doc.find(houseId);
      app.pushUndo();
      h = app.doc.find(houseId);
      PlaceCheck c = app.checkPlacement(app.lib.find(kHouse), vec3(-46, 0, 31), h->yaw, h->id);
      check(c.ok, "moved position is valid");
      h->pos = vec3(-46, c.y, 31); app.bump(*h);
      json st = app.lib.find(kHouse)->walkthrough["start"];
      app.doc.start = {true, toWorld(app, jvec3(st["pos"])), worldYaw(app, st.value("yaw", 0.0f))};
      app.doc.start.pos.y = app.terrain.height(app.doc.start.pos.x, app.doc.start.pos.z);
      fs::path p = userMapsDir() / "selftest_regression.nmap.json";
      check(app.saveMapTo(p), "map saved to " + p.filename().string());
      check(fs::exists(p) && !fs::exists(fs::path(p.string() + ".tmp")), "atomic save left no temp file");
      expected = {{"id", h->id}, {"pos", tojson(h->pos)}, {"yaw", h->yaw}, {"start", tojson(app.doc.start.pos)}};
      std::ofstream(outDir() / "expected_A.json") << expected.dump(2);
      app.camPos = toWorld(app, vec3(14, 9, 20)); app.camYaw = yawTo(app.camPos, h->pos + vec3(0, 3, 0)); app.camPitch = -14;
      app.selected = houseId;
      go(3);
    }
    break;
  case 3:
    if (stageTime > 0.4f && stageTime < 0.4f + dt * 1.5f) { shotQueue.push_back("02_creative_after_move"); app.uiShot = outDir() / "00_creative_ui.png"; }
    if (stageTime > 0.7f) go(4);
    break;
  case 4: {  // every other enterable asset that ships a walkthrough, at an arbitrary rotation
    const AssetDef* next = nullptr;
    while (extraIdx < app.lib.assets.size()) {
      const AssetDef* a = app.lib.assets[extraIdx++].get();
      if (a->id != kHouse && a->walkthrough.is_object() && a->walkthrough.contains("steps")) { next = a; break; }
    }
    if (!next) { finish(app); break; }
    curAsset = next->id; pass = 0;
    float yaw = wrapDeg(123.0f + 61.0f * (float)extraIdx);
    houseId = app.placeInstance(next, vec3(-250.0f + 48.0f * (float)(extraIdx % 11), 0, -150.0f + 80.0f * (float)(extraIdx / 11)), yaw);
    check(houseId != 0, next->name + " placed at yaw " + std::to_string((int)yaw));
    if (!houseId) break;
    json st = next->walkthrough["start"];
    app.doc.start = {true, toWorld(app, jvec3(st["pos"])), worldYaw(app, st.value("yaw", 0.0f))};
    app.doc.start.pos.y = app.terrain.height(app.doc.start.pos.x, app.doc.start.pos.z);
    check(app.enterExplore(), "F5 at " + next->name); app.useScripted = true;
    beginWalkthrough(app);
    go(5);
    break;
  }
  case 5:
    if (runWalkthrough(app, dt)) { app.exitExplore(); go(4); }
    break;

  // ======================= Phase B (after a real relaunch)
  case 100: {
    check(app.openMap(reopenPath), "relaunch: reopened " + reopenPath.filename().string());
    try { expected = json::parse(readTextFile(outDir() / "expected_A.json")); } catch (...) {}
    check(app.doc.instances.size() == 1, "exactly one placed instance after reopen");
    if (app.doc.instances.empty()) { finish(app); break; }
    Instance& h = app.doc.instances[0]; houseId = h.id;
    if (expected.is_object()) {
      check(h.id == expected["id"].get<uint64_t>(), "instance ID preserved");
      check(glm::length(h.pos - jvec3(expected["pos"])) < 1e-3f, "position preserved");
      check(std::fabs(h.yaw - expected["yaw"].get<float>()) < 1e-3f, "rotation preserved (" + std::to_string(h.yaw).substr(0, 5) + ")");
      check(glm::length(app.doc.start.pos - jvec3(expected["start"])) < 1e-3f, "exploration start preserved");
    }
    app.syncRuntime();
    stage = 101; stageTime = 0;
    check(app.enterExplore(), "F5 after reopen"); app.useScripted = true;
    beginWalkthrough(app);
    break;
  }
  case 101:
    if (runWalkthrough(app, dt)) {
      app.exitExplore();
      // duplicate at an arbitrary rotation (deep copy with fresh ID)
      app.selected = houseId;
      Instance* h = app.doc.find(houseId);
      app.pushUndo(); h = app.doc.find(houseId);
      h->openDefaults["kitchen_shutters"] = true; app.bump(*h);
      bool dup = app.duplicateSelected();
      check(dup, "duplicate building (Ctrl+D)");
      Instance* d = app.doc.find(app.selected);
      check(d && d->id != houseId, "duplicate receives a fresh instance ID");
      check(d && d->openDefaults == app.doc.find(houseId)->openDefaults, "duplicate deep-copies authored door/window defaults");
      if (d) {
        PlaceCheck c = app.checkPlacement(app.lib.find(kHouse), vec3(70, 0, 60), 251.0f, d->id);
        check(c.ok, "rotated duplicate placement valid");
        d->pos = vec3(70, c.y, 60); d->yaw = 251.0f; d->openDefaults["kitchen_shutters"] = false; app.bump(*d);
        houseId = d->id;
      }
      app.syncRuntime();
      json st = app.lib.find(kHouse)->walkthrough["start"];
      app.doc.start = {true, toWorld(app, jvec3(st["pos"])), worldYaw(app, st.value("yaw", 0.0f))};
      app.doc.start.pos.y = app.terrain.height(app.doc.start.pos.x, app.doc.start.pos.z);
      check(app.enterExplore(), "F5 at the rotated duplicate (yaw 251)"); app.useScripted = true;
      // instance independence: original's kitchen shutters authored open, duplicate's closed
      uint64_t orig = app.doc.instances[0].id;
      int k = app.lib.find(kHouse)->findInteract("kitchen_shutters");
      check(app.rt[orig].progress[k] == 1.0f && app.rt[houseId].progress[k] == 0.0f, "each copy owns its interaction state (authored defaults differ)");
      app.toggleInteract(orig, app.lib.find(kHouse)->findInteract("front_door"));
      int fd = app.lib.find(kHouse)->findInteract("front_door");
      check(app.rt[houseId].dir[fd] == 0, "opening one farmhouse door does not move the other copy's door");
      beginWalkthrough(app);
      go(102);
    }
    break;
  case 102:
    if (runWalkthrough(app, dt)) {
      app.exitExplore();
      // mode-transition cycling: no duplicated colliders or stale state
      int bodies0 = Physics::bodyCount(); size_t rt0 = app.rt.size();
      int plays0 = app.audio.playCount;
      for (int c = 0; c < 10; ++c) { app.enterExplore(); app.exitExplore(); app.syncRuntime(); }
      check(Physics::bodyCount() == bodies0, "10 Creative/Explore cycles: collider count stable (" + std::to_string(bodies0) + ")");
      check(app.rt.size() == rt0 && app.rt.size() == app.doc.instances.size(), "10 cycles: no duplicate runtime instances");
      check(app.audio.playCount == plays0, "10 cycles: no stray sound emitters triggered");
      check(!app.target.valid, "no stale interaction prompt after returning to Creative");
      // authored vs temporary state: walkthrough changes are discarded on return
      int k = app.lib.find(kHouse)->findInteract("front_door");
      check(app.rt[houseId].progress[k] == 0.0f, "door returns to authored closed state after exploring");
      // undo correctness: delete furnished building, undo, redo, undo
      Instance before = *app.doc.find(houseId);
      size_t n0 = app.doc.instances.size();
      app.selected = houseId; app.deleteSelected();
      check(app.doc.instances.size() == n0 - 1, "delete removes the building and its furniture");
      app.undo(); app.redo(); app.undo();
      int count = 0; for (auto& i : app.doc.instances) if (i.id == before.id) ++count;
      check(count == 1 && app.doc.instances.size() == n0, "undo/redo/undo restores exactly one building");
      Instance* after = app.doc.find(before.id);
      check(after && *after == before, "restored building has identical transform and authored settings");
      app.syncRuntime();
      check(Physics::bodyCount() == bodies0, "colliders after undo match (no orphans)");
      // a missing asset is preserved, not erased
      Instance ghost; ghost.id = app.doc.newId(); ghost.assetId = "future_windmill"; ghost.pos = vec3(100, 0, -100); ghost.extra = {{"customField", 42}};
      app.doc.instances.push_back(ghost); app.bump(app.doc.instances.back());
      fs::path p2 = userMapsDir() / "selftest_missing_asset.nmap.json";
      app.saveMapTo(p2);
      MapDoc reread; std::string err; loadMap(p2, reread, err);
      bool kept = false; for (auto& i : reread.instances) if (i.assetId == "future_windmill" && i.extra.value("customField", 0) == 42) kept = true;
      check(kept, "missing-asset placement keeps its ID and data through save");
      check(fs::exists(fs::path(p2.string())) , "save written");
      app.saveMapTo(p2);
      check(fs::exists(fs::path(p2.string() + ".bak")), "previous save kept as .bak");
      finish(app);
    }
    break;

  // ======================= Benchmark: dense settlement + interior walk
  case 200: {
    const TemplateInfo* t = app.findTemplate("blank_slate");
    app.newMap(*t, "Benchmark settlement");
    int placed = 0;
    // the legacy scene uses only the 14 enterable structures (ordnance and crates added later are excluded,
    // so the asset mix and the numbers stay comparable with docs/baseline/benchmark.json)
    std::vector<const AssetDef*> legacy;
    for (auto& a : app.lib.assets) if (a->station.kind.empty() && !(a->crate.is_object() && !a->crate.empty()) && !a->tree.on && a->defense.kind.empty() && !a->rack) legacy.push_back(a.get());
    benchOut["asset_pool"] = legacy.size();
    for (int gz = 0; gz < 6 && placed < 30; ++gz)
      for (int gx = 0; gx < 6 && placed < 30; ++gx) {
        const AssetDef* a = legacy[(gx + gz) % legacy.size()];
        if (app.placeInstance(a, vec3(-90 + gx * 30.0f, 0, -90 + gz * 30.0f), (gx * 37 + gz * 53) % 360)) ++placed;
      }
    benchOut["structures"] = placed;
    houseId = 0;
    for (auto& i : app.doc.instances) if (i.assetId == kHouse) { houseId = i.id; break; }
    go(201); frameTimes.clear();
    break;
  }
  case 201: {
    static double lastT = 0; double nowT = glfwGetTime();
    if (stageTime > 0.2f) frameTimes.push_back((nowT - lastT) * 1000.0);
    lastT = nowT;
    float u = std::min(1.0f, (float)stageTime / 12.0f);
    float a = u * 6.2831853f;
    { static bool mid = false; if (!mid && u > 0.5f) { mid = true; shotQueue.push_back("bench_flyover_mid"); benchOut["flyover_mid"] = {{"draws", app.R.stats.draws}, {"tris", app.R.stats.tris}, {"gpu_ms", app.R.stats.gpuMs}, {"grass_blades", app.grass.lastDrawnBlades}, {"grass_chunks_drawn", app.grass.lastDrawnChunks}, {"grass_chunks", app.grass.chunkCount()}, {"cam_y", app.camPos.y}}; } }
    app.camPos = vec3(std::cos(a) * 140, 45 - 25 * std::sin(u * 3.14159f), std::sin(a) * 140);
    app.camYaw = yawTo(app.camPos, vec3(0)); app.camPitch = -18;
    if (u >= 1) {
      std::sort(frameTimes.begin(), frameTimes.end());
      double sum = 0; for (double v : frameTimes) sum += v;
      PROCESS_MEMORY_COUNTERS pmc{}; GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc);
      benchOut["flyover"] = {{"frames", frameTimes.size()}, {"avg_ms", sum / std::max<size_t>(1, frameTimes.size())},
        {"p50_ms", frameTimes.empty() ? 0 : frameTimes[frameTimes.size() / 2]}, {"p95_ms", frameTimes.empty() ? 0 : frameTimes[frameTimes.size() * 95 / 100]}, {"p99_ms", frameTimes.empty() ? 0 : frameTimes[frameTimes.size() * 99 / 100]}, {"max_ms", frameTimes.empty() ? 0 : frameTimes.back()},
        {"last_gpu_ms", app.R.stats.gpuMs}, {"last_render_cpu_ms", app.R.stats.cpuMs}, {"draws", app.R.stats.draws}, {"tris", app.R.stats.tris},
        {"working_set_mb", pmc.WorkingSetSize / 1048576.0}};
      shotQueue.push_back("bench_settlement");
      if (houseId) {
        Instance* h = app.doc.find(houseId);
        json st = app.lib.find(kHouse)->walkthrough["start"];
        app.doc.start = {true, toWorld(app, jvec3(st["pos"])), worldYaw(app, st.value("yaw", 0.0f))};
        app.doc.start.pos.y = app.terrain.height(app.doc.start.pos.x, app.doc.start.pos.z);
        (void)h;
        app.enterExplore(); app.useScripted = true; beginWalkthrough(app);
      }
      frameTimes.clear(); go(202);
    }
    break;
  }
  case 202: {
    static double lastT = 0; double nowT = glfwGetTime();
    if (stageTime > 0.2f) frameTimes.push_back((nowT - lastT) * 1000.0);
    lastT = nowT;
    if (!houseId || runWalkthrough(app, dt)) {
      std::sort(frameTimes.begin(), frameTimes.end());
      double sum = 0; for (double v : frameTimes) sum += v;
      PROCESS_MEMORY_COUNTERS pmc{}; GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc);
      benchOut["interior_walk"] = {{"frames", frameTimes.size()}, {"avg_ms", sum / std::max<size_t>(1, frameTimes.size())},
        {"p50_ms", frameTimes.empty() ? 0 : frameTimes[frameTimes.size() / 2]}, {"p95_ms", frameTimes.empty() ? 0 : frameTimes[frameTimes.size() * 95 / 100]}, {"p99_ms", frameTimes.empty() ? 0 : frameTimes[frameTimes.size() * 99 / 100]}, {"max_ms", frameTimes.empty() ? 0 : frameTimes.back()},
        {"working_set_mb", pmc.WorkingSetSize / 1048576.0}};
      benchOut["resolution"] = {app.R.width, app.R.height};
      benchOut["gl_renderer"] = (const char*)glGetString(GL_RENDERER);
      finish(app);
    }
    break;
  }
  }
}
