// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <ostream>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "hornetlib/crypto/curve.h"

namespace hornet::crypto::ecdsa {
namespace {

// The concrete curve under test; the dual-instantiation gate lives in curve.h static_asserts.
using Curve = hornet::crypto::ecdsa::Curve<FieldElement>;

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

// Reference multiples of the secp256k1 generator (computed independently), used to anchor the
// point-arithmetic tests now that the point types are concrete secp256k1 (a = 0).
constexpr Curve::Affine TwoG() {
	return {"c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5"_h256,
	        "1ae168fea63dc339a3c58419466ceaeef7f632653266d0e1236431a950cfe52a"_h256};
}
constexpr Curve::Affine ThreeG() {
	return {"f9308a019258c31049344f85f89d5229b531c845836f99b08601f113bce036f9"_h256,
	        "388f7b0f632de8140fe337e62a37f3566500a99934c2231b6cb9fd7584b8e672"_h256};
}

template <typename CurveType>
typename CurveType::Signature ParseDerSignature(const std::vector<uint8_t>& bytes) {
	EXPECT_GE(bytes.size(), 8u);
	EXPECT_EQ(bytes[0], 0x30);
	EXPECT_EQ(static_cast<size_t>(bytes[1]) + 2, bytes.size());
	EXPECT_EQ(bytes[2], 0x02);

	const size_t r_len = bytes[3];
	const size_t s_tag_index = 4 + r_len;
	EXPECT_LT(s_tag_index + 1, bytes.size());
	EXPECT_EQ(bytes[s_tag_index], 0x02);

	const size_t s_len = bytes[s_tag_index + 1];
	const size_t r_index = 4;
	const size_t s_index = s_tag_index + 2;
	EXPECT_EQ(s_index + s_len, bytes.size());
	EXPECT_LE(r_len, static_cast<size_t>(32));
	EXPECT_LE(s_len, static_cast<size_t>(32));

	std::array<uint8_t, 32> r_bytes{};
	std::array<uint8_t, 32> s_bytes{};
	std::copy(bytes.begin() + r_index, bytes.begin() + s_tag_index, r_bytes.begin() + (32 - r_len));
	std::copy(bytes.begin() + s_index, bytes.end(), s_bytes.begin() + (32 - s_len));
	std::reverse(r_bytes.begin(), r_bytes.end());
	std::reverse(s_bytes.begin(), s_bytes.end());
	return {typename CurveType::Wide{r_bytes}, typename CurveType::Wide{s_bytes}};
}

template <typename CurveType>
std::array<uint8_t, sizeof(typename CurveType::Wide)> ToBigEndianBytes(const typename CurveType::Wide& value) {
	constexpr size_t kBytes = sizeof(typename CurveType::Wide);
	constexpr size_t kBytesPerWord = sizeof(typename CurveType::Wide::Word);
	std::array<uint8_t, kBytes> bytes{};
	for (size_t word_index = 0; word_index < value.Words().size(); ++word_index) {
		const auto word = value.Words()[word_index];
		for (size_t byte_index = 0; byte_index < kBytesPerWord; ++byte_index) {
			bytes[kBytes - 1 - (word_index * kBytesPerWord + byte_index)] =
					static_cast<uint8_t>(word >> (byte_index * 8));
		}
	}
	return bytes;
}

template <typename CurveType>

std::array<uint8_t, 1 + 2 * sizeof(typename CurveType::Wide)> EncodeUncompressedPublicKey(
		const typename CurveType::Point& point) {
	constexpr size_t kBytes = sizeof(typename CurveType::Wide);
	std::array<uint8_t, 1 + 2 * kBytes> bytes{};
	bytes[0] = 0x04;
	const typename CurveType::Affine affine = point;
	const auto x_bytes = ToBigEndianBytes<CurveType>(affine.x.Pack());
	const auto y_bytes = ToBigEndianBytes<CurveType>(affine.y.Pack());
	std::copy(x_bytes.begin(), x_bytes.end(), bytes.begin() + 1);
	std::copy(y_bytes.begin(), y_bytes.end(), bytes.begin() + 1 + kBytes);
	return bytes;
}

template <typename CurveType>
std::optional<typename CurveType::PublicKey> ParsePublicKey(const typename CurveType::Point& point) {
	return CurveType::PublicKeyFromSEC1(EncodeUncompressedPublicKey<CurveType>(point));
}

template <typename CurveType>
void ExpectPublicKeyPointEq(const typename CurveType::PublicKey& actual, const typename CurveType::Point& expected) {
	const typename CurveType::Affine& actual_affine = actual;
	const typename CurveType::Affine expected_affine = expected;
	EXPECT_EQ(actual_affine.x, expected_affine.x);
	EXPECT_EQ(actual_affine.y, expected_affine.y);
}

void ExpectPointEq(const Curve::Point& actual, const Curve::Affine& expected) {
	const Curve::Affine actual_affine = actual;
	EXPECT_EQ(actual_affine.x, expected.x);
	EXPECT_EQ(actual_affine.y, expected.y);
}

TEST(CurveTest, PointConstructionCopyMoveAndAssignmentPreserveCoordinates) {
	const Curve::Point original{Curve::G};
	const Curve::Point copy{original};
	Curve::Point moved{Curve::Point{original}};
	Curve::Point assigned;
	Curve::Point move_assigned;

	assigned = original;
	move_assigned = Curve::Point{original};

	ExpectPointEq(copy, Curve::G);
	ExpectPointEq(moved, Curve::G);
	ExpectPointEq(assigned, Curve::G);
	ExpectPointEq(move_assigned, Curve::G);
}

TEST(CurveTest, InfinityNegationAndOnCurveBehaveAsExpected) {
	const Curve::Point infinity;
	const Curve::Point generator{Curve::G};
	const Curve::Affine off_curve{FieldElement{1}, FieldElement{1}};  // 1 != 1 + 7

	EXPECT_TRUE(infinity.IsInfinity());
	EXPECT_FALSE(generator.IsInfinity());
	ExpectPointEq(-generator, Curve::Affine{Curve::G.x, -Curve::G.y});
	EXPECT_TRUE(Curve::IsOnCurve(infinity));
	EXPECT_TRUE(Curve::IsOnCurve(generator));
	EXPECT_FALSE(Curve::IsOnCurve(Curve::Point{off_curve}));
}

TEST(CurveTest, AffinePlusAffineDistinctPointsMatchesKnownMultiple) {
	const Curve::Affine generator = Curve::G;
	const Curve::Affine two_g = TwoG();

	ExpectPointEq(Curve::Point{generator + two_g}, ThreeG());
}

TEST(CurveTest, AffinePlusAffineSamePointMatchesDoublingResult) {
	const Curve::Affine generator = Curve::G;

	ExpectPointEq(Curve::Point{generator + generator}, TwoG());
}

TEST(CurveTest, AffinePlusJacobianDistinctPointsMatchesKnownMultiple) {
	const Curve::Affine generator = Curve::G;

	ExpectPointEq(generator + Curve::Point{TwoG()}, ThreeG());
}

TEST(CurveTest, AffinePlusJacobianSamePointMatchesDoublingResult) {
	const Curve::Affine generator = Curve::G;

	ExpectPointEq(generator + Curve::Point{generator}, TwoG());
}

TEST(CurveTest, JacobianPlusJacobianDistinctPointsMatchesKnownMultiple) {
	ExpectPointEq(Curve::Point{Curve::G} + Curve::Point{TwoG()}, ThreeG());
}

TEST(CurveTest, JacobianPlusJacobianSamePointMatchesDoublingResult) {
	ExpectPointEq(Curve::Point{Curve::G} + Curve::Point{Curve::G}, TwoG());
}

TEST(CurveTest, JacobianPointAdditionHandlesInfinityAndInverseInputs) {
	const Curve::Point infinity;
	const Curve::Point generator{Curve::G};

	ExpectPointEq(infinity + generator, Curve::G);
	ExpectPointEq(generator + infinity, Curve::G);
	EXPECT_TRUE((generator + (-generator)).IsInfinity());
}

TEST(CurveTest, JacobianDoubleMatchesDoublingResultForFinitePoint) {
	ExpectPointEq(Curve::Point{Curve::G}.Double(), TwoG());
}

TEST(CurveTest, JacobianDoublePreservesInfinity) {
	const Curve::Point infinity;

	EXPECT_TRUE(infinity.Double().IsInfinity());
}

TEST(CurveTest, PointAddAssignAndScalarMultiplicationMatchKnownMultiples) {
	const Curve::Affine generator = Curve::G;

	Curve::Point accumulated{generator};
	accumulated += Curve::Point{TwoG()};
	Curve::Point mixed_accumulated{TwoG()};
	mixed_accumulated += generator;  // mixed: Jacobian += affine

	ExpectPointEq(accumulated, ThreeG());
	ExpectPointEq(mixed_accumulated, ThreeG());
	ExpectPointEq(Curve::Wide{3} * Curve::Point{generator}, ThreeG());
	ExpectPointEq(Curve::Mod_n{3}.x * Curve::Point{generator}, ThreeG());
	EXPECT_TRUE((secp256k1::n * Curve::Point{generator}).IsInfinity());
}

TEST(CurveTest, PlainNafRecoderProducesExpectedSignedDigits) {
	const auto naf = NonAdjacentForm(Uint64{14});

	EXPECT_EQ(naf[0], 0);
	EXPECT_EQ(naf[1], -1);
	EXPECT_EQ(naf[2], 0);
	EXPECT_EQ(naf[3], 0);
	EXPECT_EQ(naf[4], 1);
	EXPECT_TRUE(std::all_of(naf.begin() + 5, naf.end(), [](int8_t digit) { return digit == 0; }));
}

// Reconstructs the integer encoded by a (windowed) NAF digit array. The mathematical
// value fits in 64 bits, so accumulating modulo 2^64 (with signed digits wrapping)
// yields the exact value without intermediate shift/overflow concerns.
template <typename Naf>
uint64_t ReconstructFromNaf(const Naf& naf) {
	uint64_t value = 0;
	for (int index = static_cast<int>(naf.size()) - 1; index >= 0; --index) {
		value = value * 2 + static_cast<uint64_t>(static_cast<int64_t>(naf[index]));
	}
	return value;
}

// A spread of 64-bit test values: small numbers, boundary patterns, and a pseudo-random
// sweep. Returns a fresh vector so each test can iterate over it.
std::vector<uint64_t> WnafTestValues() {
	std::vector<uint64_t> values = {0, 1, 2, 3, 14, 15, 16, 17, 18, 0x80, 0xFF, 0xAAAA, 0x5555,
																	0xFFFFFFFFull, 0x8000000000000000ull, 0x7FFFFFFFFFFFFFFFull,
																	0xFFFFFFFFFFFFFFFFull, 0xDEADBEEFCAFEBABEull};
	for (uint64_t seed = 0; seed < 4000; ++seed) values.push_back(seed * 0x9E3779B97F4A7C15ull);
	return values;
}

TEST(CurveTest, WindowedNafWidthTwoMatchesPlainNaf) {
	// Width-2 wNAF is exactly the plain NAF, so the two recoders must agree digit-for-digit.
	for (const uint64_t value : WnafTestValues()) {
		const Uint64 x{value};
		EXPECT_EQ(WindowedNonAdjacentForm(x, 2), NonAdjacentForm(x)) << "value=" << value;
	}
}

TEST(CurveTest, WindowedNafReconstructsOriginalValue) {
	// Sweep the full supported width range, including w >= 8 where an int8_t digit/accumulator
	// would silently overflow (the bug that previously broke verify).
	for (int w = 2; w <= 12; ++w) {
		for (const uint64_t value : WnafTestValues()) {
			const auto naf = WindowedNonAdjacentForm(Uint64{value}, w);
			EXPECT_EQ(ReconstructFromNaf(naf), value) << "w=" << w << " value=" << value;
		}
	}
}

TEST(CurveTest, WindowedNafDigitsAreSignedOddWithinWindowAndSeparated) {
	for (int w = 2; w <= 12; ++w) {
		const int limit = 1 << (w - 1);  // |digit| < 2^{w-1}
		for (const uint64_t value : WnafTestValues()) {
			const auto naf = WindowedNonAdjacentForm(Uint64{value}, w);
			for (size_t index = 0; index < naf.size(); ++index) {
				const int digit = naf[index];
				if (digit == 0) continue;
				EXPECT_EQ(digit & 1, 1) << "digit not odd: w=" << w << " value=" << value;
				EXPECT_GT(digit, -limit) << "w=" << w << " value=" << value;
				EXPECT_LT(digit, limit) << "w=" << w << " value=" << value;
				// Every nonzero digit is followed by at least w-1 zero digits.
				for (size_t k = 1; k < static_cast<size_t>(w) && index + k < naf.size(); ++k) {
					EXPECT_EQ(naf[index + k], 0) << "no zero run after digit: w=" << w << " value=" << value;
				}
			}
		}
	}
}

TEST(CurveTest, WindowedNafRecoderProducesExpectedSignedDigits) {
	// 14 = 7 * 2^1, a single positive odd digit at the top of the width-4 digit set.
	const auto positive = WindowedNonAdjacentForm(Uint64{14}, 4);
	EXPECT_EQ(positive[1], 7);
	EXPECT_EQ(positive[0], 0);
	EXPECT_TRUE(std::all_of(positive.begin() + 2, positive.end(), [](int8_t d) { return d == 0; }));

	// 18 = -7 * 2^1 + 1 * 2^5, exercising a negative digit and the carry it propagates.
	const auto negative = WindowedNonAdjacentForm(Uint64{18}, 4);
	EXPECT_EQ(negative[1], -7);
	EXPECT_EQ(negative[5], 1);
	EXPECT_EQ(ReconstructFromNaf(negative), 18u);
}

TEST(CurveTest, Secp256k1GeneratorIsOnCurveAndInMainSubgroup) {
	EXPECT_TRUE(Curve::IsOnCurve(Curve::G));
	EXPECT_TRUE((secp256k1::n * Curve::G).IsInfinity());
}

TEST(CurveTest, Secp256k1PublicKeyFromSEC1ParsesAndValidatesUncompressedKeys) {
	const std::array<uint8_t, 65> public_key_bytes = {
			0x04,
			0x79, 0xbe, 0x66, 0x7e, 0xf9, 0xdc, 0xbb, 0xac,
			0x55, 0xa0, 0x62, 0x95, 0xce, 0x87, 0x0b, 0x07,
			0x02, 0x9b, 0xfc, 0xdb, 0x2d, 0xce, 0x28, 0xd9,
			0x59, 0xf2, 0x81, 0x5b, 0x16, 0xf8, 0x17, 0x98,
			0x48, 0x3a, 0xda, 0x77, 0x26, 0xa3, 0xc4, 0x65,
			0x5d, 0xa4, 0xfb, 0xfc, 0x0e, 0x11, 0x08, 0xa8,
			0xfd, 0x17, 0xb4, 0x48, 0xa6, 0x85, 0x54, 0x19,
			0x9c, 0x47, 0xd0, 0x8f, 0xfb, 0x10, 0xd4, 0xb8,
	};

	const auto public_key = Curve::PublicKeyFromSEC1(public_key_bytes);

	ASSERT_TRUE(public_key.has_value());
	ExpectPublicKeyPointEq<Curve>(*public_key, Curve::G);

	auto wrong_prefix = public_key_bytes;
	wrong_prefix[0] = 0x03;
	EXPECT_FALSE(Curve::PublicKeyFromSEC1(wrong_prefix).has_value());

	auto off_curve = public_key_bytes;
	++off_curve.back();
	EXPECT_FALSE(Curve::PublicKeyFromSEC1(off_curve).has_value());
}

TEST(CurveTest, Secp256k1PublicKeyFromSEC1ParsesCompressedKeysWithEitherParity) {
	const std::array<uint8_t, 33> even_generator = {
			0x02,
			0x79, 0xbe, 0x66, 0x7e, 0xf9, 0xdc, 0xbb, 0xac,
			0x55, 0xa0, 0x62, 0x95, 0xce, 0x87, 0x0b, 0x07,
			0x02, 0x9b, 0xfc, 0xdb, 0x2d, 0xce, 0x28, 0xd9,
			0x59, 0xf2, 0x81, 0x5b, 0x16, 0xf8, 0x17, 0x98,
	};
	auto odd_generator = even_generator;
	odd_generator[0] = 0x03;

	const auto even_public_key = Curve::PublicKeyFromSEC1(even_generator);
	const auto odd_public_key = Curve::PublicKeyFromSEC1(odd_generator);
	const Curve::Affine generator_affine = Curve::G;

	ASSERT_TRUE(even_public_key.has_value());
	ASSERT_TRUE(odd_public_key.has_value());
	ExpectPublicKeyPointEq<Curve>(*even_public_key, Curve::G);
	ExpectPublicKeyPointEq<Curve>(*odd_public_key, {generator_affine.x, -generator_affine.y});

	const std::array<uint8_t, 33> invalid_x = {
			0x02,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xfc, 0x2f,
	};
	EXPECT_FALSE(Curve::PublicKeyFromSEC1(invalid_x).has_value());
}

TEST(CurveTest, Secp256k1VerifiesKnownDeterministicSignatureExample) {
	const auto public_key = ParsePublicKey<Curve>(Curve::G);
	const Curve::Signature signature{
			"f73f5ad664342164c3997a266e1dc6b066aeddacf4e231cb024c9134dd4a6ab8"_h256,
			"b8f4f7af604af853c210c202c328944c8fe64bd1001154efbaeb3715b3ec9257"_h256};
	const auto digest = "69b595411d2e081915f237bdff5a0a293f32a1138f406f7e8b89984ec74093cd"_bytes;
	const auto wrong_digest = "69b595411d2e081915f237bdff5a0a293f32a1138f406f7e8b89984ec74093cc"_bytes;

	ASSERT_TRUE(public_key.has_value());
	EXPECT_TRUE(Curve::VerifySignature(*public_key, signature, digest));
	EXPECT_FALSE(Curve::VerifySignature(*public_key, signature, wrong_digest));
}

TEST(CurveTest, Secp256k1VerifyRejectsOutOfRangeScalars) {
	const auto public_key = ParsePublicKey<Curve>(Curve::G);
	const auto digest = "69b595411d2e081915f237bdff5a0a293f32a1138f406f7e8b89984ec74093cd"_bytes;
	const Curve::Wide one{1}, zero{};

	ASSERT_TRUE(public_key.has_value());
	EXPECT_FALSE(Curve::VerifySignature(*public_key, {zero, one}, digest));
	EXPECT_FALSE(Curve::VerifySignature(*public_key, {one, zero}, digest));
	EXPECT_FALSE(Curve::VerifySignature(*public_key, {secp256k1::n, one}, digest));
	EXPECT_FALSE(Curve::VerifySignature(*public_key, {one, secp256k1::n}, digest));
}

TEST(CurveTest, Secp256k1VerifiesBitcoinExampleUsingRawDigestDerAndUncompressedPubkeyBytes) {
	const std::array<uint8_t, 32> hashed_commitment_bytes = {
			0x7a, 0x05, 0xc6, 0x14, 0x5f, 0x10, 0x10, 0x1e,
			0x9d, 0x63, 0x25, 0x49, 0x42, 0x45, 0xad, 0xf1,
			0x29, 0x7d, 0x80, 0xf8, 0xf3, 0x8d, 0x4d, 0x57,
			0x6d, 0x57, 0xcd, 0xba, 0x22, 0x0b, 0xcb, 0x19,
	};

	const std::array<uint8_t, 65> public_key_bytes = {
			0x04,
			0x11, 0xdb, 0x93, 0xe1, 0xdc, 0xdb, 0x8a, 0x01,
			0x6b, 0x49, 0x84, 0x0f, 0x8c, 0x53, 0xbc, 0x1e,
			0xb6, 0x8a, 0x38, 0x2e, 0x97, 0xb1, 0x48, 0x2e,
			0xca, 0xd7, 0xb1, 0x48, 0xa6, 0x90, 0x9a, 0x5c,
			0xb2, 0xe0, 0xea, 0xdd, 0xfb, 0x84, 0xcc, 0xf9,
			0x74, 0x44, 0x64, 0xf8, 0x2e, 0x16, 0x0b, 0xfa,
			0x9b, 0x8b, 0x64, 0xf9, 0xd4, 0xc0, 0x3f, 0x99,
			0x9b, 0x86, 0x43, 0xf6, 0x56, 0xb4, 0x12, 0xa3,
	};

	const std::vector<uint8_t> signature_bytes = {
			0x30, 0x44, 0x02, 0x20, 0x4e, 0x45, 0xe1, 0x69,
			0x32, 0xb8, 0xaf, 0x51, 0x49, 0x61, 0xa1, 0xd3,
			0xa1, 0xa2, 0x5f, 0xdf, 0x3f, 0x4f, 0x77, 0x32,
			0xe9, 0xd6, 0x24, 0xc6, 0xc6, 0x15, 0x48, 0xab,
			0x5f, 0xb8, 0xcd, 0x41, 0x02, 0x20, 0x18, 0x15,
			0x22, 0xec, 0x8e, 0xca, 0x07, 0xde, 0x48, 0x60,
			0xa4, 0xac, 0xdd, 0x12, 0x90, 0x9d, 0x83, 0x1c,
			0xc5, 0x6c, 0xbb, 0xac, 0x46, 0x22, 0x08, 0x22,
			0x21, 0xa8, 0x76, 0x8d, 0x1d, 0x09,
	};

	const auto public_key = Curve::PublicKeyFromSEC1(public_key_bytes);
	const auto signature = ParseDerSignature<Curve>(signature_bytes);

	ASSERT_TRUE(public_key.has_value());
	EXPECT_TRUE(Curve::VerifySignature(*public_key, signature, hashed_commitment_bytes));
}

Curve::Mod_n RandomScalarModN(std::mt19937_64& rng) {
	const std::array<uint64_t, 4> words{rng(), rng(), rng(), rng()};
	auto value = UIntW<256>{words}.Modulo(secp256k1::n);
	if (value == UIntW<256>::Zero()) value = UIntW<256>{1};
	return Curve::Mod_n{value};
}

struct Secp256k1SignedMessage {
	Curve::PublicKey public_key;
	Curve::Signature signature;
	std::array<uint8_t, 32> digest;
};

// Produces a genuine ECDSA signature over a random key pair and random digest, so the verify
// path is exercised with a non-degenerate public key (Q != G).
Secp256k1SignedMessage MakeRandomSecp256k1Signature(std::mt19937_64& rng) {
	const Curve::Mod_n private_key = RandomScalarModN(rng);
	const Curve::Point public_point = private_key.x * Curve::Point{Curve::G};
	const auto public_key = ParsePublicKey<Curve>(public_point);
	EXPECT_TRUE(public_key.has_value());

	std::array<uint8_t, 32> digest{};
	for (auto& byte : digest) byte = static_cast<uint8_t>(rng());
	const Curve::Mod_n z{Curve::Wide::FromBigEndianBytes(digest)};

	const Curve::Mod_n nonce = RandomScalarModN(rng);
	const Curve::Point nonce_point = nonce.x * Curve::Point{Curve::G};
	const Curve::Mod_n r{nonce_point.NormalizedX().Pack().Modulo(secp256k1::n)};
	const Curve::Mod_n s = (z + r * private_key) / nonce;
	return {*public_key, {r.x, s.x}, digest};
}

// Signs a caller-chosen digest, reducing it mod n exactly as verify's HashToInt does -- so digests
// with integer value >= n (which the random helper above never produces) can be exercised.
Secp256k1SignedMessage MakeSecp256k1SignatureForDigest(std::mt19937_64& rng,
                                                       const std::array<uint8_t, 32>& digest) {
	const Curve::Mod_n private_key = RandomScalarModN(rng);
	const Curve::Point public_point = private_key.x * Curve::Point{Curve::G};
	const auto public_key = ParsePublicKey<Curve>(public_point);
	EXPECT_TRUE(public_key.has_value());

	const Curve::Mod_n z{ReduceModuloN(Curve::Wide::FromBigEndianBytes(digest))};
	const Curve::Mod_n nonce = RandomScalarModN(rng);
	const Curve::Point nonce_point = nonce.x * Curve::Point{Curve::G};
	const Curve::Mod_n r{nonce_point.NormalizedX().Pack().Modulo(secp256k1::n)};
	const Curve::Mod_n s = (z + r * private_key) / nonce;
	return {*public_key, {r.x, s.x}, digest};
}

// A digest whose integer value is >= n must be reduced (e = hash mod n, bits2int semantics), not
// trip the Mod_n construction invariant: ~2^-128 of real hashes land there, so it is constructed.
TEST(CurveTest, Secp256k1VerifiesDigestsWithValuesAtOrAboveGroupOrder) {
	std::mt19937_64 rng{0xd1ce5eed00ff1234ull};

	// 2^256 - 1, the maximal digest: e reduces to 2^256 - 1 - n.
	std::array<uint8_t, 32> all_ff;
	all_ff.fill(0xFF);

	// Exactly n: e reduces to 0, so u1 = 0 and R = u2*Q -- the zero G-term end to end.
	const std::array<uint8_t, 32> exactly_n = ToBigEndianBytes<Curve>(secp256k1::n);

	for (const auto& digest : {all_ff, exactly_n}) {
		const auto [public_key, signature, unused] = MakeSecp256k1SignatureForDigest(rng, digest);
		EXPECT_TRUE(Curve::VerifySignature(public_key, signature, digest));

		auto tampered = digest;
		tampered[31] ^= 0x01;
		EXPECT_FALSE(Curve::VerifySignature(public_key, signature, tampered));
	}
}

// The default verify path (wNAF) must agree with the joint-NAF reference on every input: accept
// the same valid signatures and reject the same tampered ones. This is the consensus-critical net.
TEST(CurveTest, Secp256k1WnafVerifyMatchesJointNafOnRandomSignatures) {
	std::mt19937_64 rng{0x5eed0c0ffeed1234ull};
	const auto joint_naf = [](const Curve::Wide& u1, const Curve::Wide& u2, const Curve::Affine& Q) {
		return LinearCombination(u1, Curve::G, u2, Q);
	};

	for (int i = 0; i < 100; ++i) {
		const auto [public_key, signature, digest] = MakeRandomSecp256k1Signature(rng);

		EXPECT_TRUE(Curve::VerifySignature(public_key, signature, digest)) << "i=" << i;
		EXPECT_TRUE(Curve::VerifySignatureWith(public_key, signature, digest, joint_naf)) << "i=" << i;

		auto tampered = digest;
		tampered[i % tampered.size()] ^= 0xFF;
		EXPECT_FALSE(Curve::VerifySignature(public_key, signature, tampered)) << "i=" << i;
		EXPECT_EQ(Curve::VerifySignature(public_key, signature, tampered),
							Curve::VerifySignatureWith(public_key, signature, tampered, joint_naf)) << "i=" << i;
	}
}

// The generator-table window width is configurable and must not affect the result. Sweep it,
// including the previously-broken w >= 8 range, against a known-answer signature.
TEST(CurveTest, Secp256k1WnafVerifyIsCorrectAcrossGeneratorTableWidths) {
	const auto public_key = ParsePublicKey<Curve>(Curve::G);
	const Curve::Signature signature{
			"f73f5ad664342164c3997a266e1dc6b066aeddacf4e231cb024c9134dd4a6ab8"_h256,
			"b8f4f7af604af853c210c202c328944c8fe64bd1001154efbaeb3715b3ec9257"_h256};
	const auto digest = "69b595411d2e081915f237bdff5a0a293f32a1138f406f7e8b89984ec74093cd"_bytes;
	const auto wrong_digest = "69b595411d2e081915f237bdff5a0a293f32a1138f406f7e8b89984ec74093cc"_bytes;

	ASSERT_TRUE(public_key.has_value());
	for (int width = 2; width <= 12; ++width) {
		Curve::BuildGeneratorTable(width);
		EXPECT_TRUE(Curve::VerifySignature(*public_key, signature, digest)) << "width=" << width;
		EXPECT_FALSE(Curve::VerifySignature(*public_key, signature, wrong_digest)) << "width=" << width;
	}
}

// SplitLambda must give k == k1 + k2*lambda (mod n). The half-width bound |k_i| < 2^128 is enforced
// by SignedScalar's UIntW<128> type; a bound violation would truncate there and break reconstruction
// here. lambda (the endomorphism eigenvalue) is a test oracle only; the decomposition itself uses
// the lattice basis, not lambda.
TEST(CurveTest, SplitLambdaDecomposesScalarWithBoundedHalfWidthParts) {
	const auto lambda = "5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72"_h256;
	const auto residue = [](const SignedScalar& s) {
		return s.negative ? secp256k1::n - s.magnitude
		                  : s.magnitude.ZeroExtend<256>();  // canonical [0, n) representative
	};
	std::mt19937_64 rng{0x1abe11ed1234ull};
	for (int i = 0; i < 1000; ++i) {
		const UIntW<256> k = RandomScalarModN(rng).x;
		const auto split = SplitLambda(k);
		const Curve::Mod_n reconstructed = Curve::Mod_n{residue(split.k1)} +
		                                       Curve::Mod_n{residue(split.k2)} * Curve::Mod_n{lambda};
		EXPECT_EQ(reconstructed.x, k) << "i=" << i;
	}
}

// The multiply-shift reciprocals must equal round(2^384 * g / n) exactly -- pinned against
// libsecp256k1's independently-derived g1/g2 constants (scalar_impl.h).
TEST(CurveTest, GlvReciprocalsMatchLibsecp256k1Constants) {
	EXPECT_EQ(detail::kReciprocalB2,
	          "3086d221a7d46bcde86c90e49284eb153daa8a1471e8ca7fe893209a45dbb031"_h256);
	EXPECT_EQ(detail::kReciprocalMinusB1,
	          "e4437ed6010e88286f547fa90abfe4c4221208ac9df506c61571b4ae8ac47f71"_h256);
}

// |g_hat * n - 2^384 * g| <= (n-1)/2 certifies g_hat = round(2^384 * g / n) via multiplication
// alone, independent of the QuotientRemainder path that derives the constants.
TEST(CurveTest, GlvReciprocalsSatisfyRoundingCertificate) {
	const auto check = [](const UIntW<256>& g_hat, const UIntW<256>& g) {
		const UIntW<512> approx = g_hat.MultiplyWide(secp256k1::n);
		const UIntW<512> target = g.ZeroExtend<512>() << 384;
		const UIntW<512> distance = approx < target ? target - approx : approx - target;
		EXPECT_LE(distance, secp256k1::n >> 1);
	};
	check(detail::kReciprocalB2, secp256k1::glv_b2);
	check(detail::kReciprocalMinusB1, secp256k1::glv_minus_b1);
}

// RoundDivide vs the exact rounded quotient floor((g*k + n/2) / n): equal on random scalars, and
// off by exactly the predicted +-1 at adversarial k where g*k mod n == (n -+ 1)/2, i.e. g*k/n sits
// within 1/(2n) of a half-integer and the reciprocal's O(2^-129) error can flip the rounding.
TEST(CurveTest, RoundDivideMatchesExactRoundedQuotientWithinOne) {
	const auto exact = [](const UIntW<256>& g, const UIntW<256>& k) {
		const auto num = g.MultiplyWide(k) + (secp256k1::n >> 1);
		return num.QuotientRemainder(secp256k1::n).first.LowBits<256>();
	};
	const auto offset = [](const UIntW<256>& q, int d) {
		return d >= 0 ? q + UIntW<256>{d} : q - UIntW<256>{-d};
	};

	struct Boundary { UIntW<256> k; int diff_b2, diff_minus_b1; };
	const Boundary boundaries[] = {
		{"e94a482c1def4ec320706b3f29443ebb5b1e139f09cc3403b9bb6b34d521471d"_h256, +1, 0},
		{"16b5b7d3e210b13cdf8f94c0d6bbc1435f90c947a57c6c380616f357fb14fa24"_h256, 0, 0},
		{"18d8b19fe2d58fe6295c19dd8484b7ba8ad71dbab4bc5fc63bf0bbeffc939d9c"_h256, 0, 0},
		{"e7274e601d2a7019d6a3e6227b7b48442fd7bf2bfa8c407583e1a29cd3a2a3a5"_h256, 0, -1},
	};
	for (const auto& [k, diff_b2, diff_minus_b1] : boundaries) {
		EXPECT_EQ(detail::RoundDivide(detail::kReciprocalB2, k),
		          offset(exact(secp256k1::glv_b2, k), diff_b2));
		EXPECT_EQ(detail::RoundDivide(detail::kReciprocalMinusB1, k),
		          offset(exact(secp256k1::glv_minus_b1, k), diff_minus_b1));
	}

	std::mt19937_64 rng{0xd1d1dedeadbeefull};
	for (int i = 0; i < 1000; ++i) {
		const UIntW<256> k = RandomScalarModN(rng).x;
		EXPECT_EQ(detail::RoundDivide(detail::kReciprocalB2, k),
		          exact(secp256k1::glv_b2, k)) << "i=" << i;
		EXPECT_EQ(detail::RoundDivide(detail::kReciprocalMinusB1, k),
		          exact(secp256k1::glv_minus_b1, k)) << "i=" << i;
	}
}

// SplitLambda at edge scalars and at the rounding-boundary ks above: the reconstruction identity
// and the half-width bounds must hold even where the quotient differs from exact rounding.
TEST(CurveTest, SplitLambdaRemainsBoundedAtRoundingBoundaries) {
	const auto lambda = "5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72"_h256;
	const auto residue = [](const SignedScalar& s) {
		return s.negative ? secp256k1::n - s.magnitude : s.magnitude.ZeroExtend<256>();
	};
	const UIntW<256> ks[] = {
		UIntW<256>::Zero(),
		UIntW<256>{1},
		secp256k1::n - UIntW<256>{1},
		lambda,
		secp256k1::n - lambda,
		"00000000000000000000000000000000ffffffffffffffffffffffffffffffff"_h256,
		"0000000000000000000000000000000100000000000000000000000000000000"_h256,
		secp256k1::n >> 1,
		"e94a482c1def4ec320706b3f29443ebb5b1e139f09cc3403b9bb6b34d521471d"_h256,
		"16b5b7d3e210b13cdf8f94c0d6bbc1435f90c947a57c6c380616f357fb14fa24"_h256,
		"18d8b19fe2d58fe6295c19dd8484b7ba8ad71dbab4bc5fc63bf0bbeffc939d9c"_h256,
		"e7274e601d2a7019d6a3e6227b7b48442fd7bf2bfa8c407583e1a29cd3a2a3a5"_h256,
	};
	for (const auto& k : ks) {
		const auto split = SplitLambda(k);
		const Curve::Mod_n reconstructed = Curve::Mod_n{residue(split.k1)} +
		                                       Curve::Mod_n{residue(split.k2)} * Curve::Mod_n{lambda};
		EXPECT_EQ(reconstructed.x, k);
	}
}

// LinearCombination_GLV must equal the joint-NAF reference for u1*G + u2*Q over arbitrary scalars.
TEST(CurveTest, LinearCombinationGlvMatchesJointNaf) {
	constexpr int kWidth = 8;
	std::vector<Curve::Affine> g_base(1u << (kWidth - 1)), g_phi(1u << (kWidth - 1));
	PrecomputeTableAffine(Curve::G, std::span{g_base});
	const FieldElement beta{secp256k1::beta};
	for (size_t j = 0; j < g_base.size(); ++j) g_phi[j] = {beta * g_base[j].x, g_base[j].y};
	const std::span<const Curve::Affine> g_base_span{g_base}, g_phi_span{g_phi};

	const auto split = [](const UIntW<256>& u) {
		return SplitLambda(u);
	};
	std::mt19937_64 rng{0xC0FFEEull};
	for (int i = 0; i < 200; ++i) {
		const UIntW<256> u1 = RandomScalarModN(rng).x, u2 = RandomScalarModN(rng).x;
		const Curve::Affine Q = RandomScalarModN(rng).x * Curve::G;

		const GlvTerm<std::span<const Curve::Affine>, FieldElement> g_term{split(u1), g_base_span, g_phi_span};
		const auto q_term = MakeVariableGlvTerm(split(u2), Q);

		const Curve::Affine glv = LinearCombination_GLV(g_term, q_term);
		const Curve::Affine ref = LinearCombination(u1, Curve::G, u2, Q);
		EXPECT_EQ(glv.x.Pack(), ref.x.Pack()) << "i=" << i;
		EXPECT_EQ(glv.y.Pack(), ref.y.Pack()) << "i=" << i;
	}
}

// The wNAF sign flag must encode the negated value (GLV folds the decomposition sign in at recoding).
TEST(CurveTest, WindowedNafWithNegativeFlagRecodesNegatedValue) {
	for (int w = 2; w <= 12; ++w)
		for (const uint64_t value : WnafTestValues()) {
			const auto naf = WindowedNonAdjacentForm(Uint64{value}, w, /*negative=*/true);
			EXPECT_EQ(ReconstructFromNaf(naf), uint64_t{0} - value) << "w=" << w << " value=" << value;
		}
}

// PrecomputeTableGlobalZ builds the both-signs odd-multiple table in shared-Z form: every entry's
// (x, y) is the point taken at the single returned global Z g, so the true affine multiple is
// (x / g², y / g³) -- recovered here by normalizing the Jacobian point (x, y, g). Every slot must
// match the trusted PrecomputeTableAffine builder, across window widths and the both-signs layout.
// This pins the z-ratio telescoping direction and the g = C * g_C correction.
TEST(CurveTest, PrecomputeTableGlobalZMatchesAffineTableAcrossWidths) {
	std::mt19937_64 rng{0x9106a1b2c3d4e5f6ull};
	for (int w = 2; w <= 10; ++w) {
		const int size = 1 << (w - 1);
		const Curve::Affine P = RandomScalarModN(rng).x * Curve::G;

		std::vector<Curve::Affine> globalz(size), affine(size);
		const auto g = PrecomputeTableGlobalZ(P, std::span{globalz});
		PrecomputeTableAffine(P, std::span{affine});

		EXPECT_FALSE(g == 0) << "w=" << w;
		for (int k = 0; k < size; ++k) {
			const Curve::Affine recovered = Curve::Point{globalz[k].x, globalz[k].y, g};  // (x/g², y/g³)
			EXPECT_EQ(recovered.x.Pack(), affine[k].x.Pack()) << "w=" << w << " k=" << k;
			EXPECT_EQ(recovered.y.Pack(), affine[k].y.Pack()) << "w=" << w << " k=" << k;
		}
	}
}

// Anchor the shared-Z table to independently-computed generator multiples (not another
// addition-based builder), so the differential test above cannot pass on a self-consistent error.
TEST(CurveTest, PrecomputeTableGlobalZAnchorsToKnownGeneratorMultiples) {
	constexpr int w = 5, size = 1 << (w - 1), count = size >> 1;
	std::vector<Curve::Affine> table(size);
	const auto g = PrecomputeTableGlobalZ(Curve::G, std::span{table});
	const auto recover = [&](int k) -> Curve::Affine { return Curve::Point{table[k].x, table[k].y, g}; };

	EXPECT_EQ(recover(count).x.Pack(), Curve::G.x.Pack());         // +1*G
	EXPECT_EQ(recover(count).y.Pack(), Curve::G.y.Pack());
	EXPECT_EQ(recover(count + 1).x.Pack(), ThreeG().x.Pack());     // +3*G
	EXPECT_EQ(recover(count + 1).y.Pack(), ThreeG().y.Pack());
	EXPECT_EQ(recover(count - 1).x.Pack(), (-Curve::G).x.Pack());  // -1*G  (negative half: y-flip, same g)
	EXPECT_EQ(recover(count - 1).y.Pack(), (-Curve::G).y.Pack());
}

// Cubing is 3-to-1 mod p (p = 1 mod 3), so about a third of tiny y values admit an on-curve
// x = (y^2 - 7)^(1/3) -- an attacker searches a handful of candidates and cheaply constructs
// points with tiny y. For those, decompression's SquareRoot may represent its result as y + p,
// whose limb parity is flipped (p is odd): parity selection must read the canonical residue, or
// this node parses a different point than other implementations for the same attacker-supplied
// bytes -- a consensus divergence. p = 7 (mod 9) gives cube roots of cubic residues as
// v^((p+2)/9); each candidate is verified by cubing, so the formula is not load-bearing.
TEST(CurveTest, Secp256k1CompressedParseSelectsCorrectParityForAttackerChosenTinyY) {
	using Ref = Fp<secp256k1::kBits, secp256k1::p>;
	const UIntW<512> cube_root_exponent =
	    (secp256k1::p.ZeroExtend<512>() + UIntW<512>{2}).QuotientRemainder(UIntW<512>{9}).first;
	const auto pow = [](Ref base, const UIntW<512>& exponent) {
		Ref result{1};
		for (unsigned i = 0; i < exponent.SignificantBits(); ++i) {
			if (exponent.GetBit(i)) result = result * base;
			base = base.Squared();
		}
		return result;
	};

	// Tiny y with y^2 - 7 a cubic residue (both parities represented; found by the search above).
	for (const uint64_t y_value : {1ull, 6ull, 11ull, 13ull, 17ull, 20ull}) {
		const Ref y{y_value};
		const Ref x = pow(y.Squared() - Ref{7}, cube_root_exponent);
		ASSERT_EQ((x.Squared() * x + Ref{7}).x, y.Squared().x) << "construction sanity, y=" << y_value;

		std::array<uint8_t, 33> bytes{};
		bytes[0] = static_cast<uint8_t>(0x02 + (y_value & 1));
		const auto x_bytes = ToBigEndianBytes<Curve>(x.x);
		std::copy(x_bytes.begin(), x_bytes.end(), bytes.begin() + 1);

		const auto parsed = Curve::PublicKeyFromSEC1(bytes);
		ASSERT_TRUE(parsed.has_value()) << "y=" << y_value;
		const Curve::Affine& point = *parsed;
		EXPECT_EQ(point.x.Pack(), x.x) << "y=" << y_value;
		EXPECT_EQ(point.y.Pack(), y.x) << "y=" << y_value;

		// The opposite parity byte must select the complementary root p - y.
		std::array<uint8_t, 33> flipped_bytes = bytes;
		flipped_bytes[0] ^= 0x01;
		const auto flipped = Curve::PublicKeyFromSEC1(flipped_bytes);
		ASSERT_TRUE(flipped.has_value()) << "y=" << y_value;
		const Curve::Affine& flipped_point = *flipped;
		EXPECT_EQ(flipped_point.y.Pack(), (-y).x) << "y=" << y_value;
	}
}

}  // namespace
}  // namespace hornet::crypto::ecdsa
