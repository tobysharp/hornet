#include "hornetlib/data/utxo/database.h"

#include <array>
#include <chrono>
#include <random>
#include <span>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "testutil/blockchain.h"
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
    database.Append(*chain[i], i);
  
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

TEST(DatabaseTest, TestPipeline_InOrderSerial) {
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
      EXPECT_TRUE(pk_script.size() == 24u || detail.header.height == 0);
    }
    int64_t supply_growth = total_spend - total_funding;
    EXPECT_EQ(supply_growth, 50ll * 100'000'000);  // Every test block adds 50 BTC to supply.
  
    // Update the UTXO database.
    database.Append(block, height);

    // After appending the new block, all the keys that were previously funded should now be spent.
    std::fill(rids.begin(), rids.end(), kNullOutputId);
    queried = database.Query(keys, rids, 0, height + 1);
    EXPECT_EQ(queried.funded, 0);
    EXPECT_EQ(queried.spent, keys.size());
    for (OutputId rid : rids) EXPECT_EQ(rid, kSpentOutputId);

    // Update the test chain.
    chain.Append(std::move(block));
  }
}

TEST(DatabaseTest, TestPipeline_PreemptiveSerial) {
  constexpr int kLength = 100;
  constexpr int kMaxTransactions = 10;

  test::TempFolder dir;
  Database database{dir.Path()};

  test::Blockchain chain;
  for (int height = 0; height < kLength; ++height) {
    // Generate a new block to propose for the test chain.
    auto block = chain.Sample(kMaxTransactions);

    // Start by preemptively appending the block the UTXO database, 
    // anticipating that all spends will prove valid.
    database.Append(block, height);
    
    // Query all input prevouts at the previous height to check they were indeed unspent.
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
      EXPECT_TRUE(pk_script.size() == 24u || detail.header.height == 0);
    }
    int64_t supply_growth = total_spend - total_funding;
    EXPECT_EQ(supply_growth, 50ll * 100'000'000);  // Every test block adds 50 BTC to supply.

    // After appending the new block, all the keys that were previously funded should now be spent.
    std::fill(rids.begin(), rids.end(), kNullOutputId);
    queried = database.Query(keys, rids, 0, height + 1);
    EXPECT_EQ(queried.funded, 0);
    EXPECT_EQ(queried.spent, keys.size());
    for (OutputId rid : rids) EXPECT_EQ(rid, kSpentOutputId);

    // Update the test chain.
    chain.Append(std::move(block));
  }
}

TEST(DatabaseTest, TestAppend_OutOfOrderSerial) {
  constexpr int kLength = 100;
  constexpr int kMaxTransactions = 10;

  test::TempFolder dir;
  Database database{dir.Path()};

  // Create the chain as though known to a peer.
  test::Blockchain chain;
  for (int height = 0; height < kLength; ++height)
    chain.Append(chain.Sample(kMaxTransactions));

  for (int i = 0; i < kLength; i += 2) {
    database.Append(*chain[i + 1], i + 1);
    database.Append(*chain[i], i);
  }
}

