#include <filesystem>
#include <utility>

#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/validate_api.h"
#include "hornetlib/data/block_io.h"
#include "hornetlib/data/header_timechain.h"
#include "hornetlib/data/utxo/database.h"
#include "hornetlib/data/utxo/database_view.h"
#include "hornetlib/model/header_context.h"

#include "testutil/blockchain.h"
#include "testutil/data_path.h"
#include "testutil/temp_folder.h"

#include <gtest/gtest.h>

namespace hornet::test {

// Loads a blockchain from a file, validating each block in turn.
// This performs serial, single-threaded validation, intended for smallish test vectors and consensus debugging.
consensus::Result ValidateChain(const std::filesystem::path& path) {
  data::HeaderTimechain headers;
  test::TempFolder datadir;
  data::utxo::Database db{datadir.Path()};
  data::BlockReader reader{path};
  model::HeaderContext context;
  data::HeaderTimechain::ConstIterator tip;
  for (int height = 0; height < reader.Size(); ++height) {
    const auto block = reader[height];
    if (height > 0) {
      const auto ancestry = headers.GetValidationView(tip);
      const auto current_time =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
              .count();
      const auto joiner = std::make_shared<data::utxo::SpendJoiner>(db, block, height);
      while (joiner->IsAdvanceReady()) joiner->Advance();
      const data::utxo::DatabaseView utxo{joiner};
      const auto result =
          consensus::ValidateBlock(*block, headers.ChainElement(height - 1), *ancestry, current_time, utxo);
      if (!result) {
        // For completeness, rewind the database to the last good block.
        db.EraseSince(height);
        return result;
      }
    }
    tip = headers.Add(context = context.Extend(block->Header())).it;
  }
  return consensus::Result::Ok;
}

void EnsureTestVectorExists(const std::filesystem::path& path, auto&& generate) {
  if (!std::filesystem::exists(path)) {
    test::Blockchain data = generate();
    data.Save(path.string() + ".nopow");
    FAIL() << "Test file \"" << path << "\" was missing. Run tools/minetests.sh then re-run test.";
  }
}

consensus::Result TestValidateChain(auto&& generate) {
  const auto path = test::CurrentTestVectorPath();
  EnsureTestVectorExists(path, std::forward<decltype(generate)>(generate));
  return ValidateChain(path);
}

void ExpectValidationResult(std::function<test::Blockchain()> generate,
                            consensus::Result expected /* = consensus::Result::Ok */) {
  EXPECT_EQ(TestValidateChain(std::move(generate)), expected);
}

}  // namespace hornet::test
