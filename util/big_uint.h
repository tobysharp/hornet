#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace hornet::util {

// Reperesents a multi-word unsigned integer, stored in little-endian order.
template <size_t kBits, std::unsigned_integral T = uint64_t>
class BigUint {
 public:
  static constexpr int kBitsPerWord = sizeof(T) * 8;
  static constexpr int kWords = kBits / kBitsPerWord;
  static_assert(kBits > 0 && kWords > 0);

  BigUint() = default;  // Uninitialized

  template <std::unsigned_integral T2>
  BigUint(const std::array<T2, kBits / (sizeof(T2) * 8)>& array) {
    static_assert(sizeof(array) == sizeof(words_));
    std::memcpy(&words_[0], &array[0], sizeof(array));
  }

  BigUint(std::array<T, kWords>&& array) : words_(std::move(array)) {}

  BigUint(T word) {
    words_ = {};
    words_[0] = word;
  }

  BigUint(const BigUint&) = default;
  BigUint(BigUint&&) = default;
  BigUint& operator=(const BigUint&) = default;
  BigUint& operator=(BigUint&&) = default;

  BigUint& operator=(T word) {
    words_ = {};
    words_[0] = word;
    return *this;
  }

  static BigUint Zero() {
    std::array<T, kWords> words = {};
    return BigUint{std::move(words)};
  }

  BigUint operator+(const BigUint& rhs) const {
    BigUint rv;
    T carry = 0;
    for (int i = 0; i < kWords; ++i) {
      const T partial = words_[i] + carry;
      rv.words_[i] = partial + rhs.words_[i];
      carry = (partial < words_[i]) || (rv.words_[i] < partial);
    }
    // NB: if carry > 0 then overflow.
    return rv;
  }

  BigUint operator-(const BigUint& rhs) const {
    BigUint rv;
    T borrow = 0;
    for (int i = 0; i < kWords; ++i) {
      const T partial = words_[i] - borrow;
      rv.words_[i] = partial - rhs.words_[i];
      borrow = (words_[i] < partial) || (partial < rv.words_[i]);
    }
    // NB: if borrow > 0 then underflow.
    return rv;
  }

  template <std::unsigned_integral U>
  BigUint operator+(U low) const {
    BigUint rv = *this;
    rv.words_[0] = words_[0] + T{low};
    T carry = rv.words_[0] < words_[0];
    for (int i = 1; carry > 0 && i < kWords; ++i) {
      rv.words_[i] = words_[i] + carry;
      carry = rv.words_[i] < words_[i];
    }
    return rv;
  }

  BigUint operator/(const BigUint& rhs) const {
    BigUint quotient = 0;
    BigUint divisor = rhs;
    BigUint remainder = *this;
    // We maintain the invariant:
    //  Quotient * Divisor + Remainder = Numerator,
    // and continue until Remainder < Divisor.
    const int numerator_sig_bits = SignificantBits();
    const int divisor_sig_bits = divisor.SignificantBits();
    if (divisor_sig_bits == 0) throw std::invalid_argument("BigUint division by zero.");
    if (numerator_sig_bits < divisor_sig_bits) return quotient;
    // This gives us the largest possible value L such that Divisor * 2^L could still
    // be less than or equal to Remainder.
    int divisor_lshift = numerator_sig_bits - divisor_sig_bits;
    divisor <<= divisor_lshift;  // = Divisor * 2^L
    for (; divisor_lshift >= 0; --divisor_lshift, divisor >>= 1) {
      if (remainder >= divisor) {
        // Remainder >= Divisor * 2^L
        // So we subtract Divisor * 2^L from Remainder, and add 2^L to Quotient.
        remainder -= divisor;
        quotient.SetBit(divisor_lshift);
      }
    }
    // Now L=0, and Remainder < Divisor, so we're complete.
    return quotient;
  }

  BigUint& operator+=(const BigUint& rhs) {
    return *this = *this + rhs;
  }

  BigUint& operator-=(const BigUint& rhs) {
    return *this = *this - rhs;
  }

  BigUint operator~() const {
    BigUint rv;
    for (int i = 0; i < kWords; ++i) rv.words_[i] = ~words_[i];
    return rv;
  }

  bool operator==(const BigUint& rhs) const {
    return words_ == rhs.words_;
  }

  bool operator<(const BigUint& rhs) const {
    return std::lexicographical_compare(words_.rbegin(), words_.rend(), rhs.words_.rbegin(),
                                        rhs.words_.rend());
  }

  bool operator>=(const BigUint& rhs) const {
    return !(*this < rhs);
  }

  BigUint& operator<<=(int lshift) {
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

  BigUint& operator>>=(int rshift) {
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

  unsigned int SignificantBits() const {
    for (int i = kWords - 1; i >= 0; --i) {
      const T word = words_[i];
      if (word != 0) {
        const int leading_zero_bits = std::countl_zero(word);
        return (i + 1) * kBitsPerWord - leading_zero_bits;
      }
    }
    return 0;
  }

  // Set the bit at the given bit index
  void SetBit(int index) {
    if (index >= kBits) throw std::invalid_argument("SetBit index out of range.");
    words_[index / kBitsPerWord] |= T{1} << (index & (kBitsPerWord - 1));
  }

 private:
  static_assert(std::endian::native == std::endian::little);
  static_assert(kBits % kBitsPerWord == 0);

  std::array<T, kWords> words_;
};

}  // namespace hornet::util

namespace hornet {
using Uint256 = util::BigUint<256>;
}  // namespace hornet
