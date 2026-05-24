#include <gtest/gtest.h>

#include <vector>

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

std::vector<uint8_t> WithExpectedStack(Writer writer, std::initializer_list<int32_t> expected_bottom_to_top) {
  writer.Then(Op::Depth).PushInt(static_cast<int32_t>(expected_bottom_to_top.size())).Then(Op::EqualVerify);

  const std::vector<int32_t> expected(expected_bottom_to_top);
  for (auto it = expected.rbegin(); it != expected.rend(); ++it) {
    writer.PushInt(*it).Then(Op::EqualVerify);
  }

  return writer.PushInt(1).Release();
}

void ExpectStackAfterRun(const std::vector<uint8_t>& script, std::initializer_list<int32_t> expected_bottom_to_top,
                         const runtime::Policy& policy = {}) {
  Processor processor{policy};
  const auto result = processor.Run(WithExpectedStack(Writer{script.size() + 16}.Write(script), expected_bottom_to_top));

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  ExpectTopInt(processor, 1);
}

TEST(StackOpsTest, DepthPushesCurrentStackSize) {
  ExpectStackAfterRun(Writer{}.PushInt(7).PushInt(8).Then(Op::Depth).Release(), {7, 8, 2});
}

TEST(StackOpsTest, DepthPushesZeroForEmptyStack) {
  const auto script = Writer{}.Then(Op::Depth).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_FALSE(*result);

  const auto top = processor.TryPeek();
  ASSERT_TRUE(top.has_value());
  EXPECT_TRUE(top->empty());
}

TEST(StackOpsTest, Drop2RemovesTopTwoItems) {
  ExpectStackAfterRun(Writer{}.PushInt(1).PushInt(2).PushInt(3).Then(Op::Drop2).Release(), {1});
}

TEST(StackOpsTest, Drop2RequiresTwoOperands) {
  const auto script = Writer{}.PushInt(1).Then(Op::Drop2).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, Duplicate2CopiesTopTwoItems) {
  ExpectStackAfterRun(Writer{}.PushInt(1).PushInt(2).Then(Op::Duplicate2).Release(), {1, 2, 1, 2});
}

TEST(StackOpsTest, Duplicate2RequiresTwoOperands) {
  const auto script = Writer{}.PushInt(1).Then(Op::Duplicate2).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, Duplicate3CopiesTopThreeItems) {
  ExpectStackAfterRun(Writer{}.PushInt(1).PushInt(2).PushInt(3).Then(Op::Duplicate3).Release(), {1, 2, 3, 1, 2, 3});
}

TEST(StackOpsTest, Duplicate3RequiresThreeOperands) {
  const auto script = Writer{}.PushInt(1).PushInt(2).Then(Op::Duplicate3).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, IfDupDuplicatesTruthyValue) {
  ExpectStackAfterRun(Writer{}.PushInt(5).Then(Op::IfDup).Release(), {5, 5});
}

TEST(StackOpsTest, IfDupLeavesFalseyValueUnchanged) {
  const auto script = Writer{}.PushInt(0).Then(Op::IfDup).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_FALSE(*result);

  const auto top = processor.TryPeek();
  ASSERT_TRUE(top.has_value());
  EXPECT_TRUE(top->empty());
}

TEST(StackOpsTest, IfDupRequiresOneOperand) {
  const auto script = Writer{}.Then(Op::IfDup).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, NipRemovesSecondItemFromTop) {
  ExpectStackAfterRun(Writer{}.PushInt(1).PushInt(2).Then(Op::Nip).Release(), {2});
}

TEST(StackOpsTest, NipRequiresTwoOperands) {
  const auto script = Writer{}.PushInt(1).Then(Op::Nip).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, OverCopiesSecondItemToTop) {
  ExpectStackAfterRun(Writer{}.PushInt(1).PushInt(2).Then(Op::Over).Release(), {1, 2, 1});
}

TEST(StackOpsTest, OverRequiresTwoOperands) {
  const auto script = Writer{}.PushInt(1).Then(Op::Over).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, Over2CopiesThirdAndFourthItemsToTop) {
  ExpectStackAfterRun(Writer{}.PushInt(1).PushInt(2).PushInt(3).PushInt(4).Then(Op::Over2).Release(),
                      {1, 2, 3, 4, 1, 2});
}

TEST(StackOpsTest, Over2RequiresFourOperands) {
  const auto script = Writer{}.PushInt(1).PushInt(2).PushInt(3).Then(Op::Over2).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, PickCopiesNthItemAfterRemovingIndex) {
  ExpectStackAfterRun(Writer{}.PushInt(1).PushInt(2).PushInt(3).PushInt(1).Then(Op::Pick).Release(), {1, 2, 3, 2});
}

TEST(StackOpsTest, PickSupportsZeroDepth) {
  ExpectStackAfterRun(Writer{}.PushInt(1).PushInt(2).PushInt(0).Then(Op::Pick).Release(), {1, 2, 2});
}

TEST(StackOpsTest, PickRejectsNegativeDepth) {
  const auto script = Writer{}.PushInt(1).PushInt(-1).Then(Op::Pick).Release();

  Processor processor;
  const auto result = processor.Run(script);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.Error(), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, PickRejectsTooDeepDepth) {
  const auto script = Writer{}.PushInt(1).PushInt(2).PushInt(2).Then(Op::Pick).Release();

  Processor processor;
  const auto result = processor.Run(script);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.Error(), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, RollMovesNthItemToTopAfterRemovingIndex) {
  ExpectStackAfterRun(Writer{}.PushInt(1).PushInt(2).PushInt(3).PushInt(4).PushInt(5).PushInt(3).Then(Op::Roll).Release(),
                      {1, 3, 4, 5, 2});
}

