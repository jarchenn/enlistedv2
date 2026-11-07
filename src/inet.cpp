// Internet play client side of the rendezvous protocol (see inet.h, tools/rendezvous/PROTOCOL.md).
#include "inet.h"
#include <cstring>
#include <random>
#include <fstream>
#include <algorithm>

namespace {
std::string epStr(const sockaddr_in& a) { char b[64]; inet_ntop(AF_INET, &a.sin_addr, b, sizeof b); return std::string(b) + ":" + std::to_string(ntohs(a.sin_port)); }
bool parseEp(const std::string& s, sockaddr_in& a) {   // "a.b.c.d:port"
  auto c = s.rfind(':'); if (c == std::string::npos || s.size() > 21) return false;
  int port = 0; try { port = std::stoi(s.substr(c + 1)); } catch (...) { return false; }
  if (port <= 0 || port > 65535) return false;
  a = sockaddr_in{}; a.sin_family = AF_INET; a.sin_port = htons((u_short)port);
  return inet_pton(AF_INET, s.substr(0, c).c_str(), &a.sin_addr) == 1;
}
bool same(const sockaddr_in& a, const sockaddr_in& b) { return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port; }
uint32_t randU32() { std::random_device rd; uint32_t v = 0; while (v == 0) v = rd(); return v; }
std::string normCode(const std::string& s) {
  std::string o; for (char c : s) { if (c == ' ' || c == '-') continue; o.push_back((char)std::toupper((unsigned char)c)); }
  return o.size() > 16 ? std::string() : o;
}
fs::path userFile() { return exeDir() / "net_user.json"; }
}

void Inet::load() {
  try { json j = json::parse(readTextFile(userFile())); server = j.value("server", server); secret = j.value("secret", secret); lastCode = j.value("lastCode", lastCode); lastName = j.value("name", lastName); forceRelay = j.value("forceRelay", false); }
  catch (...) {}
  if (const char* e = std::getenv("NMK_INET_SERVER"); e && *e) server = e;   // tests / one-off overrides
  if (const char* e = std::getenv("NMK_INET_FORCE_RELAY"); e && *e == '1') forceRelay = true;
}
static bool testOverrides() { const char* e = std::getenv("NMK_INET_SERVER"); return e && *e; }

