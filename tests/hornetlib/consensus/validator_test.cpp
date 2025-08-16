// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/consensus/validator.h"

#include "hornetlib/consensus/types.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/transaction.h"

#include <gtest/gtest.h>

namespace hornet::consensus {
namespace {

using hornet::protocol::Block;
using hornet::protocol::BlockHeader;
using hornet::protocol::Hash;
using hornet::protocol::OutPoint;
using hornet::protocol::Transaction;

// Serialize and deserialize to capture size info
template <typename T>
T RoundTrip(const T& object) {
  encoding::Writer writer;
  object.Serialize(writer);
  encoding::Reader reader(writer.Buffer());
  T obj2;
  obj2.Deserialize(reader);
  return obj2;
}

TEST(ValidatorTest, DetectsInvalidMerkleRoot) {
  Block block;

  Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.Input(0).previous_output.hash = Hash{};  // Coinbase
  tx.Input(0).previous_output.index = OutPoint::kNullIndex;
  tx.Input(0).sequence = 0xffffffff;
  tx.ResizeOutputs(1);
  tx.Output(0).value = 50'000'000;
  tx.SetPkScript(0, std::vector<uint8_t>{0xAA});
  tx.SetLockTime(0);
  block.AddTransaction(tx);

  // Manually override the Merkle root to something invalid
  BlockHeader header = block.Header();
  header.SetMerkleRoot(Hash{0x99});
  block.SetHeader(header);

  Validator validator;
  EXPECT_EQ(validator.ValidateBlockStructure(RoundTrip(block)), BlockError::BadMerkleRoot);
}

TEST(ValidatorTest, DetectsNonFirstCoinbase) {
  Block block;

  // Normal tx
  Transaction tx1;
  tx1.SetVersion(1);
  tx1.ResizeInputs(1);
  tx1.Input(0).previous_output.hash = Hash{0x01};
  tx1.Input(0).previous_output.index = 0;
  tx1.Input(0).sequence = 0xffffffff;
  tx1.ResizeOutputs(1);
  tx1.Output(0).value = 10'000;
  tx1.SetPkScript(0, std::vector<uint8_t>{0x01});
  tx1.SetLockTime(0);
  block.AddTransaction(tx1);

  // Improper coinbase in 2nd position
  Transaction tx2;
  tx2.SetVersion(1);
  tx2.ResizeInputs(1);
  tx2.Input(0).previous_output.hash = Hash{};
  tx2.Input(0).previous_output.index = OutPoint::kNullIndex;
  tx2.Input(0).sequence = 0xffffffff;
  tx2.ResizeOutputs(1);
  tx2.Output(0).value = 25'000;
  tx2.SetPkScript(0, std::vector<uint8_t>{0x02});
  tx2.SetLockTime(0);
  block.AddTransaction(tx2);

  // Set correct merkle root so that the only error is BadCoinBase
  BlockHeader header = block.Header();
  header.SetMerkleRoot(ComputeMerkleRoot(block));
  block.SetHeader(header);

  Validator validator;
  EXPECT_EQ(validator.ValidateBlockStructure(RoundTrip(block)), BlockError::BadCoinBase);
}

TEST(ValidatorTest, RejectsBlockWithExcessiveWeight) {
  Block block;
  const int empty_input_size = sizeof(protocol::OutPoint) + 5;
  const int input_count = 1'000'001 / empty_input_size + 1;

  Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(input_count);
  tx.ResizeOutputs(1);
  tx.Output(0).value = 50'000'000;
  tx.SetLockTime(0);
  block.AddTransaction(tx);

  BlockHeader header = block.Header();
  header.SetMerkleRoot(ComputeMerkleRoot(block));
  block.SetHeader(header);

  Validator validator;
  const auto block2 = RoundTrip(block);
  EXPECT_GT(block2.GetWeightUnits(), Parameters::kMaximumWeightUnits);
  EXPECT_EQ(validator.ValidateBlockStructure(block2), BlockError::BadSize);
}

TEST(ValidatorTest, RejectsBlockWithNoTransactions) {
  Block block;

  // Empty block
  BlockHeader header = block.Header();
  header.SetMerkleRoot(Hash{});
  block.SetHeader(header);

  Validator validator;
  EXPECT_EQ(validator.ValidateBlockStructure(block), BlockError::BadTransactionCount);
}

TEST(ValidatorTest, RejectsBlockWithInvalidTransaction) {
  Block block;

  protocol::Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.Input(0) = {.previous_output = protocol::OutPoint::Null()};
  tx.ResizeOutputs(1);
  tx.Output(0).value = -1;  // Invalid
  tx.SetLockTime(0);
  block.AddTransaction(tx);

  BlockHeader header;
  header.SetMerkleRoot(ComputeMerkleRoot(block));
  block.SetHeader(header);

  Validator validator;
  EXPECT_EQ(validator.ValidateBlockStructure(RoundTrip(block)), BlockError::BadTransaction);
}

TEST(ValidatorTest, AcceptsValidTransaction) {
  protocol::Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.Input(0).previous_output.hash = protocol::Hash{0x01};
  tx.Input(0).previous_output.index = 0;
  tx.Input(0).sequence = 0xffffffff;
  tx.SetSignatureScript(0, std::vector<uint8_t>{0xaa, 0xbb});
  tx.ResizeOutputs(1);
  tx.Output(0).value = 50'000;
  tx.SetPkScript(0, std::vector<uint8_t>{0xcc, 0xdd});
  tx.SetLockTime(0);

  Validator validator;
  auto result = validator.ValidateTransaction(RoundTrip(tx));
  EXPECT_EQ(result, TransactionError::None);
}

TEST(ValidatorTest, RejectsEmptyInputs) {
  protocol::Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.ResizeOutputs(1);
  tx.Output(0).value = 1;
  tx.SetPkScript(0, std::vector<uint8_t>{0xaa});
  tx.SetLockTime(0);

  auto tx2 = RoundTrip(tx);
  tx2.ResizeInputs(0);

  Validator validator;
  auto result = validator.ValidateTransaction(tx2);
  EXPECT_EQ(result, TransactionError::EmptyInputs);
}

TEST(ValidatorTest, RejectsDuplicateInputs) {
  protocol::Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(2);
  protocol::OutPoint dup_out{.hash = protocol::Hash{0x01}, .index = 0};
  for (int i = 0; i < 2; ++i) {
    tx.Input(i).previous_output = dup_out;
    tx.Input(i).sequence = 0xffffffff;
    tx.SetSignatureScript(i, std::vector<uint8_t>{0xaa});
  }
  tx.ResizeOutputs(1);
  tx.Output(0).value = 1;
  tx.SetPkScript(0, std::vector<uint8_t>{0xbb});
  tx.SetLockTime(0);

  Validator validator;
  auto result = validator.ValidateTransaction(RoundTrip(tx));
  EXPECT_EQ(result, TransactionError::DuplicatedInput);
}

TEST(ValidatorTest, RejectsOversizedTransactionNoWitness) {
  const int empty_input_size = sizeof(protocol::OutPoint) + 5;
  const int input_count = 1'000'001 / empty_input_size + 1;

  protocol::Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(input_count);
  tx.Input(0).previous_output.hash = protocol::Hash{0x01};
  tx.Input(0).previous_output.index = 0;
  tx.Input(0).sequence = 0xffffffff;
  tx.ResizeOutputs(1);
  tx.Output(0).value = 10;
  tx.SetPkScript(0, std::vector<uint8_t>{0xbb});
  tx.SetLockTime(0);

  Validator validator;
  auto result = validator.ValidateTransaction(RoundTrip(tx));
  EXPECT_EQ(result, TransactionError::OversizedByteCount);
}

TEST(ValidatorTest, RejectsCoinbaseWithInvalidScriptSize) {
  protocol::Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.Input(0).previous_output.hash = {};  // Coinbase
  tx.Input(0).previous_output.index = protocol::OutPoint::kNullIndex;
  tx.Input(0).sequence = 0xffffffff;
  tx.SetSignatureScript(0, std::vector<uint8_t>{0x01});  // Invalid: too short
  tx.ResizeOutputs(1);
  tx.Output(0).value = 50'000'000;
  tx.SetPkScript(0, std::vector<uint8_t>{0xaa});
  tx.SetLockTime(0);

  Validator validator;
  auto result = validator.ValidateTransaction(RoundTrip(tx));
  EXPECT_EQ(result, TransactionError::BadCoinBaseSignatureScriptSize);
}

TEST(ValidatorTest, RejectsEmptyOutputs) {
  protocol::Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.Input(0).previous_output.hash = protocol::Hash{0x01};
  tx.Input(0).previous_output.index = 0;
  tx.Input(0).sequence = 0xffffffff;
  tx.SetSignatureScript(0, std::vector<uint8_t>{0x11});
  tx.ResizeOutputs(0);  // Invalid
  tx.SetLockTime(0);

  Validator validator;
  auto result = validator.ValidateTransaction(RoundTrip(tx));
  EXPECT_EQ(result, TransactionError::EmptyOutputs);
}

TEST(ValidatorTest, RejectsNegativeOutputValue) {
  protocol::Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.Input(0).previous_output.hash = protocol::Hash{0x01};
  tx.Input(0).previous_output.index = 0;
  tx.Input(0).sequence = 0xffffffff;
  tx.SetSignatureScript(0, std::vector<uint8_t>{0x11});
  tx.ResizeOutputs(1);
  tx.Output(0).value = -1;  // Invalid
  tx.SetPkScript(0, std::vector<uint8_t>{0xaa});
  tx.SetLockTime(0);

  Validator validator;
  auto result = validator.ValidateTransaction(RoundTrip(tx));
  EXPECT_EQ(result, TransactionError::NegativeOutputValue);
}

TEST(ValidatorTest, RejectsTotalOutputOverflow) {
  protocol::Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.Input(0).previous_output.hash = protocol::Hash{0x01};
  tx.Input(0).previous_output.index = 0;
  tx.Input(0).sequence = 0xffffffff;
  tx.SetSignatureScript(0, std::vector<uint8_t>{0x11});
  tx.ResizeOutputs(2);
  tx.Output(0).value = 21 * 1'000'000 * 100'000'000ll;
  tx.Output(1).value = 1;
  tx.SetPkScript(0, std::vector<uint8_t>{0xaa});
  tx.SetPkScript(1, std::vector<uint8_t>{0xbb});
  tx.SetLockTime(0);

  Validator validator;
  auto result = validator.ValidateTransaction(RoundTrip(tx));
  EXPECT_EQ(result, TransactionError::OversizedTotalOutputValues);
}

TEST(ValidatorTest, RejectsOversizedOutputValue) {
  protocol::Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.Input(0).previous_output.hash = protocol::Hash{0x01};
  tx.Input(0).previous_output.index = 0;
  tx.Input(0).sequence = 0xffffffff;
  tx.SetSignatureScript(0, std::vector<uint8_t>{0x11});
  tx.ResizeOutputs(1);
  tx.Output(0).value = 21 * 1'000'000 * 100'000'000ll + 1;  // One satoshi over limit
  tx.SetPkScript(0, std::vector<uint8_t>{0xaa});
  tx.SetLockTime(0);

  Validator validator;
  auto result = validator.ValidateTransaction(RoundTrip(tx));
  EXPECT_EQ(result, TransactionError::OversizedOutputValue);
}

}  // namespace
}  // namespace hornet::consensus
