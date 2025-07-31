// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "testutil/net/bitcoind_peer.h"

#include "hornetlib/protocol/dispatch.h"
#include "hornetlib/protocol/framer.h"
#include "hornetlib/protocol/message.h"
#include "hornetlib/protocol/message_factory.h"
#include "hornetlib/protocol/message/version.h"
#include "hornetnodelib/net/constants.h"
#include "hornetnodelib/net/receive.h"
#include "hornetnodelib/net/socket.h"

#include <gtest/gtest.h>

namespace hornet::test {
namespace {

using node::net::Network;
using node::net::Socket;

void SwapVersionMessages(Network network) {
  // Launch bitcoind on regtest
  auto node = test::BitcoindPeer::ConnectOrLaunch(network);

  // Try connecting to it
  Socket sock = Socket::Connect(node::net::kLocalhost, node.GetPort());

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
}  // namespace hornet::test
