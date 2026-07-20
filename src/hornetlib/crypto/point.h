#pragma once

#include <limits>
#include <utility>

#include "hornetlib/crypto/element.h"
#include "hornetlib/crypto/secp256k1.h"
#include "hornetlib/crypto/uintw.h"

namespace hornet::crypto::ecdsa {

class AffinePoint {
 public:
  using Element = FieldElement;
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

  friend AffinePoint operator-(const AffinePoint& lhs) { return {lhs.x, lhs.y.Negate<3>()}; }

  // Add two points on the curve
  friend AffinePoint operator+(const AffinePoint& lhs, const AffinePoint& rhs) {
    if (lhs.IsInfinity()) return rhs;
    else if (rhs.IsInfinity()) return lhs;
    else if (lhs.x != rhs.x) {
      const auto lambda = (rhs.y.Subtract<3>(lhs.y)) / (rhs.x - lhs.x);
      const auto x3 = lambda.Squared() - lhs.x - rhs.x;
      const auto y3 = (lambda * (lhs.x.Subtract<8>(x3))).Subtract<3>(lhs.y);
      return {x3.NormalizeWeak(), y3.NormalizeWeak()};
    } else if (lhs.y == rhs.y.Negate<3>()) return {};
    else return lhs.Double();
  }

  AffinePoint Double() const {
    if (IsInfinity()) return {};

    const auto lambda = (3_c * x.Squared()) / (y + y);
    const auto x3 = lambda.Squared().Subtract<4>(x + x);
    const auto y3 = (lambda * x.Subtract<7>(x3)).Subtract<3>(y);
    return {x3.NormalizeWeak(), y3.NormalizeWeak()};
  }

  friend AffinePoint operator-(const AffinePoint& lhs, const AffinePoint& rhs) {
    if (lhs.IsInfinity()) return -rhs;
    else if (rhs.IsInfinity()) return lhs;
    else if (lhs.x != rhs.x) {
      const auto lambda = rhs.y.Negate<3>().Subtract<3>(lhs.y) / (rhs.x - lhs.x);
      const auto x3 = lambda.Squared() - lhs.x - rhs.x;
      const auto y3 = (lambda * (lhs.x.Subtract<8>(x3))).Subtract<3>(lhs.y);
      return {x3.NormalizeWeak(), y3.NormalizeWeak()};
    } else if (lhs.y == rhs.y) return {};
    else return lhs.Double();
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
  AffinePoint& operator-=(const AffinePoint& rhs) { return *this = *this - rhs; }

  Element x;
  Element y;
};

class JacobianPoint {
 public:
  using Element = FieldElement;
  using Affine = AffinePoint;
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

  constexpr bool IsInfinity() const { return Z.Words() == Element::Array{}; }

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

  friend constexpr JacobianPoint operator-(const JacobianPoint& lhs) { return {lhs.X, lhs.Y.Negate<19>(), lhs.Z}; }

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
      if (r == 0) return lhs.Double();  // Coincident operands: the shallow doubling formula.
      else return {};
    }

    // 6M, 2S. Z_3 = H.Z.Z' rather than the squared 2ZZ' form: one more mul, but X_3/Y_3 shed
    // their 4x/8x scale factors and a Times level off the critical chain (see Double()).
    const auto H2 = H.Squared();
    const auto H3 = H2 * H;
    const auto U_1H2 = U_1 * H2;
    const auto X_3 = (r.Squared() - H3).Subtract<4>(2_c * U_1H2);
    const auto Y_3 = (r * U_1H2.Subtract<10>(X_3)).Subtract<2>(S_1 * H3);
    const auto Z_3 = H * (lhs.Z * rhs.Z);
    return {X_3, Y_3, Z_3};
  }
  // a = 0. Double: 3M, 4S, 1 half; Add: 12M, 4S.

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

