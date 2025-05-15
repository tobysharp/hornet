#pragma once

#include "message/visitor.h"

namespace hornet::node {

class Processor : public message::Visitor {
 public:

  // Message handlers
  void Visit(const message::Verack&);
  void Visit(const message::Version&);

 private:
  // TODO: State probably includes InboundMessage and outbox queue.
};

}  // namespace hornet::node
