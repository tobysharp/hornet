// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/net/bitcoind.h"

#include "hornetlib/net/constants.h"
#include "hornetlib/net/receive.h"
#include "hornetlib/net/socket.h"
#include "hornetlib/protocol/dispatch.h"
#include "hornetlib/protocol/framer.h"
#include "hornetlib/protocol/message.h"
#include "hornetlib/protocol/message_factory.h"
#include "hornetlib/protocol/message/version.h"

#include <gtest/gtest.h>

namespace hornet::net {
namespace {

void SwapVersionMessages(Network network) {
  // Launch bitcoind on regtest
  Bitcoind node = Bitcoind::ConnectOrLaunch(network);

  // Try connecting to it
  Socket sock = Socket::Connect(kLocalhost, node.GetPort());

  // Send a version message
  sock.Write(protocol::FrameMessage(node.GetMagic(), protocol::message::Version{}));

  // Receive a version message
  const auto msgin = ReceiveMessage<protocol::message::Version>(sock, node.GetMagic());
  EXPECT_TRUE(msgin->GetName() == "version");
}

TEST(BitcoindTest, SwapVersionMessagesMainnet) {
  SwapVersionMessages(Network::Mainnet);
}

}  // namespace
}  // namespace hornet::net
