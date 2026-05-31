// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <ostream>
#include <span>
#include <stdexcept>
#include <tuple>

#include "hornetlib/util/assert.h"

namespace hornet::util {

template <std::integral T>
inline consteval T Log2(T x)
{
  if (x == 0) return false;
  return static_cast<T>(std::bit_width(static_cast<std::make_unsigned_t<T>>(x)) - 1);
}

template <std::integral T>
inline consteval bool IsPowerOf2(T x) {
  return std::has_single_bit(static_cast<std::make_unsigned_t<T>>(x));
}

// Reperesents a multi-word unsigned integer, stored in little-endian order.
template <int kBits, std::unsigned_integral T = uint64_t>
class BigUint {
 public:
  using Word = T;
  static constexpr int kBitsPerWord = sizeof(T) * 8;
  static constexpr int kWords = kBits / kBitsPerWord;
  static_assert(kBits > 0 && kWords > 0);
  static consteval int Bits() { return kBits; }

  constexpr BigUint() = default;  // Uninitialized

  template <typename T2>
  explicit constexpr BigUint(const std::array<T2, kBits / (sizeof(T2) * 8)>& rhs) {
    static_assert(sizeof(rhs) == sizeof(words_));
    static_assert(sizeof(T) % sizeof(T2) == 0);
    constexpr int kSrcWordsPerDstWord = sizeof(T) / sizeof(T2);
    constexpr int kBitsPerSrcWord = sizeof(T2) * 8;
    for (int i = 0; i < kWords; ++i) {
      words_[i] = 0;
      for (int j = 0; j < kSrcWordsPerDstWord; ++j) {
        const T src_word = rhs[i * kSrcWordsPerDstWord + j];
        words_[i] |= src_word << (j * kBitsPerSrcWord);
      }
    }
  }

  template <std::integral U>
    requires std::convertible_to<U, T>
  constexpr BigUint(U word) {
    words_ = {};
    words_[0] = static_cast<T>(word);
  }

  constexpr BigUint(const BigUint&) = default;
  constexpr BigUint(BigUint&&) = default;
  constexpr BigUint& operator=(const BigUint&) = default;
  constexpr BigUint& operator=(BigUint&&) = default;

  constexpr BigUint& operator=(T word) {
    words_ = {};
    words_[0] = word;
    return *this;
  }

  static constexpr BigUint Zero() {
    std::array<T, kWords> words = {};
    return BigUint{words};
  }

  static constexpr BigUint Maximum() {
    return ~Zero();
  }

  static constexpr BigUint FromBigEndianBytes(std::span<const uint8_t> bytes) {
    constexpr int kBytesPerWord = sizeof(T);
    constexpr int kBytes = kWords * kBytesPerWord;
    Assert(bytes.size() == kBytes);

    BigUint result = Zero();
    for (int i = 0; i < kWords; ++i) {
      for (int j = 0; j < kBytesPerWord; ++j)
        result.words_[i] |= T{bytes[kBytes - 1 - (i * kBytesPerWord + j)]} << (j << 3);
    }
    return result;
  }

  template <int kRBits>
  [[nodiscard]] constexpr std::pair<BigUint<std::max(kBits, kRBits), T>, bool> AddWithCarry(const BigUint<kRBits, T>& rhs, bool carry_in = false) const noexcept {
    BigUint<std::max(kBits, kRBits), T> result;
    T carry = carry_in ? 1 : 0;
    constexpr int kOverlap = std::min(kWords, rhs.kWords);
    for (int i = 0; i < kOverlap; ++i) {
      const T partial = words_[i] + carry;
      result.Words()[i] = partial + rhs.Words()[i];
      carry = (partial < carry) || (result.Words()[i] < partial);
    }
    const T* src = kBits > kRBits ? words_.data() : rhs.Words().data();
    for (int i = kOverlap; i < result.kWords; ++i) {
      const T sum = src[i] + carry;
      carry = sum < src[i];
      result.Words()[i] = sum;
    }
    return {result, carry != 0};
  }

  template <int kRBits>
  constexpr BigUint& operator+=(const BigUint<kRBits, T>& rhs) noexcept {
    static_assert(kRBits <= kBits);
    return *this = AddWithCarry(rhs).first;
  }

