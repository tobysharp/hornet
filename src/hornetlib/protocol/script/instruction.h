// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <cstdint>
#include <span>

#include "hornetlib/protocol/script/op.h"

namespace hornet::protocol::script {

struct Instruction {
  Op opcode;
  std::span<const uint8_t> data;
};

}  // namespace hornet::protocol::script
