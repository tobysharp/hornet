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

  std::shared_ptr<net::Peer> GetPeer() const {
    return peer_.lock();
  }

  const protocol::Message& GetMessage() const {
    return *message_;
  }

 private:
  PeerPtr peer_;
  std::unique_ptr<protocol::Message> message_;
  std::chrono::steady_clock::time_point received_at_;
};

}  // namespace hornet::node
