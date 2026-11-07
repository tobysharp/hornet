// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/util/big_uint.h"

#include <limits>
#include <stdexcept> // For std::invalid_argument
#include <iomanip>   // For std::hex, std::setfill, std::setw
#include <array>     // For std::array in MakeBigUint

#include <gtest/gtest.h>

// Define a common type for testing to avoid repetition
using TestBigUint64 = hornet::util::BigUint<64, uint32_t>; // 2 words of 32-bit
using TestBigUint64x64 = hornet::util::BigUint<64, uint64_t>; // 1 word of 64-bit
using TestBigUint128 = hornet::util::BigUint<128, uint64_t>; // 2 words of 64-bit
using TestBigUint192 = hornet::util::BigUint<192, uint64_t>; // 3 words of 64-bit
using TestUint256 = hornet::Uint256; // 4 words of 64-bit
using TestBigUint512 = hornet::util::BigUint<512, uint64_t>; // 8 words of 64-bit

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
      // For testing, we'll assume valid input.
      break;
    }
  }
  return BigUint<kBits, T>{temp_words};
}

// Custom GTest printer for BigUint to make EXPECT_EQ output more readable.
template <size_t kBits, std::unsigned_integral T>
void PrintTo(const BigUint<kBits, T>& bu, std::ostream* os) {
  *os << "BigUint<" << kBits << ", " << sizeof(T) * 8 << "> {";
  for (int i = BigUint<kBits, T>::kWords - 1; i >= 0; --i) {
    *os << std::hex << std::setfill('0') << std::setw(sizeof(T) * 2) << bu.Words()[i];
    if (i > 0) {
      *os << "_";
    }
  }
  *os << std::dec << "}";
}


// Test fixture for BigUint operations
class BigUintTest : public ::testing::Test {
};

// --- Constructor and Assignment Tests ---

// Renamed and modified to test value-initialization
TEST_F(BigUintTest, ValueInitializationInitializesToZero) {
  TestBigUint64 bu{}; // Value-initialization
  EXPECT_EQ(bu, TestBigUint64::Zero());
}

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

TEST_F(BigUintTest, AddWithCarryReportsCarryOutAndHonorsCarryIn) {
  uint32_t max_u32 = std::numeric_limits<uint32_t>::max();

  TestBigUint64 a = MakeBigUint<64, uint32_t>({max_u32, 10});
  auto [sum, carry] = a.AddWithCarry(MakeBigUint<64, uint32_t>({0, 0}), true);
  TestBigUint64 expected_sum = MakeBigUint<64, uint32_t>({0, 11});
  EXPECT_EQ(sum, expected_sum);
  EXPECT_FALSE(carry);

  TestBigUint64 b = MakeBigUint<64, uint32_t>({max_u32, max_u32});
  auto [overflow_sum, overflow] = b.AddWithCarry(MakeBigUint<64, uint32_t>({0, 0}), true);
  EXPECT_EQ(overflow_sum, TestBigUint64::Zero());
  EXPECT_TRUE(overflow);
}

TEST_F(BigUintTest, AddWithCarryWithoutCarryInMatchesModularAddition) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({7, 11});
  TestBigUint64 b = MakeBigUint<64, uint32_t>({5, 13});
  auto [sum, carry] = a.AddWithCarry(b);
  EXPECT_EQ(sum, a + b);
  EXPECT_FALSE(carry);

  auto [wrapped_sum, wrapped_carry] =
      TestBigUint64::Maximum().AddWithCarry(MakeBigUint<64, uint32_t>({1, 0}));
  EXPECT_EQ(wrapped_sum, TestBigUint64::Zero());
  EXPECT_TRUE(wrapped_carry);
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

TEST_F(BigUintTest, SubWithBorrowReportsBorrowOutAndHonorsBorrowIn) {
  uint32_t max_u32 = std::numeric_limits<uint32_t>::max();

  TestBigUint64 a = MakeBigUint<64, uint32_t>({0, 11});
  auto [difference, borrow] = a.SubWithBorrow(MakeBigUint<64, uint32_t>({0, 0}), true);
  TestBigUint64 expected_difference = MakeBigUint<64, uint32_t>({max_u32, 10});
  EXPECT_EQ(difference, expected_difference);
  EXPECT_FALSE(borrow);

  auto [underflow_difference, underflow] =
      TestBigUint64::Zero().SubWithBorrow(TestBigUint64::Zero(), true);
  EXPECT_EQ(underflow_difference, TestBigUint64::Maximum());
  EXPECT_TRUE(underflow);
}

