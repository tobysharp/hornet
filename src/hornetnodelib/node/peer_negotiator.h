// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <memory>
#include <queue>
#include <utility>

#include "hornetnodelib/net/peer.h"
#include "hornetnodelib/net/peer_registry.h"
#include "hornetnodelib/node/broadcaster.h"
#include "hornetnodelib/node/event_handler.h"
#include "hornetlib/protocol/capabilities.h"
#include "hornetlib/protocol/message.h"

namespace hornet::protocol::message {
  class Ping;
  class SendCompact;
  class Verack;
  class Version;
}  // namespace hornet::protocol::message
namespace hornet::net {
  class PeerManager;
}  // namespace hornet::net


namespace hornet::node {

class PeerNegotiator : public EventHandler {
 public:
  PeerNegotiator() = default;

  virtual void OnPeerConnect(net::SharedPeer peer) override;

  // Message handlers
  virtual void OnMessage(const protocol::message::Ping&) override;
  virtual void OnMessage(const protocol::message::SendCompact&) override;
  virtual void OnMessage(const protocol::message::Verack&) override;
  virtual void OnMessage(const protocol::message::Version&) override;

 private:
  void AdvanceHandshake(std::shared_ptr<net::Peer> peer,
                        protocol::Handshake::Transition transition);
  void SendPeerPreferences(std::shared_ptr<net::Peer> peer);
};

}  // namespace hornet::node
