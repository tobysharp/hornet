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
#include "util/throw.h"

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
  };

  // Check for liveness of peer, i.e. that it hasn't been removed from PeerManager.
  if (const auto peer = msg.GetPeer()) {
    // Check for the socket still being open. It may have been closed by the Connection.
    if (peer->GetConnection().GetSocket().IsOpen()) {
      ScopedInboundGuard guard{*this, &msg};
      // Dispatch the message for processing via the Visitor pattern.
      msg.GetMessage().Accept(*this);
    }
  }
}

void Processor::Visit(const message::SendCompact& sendcmpct) {
  // The sendcmpct message was introduced in BIP-152. Details here:
  // https://github.com/bitcoin/bips/blob/master/bip-0152.mediawiki

  // Upon receipt of a "sendcmpct" message with the second integer set to something other than 1,
  // nodes MUST treat the peer as if they had not received the message (as it indicates the peer
  // will provide an unexpected encoding in cmpctblock, and/or other, messages).
  if (sendcmpct.GetVersion() != 1) return;

  // Set or clear the flag for compact blocks based on the value in the message.
  GetPeerCapabilities().SetCompactBlocks(sendcmpct.IsCompact());
}

void Processor::Visit(const message::Verack& v) {
  AdvanceHandshake(inbound_->GetPeer(), protocol::Handshake::Transition::ReceiveVerack);
}

void Processor::Visit(const message::Version& v) {
  if (v.version < protocol::kMinSupportedVersion)
    util::ThrowRuntimeError("Received unsupported protocol version number ", v.version, ".");
  
  GetPeerCapabilities().SetVersion(std::min(protocol::kCurrentVersion, v.version));
  AdvanceHandshake(inbound_->GetPeer(), protocol::Handshake::Transition::ReceiveVersion);
}

void Processor::InitiateHandshake(std::shared_ptr<net::Peer> peer) {
  AdvanceHandshake(peer, protocol::Handshake::Transition::Begin);
}

void Processor::AdvanceHandshake(std::shared_ptr<net::Peer> peer,
                                 protocol::Handshake::Transition transition) {
  auto& handshake = peer->GetHandshake();
  auto action = handshake.AdvanceState(transition);
  while (action.next != protocol::Handshake::Transition::None) {
    OutboundMessage outbound{factory_.Create(action.command)};
    broadcaster_.SendToOne(peer, std::move(outbound));
    action = handshake.AdvanceState(action.next);
  }
}

}  // namespace hornet::node
