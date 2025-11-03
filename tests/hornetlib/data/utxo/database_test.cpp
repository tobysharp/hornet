#include "hornetlib/data/utxo/database.h"

#include <gtest/gtest.h>

#include "hornetlib/protocol/block.h"
#include "testutil/temp_folder.h"

namespace hornet::data::utxo {
namespace {

TEST(DatabaseTest, TestAppend) {
  test::TempFolder dir;
  Database database{dir.Path()};

  const auto& genesis = protocol::Block::Genesis();
  database.Append(genesis, 0);
}

}  // namespace
}  // namespace hornet::data::utxo
