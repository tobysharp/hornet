// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <array>
#include <ranges>
#include <span>

#include "hornetlib/consensus/bips.h"
#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/consensus/merkle.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/validate_transaction.h"
#include "hornetlib/model/header_context.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/view.h"
#include "hornetlib/protocol/script/writer.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/log.h"

namespace hornet::consensus {

namespace constants {
inline constexpr int kMaxPubKeysPerMultiSig = 20;
inline constexpr int kMaximumWeightUnits = 4'000'000;
inline constexpr int kWitnessScaleFactor = 4;
inline constexpr int kMaxBlockSigOpsCost = 80'000;
}  // namespace constants

namespace detail {
inline int GetSigOpCount(std::span<const uint8_t> script) {
  using protocol::script::lang::Op;

  int count = 0;
  const protocol::script::View view{script};
  for (const auto& instruction : view.Instructions()) {
    switch (instruction.opcode) {
      case Op::CheckSig:
      case Op::CheckSigVerify:
        ++count;
        break;
      case Op::CheckMultiSig:
      case Op::CheckMultiSigVerify:
        count += constants::kMaxPubKeysPerMultiSig;  // = 20
        break;
      default:;
    }
  }
  return count;
}

inline int GetLegacySigOpCount(const protocol::TransactionConstView& tx) {
  int count = 0;
  for (int i = 0; i < tx.InputCount(); ++i) count += GetSigOpCount(tx.SignatureScript(i));
  for (int i = 0; i < tx.OutputCount(); ++i) count += GetSigOpCount(tx.PkScript(i));
  return count;
}
}  // namespace detail

// Performs non-contextual block validation, aligned with Core's CheckBlock function.
[[nodiscard]] inline BlockError ValidateBlockStructure(const protocol::Block& block) {
  // Verify that there is at least one transaction.
  if (block.GetTransactionCount() < 1) return BlockError::BadTransactionCount;

  // Verify the Merkle root is correct.
  const auto merkle_root = ComputeMerkleRoot(block);
  if (!merkle_root.unique || merkle_root.hash != block.Header().GetMerkleRoot())
    return BlockError::BadMerkleRoot;

  // Verify that the block respects size limits.
  if (block.GetWeightUnits() > constants::kMaximumWeightUnits) return BlockError::BadSize;

  // Verify that the only coin base transaction is the first one.
  for (int i = 0; i < block.GetTransactionCount(); ++i)
    if (block.Transaction(i).IsCoinBase() != (i == 0)) return BlockError::BadCoinBase;

  // Verify the transactions are all valid with no duplicate inputs.
  int signature_ops = 0;
  for (const auto& tx : block.Transactions()) {
    if (const auto tx_error = ValidateTransaction(tx); tx_error != TransactionError::None) {
      LogWarn() << "Transaction validation failed, txid " << tx.GetHash() << ", error "
                << static_cast<int>(tx_error) << ".";
      return BlockError::BadTransaction;
    }
    signature_ops += detail::GetLegacySigOpCount(tx);
  }

  // Verify the number of sig ops.
  if (signature_ops * constants::kWitnessScaleFactor > constants::kMaxBlockSigOpsCost)
    return BlockError::BadSigOpCount;

  // This concludes the non-contextual block validation.
  return BlockError::None;
}

namespace {
// With BIP141 (Segwit v0), witness data is added to transactions. But that witness data can't be
// included in the header's Merkle root for backwards compatibility. So instead, a commitment hash
// that includes the Merkle root *with witness data* is stuffed into the pubkey script of one
// of the coinbase transaction's outputs. Here we validate that the coinbase's commitment correctly
// commits to the block's witness data.
[[nodiscard]] inline BlockError ValidateWitnessCommitment(const protocol::Block& block) {
  using protocol::script::lang::Op;
  static constexpr std::array<uint8_t, 6> kWitnessCommitmentBytes = {+Op::Return, 0x24, 0xaa,
                                                                     0x21,        0xa9, 0xed};
  if (block.Empty()) return BlockError::None;

  // Discover which of the block's coinbase transaction outputs contain a witness commitment.
  const protocol::TransactionConstView coinbase = block.Transaction(0);
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
    const auto hash_witness = crypto::DoubleSha256<64>(ComputeWitnessMerkleRoot(block).hash,
                                                       coinbase.WitnessScript(0, 0));

    // Finally, this is compared against the commitment in the appropropriate coinbase pkscript.
    if (!std::ranges::equal(hash_witness, coinbase.PkScript(output_index).subspan(6)))
      return BlockError::BadWitnessMerkle;
  } else {
    // No valid witness commitment -- this block may not contain witness data.
    for (const auto& tx : block.Transactions())
      if (tx.IsWitness()) return BlockError::UnexpectedWitness;
  }

  return BlockError::None;
}
}  // namespace

// Performs contextual block validation, aligned with Core's ContextualCheckBlock function.
[[nodiscard]] inline BlockError ValidateBlockContext(const HeaderAncestryView& view, const protocol::Block& block) {
  const int height = view.Length();

  // Verify all transactions are finalized.
  // From BIP113 onwards, we enforce transaction locktime to be < median time past (MTP).
  const int64_t current_locktime = IsBIPEnabledAtHeight(BIP::LockTimeMedianPast, height)
                                       ? view.MedianTimePast()
                                       : block.Header().GetTimestamp();
  for (const auto& tx : block.Transactions()) {
    if (!detail::IsTransactionFinalAt(tx, height, current_locktime))
      return BlockError::NonFinalTransaction;
  }

  // With BIP34, each coinbase signature script must begin by pushing the block height.
  if (IsBIPEnabledAtHeight(BIP::HeightInCoinbase, height)) {
    const auto expected = protocol::script::Writer{}.PushInt(height).Release();
    if (!block.CoinbaseSignature().StartsWith(expected)) return BlockError::BadCoinBaseHeight;
  }

  // With BIP141 (Segwit v0), the coinbase's commitment must match the block's witness data.
  if (IsBIPEnabledAtHeight(BIP::SegWit, height)) {
    if (const BlockError error = ValidateWitnessCommitment(block); error != BlockError::None)
      return error;
  }

  // Verify that the block weight is within the limit.
  if (block.GetWeightUnits() > constants::kMaximumWeightUnits) return BlockError::BadBlockWeight;

  return BlockError::None;
}

}  // namespace hornet::consensus
