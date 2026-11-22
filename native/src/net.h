// Playing together over the network (scripts: net.*). One player hosts (a TCP server), the others join by address,
// or find games on the same network (LAN): the host says who it is on UDP every second.
// Messages are text (scripts send JSON); each is sent whole, in order. Nothing blocks: update() is called every frame.
#pragma once
#include <string>
#include <vector>
#include <memory>

namespace pge {

struct NetEvent { std::string type; int id = 0; std::string data; };   // type: join, leave, msg, error, connected, closed
struct NetGame { std::string name, address; int port = 0, players = 0, max = 0; double seen = 0; std::string info; };

class Net {
public:
  Net(); ~Net();
  bool host(int port, const std::string& name, int max, const std::string& info);   // start a game others can join
  bool join(const std::string& address, int port);                                   // join a game (connected / error comes as an event)
  void stop();                                                                       // leave or close the game
  bool send(const std::string& data, int to);   // to: -1 everyone (the host sends to all players; a player to the host), else one player (host only)
  void kick(int id);
  void update(double now);                      // every frame: accept, read, write, LAN beacons
  std::vector<NetEvent> take();                 // what happened since the last call
  void lanSearch(bool on);                      // listen for games on this network
  std::vector<NetGame> lan(double now) const;
  std::string role() const;                     // "", "host", "client" (connecting counts as client)
  int id() const;                               // my id: 0 the host, a player's id from the host
  std::vector<int> peers() const;
  bool connected() const;
  std::string error;
  void setInfo(const std::string& info, int players);   // what the LAN beacon tells (world name, players)
private:
  struct D; std::unique_ptr<D> d;
};

}
