#include "hornetlib/data/utxo/memory_age.h"

#include <cstdint>
#include <random>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "hornetlib/data/utxo/memory_run.h"
#include "hornetlib/data/utxo/tiled_vector.h"
#include "hornetlib/data/utxo/types.h"

namespace hornet::data::utxo {
namespace {

inline OutputKV Create(uint8_t hash, uint64_t rid, int height) {
  return { { {hash}, 0 }, { height, OutputKV::Add }, rid };
}

inline OutputKV RandomAddKV(int height) {
  static std::mt19937_64 rnd;
  OutputKV kv;
  uint64_t* words = reinterpret_cast<uint64_t*>(&kv.key.hash);
  for (int i = 0; i < 4; ++i)
    words[i] = std::uniform_int_distribution<uint64_t>{}(rnd);
  kv.key.index = std::uniform_int_distribution<uint32_t>{}(rnd);
  kv.data.height = height;
  kv.data.op = OutputKV::Add;
  return kv;
}

TEST(MemoryAgeTest, TestAppendAge0Query) {
  MemoryAge age{true};
  EXPECT_TRUE(age.IsMutable());

  constexpr int height = 1;
  TiledVector<OutputKV> entries;
  entries.PushBack(Create(0x42, 1, height));
  entries.PushBack(Create(0x43, 2, height));
  entries.PushBack(OutputKV::Spent({{0x50}}, height));
  entries.PushBack(Create(0xaf, 3, height));
  entries.PushBack(OutputKV::Spent({{0xff}}, height));
  EXPECT_TRUE(std::is_sorted(entries.begin(), entries.end()));

  age.Append(std::move(entries), {height, height + 1});

  std::vector<OutputKey> keys;
  keys.push_back({ {0x43}, 0 });
  keys.push_back({ {0x50}, 0 });

  std::vector<OutputId> rids(keys.size(), kNullOutputId);
  const auto results = age.Query(keys, rids, 0, height + 1);

  EXPECT_EQ(results.funded, 1);
  EXPECT_EQ(results.spent, 1);
  EXPECT_EQ(rids[0], 2);
  EXPECT_EQ(rids[1], kSpentOutputId);
}

TEST(MemoryAgeTest, TestLaterSortedBeforeEarlier) {
  TiledVector<OutputKV> entries;
  entries.PushBack(Create(0x42, 10, 20));
  entries.PushBack(OutputKV::Spent({{0x42}}, 21));
  EXPECT_LT(entries[1], entries[0]);
  EXPECT_FALSE(std::is_sorted(entries.begin(), entries.end()));
}

TEST(MemoryAgeTest, TestAddAndDeleteInMutableAge) {
  MemoryAge age{true};  
  EXPECT_TRUE(age.IsMutable());

  TiledVector<OutputKV> entries;
  entries.PushBack(OutputKV::Spent({{0x42}}, 21));
  entries.PushBack(OutputKV::Funded({{0x42}}, 20, 10));
  EXPECT_TRUE(std::is_sorted(entries.begin(), entries.end()));

  age.Append(MemoryRun{true, std::move(entries), {20, 22}});
  EXPECT_EQ(age.Size(), 1);

  std::vector<OutputKey> keys;
  keys.push_back({{0x42}});

  std::vector<OutputId> rids(keys.size(), kNullOutputId);
  const auto results = age.Query(keys, rids, 0, 22);

  EXPECT_EQ(results.funded, 0);
  EXPECT_EQ(results.spent, 1);
  EXPECT_EQ(rids[0], kSpentOutputId);
}

TEST(MemoryAgeTest, TestMergeMutableToMutable) {
  constexpr int kEntriesPerRun = 4;
  MemoryAge age0{true, 2}, age1{true};  
  for (int height = 0; height < 2; ++height) {
    TiledVector<OutputKV> entries;
    for (int i = 0; i < kEntriesPerRun; ++i)
      entries.PushBack(RandomAddKV(height));
    std::sort(entries.begin(), entries.end());
    age0.Append(std::move(entries), {height, height + 1});
  }

  EXPECT_TRUE(age0.IsMergeReady());
  age0.Merge(&age1);

  EXPECT_TRUE(age0.Empty());
  EXPECT_EQ(age1.Size(), 1);
  EXPECT_FALSE(age1.IsMergeReady());
  EXPECT_FALSE(age0.IsMergeReady());

  const auto run = age1.RunSnapshot(0);
  EXPECT_TRUE(std::is_sorted(run->Begin(), run->End()));
  EXPECT_EQ(run->Size(), 2 * kEntriesPerRun);
  EXPECT_EQ(run->HeightRange(), std::make_pair(0, 2));
}

TEST(MemoryAgeTest, TestMergeMutableToMutableWithDeletes) {
  constexpr int kEntriesPerRun = 4;
  MemoryAge age0{true, 2}, age1{true};  
 
  TiledVector<OutputKV> entries0, entries1;
  for (int i = 0; i < kEntriesPerRun; ++i) {
    entries0.PushBack(RandomAddKV(0));
    entries1.PushBack(OutputKV::Spent(entries0[i].key, 1));
  }
  std::sort(entries0.begin(), entries0.end());
  std::sort(entries1.begin(), entries1.end());
  age0.Append(std::move(entries0), {0, 1});
  age0.Append(std::move(entries1), {1, 2});

  EXPECT_TRUE(age0.IsMergeReady());
  age0.Merge(&age1);

  EXPECT_TRUE(age0.Empty());
  EXPECT_EQ(age1.Size(), 1);
  EXPECT_FALSE(age1.IsMergeReady());
  EXPECT_FALSE(age0.IsMergeReady());

  const auto run = age1.RunSnapshot(0);
  EXPECT_TRUE(std::is_sorted(run->Begin(), run->End()));
  EXPECT_EQ(run->Size(), 2 * kEntriesPerRun);
  EXPECT_EQ(run->HeightRange(), std::make_pair(0, 2));
}

TEST(MemoryAgeTest, TestMergeMutableToImmutableWithDeletes) {
  constexpr int kEntriesPerRun = 4;
  MemoryAge age0{true, 2}, age1{false};
 
  TiledVector<OutputKV> entries0, entries1;
  for (int i = 0; i < kEntriesPerRun; ++i) {
    entries0.PushBack(RandomAddKV(0));
    entries1.PushBack(OutputKV::Spent(entries0[i].key, 1));
  }
  std::sort(entries0.begin(), entries0.end());
  std::sort(entries1.begin(), entries1.end());
  age0.Append(std::move(entries0), {0, 1});
  age0.Append(std::move(entries1), {1, 2});

  EXPECT_TRUE(age0.IsMergeReady());
  age0.Merge(&age1);

  EXPECT_TRUE(age0.Empty());
  EXPECT_EQ(age1.Size(), 1);
  EXPECT_FALSE(age1.IsMergeReady());
  EXPECT_FALSE(age0.IsMergeReady());

  const auto run = age1.RunSnapshot(0);
  EXPECT_TRUE(std::is_sorted(run->Begin(), run->End()));
  EXPECT_TRUE(run->Empty());
  EXPECT_EQ(run->HeightRange(), std::make_pair(0, 2));
}

TEST(MemoryAgeTest, TestEraseSince) {
  constexpr int kEntriesPerRun = 4;
  MemoryAge age0{true, 2};  
 
  TiledVector<OutputKV> entries;
  for (int i = 0; i < kEntriesPerRun; ++i) {
    entries.PushBack(RandomAddKV(0));
    entries.PushBack(OutputKV::Spent(entries[i].key, 1));
  }
  std::sort(entries.begin(), entries.end());
  age0.Append(std::move(entries), {0, 2});
  age0.EraseSince(1);

  EXPECT_FALSE(age0.IsMergeReady());
  EXPECT_EQ(age0.Size(), 1);

  const auto run = age0.RunSnapshot(0);
  EXPECT_TRUE(std::is_sorted(run->Begin(), run->End()));
  EXPECT_EQ(run->Size(), kEntriesPerRun);
  EXPECT_EQ(run->HeightRange(), std::make_pair(0, 1));
}

}  // namespace
}  // namespace hornet::data::utxo
