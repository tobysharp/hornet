// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <algorithm>
#include <expected>
#include <variant>
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

using BlockValidation = std::expected<void, BlockError>;

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

template <typename T>
inline std::expected<void, T> Fail(T err) {
  return std::unexpected(err);
}

class ErrorStack {
 public:
  using ValidationError = std::variant<HeaderError, BlockError, TransactionError>;

  ErrorStack() = default;

  template <typename T>
  ErrorStack(T error) {
    stack_.push_back(error);
  }

  template <typename T>
  ErrorStack& Push(std::expected<void, T> result) {
    if (!result) stack_.push_back(result.error());
    return *this;
  }

  template <typename T>
  ErrorStack& Push(T error) {
    stack_.push_back(error);
    return *this;
  }

  ErrorStack& Push(ErrorStack&& rhs) {
    if (!rhs) {
      stack_.insert(stack_.end(), std::make_move_iterator(rhs.stack_.begin()),
                    std::make_move_iterator(rhs.stack_.end()));
      rhs.stack_.clear();
    }
    return *this;
  }

  template <typename Func>
  ErrorStack& AndPush(Func fn) {
    if (stack_.empty()) return Push(fn());
    return *this;
  }

  explicit operator bool() const {
    return stack_.empty();
  }

  const ValidationError& Error() const {
    return stack_.back();
  }

  bool operator ==(const ErrorStack& rhs) const {
    return std::ranges::equal(stack_, rhs.stack_);
  }

  auto begin() const {
    return stack_.begin();
  }
  auto end() const {
    return stack_.end();
  }

 private:
  std::vector<ValidationError> stack_;
};

}  // namespace hornet::consensus
