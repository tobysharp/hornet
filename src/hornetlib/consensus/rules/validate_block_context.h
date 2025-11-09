#include <algorithm>
#include <array>
#include <expected>
#include <ranges>
#include <span>

#include "hornetlib/consensus/bips.h"
#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/consensus/merkle.h"
#include "hornetlib/consensus/rule.h"
#include "hornetlib/consensus/rules/validate_forward.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/writer.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus {

struct BlockValidationContext {
  const protocol::Block& block;
  const HeaderAncestryView& view;
  const int height;
};

namespace rules {

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
inline bool IsTransactionFinalAt(const protocol::TransactionConstView& transaction, 
                                 const int height,
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
[[nodiscard]] inline SuccessOr<BlockError> ValidateTransactionFinality(const BlockValidationContext& context) {
  const int64_t current_locktime = IsBIPEnabledAtHeight(BIP::LockTimeMedianPast, context.height)
                                       ? context.view.MedianTimePast()
                                       : context.block.Header().GetTimestamp();
  for (const auto& tx : context.block.Transactions()) {
    if (!detail::IsTransactionFinalAt(tx, context.height, current_locktime))
      return BlockError::NonFinalTransaction;
  }
  return {};
}

// From BIP34, the coinbase transaction’s scriptSig MUST begin by pushing the block height.
[[nodiscard]] /* [[BIP::HeightInCoinbase]] */ inline SuccessOr<BlockError> ValidateCoinbaseHeight(
    const BlockValidationContext& context) {
  const auto expected = protocol::script::Writer{}.PushInt(context.height).Release();
  if (!context.block.CoinbaseSignature().StartsWith(expected)) return BlockError::BadCoinBaseHeight;
  return {};
}

// From BIP141, the coinbase transaction MUST include a valid witness commitment for blocks containing witness data.
[[nodiscard]] /* [[BIP::SegWit]] */ inline SuccessOr<BlockError> ValidateWitnessCommitment(
    const BlockValidationContext& context) {
  // With BIP141 (Segwit v0), witness data is added to transactions. But that witness data can't be
  // included in the header's Merkle root for backwards compatibility. So instead, a commitment hash
  // that includes the Merkle root *with witness data* is stuffed into the pubkey script of one
  // of the coinbase transaction's outputs. Here we validate that the coinbase's commitment correctly
  // commits to the block's witness data.  
  using protocol::script::lang::Op;
  constexpr std::array<uint8_t, 6> kWitnessCommitmentBytes = {+Op::Return, 0x24, 0xaa,
                                                              0x21,        0xa9, 0xed};
  if (context.block.Empty()) return {};

  // Discover which of the block's coinbase transaction outputs contain a witness commitment.
  const protocol::TransactionConstView coinbase = context.block.Transaction(0);
  const int output_index = [&] {
    for (int i = coinbase.OutputCount() - 1; i >= 0; --i)
      if (std::ranges::starts_with(coinbase.PkScript(i), kWitnessCommitmentBytes)) return i;
    return -1;
  }();

  if (output_index >= 0) {
    // There is a valid witness commitment in this coinbase output.
    // BIP141 requires the coinbase transaction to have a 32-byte witness field that acts as a
    // forward-compatible salt for future extensions to chain into this commitment value.
    if (coinbase.Witness(0).Size() != 1 || coinbase.WitnessScript(0, 0).size() != 32)
      return BlockError::BadWitnessNonce;

    // The commitment value is the double-SHA256 of the concatenated witness-enabled Merkle root,
    // and the arbitrary 32-byte salt from the coinbase witness script.
    const auto hash_witness = crypto::DoubleSha256<64>(ComputeWitnessMerkleRoot(context.block).hash,
                                                       coinbase.WitnessScript(0, 0));

    // Finally, this is compared against the commitment in the appropropriate coinbase pkscript.
    if (!std::ranges::starts_with(coinbase.PkScript(output_index).subspan(6), hash_witness))
      return BlockError::BadWitnessMerkle;
  } else {
    // No valid witness commitment -- this block may not contain witness data.
    for (const auto& tx : context.block.Transactions())
      if (tx.IsWitness()) return BlockError::UnexpectedWitness;
  }

  return {};
}

// A block’s total weight MUST NOT exceed 4,000,000 weight units.
[[nodiscard]] inline SuccessOr<BlockError> ValidateBlockWeight(const BlockValidationContext& context) {
  constexpr int kMaximumWeightUnits = 4'000'000;
  if (context.block.GetWeightUnits() > kMaximumWeightUnits) return BlockError::BadBlockWeight;
  return {};
}

}  // namespace rules
}  // namespace hornet::consensus
