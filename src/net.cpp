// LAN multiplayer over Winsock2 UDP (see net.h, docs/multiplayer.md).
#include <winsock2.h>
#include <ws2tcpip.h>
#include "net.h"
#include "inet.h"
#include "game.h"
#include "mpui.h"
#include "app.h"
#include "mpai.h"
#include "crumb.h"
#include "rules.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <deque>
#include <cstring>
#include <algorithm>
#include <random>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

Net* gNet = nullptr;

namespace {
constexpr uint32_t kMagic = 0x314B4D4Eu;   // "NMK1"
constexpr size_t kChunk = 1000;
constexpr double kTimeout = 6.0, kResend = 0.15;
constexpr double kConnectTimeout = 20.0;   // first contact: the host may be busy loading a map or starting a match
// Internet-facing limits (docs/multiplayer.md#security). A reliable message is at most kMaxMessage bytes; chunks more
// than kWindow ahead of the next expected one are a protocol violation; unauthenticated queries / hellos are limited
// per source address; a discovery query must be padded to kQueryPad bytes so the answer never amplifies.
constexpr uint32_t kWindow = 256;
constexpr size_t kMaxMessage = 8 * 1024 * 1024, kQueryPad = 256, kMaxName = 24;
constexpr int kUnauthPerSec = 4;
// Largest game datagram: fits a 1500-byte internet MTU with IP / UDP headers, the relay's 9-byte 'L' header and a
// margin for tunnels (PPPoE, VPNs), so nothing is ever IP-fragmented (fragments are often dropped on the internet).
constexpr size_t kMaxPacket = 1360;

struct W {
  std::string b;
  void u8(uint8_t v) { b.push_back((char)v); }
  void u16(uint16_t v) { b.append((const char*)&v, 2); }
  void u32(uint32_t v) { b.append((const char*)&v, 4); }
  void u64(uint64_t v) { b.append((const char*)&v, 8); }
  void i16(int16_t v) { b.append((const char*)&v, 2); }
  void f32(float v) { b.append((const char*)&v, 4); }
  void v3(vec3 v) { f32(v.x); f32(v.y); f32(v.z); }
  void str(const std::string& s) { u32((uint32_t)s.size()); b += s; }
};
struct R {
  const uint8_t* p; size_t n, o = 0; bool ok = true;
  R(const void* d, size_t len) : p((const uint8_t*)d), n(len) {}
  bool need(size_t k) { if (o + k > n) { ok = false; return false; } return true; }
  uint8_t u8() { if (!need(1)) return 0; return p[o++]; }
  uint16_t u16() { uint16_t v = 0; if (need(2)) { std::memcpy(&v, p + o, 2); o += 2; } return v; }
  uint32_t u32() { uint32_t v = 0; if (need(4)) { std::memcpy(&v, p + o, 4); o += 4; } return v; }
  uint64_t u64() { uint64_t v = 0; if (need(8)) { std::memcpy(&v, p + o, 8); o += 8; } return v; }
  int16_t i16() { int16_t v = 0; if (need(2)) { std::memcpy(&v, p + o, 2); o += 2; } return v; }
  float f32() { float v = 0; if (need(4)) { std::memcpy(&v, p + o, 4); o += 4; } if (!std::isfinite(v)) { ok = false; v = 0; } return v; }
  vec3 v3() { float x = f32(), y = f32(), z = f32(); return vec3(x, y, z); }
  std::string str() { uint32_t k = u32(); if (k > 1u << 24 || !need(k)) { ok = false; return {}; } std::string s((const char*)p + o, k); o += k; return s; }
};

struct Chan {
  struct Out { uint32_t id; std::string data; double sent = -1; };
  uint32_t nextOut = 0, nextIn = 0;
  std::deque<Out> out;
  std::map<uint32_t, std::string> early;
  std::string partial;
  void send(const std::string& msg) {
    size_t o = 0;
    do {
      size_t k = std::min(kChunk, msg.size() - o);
      std::string c; c.push_back(o + k < msg.size() ? 1 : 0); c.append(msg, o, k);
      out.push_back({nextOut++, c});
      o += k;
    } while (o < msg.size());
  }
  void acked(uint32_t ackNext) { while (!out.empty() && (int32_t)(out.front().id - ackNext) < 0) out.pop_front(); }
  // deliver contiguous chunks, return complete messages; false = protocol violation (a message larger than
  // kMaxMessage): the caller drops the peer. Chunks beyond the receive window are ignored (bounded memory); the
  // sender keeps them unacked and resends them once the window has moved.
  bool receive(uint32_t id, const std::string& c, std::vector<std::string>& msgs) {
    if ((int32_t)(id - nextIn) < 0) return true;            // duplicate
    if (id - nextIn >= kWindow) return true;
    early[id] = c;
    for (auto it = early.find(nextIn); it != early.end(); it = early.find(nextIn)) {
      const std::string& x = it->second;
      if (!x.empty()) {
        if (partial.size() + x.size() > kMaxMessage) return false;
        partial.append(x, 1, std::string::npos);
        if (x[0] == 0) { msgs.push_back(partial); partial.clear(); }
      }
      early.erase(it); ++nextIn;
    }
    return true;
  }
};

struct Peer {
  int id = 0; sockaddr_in addr{}; std::string name; bool ready = false; double heard = 0; Chan ch;
  uint64_t token = 0;   // random per session; every D / X to or from this peer carries it (spoofed packets are dropped)
  std::string unrel; int unrelType = 0;
  double lastSent = 0;
  bool inMatch = false;
};
std::string addrStr(const sockaddr_in& a) { char b[64]; inet_ntop(AF_INET, &a.sin_addr, b, sizeof b); return std::string(b) + ":" + std::to_string(ntohs(a.sin_port)); }
bool sameAddr(const sockaddr_in& a, const sockaddr_in& b) { return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port; }
uint64_t fnv(const std::string& s, uint64_t h = 1469598103934665603ull) { for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; } return h; }
std::string hex(uint64_t v) { char b[24]; std::snprintf(b, sizeof b, "%016llx", (unsigned long long)v); return b; }
uint64_t randomToken() { std::random_device rd; uint64_t t = 0; while (t == 0) t = ((uint64_t)rd() << 32) ^ rd(); return t; }
std::string cleanName(std::string s) {   // printable ASCII, at most kMaxName characters
  std::string o; for (unsigned char c : s) if (c >= 32 && c < 127 && o.size() < kMaxName) o.push_back((char)c);
  return o.empty() ? std::string("Player") : o;
}
}

