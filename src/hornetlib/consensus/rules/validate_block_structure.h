// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <algorithm>
#include <array>
#include <numeric>
#include <ranges>
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
#include "hornetlib/util/iterator_range.h"
#include "hornetlib/util/log.h"

namespace hornet::consensus {

namespace rules {

namespace detail {

// Returns the total sig-op cost for the whole script.
inline int GetSigOpCount(const std::span<const uint8_t> script) {
  // Build the sig-op cost table statically at compile time
  static constexpr auto kSigOpCosts = [] {
    using namespace protocol::script::lang;
    std::array<int, OpCount> table = {};
    table[+Op::CheckSig]      = table[+Op::CheckSigVerify]      = 1;
    table[+Op::CheckMultiSig] = table[+Op::CheckMultiSigVerify] = 20;
    return table;
  }();

  // Return the sum of all sig-op costs for each instruction in the script.
  /* mutable */ int sum = 0;
  for (const auto& instruction : protocol::script::View{script}.Instructions())
    sum += kSigOpCosts[+instruction.opcode];
  return sum;
}

// The legacy definition of transaction sigops is the sum of sigop counts
// across all input signature scripts and all output pkScripts.
inline int GetLegacySigOpCount(const protocol::TransactionConstView& tx) {
  /* mutable */ int sum = 0;
  for (const auto& script : tx.SignatureScripts())
    sum += GetSigOpCount(script);
  for (const auto& script : tx.PkScripts())
    sum += GetSigOpCount(script);
  return sum;
};

}  // namespace detail

// A block MUST contain at least one transaction.
[[nodiscard]] inline SuccessOr<BlockOrTransactionError> ValidateNonEmpty(const protocol::Block& block) {
  if (block.GetTransactionCount() < 1) return BlockError::BadTransactionCount;
  return {};
}

// A block’s Merkle root field MUST equal the Merkle root of its transaction list.
[[nodiscard]] inline SuccessOr<BlockOrTransactionError> ValidateMerkleRoot(const protocol::Block& block) {
  const auto merkle_root = ComputeMerkleRoot(block);
  if (!merkle_root.unique || merkle_root.hash != block.Header().GetMerkleRoot())
    return BlockError::BadMerkleRoot;
  return {};
}

// A block’s serialized size (before SegWit) MUST NOT exceed 1,000,000 bytes.
[[nodiscard]] inline SuccessOr<BlockOrTransactionError> ValidateOriginalSizeLimit(const protocol::Block& block) {
  if (block.GetStrippedSize() > 1'000'000)
      return BlockError::BadSize;
  return {};
}

// A block MUST contain exactly one coinbase transaction, and it MUST be the first transaction.
[[nodiscard]] inline SuccessOr<BlockOrTransactionError> ValidateCoinbase(const protocol::Block& block) {
  for (/* mutable */ int i = 0; i < block.GetTransactionCount(); ++i)
    if (block.Transaction(i).IsCoinBase() != (i == 0)) return BlockError::BadCoinBase;
  return {};
}

// All transactions in a block MUST be valid according to transaction-level consensus rules.
[[nodiscard]] inline SuccessOr<BlockOrTransactionError> ValidateTransactions(const protocol::Block& block) {
  for (const auto& tx : block.Transactions()) {
    if (const auto result = ValidateTransaction(tx); !result)
      return result;
  }
  return {};
}

// The total number of signature operations in a block MUST NOT exceed the consensus maximum.
[[nodiscard]] inline SuccessOr<BlockOrTransactionError> ValidateSignatureOps(const protocol::Block& block) {
  /* mutable */ int sig_ops = 0;
  for (const auto& tx : block.Transactions())
    sig_ops += detail::GetLegacySigOpCount(tx);
  if (sig_ops > 20'000) return BlockError::BadSigOpCount;
  return {};
}

}  // namespace rules
}  // namespace hornet::consensus
