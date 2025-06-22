// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "util/big_uint.h" // Assuming your BigUint class is in this header

#include <limits>
#include <stdexcept> // For std::invalid_argument
#include <iomanip>   // For std::hex, std::setfill, std::setw
#include <array>     // For std::array in MakeBigUint

#include <gtest/gtest.h>

// Define a common type for testing to avoid repetition
using TestBigUint64 = hornet::util::BigUint<64, uint32_t>; // 2 words of 32-bit
using TestBigUint128 = hornet::util::BigUint<128, uint64_t>; // 2 words of 64-bit
using TestUint256 = hornet::Uint256; // 4 words of 64-bit

namespace hornet::util {
namespace { // Anonymous namespace for internal linkage

// Helper function to create a BigUint from a list of words (little-endian)
// This helper now uses the BigUint constructor that takes an std::array.
template <size_t kBits, std::unsigned_integral T>
BigUint<kBits, T> MakeBigUint(std::initializer_list<T> words) {
  std::array<T, BigUint<kBits, T>::kWords> temp_words = {};
  int i = 0;
  for (T word : words) {
    if (i < BigUint<kBits, T>::kWords) {
      temp_words[i] = word;
      i++;
    } else {
      // For testing, we'll assume valid input, but in production code
      // you might want to assert or throw if too many words are provided.
      break;
    }
  }
  return BigUint<kBits, T>{temp_words};
}

// Custom GTest printer for BigUint to make EXPECT_EQ output more readable
// This is the only place we directly access words_, as it's for debug output.
template <size_t kBits, std::unsigned_integral T>
void PrintTo(const BigUint<kBits, T>& bu, std::ostream* os) {
  *os << "BigUint<" << kBits << ", " << sizeof(T) * 8 << "> {";
  for (int i = BigUint<kBits, T>::kWords - 1; i >= 0; --i) {
    *os << std::hex << std::setfill('0') << std::setw(sizeof(T) * 2) << bu.words_[i];
    if (i > 0) {
      *os << "_";
    }
  }
  *os << std::dec << "}";
}


// Test fixture for BigUint operations
class BigUintTest : public ::testing::Test {
 protected:
  // You can set up common test data here if needed
};

// --- Constructor and Assignment Tests ---

// Renamed and modified to test value-initialization
TEST_F(BigUintTest, ValueInitializationInitializesToZero) {
  TestBigUint64 bu{}; // Value-initialization
  EXPECT_EQ(bu, TestBigUint64::Zero());
}

// Removed the test for default constructor being zero-initialized,
// as it's now uninitialized.

TEST_F(BigUintTest, SingleWordConstructor) {
  TestBigUint64 bu(0x12345678);
  TestBigUint64 expected_bu = MakeBigUint<64, uint32_t>({0x12345678, 0});
  EXPECT_EQ(bu, expected_bu);
}

TEST_F(BigUintTest, ArrayConstructor) {
  std::array<uint32_t, 2> words = {0x11223344, 0xAABBCCDD};
  TestBigUint64 bu(words);
  TestBigUint64 expected_bu = MakeBigUint<64, uint32_t>({0x11223344, 0xAABBCCDD});
  EXPECT_EQ(bu, expected_bu);
}

TEST_F(BigUintTest, AssignmentFromSingleWord) {
  TestBigUint64 bu; // This 'bu' is uninitialized, but immediately assigned.
  bu = 0xDEADBEEF;
  TestBigUint64 expected_bu = MakeBigUint<64, uint32_t>({0xDEADBEEF, 0});
  EXPECT_EQ(bu, expected_bu);
}

TEST_F(BigUintTest, ZeroStaticMethod) {
  TestBigUint64 zero = TestBigUint64::Zero();
  TestBigUint64 expected_zero = MakeBigUint<64, uint32_t>({0, 0}); // Compare against a known zero
  EXPECT_EQ(zero, expected_zero);
}

// --- Addition Tests ---

TEST_F(BigUintTest, AdditionNoCarry) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({10, 0});
  TestBigUint64 b = MakeBigUint<64, uint32_t>({5, 0});
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({15, 0});
  EXPECT_EQ(a + b, expected);
}

TEST_F(BigUintTest, AdditionWithWordCarry) {
  uint32_t max_u32 = std::numeric_limits<uint32_t>::max();
  TestBigUint64 a = MakeBigUint<64, uint32_t>({max_u32, 0});
  TestBigUint64 b = MakeBigUint<64, uint32_t>({1, 0});
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({0, 1});
  EXPECT_EQ(a + b, expected);
}

TEST_F(BigUintTest, AdditionWithMultiWordCarry) {
  uint32_t max_u32 = std::numeric_limits<uint32_t>::max();
  TestBigUint64 a = MakeBigUint<64, uint32_t>({max_u32, max_u32});
  TestBigUint64 b = MakeBigUint<64, uint32_t>({1, 0});
  // This will cause a carry out of the highest word, which is lost.
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({0, 0});
  EXPECT_EQ(a + b, expected);

  // Let's test with a scenario where the carry propagates but doesn't go beyond kBits.
  TestBigUint64 a_carry = MakeBigUint<64, uint32_t>({max_u32, 10});
  TestBigUint64 b_carry = MakeBigUint<64, uint32_t>({1, 0});
  TestBigUint64 expected_carry = MakeBigUint<64, uint32_t>({0, 11});
  EXPECT_EQ(a_carry + b_carry, expected_carry);
}

TEST_F(BigUintTest, AdditionWithULowNoCarry) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({100, 50});
  uint32_t low_val = 25;
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({125, 50});
  EXPECT_EQ(a + low_val, expected);
}

