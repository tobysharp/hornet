#include <array>
#include <limits>

#include <gtest/gtest.h>

#include "hornetlib/protocol/script/lang/minimal.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/processor.h"
#include "hornetlib/protocol/script/writer.h"

namespace hornet::protocol::script::runtime::ops {
namespace {

using lang::Error;
using lang::Op;

constexpr int32_t kMinScriptInt32 = std::numeric_limits<int32_t>::min() + 1;

void ExpectTopInt(const Processor& processor, int32_t value) {
  const auto top = processor.TryPeekInt();
  ASSERT_TRUE(top.has_value());
  EXPECT_EQ(*top, value);
}

void ExpectTopBytes(const Processor& processor, std::span<const uint8_t> expected) {
  const auto top = processor.TryPeek();
  ASSERT_TRUE(top.has_value());
  EXPECT_EQ(std::vector<uint8_t>(top->begin(), top->end()), std::vector<uint8_t>(expected.begin(), expected.end()));
}

TEST(ArithmeticOpsTest, BinaryIntOpsProduceExpectedResults) {
  struct Case {
    const char* name;
    Op op;
    int32_t lhs;
    int32_t rhs;
    int32_t expected;
  };

  constexpr std::array cases = {
      Case{"add", Op::Add, 7, -3, 4},
      Case{"subtract", Op::Subtract, 7, -3, 10},
      Case{"min", Op::Min, -4, 3, -4},
      Case{"max", Op::Max, -4, 3, 3},
  };

  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);

    const auto script = Writer{}.PushInt(test_case.lhs).PushInt(test_case.rhs).Then(test_case.op).Release();

    Processor processor;
    const auto result = processor.Run(script);

    ASSERT_TRUE(result);
    EXPECT_EQ(*result, test_case.expected != 0);
    ExpectTopInt(processor, test_case.expected);
  }
}

TEST(ArithmeticOpsTest, AddEncodesZeroAsEmptyVector) {
  const auto script = Writer{}.PushInt(1).PushInt(-1).Then(Op::Add).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_FALSE(*result);

  const auto top = processor.TryPeek();
  ASSERT_TRUE(top.has_value());
  EXPECT_TRUE(top->empty());
}

TEST(ArithmeticOpsTest, AddSupportsResultsBeyondInt32Range) {
  const int64_t expected = int64_t(std::numeric_limits<int32_t>::max()) + 1;
  const auto script = Writer{}.PushInt(std::numeric_limits<int32_t>::max()).PushInt(1).Then(Op::Add).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  ExpectTopBytes(processor, lang::EncodeMinimalInt(expected));
}

TEST(ArithmeticOpsTest, SubtractSupportsResultsBeyondInt32Range) {
  const int64_t expected = int64_t(kMinScriptInt32) - 2;
  const auto script = Writer{}.PushInt(kMinScriptInt32).PushInt(2).Then(Op::Subtract).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  ExpectTopBytes(processor, lang::EncodeMinimalInt(expected));
}

TEST(ArithmeticOpsTest, BooleanOpsUseZeroAndNonZeroTruthiness) {
  struct Case {
    const char* name;
    Op op;
    int32_t lhs;
    int32_t rhs;
    int32_t expected;
  };

  constexpr std::array cases = {
      Case{"and_true", Op::BooleanAnd, -2, 5, 1},
      Case{"and_false", Op::BooleanAnd, 0, 5, 0},
      Case{"or_true", Op::BooleanOr, 0, -1, 1},
      Case{"or_false", Op::BooleanOr, 0, 0, 0},
  };

  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);

    const auto script = Writer{}.PushInt(test_case.lhs).PushInt(test_case.rhs).Then(test_case.op).Release();

    Processor processor;
    const auto result = processor.Run(script);

    ASSERT_TRUE(result);
    EXPECT_EQ(*result, test_case.expected != 0);
    ExpectTopInt(processor, test_case.expected);
  }
}

