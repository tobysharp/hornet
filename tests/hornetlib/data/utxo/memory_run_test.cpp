#include "hornetlib/data/utxo/memory_run.h"

#include <vector>

#include <gtest/gtest.h>

#include "hornetlib/data/utxo/types.h"

namespace hornet::data::utxo {

inline OutputKV Create(uint8_t hash, uint64_t rid, int height) {
  return { { {hash}, 0 }, { height, OutputKV::Add }, rid };
}

TEST(MemoryRunTest, TestCreate) {
  const MemoryRun::Options options {
    .prefix_bits = 8,
    .tile_bits = 8,
    .is_mutable = true
  };
  constexpr int height = 1;

  TiledVector<OutputKV> entries;
  entries.PushBack(Create(0x42, 1, height));
  entries.PushBack(Create(0x43, 2, height));
  entries.PushBack(OutputKV::Tombstone({{0x43}}, height));
  entries.PushBack(Create(0xaf, 3, height));

  const MemoryRun run = MemoryRun::Create(options, std::move(entries), height);

  EXPECT_FALSE(run.Empty());
  EXPECT_EQ(run.Size(), 4);
  EXPECT_TRUE(run.IsMutable());
  EXPECT_TRUE(run.ContainsHeight(height));
}

}  // namespace hornet::data::utxo
