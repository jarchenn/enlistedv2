// Build-menu thumbnails (W2, MP & Creative update spec 9.4). Moved out of render.cpp / app.cpp; see thumbs.h and
// docs/thumbnails.md.
//
// Why tiles were blank: the old renderer built R.worldModels for assets with an imported model (Flak 18, the three
// barbed-wire assets) but never drew them, and it framed a bounding sphere at a fixed 3/4 view, so a tall thin asset
// (telegraph pole, 8 m x 0.25 m) filled a few percent of the tile.
//
// Framing. The camera looks at the centre of the asset bounds (which already include every box part, panel, model
// and canopy) from direction d(yaw, pitch). A 128 px probe render gives the silhouette from the depth buffer; the
// projection is then zoomed and shifted in clip space so the silhouette fills 88 % of the tile, centred:
//   x_clip' = s * (x_clip - c_x * w_clip),  s = 2 * 0.88 / max(w_ndc, h_ndc)        (same for y)
// The default view is yaw 37 deg, pitch 24 deg (the old normalize(0.75, 0.45, 1.0)). When its filled share of the
// tile is below kThumbMinFill the asset is thin or tall: 8 yaws x 4 pitches (20..65 deg) are probed and the one
// with the largest filled share (weighted down slightly for steep pitches) is used.
#include "thumbs.h"
#include "app.h"
#include <stb_image.h>
#include <stb_image_write.h>
#include <algorithm>
#include <cstring>

std::map<std::string, ThumbInfo> gThumbInfo;
std::map<std::string, std::string> gThumbSalt;

namespace {
GLint U(GLuint p, const char* n) { return glGetUniformLocation(p, n); }   // as in render.cpp
const vec3 kBgHdr(0.20f, 0.205f, 0.21f);   // neutral grey (linear HDR before the fixed-exposure tonemap)
const float kFov = 32.0f;
const int kProbe = 128;

uint64_t fnv(uint64_t h, const void* p, size_t n) {
  const unsigned char* b = (const unsigned char*)p;
  for (size_t k = 0; k < n; ++k) { h ^= b[k]; h *= 1099511628211ull; }
  return h;
}
uint64_t fnvStr(uint64_t h, const std::string& s) { uint64_t n = s.size(); h = fnv(h, &n, sizeof n); return fnv(h, s.data(), s.size()); }

// clip-space zoom / shift that maps the NDC box [x0,x1]x[y0,y1] onto the centre of the tile at `fill`
mat4 fitClip(float x0, float x1, float y0, float y1, float fill) {
  float s = 2.0f * fill / std::max(1e-4f, std::max(x1 - x0, y1 - y0));
  float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
  mat4 m(1);
  m[0][0] = s; m[1][1] = s;
  m[3][0] = -s * cx; m[3][1] = -s * cy;   // times w_clip (column 3 multiplies w)
  return m;
}
}

std::string footprintLabel(vec2 fp) {
  // at least one decimal; under 0.1 m two, so a thin asset (a 4 cm picture frame) never reads 0.0
  auto num = [](float v) { char s[16]; std::snprintf(s, sizeof s, v < 0.095f ? "%.2f" : "%.1f", v); return std::string(s); };
  char b[64]; std::snprintf(b, sizeof b, "%s \xC3\x97 %s m", num(fp.x).c_str(), num(fp.y).c_str());   // U+00D7 multiplication sign
  return b;
}

fs::path thumbCacheDir() {
  if (const char* e = std::getenv("NMK_THUMB_CACHE")) if (*e) return fs::path(e);
  return exeDir() / "cache" / "thumbs";
}

std::string assetThumbKey(const AssetDef& a) {
  uint64_t h = 1469598103934665603ull;
  h = fnvStr(h, kThumbVersion);
  std::string src = readTextFile(contentDir() / "assets" / a.file);
  h = fnvStr(h, src);
  try {   // baked model: its description, mesh and texture files
    json j = json::parse(src);
    if (j.contains("model")) {
      fs::path dir = contentDir() / "assets" / "models" / j["model"].value("dir", a.id);
      h = fnvStr(h, readTextFile(dir / "model.json"));
      h = fnvStr(h, readTextFile(dir / "mesh.bin"));
      std::vector<std::pair<std::string, uintmax_t>> files; std::error_code ec;
      for (auto& e : fs::directory_iterator(dir, ec)) if (e.is_regular_file()) files.push_back({e.path().filename().string(), e.file_size()});
      std::sort(files.begin(), files.end());
      for (auto& [n, sz] : files) { h = fnvStr(h, n); h = fnv(h, &sz, sizeof sz); }
    }
  } catch (...) {}
  auto it = gThumbSalt.find(a.id); if (it != gThumbSalt.end()) h = fnvStr(h, it->second);
  char b[20]; std::snprintf(b, sizeof b, "%016llx", (unsigned long long)h);
  return b;
}

