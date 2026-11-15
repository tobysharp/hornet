#include "hornetlib/data/utxo/table.h"

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "testutil/blockchain.h"
#include "testutil/temp_folder.h"
#include "testutil/transaction.h"

namespace hornet::data::utxo {
namespace {

TEST(TableTest, TestFetchFromTail) {
  test::TempFolder folder;
  Table table{folder.Path()};
  table.SetMutableWindow(1);  // Keep the table data in the mutable tail.

  TiledVector<OutputKV> entries;
  const int outputs = table.AppendOutputs(protocol::Block::Genesis(), 0, &entries);

  EXPECT_EQ(outputs, 1);
  EXPECT_EQ(outputs, static_cast<int>(entries.Size()));

  std::vector<uint8_t> script;
  OutputDetail detail;
  const int records = table.Fetch({&entries[0].rid, 1}, {&detail, 1}, &script);
  const auto pk_script = detail.script.Span(script);
  
  EXPECT_EQ(records, outputs);
  EXPECT_EQ(detail.header.amount, 50ll * 100'000'000);
  EXPECT_EQ(detail.header.height, 0);
  EXPECT_EQ(pk_script.size(), 67);
}

TEST(TableTest, TestFetchFromSegments) {
  test::TempFolder folder;
  Table table{folder.Path()};
  table.SetMutableWindow(0);

  TiledVector<OutputKV> entries;
  const int outputs = table.AppendOutputs(protocol::Block::Genesis(), 0, &entries);

  EXPECT_EQ(outputs, 1);
  EXPECT_EQ(outputs, static_cast<int>(entries.Size()));

  // Wait for background flusher to commit from table tail to file segment.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::vector<uint8_t> script;
  OutputDetail detail;
  const int records = table.Fetch({&entries[0].rid, 1}, {&detail, 1}, &script);
  const auto pk_script = detail.script.Span(script);
  
  EXPECT_EQ(records, outputs);
  EXPECT_EQ(detail.header.amount, 50ll * 100'000'000);
  EXPECT_EQ(detail.header.height, 0);
  EXPECT_EQ(pk_script.size(), 67);
}

TEST(TableTest, TestFetchWithNullIds) {
  test::TempFolder folder;
  Table table{folder.Path()};
  table.SetMutableWindow(1);

  TiledVector<OutputKV> entries;
  table.AppendOutputs(protocol::Block::Genesis(), 0, &entries);

  std::vector<OutputId> rids = {kNullOutputId, entries[0].rid, };

  std::vector<OutputDetail> details(2);
  std::vector<uint8_t> scripts;
  int fetched = table.Fetch(rids, details, &scripts);
  EXPECT_EQ(fetched, 1);

  EXPECT_EQ(details[0].header.amount, 0);
  EXPECT_GT(details[1].header.amount, 0);
}

TEST(TableTest, TestPartialFetch) {
  test::TempFolder folder;
  Table table{folder.Path()};
  table.SetMutableWindow(1);

  test::Blockchain chain;
  
  // Create a block with multiple outputs at height 1.
  auto block1 = chain.Sample(10); 
  chain.Append(std::move(block1));

  TiledVector<OutputKV> entries;
  table.AppendOutputs(*chain[1], 1, &entries);

  std::vector<OutputId> all_rids;
  for(size_t i = 0; i < entries.Size(); ++i) {
      all_rids.push_back(entries[i].rid);
  }
  
  // Create two disjoint sets of IDs.
  std::vector<OutputId> set1, set2;
  for(size_t i = 0; i < all_rids.size(); ++i) {
      if ((i & 1) == 0) set1.push_back(all_rids[i]);
      else set2.push_back(all_rids[i]);
  }
  
  // Prepare rids for Fetch 1: set1 + Nulls for set2
  // In SpendJoiner, rids are nulled out after fetch. 
  // Here we simulate the state where we have a vector of IDs, some valid (found in current query), some null (already fetched or not found).
  std::vector<OutputId> rids1 = set1;
  rids1.resize(all_rids.size(), kNullOutputId); // Pad with Nulls
  Table::SortIds(rids1);
  
  // Prepare rids for Fetch 2: set2 + Nulls for set1
  std::vector<OutputId> rids2 = set2;
  rids2.resize(all_rids.size(), kNullOutputId); // Pad with Nulls
  Table::SortIds(rids2);
  
  // Prepare rids for Fetch All
  std::vector<OutputId> rids_all = all_rids;
  Table::SortIds(rids_all);
  
  // Fetch 1
  std::vector<OutputDetail> details1(rids1.size());
  std::vector<uint8_t> scripts1;
  int fetched1 = table.Fetch(rids1, details1, &scripts1);
  EXPECT_EQ(fetched1, static_cast<int>(set1.size()));
  
  // Fetch 2
  std::vector<OutputDetail> details2(rids2.size());
  std::vector<uint8_t> scripts2;
  int fetched2 = table.Fetch(rids2, details2, &scripts2);
  EXPECT_EQ(fetched2, static_cast<int>(set2.size()));
  
  // Fetch All
  std::vector<OutputDetail> details_all(rids_all.size());
  std::vector<uint8_t> scripts_all;
  int fetched_all = table.Fetch(rids_all, details_all, &scripts_all);
  EXPECT_EQ(fetched_all, static_cast<int>(all_rids.size()));
}

// A block whose outputs are all provably unspendable contributes no bytes to the table. The blocks after it must
// still commit to segments and remain fetchable.
TEST(TableTest, CommitsPastBlockWithNoAdmittedOutputs) {
  test::TempFolder folder;
  Table table{folder.Path()};
  table.SetMutableWindow(100);  // Hold everything in the tail; commits are driven manually below.

  const auto make_block = [](uint8_t opcode) {
    protocol::Block block;
    auto tx = test::MakeCoinbaseLikeTransaction();
    tx.SetPkScript(0, std::vector<uint8_t>{opcode});
    block.AddTransaction(tx);
    return block;
  };

  TiledVector<OutputKV> entries;
  EXPECT_EQ(table.AppendOutputs(make_block(0x51), 1, &entries), 1);
  EXPECT_EQ(table.AppendOutputs(make_block(0x6a), 2, &entries), 0);  // OP_RETURN only: nothing admitted.
  EXPECT_EQ(table.AppendOutputs(make_block(0x52), 3, &entries), 1);
  EXPECT_EQ(table.AppendOutputs(make_block(0x53), 4, &entries), 1);
  ASSERT_EQ(entries.Size(), 3u);

  const auto expect_fetch = [&](int entry, int height, uint8_t opcode) {
    std::vector<uint8_t> script;
    OutputDetail detail;
    ASSERT_EQ(table.Fetch({&entries[entry].rid, 1}, {&detail, 1}, &script), 1);
    const auto pk_script = detail.script.Span(script);
    EXPECT_EQ(detail.header.height, height);
    ASSERT_EQ(pk_script.size(), 1u);
    EXPECT_EQ(pk_script[0], opcode);
  };

  table.CommitBefore(4);  // Heights 1 to 3 leave the tail.
  expect_fetch(0, 1, 0x51);
  expect_fetch(1, 3, 0x52);
  expect_fetch(2, 4, 0x53);

  table.CommitBefore(5);  // Height 4 follows.
  expect_fetch(1, 3, 0x52);
  expect_fetch(2, 4, 0x53);
}

}  // namespace
}  // namespace hornet::data::utxo
