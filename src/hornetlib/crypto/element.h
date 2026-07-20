#pragma once

#include <array>
#include <cstdint>
#include <exception>
#include <tuple>

#include "hornetlib/crypto/reduce.h"
#include "hornetlib/crypto/secp256k1.h"
#include "hornetlib/crypto/uintw.h"
#include "hornetlib/util/assert.h"

namespace hornet::crypto::ecdsa {

#ifdef HORNETLIB_CHECK_MAGNITUDES
  static constexpr bool kCheckMagnitudes = HORNETLIB_CHECK_MAGNITUDES;
#else
  static constexpr bool kCheckMagnitudes = false;
#endif

template <bool kCheck = false> class Magnitude {
 public:
  constexpr int Get() const { return magnitude_; }
  constexpr void Set(int magnitude) { magnitude_ = magnitude; }
 private:
  int magnitude_ = 1;
};

template <> 
class Magnitude<false> {
 public:
  constexpr int Get() const { return 0; }
  constexpr void Set(int) {}
};

class FieldElement {
 public:
  static constexpr int kWords = 5;
  static constexpr int kMaxMagnitude = 1 << 12;  // 4096
  static constexpr int kMaxProductMagnitude = 8191;
  using Array = std::array<uint64_t, kWords>;

  constexpr FieldElement() : words_{} {}
  constexpr FieldElement(const FieldElement& rhs) = default;
  constexpr FieldElement(const Array& array, [[maybe_unused]] int magnitude = 1) : words_{array} {
    magnitude_.Set(magnitude);
    if constexpr (kCheckMagnitudes) CheckMagnitudes();
  }
  constexpr FieldElement(uint64_t rhs) : words_{} {
    words_[0] = rhs & kMask52;
    words_[1] = rhs >> 52;
    if constexpr (kCheckMagnitudes) CheckMagnitudes();
  }
  constexpr FieldElement(const Uint256& rhs) {
    Assert(rhs < secp256k1::p);
    const auto& rwords = rhs.Words();
    words_[0] = rwords[0] & kMask52;                                // r0[51..0]
    words_[1] = ((rwords[0] >> 52) | (rwords[1] << 12)) & kMask52;  // r1[39..0] | r0[63..52]
    words_[2] = ((rwords[1] >> 40) | (rwords[2] << 24)) & kMask52;  // r2[27..0] | r1[63..40]
    words_[3] = ((rwords[2] >> 28) | (rwords[3] << 36)) & kMask52;  // r3[15..0] | r2[63..28]
    words_[4] = rwords[3] >> 16;                                    // r3[63..16]   
    if constexpr (kCheckMagnitudes) CheckMagnitudes();
  }

  template <int kMagnitude>
  constexpr FieldElement Negate() const {
    if constexpr (kCheckMagnitudes) {
      for (int i = 0; i < kWords; ++i)
        if (words_[i] > p52[i] * (kMagnitude + 1))
          throw std::out_of_range{"Insufficient static magnitude in FieldElement::Negate."};
    }
    Array result;
    for (int i = 0; i < kWords; ++i) result[i] = p52[i] * (kMagnitude + 1) - words_[i];
    return { result, kMagnitude + 1 };
  }

  template <int kMagnitude>
  constexpr FieldElement Subtract(const FieldElement& rhs) const {
    return *this + rhs.template Negate<kMagnitude>();
  }

  constexpr FieldElement& operator=(const FieldElement& rhs) = default;

  constexpr bool operator==(const FieldElement& rhs) const {
    return (*this + rhs.NormalizeWeak().template Negate<2>()).NormalizesToZero();
  }

  constexpr bool operator==(uint64_t rhs) const {
    if (rhs == 0) return NormalizesToZero();
    return operator==(FieldElement{rhs});
  }

  constexpr const Array& Words() const { return words_; }

  Uint256 Pack() const {
    if constexpr (kCheckMagnitudes) CheckMagnitudes();
    std::array<uint64_t, 4> words;
    const auto normalized = Normalize();
    const auto& rwords = normalized.Words();
    words[0] = rwords[0] | (rwords[1] << 52);          // r1[11..0] | r0[51..0]
    words[1] = (rwords[1] >> 12) | (rwords[2] << 40);  // r2[23..0] | r1[51..12]
    words[2] = (rwords[2] >> 24) | (rwords[3] << 28);  // r3[35..0] | r2[51..24]
    words[3] = (rwords[3] >> 36) | (rwords[4] << 16);  // r4[47..0] | r3[51..36]
    return Uint256{words};
  }