TEST_F(BigUintTest, AdditionWithULowCarry) {
  uint32_t max_u32 = std::numeric_limits<uint32_t>::max();
  TestBigUint64 a = MakeBigUint<64, uint32_t>({max_u32, 100});
  uint32_t low_val = 1;
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({0, 101});
  EXPECT_EQ(a + low_val, expected);
}

TEST_F(BigUintTest, AdditionWithULowMultiWordCarry) {
  uint32_t max_u32 = std::numeric_limits<uint32_t>::max();
  TestBigUint64 a = MakeBigUint<64, uint32_t>({max_u32, max_u32});
  uint32_t low_val = 1;
  // This will cause a carry out of the highest word, which is lost.
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({0, 0});
  EXPECT_EQ(a + low_val, expected);
}

// --- Subtraction Tests ---

TEST_F(BigUintTest, SubtractionNoBorrow) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({15, 0});
  TestBigUint64 b = MakeBigUint<64, uint32_t>({5, 0});
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({10, 0});
  EXPECT_EQ(a - b, expected);
}

TEST_F(BigUintTest, SubtractionSameNumbersIsZero) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({12345, 67890});
  TestBigUint64 b = MakeBigUint<64, uint32_t>({12345, 67890});
  EXPECT_EQ(a - b, TestBigUint64::Zero());
}

TEST_F(BigUintTest, SubtractionWithWordBorrow) {
  uint32_t max_u32 = std::numeric_limits<uint32_t>::max();
  TestBigUint64 a = MakeBigUint<64, uint32_t>({0, 1}); // Represents 2^32
  TestBigUint64 b = MakeBigUint<64, uint32_t>({1, 0}); // Represents 1
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({max_u32, 0}); // Represents 2^32 - 1
  EXPECT_EQ(a - b, expected);
}

TEST_F(BigUintTest, SubtractionWithMultiWordBorrow) {
  uint32_t max_u32 = std::numeric_limits<uint32_t>::max();
  TestBigUint64 a = MakeBigUint<64, uint32_t>({10, 100});
  TestBigUint64 b = MakeBigUint<64, uint32_t>({20, 1}); // 100 * 2^32 + 10 - (1 * 2^32 + 20)
  // Expected: (100-1) * 2^32 + (10 - 20) = 99 * 2^32 + (2^32 + 10 - 20) = 99 * 2^32 + (max_u32 + 1 - 10)
  // = 99 * 2^32 + (max_u32 - 9)
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({max_u32 - 9, 98});
  EXPECT_EQ(a - b, expected);
}

