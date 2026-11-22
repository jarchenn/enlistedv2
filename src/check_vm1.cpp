// VM1 --check-vm1: the first-person weapon and arms are opaque. For each gun: the frame and the viewmodel's distance
// attachment (non-zero where the viewmodel drew) read with the view on the bright sky, then on the ground. The viewmodel
// stays put on screen, so where it drew the colour must not follow the background: the mean change inside the mask is
// held under a quarter of the change outside it (a see-through gun follows the background).
// VM2: the parts over each other. The sky view again with only the arms drawn (Renderer::vmOnly 1), then only the gun
// (2). Where the hand is nearer than the gun the full frame must show the hand as drawn alone (the gun never reads
// through it); where the gun is nearer the gun pokes through the hand: the share of the hand so hidden is held small.
// Images: selftest_out/vm1_<gun>_{sky,ground,arms,gun,poke}.png (poke: the hidden hand in red over the full frame).
// Env: NMK_VM1_MATCH=<map> a match on that map (NMK_VM1_MODE=sd: Search and Destroy, else Team Deathmatch);
// NMK_VM1_GUNS=a,b the guns; NMK_VM1_ADS aimed; NMK_VM1_MOVE walking forward.
#include "selftest.h"
#include <stb_image_write.h>
#include "game.h"
#include "mpui.h"
#include "scenario.h"

namespace {
int fails = 0, step = 0, gi = 0; float t = 0; bool want = false, got = false;
std::vector<uint8_t> px[6]; std::vector<float> vm[6]; int W = 0, H = 0;
std::vector<std::string> guns;
void chk(bool ok, const std::string& w) { logf("[vm1] %s %s", ok ? "PASS" : "FAIL", w.c_str()); if (!ok) ++fails; }
void png(const std::string& name, const std::vector<uint8_t>& p) {   // the read-back is bottom-up
  std::vector<uint8_t> f((size_t)W * H * 4); for (int y = 0; y < H; ++y) std::memcpy(&f[(size_t)y * W * 4], &p[(size_t)(H - 1 - y) * W * 4], (size_t)W * 4);
  fs::path o = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(o, ec);
  stbi_write_png((o / ("vm1_" + name + ".png")).string().c_str(), W, H, 4, f.data(), W * 4);
}
}

