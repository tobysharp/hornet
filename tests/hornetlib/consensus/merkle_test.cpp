// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/consensus/merkle.h"

#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/protocol/txid.h"

#include <gtest/gtest.h>

namespace hornet::consensus {
namespace {

using namespace hornet::protocol;
using HashPair = std::array<Hash, 2>;

TEST(MerkleRootTest, OneTransactionIsItsOwnRoot) {
  Block block;
  Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.Input(0).previous_output.hash = Hash{0x01};
  tx.Input(0).previous_output.index = 0;
  tx.Input(0).sequence = 0xffffffff;
  tx.ResizeOutputs(1);
  tx.Output(0).value = 50'000'000;
  tx.SetPkScript(0, std::vector<uint8_t>{0xaa, 0xbb, 0xcc});
  tx.SetLockTime(0);
  block.AddTransaction(tx);

  const Hash expected_root = tx.GetHash();
  const Hash computed_root = ComputeMerkleRoot(block);
  EXPECT_EQ(computed_root, expected_root);
}

TEST(MerkleRootTest, TwoTransactions) {
  Block block;
  Transaction tx1;
  tx1.SetVersion(1);
  tx1.ResizeInputs(1);
  tx1.Input(0).previous_output.hash = Hash{0x11};
  tx1.Input(0).previous_output.index = 0;
  tx1.Input(0).sequence = 0xffffffff;
  tx1.ResizeOutputs(1);
  tx1.Output(0).value = 1'000'000;
  tx1.SetPkScript(0, std::vector<uint8_t>{0x11});
  tx1.SetLockTime(0);
  block.AddTransaction(tx1);

  Transaction tx2;
  tx2.SetVersion(1);
  tx2.ResizeInputs(1);
  tx2.Input(0).previous_output.hash = Hash{0x22};
  tx2.Input(0).previous_output.index = 1;
  tx2.Input(0).sequence = 0xfffffffe;
  tx2.ResizeOutputs(1);
  tx2.Output(0).value = 2'000'000;
  tx2.SetPkScript(0, std::vector<uint8_t>{0x22});
  tx2.SetLockTime(0);
  block.AddTransaction(tx2);

  Hash expected_root = crypto::DoubleSha256(HashPair{tx1.GetHash(), tx2.GetHash()});
  Hash computed_root = ComputeMerkleRoot(block);
  EXPECT_EQ(computed_root, expected_root);
}

TEST(MerkleRootTest, ThreeTransactionsPadLast) {
  Block block;
  std::vector<Hash> hashes;

  for (int i = 0; i < 3; ++i) {
    Transaction tx;
    tx.SetVersion(1);
    tx.ResizeInputs(1);
    tx.Input(0).previous_output.hash = Hash{static_cast<uint8_t>(0x30 + i)};
    tx.Input(0).previous_output.index = 0;
    tx.Input(0).sequence = 0xffffffff;
    tx.ResizeOutputs(1);
    tx.Output(0).value = 1000 * (i + 1);
    tx.SetPkScript(0, std::vector<uint8_t>{static_cast<uint8_t>(0xa0 + i)});
    tx.SetLockTime(0);
    block.AddTransaction(tx);
    hashes.push_back(tx.GetHash());
  }

  // Tree:
  // L1: h0, h1, h2, h2
  // L2: hash(h0,h1), hash(h2,h2)
  // Root = hash(hash(h0,h1), hash(h2,h2))
  Hash l1a = crypto::DoubleSha256(HashPair{hashes[0], hashes[1]});
  Hash l1b = crypto::DoubleSha256(HashPair{hashes[2], hashes[2]});
  Hash expected_root = crypto::DoubleSha256(HashPair{l1a, l1b});
  Hash computed_root = ComputeMerkleRoot(block);
  EXPECT_EQ(computed_root, expected_root);
}

}  // namespace
}  // namespace hornet::consensus
