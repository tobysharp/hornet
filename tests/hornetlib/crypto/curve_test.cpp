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
#include <vector>

#include <gtest/gtest.h>

#include "hornetlib/crypto/curve.h"

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

inline constexpr Uint64 kToyPrime{17};
inline constexpr Uint64 kToyA{1};
inline constexpr Uint64 kToyB{4};
inline constexpr Uint64 kToyGeneratorX{4};
inline constexpr Uint64 kToyGeneratorY{2};
inline constexpr Uint64 kToyOrder{7};

using ToyCurve = Curve<64, kToyPrime, kToyA, kToyB, kToyGeneratorX, kToyGeneratorY, kToyOrder>;

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
	const auto x_bytes = ToBigEndianBytes<CurveType>(affine.x.x);
	const auto y_bytes = ToBigEndianBytes<CurveType>(affine.y.x);
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

ToyCurve::Point MakeToyPoint(uint64_t x, uint64_t y) {
	return {ToyCurve::Mod_p{x}, ToyCurve::Mod_p{y}};
}

std::array<uint8_t, 8> MakeToyDigest(uint64_t value) {
	std::array<uint8_t, 8> digest{};
	digest.back() = static_cast<uint8_t>(value);
	return digest;
}

void ExpectToyPointEq(const ToyCurve::Point& actual, const ToyCurve::Point& expected) {
	const ToyCurve::Affine actual_affine = actual;
	const ToyCurve::Affine expected_affine = expected;
	EXPECT_EQ(actual_affine.x, expected_affine.x);
	EXPECT_EQ(actual_affine.y, expected_affine.y);
}

TEST(CurveTest, PointConstructionCopyMoveAndAssignmentPreserveCoordinates) {
	const ToyCurve::Point original = MakeToyPoint(4, 2);
	const ToyCurve::Point copy{original};
	ToyCurve::Point moved{ToyCurve::Point{original}};
	ToyCurve::Point assigned;
	ToyCurve::Point move_assigned;

	assigned = original;
	move_assigned = ToyCurve::Point{original};

	ExpectToyPointEq(copy, original);
	ExpectToyPointEq(moved, original);
	ExpectToyPointEq(assigned, original);
	ExpectToyPointEq(move_assigned, original);
}

TEST(CurveTest, InfinityNegationAndOnCurveBehaveAsExpected) {
	const ToyCurve::Point infinity;
	const ToyCurve::Point generator = ToyCurve::G;
	const ToyCurve::Point negated = -generator;

	EXPECT_TRUE(infinity.IsInfinity());
	EXPECT_FALSE(generator.IsInfinity());
	ExpectToyPointEq(negated, MakeToyPoint(4, 15));
	EXPECT_TRUE(ToyCurve::IsOnCurve(infinity));
	EXPECT_TRUE(ToyCurve::IsOnCurve(generator));
	EXPECT_FALSE(ToyCurve::IsOnCurve(MakeToyPoint(1, 1)));
}

TEST(CurveTest, AffinePlusAffineDistinctPointsMatchesKnownMultiple) {
	const ToyCurve::Point generator = ToyCurve::G;
	const ToyCurve::Point two_g = MakeToyPoint(5, 7);
	const ToyCurve::Point three_g = MakeToyPoint(16, 6);
	const ToyCurve::Affine affine_generator = generator;
	const ToyCurve::Affine affine_two_g = two_g;

	ExpectToyPointEq(ToyCurve::Point{affine_generator + affine_two_g}, three_g);
}

TEST(CurveTest, AffinePlusAffineSamePointMatchesDoublingResult) {
	const ToyCurve::Point generator = ToyCurve::G;
	const ToyCurve::Point two_g = MakeToyPoint(5, 7);
	const ToyCurve::Affine affine_generator = generator;

	ExpectToyPointEq(ToyCurve::Point{affine_generator + affine_generator}, two_g);
}

