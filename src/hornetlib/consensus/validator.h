// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <variant>
#include <vector>

#include "hornetlib/consensus/difficulty_adjustment.h"
#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/consensus/merkle.h"
#include "hornetlib/consensus/parameters.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/data/header_context.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/compact_target.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/target.h"
#include "hornetlib/util/throw.h"

namespace hornet::consensus {

class Validator {
 public:
  using HeaderResult = std::variant<data::HeaderContext, HeaderError>;

  Validator(const Parameters& params = {}) : parameters_(params), difficulty_adjustment_(params) {}

  [[nodiscard]] HeaderResult ValidateDownloadedHeader(const data::HeaderContext& parent,
                                        const protocol::BlockHeader& header,
                                        const HeaderAncestryView& view) const {
    const int height = parent.height + 1;

    // Verify previous hash
    if (parent.hash != header.GetPreviousBlockHash()) 
      return HeaderError::ParentNotFound;

    // Verify PoW target is valid and is achieved by the header's hash.
    const auto hash = header.ComputeHash();
    const auto target = header.GetCompactTarget().Expand();
    if (!(hash <= target)) 
      return HeaderError::InvalidProofOfWork;

    // Verify PoW target obeys the difficulty adjustment rules.
    protocol::CompactTarget expected_bits = parent.data.GetCompactTarget();
    if (difficulty_adjustment_.IsTransition(height)) {
      const int blocks_per_period = difficulty_adjustment_.GetBlocksPerPeriod();
      Assert(height - blocks_per_period < view.Length());
      const uint32_t period_start_time =
          view.TimestampAt(height - blocks_per_period);             // block[height - 2016].time
      const uint32_t period_end_time = parent.data.GetTimestamp();  // block[height - 1].time
      expected_bits = difficulty_adjustment_.ComputeCompactTarget(
          height, parent.data.GetCompactTarget(), period_start_time, period_end_time);
    }
    if (expected_bits != header.GetCompactTarget()) 
      return HeaderError::BadDifficultyTransition;

    // Verify median of recent timestamps.
    const auto recent_times = view.LastNTimestamps(parameters_.kBlocksForMedianTime);
    const uint32_t median_time = recent_times[recent_times.size() / 2];
    if (header.GetTimestamp() <= median_time) 
      return HeaderError::BadTimestamp;

    // Verify that the timestamp isn't too far in the future.
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    if (std::chrono::seconds{header.GetTimestamp()} >
        now + std::chrono::seconds{parameters_.kTimestampTolerance})
      return HeaderError::BadTimestamp;

    // Verify that the version number is allowed at this height.
    if (IsVersionRetiredAtHeight(header.GetVersion(), height)) 
      return HeaderError::BadVersion;

    return parent.Extend(header, hash);
  }

  [[nodiscard]] BlockError ValidateBlockStructure(const protocol::Block& block) const {
    // Verify the Merkle root is correct.
    if (ComputeMerkleRoot(block) != block.Header().GetMerkleRoot())
      return BlockError::BadMerkleRoot;

    // Verify that the block respects size limits.
    if (block.GetWeightUnits() > parameters_.kMaximumWeightUnits)
      return BlockError::BadSize;

    // Verify that there is at least one transaction.
    if (block.GetTransactionCount() < 1)
      return BlockError::BadTransactionCount;

    // Verify that the only coin base transaction is the first one.
    for (int i = 0; i < block.GetTransactionCount(); ++i)
      if (block.Transaction(i).IsCoinBase() != (i == 0))
        return BlockError::BadCoinBase;
  
    // Verify the transactions are all valid with no duplicates.
    for (const auto& tx : block.Transactions()) {
      if (const auto tx_error = ValidateTransaction(tx); tx_error != TransactionError::None) {
        LogWarn() << "Transaction validation failed, txid " << tx.GetHash() << ", error " << static_cast<int>(tx_error) << ".";
        return BlockError::BadTransaction;
      }
    }

    // TODO: Verify the number of sig ops.
  
    return BlockError::None;
  }

  [[nodiscard]] TransactionError ValidateTransaction(const protocol::TransactionConstView transaction) const {
    // Verify the transaction sizes are allowed.
    if (transaction.InputCount() < 1)
      return TransactionError::EmptyInputs;
    if (transaction.OutputCount() < 1)
      return TransactionError::EmptyOutputs;
    if (transaction.SerializedBytesNoWitness() > parameters_.kMaximumTransactionBytesNoWitness)
      return TransactionError::OversizedByteCount;

    // Verify transaction output values.
    int64_t total_output_value = 0;
    for (const auto& output : transaction.Outputs()) {
      if (output.value < 0)
        return TransactionError::NegativeOutputValue;
      if (output.value > parameters_.kMoneySupplyLimit)
        return TransactionError::OversizedOutputValue;
      total_output_value += output.value;
      if (total_output_value > parameters_.kMoneySupplyLimit)
        return TransactionError::OversizedTotalOutputValues;
    }

    // Verify no duplicate inputs.
    // Uses full sort rather than set insert for better performance on average.
    std::vector<protocol::OutPoint> out_points(transaction.InputCount());
    for (int i = 0; i < transaction.InputCount(); ++i) {
      const auto& out_point = transaction.Input(i).previous_output;
      // Verify the out point is non-null (except if coin base).
      if (!transaction.IsCoinBase() && out_point.IsNull())
          return TransactionError::NullPreviousOutput;
      out_points[i] = out_point;
    }
    std::sort(out_points.begin(), out_points.end());
    if (std::adjacent_find(out_points.begin(), out_points.end()) != out_points.end())
      return TransactionError::DuplicatedInput;

    // Verify the coin base signature script size.
    if (transaction.IsCoinBase()) {
      int sig_script_size = std::ssize(transaction.SignatureScript(0));
      // Coin base script must be between 2 and 100 bytes.
      if (sig_script_size < 2 || sig_script_size > 100)
        return TransactionError::BadCoinBaseSignatureScriptSize;
    }

    return TransactionError::None;
  }

 private:
  bool IsVersionRetiredAtHeight(int version, int height) const {
    const std::array<int, 4> kVersionExpiryHeights = {
        0,                         // v0: Invalid from genesis.
        parameters_.kBIP34Height,  // v1: Retired at BIP34 height.
        parameters_.kBIP66Height,  // v2: Retired at BIP66 height.
        parameters_.kBIP65Height   // v3: Retired at BIP65 height.
    };
    return version < 0 || (version < std::ssize(kVersionExpiryHeights) &&
                           height >= kVersionExpiryHeights[version]);
  }

  Parameters parameters_;
  DifficultyAdjustment difficulty_adjustment_;
};

}  // namespace hornet::consensus
