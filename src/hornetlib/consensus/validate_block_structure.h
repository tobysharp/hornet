// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <algorithm>
#include <span>

#include "hornetlib/consensus/merkle.h"
#include "hornetlib/consensus/rule.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/validate_transaction.h"
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

namespace {

inline int GetSigOpCount(std::span<const uint8_t> script) {
  using protocol::script::lang::Op;

  int count = 0;
  const protocol::script::View view{script};
  for (const auto& instruction : view.Instructions()) {
    const Op op = instruction.opcode;
    if (op == Op::CheckSig || op == Op::CheckSigVerify)
      ++count;
    else if (op == Op::CheckMultiSig || op == Op::CheckMultiSigVerify)
      count += constants::kMaxPubKeysPerMultiSig;  // = 20
  }
  return count;
}

inline int GetLegacySigOpCount(const protocol::TransactionConstView& tx) {
  int count = 0;
  for (int i = 0; i < tx.InputCount(); ++i) count += GetSigOpCount(tx.SignatureScript(i));
  for (int i = 0; i < tx.OutputCount(); ++i) count += GetSigOpCount(tx.PkScript(i));
  return count;
}

// Verify that there is at least one transaction.
[[nodiscard]] inline ErrorStack ValidateNonEmpty(const protocol::Block& block) {
  if (block.GetTransactionCount() < 1) return BlockError::BadTransactionCount;
  return {};
}

// Verify the Merkle root is correct.
[[nodiscard]] inline ErrorStack ValidateMerkleRoot(const protocol::Block& block) {
  const auto merkle_root = ComputeMerkleRoot(block);
  if (!merkle_root.unique || merkle_root.hash != block.Header().GetMerkleRoot())
    return BlockError::BadMerkleRoot;
  return {};
}

// Verify that the block respects size limits.
[[nodiscard]] inline ErrorStack ValidateSizeLimit(const protocol::Block& block) {
  //////////
  // !!
  // !!
  // TODO: This logic is currently WRONG. It should be validating the non-witness size!
  // !!
  // !!
  //////////
  if (block.GetWeightUnits() > constants::kMaximumWeightUnits) return BlockError::BadSize;
  return {};
}

// Verify that the first transaction is the one and only coinbase.
[[nodiscard]] inline ErrorStack ValidateCoinbase(const protocol::Block& block) {
  for (int i = 0; i < block.GetTransactionCount(); ++i)
    if (block.Transaction(i).IsCoinBase() != (i == 0)) return BlockError::BadCoinBase;
  return {};
}

// Verify the transactions are all valid with no duplicate inputs.
[[nodiscard]] inline ErrorStack ValidateTransactions(const protocol::Block& block) {
  for (const auto& tx : block.Transactions()) {
    if (ErrorStack stack = ValidateTransaction(tx); !stack)
      return stack.Push(BlockError::BadTransaction);
  }
  return {};
}

// Verify the number of sig ops.
[[nodiscard]] inline ErrorStack ValidateSignatureOps(const protocol::Block& block) {
  const auto txs = block.Transactions();
  const int signature_ops = std::accumulate(txs.begin(), txs.end(), 0, [](int x, const auto& tx) {
    return x + GetLegacySigOpCount(tx);
  });
  if (signature_ops * constants::kWitnessScaleFactor > constants::kMaxBlockSigOpsCost)
    return BlockError::BadSigOpCount;
  return {};
}

}  // namespace

// Performs non-contextual block validation, aligned with Core's CheckBlock function.
[[nodiscard]] inline ErrorStack ValidateBlockStructure(const protocol::Block& block) {
  
  // clang-format off
  static constexpr std::array rules = {
    Rule{ValidateNonEmpty},
    Rule{ValidateMerkleRoot},
    Rule{ValidateSizeLimit},
    Rule{ValidateCoinbase},
    Rule{ValidateTransactions},
    Rule{ValidateSignatureOps}
  };
  // clang-format on
  
  return ValidateRules(rules, 0, block);
}

}  // namespace hornet::consensus