TEST_F(BigUintTest, SubtractionUnderflow) {
  // BigUint does not explicitly throw on underflow,
  // but the final 'borrow' would be > 0.
  // The result will wrap around.
  TestBigUint64 a = MakeBigUint<64, uint32_t>({5, 0});
  TestBigUint64 b = MakeBigUint<64, uint32_t>({10, 0});
  uint32_t max_u32 = std::numeric_limits<uint32_t>::max();
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({max_u32 - 4, max_u32}); // 5 - 10 = -5, wraps to MAX_UINT64 - 4
  EXPECT_EQ(a - b, expected);
}

// --- Compound Assignment Operators ---

TEST_F(BigUintTest, CompoundAddition) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({10, 0});
  TestBigUint64 b = MakeBigUint<64, uint32_t>({5, 0});
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({15, 0});
  a += b;
  EXPECT_EQ(a, expected);
}

TEST_F(BigUintTest, CompoundSubtraction) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({15, 0});
  TestBigUint64 b = MakeBigUint<64, uint32_t>({5, 0});
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({10, 0});
  a -= b;
  EXPECT_EQ(a, expected);
}

// --- Bitwise Operators ---

TEST_F(BigUintTest, BitwiseNot) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({0x0000FFFF, 0xFFFFFFFF});
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({0xFFFF0000, 0x00000000});
  EXPECT_EQ(~a, expected);
}

// --- Comparison Operators ---

TEST_F(BigUintTest, Equality) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({123, 456});
  TestBigUint64 b = MakeBigUint<64, uint32_t>({123, 456});
  TestBigUint64 c = MakeBigUint<64, uint32_t>({123, 457});
  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a == c);
}

TEST_F(BigUintTest, LessThan) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({10, 0});
  TestBigUint64 b = MakeBigUint<64, uint32_t>({5, 0});
  TestBigUint64 c = MakeBigUint<64, uint32_t>({10, 1}); // Higher word makes it larger
  TestBigUint64 d = MakeBigUint<64, uint32_t>({10, 0}); // Equal

  EXPECT_TRUE(b < a);
  EXPECT_FALSE(a < b);
  EXPECT_TRUE(a < c);
  EXPECT_FALSE(a < d); // Not strictly less than
}

TEST_F(BigUintTest, GreaterThanOrEqual) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({10, 0});
  TestBigUint64 b = MakeBigUint<64, uint32_t>({5, 0});
  TestBigUint64 c = MakeBigUint<64, uint32_t>({10, 1});
  TestBigUint64 d = MakeBigUint<64, uint32_t>({10, 0});

  EXPECT_TRUE(a >= b);
  EXPECT_FALSE(b >= a);
  EXPECT_FALSE(a >= c);
  EXPECT_TRUE(a >= d); // Equal
}

// --- Shift Operators ---

TEST_F(BigUintTest, LeftShiftWithinWord) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({0x00000001, 0});
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({0x00000008, 0});
  a <<= 3;
  EXPECT_EQ(a, expected);
}

TEST_F(BigUintTest, LeftShiftAcrossWordBoundary) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({0x80000000, 0}); // MSB of lower word
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({0, 1}); // Shifts to LSB of higher word
  a <<= 1;
  EXPECT_EQ(a, expected);

  a = MakeBigUint<64, uint32_t>({0x00000001, 0x00000001});
  expected = MakeBigUint<64, uint32_t>({0x00000002, 0x00000002});
  a <<= 1;
  EXPECT_EQ(a, expected);
}

TEST_F(BigUintTest, LeftShiftByWordSize) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({0x12345678, 0});
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({0, 0x12345678});
  a <<= 32; // Shift by one word
  EXPECT_EQ(a, expected);
}

