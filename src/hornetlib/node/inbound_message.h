// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <chrono>
#include <memory>
#include <ostream>

#include "hornetlib/net/peer.h"
#include "hornetlib/protocol/message.h"

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

  friend std::ostream& operator<<(std::ostream& os, const InboundMessage& m) {
    return os << "{ peer.fd = " << m.peer_.lock()->GetConnection().GetSocket().GetFD()
              << ", message = " << *m.message_ << " }";
  }

 private:
  PeerPtr peer_;
  std::unique_ptr<protocol::Message> message_;
  std::chrono::steady_clock::time_point received_at_;
};

}  // namespace hornet::node
