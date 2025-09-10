#include <gtest/gtest.h>

#include "hornetlib/protocol/script/parser.h"
#include "hornetlib/protocol/script/processor.h"
#include "hornetlib/protocol/script/view.h"
#include "hornetlib/protocol/script/writer.h"

namespace hornet::protocol::script {
namespace {

using namespace script::lang;

TEST(ScriptTest, RunSimpleScript) {
    // Build a Bitcoin script to evaluate the expression (21 + 21) == 42.
    const auto script = Writer{}.PushInt(21).
                                 PushInt(21).
                                 Then(Op::Add).
                                 PushInt(42).
                                 Then(Op::Equal).Release();

    // Execute the script using the stack-based virtual machine.
    const auto result = Processor{script}.Run();

    // Assert that the script execution completed without error.
    ASSERT_TRUE(result);
    
    // Check the result of execution is 'true'.
    EXPECT_EQ(*result, true);
}

}  // namespace
}  // namespace hornet::protocol::script
