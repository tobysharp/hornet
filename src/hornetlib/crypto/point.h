#pragma once

#include <limits>
#include <utility>

#include "hornetlib/crypto/element.h"
#include "hornetlib/crypto/fp.h"
#include "hornetlib/crypto/secp256k1.h"
#include "hornetlib/crypto/uintw.h"

namespace hornet::crypto::ecdsa {

// Scalar multiplication of a curve point. Defined in scale.h; forward-declared here so the
// operator* hidden friends below can delegate to it without point.h depending on scale.h.
template <class Point>
constexpr Point Scale(const UInt256& scalar, const Point& pt);

template <class Element>
class AffinePoint {
 public:
  using ElementT = Element;
  constexpr AffinePoint() {}
  constexpr AffinePoint(const AffinePoint& rhs) : x(rhs.x), y(rhs.y) {}
  constexpr AffinePoint(AffinePoint&& rhs) : x(std::move(rhs.x)), y(std::move(rhs.y)) {}

  constexpr AffinePoint(const Element& x, const Element& y) : 
    x(x), y(y) {}

  constexpr bool IsInfinity() const { return x == 0 && y == 0; }

  constexpr bool IsOnCurve() const {
    if (IsInfinity()) return true;
    const auto lhs = y.Squared();
    const auto rhs = x.Squared() * x + Element{secp256k1::b};
    return lhs == rhs;
  }

  const auto& NormalizedX() const { return x; }

  friend AffinePoint operator-(const AffinePoint& lhs) { return {lhs.x, lhs.y.template Negate<3>()}; }

  // Add two points on the curve
  friend AffinePoint operator+(const AffinePoint& lhs, const AffinePoint& rhs) {
    if (lhs.IsInfinity()) return rhs;
    else if (rhs.IsInfinity()) return lhs;
    else if (lhs.x != rhs.x) {
      const auto lambda = (rhs.y.template Subtract<3>(lhs.y)) / (rhs.x - lhs.x);
      const auto x3 = lambda.Squared() - lhs.x - rhs.x;
      const auto y3 = (lambda * (lhs.x.template Subtract<8>(x3))).template Subtract<3>(lhs.y);
      return {x3.NormalizeWeak(), y3.NormalizeWeak()};
    } else if (lhs.y == rhs.y.template Negate<3>()) return {};
    else return lhs.Double();
  }

  AffinePoint Double() const {
    if (IsInfinity()) return {};

    const auto lambda = (3_c * x.Squared()) / (y + y);
    const auto x3 = lambda.Squared().template Subtract<4>(x + x);
    const auto y3 = (lambda * x.template Subtract<7>(x3)).template Subtract<3>(y);
    return {x3.NormalizeWeak(), y3.NormalizeWeak()};
  }

  friend AffinePoint operator-(const AffinePoint& lhs, const AffinePoint& rhs) { 
    if (lhs.IsInfinity()) return -rhs;
    else if (rhs.IsInfinity()) return lhs;
    else if (lhs.x != rhs.x) {
      const auto lambda = rhs.y.template Negate<3>().template Subtract<3>(lhs.y) / (rhs.x - lhs.x);
      const auto x3 = lambda.Squared() - lhs.x - rhs.x;
      const auto y3 = (lambda * (lhs.x.template Subtract<8>(x3))).template Subtract<3>(lhs.y);
      return {x3.NormalizeWeak(), y3.NormalizeWeak()};
    } else if (lhs.y == rhs.y) return {};
    else return lhs.Double();
  }

  // Scalar multiplication. Implemented by Scale (scale.h).
  friend AffinePoint operator*(const UInt256& scalar, const AffinePoint& pt) { return Scale(scalar, pt); }

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

  Element x;
  Element y;
};

template <class Element>
class JacobianPoint {
 public:
  using ElementT = Element;
  using Affine = AffinePoint<Element>;
  using ZRatio = Element;

  constexpr JacobianPoint() {}
  constexpr JacobianPoint(const JacobianPoint& rhs) = default;
  constexpr JacobianPoint(JacobianPoint&& rhs) = default;
  constexpr JacobianPoint(const Affine& pt) {
    if (pt.IsInfinity()) return;
    X = pt.x;
    Y = pt.y;
    Z = 1;
  }
  constexpr JacobianPoint(const Element& x, const Element& y) : JacobianPoint(Affine{x, y}) {}
  constexpr JacobianPoint(const Element& X, const Element& Y, const Element& Z) : X(X), Y(Y), Z(Z) {}

