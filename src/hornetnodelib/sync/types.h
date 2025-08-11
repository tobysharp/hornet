// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "hornetlib/consensus/types.h"
#include "hornetlib/data/sidecar_binding.h"

namespace hornet::node::sync {

// BlockValidationBinding is a binding for the block validation status sidecar.
using BlockValidationBinding = data::KeyframeBinding<consensus::BlockValidationStatus>;

}  // namespace hornet::node::sync