TEST_F(BigUintTest, LeftShiftByTotalBits) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({0x12345678, 0x9ABCDEF0});
  TestBigUint64 expected = TestBigUint64::Zero();
  a <<= 64; // Shift by total bits, should be zero
  EXPECT_EQ(a, expected);
}

TEST_F(BigUintTest, RightShiftWithinWord) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({0x00000008, 0});
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({0x00000001, 0});
  a >>= 3;
  EXPECT_EQ(a, expected);
}

TEST_F(BigUintTest, RightShiftAcrossWordBoundary) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({0, 0x00000001}); // LSB of higher word
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({0x80000000, 0}); // Shifts to MSB of lower word
  a >>= 1;
  EXPECT_EQ(a, expected);

  a = MakeBigUint<64, uint32_t>({0x00000002, 0x00000002});
  expected = MakeBigUint<64, uint32_t>({0x00000001, 0x00000001});
  a >>= 1;
  EXPECT_EQ(a, expected);
}

TEST_F(BigUintTest, RightShiftByWordSize) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({0, 0x12345678});
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({0x12345678, 0});
  a >>= 32; // Shift by one word
  EXPECT_EQ(a, expected);
}

TEST_F(BigUintTest, RightShiftByTotalBits) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({0x12345678, 0x9ABCDEF0});
  TestBigUint64 expected = TestBigUint64::Zero();
  a >>= 64; // Shift by total bits, should be zero
  EXPECT_EQ(a, expected);
}

// --- SignificantBits Tests ---

TEST_F(BigUintTest, SignificantBitsZero) {
  TestBigUint64 a = TestBigUint64::Zero();
  EXPECT_EQ(a.SignificantBits(), 0);
}

TEST_F(BigUintTest, SignificantBitsSingleBit) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({1, 0}); // 2^0
  EXPECT_EQ(a.SignificantBits(), 1);
  a = MakeBigUint<64, uint32_t>({0x80000000, 0}); // 2^31
  EXPECT_EQ(a.SignificantBits(), 32);
  a = MakeBigUint<64, uint32_t>({0, 1}); // 2^32
  EXPECT_EQ(a.SignificantBits(), 33);
  a = MakeBigUint<64, uint32_t>({0, 0x80000000}); // 2^63
  EXPECT_EQ(a.SignificantBits(), 64);
}

TEST_F(BigUintTest, SignificantBitsFullWord) {
  uint32_t max_u32 = std::numeric_limits<uint32_t>::max();
  TestBigUint64 a = MakeBigUint<64, uint32_t>({max_u32, 0});
  EXPECT_EQ(a.SignificantBits(), 32);
  a = MakeBigUint<64, uint32_t>({max_u32, max_u32});
  EXPECT_EQ(a.SignificantBits(), 64);
}

// --- SetBit Tests ---

TEST_F(BigUintTest, SetBitWithinWord) {
  TestBigUint64 a = TestBigUint64::Zero();
  a.SetBit(0);
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({1, 0});
  EXPECT_EQ(a, expected);
  a.SetBit(5);
  expected = MakeBigUint<64, uint32_t>({1 | (1 << 5), 0});
  EXPECT_EQ(a, expected);
}

TEST_F(BigUintTest, SetBitAcrossWordBoundary) {
  TestBigUint64 a = TestBigUint64::Zero();
  a.SetBit(32); // Bit 0 of the second word
  TestBigUint64 expected = MakeBigUint<64, uint32_t>({0, 1});
  EXPECT_EQ(a, expected);
  a.SetBit(63); // Bit 31 of the second word
  expected = MakeBigUint<64, uint32_t>({0, 1u | (1u << 31)});
  EXPECT_EQ(a, expected);
}

TEST_F(BigUintTest, SetBitThrowsOutOfRange) {
  TestBigUint64 a = TestBigUint64::Zero();
  EXPECT_THROW(a.SetBit(64), std::invalid_argument); // Out of bounds for BigUint<64>
  EXPECT_THROW(a.SetBit(100), std::invalid_argument);
}

