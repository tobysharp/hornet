#include "hornetlib/data/header_timechain.h"

#include <cstdint>

#include <gtest/gtest.h>

#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/work.h"
#include "hornetlib/util/big_uint.h"
#include "hornetlib/util/hex.h"

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
  auto genesis_it = tc.Add(genesis).it;
  EXPECT_EQ(tc.ChainTipHeight(), 0);
  EXPECT_EQ(tc.ChainLength(), 1);

  auto child = MakeChild(genesis, 2, 1);
  auto tip = tc.Add(genesis_it, child).it;
  EXPECT_TRUE(tip);
  EXPECT_EQ(tip->height, 1);
  EXPECT_EQ(tc.ChainLength(), 2);
}

TEST(HeaderTimechainTest, BranchWithoutReorg) {
  HeaderTimechain tc{};
  auto genesis = MakeGenesis(1, 1);
  auto it0 = tc.Add(genesis).it;
  auto h1 = MakeChild(genesis, 2, 1);
  auto it1 = tc.Add(it0, h1).it;
  auto h2 = MakeChild(h1, 3, 1);
  auto tip = tc.Add(it1, h2).it;
  ASSERT_TRUE(tip);
  EXPECT_EQ(tip->height, 2);
  EXPECT_EQ(tc.ChainLength(), 3);

  auto branch1 = MakeChild(genesis, 10, 1);
  auto branch_it = tc.Add(it0, branch1).it;
  EXPECT_TRUE(branch_it);
  EXPECT_EQ(tc.ChainTipHeight(), 2);
  EXPECT_EQ(tc.ChainLength(), 3);
}

TEST(HeaderTimechainTest, BranchTriggersReorgOnMoreWork) {
  HeaderTimechain tc{};
  auto genesis = MakeGenesis(1, 1);
  auto it0 = tc.Add(genesis).it;
  auto h1 = MakeChild(genesis, 2, 1);
  auto it1 = tc.Add(it0, h1).it;
  auto h2 = MakeChild(h1, 3, 1);
  [[maybe_unused]] auto it2 = tc.Add(it1, h2).it;

  auto heavy_branch = MakeChild(genesis, 20, 5);
  auto tip = tc.Add(it0, heavy_branch).it;
  EXPECT_TRUE(tip);
  EXPECT_EQ(tc.ChainTipHeight(), 1);
  EXPECT_EQ(tc.ChainLength(), 2);
  EXPECT_EQ(tc.ChainTip()->total_work, Uint256{6u});

  auto h2_find = tc.FindTipOrForks(h2.hash);
  EXPECT_TRUE(h2_find);
  EXPECT_EQ(h2_find->height, 2);
}

TEST(HeaderTimechainTest, ValidationViewProvidesTimestamps) {
  HeaderTimechain tc{};
  auto genesis = MakeGenesis(1, 1);
  auto it0 = tc.Add(genesis).it;
  auto h1 = MakeChild(genesis, 2, 1, 1);
  auto it1 = tc.Add(it0, h1).it;
  auto h2 = MakeChild(h1, 3, 1, 2);
  auto tip = tc.Add(it1, h2).it;

  auto view = tc.GetValidationView(tip);
  EXPECT_EQ(view->TimestampAt(1), 1u);

  const auto stamps = view->LastNTimestamps(2);
  ASSERT_EQ(stamps.size(), 2u);
  EXPECT_EQ(stamps[0], 1u);
  EXPECT_EQ(stamps[1], 2u);
}

TEST(HeaderTimechainTest, PreventsHeaderMutation) {
  HeaderTimechain timechain;

  // Construct a fake genesis header
  BlockHeader header;
  header.SetVersion(1);
  header.SetTimestamp(1234567890);
  header.SetNonce(42);

  HeaderContext context;
  context.height = 0;
  context.total_work = Uint256{100};
  context.data = header;
  context.hash = "0000000000000000000000000000000000000000000000000000000000000001"_hash;

  // Add header to the timechain
  HeaderTimechain::Iterator it = timechain.Add(context).it;

  // Attempt to mutate the header through the iterator — should fail to compile
  // Uncommenting the next line should result in a compiler error
  // it->SetNonce(99);

  // Can read the data just fine
  EXPECT_EQ(it->data.GetNonce(), 42);

  // Optional: enforce immutability via static_assert if using Immutable<T>
  // static_assert(std::is_const_v<std::remove_reference_t<decltype(*it)>>, "Header must be immutable");
}

}  // namespace
}  // namespace hornet::data
