#pragma once

#include <memory>
#include <queue>
#include <utility>

#include "net/peer.h"
#include "message/visitor.h"
#include "protocol/factory.h"
#include "protocol/message.h"

namespace hornet::node {

class Processor : public message::Visitor {
 public:
  Processor();

  // Message handlers
  void Visit(const message::Verack&);
  void Visit(const message::Version&);

 private:
  void AdvanceHandshake(protocol::Handshake::Transition transition);

  // TODO: State probably includes InboundMessage and outbox queue.
  std::shared_ptr<net::Peer> peer_;
  protocol::Factory factory_;
  std::queue<std::pair<std::weak_ptr<net::Peer>, std::unique_ptr<protocol::Message>>> outbox_;
};

}  // namespace hornet::node
