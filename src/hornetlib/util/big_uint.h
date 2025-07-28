// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <tuple>

namespace hornet::util {

// Reperesents a multi-word unsigned integer, stored in little-endian order.
template <int kBits, std::unsigned_integral T = uint64_t>
class BigUint {
 public:
  using Word = T;
  static constexpr int kBitsPerWord = sizeof(T) * 8;
  static constexpr int kWords = kBits / kBitsPerWord;
  static_assert(kBits > 0 && kWords > 0);

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

  constexpr explicit BigUint(T word) {
    words_ = {};
    words_[0] = word;
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

  constexpr BigUint& operator+=(const BigUint& rhs) noexcept {
    T carry = 0;
    for (int i = 0; i < kWords; ++i) {
      const T partial = words_[i] + carry;
      words_[i] = partial + rhs.words_[i];
      carry = (partial < carry) || (words_[i] < partial);
    }
    // NB: if carry > 0 then overflow.
    return *this;
  }

  constexpr BigUint& operator-=(const BigUint& rhs) noexcept {
    T borrow = 0;
    for (int i = 0; i < kWords; ++i) {
      const T previous = words_[i];
      const T partial = words_[i] - borrow;
      // With no underflow, partial <= previous, borrow <= previous,
      // With underflow, previous < partial, previous < borrow.
      words_[i] = partial - rhs.words_[i];
      // With no underflow, words_[i] <= partial, rhs.words_[i] <= partial.
      // With underflow, partial < words_[i], partial < rhs.words_[i].
      borrow = (previous < borrow) || (partial < words_[i]);
    }
    // NB: if borrow > 0 then underflow.
    return *this;
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

  constexpr BigUint operator*(T rhs) const noexcept {
    if (rhs == 0) return Zero();
    if (rhs == 1) return *this;
    T carry = 0u;
    BigUint result = Zero();
    for (int i = 0; i < kWords; ++i) {
      const auto [lo, hi] = MulWide(words_[i], rhs);
      result.words_[i] += lo;
      carry = hi + (result.words_[i] < lo);
      // Propogates carry forward
      for (int j = i + 1; j < kWords && carry > 0; ++j) {
        result.words_[j] += carry;
        carry = result.words_[j] < carry;
      }
    }
    // NB: if carry > 0 then overflow.
    return result;
  }

  constexpr BigUint& operator*=(T rhs) noexcept {
    return *this = *this * rhs;
  }

  constexpr BigUint& operator /=(T rhs) {
    if (rhs == 0) throw std::invalid_argument("BigUint division by zero.");
    if (rhs == 1) return *this;

    T remainder = 0;
    for (int i = kWords - 1; i >= 0; --i) {
      std::tie(words_[i], remainder) = DivDoubleWord(remainder, words_[i], rhs);
    }
    return *this;
  }

  constexpr BigUint operator/(T rhs) const {
    return BigUint{*this} /= rhs;
  }

  constexpr BigUint& operator/=(const BigUint& rhs) {
    const int numerator_sig_bits = SignificantBits();
    const int divisor_sig_bits = rhs.SignificantBits();

    // During this function, we will maintain the invariant:
    //    Quotient * Divisor + Remainder = Numerator.
    BigUint divisor = rhs;      // Divisor
    BigUint remainder = *this;  // Remainder
    *this = 0;                  // Quotient

    // Handle special cases
    if (divisor_sig_bits == 0) throw std::invalid_argument("BigUint division by zero.");
    if (numerator_sig_bits < divisor_sig_bits) return *this;

    // This gives us the largest possible value L such that Divisor * 2^L could still
    // be less than or equal to Remainder.
    int divisor_lshift = numerator_sig_bits - divisor_sig_bits;
    divisor <<= divisor_lshift;  // = Divisor * 2^L

    // We proceed to reduce Remainder, and continue until Remainder < Divisor.
    for (; divisor_lshift >= 0; --divisor_lshift, divisor >>= 1) {
      if (remainder >= divisor) {  // Remainder >= Divisor * 2^L
        // Subtract Divisor * 2^L from Remainder, and add 2^L to Quotient:
        remainder -= divisor;
        SetBit(divisor_lshift);
      }
    }
    // Now L=0, and Remainder < Divisor, so we're complete.
    return *this;  // Quotient
  }

  constexpr BigUint operator/(const BigUint& rhs) const {
    return BigUint{*this} /= rhs;
  }

  constexpr BigUint operator+(const BigUint& rhs) const {
    return BigUint{*this} += rhs;
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
          (words_[i - lshift_words] << lshift_bits) | (words_[i - lshift_words - 1] >> rshift_bits);
    }
    words_[lshift_words] = words_[0] << lshift_bits;
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
          (words_[i + rshift_words] >> rshift_bits) | (words_[i + rshift_words + 1] << lshift_bits);
    }
    words_[kWords - rshift_words - 1] = words_[kWords - 1] >> rshift_bits;
    for (int i = kWords - rshift_words; i < kWords; ++i) words_[i] = 0;
    return *this;
  }

  constexpr BigUint operator>>(int rshift) const {
    return BigUint{*this} >>= rshift;
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

  constexpr const std::array<T, kWords>& Words() const {
    return words_;
  }

  constexpr std::array<T, kWords>& Words() {
    return words_;
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

  static constexpr std::pair<T, T> MulWide(T a, T b) noexcept {
    using Prod = decltype(a * b);

    if constexpr (sizeof(Prod) > sizeof(T)) {
      static_assert(sizeof(Prod) == 2 * sizeof(T));
      const Prod product = a * b;
      const T lo = static_cast<T>(product);
      const T hi = static_cast<T>(product >> (sizeof(T) * 8));
      return {lo, hi};
    } else {
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

  static constexpr std::pair<T, T> DivDoubleWord(T hi, T lo, T divisor) noexcept {
    using Wide = decltype((T{1} << (sizeof(T) * 8)) | T{1});

    if constexpr (sizeof(Wide) > sizeof(T)) {
      Wide dividend = (hi << (sizeof(T) * 8)) | lo;
      T q = static_cast<T>(dividend / divisor);
      T r = static_cast<T>(dividend % divisor);
      return {q, r};
    } else {
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
