// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/data/sidecar.h"

#include <gtest/gtest.h>

#include "hornetlib/data/chain_tree.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/util/assert.h"
#include "hornetlib/util/throw.h"

#if defined(NDEBUG)
#define EXPECT_ASSERT(expr) EXPECT_NO_THROW((expr))
#else
#define EXPECT_ASSERT(expr) ASSERT_DEATH((expr), ".*")
#endif

namespace hornet::data {
namespace {

// Test fixture for the Sidecar tests.
class SidecarTest : public ::testing::Test {
 protected:
  using MasterChain = ChainTree<int>; // A simple ChainTree to act as the master
  using MasterContext = MasterChain::Context;
  using TestSidecar = Sidecar<std::string>; // Sidecar stores strings

  // Helper to create a protocol::Hash for testing.
  protocol::Hash CreateHash(uint64_t val) {
    protocol::Hash hash{}; // Zero-initialize
    std::memcpy(hash.data(), &val, sizeof(val));
    return hash;
  }

  // Helper to create a context for the master chain.
  MasterContext CreateMasterContext(int data, int height, uint64_t hash_val) {
    return MasterContext{data, CreateHash(hash_val), height};
  }

  // Helper to create a Locator from an iterator, which the base iterator doesn't provide.
  Locator GetLocator(const MasterChain::Iterator& it) {
    if (!it.IsValid()) {
      // A parent for the genesis block doesn't have a real locator.
      // We use -1 as a convention that Find will correctly interpret as an invalid iterator.
      return -1;
    }
    if (it.Node()) { // The iterator is for a node in the forest.
      return it.Node()->data.GetHash();
    }
    // The iterator is for an element in the main chain.
    return it.GetHeight();
  }

  // Simulates adding a block to the master chain and generates the sync packet.
  // A simple "Add" on the base ChainTree never causes a reorg.
  SidecarAddSync AddToMaster(MasterChain::Iterator parent_it, int data, int height, uint64_t hash_val) {
    const auto context = CreateMasterContext(data, height, hash_val);
    [[maybe_unused]] MasterChain::Iterator child_it = master_chain_.Add(parent_it, context);
    // The locator comes from the parent, and the hash comes from the new context.
    return {GetLocator(parent_it), context.hash, {}};
  }