// ---------------------------------------------------------------- test helper: hostile traffic (--selftest-inet z)
// Sends malformed, spoofed, oversized and flooding datagrams to a host on 127.0.0.1:port, then checks that the host
// still answers a well-formed discovery query. Returns false (and says why) when it does not.
bool inetFuzzHost(int port, std::string& report) {
  WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) { report = "no socket"; return false; }
  sockaddr_in me{}; me.sin_family = AF_INET; me.sin_addr.s_addr = htonl(INADDR_LOOPBACK); bind(s, (sockaddr*)&me, sizeof me);
  sockaddr_in h{}; h.sin_family = AF_INET; h.sin_port = htons((u_short)port); inet_pton(AF_INET, "127.0.0.1", &h.sin_addr);
  auto put = [&](const std::string& b) { sendto(s, b.data(), (int)b.size(), 0, (sockaddr*)&h, sizeof h); };
  auto hdr = [](char t) { std::string b; uint32_t m = 0x314B4D4Eu; b.append((const char*)&m, 4); b.push_back(t); return b; };
  auto u32 = [](std::string& b, uint32_t v) { b.append((const char*)&v, 4); };
  auto u64 = [](std::string& b, uint64_t v) { b.append((const char*)&v, 8); };
  int sent = 0;
  std::mt19937 rng(77);
  for (uint32_t pid = 1; pid <= 4; ++pid) {   // spoofed leaves and data with guessed tokens for the first peer ids
    for (int k = 0; k < 50; ++k) {
      std::string x = hdr('X'); u32(x, pid); u64(x, ((uint64_t)rng() << 32) | rng()); u32(x, 4); x += "spof"; put(x); ++sent;
      std::string dd = hdr('D'); u32(dd, pid); u64(dd, ((uint64_t)rng() << 32) | rng()); u32(dd, 0); dd.push_back(1); u32(dd, 100000); dd.append("\x05\x00", 2); dd += "\x00junk"; dd.push_back(0); put(dd); ++sent;
    }
  }
  put(hdr('D').substr(0, 7)); put(hdr('X')); put(hdr('W')); put(hdr('R')); sent += 4;                   // truncated
  { std::string big = hdr('H'); big.resize(3000, 'x'); put(big); ++sent; }                               // oversized datagram
  for (const char* js : {"[1,2,3]", "{\"version\":\"two\",\"name\":42}", "{\"name\":\"\\u0001\\u0002AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"}", "{not json"}) {   // bad hellos
    std::string b = hdr('H'); std::string j = js; u32(b, (uint32_t)j.size()); b += j; put(b); ++sent;
  }
  { std::string b = hdr('H'); u32(b, 0x7fffffff); put(b); ++sent; }                                      // lying length prefix
  for (int k = 0; k < 1000; ++k) { std::string q = hdr('Q'); q.append("\x02\x00", 2); put(q); ++sent; }  // unpadded query flood
  for (int k = 0; k < 300; ++k) { std::string q = hdr('Q'); q.append("\x02\x00", 2); q.resize(256, '\0'); put(q); ++sent; }   // padded flood
  { std::string l; uint32_t m = 0x524B4D4Eu; l.append((const char*)&m, 4); l.push_back('L'); u32(l, 7); l += hdr('X'); put(l); ++sent; }   // fake relay
  { std::string c; uint32_t m = 0x524B4D4Eu; c.append((const char*)&m, 4); c.push_back('r'); c += "{\"code\":\"HACKED\"}"; put(c); ++sent; }    // fake REGOK
  for (int k = 0; k < 200; ++k) { std::string r(1 + rng() % 1400, '\0'); for (auto& ch : r) ch = (char)rng(); if (k % 2) r.replace(0, 4, hdr('D').substr(0, 4)); put(r); ++sent; }   // noise
  Sleep(1500);   // past the per-source rate-limit window
  std::string q = hdr('Q'); q.append("\x02\x00", 2); q.resize(256, '\0');
  bool answered = false;
  for (int attempt = 0; attempt < 6 && !answered; ++attempt) {
    put(q);
    fd_set fs; FD_ZERO(&fs); FD_SET(s, &fs); timeval tv{0, 500000};
    while (select(0, &fs, nullptr, nullptr, &tv) > 0) {
      char b[2048]; int g = recv(s, b, sizeof b, 0);
      if (g >= 5 && b[4] == 'A') { answered = true; break; }
      FD_ZERO(&fs); FD_SET(s, &fs);
    }
  }
  closesocket(s);
  report = std::to_string(sent) + " hostile datagrams sent (spoofed X / D for peers 1-4, truncated, oversized, bad hellos, 1300 queries, fake relay / REGOK, random noise); host " + (answered ? "still answers discovery" : "no longer answers");
  return answered;
}
void Inet::save() const {
  if (testOverrides()) return;   // environment overrides (tests): never written over the user's own settings
  json j = {{"server", server}, {"secret", secret}, {"lastCode", lastCode}, {"name", lastName}, {"forceRelay", forceRelay}};
  std::ofstream f(userFile(), std::ios::binary); f << j.dump(2);
}

bool Inet::resolveServer() {
  serverOk = false; serverErr.clear();
  std::string h = server; int port = kRendezvousPort;
  while (!h.empty() && h.back() == ' ') h.pop_back();
  while (!h.empty() && h.front() == ' ') h.erase(h.begin());
  if (auto c = h.rfind(':'); c != std::string::npos) { try { port = std::stoi(h.substr(c + 1)); } catch (...) { port = -1; } h = h.substr(0, c); }
  if (h.empty()) { serverErr = "No room server set: enter its address under Internet settings (see tools/rendezvous/README.md)."; return false; }
  if (port <= 0 || port > 65535) { serverErr = "Room server '" + server + "': bad port."; return false; }
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((u_short)port);
  if (inet_pton(AF_INET, h.c_str(), &a.sin_addr) != 1) {
    addrinfo hints{}, *res = nullptr; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(h.c_str(), nullptr, &hints, &res) != 0 || !res) { serverErr = "Room server '" + h + "' could not be resolved (check the address and your internet connection)."; if (res) freeaddrinfo(res); return false; }
    a.sin_addr = ((sockaddr_in*)res->ai_addr)->sin_addr; freeaddrinfo(res);
  }
  serverAddr = a; serverOk = true;
  return true;
}

std::string Inet::wrap(uint32_t rid, const std::string& datagram) const {
  std::string b; uint32_t m = kRendezvousMagic; b.append((const char*)&m, 4); b.push_back('L'); b.append((const char*)&rid, 4); b += datagram;
  return b;
}
void Inet::control(char type, const json& j, const sockaddr_in& to) {
  std::string b; uint32_t m = kRendezvousMagic; b.append((const char*)&m, 4); b.push_back(type); b += j.dump();
  if (send) send(to, b);
}
void Inet::punch(char type, uint32_t n, const sockaddr_in& to) {
  std::string b; uint32_t m = kRendezvousMagic; b.append((const char*)&m, 4); b.push_back(type); b.append((const char*)&n, 4);
  if (send) send(to, b);
}

