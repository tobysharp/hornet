#pragma once

#include <functional>

#include "hornetlib/consensus/rules/validate_spending.h"

#include "testutil/blockchain.h"

namespace hornet::test {

void FixMerkleRoot(protocol::Block& block);

Blockchain LoadValidationPipelineChain();

using TransactionSpendCallback =
    std::function<consensus::Result(const protocol::TransactionConstView&, std::span<const consensus::SpendRecord>,
                                    const consensus::HeaderAncestryView&, int)>;

consensus::Result EvaluateCandidateTransactions(const Blockchain& chain, const protocol::Block& block,
                                                TransactionSpendCallback callback);

using BlockSpendCallback = std::function<consensus::Result(const consensus::rules::BlockSpendContext&)>;

consensus::Result EvaluateCandidateBlock(const Blockchain& chain, const protocol::Block& block,
                                         BlockSpendCallback callback);

}  // namespace hornet::test