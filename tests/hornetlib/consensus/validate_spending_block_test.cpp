#include "hornetlib/consensus/rules/validate_spending.h"

#include "hornetlib/consensus/spending_test_harness.h"
#include "hornetlib/consensus/stub_header_ancestry_view.h"
#include "hornetlib/consensus/validate_chain_harness.h"
#include "hornetlib/data/utxo/database_view.h"

#include "testutil/blockchain.h"

#include <gtest/gtest.h>

namespace hornet::consensus::rules {
namespace {

using hornet::test::NullSpendsUnspentOutputsView;
using hornet::test::StubHeaderAncestryView;

protocol::Transaction MakeCoinbaseLikeTransaction(int64_t amount = 50'000'000) {
  protocol::Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.Input(0).previous_output = protocol::OutPoint::Null();
  tx.Input(0).sequence = 0xffffffff;
  tx.SetSignatureScript(0, std::vector<uint8_t>{0x02, 0x01});
  tx.ResizeOutputs(1);
  tx.Output(0).value = amount;
  tx.SetPkScript(0, std::vector<uint8_t>{0x51});
  tx.SetLockTime(0);
  return tx;
}

protocol::Transaction MakeSpendTransaction(const protocol::OutPoint& prevout, int64_t amount = 1) {
  protocol::Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.Input(0).previous_output = prevout;
  tx.Input(0).sequence = 0xffffffff;
  tx.SetSignatureScript(0, std::vector<uint8_t>{0x51});
  tx.ResizeOutputs(1);
  tx.Output(0).value = amount;
  tx.SetPkScript(0, std::vector<uint8_t>{0x51});
  tx.SetLockTime(0);
  return tx;
}

class ConfigurableUnspentOutputsView : public UnspentOutputsView {
 public:
  ConfigurableUnspentOutputsView(bool created, bool unspent, bool unique)
      : created_(created), unspent_(unspent), unique_(unique) {}

  bool QueryOutPointsCreated(const protocol::Block&) const override { return created_; }
  bool QueryOutPointsUnspent(const protocol::Block&) const override { return unspent_; }
  bool QueryOutPointsUnique(const protocol::Block&) const override { return unique_; }
  std::optional<JoinedSpendRange> Spends(const protocol::Block&) const override { return std::nullopt; }

 private:
  bool created_;
  bool unspent_;
  bool unique_;
};

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

template <typename Callback>
Result EvaluateCandidateSpendingRule(const test::Blockchain& chain, const protocol::Block& block,
                                     Callback&& callback) {
  data::HeaderTimechain headers;
  test::TempFolder datadir;
  data::utxo::Database db{datadir.Path()};
  model::HeaderContext header_context;
  data::HeaderTimechain::ConstIterator tip;

  for (int height = 0; height < chain.Length(); ++height) {
    tip = headers.Add(header_context = header_context.Extend(chain[height]->Header())).it;
    if (height > 0) db.Append(*chain[height], height);
  }

  const int height = chain.Length();
  const auto ancestry = headers.GetValidationView(tip);
  auto joiner = std::make_shared<data::utxo::SpendJoiner>(db, std::make_shared<const protocol::Block>(block), height);
  while (joiner->IsAdvanceReady()) joiner->Advance();

  const data::utxo::DatabaseView utxo{joiner};
  const BlockValidationContext validation{*joiner->GetBlock(), chain[height - 1]->Header(), *ancestry, 0, utxo};
  return callback(MakeBlockSpendContext(validation));
}

TEST(ValidateSpendingBlockTest, ValidateInputPrevoutsCreatedMapsFalseToNotCreated) {
  protocol::Block block;
  StubHeaderAncestryView ancestry;
  ConfigurableUnspentOutputsView unspent{false, true, true};

  EXPECT_EQ(ValidateInputPrevoutsCreated({block, ancestry, unspent, 1, 0}),
            Error::Spending_OutPointNotCreated);
}

TEST(ValidateSpendingBlockTest, ValidateInputPrevoutsUnspentMapsFalseToSpent) {
  protocol::Block block;
  StubHeaderAncestryView ancestry;
  ConfigurableUnspentOutputsView unspent{true, false, true};

  EXPECT_EQ(ValidateInputPrevoutsUnspent({block, ancestry, unspent, 1, 0}),
            Error::Spending_OutPointSpent);
}

TEST(ValidateSpendingBlockTest, ValidateOutPointsUniqueMapsFalseToDuplicate) {
  protocol::Block block;
  StubHeaderAncestryView ancestry;
  ConfigurableUnspentOutputsView unspent{true, true, false};

  EXPECT_EQ(ValidateOutPointsUnique({block, ancestry, unspent, 1, 0}),
            Error::Spending_OutPointDuplicate);
}

TEST(ValidateSpendingBlockTest, AcceptsFreshOutputsAsUnique) {
  const test::Blockchain chain = test::LoadValidationPipelineChain();
  const protocol::Block candidate = chain.Sample(2, true);

  EXPECT_EQ(EvaluateCandidateSpendingBlock(
                chain, candidate, [](const BlockSpendContext& context) { return ValidateOutPointsUnique(context); }),
            Result{});
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
      Error::Spending_OutPointDuplicate);
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
      Error::Spending_OutPointSpent);
}

