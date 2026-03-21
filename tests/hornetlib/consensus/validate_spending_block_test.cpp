#include "hornetlib/consensus/rules/validate_spending.h"

#include "hornetlib/consensus/spending_test_harness.h"
#include "hornetlib/consensus/validate_chain_harness.h"
#include "hornetlib/data/utxo/database_view.h"

#include "testutil/blockchain.h"

#include <gtest/gtest.h>

namespace hornet::consensus::rules {
namespace {

template <typename Callback>
Result EvaluateCandidateSpendingBlock(const test::Blockchain& chain, const protocol::Block& block,
                                      Callback&& callback) {
  return test::WithCandidateSpendState(
      chain, block,
      [&](const HeaderAncestryView& ancestry, const std::shared_ptr<data::utxo::SpendJoiner>& joiner, int height) {
        const data::utxo::DatabaseView utxo{joiner};
        const BlockValidationContext validation{block, chain[height - 1]->Header(), ancestry, 0, utxo};
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

}  // namespace
}  // namespace hornet::consensus::rules