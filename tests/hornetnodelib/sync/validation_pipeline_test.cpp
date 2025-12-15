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
  ValidationPipeline pipeline(*timechain, db, std::ref(callback));

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
  constexpr int kLength = 20;
  const auto path = CurrentTestVectorPath();
  if (!std::filesystem::exists(path))  {
    // Construct test data file.
    test::Blockchain data;
    for (int height = 1; height < kLength; ++height) 
      data.Append(data.Sample());  // Create a valid block
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
      data.Append(data.Sample());  // Create a valid block
    data[3]->Transaction(1).Input(0).previous_output.hash[0]++;  // Corrupt input txid.
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
    for (int height = 1; height < 4; ++height) 
      data.Append(data.Sample());  // Create a valid block
    data[3]->Transaction(1).Input(0).previous_output.hash[0]++;  // Corrupt input txid.
    auto header = data[3]->Header();
    header.SetMerkleRoot(consensus::ComputeMerkleRoot(*data[3]).hash);
    data[3]->SetHeader(header);
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

}  // namespace
}  // namespace hornet::node::sync