  FieldElement Inverse() const {
    if constexpr (kCheckMagnitudes) CheckMagnitudes();
    return detail::InvertModuloOdd<256, secp256k1::p>(Pack());
  }

  FieldElement operator /(const FieldElement& rhs) const {
    if constexpr (kCheckMagnitudes) CheckMagnitudes(), rhs.CheckMagnitudes();
    return detail::DivideModuloOdd<256, secp256k1::p>(Pack(), rhs.Pack());
  }

  template <int k> 
  constexpr FieldElement LShift() const {
    if constexpr (kCheckMagnitudes) CheckMagnitudes();
    Array result;
    for (int i = 0; i < kWords; ++i) result[i] = words_[i] << k;
    return { result, magnitude_.Get() << k };
  }

  constexpr FieldElement Half() const {
    if constexpr (kCheckMagnitudes) {
      // The limbwise +p needs headroom: w_i + p52_i < (m+1).2^52 must fit 64 bits => m <= 4095.
      if (magnitude_.Get() > kMaxMagnitude - 1) throw std::out_of_range{"FieldElement::Half magnitude exceeded 4,095."};
      CheckMagnitudes();
    }
    // Exact halving of the representative: t = w + (w odd ? p : 0) is even, and t/2 _= w.2^-1
    // (mod p) in either case. Halve limbwise, carrying each limb's parity bit down as bit 51.
    const uint64_t mask = uint64_t{0} - (words_[0] & 1);
    Array t, result;
    for (int i = 0; i < kWords; ++i) t[i] = words_[i] + (p52[i] & mask);  // < (m+1).2^52
    for (int i = 0; i < kWords - 1; ++i) result[i] = (t[i] >> 1) + ((t[i + 1] & 1) << 51);
    result[kWords - 1] = t[kWords - 1] >> 1;
    // result_i < (m+2).2^51 and result_4 < (m+1).2^47, both within magnitude ((m+1)>>1)+1.
    return { result, ((magnitude_.Get() + 1) >> 1) + 1 };
  }

  constexpr FieldElement operator-() const {
    if constexpr (kCheckMagnitudes) CheckMagnitudes();
    return Negate<2>();
  }

  constexpr FieldElement operator+(const FieldElement& rhs) const {
    if constexpr (kCheckMagnitudes) CheckMagnitudes();
    Array result;
    for (int i = 0; i < kWords; ++i) result[i] = words_[i] + rhs.words_[i];
    return { result, magnitude_.Get() + rhs.magnitude_.Get() };
  }

  constexpr FieldElement operator-(const FieldElement& rhs) const {
    return *this + (-rhs);
  }

  template <int k> FieldElement constexpr Times() const {
    if constexpr (kCheckMagnitudes) CheckMagnitudes();
    if constexpr (k < 0) return -Times<-k>();
    if constexpr (k == 0) return FieldElement{};
    if constexpr (k == 1) return *this;
    if constexpr (util::IsPowerOf2(k)) return LShift<util::Log2(k)>();

    Array result;
    for (int i = 0; i < kWords; ++i) result[i] = words_[i] * k;
    return { result, magnitude_.Get() * k };
  }

  template <int k> constexpr auto operator*(std::integral_constant<int, k>) const { return Times<k>(); }

  template <int k> friend constexpr auto operator*(std::integral_constant<int, k>, const FieldElement& x) {
    return x.template Times<k>();
  }

  template <int k> constexpr auto operator<<(std::integral_constant<int, k>) const { return LShift<k>(); }

  constexpr FieldElement& operator*=(const FieldElement& rhs) {
    if constexpr (kCheckMagnitudes) CheckMagnitudes(), rhs.CheckMagnitudes();
    return *this = *this * rhs;
  }

