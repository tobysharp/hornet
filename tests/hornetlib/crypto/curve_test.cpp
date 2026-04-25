// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include <array>
#include <cstdint>
#include <iomanip>
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
typename CurveType::Point ParseUncompressedPublicKey(const std::array<uint8_t, 65>& bytes) {
	EXPECT_EQ(bytes[0], 0x04);

	std::array<uint8_t, 32> x_bytes;
	std::array<uint8_t, 32> y_bytes;
	std::copy(bytes.begin() + 1, bytes.begin() + 33, x_bytes.begin());
	std::copy(bytes.begin() + 33, bytes.end(), y_bytes.begin());
	std::reverse(x_bytes.begin(), x_bytes.end());
	std::reverse(y_bytes.begin(), y_bytes.end());
	return {typename CurveType::Mod_p{typename CurveType::Wide{x_bytes}},
	        typename CurveType::Mod_p{typename CurveType::Wide{y_bytes}}};
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

ToyCurve::Point MakeToyPoint(uint64_t x, uint64_t y) {
	return {ToyCurve::Mod_p{x}, ToyCurve::Mod_p{y}};
}

std::array<uint8_t, 8> MakeToyDigest(uint64_t value) {
	std::array<uint8_t, 8> digest{};
	digest.back() = static_cast<uint8_t>(value);
	return digest;
}

void ExpectToyPointEq(const ToyCurve::Point& actual, const ToyCurve::Point& expected) {
	EXPECT_EQ(actual.x, expected.x);
	EXPECT_EQ(actual.y, expected.y);
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

TEST(CurveTest, PointAdditionCoversInfinityDistinctInverseAndDoublingBranches) {
	const ToyCurve::Point infinity;
	const ToyCurve::Point generator = ToyCurve::G;
	const ToyCurve::Point two_g = MakeToyPoint(5, 7);
	const ToyCurve::Point three_g = MakeToyPoint(16, 6);

	ExpectToyPointEq(infinity + generator, generator);
	ExpectToyPointEq(generator + infinity, generator);
	ExpectToyPointEq(generator + two_g, three_g);
	EXPECT_TRUE((generator + (-generator)).IsInfinity());
	ExpectToyPointEq(generator + generator, two_g);
}

TEST(CurveTest, PointAddAssignAndScalarMultiplicationMatchKnownMultiples) {
	const ToyCurve::Point generator = ToyCurve::G;
	const ToyCurve::Point three_g = MakeToyPoint(16, 6);
	const ToyCurve::Point subgroup_public_key = MakeToyPoint(16, 6);

	ToyCurve::Point accumulated = generator;
	accumulated += MakeToyPoint(5, 7);

	ExpectToyPointEq(accumulated, three_g);
	ExpectToyPointEq(ToyCurve::Wide{3} * generator, three_g);
	ExpectToyPointEq(ToyCurve::Mod_n{3} * generator, three_g);
	EXPECT_TRUE((ToyCurve::Wide{7} * generator).IsInfinity());
	EXPECT_TRUE((kToyOrder * subgroup_public_key).IsInfinity());
}

TEST(CurveTest, PublicKeyValidationRejectsInfinityOffCurveOutOfRangeAndWrongSubgroup) {
	const ToyCurve::Point generator = ToyCurve::G;
	ToyCurve::Point out_of_range_x = generator;
	ToyCurve::Point out_of_range_y = generator;
	out_of_range_x.x.x = kToyPrime;
	out_of_range_y.y.x = kToyPrime;

	EXPECT_TRUE(ToyCurve::IsPublicKeyValid(generator));
	EXPECT_FALSE(ToyCurve::IsPublicKeyValid(ToyCurve::Point{}));
	EXPECT_FALSE(ToyCurve::IsPublicKeyValid(MakeToyPoint(1, 1)));
	EXPECT_FALSE(ToyCurve::IsPublicKeyValid(out_of_range_x));
	EXPECT_FALSE(ToyCurve::IsPublicKeyValid(out_of_range_y));
	EXPECT_FALSE(ToyCurve::IsPublicKeyValid(MakeToyPoint(0, 2)));
}

TEST(CurveTest, VerifySignatureRejectsInvalidPublicKeyAndInvalidScalarBounds) {
	const ToyCurve::Point public_key = MakeToyPoint(16, 6);
	const std::array<uint8_t, 8> digest = MakeToyDigest(1);

	EXPECT_FALSE(ToyCurve::VerifySignature(ToyCurve::Point{}, {Uint64{4}, Uint64{6}}, digest));
	EXPECT_FALSE(ToyCurve::VerifySignature(public_key, {Uint64{0}, Uint64{6}}, digest));
	EXPECT_FALSE(ToyCurve::VerifySignature(public_key, {kToyOrder, Uint64{6}}, digest));
	EXPECT_FALSE(ToyCurve::VerifySignature(public_key, {Uint64{4}, Uint64{0}}, digest));
	EXPECT_FALSE(ToyCurve::VerifySignature(public_key, {Uint64{4}, kToyOrder}, digest));
}

TEST(CurveTest, VerifySignatureRejectsInfinityResultAndWrongDigest) {
	const ToyCurve::Point public_key = MakeToyPoint(16, 6);
	const ToyCurve::Signature valid_signature{Uint64{4}, Uint64{6}};
	const ToyCurve::Signature infinity_signature{Uint64{1}, Uint64{1}};

	EXPECT_FALSE(ToyCurve::VerifySignature(public_key, infinity_signature, MakeToyDigest(4)));
	EXPECT_FALSE(ToyCurve::VerifySignature(public_key, valid_signature, MakeToyDigest(2)));
}

TEST(CurveTest, VerifySignatureAcceptsKnownValidToyExample) {
	const ToyCurve::Point public_key = MakeToyPoint(16, 6);
	const ToyCurve::Signature signature{Uint64{4}, Uint64{6}};

	EXPECT_TRUE(ToyCurve::VerifySignature(public_key, signature, MakeToyDigest(1)));
}

TEST(CurveTest, VerifySignatureReducesResultXCoordinateModuloOrder) {
	const ToyCurve::Point public_key = ToyCurve::G;
	const ToyCurve::Signature signature{Uint64{2}, Uint64{3}};

	ASSERT_GT((ToyCurve::Wide{3} * ToyCurve::G).x.x, kToyOrder);
	EXPECT_TRUE(ToyCurve::VerifySignature(public_key, signature, MakeToyDigest(0)));
}

TEST(CurveTest, Secp256k1GeneratorIsOnCurveAndValidPublicKey) {
	EXPECT_TRUE(secp256k1::IsOnCurve(secp256k1::G));
	EXPECT_TRUE(secp256k1::IsPublicKeyValid(secp256k1::G));
	EXPECT_TRUE((constants::n * secp256k1::G).IsInfinity());
}

TEST(CurveTest, Secp256k1VerifiesKnownDeterministicSignatureExample) {
	const secp256k1::Point public_key{
			secp256k1::Mod_p{"79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"_h256},
			secp256k1::Mod_p{"483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8"_h256}};
	const secp256k1::Signature signature{
			"f73f5ad664342164c3997a266e1dc6b066aeddacf4e231cb024c9134dd4a6ab8"_h256,
			"b8f4f7af604af853c210c202c328944c8fe64bd1001154efbaeb3715b3ec9257"_h256};
	const auto digest = "69b595411d2e081915f237bdff5a0a293f32a1138f406f7e8b89984ec74093cd"_bytes;
	const auto wrong_digest = "69b595411d2e081915f237bdff5a0a293f32a1138f406f7e8b89984ec74093cc"_bytes;

	EXPECT_TRUE(secp256k1::VerifySignature(public_key, signature, digest));
	EXPECT_FALSE(secp256k1::VerifySignature(public_key, signature, wrong_digest));
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

	const auto public_key = ParseUncompressedPublicKey<secp256k1>(public_key_bytes);
	const auto signature = ParseDerSignature<secp256k1>(signature_bytes);

	EXPECT_TRUE(secp256k1::VerifySignature(public_key, signature, hashed_commitment_bytes));
}

}  // namespace
}  // namespace hornet::crypto::ecdsa