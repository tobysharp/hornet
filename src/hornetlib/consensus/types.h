// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

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
  BadMerkleRoot,
  BadTransaction,
  BadSigOpCount,
  NonFinalTransaction
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

}  // namespace hornet::consensus
