#pragma once
// W6 bot AI core (AI_SPEC 3-11). One AiCore per match (host only), driven from Game::aiStep:
//   commander (1 Hz per team)  -> objective, lanes (25-35 % flankers, committed from spawn), fireteams, regroup,
//                                 defense slots
//   fireteam (3 Hz)            -> bounding overwatch pairs, target assignment, room clearing sequence
//   individual (5-10 Hz, staggered, 2 Hz beyond 300 m of every human) -> utility-scored actions
//   every step                 -> aiming / firing, corridor following, avoidance, link traversal, stuck ladder
// Paths are planned per bot on the navmesh by a worker thread (ai_path.cpp) with the bot's own costs (threat
// viewsheds of team knowledge, teammates' claimed routes, lane bias, edges it got stuck on).
// Tunables: content/ai/tactics.json, content/ai/navigation.json, content/ai/difficulty.json, content/settings/rules.json.
#include "game.h"
#include "physics.h"
#include <DetourNavMeshQuery.h>
#include <DetourPathCorridor.h>
#include <DetourLocalBoundary.h>
#include <DetourObstacleAvoidance.h>
#include <array>
#include <memory>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <set>
#include <chrono>

// ---------------------------------------------------------------- difficulty (AI_SPEC 10)
struct AiDifficulty {
  std::string name = "regular";
  float reaction = 0.35f, aimError = 2.5f, aimSettle = 0.7f, boundMax = 30, boundMaxUnderFire = 14, threatCost = 3.5f;
  float shareDelay = 1.5f, suppressionResist = 0.5f, grenadeCooldown = 30, detectOpen = 220;
  int overwatch = 1;   // 0 sometimes, 1 yes, 2 tight
  static AiDifficulty load(const json& file, const std::string& name);
};
const std::vector<std::string>& aiDifficultyNames();
std::string& aiDifficultySetting();      // match setup choice (default from rules.json "difficulty")

// ---------------------------------------------------------------- actions (AI_SPEC 9)
enum AiAction : int { A_ADVANCE = 0, A_RETURN_FIRE, A_SUPPRESS, A_TAKE_COVER, A_RELOAD, A_GRENADE, A_CLEAR, A_HOLD, A_EVADE, A_OVERWATCH, A_COUNT };
const char* aiActionName(int a);
enum AiSpeed : int { SPD_STOP = 0, SPD_CRAWL, SPD_CROUCH, SPD_WALK, SPD_JOG, SPD_SPRINT };

// ---------------------------------------------------------------- cover (AI_SPEC 5)
inline int64_t coverKey(float x, float z) { return ((int64_t)std::floor(x / 8.0f) << 32) ^ (int64_t)(uint32_t)(int)std::floor(z / 8.0f); }
struct CoverPoint {
  vec3 p{0}, n{0, 0, 1};       // position (on the mesh) and the direction the cover faces (toward the protected side)
  bool high = false, peekL = false, peekR = false, peekOver = false, authored = false, natural = false;
  int tx = 0, tz = 0;          // tile it was generated for (regenerated with it)
  int heldBy = -1; double heldUntil = 0;
  uint32_t serial = 0;
};

// ---------------------------------------------------------------- threat knowledge
struct Viewshed {             // what one known enemy position can see: max visible distance per azimuth
  static constexpr int kAz = 64;
  vec3 eye{0}; int enemy = -1; double t = 0; float range = 250; float conf = 1;
  std::array<float, kAz> dist{};
  bool sees(vec3 p) const;
};
struct ThreatSnapshot { std::vector<Viewshed> v; };
struct CrowdSnapshot {        // teammates' claimed route cells (2 m), per owner
  std::unordered_map<int64_t, std::array<int8_t, 4>> cells;   // up to 4 claimers per cell (slot, -1 empty)
  static int64_t key(float x, float z) { return ((int64_t)std::floor(x * 0.5f) << 32) ^ (int64_t)(uint32_t)(int)std::floor(z * 0.5f); }
  int claims(vec3 p, int except) const;
};

struct Known {                // team knowledge of one enemy
  int enemy = -1; vec3 pos{0}; double seen = -100, shared = -100; float conf = 0; bool heard = false; int by = -1;
};

struct Fireteam {
  int role = 0, lane = LANE_MAIN, id = 0;
  std::vector<int> members;   // soldier slots, pair A = even indices, pair B = odd
  int movingPair = 0; double pairSince = 0; bool overwatch = false;
  std::map<int, int> targetFor;   // member -> assigned enemy
  // room / bunker clearing (AI_SPEC 8.2)
  int clearBuilding = -1, clearRoom = -1, clearStage = 0; double clearT = 0; int clearEntered = 0;
  std::set<std::string> roomsCleared;
};