    // 5M, 2S. Z_3 = Z.H (see the mixed add): the returned ratio Z_3/Z becomes H itself.
    const auto H2 = H.Squared();
    const auto H3 = H2 * H;
    const auto U_1H2 = U_1 * H2;
    const auto X_3 = (r.Squared() - H3).Subtract<4>(2_c * U_1H2);
    const auto Y_3 = (r * U_1H2.Subtract<10>(X_3)).Subtract<2>(S_1 * H3);
    const auto Z_3 = Z * H;

    return {{X_3, Y_3, Z_3}, H};
  }

  // Add two Jacobian points on the curve
  [[gnu::always_inline]] friend constexpr JacobianPoint operator+(const Affine& lhs, const JacobianPoint& rhs) {
    // Affine operands are table entries or fixed points, never infinity by construction; the
    // representation test costs two carry-folds per ladder add, so assert instead of branching.
    Assert(!lhs.IsInfinity());
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
      if (r == 0) return JacobianPoint{lhs}.Double();  // Coincident operands: the shallow doubling formula.
      else return {};
    }

    // 5M, 2S. Z_3 = Z.H rather than the squared 2ZH form: one more mul, but X_3/Y_3 shed their
    // 4x/8x scale factors and a Times level off the critical chain (see Double()).
    const auto H2 = H.Squared();
    const auto H3 = H2 * H;
    const auto U_1H2 = U_1 * H2;
    const auto X_3 = (r.Squared() - H3).Subtract<4>(2_c * U_1H2);
    const auto Y_3 = (r * U_1H2.Subtract<10>(X_3)).Subtract<2>(S_1 * H3);
    const auto Z_3 = rhs.Z * H;
    return {X_3, Y_3, Z_3};
  }
  // a = 0. Double: 3M, 4S, 1 half; Add: 8M, 3S.

  [[gnu::always_inline]] constexpr JacobianPoint Double() const {
    if (IsInfinity()) return {};

    // Shallow-dependency doubling (a = 0):
    //   L = (3/2).X², S = Y², T = -(X.S), X3 = L² + 2T, Y3 = -(L.(X3 + T) + S²), Z3 = Y.Z
    // L and S start in parallel and the critical path is ~3 field ops (S -> T -> X3 -> Y3) vs ~5
    // for the classic Y^4 chain; in the ladder's dependent double run, latency is throughput.
    const auto S = Y.Squared();
    const auto L = X.Squared().Times<3>().Half();
    const auto T = (X * S).Negate<2>();
    const auto X_3 = L.Squared() + 2_c * T;
    const auto Y_3 = (L * (X_3 + T) + S.Squared()).Negate<4>();
    const auto Z_3 = Y * Z;
    return {X_3, Y_3, Z_3};
  }
  // a = 0. 3M, 4S, 1 half: more muls than the 1M+7S form but ~2 fewer levels of dependency.

  friend constexpr JacobianPoint operator+(const JacobianPoint& lhs, const Affine& rhs) { return rhs + lhs; }
  friend constexpr JacobianPoint operator-(const Affine& lhs, const JacobianPoint& rhs) { return lhs + (-rhs); }
  friend constexpr JacobianPoint operator-(const JacobianPoint& lhs, const Affine& rhs) { return (-rhs) + lhs; }
  friend constexpr JacobianPoint operator-(const JacobianPoint& lhs, const JacobianPoint& rhs) { return lhs + (-rhs); }

  constexpr JacobianPoint& operator+=(const JacobianPoint& rhs) { return *this = *this + rhs; }
  constexpr JacobianPoint& operator+=(const Affine& rhs) { return *this = rhs + *this; }
  constexpr JacobianPoint& operator-=(const Affine& rhs) { return *this = *this - rhs; }
  constexpr JacobianPoint& operator-=(const JacobianPoint& rhs) { return *this = *this - rhs; }

  Element X;
  Element Y;
  Element Z;
};

}  // namespace hornet::crypto::ecdsa
