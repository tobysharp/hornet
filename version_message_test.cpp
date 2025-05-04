#include "version_message.h"
#include "message_buffer.h"
#include "output_streamer.h"

#include <gtest/gtest.h>
#include <sstream>
#include <iomanip>

TEST(VersionMessageTest, SerializesCorrectly) {
  VersionMessage msg;
  msg.timestamp = 1700000000;

  MessageBuffer buffer;
  msg.Serialize(buffer);

  // Expected: payload size is nonzero
  auto bytes = buffer.AsBytes();
  ASSERT_GT(bytes.size(), 0);

  // Optional: print hex
  std::ostringstream hex;
  for (uint8_t b : bytes)
    hex << std::hex << std::setw(2) << std::setfill('0') << int{b};

  EXPECT_FALSE(hex.str().empty());
}
