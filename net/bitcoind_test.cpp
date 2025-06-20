// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
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
  Bitcoind node = Bitcoind::ConnectOrLaunch(network);

  // Try connecting to it
  Socket sock = Socket::Connect(kLocalhost, node.GetPort());

  // Send a version message
  sock.Write(protocol::FrameMessage(node.GetMagic(), message::Version{}));

  // Receive a version message
  const auto msgin = ReceiveMessage<message::Version>(sock, node.GetMagic());
  EXPECT_TRUE(msgin->GetName() == "version");
}

TEST(BitcoindTest, SwapVersionMessagesMainnet) {
  SwapVersionMessages(Network::Mainnet);
}

}  // namespace
}  // namespace hornet::net
