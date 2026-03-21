#pragma once

#include <span>

#include "hornetlib/consensus/utxo.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus::rules::scripts {

// Returns the sum of sigop counts across all legacy input sig scripts and output pubkey scripts.
[[nodiscard]] int LegacySigOpCount(protocol::TransactionConstView tx);

// Returns the sum of sigop costs for a transaction, given spending information.
[[nodiscard]] int SigOpCost(protocol::TransactionConstView tx, std::span<const SpendRecord> spends, uint64_t flags);

}  // namespace hornet::consensus::rules::scripts