  [[gnu::always_inline]] constexpr FieldElement operator*(const FieldElement& rhs) const {
    if constexpr (kCheckMagnitudes) {
      CheckMagnitudes(), rhs.CheckMagnitudes();
      if (magnitude_.Get() * rhs.magnitude_.Get() > kMaxProductMagnitude) 
        util::ThrowOutOfRange("Exceeded product magnitude ", magnitude_.Get() * rhs.magnitude_.Get(), " in 5x52 FieldElement.");
    }
    // Rolling-fold 5x52 product. a * b = sum_{t=0}^8 S_t * 2^(52*t), with column sums
    // S_t = sum_{i+j=t} a_i.b_j. Since p = 2^256 - c_p, 2^260 _= 16 c_p = kFold260 =: R (mod p),
    // so high column t+5 folds into low column t by R. Two accumulators walk the column pairs in
    // lockstep — c gathers column t, d gathers column t+5, and d's low 52 bits fold into c the
    // moment they exist — so fold, carry propagation and limb extraction are one interleaved sweep
    // and no column is ever stored. The sweep is rotated — (3,8), (4), (0,5), (1,6), (2,7) — so
    // limb 4's 2^256 boundary is folded mid-stream and the tail needs no ripple.
    //
    // Bounds use m = m_a.m_b <= 8191 < 2^13, with a_i.b_j < m.2^104 (i,j < 4), < m.2^100 (one
    // index 4), < m.2^96 (both). The largest accumulator is 4m.2^104 + 2^101 < 2^120, admissible
    // up to m < 2^22, so the m <= 8191 contract is unchanged and now slack.
    const auto& a = words_;
    const auto& b = rhs.words_;

    // Columns 3 and 8. S_8 = a4.b4 alone; its weight 2^416 = 2^260.2^156 folds into column 3 by R.
    // R.S_8 needs up to 37+128 bits, so split: R.low64 here, high64 deferred one column up by
    // R.2^12 (2^64.2^156 = 2^12.2^208).
    __uint128_t d = __uint128_t{a[0]} * b[3] + __uint128_t{a[1]} * b[2] + __uint128_t{a[2]} * b[1] +
                    __uint128_t{a[3]} * b[0];                           // < 4m.2^104
    __uint128_t c = __uint128_t{a[4]} * b[4];                           // < m.2^96
    d += __uint128_t{kFold260} * static_cast<uint64_t>(c);              // < 4m.2^104 + 2^101
    const uint64_t s8_high = static_cast<uint64_t>(c >> 64);            // < m.2^32
    const uint64_t t3 = static_cast<uint64_t>(d) & kMask52;
    d >>= 52;                                                           // < 4m.2^52 + 2^49

    // Column 4, plus the deferred high half of S_8.
    d += __uint128_t{a[0]} * b[4] + __uint128_t{a[1]} * b[3] + __uint128_t{a[2]} * b[2] +
         __uint128_t{a[3]} * b[1] + __uint128_t{a[4]} * b[0];           // + 3.2m.2^104
    d += __uint128_t{kFold260 << 12} * s8_high;                         // + m.2^81 => < 3.3m.2^104
    uint64_t t4 = static_cast<uint64_t>(d) & kMask52;
    d >>= 52;                                                           // < 3.3m.2^52
    const uint64_t tx = t4 >> 48;  // Limb 4's top 4 bits, weight 2^256.
    t4 &= kMask48;

    // Columns 0 and 5. Column 5's low 52 bits have weight 2^260 = 2^4.2^256, so glue them above tx
    // and fold the combined 2^256 overflow into column 0 by c_p — absorbing the final overflow
    // pass into the sweep.
    c = __uint128_t{a[0]} * b[0];                                       // < m.2^104
    d += __uint128_t{a[1]} * b[4] + __uint128_t{a[2]} * b[3] + __uint128_t{a[3]} * b[2] +
         __uint128_t{a[4]} * b[1];                                      // + 2.2m.2^104 => < 2.3m.2^104
    const uint64_t u0 = ((static_cast<uint64_t>(d) & kMask52) << 4) | tx;  // < 2^56, weight 2^256
    d >>= 52;                                                           // < 2.3m.2^52
    c += __uint128_t{u0} * kFold256;                                    // + 2^89 => < m.2^104 + 2^89
    Array result;
    result[0] = static_cast<uint64_t>(c) & kMask52;
    c >>= 52;                                                           // < m.2^52 + 2^37

    // Columns 1 and 6.
    c += __uint128_t{a[0]} * b[1] + __uint128_t{a[1]} * b[0];           // < 2m.2^104 + m.2^52 + 2^37
    d += __uint128_t{a[2]} * b[4] + __uint128_t{a[3]} * b[3] +
         __uint128_t{a[4]} * b[2];                                      // + 1.3m.2^104 => < 1.4m.2^104
    c += __uint128_t{kFold260} * (static_cast<uint64_t>(d) & kMask52);  // + 2^89
    d >>= 52;                                                           // < 1.4m.2^52
    result[1] = static_cast<uint64_t>(c) & kMask52;
    c >>= 52;                                                           // < 2m.2^52 + 2^38

    // Columns 2 and 7.
    c += __uint128_t{a[0]} * b[2] + __uint128_t{a[1]} * b[1] +
         __uint128_t{a[2]} * b[0];                                      // < 3m.2^104 + 2m.2^52 + 2^38
    d += __uint128_t{a[3]} * b[4] + __uint128_t{a[4]} * b[3];           // + 2m.2^100 => < m.2^101 + 1.4m.2^52
    c += __uint128_t{kFold260} * (static_cast<uint64_t>(d) & kMask52);  // + 2^89
    d >>= 52;                                                           // < m.2^49 + 1.4m < 2^62
    result[2] = static_cast<uint64_t>(c) & kMask52;
    c >>= 52;                                                           // < 3m.2^52 + 2^38

    // Close. Three shifts moved d from column 5 to column 8, so like S_8 it folds into column 3 by
    // R; d < 2^62, so R.d fits without splitting. The stored t3 joins here.
    Assert((d >> 62) == 0);
    c += __uint128_t{kFold260} * static_cast<uint64_t>(d) + t3;         // < m.2^86 + 3m.2^52 + 2^52 < 2^100
    result[3] = static_cast<uint64_t>(c) & kMask52;
    c >>= 52;                                                           // < m.2^34 + 2^2 < 2^47 + eps
    result[4] = static_cast<uint64_t>(c) + t4;                          // < 2^48 + 2^47 + eps

    // Limbs 0..3 < 2^52; limb 4 < 1.5 * 2^48, so magnitude 2 (a true 1 would need limb 4 < 2^48).
    return { result, 2 };
  }