  [[nodiscard]] constexpr std::pair<BigUint, bool> SubWithBorrow(const BigUint& rhs, bool borrow_in = false) const noexcept {
    BigUint result;
    T borrow = borrow_in ? 1 : 0;
    for (int i = 0; i < kWords; ++i) {
      const T partial = words_[i] - borrow;
      // With no underflow, partial <= previous, borrow <= previous,
      // With underflow, previous < partial, previous < borrow.
      result.words_[i] = partial - rhs.words_[i];
      // With no underflow, words_[i] <= partial, rhs.words_[i] <= partial.
      // With underflow, partial < result.words_[i], partial < rhs.words_[i].
      borrow = (words_[i] < borrow) || (partial < result.words_[i]);
    }
    return {result, borrow != 0};
  }

  constexpr BigUint& operator-=(const BigUint& rhs) noexcept {
    return *this = SubWithBorrow(rhs).first;
  }

  constexpr BigUint& operator+=(T low) noexcept {
    T carry = low;
    for (int i = 0; carry > 0 && i < kWords; ++i) {
      words_[i] += carry;
      carry = words_[i] < carry;
    }
    // NB: if carry > 0 then overflow.
    return *this;
  }

  constexpr BigUint operator+(T low) const {
    return BigUint{*this} += low;
  }

  [[nodiscard]] constexpr BigUint<kBits * 2, T> MultiplyWide(const BigUint& rhs) const noexcept {
    const auto Add = [](T& acc, T value) -> bool {
      acc += value;
      return acc < value;
    };
    
    // Iterate over destination words.
    T c0 = 0, c1 = 0, c2 = 0;
    BigUint<kBits * 2, T> result;
    for (int i = 0; i < kWords * 2; ++i) {
      const int j_begin = std::max(0, i + 1 - kWords);
      const int j_end = std::min(i + 1, kWords);

      // Iterate over pairs contributing to column i.
      for (int j = j_begin; j < j_end; ++j) {
        int k = i - j;
        const auto [lo, hi] = MulWide(words_[j], rhs.words_[k]);
        Add(c2, Add(c1, hi + Add(c0, lo)));
      }
      result.Words()[i] = c0;
      c0 = c1; c1 = c2; c2 = 0;
    }
    return result;
  }

  [[nodiscard]] constexpr BigUint<kBits * 2, T> Squared() const noexcept {
    struct {
      const BigUint& self;
      BigUint<kBits * 2, T> result{};
      T c0 = 0, c1 = 0, c2 = 0;
    } capture{*this};

    // Iterate over destination words.
    Unroll<kWords * 2>([](auto i, auto& c) noexcept {
      const int j_begin = std::max(0, i + 1 - kWords);
      const int j_mid = std::min((i + 1) >> 1, kWords);

      // Off-diagonal terms
      for (int j = j_begin; j < j_mid; ++j) {
        const int k = i - j;
        const auto [lo, hi] = MulWide(c.self.words_[j], c.self.words_[k]);
        const T doubled_lo = lo << 1;
        const T doubled_hi = (hi << 1) + (lo >> (kBitsPerWord - 1));
        const T doubled_top = hi >> (kBitsPerWord - 1);
        Add(c.c2, doubled_top + Add(c.c1, doubled_hi + Add(c.c0, doubled_lo)));
      }

      // Diagonal term for even columns
      if constexpr ((i & 1) == 0) {
        const int j = i >> 1;
        const auto [lo, hi] = MulWide(c.self.words_[j], c.self.words_[j]);
        Add(c.c2, Add(c.c1, hi + Add(c.c0, lo)));
      }      

      c.result.Words()[i] = c.c0;
      c.c0 = c.c1; c.c1 = c.c2; c.c2 = 0;
    }, capture);
    return capture.result;
  }
  
  static constexpr bool Add(T& acc, T value) noexcept {
    acc += value;
    return acc < value;
  };

  constexpr BigUint operator*(T rhs) const noexcept {
    if (rhs == 0) return Zero();
    if (rhs == 1) return *this;
    T carry = 0u;
    BigUint result = Zero();
    for (int i = 0; i < kWords; ++i) {
      const auto [lo, hi] = MulWide(words_[i], rhs);
      const T sum = lo + carry;
      result.words_[i] = sum;
      carry = hi + (sum < carry);
    }
    // NB: if carry > 0 then overflow.
    return result;
  }

