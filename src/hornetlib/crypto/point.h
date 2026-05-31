#pragma once

#include <limits>
#include <utility>

#include "hornetlib/crypto/fp.h"
#include "hornetlib/crypto/naf.h"
#include "hornetlib/crypto/uintw.h"

namespace hornet::crypto::ecdsa {

template <int kBits, const UIntW<kBits>& p, const UIntW<kBits>& a, const UIntW<kBits>& b>
class AffinePoint {
 public:
  using Mod_p = Fp<kBits, p>;
  using Wide = typename Mod_p::Type;

  constexpr AffinePoint() {}
  constexpr AffinePoint(const AffinePoint& rhs) : x(rhs.x), y(rhs.y) {}
  constexpr AffinePoint(AffinePoint&& rhs) : x(std::move(rhs.x)), y(std::move(rhs.y)) {}
  constexpr AffinePoint(const Mod_p& x, const Mod_p& y) : x(x), y(y) {}

  constexpr bool IsInfinity() const { return x == 0 && y == 0; }

  constexpr bool IsOnCurve() const {
    if (IsInfinity()) return true;
    const auto lhs = y.Squared();
    const auto rhs = (x.Squared() + a) * x + b;
    return lhs == rhs;
  }

  const Mod_p& NormalizedX() const { return x; }

  friend AffinePoint operator-(const AffinePoint& lhs) { return {lhs.x, -lhs.y}; }

  // Add two points on the curve
  friend AffinePoint operator+(const AffinePoint& lhs, const AffinePoint& rhs) {
    if (lhs.IsInfinity() || rhs.IsInfinity()) return {lhs.x + rhs.x, lhs.y + rhs.y};
    else if (lhs.x != rhs.x) {
      const Mod_p lambda = (rhs.y - lhs.y) / (rhs.x - lhs.x);
      const Mod_p x3 = lambda.Squared() - lhs.x - rhs.x;
      const Mod_p y3 = lambda * (lhs.x - x3) - lhs.y;
      return {x3, y3};
    } else if (lhs.y == -rhs.y) return {};
    else return lhs.Double();
  }

  AffinePoint Double() const {
    if (IsInfinity()) return {};

    const Mod_p lambda = (3 * x.Squared() + a) / (y + y);
    const Mod_p x3 = lambda.Squared() - (x + x);
    const Mod_p y3 = lambda * (x - x3) - y;
    return {x3, y3};
  }

  friend AffinePoint operator-(const AffinePoint& lhs, const AffinePoint& rhs) { return lhs + (-rhs); }

  AffinePoint& operator=(const AffinePoint& rhs) {
    x = rhs.x;
    y = rhs.y;
    return *this;
  }

  AffinePoint& operator=(AffinePoint&& rhs) {
    x = std::move(rhs.x);
    y = std::move(rhs.y);
    return *this;
  }

  AffinePoint& operator+=(const AffinePoint& rhs) { return *this = *this + rhs; }
  AffinePoint& operator-=(const AffinePoint& rhs) { return *this = *this - rhs; }

  Mod_p x, y;
};

template <int kBits, const UIntW<kBits>& p, const UIntW<kBits>& a, const UIntW<kBits>& b>
class JacobianPoint {
 public:
  using Affine = AffinePoint<kBits, p, a, b>;
  using Mod_p = Fp<kBits, p>;
  using Wide = typename Mod_p::Type;

  constexpr JacobianPoint() {}
  constexpr JacobianPoint(const JacobianPoint& rhs) = default;
  constexpr JacobianPoint(JacobianPoint&& rhs) = default;
  constexpr JacobianPoint(const Affine& pt) {
    if (pt.IsInfinity()) return;
    X = pt.x;
    Y = pt.y;
    Z = 1;
  }
  constexpr JacobianPoint(const Mod_p& x, const Mod_p& y) : JacobianPoint(Affine{x, y}) {}
  constexpr JacobianPoint(const Mod_p& X, const Mod_p& Y, const Mod_p& Z) : X(X), Y(Y), Z(Z) {}

  constexpr JacobianPoint& operator=(const JacobianPoint& rhs) = default;
  constexpr JacobianPoint& operator=(JacobianPoint&& rhs) = default;

  constexpr bool IsInfinity() const { return Z == 0; }

  constexpr bool IsOnCurve() const {
    if (IsInfinity()) return true;
    // Y² == X³ + a·X·Z⁴ + b·Z⁶
    const auto Y2 = Y.Squared();
    const auto X3 = X.Squared() * X;
    const auto Z2 = Z.Squared();
    const auto Z4 = Z2.Squared();
    const auto Z6 = Z2 * Z4;
    Mod_p bZ6;
    if constexpr (b <= std::numeric_limits<typename Wide::Word>::max()) {
      bZ6 = Z6 * b.Words()[0];
    } else {
      bZ6 = b * Z6;
    }
    if constexpr (a == 0) {
      return Y2 == X3 + bZ6;
    } else {
      return Y2 == X3 + a * X * Z4 + bZ6;
    }
  }