  constexpr std::tuple<Array, uint64_t> CarryFold() const {
    // for t=0..3, w_t < m.2^52 <= 2^64 - 2^12 => m <= 2^12 - 2^-40 => m <= 2^12 - 1.
    Array result;
    uint64_t accumulator = 0;
    for (int t = 0; t < 4; ++t) {
      accumulator = (accumulator >> 52) + words_[t];
      result[t] = accumulator & kMask52;
    }
    accumulator = (accumulator >> 52) + words_[4];
    result[4] = accumulator & kMask48;
    // Since m < 2^12, w_4 < m.2^48 < 2^60, so overflow < (2^60 + 2^12) >> 48 < 2^12 + 2^-36 < 4096.
    uint64_t overflow = accumulator >> 48;  // < 2^12 at bit position 256.
    return { result, overflow };
  }

  constexpr FieldElement NormalizeWeak() const {
    if constexpr (kCheckMagnitudes) {
      // Requires words <= 2^64 - 2^12 = 2^12.2^52 - 2^12 <= magnitude <= 2^12 - 1 = 4,095.
      if (magnitude_.Get() > 4095) throw std::out_of_range{"FieldElement::NormalizeWeak magnitude exceeded 4,095."};
      CheckMagnitudes();
    }
    // TODO: Check normalization flag to verify we aren't re-normalizing unnecessarily?

    // for t=0..3, w_t < m.2^52 <= 2^64 - 2^12 => m <= 2^12 - 2^-40 => m <= 2^12 - 1.
    auto [result, overflow] = CarryFold();
    Assert(overflow < 4096);
    // overflow * c_p < 2^12 * 2^33 < 2^45
    result[0] += overflow * kFold256; // < 2^52 + 2^45 < 2^53.
    return { result, 2 };
  }

