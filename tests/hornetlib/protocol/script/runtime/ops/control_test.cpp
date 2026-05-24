#include <gtest/gtest.h>

#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/processor.h"
#include "hornetlib/protocol/script/writer.h"

namespace hornet::protocol::script::runtime::ops {
namespace {

using lang::Op;

void ExpectTopInt(const Processor& processor, int32_t value) {
  const auto top = processor.TryPeekInt();
  ASSERT_TRUE(top.has_value());
  EXPECT_EQ(*top, value);
}

TEST(ControlOpsTest, IfExecutesThenBranchWhenConditionTrue) {
  const auto script = Writer{}.PushInt(1).Then(Op::If).PushInt(2).Then(Op::Else).PushInt(3).Then(Op::EndIf).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  ExpectTopInt(processor, 2);
}

TEST(ControlOpsTest, IfExecutesElseBranchWhenConditionFalse) {
  const auto script = Writer{}.PushInt(0).Then(Op::If).PushInt(2).Then(Op::Else).PushInt(3).Then(Op::EndIf).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  ExpectTopInt(processor, 3);
}

TEST(ControlOpsTest, NotIfExecutesThenBranchWhenConditionFalse) {
  const auto script = Writer{}.PushInt(0).Then(Op::NotIf).PushInt(2).Then(Op::Else).PushInt(3).Then(Op::EndIf).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  ExpectTopInt(processor, 2);
}

TEST(ControlOpsTest, NotIfExecutesElseBranchWhenConditionTrue) {
  const auto script = Writer{}.PushInt(1).Then(Op::NotIf).PushInt(2).Then(Op::Else).PushInt(3).Then(Op::EndIf).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  ExpectTopInt(processor, 3);
}

TEST(ControlOpsTest, IfRequiresOperandWhenExecutable) {
  const auto script = Writer{}.Then(Op::If).PushInt(1).Then(Op::EndIf).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(ControlOpsTest, IfInInactiveBranchDoesNotConsumeOperand) {
  const auto script = Writer{}.PushInt(0).Then(Op::If).Then(Op::If).Then(Op::EndIf).Then(Op::Else).PushInt(1).Then(Op::EndIf).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  ExpectTopInt(processor, 1);
}

TEST(ControlOpsTest, NotIfInInactiveBranchDoesNotConsumeOperand) {
  const auto script =
      Writer{}.PushInt(0).Then(Op::If).Then(Op::NotIf).Then(Op::EndIf).Then(Op::Else).PushInt(1).Then(Op::EndIf).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  ExpectTopInt(processor, 1);
}

TEST(ControlOpsTest, EndIfAfterFalseBranchWithoutElseRestoresExecution) {
  const auto script = Writer{}.PushInt(0).Then(Op::If).PushInt(9).Then(Op::EndIf).PushInt(1).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  ExpectTopInt(processor, 1);
}

TEST(ControlOpsTest, ElseRequiresOpenCondition) {
  const auto script = Writer{}.Then(Op::Else).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::UnbalancedCondition);
}

TEST(ControlOpsTest, EndIfRequiresOpenCondition) {
  const auto script = Writer{}.Then(Op::EndIf).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::UnbalancedCondition);
}

TEST(ControlOpsTest, IfRequiresMatchingEndIf) {
  const auto script = Writer{}.PushInt(1).Then(Op::If).PushInt(2).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::UnbalancedCondition);
}

TEST(ControlOpsTest, MalformedScriptTakesPriorityOverUnbalancedCondition) {
  auto script = Writer{}.PushInt(1).Then(Op::If).Release();
  script.push_back(ToByte(Op::PushData1));

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::MalformedScript);
}

TEST(ControlOpsTest, StepExecutesConditionalsLikeRun) {
  const auto script = Writer{}.PushInt(0).Then(Op::If).PushInt(2).Then(Op::Else).PushInt(3).Then(Op::EndIf).Release();

  Processor processor;
  processor.Reset(script);

  ASSERT_TRUE(processor.Step());
  ASSERT_TRUE(processor.Step());
  ASSERT_TRUE(processor.Step());
  ASSERT_TRUE(processor.Step());
  ASSERT_TRUE(processor.Step());

  const auto last_step = processor.Step();
  ASSERT_TRUE(last_step);
  EXPECT_FALSE(*last_step);
  ExpectTopInt(processor, 3);
}

TEST(ControlOpsTest, StepReportsMalformedScriptBeforeUnbalancedCondition) {
  auto script = Writer{}.PushInt(1).Then(Op::If).Release();
  script.push_back(ToByte(Op::PushData1));

  Processor processor;
  processor.Reset(script);

  ASSERT_TRUE(processor.Step());
  EXPECT_EQ(processor.Step(), lang::Error::MalformedScript);
}

TEST(ControlOpsTest, StepReportsUnbalancedConditionAtScriptEnd) {
  const auto script = Writer{}.PushInt(1).Then(Op::If).PushInt(2).Release();

  Processor processor;
  processor.Reset(script);

  ASSERT_TRUE(processor.Step());
  ASSERT_TRUE(processor.Step());
  EXPECT_EQ(processor.Step(), lang::Error::UnbalancedCondition);
}

TEST(ControlOpsTest, VerifyConsumesTrueAndContinues) {
  const auto script = Writer{}.PushInt(1).Then(Op::Verify).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_FALSE(*result);
  EXPECT_FALSE(processor.TryPeek().has_value());
}

TEST(ControlOpsTest, VerifyFailsAndLeavesFalseOnStack) {
  const auto script = Writer{}.PushInt(0).Then(Op::Verify).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::OpVerify);

  const auto top = processor.TryPeek();
  ASSERT_TRUE(top.has_value());
  EXPECT_TRUE(top->empty());
}

TEST(ControlOpsTest, VerifyRequiresOneOperand) {
  const auto script = Writer{}.Then(Op::Verify).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

}  // namespace
}  // namespace hornet::protocol::script::runtime::ops