#include "hornetlib/consensus/rules/validate_spending.h"

#include "hornetlib/consensus/spending_test_harness.h"
#include "hornetlib/consensus/stub_header_ancestry_view.h"
#include "hornetlib/consensus/validate_chain_harness.h"
#include "hornetlib/data/utxo/database_view.h"

#include "testutil/blockchain.h"
#include "testutil/transaction.h"

#include <gtest/gtest.h>

namespace hornet::consensus::rules {
namespace {

using hornet::test::NullSpendsChainOutputsView;
using hornet::test::StubHeaderAncestryView;

using hornet::test::MakeCoinbaseLikeTransaction;
using hornet::test::MakeSpendTransaction;

class ConfigurableChainOutputsView : public ChainOutputsView {
 public:
  ConfigurableChainOutputsView(bool created, bool unspent, bool unique)
      : created_(created), unspent_(unspent), unique_(unique) {}

  bool QueryPreviousOutputsCreated(const protocol::Block&) const override { return created_; }
  bool QueryPreviousOutputsUnspent(const protocol::Block&) const override { return unspent_; }
  bool QueryOutPointsUnique(const protocol::Block&) const override { return unique_; }
  std::optional<JoinedSpendRange> Spends(const protocol::Block&) const override { return std::nullopt; }

 private:
  bool created_;
  bool unspent_;
  bool unique_;
};

test::Blockchain MakeLockingScriptSpendChain(std::span<const uint8_t> locking_script,
                                             std::span<const uint8_t> signature_script = {}) {
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
  if (!signature_script.empty()) tx.SetSignatureScript(0, signature_script);
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
  ConfigurableChainOutputsView unspent{false, true, true};

  EXPECT_EQ(ValidateInputPrevoutsCreated({block, ancestry, unspent, 1}),
            Error::Spending_OutPointNotCreated);
}

TEST(ValidateSpendingBlockTest, ValidateInputPrevoutsUnspentMapsFalseToSpent) {
  protocol::Block block;
  StubHeaderAncestryView ancestry;
  ConfigurableChainOutputsView unspent{true, false, true};

  EXPECT_EQ(ValidateInputPrevoutsUnspent({block, ancestry, unspent, 1}),
            Error::Spending_OutPointSpent);
}

TEST(ValidateSpendingBlockTest, ValidateOutPointsUniqueMapsFalseToDuplicate) {
  protocol::Block block;
  StubHeaderAncestryView ancestry;
  ConfigurableChainOutputsView unspent{true, true, false};

  EXPECT_EQ(ValidateOutPointsUnique({block, ancestry, unspent, 1}),
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
  NullSpendsChainOutputsView unspent;

  EXPECT_EQ(ValidateBlockSubsidy({candidate, ancestry, unspent, 1}), Result{});
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

TEST(ValidateSpendingBlockTest, RejectsMalformedTrailingLockingScriptBytes) {
  const std::vector<uint8_t> script = {0x51, 0x4c};
  test::ExpectValidationResult([script] { return MakeLockingScriptSpendChain(script); }, Error::Spending_ScriptLocked);
}

std::vector<uint8_t> MakeOversizedScript() {
  constexpr int kPushCount = 20;
  constexpr int kPushSize = 520;

  std::array<uint8_t, kPushSize> payload;
  payload.fill(0x01);

  protocol::script::Writer writer;
  for (int i = 0; i < kPushCount; ++i) writer.PushData(payload);
  return writer.Release();
}

// An output whose locking script exceeds 10,000 bytes is not admitted, so spending it fails S02 before any script runs.
TEST(ValidateSpendingBlockTest, RejectsOversizedLockingScriptAsNotAdmitted) {
  const auto script = MakeOversizedScript();
  ASSERT_GT(script.size(), 10'000u);
  test::ExpectValidationResult([script] { return MakeLockingScriptSpendChain(script); },
                               Error::Spending_OutPointNotCreated);
}

TEST(ValidateSpendingBlockTest, RejectsOversizedSignatureScript) {
  const auto script = MakeOversizedScript();
  ASSERT_GT(script.size(), 10'000u);
  test::ExpectValidationResult([script] { return MakeLockingScriptSpendChain(std::vector<uint8_t>{0x51}, script); },
                               Error::Spending_OversizedScript);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BIP30 search domain
//
// Core's coin set never receives a provably unspendable output (a script beginning with OP_RETURN, or longer than
// 10,000 bytes), so such outputs do not count as existing unspent outputs in the BIP30 search. A byte-identical
// duplicate of an earlier transaction is therefore permitted once its spendable outputs have all been spent.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

using protocol::script::lang::Op;

int FindUnspentIndex(const test::Blockchain& chain, const protocol::OutPoint& prevout) {
  for (int i = 0; i < chain.UnspentSize(); ++i)
    if (chain.Unspent(i).prevout == prevout) return i;
  return -1;
}

// Builds a chain whose height-1 coinbase pays a spendable output and a second output locked by `second_script`, then
// spends the spendable output once mature, optionally giving that spend a second output locked by `spend_script`. A
// later coinbase copying the height-1 coinbase byte-for-byte is the pre-BIP34 duplicate-coinbase shape that BIP30
// governs.
test::Blockchain MakeSpentCoinbaseChain(std::span<const uint8_t> second_script,
                                        std::span<const uint8_t> spend_script = {}) {
  constexpr int kCoinbaseMaturity = 100;
  test::Blockchain chain;

  protocol::Block funding = chain.Sample(1);
  auto coinbase = funding.Transaction(0);
  coinbase.ResizeOutputs(2);
  coinbase.Output(1).value = 0;
  coinbase.SetPkScript(1, second_script);
  chain.AppendFixed(std::move(funding));

  for (int i = 0; i < kCoinbaseMaturity; ++i) chain.Append(chain.Sample(1));

  const protocol::OutPoint prevout{chain[1]->Transaction(0).GetHash(), 0};
  const int unspent_index = FindUnspentIndex(chain, prevout);
  EXPECT_GE(unspent_index, 0);

  protocol::Block spend = chain.Sample(1);
  protocol::Transaction tx = MakeSpendTransaction(prevout, chain.Unspent(unspent_index).amount);
  tx.Input(0).sequence = unspent_index;  // The test chain tracks spends by unspent index.
  if (!spend_script.empty()) {
    tx.ResizeOutputs(2);
    tx.Output(1).value = 0;
    tx.SetPkScript(1, spend_script);
  }
  spend.AddTransaction(tx);
  chain.AppendFixed(std::move(spend));

  return chain;
}

// Returns the BIP30 verdict for a candidate block whose coinbase duplicates the height-1 coinbase.
Result EvaluateDuplicateCoinbase(test::Blockchain chain) {
  protocol::Block candidate = chain.Sample(1);
  candidate.Transaction(0).CopyFrom(chain[1]->Transaction(0));
  test::FixMerkleRoot(candidate);
  return EvaluateCandidateSpendingBlock(
      chain, candidate, [](const BlockSpendContext& context) { return ValidateOutPointsUnique(context); });
}

TEST(ValidateSpendingBlockTest, AcceptsDuplicateOfSpentCoinbaseWithUnspentOpReturnOutput) {
  const std::vector<uint8_t> script = {+Op::Return, 0x04, 'H', 'N', 'E', 'T'};
  EXPECT_EQ(EvaluateDuplicateCoinbase(MakeSpentCoinbaseChain(script)), Result{});
}

TEST(ValidateSpendingBlockTest, AcceptsDuplicateOfSpentCoinbaseWithUnspentOversizedOutput) {
  const std::vector<uint8_t> script(10'001, +Op::PushConst1);
  EXPECT_EQ(EvaluateDuplicateCoinbase(MakeSpentCoinbaseChain(script)), Result{});
}

TEST(ValidateSpendingBlockTest, RejectsDuplicateOfSpentCoinbaseWithUnspentMaximumSizeOutput) {
  const std::vector<uint8_t> script(10'000, +Op::PushConst1);
  EXPECT_EQ(EvaluateDuplicateCoinbase(MakeSpentCoinbaseChain(script)), Error::Spending_OutPointDuplicate);
}

TEST(ValidateSpendingBlockTest, RejectsDuplicateOfSpentCoinbaseWithUnspentNonLeadingOpReturnOutput) {
  const std::vector<uint8_t> script = {+Op::PushConst1, +Op::Return};
  EXPECT_EQ(EvaluateDuplicateCoinbase(MakeSpentCoinbaseChain(script)), Error::Spending_OutPointDuplicate);
}

TEST(ValidateSpendingBlockTest, RejectsDuplicateOfSpentCoinbaseWithUnspentEmptyScriptOutput) {
  const std::vector<uint8_t> script;
  EXPECT_EQ(EvaluateDuplicateCoinbase(MakeSpentCoinbaseChain(script)), Error::Spending_OutPointDuplicate);
}

// A non-coinbase duplicate needs its inputs recreated by a duplicate of its funder, so every duplicate chain is rooted
// in a duplicate coinbase. Here the coinbase and its child both carry an OP_RETURN output, the child's spendable output
// is spent, and the candidate replays both. Core accepts the replay since neither OP_RETURN output is a coin.
TEST(ValidateSpendingBlockTest, AcceptsReplayedDuplicateChainWhoseOnlyUnspentOutputsAreOpReturn) {
  const std::vector<uint8_t> op_return = {+Op::Return};
  test::Blockchain chain = MakeSpentCoinbaseChain(op_return, op_return);

  const int funding_height = chain.Length() - 1;
  const protocol::OutPoint prevout{chain[funding_height]->Transaction(1).GetHash(), 0};
  const int unspent_index = FindUnspentIndex(chain, prevout);
  EXPECT_GE(unspent_index, 0);

  protocol::Block spend = chain.Sample(1);
  protocol::Transaction tx = MakeSpendTransaction(prevout, chain.Unspent(unspent_index).amount);
  tx.Input(0).sequence = unspent_index;
  spend.AddTransaction(tx);
  chain.AppendFixed(std::move(spend));

  protocol::Block candidate = chain.Sample(1);
  candidate.Transaction(0).CopyFrom(chain[1]->Transaction(0));
  candidate.AddTransaction(chain[funding_height]->Transaction(1));
  test::FixMerkleRoot(candidate);

  EXPECT_EQ(EvaluateCandidateSpendingBlock(
                chain, candidate, [](const BlockSpendContext& context) { return ValidateOutPointsUnique(context); }),
            Result{});
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BIP30 skip window
//
// Core skips the BIP30 search once the block at the BIP34 height is mainnet's, because BIP34 makes new duplicates
// impossible, and resumes it from height 1,983,702, the first height a pre-BIP34 coinbase could legally be repeated.
// On any chain with a different block at the BIP34 height the search runs at every height.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FixedHashAncestryView : public HeaderAncestryView {
 public:
  explicit FixedHashAncestryView(protocol::Hash hash) : hash_(hash) {}
  int Length() const override { return 1; }
  const protocol::Hash& HashAt(int) const override { return hash_; }
  uint32_t TimestampAt(int) const override { return 0; }
  std::vector<uint32_t> LastNTimestamps(int, int) const override { return {0}; }

 private:
  protocol::Hash hash_;
};

Result EvaluateOutPointsUniqueAt(int height, const protocol::Hash& bip34_block_hash) {
  const protocol::Block block;
  const FixedHashAncestryView ancestry{bip34_block_hash};
  const ConfigurableChainOutputsView duplicates{true, true, false};
  return ValidateOutPointsUnique({block, ancestry, duplicates, height});
}

constexpr auto kMainnetBIP34Hash = "000000000000024b89b42a942fe0d9fea3bb44ab7bd1b19115dd6a759c0808b8"_sha256;
constexpr auto kOtherBIP34Hash = "000000000000024b89b42a942fe0d9fea3bb44ab7bd1b19115dd6a759c0808b9"_sha256;

TEST(ValidateSpendingBlockTest, SkipsBIP30AboveMainnetBIP34Block) {
  EXPECT_EQ(EvaluateOutPointsUniqueAt(227'932, kMainnetBIP34Hash), Result{});
  EXPECT_EQ(EvaluateOutPointsUniqueAt(300'000, kMainnetBIP34Hash), Result{});
  EXPECT_EQ(EvaluateOutPointsUniqueAt(1'983'701, kMainnetBIP34Hash), Result{});
}

TEST(ValidateSpendingBlockTest, EnforcesBIP30AtAndBelowBIP34Height) {
  EXPECT_EQ(EvaluateOutPointsUniqueAt(227'931, kMainnetBIP34Hash), Error::Spending_OutPointDuplicate);
  EXPECT_EQ(EvaluateOutPointsUniqueAt(100'000, kMainnetBIP34Hash), Error::Spending_OutPointDuplicate);
}

TEST(ValidateSpendingBlockTest, EnforcesBIP30OnChainWithoutMainnetBIP34Block) {
  EXPECT_EQ(EvaluateOutPointsUniqueAt(300'000, kOtherBIP34Hash), Error::Spending_OutPointDuplicate);
}

TEST(ValidateSpendingBlockTest, ResumesBIP30From1983702) {
  EXPECT_EQ(EvaluateOutPointsUniqueAt(1'983'702, kMainnetBIP34Hash), Error::Spending_OutPointDuplicate);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Duplicated coinbase outputs
//
// When a BIP30-exempt duplicate coinbase is connected (blocks 91842 and 91880), Core overwrites the earlier coin, so
// one coin remains at the newer height, and a spend of it leaves nothing behind. Both such coins are unspent on
// mainnet.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Builds a chain whose height-1 and height-2 coinbases are byte-identical, as the UTXO layer must store them.
test::Blockchain MakeDuplicatedCoinbaseChain() {
  test::Blockchain chain;
  chain.Append(chain.Sample(1));

  protocol::Block duplicate = chain.Sample(1);
  duplicate.Transaction(0).CopyFrom(chain[1]->Transaction(0));
  chain.AppendFixed(std::move(duplicate));
  return chain;
}

TEST(ValidateSpendingBlockTest, DuplicatedCoinbaseOutputResolvesToNewestFunding) {
  const test::Blockchain chain = MakeDuplicatedCoinbaseChain();
  const protocol::OutPoint prevout{chain[1]->Transaction(0).GetHash(), 0};

  protocol::Block candidate;
  candidate.AddTransaction(MakeCoinbaseLikeTransaction());
  candidate.AddTransaction(MakeSpendTransaction(prevout, 1));
  test::FixMerkleRoot(candidate);

  EXPECT_EQ(EvaluateCandidateSpendingBlock(chain, candidate,
                                           [](const BlockSpendContext& context) {
                                             const auto spends = context.unspent.Spends(context.block);
                                             if (!spends) return Result{Error::Spending_OutPointNotCreated};
                                             for (const JoinedSpend spend : *spends)
                                               EXPECT_EQ(spend.spends[0].funding_height, 2);
                                             return Result{};
                                           }),
            Result{});
}

TEST(ValidateSpendingBlockTest, RejectsSecondSpendOfDuplicatedCoinbaseOutput) {
  test::Blockchain chain = MakeDuplicatedCoinbaseChain();
  const protocol::OutPoint prevout{chain[1]->Transaction(0).GetHash(), 0};
  const int unspent_index = FindUnspentIndex(chain, prevout);
  EXPECT_GE(unspent_index, 0);

  protocol::Block spend = chain.Sample(1);
  protocol::Transaction tx = MakeSpendTransaction(prevout, chain.Unspent(unspent_index).amount);
  tx.Input(0).sequence = unspent_index;
  spend.AddTransaction(tx);
  chain.AppendFixed(std::move(spend));

  protocol::Block candidate;
  candidate.AddTransaction(MakeCoinbaseLikeTransaction());
  candidate.AddTransaction(MakeSpendTransaction(prevout, 1));
  test::FixMerkleRoot(candidate);

  EXPECT_EQ(
      EvaluateCandidateSpendingBlock(
          chain, candidate, [](const BlockSpendContext& context) { return ValidateInputPrevoutsUnspent(context); }),
      Error::Spending_OutPointSpent);

  // With the coin gone, a further copy of the coinbase no longer collides under BIP30.
  EXPECT_EQ(EvaluateDuplicateCoinbase(chain), Result{});
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Genesis coinbase
//
// Core never connects the genesis block's transactions, so its coinbase output is neither spendable nor part of the
// BIP30 search domain.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TEST(ValidateSpendingBlockTest, RejectsGenesisCoinbaseSpendAsNotCreated) {
  const test::Blockchain chain = test::LoadValidationPipelineChain();
  const protocol::OutPoint genesis_prevout{protocol::Block::Genesis().Transaction(0).GetHash(), 0};

  protocol::Block candidate;
  candidate.AddTransaction(MakeCoinbaseLikeTransaction());
  candidate.AddTransaction(MakeSpendTransaction(genesis_prevout, 1));
  test::FixMerkleRoot(candidate);

  EXPECT_EQ(
      EvaluateCandidateSpendingBlock(
          chain, candidate, [](const BlockSpendContext& context) { return ValidateInputPrevoutsCreated(context); }),
      Error::Spending_OutPointNotCreated);
}

TEST(ValidateSpendingBlockTest, AcceptsGenesisCoinbaseDuplicateAsUnique) {
  const test::Blockchain chain = test::LoadValidationPipelineChain();

  protocol::Block genesis = protocol::Block::Genesis();
  protocol::Block candidate;
  candidate.AddTransaction(genesis.Transaction(0));
  test::FixMerkleRoot(candidate);

  EXPECT_EQ(EvaluateCandidateSpendingBlock(
                chain, candidate, [](const BlockSpendContext& context) { return ValidateOutPointsUnique(context); }),
            Result{});
}

}  // namespace
}  // namespace hornet::consensus::rules
