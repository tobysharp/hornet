// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/protocol/script/script_view.h"

#include <cstdint>
#include <span>
#include <vector>

#include "hornetlib/protocol/script/instruction.h"   // Instruction { Op opcode; span data; }
#include "hornetlib/protocol/script/op.h"            // enum class Op : uint8_t { PushData1, ... }

#include <gtest/gtest.h>

namespace hornet::protocol::script {
namespace {

TEST(ScriptViewInstructions, EmptyScriptYieldsNoElements) {
  std::vector<uint8_t> script{};
  ScriptView view(std::span<const uint8_t>(script.data(), script.size()));

  size_t count = 0;
  for (const Instruction& ins : view.Instructions()) {
    (void)ins;
    ++count;
  }
  EXPECT_EQ(count, 0u);
}

TEST(ScriptViewInstructions, ParsesMixedSequence) {
  // Bytes:
  //  0x02 <AA BB>         (small push of 2 bytes)
  //  0xAC                 (some non-push opcode; replace with Op::CheckSig if you have it)
  //  PUSHDATA1 0x03 <DE AD BE>
  //  PUSHDATA2 0x01 0x00 <FF>
  std::vector<uint8_t> script = {
      0x02, 0xAA, 0xBB,
      0xAC,
      static_cast<uint8_t>(Op::PushData1), 0x03, 0xDE, 0xAD, 0xBE,
      static_cast<uint8_t>(Op::PushData2), 0x01, 0x00, 0xFF
  };

  ScriptView view(std::span<const uint8_t>(script.data(), script.size()));

  std::vector<Instruction> got;
  for (const Instruction& ins : view.Instructions()) {
    got.push_back(ins);
  }

  ASSERT_EQ(got.size(), 4u);

  // 1) small push of length 2
  EXPECT_EQ(static_cast<uint8_t>(got[0].opcode), 0x02);
  ASSERT_EQ(got[0].data.size(), 2u);
  EXPECT_EQ(got[0].data[0], 0xAA);
  EXPECT_EQ(got[0].data[1], 0xBB);

  // 2) non-push opcode, no data
  EXPECT_EQ(static_cast<uint8_t>(got[1].opcode), 0xAC);
  EXPECT_EQ(got[1].data.size(), 0u);

  // 3) PUSHDATA1 of length 3
  EXPECT_EQ(got[2].opcode, Op::PushData1);
  ASSERT_EQ(got[2].data.size(), 3u);
  EXPECT_EQ(got[2].data[0], 0xDE);
  EXPECT_EQ(got[2].data[1], 0xAD);
  EXPECT_EQ(got[2].data[2], 0xBE);

  // 4) PUSHDATA2 of length 1 (little endian)
  EXPECT_EQ(got[3].opcode, Op::PushData2);
  ASSERT_EQ(got[3].data.size(), 1u);
  EXPECT_EQ(got[3].data[0], 0xFF);
}

TEST(ScriptViewInstructions, ZeroLengthPushDataIsParsed) {
  // PUSHDATA4 with length=0 → legal, pushes empty vector
  std::vector<uint8_t> script = {
      static_cast<uint8_t>(Op::PushData4), 0x00, 0x00, 0x00, 0x00
  };

  ScriptView view(std::span<const uint8_t>(script.data(), script.size()));

  size_t seen = 0;
  for (const Instruction& ins : view.Instructions()) {
    ++seen;
    EXPECT_EQ(ins.opcode, Op::PushData4);
    EXPECT_EQ(ins.data.size(), 0u);
  }
  EXPECT_EQ(seen, 1u);
}

TEST(ScriptViewInstructions, MalformedHeaderTerminatesIterationCleanly) {
  // PUSHDATA2 but only 1 length byte present (needs 2) → malformed immediately
  std::vector<uint8_t> script = {
      static_cast<uint8_t>(Op::PushData2), 0x05
  };

  ScriptView view(std::span<const uint8_t>(script.data(), script.size()));

  size_t count = 0;
  for (const Instruction& ins : view.Instructions()) {
    (void)ins;
    ++count;                           // should never execute
  }
  EXPECT_EQ(count, 0u);                // iteration ends without yielding
}

TEST(ScriptViewInstructions, MalformedPayloadStopsAfterPriorGoodOps) {
  // Sequence:
  //  small push 0x01 <AA>   (valid)
  //  PUSHDATA1 0x05 <AA BB> (declares 5, but only 2 bytes present) → iteration should stop before yielding this one
  std::vector<uint8_t> script = {
      0x01, 0xAA,
      static_cast<uint8_t>(Op::PushData1), 0x05, 0xAA, 0xBB
  };

  ScriptView view(std::span<const uint8_t>(script.data(), script.size()));

  std::vector<Instruction> got;
  for (const Instruction& ins : view.Instructions()) {
    got.push_back(ins);
  }

  ASSERT_EQ(got.size(), 1u);           // only the first valid instruction
  EXPECT_EQ(static_cast<uint8_t>(got[0].opcode), 0x01);
  ASSERT_EQ(got[0].data.size(), 1u);
  EXPECT_EQ(got[0].data[0], 0xAA);
}

TEST(ScriptViewInstructions, IteratorOperatorArrowWorks) {
  // 0x01 <0x42> followed by a non-push (0xAC)
  std::vector<uint8_t> script = { 0x01, 0x42, 0xAC };
  ScriptView view(std::span<const uint8_t>(script.data(), script.size()));

  auto range = view.Instructions();
  auto it = range.begin();

  // First element via operator->()
  ASSERT_NE(it, range.end());
  EXPECT_EQ(static_cast<uint8_t>(it->opcode), 0x01);
  ASSERT_EQ(it->data.size(), 1u);
  EXPECT_EQ(it->data[0], 0x42);

  // Post-increment should advance to the non-push op
  it++;
  ASSERT_NE(it, range.end());
  EXPECT_EQ(static_cast<uint8_t>(it->opcode), 0xAC);
  EXPECT_EQ(it->data.size(), 0u);

  // Next increment should reach end
  ++it;
  EXPECT_EQ(it, range.end());
}

}  // namespace
}  // namespace hornet::protocol::script
