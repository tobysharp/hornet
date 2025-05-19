#pragma once

#include <deque>
#include <map>
#include <memory>

#include "net/peer.h"
#include "node/outbound_message.h"

namespace hornet::node {

class Broadcaster {
 public:
  virtual ~Broadcaster() {}
  virtual void SendToOne(const std::shared_ptr<net::Peer>& peer, OutboundMessage&& msg) = 0;
  virtual void SendToAll(OutboundMessage&& msg) = 0;
};

}  // namespace hornet::node