  constexpr operator Affine() const {
    if (IsInfinity()) return {};
    const auto Zinv = Z.Inverse();
    const auto Zinv2 = Zinv.Squared();
    const auto Zinv3 = Zinv2 * Zinv;
    return {X * Zinv2, Y * Zinv3};
  }

  constexpr Mod_p NormalizedX() const { return X * Z.Inverse().Squared(); }

  friend constexpr JacobianPoint operator-(const JacobianPoint& lhs) { return {lhs.X, -lhs.Y, lhs.Z}; }

  // Add two Jacobian points on the curve
  friend constexpr JacobianPoint operator+(const JacobianPoint& lhs, const JacobianPoint& rhs) {
    if (lhs.IsInfinity()) return rhs;
    if (rhs.IsInfinity()) return lhs;

    // 6M, 2S
    const Mod_p lZ2 = lhs.Z.Squared();
    const Mod_p rZ2 = rhs.Z.Squared();
    const Mod_p U_1 = lhs.X * rZ2;
    const Mod_p U_2 = rhs.X * lZ2;
    const Mod_p H = U_2 - U_1;
    const Mod_p S_1 = lhs.Y * rZ2 * rhs.Z;
    const Mod_p S_2 = rhs.Y * lZ2 * lhs.Z;
    const Mod_p r = S_2 - S_1;

    if (H == 0) {
      if (r == 0) {
        // a = 0: 2M, 5S
        // Add a (non-infinity) point to itself
        const Mod_p X2 = lhs.X.Squared();
        const Mod_p M = [&] {
          if constexpr (a == 0) {
            return 3_c * X2;
          } else {
            const Mod_p Z4 = lZ2.Squared();
            return 3_c * X2 + a * Z4;
          }
        }();
        const Mod_p Y2 = lhs.Y.Squared();
        const Mod_p S = 4_c * lhs.X * Y2;
        const Mod_p X_3 = M.Squared() - 2_c * S;
        const Mod_p Y_3 = M * (S - X_3) - 8_c * Y2.Squared();
        const Mod_p Z_3 = (lhs.Y + lhs.Z).Squared() - Y2 - lZ2;
        return {X_3, Y_3, Z_3};
      } else return {};
    }

    // 5M, 3S
    const Mod_p H2 = H.Squared();
    const Mod_p H3 = H2 * H;
    const Mod_p U_1H2 = U_1 * H2;
    const Mod_p X_3 = 4_c * (r.Squared() - H3 - 2_c * U_1H2);
    const Mod_p Y_3 = r * (8_c * U_1H2 - 2_c * X_3) - 8_c * (S_1 * H3);
    const Mod_p Z_3 = H * ((lhs.Z + rhs.Z).Squared() - lZ2 - rZ2);
    return {X_3, Y_3, Z_3};
  }
  // a = 0. Double: 8M, 7S; Add: 11M, 5S.

  // Add two Jacobian points on the curve
  friend constexpr JacobianPoint operator+(const Affine& lhs, const JacobianPoint& rhs) {
    if (lhs.IsInfinity()) return rhs;
    if (rhs.IsInfinity()) return lhs;

    // 3M, 1S
    const Mod_p rZ2 = rhs.Z.Squared();
    const Mod_p U_1 = lhs.x * rZ2;
    const Mod_p U_2 = rhs.X;
    const Mod_p H = U_2 - U_1;
    const Mod_p S_1 = lhs.y * rZ2 * rhs.Z;
    const Mod_p S_2 = rhs.Y;
    const Mod_p r = S_2 - S_1;

    if (H == 0) {
      if (r == 0) {
        // 1M, 5S
        // Add a (non-infinity) point to itself
        const Mod_p X2 = lhs.x.Squared();
        const Mod_p M = [&] {
          if constexpr (a == 0) {
            return 3_c * X2;
          } else {
            return 3_c * X2 + a;
          }
        }();
        const Mod_p Y2 = lhs.y.Squared();
        const Mod_p Y4 = Y2.Squared();
        // S = x * 4Y^2 = 2(2xY^2) = 2((x + Y^2)^2 - x^2 - Y^4)
        const Mod_p S = 2_c * ((lhs.x + Y2).Squared() - X2 - Y4);
        const Mod_p X_3 = M.Squared() - 2_c * S;
        const Mod_p Y_3 = M * (S - X_3) - 8_c * Y4;
        const Mod_p Z_3 = 2_c * lhs.y;
        return {X_3, Y_3, Z_3};
      } else return {};
    }

    // 4M, 3S
    const Mod_p H2 = H.Squared();
    const Mod_p H3 = H2 * H;
    const Mod_p U_1H2 = U_1 * H2;
    const Mod_p X_3 = 4_c * (r.Squared() - H3 - 2_c * U_1H2);
    const Mod_p Y_3 = r * (8_c * U_1H2 - 2_c * X_3) - 8_c * (S_1 * H3);
    const Mod_p Z_3 = H * ((rhs.Z + 1).Squared() - 1 - rZ2);
    return {X_3, Y_3, Z_3};
  }
  // a = 0. Double: 4M, 6S; Add: 7M, 4S.

