#include "hornetlib/consensus/rules/validate_spending.h"

#include <cstdint>
#include <utility>

#include "hornetlib/consensus/spending_test_harness.h"
#include "hornetlib/consensus/validate_chain_harness.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/script/writer.h"
#include "hornetlib/protocol/transaction.h"

#include "testutil/blockchain.h"

#include <gtest/gtest.h>

namespace hornet::consensus::rules {
namespace {

template <typename Callback>
Result EvaluateCandidateSpendingTransactions(const test::Blockchain& chain, const protocol::Block& block,
                                             Callback&& callback) {
  return test::WithCandidateSpendState(
      chain, block,
      [&](const HeaderAncestryView& ancestry, const std::shared_ptr<data::utxo::SpendJoiner>& joiner, int height) {
        return joiner->Join([&](const protocol::TransactionConstView& tx, std::span<const SpendRecord> spends) {
          return callback(tx, spends, ancestry, height);
        });
      });
}

class SequenceLocksHarness {
 public:
  using SequenceInput = std::pair<int, uint32_t>;

  protocol::Block MakeCandidateBlock(std::initializer_list<SequenceInput> inputs, int version = 2) const {
    protocol::Block block;
    const auto prev = kChain[kChain.Length() - 1];
    block.AddTransaction(MakeCoinbase(kChain.Length()));
    block.AddTransaction(MakeSpendTx(inputs, version));

    protocol::BlockHeader header;
    header.SetPreviousBlockHash(prev->Header().ComputeHash());
    header.SetTimestamp(prev->Header().GetTimestamp() + 600);
    header.SetCompactTarget(prev->Header().GetCompactTarget());
    block.SetHeader(header);
    test::FixMerkleRoot(block);
    return block;
  }

  template <typename Callback>
  Result ValidateCandidateTransactions(const protocol::Block& block, Callback&& callback) const {
    return EvaluateCandidateSpendingTransactions(kChain, block, std::forward<Callback>(callback));
  }

 private:
  static protocol::Transaction MakeCoinbase(int height) {
    protocol::Transaction tx;
    tx.SetVersion(1);
    tx.ResizeInputs(1);
    tx.ResizeOutputs(1);
    tx.Input(0).previous_output = protocol::OutPoint::Null();
    tx.Output(0).value = 50ll * 100'000'000;
    tx.SetSignatureScript(0, protocol::script::Writer{}.PushInt(height).PushInt(0).Release());
    return tx;
  }

  protocol::Transaction MakeSpendTx(std::initializer_list<SequenceInput> inputs, int version) const {
    protocol::Transaction tx;
    tx.SetVersion(version);
    tx.ResizeInputs(std::ssize(inputs));
    tx.ResizeOutputs(1);

    int64_t total = 0;
    int index = 0;
    for (auto [height, sequence] : inputs) {
      const auto funding = kChain[height]->Transaction(0);
      tx.Input(index).previous_output = {funding.GetHash(), 0};
      tx.Input(index).sequence = sequence;
      total += funding.Output(0).value;
      ++index;
    }
    tx.Output(0).value = total;
    return tx;
  }

