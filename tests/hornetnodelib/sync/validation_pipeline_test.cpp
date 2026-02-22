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
#include "hornetlib/data/utxo/database.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/util/timeout.h"
#include "testutil/blockchain.h"
#include "testutil/temp_folder.h"

namespace hornet::node::sync {
namespace {

using namespace std::chrono_literals;

// Build the header chain.
std::unique_ptr<data::Timechain> BuildHeaderChain(const test::Blockchain& data) {
  auto timechain = std::make_unique<data::Timechain>(data[0]->Header());
  for (int height = 1; height < data.Length(); ++height) {
    auto parent_it = timechain->ReadHeaders()->ChainTip();
    timechain->AddHeader(parent_it, parent_it->Extend(data[height]->Header()));
  }
  return timechain;
}

// Get the path to a blocks data file for the current test.
std::filesystem::path CurrentTestVectorPath() {
  const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
  std::string filename = std::string{info->test_suite_name()} + "_" + info->name() + ".bin";
  return test::GetDataPath(filename);
}

struct Completions {
  std::atomic<int> completions = 0;
  std::atomic<bool> success = true;
  consensus::Result rv = consensus::Result::Ok;

  void operator()(const std::shared_ptr<const protocol::Block>&, int height,
                      consensus::Result result) {
    if (!result) {
      LogDebug() << "Warning: Validation failed at height " << height << " with code " << (int)result.Error();
      bool expected = true;
      if (success.compare_exchange_strong(expected, false))
        rv = result;
    }
    ++completions;
  }
};

consensus::Result ValidateInOrder(const std::filesystem::path& path) {
  // Load the block data.
  const test::Blockchain data{path};

  // Set up the UTXO database and validation pipeline.
  const test::TempFolder dir;
  data::utxo::Database db(dir.Path());
  Completions callback;
  const auto timechain = BuildHeaderChain(data);
  ValidationPipeline pipeline(*timechain, db, std::ref(callback));

  // Submit all validations in order and wait for drain.
  for (int height = 1; height < data.Length(); ++height)
    pipeline.Submit(data[height], height);
  EXPECT_TRUE(pipeline.Wait(5s));

  // Check that every block completed.
  EXPECT_EQ(callback.completions, data.Length() - 1);
  return callback.rv;
}

consensus::Result ValidateOutOfOrder(const std::filesystem::path& path) {
  // Load the block data.
  const test::Blockchain data{path};

  // Set up the UTXO database and validation pipeline.
  const test::TempFolder dir;
  data::utxo::Database db(dir.Path());
  Completions callback;
  const auto timechain = BuildHeaderChain(data);
  ValidationPipeline pipeline(*timechain, db, std::ref(callback));

  // Submit all validations out of order and wait for drain.
  for (int height = 1; height < data.Length(); height += 3) {
    for (int offset : {1, 2, 0}) {
      const int submit = height + offset;
      if (submit < data.Length())    
        pipeline.Submit(data[submit], submit);
    }
  }
  EXPECT_TRUE(pipeline.Wait(5s));

  // Check that every block completed.
  EXPECT_EQ(callback.completions, data.Length() - 1);
  return callback.rv;
}

consensus::Result ValidateShuffle(const std::filesystem::path& path) {
  // Load the block data.
  const test::Blockchain data{path};

  // Set up the UTXO database and validation pipeline.
  const test::TempFolder dir;
  data::utxo::Database db(dir.Path());
  Completions callback;
  const auto timechain = BuildHeaderChain(data);
  ValidationPipeline pipeline(*timechain, db, std::ref(callback), 1, data.Length());

  // Submit all validations out of order and wait for drain.
  std::vector<int> heights(data.Length() - 1);
  std::iota(heights.begin(), heights.end(), 1);
  std::shuffle(heights.begin(), heights.end(), std::mt19937{69'420});

  for (int height : heights)
    pipeline.Submit(data[height], height);
  EXPECT_TRUE(pipeline.Wait(5s));

  // Check that every block completed.
  EXPECT_EQ(callback.completions, data.Length() - 1);
  return callback.rv;
}

TEST(ValidationPipelineTest, ProcessBlocks) {
  constexpr int kLength = 104;
  const auto path = CurrentTestVectorPath();
  if (!std::filesystem::exists(path))  {
    // Construct test data file.
    test::Blockchain data;
    for (int height = 1; height < kLength; ++height) 
      data.Append(data.Sample(1'000, 2, 4, true));  // Create a maturity-valid block
    data.Save(path.string() + ".nopow");
    FAIL() << "Test file \"" << path << "\" was missing. Run tools/minetests.sh, then re-run test.";

  }
  EXPECT_TRUE(ValidateInOrder(path));
  EXPECT_TRUE(ValidateOutOfOrder(path));
  EXPECT_TRUE(ValidateShuffle(path));
}

TEST(ValidationPipelineTest, ProcessInvalidMerkleRoot) {
  const auto path = CurrentTestVectorPath();
  if (!std::filesystem::exists(path))  {
    // Construct test data file.
    test::Blockchain data;
    for (int height = 1; height < 4; ++height) 
      data.Append(data.Sample(1'000, 2, 4, true));  // Create a maturity-valid block
    data[3]->Transaction(0).Output(0).value += 1;  // Corrupt block data without updating Merkle root.
    data.Save(path.string() + ".nopow");
    FAIL() << "Test file \"" << path << "\" was missing. Run tools/minetests.sh then re-run test.";
  }

  const consensus::Error expected = consensus::Error::Structure_BadMerkleRoot;
  EXPECT_EQ(ValidateInOrder(path), expected);
  EXPECT_EQ(ValidateOutOfOrder(path), expected);
  EXPECT_EQ(ValidateShuffle(path), expected);
  
  // TODO: Make sure that the block that failed validation got erased from the database.
}

TEST(ValidationPipelineTest, ProcessInvalidUTXO) {
  const auto path = CurrentTestVectorPath();
  if (!std::filesystem::exists(path))  {
    // Construct test data file.
    test::Blockchain data;
    constexpr int kLength = 104;
    for (int height = 1; height < kLength; ++height)
      data.Append(data.Sample(1'000, 2, 4, true));  // Create maturity-valid chain with spendable outputs.

    auto block = data[kLength - 1];
    ASSERT_GT(block->GetTransactionCount(), 1)
        << "Failed to create invalid UTXO fixture: no non-coinbase transaction found.";
    auto tx = block->Transaction(1);
    ASSERT_GT(tx.InputCount(), 0)
        << "Failed to create invalid UTXO fixture: transaction 1 has no inputs.";
    tx.Input(0).previous_output.hash[0] ^= 0x01;  // Corrupt exactly one spend input txid.

    auto header = block->Header();
    header.SetMerkleRoot(consensus::ComputeMerkleRoot(*block).hash);
    block->SetHeader(header);

    data.Save(path.string() + ".nopow");
    FAIL() << "Test file \"" << path << "\" was missing. Run tools/minetests.sh then re-run test.";
  }

  const consensus::Error expected = consensus::Error::Transaction_NotUnspent;
  EXPECT_EQ(ValidateInOrder(path), expected);
  EXPECT_EQ(ValidateOutOfOrder(path), expected);
  EXPECT_EQ(ValidateShuffle(path), expected);
  
  // TODO: Make sure that the block that failed validation got erased from the database.
}

TEST(ValidationPipelineTest, ProcessMainnet50Blocks) {
  const auto path = CurrentTestVectorPath();
  if (!std::filesystem::exists(path)) {
    FAIL() << "Test file \"" << path << "\" was missing. Save first 50 blocks of mainnet then re-run test.";
  }

  EXPECT_TRUE(ValidateInOrder(path));
  EXPECT_TRUE(ValidateOutOfOrder(path));
  EXPECT_TRUE(ValidateShuffle(path));
}

TEST(ValidationPipelineTest, DetectDeadlock) {
  const auto path = test::GetDataPath("ValidationPipelineTest_ProcessBlocks.bin");
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "Test file \"" << path << "\" was missing.";
  }

  // Load the block data.
  const test::Blockchain data{path};

  // Set up the UTXO database and validation pipeline.
  const test::TempFolder dir;
  data::utxo::Database db(dir.Path());
  Completions callback;
  const auto timechain = BuildHeaderChain(data);

  // max_active_count = 5
  ValidationPipeline pipeline(*timechain, db, std::ref(callback), 4, 5);

  // Submit blocks 2 to 6 (5 blocks). This fills the pipeline.
  // Block 1 is missing, so they will stall in the spend pipeline.
  for (int height = 2; height <= 6; ++height) {
    pipeline.Submit(data[height], height);
  }

  // Submit one more block. This should trigger the deadlock detection.
  EXPECT_THROW({ pipeline.Submit(data[7], 7); }, std::runtime_error);
}

}  // namespace
}  // namespace hornet::node::sync
