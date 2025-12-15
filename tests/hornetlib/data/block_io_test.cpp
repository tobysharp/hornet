#include "hornetlib/data/block_io.h"

#include <gtest/gtest.h>

#include "hornetlib/consensus/validate_api.h"
#include "hornetlib/data/utxo/database.h"
#include "hornetlib/data/utxo/database_view.h"
#include "hornetlib/data/utxo/joiner.h"
#include "hornetlib/data/timechain.h"
#include "testutil/temp_folder.h"

namespace hornet::data {
namespace {

TEST(BlockIOTest, TestFileRead) {
  BlockReader reader{"/tmp/blocks.bin"};
  Timechain timechain;

  test::TempFolder folder;
  utxo::Database database(folder.Path());

  const int64_t current_time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  auto headers = timechain.WriteHeaders();
  for (auto block : reader.Blocks()) {
    const auto tip = headers->ChainTip();
    const auto ancestry = headers->GetValidationView(static_cast<HeaderTimechain::ConstIterator>(tip));
    
    const auto joiner = std::make_shared<utxo::SpendJoiner>(database, block, tip->height + 1);
    while (joiner->IsAdvanceReady())
      joiner->Advance();
    ASSERT_TRUE(joiner->IsJoinReady());

    const auto result = consensus::ValidateBlock(*block, tip->data, *ancestry, current_time, utxo::DatabaseView{joiner});
    EXPECT_EQ(result, consensus::Result::Ok);

    timechain.AddHeader(headers->ChainTip(),
                        headers->ChainTip()->Extend(block->Header()));
  }
  EXPECT_GT(headers->ChainLength(), 50);
}

}  // namespace
}  // namespace hornet::data