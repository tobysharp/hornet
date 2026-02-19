#include "hornetlib/data/utxo/memory_run.h"

#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

#include "hornetlib/data/utxo/types.h"

namespace hornet::data::utxo {

inline OutputKV Create(uint8_t hash, uint64_t rid, int height) {
  return { { {hash}, 0 }, { height, OutputKV::Add }, rid };
}

TEST(MemoryRunTest, TestCreate) {
  constexpr int height = 1;

  TiledVector<OutputKV> entries;
  entries.PushBack(Create(0x42, 1, height));
  entries.PushBack(Create(0x43, 2, height));
  entries.PushBack(OutputKV::Spent({{0x43}}, height));
  entries.PushBack(Create(0xaf, 3, height));

  const MemoryRun run{true, std::move(entries), {height, height + 1}};

  EXPECT_FALSE(run.Empty());
  EXPECT_EQ(run.Size(), 4);
  EXPECT_TRUE(run.IsMutable());
  EXPECT_TRUE(run.ContainsHeight(height));
}

TEST(MemoryRunTest, TestSameHeightDeletePrecedesAdd) {
  constexpr int height = 7;
  const OutputKey key{{0x42}, 0u};

  TiledVector<OutputKV> entries;
  entries.PushBack(OutputKV::Funded(key, height, 123));
  entries.PushBack(OutputKV::Spent(key, height));
  std::sort(entries.begin(), entries.end());

  ASSERT_EQ(entries.Size(), 2);
  EXPECT_TRUE(entries[0].IsDelete());
  EXPECT_TRUE(entries[1].IsAdd());

  const MemoryRun run{true, std::move(entries), {height, height + 1}};
  std::vector<OutputKey> keys{key};
  std::vector<OutputId> rids(1, kNullOutputId);
  const auto result = run.Query(keys, rids, 0, height + 1);

  EXPECT_EQ(result.funded, 0);
  EXPECT_EQ(result.spent, 1);
  EXPECT_EQ(rids[0], kSpentOutputId);
}

}  // namespace hornet::data::utxo
