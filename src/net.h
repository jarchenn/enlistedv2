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
constexpr int kNetVersion = 3;   // 3: station block in snapshots, gun / rack / crate / build requests (2 is the internet branch's)

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
  // stats
  uint64_t bytesOut = 0, bytesIn = 0, packetsOut = 0, packetsIn = 0, resends = 0, dropped = 0;
  int rejectedRequests = 0, acceptedShots = 0, acceptedGunShots = 0, refusedJoins = 0;
  std::map<std::string, int> rejects;       // host: refused interaction requests by reason ("gun: taken", "build: dead", ...)
  std::map<uint64_t, int> gunOcc;           // every PC: station id -> occupant slot, as the host reports it

  bool host(App& app, std::string& err, int port = kNetPort);
  bool join(App& app, const std::string& ip, const std::string& name, std::string& err, int port = kNetPort);
  void discover();                          // send a broadcast query (results arrive in `found`)
  void update(App& app, float dt);          // every frame
  void leave(const char* reason);           // client: disconnect; host: end the session (clients return to menu)
  int humanCount() const { return role == HOST ? (int)players.size() : 1; }
  bool allReady() const;
  void setReady(bool r);
  void startMatch(App& app);                // host: lobby -> match (all clients receive START)
  void returnToLobby(App& app);             // host: after the result
  static std::string contentHash();
  static std::string mapHash(const App& app);
  // game-layer hooks (called by Game / mpai)
  void hostWorldEvent(const json& ev);      // explosion, impact, door, cloud, kill, log -> all clients in order
  void clientFire(vec3 from, vec3 dir, int weapon, bool tracer);
  void clientReload();
  void clientThrow(int kind, vec3 vel);
  void clientInteract(uint64_t inst, int idx);
  void clientRequest(const json& m);        // gun / gfire / greload / rack / crate / build (validated by the host)
  void sendStats(const json& s);            // client -> host diagnostics (tests)
  std::map<int, json> peerStats() const;
  struct Impl; Impl* d = nullptr;
};

extern Net* gNet;
