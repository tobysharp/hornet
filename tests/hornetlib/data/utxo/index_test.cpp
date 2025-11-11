#include "hornetlib/data/utxo/index.h"

#include <random>

#include <gtest/gtest.h>

#include "hornetlib/data/utxo/codec.h"

namespace hornet::data::utxo {
namespace {

static OutputKV RandomAddKV(int height) {
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

static TiledVector<OutputKV> MakeEntries(const Index& index, int count, int height) {
  auto entries = index.MakeAppendBuffer();
  for (int i = 0; i < count; ++i)
    entries.PushBack(RandomAddKV(height));
  return entries;
}

TEST(IndexTest, TestAppend) {
  constexpr int kHeights = 16;
  constexpr int kEntriesPerRun = 5000;

  Index index;
  TiledVector<OutputKV> first;
  for (int i = 0; i < kHeights; i++) {
    auto entries = MakeEntries(index, kEntriesPerRun, i);
    index.SortEntries(&entries);
    if (i == 0) first = entries;

    EXPECT_TRUE(std::is_sorted(entries.begin(), entries.end()));
    index.Append(std::move(entries), i);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  std::vector<OutputKey> keys(first.Size());
  std::vector<OutputId> rids(first.Size());
  std::transform(first.begin(), first.end(), keys.begin(), [](const OutputKV& kv) { return kv.key; });
  EXPECT_TRUE(std::is_sorted(keys.begin(), keys.end()));

  const auto result = index.Query(keys, rids, 0, kHeights);

  EXPECT_EQ(result.funded, std::ssize(keys));
  EXPECT_EQ(result.spent, 0);
}

TEST(IndexTest, TestQuery) {
  constexpr int kBytesPerRow = 40;
  Index index;
  // Block 0
  {
    TiledVector<OutputKV> entries;
    entries.PushBack(OutputKV::Unspent(OutputKey{{0x01}, 0u}, 0, 0));
    index.Append(std::move(entries), 0);
  }
  // Block 1
  {
    TiledVector<OutputKV> entries;
    entries.PushBack(OutputKV::Tombstone(OutputKey{{0x01}, 0u}, 1));
    entries.PushBack(OutputKV::Unspent(OutputKey{{0x02}, 0u}, 1, IdCodec::Encode(1 * kBytesPerRow, kBytesPerRow)));
    entries.PushBack(OutputKV::Unspent(OutputKey{{0x02}, 1u}, 1, IdCodec::Encode(2 * kBytesPerRow, kBytesPerRow)));
    entries.PushBack(OutputKV::Unspent(OutputKey{{0x02}, 2u}, 1, IdCodec::Encode(3 * kBytesPerRow, kBytesPerRow)));
    entries.PushBack(OutputKV::Unspent(OutputKey{{0x02}, 3u}, 1, IdCodec::Encode(4 * kBytesPerRow, kBytesPerRow)));
    index.Append(std::move(entries), 0);
  }

  std::vector<OutputKey> keys(4);
  for (int i = 0; i < 4; ++i)
    keys[i] = {{0x02}, static_cast<uint32_t>(i)};
  std::vector<OutputId> rids(keys.size());
  auto query = index.Query(keys, rids, 0, 2);

  EXPECT_EQ(query.funded, std::ssize(keys));
  EXPECT_EQ(query.spent, 0);
}

}  // namespace
}  // namespace hornet::data::utxo
