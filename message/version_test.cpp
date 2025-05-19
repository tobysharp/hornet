#include "message/version.h"

#include "message/registry.h"
#include "protocol/constants.h"
#include "protocol/dispatch.h"
#include "protocol/factory.h"
#include "protocol/framer.h"
#include "protocol/parser.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <gtest/gtest.h>

namespace hornet::message {
namespace {

TEST(VersionTest, SerializesCorrectly) {
  Version m;
  m.version = protocol::kCurrentVersion;
  m.services = 1;
  m.timestamp = 1234567890;
  m.addr_recv.fill(0xAA);
  m.addr_from.fill(0xBB);
  m.nonce = 0xDEADBEEFCAFEBABE;
  m.user_agent = "/Satoshi:0.21.0/";
  m.start_height = 680000;
  m.relay = true;

  protocol::Framer framer(protocol::Magic::Main);
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
  EXPECT_EQ(v, protocol::kCurrentVersion);

  uint64_t services = *reinterpret_cast<const uint64_t*>(&buf[payload_offset + 4]);
  EXPECT_EQ(services, 1u);
}

TEST(VersionTest, RoundTripSerialization) {
  Version original;
  original.version = protocol::kCurrentVersion;
  original.services = 1;
  original.timestamp = 1234567890;
  original.addr_recv.fill(0xAA);
  original.addr_from.fill(0xBB);
  original.nonce = 0xDEADBEEFCAFEBABE;
  original.user_agent = "/TestNode:0.1.0/";
  original.start_height = 800000;
  original.relay = true;

  const protocol::Factory factory = CreateMessageFactory();
  const auto buffer = protocol::FrameMessage(protocol::Magic::Main, original);
  auto msg = protocol::ParseMessage<message::Version>(factory, protocol::Magic::Main, buffer);

  EXPECT_EQ(*msg, original);
}

}  // namespace
}  // namespace hornet::message
