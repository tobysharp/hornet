// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/protocol/message/version.h"

#include "hornetlib/protocol/constants.h"
#include "hornetlib/protocol/dispatch.h"
#include "hornetlib/protocol/message_factory.h"
#include "hornetlib/protocol/framer.h"
#include "hornetlib/protocol/parser.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <gtest/gtest.h>

namespace hornet::protocol::message {
namespace {

TEST(VersionTest, SerializesCorrectly) {
  Version m;
  m.version = kCurrentVersion;
  m.services = 1;
  m.timestamp = 1234567890;
  m.addr_recv.fill(0xAA);
  m.addr_from.fill(0xBB);
  m.nonce = 0xDEADBEEFCAFEBABE;
  m.user_agent = "/Satoshi:0.21.0/";
  m.start_height = 680000;
  m.relay = true;

  Framer framer(Magic::Main);
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
  EXPECT_EQ(v, kCurrentVersion);

  uint64_t services = *reinterpret_cast<const uint64_t*>(&buf[payload_offset + 4]);
  EXPECT_EQ(services, 1u);
}

TEST(VersionTest, RoundTripSerialization) {
  Version original;
  original.version = kCurrentVersion;
  original.services = 1;
  original.timestamp = 1234567890;
  original.addr_recv.fill(0xAA);
  original.addr_from.fill(0xBB);
  original.nonce = 0xDEADBEEFCAFEBABE;
  original.user_agent = "/TestNode:0.1.0/";
  original.start_height = 800000;
  original.relay = true;

  const MessageFactory& factory = MessageFactory::Default();
  const auto buffer = FrameMessage(Magic::Main, original);
  auto msg = ParseMessage<message::Version>(factory, Magic::Main, buffer);

  EXPECT_EQ(*msg, original);
}

}  // namespace
}  // namespace hornet::protocol::message