  constexpr JacobianPoint Double() const {
    if (IsInfinity()) return {};

    // Add a (non-infinity) point to itself
    const Mod_p X2 = X.Squared();
    const Mod_p Z2 = Z.Squared();
    const Mod_p M = [&] {
      if constexpr (a == 0) {
        return X2.template Times<3>();
      } else {
        const Mod_p Z4 = Z2.Squared();
        return X2.template Times<3>() + a * Z4;
      }
    }();
    const Mod_p Y2 = Y.Squared();
    const Mod_p Y4 = Y2.Squared();
    const Mod_p S = 2_c * ((X + Y2).Squared() - X2 - Y4);
    const Mod_p X_3 = M.Squared() - 2_c * S;
    const Mod_p Y_3 = M * (S - X_3) - 8_c * Y4;
    const Mod_p Z_3 = (Y + Z).Squared() - Y2 - Z2;
    return {X_3, Y_3, Z_3};
  }
  // a = 0. 1M, 7S

  friend constexpr JacobianPoint operator+(const JacobianPoint& lhs, const Affine& rhs) { return rhs + lhs; }
  friend constexpr JacobianPoint operator-(const Affine& lhs, const JacobianPoint& rhs) { return lhs + (-rhs); }
  friend constexpr JacobianPoint operator-(const JacobianPoint& lhs, const Affine& rhs) { return (-rhs) + lhs; }
  friend constexpr JacobianPoint operator-(const JacobianPoint& lhs, const JacobianPoint& rhs) { return lhs + (-rhs); }

  constexpr JacobianPoint& operator+=(const JacobianPoint& rhs) { return *this = *this + rhs; }
  constexpr JacobianPoint& operator+=(const Affine& rhs) { return *this = rhs + *this; }
  constexpr JacobianPoint& operator-=(const Affine& rhs) { return *this = *this - rhs; }
  constexpr JacobianPoint& operator-=(const JacobianPoint& rhs) { return *this = *this - rhs; }

  Mod_p X, Y, Z;
};

template <class Point>
constexpr Point Scale(const typename Point::Wide& scalar, const Point& pt) {
  const auto naf = NonAdjacentForm(scalar);
  constexpr int kBitCount = std::ssize(naf);

  Point sum;
  Point power = pt;
  int bitIndex = 0;
  for (; bitIndex < kBitCount && naf[bitIndex] == 0; ++bitIndex) {
    power = power.Double();
  }
  if (bitIndex < kBitCount) {
    sum = naf[bitIndex] > 0 ? power : -power;
    for (++bitIndex; bitIndex < kBitCount; ++bitIndex) {
      power = power.Double();
      if (naf[bitIndex] == 1) sum += power;
      else if (naf[bitIndex] == -1) sum -= power;
    }
  }
  return sum;
}

template <int kBits, const UIntW<kBits>& p, const UIntW<kBits>& a, const UIntW<kBits>& b>
constexpr AffinePoint<kBits, p, a, b> operator*(const UIntW<kBits>& scalar, const AffinePoint<kBits, p, a, b>& pt) {
  return Scale(scalar, pt);
}

template <int kBits, const UIntW<kBits>& p, const UIntW<kBits>& a, const UIntW<kBits>& b>
constexpr JacobianPoint<kBits, p, a, b> operator*(const UIntW<kBits>& scalar, const JacobianPoint<kBits, p, a, b>& pt) {
  return Scale(scalar, pt);
}

template <int kBits, const UIntW<kBits>& p, const UIntW<kBits>& a, const UIntW<kBits>& b>
constexpr JacobianPoint<kBits, p, a, b> LinearCombination(const UIntW<kBits>& u1, const AffinePoint<kBits, p, a, b>& P,
                                                          const UIntW<kBits>& u2,
                                                          const AffinePoint<kBits, p, a, b>& Q) {
  using Affine = AffinePoint<kBits, p, a, b>;
  using Point = JacobianPoint<kBits, p, a, b>;
  const auto naf1 = NonAdjacentForm(u1);
  const auto naf2 = NonAdjacentForm(u2);
  constexpr int kBitCount = std::ssize(naf1);

  const Point P_plus_Q = Point{P} + Q;
  const Point P_minus_Q = Point{P} - Q;
  const int add_kind[9] = {2, 1, 2, 1, 0, 1, 2, 1, 2};
  const Affine affine_addends[4] = {-P, -Q, Q, P};
  const Point jacobian_addends[5] = {-P_plus_Q, -P_minus_Q, {}, P_minus_Q, P_plus_Q};

  Point sum;
  for (int bitIndex = kBitCount - 1; bitIndex >= 0; --bitIndex) {
    sum = sum.Double();
    const int8_t digit1 = naf1[bitIndex];
    const int8_t digit2 = naf2[bitIndex];
    const int8_t index = 3 * (digit1 + 1) + (digit2 + 1);
    if (add_kind[index] == 1) sum += affine_addends[index >> 1];
    else if (add_kind[index] == 2) sum += jacobian_addends[index >> 1];
  }
  return sum;
}

}  // namespace hornet::crypto::ecdsa