TEST(ArithmeticOpsTest, ComparisonOpsPushExpectedBooleanResults) {
  struct Case {
    const char* name;
    Op op;
    int32_t lhs;
    int32_t rhs;
    int32_t expected;
  };

  constexpr std::array cases = {
      Case{"greater_than", Op::GreaterThan, 5, 4, 1},
      Case{"greater_than_or_equal", Op::GreaterThanOrEqual, 4, 4, 1},
      Case{"less_than", Op::LessThan, 4, 5, 1},
      Case{"less_than_or_equal", Op::LessThanOrEqual, 4, 4, 1},
      Case{"num_equal_false", Op::NumEqual, 4, 5, 0},
      Case{"num_not_equal_false", Op::NumNotEqual, 4, 4, 0},
  };

  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);

    const auto script = Writer{}.PushInt(test_case.lhs).PushInt(test_case.rhs).Then(test_case.op).Release();

    Processor processor;
    const auto result = processor.Run(script);

    ASSERT_TRUE(result);
    EXPECT_EQ(*result, test_case.expected != 0);
    ExpectTopInt(processor, test_case.expected);
  }
}

TEST(ArithmeticOpsTest, ComparisonOpsHandleInt32Extremes) {
  const auto script = Writer{}
                          .PushInt(kMinScriptInt32)
                          .PushInt(std::numeric_limits<int32_t>::max())
                          .Then(Op::LessThan)
                          .PushInt(std::numeric_limits<int32_t>::max())
                          .PushInt(kMinScriptInt32)
                          .Then(Op::GreaterThan)
                          .Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  ExpectTopInt(processor, 1);
}

TEST(ArithmeticOpsTest, MinAndMaxHandleInt32Extremes) {
  const auto min_script = Writer{}
                              .PushInt(kMinScriptInt32)
                              .PushInt(std::numeric_limits<int32_t>::max())
                              .Then(Op::Min)
                              .Release();
  const auto max_script = Writer{}
                              .PushInt(kMinScriptInt32)
                              .PushInt(std::numeric_limits<int32_t>::max())
                              .Then(Op::Max)
                              .Release();

  Processor min_processor;
  ASSERT_TRUE(min_processor.Run(min_script));
  ExpectTopInt(min_processor, kMinScriptInt32);

  Processor max_processor;
  ASSERT_TRUE(max_processor.Run(max_script));
  ExpectTopInt(max_processor, std::numeric_limits<int32_t>::max());
}

TEST(ArithmeticOpsTest, BinaryIntRejectsFiveByteNegativeOperands) {
  const auto script = Writer{}.PushInt(std::numeric_limits<int32_t>::min()).PushInt(1).Then(Op::Add).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), Error::NumberOverflow);
}

TEST(ArithmeticOpsTest, NumEqualVerifyConsumesMatchingOperands) {
  const auto script = Writer{}.PushInt(99).PushInt(3).PushInt(3).Then(Op::NumEqualVerify).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  ExpectTopInt(processor, 99);
}

TEST(ArithmeticOpsTest, NumEqualVerifyFailsWithoutConsumingOperands) {
  const auto script = Writer{}.PushInt(99).PushInt(3).PushInt(4).Then(Op::NumEqualVerify).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), Error::NumEqualVerify);
  ExpectTopInt(processor, 4);
}

TEST(ArithmeticOpsTest, BinaryOpsRequireTwoOperands) {
  const auto script = Writer{}.PushInt(1).Then(Op::Add).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), Error::StackUnderflow);
  ExpectTopInt(processor, 1);
}

TEST(ArithmeticOpsTest, NumEqualVerifyRequiresTwoOperands) {
  const auto script = Writer{}.PushInt(1).Then(Op::NumEqualVerify).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), Error::StackUnderflow);
  ExpectTopInt(processor, 1);
}

TEST(ArithmeticOpsTest, BinaryIntRejectsNonMinimalNumbers) {
  constexpr std::array<uint8_t, 2> kNonMinimalOne = {0x01, 0x00};
  const auto script = Writer{}.PushData(kNonMinimalOne).PushInt(1).Then(Op::Add).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), Error::NonMinimalNumber);
}

TEST(ArithmeticOpsTest, BinaryIntRejectsOversizedNumbers) {
  constexpr std::array<uint8_t, 5> kOverflowNumber = {0x01, 0x00, 0x00, 0x00, 0x00};
  const auto script = Writer{}.PushData(kOverflowNumber).PushInt(1).Then(Op::Add).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), Error::NumberOverflow);
}

}  // namespace
}  // namespace hornet::protocol::script::runtime::ops