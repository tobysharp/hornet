#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

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
  explicit constexpr BigUint(const std::array<T2, kBits / (sizeof(T2) * 8)>& array) {
    static_assert(sizeof(array) == sizeof(words_));
    std::memcpy(&words_[0], &array[0], sizeof(array));
  }

  explicit constexpr BigUint(std::array<T, kWords>&& array) : words_(std::move(array)) {}

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
    return BigUint{std::move(words)};
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

  template <std::unsigned_integral U>
  constexpr BigUint& operator+=(U low) noexcept {
    T carry = T{low};
    for (int i = 0; carry > 0 && i < kWords; ++i) {
      words_[i] += carry;
      carry = words_[i] < carry;
    }
    // NB: if carry > 0 then overflow.
    return *this;
  }

  template <std::unsigned_integral U>
  constexpr BigUint operator+(U low) const {
    return BigUint{*this} += low;
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

  constexpr BigUint operator <<(int lshift) const {
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

  constexpr BigUint operator >>(int rshift) const {
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

  // Set the bit at the given bit index
  constexpr void SetBit(int index) {
    if (index >= kBits) throw std::invalid_argument("SetBit index out of range.");
    words_[index / kBitsPerWord] |= T{1} << (index & (kBitsPerWord - 1));
  }

 private:
  static_assert(std::endian::native == std::endian::little);
  static_assert(kBits % kBitsPerWord == 0);

  std::array<T, kWords> words_;
};

constexpr uint8_t HexValue(const char c) {
  return 
    (c >= '0' && c <= '9') ? static_cast<uint8_t>(c - '0') :
    (c >= 'a' && c <= 'f') ? static_cast<uint8_t>(c - 'a' + 0xA):
    (c >= 'A' && c <= 'F') ? static_cast<uint8_t>(c - 'A' + 0xA):
    throw std::invalid_argument("Invalid hex digit");
}

template <std::unsigned_integral T = uint8_t>
inline constexpr std::array<T, 32 / sizeof(T)> ParseHex32(const char (&hex)[32 * 2 + 1]) {
  constexpr int kBytesPerWord = sizeof(T);
  constexpr int kWords = 32 / kBytesPerWord;
  std::array<T, kWords> out;
  int index = 0;
  for (int i = 0; i < kWords; ++i) {
    out[i] = 0;
    for (int j = 0; j < kBytesPerWord; ++j, index += 2) {
      T byte = (HexValue(hex[index]) << 4) | HexValue(hex[index + 1]);
      out[i] |= (byte << (j * 8));
    }
  }
  return out;
}

}  // namespace hornet::util

namespace hornet {
using Uint256 = util::BigUint<256>;

inline constexpr Uint256 ParseHex32ToUint256(const char (&hex)[32 * 2 + 1]) {
  return Uint256{util::ParseHex32<Uint256::Word>(hex)};
}

}  // namespace hornet
