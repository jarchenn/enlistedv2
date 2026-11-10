// --selftest-lines, LN2 part (stages 4600-4699; docs/lines.md#ln2): fence, wire, wall and hedge lines.
//  * a post-and-wire fence line with a field gate: a walking agent passes the open gate and is stopped by the fence;
//  * a blast cuts one bay of a wire line (its colliders go, the rest stay);
//  * save / reload keeps the lines, their spans and the gate;
//  * every default map loads with no fence / wire / wall / hedge pieces left (they became lines).
#include "selftest.h"
#include "lines.h"
#include "lasso.h"
#include "physics.h"
#include <cstdarg>

extern bool lineInsertSpan(App&, uint64_t, vec2, const std::string&, float);
extern bool lineReplacedPiece(const std::string&);

namespace {
std::string fmtL(const char* f, ...) { char b[600]; va_list a; va_start(a, f); std::vsnprintf(b, sizeof b, f, a); va_end(a); return b; }
struct L2 { uint64_t fence = 0, wire = 0, gate = 0; size_t tpl = 0; int leftover = 0, lines = 0, maps = 0; std::string bad; };
L2 g2;
uint64_t gateOf(App& app, uint64_t line) {
  for (auto& i : app.doc.instances) if (i.extra.is_object() && i.extra.contains("lineGate") && i.extra["lineGate"].value("line", (uint64_t)0) == line) return i.id;
  return 0;
}
// an agent walks from `a` toward `b` for up to 6 s; returns where it got to
vec3 walk(App& app, vec2 a, vec2 b) {
  vec3 feet(a.x, app.terrain.height(a.x, a.y) + 0.05f, a.y);
  int h = Physics::createAgent(feet);
  vec2 d = glm::normalize(b - a);
  for (int k = 0; k < 360; ++k) {
    vec3 p = Physics::agent(h).pos;
    if (glm::dot(vec2(p.x, p.z) - b, d) > 0) break;
    Physics::updateAgent(h, 1.0f / 60.0f, vec3(d.x, 0, d.y) * 2.0f, false, 0, 0.0f);
  }
  vec3 p = Physics::agent(h).pos;
  Physics::destroyAgent(h);
  return p;
}
}

