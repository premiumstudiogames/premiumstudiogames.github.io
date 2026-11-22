// Playing together over the network: TCP for the game (length-prefixed text messages), UDP broadcast for finding games on the LAN.
#include "net.h"
#include <cstring>
#include <map>
#include <deque>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t; static const sock_t BAD = INVALID_SOCKET;
static int lastErr() { return WSAGetLastError(); }
static bool wouldBlock(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY; }
static void closeS(sock_t s) { closesocket(s); }
static void nonblock(sock_t s) { u_long on = 1; ioctlsocket(s, FIONBIO, &on); }
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int sock_t; static const sock_t BAD = -1;
static int lastErr() { return errno; }
static bool wouldBlock(int e) { return e == EWOULDBLOCK || e == EAGAIN || e == EINPROGRESS; }
static void closeS(sock_t s) { close(s); }
static void nonblock(sock_t s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
#endif

namespace pge {

static const int BEACON_PORT = 46601;
static const size_t MAX_MSG = 16u << 20;   // 16 MB: a whole world can be sent to a player who joins

struct Conn { sock_t s = BAD; std::string in, out; int id = 0; bool open = true, connecting = false; double t0 = 0; };

struct Net::D {
  std::string role; int myId = 0, nextId = 1, port = 0, max = 8, players = 1; std::string name, info;
  sock_t listen = BAD, beacon = BAD, finder = BAD; double beaconT = 0;
  std::map<int, Conn> conns;   // host: players by id; client: the host is conns[0]
  std::vector<NetEvent> ev; std::map<std::string, NetGame> found;
  bool wsa = false;
};

Net::Net() : d(new D) {
#ifdef _WIN32
  WSADATA w; d->wsa = WSAStartup(MAKEWORD(2, 2), &w) == 0;
#endif
}
Net::~Net() { stop(); lanSearch(false);
#ifdef _WIN32
  if (d->wsa) WSACleanup();
#endif
}

static void putMsg(Conn& c, const std::string& m) { uint32_t n = (uint32_t)m.size(); unsigned char h[4] = {(unsigned char)(n >> 24), (unsigned char)(n >> 16), (unsigned char)(n >> 8), (unsigned char)n}; c.out.append((const char*)h, 4); c.out += m; }

bool Net::host(int port, const std::string& name, int max, const std::string& info) {
  stop(); error.clear();
  sock_t s = socket(AF_INET, SOCK_STREAM, 0); if (s == BAD) { error = "socket"; return false; }
  int yes = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port); a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(s, (sockaddr*)&a, sizeof a) != 0 || ::listen(s, 16) != 0) { closeS(s); error = "the port " + std::to_string(port) + " is busy"; return false; }
  nonblock(s); d->listen = s; d->role = "host"; d->myId = 0; d->nextId = 1; d->port = port; d->max = max; d->name = name; d->info = info; d->players = 1;
  sock_t b = socket(AF_INET, SOCK_DGRAM, 0); if (b != BAD) { setsockopt(b, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof yes); nonblock(b); d->beacon = b; }
  d->beaconT = 0; return true;
}

bool Net::join(const std::string& address, int port) {
  stop(); error.clear();
  addrinfo hints{}, *res = nullptr; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(address.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) { error = "address not found"; d->ev.push_back({"error", 0, error}); return false; }
  sock_t s = socket(AF_INET, SOCK_STREAM, 0); if (s == BAD) { freeaddrinfo(res); error = "socket"; return false; }
  nonblock(s); int yes = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&yes, sizeof yes);
  int r = connect(s, res->ai_addr, (int)res->ai_addrlen); freeaddrinfo(res);
  if (r != 0 && !wouldBlock(lastErr())) { closeS(s); error = "could not connect"; d->ev.push_back({"error", 0, error}); return false; }
  Conn c; c.s = s; c.id = 0; c.connecting = r != 0; c.t0 = -1; d->conns[0] = c; d->role = "client"; d->myId = -1;
  if (r == 0) { d->conns[0].connecting = false; }
  return true;
}

