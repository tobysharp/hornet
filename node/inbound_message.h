#pragma once

#include <chrono>
#include <memory>

#include "net/peer.h"
#include "protocol/message.h"

namespace hornet::node {

using PeerPtr = std::weak_ptr<net::Peer>;

class InboundMessage {
 public:
  InboundMessage(PeerPtr peer, std::unique_ptr<protocol::Message>&& msg)
      : peer_(peer), message_(std::move(msg)), received_at_(std::chrono::steady_clock::now()) {}

 private:
  PeerPtr peer_;
  std::unique_ptr<protocol::Message> message_;
  std::chrono::steady_clock::time_point received_at_;
};

}  // namespace hornet::node
