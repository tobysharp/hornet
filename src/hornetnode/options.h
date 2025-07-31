#pragma once

#include <string>

#include "hornetnodelib/net/peer_address.h"

struct Options {
   hornet::node::net::PeerAddress connect;  // Peer address to connect to, e.g. 127.0.0.1:8333.
};
