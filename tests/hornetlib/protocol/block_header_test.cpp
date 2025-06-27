// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/protocol/block_header.h"

#include "hornetlib/crypto/hash.h"
#include "hornetlib/util/big_uint.h"

#include <gtest/gtest.h>

namespace hornet::protocol {
namespace {

TEST(BlockHeader, GenesisBlockHashMatches) {
  // Genesis block header fields
  BlockHeader header;
  header.SetVersion(1);
  header.SetPreviousBlockHash(Hash{});  // all zeros
  header.SetMerkleRoot("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_h);
  header.SetTimestamp(1231006505);
  header.SetCompactTarget(0x1d00ffff);
  header.SetNonce(2083236893);
  EXPECT_EQ(header.ComputeHash(), kGenesisHash);
  EXPECT_FALSE(!header.IsProofOfWork());
}

}  // namespace
}  // namespace hornet::protocol