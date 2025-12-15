// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <algorithm>
#include <expected>
#include <optional>
#include <vector>

namespace hornet::consensus {

// Represents errors that can occur during header validation.
enum class HeaderError {
  None = 0,
  ParentNotFound,
  InvalidProofOfWork,
  BadTimestamp,
  BadDifficultyTransition,
  BadVersion
};

// Represents the validation status of a block.
enum class BlockValidationStatus {
  Unvalidated,     // The block has not yet been validated.
  AssumedValid,    // The block is buried under enough work to be assumed valid.
  StructureValid,  // The block's transaction structure is valid, but scripts have not been
                   // validated.
  Validated        // The block has been fully validated.
};

enum class BlockError {
  None = 0,
  BadSize,
  BadTransactionCount,
  BadCoinBase,
  BadCoinBaseHeight,
  BadMerkleRoot,
  BadTransaction,
  BadSigOpCount,
  NonFinalTransaction,
  BadWitnessNonce,
  BadWitnessMerkle,
  UnexpectedWitness,
  BadBlockWeight
};

enum class TransactionError {
  None = 0,
  EmptyInputs,
  EmptyOutputs,
  OversizedByteCount,
  NegativeOutputValue,
  OversizedOutputValue,
  OversizedTotalOutputValues,
  DuplicatedInput,
  NullPreviousOutput,
  BadCoinBaseSignatureScriptSize
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

  explicit operator bool() const { return value_.has_value(); }

  bool operator ==(const SuccessOr& rhs) const {
    return value_ == rhs.value_;
  }

  // If in the "success" state, executes the given function, and stores its result.
  // The function must return SuccessOr<E> where E can be converted to type Err.
  template <typename Func>
  SuccessOr& AndThen(Func fn) {
    if (value_.has_value()) {
      if (const auto result = fn(); !result) value_ = std::unexpected{result.Error()};
    }
    return *this;
  }

  const Err& Error() const {
    return value_.error();
  }

 private:
  std::expected<void, Err> value_;
};

}  // namespace hornet::consensus
