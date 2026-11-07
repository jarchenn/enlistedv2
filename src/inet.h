#pragma once
// Internet play (docs/multiplayer.md#internet-play): room codes, UDP hole punching and a relay fallback through the
// rendezvous server (tools/rendezvous, wire format in tools/rendezvous/PROTOCOL.md, magic "NMKR").
//
// Everything goes through the game's own UDP socket, so the endpoint the server observes is the NAT mapping the game
// really uses. A relayed peer is addressed by a *virtual* sockaddr: sin_port == 0 (never a real source port) and
// sin_addr holding the relay id; Net::Impl wraps anything sent to such an address in an 'L' datagram for the server
// and unwraps 'L' datagrams from the server into that virtual source, so the rest of the protocol (H / W / D / X,
// session tokens, timeouts) is unchanged for relayed peers.
#include <winsock2.h>
#include <ws2tcpip.h>
#include "common.h"
#include <functional>

constexpr uint32_t kRendezvousMagic = 0x524B4D4Eu;   // "NMKR" read as a little-endian u32
constexpr int kRendezvousPort = 27900;

class Inet {
public:
  // settings (exeDir()/net_user.json, outside content/ so the content hash is unaffected); NMK_INET_SERVER overrides
  std::string server, secret, lastCode, lastName = "Player";
  bool forceRelay = false;                    // skip punching (tests, or networks known to block it)
  void load();
  void save() const;

  // the server, resolved from `server` ("host" or "host:port")
  sockaddr_in serverAddr{}; bool serverOk = false; std::string serverErr;
  bool resolveServer();
  bool fromServer(const sockaddr_in& a) const { return serverOk && a.sin_addr.s_addr == serverAddr.sin_addr.s_addr && a.sin_port == serverAddr.sin_port; }

  // virtual addresses for relayed peers
  static bool isVirtual(const sockaddr_in& a) { return a.sin_port == 0 && a.sin_addr.s_addr != 0; }
  static sockaddr_in virt(uint32_t rid) { sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = rid; a.sin_port = 0; return a; }
  static uint32_t ridOf(const sockaddr_in& a) { return a.sin_addr.s_addr; }
  std::string wrap(uint32_t rid, const std::string& datagram) const;   // 'L' datagram for the server

  // ---- host: a room others join with a code
  bool hosting = false;
  std::string code, pub, hostStatus;
  int relayedPeers = 0;
  bool hostStart();                           // false: no / unresolvable server (hostStatus says why)
  void hostStop();

  // ---- client: join by code
  enum Phase { OFF, LOOKING, PUNCHING, RELAYING, HANDSHAKE, FAILED };
  Phase phase = OFF;
  std::string joinStatus;
  bool usingRelay = false;
  sockaddr_in target{}; bool targetReady = false;   // set when the host endpoint is known (direct or virtual)
  bool joinStart(const std::string& code);
  void joinStop() { phase = OFF; targetReady = false; }

  // per frame; `reg` = {name, map, humans, max} for REG; localPort = the game socket's bound port
  void tick(double now, const json& reg, int localPort);
  // a datagram that starts with "NMKR" (not 'L': Net unwraps those itself); returns true when handled
  bool onPacket(double now, const sockaddr_in& from, const uint8_t* p, size_t n);

  std::function<void(const sockaddr_in&, const std::string&)> send;   // raw send on the game socket
  int ver = 0;                                                         // kNetVersion

private:
  double now_ = 0, lastReg = -10, regStart = 0, lastLook = -10, lookStart = 0, punchStart = 0, lastPunch = -10, lastRelay = -10, relayStart = 0;
  bool gotRegOk = false;
  uint32_t nonce = 0;
  std::string joinCode;
  std::vector<sockaddr_in> cands;             // client: the host's candidate endpoints (pub first, then LAN)
  struct Target { std::vector<sockaddr_in> cands; uint32_t nonce = 0; double until = 0; std::string pub; };
  std::vector<Target> targets;                // host: clients being punched toward (at most 8)
  std::vector<uint32_t> rids;                 // host: relays allocated toward this host
  std::vector<std::string> lanEndpoints(int port) const;
  void control(char type, const json& j, const sockaddr_in& to);
  void punch(char type, uint32_t n, const sockaddr_in& to);
};
