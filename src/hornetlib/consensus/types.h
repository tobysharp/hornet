// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <algorithm>
#include <expected>
#include <optional>
#include <variant>
#include <vector>

namespace hornet::consensus {

// Represents errors that can occur during validation.
enum class Error {
  // Headers
  Header_ParentNotFound,
  Header_InvalidProofOfWork,
  Header_BadTimestamp,
  Header_BadDifficultyTransition,
  Header_BadVersion,

  // Block structure
  Structure_BadSize,
  Structure_BadTransactionCount,
  Structure_BadCoinBase,
  Structure_BadCoinBaseHeight,
  Structure_BadMerkleRoot,
  Structure_BadTransaction,
  Structure_BadSigOpCount,
  Structure_NonFinalTransaction,
  Structure_BadWitnessNonce,
  Structure_BadWitnessMerkle,
  Structure_UnexpectedWitness,
  Structure_BadBlockWeight,

  // Transactions
  Transaction_EmptyInputs,
  Transaction_EmptyOutputs,
  Transaction_OversizedByteCount,
  Transaction_NegativeOutputValue,
  Transaction_OversizedOutputValue,
  Transaction_OversizedTotalOutputValues,
  Transaction_DuplicatedInput,
  Transaction_NullPreviousOutput,
  Transaction_BadCoinBaseSigScriptSize,
  Transaction_NotUnspent
};

// SuccessOr represents state that is either "success" or it is a specific typed error.
template <typename Err>
class SuccessOr {
 public:
  SuccessOr() = default;

  template <typename E>
  requires std::is_constructible_v<Err, E>
  SuccessOr(const E err) : value_(std::unexpected{err}) {}

  template <typename E>
  requires std::is_constructible_v<Err, E>
  SuccessOr(const SuccessOr<E>& rhs) : value_{rhs ? std::expected<void, Err>{} : std::unexpected{rhs.Error()}} {}

  static inline const SuccessOr Ok = {};

  explicit operator bool() const { return value_.has_value(); }

  bool operator ==(const SuccessOr& rhs) const {
    return value_ == rhs.value_;
  }

  // If in the "success" state, executes the given function, and returns the result.
  // The function fn must return SuccessOr<E> where E can be converted to type Err.
  template <typename Func>
  SuccessOr AndThen(Func fn) const {
    if (value_.has_value()) return fn();
    return *this;
  }

  const Err& Error() const {
    return value_.error();
  }

 private:
  std::expected<void, Err> value_;
};

using Result = SuccessOr<Error>;

}  // namespace hornet::consensus
