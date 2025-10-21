#include "hornetlib/data/utxo/tiled_vector.h"

#include <gtest/gtest.h>

#include "hornetlib/protocol/transaction.h"
#include "hornetlib/data/utxo/types.h"

namespace hornet::data::utxo {

TEST(TiledVectorTest, TestPushBack) {
  constexpr int kTileBits = 10;

  const OutputKV kv = { protocol::OutPoint::Null(), { 0, OutputKV::Delete }, 1 };
  TiledVector<OutputKV> entries(kTileBits);
  entries.PushBack(kv);

  EXPECT_EQ(entries.Size(), 1);
  EXPECT_EQ(entries[0], kv);
  EXPECT_EQ(*entries.begin(), kv);
}

}  // namespace hornet::data::utxo
