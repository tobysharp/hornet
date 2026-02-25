// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace hornet::crypto::SHA256 {

using hash256_t = std::array<uint8_t, 32>;

// Single SHA256 hash using Intel SHA Extensions (SHA-NI)
// Requires CPU support for SHA-NI instructions
hash256_t Hash_SHANI(std::span<const uint8_t> data);

}  // namespace hornet::crypto::SHA256