  constexpr JacobianPoint& operator=(const JacobianPoint& rhs) = default;
  constexpr JacobianPoint& operator=(JacobianPoint&& rhs) = default;

  constexpr bool IsInfinity() const { return Z.Words() == typename Element::Array{}; }

  constexpr bool IsOnCurve() const {
    if (IsInfinity()) return true;
    // Y² == X³ + a·X·Z⁴ + b·Z⁶
    const auto Y2 = Y.Squared();
    const auto X3 = X.Squared() * X;
    const auto Z2 = Z.Squared();
    const auto Z4 = Z2.Squared();
    const auto Z6 = Z2 * Z4;
    const auto bZ6 = Z6 * Element{secp256k1::b};
    return Y2 == X3 + bZ6;
  }

  constexpr operator Affine() const {
    if (IsInfinity()) return {};
    const auto Zinv = Z.Inverse();
    const auto Zinv2 = Zinv.Squared();
    const auto Zinv3 = Zinv2 * Zinv;
    return {X * Zinv2, Y * Zinv3};
  }

  constexpr auto NormalizedX() const { return X * Z.Inverse().Squared(); }

  friend constexpr JacobianPoint operator-(const JacobianPoint& lhs) { return {lhs.X, lhs.Y.template Negate<19>(), lhs.Z}; }

  // Add two Jacobian points on the curve
  friend constexpr JacobianPoint operator+(const JacobianPoint& lhs, const JacobianPoint& rhs) {
    if (lhs.IsInfinity()) return rhs;
    if (rhs.IsInfinity()) return lhs;

    // 6M, 2S
    const auto lZ2 = lhs.Z.Squared();
    const auto rZ2 = rhs.Z.Squared();
    const auto U_1 = lhs.X * rZ2;
    const auto U_2 = rhs.X * lZ2;
    const auto H = U_2 - U_1;
    const auto S_1 = lhs.Y * rZ2 * rhs.Z;
    const auto S_2 = rhs.Y * lZ2 * lhs.Z;
    const auto r = S_2 - S_1;

    if (H == 0) {
      if (r == 0) {
        // a = 0: 2M, 5S
        // Add a (non-infinity) point to itself
        const auto X2 = lhs.X.Squared();
        const auto M = 3_c * X2;
        const auto Y2 = lhs.Y.Squared();
        const auto S = 4_c * lhs.X * Y2;
        const auto X_3 = M.Squared().template Subtract<4>(2_c * S);
        const auto Y_3 = (M * S.template Subtract<7>(X_3)).template Subtract<16>(8_c * Y2.Squared());
        const auto Z_3 = (lhs.Y + lhs.Z).Squared() - Y2 - lZ2;
        return {X_3, Y_3, Z_3};
      } else return {};
    }

    // 5M, 3S
    const auto H2 = H.Squared();
    const auto H3 = H2 * H;
    const auto U_1H2 = U_1 * H2;
    const auto X_3 = 4_c * (r.Squared() - H3).template Subtract<4>(2_c * U_1H2);
    const auto Y_3 = (r * (8_c * U_1H2).template Subtract<80>(2_c * X_3)).template Subtract<16>(8_c * (S_1 * H3));
    const auto Z_3 = H * ((lhs.Z + rhs.Z).Squared() - lZ2 - rZ2);
    return {X_3, Y_3, Z_3};
  }
  // a = 0. Double: 8M, 7S; Add: 11M, 5S.

  std::tuple<JacobianPoint, ZRatio> AddWithZRatio(const Affine& affine) const {
    // 3M, 1S
    const auto rZ2 = Z.Squared();
    const auto U_1 = affine.x * rZ2;
    const auto U_2 = X;
    const auto H = U_2 - U_1;
    const auto S_1 = affine.y * rZ2 * Z;
    const auto S_2 = Y;
    const auto r = S_2 - S_1;

    Assert(H != 0);  // Implies invalid arguments for this function.

    // 4M, 3S
    const auto H2 = H.Squared();
    const auto H3 = H2 * H;
    const auto U_1H2 = U_1 * H2;
    const auto X_3 = 4_c * (r.Squared() - H3).template Subtract<4>(2_c * U_1H2);
    const auto Y_3 = (r * (8_c * U_1H2).template Subtract<80>(2_c * X_3)).template Subtract<16>(8_c * (S_1 * H3));
    const auto Z_3 = (Z + H).Squared() - rZ2 - H2;
    
    return {{X_3, Y_3, Z_3}, 2_c * H};
  }