struct Net::Impl {
  SOCKET s = INVALID_SOCKET; bool wsa = false; int port = kNetPort, localPort = 0;
  Inet inet;                                 // internet rooms (inet.h)
  bool awaitHost = false;                    // client joining by code: the host endpoint is not known yet
  bool inetLoaded = false;
  Impl() {
    inet.ver = kNetVersion;
    inet.send = [this](const sockaddr_in& to, const std::string& b) { if (s != INVALID_SOCKET) sendto(s, b.data(), (int)b.size(), 0, (const sockaddr*)&to, sizeof to); };
  }
  // the wire: a virtual (relayed) address goes to the room server wrapped in an 'L' datagram
  void wire(const sockaddr_in& to, const std::string& data) {
    if (Inet::isVirtual(to)) {
      if (!inet.serverOk) return;
      std::string b = inet.wrap(Inet::ridOf(to), data);
      sendto(s, b.data(), (int)b.size(), 0, (const sockaddr*)&inet.serverAddr, sizeof inet.serverAddr);
    } else sendto(s, data.data(), (int)data.size(), 0, (const sockaddr*)&to, sizeof to);
  }
  std::vector<Peer> peers;                   // host: clients; client: [0] = host
  int nextPeer = 1;
  struct Delayed { double at; sockaddr_in to; std::string data; };
  std::vector<Delayed> delayq;
  std::mt19937 rng{1234};
  double now = 0, lastDiscover = -10, lastHello = -10, lastSnap = 0, lastInput = 0;
  std::string helloJson; sockaddr_in hostAddr{};
  bool lobbyDirty = true;
  std::vector<json> worldLog;               // host: world events since match start (late-join replay)
  json pendingStart;                        // client: START received, waiting to enter Explore
  std::vector<json> pendingWorld;           // client: world replay waiting for the match to begin
  std::vector<json> pendingEvents;
  bool wantLobbyReturn = false, wantEnd = false; std::string endReason;
  // client: interpolation buffer per soldier
  struct NetPose { vec3 feet[2]{}; float yaw[2]{}, pitch[2]{}; double t[2]{-1, -1}; };
  std::map<int, NetPose> poses;
  std::map<int, json> stats;                // host: diagnostics reported by clients
  double serverT = 0, serverTAt = 0;
  // unauthenticated traffic (Q / H): per source address, at most kUnauthPerSec per second
  struct Rate { double t0 = -10; int n = 0; };
  std::map<uint32_t, Rate> unauth;
  bool allowUnauth(const sockaddr_in& a) {
    if (unauth.size() > 4096) unauth.clear();   // bounded memory under a spray of spoofed sources
    Rate& r = unauth[a.sin_addr.s_addr];
    if (now - r.t0 >= 1.0) { r.t0 = now; r.n = 0; }
    return ++r.n <= kUnauthPerSec;
  }
  // map hash for discovery / hello answers: serialising the map is expensive, so at most once per second
  std::string mapHashCache; double mapHashAt = -10;
  const std::string& mapHashCached(const App& app) { if (now - mapHashAt > 1.0 || mapHashCache.empty()) { mapHashCache = Net::mapHash(app); mapHashAt = now; } return mapHashCache; }
  bool warnedSnapshot = false;
  void sendRaw(const sockaddr_in& to, const std::string& data, Net& n) {
    if (n.simLoss > 0 && std::uniform_real_distribution<float>(0, 1)(rng) < n.simLoss) { ++n.dropped; return; }
    if (n.simLagMs > 0) { delayq.push_back({now + n.simLagMs / 1000.0, to, data}); return; }
    wire(to, data);
    n.bytesOut += data.size(); ++n.packetsOut;
  }
  void flushDelayed(Net& n) {
    for (size_t k = 0; k < delayq.size();) {
      if (delayq[k].at <= now) { wire(delayq[k].to, delayq[k].data); n.bytesOut += delayq[k].data.size(); ++n.packetsOut; delayq.erase(delayq.begin() + k); }
      else ++k;
    }
  }
  void header(W& w, char type) { w.u32(kMagic); w.u8((uint8_t)type); }
  void flushPeer(Peer& p, Net& n, int myId) {
    // up to 16 packets: (re)send due chunks, piggyback acks and the latest unreliable payload
    int packets = 0; bool unrelSent = p.unrel.empty();
    size_t idx = 0;
    while (packets < 16) {
      W w; header(w, 'D'); w.u32((uint32_t)myId); w.u64(p.token); w.u32(p.ch.nextIn);
      size_t countPos = w.b.size(); w.u8(0);
      int n2 = 0;
      for (; idx < p.ch.out.size(); ++idx) {
        auto& c = p.ch.out[idx];
        if (c.id - p.ch.out.front().id >= kWindow) { idx = p.ch.out.size(); break; }   // stay inside the receiver's window
        if (c.sent >= 0 && now - c.sent < kResend) continue;
        if (w.b.size() + 6 + c.data.size() + 3 > kMaxPacket) break;   // never an IP-fragmented datagram (relay, internet)
        if (c.sent >= 0) ++n.resends;
        w.u32(c.id); w.u16((uint16_t)c.data.size()); w.b += c.data; c.sent = now; ++n2;
        if (n2 >= 40) { ++idx; break; }
      }
      w.b[countPos] = (char)n2;
      if (!unrelSent && w.b.size() + 3 + p.unrel.size() <= kMaxPacket) { w.u8((uint8_t)p.unrelType); w.u16((uint16_t)p.unrel.size()); w.b += p.unrel; unrelSent = true; }
      else {
        w.u8(0);
        if (!unrelSent && n2 == 0 && !warnedSnapshot) { warnedSnapshot = true; logf("net: an unreliable payload of %zu bytes does not fit a packet and is dropped (snapshot too large for this many soldiers)", p.unrel.size()); }
      }
      bool due = n2 > 0 || now - p.lastSent > 0.05;
      if (!due && unrelSent && p.unrel.empty()) break;
      if (n2 == 0 && w.b.size() <= 15 && !due) break;
      sendRaw(p.addr, w.b, n); p.lastSent = now; ++packets;
      if (n2 == 0) break;
    }
    p.unrel.clear();
  }
};

static Game& G(App& app) { if (!gGame) gGame = new Game(app); return *gGame; }

static std::string gContentHash;   // reading every content file is slow: computed once, refreshed by host() / join()
std::string Net::contentHash() {
  if (!gContentHash.empty()) return gContentHash;
  // build protocol + gameplay content (assets, weapons, AI data, characters); sizes + leading bytes keep it fast
  uint64_t h = fnv("nmk-net-" + std::to_string(kNetVersion));
  std::vector<fs::path> files;
  for (const char* sub : {"assets", "weapons", "ai", "characters"}) {
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(contentDir() / sub, ec)) if (e.is_regular_file()) { auto ext = e.path().extension().string(); if (ext == ".json" || ext == ".glb" || ext == ".bin") files.push_back(e.path()); }
  }
  std::sort(files.begin(), files.end());
  for (auto& f : files) {
    std::error_code ec; auto sz = fs::file_size(f, ec);
    h = fnv(fs::relative(f, contentDir(), ec).generic_string() + "#" + std::to_string(sz), h);
    if (f.extension() == ".json") h = fnv(readTextFile(f), h);
  }
  gContentHash = hex(h);
  return gContentHash;
}
std::string Net::mapHash(const App& app) {
  json j = mapToJson(app.doc);
  j.erase("scenario"); j.erase("editorCamera"); j.erase("exploreStart"); j.erase("name");   // the host sends the scenario itself
  return hex(fnv(j.dump()));
}

static bool openSocket(Net::Impl& d, int port, std::string& err) {
  if (!d.wsa) { WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w) != 0) { err = "Winsock could not start"; return false; } d.wsa = true; }
  if (d.s != INVALID_SOCKET) { closesocket(d.s); d.s = INVALID_SOCKET; }   // e.g. the discovery socket: never leak it
  d.s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (d.s == INVALID_SOCKET) { err = "could not create a UDP socket"; return false; }
  BOOL yes = TRUE; setsockopt(d.s, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof yes);
  u_long nb = 1; ioctlsocket(d.s, FIONBIO, &nb);
  // Windows reports an ICMP "port unreachable" for an earlier send as WSAECONNRESET on the next recvfrom; with probes
  // to stale or unreachable endpoints that would happen constantly, so turn it off
  BOOL off = FALSE; DWORD br = 0; WSAIoctl(d.s, SIO_UDP_CONNRESET, &off, sizeof off, nullptr, 0, &br, nullptr, nullptr);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons((u_short)port);
  if (bind(d.s, (sockaddr*)&a, sizeof a) != 0) {
    err = port ? "port " + std::to_string(port) + " is in use (another host on this PC?)" : "could not bind a UDP socket";
    closesocket(d.s); d.s = INVALID_SOCKET; return false;
  }
  sockaddr_in me{}; int ml = sizeof me; d.localPort = getsockname(d.s, (sockaddr*)&me, &ml) == 0 ? ntohs(me.sin_port) : port;
  if (!d.inetLoaded) { d.inet.load(); d.inetLoaded = true; }
  return true;
}

bool Net::host(App& app, std::string& err, int port) {
  if (!d) d = new Impl();
  if (role != NONE) leave("restart");
  gContentHash.clear(); contentHash();   // fresh for this session (content may have been edited since the last one)
  if (!openSocket(*d, port, err)) { status = "Could not host: " + err; return false; }
  d->port = port; role = HOST; myPeer = 0; matchRunning = false;
  players = {{0, "Host", true}};
  char hn[256] = "host"; gethostname(hn, sizeof hn);
  std::string ips;
  addrinfo hints{}, *res = nullptr; hints.ai_family = AF_INET;
  if (getaddrinfo(hn, nullptr, &hints, &res) == 0) { for (auto* r = res; r; r = r->ai_next) { char b[64]; inet_ntop(AF_INET, &((sockaddr_in*)r->ai_addr)->sin_addr, b, sizeof b); ips += std::string(ips.empty() ? "" : ", ") + b; } freeaddrinfo(res); }
  status = "Hosting on " + (ips.empty() ? std::string("this PC") : ips) + " port " + std::to_string(port) + ". If Windows asks, allow Normandy Mapmaker on private networks.";
  hostMapName = app.doc.name;
  d->lobbyDirty = true;
  logf("net: hosting (%s)", status.c_str());
  return true;
}

