#include "hornetlib/data/utxo/tiled_vector.h"

#include <gtest/gtest.h>

#include "hornetlib/protocol/transaction.h"
#include "hornetlib/data/utxo/types.h"

namespace hornet::data::utxo {

TEST(TiledVectorTest, TestPushBack) {
  constexpr int kTileBits = 4;
  constexpr int kEntries = (1 << (kTileBits + 1)) + 1;
  OutputKV kv = { protocol::OutPoint::Null(), { 0, OutputKV::Delete }, 0 };
  TiledVector<OutputKV> entries(kTileBits);
  for (int i = 0; i < kEntries; ++i, ++kv.rid)
    entries.PushBack(kv);

  EXPECT_EQ(entries.Size(), kEntries);
  for (int i = 0; i < kEntries; ++i)
  {  
    EXPECT_EQ(entries[i].rid, i);
    EXPECT_EQ(entries.begin()[i].rid, i);
  }
}

TEST(TiledVectorTest, TestIterators)
{
  const OutputKV kv = { protocol::OutPoint::Null(), { 0, OutputKV::Delete }, 1 };
  TiledVector<OutputKV> entries;
  entries.PushBack(kv);
  entries.PushBack(kv);

  {
    int count = 0;
    for (auto it = entries.begin(); it != entries.end(); ++it)
      ++count;
    EXPECT_EQ(count, 2);
  }
  {  
    int count = entries.end() - entries.begin();
    EXPECT_EQ(count, 2);
  }
}

}  // namespace hornet::data::utxo
