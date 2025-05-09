#pragma once

#include <cstdint>
#include <string>
#include <sys/types.h>
#include <unistd.h>

#include "net/constants.h"
#include "protocol/constants.h"

namespace hornet::net {

struct Bitcoind {
  pid_t pid = -1;
  protocol::Magic magic;
  uint16_t port;
  std::string datadir;

  static Bitcoind Launch(Network network);
  ~Bitcoind();
  void Terminate();
};

}  // namespace hornet::net
