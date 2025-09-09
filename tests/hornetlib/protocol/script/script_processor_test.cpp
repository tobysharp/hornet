#include "hornetlib/protocol/script/processor.h"

#include <numeric>
#include <optional>

#include "hornetlib/protocol/script/writer.h"

#include <gtest/gtest.h>

namespace hornet::protocol::script {

void RoundTripInt(int value) {
  Writer w;
  w.PushInt(value);
  Processor vm{w};
  EXPECT_EQ(*vm.Run(), value != 0);
  const auto read = vm.TryPeekInt();
  EXPECT_TRUE(read.has_value());
  EXPECT_EQ(value, *read);
}

TEST(ScriptProcessorTest, TestRoundTripValues) {
  RoundTripInt(std::numeric_limits<int>::min() + 1);  // -2**31+1
  RoundTripInt(0x8000'0001);  // -2**31+1
  RoundTripInt(0xFF80'0001);  // -2**23+1
  RoundTripInt(0xFFFF'8001);  // -2**15+1
  RoundTripInt(0xFFFF'FF81);  // -2**7+1
  RoundTripInt(-129);
  RoundTripInt(-128);
  RoundTripInt(-1);
  RoundTripInt(0);
  for (int i = 1; i <= 16; ++i)
    RoundTripInt(i);
  RoundTripInt(0x7F);
  RoundTripInt(0x80);
  RoundTripInt(255);
  RoundTripInt(256);
  RoundTripInt(0x8000);
  RoundTripInt(0x0000'FFFF);
  RoundTripInt(0x00FF'FFFF);
  RoundTripInt(0xFFFF'FFFF);
}

}  // namespace hornet::protocol::script