bool Net::join(App& app, const std::string& ip, const std::string& name, std::string& err, int port) {
  if (!d) d = new Impl();
  if (role != NONE) leave("rejoin");
  gContentHash.clear(); contentHash();
  if (!openSocket(*d, 0, err)) { status = "Could not join: " + err; return false; }
  // "1.2.3.4", "1.2.3.4:27960", "myhost.duckdns.org" or "myhost.duckdns.org:27960" (port forwarding over the internet)
  std::string hostPart = ip; int hostPort = port;
  if (auto c = ip.rfind(':'); c != std::string::npos) { hostPart = ip.substr(0, c); try { hostPort = std::stoi(ip.substr(c + 1)); } catch (...) { hostPort = -1; } }
  while (!hostPart.empty() && hostPart.back() == ' ') hostPart.pop_back();
  while (!hostPart.empty() && hostPart.front() == ' ') hostPart.erase(hostPart.begin());
  sockaddr_in a{}; a.sin_family = AF_INET;
  bool resolved = hostPort > 0 && hostPort < 65536 && inet_pton(AF_INET, hostPart.c_str(), &a.sin_addr) == 1;
  if (!resolved && hostPort > 0 && hostPort < 65536 && !hostPart.empty()) {
    addrinfo hints{}, *res = nullptr; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(hostPart.c_str(), nullptr, &hints, &res) == 0 && res) { a.sin_addr = ((sockaddr_in*)res->ai_addr)->sin_addr; resolved = true; }
    if (res) freeaddrinfo(res);
  }
  if (!resolved) { err = "'" + ip + "' is not an IPv4 address or a host name that resolves (optionally with :port)"; status = err; closesocket(d->s); d->s = INVALID_SOCKET; return false; }
  a.sin_port = htons((u_short)hostPort);
  d->hostAddr = a; role = CLIENT; connected = false; myPeer = -1; d->awaitHost = false;
  d->peers.clear(); Peer h; h.id = 0; h.addr = a; h.heard = d->now; d->peers.push_back(h);
  d->helloJson = json{{"name", cleanName(name)}, {"version", kNetVersion}, {"content", contentHash()}, {"map", mapHash(app)}, {"mapName", app.doc.name}}.dump();
  d->lastHello = -10;
  status = "Connecting to " + addrStr(a) + "...";
  return true;
}

bool Net::joinCode(App& app, const std::string& code, const std::string& name, std::string& err) {
  if (!d) d = new Impl();
  if (role != NONE) leave("rejoin");
  gContentHash.clear(); contentHash();
  if (!openSocket(*d, 0, err)) { status = "Could not join: " + err; return false; }
  d->now = glfwGetTime();
  d->inet.tick(d->now, json::object(), d->localPort);   // brings the inet clock up to date
  d->inet.lastName = cleanName(name);
  if (!d->inet.joinStart(code)) { err = status = d->inet.joinStatus; closesocket(d->s); d->s = INVALID_SOCKET; return false; }
  role = CLIENT; connected = false; myPeer = -1; d->awaitHost = true;
  d->hostAddr = sockaddr_in{};
  d->peers.clear(); Peer h; h.id = 0; h.heard = d->now; d->peers.push_back(h);
  d->helloJson = json{{"name", cleanName(name)}, {"version", kNetVersion}, {"content", contentHash()}, {"map", mapHash(app)}, {"mapName", app.doc.name}}.dump();
  d->lastHello = -10;
  status = d->inet.joinStatus;
  return true;
}

bool Net::setInternet(App& app, bool on) {
  (void)app;
  if (role != HOST || !d) return false;
  if (!on) { d->inet.hostStop(); return true; }
  d->inet.tick(d->now, json::object(), d->localPort);
  return d->inet.hostStart();
}
std::string Net::roomCode() const { return d && d->inet.hosting ? d->inet.code : std::string(); }
bool Net::joiningByCode() const { return d && d->awaitHost; }
bool Net::relayed() const { return d && role == CLIENT && Inet::isVirtual(d->hostAddr); }

void Net::discover() {
  if (!d) d = new Impl();
  std::string err;
  if (d->s == INVALID_SOCKET && !openSocket(*d, 0, err)) { status = "Search failed: " + err; return; }
  W w; d->header(w, 'Q'); w.u16(kNetVersion); w.b.resize(kQueryPad, '\0');   // padded: the answer is never larger than the question by much
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(kNetPort);
  a.sin_addr.s_addr = htonl(INADDR_BROADCAST); sendto(d->s, w.b.data(), (int)w.b.size(), 0, (sockaddr*)&a, sizeof a);
  inet_pton(AF_INET, "127.0.0.1", &a.sin_addr); sendto(d->s, w.b.data(), (int)w.b.size(), 0, (sockaddr*)&a, sizeof a);   // same-PC host
  found.erase(std::remove_if(found.begin(), found.end(), [&](const LobbyInfo& l) { return d->now - l.seen > 5; }), found.end());
  d->lastDiscover = d->now;
}

bool Net::allReady() const { for (auto& p : players) if (!p.ready) return false; return true; }
void Net::setReady(bool r) { if (role == CLIENT && connected && !d->peers.empty()) d->peers[0].ch.send(json{{"t", "ready"}, {"v", r}}.dump()); }

void Net::leave(const char* reason) {
  if (!d) return;
  if (d->s != INVALID_SOCKET) {
    for (int k = 0; k < 3; ++k) for (auto& p : d->peers) { if (p.token == 0) continue; W w; d->header(w, 'X'); w.u32((uint32_t)(role == HOST ? 0 : myPeer)); w.u64(p.token); w.str(reason); d->wire(p.addr, w.b); }
    closesocket(d->s); d->s = INVALID_SOCKET;
  }
  d->inet.hostStop(); d->inet.joinStop(); d->awaitHost = false;
  d->peers.clear(); d->delayq.clear(); d->worldLog.clear(); d->poses.clear();
  role = NONE; connected = false; matchRunning = false; players.clear();
  logf("net: left (%s)", reason);
}

void Net::hostWorldEvent(const json& ev) {
  if (role != HOST) return;
  std::string type = ev.value("t", "");
  if (type == "ex" || type == "imp" || type == "cloud") d->worldLog.push_back(ev);
  std::string m = ev.dump();
  for (auto& p : d->peers) if (p.inMatch) p.ch.send(m);
}
void Net::clientFire(vec3 from, vec3 dir, int weapon, bool tracer) { if (role == CLIENT && connected) d->peers[0].ch.send(json{{"t", "fire"}, {"p", tojson(from)}, {"d", tojson(dir)}, {"w", weapon}, {"tr", tracer}}.dump()); }
void Net::clientReload() { if (role == CLIENT && connected) d->peers[0].ch.send(json{{"t", "reload"}}.dump()); }
void Net::clientThrow(int kind, vec3 vel) { if (role == CLIENT && connected) d->peers[0].ch.send(json{{"t", "throw"}, {"k", kind}, {"v", tojson(vel)}}.dump()); }
void Net::sendStats(const json& s) { if (role == CLIENT && connected) d->peers[0].ch.send(json{{"t", "stats"}, {"s", s}}.dump()); }
std::map<int, json> Net::peerStats() const { return d ? d->stats : std::map<int, json>{}; }
void Net::clientInteract(uint64_t inst, int idx) { if (role == CLIENT && connected) d->peers[0].ch.send(json{{"t", "door"}, {"i", inst}, {"k", idx}}.dump()); }

// ---------------------------------------------------------------- snapshots (host -> client, unreliable, 20 Hz)
static std::string writeSnapshot(Game& g, int forPeer) {
  W w;
  Match& m = g.match;
  w.f32((float)g.t); w.u8((uint8_t)m.phase); w.u8((uint8_t)(m.winner == Winner::None ? 255 : (int)m.winner)); w.u8((uint8_t)m.active);
  w.f32(m.attackerWaveIn); w.f32(m.defenderWaveIn); w.f32((float)m.time);
  w.u8((uint8_t)m.obj.size());
  for (auto& o : m.obj) { w.u8((uint8_t)o.state); w.f32(o.progress); w.u8((uint8_t)o.A); w.u8((uint8_t)o.D); }
  w.u8((uint8_t)m.slots.size());
  for (auto& r : m.slots) {
    const Soldier& s = g.sold[r.id];
    uint8_t fl = (s.alive ? 1 : 0) | (s.posture == Physics::CROUCH ? 2 : 0) | (s.posture == Physics::PRONE ? 4 : 0) | (r.human ? 8 : 0) | (r.pending ? 16 : 0) | (s.reload > 0 ? 32 : 0) | (s.throwT >= 0 ? 64 : 0) | (s.isProtected(g.t) ? 128 : 0);   // 128: W1 spawn protection
    w.u8(fl); w.v3(s.feet); w.i16((int16_t)std::lround(wrapDeg(s.localPlayer || s.human ? s.yaw : s.yaw) * 100 - 18000));
    w.i16((int16_t)std::lround(std::clamp(s.human ? s.pitch : s.br.aimPitch, -89.0f, 89.0f) * 100));
    w.i16((int16_t)std::lround(wrapDeg(s.br.aimYaw) * 100 - 18000));
    w.u8((uint8_t)std::clamp(s.hp, 0.0f, 255.0f)); w.u8((uint8_t)std::max(0, s.weapon)); w.u16((uint16_t)s.lifeId);
    w.u8((uint8_t)std::clamp(s.recoil * 255.0f, 0.0f, 255.0f)); w.u8((uint8_t)std::clamp(s.throwT < 0 ? 0.0f : s.throwT * 255.0f, 0.0f, 255.0f));
    w.u8((uint8_t)std::clamp(s.reloadTotal > 0 ? s.reload / s.reloadTotal * 255.0f : 0.0f, 0.0f, 255.0f));
    w.u8((uint8_t)(r.livesLeft < 0 ? 255 : r.livesLeft)); w.u8((uint8_t)std::min(250.0f, r.deadFor * 10)); w.u8((uint8_t)r.kills); w.u8((uint8_t)r.deaths);
    w.u8((uint8_t)(s.killer < 0 ? 255 : s.killer)); w.u8((uint8_t)s.deathStyle);
    bool own = r.human && r.peer == forPeer;
    w.u8(own ? 1 : 0);
    if (own) { w.u8((uint8_t)s.frags); w.u8((uint8_t)s.smokes); w.u16((uint16_t)s.ws.loaded); }
  }
  // grenades in flight fill what is left of one packet (36 bytes each): a 32-soldier snapshot is ~1150 bytes, and a
  // snapshot that does not fit a packet would be dropped whole. The nearest-in-list grenades are kept.
  constexpr size_t kSnapBudget = kMaxPacket - 30, kGrenBytes = 36;
  size_t room = w.b.size() + 1 < kSnapBudget ? (kSnapBudget - w.b.size() - 1) / kGrenBytes : 0;
  size_t nGren = std::min({g.gren.live.size(), (size_t)32, room});
  w.u8((uint8_t)nGren);
  for (size_t k = 0; k < nGren; ++k) { auto& x = g.gren.live[k]; w.u8((uint8_t)x.kind); w.u8((uint8_t)std::max(0, x.def)); w.v3(x.p); w.f32(x.rot.x); w.f32(x.rot.y); w.f32(x.rot.z); w.f32(x.rot.w); w.u8((uint8_t)(x.owner < 0 ? 255 : x.owner)); w.u8(x.resting ? 1 : 0); w.f32(x.fuse); }
  return w.b;
}

