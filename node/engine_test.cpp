#include "node/engine.h"

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
    net::Bitcoind node = net::Bitcoind::Launch();
    Engine engine_{node.magic};
    const auto peer = engine_.AddOutboundPeer(net::kLocalhost, node.port);
    util::Timeout timeout(1000);  // Wait up to one second for the hanshake to complete.
    engine_.RunMessageLoop([&](const Engine&) {
        return timeout.IsExpired() || peer->GetHandshake().IsComplete();
    });
    EXPECT_TRUE(peer->GetHandshake().IsComplete());
}

}  // namespace
}  // namespace hornet::node
