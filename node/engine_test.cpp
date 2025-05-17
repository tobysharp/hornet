#include "node/engine.h"

#include "net/bitcoind.h"
#include "net/constants.h"
#include "net/peer.h"
#include "node/engine.h"
#include "protocol/handshake.h"

#include <gtest/gtest.h>

namespace hornet::node {
namespace {

TEST(EngineTest, TestHandshake) {
    net::Bitcoind node = net::Bitcoind::Launch();
    Engine engine_{node.magic};
    const auto peer = engine_.AddOutboundPeer(net::kLocalhost, node.port);
    engine_.RunMessageLoop(5000);
    EXPECT_TRUE(peer->GetHandshake().IsComplete());
}

}  // namespace
}  // namespace hornet::node
