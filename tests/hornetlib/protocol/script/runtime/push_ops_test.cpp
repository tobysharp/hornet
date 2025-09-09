#include <gtest/gtest.h>

#include "hornetlib/protocol/script/processor.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/lang/minimal.h"
#include "hornetlib/protocol/script/runtime/stack.h"
#include "hornetlib/protocol/script/writer.h"

namespace hornet::protocol::script::runtime {
namespace {

using lang::Op;

static std::vector<uint8_t> MakeData(size_t size, uint8_t fill = 0x42) {
  return std::vector<uint8_t>(size, fill);
}

TEST(ScriptStackTest, PushImmediateConstants) {
  for (int32_t i = -1; i <= 16; ++i) {
    Writer w;
    w.PushInt(i);
    Processor proc(w);
    bool expected = i != 0;
    ASSERT_EQ(*proc.Run(), expected);

    auto opt = proc.TryPeekInt();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(*opt, i);
  }
}

TEST(ScriptStackTest, PushMinimalIntsBeyondImmediateRange) {
  std::vector<int32_t> values = {17, 255, -128, 1024, -32768, 12345678};
  for (int32_t val : values) {
    Writer w;
    w.PushInt(val);
    Processor proc(w);
    ASSERT_TRUE(*proc.Run());
    auto opt = proc.TryPeekInt();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(*opt, val);
  }
}

TEST(ScriptStackTest, PushSize1To75) {
  for (uint8_t size = 1; size <= 75; ++size) {
    Writer w;
    auto data = MakeData(size, 0xAB);
    w.PushData(data);
    Processor proc(w);
    ASSERT_TRUE(*proc.Run());

    auto val = proc.TryPeek();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val->size(), size);
    EXPECT_TRUE(std::all_of(val->begin(), val->end(), [](uint8_t b) { return b == 0xAB; }));
  }
}

TEST(ScriptStackTest, PushData1) {
  Writer w;
  auto data = MakeData(100);
  w.PushData(data);
  Processor proc(w);
  ASSERT_TRUE(*proc.Run());

  auto val = proc.TryPeek();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(std::vector<uint8_t>(val->begin(), val->end()), data);
}

TEST(ScriptStackTest, PushData2) {
  Writer w;
  auto data = MakeData(300);
  w.PushData(data);
  Processor proc(w);
  ASSERT_TRUE(*proc.Run());

  auto val = proc.TryPeek();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(std::vector<uint8_t>(val->begin(), val->end()), data);
}

TEST(ScriptStackTest, OpDuplicate) {
  Writer w;
  w.PushInt(42).Then(Op::Duplicate);
  Processor proc(w);
  ASSERT_TRUE(*proc.Run());

  auto opt = proc.TryPeekInt();
  ASSERT_TRUE(opt.has_value());
  EXPECT_EQ(*opt, 42);
}

TEST(ScriptStackTest, OpDuplicateOnEmptyStackFails) {
  Writer w;
  w.Then(Op::Duplicate);
  Processor proc(w);
  ASSERT_FALSE(proc.Run());

  auto err = proc.LastError();
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(*err, lang::Error::StackUnderflow);
}

TEST(ScriptStackTest, OpDrop) {
  Writer w;
  w.PushInt(99).Then(Op::Drop);
  Processor proc(w);
  ASSERT_FALSE(*proc.Run());

  auto val = proc.TryPeek();
  EXPECT_FALSE(val.has_value());
}

TEST(ScriptStackTest, OpDropOnEmptyStackFails) {
  Writer w;
  w.Then(Op::Drop);
  Processor proc(w);
  ASSERT_FALSE(proc.Run());

  auto err = proc.LastError();
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(*err, lang::Error::StackUnderflow);
}

TEST(ScriptStackTest, PushDataTooLargeFails) {
  Writer w;
  auto data = MakeData(521);  // Just above the limit
  w.PushData(data);           // Writer will pick PUSHDATA2 here
  Processor proc(w);
  ASSERT_FALSE(proc.Run());

  auto err = proc.LastError();
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(*err, lang::Error::StackItemOverflow);
}

TEST(ScriptStackTest, StackOverflowFails) {
  Writer w;
  for (int i = 0; i < 1001; ++i)
    w.PushInt(1);

  Processor proc(w);
  ASSERT_FALSE(proc.Run());

  auto err = proc.LastError();
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(*err, lang::Error::StackOverflow);
}

TEST(ScriptStackTest, PushFalseyFinalStackFails) {
  Writer w;
  w.PushData(std::vector<uint8_t>{0x00});  // A falsey value that is not OP_0

  Processor proc(w, /*require_minimal=*/false);
  ASSERT_FALSE(*proc.Run());
}

TEST(ScriptStackTest, PushExactly520BytesSucceeds) {
  Writer w;
  auto data = MakeData(520);
  w.PushData(data);

  Processor proc(w);
  ASSERT_TRUE(*proc.Run());

  auto val = proc.TryPeek();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(val->size(), 520);
}

static std::vector<uint8_t> MakePushData4(std::span<const uint8_t> data) {
  using lang::Op;

  std::vector<uint8_t> script;
  script.push_back(ToByte(Op::PushData4));

  uint32_t len = static_cast<uint32_t>(data.size());
  for (int i = 0; i < 4; ++i)
    script.push_back((len >> (8 * i)) & 0xFF);

  script.insert(script.end(), data.begin(), data.end());
  return script;
}

TEST(ScriptStackTest, PushData4_NonMinimal_ValidUnderConsensus) {
  auto data = MakeData(520, 0x42);
  auto script = MakePushData4(data);

  Processor proc(script, /*require_minimal=*/false);
  ASSERT_TRUE(*proc.Run());

  auto val = proc.TryPeek();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(std::vector<uint8_t>(val->begin(), val->end()), data);
}

TEST(ScriptStackTest, PushData4_NonMinimal_RejectedByMinimalPolicy) {
  auto data = MakeData(520, 0x42);
  auto script = MakePushData4(data);

  Processor proc(script, /*require_minimal=*/true);
  ASSERT_EQ(proc.Run(), lang::Error::NonMinimalPush);
}

TEST(ScriptStackTest, PushZeroNonMinimalFails) {
  // Script: PUSHSIZE1 0x00
  std::vector<uint8_t> script = {
      ToByte(Op::PushSize1),  // 0x01
      0x00                    // payload: 1 byte of 0x00 (non-minimal encoding of 0)
  };
  Processor proc(script);
  ASSERT_EQ(proc.Run(), lang::Error::NonMinimalPush);
}

TEST(ScriptStackTest, PushNegativeZeroFails) {
  // PUSHSIZE1 0x80 → sign bit set, encodes -0
  std::vector<uint8_t> script = {
      ToByte(Op::PushSize1),  // 0x01
      0x80                    // sign bit: -0 (non-minimal)
  };
  Processor proc(script);
  ASSERT_EQ(proc.Run(), lang::Error::NonMinimalPush);
}

TEST(ScriptStackTest, PushZeroPaddedFails) {
  // PUSHDATA2 0x0002 0x00 0x00 → padded zero
  std::vector<uint8_t> script = {
      ToByte(Op::PushData2),  // 0x4d
      0x02, 0x00,             // length: 2 bytes
      0x00, 0x00              // payload
  };
  Processor proc(script);
  ASSERT_EQ(proc.Run(), lang::Error::NonMinimalPush);
}


}  // namespace
}  // namespace hornet::protocol::script::runtime
