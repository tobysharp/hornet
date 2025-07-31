// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <deque>
#include <map>
#include <memory>

#include "hornetlib/protocol/message.h"
#include "hornetnodelib/net/peer.h"

namespace hornet::node::dispatch {

class Broadcaster {
 public:
  virtual ~Broadcaster() {}
  virtual void SendToOne(net::SharedPeer peer, std::unique_ptr<protocol::Message> message) = 0;
  virtual void SendToAll(std::unique_ptr<protocol::Message> message) = 0;

  template <typename T>
  void SendMessage(net::SharedPeer peer, std::unique_ptr<T> message) {
    static_assert(std::is_base_of_v<protocol::Message, T>);
    std::unique_ptr<protocol::Message> base{static_cast<protocol::Message*>(message.release())};
    SendToOne(peer, std::move(base));
  }

  template <typename T, typename... Args>
  void SendMessage(net::SharedPeer peer, Args&&... args) {
    SendMessage(peer, std::make_unique<T>(std::forward<Args>(args)...));
  }
};

}  // namespace hornet::node::dispatch