static void readSnapshot(Game& g, Net::Impl& d, const std::string& data, App& app) {
  R r(data.data(), data.size());
  float st = r.f32();
  Match& m = g.match;
  if (st + 1e-4f < d.serverT) return;   // out of order: keep the newer one
  d.serverT = st; d.serverTAt = d.now;
  m.phase = (MatchPhase)r.u8(); uint8_t wn = r.u8(); m.winner = wn == 255 ? Winner::None : (Winner)wn; m.active = r.u8();
  m.attackerWaveIn = r.f32(); m.defenderWaveIn = r.f32(); m.time = r.f32();
  int no = r.u8();
  for (int k = 0; k < no && k < (int)m.obj.size(); ++k) { m.obj[k].state = (ObjState)r.u8(); m.obj[k].progress = r.f32(); m.obj[k].A = r.u8(); m.obj[k].D = r.u8(); }
  int ns = r.u8();
  for (int k = 0; k < ns && k < (int)g.sold.size() && r.ok; ++k) {
    Soldier& s = g.sold[k]; RosterSlot& rs = m.slots[k];
    uint8_t fl = r.u8(); vec3 feet = r.v3(); float yaw = r.i16() / 100.0f + 180.0f, pitch = r.i16() / 100.0f, aimYaw = r.i16() / 100.0f + 180.0f;
    float hp = r.u8(); int weapon = r.u8(); int life = r.u16();
    float recoil = r.u8() / 255.0f, thr = r.u8() / 255.0f, rel = r.u8() / 255.0f;
    int lives = r.u8(); float deadFor = r.u8() / 10.0f; int kills = r.u8(), deaths = r.u8(); int killer = r.u8(); int dstyle = r.u8();
    bool own = r.u8() != 0;
    int frags = 0, smokes = 0, loaded = 0; if (own) { frags = r.u8(); smokes = r.u8(); loaded = r.u16(); }
    rs.livesLeft = lives == 255 ? -1 : lives; rs.deadFor = deadFor; rs.kills = kills; rs.deaths = deaths; rs.human = fl & 8; rs.pending = fl & 16; rs.alive = fl & 1;
    bool alive = fl & 1;
    rulesNetProtect(g, s, fl & 128);   // W1: spawn protection (shimmer, shield icon, ring)
    s.hp = hp; s.weapon = weapon < (int)app.weaponDefs.size() ? weapon : 0; s.killer = killer == 255 ? -1 : killer; s.deathStyle = dstyle;
    s.recoil = recoil; s.throwT = (fl & 64) ? thr : -1.0f; s.reload = (fl & 32) ? rel : 0; s.reloadTotal = (fl & 32) ? 1 : 0;
    s.posture = (fl & 4) ? Physics::PRONE : (fl & 2) ? Physics::CROUCH : Physics::STAND;
    if (k == g.pendingLocal && own && g.localSlot != k) { g.localSlot = k; s.localPlayer = true; s.alive = false; }   // the slot is ours now
    if (s.localPlayer) {
      if (own) { s.frags = frags; s.smokes = smokes; if (s.alive) g.localGrenadeCounts(s, true); }   // the host's count is the inventory
      if (alive && (!s.alive || life != s.lifeId)) {   // (re)spawned by the host
        s.lifeId = life; s.alive = true; s.feet = feet; s.yaw = yaw;
        Physics::setCharacterPos(feet); app.eyeYaw = yaw; app.eyePitch = 0; app.R.resetTemporal();
        g.applyLoadout(s, false);
      } else if (!alive && s.alive) {
        s.alive = false; s.diedAt = g.t; vec3 p = s.feet; Physics::setCharacterPos(vec3(p.x, p.y - 40.0f, p.z)); g.spectate = s.killer;
      }
      if (own && alive && app.weaponDefs.size() > (size_t)app.activeWeapon) {   // ammo is the host's
        auto& ws = app.wstate[app.weaponDefs[app.activeWeapon]->id];
        if (ws.reload <= 0 && ws.sinceShot > 0.5f && s.weapon == app.activeWeapon) ws.loaded = loaded;
      }
      continue;
    }
    if (alive && !s.alive) { s.alive = true; d.poses.erase(k); }
    if (!alive && s.alive) { s.alive = false; s.diedAt = g.t; }
    s.lifeId = life;
    s.br.aimPitch = pitch; s.br.aimYaw = aimYaw; s.pitch = pitch;
    auto& np = d.poses[k];
    np.feet[0] = np.feet[1]; np.yaw[0] = np.yaw[1]; np.pitch[0] = np.pitch[1]; np.t[0] = np.t[1];
    np.feet[1] = feet; np.yaw[1] = yaw; np.pitch[1] = pitch; np.t[1] = st;
    if (np.t[0] < 0) { np.feet[0] = feet; np.yaw[0] = yaw; np.t[0] = st - 0.05; }
    if (!alive) { s.feet = feet; s.yaw = yaw; }
  }
  int ngr = r.u8();
  g.gren.live.clear();
  for (int k = 0; k < ngr && r.ok; ++k) { GrenadeRT x; x.kind = r.u8(); x.def = r.u8(); x.p = r.v3(); x.rot.x = r.f32(); x.rot.y = r.f32(); x.rot.z = r.f32(); x.rot.w = r.f32(); int o = r.u8(); x.owner = o == 255 ? -1 : o; x.resting = r.u8() != 0; x.fuse = r.f32(); g.gren.live.push_back(x); }
}

// client: interpolate remote soldiers 100 ms behind the newest snapshot
void netInterpolate(Game& g, double clientT) {
  if (!gNet || !gNet->d) return;
  auto& d = *gNet->d;
  double rt = d.serverT + (d.now - d.serverTAt) - 0.1;
  g.t = d.serverT + (d.now - d.serverTAt);
  (void)clientT;
  for (auto& [k, np] : d.poses) {
    if (k >= (int)g.sold.size()) continue;
    Soldier& s = g.sold[k];
    if (s.localPlayer || !s.alive) continue;
    double span = np.t[1] - np.t[0];
    float u = span > 1e-4 ? (float)std::clamp((rt - np.t[0]) / span, 0.0, 1.5) : 1.0f;
    vec3 prev = s.feet;
    s.feet = glm::mix(np.feet[0], np.feet[1], u);
    float dy = wrapDeg(np.yaw[1] - np.yaw[0] + 180.0f) - 180.0f;
    s.yaw = wrapDeg(np.yaw[0] + dy * std::min(u, 1.0f));
    s.vel = (s.feet - prev) * 60.0f; s.speed = glm::length(vec2(s.vel.x, s.vel.z));
  }
}