// --- Division Tests ---

TEST_F(BigUintTest, DivisionByZeroThrows) {
  TestBigUint64 numerator = MakeBigUint<64, uint32_t>({10, 0});
  TestBigUint64 divisor = TestBigUint64::Zero();
  EXPECT_THROW(numerator / divisor, std::invalid_argument);
}

TEST_F(BigUintTest, DivisionNumeratorLessThanDivisor) {
  TestBigUint64 numerator = MakeBigUint<64, uint32_t>({5, 0});
  TestBigUint64 divisor = MakeBigUint<64, uint32_t>({10, 0});
  EXPECT_EQ(numerator / divisor, TestBigUint64::Zero());
}

TEST_F(BigUintTest, DivisionExact) {
  TestBigUint64 numerator = MakeBigUint<64, uint32_t>({100, 0});
  TestBigUint64 divisor = MakeBigUint<64, uint32_t>({10, 0});
  TestBigUint64 expected_quotient = MakeBigUint<64, uint32_t>({10, 0});
  EXPECT_EQ(numerator / divisor, expected_quotient);
}

TEST_F(BigUintTest, DivisionWithRemainder) {
  TestBigUint64 numerator = MakeBigUint<64, uint32_t>({103, 0});
  TestBigUint64 divisor = MakeBigUint<64, uint32_t>({10, 0});
  TestBigUint64 expected_quotient = MakeBigUint<64, uint32_t>({10, 0}); // Remainder is 3
  EXPECT_EQ(numerator / divisor, expected_quotient);
}

TEST_F(BigUintTest, DivisionByOne) {
  TestBigUint64 numerator = MakeBigUint<64, uint32_t>({12345, 67890});
  TestBigUint64 divisor = MakeBigUint<64, uint32_t>({1, 0});
  EXPECT_EQ(numerator / divisor, numerator);
}

TEST_F(BigUintTest, DivisionBySelf) {
  TestBigUint64 num = MakeBigUint<64, uint32_t>({12345, 67890});
  TestBigUint64 expected_quotient = MakeBigUint<64, uint32_t>({1, 0});
  EXPECT_EQ(num / num, expected_quotient);
}

TEST_F(BigUintTest, DivisionMultiWord) {
  // Example: (2^32 + 5) / 2
  TestBigUint64 numerator = MakeBigUint<64, uint32_t>({5, 1}); // 2^32 + 5
  TestBigUint64 divisor = MakeBigUint<64, uint32_t>({2, 0});
  // Expected: (2^32 + 5) / 2 = 2^31 + 2
  // 2^31 is 0x80000000 (uint32_t). 2 is 0x00000002.
  // So, the least significant word should be 0x80000002, and the most significant word should be 0.
  TestBigUint64 expected_quotient = MakeBigUint<64, uint32_t>({0x80000002, 0});
  EXPECT_EQ(numerator / divisor, expected_quotient);

  // Example: (2^64 - 1) / 2
  TestBigUint128 max_val_128 = MakeBigUint<128, uint64_t>({std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()});
  TestBigUint128 two_128 = MakeBigUint<128, uint64_t>({2, 0});
  // (2^128 - 1) / 2 = 2^127 - 1 (remainder 1)
  // 2^127 is 0x8000...0000 (MSB of highest word). Subtracting 1 makes all lower bits 1.
  // So, highest word: 0x7FFFFFFFFFFFFFFF, lowest word: 0xFFFFFFFFFFFFFFFF.
  TestBigUint128 expected_quotient_128 = MakeBigUint<128, uint64_t>({std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max() / 2});
  EXPECT_EQ(max_val_128 / two_128, expected_quotient_128);
}

