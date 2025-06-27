// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <deque>
#include <map>
#include <memory>

#include "hornetlib/net/peer.h"
#include "hornetlib/node/outbound_message.h"

namespace hornet::node {

class Broadcaster {
 public:
  virtual ~Broadcaster() {}
  virtual void SendToOne(const std::shared_ptr<net::Peer>& peer, OutboundMessage&& msg) = 0;
  virtual void SendToAll(OutboundMessage&& msg) = 0;

  template <typename T>
  void SendMessage(const std::shared_ptr<net::Peer> &peer, std::unique_ptr<T>&& message) {
    if (peer) {
      static_assert(std::is_base_of_v<protocol::Message, T>);
      std::unique_ptr<const protocol::Message> base{static_cast<const protocol::Message*>(message.release())};
      OutboundMessage outbound{std::move(base)};
      SendToOne(peer, std::move(outbound));
    }
  }

  template <typename T, typename... Args>
  void SendMessage(const std::shared_ptr<net::Peer>& peer, Args&&... args) {
    SendMessage(peer, std::make_unique<T>(std::forward<Args>(args)...));
  }
};

}  // namespace hornet::node
