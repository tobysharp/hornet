#pragma once

#include <cstdint>
#include <string>
#include <sys/types.h>
#include <unistd.h>

#include "net/constants.h"
#include "protocol/constants.h"

namespace hornet::net {

class Bitcoind {
 public:
  ~Bitcoind();

  static Bitcoind Connect(Network network = Network::Mainnet);
  static Bitcoind Launch(Network network = Network::Regtest);
  static Bitcoind ConnectOrLaunch(Network network);

  std::string GetCookiePath() const;

  protocol::Magic GetMagic() const {
    return magic;
  }
  uint16_t GetPort() const {
    return port;
  }

  std::string Cli(const std::string& command);

  void MineBlocks(int n);

  void Terminate();

 private:
  pid_t pid = -1;
  protocol::Magic magic;
  uint16_t port;
  std::string datadir;
  Network network;
};

}  // namespace hornet::net
