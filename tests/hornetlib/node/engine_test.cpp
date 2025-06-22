// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "node/engine.h"

#include "data/timechain.h"
#include "net/bitcoind.h"
#include "net/constants.h"
#include "net/peer.h"
#include "node/engine.h"
#include "protocol/handshake.h"
#include "util/timeout.h"

#include <gtest/gtest.h>

namespace hornet::node {
namespace {

TEST(EngineTest, TestHandshake) {
    net::Bitcoind node = net::Bitcoind::ConnectOrLaunch();
    data::Timechain timechain_;
    Engine engine_{timechain_, node.GetMagic()};
    const auto peer = engine_.AddOutboundPeer(net::kLocalhost, node.GetPort());
    util::Timeout timeout(1000);  // Wait up to one second for the hanshake to complete.
    engine_.RunMessageLoop([&](const Engine&) {
        return timeout.IsExpired() || peer->GetHandshake().IsComplete();
    });
    EXPECT_TRUE(peer->GetHandshake().IsComplete());
}

}  // namespace
}  // namespace hornet::node
