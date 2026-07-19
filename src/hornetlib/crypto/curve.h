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

#include "hornetlib/crypto/element.h"
#include "hornetlib/crypto/fp.h"
#include "hornetlib/crypto/glv.h"
#include "hornetlib/crypto/point.h"
#include "hornetlib/crypto/reduce.h"
#include "hornetlib/crypto/scale.h"
#include "hornetlib/crypto/secp256k1.h"
#include "hornetlib/crypto/uintw.h"
#include "hornetlib/util/hex.h"

namespace hornet::crypto::ecdsa {

class Curve {
 public:
  static constexpr int kBits = secp256k1::kBits;
  using Affine = AffinePoint;
  using Mod_p = FieldElement;
  using Mod_n = Fp<kBits, secp256k1::n>;
  using Wide = Uint256;
  using Signature = std::pair<Wide, Wide>;
  using Point = JacobianPoint;
  static_assert(secp256k1::p > secp256k1::n);

  class PublicKey {
   public:
    operator const Affine&() const { return point_; }

   private:
    friend class Curve;
    explicit PublicKey(Affine point) : point_(std::move(point)) {}
    Affine point_;
  };

  static constexpr Affine G = {secp256k1::Gx, secp256k1::Gy};

  // Builds the fixed-base wNAF tables of odd multiples of G and phi(G) used by verification.
  // If not called explicitly, the tables are built lazily on demand.
  static void BuildGeneratorTable(int width = 12) {
    using namespace secp256k1;
    g_table_.resize(std::size_t{1} << (width - 1));
    PrecomputeTableAffine(G, std::span{g_table_});
    phi_g_table_.resize(g_table_.size());
    MakePhiTable(std::span{g_table_}, std::span{phi_g_table_});
  }

  inline static constexpr bool IsOnCurve(const Point& point) { return point.IsOnCurve(); }

  inline static std::optional<PublicKey> PublicKeyFromSEC1(std::span<const uint8_t> bytes) {
    using namespace secp256k1;
    constexpr int kBytes = kBits >> 3;
    if (bytes.empty()) return std::nullopt;

    if (bytes[0] == 0x02 || bytes[0] == 0x03) {
      constexpr int kExpectedBytes = 1 + kBytes;
      if (std::ssize(bytes) != kExpectedBytes) return std::nullopt;

      const Wide x = Wide::FromBigEndianBytes(bytes.subspan(1, kBytes));
      if (x >= p) return std::nullopt;

      const bool even_y = (bytes[0] & 1) == 0;
      const Mod_p x_fp{x};
      const Mod_p y2 = x_fp.Squared() * x_fp + b;

      // Recover y from y^2 mod p and select the root matching even_y.
      const auto root = y2.SquareRoot();
      if (!root) return std::nullopt;
      const bool root_even = (root->Words()[0] & 1) == 0;
      const Mod_p y = (root_even == even_y) ? *root : -*root;
      const Point point{x, y};
      if (IsValidPublicKey(point)) return PublicKey{point};
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
    // GLV: split u1, u2 via the lambda endomorphism, then a 4-term Strauss over the fixed
    // G/phi(G) tables. Halves the shared doublings (docs/secp256k1-performance-history.md).
    return VerifySignatureImpl(public_key, signature, HashToInt(hashed_message),
                               [](const Wide& u1, const Wide& u2, const Affine& Q) {
                                 const GlvTerm<std::span<const Affine>> g_term{
                                     SplitLambda(u1), GeneratorTable(), PhiGeneratorTable()};
                                 return LinearCombination_GLV(g_term, MakeVariableGlvTerm(SplitLambda(u2), Q));
                               });
  }

  // Verifies using a caller-supplied combiner for R = u1*G + u2*Q. Exists so benchmarks and tests
  // can compare alternative linear-combination strategies (joint NAF, wNAF) against the default GLV
  // path without duplicating the surrounding scalar and normalization logic.
  template <class Combine>
  inline static bool VerifySignatureWith(const PublicKey& public_key, const Signature& signature,
                                         const std::array<uint8_t, kBits / 8>& hashed_message, Combine&& combine) {
    return VerifySignatureImpl(public_key, signature, HashToInt(hashed_message), std::forward<Combine>(combine));
  }

  // For u = x/z^2 (mod p), test whether u = r (mod n).
  inline static constexpr bool IsJacobianXEqual(const Mod_p& x, const Mod_p& z, const Uint256& r) {
    // u == r (mod n) => x == r * z^2 (mod n)
    // Can have p < r + n, or r + n < p < r + 2n
    // Need to test for x == r * z^2 (mod p),
    // otherwise n <= r < p, so test x == (r + n) * z^2 (mod p).
    const auto z2 = z.Squared();
    if (x == z2 * r) return true;

    if (r < secp256k1::p - secp256k1::n) return x == Mod_p{r + secp256k1::n} * z2;
    return false;
  }

 private:
  static inline std::vector<Affine> g_table_{};
  static inline std::vector<Affine> phi_g_table_{};  // odd multiples of phi(G)

  // Returns the fixed-base table, building it once at default width if no explicit build ran.
  static std::span<const Affine> GeneratorTable() {
    static std::once_flag built;
    std::call_once(built, [] {
      if (g_table_.empty()) BuildGeneratorTable();
    });
    return g_table_;
  }

  // Returns the phi(G) table, ensuring the generator tables are built.
  static std::span<const Affine> PhiGeneratorTable() {
    GeneratorTable();
    return phi_g_table_;
  }

  inline static bool IsValidPublicKey(const Point& publicKey) {
    using namespace secp256k1;
    if (publicKey.IsInfinity()) return false;
    if (!IsOnCurve(publicKey)) return false;
    return true;
  }

  template <class Combine>
  inline static bool VerifySignatureImpl(const Affine& publicKey, const Signature& signature, const Mod_n& e,
                                         Combine&& combine) {
    using namespace secp256k1;
    if (signature.first == 0 || signature.first >= n) return false;
    if (signature.second == 0 || signature.second >= n) return false;
    const Mod_n r = signature.first, s = signature.second;
    const auto sinv = s.Inverse();
    const auto u1 = e * sinv;
    const auto u2 = r * sinv;
    const Point R = combine(u1.x, u2.x, publicKey);
    if (R.IsInfinity()) return false;
    return IsJacobianXEqual(R.X, R.Z, r.x);
  }

  template <size_t Size> inline static Wide HashToInt(const std::array<uint8_t, Size>& hash) {
    static_assert(Size == kBits / 8);
    const Uint256 x = Wide::FromBigEndianBytes(std::span<const uint8_t>{hash});
    return ReduceModuloN(x);
  }
};

// Evaluated here (not in-class) because a non-template class is incomplete until its closing brace,
// so its members can't be used in a constant expression inside the body.
static_assert(Curve::IsOnCurve(Curve::G));

}  // namespace hornet::crypto::ecdsa