// ---------------------------------------------------------------- client applies world events
void netApplyEvent(App& app, const json& evIn, bool replay) {
  // a non-const copy: a missing field reads as null and a wrong type throws json::exception, which every caller
  // catches (const operator[] on a missing key would be undefined behaviour) - the host is not trusted blindly
  json ev = evIn;
  if (!ev.is_object()) return;
  std::string t = ev.value("t", "");
  Game& g = G(app);
  if (t == "ex") {
    app.blastAT = ev.value("at", false); app.blastSplashGun = ev.value("sg", 40.0f);
    app.explode(jvec3(ev["p"]), jvec3(ev["d"]), ev["dmg"].get<float>(), ev["r"].get<float>(), ev["cs"].get<float>(), ~0ull, ev["wk"].get<int>());
    app.blastAT = false; app.blastSplashGun = 40;
    if (!replay && ev.value("fx", true)) { app.fx.explosion(jvec3(ev["p"]), vec3(0, 1, 0), "ground", true); app.audio.play("explosion", jvec3(ev["p"]), 0.9f); }
  } else if (t == "imp") {
    Projectile q; q.p = jvec3(ev["pp"]); q.v = jvec3(ev["pv"]); q.mass = ev.value("m", 0.01f); q.ammo = ev.value("a", ""); q.kind = PROJ_BULLET;
    Physics::Hit h; h.pos = jvec3(ev["hp"]); h.normal = jvec3(ev["hn"]); h.ud = ev["ud"].get<uint64_t>(); h.t = ev.value("ht", 0.0f);
    std::string surf = ev.value("s", "masonry");
    app.bulletImpact(q, h, surf);
    app.spawnedProjectiles.clear();
    if (!replay) app.fx.impact(h.pos, h.normal, surf == "concrete" || surf == "masonry" ? "stone" : surf == "wood_door" || surf == "wood_shutter" ? "wood" : surf);
  } else if (t == "impfx") {
    if (!replay) app.fx.impact(jvec3(ev["hp"]), jvec3(ev["hn"]), "ground");
  } else if (t == "door") {
    uint64_t inst = ev["i"]; int k = ev["k"]; auto it = app.rt.find(inst);
    if (it != app.rt.end() && k >= 0 && k < (int)it->second.dir.size()) {
      it->second.dir[k] = ev["dir"]; float p = ev.value("p", it->second.progress[k]);
      if (std::fabs(p - it->second.progress[k]) > 0.2f || replay) { it->second.progress[k] = p; for (auto& i : app.doc.instances) if (i.id == inst) app.updateLeafBodies(i, it->second, k); }
      g.doorSeen[(inst << 8) | (uint64_t)k] = ev["dir"].get<int>();
    }
  } else if (t == "cloud") {
    SmokeCloud c; c.id = g.gren.nextId++; c.p = jvec3(ev["p"]); c.start = ev["st"].get<double>(); c.seed = ev["seed"].get<uint32_t>();
    c.radius = ev.value("r", 5.0f); c.build = ev.value("b", 2.0f); c.dense = ev.value("dn", 15.0f); c.fade = ev.value("f", 5.0f);
    g.gren.addCloud(c);
  } else if (t == "shot") {
    int w = ev.value("w", 0); vec3 p = jvec3(ev["p"]), dir = jvec3(ev["d"]);
    if (w >= 0 && w < (int)app.weaponDefs.size()) {
      const WeaponDef& d = *app.weaponDefs[w];
      g.ghosts.push_back({p, glm::normalize(dir) * d.velocity, 0, ev.value("tr", false)});
      app.fx.flash(p + dir * 0.05f, d.action == "auto" ? 0.12f : 0.16f, 0.04f, vec4(8, 5.5f, 2.5f, 1));
      app.audio.play("shot_" + d.id, p, 1.0f);
    }
  } else if (t == "gore") {   // W7: one replicated FleshHit; droplets are simulated here from its seed
    if (!replay) { extern void goreNetEvent(App&, const json&); goreNetEvent(app, ev); }
  } else if (t == "log") {
    app.toast(ev.value("text", ""), 4);
  }
}

// ---------------------------------------------------------------- host handles client requests (validated)
static void hostRequest(App& app, Net& n, Peer& p, const json& mIn) {
  json m = mIn;   // non-const copy: missing fields read as null, wrong types throw (caught by the caller)
  if (!m.is_object()) { ++n.rejectedRequests; return; }
  std::string t = m.value("t", "");
  crumb(t == "fire" ? "net: host fire request" : t == "door" ? "net: host door request" : t == "throw" ? "net: host throw request" : "net: host request");
  Game& g = G(app);
  if (t == "ready") { for (auto& pl : n.players) if (pl.peer == p.id) pl.ready = m.value("v", false); n.d->lobbyDirty = true; return; }
  if (t == "stats") { n.d->stats[p.id] = m["s"]; return; }
  if (!g.active || g.match.phase != MatchPhase::Running) { if (t != "ready") ++n.rejectedRequests; return; }
  int slot = g.match.slotOfPeer(p.id);
  if (slot < 0 || !g.match.slots[slot].human || g.match.slots[slot].peer != p.id) { ++n.rejectedRequests; return; }   // never another slot
  Soldier& s = g.sold[slot];
  if (!s.alive) { ++n.rejectedRequests; return; }
  if (t == "fire") {
    int w = m.value("w", -1);
    vec3 from = jvec3(m["p"]), dir = jvec3(m["d"]);
    bool ok = w >= 0 && w < (int)app.weaponDefs.size() && glm::length(dir) > 0.5f && glm::length(from - (s.feet + vec3(0, 1.3f, 0))) < 2.2f;
    if (ok) {
      if (w != s.weapon) { s.wsBy[s.weapon] = s.ws; s.weapon = w; auto it = s.wsBy.find(w); if (it != s.wsBy.end()) s.ws = it->second; else { const WeaponDef& d = *app.weaponDefs[w]; s.ws = WeaponState{}; s.ws.loaded = d.loaded; for (int r = d.reserve; r > 0; r -= d.containerSize) s.ws.containers.push_back(std::min(r, d.containerSize)); } }
      const WeaponDef& d = *app.weaponDefs[w];
      float minGap = 60.0f / std::max(1.0f, d.rpm) * 0.75f;   // tolerate network jitter, never faster than the weapon
      ok = s.ws.loaded > 0 && s.reload <= 0 && n.d->now - s.lastShotReal >= minGap;   // requests arrive in real time
      if (ok) {
        s.ws.loaded--; s.lastShot = g.t; s.lastShotReal = n.d->now;
        app.fireProjectile(from, dir, d.velocity, d.action == "launcher" ? PROJ_ROCKET : PROJ_BULLET, d.action == "launcher" ? d.rocketDmg : 0, ~0ull, m.value("tr", false), d.container);
        Projectile& pr = app.projectiles.back(); pr.owner = slot; pr.mass = d.bulletMass * 0.001f;
        if (d.action == "launcher") { pr.blastDmg = d.rocketDmg; pr.blastR = d.rocketR; pr.craterScale = d.rocketCrater; pr.splashGun = d.rocketSplashGun; pr.atKill = true; pr.gravScale = 0.35f; }
        g.hearSound(from, (int)s.role, 70, slot, 0);
        n.hostWorldEvent(json{{"t", "shot"}, {"p", tojson(from)}, {"d", tojson(dir)}, {"w", w}, {"tr", m.value("tr", false)}, {"by", slot}});
        ++n.acceptedShots;
      }
    }
    if (!ok) ++n.rejectedRequests;
  } else if (t == "reload") {
    const WeaponDef& d = *app.weaponDefs[s.weapon];
    if (s.reload <= 0 && !s.ws.containers.empty() && s.ws.loaded < d.cap) { s.reloadTotal = s.ws.loaded == 0 ? d.reloadEmpty : d.reloadPartial; s.reload = s.reloadTotal; }
    else ++n.rejectedRequests;
  } else if (t == "throw") {
    vec3 v = jvec3(m["v"]);
    if (glm::length(v) > 25.0f || !g.startThrow(s, m.value("k", 0), v, vec3(0))) ++n.rejectedRequests;   // inventory checked again at release
  } else if (t == "door") {
    uint64_t inst = m["i"]; int k = m["k"];
    auto it = app.rt.find(inst); const Instance* in = nullptr; for (auto& i : app.doc.instances) if (i.id == inst) in = &i;
    if (it != app.rt.end() && in && k >= 0 && k < (int)it->second.asset->interactables.size()) {
      auto& def = it->second.asset->interactables[k];
      vec3 c = vec3(app.instanceMatrix(*in) * vec4((def.apMin + def.apMax) * 0.5f, 1));
      if (glm::length(c - (s.feet + vec3(0, 1.2f, 0))) < 3.5f) app.toggleInteract(inst, k); else ++n.rejectedRequests;
    }
  }
}

