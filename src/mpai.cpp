// Glue between the shared core and the multiplayer + AI systems (see mpai.h).
#include "mpai.h"
#include "mptest.h"
#include "game.h"
#include "mpui.h"
#include "net.h"
#include "app.h"
#include "model.h"
#include <GLFW/glfw3.h>
#include <imgui.h>

int mpaiExit = -1;
static int gProjOwner = -1;        // owner of the projectile being resolved (explosions it causes inherit it)
static bool gInGrenadeBlast = false;
extern int runAiSelfTestArg;       // selftest_ai.cpp

static bool edge(GLFWwindow* w, int key) { static std::map<int, bool> prev; bool d = glfwGetKey(w, key) == GLFW_PRESS, p = prev[key]; prev[key] = d; return d && !p; }

void installCrashReporter();
bool mpaiArg(App& app, int argc, char** argv, int& k, bool& runSelf) {
  (void)app; (void)argc;
  installCrashReporter();
  std::string a = argv[k];
  if (a == "--selftest-match") { mpaiExit = runMatchSelfTest(); return true; }
  if (a == "--bake-mpai-models" && k + 1 < argc) {   // offline: grenade GLBs -> content/mpai/models/<dir>
    fs::path src = argv[++k], root = k + 1 < argc && argv[k + 1][0] != '-' ? fs::path(argv[++k]) : contentDir();
    json cfg = json::parse(readTextFile(root / "mpai" / "grenade_models.json"));
    int bad = 0;
    for (auto& m : cfg["models"]) {
      ImportSpec s = importSpecFromJson(m);
      std::string err, sum;
      if (!fs::exists(src / s.src)) { logf("bake %s skipped: %s not found", s.dir.c_str(), s.src.c_str()); continue; }
      if (bakeGlbModel(src / s.src, s, root / "mpai" / "models" / s.dir, err, sum)) logf("bake %s: %s", s.dir.c_str(), sum.c_str());
      else { logf("bake %s FAILED: %s", s.dir.c_str(), err.c_str()); ++bad; }
    }
    mpaiExit = bad ? 1 : 0; return true;
  }
  if (a == "--selftest-ai") { runSelf = true; runAiSelfTestArg = 1; return true; }
  if (a == "--benchmark-match") { runSelf = true; runAiSelfTestArg = 2; return true; }
  if (a == "--selftest-net") {   // --selftest-net <host|a|b|c|d> [port]
    extern void setNetTestRole(const std::string&, int);
    std::string r = k + 1 < argc ? argv[k + 1] : "host"; int port = 27970;
    if (k + 1 < argc) ++k;
    if (k + 1 < argc && argv[k + 1][0] != '-') port = std::atoi(argv[++k]);
    setNetTestRole(r, port); runSelf = true; return true;
  }
  return false;
}

void mpaiUI(App& app) {
  if (gNet) gNet->update(app, app.frameDt);
  // a LAN host keeps simulating while its pause menu is open (clients depend on it)
  if (gNet && gNet->role == Net::HOST && app.mode == Mode::Explore && app.pauseMenu && gGame && gGame->active) {
    static float acc = 0; acc += app.frameDt; int n = 0;
    while (acc >= 1.0f / 60.0f && n++ < 4) { ExploreInput none; app.exploreStep(1.0f / 60.0f, none); gGame->step(1.0f / 60.0f); acc -= 1.0f / 60.0f; }
    Physics::step(app.frameDt);
  }
  if (app.mode == Mode::Creative) {
    gUi.creativeMenus(app); gUi.scenarioPanel(app); gUi.setupWindow(app); gUi.joinWindow(app);
    return;
  }
  if (!gGame || !gGame->active) return;
  if (!app.selftest && edge(app.win, GLFW_KEY_F6)) gGame->debugOverlay = !gGame->debugOverlay;
  gGame->drawHud();
  if (gUi.wantExit) {
    gUi.wantExit = false;
    bool host = gNet && gNet->role == Net::HOST;
    if (host) gNet->returnToLobby(app);
    app.exitExplore();
    if (gNet && gNet->role == Net::CLIENT) gUi.showJoin = true; else { gUi.showSetup = true; gUi.setupMode = host ? 1 : 0; }
  }
}

void mpaiFrame(App& app, Overlay& ov) {
  if (app.mode == Mode::Creative) { gUi.creativeOverlay(app, ov); return; }
  if (gGame && gGame->active) gGame->drawItems();
}

bool mpaiCamera(App& app, vec3& e, vec3& f, float& fov) { (void)app; return gGame && gGame->active && gGame->camera(e, f, fov); }

void mpaiEnterExplore(App& app) {
  if (!gGame || !gGame->pendingStart) return;
  std::string err;
  if (!gGame->begin(err)) {
    app.toast("Match could not start: " + err, 8);
    gUi.worldIssues = {err};
    app.exitExplore();
    gUi.showSetup = true;
  }
}

void mpaiExitExplore(App& app) {
  if (gGame && gGame->active) {
    if (gNet && gNet->role == Net::HOST && gNet->matchRunning) gNet->returnToLobby(app);
    gGame->end();
  }
}

