#include <vector>

#include <gtest/gtest.h>

#include "hornetlib/crypto/hash.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/processor.h"
#include "hornetlib/protocol/script/writer.h"
#include "hornetlib/util/hex.h"

namespace hornet::protocol::script::runtime::ops {
namespace {

using lang::Op;

void ExpectTopBytes(const Processor& processor, std::span<const uint8_t> expected) {
  const auto top = processor.TryPeek();
  ASSERT_TRUE(top.has_value());
  EXPECT_EQ(std::vector<uint8_t>(top->begin(), top->end()), std::vector<uint8_t>(expected.begin(), expected.end()));
}

TEST(CryptoOpsTest, Hash160MatchesKnownVectorForHello) {
  static constexpr auto expected = "b6a9c8c230722b7c748331a8b450f05566dc7d0f"_bytes;
  const std::vector<uint8_t> input = {'h', 'e', 'l', 'l', 'o'};
  const auto script = Writer{}.PushData(input).Then(Op::Hash160).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);

  const auto top = processor.TryPeek();
  ASSERT_TRUE(top.has_value());
  EXPECT_EQ(std::vector<uint8_t>(top->begin(), top->end()),
            std::vector<uint8_t>(expected.begin(), expected.end()));
}

TEST(CryptoOpsTest, Hash160MatchesKnownVectorForEmptyInput) {
  static constexpr auto expected = "b472a266d0bd89c13706a4132ccfb16f7c3b9fcb"_bytes;
  const auto script = Writer{}.PushData({}).Then(Op::Hash160).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);

  const auto top = processor.TryPeek();
  ASSERT_TRUE(top.has_value());
  EXPECT_EQ(std::vector<uint8_t>(top->begin(), top->end()),
            std::vector<uint8_t>(expected.begin(), expected.end()));
}

TEST(CryptoOpsTest, Hash160RequiresOneOperand) {
  const auto script = Writer{}.Then(Op::Hash160).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(CryptoOpsTest, Sha256MatchesKnownVectorForHello) {
  static constexpr auto expected =
      "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"_bytes;
  const std::vector<uint8_t> input = {'h', 'e', 'l', 'l', 'o'};
  const auto script = Writer{}.PushData(input).Then(Op::SHA256).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);

  const auto top = processor.TryPeek();
  ASSERT_TRUE(top.has_value());
  EXPECT_EQ(std::vector<uint8_t>(top->begin(), top->end()),
            std::vector<uint8_t>(expected.begin(), expected.end()));
}

TEST(CryptoOpsTest, Sha256MatchesKnownVectorForEmptyInput) {
  static constexpr auto expected =
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"_bytes;
  const auto script = Writer{}.PushData({}).Then(Op::SHA256).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);

  const auto top = processor.TryPeek();
  ASSERT_TRUE(top.has_value());
  EXPECT_EQ(std::vector<uint8_t>(top->begin(), top->end()),
            std::vector<uint8_t>(expected.begin(), expected.end()));
}

TEST(CryptoOpsTest, Sha256RequiresOneOperand) {
  const auto script = Writer{}.Then(Op::SHA256).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(CryptoOpsTest, Sha1MatchesKnownVectorForHello) {
  static constexpr auto expected = "aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d"_bytes;
  const std::vector<uint8_t> input = {'h', 'e', 'l', 'l', 'o'};
  const auto script = Writer{}.PushData(input).Then(Op::SHA1).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  ExpectTopBytes(processor, expected);
}

TEST(CryptoOpsTest, Sha1MatchesKnownVectorForEmptyInput) {
  static constexpr auto expected = "da39a3ee5e6b4b0d3255bfef95601890afd80709"_bytes;
  const auto script = Writer{}.PushData({}).Then(Op::SHA1).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  ExpectTopBytes(processor, expected);
}

TEST(CryptoOpsTest, Sha1RequiresOneOperand) {
  const auto script = Writer{}.Then(Op::SHA1).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(CryptoOpsTest, Hash256MatchesDoubleSha256ForHello) {
  const std::vector<uint8_t> input = {'h', 'e', 'l', 'l', 'o'};
  const auto script = Writer{}.PushData(input).Then(Op::Hash256).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  ExpectTopBytes(processor, crypto::Hash256(input));
}

TEST(CryptoOpsTest, Hash256MatchesDoubleSha256ForEmptyInput) {
  const std::vector<uint8_t> input;
  const auto script = Writer{}.PushData({}).Then(Op::Hash256).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  ExpectTopBytes(processor, crypto::Hash256(input));
}

TEST(CryptoOpsTest, Hash256RequiresOneOperand) {
  const auto script = Writer{}.Then(Op::Hash256).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(CryptoOpsTest, Ripemd160MatchesKnownVectorForHello) {
  static constexpr auto expected = "108f07b8382412612c048d07d13f814118445acd"_bytes;
  const std::vector<uint8_t> input = {'h', 'e', 'l', 'l', 'o'};
  const auto script = Writer{}.PushData(input).Then(Op::RIPEMD160).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  ExpectTopBytes(processor, expected);
}

TEST(CryptoOpsTest, Ripemd160MatchesKnownVectorForEmptyInput) {
  static constexpr auto expected = "9c1185a5c5e9fc54612808977ee8f548b2258d31"_bytes;
  const auto script = Writer{}.PushData({}).Then(Op::RIPEMD160).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  ExpectTopBytes(processor, expected);
}

TEST(CryptoOpsTest, Ripemd160RequiresOneOperand) {
  const auto script = Writer{}.Then(Op::RIPEMD160).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(CryptoOpsTest, CodeSeparatorHasNoStackEffect) {
  const auto script = Writer{}.PushInt(7).Then(Op::CodeSeparator).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);

  const auto top = processor.TryPeekInt();
  ASSERT_TRUE(top.has_value());
  EXPECT_EQ(*top, 7);
}

}  // namespace
}  // namespace hornet::protocol::script::runtime::ops