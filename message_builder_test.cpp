#include "version_message.h"
#include "message_builder.h"
#include "protocol.h"

#include <gtest/gtest.h>

TEST(MessageBuilderTest, VersionMessageFraming) {
    VersionMessage msg;
    msg.timestamp = 1700000000;
    msg.user_agent = "/btchornet:test/";
  
    MessageBuilder builder(Magic::Main);
    builder << "version" << msg;
  
    auto bytes = builder.AsBytes();
    ASSERT_GT(bytes.size(), 0u);
  
    // Basic sanity check: message starts with magic
    EXPECT_EQ(bytes[0], 0xF9);
    EXPECT_EQ(bytes[1], 0xBE);
    EXPECT_EQ(bytes[2], 0xB4);
    EXPECT_EQ(bytes[3], 0xD9);
  
    // Length field: check payload size
    uint32_t length = *reinterpret_cast<const uint32_t*>(&bytes[16]);
    EXPECT_EQ(length, bytes.size() - 24);
  
    // Check known hash of payload (double SHA256 of payload)
    const auto payload = std::span<const uint8_t>(bytes.data() + 24, bytes.size() - 24);
    const auto checksum = Sha256(Sha256(payload));
    EXPECT_TRUE(std::equal(checksum.begin(), checksum.begin() + 4, bytes.begin() + 20));
  }
  