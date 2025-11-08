#include "hornetlib/data/utxo/database.h"

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "hornetlib/protocol/block.h"
#include "testutil/temp_folder.h"

namespace hornet::data::utxo {
namespace {

TEST(DatabaseTest, TestAppendGenesis) {
  test::TempFolder dir;
  Database database{dir.Path()};
  database.Append(protocol::Block::Genesis(), 0);
}

}  // namespace
}  // namespace hornet::data::utxo
