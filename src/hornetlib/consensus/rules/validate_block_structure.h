// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <algorithm>
#include <span>

#include "hornetlib/consensus/merkle.h"
#include "hornetlib/consensus/rule.h"
#include "hornetlib/consensus/rules/validate_forward.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/view.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/log.h"

namespace hornet::consensus {

namespace rules {

namespace detail {
inline int GetSigOpCount(std::span<const uint8_t> script) {
  constexpr int kMaxPubKeysPerMultiSig = 20;
  using protocol::script::lang::Op;

  int count = 0;
  const protocol::script::View view{script};
  for (const auto& instruction : view.Instructions()) {
    const Op op = instruction.opcode;
    if (op == Op::CheckSig || op == Op::CheckSigVerify)
      ++count;
    else if (op == Op::CheckMultiSig || op == Op::CheckMultiSigVerify)
      count += kMaxPubKeysPerMultiSig;  // = 20
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

// A block MUST contain at least one transaction.
[[nodiscard]] inline ValidateBlockResult ValidateNonEmpty(const protocol::Block& block) {
  if (block.GetTransactionCount() < 1) return BlockError::BadTransactionCount;
  return {};
}

// A block’s Merkle root field MUST equal the Merkle root of its transaction list.
[[nodiscard]] inline ValidateBlockResult ValidateMerkleRoot(const protocol::Block& block) {
  const auto merkle_root = ComputeMerkleRoot(block);
  if (!merkle_root.unique || merkle_root.hash != block.Header().GetMerkleRoot())
    return BlockError::BadMerkleRoot;
  return {};
}

// A block’s serialized size (before SegWit) MUST NOT exceed 1,000,000 bytes.
[[nodiscard]] inline ValidateBlockResult ValidateOriginalSizeLimit(const protocol::Block& block) {
  if (block.GetStrippedSize() > 1'000'000)
      return BlockError::BadSize;
  return {};
}

// A block MUST contain exactly one coinbase transaction, and it MUST be the first transaction.
[[nodiscard]] inline ValidateBlockResult ValidateCoinbase(const protocol::Block& block) {
  for (int i = 0; i < block.GetTransactionCount(); ++i)
    if (block.Transaction(i).IsCoinBase() != (i == 0)) return BlockError::BadCoinBase;
  return {};
}

// All transactions in a block MUST be valid according to transaction-level consensus rules.
[[nodiscard]] inline ValidateBlockResult ValidateTransactions(const protocol::Block& block) {
  ValidateBlockResult result;
  for (const auto& tx : block.Transactions()) {
    if (!result.Push(ValidateTransaction(tx))) return result.Push(BlockError::BadTransaction);
  }
  return result;
}

// The total number of signature operations in a block MUST NOT exceed the consensus maximum.
[[nodiscard]] inline ValidateBlockResult ValidateSignatureOps(const protocol::Block& block) {
  constexpr int kWitnessScaleFactor = 4;
  constexpr int kMaxBlockSigOpsCost = 80'000;

  const auto txs = block.Transactions();
  const int signature_ops = std::accumulate(txs.begin(), txs.end(), 0, [](int x, const auto& tx) {
    return x + detail::GetLegacySigOpCount(tx);
  });
  if (signature_ops * kWitnessScaleFactor > kMaxBlockSigOpsCost) return BlockError::BadSigOpCount;
  return {};
}

}  // namespace rules
}  // namespace hornet::consensus
