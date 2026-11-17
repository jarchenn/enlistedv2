// UI2 main menu. Shown at launch (not in self-tests or when a map path is given), and from File > Main menu / the pause
// menu. Everything is drawn on ImGui's background list in screen pixels scaled by s = height / 1080, over:
//  - the backdrop: content/ui/menu_backdrop.jpg, a still of our own map (Newtown) graded dark, with the game's own soldier
//    in it. When the local-only cut-out content/ui/local/menu_soldier.png exists (the supplied city soldier, licence
//    unknown, git-ignored), the empty still content/ui/menu_backdrop_empty.jpg is drawn and the cut-out stands in it.
//    A slow push-in and drift keep it alive.
//  - two layers of drifting fog (a tileable noise texture made here), rising embers, a vignette and edge shading.
//  - the logo top left (logo.cpp), the left menu, a glass panel on the right for Play / Character / Credits.
// Keys: Up / Down (W / S) pick, Enter / Space / Right open, Esc / Left / Backspace back. The mouse hovers and clicks.
#include "mainmenu.h"
#include "logo.h"
#include "app.h"
#include "settings.h"
#include "mpui.h"
#include "combat.h"
#include "character.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <cstdlib>

namespace {
const ImU32 kAccent = IM_COL32(240, 118, 32, 255);   // the logo orange: the one accent
inline ImU32 rgba(int r, int g, int b, float a) { return IM_COL32(r, g, b, (int)std::clamp(a * 255.0f, 0.0f, 255.0f)); }
inline ImU32 alphaOf(ImU32 c, float a) { return (c & 0x00ffffffu) | ((ImU32)std::clamp((float)(c >> 24) * a, 0.0f, 255.0f) << 24); }
inline float ease(float t) { t = std::clamp(t, 0.0f, 1.0f); return 1 - (1 - t) * (1 - t) * (1 - t); }
inline ImVec2 V(float x, float y) { return ImVec2(x, y); }

struct Img { GLuint tex = 0; int w = 0, h = 0; ImVec2 uv0{0, 0}, uv1{1, 1}; };
Img upload(const uint8_t* px, int w, int h, bool repeatX = false) {
  Img m; m.w = w; m.h = h;
  glCreateTextures(GL_TEXTURE_2D, 1, &m.tex); glTextureStorage2D(m.tex, 1, GL_RGBA8, w, h);
  glTextureSubImage2D(m.tex, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
  glTextureParameteri(m.tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(m.tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(m.tex, GL_TEXTURE_WRAP_S, repeatX ? GL_REPEAT : GL_CLAMP_TO_EDGE); glTextureParameteri(m.tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return m;
}
// the backdrop and an eighth-size box-blurred copy (the glass panels show it through)
bool loadBackdrop(const fs::path& p, Img& img, Img& blur) {
  int w, h, c; stbi_uc* d = stbi_load(p.string().c_str(), &w, &h, &c, 4);
  if (!d) return false;
  img = upload(d, w, h);
  const int bw = std::max(1, w / 8), bh = std::max(1, h / 8);
  std::vector<float> a((size_t)bw * bh * 3, 0.0f), b;
  for (int y = 0; y < bh * 8 && y < h; ++y) for (int x = 0; x < bw * 8 && x < w; ++x)
    for (int k = 0; k < 3; ++k) a[((size_t)(y / 8) * bw + x / 8) * 3 + k] += d[((size_t)y * w + x) * 4 + k] / 64.0f;
  stbi_image_free(d);
  for (int pass = 0; pass < 3; ++pass) {   // three 5x5 box passes ~ a gaussian of a few cells
    b = a;
    for (int y = 0; y < bh; ++y) for (int x = 0; x < bw; ++x) for (int k = 0; k < 3; ++k) {
      float s = 0; int n = 0;
      for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) { int xx = std::clamp(x + dx, 0, bw - 1), yy = std::clamp(y + dy, 0, bh - 1); s += b[((size_t)yy * bw + xx) * 3 + k]; ++n; }
      a[((size_t)y * bw + x) * 3 + k] = s / n;
    }
  }
  std::vector<uint8_t> o((size_t)bw * bh * 4);
  for (size_t i = 0; i < (size_t)bw * bh; ++i) { for (int k = 0; k < 3; ++k) o[i * 4 + k] = (uint8_t)std::clamp(a[i * 3 + k], 0.0f, 255.0f); o[i * 4 + 3] = 255; }
  blur = upload(o.data(), bw, bh);
  return true;
}
// a transparent cut-out, its uv window cropped to the opaque pixels
bool loadCutout(const fs::path& p, Img& img) {
  int w, h, c; stbi_uc* d = stbi_load(p.string().c_str(), &w, &h, &c, 4);
  if (!d) return false;
  int x0 = w, y0 = h, x1 = 0, y1 = 0;
  for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) if (d[((size_t)y * w + x) * 4 + 3] > 8) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
  for (size_t i = 0; i < (size_t)w * h; ++i) {   // graded into the street: half the colour, cooler
    uint8_t* q = d + i * 4; float l = 0.2126f * q[0] + 0.7152f * q[1] + 0.0722f * q[2];
    const float k[3] = {0.92f, 1.0f, 1.04f};
    for (int c = 0; c < 3; ++c) q[c] = (uint8_t)std::clamp((l + (q[c] - l) * 0.45f) * k[c], 0.0f, 255.0f);
  }
  img = upload(d, w, h); stbi_image_free(d);
  if (x1 <= x0 || y1 <= y0) return false;
  img.uv0 = V((float)x0 / w, (float)y0 / h); img.uv1 = V((float)(x1 + 1) / w, (float)(y1 + 1) / h);
  return true;
}
// tileable fbm fog (alpha, white), fading in from its top edge
Img makeFog() {
  const int W = 512, H = 256;
  auto hash = [](int x, int y, int s) { uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)s * 2246822519u; h = (h ^ (h >> 13)) * 1274126177u; return ((h ^ (h >> 16)) & 0xffff) / 65535.0f; };
  auto noise = [&](float x, float y, int per, int s) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y); float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    auto g = [&](int a, int b) { return hash(((a % per) + per) % per, b, s); };
    return (g(xi, yi) * (1 - fx) + g(xi + 1, yi) * fx) * (1 - fy) + (g(xi, yi + 1) * (1 - fx) + g(xi + 1, yi + 1) * fx) * fy;
  };
  std::vector<uint8_t> px((size_t)W * H * 4);
  for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
    float u = (float)x / W, v = (float)y / H, f = 0, amp = 0.55f; int per = 4;
    for (int o = 0; o < 5; ++o, per *= 2, amp *= 0.5f) f += amp * noise(u * per, v * per * 0.5f, per, o);
    float a = std::clamp((f - 0.35f) * 1.9f, 0.0f, 1.0f);
    a *= std::clamp(v / 0.45f, 0.0f, 1.0f);   // soft top edge
    size_t i = ((size_t)y * W + x) * 4; px[i] = px[i + 1] = px[i + 2] = 255; px[i + 3] = (uint8_t)(a * 255);
  }
  return upload(px.data(), W, H, true);
}
Img makeVignette() {
  const int N = 256; std::vector<uint8_t> px((size_t)N * N * 4);
  for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
    float u = (x + 0.5f) / N - 0.5f, v = (y + 0.5f) / N - 0.5f, d = std::sqrt(u * u * 1.0f + v * v * 1.6f) * 2.0f;
    float a = std::pow(std::clamp((d - 0.42f) / 0.85f, 0.0f, 1.0f), 1.5f) * 0.95f;
    size_t i = ((size_t)y * N + x) * 4; px[i] = px[i + 1] = px[i + 2] = 0; px[i + 3] = (uint8_t)(a * 255);
  }
  return upload(px.data(), N, N);
}

