#pragma once

#include "hornetlib/consensus/algebra.h"
#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/consensus/rules/context.h"
#include "hornetlib/consensus/rules/from.h"
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
  static const auto ruleset = All{
    Rule{ValidatePreviousHash},         // A header MUST reference the hash of its valid parent.
    Rule{ValidateProofOfWork},          // A header MUST satisfy the chain's target proof-of-work.
    Rule{ValidateDifficultyAdjustment}, // A header's proof-of-work target MUST satisfy the difficulty adjustment formula.
    Rule{ValidateMedianTimePast},       // A header timestamp MUST be strictly greater than the median of its 11 ancestors' timestamps.
    Rule{ValidateTimestampCurrent},     // A header timestamp MUST be less than or equal to network-adjusted time plus 2 hours.
    Rule{ValidateVersion}               // A header version number MUST meet deployment requirements depending on activated BIPs.
  };
  // clang-format on
  return Validate(ruleset, context);
}

[[nodiscard]] inline Result ValidateTransaction(const protocol::TransactionConstView tx) {
  static const auto ruleset = All{
    Rule{ValidateInputCount},             // A transaction MUST contain at least one input.
    Rule{ValidateOutputCount},            // A transaction MUST contain at least one output.
    Rule{ValidateTransactionSize},        // A transaction's serialized size (excluding witness data) MUST NOT exceed 1,000,000 bytes.
    Rule{ValidateOutputsNonNegative},     // A transaction output amount MUST be non-negative.
    Rule{ValidateOutputsSum},             // The sum of a transaction's output amounts MUST NOT exceed 21,000,000 coins.
    Rule{ValidateUniqueInputs},           // A transaction's inputs MUST reference distinct outpoints (no duplicates).
    Rule{ValidateCoinbaseSignatureSize},  // In a coinbase transaction, the scriptSig MUST be between 2 and 100 bytes inclusive.
    Rule{ValidateInputsPrevout}           // A non-coinbase transaction's inputs MUST have non-null prevout values.    
  };
  return Validate(ruleset, tx);
}

// Local Rules
[[nodiscard]] inline Result ValidateLocal(const protocol::Block& block) {
  // clang-format off
  static const auto ruleset = All{
    Rule{ValidateNonEmpty},               // A block MUST contain at least one transaction.
    Rule{ValidateMerkleRoot},             // A block’s Merkle root field MUST equal the Merkle root of its transaction list.
    Rule{ValidateOriginalSizeLimit},      // A block’s serialized size (before SegWit) MUST NOT exceed 1,000,000 bytes.
    Rule{ValidateCoinbase},               // A block MUST contain exactly one coinbase transaction, and it MUST be the first transaction.
    Each{TransactionsInBlock{},
      Rule{ValidateTransaction}
    },
    Rule{ValidateSignatureOps}            // The total number of signature operations in a block MUST NOT exceed the consensus maximum.
  };
  // clang-format on
  return Validate(ruleset, block);
}

// BIP141: A post-Segwit block MUST satisfy witness malleation rules.
[[nodiscard]] /* [[BIP::SegWit]] */ inline Result ValidateWitnessCommitment(const BlockEnvironmentContext& context) {
  // clang-format off
  static const auto ruleset = With{MakeWitnessContext, All{
    // BIP141: A block containing witness data MUST contain a witness commitment.
    Rule{ValidateWitnessDataHasCommitment}, 
    // BIP141: A post-Segwit block containing a witness commitment MUST contain a witness nonce.
    Rule{ValidateWitnessNonce}, 
    // BIP141: A post-SegWit block containing a witness commitment MUST commit to its witness Merkle root and nonce.
    Rule{ValidateWitnessMerkle}
  }};
  // clang-format on
  return Validate(ruleset, context);
}

// Contextual Rules
[[nodiscard]] inline Result ValidateContextual(const BlockEnvironmentContext& context) {
  // clang-format off
  static const auto ruleset = All{
    Rule {ValidateTransactionFinality},                            // All transactions in the block MUST be final given the block height and locktime rules.
    From (ValidateCoinbaseHeight,        BIP::HeightInCoinbase ),  // From BIP34, the coinbase transaction’s scriptSig MUST begin by pushing the block height.
    From (ValidateWitnessCommitment,     BIP::SegWit           ),  // From BIP141, the coinbase transaction MUST include a valid witness commitment for blocks containing witness data.
    Rule {ValidateNoWitnessPreSegwit},
    Rule {ValidateBlockWeight}                                     // A block’s total weight MUST NOT exceed 4,000,000 weight units.
  };
  // clang-format on
  return Validate(ruleset, context);
}

[[nodiscard]] inline Result ValidateSpendingInput(const InputSpendContext& context) {
  // clang-format off
  static const auto ruleset = 
      Rule{ValidateCoinbaseMaturity}         // Coinbase outputs MUST NOT be spent until 100 blocks after their creation.
  ;
  //clang-format on
  return Validate(ruleset, context);
}

[[nodiscard]] inline Result ValidateSpendingTransaction(const TransactionSpendContext& context) {
  // clang-format off
  static const auto ruleset = All{
    Each{InputsInSpend{}, MakeInputSpendContext{}, 
      Rule{ValidateCoinbaseMaturity}         // Coinbase outputs MUST NOT be spent until 100 blocks after their creation.
    },
    Rule{ValidateOutputValuesAtMostInputValues},
    From(ValidateSequenceLocks, BIP::SequenceLocks)
    // TODO: Input scripts rule
  };
  //clang-format on
  return Validate(ruleset, context);
}

[[nodiscard]] inline Result ValidateSpending(const BlockSpendContext& context) {
  // clang-format off
  static const auto ruleset = All{
    Rule{ValidateOutPointsUnique},
    Rule{ValidateInputPrevoutsUnspent},
    Each{SpendsInBlock{}, MakeTransactionSpendContext{}, 
      Rule{ValidateSpendingTransaction}
    },
    Rule{ValidateSigOpCosts},
    // TODO: Script validation
    Rule{ValidateBlockSubsidy}
  };
  // clang-format on
  return Validate(ruleset, context);
}

// Block Validation Rules
[[nodiscard]] inline Result ValidateBlock(const protocol::Block& block,
                                        const protocol::BlockHeader& parent,
                                        const HeaderAncestryView& view,
                                        const int64_t current_time,
                                        const UnspentOutputsView& unspent) {
  // clang-format off
  static const auto ruleset = All{
    With{MakeHeaderContext,      Rule{ValidateHeader}},      // Header Rules
    With{MakeEnvironmentContext, Rule{ValidateLocal     }},  // Local Rules
    With{MakeEnvironmentContext, Rule{ValidateContextual}},  // Contextual Rules
    With{MakeBlockSpendContext,  Rule{ValidateSpending  }},  // Spending Rules
  };
  // clang-format on                                            
  const BlockValidationContext context{block, parent, view, current_time, unspent};
  return Validate(ruleset, context);
}

}  // namespace hornet::consensus::rules
