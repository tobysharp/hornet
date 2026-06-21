// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include <array>

#include <gtest/gtest.h>

#include "hornetlib/crypto/curve.h"
#include "hornetlib/crypto/signature.h"

namespace hornet::crypto::ecdsa {
namespace {

constexpr std::array<uint8_t, 70> kStrictDERExample = {
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

constexpr auto kExpectedR = "4e45e16932b8af514961a1d3a1a25fdf3f4f7732e9d624c6c61548ab5fb8cd41"_h256;
constexpr auto kExpectedS = "181522ec8eca07de4860a4acdd12909d831cc56cbbac4622082221a8768d1d09"_h256;

TEST(SignatureTest, ParseSignatureDERLaxParsesStrictDERExample) {
	const auto signature = ParseSignatureDER<Curve::Wide>(kStrictDERExample, DERParseType::Lax);

	ASSERT_TRUE(signature.has_value());
	EXPECT_EQ(signature->first, kExpectedR);
	EXPECT_EQ(signature->second, kExpectedS);
}

TEST(SignatureTest, ParseSignatureDERStrictParsesStrictDERExample) {
	const auto signature = ParseSignatureDER<Curve::Wide>(kStrictDERExample, DERParseType::Strict);

	ASSERT_TRUE(signature.has_value());
	EXPECT_EQ(signature->first, kExpectedR);
	EXPECT_EQ(signature->second, kExpectedS);
}

TEST(SignatureTest, ParseSignatureDERLaxIgnoresTrailingGarbage) {
	const std::array<uint8_t, 72> der = {
			0x30, 0x44, 0x02, 0x20, 0x4e, 0x45, 0xe1, 0x69,
			0x32, 0xb8, 0xaf, 0x51, 0x49, 0x61, 0xa1, 0xd3,
			0xa1, 0xa2, 0x5f, 0xdf, 0x3f, 0x4f, 0x77, 0x32,
			0xe9, 0xd6, 0x24, 0xc6, 0xc6, 0x15, 0x48, 0xab,
			0x5f, 0xb8, 0xcd, 0x41, 0x02, 0x20, 0x18, 0x15,
			0x22, 0xec, 0x8e, 0xca, 0x07, 0xde, 0x48, 0x60,
			0xa4, 0xac, 0xdd, 0x12, 0x90, 0x9d, 0x83, 0x1c,
			0xc5, 0x6c, 0xbb, 0xac, 0x46, 0x22, 0x08, 0x22,
			0x21, 0xa8, 0x76, 0x8d, 0x1d, 0x09, 0xaa, 0xbb,
	};

	const auto signature = ParseSignatureDER<Curve::Wide>(der, DERParseType::Lax);

	ASSERT_TRUE(signature.has_value());
	EXPECT_EQ(signature->first, kExpectedR);
	EXPECT_EQ(signature->second, kExpectedS);
	EXPECT_FALSE(ParseSignatureDER<Curve::Wide>(der, DERParseType::Strict).has_value());
}

TEST(SignatureTest, ParseSignatureDERLaxAcceptsLongFormLengths) {
	const std::array<uint8_t, 11> der = {
			0x30, 0x81, 0x06,
			0x02, 0x81, 0x01, 0x01,
			0x02, 0x81, 0x01, 0x01,
	};

	const auto signature = ParseSignatureDER<Curve::Wide>(der, DERParseType::Lax);

	ASSERT_TRUE(signature.has_value());
	EXPECT_EQ(signature->first, Curve::Wide{1});
	EXPECT_EQ(signature->second, Curve::Wide{1});
	EXPECT_FALSE(ParseSignatureDER<Curve::Wide>(der, DERParseType::Strict).has_value());
}

TEST(SignatureTest, ParseSignatureDERLaxAcceptsOverpaddedIntegerButStrictRejects) {
	const std::array<uint8_t, 9> der = {
			0x30, 0x07,
			0x02, 0x02, 0x00, 0x01,
			0x02, 0x01, 0x01,
	};
	const auto signature = ParseSignatureDER<Curve::Wide>(der, DERParseType::Lax);

	ASSERT_TRUE(signature.has_value());
	EXPECT_EQ(signature->first, Curve::Wide{1});
	EXPECT_EQ(signature->second, Curve::Wide{1});
	EXPECT_FALSE(ParseSignatureDER<Curve::Wide>(der, DERParseType::Strict).has_value());
}

TEST(SignatureTest, ParseSignatureDERLaxAcceptsZeroLengthIntegerButStrictRejects) {
	const std::array<uint8_t, 6> der = {
			0x30, 0x04,
			0x02, 0x00,
			0x02, 0x00,
	};

	const auto signature = ParseSignatureDER<Curve::Wide>(der, DERParseType::Lax);

	ASSERT_TRUE(signature.has_value());
	EXPECT_EQ(signature->first, Curve::Wide{});
	EXPECT_EQ(signature->second, Curve::Wide{});
	EXPECT_FALSE(ParseSignatureDER<Curve::Wide>(der, DERParseType::Strict).has_value());
}

TEST(SignatureTest, ParseSignatureDERLaxTreatsNegativeIntegerAsPositiveButStrictRejects) {
	const std::array<uint8_t, 8> der = {
			0x30, 0x06,
			0x02, 0x01, 0x80,
			0x02, 0x01, 0x01,
	};

	const auto signature = ParseSignatureDER<Curve::Wide>(der, DERParseType::Lax);

	ASSERT_TRUE(signature.has_value());
	EXPECT_EQ(signature->first, Curve::Wide{0x80});
	EXPECT_EQ(signature->second, Curve::Wide{1});
	EXPECT_FALSE(ParseSignatureDER<Curve::Wide>(der, DERParseType::Strict).has_value());
}

TEST(SignatureTest, ParseSignatureDERLaxZeroesOverflowingValues) {
	const std::array<uint8_t, 40> der = {
			0x30, 0x26,
			0x02, 0x21,
			0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00,
			0x02, 0x01, 0x01,
	};

	const auto signature = ParseSignatureDER<Curve::Wide>(der, DERParseType::Lax);

	ASSERT_TRUE(signature.has_value());
	EXPECT_EQ(signature->first, Curve::Wide{});
	EXPECT_EQ(signature->second, Curve::Wide{});
}

TEST(SignatureTest, ParseSignatureDERLaxRejectsMalformedStructure) {
	const std::array<uint8_t, 6> der = {0x30, 0x04, 0x02, 0x01, 0x01, 0x03};

	EXPECT_FALSE(ParseSignatureDER<Curve::Wide>(der, DERParseType::Lax).has_value());
}

}  // namespace
}  // namespace hornet::crypto::ecdsa