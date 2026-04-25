#pragma once

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <random>

#include "hornetlib/crypto/fp.h"
#include "hornetlib/util/hex.h"

namespace hornet::crypto::ecdsa {

template <int kBits, const UIntW<kBits>& p, const UIntW<kBits>& a, const UIntW<kBits>& b,
          const UIntW<kBits>& Gx, const UIntW<kBits>& Gy, const UIntW<kBits>& n>
class Curve
{
public:
    using Mod_p = Fp<kBits, p>;
    using Mod_n = Fp<kBits, n>;
    using Wide = typename Mod_p::Type;
    using Signature = std::pair<Wide, Wide>;
    static_assert(p > n);

    class Point
    {
    public:
        constexpr Point() {}
        constexpr Point(const Point& rhs) : x(rhs.x), y(rhs.y) {}
        constexpr Point(Point&& rhs) : x(std::move(rhs.x)), y(std::move(rhs.y)) {}
        constexpr Point(const Mod_p& x, const Mod_p& y) : x(x), y(y) {}

        constexpr bool IsInfinity() const
        {
            return x == 0 && y == 0;
        }

        friend Point operator -(const Point& lhs)
        {
            return { lhs.x, -lhs.y };
        }

        // Add two points on the curve
        friend Point operator +(const Point& lhs, const Point& rhs)
        {
            if (lhs.IsInfinity() || rhs.IsInfinity())
                return { lhs.x + rhs.x, lhs.y + rhs.y };
            else if (lhs.x != rhs.x)
            {
                const Mod_p lambda = (rhs.y - lhs.y) / (rhs.x - lhs.x);
                const Mod_p x3 = lambda.Squared() - lhs.x - rhs.x;
                const Mod_p y3 = lambda * (lhs.x - x3) - lhs.y;
                return { x3, y3 };
            }
            else if (lhs.y == -rhs.y)
                return {};
            else
            {
                // Add a (non-infinity) point to itself 
                const Mod_p lambda = (3 * lhs.x.Squared() + a) / (lhs.y + lhs.y);
                const Mod_p x3 = lambda.Squared() - (lhs.x + lhs.x);
                const Mod_p y3 = lambda * (lhs.x - x3) - lhs.y;
                return { x3, y3 };
            }
        }

        Point& operator =(const Point& rhs)
        {
            x = rhs.x;
            y = rhs.y;
            return *this;
        }

        Point& operator =(Point&& rhs)
        {
            x = std::move(rhs.x);
            y = std::move(rhs.y);
            return *this;
        }

        Point& operator +=(const Point& rhs)
        {
            return *this = *this + rhs;
        }

        // Scalar multiplication
        friend Point operator *(const Wide& scalar, const Point& pt)
        {
            // Scalar multiplication of elliptic curve points can be computed efficiently using the 
            // addition rule together with the double-and-add algorithm
            Point sum;
            Point power = pt;
            for (int bitIndex = 0; bitIndex < kBits; ++bitIndex)
            {
                if (scalar.GetBit(bitIndex))
                    sum += power;
                power += power;
            }
            return sum;
        }

        friend Point operator *(const Mod_n& scalar, const Point& pt)
        {
            return scalar.x * pt;
        }

        Mod_p x, y;
    };

    static constexpr Point G = { Gx, Gy };

    inline static constexpr bool IsOnCurve(const Point& point)
    {
        if (point.IsInfinity())
            return true;
        const auto lhs = point.y.Squared();
        const auto rhs = (point.x.Squared() + a) * point.x + b;
        return lhs == rhs;
    }

    static_assert(IsOnCurve(G));

    inline static bool IsPublicKeyValid(const Point& publicKey)
    {
        if (publicKey.IsInfinity())
            return false;
        if (publicKey.x.x >= p || publicKey.y.x >= p)
            return false;
        if (!IsOnCurve(publicKey))
            return false;
        if (!(n * publicKey).IsInfinity())
            return false;
        return true;
    }

    inline static bool VerifySignature(const Point& publicKey, const Signature& signature, const std::array<uint8_t, kBits / 8>& hashedMessage)
    {
        return VerifySignatureImpl(publicKey, signature, HashToInt(hashedMessage));
    }

private:
    inline static bool VerifySignatureImpl(const Point& publicKey, const Signature& signature, const Mod_n& e)
    {
        if (!IsPublicKeyValid(publicKey))
            return false;
        if (signature.first == 0 || signature.first >= n)
            return false;
        if (signature.second == 0 || signature.second >= n)
            return false;
        const Mod_n r = signature.first, s = signature.second;
        const auto sinv = s.Inverse();
        const auto u1 = e * sinv;
        const auto u2 = r * sinv;
        const Point R = u1 * G + u2 * publicKey;
        if (R.IsInfinity())
            return false;
        return R.x.x == r.x;
    }

    template <size_t Size>
    inline static Wide HashToInt(const std::array<uint8_t, Size>& hash)
    {
        static_assert(Size == kBits / 8);

        auto bytes = hash;
        std::reverse(bytes.begin(), bytes.end());
        return Wide{bytes};
    }

    static consteval bool IsNonSingularCurve() {
        constexpr auto a2 = a.MultiplyWide(a);
        const auto discriminant = a2.MultiplyWide(a.template ZeroExtend<kBits * 2>()) * 4 +
                                  b.MultiplyWide(b).template ZeroExtend<kBits * 4>() * 27;
        return discriminant != decltype(discriminant)::Zero();
    }

    static_assert(IsNonSingularCurve());
};

namespace constants {
// Values copied from p9 of https://www.secg.org/sec2-v2.pdf
static constexpr UIntW<256>  p = "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f"_h256;
static constexpr UIntW<256>  a = "0000000000000000000000000000000000000000000000000000000000000000"_h256;
static constexpr UIntW<256>  b = "0000000000000000000000000000000000000000000000000000000000000007"_h256;
static constexpr UIntW<256> Gx = "79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"_h256;
static constexpr UIntW<256> Gy = "483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8"_h256;
static constexpr UIntW<256>  n = "fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141"_h256;
}
 
using secp256k1 = Curve<256, constants::p, constants::a, constants::b, constants::Gx, constants::Gy, constants::n>;

}  // namespace hornet::crypto::ecdsa