// ---------------------------------------------------------------- per-frame update
void Net::update(App& app, float dt) {
  if (!d || d->s == INVALID_SOCKET) return;
  (void)dt; d->now = glfwGetTime();   // real time: two processes simulate at their own frame rates
  d->flushDelayed(*this);
  char buf[2048];
  sockaddr_in from{}; int fl = sizeof from;
  for (int iter = 0; iter < 512; ++iter) {
    fl = sizeof from;
    int got = recvfrom(d->s, buf, sizeof buf, 0, (sockaddr*)&from, &fl);
    if (got < 0) {
      int e = WSAGetLastError();
      if (e == WSAEWOULDBLOCK) break;
      continue;   // WSAECONNRESET (stale ICMP) / WSAEMSGSIZE (oversized datagram) etc.: skip it, keep reading
    }
    if (got == 0) continue;
    bytesIn += got; ++packetsIn;
    if (got >= 5) {   // rendezvous traffic ("NMKR"): control messages and punches to Inet, relayed game packets unwrapped
      uint32_t mg; std::memcpy(&mg, buf, 4);
      if (mg == kRendezvousMagic) {
        if (buf[4] != 'L') { d->inet.onPacket(d->now, from, (const uint8_t*)buf, (size_t)got); continue; }
        if (got < 9 || !d->inet.fromServer(from)) continue;   // only the room server relays
        uint32_t rid; std::memcpy(&rid, buf + 5, 4);
        if (rid == 0) continue;
        std::memmove(buf, buf + 9, (size_t)got - 9); got -= 9;
        from = Inet::virt(rid);   // the relayed peer's virtual address (inet.h)
        if (got <= 0) continue;
      }
    }
    R r(buf, got);
    if (r.u32() != kMagic) continue;
    char type = (char)r.u8();
    if (role == HOST) {
      if (type == 'Q') {   // discovery: must be padded (no amplification), rate-limited per source
        if ((size_t)got < kQueryPad || !d->allowUnauth(from)) continue;
        json info = {{"host", players.empty() ? "Host" : players[0].name}, {"map", app.doc.name}, {"mapHash", d->mapHashCached(app)}, {"content", contentHash()}, {"humans", (int)players.size()},
                     {"max", gUi.rules.defenderSlots}, {"phase", matchRunning ? 1 : 0}, {"version", kNetVersion}};
        W w; d->header(w, 'A'); w.str(info.dump()); d->sendRaw(from, w.b, *this);
        continue;
      }
      crumb("net: hello");
      if (type == 'H') {
        bool known = false; for (auto& p : d->peers) if (sameAddr(p.addr, from)) known = true;
        if (!known && !d->allowUnauth(from)) continue;
        json h; try { h = json::parse(r.str()); } catch (...) { continue; }
        if (!h.is_object()) continue;
        Peer* ex = nullptr; for (auto& p : d->peers) if (sameAddr(p.addr, from)) ex = &p;
        auto reject = [&](const std::string& why) { W w; d->header(w, 'R'); w.str(why); d->sendRaw(from, w.b, *this); logf("net: rejected %s: %s", addrStr(from).c_str(), why.c_str()); };
        int hv = 0; try { hv = h.value("version", 0); } catch (...) {}
        std::string hc, hm; try { hc = h.value("content", ""); hm = h.value("map", ""); } catch (...) {}
        if (hv != kNetVersion) { reject("Different game build (network version " + std::to_string(hv) + ", host " + std::to_string(kNetVersion) + "). Install the same build on both PCs."); continue; }
        if (hc != contentHash()) { reject("Game content differs from the host's (weapons / assets / AI data). Copy the same build and content folder to both PCs."); continue; }
        if (hm != d->mapHashCached(app)) { reject("Map differs: the host plays '" + app.doc.name + "'. Open the same map file on this PC (File > Open), then join again."); continue; }
        if (!ex) {
          int maxHumans = std::max(1, gUi.rules.defenderSlots);
          if ((int)players.size() >= maxHumans || (int)players.size() >= ScenarioRules::kMaxCombatants) { reject("Lobby full (" + std::to_string(players.size()) + " of " + std::to_string(maxHumans) + " defender slots taken by players)."); continue; }
          std::string nm; try { nm = h.value("name", "Player"); } catch (...) {}
          Peer p; p.id = d->nextPeer++; p.addr = from; p.name = cleanName(nm); p.heard = d->now; p.token = randomToken();
          if (matchRunning) {
            int slot = G(app).match.humanJoin(p.id, p.name);
            if (slot < 0) { reject("Match full: no defender slot can be taken."); continue; }
          }
          d->peers.push_back(p); ex = &d->peers.back();
          players.push_back({p.id, p.name, false});
          d->lobbyDirty = true;
          logf("net: %s joined from %s as peer %d", p.name.c_str(), addrStr(from).c_str(), p.id);
          if (matchRunning) {   // late join: the match, then the world as it is now
            Game& g = G(app);
            json slots = json::array(); for (auto& s : g.match.slots) slots.push_back({{"id", s.id}, {"f", (int)s.faction}, {"r", (int)s.role}, {"h", s.human || s.pendingPeer >= 0}, {"p", s.pendingPeer >= 0 ? s.pendingPeer : s.peer}, {"n", s.name}, {"lane", s.lane}});
            ex->ch.send(json{{"t", "start"}, {"scenario", gUi.matchScenario}, {"slots", slots}, {"st", g.t}, {"late", true}}.dump());
            json doors = json::array();
            for (auto& [id, rt] : app.rt) for (size_t k = 0; k < rt.dir.size(); ++k) if (rt.progress[k] > 0.01f || rt.dir[k] != 0) doors.push_back({{"i", id}, {"k", k}, {"dir", rt.dir[k]}, {"p", rt.progress[k]}});
            ex->ch.send(json{{"t", "world"}, {"log", d->worldLog}, {"doors", doors}}.dump());
            ex->inMatch = true;
          }
        }
        W w; d->header(w, 'W'); w.u32((uint32_t)ex->id); w.u64(ex->token); w.str(json{{"host", players[0].name}}.dump()); d->sendRaw(from, w.b, *this);
        continue;
      }
      if (type == 'D' || type == 'X') {
        uint32_t pid = r.u32(); uint64_t tok = r.u64();
        if (!r.ok) continue;
        Peer* p = nullptr; for (auto& x : d->peers) if ((uint32_t)x.id == pid && x.token == tok && sameAddr(x.addr, from)) p = &x;
        if (!p) continue;   // unknown peer, wrong address or wrong session token (spoofed): ignored
        p->heard = d->now;
        if (type == 'X') { p->heard = -100; continue; }
        p->ch.acked(r.u32());
        int nc = r.u8();
        std::vector<std::string> msgs;
        bool bad = false;
        for (int k = 0; k < nc && r.ok && !bad; ++k) { uint32_t id = r.u32(); uint16_t len = r.u16(); if (!r.need(len)) break; std::string c((const char*)r.p + r.o, len); r.o += len; bad = !p->ch.receive(id, c, msgs); }
        if (bad) { logf("net: peer %d (%s) broke the protocol (reliable window / message size); dropped", p->id, p->name.c_str()); p->heard = -100; continue; }
        int ut = r.u8(); uint16_t ul = r.u16();
        crumb("net: host input");
        if (ut == 2 && r.need(ul)) {   // client movement (latest wins, validated)
          R in(r.p + r.o, ul);
          vec3 feet = in.v3(); float yaw = in.f32(), pitch = in.f32(); int posture = in.u8(), weapon = in.u8();
          Game& g = G(app);
          int slot = g.match.slotOfPeer(p->id);
          if (in.ok && g.active && slot >= 0 && g.match.slots[slot].human && g.sold[slot].alive) {
            Soldier& s = g.sold[slot];
            float dtIn = std::max(0.05f, (float)(d->now - s.netHeard)); s.netHeard = d->now;
            float step = glm::length(vec2(feet.x - s.feet.x, feet.z - s.feet.z));
            bool ok = step < 7.0f * dtIn + 0.6f && std::fabs(feet.y - s.feet.y) < 2.5f + 9.0f * dtIn;
            Physics::Hit hh;
            if (ok && step > 0.05f) ok = !Physics::castRayEx(s.feet + vec3(0, 0.6f, 0), feet - s.feet, glm::length(feet - s.feet), hh);   // no walking through walls
            if (ok) { s.feet = feet; if (s.agent >= 0) Physics::setAgentPos(s.agent, feet); }
            else { ++rejectedRequests; p->ch.send(json{{"t", "correct"}, {"p", tojson(s.feet)}}.dump()); }
            s.yaw = yaw; s.pitch = std::clamp(pitch, -89.0f, 89.0f); s.posture = std::clamp(posture, 0, 2);
            if (weapon != s.weapon && weapon < (int)app.weaponDefs.size()) { s.wsBy[s.weapon] = s.ws; s.weapon = weapon; auto it = s.wsBy.find(weapon); if (it != s.wsBy.end()) s.ws = it->second; else { const WeaponDef& wd = *app.weaponDefs[weapon]; s.ws = WeaponState{}; s.ws.loaded = wd.loaded; for (int q = wd.reserve; q > 0; q -= wd.containerSize) s.ws.containers.push_back(std::min(q, wd.containerSize)); } }
            s.vel = vec3(0); s.speed = step / dtIn;
          }
        }
        for (auto& m : msgs) {
          try { json j = json::parse(m); hostRequest(app, *this, *p, j); }
          catch (const std::exception& e) { ++rejectedRequests; logf("net: malformed request from peer %d ignored (%s)", p->id, e.what()); }
        }
      }
    } else if (role == CLIENT) {
      if (!sameAddr(from, d->hostAddr)) { if (type == 'A') { json a; try { a = json::parse(r.str()); } catch (...) { continue; } LobbyInfo li; li.addr = addrStr(from).substr(0, addrStr(from).find(':')); li.hostName = a.value("host", ""); li.map = a.value("map", ""); li.mapHash = a.value("mapHash", ""); li.contentHash = a.value("content", ""); li.humans = a.value("humans", 0); li.maxHumans = a.value("max", 0); li.phase = a.value("phase", 0); li.version = a.value("version", 0); li.seen = d->now; bool dup = false; for (auto& f : found) if (f.addr == li.addr) { f = li; dup = true; } if (!dup) found.push_back(li); } continue; }
      Peer& h = d->peers[0];
      if (type == 'R') { if (connected) continue; h.heard = d->now; status = "Join refused: " + r.str(); leave("refused"); return; }
      if (type == 'W') {
        if (connected) { h.heard = d->now; continue; }   // a resend of the welcome
        int id = (int)r.u32(); uint64_t tok = r.u64();
        if (!r.ok || tok == 0) continue;
        myPeer = id; h.token = tok; h.heard = d->now; connected = true; status = "Connected to the host lobby."; continue;
      }
      if (type == 'X' || type == 'D') {
        uint32_t sender = r.u32(); uint64_t tok = r.u64();
        if (!r.ok || !connected || sender != 0 || tok != h.token) continue;   // not from this session's host
        h.heard = d->now;
        if (type == 'X') { std::string why = r.str(); d->endReason = "The host ended the session (" + why + ")."; d->wantEnd = true; continue; }
        h.ch.acked(r.u32());
        int nc = r.u8();
        std::vector<std::string> msgs;
        bool bad = false;
        for (int k = 0; k < nc && r.ok && !bad; ++k) { uint32_t id = r.u32(); uint16_t len = r.u16(); if (!r.need(len)) break; std::string c((const char*)r.p + r.o, len); r.o += len; bad = !h.ch.receive(id, c, msgs); }
        if (bad) { d->endReason = "The host sent malformed data; disconnected."; d->wantEnd = true; continue; }
        int ut = r.u8(); uint16_t ul = r.u16();
        if (ut == 1 && r.need(ul) && gGame && gGame->active && gGame->isClient) readSnapshot(*gGame, *d, std::string((const char*)r.p + r.o, ul), app);
        for (auto& m : msgs) {
          try {
            json j = json::parse(m);
            if (!j.is_object()) continue;
            std::string t = j.value("t", "");
            if (t == "lobby") {
              std::vector<LobbyPlayer> pl;
              for (auto& p : j["players"]) pl.push_back({p["id"].get<int>(), cleanName(p["name"].get<std::string>()), p["ready"].get<bool>()});
              players = pl; lobbyRules = j["rules"]; hostMapName = j.value("map", ""); matchRunning = j.value("running", false);
            }
            else if (t == "start") d->pendingStart = j;
            else if (t == "world") d->pendingWorld.push_back(j);
            else if (t == "lobbyReturn") d->wantLobbyReturn = true;
            else if (t == "correct") { if (gGame && gGame->active) { vec3 p = jvec3(j["p"]); Physics::setCharacterPos(p); } }
            else if (gGame && gGame->active && gGame->isClient) netApplyEvent(app, j, false);
            else if (d->pendingEvents.size() < 4096) d->pendingEvents.push_back(j);
          } catch (const std::exception& e) { logf("net: malformed message from the host ignored (%s)", e.what()); }
        }
      }
    }
  }
  // ---- host: timeouts, lobby, snapshots, flush
  if (role == HOST) {
    if (d->inet.hosting) d->inet.tick(d->now, json{{"name", players.empty() ? std::string("Host") : players[0].name}, {"map", app.doc.name}, {"humans", (int)players.size()}, {"max", std::max(1, gUi.rules.defenderSlots)}}, d->localPort);
    for (size_t k = 0; k < d->peers.size();) {
      if (d->now - d->peers[k].heard > kTimeout) {
        int id = d->peers[k].id;
        logf("net: peer %d (%s) %s", id, d->peers[k].name.c_str(), d->peers[k].heard < 0 ? "left" : "timed out");
        if (gGame && gGame->active) gGame->match.humanLeave(id);
        players.erase(std::remove_if(players.begin(), players.end(), [&](const LobbyPlayer& p) { return p.peer == id; }), players.end());
        d->peers.erase(d->peers.begin() + k); d->lobbyDirty = true; continue;
      }
      ++k;
    }
    if (d->lobbyDirty) {
      d->lobbyDirty = false;
      json pl = json::array(); for (auto& p : players) pl.push_back({{"id", p.peer}, {"name", p.name}, {"ready", p.ready}});
      std::string m = json{{"t", "lobby"}, {"players", pl}, {"rules", gUi.rules.toJson()}, {"map", app.doc.name}, {"running", matchRunning}}.dump();
      for (auto& p : d->peers) p.ch.send(m);
    }
    if (gGame && gGame->active && !gGame->isClient && d->now - d->lastSnap >= 0.05) {
      d->lastSnap = d->now;
      crumb("net: snapshot");
      for (auto& p : d->peers) if (p.inMatch) { p.unrel = writeSnapshot(*gGame, p.id); p.unrelType = 1; }
    }
    crumb("net: flush"); for (auto& p : d->peers) d->flushPeer(p, *this, 0);
  } else if (role == CLIENT) {
    Peer& h = d->peers[0];
    if (d->awaitHost) {   // joining by room code: Inet looks the room up, punches, falls back to the relay
      d->inet.tick(d->now, json::object(), d->localPort);
      status = d->inet.joinStatus;
      if (d->inet.targetReady) {
        d->inet.targetReady = false; d->awaitHost = false;
        d->hostAddr = d->inet.target; h.addr = d->inet.target; h.heard = d->now; d->lastHello = -10;
        logf("net: room %s reached %s", d->inet.lastCode.c_str(), d->inet.usingRelay ? "through the relay" : ("directly at " + addrStr(d->inet.target)).c_str());
      } else if (d->inet.phase == Inet::FAILED) {
        std::string why = d->inet.joinStatus; leave("room join failed"); status = why; return;
      } else return;   // nothing to send yet
    }
    if (!connected && d->now - d->lastHello > 0.5) { W w; d->header(w, 'H'); w.str(d->helloJson); d->sendRaw(d->hostAddr, w.b, *this); d->lastHello = d->now; }
    if (d->now - h.heard > (connected ? kTimeout : kConnectTimeout)) { d->endReason = connected ? "Connection to the host was lost (no reply for 6 s)." : "No answer from " + addrStr(d->hostAddr) + ". Check the address, that the host is in its lobby, and the host's firewall."; d->wantEnd = true; }
    if (connected && gGame && gGame->active && gGame->isClient && d->now - d->lastInput >= 1.0 / 30) {
      d->lastInput = d->now;
      if (Soldier* L = gGame->local(); L && L->alive) {
        W w; auto c = Physics::character();
        w.v3(c.pos); w.f32(app.eyeYaw); w.f32(app.eyePitch); w.u8((uint8_t)c.posture); w.u8((uint8_t)app.activeWeapon);
        h.unrel = w.b; h.unrelType = 2;
      }
    }
    if (connected) d->flushPeer(h, *this, myPeer);
    // start / world / end transitions (outside the receive loop)
    if (!d->pendingStart.is_null()) {
      json st = d->pendingStart; d->pendingStart = json();
      bool valid = st.is_object() && st.contains("scenario") && st["scenario"].is_object() && st.contains("slots") && st["slots"].is_array()
                   && (!st.contains("st") || st["st"].is_number());
      if (!valid) { d->endReason = "The host sent an invalid match start; disconnected."; d->wantEnd = true; }
      else {
        gUi.matchScenario = st["scenario"];
        Game& g = G(app); g.isClient = true; g.clientStart = st; g.pendingStart = true;
        if (app.mode == Mode::Explore) app.exitExplore();
        gUi.showJoin = false;
        if (!app.enterExplore()) status = "Could not enter the match: the start marker is obstructed on this PC.";
        d->poses.clear(); d->serverT = st.value("st", 0.0); d->serverTAt = d->now;
      }
    }
    if (gGame && gGame->active && gGame->isClient) {
      for (auto& w : d->pendingWorld) {
        vec3 l = app.eyePos(); app.audio.setListener(l + vec3(0, 5000, 0), vec3(0, 0, -1));   // replay quietly
        size_t nEv = 0, nDoor = 0, bad = 0;
        if (w.contains("log") && w["log"].is_array()) for (auto& ev : w["log"]) { try { netApplyEvent(app, ev, true); ++nEv; } catch (...) { ++bad; } }
        if (w.contains("doors") && w["doors"].is_array()) for (auto& dj : w["doors"]) { try { json e = dj; e["t"] = "door"; netApplyEvent(app, e, true); ++nDoor; } catch (...) { ++bad; } }
        app.fx.clear();
        logf("net: late-join replay of %zu world events, %zu doors (%zu malformed skipped)", nEv, nDoor, bad);
      }
      d->pendingWorld.clear();
      for (auto& e : d->pendingEvents) { try { netApplyEvent(app, e, false); } catch (...) {} }
      d->pendingEvents.clear();
    }
    if (d->wantLobbyReturn) { d->wantLobbyReturn = false; if (app.mode == Mode::Explore) app.exitExplore(); gUi.showJoin = true; status = "Back in the host's lobby."; setReady(false); }
    if (d->wantEnd) {
      d->wantEnd = false; std::string why = d->endReason;
      if (app.mode == Mode::Explore) app.exitExplore();
      leave("ended"); status = why; gUi.showJoin = true;
      app.toast(why, 8);
    }
  }
}

