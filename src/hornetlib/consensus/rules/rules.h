#pragma once

#include "hornetlib/consensus/algebra.h"
#include "hornetlib/consensus/bips.h"
#include "hornetlib/consensus/rules/context.h"
#include "hornetlib/consensus/rules/from.h"
#include "hornetlib/consensus/rules/spec.h"
#include "hornetlib/consensus/rules/validate_contextual.h"
#include "hornetlib/consensus/rules/validate_header.h"
#include "hornetlib/consensus/rules/validate_local.h"
#include "hornetlib/consensus/rules/validate_spending.h"
#include "hornetlib/consensus/rules/validate_transaction.h"

// clang-format off

namespace hornet::consensus::rules {

// ## Header Rules
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

// ## Local Rules
static constexpr auto kLocalRules = All{
  Rule{ValidateNonEmpty},                 // A block MUST contain at least one transaction.
  Rule{ValidateMerkleRoot},               // A block’s Merkle root field MUST equal the unique Merkle root of its transactions.
  Rule{ValidateOriginalSizeLimit},        // A block’s serialized size excluding witness flags and data MUST NOT exceed 1,000,000 bytes.
  Rule{ValidateCoinbase},                 // A block's first transaction MUST be its only coinbase transaction.
  Rule{ValidateSignatureOps},             // The total legacy signature-operation count over all input and output scripts MUST NOT exceed 20,000.
  Each{TransactionsInBlock{}, kTransactionRules}
};

// BIP141 Witness Rules
static constexpr auto kWitnessRules =  All{
  Rule{ValidateWitnessCommitment},        // From BIP141: A block containing witness data MUST contain a witness commitment.
  Rule{ValidateWitnessNonce},             // From BIP141: A post-Segwit block containing a witness commitment MUST contain a witness nonce.
  Rule{ValidateWitnessMerkle}             // From BIP141: A post-SegWit block containing a witness commitment MUST commit to its witness Merkle root and nonce.
};

// ## Contextual Rules
static constexpr auto kContextualRules = All{
  Rule {ValidateTransactionFinality},     // All transactions in the block MUST be final given the block height and locktime rules.
  Rule {ValidateNoWitnessPreSegwit},      // A pre-SegWit block MUST NOT contain any witness data.
  Rule {ValidateBlockWeight},             // A block’s total weight MUST NOT exceed 4,000,000 weight units.
  From (BIP::HeightInCoinbase,
    Rule{ValidateCoinbaseHeight}),        // From BIP34: The coinbase transaction’s sig script MUST begin by pushing the block height.
  From(BIP::SegWit, With{MakeWitnessContext, kWitnessRules}),
};

static constexpr auto kSpendingTransactionRules = All{
  Rule{ValidateOutputsAtMostInputs},      // The sum of output values in a transaction MUST NOT exceed the sum of all input values being spent.
  From(BIP::SequenceLocks,
    Rule{ValidateSequenceLocks}),         // From BIP68: Each input that signals a relative lock-time interval MUST have reached relative finality.
  Each{InputsInSpend{}, MakeInputSpendContext{}, All{
    Rule{ValidateScriptSize},             // A pre-Taproot script required to determine whether an input successfully spends its previous output MUST NOT exceed 10,000 bytes.
    Rule{ValidateScripts},                // A non-coinbase input MUST satisfy the spent output's locking script.
    Rule{ValidateCoinbaseMaturity}        // Coinbase outputs MUST NOT be spent before 100 blocks after their creation.
  }}
};

// ## Spending Rules
static constexpr auto kSpendingRules = All{
  Rule{ValidateOutPointsUnique},          // Transaction outputs MUST NOT give rise to duplicates of existing unspent outpoints (BIP30).
  Rule{ValidateInputPrevoutsUnspent},     // A transaction input MUST reference a previous transaction output that remains unspent.
  Rule{ValidateSigOpCosts},               // The total signature-operation cost over all transactions MUST NOT exceed 80,000.
  Rule{ValidateBlockSubsidy},             // The total amount in coinbase outputs MUST NOT exceed the block subsidy plus its total fees.
  Each{SpendsInBlock{}, MakeTransactionSpendContext{}, kSpendingTransactionRules}
};

// Block Validation Rules
static constexpr auto kBlockRules = All{
  With{MakeHeaderContext,      kHeaderRules},
  With{MakeEnvironmentContext, All{kLocalRules, kContextualRules}},
  With{MakeBlockSpendContext,  kSpendingRules}
};

// This line statically guarantees that the kBlockRules definition, based on named subgraphs
// of the specification, exactly matches the full consensus specification in spec.h.
static_assert(std::is_same_v<decltype(kBlockRules), decltype(kConsensusRules)>);

}  // namespace hornet::consensus::rules

// clang-format on