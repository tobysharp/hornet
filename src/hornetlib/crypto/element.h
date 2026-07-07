#pragma once

#include <array>
#include <cstdint>

#include "hornetlib/crypto/secp256k1.h"
#include "hornetlib/crypto/uintw.h"
#include "hornetlib/util/assert.h"

namespace hornet::crypto::ecdsa {

template <int kMagnitude = 1> class FieldElement {
 public:
  static constexpr int kWords = 5;
  static constexpr int kMaxProductMagnitude = 8191;
  using Array = std::array<uint64_t, kWords>;

  constexpr FieldElement() : words_{} {}
  constexpr FieldElement(const FieldElement& rhs) = default;
  constexpr FieldElement(const Array& array) : words_{array} {
    for (int i = 0; i < 4; ++i) Assert((words_[i] >> 52) < kMagnitude);
    Assert((words_[4] >> 48) < kMagnitude);
  }
  constexpr FieldElement(uint64_t rhs) : words_{} {
    Assert((rhs >> 52) < kMagnitude);
    words_[0] = rhs;
  }

  template <int kRMagnitude> requires(kRMagnitude < kMagnitude)
  constexpr FieldElement(const FieldElement<kRMagnitude>& rhs) : words_(rhs.words_) {}

  constexpr FieldElement& operator=(const FieldElement& rhs) = default;

  constexpr const Array& Words() const { return words_; }

  template <int k> requires((kMagnitude << k) <= (1 << 12))
  constexpr FieldElement<kMagnitude << k> LShift() const {
    Array result;
    for (int i = 0; i < kWords; ++i) result[i] = words_[i] << k;
    return result;
  }

  constexpr FieldElement<kMagnitude + 1> operator-() const {
    Array result;
    for (int i = 0; i < kWords; ++i) result[i] = p52[i] * (kMagnitude + 1) - words_[i];
    return result;
  }

  template <int kRMagnitude>
  constexpr FieldElement<kMagnitude + kRMagnitude> operator+(const FieldElement<kRMagnitude>& rhs) const {
    Array result;
    for (int i = 0; i < kWords; ++i) result[i] = words_[i] + rhs.words_[i];
    return result;
  }

  template <int kRMagnitude> constexpr auto operator-(const FieldElement<kRMagnitude>& rhs) const {
    return *this + (-rhs);
  }

  template <int k> FieldElement < k<0 ? (1 - kMagnitude * k) : kMagnitude * k> constexpr Times() const {
    if constexpr (k < 0) return -Times<-k>();
    if constexpr (k == 0) return FieldElement<0>{};
    if constexpr (k == 1) return *this;
    if constexpr (util::IsPowerOf2(k)) return LShift<util::Log2(k)>();

    Array result;
    for (int i = 0; i < kWords; ++i) result[i] = words_[i] * k;
    return result;
  }

  template <int k> constexpr auto operator*(std::integral_constant<int, k>) const { return Times<k>(); }

  template <int k> friend constexpr auto operator*(std::integral_constant<int, k>, const FieldElement& x) {
    return x.template Times<k>();
  }

  template <int k> constexpr auto operator<<(std::integral_constant<int, k>) const { return LShift<k>(); }

  template <int kRMagnitude> requires(kMagnitude * kRMagnitude <= kMaxProductMagnitude)
  constexpr FieldElement<2> operator*(const FieldElement<kRMagnitude>& rhs) const {
    // Full 5x52 limb product with interleaved fold mod p.
    Array result;
    constexpr uint64_t c_p = secp256k1::c_p.Words()[0];
    constexpr __uint128_t R = c_p << 4;

    // a * b = sum_{t=0}^8 S_t * 2^(52*t).
    // a_i < m_a * 2^B_i (B_i = 2^52 for i = 0..3, B_4 = 2^48)
    // b_i < m_b * 2^B_i.

    // Accumulate 128-bit column products, S_t:
    // (This step only requires that m_a.m_b <= 2^20 since there are at most 5 terms added.)
    __uint128_t S[9] = {};
    for (int t = 0; t <= 8; ++t) {
      for (int i = std::max(0, t - 4); i <= std::min(t, 4); ++i) {
        const int j = t - i;
        S[t] += __uint128_t{words_[i]} * rhs.words_[j];  // S_t < 5m_a.m_b.4^B_t
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
      S[t] += R * low;  // S_t < 5m_a.m_b.4^B_t + 2^101
      remainder = (remainder - low) >> 52;
    }
    Assert(remainder == 0);  // The whole remainder should have been used and folded into S[0..4].
    // The above requires that the final iteration's remainder (t=4) fits in 64 bits, i.e. that the t=3
    // remainder from S[8] was < 2^116.

    // Now convert S[0..4] into the efficient representation of 64-bit result words, each < 2.2^B_i
    __uint128_t accumulator = 0;
    for (int t = 0; t < kWords; ++t) {
      accumulator = (accumulator >> 52) + S[t];                  // Requires 2^76 + S_t < 2^128 i.e. S_t < 2^127.
      result[t] = static_cast<uint64_t>(accumulator) & kMask52;  // < 2^52
    }
    const __uint128_t overflow = accumulator >> 52;  // < 2^76. The remaining accumulator, at bit position 260.

    // Still need to accumulate 2^260 * overflow into result, using
    // 2^260 * overflow _= R * overflow (mod p), which we fold into bit position 0.
    __uint128_t residual = R * overflow + result[0];  // < 2^(37+76) + 2^52 < 2^114.
    result[0] = static_cast<uint64_t>(residual) & kMask52;
    result[1] += static_cast<uint64_t>(residual >> 52);  // < 2^52 + 2^(114-52) < 2^63.
    // result[1] < 2.2^52 <= residual < 2^104 <= overflow < 2^67 <= accumulator < 2^119 <= S[4] < 2^118
    // <= 5.m_a.m_b.2^104 + 2^101 < 2^118 <= 5.m_a.m_b < 2^14 <= m_a.m_b < 3276

    Assert((residual >> 104) == 0);  // residual should be fully used up now.

    // Fold from bit 256 and higher, using 2^256 _= c_p (mod p).
    const uint64_t high = result[4] >> 48;  // bits 256..259, high < 2^4
    result[0] += high * c_p;                // Fold into bit position 0.
    result[4] &= kMask48;                   // Dispose folded high bits.

    return result;
  }

  constexpr FieldElement<2> Squared() const requires(kMagnitude * kMagnitude <= kMaxProductMagnitude) {
    // TODO: More efficient version.
    return *this * *this;
  }

 private:
  template <int> friend class FieldElement;
  static constexpr uint64_t kMask52 = (1ull << 52) - 1;
  static constexpr uint64_t kMask48 = (1ull << 48) - 1;

  static constexpr Array p52 = [] {
    Array words;
    for (int i = 0; i < kWords; ++i) words[i] = (secp256k1::p >> (52 * i)).LowBits<64>() & kMask52;
    return words;
  }();

  static_assert(kMagnitude <= (1 << 12));
  static_assert([] {
    UInt256 p = UInt256::Zero();
    for (int i = 0; i < kWords; ++i) p += UInt256{p52[i]} << (52 * i);
    return p == secp256k1::p;
  }());

  // For kMagnitude > 0, words_[i] < kMagnitude * 2^52 for i<4, and words_[4] < kMagnitude * 2^48.
  // For kMagnitude == 0, words_[i] == 0 for all i.

  Array words_;
};

}  // namespace hornet::crypto::ecdsa