  constexpr BigUint& operator*=(T rhs) noexcept {
    return *this = *this * rhs;
  }

  template <int kOutBits>
  [[nodiscard]] constexpr BigUint<kOutBits, T> ZeroExtend() const noexcept {
    static_assert(kOutBits >= kBits);
    BigUint<kOutBits, T> result;
    for (int i = 0; i < result.kWords; ++i) result.Words()[i] = i < kWords ? words_[i] : T{0};
    return result;
  }

  template <int kDivBits>
  [[nodiscard]] constexpr std::pair<BigUint, BigUint<kDivBits, T>> QuotientRemainder(const BigUint<kDivBits, T>& rhs) const {
    static_assert(kDivBits <= kBits);
    const int numerator_sig_bits = SignificantBits();
    const int divisor_sig_bits = rhs.SignificantBits();

    // During this function, we will maintain the invariant:
    //    Quotient * Divisor + Remainder = Numerator.
    BigUint divisor = rhs.template ZeroExtend<kBits>();  // Divisor
    BigUint remainder = *this;                           // Remainder
    BigUint quotient = BigUint::Zero();                  // Quotient

    // Handle special cases
    Assert(divisor_sig_bits != 0);  // Division by zero is undefined behavior.
    if (numerator_sig_bits < divisor_sig_bits) return {quotient, remainder.template LowBits<kDivBits>()};

    // This gives us the largest possible value L such that Divisor * 2^L could still
    // be less than or equal to Remainder.
    int divisor_lshift = numerator_sig_bits - divisor_sig_bits;
    divisor <<= divisor_lshift;  // = Divisor * 2^L

    // We proceed to reduce Remainder, and continue until Remainder < Divisor.
    for (; divisor_lshift >= 0; --divisor_lshift, divisor >>= 1) {
      if (remainder >= divisor) {  // Remainder >= Divisor * 2^L
        // Subtract Divisor * 2^L from Remainder, and add 2^L to Quotient:
        remainder -= divisor;
        quotient.SetBit(divisor_lshift);
      }
    }
    // Now L=0, and Remainder < Divisor, so we're complete.
    return {quotient, remainder.template LowBits<kDivBits>()};
  }

  template <int kDivBits>
  [[nodiscard]] constexpr BigUint<kDivBits, T> Modulo(const BigUint<kDivBits, T>& rhs) const {
    return QuotientRemainder(rhs).second;
  }

  constexpr BigUint& operator /=(T rhs) {
    if (rhs == 0) throw std::invalid_argument("BigUint division by zero.");
    if (rhs == 1) return *this;

    T remainder = 0;
    for (int i = kWords - 1; i >= 0; --i) {
      std::tie(words_[i], remainder) = DivWide(remainder, words_[i], rhs);
    }
    return *this;
  }

  constexpr BigUint operator/(T rhs) const {
    return BigUint{*this} /= rhs;
  }

  constexpr BigUint& operator/=(const BigUint& rhs) {
    return *this = QuotientRemainder(rhs).first;
  }

  constexpr BigUint operator/(const BigUint& rhs) const {
    return BigUint{*this} /= rhs;
  }

  template <int kRBits>
  constexpr BigUint<std::max(kBits, kRBits), T> operator+(const BigUint<kRBits, T>& rhs) const {
    return AddWithCarry(rhs).first;
  }

  constexpr BigUint operator-(const BigUint& rhs) const {
    return BigUint{*this} -= rhs;
  }

  constexpr BigUint operator~() const {
    BigUint rv;
    for (int i = 0; i < kWords; ++i) rv.words_[i] = ~words_[i];
    return rv;
  }

  constexpr bool operator==(const BigUint& rhs) const {
    return words_ == rhs.words_;
  }

  constexpr bool operator<(const BigUint& rhs) const {
    return std::lexicographical_compare(words_.rbegin(), words_.rend(), rhs.words_.rbegin(),
                                        rhs.words_.rend());
  }

  constexpr bool operator>(const BigUint& rhs) const {
    return rhs < *this;
  }

