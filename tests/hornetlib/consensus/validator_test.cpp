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
  EXPECT_EQ(validator.ValidateBlockStructure(block), BlockError::BadMerkleRoot);
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
  EXPECT_EQ(validator.ValidateBlockStructure(block), BlockError::BadCoinBase);
}

}  // namespace
}  // namespace hornet::consensus
