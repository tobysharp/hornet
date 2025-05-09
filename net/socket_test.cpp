#include "net/socket.h"

#include "messages/registry.h"
#include "messages/version.h"
#include "net/constants.h"
#include "protocol/message_dispatch.h"
#include "protocol/message_framer.h"

#include <gtest/gtest.h>

namespace {

TEST(SocketTest, ConnectsToBitcoindAndExchangesVersion) {
    // 1. Connect
    Socket sock = Socket::Connect(kLocalhost, kRegtestPort);

    // 2. Send version message
    VersionMessage out;
    sock.Write(FrameMessage(Magic::Testnet, out));

    // 3. Read response
    std::array<uint8_t, 2048> recv_buf{};
    size_t n = sock.Read(recv_buf);

    // 4. Attempt to parse and deserialize
    MessageFactory factory = CreateMessageFactory();
    auto msg = ParseMessage(factory, Magic::Testnet, {recv_buf.data(), n});

    EXPECT_TRUE(msg->GetName() == "version");
}

}  // namespace
