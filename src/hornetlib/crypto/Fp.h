#pragma once

#include "hornetlib/util/big_uint.h"
#include "hornetlib/util/throw.h"

namespace hornet::crypto::ecdsa {

template <size_t kBits>
using UIntW = util::BigUint<kBits>;

namespace detail {

template <size_t kBits>
constexpr bool IsEven(const UIntW<kBits>& x) {
  return (x.Words()[0] & 1) == 0;
}

template <size_t kBits, const UIntW<kBits>& p>
constexpr UIntW<kBits> HalfModuloOdd(const UIntW<kBits>& x) {
  if (IsEven<kBits>(x)) return x >> 1;
  auto [sum, carry] = x.AddWithCarry(p);
  sum >>= 1;
  if (carry) sum.SetBit(kBits - 1);
  return sum;
}

template <size_t kBits, const UIntW<kBits>& p>
constexpr UIntW<kBits> MultiplyModuloM(const UIntW<kBits>& x, const UIntW<kBits>& y) {
  return x.MultiplyWide(y).Modulo(p);
}

template <size_t kBits, const UIntW<kBits>& p>
constexpr UIntW<kBits> InvertModuloOdd(const UIntW<kBits>& b) {
  using Type = UIntW<kBits>;
  Type aa = b, uu = 1, bb = p, vv = 0;
  while (aa != 0) {
    if (IsEven<kBits>(aa)) {
      aa >>= 1;
      uu = HalfModuloOdd<kBits, p>(uu);
    } else {
      if (aa < bb) {
        std::swap(aa, bb);
        std::swap(uu, vv);
      }
      aa = (aa - bb) >> 1;
      const auto num = uu >= vv ? uu - vv : uu + p - vv;
      uu = HalfModuloOdd<kBits, p>(num);
    }
  }
  if (bb != 1) util::ThrowRuntimeError("Value not invertible mod p");
  return vv;
}

template <size_t kBits, const UIntW<kBits>& p>
constexpr UIntW<kBits> DivideModuloOdd(const UIntW<kBits>& a, const UIntW<kBits>& b) {
  const auto s = InvertModuloOdd<kBits, p>(b);
  return MultiplyModuloM<kBits, p>(s, a);
}

}  // namespace detail

// Represents an element of the finite field Fp for an odd prime p. The template parameter kBits is the bit width of the
// underlying UIntW type, and must be large enough to represent p.
template <size_t kBits, const UIntW<kBits>& p>
class Fp {
 public:
  using Type = UIntW<kBits>;
  static_assert(!detail::IsEven<kBits>(p));

  constexpr Fp() : x(Type::Zero()) {}
  constexpr Fp(const Fp& rhs) = default;
  constexpr Fp(const Type& rhs) : x(rhs) { Assert(x < p); }
  constexpr Fp& operator=(const Fp& rhs) = default;

  bool constexpr operator!=(const Fp& rhs) const { return x != rhs.x; }
  bool constexpr operator==(const Fp& rhs) const { return x == rhs.x; }

  constexpr Fp Squared() const { return detail::MultiplyModuloM<kBits, p>(x, x); }
  constexpr Fp Inverse() const { return detail::InvertModuloOdd<kBits, p>(x); }

  constexpr const Type* operator->() const { return &x; }

  friend constexpr Fp operator-(const Fp& lhs) { return (lhs.x == Type::Zero()) ? lhs : p - lhs.x; }

  friend constexpr Fp operator+(const Fp& lhs, const Fp& rhs) {
    const auto [sum, carry] = lhs.x.AddWithCarry(rhs.x);
    if (!carry && sum < p) return sum;
    return sum - p;
  }

  friend constexpr Fp operator-(const Fp& lhs, const Fp& rhs) {
    if (lhs.x >= rhs.x) return lhs.x - rhs.x;
    else return (p - rhs.x) + lhs.x;
  }

  friend constexpr Fp operator*(const Fp& lhs, const Fp& rhs) {
    return detail::MultiplyModuloM<kBits, p>(lhs.x, rhs.x);
  }

  friend constexpr Fp operator/(const Fp& lhs, const Fp& rhs) {
    return detail::DivideModuloOdd<kBits, p>(lhs.x, rhs.x);
  }

  friend constexpr std::ostream& operator<<(std::ostream& s, const Fp& rhs) { return s << rhs.x; }

 private:
  Type x;
};

}  // namespace hornet::crypto::ecdsa
