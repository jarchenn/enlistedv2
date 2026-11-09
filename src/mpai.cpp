// Glue between the shared core and the multiplayer + AI systems (see mpai.h).
#include "mpai.h"
#include "gore_api.h"
#include "rules.h"
#include "mptest.h"
#include "game.h"
#include "mpui.h"
#include "net.h"
#include "app.h"
#include "model.h"
#include "grenade.h"
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
  if (a == "--selftest-rules") { runSelf = true; return true; }   // W1 gameplay rules (selftest_rules.cpp)
  if (a == "--selftest-match") { mpaiExit = runMatchSelfTest(); return true; }
  if (a == "--selftest-ai") { runSelf = true; runAiSelfTestArg = 1; return true; }
  if (a == "--benchmark-match") { runSelf = true; runAiSelfTestArg = 2; return true; }
  if (a == "--baseline-ai") { runSelf = true; runAiSelfTestArg = 3; return true; }   // AI_SPEC 2.2 baseline
  { extern bool aiTestArg(const std::string&, int, char**, int&, bool&); if (aiTestArg(a, argc, argv, k, runSelf)) return true; }   // W6: --selftest-ai-course, --soak-ai <min>, --make-ai-course
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
    gUi.scenarioPanel(app); gUi.setupWindow(app); gUi.joinWindow(app);
    return;
  }
  if (!gGame || !gGame->active) return;
  if (!app.selftest && edge(app.win, GLFW_KEY_F6)) gGame->debugOverlay = !gGame->debugOverlay;
  gGame->drawHud();
  rulesHud(*gGame);   // W1: shield icons over protected enemies, own protection ring
  if (gUi.wantExit) {
    gUi.wantExit = false;
    bool host = gNet && gNet->role == Net::HOST;
    if (host) gNet->returnToLobby(app);
    app.exitExplore();
    if (gNet && gNet->role == Net::CLIENT) gUi.showJoin = true; else { gUi.showSetup = true; gUi.setupMode = host ? 1 : 0; }
  }
}

void mpaiMenus(App& app) { gUi.creativeMenus(app); }
bool mpaiCreativeMouse(App& app) { return gUi.creativeMouse(app); }
bool mpaiBeforeSave(App& app) { return gUi.beforeSave(app); }

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
  goreReset(app);
  if (gGame && gGame->active) {
    if (gNet && gNet->role == Net::HOST && gNet->matchRunning) gNet->returnToLobby(app);
    gGame->end();
  }
}

void mpaiInput(App& app, ExploreInput& in) {
  if (!gGame || !gGame->active) return;
  Soldier* L = gGame->local();
  bool frozen = !L || !L->alive || gGame->match.phase != MatchPhase::Running;
  if (frozen) { if (L && !L->alive && app.occupied) { logf("station: leave, crew dead"); app.leaveStation(); } in = ExploreInput{}; return; }   // the dead free their gun
  if (app.selftest || app.pauseMenu) return;
  // grenades are the E3 items: Q / LMB, H quick frag (shared bindings); quick smoke on "smokeGrenade" (default J, match only)
  if (edge(app.win, app.binds.smokeGrenade) && L->throwT < 0 && app.quickReturn < 0) {
    int sd = gGame->smokeDef();
    const WeaponDef& cur = *app.weaponDefs[app.activeWeapon];
    if (sd < 0 || L->smokes <= 0) app.toast("No smoke grenades", 2);
    else if (!cur.grenade.on) {
      WeaponState& gs = app.wstate[app.weaponDefs[sd]->id];
      app.quickReturn = app.activeWeapon; app.selectWeapon(sd);
      if (gs.loaded == 0 && !gs.containers.empty()) { gs.containers.pop_back(); gs.loaded = 1; }
      gs.cooldown = 0; gs.throwT = 0; gs.triggerReleased = false;
    }
  }
  if (gGame->isClient && in.reload) {
    if (StationRT* st = app.station(app.occupied)) gNet->clientRequest(json{{"t", "greload"}, {"i", app.occupied}, {"at", st->selAT}});   // the host reloads the gun
    else gNet->clientReload();
  }
  if (gGame->isClient && app.occupied && in.switchPos) { in.switchPos = false; app.toast("Moving a gun is host-only in LAN matches.", 3); }
  if (L->throwT >= 0) { in.fire = false; in.aim = false; in.reload = false; }   // the throw occupies the hands
}

bool mpaiLocalFrozen(App& app) {
  (void)app;
  if (!gGame || !gGame->active) return false;
  Soldier* L = gGame->local();
  return !L || !L->alive;
}

void mpaiIdleSmoke(App& app, float dt);
void mpaiStep(App& app, float dt) { if (gGame && gGame->active) gGame->step(dt); else mpaiIdleSmoke(app, dt); goreStep(app, dt); }