  constexpr FieldElement Normalize() const {
    if constexpr (kCheckMagnitudes) CheckMagnitudes();
    const auto weak = NormalizeWeak();
    // Has w_0 < 2^53, w_1..w_3 < 2^52, w_4 < 2^48, so weak < 2^256 + 2^52.
    
    // If w < p, then overflow_0 = 0 and s = w + c_p < 2^256, so overflow_1 = 0 => return w
    // If p <= w < 2^256 < 2p, then overflow_0 = 0 and s = w + c_p >= p + c_p = 2^256, so overflow_1 = 1 => return s
    // If 2^256 <= w < 2p, then overflow_0 = 1 and s = w + c_p - 2^256 = w - p < p, so overflow_1 = 0 => return s
    // i.e. return (overflow_0 | overflow_1) ? s : w
  
    auto [result, overflow] = weak.CarryFold();
    // Since m = 2, w_4 < 2.2^48 < 2^49, so overflow < (2^49 + 2^12) >> 48 < 2 + 2^-36 < 2.
    bool overflow_0 = overflow != 0;  // 0 or 1 at bit position 256.
    
    Array s;
    uint64_t accumulator = result[0] + kFold256;
    for (int t = 0; t < 4; ++t) {
      s[t] = accumulator & kMask52;
      accumulator = (accumulator >> 52) + result[t+1];
    }
    s[4] = accumulator & kMask48;
    bool overflow_1 = (accumulator >> 48) != 0;

    return { (overflow_0 | overflow_1) ? s : result, 1 };
  }

  constexpr bool NormalizesToZero() const {
    if constexpr (kCheckMagnitudes) CheckMagnitudes();

    const auto weak = NormalizeWeak();
    // Has w_0 < 2^53, w_1..w_3 < 2^52, w_4 < 2^48, so weak < 2^256 + 2^52.
    
    auto [result, overflow] = weak.CarryFold();
    // Since m = 2, w_4 < 2.2^48 < 2^49, so overflow < (2^49 + 2^12) >> 48 < 2 + 2^-36 < 2.
    bool overflow_0 = overflow != 0;  // 0 or 1 at bit position 256.
    if (overflow_0) return false;  // p < 2^256 <= w < 2^256 + 2^52 < 2p

    // Zero if w==0 or w==p52.
    bool equal_zero = true, equal_p = true;
    for (int i = 0; i < kWords; ++i) {
      equal_zero &= result[i] == 0;
      equal_p &= result[i] == p52[i];
    }
    return equal_zero | equal_p;
  }

  [[gnu::always_inline]] constexpr FieldElement Squared() const {
    if constexpr (kCheckMagnitudes) {
      CheckMagnitudes();
      if (magnitude_.Get() * magnitude_.Get() > kMaxProductMagnitude)
        util::ThrowOutOfRange("Exceeded product magnitude ", magnitude_.Get() * magnitude_.Get(), " in 5x52 FieldElement.");
    }
    // Squaring specialization of operator*'s rolling-fold sweep (see there for the fold identities
    // and per-step bound proofs, which carry over verbatim with m = m_a^2): symmetry halves the
    // cross products, S_t = 2.sum_{i<j, i+j=t} a_i.a_j (+ a_{t/2}^2 for even t), so 15 limb
    // products replace 25. Cross terms double by pre-shifting the low-index limb: the admission
    // m_a^2 <= 8191 gives m_a <= 90, so a_i < 90.2^52 < 2^58.5 and a_i << 1 cannot overflow.
    // Every column sum then meets the same S_t bound as in operator*.
    const auto& a = words_;
    const uint64_t a0_2 = a[0] << 1;
    const uint64_t a1_2 = a[1] << 1;
    const uint64_t a2_2 = a[2] << 1;
    const uint64_t a3_2 = a[3] << 1;

    // Columns 3 and 8.
    __uint128_t d = __uint128_t{a0_2} * a[3] + __uint128_t{a1_2} * a[2];  // < 4m.2^104
    __uint128_t c = __uint128_t{a[4]} * a[4];                             // < m.2^96
    d += __uint128_t{kFold260} * static_cast<uint64_t>(c);                // < 4m.2^104 + 2^101
    const uint64_t s8_high = static_cast<uint64_t>(c >> 64);              // < m.2^32
    const uint64_t t3 = static_cast<uint64_t>(d) & kMask52;
    d >>= 52;

    // Column 4, plus the deferred high half of S_8.
    d += __uint128_t{a0_2} * a[4] + __uint128_t{a1_2} * a[3] + __uint128_t{a[2]} * a[2];
    d += __uint128_t{kFold260 << 12} * s8_high;                           // < 3.3m.2^104
    uint64_t t4 = static_cast<uint64_t>(d) & kMask52;
    d >>= 52;
    const uint64_t tx = t4 >> 48;  // Limb 4's top 4 bits, weight 2^256.
    t4 &= kMask48;

    // Columns 0 and 5.
    c = __uint128_t{a[0]} * a[0];
    d += __uint128_t{a1_2} * a[4] + __uint128_t{a2_2} * a[3];             // < 2.3m.2^104
    const uint64_t u0 = ((static_cast<uint64_t>(d) & kMask52) << 4) | tx;
    d >>= 52;
    c += __uint128_t{u0} * kFold256;
    Array result;
    result[0] = static_cast<uint64_t>(c) & kMask52;
    c >>= 52;

    // Columns 1 and 6.
    c += __uint128_t{a0_2} * a[1];
    d += __uint128_t{a2_2} * a[4] + __uint128_t{a[3]} * a[3];             // < 1.4m.2^104
    c += __uint128_t{kFold260} * (static_cast<uint64_t>(d) & kMask52);
    d >>= 52;
    result[1] = static_cast<uint64_t>(c) & kMask52;
    c >>= 52;

    // Columns 2 and 7.
    c += __uint128_t{a0_2} * a[2] + __uint128_t{a[1]} * a[1];
    d += __uint128_t{a3_2} * a[4];                                        // < m.2^101 + 1.4m.2^52
    c += __uint128_t{kFold260} * (static_cast<uint64_t>(d) & kMask52);
    d >>= 52;                                                             // < 2^62
    result[2] = static_cast<uint64_t>(c) & kMask52;
    c >>= 52;

    // Close, exactly as in operator*.
    Assert((d >> 62) == 0);
    c += __uint128_t{kFold260} * static_cast<uint64_t>(d) + t3;
    result[3] = static_cast<uint64_t>(c) & kMask52;
    c >>= 52;
    result[4] = static_cast<uint64_t>(c) + t4;

    // Same output bounds as operator*: limbs 0..3 < 2^52, limb 4 < 1.5 * 2^48 => magnitude 2.
    return { result, 2 };
  }

