// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "hornetlib/consensus/types.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus {

namespace constants {
  static constexpr int kMaximumTransactionBytesNoWitness = 1'000'000;
  static constexpr int64_t kSatoshisPerBitcoin = 100'000'000;
  static constexpr int64_t kMoneySupplyLimit = 21'000'000 * kSatoshisPerBitcoin;
}

[[nodiscard]] inline TransactionError ValidateTransaction(const protocol::TransactionConstView transaction) {
  // Verify the transaction sizes are allowed.
  if (transaction.InputCount() < 1)
    return TransactionError::EmptyInputs;
  if (transaction.OutputCount() < 1)
    return TransactionError::EmptyOutputs;
  if (transaction.SerializedBytesNoWitness() > constants::kMaximumTransactionBytesNoWitness)
    return TransactionError::OversizedByteCount;

  // Verify transaction output values.
  int64_t total_output_value = 0;
  for (const auto& output : transaction.Outputs()) {
    if (output.value < 0)
      return TransactionError::NegativeOutputValue;
    if (output.value > constants::kMoneySupplyLimit)
      return TransactionError::OversizedOutputValue;
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

}  // namespace hornet::consensus
