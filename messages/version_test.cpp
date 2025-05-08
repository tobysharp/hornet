#include "messages/version.h"

#include "messages/registry.h"
#include "protocol/constants.h"
#include "protocol/message_dispatch.h"
#include "protocol/message_factory.h"
#include "protocol/message_framer.h"
#include "protocol/message_parser.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <gtest/gtest.h>

namespace {

TEST(VersionMessageTest, SerializesCorrectly) {
    VersionMessage m;
    m.version = 70015;
    m.services = 1;
    m.timestamp = 1234567890;
    m.addr_recv.fill(0xAA);
    m.addr_from.fill(0xBB);
    m.nonce = 0xDEADBEEFCAFEBABE;
    m.user_agent = "/Satoshi:0.21.0/";
    m.start_height = 680000;
    m.relay = true;

    MessageFramer framer(Magic::Main);
    framer.Frame(m);

    const auto& buf = framer.Buffer();
    ASSERT_GT(buf.size(), 24);

    // Check magic
    EXPECT_EQ(buf[0], 0xF9);
    EXPECT_EQ(buf[1], 0xBE);
    EXPECT_EQ(buf[2], 0xB4);
    EXPECT_EQ(buf[3], 0xD9);

    // Check command
    std::string cmd(reinterpret_cast<const char*>(&buf[4]), 7);
    EXPECT_EQ(cmd.substr(0, 7), "version");

    // Check payload prefix
    size_t payload_offset = 24;
    uint32_t v = *reinterpret_cast<const uint32_t*>(&buf[payload_offset]);
    EXPECT_EQ(v, 70015u);

    uint64_t services = *reinterpret_cast<const uint64_t*>(&buf[payload_offset + 4]);
    EXPECT_EQ(services, 1u);
}

TEST(VersionMessageTest, RoundTripSerialization) {
    VersionMessage original;
    original.version = 70015;
    original.services = 1;
    original.timestamp = 1234567890;
    original.addr_recv.fill(0xAA);
    original.addr_from.fill(0xBB);
    original.nonce = 0xDEADBEEFCAFEBABE;
    original.user_agent = "/TestNode:0.1.0/";
    original.start_height = 800000;
    original.relay = true;

    const MessageFactory factory = CreateMessageFactory();
    const auto buffer = FrameMessage(Magic::Main, original);
    auto msg = ParseMessage<VersionMessage>(factory, Magic::Main, buffer);

    EXPECT_EQ(*msg, original);
}

TEST(VersionMessageTest, SendVersionMessage) {
    // Set up socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(sock, -1);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8333); // mainnet port
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    ASSERT_NE(connect(sock, (sockaddr*)&addr, sizeof(addr)), -1);

    // Create and serialize the version message
    VersionMessage msg;
    msg.timestamp = 1700000000;
    msg.user_agent = "/btchornet:test/";

    MessageFramer framer;
    framer.Frame(msg);
    const auto& bytes = framer.Buffer();
    send(sock, bytes.data(), bytes.size(), 0);

    // Receive response
    uint8_t header[24];
    size_t n = recv(sock, header, sizeof(header), 0);
    ASSERT_EQ(n, 24) << "Expected 24-byte message header from bitcoind";

    // Expect response command to be "version"
    std::string command(reinterpret_cast<char*>(header + 4), 12);
    EXPECT_EQ(strcmp(command.c_str(), "version"), 0);

    close(sock);
}

}  // namespace 