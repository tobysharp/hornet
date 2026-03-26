#pragma once

#include "hornetlib/consensus/algebra.h"
#include "hornetlib/consensus/bips.h"
#include "hornetlib/consensus/rules/context.h"
#include "hornetlib/consensus/rules/from.h"
#include "hornetlib/consensus/rules/validate_contextual.h"
#include "hornetlib/consensus/rules/validate_header.h"
#include "hornetlib/consensus/rules/validate_local.h"
#include "hornetlib/consensus/rules/validate_spending.h"
#include "hornetlib/consensus/rules/validate_transaction.h"

namespace hornet::consensus::rules {

// clang-format off

// @section Header Rules
static constexpr auto kHeaderRules = All{
  Rule{ValidatePreviousHash},             // A header MUST reference the hash of its valid parent.
  Rule{ValidateProofOfWork},              // A header MUST satisfy the chain's target proof-of-work.
  Rule{ValidateDifficultyAdjustment},     // A header's proof-of-work target MUST satisfy the difficulty adjustment formula.
  Rule{ValidateMedianTimePast},           // A header timestamp MUST be strictly greater than the median of its 11 ancestors' timestamps.
  Rule{ValidateTimestampCurrent},         // A header timestamp MUST be less than or equal to network-adjusted time plus 2 hours.
  Rule{ValidateVersion}                   // A header version number MUST meet deployment requirements depending on activated BIPs.
};

// Transaction Rules
static constexpr auto kTransactionRules = All{
  Rule{ValidateInputCount},               // A transaction MUST contain at least one input.
  Rule{ValidateOutputCount},              // A transaction MUST contain at least one output.
  Rule{ValidateTransactionSize},          // A transaction's serialized size (excluding witness data) MUST NOT exceed 1,000,000 bytes.
  Rule{ValidateOutputsNonNegative},       // A transaction output amount MUST be non-negative.
  Rule{ValidateOutputsSum},               // The sum of a transaction's output amounts MUST NOT exceed 21,000,000 coins.
  Rule{ValidateUniqueInputs},             // A transaction's inputs MUST reference distinct outpoints (no duplicates).
  Rule{ValidateCoinbaseSignatureSize},    // In a coinbase transaction, the scriptSig MUST be between 2 and 100 bytes inclusive.
  Rule{ValidateInputsPrevout}             // A non-coinbase transaction's inputs MUST have non-null prevout values.    
};

// @section Local Rules
static constexpr auto kLocalRules = All{
  Rule{ValidateNonEmpty},                 // A block MUST contain at least one transaction.
  Rule{ValidateMerkleRoot},               // A block’s Merkle root field MUST equal the Merkle root of its transaction list.
  Rule{ValidateOriginalSizeLimit},        // A block’s serialized size (before SegWit) MUST NOT exceed 1,000,000 bytes.
  Rule{ValidateCoinbase},                 // A block MUST contain exactly one coinbase transaction, and it MUST be the first transaction.
  Each{TransactionsInBlock{}, kTransactionRules},
  Rule{ValidateSignatureOps}              // The total number of signature operations in a block MUST NOT exceed the consensus maximum.
};

// BIP141 Witness Rules
static constexpr auto kWitnessRules =  All{
  Rule{ValidateWitnessDataHasCommitment}, // BIP141: A block containing witness data MUST contain a witness commitment.
  Rule{ValidateWitnessNonce},             // BIP141: A post-Segwit block containing a witness commitment MUST contain a witness nonce.
  Rule{ValidateWitnessMerkle}             // BIP141: A post-SegWit block containing a witness commitment MUST commit to its witness Merkle root and nonce.
};

// @section Contextual Rules
static constexpr auto kContextualRules = All{
  Rule {ValidateTransactionFinality},     // All transactions in the block MUST be final given the block height and locktime rules.
  From (BIP::HeightInCoinbase,
    Rule{ValidateCoinbaseHeight}),        // From BIP34, the coinbase transaction’s scriptSig MUST begin by pushing the block height.
  From(BIP::SegWit, With{MakeWitnessContext, kWitnessRules}),
  Rule {ValidateNoWitnessPreSegwit},      // A pre-SegWit block MUST NOT contain any witness data.
  Rule {ValidateBlockWeight}              // A block’s total weight MUST NOT exceed 4,000,000 weight units.
};

static constexpr auto kSpendingTransactionRules = All{
  Each{InputsInSpend{}, MakeInputSpendContext{}, 
    Rule{ValidateCoinbaseMaturity}},      // Coinbase outputs MUST NOT be spent until 100 blocks after their creation.
  Rule{ValidateOutputsAtMostInputs},      // A pre-SegWit block MUST NOT contain any witness data.
  From(BIP::SequenceLocks,
    Rule{ValidateSequenceLocks})          // BIP68: Each input that signals a relative lock-time interval MUST have reached relative finality.
  // TODO: Input scripts rule
};

// @section Spending Rules
static constexpr auto kSpendingRules = All{
  Rule{ValidateOutPointsUnique},          // BIP30: Transaction output identifiers MUST NOT collide with those of existing unspent outputs.
  Rule{ValidateInputPrevoutsUnspent},     // For every transaction input spending a previous transaction output, that output MUST exist and be unspent.
  Each{SpendsInBlock{}, MakeTransactionSpendContext{}, kSpendingTransactionRules},
  Rule{ValidateSigOpCosts},               // The sum of sigop costs across all transactions MUST NOT exceed the maximum of 80,000.
  Rule{ValidateBlockSubsidy}              // The total amount in coinbase outputs MUST NOT exceed the block reward.
};

// Block Validation Rules
static constexpr auto kBlockRules = All{
  With{MakeHeaderContext,      kHeaderRules},
  With{MakeEnvironmentContext, kLocalRules},
  With{MakeEnvironmentContext, kContextualRules},
  With{MakeBlockSpendContext,  kSpendingRules}
};

// clang-format on

}  // namespace hornet::consensus::rules