TEST_F(BigUintTest, SubWithBorrowWithoutBorrowInMatchesModularSubtraction) {
  TestBigUint64 a = MakeBigUint<64, uint32_t>({25, 30});
  TestBigUint64 b = MakeBigUint<64, uint32_t>({10, 12});
  auto [difference, borrow] = a.SubWithBorrow(b);
  EXPECT_EQ(difference, a - b);
  EXPECT_FALSE(borrow);

  auto [wrapped_difference, wrapped_borrow] =
      TestBigUint64::Zero().SubWithBorrow(MakeBigUint<64, uint32_t>({1, 0}));
  EXPECT_EQ(wrapped_difference, TestBigUint64::Maximum());
  EXPECT_TRUE(wrapped_borrow);
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

TEST_F(BigUintTest, LeftShiftByWordSizeThreeWords) {
  TestBigUint192 a = MakeBigUint<192, uint64_t>({0x0123456789ABCDEFULL, 0x0FEDCBA987654321ULL,
                                                 0xAAAAAAAA55555555ULL});
  TestBigUint192 expected =
      MakeBigUint<192, uint64_t>({0, 0x0123456789ABCDEFULL, 0x0FEDCBA987654321ULL});
  a <<= 64; // Shift by one word on a 3-word value.
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

TEST_F(BigUintTest, RightShiftByWordSizeThreeWords) {
  TestBigUint192 a = MakeBigUint<192, uint64_t>({0x0123456789ABCDEFULL, 0x0FEDCBA987654321ULL,
                                                 0xAAAAAAAA55555555ULL});
  TestBigUint192 expected =
      MakeBigUint<192, uint64_t>({0x0FEDCBA987654321ULL, 0xAAAAAAAA55555555ULL, 0});
  a >>= 64; // Shift by one word on a 3-word value.
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

TEST_F(BigUintTest, DivisionByZeroAsserts) {
  TestBigUint64 numerator = MakeBigUint<64, uint32_t>({10, 0});
  TestBigUint64 divisor = TestBigUint64::Zero();
  EXPECT_DEBUG_DEATH(numerator / divisor, "");
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

  // Full 64x64 -> 128 product in the low two limbs.
  U c{std::numeric_limits<uint64_t>::max()};
  c *= std::numeric_limits<uint64_t>::max();
  EXPECT_EQ(c.Words()[0], 1u);
  EXPECT_EQ(c.Words()[1], std::numeric_limits<uint64_t>::max() - 1);
  for (int i = 2; i < U::kWords; ++i)
    EXPECT_EQ(c.Words()[i], 0u);

  // Carry from one 64-bit product must feed the next limb correctly.
  U d = MakeBigUint<256, uint64_t>({std::numeric_limits<uint64_t>::max(),
                                    std::numeric_limits<uint64_t>::max(), 0, 0});
  d *= 2u;
  EXPECT_EQ(d.Words()[0], std::numeric_limits<uint64_t>::max() - 1);
  EXPECT_EQ(d.Words()[1], std::numeric_limits<uint64_t>::max());
  EXPECT_EQ(d.Words()[2], 1u);
  EXPECT_EQ(d.Words()[3], 0u);
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

  // Exact division of a 128-bit value by a 64-bit divisor.
  U e = MakeBigUint<256, uint64_t>({std::numeric_limits<uint64_t>::max() - 1, 1, 0, 0});
  EXPECT_EQ(e / std::numeric_limits<uint64_t>::max(), U{2u});

  // Division with non-zero remainder from a high limb dividend.
  U f = MakeBigUint<256, uint64_t>({5, 1, 0, 0});
  U expected_f = MakeBigUint<256, uint64_t>({0x5555555555555557ULL, 0, 0, 0});
  EXPECT_EQ(f / 3u, expected_f);
}

TEST_F(BigUintTest, LowBitsAndHighBitsExtractWordAlignedSlices) {
  TestUint256 value = MakeBigUint<256, uint64_t>({1, 2, 3, 4});
  TestBigUint64x64 expected_low_64 = MakeBigUint<64, uint64_t>({1});
  TestBigUint128 expected_low_128 = MakeBigUint<128, uint64_t>({1, 2});
  TestBigUint64x64 expected_high_64 = MakeBigUint<64, uint64_t>({4});
  TestBigUint128 expected_high_128 = MakeBigUint<128, uint64_t>({3, 4});

  EXPECT_EQ(value.template LowBits<64>(), expected_low_64);
  EXPECT_EQ(value.template LowBits<128>(), expected_low_128);
  EXPECT_EQ(value.template HighBits<64>(), expected_high_64);
  EXPECT_EQ(value.template HighBits<128>(), expected_high_128);
  EXPECT_EQ(value.template LowBits<256>(), value);
  EXPECT_EQ(value.template HighBits<256>(), value);
}

TEST_F(BigUintTest, MultiplyWideReturnsFullPrecisionProduct) {
  TestUint256 a =
      MakeBigUint<256, uint64_t>({std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max(), 0, 0});
  TestUint256 b = MakeBigUint<256, uint64_t>({2, 0, 0, 0});

  TestBigUint512 expected = MakeBigUint<512, uint64_t>(
      {std::numeric_limits<uint64_t>::max() - 1, std::numeric_limits<uint64_t>::max(), 1, 0, 0, 0, 0, 0});
  EXPECT_EQ(a.MultiplyWide(b), expected);
  EXPECT_EQ(TestUint256::Zero().MultiplyWide(a), TestBigUint512::Zero());
}

TEST_F(BigUintTest, MultiplyFullSplitsLowAndHighHalves) {
  TestUint256 a = MakeBigUint<256, uint64_t>({0, 0, 0, 1});
  auto [lo, hi] = a.MultiplyFull(a);
  TestUint256 expected_hi = MakeBigUint<256, uint64_t>({0, 0, 1, 0});

  EXPECT_EQ(lo, TestUint256::Zero());
  EXPECT_EQ(hi, expected_hi);
}

TEST_F(BigUintTest, MultiplyFullMatchesMultiplyWideWhenBothHalvesAreNonZero) {
  TestUint256 a = MakeBigUint<256, uint64_t>({std::numeric_limits<uint64_t>::max(),
                                              std::numeric_limits<uint64_t>::max(),
                                              std::numeric_limits<uint64_t>::max(),
                                              std::numeric_limits<uint64_t>::max()});
  TestUint256 b = MakeBigUint<256, uint64_t>({2, 0, 0, 0});

  const auto wide = a.MultiplyWide(b);
  const auto [lo, hi] = a.MultiplyFull(b);

  EXPECT_EQ(lo, wide.template LowBits<256>());
  EXPECT_EQ(hi, wide.template HighBits<256>());
  EXPECT_NE(lo, TestUint256::Zero());
  EXPECT_NE(hi, TestUint256::Zero());
}

}  // namespace
}  // namespace hornet::util