TEST(CurveTest, AffinePlusJacobianDistinctPointsMatchesKnownMultiple) {
	const ToyCurve::Point generator = ToyCurve::G;
	const ToyCurve::Point two_g = MakeToyPoint(5, 7);
	const ToyCurve::Point three_g = MakeToyPoint(16, 6);
	const ToyCurve::Affine affine_generator = generator;

	ExpectToyPointEq(affine_generator + two_g, three_g);
}

TEST(CurveTest, AffinePlusJacobianSamePointMatchesDoublingResult) {
	const ToyCurve::Point generator = ToyCurve::G;
	const ToyCurve::Point two_g = MakeToyPoint(5, 7);
	const ToyCurve::Affine affine_generator = generator;

	ExpectToyPointEq(affine_generator + generator, two_g);
}

TEST(CurveTest, JacobianPlusJacobianDistinctPointsMatchesKnownMultiple) {
	const ToyCurve::Point generator = ToyCurve::G;
	const ToyCurve::Point two_g = MakeToyPoint(5, 7);
	const ToyCurve::Point three_g = MakeToyPoint(16, 6);

	ExpectToyPointEq(generator + two_g, three_g);
}

TEST(CurveTest, JacobianPlusJacobianSamePointMatchesDoublingResult) {
	const ToyCurve::Point generator = ToyCurve::G;
	const ToyCurve::Point two_g = MakeToyPoint(5, 7);

	ExpectToyPointEq(generator + generator, two_g);
}

TEST(CurveTest, JacobianPointAdditionHandlesInfinityAndInverseInputs) {
	const ToyCurve::Point infinity;
	const ToyCurve::Point generator = ToyCurve::G;

	ExpectToyPointEq(infinity + generator, generator);
	ExpectToyPointEq(generator + infinity, generator);
	EXPECT_TRUE((generator + (-generator)).IsInfinity());
}

TEST(CurveTest, JacobianDoubleMatchesDoublingResultForFinitePoint) {
	const ToyCurve::Point generator = ToyCurve::G;
	const ToyCurve::Point two_g = MakeToyPoint(5, 7);

	ExpectToyPointEq(generator.Double(), two_g);
}

TEST(CurveTest, JacobianDoublePreservesInfinity) {
	const ToyCurve::Point infinity;

	EXPECT_TRUE(infinity.Double().IsInfinity());
}

TEST(CurveTest, PointAddAssignAndScalarMultiplicationMatchKnownMultiples) {
	const ToyCurve::Point generator = ToyCurve::G;
	const ToyCurve::Point three_g = MakeToyPoint(16, 6);
	const ToyCurve::Point subgroup_public_key = MakeToyPoint(16, 6);
	const ToyCurve::Affine affine_generator = generator;

	ToyCurve::Point accumulated = generator;
	accumulated += MakeToyPoint(5, 7);
	ToyCurve::Point mixed_accumulated = MakeToyPoint(5, 7);
	mixed_accumulated += affine_generator;

	ExpectToyPointEq(accumulated, three_g);
	ExpectToyPointEq(mixed_accumulated, three_g);
	ExpectToyPointEq(ToyCurve::Wide{3} * generator, three_g);
	ExpectToyPointEq(ToyCurve::Mod_n{3}.x * generator, three_g);
	EXPECT_TRUE((ToyCurve::Wide{7} * generator).IsInfinity());
	EXPECT_TRUE((kToyOrder * subgroup_public_key).IsInfinity());
}

TEST(CurveTest, PlainNafRecoderProducesExpectedSignedDigits) {
	const auto naf = NonAdjacentForm(ToyCurve::Wide{14});

	EXPECT_EQ(naf[0], 0);
	EXPECT_EQ(naf[1], -1);
	EXPECT_EQ(naf[2], 0);
	EXPECT_EQ(naf[3], 0);
	EXPECT_EQ(naf[4], 1);
	EXPECT_TRUE(std::all_of(naf.begin() + 5, naf.end(), [](int8_t digit) { return digit == 0; }));
}

