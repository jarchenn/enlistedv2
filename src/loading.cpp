// UI2 loading screen (loading.h). Composition: the map's thumbnail (or the menu backdrop, blurred) graded dark and cold,
// pushed in slowly, fire light from below and rising embers; the emblem and BATTLE FOUNDRY large in the middle, the map
// name and mode beneath; a slim progress bar with the stage label and percentage near the bottom, and a tip line that
// changes every 6 s.
// Progress is real: a job (loadingBegin) expects n stages; each LoadStep that ends moves the bar one stage, and a
// stage that reports its own fraction (the navigation bake) fills its share smoothly. The bar never reaches 100 % before
// loadingEnd. Frames are presented from inside the work on a second ImGui context (shared font atlas), at most 30 a
// second plus one per stage change, so the window keeps answering and never shows a frozen frame.
// NMK_UI2_LOADSHOT=1: each stage change also writes selftest_out/ui2_load_<k>.png (captures).
#include "loading.h"
#include "mainmenu.h"
#include "logo.h"
#include "app.h"
#include "crumb.h"
#include "game.h"
#include "platform_gfx.h"
#include <imgui.h>
#include <stb_image_write.h>
#include <stb_image.h>
#include <map>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <thread>

namespace {
struct Job {
  int depth = 0, expected = 1, done = 0; float sub = 0, shown = 0;
  std::string title, subtitle, label = "Loading";
  unsigned thumb = 0, still = 0; double t0 = 0, lastPresent = -1, endedAt = -10; bool fading = false;   // still: the map's 1080p loading still (UI4)
  int shot = 0;
  const char* step = "";   // the internal LoadStep name (watchdog lines)
  const char* crumbAtPresent = "-";   // the crumb when the last frame went out: a stall lies between it and the next one
} J;
ImGuiContext* gCtx = nullptr;   // the loading screen's own context (shares the main font atlas)

inline ImU32 rgba(int r, int g, int b, float a) { return IM_COL32(r, g, b, (int)std::clamp(a * 255.0f, 0.0f, 255.0f)); }
const char* friendly(const char* w) {
  struct M { const char* k; const char* v; };
  static const M m[] = {{"openMap", "Reading the map"}, {"syncRuntime", "Building the world"}, {"enterExplore syncRuntime", "Building the world"},
                        {"autoFurnishAll", "Furnishing buildings"}, {"applyTemplate", "Shaping the terrain"}, {"newMap content", "Placing the battlefield"},
                        {"enterExplore match start", "Deploying the teams"}};
  for (auto& e : m) if (!std::strcmp(e.k, w)) return e.v;
  return w;
}
const char* const kTips[] = {
  "Esc opens the menu, the controls and the settings.", "Hold Tab for the scoreboard.", "M opens the full map; the corner map can turn with you (Settings > Interface).",
  "G throws a frag grenade, H a smoke grenade.", "Q and E lean around corners.", "Fog hides you from the AI as well as from your eyes.",
  "In Create Map, F5 play-tests from the start marker.", "Drag the soldier on the Character screen to turn him.",
  "Gunfire has its own volume slider under Settings > Audio.", "F12 shows the developer overlay: frame time and debug lines."};
const int kTipN = (int)(sizeof kTips / sizeof kTips[0]);

void drawLoading(ImDrawList* dl, float W, float H, float a, float t) {
  const float s = H / 1080.0f;
  dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), rgba(4, 6, 7, a));
  // UI4 backdrop: the map itself, clearly recognisable (its 1080p loading still, else its thumbnail, else the menu's
  // street), a slow push-in, under a dark neutral glass (~50 %) and a soft, wide vignette at the sides
  const bool still = J.still != 0;
  unsigned tex = still ? J.still : J.thumb ? J.thumb : menuBackdropTex(false);
  if (tex) {
    const float z = 1.03f + 0.02f * std::min(t / 12.0f, 1.0f);
    ImVec2 uv0(0.5f - 0.5f / z, 0.5f - 0.5f / z), uv1(0.5f + 0.5f / z, 0.5f + 0.5f / z);
    if (!still && J.thumb) { uv0.y = 0.8f - (0.8f - 0.5f) * (1 - 1 / z); uv1.y = 0.2f + (0.5f - 0.2f) * (1 - 1 / z); }   // thumbnails are stored bottom-up (as the editor draws them)
    dl->AddImage((ImTextureID)(intptr_t)tex, ImVec2(0, 0), ImVec2(W, H), uv0, uv1, rgba(235, 232, 228, a));
  }
  dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), rgba(4, 4, 4, (still ? 0.50f : 0.56f) * a));   // the black glass
  dl->AddRectFilledMultiColor(ImVec2(0, H * 0.6f), ImVec2(W, H), rgba(255, 110, 30, 0), rgba(255, 110, 30, 0), rgba(255, 96, 24, 0.10f * a), rgba(255, 96, 24, 0.10f * a));   // a little fire light
  for (int k = 0; k < 2; ++k) {   // the side vignette in two widths, blended (no band edge)
    const float w = W * (k ? 0.42f : 0.20f), al = (k ? 0.40f : 0.32f) * a;
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(w, H), rgba(0, 0, 0, al), rgba(0, 0, 0, 0), rgba(0, 0, 0, 0), rgba(0, 0, 0, al));
    dl->AddRectFilledMultiColor(ImVec2(W - w, 0), ImVec2(W, H), rgba(0, 0, 0, 0), rgba(0, 0, 0, al), rgba(0, 0, 0, al), rgba(0, 0, 0, 0));
  }
  dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, H * 0.25f), rgba(0, 0, 0, 0.35f * a), rgba(0, 0, 0, 0.35f * a), rgba(0, 0, 0, 0), rgba(0, 0, 0, 0));
  dl->AddRectFilledMultiColor(ImVec2(0, H * 0.72f), ImVec2(W, H), rgba(0, 0, 0, 0), rgba(0, 0, 0, 0), rgba(0, 0, 0, 0.55f * a), rgba(0, 0, 0, 0.55f * a));
  // embers: stateless (position from time), so presents at any rate move them correctly
  for (int i = 0; i < 90; ++i) {
    uint32_t h = (uint32_t)i * 2654435761u; auto r = [&](int k) { uint32_t x = h ^ (uint32_t)(k * 0x9E3779B9u); x ^= x >> 15; x *= 2246822519u; x ^= x >> 13; return (x & 0xffff) / 65535.0f; };
    const float life = 4 + 5 * r(1), age = std::fmod(t + life * r(2), life), k = age / life;
    const float x = W * (0.05f + 0.95f * r(3)) + (10 + 30 * r(4)) * s * age + std::sin(t * 1.4f + 6.28f * r(5)) * 12 * s;
    const float y = H * (0.95f + 0.1f * r(6)) - (40 + 80 * r(7)) * s * age, sz = (1.0f + 1.8f * r(8)) * s;
    const float al = std::sin(k * 3.14159f) * (0.6f + 0.4f * std::sin(t * 9 + i)) * a;
    dl->AddCircleFilled(ImVec2(x, y), sz * 3.2f, rgba(255, 120, 30, 0.11f * al));
    dl->AddCircleFilled(ImVec2(x, y), sz, rgba(255, 180, 96, al));
  }
  // a few faster sparks: short bright streaks rising quickly from the fire light, with a little turbulence (UI3)
  for (int i = 0; i < 14; ++i) {
    uint32_t h = (uint32_t)(i + 101) * 2654435761u; auto r = [&](int k) { uint32_t x = h ^ (uint32_t)(k * 0x9E3779B9u); x ^= x >> 15; x *= 2246822519u; x ^= x >> 13; return (x & 0xffff) / 65535.0f; };
    const float life = 1.2f + 1.3f * r(1), age = std::fmod(t + life * r(2), life), k = age / life;
    const float vy = (220 + 260 * r(3)) * s, vx = (40 + 90 * r(4)) * s * (r(5) < 0.3f ? -0.5f : 1.0f);
    const float x = W * (0.1f + 0.85f * r(6)) + vx * age + std::sin(t * 7 + 6.28f * r(7)) * 6 * s, y = H * 1.02f - vy * age;
    const float al = (1 - k) * std::min(1.0f, k * 8) * (0.7f + 0.3f * std::sin(t * 23 + i)) * a, len = (6 + 10 * r(8)) * s;
    const float n = std::sqrt(vx * vx + vy * vy); const ImVec2 tail(x - vx / n * len, y + vy / n * len);
    dl->AddLine(tail, ImVec2(x, y), rgba(255, 200, 120, al), std::max(1.0f, 1.3f * s));
  }
  // the logo (the user's artwork, content/ui/logo.png; else the vector emblem with the wordmark between rules), map name, mode
  const float cx = W * 0.5f, top = H * 0.24f, eh = 230 * s;
  const float la = logoImageAspect();
  if (la > 0) { const float lw = std::min(W * 0.62f, 1150 * s), lh = lw / la; drawLogo(dl, ImVec2(cx - lw * 0.5f, H * 0.42f - lh), lh, a, true); }
  else drawLogo(dl, ImVec2(cx - eh * 0.43f, top), eh, a, false);
  ImFont* fd = uiFontDisplay(); ImFont* fh = uiFontHead(); ImFont* fb = ImGui::GetFont();
  auto centred = [&](ImFont* f, float px, float y, ImU32 c, const std::string& txt, float track) {
    float w = 0; for (char ch : txt) w += f->CalcTextSizeA(px, 1e9f, 0, &ch, &ch + 1).x + track;
    w -= track; float x = cx - w * 0.5f;
    for (char ch : txt) { dl->AddText(f, px, ImVec2(x, y), c, &ch, &ch + 1); x += f->CalcTextSizeA(px, 1e9f, 0, &ch, &ch + 1).x + track; }
    return w; };
  const float wy = la > 0 ? H * 0.42f + 40 * s - 104 * s * 1.18f : top + eh + 30 * s, wpx = 104 * s;   // with the image: the lines below line up under it
  float ww = 0;   // a soft shadow under the wordmark, then the letters
  if (la <= 0) { const float o = 3 * s; const int v0 = dl->VtxBuffer.Size; centred(fd, wpx, wy + o, rgba(0, 0, 0, 0.55f * a), "BATTLE FOUNDRY", wpx * 0.05f); for (int k = v0; k < dl->VtxBuffer.Size; ++k) dl->VtxBuffer[k].pos.x += o * 0.5f; }
  if (la <= 0) {
    ww = centred(fd, wpx, wy, rgba(236, 232, 224, a), "BATTLE FOUNDRY", wpx * 0.05f);
    dl->AddRectFilled(ImVec2(cx - ww * 0.5f, wy - 8 * s), ImVec2(cx + ww * 0.5f, wy - 6.5f * s), rgba(236, 232, 224, 0.8f * a));
    dl->AddRectFilled(ImVec2(cx - ww * 0.5f, wy + wpx * 1.18f), ImVec2(cx + ww * 0.5f, wy + wpx * 1.18f + 1.5f * s), rgba(236, 232, 224, 0.8f * a));
  }
  std::string T = J.title; for (auto& c : T) c = (char)std::toupper((unsigned char)c);
  if (!T.empty()) centred(uiFontTitle(), 58 * s, wy + wpx * 1.18f + 30 * s, rgba(236, 232, 224, 0.97f * a), T, 58 * s * 0.03f);   // UI4: the heavy condensed title face
  std::string Sb = J.subtitle; for (auto& c : Sb) c = (char)std::toupper((unsigned char)c);
  centred(fh, 17 * s, wy + wpx * 1.18f + (T.empty() ? 44 : 100) * s, rgba(214, 210, 200, 0.70f * a), Sb, 17 * s * 0.16f);
  // progress: label, percent, slim bar, tip
  const float x0 = W * 0.30f, x1 = W * 0.70f, by = H - 112 * s;
  std::string L = J.label; for (auto& c : L) c = (char)std::toupper((unsigned char)c);
  float lx = x0; for (char ch : L) { dl->AddText(fb, 15 * s, ImVec2(lx, by - 28 * s), rgba(236, 232, 224, 0.8f * a), &ch, &ch + 1); lx += fb->CalcTextSizeA(15 * s, 1e9f, 0, &ch, &ch + 1).x + 2.4f * s; }
  char pc[16]; std::snprintf(pc, sizeof pc, "%d%%", (int)std::lround(J.shown * 100));
  ImVec2 ps = fb->CalcTextSizeA(15 * s, 1e9f, 0, pc); dl->AddText(fb, 15 * s, ImVec2(x1 - ps.x, by - 28 * s), rgba(236, 232, 224, 0.8f * a), pc);
  dl->AddRectFilled(ImVec2(x0, by), ImVec2(x1, by + 3 * s), rgba(255, 255, 255, 0.12f * a), 1.5f * s);
  dl->AddRectFilled(ImVec2(x0, by), ImVec2(x0 + (x1 - x0) * J.shown, by + 3 * s), rgba(240, 118, 32, a), 1.5f * s);
  dl->AddCircleFilled(ImVec2(x0 + (x1 - x0) * J.shown, by + 1.5f * s), 6 * s, rgba(255, 150, 60, 0.18f * a));   // the bar's glowing head
  const int tip = (int)(t / 6.0f) % kTipN; const float tf = std::min({1.0f, std::fmod(t, 6.0f) / 0.4f, (6.0f - std::fmod(t, 6.0f)) / 0.4f});
  std::string tt = std::string("TIP   ") + kTips[tip];
  ImVec2 ts = fb->CalcTextSizeA(16 * s, 1e9f, 0, tt.c_str()); dl->AddText(fb, 16 * s, ImVec2(cx - ts.x * 0.5f, by + 26 * s), rgba(214, 214, 206, 0.62f * a * tf), tt.c_str());
}