void mpaiInput(App& app, ExploreInput& in) {
  if (!gGame || !gGame->active) return;
  Soldier* L = gGame->local();
  bool frozen = !L || !L->alive || gGame->match.phase != MatchPhase::Running;
  if (frozen) { in = ExploreInput{}; return; }
  if (app.selftest || app.pauseMenu) return;
  bool g = edge(app.win, GLFW_KEY_G), h = edge(app.win, GLFW_KEY_H);
  if ((g || h) && L->throwT < 0) {
    int kind = g ? GK_FRAG : GK_SMOKE;
    vec3 v = App::forward(app.eyeYaw, app.eyePitch + 8.0f) * 15.0f + vec3(L->vel.x, 0, L->vel.z);
    if ((kind == GK_FRAG ? L->frags : L->smokes) <= 0) app.toast(kind == GK_FRAG ? "No frag grenades left" : "No smoke grenades", 2);
    else if (gGame->isClient) gNet->clientThrow(kind, v);          // the host checks inventory and releases it
    else gGame->startThrow(*L, kind, v, vec3(0));
  }
  if (gGame->isClient && in.reload) gNet->clientReload();
  if (gGame->isClient && in.interact && app.target.valid && app.target.kind != 0) {   // stations / crates / racks are not replicated yet
    in.interact = false;
    app.toast("Crew-served guns, supply crates and weapon racks are host-only in LAN matches (this milestone).", 4);
  }
  if (gGame->isClient) in.build = false;   // field defences are not replicated yet
  if (L->throwT >= 0) { in.fire = false; in.aim = false; in.reload = false; }   // the throw occupies the hands
}

bool mpaiLocalFrozen(App& app) {
  (void)app;
  if (!gGame || !gGame->active) return false;
  Soldier* L = gGame->local();
  return !L || !L->alive;
}

void mpaiStep(App& app, float dt) { (void)app; if (gGame && gGame->active) gGame->step(dt); }

bool mpaiProjectile(App& app, Projectile& p, vec3 seg, float len, float tWorld) {
  gProjOwner = p.owner;
  if (!gGame || !gGame->active || len <= 1e-5f) return false;
  if (gGame->isClient) {
    // a LAN client never resolves rounds itself: its own shots become requests, drawn as visual tracers meanwhile
    if (p.owner == -1 && p.life == 0 && gNet) gNet->clientFire(p.p, glm::normalize(p.v), app.activeWeapon, p.tracer);
    gGame->ghosts.push_back({p.p, p.v, 0, p.tracer});
    return true;
  }
  if (p.owner == -1 && p.life == 0 && gNet && gNet->role == Net::HOST)   // the host's own shots: tracers / sound on clients
    gNet->hostWorldEvent(json{{"t", "shot"}, {"p", tojson(p.p)}, {"d", tojson(glm::normalize(p.v))}, {"w", app.activeWeapon}, {"tr", p.tracer}, {"by", gGame->localSlot}});
  float tHit = tWorld;
  vec3 a = p.p;
  if (!gGame->projectileSegment(p, a, seg, len, tHit)) return false;
  vec3 end = a + seg / len * tHit;
  if (p.tracer) app.fx.streak(a, end, 0.025f, 0.06f, vec4(9.0f, 2.2f, 0.8f, 1));
  else app.fx.streak(a, end, app.fx.rangeMode ? 0.012f : 0.007f, app.fx.rangeMode ? 0.05f : 0.025f, app.fx.rangeMode ? vec4(1.6f, 1.5f, 1.3f, 1) : vec4(0.7f, 0.68f, 0.62f, 1));
  return true;
}

void mpaiExplosion(App& app, vec3 p, vec3 dir, float dmg, float radius, float craterScale, int wallKind, int breached, size_t wallHits) {
  if (gNet && gNet->role == Net::HOST && gGame && gGame->active)
    gNet->hostWorldEvent(json{{"t", "ex"}, {"p", tojson(p)}, {"d", tojson(dir)}, {"dmg", dmg}, {"r", radius}, {"cs", craterScale}, {"wk", wallKind}, {"at", app.blastAT}, {"sg", app.blastSplashGun}, {"fx", !gInGrenadeBlast || true}});
  if (!gGame || !gGame->active) return;
  if (!gGame->isClient && !gInGrenadeBlast) gGame->explosionDamage(p, dmg, radius, gGame->soldierOfOwner(gProjOwner), false);
  gGame->hearSound(p, -1, 90.0f, gGame->soldierOfOwner(gProjOwner), 1);
  if (breached > 0 || wallHits > 0) gGame->worldChanged(p - vec3(radius + 2.5f), p + vec3(radius + 2.5f));
}

void mpaiGrenadeBlastScope(bool on) { gInGrenadeBlast = on; }

void mpaiImpact(App& app, const Projectile& p, vec3 hitPos, vec3 hitNormal, uint64_t hitUd, float hitT, const std::string& surface) {
  (void)app;
  if (!gNet || gNet->role != Net::HOST || !gGame || !gGame->active) return;
  bool ground = hitUd == 0 || (hitUd >> 60) == 0xF;
  json ev = {{"t", "imp"}, {"pp", tojson(p.p)}, {"pv", tojson(p.v)}, {"m", p.mass}, {"a", p.ammo}, {"hp", tojson(hitPos)}, {"hn", tojson(hitNormal)}, {"ud", hitUd}, {"ht", hitT}, {"s", ground ? std::string("ground") : surface}};
  if (ground) ev["t"] = "impfx";   // dust only: no persistent state, not kept for late joiners
  gNet->hostWorldEvent(ev);
}

// ---------------------------------------------------------------- crash breadcrumbs
#include "crumb.h"
#include <windows.h>
const char* volatile gCrumb = "start";
static LONG WINAPI crashFilter(EXCEPTION_POINTERS* e) {
  logf("CRASH: exception 0x%08lx at %p, last stage: %s", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress, gCrumb ? gCrumb : "?");
  fflush(stdout);
  return EXCEPTION_CONTINUE_SEARCH;
}
void installCrashReporter() { static bool done = false; if (!done) { done = true; SetUnhandledExceptionFilter(crashFilter); } }