struct TeamBrain {
  int role = 0;
  std::map<int, Known> known;
  std::vector<Fireteam> fts;
  std::shared_ptr<const ThreatSnapshot> threat = std::make_shared<ThreatSnapshot>();
  std::shared_ptr<const CrowdSnapshot> crowd = std::make_shared<CrowdSnapshot>();
  std::vector<Viewshed> shedBuild;   // viewsheds under construction / current
  int stage = -1; double regroupUntil = -1; vec3 regroupAt{0};
  double nextCommand = 0, nextFireteam = 0, nextCrowd = 0;
};

// defense positions for one objective (AI_SPEC 6.6, SPEC 5.5)
struct DefSlot { vec3 p{0}; float yaw = 0; int priority = 1; int kind = 0; int heldBy = -1; double suppressedAt = -100; std::string src; };

// building / bunker clearing annotations, world space (from asset nav metadata or generated)
struct AiBuilding {
  uint64_t inst = 0; std::string asset; vec3 bmin{0}, bmax{0};
  struct Room { std::string id; vec3 mn{0}, mx{0}; };
  struct Entrance { vec3 p{0}, dir{0}; std::string room; };
  struct Corner { vec3 p{0}; std::string room; };
  struct Door { std::string a, b; vec3 p{0}; };
  std::vector<Room> rooms; std::vector<Entrance> entrances; std::vector<Corner> corners; std::vector<Door> doors;
  std::vector<NavFiringPoint> firing;   // world p, yaw
  bool generated = false, bunker = false;
  int roomAt(vec3 p) const;
};

// ---------------------------------------------------------------- per bot
struct StuckEvent { int slot = -1; double t0 = 0, t1 = 0; vec3 pos{0}; int step = 0; bool resolved = false, open = true; std::string why; };
struct EdgePenalty { vec3 p{0}; float r = 2; double until = 0; };

struct BotMind {
  int life = -1;
  // path
  std::unique_ptr<dtPathCorridor> corridor; bool hasPath = false, partial = false;
  vec3 goal{0}; float goalRadius = 1.0f; int goalSerial = 0; uint32_t pathNavStamp = 0;
  int pendingReq = 0; bool wantPath = false; float priority = 0; double pathAt = -100, lastReplan = -100;
  std::vector<std::pair<int, int>> pathTiles;   // tiles the corridor crosses (re-plan when they rebuild)
  double optT = 0, topoT = 0, validT = 0;
  // link traversal
  struct Trav { bool on = false; int kind = 0; vec3 p[4]{}; int np = 0; float t = 0, dur = 1; uint32_t link = 0; vec3 end{0}; } trav;
  int linksTaken = 0;
  // steering
  dtLocalBoundary boundary; double boundaryT = -100; vec3 boundaryPos{1e9f};
  vec3 avoidVel{0}; int avoidFrame = 0; std::vector<vec4> probeSegs; double probeT = 0;
  int speed = SPD_JOG; vec3 lastVel{0}; vec3 moveDir{0, 0, 1}; int posture = 0; std::vector<vec3> route; float pathFails = 0; double pathFailAt = -100; bool replanAfterBack = false;
  float doorWait = 0;
  // stuck ladder (AI_SPEC 3.6)
  vec3 progAnchor{0}; float progT = 0; int ladder = 0; double ladderT = 0; int eventIdx = -1; vec3 stuckPos{0};
  vec3 backTarget{0}; double backUntil = -100; bool needNewGoal = false; vec3 subGoal{0}; bool hasSubGoal = false;
  std::vector<EdgePenalty> penalties;
  // decisions
  int action = A_ADVANCE; double actionSince = 0; std::array<float, A_COUNT> util{}; double nextThink = 0, nextFt = 0;
  int team = 0, ft = -1, ftIdx = 0; bool flanker = false; bool flankReached = false; vec3 flankPos{0};
  int cover = -1; uint32_t coverSerial = 0; vec3 coverPos{0}; bool coverHigh = false; bool set = false; double setAt = -100, peekUntil = -100;
  int slot = -1;                       // defense slot index
  double boundStart = -100; vec3 boundFrom{0}; float exposedThisBound = 0; bool bounding = false;
  // perception extras
  double nearMissAt = -100; vec3 nearMissFrom{0}; double grenadeSeenAt = -100;
  // aim model (AI_SPEC 6.4)
  int aimTarget = -1; float aimErr = 0, onTarget = 0; vec2 aimOff{0};
  // clearing
  int clearSector = 0, clearCorner = 0; vec3 clearGoal{1e9f}; double clearGoalT = 0;
  // stats
  double underFireT = 0, exposedT = 0, lastAct = 0;
};

