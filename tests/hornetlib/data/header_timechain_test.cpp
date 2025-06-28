#include "hornetlib/data/header_timechain.h"

#include <cstdint>

#include <gtest/gtest.h>

#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/work.h"
#include "hornetlib/util/big_uint.h"


namespace hornet::data {
namespace {

using protocol::BlockHeader;
using protocol::Hash;
using protocol::Work;

HeaderContext MakeGenesis(uint32_t nonce, uint64_t work_val = 1) {
  BlockHeader h{};
  h.SetVersion(1);
  h.SetNonce(nonce);
  const Hash hash = h.ComputeHash();
  Work work{Uint256{work_val}};
  return HeaderContext{h, hash, work, work, 0};
}

HeaderContext MakeChild(const HeaderContext& parent, uint32_t nonce, uint64_t work_val = 1,
                        uint32_t timestamp = 0) {
  BlockHeader h{};
  h.SetVersion(1);
  h.SetPreviousBlockHash(parent.hash);
  h.SetNonce(nonce);
  h.SetTimestamp(timestamp);
  const Hash hash = h.ComputeHash();
  Work work{Uint256{work_val}};
  return HeaderContext{h, hash, work, parent.total_work + work, parent.height + 1};
}

TEST(HeaderTimechainTest, AddExtendsChain) {
  HeaderTimechain tc{};
  auto genesis = MakeGenesis(1, 1);
  auto genesis_it = tc.Add(genesis);
  EXPECT_EQ(tc.GetHeaviestTipHeight(), 0);
  EXPECT_EQ(tc.GetHeaviestLength(), 1);

  auto child = MakeChild(genesis, 2, 1);
  auto tip = tc.Add(child, genesis_it);
  EXPECT_TRUE(tip.IsValid());
  EXPECT_EQ(tip.GetHeight(), 1);
  EXPECT_EQ(tc.GetHeaviestLength(), 2);
}

TEST(HeaderTimechainTest, BranchWithoutReorg) {
  HeaderTimechain tc{};
  auto genesis = MakeGenesis(1, 1);
  auto it0 = tc.Add(genesis);
  auto h1 = MakeChild(genesis, 2, 1);
  auto it1 = tc.Add(h1, it0);
  auto h2 = MakeChild(h1, 3, 1);
  auto tip = tc.Add(h2, it1);
  ASSERT_TRUE(tip.IsValid());
  EXPECT_EQ(tip.GetHeight(), 2);
  EXPECT_EQ(tc.GetHeaviestLength(), 3);

  auto branch1 = MakeChild(genesis, 10, 1);
  auto branch_it = tc.Add(branch1, it0);
  EXPECT_TRUE(branch_it.IsValid());
  EXPECT_EQ(tc.GetHeaviestTipHeight(), 2);
  EXPECT_EQ(tc.GetHeaviestLength(), 3);
}

TEST(HeaderTimechainTest, BranchTriggersReorgOnMoreWork) {
  HeaderTimechain tc{};
  auto genesis = MakeGenesis(1, 1);
  auto it0 = tc.Add(genesis);
  auto h1 = MakeChild(genesis, 2, 1);
  auto it1 = tc.Add(h1, it0);
  auto h2 = MakeChild(h1, 3, 1);
  tc.Add(h2, it1);

  auto heavy_branch = MakeChild(genesis, 20, 5);
  auto tip = tc.Add(heavy_branch, it0);
  EXPECT_TRUE(tip.IsValid());
  EXPECT_EQ(tc.GetHeaviestTipHeight(), 1);
  EXPECT_EQ(tc.GetHeaviestLength(), 2);
  EXPECT_EQ(tc.HeaviestTip().second->total_work, Uint256{6u});

  auto h2_find = tc.Find(h2.hash);
  EXPECT_TRUE(h2_find.first.IsValid());
  ASSERT_TRUE(h2_find.second.has_value());
  EXPECT_EQ(h2_find.second->height, 2);
}

TEST(HeaderTimechainTest, ValidationViewProvidesTimestamps) {
  HeaderTimechain tc{};
  auto genesis = MakeGenesis(1, 1);
  auto it0 = tc.Add(genesis);
  auto h1 = MakeChild(genesis, 2, 1, 1);
  auto it1 = tc.Add(h1, it0);
  auto h2 = MakeChild(h1, 3, 1, 2);
  auto tip = tc.Add(h2, it1);

  auto view = tc.GetValidationView(tip);
  EXPECT_EQ(view->TimestampAt(1), 1u);

  const auto stamps = view->LastNTimestamps(2);
  ASSERT_EQ(stamps.size(), 1u);
  EXPECT_EQ(stamps[0], 2u);
}

}  // namespace
}  // namespace hornet::data
