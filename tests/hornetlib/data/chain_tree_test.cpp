#include "hornetlib/data/chain_tree.h"

#include <gtest/gtest.h>

#include "hornetlib/protocol/hash.h"

namespace hornet::data {
namespace {

// A trivial fake data type for testing
struct TestData {
  int value;
  bool operator==(const TestData& other) const { return value == other.value; }
};

// Deterministically generate a hash from an int
protocol::Hash MakeHash(int value) {
  protocol::Hash h{};
  h[0] = static_cast<uint8_t>(value);
  return h;
}

using TestContext = ContextWrapper<TestData>;
using TestTree = ChainTree<TestData, TestContext>;

TestContext MakeContext(int height, int value) {
  return {.data = {value}, .hash = MakeHash(value), .height = height};
}

TEST(ChainTreeTest, AddToEmptyChain) {
  TestTree tree;
  auto ctx = MakeContext(0, 100);
  auto tip = tree.Add(tree.BeginChain(-1), ctx);
  EXPECT_TRUE(tip);
  EXPECT_EQ(*tip, TestData{100});
  EXPECT_EQ(tree.ChainLength(), 1);
}

TEST(ChainTreeTest, AddMultipleChainElements) {
  TestTree tree;
  auto tip = tree.BeginChain(-1);
  for (int i = 0; i < 5; ++i) {
    auto ctx = MakeContext(i, i);
    tip = tree.Add(tip, ctx);
    EXPECT_EQ(*tip, TestData{i});
  }
  EXPECT_EQ(tree.ChainLength(), 5);
  EXPECT_EQ(tree.ChainTip().second->data.value, 4);
}

TEST(ChainTreeTest, FindInTipOrForestReturnsTipOnly) {
  TestTree tree;
  auto tip = tree.BeginChain(-1);
  for (int i = 0; i < 3; ++i) tip = tree.Add(tip, MakeContext(i, i));
  auto [iter, ctx] = tree.FindInTipOrForest(MakeHash(2));
  ASSERT_TRUE(ctx.has_value());
  EXPECT_EQ(ctx->data.value, 2);
}

TEST(ChainTreeTest, AddForkAndFindInForest) {
  TestTree tree;
  auto tip = tree.BeginChain(-1);
  for (int i = 0; i < 3; ++i) tip = tree.Add(tip, MakeContext(i, i));

  auto fork_parent = tree.BeginChain(1);  // fork from height 1
  [[maybe_unused]] auto fork = tree.Add(fork_parent, MakeContext(2, 42));

  auto [found, ctx] = tree.FindInTipOrForest(MakeHash(42));
  EXPECT_TRUE(ctx.has_value());
  EXPECT_EQ(ctx->data.value, 42);
}

TEST(ChainTreeTest, PromoteBranchPromotesFork) {
  TestTree tree;
  auto tip = tree.BeginChain(-1);
  std::vector<TestContext> chain;
  for (int i = 0; i < 4; ++i) {
    chain.push_back(MakeContext(i, i));
    tip = tree.Add(tip, chain.back());
  }
  // Fork from height 2
  auto fork_parent = tree.BeginChain(2);
  auto alt_ctx1 = MakeContext(3, 99);
  auto alt_tip = tree.Add(fork_parent, alt_ctx1);

  // Reorg to promote alt_tip
  auto hashes = {MakeHash(3)};
  tree.PromoteBranch(alt_tip, hashes);

  auto [new_tip, ctx] = tree.ChainTip();
  ASSERT_TRUE(ctx.has_value());
  EXPECT_EQ(ctx->data.value, 99);
}

TEST(ChainTreeTest, GetAncestorAtHeightReturnsCorrectNode) {
  TestTree tree;
  auto tip = tree.BeginChain(-1);
  for (int i = 0; i < 5; ++i) tip = tree.Add(tip, MakeContext(i, 10 + i));
  for (int i = 0; i < 5; ++i)
    EXPECT_EQ(tree.GetAncestorAtHeight(tip, i), TestData{10 + i});
}

TEST(ChainTreeTest, AncestorIteratorTraversesChain) {
  TestTree tree;
  auto tip = tree.BeginChain(-1);
  for (int i = 0; i < 3; ++i) tip = tree.Add(tip, MakeContext(i, i));

  std::vector<int> seen;
  for (auto it = tip; it.GetHeight() >= 0; ++it)
    seen.push_back((*it).value);

  EXPECT_EQ(seen, std::vector<int>({2, 1, 0}));
}

TEST(ChainTreeTest, PruneReorgTreeRemovesOldForks) {
  TestTree tree;
  auto tip = tree.BeginChain(-1);
  for (int i = 0; i < 5; ++i) tip = tree.Add(tip, MakeContext(i, i));

  [[maybe_unused]] auto fork = tree.Add(tree.BeginChain(1), MakeContext(2, 99));
  tree.PruneForest(1);  // Only keep forks newer than height 4

  auto [found, ctx] = tree.FindInTipOrForest(MakeHash(99));
  EXPECT_FALSE(ctx.has_value());  // Pruned
}

}  // namespace
}  // namespace hornet::data