void SelfTest::linesLn2Update(App& app, float dt) {
  (void)dt;
  auto go = [&](int s) { stage = s; stageTime = 0; };
  switch (stage) {
  case 4600: {
    reportName = reportName.empty() ? "lines" : reportName;
    app.newMap(*app.findTemplate("blank_slate"), "Lines LN2");
    g2 = L2();
    // a fence with a gate in the middle
    g2.fence = lineCreate(app, "post_wire_fence", {vec2(-20, 10), vec2(20, 10)}, json::object(), 7);
    Instance* f = app.doc.find(g2.fence);
    check(f && lineInsertSpan(app, g2.fence, vec2(0, 10), "field_gate", 3.8f), "a field gate inserted in the middle of a 40 m post-and-wire fence line");
    g2.wire = lineCreate(app, "wire_fence", {vec2(-12, -12), vec2(0, -14), vec2(12, -12)}, json::object(), 11);
    app.syncRuntime();
    g2.gate = gateOf(app, g2.fence);
    const Instance* gi = app.doc.find(g2.gate);
    check(gi && gi->assetId == "field_gate" && glm::length(vec2(gi->pos.x, gi->pos.z) - vec2(0, 10)) < 0.05f && !app.assetFor(*gi)->interactables.empty(),
          "the gate span has its field gate (an openable door) on the line");
    auto& rf = app.rt[g2.fence]; auto& rw = app.rt[g2.wire];
    int inGap = 0; for (auto& c : rf.line->colliders) { float wx = c.part.center.x + f->pos.x; if (std::fabs(wx) < 1.85f - c.part.size.x * 0.5f) ++inGap; }
    check(rf.line && rf.line->mesh.i.size() > 0 && rf.line->colliders.size() > 10 && inGap == 0,
          fmtL("fence line: %zu triangles, %zu colliders, none inside the gate span", rf.line->mesh.i.size() / 3, rf.line->colliders.size()));
    check(rw.line && rw.line->sections >= 4 && rw.secCut.size() == (size_t)rw.line->sections && !rw.line->rigid.empty(),
          fmtL("wire line: %d bays (damage sections), %zu model bays", rw.line ? rw.line->sections : 0, rw.line ? rw.line->rigid.size() : 0));
    go(4601);
    break;
  }
  case 4601: {
    if (stageTime < 0.5f) break;
    vec3 shut = walk(app, vec2(0, 6), vec2(0, 14));
    {   // open the gate (Explore animates doors; here the leaf is set open directly)
      Instance* gi = app.doc.find(g2.gate); auto& rg = app.rt[g2.gate];
      rg.progress[0] = 1.0f; rg.dir[0] = 0; app.updateLeafBodies(*gi, rg, 0);
    }
    vec3 through = walk(app, vec2(0, 6), vec2(0, 14)), blocked = walk(app, vec2(15.5f, 6), vec2(15.5f, 14));
    check(shut.z < 10.0f, fmtL("the closed gate stops an agent (reached z %.2f)", shut.z));
    check(through.z > 13.0f, fmtL("an agent walks through the open gate (z %.2f -> %.2f)", 6.0f, through.z));
    check(blocked.z < 10.0f, fmtL("the fence stops an agent beside the gate (reached z %.2f, the fence is at z 10)", blocked.z));
    // a blast on the wire line's middle bay
    auto& rw = app.rt[g2.wire];
    size_t cols0 = 0; for (auto& c : rw.line->colliders) cols0 += c.section >= 0;
    vec3 p(0, app.terrain.height(0, -14) + 0.5f, -14);
    app.blastDefenses(p, 400.0f, 2.0f);
    int cut = 0; for (uint8_t c : rw.secCut) cut += c;
    std::vector<Part> solid, player; extern void lineColliders(const InstanceRT&, std::vector<Part>&, std::vector<Part>&); lineColliders(rw, solid, player);
    check(cut >= 1 && cut < (int)rw.secCut.size() && solid.size() + player.size() < cols0,
          fmtL("a blast cuts %d of %zu wire bays; their colliders go (%zu of %zu left)", cut, rw.secCut.size(), solid.size() + player.size(), cols0));
    // save / reload
    json before = json::object(); for (auto& i : app.doc.instances) if (i.extra.is_object() && i.extra.contains("line")) before[std::to_string(i.id)] = i.extra["line"];
    fs::path tmp = userMapsDir() / "selftest_lines_ln2.nmap.json";
    bool ok = app.saveMapTo(tmp) && app.openMap(tmp);
    app.syncRuntime();
    json after = json::object(); for (auto& i : app.doc.instances) if (i.extra.is_object() && i.extra.contains("line")) after[std::to_string(i.id)] = i.extra["line"];
    check(ok && before == after && gateOf(app, g2.fence) != 0, fmtL("save / reload keeps %zu lines with their points and spans, and the gate", after.size()));
    g2.tpl = 0;
    go(4602);
    break;
  }
  case 4602: {   // every default map: no piece a line type replaces is left (single knife rests stay obstacles by design)
    if (g2.tpl >= app.templates.size()) {
      check(g2.leftover == 0 && g2.maps > 0, fmtL("%d default maps load with %d fence / wire / wall / hedge pieces left over (%d lines)%s", g2.maps, g2.leftover, g2.lines, g2.bad.c_str()));
      finish(app);
      break;
    }
    const TemplateInfo& t = app.templates[g2.tpl++];
    if (!fs::exists(contentDir() / "templates" / (t.id + ".nmap.json"))) break;
    app.newMap(t, "migration " + t.id);
    int left = 0, knife = 0, lines = 0;
    for (auto& i : app.doc.instances) {
      if (isLineAsset(app.assetFor(i))) ++lines;
      else if (lineReplacedPiece(i.assetId)) { if (i.assetId == "wire_knife_rest") ++knife; else ++left; }
    }
    g2.leftover += left; g2.lines += lines; ++g2.maps;
    if (left) g2.bad += "; " + t.id + ": " + std::to_string(left);
    logf("%s", fmtL("%s: %d lines, %d pieces left, %d single knife rests (obstacles)", t.id.c_str(), lines, left, knife).c_str());
    break;
  }
  }
}
