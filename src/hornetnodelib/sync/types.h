// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "hornetlib/consensus/types.h"
#include "hornetlib/data/sidecar_binding.h"

namespace hornet::node::sync {

// Represents the validation status of a block.
enum class BlockValidationStatus {
  Unvalidated,     // The block has not yet been validated.
  AssumedValid,    // The block is buried under enough work to be assumed valid.
  StructureValid,  // The block's transaction structure is valid, but scripts have not been
                   // validated.
  Validated        // The block has been fully validated.
};

// BlockValidationBinding is a binding for the block validation status sidecar.
using BlockValidationBinding = data::KeyframeBinding<BlockValidationStatus>;

}  // namespace hornet::node::sync
