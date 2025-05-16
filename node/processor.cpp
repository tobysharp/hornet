#include <queue>
#include <utility>

#include "message/registry.h"
#include "message/verack.h"
#include "message/version.h"
#include "net/constants.h"
#include "node/broadcaster.h"
#include "node/inbound_message.h"
#include "node/processor.h"
#include "protocol/handshake.h"

namespace hornet::node {

Processor::Processor(const protocol::Factory& factory, Broadcaster& broadcaster)
    : factory_(factory), broadcaster_(broadcaster) {}

void Processor::Process(const InboundMessage& msg) {
  // Use an RAII pattern to guarantee inbound_ is valid if and only if we're inside
  // one of the Visit methods.
  struct ScopedInboundGuard {
    ScopedInboundGuard(Processor& p, const InboundMessage* msg) : p_(p) {
      p_.inbound_ = msg;
    }
    ~ScopedInboundGuard() {
      p_.inbound_ = nullptr;
    }
    Processor& p_;
  } guard(*this, &msg);

  msg.GetMessage().Accept(*this);
}

void Processor::Visit(const message::Verack& v) {
  AdvanceHandshake(protocol::Handshake::Transition::ReceiveVerack);
}

void Processor::Visit(const message::Version& v) {
  // TODO: Validate the version message parameters, and throw if we don't accept it.
  AdvanceHandshake(protocol::Handshake::Transition::ReceiveVersion);
}

void Processor::AdvanceHandshake(protocol::Handshake::Transition transition) {
  auto peer = inbound_->GetPeer();
  auto& handshake = peer->GetHandshake();
  const auto [next, command] = handshake.AdvanceState(transition);
  if (!command.empty()) {
    OutboundMessagePtr outbound = std::make_shared<OutboundMessage>(factory_.Create(command));
    broadcaster_.SendToOne(peer, outbound);
    handshake.AdvanceState(next);
  }
}

}  // namespace hornet::node
