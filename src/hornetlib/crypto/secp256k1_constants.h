// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#pragma once

#include "hornetlib/util/big_uint.h"
#include "hornetlib/util/hex.h"

namespace hornet::crypto::ecdsa {

template <int kBits>
using UIntW = util::BigUint<kBits>;

namespace constants {
// Values copied from p9 of https://www.secg.org/sec2-v2.pdf
static constexpr UIntW<256> p = "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f"_h256;
static constexpr UIntW<256> a = "0000000000000000000000000000000000000000000000000000000000000000"_h256;
static constexpr UIntW<256> b = "0000000000000000000000000000000000000000000000000000000000000007"_h256;
static constexpr UIntW<256> Gx = "79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"_h256;
static constexpr UIntW<256> Gy = "483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8"_h256;
static constexpr UIntW<256> n = "fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141"_h256;
}  // namespace constants

}  // namespace hornet::crypto::ecdsa
