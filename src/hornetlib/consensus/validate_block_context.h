#include <array>
#include <expected>
#include <ranges>
#include <span>

#include "hornetlib/consensus/bips.h"
#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/consensus/merkle.h"
#include "hornetlib/consensus/rule.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/validate_transaction.h"
#include "hornetlib/model/header_context.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/writer.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus {

namespace {

struct BlockValidationContext {
  const protocol::Block& block;
  const HeaderAncestryView& view;
  int height;
};

using BlockValidation = std::expected<void, BlockError>;

template <typename T>
inline std::expected<void, T> Fail(T err) {
  return std::unexpected(err);
}

// Verify all transactions are finalized.
// From BIP113 onwards, we enforce transaction locktime to be < median time past (MTP).
[[nodiscard]] inline BlockValidation ValidateTransactionFinality(
    const BlockValidationContext& context) {
  const int64_t current_locktime = IsBIPEnabledAtHeight(BIP::LockTimeMedianPast, context.height)
                                       ? context.view.MedianTimePast()
                                       : context.block.Header().GetTimestamp();
  for (const auto& tx : context.block.Transactions()) {
    if (!detail::IsTransactionFinalAt(tx, context.height, current_locktime))
      return Fail(BlockError::NonFinalTransaction);
  }
  return {};
}

// With BIP34, each coinbase signature script must begin by pushing the block height.
/* [[BIP::HeightInCoinbase]] */ [[nodiscard]] inline BlockValidation ValidateCoinbaseHeight(
    const BlockValidationContext& context) {
  const auto expected = protocol::script::Writer{}.PushInt(context.height).Release();
  if (!context.block.CoinbaseSignature().StartsWith(expected))
    return Fail(BlockError::BadCoinBaseHeight);
  return {};
}

// With BIP141 (Segwit v0), witness data is added to transactions. But that witness data can't be
// included in the header's Merkle root for backwards compatibility. So instead, a commitment hash
// that includes the Merkle root *with witness data* is stuffed into the pubkey script of one
// of the coinbase transaction's outputs. Here we validate that the coinbase's commitment correctly
// commits to the block's witness data.
/* [[BIP::SegWit]] */ [[nodiscard]] inline BlockValidation ValidateWitnessCommitment(
    const BlockValidationContext& context) {
  using protocol::script::lang::Op;
  static constexpr std::array<uint8_t, 6> kWitnessCommitmentBytes = {+Op::Return, 0x24, 0xaa,
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
      return Fail(BlockError::BadWitnessNonce);

    // The commitment value is the double-SHA256 of the concatenated witness-enabled Merkle root,
    // and the arbitrary 32-byte salt from the coinbase witness script.
    const auto hash_witness = crypto::DoubleSha256<64>(ComputeWitnessMerkleRoot(context.block).hash,
                                                       coinbase.WitnessScript(0, 0));

    // Finally, this is compared against the commitment in the appropropriate coinbase pkscript.
    if (!std::ranges::equal(hash_witness, coinbase.PkScript(output_index).subspan(6)))
      return Fail(BlockError::BadWitnessMerkle);
  } else {
    // No valid witness commitment -- this block may not contain witness data.
    for (const auto& tx : context.block.Transactions())
      if (tx.IsWitness()) return Fail(BlockError::UnexpectedWitness);
  }

  return {};
}

// Verify that the block weight is within the limit.
[[nodiscard]] inline BlockValidation ValidateBlockWeight(const BlockValidationContext& context) {
  constexpr int kMaximumWeightUnits = 4'000'000;
  if (context.block.GetWeightUnits() > kMaximumWeightUnits)
    return Fail(BlockError::BadBlockWeight);
  return {};
}

}  // namespace

[[nodiscard]] inline BlockValidation ValidateBlockContext(const HeaderAncestryView& ancestry,
                                                          const protocol::Block& block) {
  // Defines the set of validation rules applied to the contextual-block phase.
  // clang-format off
  static constexpr std::array rules = {
      Rule{ValidateTransactionFinality}, 
      Rule{ValidateCoinbaseHeight,      BIP::HeightInCoinbase },
      Rule{ValidateWitnessCommitment,   BIP::SegWit           }, 
      Rule{ValidateBlockWeight}};
  //clang-format on

  BlockValidationContext context{block, ancestry, ancestry.Length()};
  return ValidateRules(rules, context.height, context);
}

}  // namespace hornet::consensus
