#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <utility>
#include <vector>

#include "hornetlib/crypto/point.h"
#include "hornetlib/crypto/scale.h"
#include "hornetlib/crypto/fp.h"
#include "hornetlib/crypto/secp256k1_constants.h"
#include "hornetlib/crypto/uintw.h"
#include "hornetlib/util/hex.h"

namespace hornet::crypto::ecdsa {

template <int kBits, const UIntW<kBits>& p, const UIntW<kBits>& a, const UIntW<kBits>& b, const UIntW<kBits>& Gx,
          const UIntW<kBits>& Gy, const UIntW<kBits>& n>
class Curve {
 public:
  using Affine = AffinePoint<kBits, p, a>;
  using Mod_p = Fp<kBits, p>;
  using Mod_n = Fp<kBits, n>;
  using Wide = typename Mod_p::Type;
  using Signature = std::pair<Wide, Wide>;
  using Point = JacobianPoint<kBits, p, a>;
  static_assert(p > n);

  class PublicKey {
   public:
    operator const Affine&() const { return point_; }
   private:
    friend class Curve;
    explicit PublicKey(Affine point) : point_(std::move(point)) {}
    Affine point_; 
  };

  static constexpr Affine G = {Mod_p{Gx}, Mod_p{Gy}};

  // Builds the fixed-base wNAF table of odd multiples of G used by verification.
  // If not called explicitly, the table is built lazily on demand.
  static void BuildGeneratorTable(int width = 10) {
    g_table_.resize(std::size_t{1} << (width - 1));
    PrecomputeTableAffine(G, std::span{g_table_});
  }

  inline static constexpr bool IsOnCurve(const Point& point) {
    return point.template IsOnCurve<b>();
  }

  static_assert(IsOnCurve(G));

  inline static std::optional<PublicKey> PublicKeyFromSEC1(std::span<const uint8_t> bytes) {
    constexpr int kBytes = kBits >> 3;
    if (bytes.empty()) return std::nullopt;

    if (bytes[0] == 0x02 || bytes[0] == 0x03) {
      if constexpr (!Mod_p::HasSquareRoot()) return std::nullopt;
      else {
        constexpr int kExpectedBytes = 1 + kBytes;
        if (std::ssize(bytes) != kExpectedBytes) return std::nullopt;

        const Wide x = Wide::FromBigEndianBytes(bytes.subspan(1, kBytes));
        if (x >= p) return std::nullopt;

        const bool even_y = (bytes[0] & 1) == 0;
        const Mod_p x_fp{x};
        const Mod_p y2 = (x_fp.Squared() + a) * x_fp + b;

        // Recover y from y^2 mod p and select the root matching even_y.
        const auto root = y2.SquareRoot();
        if (!root) return std::nullopt;
        const Mod_p y = (detail::IsEven(root->x) == even_y) ? *root : -*root;
        const Point point{x, y.x};
        if (IsValidPublicKey(point)) return PublicKey{point};
      }
    } else if (bytes[0] == 0x04) {
      constexpr int kExpectedBytes = 1 + 2 * kBytes;
      if (std::ssize(bytes) != kExpectedBytes) return std::nullopt;

      const auto x = Wide::FromBigEndianBytes(bytes.subspan(1, kBytes));
      const auto y = Wide::FromBigEndianBytes(bytes.subspan(1 + kBytes, kBytes));
      if (x >= p || y >= p) return std::nullopt;
      const Point point{x, y};
      if (IsValidPublicKey(point)) return PublicKey{point};
    }
    return std::nullopt;
  }

  inline static bool VerifySignature(const PublicKey& public_key, const Signature& signature,
                                     const std::array<uint8_t, kBits / 8>& hashed_message) {
    // Default path is wNAF over the fixed-base generator table (faster end to end than joint
    // NAF; see docs/secp256k1-performance-history.md). Q gets a narrow per-call table inside
    // LinearCombination_wNAF.
    const std::span<const Affine> g_table = GeneratorTable();
    return VerifySignatureImpl(public_key, signature, HashToInt(hashed_message),
                               [g_table](const Wide& u1, const Wide& u2, const Affine& Q) {
                                 return hornet::crypto::ecdsa::LinearCombination_wNAF(u1, g_table, u2, Q);
                               });
  }

  // Verifies using a caller-supplied combiner for R = u1*G + u2*Q. Exists so benchmarks and
  // tests can compare alternative linear-combination strategies (e.g. joint NAF) against the
  // default wNAF path without duplicating the surrounding scalar and normalization logic.
  template <class Combine>
  inline static bool VerifySignatureWith(const PublicKey& public_key, const Signature& signature,
                                         const std::array<uint8_t, kBits / 8>& hashed_message, Combine&& combine) {
    return VerifySignatureImpl(public_key, signature, HashToInt(hashed_message), std::forward<Combine>(combine));
  }

 private:
  static inline std::vector<Affine> g_table_{};

  // Returns the fixed-base table, building it once at default width if no explicit build ran.
  static std::span<const Affine> GeneratorTable() {
    static std::once_flag built;
    std::call_once(built, [] { if (g_table_.empty()) BuildGeneratorTable(); });
    return g_table_;
  }

  inline static bool IsValidPublicKey(const Point& publicKey) {
    if (publicKey.IsInfinity()) return false;
    if (!IsOnCurve(publicKey)) return false;
    if (!(n * publicKey).IsInfinity()) return false;
    return true;
  }

  template <class Combine>
  inline static bool VerifySignatureImpl(const Affine& publicKey, const Signature& signature, const Mod_n& e,
                                         Combine&& combine) {
    if (signature.first == 0 || signature.first >= n) return false;
    if (signature.second == 0 || signature.second >= n) return false;
    const Mod_n r = signature.first, s = signature.second;
    const auto sinv = s.Inverse();
    const auto u1 = e * sinv;
    const auto u2 = r * sinv;
    const Point R = combine(u1.x, u2.x, publicKey);
    if (R.IsInfinity()) return false;
    return R.NormalizedX().x.Modulo(n) == r.x;
  }

  template <size_t Size>
  inline static Wide HashToInt(const std::array<uint8_t, Size>& hash) {
    static_assert(Size == kBits / 8);
    return Wide::FromBigEndianBytes(std::span<const uint8_t>{hash});
  }

  static consteval bool IsNonSingularCurve() {
    constexpr auto a2 = a.MultiplyWide(a);
    const auto discriminant = a2.MultiplyWide(a.template ZeroExtend<kBits * 2>()) * 4 +
                              b.MultiplyWide(b).template ZeroExtend<kBits * 4>() * 27;
    return discriminant != decltype(discriminant)::Zero();
  }

  static_assert(IsNonSingularCurve());
};

using secp256k1 = Curve<256, constants::p, constants::a, constants::b, constants::Gx, constants::Gy, constants::n>;

}  // namespace hornet::crypto::ecdsa
