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

TEST(MemoryRunTest, TestHigherHeightAddWinsForSameKey) {
  const OutputKey key{{0x42}, 0u};

  TiledVector<OutputKV> entries;
  entries.PushBack(OutputKV::Funded(key, 5, 111));
  entries.PushBack(OutputKV::Funded(key, 7, 222));
  std::sort(entries.begin(), entries.end());

  ASSERT_EQ(entries.Size(), 2);
  EXPECT_EQ(entries[0].rid, 222);
  EXPECT_EQ(entries[0].Height(), 7);
  EXPECT_EQ(entries[1].rid, 111);
  EXPECT_EQ(entries[1].Height(), 5);

  const MemoryRun run{true, std::move(entries), {5, 8}};
  std::vector<OutputKey> keys{key};
  std::vector<OutputId> rids(1, kNullOutputId);
  const auto result = run.Query(keys, rids, 0, 8);

  EXPECT_EQ(result.funded, 1);
  EXPECT_EQ(result.spent, 0);
  EXPECT_EQ(rids[0], 222);
}

TEST(MemoryRunTest, TestSameKeyAtOrAfterBeforeIsIgnored) {
  const OutputKey key{{0x42}, 0u};

  TiledVector<OutputKV> entries;
  entries.PushBack(OutputKV::Funded(key, 10, 111));
  entries.PushBack(OutputKV::Funded(key, 12, 222));
  std::sort(entries.begin(), entries.end());

  const MemoryRun run{true, std::move(entries), {10, 13}};
  std::vector<OutputKey> keys{key};
  std::vector<OutputId> rids(1, kNullOutputId);
  const auto result = run.Query(keys, rids, 0, 10);

  EXPECT_EQ(result.funded, 0);
  EXPECT_EQ(result.spent, 0);
  EXPECT_EQ(rids[0], kNullOutputId);
}

TEST(MemoryRunTest, TestSameKeyFallsBackToHighestHeightBelowBefore) {
  const OutputKey key{{0x42}, 0u};

  TiledVector<OutputKV> entries;
  entries.PushBack(OutputKV::Funded(key, 5, 111));
  entries.PushBack(OutputKV::Funded(key, 10, 222));
  std::sort(entries.begin(), entries.end());

  const MemoryRun run{true, std::move(entries), {5, 11}};
  std::vector<OutputKey> keys{key};
  std::vector<OutputId> rids(1, kNullOutputId);
  const auto result = run.Query(keys, rids, 0, 10);

  EXPECT_EQ(result.funded, 1);
  EXPECT_EQ(result.spent, 0);
  EXPECT_EQ(rids[0], 111);
}

// Pre-BIP30, duplicate txids could recreate the same outpoint while an older version still existed.
// The query must report the newest event for that key within the requested height window.
TEST(MemoryRunTest, TestPreBIP30_F1F2S1_IsSpent) {
  const OutputKey key{{0x42}, 0u};

  TiledVector<OutputKV> entries;
  entries.PushBack(OutputKV::Funded(key, 5, 111));
  entries.PushBack(OutputKV::Funded(key, 7, 222));
  entries.PushBack(OutputKV::Spent(key, 9));
  std::sort(entries.begin(), entries.end());

  const MemoryRun run{true, std::move(entries), {5, 10}};
  std::vector<OutputKey> keys{key};
  std::vector<OutputId> rids(1, kNullOutputId);
  const auto result = run.Query(keys, rids, 0, 10);

  EXPECT_EQ(result.funded, 0);
  EXPECT_EQ(result.spent, 1);
  EXPECT_EQ(rids[0], kSpentOutputId);
}

// After the old outpoint is spent, a later duplicate funding recreates the same key as unspent.
TEST(MemoryRunTest, TestPreBIP30_F1S1F2_IsFunded) {
  const OutputKey key{{0x42}, 0u};

  TiledVector<OutputKV> entries;
  entries.PushBack(OutputKV::Funded(key, 5, 111));
  entries.PushBack(OutputKV::Spent(key, 6));
  entries.PushBack(OutputKV::Funded(key, 7, 222));
  std::sort(entries.begin(), entries.end());

  const MemoryRun run{true, std::move(entries), {5, 8}};
  std::vector<OutputKey> keys{key};
  std::vector<OutputId> rids(1, kNullOutputId);
  const auto result = run.Query(keys, rids, 0, 8);

  EXPECT_EQ(result.funded, 1);
  EXPECT_EQ(result.spent, 0);
  EXPECT_EQ(rids[0], 222);
}

// A second spend of the recreated outpoint should leave the key spent again.
TEST(MemoryRunTest, TestPreBIP30_F1S1F2S2_IsSpent) {
  const OutputKey key{{0x42}, 0u};

  TiledVector<OutputKV> entries;
  entries.PushBack(OutputKV::Funded(key, 5, 111));
  entries.PushBack(OutputKV::Spent(key, 6));
  entries.PushBack(OutputKV::Funded(key, 7, 222));
  entries.PushBack(OutputKV::Spent(key, 8));
  std::sort(entries.begin(), entries.end());

  const MemoryRun run{true, std::move(entries), {5, 9}};
  std::vector<OutputKey> keys{key};
  std::vector<OutputId> rids(1, kNullOutputId);
  const auto result = run.Query(keys, rids, 0, 9);

  EXPECT_EQ(result.funded, 0);
  EXPECT_EQ(result.spent, 1);
  EXPECT_EQ(rids[0], kSpentOutputId);
}

}  // namespace hornet::data::utxo
