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

}  // namespace
}  // namespace hornet::data::utxo
