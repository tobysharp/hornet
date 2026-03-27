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
  Rule{ValidatePreviousHash},             // A header MUST reference the hash of a valid parent block.
  Rule{ValidateProofOfWork},              // A header's hash MUST achieve its own proof-of-work target.
  Rule{ValidateDifficultyAdjustment},     // A header's proof-of-work target MUST satisfy the difficulty adjustment formula for the timechain.
  Rule{ValidateMedianTimePast},           // A header timestamp MUST be strictly greater than the median of its 11 ancestors' timestamps.
  Rule{ValidateTimestampCurrent},         // A header timestamp MUST be less than or equal to network-adjusted time plus 2 hours.
  Rule{ValidateVersion}                   // A header's version number MUST NOT have been retired by any activated soft fork.
};

// Transaction Rules
static constexpr auto kTransactionRules = All{
  Rule{ValidateInputCount},               // A transaction MUST contain at least one input.
  Rule{ValidateOutputCount},              // A transaction MUST contain at least one output.
  Rule{ValidateTransactionSize},          // A transaction's serialized size excluding witness flags and data MUST NOT exceed 1,000,000 bytes.
  Rule{ValidateOutputsNonNegative},       // All transaction output amounts MUST be non-negative.
  Rule{ValidateOutputsSum},               // The sum of a transaction's output amounts MUST NOT exceed 21,000,000 coins.
  Rule{ValidateUniqueInputs},             // A transaction's inputs MUST NOT contain duplicate outpoints.
  Rule{ValidateCoinbaseSignatureSize},    // A coinbase transaction's sig script size MUST be between 2 and 100 bytes inclusive.
  Rule{ValidateInputsPrevout}             // A non-coinbase transaction's inputs MUST have non-null previous outputs.    
};

// @section Local Rules
static constexpr auto kLocalRules = All{
  Rule{ValidateNonEmpty},                 // A block MUST contain at least one transaction.
  Rule{ValidateMerkleRoot},               // A block’s Merkle root field MUST equal the unique Merkle root of its transactions.
  Rule{ValidateOriginalSizeLimit},        // A block’s serialized size excluding witness flags and data MUST NOT exceed 1,000,000 bytes.
  Rule{ValidateCoinbase},                 // A block's first transaction MUST be its only coinbase transaction.
  Each{TransactionsInBlock{}, kTransactionRules},
  Rule{ValidateSignatureOps}              // The total legacy signature operation count over all input and output scripts MUST NOT exceed 20,000.
};

// BIP141 Witness Rules
static constexpr auto kWitnessRules =  All{
  Rule{ValidateWitnessDataHasCommitment}, // From BIP141: A block containing witness data MUST contain a witness commitment.
  Rule{ValidateWitnessNonce},             // From BIP141: A post-Segwit block containing a witness commitment MUST contain a witness nonce.
  Rule{ValidateWitnessMerkle}             // From BIP141: A post-SegWit block containing a witness commitment MUST commit to its witness Merkle root and nonce.
};

// @section Contextual Rules
static constexpr auto kContextualRules = All{
  Rule {ValidateTransactionFinality},     // All transactions in the block MUST be final given the block height and locktime rules.
  From (BIP::HeightInCoinbase,
    Rule{ValidateCoinbaseHeight}),        // From BIP34: The coinbase transaction’s sig script MUST begin by pushing the block height.
  From(BIP::SegWit, With{MakeWitnessContext, kWitnessRules}),
  Rule {ValidateNoWitnessPreSegwit},      // A pre-SegWit block MUST NOT contain any witness data.
  Rule {ValidateBlockWeight}              // A block’s total weight MUST NOT exceed 4,000,000 weight units.
};

static constexpr auto kSpendingTransactionRules = All{
  Each{InputsInSpend{}, MakeInputSpendContext{}, 
    Rule{ValidateCoinbaseMaturity}},      // Coinbase outputs MUST NOT be spent before 100 blocks after their creation.
  Rule{ValidateOutputsAtMostInputs},      // The sum of output values in a transaction MUST NOT exceed the sum of all input values being spent.
  From(BIP::SequenceLocks,
    Rule{ValidateSequenceLocks}),         // From BIP68: Each input that signals a relative lock-time interval MUST have reached relative finality.
  Rule{ValidateScripts}                   // A non-coinbase input's sig script and spent output’s pubkey script MUST evaluate successfully.
};

// @section Spending Rules
static constexpr auto kSpendingRules = All{
  Rule{ValidateOutPointsUnique},          // Transaction outputs MUST NOT give rise to duplicates of existing unspent outpoints (BIP30).
  Rule{ValidateInputPrevoutsUnspent},     // A transaction input MUST reference a previous transaction output that remains unspent.
  Each{SpendsInBlock{}, MakeTransactionSpendContext{}, kSpendingTransactionRules},
  Rule{ValidateSigOpCosts},               // The sum of sigop costs over all transactions MUST NOT exceed 80,000.
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
