#include "hornetlib/data/utxo/spend_pipeline.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

#include <gtest/gtest.h>

#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/data/utxo/database.h"

#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"

#include "testutil/blockchain.h"
#include "testutil/temp_folder.h"

namespace hornet::data::utxo {
namespace {

class SpendPipelineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    db_ = std::make_unique<Database>(temp_dir_.Path());
    // Use 2 threads to test concurrency.
    pipeline_ = std::make_unique<SpendPipeline>(*db_, 2);
  }

  test::TempFolder temp_dir_;
  std::unique_ptr<Database> db_;
  std::unique_ptr<SpendPipeline> pipeline_;
};

TEST_F(SpendPipelineTest, ProcessBlocks) {
  constexpr int kLength = 50;
  test::Blockchain chain;
  
  while (chain.Length() < kLength) {
    chain.Append(chain.Sample());
    int i = chain.Length() - 1;
    auto joiner = pipeline_->Add(chain[i], i);
    
    // Wait for the joiner to be ready.
    EXPECT_TRUE(joiner->WaitForFetch());
    
    const consensus::Result result = joiner->Join([i](const consensus::SpendRecord& spend) {
        EXPECT_LT(spend.funding_height, i);
        EXPECT_GT(spend.amount, 0);
        return consensus::Result{};
    });
    
    EXPECT_EQ(result, consensus::Result{});
  }
}

TEST_F(SpendPipelineTest, ProcessBlocksOutOfOrder) {
  constexpr int kBlocks = 20;
  test::Blockchain chain;
  
  // Generate blocks.
  for (int i = 0; i < kBlocks; ++i)
    chain.Append(chain.Sample());

  // Create indices and shuffle them.
  std::vector<int> heights(kBlocks);
  std::iota(heights.begin(), heights.end(), 1);
  std::shuffle(heights.begin(), heights.end(), std::mt19937{std::random_device{}()});

  std::vector<std::shared_ptr<SpendJoiner>> joiners(chain.Length());

  // Add blocks in random order.
  for (int height : heights)
    joiners[height] = pipeline_->Add(chain[height], height);

  // Verify that all blocks complete successfully.
  for (int i = 0; i < kBlocks; ++i) {
    int height = i + 1;
    EXPECT_TRUE(joiners[height]->WaitForFetch());
    
    const consensus::Result result = joiners[height]->Join([height](const consensus::SpendRecord& spend) {
        EXPECT_LT(spend.funding_height, height);
        EXPECT_GT(spend.amount, 0);
        return consensus::Result{};
    });
    EXPECT_EQ(result, consensus::Result{});
  }
}

TEST_F(SpendPipelineTest, ProcessInvalidBlock) {
  test::Blockchain chain;
  
  // Add a valid block.
  chain.Append(chain.Sample());
  auto joiner0 = pipeline_->Add(chain[1], 1);

  EXPECT_TRUE(joiner0->WaitForFetch());
  EXPECT_EQ(joiner0->Join([](const consensus::SpendRecord&) { return consensus::Result{}; }), consensus::Result{});

  // Add an invalid block that spends a non-existent output.
  auto block1 = std::make_shared<protocol::Block>(chain.Sample());
  ASSERT_GT(block1->GetTransactionCount(), 1);
  block1->Transaction(1).Input(0).previous_output.hash[0]++;
  auto joiner1 = pipeline_->Add(block1, 2);
  
  // WaitForFetch should return false because it will fail at the Query stage (inputs not found).
  EXPECT_FALSE(joiner1->WaitForFetch());
  EXPECT_EQ(joiner1->GetState(), SpendJoiner::State::Error);
}

} // namespace
} // namespace hornet::data::utxo