TEST(DatabaseTest, TestPipeline_UnorderedSerial) {
  constexpr int kLength = 100;
  constexpr int kMaxTransactions = 10;

  test::TempFolder dir;
  Database database{dir.Path()};

  // Create the chain as though known to a peer.
  test::Blockchain chain;
  for (int height = 0; height < kLength; ++height)
    chain.Append(chain.Sample(kMaxTransactions));

  std::vector<OutputKey> prev_keys;
  std::vector<OutputId> prev_rids;
  bool incomplete = false;
  QueryResult prev_query;

  for (int i = 0; i < kLength; i += 2) {
    // Usually we would append and process the even block (0) followed by the odd block (1).
    // In this test we process them in the opposite order to check out-of-order operation.
    
    // Block i+1 arrives before block i, and we process it as far as possible.
    {
      const int height = i + 1;
      const auto& block = *chain[height];

      // Start by preemptively appending the block the UTXO database, 
      // anticipating that all spends will prove valid.
      database.Append(block, height);
    
      // Partial query all input prevouts before height - 1 to check they were indeed unspent.
      std::vector<OutputKey> keys = database.ExtractSpentKeys(block);
      std::vector<OutputId> rids(keys.size(), kNullOutputId);
      database.SortKeys(keys);
      auto queried = database.Query(keys, rids, 0, height - 1);
      EXPECT_LE(queried.funded, keys.size());
      EXPECT_EQ(queried.spent, 0);

      // Cannot process block i+1 further until block i arrives. Save for later.
      incomplete = queried.funded < std::ssize(keys);
      prev_keys = std::move(keys);
      prev_rids = std::move(rids);
      prev_query = std::move(queried);
    }

    // Later, block i arrives out of order.
    {
      const int height = i;
      const auto& block = *chain[height];

      // Preemptive out-of-order append.
      database.Append(block, height);
      
      // Full query since we have all contiguous blocks up to this point.
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
        EXPECT_TRUE(pk_script.size() == 24u || detail.header.height == 0);
      }
      int64_t supply_growth = total_spend - total_funding;
      EXPECT_EQ(supply_growth, 50ll * 100'000'000);  // Every test block adds 50 BTC to supply.

      // After appending the new block, all the keys that were previously funded should now be spent.
      std::fill(rids.begin(), rids.end(), kNullOutputId);
      queried = database.Query(keys, rids, 0, height + 1);
      EXPECT_EQ(queried.funded, 0);
      EXPECT_EQ(queried.spent, keys.size());
      for (OutputId rid : rids) EXPECT_EQ(rid, kSpentOutputId);
    }

    // Finally, we can return to finishing the odd height that was begun first.
    {
      int height = i + 1;
      const auto& block = *chain[height];
      
      std::vector<OutputKey> keys(std::move(prev_keys));
      std::vector<OutputId> rids(std::move(prev_rids));
      QueryResult queried = prev_query;

      // Remainder query.
      if (incomplete) {
        queried += database.Query(keys, rids, height - 1, height);
        EXPECT_EQ(queried.funded, keys.size());
        EXPECT_EQ(queried.spent, 0);
      }

      // All rid's should now be valid for this block's funding.
      for (OutputId rid : rids) {
        EXPECT_NE(rid, kNullOutputId);
        EXPECT_NE(rid, kSpentOutputId);
      }

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
        EXPECT_TRUE(pk_script.size() == 24u || detail.header.height == 0);
      }
      int64_t total_spend = 0;  // Total block output
      for (const auto tx : block.Transactions())
        for (const auto& output : tx.Outputs())
          total_spend += output.value;
      int64_t supply_growth = total_spend - total_funding;
      EXPECT_EQ(supply_growth, 50ll * 100'000'000);  // Every test block adds 50 BTC to supply.

      // After appending the new block, all the keys that were previously funded should now be spent.
      std::fill(rids.begin(), rids.end(), kNullOutputId);
      queried = database.Query(keys, rids, 0, height + 1);
      EXPECT_EQ(queried.funded, 0);
      EXPECT_EQ(queried.spent, keys.size());
      for (OutputId rid : rids) EXPECT_EQ(rid, kSpentOutputId);
    }
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
    database.Append(*chain[height], height);
  
    {
      // Query for the most recent block's coinbase output, which should exist at the current height.
      std::vector<OutputKey> keys(1, {chain[height]->Transaction(0).GetHash(), 0u});
      std::vector<OutputId> rids(1, kNullOutputId);
      int query = database.Query(keys, rids, height + 1);
      EXPECT_EQ(query, 1);
      EXPECT_NE(rids[0], kNullOutputId);
    }

    {
      // Similary perform the same query at one specific height.
      std::vector<OutputKey> keys(1, {chain[height]->Transaction(0).GetHash(), 0u});
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
    database.Append(*chain[i], i);
  });
}

TEST(DatabaseTest, TestFetchWithNullIds) {
  test::TempFolder dir;
  Database database{dir.Path()};
  database.Append(protocol::Block::Genesis(), 0);

  // We need to get the OutputId of the genesis output.
  auto block = protocol::Block::Genesis();
  std::vector<OutputKey> keys = { {(*block.Transactions().begin()).GetHash(), 0}, {} };
  std::vector<OutputId> rids(keys.size());
  database.SortKeys(keys);
  int queried = database.Query(keys, rids, 1);
  EXPECT_EQ(queried, 1);

  database.SortIds(rids);
  std::vector<OutputDetail> outputs(rids.size());
  std::vector<uint8_t> scripts;
  int fetched = database.Fetch(rids, outputs, &scripts);
  EXPECT_EQ(fetched, 1);
  EXPECT_GT(outputs[1].header.amount, 0);
}

