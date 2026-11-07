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
#include "hornetlib/data/key.h"
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

  void operator()(const std::shared_ptr<const protocol::Block>&, const data::Key& id,
                      consensus::Result result) {
    if (!result) {
      LogDebug() << "Warning: Validation failed at height " << id.height << " with code " << (int)result.Error();
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
  ValidationPipeline pipeline(*timechain, db);

  // Submit all validations in order and wait for drain.
  for (int height = 1; height < data.Length(); ++height)
    pipeline.Submit(data[height], height, std::ref(callback));
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
  ValidationPipeline pipeline(*timechain, db);

  // Submit all validations out of order and wait for drain.
  for (int height = 1; height < data.Length(); height += 3) {
    for (int offset : {1, 2, 0}) {
      const int submit = height + offset;
      if (submit < data.Length())    
        pipeline.Submit(data[submit], submit, std::ref(callback));
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
  ValidationPipeline pipeline(*timechain, db, 1, data.Length());

  // Submit all validations out of order and wait for drain.
  std::vector<int> heights(data.Length() - 1);
  std::iota(heights.begin(), heights.end(), 1);
  std::shuffle(heights.begin(), heights.end(), std::mt19937{69'420});

  for (int height : heights)
    pipeline.Submit(data[height], height, std::ref(callback));
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
      data.Append(data.Sample(1'000, true));  // Create a maturity-valid block
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
      data.Append(data.Sample(1'000, true));  // Create a maturity-valid block
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
    test::Blockchain data{test::GetDataPath("ValidationPipelineTest_ProcessBlocks.bin")};

    const auto block = data.Back();
    block->Transaction(1).Input(0).previous_output.hash[0]++;  // Corrupt exactly one spend input txid.
    auto header = block->Header();
    header.SetMerkleRoot(consensus::ComputeMerkleRoot(*block).hash);
    block->SetHeader(header);

    data.Save(path.string() + ".nopow");
    FAIL() << "Test file \"" << path << "\" was missing. Run tools/minetests.sh then re-run test.";
  }

  const consensus::Error expected = consensus::Error::Spending_OutPointNotCreated;
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

TEST(ValidationPipelineTest, WindowBackPressure) {
  const auto path = test::GetDataPath("ValidationPipelineTest_ProcessBlocks.bin");
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "Test file \"" << path << "\" was missing.";
  }

  // Load the block data.
  const test::Blockchain data{path};

  // Set up the UTXO database and validation pipeline with a window of 5 heights.
  const test::TempFolder dir;
  data::utxo::Database db(dir.Path());
  Completions callback;
  const auto timechain = BuildHeaderChain(data);
  ValidationPipeline pipeline(*timechain, db, 4, 5);

  // The frontier starts at height 1 (only genesis is in the database), so the admission window
  // covers heights [1, 6). Submit blocks 2..5 with block 1 missing: they are admitted but stall in
  // the spend pipeline waiting for block 1 to append, and the frontier cannot advance.
  for (int height = 2; height <= 5; ++height) pipeline.Submit(data[height], height, std::ref(callback));

  // Block 6 lies one beyond the window. Submitting it must apply back-pressure (block), not throw.
  std::atomic<bool> submitted_six = false;
  std::thread t([&] {
    pipeline.Submit(data[6], 6, std::ref(callback));
    submitted_six = true;
  });

  // While the frontier is stuck at height 1, block 6 stays blocked and nothing retires.
  std::this_thread::sleep_for(200ms);
  EXPECT_FALSE(submitted_six.load());
  EXPECT_EQ(callback.completions.load(), 0);

  // Supplying the missing block 1 advances the frontier, which both drains the stalled blocks and
  // releases the back-pressured submit of block 6 — no deadlock, no abort.
  pipeline.Submit(data[1], 1, std::ref(callback));
  t.join();
  EXPECT_TRUE(submitted_six.load());

  EXPECT_TRUE(pipeline.Wait(5s));
  EXPECT_EQ(callback.completions.load(), 6);  // heights 1..6 all validated
}

TEST(ValidationPipelineTest, FrontierSeededFromDatabase) {
  const auto path = test::GetDataPath("ValidationPipelineTest_ProcessBlocks.bin");
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "Test file \"" << path << "\" was missing.";
  }

  const test::Blockchain data{path};
  const test::TempFolder dir;
  data::utxo::Database db(dir.Path());
  const auto timechain = BuildHeaderChain(data);

  constexpr int kSeed = 10;     // Validate heights 1..9 to advance the database's contiguous length.
  constexpr int kWindow = 5;

  // First pipeline: validate the first kSeed - 1 blocks so they append to the database.
  {
    Completions callback;
    ValidationPipeline pipeline(*timechain, db);
    for (int height = 1; height < kSeed; ++height) pipeline.Submit(data[height], height, std::ref(callback));
    EXPECT_TRUE(pipeline.Wait(5s));
    EXPECT_EQ(callback.completions.load(), kSeed - 1);
  }

  // The database is now contiguous up to kSeed (heights 0..kSeed-1 are present).
  ASSERT_EQ(db.GetContiguousLength(), kSeed);

  // A fresh pipeline on the same database must seed its retirement frontier from the database rather
  // than from height 1, so its admission window starts at kSeed.
  ValidationPipeline resumed(*timechain, db, 4, kWindow);
  EXPECT_EQ(resumed.GetAdmissibleHeightLimit(), kSeed + kWindow);

  // Consequently it can immediately admit and validate the next block (height kSeed) — which a
  // frontier wrongly stuck at 1 would have refused, since kSeed >= 1 + kWindow.
  Completions callback;
  resumed.Submit(data[kSeed], kSeed, std::ref(callback));
  EXPECT_TRUE(resumed.Wait(5s));
  EXPECT_EQ(callback.completions.load(), 1);
}

TEST(ValidationPipelineTest, AbortReleasesBlockedSubmit) {
  const auto path = test::GetDataPath("ValidationPipelineTest_ProcessBlocks.bin");
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "Test file \"" << path << "\" was missing.";
  }

  const test::Blockchain data{path};
  const test::TempFolder dir;
  data::utxo::Database db(dir.Path());
  Completions callback;
  const auto timechain = BuildHeaderChain(data);

  // window = 1, frontier = 1: only height 1 is admissible. With block 1 never submitted the frontier
  // can never advance, so submitting any higher block parks in the back-pressure wait indefinitely.
  ValidationPipeline pipeline(*timechain, db, 4, 1);

  std::future<void> blocked =
      std::async(std::launch::async, [&] { pipeline.Submit(data[3], 3, std::ref(callback)); });

  // Confirm the submit is genuinely blocked, not racing through.
  EXPECT_EQ(blocked.wait_for(200ms), std::future_status::timeout);

  // Aborting must release the blocked submit promptly rather than leaving it wedged.
  pipeline.Abort();
  ASSERT_EQ(blocked.wait_for(2s), std::future_status::ready);
  blocked.get();  // Propagate any exception; the submit should have returned cleanly.

  // The block was never admitted, so nothing was validated.
  EXPECT_EQ(callback.completions.load(), 0);
}

}  // namespace
}  // namespace hornet::node::sync
