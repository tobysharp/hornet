// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#pragma once

#include "hornetlib/crypto/uintw.h"
#include "hornetlib/util/hex.h"

namespace hornet::crypto::ecdsa::secp256k1 {
  
static constexpr int kBits = 256;
using Wide = UIntW<kBits>;

// secp256k1 domain parameters (p9 of https://www.secg.org/sec2-v2.pdf) and the GLV endomorphism constants.
static constexpr Wide p = "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f"_h256;
static constexpr Wide n = "fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141"_h256;
static constexpr uint32_t b = 7u;
static constexpr Wide Gx = "79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"_h256;
static constexpr Wide Gy = "483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8"_h256;

// GLV endomorphism: phi(x,y) = (beta * x, y) acts as multiplication by lambda on the order-n subgroup. 
// Lattice basis (a1,b1), (a2,b2): a1*b2 - a2*b1 = n. b1 < 0 so glv_minus_b1 = -b1 is positive.
static constexpr Wide beta         = "7ae96a2b657c07106e64479eac3434e99cf0497512f58995c1396c28719501ee"_h256;
static constexpr Wide glv_a1       = "000000000000000000000000000000003086d221a7d46bcde86c90e49284eb15"_h256;
static constexpr Wide glv_minus_b1 = "00000000000000000000000000000000e4437ed6010e88286f547fa90abfe4c3"_h256;
static constexpr Wide glv_a2       = "0000000000000000000000000000000114ca50f7a8e2f3f657c1108d9d44cfd8"_h256;
static constexpr Wide glv_b2       = "000000000000000000000000000000003086d221a7d46bcde86c90e49284eb15"_h256;

// Constants derived from p and n used in modular reduction.
static constexpr UIntW<64> c_p  = (-p).template LowBits<64>();
static constexpr UIntW<128> d_n = (-n).template LowBits<128>();
static_assert(-secp256k1::p == c_p);
static_assert((-n).template HighBits<128>() == UIntW<128>{1});

}  // namespace hornet::crypto::ecdsa::secp256k1
