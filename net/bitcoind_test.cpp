#include "net/bitcoind.h"

#include "message/registry.h"
#include "message/version.h"
#include "net/constants.h"
#include "net/receive.h"
#include "net/socket.h"
#include "protocol/dispatch.h"
#include "protocol/factory.h"
#include "protocol/framer.h"
#include "protocol/message.h"

#include <gtest/gtest.h>

namespace hornet::net {
namespace {

void SwapVersionMessages(Network network) {
  // Launch bitcoind on regtest
  Bitcoind node = Bitcoind::Launch(network);

  // Try connecting to it
  Socket sock = Socket::Connect(kLocalhost, node.port);

  // Send a version message
  sock.Write(FrameMessage(node.magic, message::Version{}));

  // Receive a version message
  const auto msgin = ReceiveMessage<message::Version>(sock, node.magic);
  EXPECT_TRUE(msgin->GetName() == "version");
}

TEST(BitcoindTest, SwapVersionMessagesRegtest) {
  SwapVersionMessages(Network::Regtest);
}

TEST(BitcoindTest, SwapVersionMessagesTestnet) {
  SwapVersionMessages(Network::Testnet);
}

TEST(BitcoindTest, SwapVersionMessagesMainnet) {
  SwapVersionMessages(Network::Mainnet);
}

}  // namespace
}  // namespace hornet::net
