#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/stack.h"
#include "hornetlib/protocol/script/runtime/throw.h"

namespace hornet::protocol::script::runtime {
namespace {

using lang::Bytes;
using lang::Error;

void ExpectScriptError(const auto& fn, Error expected) {
  try {
    fn();
    FAIL() << "Expected runtime::Exception";
  } catch (const Exception& ex) {
    EXPECT_EQ(ex.GetError(), expected);
  }
}

TEST(StackCallTest, PopsArgumentsAndPushesReturnValue) {
  Stack stack;
  const std::array<uint8_t, 1> first = {0x11};
  const std::array<uint8_t, 1> second = {0x22};

  stack.Push(first).Push(second);
  stack.Call([](Bytes lhs, Bytes rhs) {
    return lhs[0] == 0x11 && rhs[0] == 0x22;
  });

  ASSERT_EQ(stack.Size(), 1);
  EXPECT_TRUE(stack.TopAsBool());
}

TEST(StackCallTest, PreservesArgumentOrder) {
  Stack stack;
  const std::array<uint8_t, 1> first = {0x01};
  const std::array<uint8_t, 1> second = {0x02};

  stack.Push(first).Push(second);
  stack.Call([](Bytes lhs, Bytes rhs) {
    return lhs[0] < rhs[0];
  });

  ASSERT_EQ(stack.Size(), 1);
  EXPECT_TRUE(stack.TopAsBool());
}

TEST(StackCallTest, VoidCallPopsArgumentsWithoutPushingResult) {
  Stack stack;
  const std::array<uint8_t, 1> base = {0x55};
  const std::array<uint8_t, 1> first = {0x11};
  const std::array<uint8_t, 1> second = {0x22};
  std::vector<uint8_t> observed;

  stack.Push(base).Push(first).Push(second);
  stack.Call([&](Bytes lhs, Bytes rhs) {
    observed.assign({lhs[0], rhs[0]});
  });

  EXPECT_EQ(observed, (std::vector<uint8_t>{0x11, 0x22}));
  ASSERT_EQ(stack.Size(), 1);
  EXPECT_EQ(stack.Top()[0], 0x55);
}

TEST(StackCallTest, ZeroArgumentCallPushesReturnValue) {
  Stack stack;

  stack.Call([]() {
    return true;
  });

  ASSERT_EQ(stack.Size(), 1);
  EXPECT_TRUE(stack.TopAsBool());
}

TEST(StackCallTest, ThrowsWhenThereAreTooFewArguments) {
  Stack stack;
  const std::array<uint8_t, 1> only = {0x42};
  stack.Push(only);

  ExpectScriptError([&] {
    stack.Call([](Bytes lhs, Bytes rhs) {
      return lhs[0] == rhs[0];
    });
  }, Error::StackUnderflow);
}

TEST(StackCallTest, ThrowsWhenReturnValueExceedsStackItemLimit) {
  Stack stack;
  const std::array<uint8_t, 1> item = {0x42};
  const std::vector<uint8_t> oversized(521, 0x99);
  stack.Push(item);

  ExpectScriptError([&] {
    stack.Call([&](Bytes) -> Bytes {
      return oversized;
    });
  }, Error::StackItemOverflow);

  EXPECT_TRUE(stack.Empty());
}

TEST(StackCallTest, SupportsCapturingLambdas) {
  Stack stack;
  const std::array<uint8_t, 1> item = {0x03};
  const uint8_t threshold = 0x02;
  stack.Push(item);

  stack.Call([&](Bytes arg) {
    return arg[0] > threshold;
  });

  ASSERT_EQ(stack.Size(), 1);
  EXPECT_TRUE(stack.TopAsBool());
}

}  // namespace
}  // namespace hornet::protocol::script::runtime