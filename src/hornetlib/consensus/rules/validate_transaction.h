// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "hornetlib/consensus/rule.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus::rules {

// A transaction MUST contain at least one input.
[[nodiscard]] inline Result ValidateInputCount(
    const protocol::TransactionConstView transaction) {
  if (transaction.InputCount() < 1) return Error::Transaction_EmptyInputs;
  return {};
}

// A transaction MUST contain at least one output.
[[nodiscard]] inline Result ValidateOutputCount(
    const protocol::TransactionConstView transaction) {
  if (transaction.OutputCount() < 1) return Error::Transaction_EmptyOutputs;
  return {};
}

// A transaction's serialized size (excluding witness data) MUST NOT exceed 1,000,000 bytes.
[[nodiscard]] inline Result ValidateTransactionSize(
    const protocol::TransactionConstView transaction) {
  if (transaction.SerializedBytesNoWitness() > 1'000'000)
    return Error::Transaction_OversizedByteCount;
  return {};
}

// All output values MUST be non-negative, and their sum MUST NOT exceed 21,000,000 coins.
[[nodiscard]] inline Result ValidateOutputValues(
    const protocol::TransactionConstView transaction) {
  constexpr int64_t kSatoshisPerBitcoin = 100'000'000;
  constexpr int64_t kMoneySupplyLimit = 21'000'000 * kSatoshisPerBitcoin;

  /* mutable */ int64_t total_output_value = 0;
  for (const auto& output : transaction.Outputs()) {
    if (output.value < 0) return Error::Transaction_NegativeOutputValue;
    if (output.value > kMoneySupplyLimit) return Error::Transaction_OversizedOutputValue;
    total_output_value += output.value;
    if (total_output_value > kMoneySupplyLimit) return Error::Transaction_OversizedTotalOutputValues;
  }
  return {};
}

// A transaction's inputs MUST reference distinct outpoints (no duplicates).
[[nodiscard]] inline Result ValidateUniqueInputs(
    const protocol::TransactionConstView transaction) {
  // Uses full sort rather than set insert for better performance on average.
  /* mutable */ std::vector<protocol::OutPoint> out_points;
  out_points.reserve(transaction.InputCount());
  for (const auto& input : transaction.Inputs())
    out_points.push_back(input.previous_output);
  std::sort(out_points.begin(), out_points.end());
  if (std::adjacent_find(out_points.begin(), out_points.end()) != out_points.end())
    return Error::Transaction_DuplicatedInput;
  return {};
}

// In a coinbase transaction, the scriptSig MUST be between 2 and 100 bytes inclusive.
[[nodiscard]] inline Result ValidateCoinbaseSignatureSize(
    const protocol::TransactionConstView transaction) {
  if (transaction.IsCoinBase()) {
    const int sig_script_size = std::ssize(transaction.SignatureScript(0));
    // Coin base script must be between 2 and 100 bytes.
    if (sig_script_size < 2 || sig_script_size > 100)
      return Error::Transaction_BadCoinBaseSigScriptSize;
  }
  return {};
}

// A non-coinbase transaction's inputs MUST have non-null prevout values.
[[nodiscard]] inline Result ValidateInputsPrevout(
    const protocol::TransactionConstView transaction) {
  if (!transaction.IsCoinBase()) {
    for (const auto& input : transaction.Inputs())
      if (input.previous_output.IsNull()) return Error::Transaction_NullPreviousOutput;
  }
  return {};
}

}  // namespace hornet::consensus::rules
