#include "hornetlib/protocol/block.h"

#include <gtest/gtest.h>

#include "hornetlib/protocol/constants.h"

namespace hornet::protocol {

TEST(BlockTest, GetGenesis) {
  const auto& genesis = Block::Genesis();
  EXPECT_EQ(genesis.Header().ComputeHash(), kGenesisHash);
}

}  // namespacae hornet::protocol
