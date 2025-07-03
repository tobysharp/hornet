// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/node/sync_manager.h"

#include "hornetlib/data/timechain.h"
#include "hornetlib/net/bitcoind.h"
#include "hornetlib/node/engine.h"
#include "hornetlib/protocol/handshake.h"
#include "hornetlib/util/log.h"
#include "hornetlib/util/timeout.h"

#include <gtest/gtest.h>

namespace hornet::node {
namespace {

TEST(SyncManagerTest, TestGetHeaders) {
    net::Bitcoind node = net::Bitcoind::ConnectOrLaunch();
    data::Timechain timechain;
    Engine engine{timechain, node.GetMagic()};
    const auto peer = engine.AddOutboundPeer(net::kLocalhost, node.GetPort());
    util::Timeout timeout(1000);  // Wait up to one second for the handshake to complete.
    engine.RunMessageLoop([&](const Engine&) {
        return timeout.IsExpired() || peer->GetHandshake().IsComplete();
    });
    EXPECT_TRUE(peer->GetHandshake().IsComplete());
}

TEST(SyncManagerTest, TestMainnetSyncHeaders) {
    net::Bitcoind node = net::Bitcoind::Connect();
    data::Timechain timechain;
    Engine engine{timechain, node.GetMagic()};
    const auto peer = engine.AddOutboundPeer(net::kLocalhost, node.GetPort());
    engine.RunMessageLoop([&](const Engine&) {
        return timechain.Headers().GetHeaviestLength() >= 9000;
    });
    LogDebug() << "Header count: " << timechain.Headers().GetHeaviestLength();
    EXPECT_TRUE(timechain.Headers().GetHeaviestLength() >= 9000);
}

TEST(SyncManagerTest, TestMainnetSyncBlocks) {
    net::Bitcoind node = net::Bitcoind::Connect();
    data::Timechain timechain;
    Engine engine{timechain, node.GetMagic()};
    engine.AddOutboundPeer(net::kLocalhost, node.GetPort());
    engine.RunMessageLoop([&](const Engine&) {
        return false;
    });
}

}  // namespace
}  // namespace hornet::node