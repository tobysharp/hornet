#pragma once

#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/consensus/rules/validate_block_context.h"
#include "hornetlib/consensus/rules/validate_block_structure.h"
#include "hornetlib/consensus/rules/validate_forward.h"
#include "hornetlib/consensus/rules/validate_header.h"
#include "hornetlib/consensus/rules/validate_transaction.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/model/header_context.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus {
namespace rules {

// Performs header validation, aligned with Core's CheckBlockHeader and ContextualCheckBlockHeader.
[[nodiscard]] inline SuccessOr<HeaderError> ValidateHeader(const protocol::BlockHeader& header,
                                                         const model::HeaderContext& parent,
                                                         const HeaderAncestryView& view,
                                                         const int64_t current_time) {
  // clang-format off
  static constexpr std::array ruleset = {
    Rule{ValidatePreviousHash},         // A header MUST reference the hash of its valid parent.
    Rule{ValidateProofOfWork},          // A header MUST satisfy the chain's target proof-of-work.
    Rule{ValidateDifficultyAdjustment}, // A header's proof-of-work target MUST satisfy the difficulty adjustment formula.
    Rule{ValidateMedianTimePast},       // A header timestamp MUST be strictly greater than the median of its 11 ancestors' timestamps.
    Rule{ValidateTimestampCurrent},     // A header timestamp MUST be less than or equal to network-adjusted time plus 2 hours.
    Rule{ValidateVersion}               // A header version number MUST meet deployment requirements depending on activated BIPs.
  };
  // clang-format on
  const HeaderValidationContext context{header, parent, view, current_time, parent.height + 1};
  return ValidateRules<HeaderError>(ruleset, 0, context);
}

// Performs transaction validation, aligned with Core's CheckTransaction function.
[[nodiscard]] inline SuccessOr<TransactionError> ValidateTransaction(
    const protocol::TransactionConstView transaction) {
  // clang-format off
  static constexpr std::array ruleset = {
    Rule{ValidateInputCount},             // A transaction MUST contain at least one input.
    Rule{ValidateOutputCount},            // A transaction MUST contain at least one output.
    Rule{ValidateTransactionSize},        // A transaction's serialized size (excluding witness data) MUST NOT exceed 1,000,000 bytes.
    Rule{ValidateOutputValues},           // All output values MUST be non-negative, and their sum MUST NOT exceed 21,000,000 coins.
    Rule{ValidateUniqueInputs},           // A transaction's inputs MUST reference distinct outpoints (no duplicates).
    Rule{ValidateCoinbaseSignatureSize},  // In a coinbase transaction, the scriptSig MUST be between 2 and 100 bytes inclusive.
    Rule{ValidateInputsPrevout}           // A non-coinbase transaction's inputs MUST have non-null prevout values.
  };
  // clang-format on
  return ValidateRules<TransactionError>(ruleset, 0, transaction);
}

// Performs non-contextual block validation, aligned with Core's CheckBlock function.
[[nodiscard]] inline SuccessOr<BlockOrTransactionError> ValidateBlockStructure(const protocol::Block& block) {
  // clang-format off
  static constexpr std::array ruleset = {
    Rule{ValidateNonEmpty},           // A block MUST contain at least one transaction.
    Rule{ValidateMerkleRoot},         // A block’s Merkle root field MUST equal the Merkle root of its transaction list.
    Rule{ValidateOriginalSizeLimit},  // A block’s serialized size (before SegWit) MUST NOT exceed 1,000,000 bytes.
    Rule{ValidateCoinbase},           // A block MUST contain exactly one coinbase transaction, and it MUST be the first transaction.
    Rule{ValidateTransactions},       // All transactions in a block MUST be valid according to transaction-level consensus rules.
    Rule{ValidateSignatureOps}        // The total number of signature operations in a block MUST NOT exceed the consensus maximum.
  };
  // clang-format on
  return ValidateRules<BlockOrTransactionError>(ruleset, 0, block);
}

// Performs contextual block validation, aligned with Core's ContextualCheckBlock function.
[[nodiscard]] inline SuccessOr<BlockError> ValidateBlockContext(const HeaderAncestryView& ancestry,
                                                              const protocol::Block& block) {
  // clang-format off
  static constexpr std::array ruleset = {
    Rule{ValidateTransactionFinality},                            // All transactions in the block MUST be final given the block height and locktime rules.
    Rule{ValidateCoinbaseHeight,        BIP::HeightInCoinbase },  // From BIP34, the coinbase transaction’s scriptSig MUST begin by pushing the block height.
    Rule{ValidateWitnessCommitment,     BIP::SegWit           },  // From BIP141, the coinbase transaction MUST include a valid witness commitment for blocks containing witness data.
    Rule{ValidateBlockWeight}                                     // A block’s total weight MUST NOT exceed 4,000,000 weight units.
  };
  //clang-format on
  const BlockValidationContext context{block, ancestry, ancestry.Length()};
  return ValidateRules<BlockError>(ruleset, context.height, context);
}

}  // namespace rules

// Export the top-level validation functions to the hornet::consensus namespace.
using rules::ValidateHeader;
using rules::ValidateTransaction;
using rules::ValidateBlockStructure;
using rules::ValidateBlockContext;

}  // namespace hornet::consensus