void present(App& app, bool force) {
  const double now = glfwGetTime();
  if (!force && now - J.lastPresent < 1.0 / 30.0) return;
  if (!app.win) return;
  {   // watchdog: a stretch of the load that presented nothing (the window stops answering and the embers freeze) is named in the log
    static const double thr = [] { const char* e = std::getenv("NMK_LOAD_GAPS"); return e && *e ? std::atof(e) : 1.0; }();
    if (J.lastPresent > 0 && now - J.lastPresent > thr) logf("loading: no frame for %.2f s during \"%s\" (step %s, between crumbs %s .. %s)", now - J.lastPresent, J.label.c_str(), J.step, J.crumbAtPresent, gCrumb ? gCrumb : "-");
    J.crumbAtPresent = gCrumb ? gCrumb : "-";
  }
  const float dt = J.lastPresent < 0 ? 1.0f / 60.0f : (float)std::min(0.1, now - J.lastPresent);
  J.lastPresent = now;
  glfwPollEvents();   // the window keeps answering ("Not responding" after 5 s otherwise)
  // UI3: a frame can now be presented from inside any long loop (crumb / loadingTick): leave the caller's GL state as it was
  GLint fbDraw = 0, fbRead = 0, vp[4] = {0, 0, 0, 0}; GLfloat cc[4] = {0, 0, 0, 0}; GLboolean scis = glIsEnabled(GL_SCISSOR_TEST), depthT = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbDraw); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &fbRead); glGetIntegerv(GL_VIEWPORT, vp); glGetFloatv(GL_COLOR_CLEAR_VALUE, cc);
  ImGuiContext* prev = ImGui::GetCurrentContext();
  ImFontAtlas* atlas = ImGui::GetIO().Fonts;
  if (!gCtx) { gCtx = ImGui::CreateContext(atlas); ImGui::SetCurrentContext(gCtx); ImGui::GetIO().IniFilename = nullptr; platformImGuiInitRenderer(); }
  ImGui::SetCurrentContext(gCtx);
  int fw, fh; glfwGetFramebufferSize(app.win, &fw, &fh);
  ImGuiIO& io = ImGui::GetIO(); io.DisplaySize = ImVec2((float)std::max(fw, 1), (float)std::max(fh, 1)); io.DeltaTime = dt;
  const float target = std::min(0.97f, (J.done + J.sub) / (float)std::max(1, J.expected));
  J.shown += (std::max(J.shown, target) - J.shown) * std::min(1.0f, dt / 0.12f);
  platformImGuiRendererNewFrame(); ImGui::NewFrame();
  drawLoading(ImGui::GetForegroundDrawList(), io.DisplaySize.x, io.DisplaySize.y, 1.0f, (float)(now - J.t0));
  ImGui::Render();
  glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, fw, fh); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
  platformImGuiRender(ImGui::GetDrawData());
  if (std::getenv("NMK_UI2_LOADSHOT") && force && J.shot < 16) {   // captures: one picture per stage change
    std::vector<uint8_t> px((size_t)fw * fh * 4), fl(px.size());
    glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadBuffer(GL_BACK); glReadPixels(0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < fh; ++y) std::memcpy(&fl[(size_t)y * fw * 4], &px[(size_t)(fh - 1 - y) * fw * 4], (size_t)fw * 4);
    fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec);
    stbi_write_png((p / ("ui2_load_" + std::to_string(J.shot++) + ".png")).string().c_str(), fw, fh, 4, fl.data(), fw * 4);
  }
  platformPresent(app.win);
  ImGui::SetCurrentContext(prev);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)fbDraw); glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)fbRead); glViewport(vp[0], vp[1], vp[2], vp[3]); glClearColor(cc[0], cc[1], cc[2], cc[3]);
  auto en = [](GLenum c, GLboolean on) { if (on) glEnable(c); else glDisable(c); }; en(GL_SCISSOR_TEST, scis); en(GL_DEPTH_TEST, depthT); en(GL_BLEND, blend); en(GL_CULL_FACE, cull);
}
}  // namespace

