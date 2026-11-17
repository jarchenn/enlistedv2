// VM1 --check-vm1: the first-person weapon and arms are opaque. For each gun: the frame and the viewmodel's distance
// attachment (non-zero where the viewmodel drew) read with the view on the bright sky, then on the ground. The viewmodel
// stays put on screen, so where it drew the colour must not follow the background: the mean change inside the mask is
// held under a quarter of the change outside it (a see-through gun follows the background). Images: selftest_out/vm1_*.
#include "selftest.h"
#include <stb_image_write.h>
#include "game.h"
#include "mpui.h"
#include "scenario.h"

namespace {
int fails = 0, step = 0, gi = 0; float t = 0; bool want = false, got = false;
std::vector<uint8_t> px[2]; std::vector<float> vm[2]; int W = 0, H = 0;
const char* kGuns[] = {"m1_garand", "modern_m4"};
void chk(bool ok, const std::string& w) { logf("[vm1] %s %s", ok ? "PASS" : "FAIL", w.c_str()); if (!ok) ++fails; }
}

bool vm1SelfTest(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--check-vm1") mode = 1; }
  if (mode != 1) return false;
  if (dt < 0) {   // after the frame: read it
    if (want) { int w, h; a.R.readPixels(px[step & 1], w, h); a.R.readVmDist(vm[step & 1], W, H); W = w; H = h; want = false; got = true; }
    return true;
  }
  t += dt;
  static bool started = false;
  if (!started) {
    started = true;
    const char* mp = std::getenv("NMK_VM1_MATCH");   // a Team Deathmatch on this map (the player's own character body in first person)
    if (mp && *mp) {
      a.openMap(contentDir() / "templates" / (std::string(mp) + ".nmap.json"));
      Scenario sc = Scenario::fromJson(a.doc.extra.is_object() ? a.doc.extra.value("scenario", json::object()) : json::object());
      sc.rules.mode = GameMode::TDM;
      gUi.loaded = false; gUi.matchScenario = sc.toJson();
      if (!gGame) gGame = new Game(a);
      gGame->pendingStart = true;
      bool ok = a.enterExplore() && gGame->active;
      chk(ok, std::string("team deathmatch on ") + mp + " starts (" + (gGame ? gGame->setupError : std::string()) + ")");
    } else { a.newMap(*a.findTemplate("blank_slate"), "VM1"); a.enterExplore(); }
    a.useScripted = true; a.scriptedInput = ExploreInput{}; t = 0; return true;
  }
  if (gGame && gGame->active) { Soldier* L = gGame->local(); if (!L || !L->alive) { t = 0; return true; } L->hp = 100; L->protectUntil = gGame->t + 5; }
  if (gi >= 2) { if (a.mode == Mode::Explore) a.exitExplore(); logf("[vm1] done: %d failures", fails); a.selftest->exitCode = fails ? 1 : 0; a.selftest->done = true; return true; }
  if (t < 0.3f) return true;
  int wi = a.weaponIndex(kGuns[gi]);
  if (wi < 0) { chk(false, std::string("weapon ") + kGuns[gi] + " missing"); ++gi; t = 0; return true; }
  if (a.activeWeapon != wi) { std::string m; a.equipWeapon(wi, m); t = 0; return true; }
  a.eyePitch = (step & 1) ? -55.0f : 55.0f; a.eyeYaw = 30;   // sky, then ground
  static const bool ads = std::getenv("NMK_VM1_ADS") != nullptr; a.scriptedInput.aim = ads;   // aimed down the sights
  if (t > 1.5f && !want && !got) want = true;
  if (!got) return true;
  got = false;
  if (step & 1) {   // both views read: compare
    double din = 0, dout = 0; long nin = 0, nout = 0;
    for (int i = 0; i < W * H; ++i) {
      double d = 0; for (int c = 0; c < 3; ++c) d += std::abs((int)px[0][i * 4 + c] - (int)px[1][i * 4 + c]);
      bool inA = vm[0][i] > 0, inB = vm[1][i] > 0;
      if (inA && inB) { din += d; ++nin; } else if (!inA && !inB) { dout += d; ++nout; }
    }
    din = nin ? din / nin / 3 : 0; dout = nout ? dout / nout / 3 : 0;
    char b[220]; std::snprintf(b, sizeof b, "%s: the viewmodel covers %.1f %% of the frame; mean change inside it %.1f, outside %.1f (sky -> ground)", kGuns[gi], 100.0 * nin / std::max(1, W * H), din, dout);
    chk(nin > W * H / 50 && dout > 20 && din < 0.25 * dout, b);
    for (int k = 0; k < 2; ++k) {   // the read-back is bottom-up
      std::vector<uint8_t> f((size_t)W * H * 4); for (int y = 0; y < H; ++y) std::memcpy(&f[(size_t)y * W * 4], &px[k][(size_t)(H - 1 - y) * W * 4], (size_t)W * 4);
      fs::path o = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(o, ec);
      stbi_write_png((o / ("vm1_" + std::string(kGuns[gi]) + (k ? "_ground.png" : "_sky.png"))).string().c_str(), W, H, 4, f.data(), W * 4);
    }
    ++gi;
  }
  ++step; t = 0.3f;
  return true;
}