  // Add two Jacobian points on the curve
  friend constexpr JacobianPoint operator+(const Affine& lhs, const JacobianPoint& rhs) {
    if (lhs.IsInfinity()) return rhs;
    if (rhs.IsInfinity()) return lhs;

    // 3M, 1S
    const auto rZ2 = rhs.Z.Squared();
    const auto U_1 = lhs.x * rZ2;
    const auto U_2 = rhs.X;
    const auto H = U_2 - U_1;
    const auto S_1 = lhs.y * rZ2 * rhs.Z;
    const auto S_2 = rhs.Y;
    const auto r = S_2 - S_1;

    if (H == 0) {
      if (r == 0) {
        // 1M, 5S
        // Add a (non-infinity) point to itself
        const auto X2 = lhs.x.Squared();
        const auto M = 3_c * X2;
        const auto Y2 = lhs.y.Squared();
        const auto Y4 = Y2.Squared();
        // S = x * 4Y^2 = 2(2xY^2) = 2((x + Y^2)^2 - x^2 - Y^4)
        const auto S = 2_c * ((lhs.x + Y2).Squared() - X2 - Y4);
        const auto X_3 = M.Squared().template Subtract<32>(2_c * S);
        const auto Y_3 = (M * S.template Subtract<35>(X_3)).template Subtract<16>(8_c * Y4);
        const auto Z_3 = 2_c * lhs.y;
        return {X_3, Y_3, Z_3};
      } else return {};
    }

    // 4M, 3S
    const auto H2 = H.Squared();
    const auto H3 = H2 * H;
    const auto U_1H2 = U_1 * H2;
    const auto X_3 = 4_c * (r.Squared() - H3).template Subtract<4>(2_c * U_1H2);
    const auto Y_3 = (r * (8_c * U_1H2).template Subtract<80>(2_c * X_3)).template Subtract<16>(8_c * (S_1 * H3));
    const auto Z_3 = (rhs.Z + H).Squared() - rZ2 - H2;
    return {X_3, Y_3, Z_3};
  }
  // a = 0. Double: 4M, 6S; Add: 7M, 4S.

  constexpr JacobianPoint Double() const {
    if (IsInfinity()) return {};

    // Add a (non-infinity) point to itself
    const auto X2 = X.Squared();
    const auto Z2 = Z.Squared();
    const auto M = X2.template Times<3>();
    const auto Y2 = Y.Squared();
    const auto Y4 = Y2.Squared();
    const auto S = 2_c * ((X + Y2).Squared() - X2 - Y4);
    const auto X_3 = M.Squared().template Subtract<32>(2_c * S);
    const auto Y_3 = (M * S.template Subtract<35>(X_3)).template Subtract<16>(8_c * Y4);
    const auto Z_3 = (Y + Z).Squared() - Y2 - Z2;
    return {X_3, Y_3, Z_3};
  }
  // a = 0. 1M, 7S

  friend constexpr JacobianPoint operator+(const JacobianPoint& lhs, const Affine& rhs) { return rhs + lhs; }
  friend constexpr JacobianPoint operator-(const Affine& lhs, const JacobianPoint& rhs) { return lhs + (-rhs); }
  friend constexpr JacobianPoint operator-(const JacobianPoint& lhs, const Affine& rhs) { return (-rhs) + lhs; }
  friend constexpr JacobianPoint operator-(const JacobianPoint& lhs, const JacobianPoint& rhs) { return lhs + (-rhs); }

  // Scalar multiplication. Implemented by Scale (scale.h).
  friend constexpr JacobianPoint operator*(const UInt256& scalar, const JacobianPoint& pt) { return Scale(scalar, pt); }

  constexpr JacobianPoint& operator+=(const JacobianPoint& rhs) { return *this = *this + rhs; }
  constexpr JacobianPoint& operator+=(const Affine& rhs) { return *this = rhs + *this; }
  constexpr JacobianPoint& operator-=(const Affine& rhs) { return *this = *this - rhs; }
  constexpr JacobianPoint& operator-=(const JacobianPoint& rhs) { return *this = *this - rhs; }

  Element X;
  Element Y;
  Element Z;
};

}  // namespace hornet::crypto::ecdsa
