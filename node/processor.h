#pragma once

#include <memory>
#include <queue>
#include <utility>

#include "message/visitor.h"
#include "net/peer.h"
#include "node/broadcaster.h"
#include "node/inbound_message.h"
#include "protocol/factory.h"
#include "protocol/message.h"

namespace hornet::node {

class Processor : public message::Visitor {
 public:
  Processor(const protocol::Factory& factory, Broadcaster& broadcaster);

  void InitiateHandshake(std::shared_ptr<net::Peer> peer);
  void Process(const InboundMessage& msg);

  // Message handlers
  void Visit(const message::SendCompact&);
  void Visit(const message::Verack&);
  void Visit(const message::Version&);

 private:
  void AdvanceHandshake(std::shared_ptr<net::Peer> peer,
                        protocol::Handshake::Transition transition);
  void SendPeerPreferences();
  protocol::Capabilities& GetPeerCapabilities() {
    return inbound_->GetPeer()->GetCapabilities();
  }
  const InboundMessage* inbound_ = nullptr;
  const protocol::Factory& factory_;
  Broadcaster& broadcaster_;
};

}  // namespace hornet::node
