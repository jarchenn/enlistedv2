// --selftest-textures (L4, docs/textures.md): the ambientCG texture layer.
//  * mapping: content/textures/textures.json loads, the building / prop / defence / path patterns have slots, material
//    names resolve to their codes (Material::texCode, vertex params.x = pattern + 100 * code)
//  * sets: every slot whose set folder exists is live on the GPU (units 20-22), sizes and load time reported
//  * look: building tiles and a terrain view rendered with the layer on and off: finite, brightness kept, and changed
//    only where sets are live; --legacy-look stays procedural
//  * missing-texture fallback: re-initialised from a folder with the mapping but no sets (and one corrupt set) the
//    layer is inactive, nothing crashes, and the tiles equal the procedural ones; then the real sets come back
#include "selftest.h"
#include "mptest.h"
#include "app.h"
#include "textures.h"
#include "thumbs.h"
#include <stb_image_write.h>
#include <cstdarg>
#include <fstream>

namespace {
MpTest* T = nullptr;
int mode = -1; bool started = false; int step = 0; float st = 0;
char B[600];
bool chk(bool ok, const char* f, ...) { va_list a; va_start(a, f); std::vsnprintf(B, sizeof B, f, a); va_end(a); return T->check(ok, B); }
void note(const char* f, ...) { va_list a; va_start(a, f); std::vsnprintf(B, sizeof B, f, a); va_end(a); T->note(B); logf("[textures] %s", B); }
fs::path outDir() { fs::path p = exeDir() / "selftest_out" / "textures"; std::error_code ec; fs::create_directories(p, ec); return p; }

std::vector<uint8_t> tile(App& a, const AssetDef* as, bool terrain = false) {
  GLuint t;
  if (terrain) {
    float y = a.terrain.height(0, 0);
    mat4 v = glm::lookAt(vec3(0.5f, y + 2.2f, 6.0f), vec3(0, y, 0), vec3(0, 1, 0));
    t = a.R.renderThumbnail({}, {}, vec3(-1), vec3(1), &a.terrain, &v);
  } else {
    std::vector<DrawItem> op{{&as->opaque, mat4(1)}, {&as->panelsIntact, mat4(1)}}, gl;
    t = a.R.renderThumbnail(op, gl, as->bmin, as->bmax);
  }
  std::vector<uint8_t> px((size_t)kThumbSize * kThumbSize * 4);
  glGetTextureImage(t, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)px.size(), px.data());
  glDeleteTextures(1, &t);
  return px;
}
void save(const std::vector<uint8_t>& px, const std::string& name) {
  stbi_write_png((outDir() / (name + ".png")).string().c_str(), kThumbSize, kThumbSize, 4, px.data(), kThumbSize * 4);
}
double meanLum(const std::vector<uint8_t>& p) { double s = 0; for (size_t i = 0; i < p.size(); i += 4) s += 0.2126 * p[i] + 0.7152 * p[i + 1] + 0.0722 * p[i + 2]; return s / (p.size() / 4); }
void diff(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, double& mean, int& mx) {
  mean = 0; mx = 0;
  for (size_t i = 0; i < a.size(); ++i) if (i % 4 != 3) { int d = std::abs((int)a[i] - (int)b[i]); mean += d; mx = std::max(mx, d); }
  mean /= a.size() * 3 / 4;
}
bool slotSetOnDisk(const std::string& slot) {
  // the set a slot names, from textures.json (the engine keeps only resolved layers)
  static json j = [] { try { return json::parse(readTextFile(contentDir() / "textures" / "textures.json")); } catch (...) { return json::object(); } }();
  if (!j.contains("slots") || !j["slots"].contains(slot)) return false;
  std::string s = j["slots"][slot].value("set", "");
  return fs::exists(contentDir() / "textures" / s / "color.jpg");
}
// does the asset draw any surface whose slot is live? (code 0: the pattern's slot, 1: none, 2+: slot)
bool usesLiveSlot(const AssetDef* as) {
  if (!as) return true;
  for (auto& m : as->materials) {
    int s = m.texCode == 0 ? tex::patternSlot(m.pattern) : m.texCode - 2;
    if (m.texCode != 1 && tex::slotLayer(s) >= 0) return true;
  }
  return false;
}
const char* kAssets[] = {"vil_norman_farmhouse", "house_stone_cottage", "vil_church_ruined"};

