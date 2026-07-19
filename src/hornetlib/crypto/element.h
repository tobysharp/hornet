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

  constexpr FieldElement operator*(const FieldElement& rhs) const {
    if constexpr (kCheckMagnitudes) {
      CheckMagnitudes(), rhs.CheckMagnitudes();
      if (magnitude_.Get() * rhs.magnitude_.Get() > kMaxProductMagnitude) 
        util::ThrowOutOfRange("Exceeded product magnitude ", magnitude_.Get() * rhs.magnitude_.Get(), " in 5x52 FieldElement.");
    }
    // Full 5x52 limb product with interleaved fold mod p.
    Array result;

    // a * b = sum_{t=0}^8 S_t * 2^(52*t).
    // a_i < m_a * 2^B_i (B_i = 2^52 for i = 0..3, B_4 = 2^48)
    // b_i < m_b * 2^B_i.

    // Accumulate 128-bit column products, S_t:
    __uint128_t S[9] = {};
    for (int t = 0; t <= 8; ++t) {
      for (int i = std::max(0, t - 4); i <= std::min(t, 4); ++i) {
        const int j = t - i;
        S[t] += __uint128_t{words_[i]} * rhs.words_[j];  // S_t < 4m_a.m_b.2^104
      }
    }

    // Let L, H be s.t. a * b = L + 2^260 H.
    // Then H = sum_{t=0}^3 S_{t+5} * 2^(52*t).
    // We know p = 2^256 - c_p, so 2^260 _= 16 c_p (mod p).
    // Let R = 16 c < 2^37. Then for t=5..8, S_t.2^260 _= R.S_t (mod p), which reduces S_t
    // by about 260-37=223 bits. We can therefore fold R.S_t into column t-5.

    __uint128_t remainder = 0;
    for (int t = 0; t <= 4; ++t) {
      if (t < 4) remainder += S[t + 5];
      // In this iteration, we can only pick up the low 64 bits of remainder, since after multiplying
      // by R, we are already at 64+37=101 bits. We can safely leave any remaining bits in remainder until the
      // next iteration, because at this stage each S_i is a 128-bit value at bit position 52*i, so the
      // representation isn't tight yet.
      const uint64_t low = static_cast<uint64_t>(remainder);
      S[t] += __uint128_t{kFold260} * low;  // S_t < 4m_a.m_b.2^104 + 2^101
      remainder = (remainder - low) >> 52;
    }
    Assert(remainder == 0);  // The whole remainder should have been used and folded into S[0..4].
    // The above requires that the final iteration's remainder (t=4) fits in 64 bits, i.e. that the t=3
    // remainder from S[8] was < 2^116.

    // Now convert S[0..4] into the efficient representation of 64-bit result words, each < 2.2^B_i
    __uint128_t accumulator = 0;
    for (int t = 0; t < 4; ++t) {
      accumulator = (accumulator >> 52) + S[t];                  // Requires 2^76 + S_t < 2^128
      result[t] = static_cast<uint64_t>(accumulator) & kMask52;  // < 2^52
    }
    // S[4] < 2.m_a.m_b.2^48.2^52 + 3.m_a.m_b.2^52.2^52 + 2^101 < m_a.m_b.2^101 + 3.m_a.m_b.2^104 + 2^101 < (3.125)m_a.m_b.2^104 + 2^101 < m_a.m_b.2^106 + 2^101
    accumulator = (accumulator >> 52) + S[4];  // < m.a.m_b.2^106 + 2^101 + 2^67.
    result[4] = static_cast<uint64_t>(accumulator) & kMask48;
    const __uint128_t overflow = accumulator >> 48;  // < m_a.m_b.2^58 + 2^53 + 2^19 < 2^13.2^58 + eps = 2^71. The remaining accumulator, at bit position 256.

    // Still need to accumulate 2^256 * overflow into result, using
    // 2^256 * overflow _= overflow * c_p (mod p), which we fold into bit position 0.
    // c_p < 2^32 + 2^10.
    __uint128_t residual = overflow * kFold256 + result[0];  // < 2^104
    result[0] = static_cast<uint64_t>(residual) & kMask52;
    result[1] += static_cast<uint64_t>(residual >> 52);  // < 2^52 + 2^52 < 2^53.
    
    // result[1] < 2.2^52 <= residual < 2^104 <= overflow < 2^71 <= accumulator < 2^119 
    // <= 2^76 + S[4] < 2^119 <= S[4] < 2^119 - 2^76

    // 119 - 52 = 67. So if S_t <= 2^119 - 2^67 for all t, then
    // acc_0 < 2^119 => carry_1 = (acc_0 >> 52) < 2^67 => acc_1 < 2^119 => ...
    // => all accumulators < 2^119 => result[1] < 2.2^52.

    // So it is sufficient to require S_t <= 2^119 - 2^67, i.e.
    // 4.m_a.m_b.2^104 + 2^101 <= 2^119 - 2^67 
    // m_a.m_b <= (2^119 - 2^101 - 2^67) / 2^106 = 2^13 - 2^-5 - 2^-39 < 2^13 = 8192.
    // So max(m_a.m_b) = 2^13 - 1 = 8191.

    Assert((residual >> 104) == 0);  // residual should be fully used up now.

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

  constexpr FieldElement Squared() const {
    // TODO: More efficient version.
    return *this * *this;
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
