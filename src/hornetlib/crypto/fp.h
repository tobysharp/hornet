#pragma once

#include <optional>

#include "hornetlib/crypto/reduce.h"
#include "hornetlib/crypto/secp256k1.h"
#include "hornetlib/crypto/uintw.h"
#include "hornetlib/util/hex.h"
#include "hornetlib/util/throw.h"

namespace hornet::crypto::ecdsa {

// Represents an element of the finite field Fp for an odd prime p. The template parameter kBits is the bit width of the
// underlying UIntW type, and must be large enough to represent p.
template <int kBits, const UIntW<kBits>& p>
struct Fp {
  using Type = UIntW<kBits>;
  using Array = UIntW<kBits>::Array;
  static_assert(!detail::IsEven<kBits>(p));

  constexpr Fp() : x(Type::Zero()) {}
  constexpr Fp(const Fp& rhs) = default;
  constexpr Fp(const Type& rhs) : x(rhs) { Assert(x < p); }
  constexpr Fp(typename Type::Word rhs) : Fp(Type{rhs}) {}
  constexpr Fp& operator=(const Fp& rhs) = default;

  constexpr static bool HasSquareRoot() { return p.template Modulo<sizeof(typename Type::Word)*8>(4) == 3; }
  constexpr bool operator!=(const Fp& rhs) const { return x != rhs.x; }
  constexpr bool operator==(const Fp& rhs) const { return x == rhs.x; }

  constexpr const auto& Words() const { return x.Words(); }

  constexpr Fp Squared() const { return ReduceModulo<kBits, p>(x.Squared()); }
  constexpr Fp Inverse() const { return detail::InvertModuloOdd<kBits, p>(x); }

  constexpr std::optional<Fp> SquareRoot() const {
    // We rely on p = 3 (mod 4) in this implementation, with p an odd prime, which guarantees that (p+1)/4 is an
    // integer and for any quadratic residue x, x^((p+1)/4) (mod p) is a square root of x, via Euler's criterion.
    static_assert(HasSquareRoot());
    static constexpr Type kExponent = (p + 1) >> 2;
    static constexpr int kExponentBits = kExponent.SignificantBits();
    
    Fp result = 1;
    Fp power = x;
    for (int i = 0; i < kExponentBits; ++i)
    {
      if (kExponent.GetBit(i)) result *= power;
      power = power.Squared();
    }

    // Now we need to check that result.Squared() == x, because otherwise x is a quadratic non-residue, and doesn't
    // have a valid square root.
    if (result.Squared() != *this) return std::nullopt;
    return result;
  }

  template <int k>
  constexpr Fp LShift() const {
    constexpr int kResultBits = NextWord<kBits + k>();
    const auto ext = x.template ZeroExtend<kResultBits>();
    const auto mul = ext << k;
    return ReduceModulo<kBits, p>(mul);
  }

  template <int k> Fp constexpr Times() const {
    if constexpr (k < 0) return -Times<-k>();
    if constexpr (k == 0) return {};
    if constexpr (k == 1) return *this;
    if constexpr (k == 2) return (*this + *this);
    if constexpr (util::IsPowerOf2(k)) return LShift<util::Log2(k)>();
    return *this * static_cast<typename Type::Word>(k);    
  }

  template <int k>
  friend constexpr Fp operator *(const Fp& x, std::integral_constant<int, k>) {
    return x.template Times<k>();
  }

  template <int k>
  friend constexpr Fp operator *(std::integral_constant<int, k>, const Fp& x) {
    return x.template Times<k>();
  }

  template <int k>
  friend constexpr Fp operator <<(const Fp& x, std::integral_constant<int, k>) {
    return x.template LShift<k>();
  }

  constexpr const Type* operator->() const { return &x; }

  friend constexpr Fp operator-(const Fp& lhs) { return (lhs.x == Type::Zero()) ? lhs : p - lhs.x; }

  friend constexpr Fp operator+(const Fp& lhs, const Fp& rhs) {
    const auto [sum, carry] = lhs.x.AddWithCarry(rhs.x);
    const auto [reduced, borrow] = sum.SubWithBorrow(p);
    return reduced.ConditionalAdd(borrow && !carry, p);
  }

  friend constexpr Fp operator-(const Fp& lhs, const Fp& rhs) {
    const auto [diff, borrow] = lhs.x.SubWithBorrow(rhs.x);
    return diff.ConditionalAdd(borrow, p);
  }

  friend constexpr Fp operator*(const Fp& lhs, const Fp& rhs) {
    return ReduceModulo<kBits, p>(lhs.x * rhs.x);
  }

  constexpr Fp& operator *=(const Fp& rhs) {
    return *this = *this * rhs;
  }

  friend constexpr Fp operator/(const Fp& lhs, const Fp& rhs) {
    return detail::DivideModuloOdd<kBits, p>(lhs.x, rhs.x);
  }

  friend constexpr std::ostream& operator<<(std::ostream& s, const Fp& rhs) { return s << rhs.x; }

  Type x;
};

}  // namespace hornet::crypto::ecdsa
