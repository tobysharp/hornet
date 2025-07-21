// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/protocol/block.h"

#include <gtest/gtest.h>

#include "hornetlib/protocol/constants.h"

namespace hornet::protocol {

TEST(BlockTest, GetGenesis) {
  const auto& genesis = Block::Genesis();
  EXPECT_EQ(genesis.Header().ComputeHash(), kGenesisHash);
}

}  // namespacae hornet::protocol
