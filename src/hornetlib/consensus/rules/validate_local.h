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
#include <tuple>
#include <vector>

#include "hornetlib/consensus/merkle.h"
#include "hornetlib/consensus/rules/context.h"
#include "hornetlib/consensus/rules/scripts/sigops.h"
#include "hornetlib/consensus/rules/validate_transaction.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/iterator_range.h"
#include "hornetlib/util/log.h"

namespace hornet::consensus::rules {

// A block MUST contain at least one transaction.
[[nodiscard]] inline Result ValidateNonEmpty(const protocol::Block& block) {
  if (block.GetTransactionCount() < 1) return Error::Structure_BadTransactionCount;
  return {};
}

// A block’s Merkle root field MUST equal the Merkle root of its transaction list.
[[nodiscard]] inline Result ValidateMerkleRoot(const protocol::Block& block) {
  const auto merkle_root = ComputeMerkleRoot(block);
  if (!merkle_root.unique || merkle_root.hash != block.Header().GetMerkleRoot())
    return Error::Structure_BadMerkleRoot;
  return {};
}

// A block’s serialized size (before SegWit) MUST NOT exceed 1,000,000 bytes.
[[nodiscard]] inline Result ValidateOriginalSizeLimit(const protocol::Block& block) {
  if (block.GetStrippedSize() > 1'000'000)
      return Error::Structure_BadSize;
  return {};
}

// A block MUST contain exactly one coinbase transaction, and it MUST be the first transaction.
[[nodiscard]] inline Result ValidateCoinbase(const protocol::Block& block) {
  for (/* mutable */ int i = 0; i < block.GetTransactionCount(); ++i)
    if (block.Transaction(i).IsCoinBase() != (i == 0)) return Error::Structure_BadCoinBase;
  return {};
}

// The total number of signature operations in a block MUST NOT exceed the consensus maximum.
[[nodiscard]] inline Result ValidateSignatureOps(const protocol::Block& block) {
  /* mutable */ int sig_ops = 0;
  for (const auto& tx : block.Transactions())
    sig_ops += scripts::LegacySigOpCount(tx);
  if (sig_ops > 20'000) return Error::Structure_BadSigOpCount;
  return {};
}

}  // namespace hornet::consensus::rules
