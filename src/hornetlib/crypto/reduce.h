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

constexpr Uint256 ReduceModuloP(const UIntW<256>& x) {
  return (x < secp256k1::p) ? x : x - secp256k1::p;
}

constexpr Uint256 ReduceModuloP(const UIntW<320>& x) {
  const auto [hi, lo] = Partition(x); // [64b | 256b]
  auto [y, carry] = lo.AddWithCarry(hi * secp256k1::c_p);
  if (carry) return y - secp256k1::p;
  return ReduceModuloP(y);
}

constexpr Uint256 ReduceModuloP(const UIntW<512>& x) {
const auto [hi, lo] = Partition(x);  // [256b | 256b]
  return ReduceModuloP(hi * secp256k1::c_p + lo);
}

inline constexpr Uint256 ReduceModuloN(const UIntW<256>& x) {
  return x < secp256k1::n ? x : x - secp256k1::n;
}

inline constexpr Uint256 ReduceModuloN(const UIntW<512>& x) {
  const auto [t_h, t_l] = Partition(x);                                   // [256b | 256b]
  const auto u = t_h * secp256k1::d_n + t_l + (Extend<385>(t_h) << 128);               // < 2^385
  const auto [u_h, u_l] = Partition(u);                                   // [130b | 256b]
  const auto v = (Extend<259>(u_h) << 128) + u_h * secp256k1::d_n + u_l;               // < 2^259
  const auto [v_h, v_l] = Partition(v);                                   // [3b | 256b]  (v_h is a single-word big-int)
  const auto [w, carry] = v_l.AddWithCarry((Extend<131>(v_h) << 128) + v_h * secp256k1::d_n);  // < 2^257
  if (carry) return w - secp256k1::n;
  return ReduceModuloN(w);
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