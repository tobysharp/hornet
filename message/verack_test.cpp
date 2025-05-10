#include "message/verack.h"

#include <array>

#include "message/registry.h"
#include "message/version.h"
#include "net/bitcoind.h"
#include "net/connection.h"
#include "net/constants.h"
#include "net/receive.h"
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
  // Launch a local bitcoind node instance if necessary, and connect
  net::Bitcoind node = net::Bitcoind::Launch();
  const auto sock = net::Socket::Connect(net::kLocalhost, node.port);
  net::Connection connection{sock, node.magic};

  // Send a version message
  sock.Write(FrameMessage(node.magic, Version{}));
  const auto version = connection.NextMessage();
  EXPECT_TRUE(version->GetName() == "version");

  // Send verack message
  sock.Write(FrameMessage(node.magic, Verack{}));
  const auto verack =connection.NextMessage();
  EXPECT_EQ(verack->GetName(), "verack");
}

}  // namespace
}  // namespace hornet::message