// ---------------------------------------------------------------- path service (ai_path.cpp)
class BotFilter : public dtQueryFilter {
public:
  std::shared_ptr<const ThreatSnapshot> threat; std::shared_ptr<const CrowdSnapshot> crowd;
  float threatCost = 1, crowdCost = 0.6f, laneGain = 0.75f, laneHalfWidth = 25, laneOffset = 0; bool laneOn = false;
  vec2 laneOrigin{0}, laneAxis{0, 1}, laneRight{1, 0};
  float linkExtra[4] = {2, 4, 2.5f, 3};
  std::vector<EdgePenalty> penalties; int self = -1;
  float getCost(const float* pa, const float* pb, const dtPolyRef prevRef, const dtMeshTile* prevTile, const dtPoly* prevPoly,
                const dtPolyRef curRef, const dtMeshTile* curTile, const dtPoly* curPoly,
                const dtPolyRef nextRef, const dtMeshTile* nextTile, const dtPoly* nextPoly) const override;
};

struct PathResult { int id = 0, slot = -1, life = -1; bool ok = false, partial = false; std::vector<NavRef> polys; vec3 target{0}; double ms = 0; uint32_t navStamp = 0; };
class PathService {
public:
  ~PathService();
  void start(Nav* nav);
  void stop();
  int submit(int slot, int life, NavRef startRef, vec3 start, NavRef endRef, vec3 end, std::unique_ptr<BotFilter> f, float priority);
  void cancelSlot(int slot);
  std::vector<PathResult> poll();
  int inFlight() const;
  int completed = 0, failed = 0; double msSum = 0, msMax = 0;
private:
  struct Req { int id, slot, life; NavRef s, e; vec3 sp, ep; std::unique_ptr<BotFilter> f; float pri; };
  Nav* nav_ = nullptr;
  mutable std::mutex m_; std::condition_variable cv_;
  std::deque<Req> q_; std::vector<PathResult> done_; std::vector<std::thread> th_; bool quit_ = false; int next_ = 1; int running_ = 0;
  void loop();
};

// ---------------------------------------------------------------- profiling (AI_SPEC 11)
enum AiProfCat : int { PROF_NAV = 0, PROF_PERCEPTION, PROF_DECISIONS, PROF_AIMING, PROF_MOVEMENT, PROF_TOTAL, PROF_COUNT };
struct AiProf {
  double frame[PROF_COUNT]{}, sum[PROF_COUNT]{}, max[PROF_COUNT]{}; long frames = 0; std::vector<double> totals;
  struct Scope { AiProf& p; int c; std::chrono::steady_clock::time_point t0; Scope(AiProf& pp, int cc) : p(pp), c(cc), t0(std::chrono::steady_clock::now()) {}
    ~Scope() { p.frame[c] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); } };
  static const char* name(int c);
};

// ---------------------------------------------------------------- metrics (AI_SPEC 12.3)
struct AiMetrics {
  double t = 0, advT = 0, advSpeedSum = 0; long advN = 0; double nnSum = 0; long nnN = 0;
  double fireT = 0, exposedT = 0, coverNear5T = 0; int bounds = 0; double boundExposure = 0;
  int stuckEvents = 0, stuckOver5 = 0, stuckUnrecovered = 0, stuckResolvedBy[7]{};
  int grenadesThrown = 0, grenadesHurtTeammate = 0, rebuildReplans = 0, linkTraversals = 0, linkAborts = 0, snaps = 0, linkKinds[4]{};
  int attackerDeaths0 = -1;
  json toJson(const Game& G) const;
};

