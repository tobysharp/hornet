// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "hornetlib/protocol/message.h"
#include "hornetlib/protocol/message_handler.h"
#include "hornetnodelib/net/peer_registry.h"
#include "hornetnodelib/dispatch/broadcaster.h"

namespace hornet::node::net {
  class PeerManager;
}  // namespace hornet::node::net

namespace hornet::node::dispatch {

class EventHandler : public protocol::MessageHandler {
 public:
  EventHandler() = default;
  EventHandler(Broadcaster& broadcaster, const net::PeerRegistry& registry)
      : broadcaster_(&broadcaster), registry_(&registry) {}

  void SetBroadcaster(Broadcaster& broadcaster) {
    broadcaster_ = &broadcaster;
  }

  void SetPeerRegistry(const net::PeerRegistry& registry) {
    registry_ = &registry;
  }

  virtual void OnPeerConnect(net::SharedPeer) {}
  virtual void OnPeerDisconnect(net::SharedPeer) {}
  virtual void OnHandshakeComplete(net::SharedPeer) {}
  virtual void OnLoop(net::PeerManager&) {}

 protected:
  // Returns the peer that sent the given inbound message.
  net::SharedPeer GetPeer(const protocol::Message& message) const {
    Assert(registry_ != nullptr);
    if (!message.HasEnvelope()) return nullptr;
    return registry_->FromId(message.GetEnvelope()->peer_id);
  }

  template <typename T, typename... Args>
  void Reply(std::shared_ptr<net::Peer> peer, Args&&... args) const {
    Assert(broadcaster_ != nullptr);
    broadcaster_->SendMessage<T>(peer, std::forward<Args>(args)...);
  }
  template <typename T, typename... Args>
  void Reply(const protocol::Message& inbound, Args&&... args) const {
    if (auto peer = GetPeer(inbound)) Reply<T>(peer, std::forward<Args>(args)...);
  }
  template <typename T>
  void Reply(const protocol::Message& inbound, std::unique_ptr<T>&& outbound) const {
    Assert(broadcaster_ != nullptr);
    if (auto peer = GetPeer(inbound)) broadcaster_->SendMessage<T>(peer, std::move(outbound));
  }
  template <typename T>
  void Send(std::shared_ptr<net::Peer> peer, std::unique_ptr<T>&& outbound) const {
    Assert(broadcaster_ != nullptr);
    if (peer != nullptr) broadcaster_->SendMessage<T>(peer, std::move(outbound));
  }

  Broadcaster* broadcaster_ = nullptr;
  const net::PeerRegistry* registry_ = nullptr;
};

}  // namespace hornet::node::dispatch