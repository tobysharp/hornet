// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "hornetlib/consensus/types.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus {

namespace constants {
inline constexpr int kMaximumTransactionBytesNoWitness = 1'000'000;
inline constexpr int64_t kSatoshisPerBitcoin = 100'000'000;
inline constexpr int64_t kMoneySupplyLimit = 21'000'000 * kSatoshisPerBitcoin;
inline constexpr uint32_t kLocktimeMinimumTimestamp = 500'000'000;
inline constexpr uint32_t kSequenceFinal = 0xFFFF'FFFF;
}  // namespace constants

[[nodiscard]] inline TransactionError ValidateTransaction(
    const protocol::TransactionConstView transaction) {
  // Verify the transaction sizes are allowed.
  if (transaction.InputCount() < 1) return TransactionError::EmptyInputs;
  if (transaction.OutputCount() < 1) return TransactionError::EmptyOutputs;
  if (transaction.SerializedBytesNoWitness() > constants::kMaximumTransactionBytesNoWitness)
    return TransactionError::OversizedByteCount;

  // Verify transaction output values.
  int64_t total_output_value = 0;
  for (const auto& output : transaction.Outputs()) {
    if (output.value < 0) return TransactionError::NegativeOutputValue;
    if (output.value > constants::kMoneySupplyLimit) return TransactionError::OversizedOutputValue;
    total_output_value += output.value;
    if (total_output_value > constants::kMoneySupplyLimit)
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

namespace detail {

// Determines whether the locktime should be interpreted as a block height (returns true),
// otherwise it should be interpreted as a timestamp.
inline bool IsLockTimeABlockHeight(uint32_t locktime) {
  return locktime < constants::kLocktimeMinimumTimestamp;
}

// Determines whether the transaction is final at the given height/timestamp.
// A transaction is considered final if its locktime has expired.
// This function is equivalent to Bitcoin Core's IsFinalTx function.
inline bool IsTransactionFinalAt(const protocol::TransactionConstView& transaction, int height,
                                 int64_t timestamp) {
  // A locktime of zero means the transaction is immediately final.
  if (transaction.LockTime() == 0) return true;

  // If we have reached the locktime, then we have finality.
  const int64_t compare_time = IsLockTimeABlockHeight(transaction.LockTime()) ? height : timestamp;
  if (transaction.LockTime() < compare_time) return true;

  // Otherwise the transaction is only final if all the inputs have sequence 0xFFFFFFFF.
  for (const auto& input : transaction.Inputs()) {
    if (input.sequence != constants::kSequenceFinal) return false;
  }
  return true;
}

}  // namespace detail
}  // namespace hornet::consensus