struct Ember { float x, y, vx, vy, age, life, size, phase; };
enum Page { Main, Play, Character, Credits };
struct Item { const char* label; const char* hint; };
const Item kItems[] = {
  {"CREATE MAP", "Build and edit battlefields"}, {"PLAY", "Singleplayer and LAN matches"}, {"CHARACTER", "Your soldier and loadout"},
  {"SETTINGS", "Graphics, audio, gameplay, interface"}, {"CONTROLS", "Keys and mouse"}, {"CREDITS", "Libraries, fonts and models"}, {"QUIT", "Leave Battle Foundry"}};
const int kN = (int)(sizeof kItems / sizeof kItems[0]);

struct State {
  bool open = false, loaded = false, launched = false, showControls = false, soldierLocal = false;
  int sel = 0, sel2 = 0, focus = 0; Page page = Main;
  double openedAt = 0, pageAt = -10; float hl[kN] = {};
  Img bg, bgBlur, soldier, fog, vig;
  std::vector<Ember> embers; uint32_t rng = 12345;
  ImVec2 uvC{0.5f, 0.5f}, uvH{0.5f, 0.5f};   // the backdrop's current uv window (centre, half extent), for the glass
  float W = 1920, H = 1080, s = 1;
} S;
float frand() { S.rng = S.rng * 1664525u + 1013904223u; return (S.rng >> 8) / 16777216.0f; }

void ensureLoaded() {
  if (S.loaded) return;
  S.loaded = true;
  const fs::path ui = contentDir() / "ui";
  if (!std::getenv("NMK_UI2_STANDIN") && loadCutout(ui / "local" / "menu_soldier.png", S.soldier) && loadBackdrop(ui / "menu_backdrop_empty.jpg", S.bg, S.bgBlur)) S.soldierLocal = true;
  else { S.soldierLocal = false; if (!loadBackdrop(ui / "menu_backdrop.jpg", S.bg, S.bgBlur)) logf("menu: %s missing (plain dark backdrop)", (ui / "menu_backdrop.jpg").string().c_str()); }
  S.fog = makeFog(); S.vig = makeVignette();
}

