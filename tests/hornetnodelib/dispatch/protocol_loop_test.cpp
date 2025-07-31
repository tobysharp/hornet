// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetnodelib/dispatch/protocol_loop.h"

#include "hornetlib/data/timechain.h"
#include "hornetlib/protocol/handshake.h"
#include "hornetlib/util/timeout.h"
#include "hornetnodelib/dispatch/protocol_loop.h"
#include "hornetnodelib/dispatch/peer_negotiator.h"
#include "hornetnodelib/net/constants.h"
#include "hornetnodelib/net/peer.h"
#include "testutil/net/bitcoind_peer.h"

#include <gtest/gtest.h>

namespace hornet::node::dispatch {
namespace {

TEST(ProtocolLoopTest, TestHandshake) {
    auto node = test::BitcoindPeer::ConnectOrLaunch();
    net::PeerManager peers;
    ProtocolLoop loop(peers);
    PeerNegotiator negotiator;
    loop.AddEventHandler(&negotiator);
    const auto peer = loop.AddOutboundPeer(net::kLocalhost, node.GetPort());
    util::Timeout timeout(1000);  // Wait up to one second for the handshake to complete.
    loop.RunMessageLoop([&](const ProtocolLoop&) {
        return timeout.IsExpired() || peer->GetHandshake().IsComplete();
    });
    EXPECT_TRUE(peer->GetHandshake().IsComplete());
}

}  // namespace
}  // namespace hornet::node::dispatch
