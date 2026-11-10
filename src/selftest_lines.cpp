// --selftest-lines (LN1, docs/lines.md): line features driven through the functions the lasso's Line mode calls.
//  * a trench line over a hill: one GX1 network, a continuous floor (no step between samples), free ends ramped, and a
//    character walks it from one ramp to the other with nothing blocking it;
//  * a crossing line and a T line join the same network; the T end opens into the trench (walkable across);
//  * undo removes a drawn line exactly; editing points changes the map hash; save / reopen keeps lines, hash and floor;
//  * old trench pieces (a joined chain) migrate to one trench line.
#include "selftest.h"
#include "lasso.h"
#include "lines.h"
#include "geom2d.h"
#include "net.h"
#include "editor_nav.h"
#include "physics.h"
#include <cstdarg>
#include <functional>

int lineMigrateTrenches(App& app);   // lines.cpp

namespace {
App* gLinesApp = nullptr;   // checks go into the SelfTest report (LN2's stages 4600+ run after these and finish it)
int mode = -1; bool started = false;
struct Step { std::string name; std::function<bool(App&, float)> fn; };
std::vector<Step> seq; size_t cur = 0; float st = 0, total = 0;
char B[900];
bool chk(bool ok, const char* f, ...) { va_list a; va_start(a, f); std::vsnprintf(B, sizeof B, f, a); va_end(a); gLinesApp->selftest->extCheck(ok, B); return ok; }
void push(const std::string& n, std::function<bool(App&, float)> f) { seq.push_back({n, f}); }
uint64_t L1 = 0, L2 = 0, L3 = 0;
int stamps(const App& a) { int n = 0; for (auto& s : a.terrain.stamps) n += s.kind == TerrainStamp::TRENCH; return n; }
std::vector<LineSample> splineOf(App& a, uint64_t id) { LineData d; Instance* i = a.doc.find(id); return i && lineGet(*i, d) ? lineSpline(d, 1.0f) : std::vector<LineSample>{}; }
void sync(App& a) { a.syncRuntime(); navSyncCuts(a); }

// walk a character along waypoints (floor level); returns the distance it covered along them and whether it got stuck
struct Walk { int agent = -1; size_t wp = 0; float stuck = 0, best = 1e30f; vec3 last{0}; bool done = false, fail = false; std::vector<vec3> pts; };
Walk W;
bool walkStep(Walk& w, float dt) {
  if (w.done || w.fail) return true;
  auto s = Physics::agent(w.agent);
  vec3 tgt = w.pts[w.wp];
  vec2 d(tgt.x - s.pos.x, tgt.z - s.pos.z);
  if (glm::length(d) < 0.8f) { if (++w.wp >= w.pts.size()) { w.done = true; return true; } return false; }
  vec2 v = glm::normalize(d) * 3.0f;
  Physics::updateAgent(w.agent, dt, vec3(v.x, 0, v.y), false, Physics::STAND, std::atan2(-v.x, -v.y));
  Physics::step(dt);
  if (glm::length(s.pos - w.last) < 0.02f) w.stuck += dt; else { w.stuck = 0; w.last = s.pos; }
  if (w.stuck > 2.0f) w.fail = true;
  return false;
}

void build() {
  push("map with a hill", [](App& a, float) {
    a.newMap(*a.findTemplate("blank_slate"), "Lines test");
    LassoRegion r; r.add(geom2d::circle(vec2(0, 0), 22.0f, 64));
    lassoElevate(a, r, ElevParams{6.0f, 28.0f, 1, false});
    sync(a);
    return true;
  });
  push("trench line over the hill", [](App& a, float) {
    L1 = lineCreate(a, "trench", {vec2(-70, 4), vec2(-25, -6), vec2(0, 2), vec2(30, 10), vec2(70, 0)}, json{{"fire", "left"}, {"ramps", "both"}});
    sync(a);
    chk(L1 != 0, "a trench line is drawn (5 points)");
    chk(stamps(a) == 1, "it is one GX1 trench network (%d)", stamps(a));
    auto sp = splineOf(a, L1);
    float worst = 0, deep = 0; int n = 0;
    for (size_t k = 1; k < sp.size(); ++k) {
      float h0 = a.terrain.height(sp[k - 1].p.x, sp[k - 1].p.y), h1 = a.terrain.height(sp[k].p.x, sp[k].p.y);
      worst = std::max(worst, std::fabs(h1 - h0));
      float g = a.terrain.groundBase(sp[k].p.x, sp[k].p.y);
      if (sp[k].s > 6 && sp[k].s < sp.back().s - 6) { deep += g - h1; ++n; }
    }
    chk(n > 0 && deep / n > 1.3f, "the floor is %.2f m below the ground along the line (middle)", n ? deep / n : 0.0f);
    chk(worst < 0.45f, "the floor is continuous over the hill: largest step between 1 m samples %.2f m (climb 0.45)", worst);
    float e0 = a.terrain.groundBase(sp.front().p.x, sp.front().p.y) - a.terrain.height(sp.front().p.x, sp.front().p.y);
    chk(e0 < 0.4f, "a free end ramps up to the ground (%.2f m below it at the end)", e0);
    return true;
  });
  push("walk the trench end to end", [](App& a, float t) {
    if (W.agent < 0) {
      if (t < 1.0f) return false;   // terrain colliders settle
      auto sp = splineOf(a, L1);
      for (size_t k = 2; k < sp.size(); k += 3) W.pts.push_back(vec3(sp[k].p.x, a.terrain.height(sp[k].p.x, sp[k].p.y), sp[k].p.y));
      vec3 s0(sp[0].p.x, a.terrain.height(sp[0].p.x, sp[0].p.y) + 0.1f, sp[0].p.y);
      W.agent = Physics::createAgent(s0); W.last = s0;
      return false;
    }
    for (int k = 0; k < 4; ++k) if (walkStep(W, 1.0f / 60)) break;
    if (!W.done && !W.fail && t < 90) return false;
    auto s = Physics::agent(W.agent);
    chk(W.done, "a character walks the whole trench from ramp to ramp: %zu of %zu waypoints (%s at %.1f, %.1f)", W.wp, W.pts.size(), W.fail ? "stuck" : W.done ? "arrived" : "timed out", s.pos.x, s.pos.z);
    Physics::destroyAgent(W.agent); W = Walk();
    return true;
  });
  push("crossing and T lines join the network", [](App& a, float) {
    L2 = lineCreate(a, "trench", {vec2(-5, -40), vec2(0, 0), vec2(5, 40)}, json{{"ramps", "both"}});
    sync(a);
    chk(stamps(a) == 1, "a line crossing it joins the same network (%d networks)", stamps(a));
    auto sp = splineOf(a, L1);
    vec2 mid = sp[sp.size() * 3 / 4].p;   // a T from the side, ending on the first line
    L3 = lineCreate(a, "trench", {mid + vec2(0, 30), mid + vec2(0, 0.4f)}, json{{"ramps", "both"}});
    sync(a);
    chk(stamps(a) == 1, "a T line ending on it joins too (%d networks)", stamps(a));
    float drop = a.terrain.groundBase(mid.x, mid.y + 1.5f) - a.terrain.height(mid.x, mid.y + 1.5f);
    chk(drop > 1.2f, "the T end is open into the trench: floor %.2f m down where it meets (no ramp, no wall)", drop);
    return true;
  });
  push("undo, edit and the map hash", [](App& a, float) {
    size_t n0 = a.doc.instances.size();
    std::string h0 = Net::mapHash(a);
    uint64_t id = lineCreate(a, "trench", {vec2(80, -60), vec2(110, -60)}, json::object());
    chk(id && a.doc.instances.size() == n0 + 1 && Net::mapHash(a) != h0, "a drawn line is in the map and its hash");
    a.undo(); sync(a);
    chk(a.doc.instances.size() == n0 && Net::mapHash(a) == h0, "one undo removes it exactly (hash restored)");
    std::string h1 = Net::mapHash(a);
    chk(lineInsertPoint(a, L2, vec2(3, 20)), "a point is inserted on a line");
    chk(Net::mapHash(a) != h1, "editing a line's points changes the map hash (LAN identity follows the line)");
    a.undo(); sync(a);
    chk(Net::mapHash(a) == h1, "and undo restores it");
    return true;
  });
  push("save and reopen", [](App& a, float) {
    fs::path p = exeDir() / "selftest_out" / "lines_test.nmap.json";
    std::string h0 = Net::mapHash(a);
    auto sp = splineOf(a, L1); std::vector<float> f0; for (auto& q : sp) f0.push_back(a.terrain.height(q.p.x, q.p.y));
    bool ok = a.saveMapTo(p) && a.openMap(p);
    sync(a);
    float worst = 0; for (size_t k = 0; k < sp.size(); ++k) worst = std::max(worst, std::fabs(a.terrain.height(sp[k].p.x, sp[k].p.y) - f0[k]));
    chk(ok && Net::mapHash(a) == h0, "save and reopen give the same map hash");
    chk(worst < 0.01f && stamps(a) == 1, "and the same trench floor (largest change %.3f m, %d network)", worst, stamps(a));
    return true;
  });
  push("old trench pieces migrate to a line", [](App& a, float) {
    a.newMap(*a.findTemplate("blank_slate"), "Lines migration");
    const AssetDef* e = a.lib.find("aw_trench_entry"); const AssetDef* s = a.lib.find("aw_trench_straight");
    if (!e || !s) { chk(false, "trench piece assets exist"); return true; }
    auto add = [&](const AssetDef* d, vec3 p) { Instance i; i.id = a.doc.newId(); i.assetId = d->id; i.assetVersion = d->version; i.pos = p; i.yaw = 0; a.doc.instances.push_back(i); };
    add(e, vec3(-3, 0, 50)); add(s, vec3(4, 0, 50)); add(s, vec3(12, 0, 50));   // joined at x = 0 and 8

    int n = lineMigrateTrenches(a);
    int pieces = 0, lines = 0; for (auto& i : a.doc.instances) { pieces += i.assetId.rfind("aw_trench_", 0) == 0; lines += i.assetId == "line_trench"; }
    LineData d; for (auto& i : a.doc.instances) if (i.assetId == "line_trench") lineGet(i, d);
    chk(n == 1 && pieces == 0 && lines == 1, "a chain of three joined pieces becomes one trench line (%d made, %d pieces left)", n, pieces);
    chk(!d.pts.empty() && d.pts.front().d < 0.01f && glm::length(d.pts.back().p - vec2(16, 50)) < 0.05f, "it runs from the entry ramp (depth 0) to the last piece's end");
    return true;
  });
}
}   // namespace

bool linesSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-lines") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return false;
  if (!started) { started = true; gLinesApp = &app; build(); cur = 0; st = 0; }
  if (cur < seq.size()) {
    bool done = false;
    try { done = seq[cur].fn(app, st); } catch (std::exception& e) { app.selftest->extCheck(false, "step '" + seq[cur].name + "' threw: " + e.what()); done = true; }
    st += dt; total += dt;
    if (done) { logf("[lines] step done: %s (%.1f s)", seq[cur].name.c_str(), total); ++cur; st = 0; }
    if (total > 300) { app.selftest->extCheck(false, "lines test (LN1) timed out in step '" + seq[std::min(cur, seq.size() - 1)].name + "'"); cur = seq.size(); }
    return true;
  }
  mode = 2;   // LN1's part is done: SelfTest's stages 4600+ (LN2, selftest_lines_ln2.cpp) run next and write report_lines.txt
  return false;
}