void Net::startMatch(App& app) {
  if (role != HOST) return;
  Game& g = G(app);
  g.humanPeers.clear();
  for (auto& p : players) g.humanPeers.push_back({p.peer, p.name});
  gUi.startMatch(app);
  if (!g.active) { status = "Match could not start: " + g.setupError; return; }
  matchRunning = true;
  d->worldLog.clear();
  json slots = json::array(); for (auto& s : g.match.slots) slots.push_back({{"id", s.id}, {"f", (int)s.faction}, {"r", (int)s.role}, {"h", s.human}, {"p", s.peer}, {"n", s.name}, {"lane", s.lane}});
  std::string m = json{{"t", "start"}, {"scenario", gUi.matchScenario}, {"slots", slots}, {"st", g.t}}.dump();
  for (auto& p : d->peers) { p.ch.send(m); p.inMatch = true; }
  d->lobbyDirty = true;
}

void Net::returnToLobby(App& app) {
  if (role != HOST) return;
  matchRunning = false;
  for (auto& p : d->peers) { p.ch.send(json{{"t", "lobbyReturn"}}.dump()); p.inMatch = false; }
  for (auto& p : players) p.ready = p.peer == 0;
  d->lobbyDirty = true;
  (void)app;
}

// ---------------------------------------------------------------- lobby UI
// room server address / secret / relay preference (exeDir()/net_user.json)
static void internetSettings(Inet& in) {
  if (!ImGui::TreeNode("Internet settings")) return;
  static char srv[128] = "", sec[65] = ""; static bool init = false;
  if (!init) { std::snprintf(srv, sizeof srv, "%s", in.server.c_str()); std::snprintf(sec, sizeof sec, "%s", in.secret.c_str()); init = true; }
  ImGui::InputText("Room server", srv, sizeof srv); ImGui::SameLine(); ImGui::TextDisabled("host or host:port");
  ImGui::InputText("Server secret", sec, sizeof sec, ImGuiInputTextFlags_Password);
  ImGui::Checkbox("Always use the relay (skip direct connection)", &in.forceRelay);
  if (ImGui::Button("Save settings")) { in.server = srv; in.secret = sec; in.save(); }
  ImGui::TextDisabled("Set up your free room server with tools/rendezvous/README.md (Oracle Cloud Always Free).");
  ImGui::TreePop();
}

