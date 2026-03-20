#pragma once

#include <span>

#include "hornetlib/consensus/utxo.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus::rules::scripts {

// The legacy definition of transaction sigops is the sum of sigop counts
// across all input signature scripts and all output pkScripts.
[[nodiscard]] int LegacySigOpCount(protocol::TransactionConstView tx);

[[nodiscard]] int SigOpCost(protocol::TransactionConstView tx, std::span<const SpendRecord> spends, uint64_t flags);

}  // namespace hornet::consensus::rules::scripts
