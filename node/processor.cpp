#include <queue>
#include <utility>

#include "message/ping.h"
#include "message/pong.h"
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
    : InboundHandler(&broadcaster), factory_(factory) {}

void Processor::Visit(const message::Ping& ping) {
  Reply<message::Pong>(ping.GetNonce());
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

void Processor::Visit(const message::Verack&) {
  AdvanceHandshake(GetPeer(), protocol::Handshake::Transition::ReceiveVerack);
}

void Processor::Visit(const message::Version& v) {
  if (v.version < protocol::kMinSupportedVersion)
    util::ThrowRuntimeError("Received unsupported protocol version number ", v.version, ".");
  
  // The peer's version number is sent to the minimum of the two exchanged versions.
  GetPeerCapabilities().SetVersion(std::min(protocol::kCurrentVersion, v.version));
  GetPeerCapabilities().SetStartHeight(v.start_height);

  AdvanceHandshake(GetPeer(), protocol::Handshake::Transition::ReceiveVersion);
}

// Sets the Handshake state machine into the Start state, ready to begin negotiation.
void Processor::InitiateHandshake(std::shared_ptr<net::Peer> peer) {
  AdvanceHandshake(peer, protocol::Handshake::Transition::Begin);
}

// Advances the Handshake state machine and performs any necessary resulting actions.
void Processor::AdvanceHandshake(std::shared_ptr<net::Peer> peer,
                                 protocol::Handshake::Transition transition) {
  auto& handshake = peer->GetHandshake();

  // Run the state machine forward until we complete or must wait for new input.
  auto action = handshake.AdvanceState(transition);
  while (action.next != protocol::Handshake::Transition::None) {
    Send(peer, factory_.Create(action.command));
    action = handshake.AdvanceState(action.next);
  }

  // Once the handshake is complete, send our preference notifications.
  if (handshake.IsComplete()) {
    SendPeerPreferences();
    //sync_manager_.OnHandshakeComplete(GetPeer());
  }
}

void Processor::SendPeerPreferences() {
  // Request compact blocks if available
  if (GetPeerCapabilities().GetVersion() >= protocol::kMinVersionForSendCompact)
    Reply<message::SendCompact>();
}

}  // namespace hornet::node