bool mpaiProjectile(App& app, Projectile& p, vec3 seg, float len, float tWorld) {
  gProjOwner = p.owner;
  if (!gGame || !gGame->active || len <= 1e-5f) return false;
  if (gGame->isClient) {
    // a LAN client never resolves rounds itself: its own shots become requests, drawn as visual tracers meanwhile
    if (p.owner == -1 && p.life == 0 && gNet && !app.occupied) gNet->clientFire(p.p, glm::normalize(p.v), app.activeWeapon, p.tracerKind);   // a gun's rounds are gfire requests (stationShoot)
    gGame->ghosts.push_back({p.p, p.v, 0, p.tracerKind});
    return true;
  }
  if (p.owner == -1 && p.life == 0 && gNet && gNet->role == Net::HOST && !app.occupied)   // the host's own shots: tracers / sound on clients (a gun's: gshot)
    gNet->hostWorldEvent(json{{"t", "shot"}, {"p", tojson(p.p)}, {"d", tojson(glm::normalize(p.v))}, {"w", app.activeWeapon}, {"tr", p.tracerKind}, {"by", gGame->localSlot}});
  if (p.life == 0) rulesOnFire(*gGame, gGame->soldierOfOwner(p.owner));   // W1: firing ends spawn protection
  float tHit = tWorld;
  vec3 a = p.p;
  if (!gGame->projectileSegment(p, a, seg, len, tHit)) return false;
  vec3 end = a + seg / len * tHit;
  app.fx.bullet(a, end, p.tracerKind);
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

// ---------------------------------------------------------------- grenades on the shared core (equipment.cpp)
bool mpaiGrenadeSpawn(App& app, int def, vec3 pos, vec3 vel, int owner) {
  (void)pos;
  if (gGame && gGame->active && !gGame->isClient) rulesOnThrow(*gGame, gGame->soldierOfOwner(owner));   // W1: a throw ends spawn protection
  if (!gGame || !gGame->active || !gGame->isClient || owner != -1) return false;
  // a LAN client's own release becomes a request: the host checks the inventory and releases it from its soldier
  if (gNet) gNet->clientThrow(app.weaponDefs[def]->grenade.smoke ? GK_SMOKE : GK_FRAG, vel);
  return true;
}

static Grenades gIdleSmoke;   // smoke thrown in a plain Explore walkthrough (no match running)
static double gIdleT = 0;
Grenades& mpaiSmoke() { return gGame && gGame->active ? gGame->gren : gIdleSmoke; }

bool mpaiGrenadeDetonate(App& app, int def, vec3 pos, int owner, int phase) {
  bool smoke = def >= 0 && def < (int)app.weaponDefs.size() && app.weaponDefs[def]->grenade.smoke;
  bool match = gGame && gGame->active;
  if (phase == 0) {
    if (smoke) {
      Grenades& g = mpaiSmoke();
      double t = match ? gGame->t : gIdleT;
      g.addCloud(pos, t, (uint32_t)(pos.x * 7919.0f) ^ (uint32_t)(t * 1000.0));
      app.audio.play("bolt", pos, 0.3f);
      if (match && gNet && gNet->role == Net::HOST) { auto& c = g.clouds.back(); gNet->hostWorldEvent(json{{"t", "cloud"}, {"p", tojson(c.p)}, {"st", c.start}, {"seed", c.seed}, {"r", c.radius}, {"b", c.build}, {"dn", c.dense}, {"f", c.fade}}); }
      return true;
    }
    gInGrenadeBlast = true; gProjOwner = owner;   // the blast's shell-style personnel damage is replaced by the frag profile
    return false;
  }
  gInGrenadeBlast = false;
  if (match && !gGame->isClient) {
    gGame->explosionDamage(pos, 0, 0, owner < 0 ? gGame->localSlot : owner, true);
    gGame->hearSound(pos, -1, gGame->archetypes["perception"].value("hearBlast_m", 90.0f), owner < 0 ? gGame->localSlot : owner, 1);
  }
  return false;
}

void mpaiIdleSmoke(App& app, float dt) {   // walkthrough smoke (no match): age, expire, particles
  if (gGame && gGame->active) return;
  gIdleT += dt; gIdleSmoke.expire(gIdleT); gIdleSmoke.particles(app.fx, gIdleT, dt);
}

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
const char* volatile gCrumb = "start";
#ifdef _WIN32
#include <windows.h>
static LPTOP_LEVEL_EXCEPTION_FILTER gPrevFilter = nullptr;   // crashlog.cpp's backtrace writer runs after this line
static LONG WINAPI crashFilter(EXCEPTION_POINTERS* e) {
  logf("CRASH: exception 0x%08lx at %p, last stage: %s", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress, gCrumb ? gCrumb : "?");
  fflush(stdout);
  return gPrevFilter ? gPrevFilter(e) : EXCEPTION_CONTINUE_SEARCH;
}
void installCrashReporter() { static bool done = false; if (!done) { done = true; gPrevFilter = SetUnhandledExceptionFilter(crashFilter); } }
#else
void installCrashReporter() {}   // POSIX: crashlog.cpp's signal handler already logs gCrumb
#endif
