#include "hornetlib/data/utxo/table.h"

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "testutil/temp_folder.h"

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

}  // namespace
}  // namespace hornet::data::utxo