TEST(CurveTest, PublicKeyValidationRejectsInfinityOffCurveOutOfRangeAndWrongSubgroup) {
	const ToyCurve::Point generator = ToyCurve::G;
	auto out_of_range_x = EncodeUncompressedPublicKey<ToyCurve>(generator);
	auto out_of_range_y = EncodeUncompressedPublicKey<ToyCurve>(generator);
	out_of_range_x[8] = static_cast<uint8_t>(kToyPrime.Words()[0]);
	out_of_range_y[16] = static_cast<uint8_t>(kToyPrime.Words()[0]);

	const auto public_key = ParsePublicKey<ToyCurve>(generator);

	ASSERT_TRUE(public_key.has_value());
	ExpectPublicKeyPointEq<ToyCurve>(*public_key, generator);
	EXPECT_FALSE(ToyCurve::PublicKeyFromSEC1(EncodeUncompressedPublicKey<ToyCurve>(ToyCurve::Point{})).has_value());
	EXPECT_FALSE(ToyCurve::PublicKeyFromSEC1(EncodeUncompressedPublicKey<ToyCurve>(MakeToyPoint(1, 1))).has_value());
	EXPECT_FALSE(ToyCurve::PublicKeyFromSEC1(out_of_range_x).has_value());
	EXPECT_FALSE(ToyCurve::PublicKeyFromSEC1(out_of_range_y).has_value());
	EXPECT_FALSE(ToyCurve::PublicKeyFromSEC1(EncodeUncompressedPublicKey<ToyCurve>(MakeToyPoint(0, 2))).has_value());
}

TEST(CurveTest, VerifySignatureRejectsInvalidScalarBounds) {
	const auto public_key = ParsePublicKey<ToyCurve>(MakeToyPoint(16, 6));
	const std::array<uint8_t, 8> digest = MakeToyDigest(1);

	ASSERT_TRUE(public_key.has_value());
	EXPECT_FALSE(ToyCurve::VerifySignature(*public_key, {Uint64{0}, Uint64{6}}, digest));
	EXPECT_FALSE(ToyCurve::VerifySignature(*public_key, {kToyOrder, Uint64{6}}, digest));
	EXPECT_FALSE(ToyCurve::VerifySignature(*public_key, {Uint64{4}, Uint64{0}}, digest));
	EXPECT_FALSE(ToyCurve::VerifySignature(*public_key, {Uint64{4}, kToyOrder}, digest));
}

TEST(CurveTest, VerifySignatureRejectsInfinityResultAndWrongDigest) {
	const auto public_key = ParsePublicKey<ToyCurve>(MakeToyPoint(16, 6));
	const ToyCurve::Signature valid_signature{Uint64{4}, Uint64{6}};
	const ToyCurve::Signature infinity_signature{Uint64{1}, Uint64{1}};

	ASSERT_TRUE(public_key.has_value());
	EXPECT_FALSE(ToyCurve::VerifySignature(*public_key, infinity_signature, MakeToyDigest(4)));
	EXPECT_FALSE(ToyCurve::VerifySignature(*public_key, valid_signature, MakeToyDigest(2)));
}

TEST(CurveTest, VerifySignatureAcceptsKnownValidToyExample) {
	const auto public_key = ParsePublicKey<ToyCurve>(MakeToyPoint(16, 6));
	const ToyCurve::Signature signature{Uint64{4}, Uint64{6}};

	ASSERT_TRUE(public_key.has_value());
	EXPECT_TRUE(ToyCurve::VerifySignature(*public_key, signature, MakeToyDigest(1)));
}

TEST(CurveTest, VerifySignatureAcceptsToyExampleWithZeroBitStepInLinearCombination) {
	const auto public_key = ParsePublicKey<ToyCurve>(MakeToyPoint(16, 6));
	const ToyCurve::Signature signature{Uint64{2}, Uint64{1}};

	ASSERT_TRUE(public_key.has_value());
	EXPECT_TRUE(ToyCurve::VerifySignature(*public_key, signature, MakeToyDigest(4)));
}