  constexpr std::optional<FieldElement> SquareRoot() const {
    // We rely on p = 3 (mod 4) in this implementation, with p an odd prime, which guarantees that (p+1)/4 is an
    // integer and for any quadratic residue x, x^((p+1)/4) (mod p) is a square root of x, via Euler's criterion.
    static constexpr Uint256 kExponent = (secp256k1::p + 1) >> 2;
    static constexpr int kExponentBits = kExponent.SignificantBits();
    
    FieldElement result = 1;
    FieldElement power = *this;
    for (int i = 0; i < kExponentBits; ++i)
    {
      if (kExponent.GetBit(i)) result *= power;
      power = power.Squared();
    }

    // Now we need to check that result.Squared() == x, because otherwise x is a quadratic non-residue, and doesn't
    // have a valid square root.
    if (result.Squared() != *this) return std::nullopt;
    return result.Normalize();
  }

 private:
  [[no_unique_address]] Magnitude<kCheckMagnitudes> magnitude_;

  static constexpr uint64_t kMask52 = (1ull << 52) - 1;
  static constexpr uint64_t kMask48 = (1ull << 48) - 1;
  static constexpr uint64_t kFold256 = secp256k1::c_p.Words()[0];  // < 2^33
  static constexpr uint64_t kFold260 = kFold256 << 4;  // < 2^37

  static constexpr Array p52 = [] {
    Array words;
    for (int i = 0; i < kWords; ++i) words[i] = (secp256k1::p >> (52 * i)).LowBits<64>() & kMask52;
    return words;
  }();

  static_assert([] {
    UInt256 p = UInt256::Zero();
    for (int i = 0; i < kWords; ++i) p += UInt256{p52[i]} << (52 * i);
    return p == secp256k1::p;
  }());

  constexpr void CheckMagnitudes() const {
    if constexpr (!kCheckMagnitudes) return;
    const int mag = magnitude_.Get();
    if (mag > kMaxMagnitude)
        throw std::out_of_range{"Invalid magnitude in 5x52 FieldElement."};

    static constexpr int kBitLengths[kWords] = { 52, 52, 52, 52, 48 };
    for (int i = 0; i < kWords; ++i) 
      if ((words_[i] >> kBitLengths[i]) >= static_cast<uint64_t>(mag)) 
        throw std::out_of_range{"Exceeded magnitude in 5x52 FieldElement."};
  }

  // For kMagnitude > 0, words_[i] < kMagnitude * 2^52 for i<4, and words_[4] < kMagnitude * 2^48.
  // For kMagnitude == 0, words_[i] == 0 for all i.

  Array words_;
};

}  // namespace hornet::crypto::ecdsa