void MpUi::netLobby(App& app, bool blocked) {
  if (!gNet) gNet = new Net();
  Net& n = *gNet;
  if (!n.d) n.d = new Net::Impl();
  if (!n.d->inetLoaded) { n.d->inet.load(); n.d->inetLoaded = true; }
  ImGui::Separator();
  if (n.role != Net::HOST) {
    ImGui::TextWrapped("Host a lobby on this PC. Players on the same network join with Play > Join LAN (search or the address shown); players anywhere join with a room code once you switch on Internet room.");
    if (ImGui::Button("Open lobby", ImVec2(220, 0))) { std::string err; n.host(app, err); }
    internetSettings(n.d->inet);
  } else {
    ImGui::TextWrapped("%s", n.status.c_str());
    Inet& in = n.d->inet;
    bool on = in.hosting;
    if (ImGui::Checkbox("Internet room (friends anywhere join with a code)", &on)) n.setInternet(app, on);
    if (in.hosting && !in.code.empty()) {
      ImGui::Text("Room code:"); ImGui::SameLine(); ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1), "%s", in.code.c_str());
      ImGui::SameLine(); if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(in.code.c_str());
    }
    if (!in.hostStatus.empty()) ImGui::TextWrapped("%s", in.hostStatus.c_str());
    if (in.relayedPeers > 0) ImGui::Text("%d player(s) connected through the relay", in.relayedPeers);
    internetSettings(in);
    ImGui::Text("Players (%zu, all defend as %s):", n.players.size(), factionName(rules.defendingFaction));
    for (auto& p : n.players) ImGui::BulletText("%s%s  %s", p.name.c_str(), p.peer == 0 ? " (host)" : "", p.ready ? "READY" : "not ready");
    ImGui::BeginDisabled(blocked || !n.allReady());
    if (ImGui::Button("Start LAN match", ImVec2(220, 0))) n.startMatch(app);
    ImGui::EndDisabled();
    if (!n.allReady()) { ImGui::SameLine(); ImGui::TextDisabled("waiting for every player to be ready"); }
    if (ImGui::Button("Close lobby")) { n.leave("host closed the lobby"); n.status = "Lobby closed."; }
  }
  if (!n.status.empty() && n.role != Net::HOST) ImGui::TextWrapped("%s", n.status.c_str());
}

void MpUi::netJoin(App& app) {
  if (!gNet) gNet = new Net();
  Net& n = *gNet;
  if (!n.d) n.d = new Net::Impl();
  if (!n.d->inetLoaded) { n.d->inet.load(); n.d->inetLoaded = true; }
  Inet& in = n.d->inet;
  static char name[32] = "Player", ip[64] = "192.168.1.", code[17] = ""; static bool init = false;
  if (!init) { std::snprintf(name, sizeof name, "%s", in.lastName.c_str()); std::snprintf(code, sizeof code, "%s", in.lastCode.c_str()); init = true; }
  ImGui::InputText("Your name", name, sizeof name);
  if (n.role == Net::CLIENT && n.joiningByCode()) {
    ImGui::TextWrapped("%s", n.status.c_str());
    if (ImGui::Button("Cancel")) { n.leave("cancelled"); n.status = "Cancelled."; }
    return;
  }
  if (n.role == Net::CLIENT && n.connected) {
    ImGui::Text("Lobby of %s on map '%s'%s", n.players.empty() ? "host" : n.players[0].name.c_str(), n.hostMapName.c_str(), n.relayed() ? "  (through the relay)" : "");
    for (auto& p : n.players) ImGui::BulletText("%s%s  %s", p.name.c_str(), p.peer == n.myPeer ? " (you)" : "", p.ready ? "READY" : "not ready");
    if (!n.lobbyRules.is_null()) { ScenarioRules r = ScenarioRules::fromJson(n.lobbyRules); ImGui::Text("%s defend (%d slots) vs %d attacking AI, %d lives per attacker slot", factionName(r.defendingFaction), r.defenderSlots, r.attackerAI, 1 + r.attackerRespawns); }
    bool me = false; for (auto& p : n.players) if (p.peer == n.myPeer) me = p.ready;
    if (ImGui::Button(me ? "Not ready" : "Ready", ImVec2(160, 0))) n.setReady(!me);
    ImGui::SameLine(); if (ImGui::Button("Leave lobby")) { n.leave("player left"); n.status = "Left the lobby."; }
    if (n.matchRunning) ImGui::TextWrapped("A match is running: you will join it at the next defender respawn.");
  } else {
    if (ImGui::Button("Search LAN")) n.discover();
    if (n.d && n.d->now - n.d->lastDiscover < 3.0) { ImGui::SameLine(); ImGui::TextDisabled("searching..."); }
    std::string mine = Net::mapHash(app), content = Net::contentHash();
    for (auto& f : n.found) {
      bool mapOk = f.mapHash == mine, contOk = f.contentHash == content && f.version == kNetVersion;
      ImGui::PushID(f.addr.c_str());
      ImGui::Text("%s @ %s  map '%s'  %d/%d players %s", f.hostName.c_str(), f.addr.c_str(), f.map.c_str(), f.humans, f.maxHumans, f.phase ? "(in match)" : "");
      if (!mapOk) ImGui::TextColored(ImVec4(1, 0.6f, 0.45f, 1), "  your open map differs: open '%s' first", f.map.c_str());
      if (!contOk) ImGui::TextColored(ImVec4(1, 0.6f, 0.45f, 1), "  different game build / content");
      ImGui::SameLine(); if (ImGui::SmallButton("Join")) { std::string err; n.join(app, f.addr, name, err); }
      ImGui::PopID();
    }
    ImGui::InputText("Host address", ip, sizeof ip); ImGui::SameLine();
    if (ImGui::Button("Join address")) { std::string err; n.join(app, ip, name, err); }
    ImGui::Separator();
    ImGui::Text("Internet");
    ImGui::InputText("Room code", code, sizeof code, ImGuiInputTextFlags_CharsUppercase); ImGui::SameLine();
    if (ImGui::Button("Join room")) { std::string err; in.lastName = name; n.joinCode(app, code, name, err); }
    internetSettings(in);
    if (!n.status.empty()) ImGui::TextWrapped("%s", n.status.c_str());
    ImGui::TextDisabled("Every PC needs the same build, content and map file. Internet rooms need a room server (Internet settings).");
  }
}
