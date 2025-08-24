// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

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

// Performs contextual block validation, aligned with Core's ContextualCheckBlock function.
[[nodiscard]] inline BlockError ValidateBlockContext(const model::HeaderContext& parent,
                                                     const protocol::Block& block,
                                                     const HeaderAncestryView& view) {
  const int height = parent.height + 1;

  // Verify all transactions are finalized.
  // From BIP113 onwards, we enforce transaction locktime to be < median time past (MTP).
  const int64_t current_locktime = IsBIPEnabledAtHeight(BIP::LockTimeMedianPast, height)
                                       ? view.MedianTimePast()
                                       : block.Header().GetTimestamp();
  for (const auto& tx : block.Transactions()) {
    if (!detail::IsTransactionFinalAt(tx, height, current_locktime))
      return BlockError::NonFinalTransaction;
  }

  // TODO: !! Other checks. Not yet finished !!

  return BlockError::None;
}

}  // namespace hornet::consensus
