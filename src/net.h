#pragma once
// LAN multiplayer: listen server over Winsock2 UDP (docs/multiplayer.md).
//   transport: one UDP socket; per peer a reliable ordered channel (chunked, acked, resent) plus unreliable
//   snapshots / inputs (latest wins). Discovery: broadcast query on the game port, hosts answer with lobby info.
//   authority: the host runs the match (Game); clients send movement (validated), fire / reload / throw / door
//   requests, and gun / rack / crate / build requests; the host replicates soldiers and crew-served guns (20 Hz
//   snapshots), world events (explosions, bullet impacts, doors, smoke, placements, crate stock)
//   in order, and a full world state to late joiners.
#include "common.h"

class App;

constexpr int kNetPort = 27960;
constexpr int kNetVersion = 42;   // 42: FK2 AI type 45 Fallen Knight 2; 41: OPF1 AI type 44 Giant Operator; 40: NPC1 AI type 43 Civilians; 39: KS AI type 42 Spear Knight; 38: FR5 weapon pkm; 37: FR2 weapons g43 / ppsh41 (weapon indices shift); 36: VK1 vickers_k weapon def (the KR-714's gun; weapon indices shift); 35: MT1 AI type 41 Mech Tank (never placed on LAN); 34: DR1 AI type 40 Dragon; 33: FK1 AI type 39 Fallen Knight; 32: AK1 AI type 38 Assorted Knights; 31: HK1 AI type 37 Mounted Knight; 30: AR2 AI type 36 Archer 2; 29: K10 AI type 35 Knight 10; 28: AR1 AI type 34 Archer 1 (ranged: arrows as crossbow-bolt rounds, tracer kind -1); 27: MG13 modern_minigun (weapon indices shift; spinUp); 26: XB1 weapon defs + modern_crossbow (weapon indices shift); 25: KN8 / KN9 AI types 32 / 33, strike spheres on the weapons (attacks bone + at); 24: KN7 AI type 31 Knight 7; 23: KN6 AI type 30 Knight 6, the aiTypes mask 64-bit; 22: KN4 / KN5 AI types 28 / 29 Knight 4 / Knight 5; 21: KN3 AI type 27 Knight 3; 20: RB1 AI type Mechs (24) spawns the KR-714 robot soldier (kit 12) with perf mechrobot; 19: KN2 AI type 26 Knight 2 (aiTypes / aiCount bit 26); 18: SD3 rules sdVehicles (vehicles out of the world on every peer), plant pads;  17: SD1 Search and Destroy ("sd" state / "sdboom" events, "sdhold" / "sdbuy" requests, rules sdShort / sdWarmup, mode "snd");  16: MV3 the players' lean (client input posture bits 2-6, snapshot per-soldier byte bits 1-5);  15: AI3 per-type AI counts in the rules (clients assign the types from them);  14: AI types Abominations / Golems / Boglords (three more checklist bits);  13: AI2 up to 1000 AI: 16-bit slot ids in snapshots (interest-filtered, far soldiers at 10 Hz);  12: AI types Knights / Animatronics / Aliens / Alien queens (four more checklist bits); 11: AI types Brutes / Spiders / Creatures (Heavies split; two more checklist bits); 10: a seventh AI type checklist bit (that type removed in RM1, its bit kept); 9: AI types as a per-side checklist (rules "aiTypes" masks); 8: EN3 AI type Heavies + heavyTrollPct in the rules; 7: EN1 u16 hp + enemy type per soldier;   // 4: G1 modes (team in START, u16 kills / deaths + score, Solo winner); 5: PF1 snapshot in parts (200 AI); 6: FT1 flame / burn events
// was 3:   // 3: station block in snapshots, gun / rack / crate / build requests (2 is the internet branch's)

struct LobbyInfo { std::string addr, hostName, map, mapHash, contentHash; int humans = 0, maxHumans = 0, phase = 0, version = 0; double seen = 0; };
struct LobbyPlayer { int peer = 0; std::string name; bool ready = false; };

class Net {
public:
  enum Role { NONE, HOST, CLIENT };
  Role role = NONE;
  std::string status;                       // user-facing state / last error
  bool connected = false;                   // client: welcomed by the host
  int myPeer = 0;                           // client: id assigned by the host (host = 0)
  std::vector<LobbyPlayer> players;         // lobby roster (host's view, replicated to clients)
  std::vector<LobbyInfo> found;             // discovery results
  json lobbyRules;                          // host's setup rules (replicated)
  std::string hostMapName;
  bool matchRunning = false;
  // simulated network conditions (tests / tuning): one-way delay and loss on everything this process sends
  float simLagMs = 0, simLoss = 0;
  bool simDeaf = false;   // tests: ignore every datagram from the host (a host that went silent)
  // stats
  uint64_t bytesOut = 0, bytesIn = 0, packetsOut = 0, packetsIn = 0, resends = 0, dropped = 0;
  int rejectedRequests = 0, acceptedShots = 0, acceptedGunShots = 0, refusedJoins = 0;
  std::map<std::string, int> rejects;       // host: refused interaction requests by reason ("gun: taken", "build: dead", ...)
  std::map<uint64_t, int> gunOcc;           // every PC: station id -> occupant slot, as the host reports it

  bool host(App& app, std::string& err, int port = kNetPort);
  bool join(App& app, const std::string& ip, const std::string& name, std::string& err, int port = kNetPort);
  void discover();                          // send a broadcast query (results arrive in `found`)
  void update(App& app, float dt);          // every frame
  void keepAlive();                         // PF1: during a long blocking step (the first navmesh bake): acks / keep-alives so peers do not time out
  void leave(const char* reason);           // client: disconnect; host: end the session (clients return to menu)
  int humanCount() const { return role == HOST ? (int)players.size() : 1; }
  bool allReady() const;
  void setReady(bool r);
  void startMatch(App& app);                // host: lobby -> match (all clients receive START)
  void returnToLobby(App& app);             // host: after the result
  static std::string contentHash();
  static std::string contentHashAt(const fs::path& contentRoot);   // contentHash() of another content folder (tests)
  static std::string contentHashOf(const fs::path& contentRoot, std::initializer_list<const char*> subs, const std::string& seed);   // the same over chosen folders (PF1 nav key: assets)
  static std::string mapHash(const App& app);
  // game-layer hooks (called by Game / mpai)
  void hostWorldEvent(const json& ev);      // explosion, impact, door, cloud, kill, log -> all clients in order
  void clientFire(vec3 from, vec3 dir, int weapon, int tracer);   // tracer: tracerKindFor()
  void clientFlame(vec3 from, vec3 dir, int weapon, float dt);    // FT1: the flamethrower's nozzle was open for dt (10 Hz while firing)
  void clientReload();
  void clientDeploy(const std::string& cls, int p0, int p1);   // GM1: class choice for the next spawn
  void clientThrow(int kind, vec3 vel);
  void clientInteract(uint64_t inst, int idx);
  void clientRequest(const json& m);        // gun / gfire / greload / rack / crate / build (validated by the host)
  void sendStats(const json& s);            // client -> host diagnostics (tests)
  std::map<int, json> peerStats() const;
  struct Impl; Impl* d = nullptr;
};

extern Net* gNet;
