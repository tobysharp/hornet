#include "hornetlib/protocol/script/processor.h"

#include "hornetlib/protocol/script/writer.h"

#include <gtest/gtest.h>

namespace hornet::protocol::script {

void RoundTripInt(int value) {
  Writer w;
  w.PushInt(value);
  Processor vm{w};
  EXPECT_EQ(vm.Run(), value != 0);
  const auto read = vm.TryPeekInt();
  EXPECT_TRUE(read.has_value());
  EXPECT_EQ(value, *read);
}

}  // namespace hornet::protocol::script