// UI4: content/templates/<id>.loading.jpg, loaded once per map
unsigned stillFor(const std::string& id) {
  static std::map<std::string, unsigned> cache;
  if (id.empty()) return 0;
  auto it = cache.find(id); if (it != cache.end()) return it->second;
  unsigned tex = 0; int w, h, c;
  if (stbi_uc* d = stbi_load((contentDir() / "templates" / (id + ".loading.jpg")).string().c_str(), &w, &h, &c, 4)) {
    glCreateTextures(GL_TEXTURE_2D, 1, &tex); glTextureStorage2D(tex, 1, GL_RGBA8, w, h); glTextureSubImage2D(tex, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, d);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR); stbi_image_free(d);
  }
  return cache[id] = tex;
}
void loadingBegin(App& app, const std::string& title, const std::string& sub, int expectedStages, unsigned thumbTex, const std::string& mapId) {
  if (J.depth++ > 0) { J.expected = std::max(J.expected, J.done + expectedStages); return; }   // nested: the outer job grows
  gLoadingDepth = J.depth;
  mainMenuLeaveForLoad(gGame && gGame->pendingStart);   // UI3: loading a map / starting a match from the menu leaves it (the loading screen covers the work)
  J.expected = std::max(1, expectedStages); J.done = 0; J.sub = 0; J.shown = 0; J.title = title; J.subtitle = sub; J.label = "Loading";
  J.thumb = thumbTex; J.still = stillFor(mapId); J.t0 = glfwGetTime(); J.lastPresent = -1; J.fading = false;
  present(app, true);
}
void loadingStage(const char* what) { if (J.depth <= 0 || !gApp) return; J.step = what; J.label = friendly(what); J.sub = 0; present(*gApp, true); }
int gLoadingDepth = 0;
static const std::thread::id gMainThread = std::this_thread::get_id();   // the GL context and the window belong to it
void loadingTick() { if (J.depth > 0 && gApp && std::this_thread::get_id() == gMainThread) present(*gApp, false); }   // from crumb() and the long loops: a frame (and the message pump) every 33 ms
void loadingStageDone() { if (J.depth <= 0 || !gApp) return; J.done = std::min(J.done + 1, J.expected); J.sub = 0; present(*gApp, false); }
void loadingProgress(const char* label, float f) { if (J.depth <= 0 || !gApp) return; const bool change = J.label != label; J.label = label; J.sub = std::clamp(f, 0.0f, 1.0f); present(*gApp, change); }
void loadingEnd() {
  if (J.depth <= 0) return;
  gLoadingDepth = --J.depth;
  if (J.depth > 0) return;
  J.done = J.expected; J.sub = 0; J.endedAt = glfwGetTime(); J.fading = true;
}
bool loadingActive() { return J.depth > 0; }

void loadingOverlay(App& app) {
  if (!J.fading) return;
  const float k = (float)(glfwGetTime() - J.endedAt) / 0.45f;   // full for a moment at 100 %, then a fade into the game
  if (k >= 1) { J.fading = false; return; }
  J.shown = 1.0f;
  ImGuiIO& io = ImGui::GetIO(); (void)app;
  drawLoading(ImGui::GetForegroundDrawList(), io.DisplaySize.x, io.DisplaySize.y, std::clamp(1.6f - 1.6f * k, 0.0f, 1.0f), (float)(glfwGetTime() - J.t0));
}
