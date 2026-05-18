#include "hornetlib/consensus/rules/validate_spending.h"

#include "hornetlib/consensus/spending_test_harness.h"
#include "hornetlib/consensus/stub_header_ancestry_view.h"
#include "hornetlib/consensus/validate_chain_harness.h"
#include "hornetlib/data/utxo/database_view.h"
#include "hornetlib/protocol/script/writer.h"
#include "hornetlib/protocol/transaction.h"

#include "testutil/blockchain.h"

#include <array>

#include <gtest/gtest.h>

namespace hornet::consensus::rules {
namespace {

using hornet::test::NullSpendsUnspentOutputsView;
using hornet::test::StubHeaderAncestryView;

test::Blockchain MakeLockingScriptSpendChain(std::span<const uint8_t> locking_script) {
  test::Blockchain chain = test::LoadValidationPipelineChain();

  protocol::Block funding = chain.Sample(2, true, 1, 1);
  funding.Transaction(0).SetPkScript(0, std::vector<uint8_t>{0x51});
  funding.Transaction(1).SetPkScript(0, locking_script);

  const protocol::OutPoint prevout{funding.Transaction(1).GetHash(), 0};
  const int64_t amount = funding.Transaction(1).Output(0).value;

  test::FixMerkleRoot(funding);
  chain.AppendFixed(funding);

  protocol::Block spend = chain.Sample(1);
  spend.Transaction(0).SetPkScript(0, std::vector<uint8_t>{0x51});

  protocol::Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.ResizeOutputs(1);
  tx.Input(0).previous_output = prevout;
  tx.Input(0).sequence = chain.UnspentSize() - 1;
  tx.Output(0).value = amount;
  tx.SetPkScript(0, std::vector<uint8_t>{0x51});
  tx.SetLockTime(0);

  spend.AddTransaction(tx);
  test::FixMerkleRoot(spend);
  chain.AppendFixed(spend);

  return chain;
}

template <typename Callback>
Result EvaluateCandidateSpendingBlock(const test::Blockchain& chain, const protocol::Block& block,
                                      Callback&& callback) {
  return test::WithCandidateSpendState(
      chain, block,
      [&](const HeaderAncestryView& ancestry, const std::shared_ptr<data::utxo::SpendJoiner>& joiner, int height) {
        const data::utxo::DatabaseView utxo{joiner};
        const BlockValidationContext validation{*joiner->GetBlock(), chain[height - 1]->Header(), ancestry, 0, utxo};
        return callback(MakeBlockSpendContext(validation));
      });
}

TEST(ValidateSpendingBlockTest, AcceptsCoinbaseAtBlockReward) {
  const test::Blockchain chain = test::LoadValidationPipelineChain();
  const protocol::Block candidate = chain.Sample(2, true);

  EXPECT_EQ(EvaluateCandidateSpendingBlock(
                chain, candidate, [](const BlockSpendContext& context) { return ValidateBlockSubsidy(context); }),
            Result{});
}

TEST(ValidateSpendingBlockTest, RejectsCoinbaseAboveBlockReward) {
  const test::Blockchain chain = test::LoadValidationPipelineChain();

  protocol::Block candidate = chain.Sample(2, true);
  candidate.Transaction(0).Output(0).value += 1;
  test::FixMerkleRoot(candidate);

  EXPECT_EQ(EvaluateCandidateSpendingBlock(
                chain, candidate, [](const BlockSpendContext& context) { return ValidateBlockSubsidy(context); }),
            Error::Spending_CoinbaseAmountExceedsBlockReward);
}

TEST(ValidateSpendingBlockTest, ValidateBlockSubsidySucceedsWhenJoinedSpendsUnavailable) {
  const test::Blockchain chain = test::LoadValidationPipelineChain();

  protocol::Block candidate = chain.Sample(2, true);
  candidate.Transaction(0).Output(0).value += 1;
  test::FixMerkleRoot(candidate);

  StubHeaderAncestryView ancestry;
  NullSpendsUnspentOutputsView unspent;

  EXPECT_EQ(ValidateBlockSubsidy({candidate, ancestry, unspent, 1, 0}), Result{});
}

TEST(ValidateSpendingBlockTest, RejectsDuplicateOutPoint) {
  test::ExpectValidationResult(
      [] {
        test::Blockchain data;
        for (int height = 1; height < 4; ++height) data.Append(data.Sample(1'000, true));

        data[3]->Transaction(0).CopyFrom(data[1]->Transaction(0));
        test::FixMerkleRoot(*data[3]);
        return data;
      },
      Error::Spending_DuplicateOutPoint);
}

TEST(ValidateSpendingBlockTest, AcceptsFullySpentDuplicateOutPoint) {
  test::ExpectValidationResult([] {
    test::Blockchain data = test::LoadValidationPipelineChain();

    data.Append(data.Sample(1'000, true));
    data.Back()->Transaction(0).CopyFrom(data[1]->Transaction(0));
    test::FixMerkleRoot(*data.Back());
    return data;
  });
}

TEST(ValidateSpendingBlockTest, RejectsCoinbaseAmountExceedsBlockReward) {
  test::ExpectValidationResult(
      [] {
        test::Blockchain data = test::LoadValidationPipelineChain();

        protocol::Block block = data.Sample(2, true);
        block.Transaction(0).Output(0).value += 1;
        data.AppendFixed(std::move(block));
        return data;
      },
      Error::Spending_CoinbaseAmountExceedsBlockReward);
}

TEST(ValidateSpendingBlockTest, RejectsBlockInternalDoubleSpendOfExistingUtxo) {
  test::ExpectValidationResult(
      [] {
        test::Blockchain data = test::LoadValidationPipelineChain();

        protocol::Block block = data.Sample(2, true, 1, 1);
        data.AppendFixed(block);

        auto final_block = data.Back();
        const auto spent_prevout = final_block->Transaction(1).Input(0).previous_output;
        const auto spent_index = final_block->Transaction(1).Input(0).sequence;
        const auto spent_amount = data.Unspent(final_block->Transaction(1).Input(0).sequence).amount;

        protocol::Transaction duplicate;
        duplicate.SetVersion(1);
        duplicate.ResizeInputs(1);
        duplicate.Input(0).previous_output = spent_prevout;
        duplicate.Input(0).sequence = spent_index;
        duplicate.SetSignatureScript(0, std::vector<uint8_t>{0x51});
        duplicate.ResizeOutputs(1);
        duplicate.Output(0).value = spent_amount;
        duplicate.SetPkScript(0, std::vector<uint8_t>{0x51});
        duplicate.SetLockTime(0);

        final_block->AddTransaction(duplicate);
        test::FixMerkleRoot(*final_block);
        return data;
      },
      Error::Spending_PrevoutNotUnspent);
}

TEST(ValidateSpendingBlockTest, RejectsOversizedLockingScript) {
  constexpr int kPushCount = 20;
  constexpr int kPushSize = 520;

  std::array<uint8_t, kPushSize> payload;
  payload.fill(0x01);

  protocol::script::Writer writer;
  for (int i = 0; i < kPushCount; ++i) writer.PushData(payload);
  const auto script = writer.Release();

  ASSERT_GT(script.size(), 10'000u);
  test::ExpectValidationResult([script] { return MakeLockingScriptSpendChain(script); }, Error::Spending_OversizedScript);
}

}  // namespace
}  // namespace hornet::consensus::rules