TEST(ValidateSpendingBlockTest, RejectsMissingOutPointAsNotCreated) {
  const test::Blockchain chain = test::LoadValidationPipelineChain();

  protocol::Block candidate = chain.Sample(2, true);
  auto tx = candidate.Transaction(1);
  tx.Input(0).previous_output.hash[0] ^= 0x42;
  test::FixMerkleRoot(candidate);

  EXPECT_EQ(EvaluateCandidateSpendingBlock(
                chain, candidate, [](const BlockSpendContext& context) { return ValidateInputPrevoutsCreated(context); }),
            Error::Spending_OutPointNotCreated);
}

TEST(ValidateSpendingBlockTest, AcceptsLocalPrecedingOutPointAsCreatedAndUnspent) {
  const test::Blockchain chain = test::LoadValidationPipelineChain();

  protocol::Block candidate;
  candidate.AddTransaction(MakeCoinbaseLikeTransaction());
  candidate.AddTransaction(MakeSpendTransaction(chain.Unspent(0).prevout, 2));
  candidate.AddTransaction(MakeSpendTransaction({candidate.Transaction(1).GetHash(), 0}, 1));
  test::FixMerkleRoot(candidate);

  EXPECT_EQ(EvaluateCandidateSpendingRule(
                chain, candidate, [](const BlockSpendContext& context) { return ValidateInputPrevoutsCreated(context); }),
            Result{});
  EXPECT_EQ(EvaluateCandidateSpendingRule(
                chain, candidate, [](const BlockSpendContext& context) { return ValidateInputPrevoutsUnspent(context); }),
            Result{});
}

TEST(ValidateSpendingBlockTest, RejectsForwardReferenceAsNotCreated) {
  const test::Blockchain chain = test::LoadValidationPipelineChain();

  protocol::Block candidate;
  candidate.AddTransaction(MakeCoinbaseLikeTransaction());

  protocol::Transaction late = MakeSpendTransaction(chain.Unspent(0).prevout, 2);
  protocol::Transaction early = MakeSpendTransaction({late.GetHash(), 0}, 1);

  candidate.AddTransaction(early);
  candidate.AddTransaction(late);
  test::FixMerkleRoot(candidate);

  EXPECT_EQ(EvaluateCandidateSpendingRule(
                chain, candidate, [](const BlockSpendContext& context) { return ValidateInputPrevoutsCreated(context); }),
            Error::Spending_OutPointNotCreated);
}

TEST(ValidateSpendingBlockTest, RejectsLocalDoubleSpendAsSpentButNotMissing) {
  const test::Blockchain chain = test::LoadValidationPipelineChain();

  protocol::Block candidate;
  candidate.AddTransaction(MakeCoinbaseLikeTransaction());
  candidate.AddTransaction(MakeSpendTransaction(chain.Unspent(0).prevout, 2));

  const protocol::OutPoint local_prevout{candidate.Transaction(1).GetHash(), 0};
  candidate.AddTransaction(MakeSpendTransaction(local_prevout, 1));
  candidate.AddTransaction(MakeSpendTransaction(local_prevout, 1));
  test::FixMerkleRoot(candidate);

  EXPECT_EQ(EvaluateCandidateSpendingRule(
                chain, candidate, [](const BlockSpendContext& context) { return ValidateInputPrevoutsCreated(context); }),
            Result{});
  EXPECT_EQ(EvaluateCandidateSpendingRule(
                chain, candidate, [](const BlockSpendContext& context) { return ValidateInputPrevoutsUnspent(context); }),
            Error::Spending_OutPointSpent);
}

TEST(ValidateSpendingBlockTest, RejectsNonAdjacentDuplicatePrevoutsAsSpentButCreated) {
  const test::Blockchain chain = test::LoadValidationPipelineChain();

  const auto duplicate_prevout = chain.Unspent(0).prevout;
  const auto distinct_prevout = chain.Unspent(1).prevout;

  protocol::Block candidate;
  candidate.AddTransaction(MakeCoinbaseLikeTransaction());
  candidate.AddTransaction(MakeSpendTransaction(duplicate_prevout, 2));
  candidate.AddTransaction(MakeSpendTransaction(distinct_prevout, 2));
  candidate.AddTransaction(MakeSpendTransaction(duplicate_prevout, 2));
  test::FixMerkleRoot(candidate);

  EXPECT_EQ(EvaluateCandidateSpendingRule(
                chain, candidate, [](const BlockSpendContext& context) { return ValidateInputPrevoutsCreated(context); }),
            Result{});
  EXPECT_EQ(EvaluateCandidateSpendingRule(
                chain, candidate, [](const BlockSpendContext& context) { return ValidateInputPrevoutsUnspent(context); }),
            Error::Spending_OutPointSpent);
}

}  // namespace
}  // namespace hornet::consensus::rules