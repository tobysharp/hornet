#include "hornetlib/data/utxo/memory_run.h"

#include <algorithm>
#include <memory>
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

// Core treats a BIP30-exempt duplicate coinbase (blocks 91842 and 91880) as overwriting the earlier coin, leaving one
// coin at the newer height. Hornet appends the repeat as a new Add and retires the superseded output with a Delete at
// the height below, so the key carries Add@h, Delete@h-1, Add@h0. Lookups resolve to the newest entry, compaction
// annihilates the superseded pair, and a later spend leaves nothing funded, matching Core at every step.

MemoryRun MakeSupersededAddsRun(const OutputKey& key, bool is_mutable) {
  TiledVector<OutputKV> entries;
  entries.PushBack(OutputKV::Funded(key, 91812, 1));
  entries.PushBack(OutputKV::Spent(key, 91841));
  entries.PushBack(OutputKV::Funded(key, 91842, 2));
  std::sort(entries.begin(), entries.end());
  return MemoryRun{is_mutable, std::move(entries), {91812, 91843}};
}

TEST(MemoryRunTest, SupersededAddsResolveToNewest) {
  const OutputKey key{{0x42}, 0u};
  const MemoryRun run = MakeSupersededAddsRun(key, true);

  std::vector<OutputKey> keys{key};
  std::vector<OutputId> rids(1, kNullOutputId);
  const auto result = run.Query(keys, rids, 0, 91843);

  EXPECT_EQ(result.funded, 1);
  EXPECT_EQ(rids[0], OutputId{2});
}

TEST(MemoryRunTest, ImmutableMergeAnnihilatesSupersededAdd) {
  const OutputKey key{{0x42}, 0u};
  const std::vector<std::shared_ptr<const MemoryRun>> inputs{
      std::make_shared<const MemoryRun>(MakeSupersededAddsRun(key, false))};
  const MemoryRun merged = MemoryRun::Merge(false, inputs);

  EXPECT_EQ(merged.Size(), 1u);

  std::vector<OutputKey> keys{key};
  std::vector<OutputId> rids(1, kNullOutputId);
  const auto result = merged.Query(keys, rids, 0, 91843);

  EXPECT_EQ(result.funded, 1);
  EXPECT_EQ(rids[0], OutputId{2});
}

TEST(MemoryRunTest, ImmutableMergeOfSpendAfterSupersessionLeavesNothingFunded) {
  const OutputKey key{{0x42}, 0u};
  TiledVector<OutputKV> spend;
  spend.PushBack(OutputKV::Spent(key, 500'000));

  const std::vector<std::shared_ptr<const MemoryRun>> inputs{
      std::make_shared<const MemoryRun>(false, std::move(spend), std::pair{500'000, 500'001}),
      std::make_shared<const MemoryRun>(MakeSupersededAddsRun(key, false))};
  const MemoryRun merged = MemoryRun::Merge(false, inputs);

  EXPECT_TRUE(merged.Empty());

  std::vector<OutputKey> keys{key};
  std::vector<OutputId> rids(1, kNullOutputId);
  const auto result = merged.Query(keys, rids, 0, 500'001);

  EXPECT_EQ(result.funded, 0);
  EXPECT_FALSE(IsOutputIdValid(rids[0]));
}

}  // namespace hornet::data::utxo
