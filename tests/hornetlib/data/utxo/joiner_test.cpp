#include "hornetlib/data/utxo/joiner.h"

#include <gtest/gtest.h>

#include "testutil/blockchain.h"
#include "testutil/temp_folder.h"

namespace hornet::data::utxo {
namespace {

TEST(SpendJoinerTest, TestConstruct) {
  test::Blockchain chain;

  test::TempFolder dir;
  Database db{dir.Path()};

  const auto block = std::make_shared<protocol::Block>(chain.Sample());
  SpendJoiner joiner{db, block, 0};

  EXPECT_EQ(joiner.GetState(), SpendJoiner::State::Init);
  EXPECT_FALSE(joiner.IsJoinReady());
  EXPECT_TRUE(joiner.IsAdvanceReady());
}

TEST(SpendJoinerTest, TestPreemptiveSerial) {
  test::TempFolder dir;
  Database db{dir.Path()};

  constexpr int kLength = 100;
  test::Blockchain chain;
  for (int i = 0; i < kLength; ++i) {
    auto block = std::make_shared<protocol::Block>(chain.Sample());
    {
      SpendJoiner joiner{db, block, i};

      EXPECT_EQ(joiner.GetState(), SpendJoiner::State::Init);
      EXPECT_FALSE(joiner.IsJoinReady());
      EXPECT_TRUE(joiner.IsAdvanceReady());

      // Simulates a serial validation pipeline.
      while (joiner.IsAdvanceReady())
        joiner.Advance();  // Execute preemptive Append, then Query and Fetch.
      
      EXPECT_TRUE(joiner.IsJoinReady());

      // Join the block's transactions with their inputs' funding prevouts.
      const consensus::Result result = joiner.Join([i](const consensus::SpendRecord& spend) {
        EXPECT_LT(spend.funding_height, i);
        EXPECT_GT(spend.amount, 0);
        return consensus::Result{};
      });
      EXPECT_EQ(joiner.GetState(), SpendJoiner::State::Joined);
      EXPECT_EQ(result, consensus::Result{});
    }
    chain.Append(std::move(*block));
  }
}

TEST(SpendJoinerTest, TestPreemptiveInvalidBlock) {
  test::TempFolder dir;
  Database db{dir.Path()};

  constexpr int kLength = 100;
  test::Blockchain chain;
  for (int i = 0; i < kLength / 2; ++i) {
    auto block = std::make_shared<protocol::Block>(chain.Sample());
    {
      SpendJoiner joiner{db, block, i};

      EXPECT_EQ(joiner.GetState(), SpendJoiner::State::Init);
      EXPECT_FALSE(joiner.IsJoinReady());
      EXPECT_TRUE(joiner.IsAdvanceReady());

      // Simulates a serial validation pipeline.
      while (joiner.IsAdvanceReady())
        joiner.Advance();  // Execute preemptive Append, then Query and Fetch.
      
      EXPECT_TRUE(joiner.IsJoinReady());

      // Join the block's transactions with their inputs' funding prevouts.
      const consensus::Result result = joiner.Join([i](const consensus::SpendRecord& spend) {
        EXPECT_LT(spend.funding_height, i);
        EXPECT_GT(spend.amount, 0);
        return consensus::Result{};
      });
      EXPECT_EQ(joiner.GetState(), SpendJoiner::State::Joined);
      EXPECT_EQ(result, consensus::Result{});
    }
    chain.Append(std::move(*block));
  }

  // Generate an invalid block
  auto block = std::make_shared<protocol::Block>(chain.Sample());
  block->Transaction(1).Input(0).previous_output.hash[0]++;  // Corrupts one prevout txid.
  {
      SpendJoiner joiner{db, block, kLength / 2};

      EXPECT_EQ(joiner.GetState(), SpendJoiner::State::Init);
      EXPECT_FALSE(joiner.IsJoinReady());
      EXPECT_TRUE(joiner.IsAdvanceReady());

      // Simulates a serial validation pipeline.
      while (joiner.IsAdvanceReady())
        joiner.Advance();  // Execute preemptive Append, then Query and Fetch.
      
      EXPECT_FALSE(joiner.IsJoinReady());
      EXPECT_EQ(joiner.GetState(), SpendJoiner::State::Error);

      // Now we have to un-append the invalid block.
      db.EraseSince(kLength / 2);
  }

  for (int i = kLength / 2; i < kLength; ++i) {
    auto block = std::make_shared<protocol::Block>(chain.Sample());
    {
      SpendJoiner joiner{db, block, i};

      EXPECT_EQ(joiner.GetState(), SpendJoiner::State::Init);
      EXPECT_FALSE(joiner.IsJoinReady());
      EXPECT_TRUE(joiner.IsAdvanceReady());

      // Simulates a serial validation pipeline.
      while (joiner.IsAdvanceReady())
        joiner.Advance();  // Execute preemptive Append, then Query and Fetch.
      
      EXPECT_TRUE(joiner.IsJoinReady());

      // Join the block's transactions with their inputs' funding prevouts.
      const consensus::Result result = joiner.Join([i](const consensus::SpendRecord& spend) {
        EXPECT_LT(spend.funding_height, i);
        EXPECT_GT(spend.amount, 0);
        return consensus::Result{};
      });
      EXPECT_EQ(joiner.GetState(), SpendJoiner::State::Joined);
      EXPECT_EQ(result, consensus::Result{});
    }
    chain.Append(std::move(*block));
  }  
}

TEST(SpendJoinerTest, TestPartialFetchBug) {
  test::TempFolder dir;
  Database db{dir.Path()};

  // 1. Setup chain with 3 blocks: 0 -> 1 -> 2
  test::Blockchain chain;
  chain.Append(chain.Sample());
  // Genesis is Block 0.
  db.Append(*chain[0], 0); // Append Genesis to DB. ContiguousLength -> 1.

  auto block1_val = chain.Sample(10);
  auto block1 = std::make_shared<protocol::Block>(block1_val); // Block 1
  chain.Append(std::move(block1_val));
  // Do NOT append Block 1 to DB.

  auto block2_val = chain.Sample(10);
  auto block2 = std::make_shared<protocol::Block>(block2_val); // Block 2
  chain.Append(std::move(block2_val));

  // 2. Create SpendJoiner for Block 2
  SpendJoiner joiner{db, block2, 2};

  EXPECT_EQ(joiner.GetState(), SpendJoiner::State::Init);
  joiner.Advance(); // Parse
  EXPECT_EQ(joiner.GetState(), SpendJoiner::State::Parsed);
  joiner.Advance(); // Append
  EXPECT_EQ(joiner.GetState(), SpendJoiner::State::Appended);
  joiner.Advance(); // Query
  
  // Because Block 1 is missing from DB, and Block 2 likely spends it,
  // and query range is [0, 1), Block 1 outputs won't be found.
  // Also query_before_ (1) < height_ (2), so it goes to QueriedPartial.
  EXPECT_EQ(joiner.GetState(), SpendJoiner::State::QueriedPartial);
  
  // This next step should crash due to the bug in Table::Unpack called via Fetch.
  joiner.Advance(); // Fetch
}

TEST(SpendJoinerTest, TestIncrementalResolution) {
  test::TempFolder dir;
  Database db{dir.Path()};

  test::Blockchain chain;
  chain.Append(chain.Sample()); // Genesis
  db.Append(*chain[0], 0);

  auto block1_val = chain.Sample(10);
  auto block1 = std::make_shared<protocol::Block>(block1_val);
  chain.Append(std::move(block1_val));
  // Don't append Block 1 yet.

  auto block2_val = chain.Sample(10);
  auto block2 = std::make_shared<protocol::Block>(block2_val);
  chain.Append(std::move(block2_val));

  SpendJoiner joiner{db, block2, 2};
  
  // Parse & Append (Block 2)
  joiner.Advance(); 
  joiner.Advance(); 
  
  // Query. Should be partial because Block 1 is missing from DB.
  // It will query [0, 1) (Genesis).
  joiner.Advance();
  EXPECT_EQ(joiner.GetState(), SpendJoiner::State::QueriedPartial);

  // Fetch. Should fetch Genesis outputs.
  joiner.Advance();
  EXPECT_EQ(joiner.GetState(), SpendJoiner::State::FetchedPartial);

  // Now append Block 1 to DB.
  db.Append(*block1, 1);

  // Query again. Should query [1, 2) (Block 1).
  joiner.Advance();
  EXPECT_EQ(joiner.GetState(), SpendJoiner::State::Queried);

  // Fetch again. Should fetch Block 1 outputs.
  joiner.Advance();
  EXPECT_EQ(joiner.GetState(), SpendJoiner::State::Fetched);

  // Join.
  auto result = joiner.Join([](const consensus::SpendRecord&) { return consensus::Result{}; });
  EXPECT_EQ(joiner.GetState(), SpendJoiner::State::Joined);
  EXPECT_TRUE(result);
}

TEST(SpendJoinerTest, TestUnsortedFetchBug) {
  test::TempFolder dir;
  Database db{dir.Path()};
  test::Blockchain chain;

  constexpr int kLength = 20;

  int height;
  for (height = 0; height < kLength - 2; ++height) {
    chain.Append(chain.Sample());
    db.Append(*chain[height], height);
  }

  // Height kLength - 2
  chain.Append(chain.Sample());
  // Don't append to db yet, to force partial query/fetch.
  ++height;

  // Height kLength - 1
  chain.Append(chain.Sample());
  SpendJoiner joiner{db, chain[height], height};

  EXPECT_EQ(joiner.GetState(), SpendJoiner::State::Init);
  EXPECT_FALSE(joiner.IsJoinReady());
  EXPECT_TRUE(joiner.IsAdvanceReady());

  // Simulates a serial validation pipeline.
  while (joiner.IsAdvanceReady())
    joiner.Advance();  // Execute preemptive Append, then partial Query and Fetch.
  
  EXPECT_FALSE(joiner.IsJoinReady());

  db.Append(*chain[kLength - 2], kLength - 2);

  EXPECT_TRUE(joiner.IsAdvanceReady());
  joiner.Advance();  // Re-query
  EXPECT_TRUE(joiner.IsAdvanceReady());
  joiner.Advance();  // Re-fetch
  EXPECT_FALSE(joiner.IsAdvanceReady());
  EXPECT_TRUE(joiner.IsJoinReady());
}

TEST(SpendJoinerTest, TestPartialFetchMisalignment) {
  test::TempFolder dir;
  Database db{dir.Path()};

  // 1. Create Block 0 (Genesis) and append to DB.
  test::Blockchain chain;
  chain.Append(chain.Sample());
  db.Append(*chain[0], 0);
  
  const auto tx0 = chain[0]->Transaction(0);
  const OutputKey key0{tx0.GetHash(), 0};

  // 2. Create Block 1 such that its output key is "smaller" than Block 0's key.
  // This ensures that when sorted by Key, Block 1 comes first.
  // But when sorted by ID (append order), Block 0 comes first.
  std::shared_ptr<protocol::Block> block1;
  OutputKey key1;
  do {
    // Use Sample(1) to ensure we only get a coinbase transaction, so we don't spend key0.
    auto candidate = chain.Sample(1);
    const auto tx = candidate.Transaction(0);
    key1 = OutputKey{tx.GetHash(), 0};
    if (key1 < key0) {
      block1 = std::make_shared<protocol::Block>(std::move(candidate));
      break;
    }
  } while (true);
  chain.Append(protocol::Block(*block1));
  // Do NOT append Block 1 to DB yet.

  // 3. Create Block 2 spending both key0 and key1.
  auto block2_val = chain.Sample(10);
  // Manually construct inputs to ensure we spend exactly these two.
  // We don't care about the validity of the signature for this test, just the DB lookup.
  protocol::Transaction tx;
  tx.ResizeInputs(2);
  tx.Input(0).previous_output = key0;
  tx.Input(1).previous_output = key1;
  tx.ResizeOutputs(1);
  tx.Output(0).value = 100;
  
  auto block2 = std::make_shared<protocol::Block>();
  block2->AddTransaction(tx);

  // 4. Start SpendJoiner for Block 2.
  SpendJoiner joiner{db, block2, 2};
  joiner.Advance();  // Parse.
  joiner.Advance();  // Append.

  // Query 1: Partial. DB has Block 0 (Height 0). Block 1 is missing.
  // Query range [0, 1). Should find key0. key1 is not found.
  joiner.Advance();
  EXPECT_EQ(joiner.GetState(), SpendJoiner::State::QueriedPartial);

  // Fetch 1: Partial. Fetches data for key0.
  // outputs_ now has data for key0.
  joiner.Advance();
  EXPECT_EQ(joiner.GetState(), SpendJoiner::State::FetchedPartial);

  // 5. Append Block 1 to DB.
  db.Append(*block1, 1);

  joiner.Advance();
  EXPECT_EQ(joiner.GetState(), SpendJoiner::State::Queried);

  joiner.Advance();
  EXPECT_EQ(joiner.GetState(), SpendJoiner::State::Fetched);

  // Join. We expect:
  // Input spending key0 -> Funding Height 0.
  // Input spending key1 -> Funding Height 1.
  bool found_key0 = false;
  bool found_key1 = false;
  auto result = joiner.Join([&](const consensus::SpendRecord& spend) {
    if (spend.spend_input_index == 0) {
      // Spending key0. Should have height 0.
      EXPECT_EQ(spend.funding_height, 0) << "Input 0 (Key0) should have funding height 0";
      found_key0 = true;
    } else if (spend.spend_input_index == 1) {
      // Spending key1. Should have height 1.
      EXPECT_EQ(spend.funding_height, 1) << "Input 1 (Key1) should have funding height 1";
      found_key1 = true;
    }
    return consensus::Result{};
  });
  EXPECT_TRUE(found_key0);
  EXPECT_TRUE(found_key1);
}

}  // namespace
}  // namespace hornet::data::utxo