TEST(DatabaseTest, TestPartialQueryAndFetch) {
  test::TempFolder dir;
  Database database{dir.Path()};
  database.SetMutableWindow(2);

  // Create a chain with 2 blocks (Genesis + 1).
  test::Blockchain chain;
  chain.Append(chain.Sample()); // Genesis
  
  // Use Sample(1) to ensure block1 only has a coinbase and doesn't spend Genesis outputs.
  // This simplifies the test logic regarding spent outputs.
  auto block1 = chain.Sample(1);
  chain.Append(std::move(block1));

  // Append both to DB.
  database.Append(*chain[0], 0);
  database.Append(*chain[1], 1);

  // We want to query inputs for a hypothetical block that spends outputs from both Genesis and Block 1.
  std::vector<OutputKey> keys;
  keys.push_back({chain[0]->Transaction(0).GetHash(), 0}); // From Genesis
  keys.push_back({chain[1]->Transaction(0).GetHash(), 0}); // From Block 1
  std::vector<int> heights = {0, 1};

  SortTogether(keys.begin(), keys.end(), heights.begin());
  std::vector<OutputId> rids(keys.size(), kNullOutputId);
  std::vector<OutputDetail> outputs(keys.size());

  // 1. Partial Query: Query only up to height 1 (exclusive of Block 1).
  // This should find the Genesis output but not the Block 1 output.
  auto result1 = database.Query(keys, rids, 0, 1);
  EXPECT_EQ(result1.funded, 1);
  EXPECT_EQ(result1.spent, 0);
  SortTogether(heights.begin(), heights.end(), keys.begin(), rids.begin(), outputs.begin());

  EXPECT_NE(rids[0], kNullOutputId);
  EXPECT_EQ(rids[1], kNullOutputId);

  // 2. Partial Fetch: Fetch what we found.
  EXPECT_TRUE(outputs[0].header.IsNull());
  EXPECT_TRUE(outputs[1].header.IsNull());
  std::vector<uint8_t> scripts;
  SortTogether(rids.begin(), rids.end(), heights.begin(), keys.begin());
  int fetched1 = database.Fetch(rids, outputs, &scripts);
  EXPECT_EQ(fetched1, 1);
  SortTogether(heights.begin(), heights.end(), keys.begin(), rids.begin(), outputs.begin());

  // Verify we got the Genesis output.
  EXPECT_EQ(outputs[0].header.height, 0);
  EXPECT_GT(outputs[0].header.amount, 0);
  EXPECT_TRUE(outputs[1].header.IsNull());

  // 3. Remainder Query: Query from height 1 to 2.
  // This should find the Block 1 output.
  SortTogether(keys.begin(), keys.end(), heights.begin(), rids.begin(), outputs.begin());
  auto result2 = database.Query(keys, rids, 1, 2);
  EXPECT_EQ(result2.funded, 1);
  EXPECT_EQ(result2.spent, 0);

  // Now we should have both IDs in rids.
  EXPECT_NE(rids[0], kNullOutputId);
  EXPECT_NE(rids[1], kNullOutputId);

  // 4. Remainder Fetch: Fetch again.
  SortTogether(rids.begin(), rids.end(), heights.begin(), keys.begin(), outputs.begin());
  int fetched2 = database.Fetch(rids, outputs, &scripts);
  EXPECT_EQ(fetched2, 1);

  SortTogether(heights.begin(), heights.end(), keys.begin(), rids.begin(), outputs.begin());
  EXPECT_EQ(outputs[0].header.height, 0);
  EXPECT_GT(outputs[0].header.amount, 0);
  EXPECT_EQ(outputs[1].header.height, 1);
  EXPECT_GT(outputs[1].header.amount, 0);

  EXPECT_FALSE(outputs[0].header.IsNull());
  EXPECT_FALSE(outputs[1].header.IsNull());
}

}  // namespace
}  // namespace hornet::data::utxo