bool vm1SelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--check-vm1") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) {   // after the frame: read it
    if (want) { int w, h; a.R.readPixels(px[step], w, h); a.R.readVmDist(vm[step], W, H); W = w; H = h; want = false; got = true; a.R.vmOnly = 0; }
    return true;
  }
  t += dt;
  static bool started = false;
  if (!started) {
    started = true;
    const char* gl = std::getenv("NMK_VM1_GUNS"); std::string s = gl && *gl ? gl : "m1_garand,modern_m4";
    for (size_t p = 0; p <= s.size();) { size_t q = s.find(',', p); if (q == std::string::npos) q = s.size(); if (q > p) guns.push_back(s.substr(p, q - p)); p = q + 1; }
    const char* mp = std::getenv("NMK_VM1_MATCH");   // a match on this map (the player's own character body in first person)
    if (mp && *mp) {
      a.openMap(contentDir() / "templates" / (std::string(mp) + ".nmap.json"));
      Scenario sc = Scenario::fromJson(a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object());
      const char* md = std::getenv("NMK_VM1_MODE");
      sc.rules.mode = md && std::string(md) == "sd" ? GameMode::SearchDestroy : GameMode::TDM;
      gUi.loaded = false; gUi.matchScenario = sc.toJson();
      if (!gGame) gGame = new Game(a);
      gGame->pendingStart = true;
      bool ok = a.enterExplore() && gGame->active;
      chk(ok, std::string(gameModeName(sc.rules.mode)) + " on " + mp + " starts (" + (gGame ? gGame->setupError : std::string()) + ")");
    } else if (const char* mp2 = std::getenv("NMK_VM1_MAP"); mp2 && *mp2) { a.openMap(contentDir() / "templates" / (std::string(mp2) + ".nmap.json")); a.enterExplore(); }   // walking the map, no match
    else { a.newMap(*a.findTemplate("blank_slate"), "VM1"); a.enterExplore(); }
    a.useScripted = true; a.scriptedInput = ExploreInput{}; t = 0; return true;
  }
  if (gGame && gGame->active) { Soldier* L = gGame->local(); if (!L || !L->alive) { t = 0; return true; } L->hp = 100; L->protectUntil = gGame->t + 5; }
  if (gi >= (int)guns.size()) { if (a.mode == Mode::Explore) a.exitExplore(); logf("[vm1] done: %d failures", fails); a.selftest->exitCode = fails ? 1 : 0; a.selftest->done = true; return true; }
  if (t < 0.3f) return true;
  const std::string& gun = guns[gi];
  int wi = a.weaponIndex(gun);
  if (wi < 0) { chk(false, "weapon " + gun + " missing"); ++gi; t = 0; return true; }
  if (a.activeWeapon != wi) { std::string m; a.equipWeapon(wi, m); t = 0; return true; }
  a.eyePitch = step == 1 ? -55.0f : step >= 4 ? -30.0f : 55.0f; a.eyeYaw = 30;   // sky, ground, the sky with the arms alone and the gun alone, then the ground behind the gun with the world as drawn and black
  static const bool ads = std::getenv("NMK_VM1_ADS") != nullptr, move = std::getenv("NMK_VM1_MOVE") != nullptr;   // walking
  a.scriptedInput.aim = ads; a.scriptedInput.move = vec2(0, move ? 1.0f : 0.0f);
  if (t > 1.5f && !want && !got) { want = true; a.R.vmOnly = step == 2 ? 1 : step == 3 ? 2 : step == 5 ? 3 : 0; }
  if (!got) return true;
  got = false;
  if (step == 1) {   // sky and ground read: the viewmodel against the scene
    double din = 0, dout = 0; long nin = 0, nout = 0;
    for (int i = 0; i < W * H; ++i) {
      double d = 0; for (int c = 0; c < 3; ++c) d += std::abs((int)px[0][i * 4 + c] - (int)px[1][i * 4 + c]);
      bool inA = vm[0][i] > 0, inB = vm[1][i] > 0;
      if (inA && inB) { din += d; ++nin; } else if (!inA && !inB) { dout += d; ++nout; }
    }
    din = nin ? din / nin / 3 : 0; dout = nout ? dout / nout / 3 : 0;
    char b[220]; std::snprintf(b, sizeof b, "%s: the viewmodel covers %.1f %% of the frame; mean change inside it %.1f, outside %.1f (sky -> ground)", gun.c_str(), 100.0 * nin / std::max(1, W * H), din, dout);
    chk(nin > W * H / 50 && dout > 20 && din < 0.25 * dout, b);
    png(gun + "_sky", px[0]); png(gun + "_ground", px[1]);
  }
  if (step == 3) {   // the arms alone, the gun alone: the hand over the gun
    long arm = 0, front = 0, wrong = 0, poke = 0; std::vector<uint8_t> pk = px[0];
    for (int i = 0; i < W * H; ++i) {
      float da = vm[2][i], dg = vm[3][i]; if (da <= 0) continue;
      ++arm; if (dg <= 0) continue;
      if (da < dg - 0.002f) {   // the hand in front: the full frame shows the hand
        ++front; int d = 0; for (int c = 0; c < 3; ++c) d += std::abs((int)px[0][i * 4 + c] - (int)px[2][i * 4 + c]);
        if (d > 3 * 12) ++wrong;
      } else if (dg < da - 0.002f) { ++poke; pk[i * 4] = 255; pk[i * 4 + 1] = pk[i * 4 + 1] / 3; pk[i * 4 + 2] = pk[i * 4 + 2] / 3; }
    }
    png(gun + "_arms", px[2]); png(gun + "_gun", px[3]); png(gun + "_poke", pk);
    char b[260];
    std::snprintf(b, sizeof b, "%s: where the hand is in front of the gun (%ld px) the frame shows the hand as drawn alone (%ld px differ); the gun hides %.0f %% of the hand behind it", gun.c_str(), front, wrong, 100.0 * poke / std::max(1L, arm));
    chk(arm > 0 && wrong <= front / 100, b);
  }
  if (step == 5) {   // VM2: the frame of step 4 again with the world black under the weapon: the weapon's pixels must not change (the
    // world's bloom blended over it showed the ground through the dark gun, ~13 levels)
    double sum = 0, mx = 0; long n = 0; std::vector<uint8_t> vis(px[4].size(), 0); for (size_t i = 3; i < vis.size(); i += 4) vis[i] = 255;
    for (int i = 0; i < W * H; ++i) {
      if (!(vm[4][i] > 0 && vm[5][i] > 0)) continue;
      double d = 0; for (int c = 0; c < 3; ++c) d += std::abs((int)px[4][i * 4 + c] - (int)px[5][i * 4 + c]); d /= 3;
      sum += d; mx = std::max(mx, d); ++n; uint8_t v = (uint8_t)std::min(255.0, d * 16); vis[i * 4] = vis[i * 4 + 1] = vis[i * 4 + 2] = v;
    }
    png(gun + "_hip", px[4]); png(gun + "_worldblack", px[5]); png(gun + "_leak", vis);
    { std::vector<uint8_t> mk(px[4].size(), 0); for (int i = 0; i < W * H; ++i) { mk[i * 4] = vm[4][i] > 0 ? 255 : 0; mk[i * 4 + 1] = vm[5][i] > 0 ? 255 : 0; mk[i * 4 + 3] = 255; } png(gun + "_vmmask", mk); }   // FR9: the weapon pixels each frame counts (red: the normal frame, green: the black-world one; yellow both)
    char b[220]; std::snprintf(b, sizeof b, "%s: the world blacked out under the weapon changes its pixels by %.2f levels on average (max %.0f; held under 1)", gun.c_str(), n ? sum / n : 0.0, mx);
    chk(n > 0 && sum / std::max(1L, n) < 1.0, b);
    ++gi; step = 0; t = 0.3f; return true;
  }
  ++step; t = 0.3f;
  return true;
}
