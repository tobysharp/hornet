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
  net::Connection connection{net::kLocalhost, node.port, node.magic};

  // Send a version message
  connection.SendMessage(Version{});
  const auto version = connection.NextMessage();
  ASSERT_NE(version, nullptr);
  EXPECT_TRUE(version->GetName() == "version");

  // Send verack message
  connection.SendMessage(Verack{});
  const auto verack = connection.NextMessage();
  ASSERT_NE(verack, nullptr);
  EXPECT_EQ(verack->GetName(), "verack");
}

}  // namespace
}  // namespace hornet::message
