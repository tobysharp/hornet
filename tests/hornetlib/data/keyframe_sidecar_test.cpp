#include "hornetlib/data/keyframe_sidecar.h"

#include <stack>

#include <gtest/gtest.h>

#include "hornetlib/data/chain_tree.h"
#include "hornetlib/data/hashed_tree.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/util/assert.h"
#include "hornetlib/util/throw.h"

namespace hornet::data {
namespace {

// Test fixture for the KeyframeSidecar tests.
class KeyframeSidecarTest : public ::testing::Test {
 public:
  KeyframeSidecarTest() {
    sidecar_.AddSync({-1, {}, {}});
  }
 protected:
  // Use int as the enum type for simplicity. 0 is default.
  using TestSidecar = KeyframeSidecar<int>;

  // Helper to create a protocol::Hash for testing.
  protocol::Hash CreateHash(uint64_t val) {
    protocol::Hash hash{};
    std::memcpy(hash.data(), &val, sizeof(val));
    return hash;
  }

  TestSidecar sidecar_{0}; // Default value is 0
};

// --- GET Method Tests ---

TEST_F(KeyframeSidecarTest, GetInitialState) {
  EXPECT_NE(sidecar_.Get(0), nullptr);
  EXPECT_EQ(*sidecar_.Get(0), 0);
  EXPECT_EQ(sidecar_.Get(-1), nullptr);
  EXPECT_EQ(sidecar_.Get(1), nullptr); // length_ is 1, so height 1 is out of bounds
}

TEST_F(KeyframeSidecarTest, GetAfterSet) {
  sidecar_.AddSync({-1, CreateHash(1), {}}); // length_ becomes 1
  sidecar_.AddSync({0, CreateHash(2), {}});  // length_ becomes 2
  sidecar_.AddSync({1, CreateHash(3), {}});  // length_ becomes 3

  sidecar_.Set(1, 5); // Set a new keyframe at height 1
  EXPECT_EQ(*sidecar_.Get(0), 0);
  EXPECT_EQ(*sidecar_.Get(1), 5);
  EXPECT_EQ(*sidecar_.Get(2), 5); // Inherits from keyframe at height 1
}

TEST_F(KeyframeSidecarTest, GetNonExistentFork) {
  EXPECT_EQ(sidecar_.Get(CreateHash(99)), nullptr);
}

// --- SET Method Tests ---

TEST_F(KeyframeSidecarTest, SetNoOp) {
  sidecar_.AddSync({-1, CreateHash(1), {}});
  EXPECT_TRUE(sidecar_.Set(0, 0)); // Value is already 0
}

TEST_F(KeyframeSidecarTest, SetOutOfBounds) {
  EXPECT_FALSE(sidecar_.Set(1, 1)); // length_ is 1, height 1 is out of bounds
}

TEST_F(KeyframeSidecarTest, SetNonExistentFork) {
  EXPECT_FALSE(sidecar_.Set(CreateHash(99), 1));
}

TEST_F(KeyframeSidecarTest, SetToSplitKeyframe) {
  // Chain of 5 blocks, all with value 0
  for (int i = 0; i < 4; ++i) sidecar_.AddSync({i, CreateHash(i + 2), {}});
  
  // Set height 2 to value 5. Should split the keyframe [0, 0] into [0, 0], [2, 5], [3, 0]
  sidecar_.Set(2, 5);

  EXPECT_EQ(*sidecar_.Get(0), 0);
  EXPECT_EQ(*sidecar_.Get(1), 0);
  EXPECT_EQ(*sidecar_.Get(2), 5);
  EXPECT_EQ(*sidecar_.Get(3), 0);
  EXPECT_EQ(*sidecar_.Get(4), 0);
}

TEST_F(KeyframeSidecarTest, SetAtStartOfKeyframeToSplit) {
  for (int i = 0; i < 4; ++i) sidecar_.AddSync({i, CreateHash(i + 2), {}});
  sidecar_.Set(1, 5); // State: [0,0], [1,5]

  // Set height 1 to a new value, 7. This should not be a size-1 overwrite,
  // it should split the [1,5] keyframe into [1,7] and [2,5].
  sidecar_.Set(1, 7);

  EXPECT_EQ(*sidecar_.Get(0), 0);
  EXPECT_EQ(*sidecar_.Get(1), 7);
  EXPECT_EQ(*sidecar_.Get(2), 5);
  EXPECT_EQ(*sidecar_.Get(3), 5);
  EXPECT_EQ(*sidecar_.Get(4), 5);
}

TEST_F(KeyframeSidecarTest, SetAtStartOfKeyframeToImplicitlyMerge) {
  for (int i = 0; i < 4; ++i) sidecar_.AddSync({i, CreateHash(i + 2), {}});
  sidecar_.Set(1, 5); // State: [0,0], [1,5]

  // Set height 1 back to 0. This should not insert a new keyframe,
  // but just shorten the [1,5] keyframe to start at height 2.
  sidecar_.Set(1, 0);

  EXPECT_EQ(*sidecar_.Get(0), 0);
  EXPECT_EQ(*sidecar_.Get(1), 0);
  EXPECT_EQ(*sidecar_.Get(2), 5);
  EXPECT_EQ(*sidecar_.Get(3), 5);
  EXPECT_EQ(*sidecar_.Get(4), 5);
}


TEST_F(KeyframeSidecarTest, SetSize1KeyframeAndMergeWithPrevious) {
  for (int i = 0; i < 4; ++i) sidecar_.AddSync({i, CreateHash(i + 2), {}});
  sidecar_.Set(1, 5); // State: [0,0], [1,5]
  sidecar_.Set(2, 5); // State: [0,0], [1,5]
  sidecar_.Set(3, 5); // State: [0,0], [1,5]
  
  // Set height 1 back to 0. Should merge the size-1 keyframe [1,5] with [0,0].
  sidecar_.Set(1, 0);

  EXPECT_EQ(*sidecar_.Get(0), 0);
  EXPECT_EQ(*sidecar_.Get(1), 0);
  EXPECT_EQ(*sidecar_.Get(2), 5);
  EXPECT_EQ(*sidecar_.Get(3), 5);
}

TEST_F(KeyframeSidecarTest, SetSize1KeyframeAndMergeWithNext) {
  for (int i = 0; i < 4; ++i) sidecar_.AddSync({i, CreateHash(i + 2), {}});
  sidecar_.Set(1, 5); // State: [0,0], [1,5]
  sidecar_.Set(2, 0); // State: [0,0], [1,5], [2,0]

  // Set height 1 to 0. Should merge [1,5] with [2,0] by erasing [1,5].
  sidecar_.Set(1, 0);

  EXPECT_EQ(*sidecar_.Get(0), 0);
  EXPECT_EQ(*sidecar_.Get(1), 0);
  EXPECT_EQ(*sidecar_.Get(2), 0);
}

TEST_F(KeyframeSidecarTest, SetSize1KeyframeAndMergeWithBoth) {
  for (int i = 0; i < 4; ++i) sidecar_.AddSync({i, CreateHash(i + 2), {}});
  sidecar_.Set(1, 5); // State: [0,0], [1,5]
  sidecar_.Set(2, 0); // State: [0,0], [1,5], [2,0]

  // Set height 1 to 0. Should merge with both previous and next.
  sidecar_.Set(1, 0);
  
  for(int i=0; i<5; ++i) EXPECT_EQ(*sidecar_.Get(i), 0);
}


// --- ADDSYNC Method Tests ---

TEST_F(KeyframeSidecarTest, AddSyncChainExtension) {
  sidecar_.AddSync({-1, CreateHash(1), {}});
  EXPECT_EQ(*sidecar_.Get(0), 0);
  EXPECT_EQ(sidecar_.Get(1), nullptr);

  sidecar_.AddSync({0, CreateHash(2), {}});
  EXPECT_EQ(*sidecar_.Get(1), 0);
}

TEST_F(KeyframeSidecarTest, AddSyncChainExtensionWithNonDefaultTip) {
  sidecar_.AddSync({-1, CreateHash(1), {}});
  sidecar_.Set(0, 5); // Tip value is now 5
  
  // Add a new block. It should get the default value (0), creating a new keyframe.
  sidecar_.AddSync({0, CreateHash(2), {}});
  EXPECT_EQ(*sidecar_.Get(0), 5);
  EXPECT_EQ(*sidecar_.Get(1), 0);
}

TEST_F(KeyframeSidecarTest, AddSyncForkFromMainChain) {
  sidecar_.AddSync({-1, CreateHash(1), {}});
  sidecar_.AddSync({0, CreateHash(2), {}});

  // Fork from height 0
  sidecar_.AddSync({0, CreateHash(10), {}});

  const int* fork_val = sidecar_.Get(CreateHash(10));
  ASSERT_NE(fork_val, nullptr);
  EXPECT_EQ(*fork_val, 0); // Inherits value from parent on main chain
}

TEST_F(KeyframeSidecarTest, AddSyncExtendExistingFork) {
  sidecar_.AddSync({-1, CreateHash(1), {}});
  sidecar_.AddSync({0, CreateHash(10), {}}); // Fork from height 0
  sidecar_.Set(CreateHash(10), 8); // Set fork value

  // Extend the fork
  sidecar_.AddSync({CreateHash(10), CreateHash(11), {}});
  
  const int* fork_child_val = sidecar_.Get(CreateHash(11));
  ASSERT_NE(fork_child_val, nullptr);
  EXPECT_EQ(*fork_child_val, 8); // Inherits value from fork parent
}

TEST_F(KeyframeSidecarTest, AddSyncReorg) {
  // Initial chain: 0 -> 1 -> 2 (all value 0)
  sidecar_.AddSync({-1, CreateHash(1), {}});
  sidecar_.AddSync({0, CreateHash(2), {}});
  sidecar_.AddSync({1, CreateHash(3), {}});

  // A fork appears with a different value
  sidecar_.AddSync({0, CreateHash(4), {}}); // Fork from height 0
  sidecar_.Set(CreateHash(4), 5); // Fork value is 5

  // The final block arrives, causing a reorg.
  // The new chain will be 0 -> 4 -> 5.
  // The demoted chain is 1 -> 2.
  std::vector<protocol::Hash> demoted_hashes = {CreateHash(2), CreateHash(3)};
  SidecarAddSync reorg_sync = {CreateHash(4), CreateHash(5), demoted_hashes};
  sidecar_.AddSync(reorg_sync);

  // Verify new chain state
  EXPECT_EQ(*sidecar_.Get(0), 0);
  EXPECT_EQ(*sidecar_.Get(1), 5); // New keyframe from promoted branch
  EXPECT_EQ(*sidecar_.Get(2), 5); // Inherits from new keyframe

  // Verify old chain is now a fork
  const int* old_block_1 = sidecar_.Get(CreateHash(2));
  ASSERT_NE(old_block_1, nullptr);
  EXPECT_EQ(*old_block_1, 0); // Should have its original value

  const int* old_block_2 = sidecar_.Get(CreateHash(3));
  ASSERT_NE(old_block_2, nullptr);
  EXPECT_EQ(*old_block_2, 0);
}

}  // namespace
}  // namespace hornet::data