float trackedW(ImFont* f, float px, const char* t, float track) { float w = 0; for (const char* c = t; *c; ++c) w += f->CalcTextSizeA(px, 1e9f, 0, c, c + 1).x + track; return t[0] ? w - track : 0; }
void tracked(ImDrawList* dl, ImFont* f, float px, ImVec2 p, ImU32 col, const char* t, float track) {
  for (const char* c = t; *c; ++c) { dl->AddText(f, px, p, col, c, c + 1); p.x += f->CalcTextSizeA(px, 1e9f, 0, c, c + 1).x + track; }
}

// ---------------------------------------------------------------- character preview (the turning player body)
struct Preview { std::string id; std::unique_ptr<CharModel> m; CharInstance ci; bool ok = false; GLuint tex = 0; int w = 0, h = 0; double next = 0; float yaw = 20; int weapon = -2; } P;   // yaw 20: starts facing the viewer
std::vector<std::pair<std::string, std::string>> bodies() {   // id, label (soldier bodies only: no creatures)
  std::vector<std::pair<std::string, std::string>> v{{"", "By faction (default)"}};
  std::error_code ec;
  for (auto& e : fs::directory_iterator(contentDir() / "characters", ec)) {
    if (e.path().extension() != ".glb") continue;
    std::string id = e.path().stem().string();
    if (id.find("zombie") != std::string::npos || id == "creature" || id == "spider" || id == "troll") continue;
    std::string label = id == "allied_ranger_0" ? "US Ranger" : id == "allied_ranger_2" ? "US Ranger (pack)" : id == "allied_ranger_4" ? "US Ranger (radio)" :
                        id == "axis_grenadier" ? "German grenadier" : id == "operator_allied" ? "Allied operator" : id == "operator_axis" ? "Axis operator" :
                        id == "br_marine" ? "Marine (Battle Royale)" : id;
    v.push_back({id, label});
  }
  std::sort(v.begin() + 1, v.end(), [](auto& a, auto& b) { return a.second < b.second; });
  return v;
}
std::vector<int> primaries(App& app) {
  std::vector<int> v;
  for (size_t k = 0; k < app.weaponDefs.size(); ++k) { const WeaponDef& d = *app.weaponDefs[k]; if (app.isSmallArm(d) && !d.equipment() && !d.modern && !d.pack.on && d.action != "tool") v.push_back((int)k); }
  std::sort(v.begin(), v.end(), [&](int a, int b) { return app.weaponDefs[a]->name < app.weaponDefs[b]->name; });
  return v;
}
void drawPreview(App& app, ImDrawList* dl, ImVec2 mn, ImVec2 mx, float a) {
  std::string id = settings().s.character.empty() ? "allied_ranger_0" : settings().s.character;
  if (P.id != id) {
    if (P.ok) P.ci.destroy();
    P.ok = false; P.id = id; P.m = std::make_unique<CharModel>(); std::string e;
    if (initSkinning(e) && P.m->load(contentDir() / "characters" / (id + ".glb"), e) && P.ci.create(*P.m)) P.ok = true;
    else logf("menu: character preview %s: %s", id.c_str(), e.c_str());
    P.next = 0;
  }
  if (!P.ok) return;
  ImGui::SetCursorScreenPos(mn); ImGui::InvisibleButton("##charturn", V(std::max(1.0f, mx.x - mn.x), std::max(1.0f, mx.y - mn.y)));
  P.yaw += ImGui::IsItemActive() ? ImGui::GetIO().MouseDelta.x * 0.6f : ImGui::GetIO().DeltaTime * 14.0f;   // drag to turn, else a slow spin
  const int w = std::clamp((int)(mx.x - mn.x), 16, 900), h = std::clamp((int)(mx.y - mn.y), 16, 1400);
  int wi = app.weaponIndex(settings().s.primary.empty() ? "m1_garand" : settings().s.primary);
  if (ImGui::GetTime() >= P.next || w != P.w || h != P.h || wi != P.weapon) {
    P.next = ImGui::GetTime() + 0.05; P.weapon = wi;
    PoseParams pp; pp.carry = 1; pp.holdWeapon = wi >= 0;
    const WeaponDef* d = wi >= 0 ? app.weaponDefs[wi].get() : nullptr;
    if (d) { pp.gripR = d->gripR; pp.gripL = d->gripL; pp.magPos = d->gripL + vec3(0, -0.1f, -0.12f); }
    PoseResult pr; poseCharacter(*P.m, pp, pr); P.ci.skin(pr.skin);
    std::vector<ModelDraw> md; std::vector<DrawItem> boxes;
    mat4 world = glm::rotate(mat4(1), glm::radians(P.yaw), vec3(0, 1, 0));
    for (int k = 0; k < (int)P.ci.view.parts.size(); ++k) md.push_back({&P.ci.view, k, world});
    if (d && d->useModel()) { mat4 W = world * pr.weapon; for (int p = 0; p < 8; ++p) for (int k : d->worldParts()[p]) md.push_back({d->worldModel(), k, W}); }
    float tv = std::tan(glm::radians(15.0f)), dist = std::max(1.08f / tv, 0.45f / (tv * (float)w / h)) + 0.4f;
    vec3 c(0, 0.93f, 0), eye(0, 1.05f, dist);
    std::vector<uint8_t> px;
    if (app.R.renderModelSheet(md, boxes, glm::lookAt(eye, c, vec3(0, 1, 0)), glm::perspective(glm::radians(30.0f), (float)w / h, 0.1f, 40.0f), eye, w, h, px, true)) {
      if (!P.tex || w != P.w || h != P.h) {
        if (P.tex) glDeleteTextures(1, &P.tex);
        glCreateTextures(GL_TEXTURE_2D, 1, &P.tex); glTextureStorage2D(P.tex, 1, GL_RGBA8, w, h);
        glTextureParameteri(P.tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTextureParameteri(P.tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        P.w = w; P.h = h;
      }
      glTextureSubImage2D(P.tex, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    }
  }
  // a soft floor glow under the feet, then the body
  const float cx = (mn.x + mx.x) * 0.5f, fy = mn.y + (mx.y - mn.y) * 0.93f;
  for (int i = 6; i >= 1; --i) dl->AddEllipseFilled(V(cx, fy), V(i * 0.045f * (mx.x - mn.x), i * 0.008f * (mx.y - mn.y)), rgba(0, 0, 0, 0.09f * a));
  if (P.tex) dl->AddImage((ImTextureID)(intptr_t)P.tex, mn, V(mn.x + P.w, mn.y + P.h), V(0, 0), V(1, 1), rgba(255, 255, 255, a));
}
} // namespace

void menuGlass(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float a) {
  const float s = S.s, R = 18 * s;
  for (int i = 1; i <= 6; ++i) { float g = i * 3 * s; dl->AddRectFilled(V(mn.x - g, mn.y - g + 6 * s), V(mx.x + g, mx.y + g + 6 * s), rgba(0, 0, 0, 0.045f * a * (7 - i) / 6.0f), R + g); }
  const bool low = settings().s.gfx == presetOpts(GfxPreset::Low);
  if (!low && S.bgBlur.tex) {   // the blurred backdrop through the glass
    auto uv = [&](ImVec2 p) { return V(S.uvC.x + (p.x / S.W - 0.5f) * 2 * S.uvH.x, S.uvC.y + (p.y / S.H - 0.5f) * 2 * S.uvH.y); };
    dl->AddImageRounded((ImTextureID)(intptr_t)S.bgBlur.tex, mn, mx, uv(mn), uv(mx), rgba(205, 212, 218, a), R);
    dl->AddRectFilled(mn, mx, rgba(12, 15, 18, 0.50f * a), R);
  } else dl->AddRectFilled(mn, mx, rgba(16, 19, 22, 0.86f * a), R);   // Low: a tinted panel
  dl->AddRectFilledMultiColor(V(mn.x + R * 0.5f, mn.y + 1), V(mx.x - R * 0.5f, mn.y + (mx.y - mn.y) * 0.35f), rgba(255, 255, 255, 0.05f * a), rgba(255, 255, 255, 0.05f * a), rgba(255, 255, 255, 0), rgba(255, 255, 255, 0));
  dl->AddRect(V(mn.x + 0.5f, mn.y + 0.5f), V(mx.x - 0.5f, mx.y - 0.5f), rgba(255, 255, 255, 0.13f * a), R, 0, std::max(1.0f, s));
  dl->AddLine(V(mn.x + R, mn.y + 1.0f), V(mx.x - R, mn.y + 1.0f), rgba(255, 255, 255, 0.30f * a), std::max(1.0f, s));
}

void mainMenuOpen(App& app) {
  (void)app;
  S.open = true; S.page = Main; S.focus = 0; S.openedAt = ImGui::GetTime(); S.launched = false;
}

bool mainMenuActive() { return S.open; }

static void activate(App& app, int k) {
  switch (k) {
    case 0: S.open = false; break;   // Create Map: the editor with the current (blank) map
    case 1: S.page = Play; S.focus = 1; S.sel2 = 0; S.pageAt = ImGui::GetTime(); break;
    case 2: S.page = Character; S.focus = 1; S.sel2 = 0; S.pageAt = ImGui::GetTime(); break;
    case 3: settings().open = true; break;
    case 4: S.showControls = true; break;
    case 5: S.page = Credits; S.focus = 0; S.pageAt = ImGui::GetTime(); break;
    case 6: glfwSetWindowShouldClose(app.win, GLFW_TRUE); break;
  }
}

void mainMenuFrame(App& app) {
  if (!S.open) {   // a match started from the menu came back to its setup / join window: back to the menu's Play page
    if (S.launched && app.mode == Mode::Creative) { if (gUi.showSetup || gUi.showJoin) { S.open = true; S.page = Play; S.focus = 1; S.pageAt = ImGui::GetTime(); } S.launched = false; }
    return;
  }
  if (app.mode == Mode::Explore) { S.open = false; S.launched = true; return; }   // a match / Explore session started
  ensureLoaded();
  ImGuiIO& io = ImGui::GetIO();
  S.W = io.DisplaySize.x; S.H = io.DisplaySize.y; S.s = S.H / 1080.0f;
  const float W = S.W, H = S.H, s = S.s, t = (float)ImGui::GetTime(), dt = io.DeltaTime;
  const float fadeIn = ease((t - (float)S.openedAt) / 0.9f);
  ImDrawList* dl = ImGui::GetBackgroundDrawList();

  // ---- backdrop: cover-fit, a slow push-in and drift
  dl->AddRectFilled(V(0, 0), V(W, H), IM_COL32(6, 8, 9, 255));
  if (S.bg.tex) {
    const float ia = (float)S.bg.w / S.bg.h, sa = W / H, z = 1.07f + 0.025f * std::sin(t * 0.045f);
    S.uvH = sa > ia ? V(0.5f / z, 0.5f * ia / sa / z) : V(0.5f * sa / ia / z, 0.5f / z);
    S.uvC = V(0.5f + 0.012f * std::sin(t * 0.031f), 0.5f + 0.006f * std::sin(t * 0.023f + 1));
    dl->AddImage((ImTextureID)(intptr_t)S.bg.tex, V(0, 0), V(W, H), V(S.uvC.x - S.uvH.x, S.uvC.y - S.uvH.y), V(S.uvC.x + S.uvH.x, S.uvC.y + S.uvH.y), rgba(255, 255, 255, fadeIn));
  }
  // ---- the supplied soldier (local-only), standing centre-left, slightly more parallax than the street
  static float soldierA = 1; soldierA += ((S.page == Character ? 0.0f : 1.0f) - soldierA) * std::min(1.0f, io.DeltaTime / 0.08f);
  if (S.soldierLocal && soldierA > 0.01f) {
    const float ch = H * 0.74f, cw = ch * (S.soldier.uv1.x - S.soldier.uv0.x) * S.soldier.w / ((S.soldier.uv1.y - S.soldier.uv0.y) * S.soldier.h);
    const float cx = W * 0.43f - (S.uvC.x - 0.5f) * W * 0.6f, fy = H * 0.965f;
    for (int i = 7; i >= 1; --i) dl->AddEllipseFilled(V(cx + 6 * s, fy - 4 * s), V(cw * 0.11f * i, 7.0f * s * i * 0.5f), rgba(0, 0, 0, 0.07f * fadeIn * soldierA));   // contact shadow
    dl->AddImage((ImTextureID)(intptr_t)S.soldier.tex, V(cx - cw * 0.5f, fy - ch), V(cx + cw * 0.5f, fy), S.soldier.uv0, S.soldier.uv1, rgba(150, 156, 160, fadeIn * soldierA));   // into the street's light
  }
  // ---- fog (two layers drifting at different speeds), embers, shading
  if (S.fog.tex) {
    const float u0 = t * 0.0045f, u1 = -t * 0.0075f;
    dl->AddImage((ImTextureID)(intptr_t)S.fog.tex, V(0, H * 0.42f), V(W, H), V(u0, 0), V(u0 + 1.1f, 1), rgba(150, 162, 168, 0.30f * fadeIn));
    dl->AddImage((ImTextureID)(intptr_t)S.fog.tex, V(0, H * 0.58f), V(W, H * 1.04f), V(u1 + 0.3f, 0), V(u1 + 2.1f, 1), rgba(130, 142, 150, 0.36f * fadeIn));
  }
  {
    const float dtc = std::min(dt, 0.1f);
    const bool first = S.embers.empty();
    while ((int)S.embers.size() < 70) {
      Ember e; e.x = W * (0.25f + 0.8f * frand()); e.y = H * (0.75f + 0.35f * frand()); e.vx = (8 + 34 * frand()) * s; e.vy = -(26 + 70 * frand()) * s;
      e.life = 4 + 6 * frand(); e.size = (0.9f + 1.8f * frand()) * s; e.phase = 6.28f * frand();
      e.age = first ? e.life * frand() : 0;   // the first batch starts mid-flight
      if (first) { e.x += e.vx * e.age; e.y += e.vy * e.age; }
      S.embers.push_back(e);
    }
    for (auto& e : S.embers) {
      e.age += dtc; e.x += (e.vx + std::sin(t * 1.3f + e.phase) * 14 * s) * dtc; e.y += e.vy * dtc;
      const float k = e.age / e.life, a = std::sin(std::min(k, 1.0f) * 3.14159f) * (0.65f + 0.35f * std::sin(t * 9 + e.phase * 3)) * fadeIn;
      dl->AddCircleFilled(V(e.x, e.y), e.size * 3.2f, rgba(255, 120, 30, 0.10f * a));
      dl->AddCircleFilled(V(e.x, e.y), e.size, rgba(255, 176, 92, 0.95f * a));
    }
    S.embers.erase(std::remove_if(S.embers.begin(), S.embers.end(), [&](const Ember& e) { return e.age > e.life || e.y < -20; }), S.embers.end());
  }
  if (S.vig.tex) dl->AddImage((ImTextureID)(intptr_t)S.vig.tex, V(0, 0), V(W, H));
  dl->AddRectFilledMultiColor(V(0, 0), V(W * 0.46f, H), rgba(4, 6, 7, 0.78f), rgba(4, 6, 7, 0), rgba(4, 6, 7, 0), rgba(4, 6, 7, 0.78f));   // the menu side
  dl->AddRectFilledMultiColor(V(0, 0), V(W, H * 0.26f), rgba(4, 6, 7, 0.55f), rgba(4, 6, 7, 0.55f), rgba(4, 6, 7, 0), rgba(4, 6, 7, 0));    // a darker sky
  dl->AddRectFilledMultiColor(V(0, H * 0.80f), V(W, H), rgba(4, 6, 7, 0), rgba(4, 6, 7, 0), rgba(4, 6, 7, 0.70f), rgba(4, 6, 7, 0.70f));

  // ---- logo
  drawLogo(dl, V(72 * s, 56 * s), 128 * s, fadeIn);

  // ---- input
  const bool overlay = settings().open || S.showControls || gUi.showSetup || gUi.showJoin;   // their own windows take the keys
  auto key = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
  const bool up = key(ImGuiKey_UpArrow) || key(ImGuiKey_W), down = key(ImGuiKey_DownArrow) || key(ImGuiKey_S);
  const bool enter = key(ImGuiKey_Enter) || key(ImGuiKey_KeypadEnter) || key(ImGuiKey_Space), right = key(ImGuiKey_RightArrow);
  const bool back = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || key(ImGuiKey_LeftArrow) || key(ImGuiKey_Backspace);
  if (!overlay && !io.WantCaptureKeyboard) {
    if (S.focus == 0) {
      if (up) S.sel = (S.sel + kN - 1) % kN;
      if (down) S.sel = (S.sel + 1) % kN;
      if (enter || (right && (S.sel == 1 || S.sel == 2))) activate(app, S.sel);
      if (back && S.page != Main) S.page = Main;
    } else if (back) { S.focus = 0; S.page = Main; }
  }

  // ---- left menu
  ImFont* fh = uiFontHead(); ImFont* fb = ImGui::GetFont();
  const float px = 25 * s, track = px * 0.16f, x0 = 76 * s, y0 = H * 0.40f, dy = 60 * s;
  for (int k = 0; k < kN; ++k) {
    const float y = y0 + k * dy;
    const ImVec2 rmn(x0 - 20 * s, y - 12 * s), rmx(x0 + 360 * s, y + px + 12 * s);
    const bool hover = !overlay && !io.WantCaptureMouse && ImGui::IsMouseHoveringRect(rmn, rmx, false);
    if (hover && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) { S.sel = k; S.focus = 0; }
    if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { S.sel = k; S.focus = 0; activate(app, k); }
    const bool on = S.sel == k;
    S.hl[k] += ((on ? 1.0f : 0.0f) - S.hl[k]) * std::min(1.0f, dt / 0.06f);   // ~150 ms ease
    const float h = S.hl[k], a = fadeIn * (overlay ? 0.45f : 1.0f);
    const float tx = x0 + 22 * s + 8 * s * h;
    if (h > 0.01f) dl->AddRectFilled(V(x0, y + px * 0.12f), V(x0 + 3 * s, y + px * 0.98f), alphaOf(kAccent, h * a));
    tracked(dl, fh, px, V(tx, y), rgba(236, 232, 224, (0.50f + 0.50f * h) * a), kItems[k].label, track);
    if (h > 0.02f) dl->AddText(fb, 15 * s, V(tx, y + px * 1.22f), rgba(200, 200, 194, 0.62f * h * a), kItems[k].hint);
  }
  // ---- footer: version, key hints
  {
    char ver[96]; snprintf(ver, sizeof ver, "BATTLE FOUNDRY   PRE-ALPHA   BUILD %s", __DATE__);
    tracked(dl, fb, 13 * s, V(x0, H - 44 * s), rgba(220, 220, 214, 0.42f * fadeIn), ver, 1.2f * s);
    const char* hint = S.focus == 0 ? "UP / DOWN  SELECT      ENTER  OPEN      ESC  BACK" : "UP / DOWN  SELECT      ENTER  CHOOSE      ESC  BACK";
    float hw = trackedW(fb, 13 * s, hint, 1.2f * s);
    tracked(dl, fb, 13 * s, V(W - 72 * s - hw, H - 44 * s), rgba(220, 220, 214, 0.42f * fadeIn), hint, 1.2f * s);
  }

  // ---- right panel (Play / Character / Credits)
  if (S.page != Main) {
    const float pa = ease(((float)ImGui::GetTime() - (float)S.pageAt) / 0.16f) * fadeIn, off = (1 - pa) * 26 * s;
    const ImVec2 mn(W * 0.555f + off, 150 * s), mx(W - 72 * s + off, S.page == Play ? 150 * s + 108 * s + 3 * 130 * s + 64 * s : H - 112 * s);   // Play: as tall as its cards
    menuGlass(dl, mn, mx, pa);
    const char* title = S.page == Play ? "PLAY" : S.page == Character ? "CHARACTER" : "CREDITS";
    tracked(dl, fh, 30 * s, V(mn.x + 40 * s, mn.y + 34 * s), rgba(236, 232, 224, pa), title, 30 * s * 0.16f);
    dl->AddRectFilled(V(mn.x + 40 * s, mn.y + 82 * s), V(mn.x + 84 * s, mn.y + 84 * s), alphaOf(kAccent, pa));
    const ImVec2 cmn(mn.x + 40 * s, mn.y + 108 * s), cmx(mx.x - 40 * s, mx.y - 36 * s);
    if (S.page == Play) {
      struct Card { const char* t; const char* d; };
      const Card cards[] = {{"SINGLEPLAYER", "Pick a ready map or your own, set the mode, teams and AI, and deploy."},
                            {"HOST LAN", "Run a match that players on your network can join."},
                            {"JOIN LAN", "Find a match hosted on your network."}};
      const float chh = 112 * s, gap = 18 * s;
      if (S.focus == 1 && !overlay && !io.WantCaptureKeyboard) { if (up) S.sel2 = (S.sel2 + 2) % 3; if (down) S.sel2 = (S.sel2 + 1) % 3; }
      bool go = S.focus == 1 && enter && !overlay && !io.WantCaptureKeyboard;
      for (int k = 0; k < 3; ++k) {
        const ImVec2 a0(cmn.x, cmn.y + k * (chh + gap)), a1(cmx.x, cmn.y + k * (chh + gap) + chh);
        const bool hover = !overlay && !io.WantCaptureMouse && ImGui::IsMouseHoveringRect(a0, a1, false);
        if (hover && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) { S.focus = 1; S.sel2 = k; }
        if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { S.focus = 1; S.sel2 = k; go = true; }
        const bool on = S.focus == 1 && S.sel2 == k;
        dl->AddRectFilled(a0, a1, rgba(255, 255, 255, (on ? 0.075f : 0.035f) * pa), 12 * s);
        dl->AddRect(a0, a1, on ? alphaOf(kAccent, 0.75f * pa) : rgba(255, 255, 255, 0.10f * pa), 12 * s, 0, std::max(1.0f, s));
        if (on) dl->AddRectFilled(V(a0.x, a0.y + 26 * s), V(a0.x + 3 * s, a1.y - 26 * s), alphaOf(kAccent, pa));
        tracked(dl, fh, 22 * s, V(a0.x + 28 * s, a0.y + 26 * s), rgba(236, 232, 224, (on ? 1.0f : 0.78f) * pa), cards[k].t, 22 * s * 0.14f);
        dl->AddText(fb, 16 * s, V(a0.x + 28 * s, a0.y + 64 * s), rgba(205, 205, 198, 0.70f * pa), cards[k].d);
      }
      if (go) {
        if (S.sel2 == 0) { gUi.showSetup = true; gUi.setupMode = 0; }
        else if (S.sel2 == 1) { gUi.showSetup = true; gUi.setupMode = 1; }
        else gUi.showJoin = true;
      }
      dl->AddText(fb, 15 * s, V(cmn.x, cmn.y + 3 * 130 * s + 2 * s), rgba(205, 205, 198, 0.55f * pa), "Maps you build in Create Map are listed in the match setup.");
    } else if (S.page == Character) {
      const float split = cmn.x + (cmx.x - cmn.x) * 0.48f;
      // one undecorated window over the panel: the turning preview on the left, the choices (scrolling) on the right
      ImGui::SetNextWindowPos(cmn); ImGui::SetNextWindowSize(V(cmx.x - cmn.x, cmx.y - cmn.y));
      ImGui::SetNextWindowBgAlpha(0);
      ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(1, 1, 1, 0.07f)); ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.10f)); ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.94f, 0.46f, 0.13f, 0.35f));
      ImGui::PushStyleVar(ImGuiStyleVar_Alpha, pa);
      if (ImGui::Begin("##menu_character", nullptr, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav)) {
        drawPreview(app, dl, cmn, V(split - 16 * s, cmx.y), pa);
        ImGui::SetCursorScreenPos(V(split + 8 * s, cmn.y));
        ImGui::BeginChild("##lists", V(cmx.x - split - 8 * s, cmx.y - cmn.y), 0, ImGuiWindowFlags_NoBackground);
        ImGui::SetWindowFontScale(1.2f);
        auto B = bodies(); auto Wp = primaries(app);
        const int n = (int)B.size() + (int)Wp.size();
        if (S.focus == 1 && !overlay) { if (up) S.sel2 = (S.sel2 + n - 1) % n; if (down) S.sel2 = (S.sel2 + 1) % n; }
        auto heading = [&](const char* h) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.46f, 0.13f, 1)); ImGui::TextUnformatted(h); ImGui::PopStyleColor(); };
        int row = 0; bool changed = false;
        auto entry = [&](const std::string& label, bool chosen, auto&& pick) {
          const bool cur = S.focus == 1 && S.sel2 == row;
          std::string l = label + "##e" + std::to_string(row);
          if (chosen) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.46f, 0.13f, 1));   // the choice in the accent
          const bool hit = ImGui::Selectable(l.c_str(), cur);
          if (chosen) ImGui::PopStyleColor();
          if (hit) { S.focus = 1; S.sel2 = row; pick(); changed = true; }
          if (cur && enter && !overlay) { pick(); changed = true; }
          if (cur && (up || down)) ImGui::SetScrollHereY(0.5f);
          ++row;
        };
        heading("BODY");
        for (auto& b : B) entry(b.second, settings().s.character == b.first, [&] { settings().s.character = b.first; });
        ImGui::Spacing(); heading("PRIMARY WEAPON");
        for (int k : Wp) { const WeaponDef& d = *app.weaponDefs[k]; entry(d.name, settings().s.primary.empty() ? d.id == "m1_garand" : settings().s.primary == d.id, [&] { settings().s.primary = d.id; }); }
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.77f, 0.65f));
        ImGui::TextWrapped("Your body in every match and your first weapon in Explore and at spawn. Grenades, knife and the rest of the kit are unchanged. Drag the soldier to turn him.");
        ImGui::PopStyleColor();
        if (changed) { settings().save(); app.resetLoadout(); }
        ImGui::EndChild();
      }
      ImGui::End();
      ImGui::PopStyleVar(); ImGui::PopStyleColor(3);
    } else {   // Credits
      ImGui::SetNextWindowPos(cmn); ImGui::SetNextWindowSize(V(cmx.x - cmn.x, cmx.y - cmn.y)); ImGui::SetNextWindowBgAlpha(0);
      ImGui::PushStyleVar(ImGuiStyleVar_Alpha, pa);
      if (ImGui::Begin("##menu_credits", nullptr, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        auto head = [](const char* h) { ImGui::Spacing(); ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.46f, 0.13f, 1)); ImGui::TextUnformatted(h); ImGui::PopStyleColor(); };
        auto line = [](const char* a, const char* b) { ImGui::TextUnformatted(a); ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.48f); ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.77f, 0.7f)); ImGui::TextUnformatted(b); ImGui::PopStyleColor(); };
        head("BATTLE FOUNDRY"); ImGui::TextWrapped("A battlefield builder and combined-arms shooter.");
        head("ENGINE LIBRARIES");
        line("Dear ImGui", "MIT"); line("GLFW", "zlib"); line("GLM", "MIT"); line("Jolt Physics", "MIT"); line("nlohmann/json", "MIT"); line("stb", "MIT / public domain"); line("miniaudio", "MIT-0 / public domain");
        head("TYPE"); line("Barlow, Barlow Condensed (The Barlow Project Authors)", "SIL OFL 1.1"); line("Black Ops One (The Black Ops One Project Authors)", "SIL OFL 1.1");
        ImGui::TextWrapped("The font files and their licences are in content/fonts.");
        head("MODELS, TEXTURES AND SOUNDS");
        ImGui::TextWrapped("Third-party models, textures and sounds are listed with their authors and licences in docs/credits.md.");
      }
      ImGui::End(); ImGui::PopStyleVar();
    }
  }
  if (S.showControls) controlsWindow(app, &S.showControls);
}

void mainMenuGo(App& app, int item) {
  if (item < 0) { S.page = Main; S.focus = 0; return; }
  S.sel = item; S.focus = 0; activate(app, item);
}

unsigned menuBackdropTex(bool blurred) { ensureLoaded(); return blurred ? S.bgBlur.tex : S.bg.tex; }
