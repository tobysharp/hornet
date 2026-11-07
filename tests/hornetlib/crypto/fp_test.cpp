// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include <array>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>

#include <gtest/gtest.h>

#include "hornetlib/crypto/fp.h"

namespace hornet::crypto::ecdsa {
namespace {

template <size_t kBits, std::unsigned_integral T>
void PrintTo(const util::BigUint<kBits, T>& value, std::ostream* os) {
  *os << "BigUint<" << kBits << ", " << sizeof(T) * 8 << ">{";
  for (int index = util::BigUint<kBits, T>::kWords - 1; index >= 0; --index) {
    *os << std::hex << std::setfill('0') << std::setw(sizeof(T) * 2) << value.Words()[index];
    if (index > 0) *os << "_";
  }
  *os << std::dec << "}";
}

using Uint64 = util::BigUint<64, uint64_t>;
using Uint128 = util::BigUint<128, uint64_t>;

inline constexpr Uint64 kPrime17{17};
inline constexpr Uint64 kPrime11{11};
inline constexpr Uint64 kLargePrime{18446744073709551557ull};
inline constexpr Uint64 kComposite15{15};
inline constexpr Uint128 kPrime128{std::array<uint64_t, 2>{0xffffffffffffff61ull, 0xffffffffffffffffull}};

using Fp17 = Fp<64, kPrime17>;
using Fp11 = Fp<64, kPrime11>;
using FpLarge = Fp<64, kLargePrime>;
using Fp15 = Fp<64, kComposite15>;
using Fp128 = Fp<128, kPrime128>;

Fp17 MakeFp17(uint64_t value) {
  return Fp17{Uint64{value}};
}

Fp11 MakeFp11(uint64_t value) {
  return Fp11{Uint64{value}};
}

FpLarge MakeFpLarge(uint64_t value) {
  return FpLarge{Uint64{value}};
}

Uint128 MakeUint128(uint64_t lo, uint64_t hi) {
  return Uint128{std::array<uint64_t, 2>{lo, hi}};
}

Fp128 MakeFp128(uint64_t lo, uint64_t hi) {
  return Fp128{MakeUint128(lo, hi)};
}

TEST(FpTest, DefaultConstructsToZero) {
  const Fp17 value;
  EXPECT_EQ(value, MakeFp17(0));
}

TEST(FpTest, CopyConstructionAssignmentEqualityAndArrowAccess) {
  const Fp17 original = MakeFp17(5);
  const Fp17 copy{original};
  Fp17 assigned;
  assigned = original;

  EXPECT_EQ(copy, original);
  EXPECT_EQ(assigned, original);
  EXPECT_FALSE(copy != assigned);
  EXPECT_EQ(copy->Words()[0], 5u);
}

TEST(FpTest, InequalityIsTrueForDistinctValues) {
  EXPECT_TRUE(MakeFp17(5) != MakeFp17(6));
}

TEST(FpTest, UnaryMinusHandlesZeroAndNonZero) {
  EXPECT_EQ(-MakeFp17(0), MakeFp17(0));
  EXPECT_EQ(-MakeFp17(16), MakeFp17(1));
  EXPECT_EQ(-MakeFp17(5), MakeFp17(12));
}

TEST(FpTest, AdditionCoversNoReductionAndReductionPaths) {
  EXPECT_EQ(MakeFp17(0) + MakeFp17(9), MakeFp17(9));
  EXPECT_EQ(MakeFp17(5) + MakeFp17(6), MakeFp17(11));
  EXPECT_EQ(MakeFp17(9) + MakeFp17(9), MakeFp17(1));
  EXPECT_EQ(MakeFpLarge(kLargePrime.Words()[0] - 1) + MakeFpLarge(100), MakeFpLarge(99));
}

TEST(FpTest, SubtractionCoversBothBranches) {
  EXPECT_EQ(MakeFp17(0) - MakeFp17(1), MakeFp17(16));
  EXPECT_EQ(MakeFp17(11) - MakeFp17(5), MakeFp17(6));
  EXPECT_EQ(MakeFp17(3) - MakeFp17(5), MakeFp17(15));
  EXPECT_EQ(MakeFp17(5) - MakeFp17(5), MakeFp17(0));
}

TEST(FpTest, MultiplySquareAndStreamOutputUseCanonicalReduction) {
  EXPECT_EQ(MakeFp17(0) * MakeFp17(9), MakeFp17(0));
  EXPECT_EQ(MakeFp17(1) * MakeFp17(9), MakeFp17(9));
  EXPECT_EQ(MakeFp17(5) * MakeFp17(7), MakeFp17(1));
  EXPECT_EQ(MakeFp17(6).Squared(), MakeFp17(2));
  EXPECT_EQ(MakeFp17(16) * MakeFp17(16), MakeFp17(1));

  std::ostringstream stream;
  stream << MakeFp17(5);
  EXPECT_EQ(stream.str(), "\"0000000000000005\"");
}

TEST(FpTest, InverseAndDivisionMatchKnownResults) {
  EXPECT_EQ(MakeFp17(1).Inverse(), MakeFp17(1));
  EXPECT_EQ(MakeFp17(5).Inverse(), MakeFp17(7));
  EXPECT_EQ(MakeFp17(16).Inverse(), MakeFp17(16));
  EXPECT_EQ(MakeFp17(0) / MakeFp17(5), MakeFp17(0));
  EXPECT_EQ(MakeFp17(9) / MakeFp17(9), MakeFp17(1));
  EXPECT_EQ(MakeFp17(6) / MakeFp17(5), MakeFp17(8));
}

TEST(FpTest, InverseAndDivisionThrowForZeroDenominator) {
  EXPECT_THROW(static_cast<void>(MakeFp17(0).Inverse()), std::runtime_error);
  EXPECT_THROW(static_cast<void>(MakeFp17(5) / MakeFp17(0)), std::runtime_error);
}

TEST(FpTest, SquareRootReturnsValidRootForQuadraticResiduesMod11) {
  EXPECT_EQ(MakeFp11(0).SquareRoot(), MakeFp11(0));

  for (uint64_t value : {1u, 3u, 4u, 5u, 9u}) {
    const auto root = MakeFp11(value).SquareRoot();
    ASSERT_TRUE(root.has_value()) << "value=" << value;
    EXPECT_EQ(root->Squared(), MakeFp11(value)) << "value=" << value;
  }
}

TEST(FpTest, SquareRootRejectsQuadraticNonResiduesMod11) {
  for (uint64_t value : {2u, 6u, 7u, 8u, 10u}) {
    EXPECT_FALSE(MakeFp11(value).SquareRoot().has_value()) << "value=" << value;
  }
}

TEST(FpTest, ConstructorRejectsOutOfRangeValuesInDebugBuilds) {
#if !defined(NDEBUG)
  EXPECT_DEATH({
    const Fp17 invalid{Uint64{17}};
    (void)invalid;
  }, "");
#endif
}

TEST(FpTest, ExhaustiveSmallFieldIdentitiesHoldModulo17) {
  for (uint64_t a = 0; a < 17; ++a) {
    const Fp17 fa = MakeFp17(a);
    EXPECT_EQ(fa + (-fa), MakeFp17(0)) << "a=" << a;
    EXPECT_EQ(fa - fa, MakeFp17(0)) << "a=" << a;
    EXPECT_EQ(fa.Squared(), fa * fa) << "a=" << a;

    if (a != 0) {
      EXPECT_EQ(fa * fa.Inverse(), MakeFp17(1)) << "a=" << a;
    }

    for (uint64_t b = 0; b < 17; ++b) {
      const Fp17 fb = MakeFp17(b);
      EXPECT_EQ(fa + fb, MakeFp17((a + b) % 17)) << "a=" << a << ", b=" << b;
      EXPECT_EQ(fa - fb, MakeFp17((a + 17 - b) % 17)) << "a=" << a << ", b=" << b;
      EXPECT_EQ(fa * fb, MakeFp17((a * b) % 17)) << "a=" << a << ", b=" << b;

      if (b != 0) {
        EXPECT_EQ((fa / fb) * fb, fa) << "a=" << a << ", b=" << b;
      }
    }
  }
}

TEST(FpTest, MultiLimbAdditionSubtractionMultiplicationAndDivisionWork) {
  const Fp128 low_word_overflow_sum = MakeFp128(0xfffffffffffffffdull, 0x0ull) + MakeFp128(10ull, 0x0ull);
  const Fp128 expected_sum = MakeFp128(7ull, 1ull);
  EXPECT_EQ(low_word_overflow_sum, expected_sum);

  const Fp128 wrapped_sum = MakeFp128(0xffffffffffffff5cull, 0xffffffffffffffffull) + MakeFp128(10ull, 0x0ull);
  EXPECT_EQ(wrapped_sum, MakeFp128(5ull, 0x0ull));

  const Fp128 borrowed_difference = MakeFp128(3ull, 2ull) - MakeFp128(5ull, 1ull);
  const Fp128 expected_difference = MakeFp128(0xfffffffffffffffeull, 0x0ull);
  EXPECT_EQ(borrowed_difference, expected_difference);

  EXPECT_EQ(MakeFp128(0xffffffffffffff60ull, 0xffffffffffffffffull) *
                MakeFp128(0xffffffffffffff60ull, 0xffffffffffffffffull),
            MakeFp128(1ull, 0x0ull));
  EXPECT_EQ(MakeFp128(0xffffffffffffff60ull, 0xffffffffffffffffull).Inverse(),
            MakeFp128(0xffffffffffffff60ull, 0xffffffffffffffffull));
  EXPECT_EQ(MakeFp128(0ull, 1ull) / MakeFp128(0xffffffffffffff60ull, 0xffffffffffffffffull),
            MakeFp128(0xffffffffffffff61ull, 0xfffffffffffffffeull));
}

TEST(FpDetailTest, IsEvenMatchesLowBitParity) {
  EXPECT_TRUE(detail::IsEven<64>(Uint64{0}));
  EXPECT_TRUE(detail::IsEven<64>(Uint64{12}));
  EXPECT_FALSE(detail::IsEven<64>(Uint64{5}));
}

TEST(FpDetailTest, HalfModuloOddHandlesEvenOddAndCarryPaths) {
  const Uint64 half_even = detail::HalfModuloOdd<64, kPrime17>(Uint64{6});
  const Uint64 half_odd = detail::HalfModuloOdd<64, kPrime17>(Uint64{9});
  const Uint64 half_carry = detail::HalfModuloOdd<64, kLargePrime>(Uint64{99});
  const Uint128 half_multilimb_carry = detail::HalfModuloOdd<128, kPrime128>(MakeUint128(199ull, 0ull));
  EXPECT_EQ(half_even, Uint64{3});
  EXPECT_EQ(half_odd, Uint64{13});
  EXPECT_EQ(half_carry, Uint64{(uint64_t{1} << 63) + 20});
  EXPECT_EQ(half_multilimb_carry, MakeUint128(20ull, 0x8000000000000000ull));
}

TEST(FpDetailTest, MultiplyModuloMatchesKnownReduction) {
  const Uint64 product = detail::MultiplyModuloM<64, kPrime17>(Uint64{8}, Uint64{15});
  EXPECT_EQ(product, Uint64{1});
}

TEST(FpDetailTest, DivideModuloOddUsesInverseProduct) {
  const Uint64 quotient = detail::DivideModuloOdd<64, kPrime17>(Uint64{6}, Uint64{5});
  EXPECT_EQ(quotient, Uint64{8});
}

TEST(FpDetailTest, InvertModuloOddHandlesPrimeAndCompositeModuli) {
  const Uint64 inverse = detail::InvertModuloOdd<64, kPrime17>(Uint64{5});
  EXPECT_EQ(inverse, Uint64{7});
  EXPECT_THROW(static_cast<void>(detail::InvertModuloOdd<64, kComposite15>(Uint64{5})), std::runtime_error);
}

}  // namespace
}  // namespace hornet::crypto::ecdsa