std::vector<std::string> Inet::lanEndpoints(int port) const {
  std::vector<std::string> out;
  char hn[256] = ""; if (gethostname(hn, sizeof hn) != 0) return out;
  addrinfo hints{}, *res = nullptr; hints.ai_family = AF_INET;
  if (getaddrinfo(hn, nullptr, &hints, &res) == 0) {
    for (auto* r = res; r && out.size() < 3; r = r->ai_next) { sockaddr_in a = *(sockaddr_in*)r->ai_addr; a.sin_port = htons((u_short)port); std::string s = epStr(a); if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s); }
    freeaddrinfo(res);
  }
  out.push_back("127.0.0.1:" + std::to_string(port));   // a second copy of the game on the same PC
  return out;
}

// ---------------------------------------------------------------- host
bool Inet::hostStart() {
  hosting = false; code = lastCode; pub.clear(); gotRegOk = false; targets.clear(); rids.clear(); relayedPeers = 0;
  if (!resolveServer()) { hostStatus = serverErr; return false; }
  hosting = true; lastReg = -10; regStart = now_;
  hostStatus = "Registering with the room server " + epStr(serverAddr) + "...";
  return true;
}
void Inet::hostStop() { hosting = false; targets.clear(); rids.clear(); relayedPeers = 0; hostStatus.clear(); }

// ---------------------------------------------------------------- client
bool Inet::joinStart(const std::string& c) {
  joinCode = normCode(c); cands.clear(); targetReady = false; usingRelay = false;
  if (joinCode.size() != 6) { phase = FAILED; joinStatus = "Room codes have 6 letters and digits (for example K7Q2MX)."; return false; }
  if (!resolveServer()) { phase = FAILED; joinStatus = serverErr; return false; }
  lastCode = joinCode; save();
  nonce = randU32(); phase = LOOKING; lookStart = now_; lastLook = -10;
  joinStatus = "Looking up room " + joinCode + "...";
  return true;
}

void Inet::tick(double now, const json& reg, int localPort) {
  now_ = now;
  if (hosting && serverOk) {
    if (now - lastReg >= 5.0 || (!gotRegOk && now - lastReg >= 1.0)) {   // retry quickly until the first REGOK
      lastReg = now;
      json j = {{"ver", ver}, {"name", reg.value("name", std::string("Host"))}, {"map", reg.value("map", std::string(""))}, {"humans", reg.value("humans", 1)}, {"max", reg.value("max", 1)}, {"lan", lanEndpoints(localPort)}};
      if (!code.empty()) j["code"] = code;
      if (!secret.empty()) j["secret"] = secret;
      control('R', j, serverAddr);
    }
    if (!gotRegOk && now - regStart > 12.0) hostStatus = "The room server " + epStr(serverAddr) + " is not answering. Check the address under Internet settings and that the server is running (tools/rendezvous/README.md).";
    for (auto& t : targets) if (now < t.until && now - lastPunch >= 0.1) for (auto& c : t.cands) punch('P', t.nonce, c);
    if (now - lastPunch >= 0.1) lastPunch = now;
    targets.erase(std::remove_if(targets.begin(), targets.end(), [&](const Target& t) { return now > t.until + 10; }), targets.end());
  }
  switch (phase) {
    case LOOKING:
      if (now - lastLook >= 0.5) { lastLook = now; control('K', json{{"code", joinCode}, {"ver", ver}, {"nonce", nonce}, {"lan", lanEndpoints(localPort)}}, serverAddr); }
      if (now - lookStart > 6.0) { phase = FAILED; joinStatus = "No answer from the room server " + epStr(serverAddr) + ". Check the server address under Internet settings and your internet connection."; }
      break;
    case PUNCHING:
      if (now - lastPunch >= 0.1) { lastPunch = now; for (auto& c : cands) punch('P', nonce, c); }
      if (now - punchStart > 3.0) { phase = RELAYING; relayStart = now; lastRelay = -10; joinStatus = "Direct connection failed: connecting through the relay..."; }
      break;
    case RELAYING:
      if (now - lastRelay >= 0.5) { lastRelay = now; control('Y', json{{"code", joinCode}, {"nonce", nonce}}, serverAddr); }
      if (now - relayStart > 5.0) { phase = FAILED; joinStatus = "The relay did not answer. The room server may be overloaded or the room has closed; try again."; }
      break;
    default: break;
  }
}

