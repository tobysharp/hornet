#include "message/verack.h"

#include <array>

#include "message/registry.h"
#include "message/version.h"
#include "net/bitcoind.h"
#include "net/constants.h"
#include "net/socket.h"
#include "protocol/constants.h"
#include "protocol/dispatch.h"
#include "protocol/factory.h"
#include "protocol/framer.h"
#include "protocol/parser.h"

#include <gtest/gtest.h>

namespace hornet::message {
namespace {

TEST(VerackMessageTest, TestVerack) {
  Verack m;
  EXPECT_EQ(m.GetName(), "verack");
}

TEST(VerackMessageTest, TestSendVerack) {
  const auto network = net::Network::Regtest;
  net::Bitcoind node = net::Bitcoind::Launch(network);
  std::array<uint8_t, 2048> buf{};
  protocol::Factory factory = message::CreateMessageFactory();
 
  // Try connecting to it
  const auto sock = net::Socket::Connect(net::kLocalhost, node.port);

  // Send a version message
  sock.Write(FrameMessage(node.magic, Version{}));

  // Try reading the response
  size_t n = sock.Read(buf);
  EXPECT_GT(n, 24);
  EXPECT_EQ(*reinterpret_cast<const protocol::Magic*>(&buf[0]), node.magic);

  // Attempt to parse and deserialize
  const auto msgin = protocol::ParseMessage<Version>(factory, node.magic, {buf.data(), n});
  EXPECT_TRUE(msgin->GetName() == "version");

  // Send verack message
  sock.Write(FrameMessage(node.magic, Verack{}));

  // Read response
  n = sock.Read(buf);
  EXPECT_EQ(n, 24);
  EXPECT_EQ(*reinterpret_cast<const protocol::Magic*>(&buf[0]), node.magic);

  // Parse and deserialize
  const auto verack = protocol::ParseMessage<Verack>(factory, node.magic, {buf.data(), n});
  EXPECT_EQ(verack->GetName(), "verack");
}

}  // namespace
}  // namespace hornet::message
