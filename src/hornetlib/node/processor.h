// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <memory>
#include <queue>
#include <utility>

#include "message/visitor.h"
#include "net/peer.h"
#include "node/broadcaster.h"
#include "node/inbound_message.h"
#include "node/inbound_handler.h"
#include "node/outbound_message.h"
#include "protocol/factory.h"
#include "protocol/message.h"

namespace hornet::node {

class Processor : public InboundHandler {
 public:
  Processor(const protocol::Factory& factory, Broadcaster& broadcaster);

  void InitiateHandshake(std::shared_ptr<net::Peer> peer);

  // Message handlers
  virtual void Visit(const message::Ping&) override;
  virtual void Visit(const message::SendCompact&) override;
  virtual void Visit(const message::Verack&) override;
  virtual void Visit(const message::Version&) override;

 private:
  protocol::Capabilities& GetPeerCapabilities() {
    return GetPeer()->GetCapabilities();
  }
  void AdvanceHandshake(std::shared_ptr<net::Peer> peer,
                        protocol::Handshake::Transition transition);
  void SendPeerPreferences();

  const protocol::Factory& factory_;
};

}  // namespace hornet::node
