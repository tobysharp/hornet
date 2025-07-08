// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include <queue>
#include <utility>

#include "hornetlib/net/constants.h"
#include "hornetlib/net/peer.h"
#include "hornetlib/net/peer_manager.h"
#include "hornetlib/net/peer_registry.h"
#include "hornetlib/node/broadcaster.h"
#include "hornetlib/node/peer_negotiator.h"
#include "hornetlib/protocol/capabilities.h"
#include "hornetlib/protocol/handshake.h"
#include "hornetlib/protocol/message_factory.h"
#include "hornetlib/protocol/message/ping.h"
#include "hornetlib/protocol/message/pong.h"
#include "hornetlib/protocol/message/sendcmpct.h"
#include "hornetlib/protocol/message/verack.h"
#include "hornetlib/protocol/message/version.h"
#include "hornetlib/util/throw.h"

namespace hornet::node {

void PeerNegotiator::OnMessage(const protocol::message::Ping& ping) {
  Reply<protocol::message::Pong>(ping, ping.GetNonce());
}

void PeerNegotiator::OnMessage(const protocol::message::SendCompact& sendcmpct) {
  // The sendcmpct message was introduced in BIP-152. Details here:
  // https://github.com/bitcoin/bips/blob/master/bip-0152.mediawiki

  // Upon receipt of a "sendcmpct" message with the second integer set to something other than 1,
  // nodes MUST treat the peer as if they had not received the message (as it indicates the peer
  // will provide an unexpected encoding in cmpctblock, and/or other, messages).
  if (sendcmpct.GetVersion() != 1) return;

  // Set or clear the flag for compact blocks based on the value in the message.
  if (auto peer = GetPeer(sendcmpct))
    peer->GetCapabilities().SetCompactBlocks(sendcmpct.IsCompact());
}

void PeerNegotiator::OnMessage(const protocol::message::Verack& verack) {
  AdvanceHandshake(GetPeer(verack), protocol::Handshake::Transition::ReceiveVerack);
}

void PeerNegotiator::OnMessage(const protocol::message::Version& v) {
  if (v.version < protocol::kMinSupportedVersion)
    util::ThrowRuntimeError("Received unsupported protocol version number ", v.version, ".");
  
  // The peer's version number is sent to the minimum of the two exchanged versions.
  if (const auto peer = GetPeer(v)) {
    peer->GetCapabilities().SetVersion(std::min(protocol::kCurrentVersion, v.version));
    peer->GetCapabilities().SetStartHeight(v.start_height);
    AdvanceHandshake(peer, protocol::Handshake::Transition::ReceiveVersion);
  }
}

// Sets the Handshake state machine into the Start state, ready to begin negotiation.
void PeerNegotiator::OnPeerConnect(net::WeakPeer weak) {
  if (const auto peer = weak.lock())
    AdvanceHandshake(peer, protocol::Handshake::Transition::Begin);
}

void PeerNegotiator::OnLoop(net::PeerManager& manager) {
  manager.RemoveClosedPeers();
}

// Advances the Handshake state machine and performs any necessary resulting actions.
void PeerNegotiator::AdvanceHandshake(net::SharedPeer peer,
                                 protocol::Handshake::Transition transition) {
  auto& handshake = peer->GetHandshake();

  // Run the state machine forward until we complete or must wait for new input.
  auto action = handshake.AdvanceState(transition);
  while (action.next != protocol::Handshake::Transition::None) {
    Send(peer, protocol::MessageFactory::Default().Create(action.command));
    action = handshake.AdvanceState(action.next);
  }

  // Once the handshake is complete, send our preference notifications.
  if (handshake.IsComplete()) {
    SendPeerPreferences(peer);
  }
}

void PeerNegotiator::SendPeerPreferences(std::shared_ptr<net::Peer> peer) {
  // Request compact blocks if available
  if (peer->GetCapabilities().GetVersion() >= protocol::kMinVersionForSendCompact)
    Reply<protocol::message::SendCompact>(peer);
}

}  // namespace hornet::node
