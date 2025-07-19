#include "hornetlib/data/chain_tree.h"

#include "hornetlib/data/hashed_tree.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/util/assert.h"
#include "hornetlib/util/throw.h"

#include <gtest/gtest.h>

namespace hornet::data {
namespace {

class ChainTreeTest : public ::testing::Test {
 protected:
  using TestChainTree = ChainTree<int>;
  using Context = TestChainTree::Context;
  using Iterator = TestChainTree::Iterator;

  TestChainTree tree_;

  // Helper to create a protocol::Hash for testing.
  protocol::Hash CreateHash(uint64_t val) {
    protocol::Hash hash{}; // Zero-initialize
    // Place the value in the first 8 bytes of the hash array.
    std::memcpy(hash.data(), &val, sizeof(val));
    return hash;
  }

  // Helper to create a context.
  Context CreateContext(int data, int height, uint64_t hash_val) {
    return Context{data, CreateHash(hash_val), height};
  }
};

// Test case for an empty ChainTree.
TEST_F(ChainTreeTest, InitialState) {
  EXPECT_TRUE(tree_.Empty());
  EXPECT_EQ(tree_.ChainLength(), 0);
  EXPECT_EQ(tree_.ChainTipHeight(), -1);
  auto tip = tree_.ChainTip();
  EXPECT_FALSE(tip.first.IsValid());
  EXPECT_FALSE(tip.second.has_value());
}

// Test case for adding the first element to the chain.
TEST_F(ChainTreeTest, AddFirstElement) {
  auto context = CreateContext(100, 0, 1);
  auto it = tree_.Add({}, context);

  EXPECT_TRUE(it.IsValid());
  EXPECT_EQ(it.GetHeight(), 0);
  EXPECT_EQ(*it, 100);

  EXPECT_FALSE(tree_.Empty());
  EXPECT_EQ(tree_.ChainLength(), 1);
  EXPECT_EQ(tree_.ChainTipHeight(), 0);
  EXPECT_EQ(tree_.ChainElement(0), 100);

  auto tip = tree_.ChainTip();
  EXPECT_TRUE(tip.first.IsValid());
  EXPECT_EQ(tip.first.GetHeight(), 0);
  EXPECT_TRUE(tip.second.has_value());
  EXPECT_EQ(tip.second->data, 100);
  EXPECT_EQ(tip.second->hash, CreateHash(1));
}

// Test case for extending the main chain.
TEST_F(ChainTreeTest, ExtendChain) {
  tree_.Add({}, CreateContext(100, 0, 1));
  auto parent_it = tree_.ChainTip().first;
  auto context = CreateContext(101, 1, 2);
  auto it = tree_.Add(parent_it, context);

  EXPECT_TRUE(it.IsValid());
  EXPECT_EQ(it.GetHeight(), 1);
  EXPECT_EQ(*it, 101);

  EXPECT_EQ(tree_.ChainLength(), 2);
  EXPECT_EQ(tree_.ChainTipHeight(), 1);
  EXPECT_EQ(tree_.ChainElement(1), 101);
}

// Test case for creating a fork from the main chain.
TEST_F(ChainTreeTest, CreateFork) {
  tree_.Add({}, CreateContext(100, 0, 1));
  tree_.Add(tree_.ChainTip().first, CreateContext(101, 1, 2));

  auto fork_parent_it = tree_.Find(0);
  auto context = CreateContext(200, 1, 3);
  auto it = tree_.Add(fork_parent_it, context);

  EXPECT_TRUE(it.IsValid());
  EXPECT_EQ(it.GetHeight(), 1);
  EXPECT_EQ(*it, 200);

  EXPECT_EQ(tree_.ChainLength(), 2); // Chain should be unchanged
  EXPECT_EQ(tree_.ChainTipHeight(), 1);

  auto find_res = tree_.FindTipOrForks(CreateHash(3));
  EXPECT_TRUE(find_res.first.IsValid());
  EXPECT_EQ(*find_res.first, 200);
}

// Test case for finding elements by height and hash.
TEST_F(ChainTreeTest, Find) {
  tree_.Add({}, CreateContext(100, 0, 1));
  tree_.Add(tree_.ChainTip().first, CreateContext(101, 1, 2));
  auto fork_parent_it = tree_.Find(0);
  tree_.Add(fork_parent_it, CreateContext(200, 1, 3));

  // Find in chain by height
  auto it_height = tree_.Find(1);
  EXPECT_TRUE(it_height.IsValid());
  EXPECT_EQ(*it_height, 101);
  EXPECT_EQ(it_height, tree_.ChainTip().first);

  // Find in forest by hash
  auto it_hash = tree_.Find(CreateHash(3));
  EXPECT_TRUE(it_hash.IsValid());
  EXPECT_EQ(*it_hash, 200);

  // Find non-existent
  auto it_invalid_height = tree_.Find(5);
  EXPECT_FALSE(it_invalid_height.IsValid());
  auto it_invalid_hash = tree_.Find(CreateHash(99));
  EXPECT_FALSE(it_invalid_hash.IsValid());
}

// Test case for the AncestorIterator.
TEST_F(ChainTreeTest, AncestorIterator) {
  tree_.Add({}, CreateContext(100, 0, 1));
  tree_.Add(tree_.ChainTip().first, CreateContext(101, 1, 2));
  tree_.Add(tree_.ChainTip().first, CreateContext(102, 2, 3));
  auto fork_parent_it = tree_.Find(1);
  auto fork_it = tree_.Add(fork_parent_it, CreateContext(201, 2, 4));

  // Iterate up the fork
  int height = 2;
  int expected_data[] = {201, 101};
  int i = 0;
  for (auto it = fork_it; it.IsValid() && it.GetHeight() > 0; ++it, ++i) {
      ASSERT_LT(i, 2);
      EXPECT_EQ(it.GetHeight(), height--);
      EXPECT_EQ(*it, expected_data[i]);
  }

  // Iterate up the main chain
  height = 2;
  int expected_chain_data[] = {102, 101, 100};
  i = 0;
  for (auto it = tree_.ChainTip().first; it.IsValid(); ++it, ++i) {
      ASSERT_LT(i, 3);
      EXPECT_EQ(it.GetHeight(), height--);
      EXPECT_EQ(*it, expected_chain_data[i]);
  }
}

// Test case for GetAncestorAtHeight.
TEST_F(ChainTreeTest, GetAncestorAtHeight) {
    tree_.Add({}, CreateContext(100, 0, 1));
    tree_.Add(tree_.ChainTip().first, CreateContext(101, 1, 2));
    auto fork_parent_it = tree_.Find(0);
    auto fork_it = tree_.Add(fork_parent_it, CreateContext(200, 1, 3));
    tree_.Add(fork_it, CreateContext(201, 2, 4));

    auto tip_it = tree_.Find(CreateHash(4));
    EXPECT_EQ(tree_.GetAncestorAtHeight(tip_it, 2), 201);
    EXPECT_EQ(tree_.GetAncestorAtHeight(tip_it, 1), 200);
    EXPECT_EQ(tree_.GetAncestorAtHeight(tip_it, 0), 100);

    auto chain_tip_it = tree_.ChainTip().first;
    EXPECT_EQ(tree_.GetAncestorAtHeight(chain_tip_it, 1), 101);
    EXPECT_EQ(tree_.GetAncestorAtHeight(chain_tip_it, 0), 100);
}

// Test case for PromoteBranch.
TEST_F(ChainTreeTest, PromoteBranch) {
   // Setup: chain 0-1-2, fork from 0: 0-201-202
    tree_.Add({}, CreateContext(100, 0, 1));
    tree_.Add(tree_.ChainTip().first, CreateContext(101, 1, 2));
    tree_.Add(tree_.ChainTip().first, CreateContext(102, 2, 3));

    auto fork_parent_it = tree_.Find(0);
    auto fork_it = tree_.Add(fork_parent_it, CreateContext(201, 1, 4));
    tree_.Add(fork_it, CreateContext(202, 2, 5));

    auto new_tip_it = tree_.Find(CreateHash(5));

    // The hashes list should only contain the hashes of the demoted part of the chain,
    // which starts *after* the fork point (height 0).
    std::vector<protocol::Hash> old_chain_hashes = {CreateHash(2), CreateHash(3)};
    std::span<const protocol::Hash> span = old_chain_hashes;
    auto promoted_tip = tree_.PromoteBranch(new_tip_it, span);

    EXPECT_TRUE(promoted_tip.IsValid());
    EXPECT_EQ(promoted_tip.GetHeight(), 2);
    EXPECT_EQ(*promoted_tip, 202);

    // Check new chain
    EXPECT_EQ(tree_.ChainLength(), 3);
    EXPECT_EQ(tree_.ChainElement(0), 100);
    EXPECT_EQ(tree_.ChainElement(1), 201);
    EXPECT_EQ(tree_.ChainElement(2), 202);

    // Check that old chain is now a fork
    auto old_tip_find = tree_.FindTipOrForks(CreateHash(3));
    EXPECT_TRUE(old_tip_find.first.IsValid());
    EXPECT_EQ(*old_tip_find.first, 102);
}

// Test case for PruneForest.
TEST_F(ChainTreeTest, PruneForest) {
    tree_.Add({}, CreateContext(100, 0, 1));
    tree_.Add(tree_.ChainTip().first, CreateContext(101, 1, 2));
    tree_.Add(tree_.ChainTip().first, CreateContext(102, 2, 3));
    tree_.Add(tree_.ChainTip().first, CreateContext(103, 3, 4));
    tree_.Add(tree_.ChainTip().first, CreateContext(104, 4, 5));

    // Fork at height 1
    auto fork1_parent = tree_.Find(1);
    auto fork1 = tree_.Add(fork1_parent, CreateContext(202, 2, 6));
    tree_.Add(fork1, CreateContext(203, 3, 7));

    // Fork at height 3
    auto fork2_parent = tree_.Find(3);
    tree_.Add(fork2_parent, CreateContext(304, 4, 8));

    // Before pruning, all forks should be findable.
    EXPECT_TRUE(tree_.Find(CreateHash(7)).IsValid());
    EXPECT_TRUE(tree_.Find(CreateHash(8)).IsValid());

    tree_.PruneForest(2); // Keep forks with root_height >= 4 - 2 = 2

    // Fork at height 1's branch (root_height=2) should be kept
    EXPECT_TRUE(tree_.Find(CreateHash(7)).IsValid());
    // Fork at height 3's branch (root_height=4) should also be kept
    EXPECT_TRUE(tree_.Find(CreateHash(8)).IsValid());

    tree_.PruneForest(1); // Keep forks with root_height >= 4 - 1 = 3
    // Fork at height 1's branch (root_height=2) should be pruned
    EXPECT_FALSE(tree_.Find(CreateHash(7)).IsValid());
    // Fork at height 3's branch (root_height=4) should be kept
    EXPECT_TRUE(tree_.Find(CreateHash(8)).IsValid());
}

TEST_F(ChainTreeTest, AncestorIterator_WalksFromForkToChain) {
  tree_.Add({}, CreateContext(100, 0, 1));
  tree_.Add(tree_.ChainTip().first, CreateContext(101, 1, 2));
  // Fork from height 0, which is NOT the tip. This correctly creates a fork.
  auto fork_parent_it = tree_.Find(0);
  auto fork_it = tree_.Add(fork_parent_it, CreateContext(201, 1, 4));

  // Start at the tip of the fork.
  auto it = fork_it;
  EXPECT_TRUE(it.IsValid());
  EXPECT_EQ(it.GetHeight(), 1);
  EXPECT_EQ(*it, 201);

  // Go to the parent, which is the root of the fork (on the main chain).
  ++it;
  EXPECT_TRUE(it.IsValid()); // This would fail with the buggy operator++.
  EXPECT_EQ(it.GetHeight(), 0);
  EXPECT_EQ(*it, 100);

  // Go past the genesis block.
  ++it;
  EXPECT_FALSE(it.IsValid());
}

}  // namespace 
}  // namespace hornet::data