  constexpr bool operator>=(const BigUint& rhs) const {
    return !(*this < rhs);
  }

  constexpr bool operator<=(const BigUint& rhs) const {
    return !(rhs < *this);
  }

  constexpr BigUint& operator<<=(int lshift) {
    if (lshift == 0) return *this;  // No shift needed
    if (lshift >= kBits) {
      words_ = {};
      return *this;
    }
    // Left shift the words by lshift bits, starting with the highest order word
    const int lshift_words = lshift / kBitsPerWord;
    const int lshift_bits = lshift - lshift_words * kBitsPerWord;
    const int rshift_bits = kBitsPerWord - lshift_bits;
    for (int i = kWords - 1; i >= lshift_words + 1; --i) {
      words_[i] =
          Shl(words_[i - lshift_words], lshift_bits) | Shr(words_[i - lshift_words - 1], rshift_bits);
    }
    words_[lshift_words] = Shl(words_[0], lshift_bits);
    for (int i = lshift_words - 1; i >= 0; --i) words_[i] = 0;
    return *this;
  }

  constexpr BigUint operator<<(int lshift) const {
    return BigUint{*this} <<= lshift;
  }

  constexpr BigUint& operator>>=(int rshift) {
    if (rshift == 0) return *this;  // No shift needed
    if (rshift >= kBits) {
      words_ = {};
      return *this;
    }
    // Right shift the words by rshift bits, starting with the lowest order word
    const int rshift_words = rshift / kBitsPerWord;
    const int rshift_bits = rshift - rshift_words * kBitsPerWord;
    const int lshift_bits = kBitsPerWord - rshift_bits;
    for (int i = 0; i < kWords - rshift_words - 1; ++i) {
      words_[i] =
          Shr(words_[i + rshift_words], rshift_bits) | Shl(words_[i + rshift_words + 1], lshift_bits);
    }
    words_[kWords - rshift_words - 1] = Shr(words_[kWords - 1], rshift_bits);
    for (int i = kWords - rshift_words; i < kWords; ++i) words_[i] = 0;
    return *this;
  }

  constexpr BigUint operator>>(int rshift) const {
    return BigUint{*this} >>= rshift;
  }

  constexpr T operator&(T x) const {
    return words_[0] & x;
  }

  constexpr unsigned int SignificantBits() const {
    for (int i = kWords - 1; i >= 0; --i) {
      const T word = words_[i];
      if (word != 0) {
        const int leading_zero_bits = std::countl_zero(word);
        return (i + 1) * kBitsPerWord - leading_zero_bits;
      }
    }
    return 0;
  }

  template <int kOutBits> [[nodiscard]] constexpr BigUint<kOutBits, T> LowBits() const noexcept {
    static_assert(kOutBits > 0);
    static_assert(kOutBits <= kBits);
    static_assert(kOutBits % kBitsPerWord == 0);

    constexpr int kOutWords = kOutBits / kBitsPerWord;
    std::array<T, kOutWords> array;
    std::copy(words_.begin(), words_.begin() + kOutWords, array.begin());
    return BigUint<kOutBits, T>{array};
  }

  template <int kOutBits> [[nodiscard]] constexpr BigUint<kOutBits, T> HighBits() const noexcept {
    static_assert(kOutBits > 0);
    static_assert(kOutBits <= kBits);
    static_assert(kOutBits % kBitsPerWord == 0);

    constexpr int kOutWords = kOutBits / kBitsPerWord;
    std::array<T, kOutWords> array;
    std::copy(words_.begin() + (kWords - kOutWords), words_.end(), array.begin());
    return BigUint<kOutBits, T>{array};
  }
  
  [[nodiscard]] constexpr std::pair<BigUint, BigUint> MultiplyFull(const BigUint& rhs) const noexcept {
    const auto wide = MultiplyWide(rhs);
    return { wide.template LowBits<kBits>(), wide.template HighBits<kBits>() };
  }

  constexpr const std::array<T, kWords>& Words() const {
    return words_;
  }

  constexpr std::array<T, kWords>& Words() {
    return words_;
  }