  inline static const test::Blockchain kChain = test::Blockchain::Generate(140, 2, true);
};

TEST(ValidateSpendingTransactionTest, IgnoresVersion1Transactions) {
  const SequenceLocksHarness harness;
  const protocol::Block block = harness.MakeCandidateBlock({{120, 1u}}, 1);
  EXPECT_EQ(harness.ValidateCandidateTransactions(
                block, [](const protocol::TransactionConstView& tx, std::span<const SpendRecord> spends,
                          const HeaderAncestryView& ancestry,
                          int height) { return ValidateSequenceLocks({tx, spends, ancestry, height}); }),
            Result{});
}

TEST(ValidateSpendingTransactionTest, IgnoresDisabledInputs) {
  constexpr uint32_t kDisableMask = 1u << 31;

  const SequenceLocksHarness harness;
  const protocol::Block block = harness.MakeCandidateBlock({{120, kDisableMask | 1u}});
  EXPECT_EQ(harness.ValidateCandidateTransactions(
                block, [](const protocol::TransactionConstView& tx, std::span<const SpendRecord> spends,
                          const HeaderAncestryView& ancestry,
                          int height) { return ValidateSequenceLocks({tx, spends, ancestry, height}); }),
            Result{});
}

TEST(ValidateSpendingTransactionTest, RejectsNonFinalHeightLock) {
  const SequenceLocksHarness harness;
  const protocol::Block block = harness.MakeCandidateBlock({{120, 21u}});
  EXPECT_EQ(harness.ValidateCandidateTransactions(
                block, [](const protocol::TransactionConstView& tx, std::span<const SpendRecord> spends,
                          const HeaderAncestryView& ancestry,
                          int height) { return ValidateSequenceLocks({tx, spends, ancestry, height}); }),
            Error::Spending_NonFinalTransaction);
}

TEST(ValidateSpendingTransactionTest, AcceptsFinalHeightLock) {
  const SequenceLocksHarness harness;
  const protocol::Block block = harness.MakeCandidateBlock({{120, 20u}});
  EXPECT_EQ(harness.ValidateCandidateTransactions(
                block, [](const protocol::TransactionConstView& tx, std::span<const SpendRecord> spends,
                          const HeaderAncestryView& ancestry,
                          int height) { return ValidateSequenceLocks({tx, spends, ancestry, height}); }),
            Result{});
}

TEST(ValidateSpendingTransactionTest, RejectsNonFinalTimeLock) {
  constexpr uint32_t kTimeMask = 1u << 22;

  const SequenceLocksHarness harness;
  const protocol::Block block = harness.MakeCandidateBlock({{120, kTimeMask | 24u}});
  EXPECT_EQ(harness.ValidateCandidateTransactions(
                block, [](const protocol::TransactionConstView& tx, std::span<const SpendRecord> spends,
                          const HeaderAncestryView& ancestry,
                          int height) { return ValidateSequenceLocks({tx, spends, ancestry, height}); }),
            Error::Spending_NonFinalTransaction);
}

TEST(ValidateSpendingTransactionTest, AcceptsFinalTimeLock) {
  constexpr uint32_t kTimeMask = 1u << 22;

  const SequenceLocksHarness harness;
  const protocol::Block block = harness.MakeCandidateBlock({{120, kTimeMask | 23u}});
  EXPECT_EQ(harness.ValidateCandidateTransactions(
                block, [](const protocol::TransactionConstView& tx, std::span<const SpendRecord> spends,
                          const HeaderAncestryView& ancestry,
                          int height) { return ValidateSequenceLocks({tx, spends, ancestry, height}); }),
            Result{});
}

TEST(ValidateSpendingTransactionTest, UsesMostRestrictiveInput) {
  constexpr uint32_t kTimeMask = 1u << 22;

  const SequenceLocksHarness harness;
  const protocol::Block block = harness.MakeCandidateBlock({{120, 1u}, {120, kTimeMask | 24u}});
  EXPECT_EQ(harness.ValidateCandidateTransactions(
                block, [](const protocol::TransactionConstView& tx, std::span<const SpendRecord> spends,
                          const HeaderAncestryView& ancestry,
                          int height) { return ValidateSequenceLocks({tx, spends, ancestry, height}); }),
            Error::Spending_NonFinalTransaction);
}

TEST(ValidateSpendingTransactionTest, SkipsSequenceLocksBeforeActivation) {
  const SequenceLocksHarness harness;
  const protocol::Block block = harness.MakeCandidateBlock({{20, 121u}});
  EXPECT_EQ(harness.ValidateCandidateTransactions(
                block, [](const protocol::TransactionConstView& tx, std::span<const SpendRecord> spends,
                          const HeaderAncestryView& ancestry,
                          int height) { return ValidateSpendingTransaction(tx, spends, ancestry, height); }),
            Result{});
}

TEST(ValidateSpendingTransactionTest, RejectsOutputAmountsExceedInputAmounts) {
  test::ExpectValidationResult(
      [] {
        test::Blockchain data = test::LoadValidationPipelineChain();

        data.Append(data.Sample(2, true));
        data.Back()->Transaction(1).Output(0).value += 1;
        test::FixMerkleRoot(*data.Back());
        return data;
      },
      Error::Spending_OutputAmountsExceedInputAmounts);
}

}  // namespace
}  // namespace hornet::consensus::rules