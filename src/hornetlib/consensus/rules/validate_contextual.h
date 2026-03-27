#pragma once

#include <algorithm>
#include <array>
#include <expected>
#include <ranges>
#include <span>

#include "hornetlib/consensus/bips.h"
#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/consensus/merkle.h"
#include "hornetlib/consensus/rules/context.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/writer.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus::rules {

namespace detail {
// Determines whether the locktime should be interpreted as a block height (returns true),
// otherwise it should be interpreted as a timestamp.
inline bool IsLockTimeABlockHeight(const uint32_t locktime) {
  constexpr uint32_t kLocktimeMinimumTimestamp = 500'000'000;
  return locktime < kLocktimeMinimumTimestamp;
}

// Determines whether the transaction is final at the given height/timestamp.
// A transaction is considered final if its locktime has expired.
// This function is equivalent to Bitcoin Core's IsFinalTx function.
inline bool IsTransactionFinalAt(const protocol::TransactionConstView& transaction, const int height,
                                 const int64_t timestamp) {
  constexpr uint32_t kSequenceFinal = 0xFFFF'FFFF;

  // A locktime of zero means the transaction is immediately final.
  if (transaction.LockTime() == 0) return true;

  // If we have reached the locktime, then we have finality.
  const int64_t compare_time = IsLockTimeABlockHeight(transaction.LockTime()) ? height : timestamp;
  if (transaction.LockTime() < compare_time) return true;

  // Otherwise the transaction is only final if all the inputs have sequence 0xFFFFFFFF.
  for (const auto& input : transaction.Inputs()) {
    if (input.sequence != kSequenceFinal) return false;
  }
  return true;
}
}  // namespace detail

// All transactions in the block MUST be final given the block height and locktime rules.
[[nodiscard]] inline Result ValidateTransactionFinality(const BlockEnvironmentContext& context) {
  const int64_t current_locktime = IsBIPActiveAtHeight(BIP::LockTimeMedianPast, context.height)
                                       ? context.view.MedianTimePast()
                                       : context.block.Header().GetTimestamp();
  for (const auto& tx : context.block.Transactions()) {
    if (!detail::IsTransactionFinalAt(tx, context.height, current_locktime))
      return Error::Structure_NonFinalTransaction;
  }
  return {};
}

// From BIP34: The coinbase transaction’s sig script MUST begin by pushing the block height.
[[nodiscard]] /* [[BIP::HeightInCoinbase]] */ inline Result ValidateCoinbaseHeight(
    const BlockEnvironmentContext& context) {
  const auto expected = protocol::script::Writer{}.PushInt(context.height).Release();
  if (!context.block.CoinbaseSignature().StartsWith(expected)) return Error::Structure_BadCoinBaseHeight;
  return {};
}

// From BIP141: A block containing witness data MUST contain a witness commitment.
[[nodiscard]] inline Result ValidateWitnessCommitment(const WitnessContext& context) {
  // Contrapositive: A block without a witness commitment MUST NOT contain witness data.
  if (!context.commitment) {
    for (const auto& tx : context.block.Transactions()) {
      if (tx.IsWitness()) return Error::Structure_WitnessDataWithoutCommitment;
    }
  }
  return {};
}

// From BIP141: A post-Segwit block containing a witness commitment MUST contain a witness nonce.
[[nodiscard]] inline Result ValidateWitnessNonce(const WitnessContext& context) {
  if (context.commitment && !scripts::ExtractWitnessNonce(context.block)) return Error::Structure_BadWitnessNonce;

  return {};
}

// From BIP141: A post-SegWit block containing a witness commitment MUST commit to its witness Merkle root and nonce.
[[nodiscard]] inline Result ValidateWitnessMerkle(const WitnessContext& context) {
  if (context.commitment) {
    const auto nonce = scripts::ExtractWitnessNonce(context.block);
    if (!nonce) return {};

    // The commitment value is the double-SHA256 of the concatenated witness-enabled Merkle root,
    // and the arbitrary 32-byte salt from the witness nonce.
    const auto hash_witness = crypto::DoubleSha256<64>(ComputeWitnessMerkleRoot(context.block).hash, *nonce);

    // Finally, this is compared against the commitment in the appropriate coinbase pubkey script.
    if (!std::ranges::equal(*context.commitment, hash_witness)) return Error::Structure_BadWitnessMerkle;
  }

  return {};
}

// A pre-SegWit block MUST NOT contain any witness data.
[[nodiscard]] inline Result ValidateNoWitnessPreSegwit(const BlockEnvironmentContext& context) {
  if (!IsBIPActiveAtHeight(BIP::SegWit, context.height)) {
    for (const auto& tx : context.block.Transactions()) {
      if (tx.IsWitness()) return Error::Structure_WitnessDataPreSegwit;
    }
  }
  return {};
}

// A block’s total weight MUST NOT exceed 4,000,000 weight units.
[[nodiscard]] inline Result ValidateBlockWeight(const protocol::Block& block) {
  constexpr int kMaximumWeightUnits = 4'000'000;
  if (block.GetWeightUnits() > kMaximumWeightUnits) return Error::Structure_BadBlockWeight;
  return {};
}

}  // namespace hornet::consensus::rules
