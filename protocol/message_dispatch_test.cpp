#include "protocol/message_dispatch.h"
#include "protocol/message.h"
#include "messages/registry.h"
#include "messages/version.h"
#include "protocol/message_framer.h"

#include <gtest/gtest.h>

namespace {

TEST(MessageDispatchTest, ParsesAndDeserializesVersionMessage) {
    VersionMessage original;
    original.version = 70015;
    original.services = 0x01;
    original.timestamp = 1700000000;
    original.addr_recv.fill(0xAA);
    original.addr_from.fill(0xBB);
    original.nonce = 0xCAFEBABEDEADBEEF;
    original.user_agent = "/TestNode:0.1.0/";
    original.start_height = 800000;
    original.relay = true;

    const MessageFactory factory = CreateMessageFactory();
    const auto buffer = FrameMessage(Magic::Main, original);
    auto msg = ParseMessage<VersionMessage>(factory, Magic::Main, buffer);

    EXPECT_EQ(msg->version, original.version);
    EXPECT_EQ(msg->services, original.services);
    EXPECT_EQ(msg->timestamp, original.timestamp);
    EXPECT_EQ(msg->addr_recv, original.addr_recv);
    EXPECT_EQ(msg->addr_from, original.addr_from);
    EXPECT_EQ(msg->nonce, original.nonce);
    EXPECT_EQ(msg->user_agent, original.user_agent);
    EXPECT_EQ(msg->start_height, original.start_height);
    EXPECT_EQ(msg->relay, original.relay);
}

}  // namespace