class AiCore {
public:
  explicit AiCore(Game& g) : G(g) {}
  Game& G;
  json tactics, navj, diffj, rules;
  AiDifficulty diff;
  std::vector<std::unique_ptr<BotMind>> minds;   // index = soldier slot
  TeamBrain teams[2];                    // index = (int)CombatRole
  std::vector<CoverPoint> cover; std::unordered_map<int64_t, std::vector<int>> coverGrid; uint32_t coverSerial = 1;
  std::vector<std::pair<int, int>> coverTodo;   // tiles waiting for cover generation
  std::vector<DefSlot> defSlots; int defStage = -2;
  std::vector<AiBuilding> buildings; std::set<std::string> clearedRooms;   // "instance:room" cleared this match
  PathService paths;
  AiProf prof; AiMetrics metrics;
  std::vector<StuckEvent> stuck;
  bool frozen = false, stepRun = false; int stepFrames = 0; int follow = -1; bool overlayNav = true, overlayPaths = true, overlayCover = false, overlayThreats = true;
  int rayBudget = 200, raysUsed = 0, raysThisFrame = 0; long raysTotal = 0;
  int pathStartsPerFrame = 4;
  uint64_t lastFrameStep = ~0ull; double frameT = 0;
  std::map<uint64_t, std::pair<uint64_t, std::pair<vec3, vec3>>> instPrint;   // instance -> fingerprint, bounds
  std::map<int, uint32_t> terrainRev; uint32_t lastGroundRev = 0; double nextWorldPoll = 0;
  std::map<uint32_t, std::pair<int, vec3>> grenadeWatch;   // live grenade id -> (owner, last position)
  struct Rebuild { double at; vec3 lo, hi; };
  std::vector<Rebuild> rebuildLog;       // creative live rebuild timing (tests)
  double lastCommitAt = -100; int committedTiles = 0, commitLogs = 0;
  std::string mapName;
  // speeds (movement.json) and tunables
  float vSprint = 6.325f, vJog = 3.1625f, vWalk = 1.6f, vCrouch = 1.32f, vProne = 0.66f, gravity = 7.848f, jumpV = 3.757f;
  int perceptionCursor = 0;
  class dtObstacleAvoidanceQuery* oaq = nullptr; dtObstacleAvoidanceParams oaParams{};

  // ---- lifecycle (ai_core.cpp)
  void begin(vec3 lo, vec3 hi);
  void end();
  void frame();                          // once per Game step, before the first bot
  BotMind& mind(Soldier& s);
  bool ray(vec3 o, vec3 d, float len, Physics::Hit* h = nullptr);   // budgeted raycast (true = hit)
  bool rayFree() const { return raysThisFrame < rayBudget; }
  float tj(const char* sect, const char* key, float def) const;
  bool humanSees(const Soldier& s);
  float nearestHumanDist(vec3 p) const;
  void logStuck(const StuckEvent& e);
  void pollWorld(bool rebuild = true);
  void onTilesChanged();
  // ---- knowledge (ai_core.cpp)
  void shareKnowledge();
  void updateViewsheds();
  void updateCrowd(TeamBrain& tb);
  float exposure(int role, vec3 p) const;     // 0..1 how visible p is to enemies known to `role`'s team
  // ---- paths and movement (ai_path.cpp)
  void requestPath(Soldier& s, vec3 goal, float radius, float priority, bool force = false);
  void dispatchPaths();
  void receivePaths();
  std::unique_ptr<BotFilter> makeFilter(Soldier& s);
  void move(Soldier& s, float dt);
  bool arrived(Soldier& s, float r = -1) const;
  float pathRemaining(Soldier& s) const;
  void stuckLadder(Soldier& s, BotMind& m, float dt, bool wantsMove);
  void startTraversal(Soldier& s, BotMind& m, NavRef offRef, vec3 sp, vec3 ep);
  bool stepTraversal(Soldier& s, BotMind& m, float dt);
  // ---- cover (ai_cover.cpp)
  void generateCoverTile(int tx, int tz, int maxRays);
  void addAuthoredCover();
  int pickCover(Soldier& s, vec3 goal, float maxDist, float minProgress, bool reactive, vec3 fireFrom);
  void releaseCover(Soldier& s);
  bool coverProtects(const CoverPoint& c, vec3 threat);
  void buildDefSlots();
  int pickDefSlot(Soldier& s);
  void scanBuildings();
  // ---- decision layers (ai_layers.cpp)
  void commander(TeamBrain& tb);
  void fireteamStep(TeamBrain& tb, Fireteam& ft);
  void think(Soldier& s);
  void act(Soldier& s, float dt);
  vec3 objectiveGoal(Soldier& s);
  // ---- combat (ai_combat.cpp)
  void perceive(Soldier& s);
  void nearMisses();
  void aim(Soldier& s, float dt);
  bool planGrenade(Soldier& s, vec3 target, vec3& vel);
  void watchGrenades();
  // ---- debug (ai_debug.cpp)
  void drawDebug();
};

AiCore* aiCore();                         // current match (host), or nullptr
#include "ai_hooks.h"   // one-line hooks (game.cpp, mpui.cpp)