// ---------------------------------------------------------------- renderer side (moved from render.cpp)
GLuint Renderer::renderThumbnail(const std::vector<DrawItem>& opaque, const std::vector<DrawItem>& glass, vec3 bmin, vec3 bmax, const Terrain* terr, const mat4* customView) {
  // map templates (terrain views) and the legacy path: sky background, fixed 3/4 sphere framing
  const int S = kThumbSize;
  GLuint outTex, outFbo;
  glCreateTextures(GL_TEXTURE_2D, 1, &outTex); glTextureStorage2D(outTex, 1, GL_RGBA8, S, S);
  glTextureParameteri(outTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(outTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glCreateFramebuffers(1, &outFbo); glNamedFramebufferTexture(outFbo, GL_COLOR_ATTACHMENT0, outTex, 0);
  vec3 c = (bmin + bmax) * 0.5f; float r = glm::length(bmax - bmin) * 0.5f;
  RenderView v;
  v.camPos = c + glm::normalize(vec3(0.75f, 0.45f, 1.0f)) * r * 2.1f;
  v.view = customView ? *customView : glm::lookAt(v.camPos, c, vec3(0, 1, 0));
  if (customView) v.camPos = vec3(glm::inverse(*customView)[3]);
  v.proj = glm::perspective(radians(32.0f), 1.0f, 0.1f, 2000.0f);
  thumbMode = true;
  mat4 vp = v.proj * v.view;
  glBindFramebuffer(GL_FRAMEBUFFER, thumbHdrFbo);
  glViewport(0, 0, S, S);
  glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  setLighting(progSky, v);
  glUniformMatrix4fv(U(progSky, "uInvVP"), 1, GL_FALSE, glm::value_ptr(glm::inverse(vp)));
  glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE);
  glBindVertexArray(emptyVao); glDrawArrays(GL_TRIANGLES, 0, 3);
  glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
  if (terr) {
    setLighting(progTerrain, v);
    glUniformMatrix4fv(U(progTerrain, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    glUniform1i(U(progTerrain, "uGrid"), 0);
    terr->draw(progTerrain);
  }
  setLighting(progMain, v);
  glUniformMatrix4fv(U(progMain, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
  drawItems(progMain, opaque, false);
  if (!foliage.empty()) { setLighting(progLeaf, v); drawFoliage(progLeaf, vp); }
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
  setLighting(progGlass, v);
  glUniformMatrix4fv(U(progGlass, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
  drawItems(progGlass, glass, false);
  glDepthMask(GL_TRUE); glDisable(GL_BLEND);
  glBindFramebuffer(GL_FRAMEBUFFER, outFbo);
  glDisable(GL_DEPTH_TEST);
  glUseProgram(progTone);
  glBindTextureUnit(0, thumbHdr); glBindTextureUnit(1, expoTex[0]);
  glUniform1i(U(progTone, "uHdr"), 0); glUniform1i(U(progTone, "uExpo"), 1);
  glUniform1f(U(progTone, "uFixedExpo"), 1.1f);
  glUniform1f(U(progTone, "uBloomAmt"), 0.0f);   // the main frame's bloom setting must not leak into tiles
  glBindVertexArray(emptyVao); glDrawArrays(GL_TRIANGLES, 0, 3);
  glEnable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &outFbo);
  thumbMode = false;
  return outTex;
}

GLuint Renderer::renderAssetThumbnail(const std::vector<DrawItem>& opaque, const std::vector<DrawItem>& glass, vec3 bmin, vec3 bmax, float thinPx, ThumbInfo* info) {
  const int S = kThumbSize;
  vec3 c = (bmin + bmax) * 0.5f;
  float r = std::max(0.05f, glm::length(bmax - bmin) * 0.5f);
  float dist = r / std::sin(radians(kFov) * 0.5f) * 1.15f;
  mat4 baseProj = glm::perspective(radians(kFov), 1.0f, std::max(0.01f, dist - r * 1.5f), dist + r * 1.5f);
  vec3 savedSun = sunDir; float savedPx = pxAngle;
  std::vector<ModelDraw> models = worldModels;
  for (auto& m : models) m.thinPx = thinPx;

  auto viewFor = [&](float yawDeg, float pitchDeg, vec3& eye) {
    float y = radians(yawDeg), p = radians(pitchDeg);
    vec3 d(std::sin(y) * std::cos(p), std::sin(p), std::cos(y) * std::cos(p));
    eye = c + d * dist;
    return glm::lookAt(eye, c, vec3(0, 1, 0));
  };
  // one pass into the HDR thumbnail target at n x n: neutral background, camera-fixed key light, no shadows / fog
  auto pass = [&](const mat4& view, const mat4& proj, vec3 eye, int n) {
    RenderView v; v.view = view; v.proj = proj; v.camPos = eye;
    mat4 inv = glm::inverse(view);
    vec3 right = vec3(inv[0]), up = vec3(inv[1]), back = vec3(inv[2]);
    sunDir = glm::normalize(-right * 0.55f + up * 0.85f + back * 0.45f);   // key from upper left, in front
    pxAngle = radians(kFov) / n / std::max(1.0f, proj[0][0] / baseProj[0][0]);   // per pixel after the clip zoom
    mat4 vp = proj * view;
    glBindFramebuffer(GL_FRAMEBUFFER, thumbHdrFbo);
    glViewport(0, 0, n, n);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDepthFunc(GL_LESS); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glClearColor(kBgHdr.r, kBgHdr.g, kBgHdr.b, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    setLighting(progMain, v);
    glUniformMatrix4fv(U(progMain, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    drawItems(progMain, opaque, false);
    if (!models.empty()) { setLighting(progModel, v); glUniformMatrix4fv(U(progModel, "uVP"), 1, GL_FALSE, glm::value_ptr(vp)); drawModels(models); }
    if (!foliage.empty()) { setLighting(progLeaf, v); drawFoliage(progLeaf, vp); }
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
    setLighting(progGlass, v);
    glUniformMatrix4fv(U(progGlass, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
    drawItems(progGlass, glass, false);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
  };
  // silhouette of the last pass: covered share and NDC bounds (depth < 1)
  struct Sil { float fill = 0; float x0 = 1, x1 = -1, y0 = 1, y1 = -1; bool any = false; };
  auto silhouette = [&](int n) {
    std::vector<float> d((size_t)n * n);
    glGetTextureSubImage(thumbDepth, 0, 0, 0, 0, n, n, 1, GL_DEPTH_COMPONENT, GL_FLOAT, (GLsizei)(d.size() * 4), d.data());
    Sil s; int cnt = 0;
    for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) {
      if (d[(size_t)y * n + x] >= 1.0f) continue;
      ++cnt; s.any = true;
      float nx0 = x * 2.0f / n - 1, nx1 = (x + 1) * 2.0f / n - 1, ny0 = y * 2.0f / n - 1, ny1 = (y + 1) * 2.0f / n - 1;
      s.x0 = std::min(s.x0, nx0); s.x1 = std::max(s.x1, nx1); s.y0 = std::min(s.y0, ny0); s.y1 = std::max(s.y1, ny1);
    }
    s.fill = cnt / float(n * n);
    return s;
  };
  // framed fill of a view: probe at bounds framing, fit the silhouette, probe again
  thumbMode = true;
  auto framed = [&](float yaw, float pitch, mat4& view, mat4& proj, vec3& eye) {
    view = viewFor(yaw, pitch, eye);
    pass(view, baseProj, eye, kProbe);
    Sil s = silhouette(kProbe);
    proj = s.any ? fitClip(s.x0, s.x1, s.y0, s.y1, 0.88f) * baseProj : baseProj;
    pass(view, proj, eye, kProbe);
    return silhouette(kProbe).fill;
  };
  float yaw = 37.0f, pitch = 24.0f;   // atan(0.75, 1.0), asin(0.45 / |(0.75, 0.45, 1)|)
  mat4 view, proj; vec3 eye;
  float fill = framed(yaw, pitch, view, proj, eye);
  bool searched = false;
  if (fill < kThumbMinFill && (!opaque.empty() || !models.empty() || !foliage.empty())) {
    searched = true;
    float best = fill;
    for (float p : {20.0f, 35.0f, 50.0f, 65.0f})
      for (int k = 0; k < 8; ++k) {
        float yw = 37.0f + 45.0f * k; mat4 v2, p2; vec3 e2;
        float f = framed(yw, p, v2, p2, e2);
        float w = p <= 35.0f ? 1.0f : p <= 50.0f ? 0.92f : 0.82f;   // prefer the gentler views on a near tie
        if (f * w > best) { best = f * w; yaw = yw; pitch = p; fill = f; }
      }
    framed(yaw, pitch, view, proj, eye);
  }
  // final tile
  pass(view, proj, eye, S);
  fill = silhouette(S).fill;
  GLuint outTex, outFbo;
  glCreateTextures(GL_TEXTURE_2D, 1, &outTex); glTextureStorage2D(outTex, 1, GL_RGBA8, S, S);
  glTextureParameteri(outTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(outTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glCreateFramebuffers(1, &outFbo); glNamedFramebufferTexture(outFbo, GL_COLOR_ATTACHMENT0, outTex, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, outFbo);
  glViewport(0, 0, S, S);
  glDisable(GL_DEPTH_TEST);
  glUseProgram(progTone);
  glBindTextureUnit(0, thumbHdr); glBindTextureUnit(1, expoTex[0]);
  glUniform1i(U(progTone, "uHdr"), 0); glUniform1i(U(progTone, "uExpo"), 1);
  glUniform1f(U(progTone, "uFixedExpo"), 1.1f);
  glUniform1f(U(progTone, "uBloomAmt"), 0.0f);
  glUniform1i(U(progTone, "uTonemap"), 1);
  glBindVertexArray(emptyVao); glDrawArrays(GL_TRIANGLES, 0, 3);
  glEnable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &outFbo);
  thumbMode = false;
  sunDir = savedSun; pxAngle = savedPx;
  if (info) { info->fill = fill; info->yaw = yaw; info->pitch = pitch; info->searched = searched; }
  return outTex;
}

// ---------------------------------------------------------------- app side (moved from App::makeThumbnails)
namespace {
bool loadTile(const fs::path& p, GLuint& tex) {
  int w, h, ch;
  stbi_uc* d = stbi_load(p.string().c_str(), &w, &h, &ch, 4);
  if (!d) return false;
  if (w != kThumbSize || h != kThumbSize) { stbi_image_free(d); return false; }
  std::vector<uint8_t> flip((size_t)w * h * 4);   // PNG rows top-down; GL textures bottom-up
  for (int y = 0; y < h; ++y) std::memcpy(&flip[(size_t)y * w * 4], d + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
  stbi_image_free(d);
  glCreateTextures(GL_TEXTURE_2D, 1, &tex); glTextureStorage2D(tex, 1, GL_RGBA8, w, h);
  glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTextureSubImage2D(tex, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, flip.data());
  return true;
}
std::vector<uint8_t> readTile(GLuint tex) {
  std::vector<uint8_t> px((size_t)kThumbSize * kThumbSize * 4);
  glGetTextureImage(tex, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)px.size(), px.data());
  return px;
}
void saveTile(const fs::path& p, GLuint tex) {
  std::vector<uint8_t> px = readTile(tex), flip(px.size());
  const int S = kThumbSize;
  for (int y = 0; y < S; ++y) std::memcpy(&flip[(size_t)y * S * 4], &px[(size_t)(S - 1 - y) * S * 4], (size_t)S * 4);
  for (size_t k = 3; k < flip.size(); k += 4) flip[k] = 255;
  std::error_code ec; fs::create_directories(p.parent_path(), ec);
  stbi_write_png(p.string().c_str(), S, S, 4, flip.data(), S * 4);
}
std::vector<uint8_t> gBg;   // the empty tile (background + vignette), for the blank-tile measure
}

ThumbStats makeAssetThumbnails(App& app) {
  ThumbStats st;
  Renderer& R = app.R;
  fs::path dir = thumbCacheDir();
  std::error_code ec; fs::create_directories(dir, ec);
  if (gBg.empty()) {   // reference: a tile with nothing in it
    R.foliage.clear(); R.worldModels.clear();
    GLuint t = R.renderAssetThumbnail({}, {}, vec3(-1), vec3(1), 0, nullptr);
    gBg = readTile(t); glDeleteTextures(1, &t);
  }
  for (auto& a : app.lib.assets) {
    // vehicles: their tile is drawn by TankSys::makeThumbnails (tank_ui.cpp) from the baked hull / turret / tracks
    if (a->raw.is_object() && a->raw.contains("vehicle")) { ++st.external; continue; }
    ThumbInfo info; info.key = assetThumbKey(*a);
    fs::path file = dir / (a->id + "-" + info.key + ".png");
    if (a->thumbnail) { glDeleteTextures(1, &a->thumbnail); a->thumbnail = 0; }
    if (fs::exists(file, ec) && loadTile(file, a->thumbnail)) { info.cached = true; ++st.loaded; gThumbInfo[a->id] = info; continue; }
    std::vector<DrawItem> op{{&a->opaque, mat4(1)}, {&a->panelsIntact, mat4(1)}}, gl;
    if (a->glass.count) gl.push_back({&a->glass, mat4(1)});
    for (auto& d : a->interactables)
      for (auto& lf : d.leaves) {
        mat4 L = glm::translate(mat4(1), lf.hinge) * glm::mat4_cast(glm::angleAxis(radians(lf.sign * (d.defaultOpen ? d.openAngle : 0)), d.axis));
        op.push_back({&lf.opaque, L});
        if (lf.glass.count) gl.push_back({&lf.glass, L});
      }
    R.foliage.clear(); if (a->tree.on) R.foliage.push_back({&a->tree, mat4(1)});
    R.worldModels.clear();
    if (a->model) for (size_t k = 0; k < a->model->parts.size(); ++k) R.worldModels.push_back({a->model.get(), (int)k, mat4(1)});
    // thin wire: at least 1.2 px wide in the tile (the world uses the asset's own thinLinesPx at range)
    // Box-part wire (fence strands: 8 mm on an 8 m fence = 0.26 px) gets the same: a tile-only copy of every visible
    // box part thinner than ~1.5 px of the framed bounds, thickened to that, drawn with the normal mesh.
    float thin = a->modelThinPx > 0 ? std::max(1.2f, a->modelThinPx) : 0.0f;
    float minT = glm::length(a->bmax - a->bmin) / kThumbSize * 1.5f;
    MeshData fat; GpuMesh fatMesh;
    for (auto& p : a->parts) {
      if (!p.visible || p.shape != Part::BOX || p.mat < 0 || p.mat >= (int)a->materials.size()) continue;
      if (std::min(p.size.x, std::min(p.size.y, p.size.z)) >= minT) continue;
      Part q = p; q.size = glm::max(q.size, vec3(minT));
      appendPart(fat, q, a->materials[p.mat], mat4(1));
    }
    if (!fat.i.empty()) { fatMesh.upload(fat); op.push_back({&fatMesh, mat4(1)}); }
    a->thumbnail = R.renderAssetThumbnail(op, gl, a->bmin, a->bmax, thin, &info);
    if (!fat.i.empty()) fatMesh.destroy();
    R.worldModels.clear(); R.foliage.clear();
    ++st.rendered; st.searched += info.searched;
    for (auto& e : fs::directory_iterator(dir, ec)) {   // drop this asset's stale tiles (older keys)
      std::string n = e.path().filename().string();
      if (n.rfind(a->id + "-", 0) == 0 && n.size() == a->id.size() + 1 + 16 + 4 && e.path() != file) fs::remove(e.path(), ec);
    }
    saveTile(file, a->thumbnail);
    gThumbInfo[a->id] = info;
  }
  logf("thumbnails: %d rendered (%d with a view search), %d from the cache %s, %d vehicles drawn by TankSys", st.rendered, st.searched, st.loaded, dir.string().c_str(), st.external);
  return st;
}

float thumbBackgroundShare(App& app, GLuint tex) {
  (void)app;
  if (!tex || gBg.empty()) return 1.0f;
  std::vector<uint8_t> px = readTile(tex);
  size_t bg = 0, n = px.size() / 4;
  for (size_t k = 0; k < n; ++k) {
    int d = std::abs(px[k * 4] - gBg[k * 4]) + std::abs(px[k * 4 + 1] - gBg[k * 4 + 1]) + std::abs(px[k * 4 + 2] - gBg[k * 4 + 2]);
    if (d <= 6) ++bg;
  }
  return bg / float(n);
}

void saveThumbPng(GLuint tex, const fs::path& p) { saveTile(p, tex); }
bool sameThumb(GLuint a, GLuint b) { return a && b && readTile(a) == readTile(b); }