void Net::stop() {
  for (auto& [id, c] : d->conns) if (c.s != BAD) closeS(c.s);
  d->conns.clear(); if (d->listen != BAD) closeS(d->listen); d->listen = BAD; if (d->beacon != BAD) closeS(d->beacon); d->beacon = BAD;
  if (!d->role.empty()) d->ev.push_back({"closed", 0, ""}); d->role.clear(); d->myId = 0;
}

bool Net::send(const std::string& data, int to) {
  if (d->role.empty() || data.size() > MAX_MSG) return false;
  if (d->role == "client") { auto it = d->conns.find(0); if (it == d->conns.end()) return false; putMsg(it->second, data); return true; }
  if (to < 0) { for (auto& [id, c] : d->conns) if (c.open) putMsg(c, data); return true; }
  auto it = d->conns.find(to); if (it == d->conns.end()) return false; putMsg(it->second, data); return true;
}

void Net::kick(int id) { auto it = d->conns.find(id); if (it == d->conns.end() || d->role != "host") return; closeS(it->second.s); d->conns.erase(it); d->ev.push_back({"leave", id, ""}); }

void Net::update(double now) {
  // the host: new players
  if (d->listen != BAD) for (int k = 0; k < 8; k++) { sockaddr_in a{}; socklen_t n = sizeof a; sock_t s = accept(d->listen, (sockaddr*)&a, &n); if (s == BAD) break;
    nonblock(s); int yes = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&yes, sizeof yes);
    if ((int)d->conns.size() + 1 >= d->max) { Conn c; c.s = s; putMsg(c, "{\"t\":\"full\"}"); ::send(s, c.out.data(), (int)c.out.size(), 0); closeS(s); continue; }
    Conn c; c.s = s; c.id = d->nextId++; putMsg(c, "{\"t\":\"id\",\"id\":" + std::to_string(c.id) + "}"); d->conns[c.id] = c;
    char ip[64] = "?"; inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip); d->ev.push_back({"join", c.id, ip}); }
  std::vector<int> dead;
  for (auto& [id, c] : d->conns) {
    if (c.connecting) {   // a player connecting to the host: done when the socket can be written
      fd_set w; FD_ZERO(&w); FD_SET(c.s, &w); timeval tv{0, 0}; int r = select((int)c.s + 1, nullptr, &w, nullptr, &tv);
      if (r > 0) { int e = 0; socklen_t n = sizeof e; getsockopt(c.s, SOL_SOCKET, SO_ERROR, (char*)&e, &n); if (e) { error = "could not connect"; d->ev.push_back({"error", 0, error}); dead.push_back(id); continue; } c.connecting = false; }
      else { if (c.t0 < 0) c.t0 = now; if (now - c.t0 > 8) { error = "no answer"; d->ev.push_back({"error", 0, error}); dead.push_back(id); } continue; } }
    // write what is waiting
    while (!c.out.empty()) { int n = (int)::send(c.s, c.out.data(), (int)std::min<size_t>(c.out.size(), 1 << 16), 0); if (n > 0) c.out.erase(0, n); else { if (n < 0 && !wouldBlock(lastErr())) c.open = false; break; } }
    // read
    char buf[1 << 16]; for (int k = 0; k < 64; k++) { int n = (int)recv(c.s, buf, sizeof buf, 0); if (n > 0) c.in.append(buf, n); else { if (n == 0 || !wouldBlock(lastErr())) c.open = false; break; } }
    while (c.in.size() >= 4) { const unsigned char* h = (const unsigned char*)c.in.data(); uint32_t len = (uint32_t(h[0]) << 24) | (uint32_t(h[1]) << 16) | (uint32_t(h[2]) << 8) | h[3];
      if (len > MAX_MSG) { c.open = false; break; } if (c.in.size() < 4 + len) break; std::string m = c.in.substr(4, len); c.in.erase(0, 4 + len);
      if (d->role == "client" && d->myId < 0 && m.rfind("{\"t\":\"id\"", 0) == 0) { size_t p = m.find("\"id\":"); d->myId = p == std::string::npos ? 0 : std::atoi(m.c_str() + p + 5); d->ev.push_back({"connected", d->myId, ""}); continue; }
      if (d->role == "client" && m == "{\"t\":\"full\"}") { error = "the game is full"; d->ev.push_back({"error", 0, error}); c.open = false; break; }
      d->ev.push_back({"msg", id, m}); }
    if (!c.open) dead.push_back(id); }
  for (int id : dead) { auto it = d->conns.find(id); if (it == d->conns.end()) continue; closeS(it->second.s); d->conns.erase(it);
    if (d->role == "host") d->ev.push_back({"leave", id, ""}); else { d->role.clear(); d->ev.push_back({"closed", 0, error}); } }
  // the host tells the LAN it is here (every second)
  if (d->beacon != BAD && now - d->beaconT > 1) { d->beaconT = now;
    std::string b = "PGENET1\n" + std::to_string(d->port) + "\n" + std::to_string(d->conns.size() + 1) + "\n" + std::to_string(d->max) + "\n" + d->name + "\n" + d->info;
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(BEACON_PORT); a.sin_addr.s_addr = htonl(INADDR_BROADCAST); sendto(d->beacon, b.data(), (int)b.size(), 0, (sockaddr*)&a, sizeof a);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); sendto(d->beacon, b.data(), (int)b.size(), 0, (sockaddr*)&a, sizeof a); }
  // games on this network
  if (d->finder != BAD) for (int k = 0; k < 32; k++) { char buf[2048]; sockaddr_in a{}; socklen_t n = sizeof a; int r = (int)recvfrom(d->finder, buf, sizeof buf - 1, 0, (sockaddr*)&a, &n); if (r <= 0) break;
    std::string s(buf, r); if (s.rfind("PGENET1\n", 0) != 0) continue; std::vector<std::string> L; size_t p = 8; for (int i = 0; i < 5; i++) { size_t q = s.find('\n', p); if (q == std::string::npos) { L.push_back(s.substr(p)); p = s.size(); break; } L.push_back(s.substr(p, q - p)); p = q + 1; }
    std::string rest = p < s.size() ? s.substr(p) : ""; if (L.size() < 4) continue;
    char ip[64]; inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip); NetGame g; g.address = ip; g.port = std::atoi(L[0].c_str()); g.players = std::atoi(L[1].c_str()); g.max = std::atoi(L[2].c_str()); g.name = L[3]; g.info = L.size() > 4 ? L[4] + (rest.empty() ? "" : "\n" + rest) : rest; g.seen = now;
    d->found[g.address + ":" + std::to_string(g.port)] = g; }
}

std::vector<NetEvent> Net::take() { std::vector<NetEvent> r; r.swap(d->ev); return r; }

void Net::lanSearch(bool on) {
  if (!on) { if (d->finder != BAD) closeS(d->finder); d->finder = BAD; d->found.clear(); return; }
  if (d->finder != BAD) return;
  sock_t s = socket(AF_INET, SOCK_DGRAM, 0); if (s == BAD) return; int yes = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
#ifdef SO_REUSEPORT
  setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (const char*)&yes, sizeof yes);
#endif
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(BEACON_PORT); a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(s, (sockaddr*)&a, sizeof a) != 0) { closeS(s); return; } nonblock(s); d->finder = s;
}
std::vector<NetGame> Net::lan(double now) const { std::vector<NetGame> r; for (auto& [k, g] : d->found) if (now - g.seen < 4) r.push_back(g); return r; }
std::string Net::role() const { return d->role; }
int Net::id() const { return d->myId; }
std::vector<int> Net::peers() const { std::vector<int> r; for (auto& [id, c] : d->conns) r.push_back(id); return r; }
bool Net::connected() const { return d->role == "host" || (d->role == "client" && d->myId >= 0); }
void Net::setInfo(const std::string& info, int players) { d->info = info; d->players = players; }

}