TEST_F(BigUintTest, DivisionComplexMultiWord) {
  // Test with larger numbers and more complex shifts
  // Example: (2^64 + 2^32) / (2^32 + 1)
  TestBigUint128 num = MakeBigUint<128, uint64_t>({0x100000000ULL, 1ULL}); // 2^32 + 2^64
  TestBigUint128 div = MakeBigUint<128, uint64_t>({0x100000001ULL, 0ULL}); // 2^32 + 1
  // (2^64 + 2^32) = 2^32 * (2^32 + 1)
  // So, result should be 2^32
  TestBigUint128 expected_q = MakeBigUint<128, uint64_t>({0x100000000ULL, 0ULL});
  EXPECT_EQ(num / div, expected_q);
}

// --- Test with Uint256 alias ---
TEST_F(BigUintTest, Uint256Addition) {
  TestUint256 a = MakeBigUint<256, uint64_t>({1, 0, 0, 0});
  TestUint256 b = MakeBigUint<256, uint64_t>({std::numeric_limits<uint64_t>::max(), 0, 0, 0});
  TestUint256 expected = MakeBigUint<256, uint64_t>({0, 1, 0, 0});
  EXPECT_EQ(a + b, expected);

  a = MakeBigUint<256, uint64_t>({std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()});
  b = MakeBigUint<256, uint64_t>({1, 0, 0, 0});
  expected = MakeBigUint<256, uint64_t>({0, 0, 0, 0}); // Overflow beyond 256 bits, result wraps
  EXPECT_EQ(a + b, expected);
}

TEST_F(BigUintTest, Uint256Subtraction) {
  TestUint256 a = MakeBigUint<256, uint64_t>({0, 1, 0, 0}); // 2^64
  TestUint256 b = MakeBigUint<256, uint64_t>({1, 0, 0, 0}); // 1
  TestUint256 expected = MakeBigUint<256, uint64_t>({std::numeric_limits<uint64_t>::max(), 0, 0, 0}); // 2^64 - 1
  EXPECT_EQ(a - b, expected);

  a = MakeBigUint<256, uint64_t>({0, 0, 1, 0}); // 2^128
  b = MakeBigUint<256, uint64_t>({1, 0, 0, 0}); // 1
  expected = MakeBigUint<256, uint64_t>({std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max(), 0, 0}); // 2^128 - 1
  EXPECT_EQ(a - b, expected);
}

TEST_F(BigUintTest, MultiplyByWord) {
  using U = Uint256;

  // 1 * 0 = 0
  EXPECT_EQ(U{1} * 0u, U::Zero());

  // 0 * N = 0
  EXPECT_EQ(U{0} * 12345u, U::Zero());

  // 1 * N = N
  EXPECT_EQ(U{1} * 42u, U{42});

  // N * 1 = N
  U a{123456789u};
  EXPECT_EQ(a * 1u, a);

  // Accumulate result with *=
  U b{0xFFFFFFFFFFFFFFFF};
  b *= 2u;
  EXPECT_EQ(b.Words()[0], 0xFFFFFFFFFFFFFFFEu);
  EXPECT_EQ(b.Words()[1], 1u);
  for (int i = 2; i < U::kWords; ++i)
    EXPECT_EQ(b.Words()[i], 0u);
}

TEST_F(BigUintTest, DivideByWord) {
  using U = Uint256;

  // N / 1 = N
  U a{987654321u};
  EXPECT_EQ(a / 1u, a);

  // N / N = 1
  U b{12345u};
  EXPECT_EQ(b / 12345u, U{1});

  // N / (larger number) = 0
  EXPECT_EQ(U{1000} / 1000000u, U::Zero());

  // Full limb division
  U c{0xFFFFFFFFFFFFFFFF};
  c.Words()[1] = 1;
  EXPECT_EQ(c / 2u, U{0xFFFFFFFFFFFFFFFF});
  EXPECT_EQ((c / 2u).Words()[1], 0u);

  // In-place division
  U d{1209600u * 42};
  d /= 1209600u;
  EXPECT_EQ(d, U{42u});
}

}  // namespace
}  // namespace hornet::util