  constexpr bool GetBit(int bitIndex) const
  {
    constexpr int Log2BitsPerElement = Log2(kBitsPerWord);    
    const int elementIndex = bitIndex >> Log2BitsPerElement;
    const int bitWithinElement = bitIndex - (elementIndex << Log2BitsPerElement);
    const T bitMask = T{1} << bitWithinElement;
    return (words_[elementIndex] & bitMask) != 0;
  }
  
  // Set the bit at the given bit index
  constexpr void SetBit(int index) {
    if (index >= kBits) throw std::invalid_argument("SetBit index out of range.");
    words_[index / kBitsPerWord] |= T{1} << (index & (kBitsPerWord - 1));
  }

  friend std::ostream& operator <<(std::ostream& os, const BigUint& obj) {
    os << "\"";
    for (int i = kWords - 1; i >= 0; --i) {
      os << std::hex << std::setfill('0') << std::setw(kBitsPerWord >> 2) << obj.words_[i];
    }
    os << "\"";
    return os;
  }

 private:
  static_assert(std::endian::native == std::endian::little);
  static_assert(kBits % kBitsPerWord == 0);

  template <typename U> struct DoubleWord;
  template <> struct DoubleWord<uint32_t> { using Type = uint64_t; };
#if defined(__SIZEOF_INT128__)
  template <> struct DoubleWord<uint64_t> { using Type = unsigned __int128; };
#endif

  static constexpr T Shl(T a, int b) {
    return b >= kBitsPerWord ? 0 : (a << b);
  }

  static constexpr T Shr(T a, int b) {
    return b >= kBitsPerWord ? 0 : (a >> b);
  }
  
  static constexpr std::pair<T, T> MulWide(T a, T b) noexcept {
    if constexpr (requires { typename DoubleWord<T>::Type; }) {
      const typename DoubleWord<T>::Type product = static_cast<DoubleWord<T>::Type>(a) * b;
      return { static_cast<T>(product), static_cast<T>(product >> kBitsPerWord) };
    }
#if defined(_MSC_VER)
    else if constexpr (sizeof(T) == 8) {
      unsigned long long hi;
      unsigned long long lo = _umul128(a, b, &hi);
      return { static_cast<T>(lo), static_cast<T>(hi) };
    }
#endif
    else {
      // Manual full-width multiplication fallback
      constexpr int kHalfBits = sizeof(T) * 4;
      constexpr T kLowMask = (T{1} << kHalfBits) - 1;

      const T a_lo = a & kLowMask;
      const T a_hi = a >> kHalfBits;
      const T b_lo = b & kLowMask;
      const T b_hi = b >> kHalfBits;

      const T p0 = a_lo * b_lo;
      const T p1 = a_lo * b_hi;
      const T p2 = a_hi * b_lo;
      const T p3 = a_hi * b_hi;

      const T mid = (p0 >> kHalfBits) + (p1 & kLowMask) + (p2 & kLowMask);
      const T lo = (mid << kHalfBits) | (p0 & kLowMask);
      const T hi = p3 + (p1 >> kHalfBits) + (p2 >> kHalfBits) + (mid >> kHalfBits);
      return {lo, hi};
    }
  }

  static constexpr std::pair<T, T> DivWide(T hi, T lo, T divisor) noexcept {
    if constexpr (requires { typename DoubleWord<T>::Type; }) {
      typename DoubleWord<T>::Type dividend = (static_cast<DoubleWord<T>::Type>(hi) << kBitsPerWord) | lo;
      T q = static_cast<T>(dividend / divisor);
      T r = static_cast<T>(dividend % divisor);
      return {q, r};
    }
#if defined(_MSC_VER)
    else if constexpr (sizeof(T) == 8) {
      unsigned long long r = 0;
      const unsigned long long q = _udiv128(hi, lo, divisor, &r);
      return { static_cast<T>(q), static_cast<T>(r) };
    }
#endif
    else {
      // Manual long division
      T q = 0;
      T r = hi;
      for (int i = kBitsPerWord - 1; i >= 0; --i) {
        r = static_cast<T>((r << 1) | ((lo >> i) & 1u));
        if (r >= divisor) {
          r -= divisor;
          q |= T{1} << i;
        }
      }
      return {q, r};
    }
  }

private:
  std::array<T, kWords> words_;
};

}  // namespace hornet::util

namespace hornet {

using Uint256 = util::BigUint<256>;

}  // namespace hornet
