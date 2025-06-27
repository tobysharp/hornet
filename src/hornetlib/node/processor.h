// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <memory>
#include <queue>
#include <utility>

#include "hornetlib/message/visitor.h"
#include "hornetlib/net/peer.h"
#include "hornetlib/node/broadcaster.h"
#include "hornetlib/node/inbound_message.h"
#include "hornetlib/node/inbound_handler.h"
#include "hornetlib/node/outbound_message.h"
#include "hornetlib/protocol/factory.h"
#include "hornetlib/protocol/message.h"

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
