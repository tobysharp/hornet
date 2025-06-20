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
    Engine engine_{node.magic};
    const auto peer = engine_.AddOutboundPeer(net::kLocalhost, node.port);
    util::Timeout timeout(2000);  // Wait up to two seconds for the handshake to complete.
    engine_.RunMessageLoop([&](const Engine&) {
        return timeout.IsExpired();
    });
    EXPECT_TRUE(peer->GetHandshake().IsComplete());
}

}  // namespace
}  // namespace hornet::node