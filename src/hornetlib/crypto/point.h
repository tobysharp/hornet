#pragma once

#include "hornetlib/crypto/fp.h"

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
    else {
      // Add a (non-infinity) point to itself
      const Mod_p lambda = (3 * lhs.x.Squared() + a) / (lhs.y + lhs.y);
      const Mod_p x3 = lambda.Squared() - (lhs.x + lhs.x);
      const Mod_p y3 = lambda * (lhs.x - x3) - lhs.y;
      return {x3, y3};
    }
  }

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

  // Scalar multiplication
  friend AffinePoint operator*(const Wide& scalar, const AffinePoint& pt) {
    // Scalar multiplication of elliptic curve points can be computed efficiently using the
    // addition rule together with the double-and-add algorithm
    AffinePoint sum;
    AffinePoint power = pt;
    for (int bitIndex = 0; bitIndex < kBits; ++bitIndex) {
      if (scalar.GetBit(bitIndex)) sum += power;
      power += power;
    }
    return sum;
  }

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
    X = pt.x; Y = pt.y; Z = 1;
  }
  constexpr JacobianPoint(const Mod_p& x, const Mod_p& y) : JacobianPoint(Affine{x, y}) {}
  constexpr JacobianPoint(const Mod_p& X, const Mod_p& Y, const Mod_p& Z) : X(X), Y(Y), Z(Z) {}

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
    return { X * Zinv2, Y * Zinv3 };
  }

  Mod_p NormalizedX() const {
    return X * Z.Inverse().Squared();
  }

  friend JacobianPoint operator-(const JacobianPoint& lhs) { return {lhs.X, -lhs.Y, lhs.Z}; }

  // Add two Jacobian points on the curve
  friend JacobianPoint operator+(const JacobianPoint& lhs, const JacobianPoint& rhs) {
    if (lhs.IsInfinity()) return rhs;
    if (rhs.IsInfinity()) return lhs;

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
        // Add a (non-infinity) point to itself
        const Mod_p X2 = lhs.X.Squared();
        const Mod_p M = [&] {
          if constexpr (a == 0) {
            return X2.template Times<3>();
          } else {
            const Mod_p Z4 = lZ2.Squared();
            return X2.template Times<3>() + a * Z4;
          }
        }();
        const Mod_p Y2 = lhs.Y.Squared();
        const Mod_p S = lhs.X * Y2.template Times<4>();
        const Mod_p X_3 = M.Squared() - S.template Times<2>();
        const Mod_p Y_3 = M * (S - X_3) - Y2.Squared().template Times<8>();
        // 2YZ = (Y + Z)^2 - Y^2 - Z^2
        const Mod_p Z_3 = (lhs.Y + lhs.Z).Squared() - Y2 - lZ2;
        return {X_3, Y_3, Z_3};
      } 
      else return {};
    }
    const Mod_p H2 = H.Squared();
    const Mod_p H3 = H2 * H;
    const Mod_p U_1H2 = U_1 * H2;
    const Mod_p X_3 = r.Squared() - H3 - U_1H2.template Times<2>();
    const Mod_p Y_3 = r * (U_1H2 - X_3) - S_1 * H3;
    const Mod_p Z_3 = H * lhs.Z * rhs.Z;
    // Note that one could use the identity 
    //  2.Z_1.Z_2 = (Z_1 + Z_2)^2 - Z_1^2 - Z_2^2
    // to avoid the Z_1*Z_2 multiply here, but scaling through by the factor 2 is then required.
    return {X_3, Y_3, Z_3};
  }

  JacobianPoint Double() const {
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
    const Mod_p S = X * Y2.template Times<4>();
    const Mod_p X_3 = M.Squared() - S.template Times<2>();
    const Mod_p Y_3 = M * (S - X_3) - Y2.Squared().template Times<8>();
    // 2YZ = (Y + Z)^2 - Y^2 - Z^2
    const Mod_p Z_3 = (Y + Z).Squared() - Y2 - Z2;
    return {X_3, Y_3, Z_3};
  }

  JacobianPoint& operator=(const JacobianPoint& rhs) = default;
  JacobianPoint& operator=(JacobianPoint&& rhs) = default;

  JacobianPoint& operator+=(const JacobianPoint& rhs) { return *this = *this + rhs; }

  // Scalar multiplication
  friend JacobianPoint operator*(const Wide& scalar, const JacobianPoint& pt) {
    // Scalar multiplication of elliptic curve points can be computed efficiently using the
    // addition rule together with the double-and-add algorithm
    JacobianPoint sum;
    JacobianPoint power = pt;
    int bitIndex = 0;
    for (; bitIndex < kBits && !scalar.GetBit(bitIndex); ++bitIndex)
      power = power.Double();
    if (bitIndex < kBits) {
      sum = power;
      for (++bitIndex; bitIndex < kBits; ++bitIndex) {
        power = power.Double();
        if (scalar.GetBit(bitIndex)) sum += power;
      }
    }
    return sum;
  }

  Mod_p X, Y, Z;
};

}   // namespace hornet::crypto::ecdsa
