#include <queue>
#include <utility>

#include "message/registry.h"
#include "message/verack.h"
#include "message/version.h"
#include "net/constants.h"
#include "node/processor.h"
#include "protocol/handshake.h"

namespace hornet::node {

Processor::Processor() : factory_(message::CreateMessageFactory()) {}

void Processor::Visit(const message::Verack& v) {
  AdvanceHandshake(protocol::Handshake::Transition::ReceiveVerack);
}

void Processor::Visit(const message::Version& v) {
  // TODO: Validate the version message parameters, and throw if we don't accept it.
  AdvanceHandshake(protocol::Handshake::Transition::ReceiveVersion);
}

void Processor::AdvanceHandshake(protocol::Handshake::Transition transition) {
  auto& handshake = peer_->GetHandshake();
  const auto [next, command] = handshake.AdvanceState(transition);
  if (!command.empty()) {
    auto msg = factory_.Create(command);
    outbox_.push({peer_, std::move(msg)});
    handshake.AdvanceState(next);
  }
}

}  // namespace hornet::node
