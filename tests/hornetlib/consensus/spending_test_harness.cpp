#include "hornetlib/consensus/spending_test_harness.h"

#include <memory>
#include <utility>

#include "hornetlib/consensus/merkle.h"
#include "hornetlib/data/header_timechain.h"
#include "hornetlib/data/utxo/database.h"
#include "hornetlib/data/utxo/database_view.h"
#include "hornetlib/data/utxo/joiner.h"
#include "hornetlib/model/header_context.h"

#include "testutil/data_path.h"
#include "testutil/temp_folder.h"

namespace hornet::test {

void FixMerkleRoot(protocol::Block& block) {
  auto header = block.Header();
  header.SetMerkleRoot(consensus::ComputeMerkleRoot(block).hash);
  block.SetHeader(header);
}

Blockchain LoadValidationPipelineChain() {
  Blockchain chain;
  chain.Load(test::GetDataPath("ValidationPipelineTest_ProcessBlocks.bin"));
  return chain;
}

consensus::Result EvaluateCandidateTransactions(const Blockchain& chain, const protocol::Block& block,
                                                TransactionSpendCallback callback) {
  data::HeaderTimechain headers;
  TempFolder datadir;
  data::utxo::Database db{datadir.Path()};
  model::HeaderContext context;
  data::HeaderTimechain::ConstIterator tip;

  for (int height = 0; height < chain.Length(); ++height) {
    tip = headers.Add(context = context.Extend(chain[height]->Header())).it;
    if (height > 0) db.Append(*chain[height], height);
  }

  const int height = chain.Length();
  const auto ancestry = headers.GetValidationView(tip);
  auto joiner = std::make_shared<data::utxo::SpendJoiner>(db, std::make_shared<const protocol::Block>(block), height);

  while (joiner->IsAdvanceReady()) joiner->Advance();
  if (!joiner->IsJoinReady()) return consensus::Error::Spending_PrevoutNotUnspent;

  return joiner->Join([&](const protocol::TransactionConstView& tx, std::span<const consensus::SpendRecord> spends) {
    return callback(tx, spends, *ancestry, height);
  });
}

consensus::Result EvaluateCandidateBlock(const Blockchain& chain, const protocol::Block& block,
                                         BlockSpendCallback callback) {
  data::HeaderTimechain headers;
  TempFolder datadir;
  data::utxo::Database db{datadir.Path()};
  model::HeaderContext context;
  data::HeaderTimechain::ConstIterator tip;

  for (int height = 0; height < chain.Length(); ++height) {
    tip = headers.Add(context = context.Extend(chain[height]->Header())).it;
    if (height > 0) db.Append(*chain[height], height);
  }

  const int height = chain.Length();
  const auto ancestry = headers.GetValidationView(tip);
  auto joiner = std::make_shared<data::utxo::SpendJoiner>(db, std::make_shared<const protocol::Block>(block), height);

  while (joiner->IsAdvanceReady()) joiner->Advance();
  if (!joiner->IsJoinReady()) return consensus::Error::Spending_PrevoutNotUnspent;

  const data::utxo::DatabaseView utxo{joiner};
  const consensus::rules::BlockValidationContext validation{block, chain[height - 1]->Header(), *ancestry, 0, utxo};
  return callback(consensus::rules::MakeBlockSpendContext(validation));
}

}  // namespace hornet::test