bool Inet::onPacket(double now, const sockaddr_in& from, const uint8_t* p, size_t n) {
  now_ = now;
  if (n < 5) return true;
  char type = (char)p[4];
  // ---- punches come straight from the other game (never from the server)
  if (type == 'P' || type == 'p') {
    if (n != 9) return true;
    uint32_t nn; std::memcpy(&nn, p + 5, 4);
    if (hosting) {
      for (auto& t : targets) if (t.nonce == nn) { if (type == 'P') punch('p', nn, from); return true; }
    }
    if (phase == PUNCHING && nn == nonce) {
      if (type == 'P') punch('p', nn, from);
      target = from; targetReady = true; phase = HANDSHAKE; usingRelay = false;
      joinStatus = "Connected directly to " + epStr(from) + ": joining the lobby...";
    }
    return true;
  }
  if (!fromServer(from)) return true;   // control messages only from our server
  json j; try { j = json::parse(std::string((const char*)p + 5, n - 5)); } catch (...) { return true; }
  if (!j.is_object()) return true;
  try {
    if (type == 'r' && hosting) {   // REGOK
      code = j.value("code", code); pub = j.value("pub", pub); gotRegOk = true; lastCode = code;
      hostStatus = "Internet room open. Friends join with Play > Join LAN... > Room code. Your public address: " + pub + ".";
    } else if (type == 'I' && hosting) {   // INTRO: a client is about to punch toward us
      Target t; t.nonce = j.value("nonce", 0u); t.pub = j.value("pub", std::string()); t.until = now + 3.5;
      sockaddr_in a; if (parseEp(t.pub, a)) t.cands.push_back(a);
      if (j.contains("lan") && j["lan"].is_array()) for (auto& e : j["lan"]) if (e.is_string() && parseEp(e.get<std::string>(), a) && t.cands.size() < 5) t.cands.push_back(a);
      bool dup = false; for (auto& x : targets) if (x.nonce == t.nonce && x.pub == t.pub) { x.until = t.until; dup = true; }
      if (!dup && targets.size() < 8 && !t.cands.empty()) targets.push_back(t);
    } else if (type == 'y' && hosting && j.contains("pub")) {   // RELAYOK sent to the host (carries the client's pub)
      uint32_t rid = j.value("rid", 0u);
      if (rid && std::find(rids.begin(), rids.end(), rid) == rids.end()) { rids.push_back(rid); relayedPeers = (int)rids.size(); }
    } else if (type == 'O' && phase == LOOKING && j.value("nonce", 0u) == nonce) {   // ROOM
      cands.clear(); sockaddr_in a;
      if (parseEp(j.value("pub", std::string()), a)) cands.push_back(a);
      if (j.contains("lan") && j["lan"].is_array()) for (auto& e : j["lan"]) if (e.is_string() && parseEp(e.get<std::string>(), a) && cands.size() < 5) cands.push_back(a);
      std::string nm = j.value("name", std::string("host")), mp = j.value("map", std::string(""));
      if (forceRelay || cands.empty()) { phase = RELAYING; relayStart = now; lastRelay = -10; joinStatus = "Found " + nm + "'s room (" + mp + "): connecting through the relay..."; }
      else { phase = PUNCHING; punchStart = now; lastPunch = -10; joinStatus = "Found " + nm + "'s room (" + mp + "): connecting directly..."; }
    } else if (type == 'N' && phase == LOOKING) {
      phase = FAILED; joinStatus = "Room " + joinCode + " not found. Check the code; the host's lobby must be open with Internet room switched on.";
    } else if (type == 'V' && phase == LOOKING) {
      phase = FAILED; joinStatus = "Room " + joinCode + " runs a different game build (network version " + std::to_string(j.value("ver", 0)) + ", yours " + std::to_string(ver) + "). Everyone needs the same build.";
    } else if (type == 'y' && phase == RELAYING && j.value("nonce", 0u) == nonce) {   // RELAYOK to the client
      uint32_t rid = j.value("rid", 0u);
      if (rid) { target = virt(rid); targetReady = true; phase = HANDSHAKE; usingRelay = true; joinStatus = "Connected through the relay: joining the lobby..."; }
    } else if (type == 'E') {
      std::string e = j.value("err", std::string());
      if (e == "secret") { if (hosting) hostStatus = "The room server refused the registration: wrong or missing secret (Internet settings)."; }
      else if (e == "rooms" || e == "full") { if (hosting) hostStatus = "The room server is full; try again later."; if (phase == RELAYING) { phase = FAILED; joinStatus = "The relay is full; try again later."; } }
      else if (e == "relays" && phase == RELAYING) { phase = FAILED; joinStatus = "This room has too many relayed players (16)."; }
      // "norelay": the game's own timeout reports the lost connection
    }
  } catch (const std::exception& e) { logf("inet: malformed '%c' from the room server ignored (%s)", type, e.what()); }
  return true;
}
