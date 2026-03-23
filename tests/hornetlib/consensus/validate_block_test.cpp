// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/consensus/validate_api.h"

#include "hornetlib/encoding/reader.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/consensus/merkle.h"
#include "hornetlib/consensus/rules/context.h"
#include "hornetlib/consensus/rules/validate_block_context.h"
#include "hornetlib/consensus/spending_test_harness.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/crypto/hash.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/script/writer.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/consensus/stub_header_ancestry_view.h"
#include "testutil/round_trip.h"

#include <array>
#include <vector>

#include <gtest/gtest.h>

namespace hornet::consensus {
namespace {

using hornet::protocol::Block;
using hornet::protocol::BlockHeader;
using hornet::protocol::Hash;
using hornet::protocol::OutPoint;
using hornet::protocol::Transaction;
using hornet::test::StubHeaderAncestryView;
using test::RoundTrip;

constexpr int kSegWitHeight = GetSoftForkActivationHeight(BIP::SegWit);
constexpr int kWitnessCommitmentOutputIndex = 1;

Transaction MakeCoinbaseTransaction() {
  Transaction coinbase;
  coinbase.SetVersion(1);
  coinbase.ResizeInputs(1);
  coinbase.Input(0).previous_output = OutPoint::Null();
  coinbase.Input(0).sequence = 0xffffffff;
  coinbase.SetSignatureScript(0, std::vector<uint8_t>{0x01, 0x01});
  coinbase.ResizeOutputs(2);
  coinbase.Output(0).value = 50'000'000;
  coinbase.SetPkScript(0, std::vector<uint8_t>{0x51});
  coinbase.Output(kWitnessCommitmentOutputIndex).value = 0;
  coinbase.SetPkScript(kWitnessCommitmentOutputIndex, std::vector<uint8_t>(38, 0));
  coinbase.SetLockTime(0);
  return coinbase;
}

Transaction MakeSpendTransaction(const bool witness = false) {
  Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.Input(0).previous_output = {Hash{0x01}, 0};
  tx.Input(0).sequence = 0xffffffff;
  tx.SetSignatureScript(0, std::vector<uint8_t>{0x51});
  tx.ResizeOutputs(1);
  tx.Output(0).value = 10'000;
  tx.SetPkScript(0, std::vector<uint8_t>{0x51});
  if (witness) {
    tx.ResizeWitnesses(1);
    tx.ResizeComponents(0, 1);
    tx.SetWitnessScript(0, 0, std::vector<uint8_t>{0x01});
  }
  tx.SetLockTime(0);
  return tx;
}

std::vector<uint8_t> MakeBytes(const int size, const uint8_t seed = 1) {
  std::vector<uint8_t> bytes(size);
  for (int i = 0; i < size; ++i) bytes[i] = static_cast<uint8_t>(seed + i);
  return bytes;
}

std::array<uint8_t, 32> MakeWitnessNonce(const uint8_t seed = 1) {
  std::array<uint8_t, 32> nonce{};
  for (int i = 0; i < std::ssize(nonce); ++i) nonce[i] = static_cast<uint8_t>(seed + i);
  return nonce;
}

void SetCoinbaseWitnessNonce(Transaction& coinbase, std::span<const uint8_t> nonce) {
  coinbase.ResizeWitnesses(1);
  coinbase.ResizeComponents(0, 1);
  coinbase.SetWitnessScript(0, 0, nonce);
}

void SetCoinbaseWitnessEmptyStack(Transaction& coinbase) {
  coinbase.ResizeWitnesses(1);
  coinbase.ResizeComponents(0, 0);
}

void SetWitnessEmptyStack(Transaction& tx) {
  tx.ResizeWitnesses(1);
  tx.ResizeComponents(0, 0);
}

Block MakeBlock(const Transaction& coinbase, const bool witness_tx = false) {
  Block block;
  block.AddTransaction(coinbase);
  block.AddTransaction(MakeSpendTransaction(witness_tx));
  test::FixMerkleRoot(block);
  return block;
}

std::vector<uint8_t> MakeWitnessCommitmentScript(std::span<const uint8_t> commitment) {
  static constexpr std::array<uint8_t, 6> kCommitmentPrefix = {0x6a, 0x24, 0xaa, 0x21, 0xa9, 0xed};

  std::vector<uint8_t> script(kCommitmentPrefix.begin(), kCommitmentPrefix.end());
  script.insert(script.end(), commitment.begin(), commitment.end());
  return script;
}

std::array<uint8_t, 32> ComputeWitnessCommitmentValue(const Block& block) {
  return crypto::DoubleSha256<64>(ComputeWitnessMerkleRoot(block).hash, block.Transaction(0).WitnessScript(0, 0));
}

void SetWitnessCommitmentOutput(Block& block, std::span<const uint8_t> commitment) {
  auto coinbase = block.Transaction(0);
  coinbase.Output(kWitnessCommitmentOutputIndex).value = 0;
  coinbase.SetPkScript(kWitnessCommitmentOutputIndex, MakeWitnessCommitmentScript(commitment));
  test::FixMerkleRoot(block);
}

rules::BlockEnvironmentContext MakeSegWitContext(const Block& block) {
  static const StubHeaderAncestryView ancestry;
  return {block, ancestry, kSegWitHeight};
}

rules::WitnessContext MakeSegWitWitnessContext(const Block& block) {
  return rules::MakeWitnessContext(MakeSegWitContext(block));
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

   EXPECT_EQ(ValidateStructural(RoundTrip(block)), Error::Structure_BadMerkleRoot);
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
  header.SetMerkleRoot(ComputeMerkleRoot(block).hash);
  block.SetHeader(header);

  EXPECT_EQ(ValidateStructural(RoundTrip(block)), Error::Structure_BadCoinBase);
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
  header.SetMerkleRoot(ComputeMerkleRoot(block).hash);
  block.SetHeader(header);

  const auto block2 = RoundTrip(block);
  EXPECT_GT(block2.GetWeightUnits(), 4'000'000);
  EXPECT_EQ(ValidateStructural(block2), Error::Structure_BadSize);
}

TEST(ValidatorTest, RejectsBlockWithNoTransactions) {
  Block block;

  // Empty block
  BlockHeader header = block.Header();
  header.SetMerkleRoot(Hash{});
  block.SetHeader(header);

  EXPECT_EQ(ValidateStructural(block), Error::Structure_BadTransactionCount);
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
  header.SetMerkleRoot(ComputeMerkleRoot(block).hash);
  block.SetHeader(header);

  EXPECT_EQ(ValidateStructural(RoundTrip(block)), Error::Transaction_NegativeOutputValue);
}

TEST(ValidatorTest, RejectsWitnessDataBeforeSegwitActivation) {
  Block block;

  Transaction coinbase;
  coinbase.SetVersion(1);
  coinbase.ResizeInputs(1);
  coinbase.Input(0).previous_output = OutPoint::Null();
  coinbase.Input(0).sequence = 0xffffffff;
  coinbase.SetSignatureScript(0, std::vector<uint8_t>{0x01, 0x01});
  coinbase.ResizeOutputs(1);
  coinbase.Output(0).value = 50'000'000;
  coinbase.SetPkScript(0, std::vector<uint8_t>{0x51});
  coinbase.SetLockTime(0);
  block.AddTransaction(coinbase);

  Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.Input(0).previous_output = {Hash{0x01}, 0};
  tx.Input(0).sequence = 0xffffffff;
  tx.SetSignatureScript(0, std::vector<uint8_t>{0x51});
  tx.ResizeOutputs(1);
  tx.Output(0).value = 10'000;
  tx.SetPkScript(0, std::vector<uint8_t>{0x51});
  tx.ResizeWitnesses(1);
  tx.ResizeComponents(0, 1);
  tx.SetWitnessScript(0, 0, std::vector<uint8_t>{0x01});
  tx.SetLockTime(0);
  block.AddTransaction(tx);

  const StubHeaderAncestryView ancestry;
  const rules::BlockEnvironmentContext context{block, ancestry, 100};
  EXPECT_EQ(rules::ValidateNoWitnessPreSegwit(context), Error::Structure_WitnessDataPreSegwit);
}

TEST(ValidatorTest, WitnessDataHasCommitmentAcceptsBlocksWithoutWitnessData) {
  const Block block = MakeBlock(MakeCoinbaseTransaction());

  EXPECT_EQ(rules::ValidateWitnessDataHasCommitment(MakeSegWitWitnessContext(block)), Result{});
}

TEST(ValidatorTest, WitnessDataHasCommitmentRejectsWitnessBlocksWithoutCommitment) {
  const Block block = MakeBlock(MakeCoinbaseTransaction(), true);

  EXPECT_EQ(rules::ValidateWitnessDataHasCommitment(MakeSegWitWitnessContext(block)),
            Error::Structure_WitnessDataWithoutCommitment);
}

TEST(ValidatorTest, WitnessDataHasCommitmentAcceptsBlocksWithCommitment) {
  Transaction coinbase = MakeCoinbaseTransaction();
  const auto nonce = MakeWitnessNonce();
  SetCoinbaseWitnessNonce(coinbase, nonce);

  Block block = MakeBlock(coinbase, true);
  SetWitnessCommitmentOutput(block, ComputeWitnessCommitmentValue(block));

  EXPECT_EQ(rules::ValidateWitnessDataHasCommitment(MakeSegWitWitnessContext(block)), Result{});
}

TEST(ValidatorTest, WitnessNonceAcceptsBlocksWithoutCommitment) {
  Transaction coinbase = MakeCoinbaseTransaction();
  SetCoinbaseWitnessNonce(coinbase, MakeWitnessNonce());
  const Block block = MakeBlock(coinbase, true);

  EXPECT_EQ(rules::ValidateWitnessNonce(MakeSegWitWitnessContext(block)), Result{});
}

TEST(ValidatorTest, WitnessNonceRejectsCoinbaseWitnessWithEmptyStack) {
  Transaction coinbase = MakeCoinbaseTransaction();
  SetCoinbaseWitnessEmptyStack(coinbase);

  Block block = MakeBlock(coinbase, true);
  SetWitnessCommitmentOutput(block, MakeBytes(32, 0x11));

  EXPECT_EQ(rules::ValidateWitnessNonce(MakeSegWitWitnessContext(block)), Error::Structure_BadWitnessNonce);
}

TEST(ValidatorTest, WitnessNonceRejectsCoinbaseWitnessWithShortNonce) {
  Transaction coinbase = MakeCoinbaseTransaction();
  SetCoinbaseWitnessNonce(coinbase, MakeBytes(31, 0x21));

  Block block = MakeBlock(coinbase, true);
  SetWitnessCommitmentOutput(block, MakeBytes(32, 0x31));

  EXPECT_EQ(rules::ValidateWitnessNonce(MakeSegWitWitnessContext(block)), Error::Structure_BadWitnessNonce);
}

TEST(ValidatorTest, WitnessNonceAcceptsSingle32ByteNonce) {
  Transaction coinbase = MakeCoinbaseTransaction();
  SetCoinbaseWitnessNonce(coinbase, MakeWitnessNonce());

  Block block = MakeBlock(coinbase, true);
  SetWitnessCommitmentOutput(block, MakeBytes(32, 0x41));

  EXPECT_EQ(rules::ValidateWitnessNonce(MakeSegWitWitnessContext(block)), Result{});
}

TEST(ValidatorTest, WitnessMerkleAcceptsBlocksWithoutCommitment) {
  Transaction coinbase = MakeCoinbaseTransaction();
  SetCoinbaseWitnessNonce(coinbase, MakeWitnessNonce());
  const Block block = MakeBlock(coinbase, true);

  EXPECT_EQ(rules::ValidateWitnessMerkle(MakeSegWitWitnessContext(block)), Result{});
}

TEST(ValidatorTest, WitnessMerkleAcceptsBlocksWithCommitmentAndNoCoinbaseWitness) {
  Block block = MakeBlock(MakeCoinbaseTransaction(), true);
  SetWitnessCommitmentOutput(block, MakeBytes(32, 0x51));

  EXPECT_EQ(rules::ValidateWitnessMerkle(MakeSegWitWitnessContext(block)), Result{});
}

TEST(ValidatorTest, WitnessMerkleAcceptsBlocksWithCommitmentAndEmptyCoinbaseWitnessStack) {
  Transaction coinbase = MakeCoinbaseTransaction();
  SetCoinbaseWitnessEmptyStack(coinbase);

  Block block = MakeBlock(coinbase, true);
  SetWitnessCommitmentOutput(block, MakeBytes(32, 0x61));

  EXPECT_EQ(rules::ValidateWitnessMerkle(MakeSegWitWitnessContext(block)), Result{});
}

TEST(ValidatorTest, WitnessMerkleRejectsMismatchedCommitment) {
  Transaction coinbase = MakeCoinbaseTransaction();
  SetCoinbaseWitnessNonce(coinbase, MakeWitnessNonce());

  Block block = MakeBlock(coinbase, true);
  auto commitment = ComputeWitnessCommitmentValue(block);
  commitment[0] ^= 0xff;
  SetWitnessCommitmentOutput(block, commitment);

  EXPECT_EQ(rules::ValidateWitnessMerkle(MakeSegWitWitnessContext(block)), Error::Structure_BadWitnessMerkle);
}

TEST(ValidatorTest, WitnessMerkleAcceptsMatchingCommitment) {
  Transaction coinbase = MakeCoinbaseTransaction();
  SetCoinbaseWitnessNonce(coinbase, MakeWitnessNonce());

  Block block = MakeBlock(coinbase, true);
  SetWitnessCommitmentOutput(block, ComputeWitnessCommitmentValue(block));

  EXPECT_EQ(rules::ValidateWitnessMerkle(MakeSegWitWitnessContext(block)), Result{});
}

TEST(ValidatorTest, WitnessCommitmentRejectsWitnessBlocksWithoutCommitment) {
  Transaction coinbase = MakeCoinbaseTransaction();
  SetCoinbaseWitnessNonce(coinbase, MakeWitnessNonce());
  const Block block = MakeBlock(coinbase, true);

  EXPECT_EQ(rules::ValidateWitnessCommitment(MakeSegWitContext(block)), Error::Structure_WitnessDataWithoutCommitment);
}

TEST(ValidatorTest, WitnessCommitmentRejectsBadWitnessNonce) {
  Transaction coinbase = MakeCoinbaseTransaction();
  SetCoinbaseWitnessEmptyStack(coinbase);

  Block block = MakeBlock(coinbase, true);
  SetWitnessCommitmentOutput(block, MakeBytes(32, 0x71));

  EXPECT_EQ(rules::ValidateWitnessCommitment(MakeSegWitContext(block)), Error::Structure_BadWitnessNonce);
}

TEST(ValidatorTest, WitnessCommitmentRejectsBadWitnessMerkle) {
  Transaction coinbase = MakeCoinbaseTransaction();
  SetCoinbaseWitnessNonce(coinbase, MakeWitnessNonce());

  Block block = MakeBlock(coinbase, true);
  auto commitment = ComputeWitnessCommitmentValue(block);
  commitment[1] ^= 0xff;
  SetWitnessCommitmentOutput(block, commitment);

  EXPECT_EQ(rules::ValidateWitnessCommitment(MakeSegWitContext(block)), Error::Structure_BadWitnessMerkle);
}

TEST(ValidatorTest, ContextualValidationAcceptsCommitmentPrefixWithoutWitnessData) {
  Transaction coinbase = MakeCoinbaseTransaction();
  coinbase.SetSignatureScript(0, protocol::script::Writer{}.PushInt(kSegWitHeight).PushInt(0).Release());

  Block block = MakeBlock(coinbase, false);

  auto block_coinbase = block.Transaction(0);
  block_coinbase.Output(kWitnessCommitmentOutputIndex).value = 0;
  block_coinbase.SetPkScript(kWitnessCommitmentOutputIndex,
                             std::vector<uint8_t>{0x6a, 0x24, 0xaa, 0x21, 0xa9, 0xed, 0x01});
  test::FixMerkleRoot(block);

  EXPECT_EQ(rules::ValidateContextual(MakeSegWitContext(block)), Result{});
}

TEST(ValidatorTest, WitnessCommitmentAcceptsValidWitnessCommitment) {
  Transaction coinbase = MakeCoinbaseTransaction();
  SetCoinbaseWitnessNonce(coinbase, MakeWitnessNonce());

  Block block = MakeBlock(coinbase, true);
  SetWitnessCommitmentOutput(block, ComputeWitnessCommitmentValue(block));

  EXPECT_EQ(rules::ValidateWitnessCommitment(MakeSegWitContext(block)), Result{});
}

TEST(ValidatorTest, RejectsBlockWithEmptyWitnessSerialization) {
  Transaction coinbase = MakeCoinbaseTransaction();
  coinbase.SetSignatureScript(0, protocol::script::Writer{}.PushInt(kSegWitHeight).PushInt(0).Release());
  SetCoinbaseWitnessNonce(coinbase, MakeWitnessNonce());

  Transaction tx = MakeSpendTransaction();
  SetWitnessEmptyStack(tx);

  Block block;
  block.AddTransaction(coinbase);
  block.AddTransaction(tx);
  SetWitnessCommitmentOutput(block, ComputeWitnessCommitmentValue(block));

  encoding::Writer writer;
  block.Serialize(writer);

  encoding::Reader reader(writer.Buffer());
  Block decoded;
  EXPECT_ANY_THROW(decoded.Deserialize(reader));
}

}  // namespace
}  // namespace hornet::consensus
