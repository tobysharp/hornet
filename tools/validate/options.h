#pragma once

#include <string>

#include "hornetnodelib/net/peer_address.h"

struct Options {
   uint16_t notify_tcp_port;  // TCP port number for sending notifications.
   std::string data_dir;
   std::string blocks_dir;
   int length;
};
