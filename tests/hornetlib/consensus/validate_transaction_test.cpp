
// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/consensus/validate_transaction.h"

#include "hornetlib/protocol/transaction.h"
#include "testutil/round_trip.h"

#include <gtest/gtest.h>

namespace hornet::consensus {
namespace {
  
using test::RoundTrip;

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

  auto result = ValidateTransaction(RoundTrip(tx));
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

  auto result = ValidateTransaction(tx2);
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

  auto result = ValidateTransaction(RoundTrip(tx));
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

  auto result = ValidateTransaction(RoundTrip(tx));
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

  auto result = ValidateTransaction(RoundTrip(tx));
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

  auto result = ValidateTransaction(RoundTrip(tx));
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

  auto result = ValidateTransaction(RoundTrip(tx));
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

  auto result = ValidateTransaction(RoundTrip(tx));
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

  auto result = ValidateTransaction(RoundTrip(tx));
  EXPECT_EQ(result, TransactionError::OversizedOutputValue);
}

}  // namespace
}  // namespace hornet::consensus