// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetnodelib/sync/sync_manager.h"

#include "hornetlib/data/sidecar_binding.h"
#include "hornetlib/data/timechain.h"
#include "hornetlib/protocol/handshake.h"
#include "hornetlib/util/log.h"
#include "hornetlib/util/timeout.h"
#include "hornetnodelib/dispatch/protocol_loop.h"
#include "hornetnodelib/dispatch/peer_negotiator.h"
#include "hornetnodelib/sync/sync_manager.h"
#include "hornetnodelib/sync/types.h"
#include "testutil/net/bitcoind_peer.h"

#include <gtest/gtest.h>

namespace hornet::node::dispatch {
namespace {

TEST(SyncManagerTest, TestGetHeaders) {
    auto node = test::BitcoindPeer::ConnectOrLaunch();
    net::PeerManager peers;
    ProtocolLoop loop(peers);
    PeerNegotiator negotiator;
    loop.AddEventHandler(&negotiator);
    const auto peer = loop.AddOutboundPeer(net::kLocalhost, node.GetPort());
    util::Timeout timeout(1000);  // Wait up to one second for the handshake to complete.
    loop.RunMessageLoop([&]() {
        return timeout.IsExpired() || peer->GetHandshake().IsComplete();
    });
    EXPECT_TRUE(peer->GetHandshake().IsComplete());
}

TEST(SyncManagerTest, TestMainnetSyncHeaders) {
    auto node = test::BitcoindPeer::Connect();
    net::PeerManager peers;
    ProtocolLoop loop(peers);
    PeerNegotiator negotiator;
    loop.AddEventHandler(&negotiator);
    data::Timechain timechain;
    auto validation = sync::BlockValidationBinding::Create(timechain);
    sync::SyncManager sync(timechain, validation);
    loop.AddEventHandler(&sync);

    loop.AddOutboundPeer(net::kLocalhost, node.GetPort());
    loop.RunMessageLoop([&]() {
        return timechain.ReadHeaders()->ChainLength() >= 9000;
    });
    LogDebug() << "Header count: " << timechain.ReadHeaders()->ChainLength();
    EXPECT_TRUE(timechain.ReadHeaders()->ChainLength() >= 9000);
}

}  // namespace
}  // namespace hornet::node::dispatch