void run(App& a) {
  const tex::Info I = tex::info();   // a copy: the fallback step re-initialises the layer
  // ---- mapping
  chk(I.slots >= 40, "textures.json: %d slots", I.slots);
  int pats[] = {1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 17, 18, 19, 22, 25, 26, 29}, missingPat = 0;
  for (int p : pats) if (tex::patternSlot(p) < 0) { ++missingPat; note("pattern %d has no slot", p); }
  chk(missingPat == 0, "every building / prop / furniture / defence / path pattern family has a slot (%d checked)", (int)(sizeof pats / sizeof *pats));
  int turf = -1, wallpaper = -1, render = -1, explicitOk = 1;
  for (auto& as : a.lib.assets) for (auto& m : as->materials) {
    if (m.name == "turf" && turf < 0) turf = m.texCode;
    if (m.name == "wallpaper" && wallpaper < 0) wallpaper = m.texCode;
    if (m.name == "render" && render < 0) render = m.texCode;
  }
  chk(turf == tex::slotIndex("turf") + 2, "material 'turf' resolves to its slot (code %d)", turf);
  chk(wallpaper == 1, "material 'wallpaper' is procedural only (code %d)", wallpaper);
  chk(render == 0, "material 'render' takes its pattern's slot (code %d)", render);
  int c1 = tex::materialCode("x", 0, "none"), c2 = tex::materialCode("x", 0, "limewash");
  explicitOk = c1 == 1 && c2 == tex::slotIndex("limewash") + 2;
  chk(explicitOk, "a material's own \"texture\" field wins (none -> %d, limewash -> %d)", c1, c2);

  int paintOk = 0;   // painted ground (lasso, ground_paint.cpp): every type has a slot named paint_<type>
  for (int k = 0; k < tex::kPaintTypes; ++k) if (tex::slotIndex(std::string("paint_") + tex::kPaintTypeNames[k]) >= 0) ++paintOk; else note("paint type %s has no slot", tex::kPaintTypeNames[k]);
  chk(paintOk == tex::kPaintTypes, "painted-ground types have slots (%d of %d)", paintOk, tex::kPaintTypes);

  // ---- sets
  int onDisk = 0, liveOk = 0;
  for (const char* s : {"limewash", "brick", "wood", "slate", "concrete", "bunker", "metal", "fabric", "terrain_grass", "terrain_rock"}) {
    bool disk = slotSetOnDisk(s); onDisk += disk;
    int L = tex::slotLayer(tex::slotIndex(s));
    if (disk == (L >= 0)) ++liveOk; else note("slot %s: set on disk %d, layer %d", s, (int)disk, L);
  }
  chk(liveOk == 10, "slots are live exactly when their set is on disk (%d of 10 sampled slots have files)", onDisk);
  note("%d sets on the GPU (%s, %.1f MB), %d of %d slots live, loaded in %.0f ms", I.sets, I.compressed ? "BC3/BC5" : "uncompressed", I.bytes / 1048576.0, I.slotsLive, I.slots, I.ms);
  if (!I.missing.empty()) { std::string m; for (auto& s : I.missing) m += s + " "; note("sets named by slots but not on disk: %s", m.c_str()); }
  GLint units = 0; glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
  chk(units >= tex::kUnitBase + 3, "GL_MAX_TEXTURE_IMAGE_UNITS = %d (units %d-%d used)", units, tex::kUnitBase, tex::kUnitBase + 2);

  // ---- look: on / off
  struct Pair { std::string name; std::vector<uint8_t> on, off; bool live = true; };
  std::vector<Pair> P;
  bool any = tex::active();
  for (const char* id : kAssets) {
    const AssetDef* as = a.lib.find(id);
    if (!as) { chk(false, "asset %s exists", id); continue; }
    Pair p; p.name = id; p.live = usesLiveSlot(as);
    tex::setEnabled(true); p.on = tile(a, as);
    tex::setEnabled(false); p.off = tile(a, as);
    P.push_back(std::move(p));
  }
  { Pair p; p.name = "terrain"; p.live = tex::slotLayer(tex::slotIndex("terrain_grass")) >= 0 || tex::slotLayer(tex::slotIndex("terrain_soil")) >= 0; tex::setEnabled(true); p.on = tile(a, nullptr, true); tex::setEnabled(false); p.off = tile(a, nullptr, true); P.push_back(std::move(p)); }
  tex::setEnabled(true);
  for (auto& p : P) {
    save(p.on, p.name + "_on"); save(p.off, p.name + "_off");
    double m, lOn = meanLum(p.on), lOff = meanLum(p.off); int mx; diff(p.on, p.off, m, mx);
    chk(lOn > 8 && lOff > 8, "%s: tiles are not black (mean luma on %.1f, off %.1f)", p.name.c_str(), lOn, lOff);
    chk(lOn / std::max(lOff, 1.0) > 0.7 && lOn / std::max(lOff, 1.0) < 1.4, "%s: brightness kept within 30 %% (on / off %.2f)", p.name.c_str(), lOn / std::max(lOff, 1.0));
    if (any && p.live) chk(m > 0.1 || mx >= 3, "%s: the layer changes the look (mean abs diff %.2f / 255, max %d)", p.name.c_str(), m, mx);
    else chk(mx <= 1, "%s: no live set on its surfaces: identical to procedural (max diff %d)", p.name.c_str(), mx);
  }
  // legacy look stays procedural
  a.R.q.legacy = true;
  { const AssetDef* as = a.lib.find(kAssets[0]);
    if (as) { tex::setEnabled(true); auto on = tile(a, as); tex::setEnabled(false); auto off = tile(a, as); tex::setEnabled(true);
      double m; int mx; diff(on, off, m, mx); chk(mx <= 1, "--legacy-look ignores the layer (max diff %d)", mx); } }
  a.R.q.legacy = false;

  // ---- missing-texture fallback
  fs::path miss = outDir() / "missing_root";
  std::error_code ec; fs::remove_all(miss, ec); fs::create_directories(miss / "corrupt_set", ec);
  fs::copy_file(contentDir() / "textures" / "textures.json", miss / "textures.json", ec);
  std::ofstream(miss / "corrupt_set" / "color.jpg") << "not a jpeg";
  tex::init(miss);
  chk(!tex::active() && tex::info().sets == 0 && tex::info().slotsLive == 0, "missing sets: layer inactive (%d sets, %d live slots), corrupt set skipped", tex::info().sets, tex::info().slotsLive);
  chk(!tex::info().missing.empty(), "missing sets are reported (%d)", (int)tex::info().missing.size());
  int same = 0;
  for (auto& p : P) {
    std::vector<uint8_t> t = p.name == "terrain" ? tile(a, nullptr, true) : tile(a, a.lib.find(p.name));
    double m; int mx; diff(t, p.off, m, mx);
    if (mx <= 1) ++same; else note("%s: fallback differs from procedural (max %d, mean %.2f)", p.name.c_str(), mx, m);
  }
  chk(same == (int)P.size(), "missing sets fall back to the procedural patterns (%d of %d tiles identical)", same, (int)P.size());
  // one placeholder set: only its slots go live
  tex::writePlaceholders(miss, {"plaster_old"});
  tex::init(miss);
  int live = tex::info().slotsLive, expect = tex::slotLayer(tex::slotIndex("limewash")) >= 0;
  chk(tex::info().sets == 1 && live >= 1 && expect, "a lone set brings only its slots live (%d live)", live);
  tex::init(contentDir() / "textures");
  chk(tex::info().sets == I.sets || !any, "real sets restored (%d)", tex::info().sets);
  chk(a.R.shaderErrors == 0, "shader errors: %d", a.R.shaderErrors);
  std::ofstream(outDir() / "summary.json") << json{{"sets", tex::info().sets}, {"slots", tex::info().slots}, {"live", tex::info().slotsLive},
                                                    {"mb", tex::info().bytes / 1048576.0}, {"ms", tex::info().ms}, {"compressed", tex::info().compressed}}.dump(1);
}
}  // namespace

bool texturesSelfTest(App& app, float dt) {
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--selftest-textures") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) return true;
  if (!started) { started = true; T = new MpTest("textures"); }
  st += dt;
  if (step == 0 && st > 0.5f) { run(app); step = 1; }
  if (step == 1 && !app.selftest->done) { app.selftest->exitCode = T->finish(); app.selftest->done = true; }
  return true;
}
