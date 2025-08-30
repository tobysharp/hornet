// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/protocol/script/parser.h"

#include <gtest/gtest.h>

namespace hornet::protocol::script {
namespace {

using lang::Op;

void ExpectNext(Parser& p, Op expected_opcode, std::initializer_list<uint8_t> expected_data) {
  auto ins = p.Next();
  ASSERT_TRUE(ins.has_value()) << "expected another instruction";
  EXPECT_EQ(ins->opcode, expected_opcode);
  ASSERT_EQ(ins->data.size(), expected_data.size());
  size_t i = 0;
  for (uint8_t b : expected_data) {
    EXPECT_EQ(ins->data[i++], b);
  }
}

void ExpectNextRaw(Parser& p, uint8_t expected_opcode_byte, std::initializer_list<uint8_t> expected_data) {
  auto ins = p.Next();
  ASSERT_TRUE(ins.has_value()) << "expected another instruction";
  EXPECT_EQ(static_cast<uint8_t>(ins->opcode), expected_opcode_byte);
  ASSERT_EQ(ins->data.size(), expected_data.size());
  size_t i = 0;
  for (uint8_t b : expected_data) {
    EXPECT_EQ(ins->data[i++], b);
  }
}

TEST(ScriptParserTest, ParsesMixedSequence) {
  // Script bytes:
  //   0x02 <AA BB>           (small push of 2 bytes)
  //   0xAC                   (CHECKSIG, as an example non-push opcode)
  //   OP_PUSHDATA1 0x03 <DE AD BE>
  //   OP_PUSHDATA2 0x01 0x00 <FF>
  std::vector<uint8_t> script = {
      0x02, 0xAA, 0xBB,
      0xAC,
      static_cast<uint8_t>(Op::PushData1), 0x03, 0xDE, 0xAD, 0xBE,
      static_cast<uint8_t>(Op::PushData2), 0x01, 0x00, 0xFF
  };

  Parser p{std::span<const uint8_t>(script.data(), script.size())};

  // Small push (opcode byte 0x02, two data bytes)
  ExpectNextRaw(p, 0x02, {0xAA, 0xBB});

  // Non-push opcode (0xAC). Replace 0xAC with Op::CheckSig in your enum if defined.
  {
    auto ins = p.Next();
    ASSERT_TRUE(ins.has_value());
    EXPECT_EQ(static_cast<uint8_t>(ins->opcode), 0xAC); // or EXPECT_EQ(ins->opcode, Op::CheckSig);
    EXPECT_EQ(ins->data.size(), 0u);
  }

  // PUSHDATA1 of length 3
  ExpectNext(p, Op::PushData1, {0xDE, 0xAD, 0xBE});

  // PUSHDATA2 of length 1 -> single 0xFF
  ExpectNext(p, Op::PushData2, {0xFF});

  // EOF
  EXPECT_FALSE(p.Next().has_value());
}

TEST(ScriptParserTest, EmptyScriptYieldsEofImmediately) {
  std::vector<uint8_t> script; // empty
  Parser p{std::span<const uint8_t>(script.data(), script.size())};
  EXPECT_FALSE(p.Next().has_value());
}

TEST(ScriptParserTest, MalformedHeaderTerminates) {
  // OP_PUSHDATA2 but only one length byte present (needs 2)
  std::vector<uint8_t> script = {
      static_cast<uint8_t>(Op::PushData2), 0x01
  };
  Parser p{std::span<const uint8_t>(script.data(), script.size())};

  // First Next() should detect malformed header, return nullopt and set EOF.
  auto ins = p.Next();
  EXPECT_FALSE(ins.has_value());
}

TEST(ScriptParserTest, MalformedPayloadTerminates) {
  // OP_PUSHDATA1 length=5 but only 2 data bytes present
  std::vector<uint8_t> script = {
      static_cast<uint8_t>(Op::PushData1), 0x05, 0xAA, 0xBB
  };
  Parser p{std::span<const uint8_t>(script.data(), script.size())};

  auto ins = p.Next();
  EXPECT_FALSE(ins.has_value());
}

TEST(ScriptParserTest, PushData4ZeroLengthAllowed) {
  // OP_PUSHDATA4 with length=0 (legal; pushes empty)
  std::vector<uint8_t> script = {
      static_cast<uint8_t>(Op::PushData4), 0x00, 0x00, 0x00, 0x00
  };
  Parser p{std::span<const uint8_t>(script.data(), script.size())};

  auto ins = p.Next();
  ASSERT_TRUE(ins.has_value());
  EXPECT_EQ(ins->opcode, Op::PushData4);
  EXPECT_EQ(ins->data.size(), 0u);

}

TEST(ScriptParserTest, PeekDoesNotAdvance) {
  std::vector<uint8_t> script = { 0x01, 0xAA, 0xAC };
  Parser p{std::span<const uint8_t>(script.data(), script.size())};

  // Peek small push
  if constexpr (requires(Parser q) { q.Peek(); }) {
    auto first = p.Peek();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(static_cast<uint8_t>(*first), 0x01);

    // Next should return the same instruction, then advance.
    auto n = p.Next();
    ASSERT_TRUE(n.has_value());
    EXPECT_EQ(static_cast<uint8_t>(n->opcode), 0x01);
    EXPECT_EQ(n->data.size(), 1u);
    EXPECT_EQ(n->data[0], 0xAA);

    // Now the next is 0xAC
    auto m = p.Next();
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(static_cast<uint8_t>(m->opcode), 0xAC);
    EXPECT_EQ(m->data.size(), 0u);
  }
}

}  // namepsace
}  // namespace hornet::protocol::script
