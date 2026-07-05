#pragma once

#include "hornetlib/crypto/secp256k1.h"
#include "hornetlib/crypto/uintw.h"

namespace hornet::crypto::ecdsa {

template <int kBits>
constexpr std::pair<UIntW<kBits - 256>, UIntW<256>> Partition(const UIntW<kBits>& input) {
  return {input.template HighBits<kBits - 256>(), input.template LowBits<256>()};
}

template <int kBits>
consteval int NextWord() {
  constexpr int kDefaultSize = sizeof(typename Uint256::Word) * 8;
  return (kBits + kDefaultSize - 1) & ~(kDefaultSize - 1);
}

template <int kBits, int kRBits>
constexpr auto Extend(const UIntW<kRBits>& x) {
  return x.template ZeroExtend<NextWord<kBits>()>();
}

template <int kBits>
  requires(kBits < 256)
constexpr Uint256 ReduceModuloP(const UIntW<kBits>& x) {
  return x.template ZeroExtend<256>();
}

constexpr Uint256 ReduceModuloP(const UIntW<256>& b_2) {
  if (b_2 < secp256k1::p) return b_2;
  else return b_2 - secp256k1::p;
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

constexpr Uint256 ReduceModuloN(const UIntW<512>& x) {
  constexpr auto c_n = Uint256::Zero() - secp256k1::n;
  constexpr auto d = c_n.template LowBits<128>();
  static_assert(c_n.template HighBits<128>() == UIntW<128>{1});

  const auto [t_h, t_l] = Partition(x);                                   // [256b | 256b]
  const auto u = t_h * d + t_l + (Extend<385>(t_h) << 128);               // < 2^385
  const auto [u_h, u_l] = Partition(u);                                   // [130b | 256b]
  const auto v = (Extend<259>(u_h) << 128) + u_h * d + u_l;               // < 2^259
  const auto [v_h, v_l] = Partition(v);                                   // [3b | 256b]  (v_h is a single-word big-int)
  const auto w = (Extend<131>(v_h) << 128) + v_h * d + Extend<257>(v_l);  // < 2^257
  const auto y = (w < secp256k1::n) ? w : (w - secp256k1::n);
  return y.LowBits<256>();
}

template <int kMBits, const UIntW<kMBits>& kModulus>
inline constexpr auto ReduceModulo = [](const auto& x) { return x.Modulo(kModulus); };

template <>
inline constexpr auto ReduceModulo<256, secp256k1::p> = [](const auto& x) { return ReduceModuloP(x); };

template <>
inline constexpr auto ReduceModulo<256, secp256k1::n> = [](const auto& x) { return ReduceModuloN(x); };

// For t = x/z^2 (mod p), test whether t = r (mod n).
constexpr bool IsJacobianXEqual(const Uint256& x, const Uint256& z, const Uint256& r) {
  const Uint256 z2 = ReduceModuloP(z.Squared());
  if (ReduceModuloP(r * z2) == x) return true;
  constexpr Uint256 p_n = secp256k1::p - secp256k1::n;
  return (r < p_n) && (ReduceModuloP((r + secp256k1::n) * z2) == x);
}

}  // namespace hornet::crypto::ecdsa