#pragma once

#include <memory>
#include <queue>
#include <utility>

#include "net/peer.h"
#include "message/visitor.h"
#include "protocol/message.h"

namespace hornet::node {

class Processor : public message::Visitor {
 public:

  // Message handlers
  void Visit(const message::Verack&);
  void Visit(const message::Version&);

 private:
  // TODO: State probably includes InboundMessage and outbox queue.
  std::shared_ptr<net::Peer> peer_;
  std::queue<std::pair<std::weak_ptr<net::Peer>, std::unique_ptr<protocol::Message>>> outbox_;
};

}  // namespace hornet::node