TEST(StackOpsTest, RollSupportsZeroDepth) {
  ExpectStackAfterRun(Writer{}.PushInt(1).PushInt(2).PushInt(0).Then(Op::Roll).Release(), {1, 2});
}

TEST(StackOpsTest, RollRejectsNegativeDepth) {
  const auto script = Writer{}.PushInt(1).PushInt(-1).Then(Op::Roll).Release();

  Processor processor;
  const auto result = processor.Run(script);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.Error(), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, RollRejectsTooDeepDepth) {
  const auto script = Writer{}.PushInt(1).PushInt(2).PushInt(2).Then(Op::Roll).Release();

  Processor processor;
  const auto result = processor.Run(script);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.Error(), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, RotateReordersTopThreeItems) {
  ExpectStackAfterRun(Writer{}.PushInt(1).PushInt(2).PushInt(3).Then(Op::Rotate).Release(), {2, 3, 1});
}

TEST(StackOpsTest, RotateRequiresThreeOperands) {
  const auto script = Writer{}.PushInt(1).PushInt(2).Then(Op::Rotate).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, Rotate2ReordersTopSixItems) {
  ExpectStackAfterRun(
      Writer{}.PushInt(1).PushInt(2).PushInt(3).PushInt(4).PushInt(5).PushInt(6).Then(Op::Rotate2).Release(),
      {3, 4, 5, 6, 1, 2});
}

TEST(StackOpsTest, Rotate2RequiresSixOperands) {
  const auto script = Writer{}.PushInt(1).PushInt(2).PushInt(3).PushInt(4).PushInt(5).Then(Op::Rotate2).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, SwapExchangesTopTwoItems) {
  ExpectStackAfterRun(Writer{}.PushInt(1).PushInt(2).Then(Op::Swap).Release(), {2, 1});
}

TEST(StackOpsTest, SwapRequiresTwoOperands) {
  const auto script = Writer{}.PushInt(1).Then(Op::Swap).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, Swap2ExchangesTopPairs) {
  ExpectStackAfterRun(Writer{}.PushInt(1).PushInt(2).PushInt(3).PushInt(4).Then(Op::Swap2).Release(), {3, 4, 1, 2});
}

TEST(StackOpsTest, Swap2RequiresFourOperands) {
  const auto script = Writer{}.PushInt(1).PushInt(2).PushInt(3).Then(Op::Swap2).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, TuckCopiesTopItemBelowSecond) {
  ExpectStackAfterRun(Writer{}.PushInt(1).PushInt(2).Then(Op::Tuck).Release(), {2, 1, 2});
}

TEST(StackOpsTest, TuckRequiresTwoOperands) {
  const auto script = Writer{}.PushInt(1).Then(Op::Tuck).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, DuplicateCopiesTopItemAndPreservesLowerItems) {
  const auto script = Writer{}
                          .PushInt(3)
                          .PushInt(7)
                          .Then(Op::Duplicate)
                          .Then(Op::EqualVerify)
                          .PushInt(3)
                          .Then(Op::Equal)
                          .Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  ExpectTopInt(processor, 1);
}

TEST(StackOpsTest, DuplicateRequiresOneOperand) {
  const auto script = Writer{}.Then(Op::Duplicate).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, DropRemovesOnlyTopItem) {
  const auto script = Writer{}.PushInt(3).PushInt(7).Then(Op::Drop).PushInt(3).Then(Op::Equal).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  ExpectTopInt(processor, 1);
}

TEST(StackOpsTest, DropRequiresOneOperand) {
  const auto script = Writer{}.Then(Op::Drop).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, ToAltStackRemovesTopItemFromMainStack) {
  const auto script = Writer{}.PushInt(1).PushInt(2).Then(Op::ToAltStack).PushInt(1).Then(Op::Equal).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  ExpectTopInt(processor, 1);
}

TEST(StackOpsTest, FromAltStackRestoresItemsInLifoOrder) {
  const auto script = Writer{}
                          .PushInt(1)
                          .PushInt(2)
                          .PushInt(3)
                          .Then(Op::ToAltStack)
                          .Then(Op::ToAltStack)
                          .Then(Op::FromAltStack)
                          .PushInt(2)
                          .Then(Op::EqualVerify)
                          .Then(Op::FromAltStack)
                          .PushInt(3)
                          .Then(Op::EqualVerify)
                          .Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  ExpectTopInt(processor, 1);
}

TEST(StackOpsTest, ToAltStackRequiresOneOperand) {
  const auto script = Writer{}.Then(Op::ToAltStack).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, FromAltStackRequiresOneAltOperand) {
  const auto script = Writer{}.Then(Op::FromAltStack).Release();

  Processor processor;
  EXPECT_EQ(processor.Run(script), lang::Error::StackUnderflow);
}

TEST(StackOpsTest, AltStackOpsAreSkippedInInactiveBranch) {
  const auto script = Writer{}
                          .PushInt(0)
                          .Then(Op::If)
                          .Then(Op::Duplicate)
                          .Then(Op::Drop)
                          .Then(Op::ToAltStack)
                          .Then(Op::FromAltStack)
                          .Then(Op::EndIf)
                          .PushInt(1)
                          .Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  ExpectTopInt(processor, 1);
}

TEST(StackOpsTest, AltStackTransfersDoNotOverflowAtSharedStackLimit) {
  Writer writer;
  for (int i = 0; i < runtime::Stack::kMaxItems; ++i) writer.PushInt(1);
  const auto script = writer.Then(Op::ToAltStack).Then(Op::FromAltStack).Release();

  Processor processor;
  const auto result = processor.Run(script);

  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  ExpectTopInt(processor, 1);
}

}  // namespace
}  // namespace hornet::protocol::script::runtime::ops