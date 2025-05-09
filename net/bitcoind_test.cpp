#include "net/bitcoind.h"

#include "messages/registry.h"
#include "messages/version.h"
#include "net/constants.h"
#include "net/socket.h"
#include "protocol/message.h"
#include "protocol/message_factory.h"
#include "protocol/message_framer.h"
#include "protocol/message_dispatch.h"

#include <gtest/gtest.h>

namespace {

void SwapVersionMessages(Network network) {
    // Launch bitcoind on regtest
    Bitcoind node = Bitcoind::Launch(network);

    // Try connecting to it
    Socket sock = Socket::Connect(kLocalhost, node.port);

    // Send a version message
    VersionMessage msgout;
    sock.Write(FrameMessage(node.magic, msgout));

    // Try reading the response
    std::array<uint8_t, 2048> buf{};
    const size_t n = sock.Read(buf);
    EXPECT_GT(n, 24);

    EXPECT_EQ(*reinterpret_cast<const Magic*>(&buf[0]), node.magic);

    // Attempt to parse and deserialize
    MessageFactory factory = CreateMessageFactory();
    const auto msgin = ParseMessage(factory, node.magic, {buf.data(), n});

    EXPECT_TRUE(msgin->GetName() == "version");
}

TEST(BitcoindTest, SwapVersionMessagesRegtest) {
    SwapVersionMessages(Network::Regtest);
}


TEST(BitcoindTest, SwapVersionMessagesTestnett) {
    SwapVersionMessages(Network::Testnet);
}

TEST(BitcoindTest, SwapVersionMessagesMainnett) {
    SwapVersionMessages(Network::Mainnet);
}

}  // namespace
