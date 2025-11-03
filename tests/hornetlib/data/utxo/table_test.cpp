#include "hornetlib/data/utxo/table.h"

#include <gtest/gtest.h>

#include "testutil/temp_folder.h"

namespace hornet::data::utxo {
namespace {

TEST(TableTest, TestAppendGenesis) {
  test::TempFolder folder;
  Table table{folder.Path()};

  TiledVector<OutputKV> entries;
  const int outputs = table.AppendOutputs(protocol::Block::Genesis(), 0, &entries);

  EXPECT_EQ(outputs, 1);
  EXPECT_EQ(outputs, static_cast<int>(entries.Size()));

  std::vector<uint8_t> script;
  OutputDetail detail;
  const int records = table.Fetch({&entries[0].rid, 1}, {&detail, 1}, &script);
  EXPECT_EQ(records, outputs);

  EXPECT_EQ(detail.header.amount, 50ll * 100'000'000);
  EXPECT_EQ(detail.header.height, 0);
  
  const auto pkscript = detail.script.Span(script);
  EXPECT_EQ(pkscript.size(), 67);
}

}  // namespace
}  // namespace hornet::data::utxo
