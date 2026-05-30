#pragma once

#include "hornetlib/crypto/secp256k1_constants.h"
#include "hornetlib/util/big_uint.h"

namespace hornet::crypto::ecdsa {

template <int kBits>
using UIntW = util::BigUint<kBits>;

template <int kBits>
constexpr std::pair<UIntW<kBits - 256>, UIntW<256>> Partition(const UIntW<kBits>& input) {
  return { input.template HighBits<kBits - 256>(), input.template LowBits<256>() };
}

template <int kBits>
consteval int NextWord() {
  constexpr int kDefaultSize = sizeof(typename Uint256::Word) * 8;
  return (kBits + kDefaultSize - 1) & ~(kDefaultSize - 1);
}

template <int kBits> requires (kBits < 256)
constexpr Uint256 ReduceModuloP(const UIntW<kBits>& x) {
  if constexpr (kBits == 256) return x;
  return x.template ZeroExtend<256>();
}

constexpr Uint256 ReduceModuloP(const UIntW<256>& b_2) {
  if (b_2 < constants::p) return b_2;
  else return b_2 - constants::p;  
}

constexpr Uint256 ReduceModuloP(const UIntW<320>& t1) {
  constexpr uint64_t c = (1ull << 32) + 977u;
  //                33b

  const auto [a_1, b_1] = Partition(t1);
  //          33b| 256b           |289b|

  const auto ca_1_66 = (a_1.ZeroExtend<NextWord<66>()>() << 32) + (a_1 * 977u);
  //        |66b|       \      65 bits                       /    \ 43 bits /

  auto [b_2, a_2] = b_1.AddWithCarry(ca_1_66);
  //    1b | 256b  |256b|            \ 66b / 

  if (a_2) return b_2 + c;
  return ReduceModuloP(b_2);
}

constexpr Uint256 ReduceModuloP(const UIntW<512>& x) {
  // Each variable is labeled with its maximum effective bit width.

  const auto [t_H, t_L] = Partition(x);
  //         |256b|256b|          |512b|

  const auto t_H_ext = t_H.ZeroExtend<NextWord<289>()>();
  //         | 256b|  |256b|

  const auto t1 = (t_H_ext << 32) + (t_H_ext * 977u + t_L);
  //               \  288 bits /  +  \  266 bits    + 256b|
  //      289b| =   \ 288 bits/   +   \     267 bits     /

  return ReduceModuloP(t1);
}

constexpr Uint256 ReduceModuloP(const Uint256& x, const Uint256& y) {
  return ReduceModuloP(x.MultiplyWide(y));
}


}  // namespace hornet::crypto::ecdsa