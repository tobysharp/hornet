#include <gtest/gtest.h>

#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/processor.h"
#include "hornetlib/protocol/script/writer.h"

namespace hornet::protocol::script::runtime::ops {
namespace {

using lang::Op;

TEST(BitwiseOpsTest, EqualPushesTrueForEqualOperands) {
  const auto script = Writer{}.PushInt(7).PushInt(7).Then(Op::Equal).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);

  const auto top = processor.TryPeekInt();
  ASSERT_TRUE(top.has_value());
  EXPECT_EQ(*top, 1);
}

TEST(BitwiseOpsTest, EqualPushesFalseForDifferentOperands) {
  const auto script = Writer{}.PushInt(7).PushInt(8).Then(Op::Equal).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_FALSE(*result);

  const auto top = processor.TryPeek();
  ASSERT_TRUE(top.has_value());
  EXPECT_TRUE(top->empty());
}

TEST(BitwiseOpsTest, EqualVerifyConsumesTrueResultOnSuccess) {
  const auto script = Writer{}.PushInt(3).PushInt(3).Then(Op::EqualVerify).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_FALSE(*result);
  EXPECT_FALSE(processor.TryPeek().has_value());
}

TEST(BitwiseOpsTest, EqualVerifyFailsAndLeavesFalseOnStack) {
  const auto script = Writer{}.PushInt(3).PushInt(4).Then(Op::EqualVerify).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::OpEqualVerify);

  const auto top = processor.TryPeek();
  ASSERT_TRUE(top.has_value());
  EXPECT_TRUE(top->empty());
}

TEST(BitwiseOpsTest, EqualVerifyRequiresTwoOperands) {
  const auto script = Writer{}.PushInt(1).Then(Op::EqualVerify).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

}  // namespace
}  // namespace hornet::protocol::script::runtime::ops