TEST(CurveTest, VerifySignatureReducesResultXCoordinateModuloOrder) {
	const auto public_key = ParsePublicKey<ToyCurve>(ToyCurve::G);
	const ToyCurve::Signature signature{Uint64{2}, Uint64{3}};

	ASSERT_GT((ToyCurve::Wide{3} * ToyCurve::G).NormalizedX().x, kToyOrder);
	ASSERT_TRUE(public_key.has_value());
	EXPECT_TRUE(ToyCurve::VerifySignature(*public_key, signature, MakeToyDigest(0)));
}

TEST(CurveTest, Secp256k1GeneratorIsOnCurveAndInMainSubgroup) {
	EXPECT_TRUE(secp256k1::IsOnCurve(secp256k1::G));
	EXPECT_TRUE((constants::n * secp256k1::G).IsInfinity());
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

	const auto public_key = secp256k1::PublicKeyFromSEC1(public_key_bytes);

	ASSERT_TRUE(public_key.has_value());
	ExpectPublicKeyPointEq<secp256k1>(*public_key, secp256k1::G);

	auto wrong_prefix = public_key_bytes;
	wrong_prefix[0] = 0x03;
	EXPECT_FALSE(secp256k1::PublicKeyFromSEC1(wrong_prefix).has_value());

	auto off_curve = public_key_bytes;
	++off_curve.back();
	EXPECT_FALSE(secp256k1::PublicKeyFromSEC1(off_curve).has_value());
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

	const auto even_public_key = secp256k1::PublicKeyFromSEC1(even_generator);
	const auto odd_public_key = secp256k1::PublicKeyFromSEC1(odd_generator);
	const secp256k1::Affine generator_affine = secp256k1::G;

	ASSERT_TRUE(even_public_key.has_value());
	ASSERT_TRUE(odd_public_key.has_value());
	ExpectPublicKeyPointEq<secp256k1>(*even_public_key, secp256k1::G);
	ExpectPublicKeyPointEq<secp256k1>(*odd_public_key, {generator_affine.x, -generator_affine.y});

	const std::array<uint8_t, 33> invalid_x = {
			0x02,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xfc, 0x2f,
	};
	EXPECT_FALSE(secp256k1::PublicKeyFromSEC1(invalid_x).has_value());
}

TEST(CurveTest, Secp256k1VerifiesKnownDeterministicSignatureExample) {
	const auto public_key = ParsePublicKey<secp256k1>(secp256k1::G);
	const secp256k1::Signature signature{
			"f73f5ad664342164c3997a266e1dc6b066aeddacf4e231cb024c9134dd4a6ab8"_h256,
			"b8f4f7af604af853c210c202c328944c8fe64bd1001154efbaeb3715b3ec9257"_h256};
	const auto digest = "69b595411d2e081915f237bdff5a0a293f32a1138f406f7e8b89984ec74093cd"_bytes;
	const auto wrong_digest = "69b595411d2e081915f237bdff5a0a293f32a1138f406f7e8b89984ec74093cc"_bytes;

	ASSERT_TRUE(public_key.has_value());
	EXPECT_TRUE(secp256k1::VerifySignature(*public_key, signature, digest));
	EXPECT_FALSE(secp256k1::VerifySignature(*public_key, signature, wrong_digest));
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

	const auto public_key = secp256k1::PublicKeyFromSEC1(public_key_bytes);
	const auto signature = ParseDerSignature<secp256k1>(signature_bytes);

	ASSERT_TRUE(public_key.has_value());
	EXPECT_TRUE(secp256k1::VerifySignature(*public_key, signature, hashed_commitment_bytes));
}

}  // namespace
}  // namespace hornet::crypto::ecdsa