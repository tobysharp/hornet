#include "node/sync_manager.h"

#include "net/bitcoind.h"
#include "node/engine.h"
#include "protocol/handshake.h"
#include "util/timeout.h"

#include <gtest/gtest.h>

namespace hornet::node {
namespace {

TEST(SyncManagerTest, TestGetHeaders) {
    net::Bitcoind node = net::Bitcoind::Launch();
    node.MineBlocks(3000);
    Engine engine{node.GetMagic()};
    const auto peer = engine.AddOutboundPeer(net::kLocalhost, node.GetPort());
    util::Timeout timeout(2000);  // Wait up to one second for the hanshake to complete.
    engine.RunMessageLoop([&](const Engine&) {
        return timeout.IsExpired();
    });
    EXPECT_TRUE(peer->GetHandshake().IsComplete());
}

TEST(SyncManagerTest, TestMainnetSyncHeaders) {
    net::Bitcoind node = net::Bitcoind::Connect(net::Network::Mainnet);
    Engine engine{node.GetMagic()};
    const auto peer = engine.AddOutboundPeer(net::kLocalhost, node.GetPort());
    // engine.RunMessageLoop([&](const Engine&) {
    //     return engine.GetSyncManager().GetHeaderTip() > 0;
    // });
}

}  // namespace
}  // namespace hornet::node