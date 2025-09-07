// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include <memory>

#include <gtest/gtest.h>

#include "hornetlib/consensus/merkle.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/util/hex.h"
#include "hornetnodelib/net/constants.h"
#include "testutil/net/bitcoind_peer.h"
#include "testutil/protocol_session.h"

namespace hornet::node {
namespace {

TEST(ValidatorTest, ValidateSegwitMerkleRoot) {
  test::ProtocolSession session;
  auto node = test::BitcoindPeer::Connect();
  auto peer = session.Loop().AddOutboundPeer(hornet::node::net::kLocalhost, node.GetPort());

  // Request the first Segwit (BIP141) block, height 481,824.
  const protocol::Hash hash = "0000000000000000001c8018d9cb3b742ef25114f27563e3fc4a1902167f9893"_hash;
  const auto block = session.DownloadBlock(peer, hash);

  EXPECT_TRUE(block != nullptr);
  EXPECT_EQ(block->Header().ComputeHash(), hash);

  const auto merkle_root = consensus::ComputeMerkleRoot(*block);
  EXPECT_TRUE(merkle_root.unique);
  EXPECT_EQ(block->Header().GetMerkleRoot(), merkle_root.hash);
}

}  // namespace
}  // namespacae hornet::node