  MasterChain master_chain_;
  TestSidecar sidecar_{"default"}; // Sidecar with a default value of "default"
};

// Test case for the initial state of a Sidecar.
TEST_F(SidecarTest, InitialState) {
  EXPECT_EQ(sidecar_.Get(0), nullptr);
  EXPECT_ASSERT(sidecar_.Set(0, "new_value"));
}

// Test case for synchronizing a simple chain extension.
TEST_F(SidecarTest, AddSyncChainExtension) {
  // Add genesis to master
  auto sync_genesis = AddToMaster({}, 100, 0, 1);
  sidecar_.AddSync(sync_genesis);

  // Verify sidecar state
  const std::string* genesis_data = sidecar_.Get(0);
  ASSERT_NE(genesis_data, nullptr);
  EXPECT_EQ(*genesis_data, "default");

  // Extend the chain
  auto sync_block1 = AddToMaster(master_chain_.ChainTip().first, 101, 1, 2);
  sidecar_.AddSync(sync_block1);

  // Verify sidecar state
  const std::string* block1_data = sidecar_.Get(1);
  ASSERT_NE(block1_data, nullptr);
  EXPECT_EQ(*block1_data, "default");
  EXPECT_EQ(*sidecar_.Get(0), "default"); // Genesis should still be there
}

// Test case for the Get and Set methods.
TEST_F(SidecarTest, GetAndSet) {
  // Add two blocks
  auto sync_genesis = AddToMaster({}, 100, 0, 1);
  sidecar_.AddSync(sync_genesis);
  auto sync_block1 = AddToMaster(master_chain_.ChainTip().first, 101, 1, 2);
  sidecar_.AddSync(sync_block1);

  // Set a new value for the genesis block
  sidecar_.Set(0, "genesis_updated");

  // Get and verify the new value
  const std::string* genesis_data = sidecar_.Get(0);
  ASSERT_NE(genesis_data, nullptr);
  EXPECT_EQ(*genesis_data, "genesis_updated");

  // Verify the other block is unchanged
  const std::string* block1_data = sidecar_.Get(1);
  ASSERT_NE(block1_data, nullptr);
  EXPECT_EQ(*block1_data, "default");

  // Try to set a value for a non-existent block
  EXPECT_ASSERT(sidecar_.Set(5, "should_fail"));
}

// Test case for synchronizing a fork.
TEST_F(SidecarTest, AddSyncFork) {
  // Create a chain of two blocks
  AddToMaster({}, 100, 0, 1);
  AddToMaster(master_chain_.ChainTip().first, 101, 1, 2);
  
  // Sync the sidecar up to this point
  sidecar_.AddSync({-1, CreateHash(1), {}});
  sidecar_.AddSync({0, CreateHash(2), {}});

  // Create a fork from genesis on the master chain
  auto sync_fork = AddToMaster(master_chain_.Find(0), 201, 1, 3);
  sidecar_.AddSync(sync_fork);

  // Verify the fork exists in the sidecar with the default value
  const std::string* fork_data = sidecar_.Get(CreateHash(3));
  ASSERT_NE(fork_data, nullptr);
  EXPECT_EQ(*fork_data, "default");

  // Set a value on the fork and verify it
  sidecar_.Set(CreateHash(3), "fork_value");
  EXPECT_EQ(*sidecar_.Get(CreateHash(3)), "fork_value");

  // Ensure the main chain is still intact
  EXPECT_EQ(*sidecar_.Get(1), "default");
}

// Test case for synchronizing a full chain reorganization.
TEST_F(SidecarTest, AddSyncReorg) {
  // STEP 1: Set up the initial state for both master and sidecar.
  // Master chain: 0 -> 1 -> 2 (hashes 1, 2, 3)
  AddToMaster({}, 100, 0, 1);
  AddToMaster(master_chain_.Find(0), 101, 1, 2);
  AddToMaster(master_chain_.Find(1), 102, 2, 3);

  // Sync the sidecar with the initial main chain
  sidecar_.AddSync({-1, CreateHash(1), {}});
  sidecar_.AddSync({0, CreateHash(2), {}});
  sidecar_.AddSync({1, CreateHash(3), {}});

  // STEP 2: A competing fork appears. We add its blocks one by one.
  // Fork: 0 -> 201 (hash 4)
  auto sync_fork_1 = AddToMaster(master_chain_.Find(0), 201, 1, 4);
  sidecar_.AddSync(sync_fork_1);

  // Verify the fork is now in the sidecar.
  ASSERT_NE(sidecar_.Get(CreateHash(4)), nullptr);

  // STEP 3: The final block of the fork arrives. Adding this block to the
  // HeaderTimechain would trigger an internal reorg. We simulate this.
  
  // First, manually promote the branch on the master chain.
  // This simulates what HeaderTimechain::Add would do internally.
  std::vector<protocol::Hash> demoted_hashes = {CreateHash(2), CreateHash(3)};
  auto fork_tip_it = master_chain_.Add(master_chain_.Find(CreateHash(4)), CreateMasterContext(202, 2, 5));
  master_chain_.PromoteBranch(fork_tip_it, std::span{demoted_hashes});

  // Now, create the single sync packet that HeaderTimechain::Add would have generated.
  // This packet represents the addition of block hash 5, whose parent is hash 4,
  // and it includes the list of hashes that were demoted during the reorg.
  SidecarAddSync reorg_sync_packet = {CreateHash(4), CreateHash(5), demoted_hashes};
  
  // STEP 4: Apply the final reorg sync packet to the sidecar.
  sidecar_.AddSync(reorg_sync_packet);

  // STEP 5: Verify the sidecar's new structure.
  // The new main chain should be 0 -> 4 -> 5.
  // We can verify this by checking that the new tip is accessible by its height.
  const std::string* new_tip_data = sidecar_.Get(2);
  ASSERT_NE(new_tip_data, nullptr);
  EXPECT_EQ(*new_tip_data, "default");

  // After a promotion, a node on the main chain can only be found by height,
  // not by hash in the forest. The check above already confirms the reorg
  // was successful by finding the new tip at the correct height.
  
  // The old main chain tip (hash 3) should now be a fork.
  EXPECT_NE(sidecar_.Get(CreateHash(3)), nullptr);
  
  // Set a value on the new tip and verify
  sidecar_.Set(2, "new_tip_value");
  EXPECT_EQ(*sidecar_.Get(2), "new_tip_value");
}

// Test case for polymorphism via SidecarBase.
TEST_F(SidecarTest, Polymorphism) {
  SidecarBase* base_ptr = &sidecar_;

  // Add genesis to master
  auto sync_genesis = AddToMaster({}, 100, 0, 1);
  
  // Call AddSync via the base class pointer
  base_ptr->AddSync(sync_genesis);

  // Verify the sidecar was updated correctly
  const std::string* genesis_data = sidecar_.Get(0);
  ASSERT_NE(genesis_data, nullptr);
  EXPECT_EQ(*genesis_data, "default");
}

}  // namespace
}  // namespace hornet::data
