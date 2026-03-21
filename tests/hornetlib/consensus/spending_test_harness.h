#pragma once

#include <memory>
#include <utility>

#include "hornetlib/consensus/rules/validate_spending.h"
#include "hornetlib/data/header_timechain.h"
#include "hornetlib/data/utxo/database.h"
#include "hornetlib/data/utxo/joiner.h"
#include "hornetlib/model/header_context.h"

#include "testutil/blockchain.h"
#include "testutil/temp_folder.h"

namespace hornet::test {

void FixMerkleRoot(protocol::Block& block);

Blockchain LoadValidationPipelineChain();

template <typename Callback>
consensus::Result WithCandidateSpendState(const Blockchain& chain, const protocol::Block& block, Callback&& callback) {
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

  return std::forward<Callback>(callback)(*ancestry, joiner, height);
}

}  // namespace hornet::test