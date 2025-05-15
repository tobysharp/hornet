#include <queue>
#include <utility>

#include "message/verack.h"
#include "message/version.h"
#include "node/processor.h"
#include "net/constants.h"
#include "protocol/handshake.h"

namespace hornet::node {

void Processor::Visit(const message::Verack& v) {
  using protocol::Handshake;
  // The only time we should be receiving a verack message is as part
  // of the initial handshake with a peer.
  auto& handshake = peer_->GetHandshake();
  handshake.AdvanceState(Handshake::Transition::ReceiveVerack);
}

void Processor::Visit(const message::Version& v) {
  // TODO: Validate the version message parameters, and throw if we don't accept it.

  using protocol::Handshake;
  // The only time we should be receiving a version message is as part
  // of the initial handshake with a peer.
  auto& handshake = peer_->GetHandshake();
  const auto next = handshake.AdvanceState(
      Handshake::Transition::ReceiveVersion);
  std::unique_ptr<protocol::Message> msg;
  if (next == Handshake::Transition::SendVersion)
    msg = std::make_unique<message::Version>();
  else if (next == Handshake::Transition::SendVerack)
    msg = std::make_unique<message::Verack>();
  if (msg) {
    outbox_.push({peer_, std::move(msg)});
    handshake.AdvanceState(next);
  }
}

}  // namespace hornet::node
