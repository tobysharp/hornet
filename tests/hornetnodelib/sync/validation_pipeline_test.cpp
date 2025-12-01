#include "hornetnodelib/sync/validation_pipeline.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <random>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "hornetlib/consensus/merkle.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/data/timechain.h"
#include "testutil/blockchain.h"
#include "hornetlib/data/utxo/database.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/util/timeout.h"
#include "testutil/temp_folder.h"

namespace hornet::node::sync {
namespace {

TEST(ValidationPipelineTest, ProcessBlocks) {

  constexpr int kLength = 20; 
  
  test::TempFolder temp_dir;
  data::utxo::Database db(temp_dir.Path());
  data::Timechain timechain;
  test::Blockchain data;

  data.Append(data.Sample());
  
  // Currently we have to manually append the genesis block to the db, since we can't validate it.
  // In the future, we might want to clean this up and make it more uniform, e.g. by having a 
  // special-case validation for a block with null parent, comparing it against the known genesis.
  db.Append(*data[0], 0);
  
  for (int i = 1; i < kLength; ++i) {
    data.Append(data.Sample());
    auto parent_it = timechain.ReadHeaders()->ChainTip();
    timechain.AddHeader(parent_it, parent_it->Extend(data[i]->Header()));
  }

  std::atomic<int> completed_count = 0;
  auto callback = [&](const std::shared_ptr<const protocol::Block>&, int, consensus::Result result) {
    EXPECT_TRUE(result);
    ++completed_count;
  };

  ValidationPipeline pipeline(timechain, db, callback);

  const auto start = std::chrono::high_resolution_clock::now();

  for (int i = 1; i < kLength; ++i)  // Don't submit genesis block for validation.
    pipeline.Submit(data[i], i);

  util::Timeout timeout(10'000);
  int completed = completed_count;
  while (completed != kLength - 1 && timeout) {
    completed_count.wait(completed);
    completed = completed_count;
  }
  
  const auto duration = std::chrono::high_resolution_clock::now() - start;
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  std::cout << "Took " << ms << " ms\n";

  EXPECT_EQ(completed_count, kLength - 1);
}

TEST(ValidationPipelineTest, ProcessInvalidBlock) {
  test::TempFolder temp_dir;
  data::utxo::Database db(temp_dir.Path());
  data::Timechain timechain;

  test::Blockchain chain;
  chain.Append(protocol::Block(protocol::Block::Genesis()));
  
  { // Create a valid block
    chain.Append(chain.Sample());
    const auto block = chain[chain.Length() - 1];
    auto parent_it = timechain.ReadHeaders()->Search(block->Header().GetPreviousBlockHash());
    timechain.AddHeader(parent_it, parent_it->Extend(block->Header()));
  }

  { // Create an invalid block
    chain.Append(chain.Sample());
    auto block = chain[chain.Length() - 1];
    ASSERT_GT(block->GetTransactionCount(), 1);
    auto tx = block->Transaction(1);
    ASSERT_GT(tx.InputCount(), 0);
    tx.Input(0).previous_output.hash[0]++;  // Corrupt input txid.
    auto parent_it = timechain.ReadHeaders()->Search(block->Header().GetPreviousBlockHash());
    timechain.AddHeader(parent_it, parent_it->Extend(block->Header()));
  }

  std::promise<consensus::Result> result_promise;
  auto on_complete = [&](const std::shared_ptr<const protocol::Block>&, int height, consensus::Result result) {
    if (height == 1)
      EXPECT_TRUE(result);
    else if (height == 2)
      result_promise.set_value(result);
  };

  ValidationPipeline pipeline{timechain, db, on_complete};
  pipeline.Submit(chain[1], 1);
  pipeline.Submit(chain[2], 2);

  // Unblock execution of the spend pipeline.
  db.Append(*chain[0], 0);

  auto result_future = result_promise.get_future();
  ASSERT_EQ(result_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
  EXPECT_EQ(result_future.get(), consensus::Error::Transaction_NotUnspent);
}

} // namespace
} // namespace hornet::node::sync
