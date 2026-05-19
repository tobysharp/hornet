#include <gtest/gtest.h>

#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/processor.h"
#include "hornetlib/protocol/script/writer.h"

namespace hornet::protocol::script::runtime::ops {
namespace {

using lang::Op;

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