#include "hornetlib/data/utxo/database.h"

#include <array>
#include <chrono>
#include <random>
#include <span>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "hornetlib/data/utxo/blockchain.h"
#include "hornetlib/data/utxo/sort.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/script/writer.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/log.h"
#include "testutil/temp_folder.h"

namespace hornet::data::utxo {
namespace {

TEST(DatabaseTest, TestAppendGenesis) {
  test::TempFolder dir;
  Database database{dir.Path()};
  database.Append(protocol::Block::Genesis(), 0);
}

TEST(DatabaseTest, TestSpentOutputsNotFound_MutableSerial) {
  // Generates a test chain.
  test::Blockchain chain = test::Blockchain::Generate(3, 3);

  // Constructs a UTXO database.
  test::TempFolder dir;
  Database database{dir.Path()};
  database.SetMutableWindow(chain.Length());

  // Appends the blocks to the database, serially in order.
  for (int i = 0; i < chain.Length(); ++i)
    database.Append(chain[i], i);
  
  // Query everything that was spent already and check it is unfound.
  std::vector<OutputKey> keys(chain.SpentSize());
  for (int i = 0; i < chain.SpentSize(); ++i)
    keys[i] = chain.Spent(i).prevout;
  database.SortKeys(keys);
  std::vector<OutputId> rids(keys.size(), kNullOutputId);
  LogDebug() << "Query keys: ";
  for (const auto& key : keys)
    LogDebug() << "   key: {" << key.hash << ", " << key.index << "}";
  int query = database.Query(keys, rids, chain.Length());
  EXPECT_EQ(query, 0);
  for (auto rid : rids) EXPECT_EQ(rid, kSpentOutputId);
}

TEST(DatabaseTest, TestValidateUnspent_InOrderSerial) {
  constexpr int kLength = 100;
  constexpr int kMaxTransactions = 10;

  test::TempFolder dir;
  Database database{dir.Path()};

  test::Blockchain chain;
  for (int height = 0; height < kLength; ++height) {
    // Generate a new block to propose for the test chain.
    auto block = chain.Sample(kMaxTransactions);

    // Query all input prevouts to check they are unspent.
    std::vector<OutputKey> keys = database.ExtractSpentKeys(block);
    std::vector<OutputId> rids(keys.size(), kNullOutputId);
    database.SortKeys(keys);
    auto result = database.Query(keys, rids, 0, height);
    EXPECT_EQ(result.funded, keys.size());
    EXPECT_EQ(result.spent, 0);

    // Update the UTXO database.
    database.Append(block, height);

    // Update the test chain.
    chain.Append(std::move(block));
  }
}

TEST(DatabaseTest, TestValidateFetch_InOrderSerial) {
  constexpr int kLength = 100;
  constexpr int kMaxTransactions = 10;

  test::TempFolder dir;
  Database database{dir.Path()};

  test::Blockchain chain;
  for (int height = 0; height < kLength; ++height) {
    // Generate a new block to propose for the test chain.
    auto block = chain.Sample(kMaxTransactions);

    // Query all input prevouts to check they are unspent.
    std::vector<OutputKey> keys = database.ExtractSpentKeys(block);
    std::vector<OutputId> rids(keys.size(), kNullOutputId);
    int64_t total_spend = 0;  // Total block output
    for (const auto tx : block.Transactions())
      for (const auto& output : tx.Outputs())
        total_spend += output.value;
    database.SortKeys(keys);
    auto queried = database.Query(keys, rids, 0, height);
    EXPECT_EQ(queried.funded, keys.size());
    EXPECT_EQ(queried.spent, 0);
    for (OutputId rid : rids) EXPECT_NE(rid, kNullOutputId);

    // Fetch all funding output data and validate.
    std::vector<OutputDetail> outputs(keys.size());
    std::vector<uint8_t> scripts;
    database.SortIds(rids);
    int fetched = database.Fetch(rids, outputs, &scripts);
    EXPECT_EQ(fetched, rids.size());
    int64_t total_funding = 0;
    for (const auto& detail : outputs) {
      total_funding += detail.header.amount;
      EXPECT_LT(detail.header.height, height);
      const auto pk_script = detail.script.Span(scripts);
      EXPECT_EQ(pk_script.size(), 24u);
      EXPECT_EQ(pk_script.front(), 0x01);
      EXPECT_EQ(pk_script.back(), 0x18);
    }
    int64_t supply_growth = total_spend - total_funding;
    EXPECT_EQ(supply_growth, 50ll * 100'000'000);  // Every test block adds 50 BTC to supply.
  
    // Update the UTXO database.
    database.Append(block, height);

    // Update the test chain.
    chain.Append(std::move(block));
  }
}

TEST(DatabaseTest, TestAppendMutableSerial) {
  // Generates a test chain.
  constexpr int kBlocks = 3;
  test::Blockchain chain = test::Blockchain::Generate(kBlocks, 3);

  // Constructs a UTXO database.
  test::TempFolder dir;
  Database database{dir.Path()};
  database.SetMutableWindow(chain.Length());

  // Appends the blocks to the database, serially in order.
  for (int height = 0; height < chain.Length(); ++height) {
    database.Append(chain[height], height);
  
    {
      // Query for the most recent block's coinbase output, which should exist at the current height.
      std::vector<OutputKey> keys(1, {chain[height].Transaction(0).GetHash(), 0u});
      std::vector<OutputId> rids(1, kNullOutputId);
      int query = database.Query(keys, rids, height + 1);
      EXPECT_EQ(query, 1);
      EXPECT_NE(rids[0], kNullOutputId);
    }

    {
      // Similary perform the same query at one specific height.
      std::vector<OutputKey> keys(1, {chain[height].Transaction(0).GetHash(), 0u});
      std::vector<OutputId> rids(1, kNullOutputId);
      auto query = database.Query(keys, rids, height, height + 1);
      EXPECT_EQ(query.funded, 1);
      EXPECT_EQ(query.spent, 0);
      EXPECT_NE(rids[0], kNullOutputId);
    }
  }

  {
    // Queries for all unspent outputs at the chain tip.
    std::vector<OutputKey> keys(chain.UnspentSize());
    std::vector<OutputId> rids(keys.size(), kNullOutputId);
    for (int i = 0; i < std::ssize(keys); ++i) keys[i] = chain.Unspent(i).prevout;
    database.SortKeys(keys);
    int query = database.Query(keys, rids, chain.Length());
    EXPECT_EQ(query, chain.UnspentSize());
    for (int i = 0; i < std::ssize(keys); ++i)
      EXPECT_NE(rids[i], kNullOutputId);

    // Fetches all unspent output data.
    std::vector<OutputDetail> outputs(rids.size());
    std::vector<uint8_t> scripts;
    database.SortIds(rids);
    int fetched = database.Fetch(rids, outputs, &scripts);
    EXPECT_EQ(fetched, std::ssize(rids));
    EXPECT_LE(scripts.size(), 24u * fetched);
  
    // Verify the conservation of value of all unspent transactions.
    int64_t total = 0;
    for (const OutputDetail& detail : outputs) total += detail.header.amount;
    EXPECT_EQ(total, kBlocks * 50ll * 100'000'000);
  }

  // Query everything that was spent already and check it is unfound.
  {
    std::vector<OutputKey> keys(chain.SpentSize());
    for (int i = 0; i < chain.SpentSize(); ++i)
      keys[i] = chain.Spent(i).prevout;
    database.SortKeys(keys);
    std::vector<OutputId> rids(keys.size(), kNullOutputId);
    LogDebug() << "Query keys: ";
    for (const auto& key : keys)
      LogDebug() << "   key: {" << key.hash << ", " << key.index << "}";
    int query = database.Query(keys, rids, chain.Length());
    EXPECT_EQ(query, 0);
    for (auto rid : rids) EXPECT_EQ(rid, kSpentOutputId);
  }
}

TEST(DatabaseTest, TestAppendGeneratedParallel) {
  // Generates a test chain.
  constexpr int kBlocks = 100;
  test::Blockchain chain = test::Blockchain::Generate(kBlocks);

  // Constructs a UTXO database.
  test::TempFolder dir;
  Database database{dir.Path()};

  // Appends the blocks to the database.
  ParallelFor(0, chain.Length(), [&](int i) {
    database.Append(chain[i], i);
  });
}

}  // namespace
}  // namespace hornet::data::utxo
