// CAPE --capture-enemy [zombie.json type id] (default knight10; the user: "send an animation of a knight 10 in game"): one enemy of
// that type in the spawn-egg sandbox on the blank map, the editor camera beside it above the grass, no UI. Every other frame
// (the selftest's fixed 1/60 s: 30 fps) goes to selftest_out/enemy_<id>_NNNN.png for an mp4.
//   1 idle (the AI held) 3 s
//   2 the AI let go: it runs at a zombie 24 m off and strikes it (the zombie kept alive for three blows, then left to die)
//   3 a hit hard enough to stagger it (its hit reaction)
//   4 its death, and 4 s of the body
// The log lines "[cape] ..." say when each thing happened.
#include "selftest.h"
#include "game.h"
#include "ai_core.h"
#include "enemy_types.h"
#include "scenario.h"

namespace {
int phase = 0, frame = 0, me = -1, z1 = -1, blows = 0; float t = 0, tGo = -1, tDown = -1, tHit = -1, tDie = -1; bool wasWind = false; fs::path pend; vec3 camP(0), look(0), deadAt(0);
std::string id = "knight10";
fs::path out(int n) { fs::path p = exeDir() / "selftest_out"; std::error_code ec; fs::create_directories(p, ec); char b[96]; std::snprintf(b, sizeof b, "enemy_%s_%04d.png", id.c_str(), n); return p / b; }
}

bool enemyCapture(App& a, float dt) {
  static int mode = -1;
  if (mode < 0) { mode = 0; for (int k = 1; k < __argc; ++k) if (std::string(__argv[k]) == "--capture-enemy") { mode = 1; if (k + 1 < __argc && __argv[k + 1][0] != '-') id = __argv[k + 1]; } }
  if (mode != 1) return false;
  if (dt < 0) { if (!pend.empty()) { a.screenshot(pend); pend.clear(); } return true; }
  auto fail = [&](const char* why) { logf("[cape] FAIL %s", why); a.selftest->exitCode = 1; a.selftest->done = true; return true; };
  if (phase == 0) { a.newMap(*a.findTemplate("blank_slate"), "Enemy capture"); a.syncRuntime(); phase = 1; t = 0; return true; }
  t += dt; ++frame;
  AiCore* A = aiCore();
  if (phase == 1) {
    if (t < 1.0f) return true;
    const int et = enemyTypeIndex(id); if (et < 0) return fail("no such enemy type");
    if (!gGame) gGame = new Game(a);
    std::string err; if (!gGame->sandboxBegin(err)) return fail(err.c_str());
    me = gGame->sandboxAdd(AiType::Zombies, et, 0, vec3(0, a.terrain.height(0, 0), 0), 180.0f);
    z1 = gGame->sandboxAdd(AiType::Zombies, 0, 1, vec3(0, a.terrain.height(0, 24), 24), 0.0f);
    if (me < 0 || z1 < 0) return fail("spawn");
    if (A) A->frozen = true;
    const Soldier& s = gGame->sold[me]; logf("[cape] %s: slot %d, model %s, %.0f health; zombie slot %d", id.c_str(), me, gGame->models[s.model]->id.c_str(), s.hp, z1);
    phase = 2; t = 0; frame = 0; return true;
  }
  Game& G = *gGame; if (!G.active) return fail("the sandbox ended");
  Soldier& s = G.sold[me]; Soldier& z = G.sold[z1]; const EnemyType& e = enemyType(s.etype);
  // the camera: ahead of it and to its side, at chest height (above the grass), near enough that it fills most of the frame
  const float H = e.height * e.scale; const vec3 at = (s.alive ? s.feet : deadAt) + vec3(0, 0.52f * H, 0);
  vec3 f = z.feet - s.feet; f.y = 0; f = glm::length(f) > 0.5f && tDown < 0 ? glm::normalize(f) : vec3(0, 0, 1); const vec3 r(-f.z, 0, f.x);
  const vec3 wantP = at + (r * 0.78f + f * 0.62f) * (1.75f * H) + vec3(0, 0.12f * H, 0);
  camP = frame < 3 ? wantP : glm::mix(camP, wantP, 0.05f); look = frame < 3 ? at : glm::mix(look, at, 0.12f);
  const vec3 d = glm::normalize(look - camP); a.camPos = camP; a.camYaw = degrees(std::atan2(-d.x, -d.z)); a.camPitch = degrees(std::asin(d.y));
  if (frame % 2 == 0) pend = out(frame / 2);
  if (s.alive) deadAt = s.feet;
  if (phase == 2) {   // 1: idle
    if (t > 3.0f) { if (A) A->frozen = false; tGo = t; phase = 3; logf("[cape] %.2f s: the AI let go (the zombie %.1f m off)", t, glm::length(z.feet - s.feet)); }
    return true;
  }
  if (phase == 3) {   // 2: the run and the blows
    s.hp = std::max(s.hp, 0.8f * e.hp);
    const bool wind = s.meleeWind >= 0; if (wasWind && !wind) { ++blows; logf("[cape] %.2f s: blow %d done (the zombie %.0f health)", t, blows, z.hp); } wasWind = wind;
    if (z.alive && blows < 3) z.hp = std::max(z.hp, 2000.0f); else if (z.alive && z.hp > 30.0f) z.hp = 30.0f;
    if ((!z.alive && tDown < 0) || t > tGo + 22.0f) { tDown = t; phase = 4; if (A) A->frozen = true; logf("[cape] %.2f s: the zombie is down after %d blows; the AI held", t, blows); }
    return true;
  }
  if (phase == 4) {   // 3: the hit reaction, 4: the death
    if (tHit < 0 && t > tDown + 1.2f) { tHit = t; const float dmg = std::max(e.staggerMin + 5.0f, 40.0f); s.hp = std::max(s.hp, dmg + 50.0f); G.damage(me, dmg, -1, vec3(0, 0, -1), "upper torso"); logf("[cape] %.2f s: hit for %.0f (staggers at %.0f): %s", t, dmg, e.staggerMin, G.t < s.staggerUntil ? "staggered" : "no stagger"); }
    if (tHit >= 0 && tDie < 0 && t > tHit + 2.2f) { tDie = t; G.damage(me, 100000.0f, -1, vec3(0, 0, -1), "upper torso"); logf("[cape] %.2f s: killed (alive %d)", t, (int)s.alive); }
    if (tDie >= 0 && t > tDie + 4.5f) { logf("[cape] done: %d frames (30 fps) as selftest_out/enemy_%s_NNNN.png", frame / 2, id.c_str()); a.selftest->exitCode = 0; a.selftest->done = true; }
  }
  return true;
}
