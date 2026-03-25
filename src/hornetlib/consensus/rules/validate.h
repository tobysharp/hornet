#pragma once

#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/consensus/rules/context.h"
#include "hornetlib/consensus/rules/validate_contextual.h"
#include "hornetlib/consensus/rules/validate_header.h"
#include "hornetlib/consensus/rules/validate_local.h"
#include "hornetlib/consensus/rules/validate_spending.h"
#include "hornetlib/consensus/rules/validate_transaction.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus::rules {

// Header Rules
[[nodiscard]] inline Result ValidateHeader(const HeaderValidationContext& context) {
  // clang-format off
  static const auto ruleset = std::make_tuple(
    Rule{ValidatePreviousHash},         // A header MUST reference the hash of its valid parent.
    Rule{ValidateProofOfWork},          // A header MUST satisfy the chain's target proof-of-work.
    Rule{ValidateDifficultyAdjustment}, // A header's proof-of-work target MUST satisfy the difficulty adjustment formula.
    Rule{ValidateMedianTimePast},       // A header timestamp MUST be strictly greater than the median of its 11 ancestors' timestamps.
    Rule{ValidateTimestampCurrent},     // A header timestamp MUST be less than or equal to network-adjusted time plus 2 hours.
    Rule{ValidateVersion}               // A header version number MUST meet deployment requirements depending on activated BIPs.
  );
  // clang-format on
  return ValidateRules(ruleset, 0, context);
}

[[nodiscard]] inline Result ValidateTransaction(const protocol::TransactionConstView tx) {
  const auto ruleset = std::make_tuple(
    Rule{ValidateInputCount},             // A transaction MUST contain at least one input.
    Rule{ValidateOutputCount},            // A transaction MUST contain at least one output.
    Rule{ValidateTransactionSize},        // A transaction's serialized size (excluding witness data) MUST NOT exceed 1,000,000 bytes.
    Rule{ValidateOutputsNonNegative},     // A transaction output amount MUST be non-negative.
    Rule{ValidateOutputsSum},             // The sum of a transaction's output amounts MUST NOT exceed 21,000,000 coins.
    Rule{ValidateUniqueInputs},           // A transaction's inputs MUST reference distinct outpoints (no duplicates).
    Rule{ValidateCoinbaseSignatureSize},  // In a coinbase transaction, the scriptSig MUST be between 2 and 100 bytes inclusive.
    Rule{ValidateInputsPrevout}           // A non-coinbase transaction's inputs MUST have non-null prevout values.    
  );
  return ValidateRules(ruleset, 0, tx);
}

// Local Rules
[[nodiscard]] inline Result ValidateLocal(const protocol::Block& block) {
  // clang-format off
  static const auto ruleset = std::make_tuple(
    Rule{ValidateNonEmpty},               // A block MUST contain at least one transaction.
    Rule{ValidateMerkleRoot},             // A block’s Merkle root field MUST equal the Merkle root of its transaction list.
    Rule{ValidateOriginalSizeLimit},      // A block’s serialized size (before SegWit) MUST NOT exceed 1,000,000 bytes.
    Rule{ValidateCoinbase},               // A block MUST contain exactly one coinbase transaction, and it MUST be the first transaction.
    Each{ValidateTransaction, TransactionsInBlock{}},
    Rule{ValidateSignatureOps}            // The total number of signature operations in a block MUST NOT exceed the consensus maximum.
  );
  // clang-format on
  return ValidateRules(ruleset, -1, block);
}

// Contextual Rules
[[nodiscard]] inline Result ValidateContextual(const BlockEnvironmentContext& context) {
  // clang-format off
  static const auto ruleset = std::make_tuple(
    Rule {ValidateTransactionFinality},                            // All transactions in the block MUST be final given the block height and locktime rules.
    Rule {ValidateCoinbaseHeight,        BIP::HeightInCoinbase },  // From BIP34, the coinbase transaction’s scriptSig MUST begin by pushing the block height.
    Group{ValidateWitnessCommitment,     BIP::SegWit           },  // From BIP141, the coinbase transaction MUST include a valid witness commitment for blocks containing witness data.
    Rule {ValidateNoWitnessPreSegwit},
    Rule {ValidateBlockWeight}                                     // A block’s total weight MUST NOT exceed 4,000,000 weight units.
  );
  //clang-format on
  return ValidateRules(ruleset, context.height, context);
}

[[nodiscard]] inline Result ValidateSpending(const BlockSpendContext& context) {
  // clang-format off
  static const auto ruleset = std::make_tuple(
    Rule{ValidateOutPointsUnique},
    Rule{ValidateInputPrevoutsUnspent},
    Each{ValidateSpendingTransaction, SpendsInBlock{}, MakeTransactionSpendContext{}},
    Rule{ValidateSigOpCosts},
    // TODO: Script validation
    Rule{ValidateBlockSubsidy}
  );
  // clang-format on
  return ValidateRules(ruleset, context.height, context);
}

// Block Validation Rules
[[nodiscard]] inline Result ValidateBlock(const protocol::Block& block,
                                        const protocol::BlockHeader& parent,
                                        const HeaderAncestryView& view,
                                        const int64_t current_time,
                                        const UnspentOutputsView& unspent) {
  // clang-format off
  static const auto ruleset = std::make_tuple(
    Group{ValidateHeader,          MakeHeaderContext},       // Header Rules
    Group{ValidateLocal,           MakeEnvironmentContext},  // Local Rules
    Group{ValidateContextual,      MakeEnvironmentContext},  // Contextual Rules
    Group{ValidateSpending,        MakeBlockSpendContext}    // Spending Rules
  );
  //clang-format on                                            
  const BlockValidationContext context{block, parent, view, current_time, unspent};
  return ValidateRules(ruleset, view.Length(), context);
}

}  // namespace hornet::consensus::rules
