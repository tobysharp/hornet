#include "hornetlib/consensus/rules/validate.h"

#include <limits>
#include <cstdint>
#include <utility>

#include "hornetlib/consensus/spending_test_harness.h"
#include "hornetlib/consensus/stub_header_ancestry_view.h"
#include "hornetlib/consensus/validate_chain_harness.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/script/writer.h"
#include "hornetlib/protocol/transaction.h"

#include "testutil/blockchain.h"

#include <gtest/gtest.h>

namespace hornet::consensus::rules {
namespace {

protocol::Transaction MakeCoinbaseTx(int64_t value) {
  protocol::Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.ResizeOutputs(1);
  tx.Input(0).previous_output = protocol::OutPoint::Null();
  tx.Input(0).sequence = 0xffffffff;
  tx.Output(0).value = value;
  tx.SetSignatureScript(0, protocol::script::Writer{}.PushInt(1).PushInt(0).Release());
  tx.SetPkScript(0, std::vector<uint8_t>{0x51});
  return tx;
}

protocol::Transaction MakeMaxMoneySpendTx(int index, int64_t output_value = 21'000'000ll * 100'000'000ll) {
  protocol::Transaction tx;
  tx.SetVersion(2);
  tx.ResizeInputs(1);
  tx.ResizeOutputs(1);
  tx.Input(0).previous_output = {protocol::Hash{static_cast<uint8_t>(index + 1)}, 0};
  tx.Input(0).sequence = 0xffffffff;
  tx.Output(0).value = output_value;
  tx.SetSignatureScript(0, std::vector<uint8_t>{0x51});
  tx.SetPkScript(0, std::vector<uint8_t>{0x51});
  return tx;
}

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
  const protocol::Block block = harness.MakeCandidateBlock({{120, 1u}, {121, kTimeMask | 24u}});
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
                          int height) { return ValidateSpendingTransaction({tx, spends, ancestry, height}); }),
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

TEST(ValidateSpendingTransactionTest, AcceptsCoreValidBlockDespiteAccumulatorOverflowRisk) {
  constexpr int64_t kMaxMoney = 21'000'000ll * 100'000'000ll;
  constexpr int64_t kSubsidy = 50ll * 100'000'000ll;
  constexpr int kHeight = 1;
  constexpr int kSpendCount = 4'500;

  protocol::Block block;
  block.AddTransaction(MakeCoinbaseTx(kSubsidy));

  test::StaticUnspentOutputsView unspent;
  for (int i = 0; i < kSpendCount; ++i) {
    auto tx = MakeMaxMoneySpendTx(i);
    block.AddTransaction(tx);
    unspent.Add(std::move(tx), {{.funding_height = 0,
                                 .funding_flags = 0u,
                                 .amount = kMaxMoney,
                                 .pubkey_script = {},
                                 .spend_input_index = 0}});
  }

  const __int128 safe_inputs_total = static_cast<__int128>(kSpendCount) * kMaxMoney;
  const __int128 safe_outputs_total = static_cast<__int128>(kSpendCount) * kMaxMoney;
  ASSERT_GT(safe_inputs_total, static_cast<__int128>(std::numeric_limits<int64_t>::max()));
  ASSERT_EQ(safe_inputs_total, safe_outputs_total);

  const test::StubHeaderAncestryView ancestry;
  EXPECT_EQ(ValidateBlockSubsidy({block, ancestry, unspent, kHeight, 0}), Result{});
}

TEST(ValidateSpendingTransactionTest, AcceptsExactReferenceRewardDespiteAccumulatorOverflowRisk) {
  constexpr int64_t kMaxMoney = 21'000'000ll * 100'000'000ll;
  constexpr int64_t kSubsidy = 50ll * 100'000'000ll;
  constexpr int kHeight = 1;
  constexpr int kSpendCount = 4'393;

  protocol::Block block;
  block.AddTransaction(MakeCoinbaseTx(kSubsidy + 1));

  test::StaticUnspentOutputsView unspent;
  for (int i = 0; i < kSpendCount; ++i) {
    const int64_t output_value = (i + 1 == kSpendCount) ? (kMaxMoney - 1) : kMaxMoney;
    auto tx = MakeMaxMoneySpendTx(i, output_value);
    block.AddTransaction(tx);
    unspent.Add(std::move(tx), {{.funding_height = 0,
                                 .funding_flags = 0u,
                                 .amount = kMaxMoney,
                                 .pubkey_script = {},
                                 .spend_input_index = 0}});
  }

  const __int128 safe_inputs_total = static_cast<__int128>(kSpendCount) * kMaxMoney;
  const __int128 safe_outputs_total = static_cast<__int128>(kSpendCount) * kMaxMoney - 1;
  ASSERT_GT(safe_inputs_total, static_cast<__int128>(std::numeric_limits<int64_t>::max()));
  ASSERT_GT(safe_outputs_total, static_cast<__int128>(std::numeric_limits<int64_t>::max()));
  ASSERT_EQ(safe_inputs_total - safe_outputs_total, static_cast<__int128>(1));

  const test::StubHeaderAncestryView ancestry;
  EXPECT_EQ(ValidateBlockSubsidy({block, ancestry, unspent, kHeight, 0}), Result{});
}

TEST(ValidateSpendingTransactionTest, RejectsCoinbaseAboveReferenceRewardDespiteAccumulatorOverflowRisk) {
  constexpr int64_t kMaxMoney = 21'000'000ll * 100'000'000ll;
  constexpr int64_t kSubsidy = 50ll * 100'000'000ll;
  constexpr int kHeight = 1;
  constexpr int kSpendCount = 4'393;

  protocol::Block block;
  block.AddTransaction(MakeCoinbaseTx(kSubsidy + 2));

  test::StaticUnspentOutputsView unspent;
  for (int i = 0; i < kSpendCount; ++i) {
    const int64_t output_value = (i + 1 == kSpendCount) ? (kMaxMoney - 1) : kMaxMoney;
    auto tx = MakeMaxMoneySpendTx(i, output_value);
    block.AddTransaction(tx);
    unspent.Add(std::move(tx), {{.funding_height = 0,
                                 .funding_flags = 0u,
                                 .amount = kMaxMoney,
                                 .pubkey_script = {},
                                 .spend_input_index = 0}});
  }

  const __int128 safe_inputs_total = static_cast<__int128>(kSpendCount) * kMaxMoney;
  const __int128 safe_outputs_total = static_cast<__int128>(kSpendCount) * kMaxMoney - 1;
  ASSERT_GT(safe_inputs_total, static_cast<__int128>(std::numeric_limits<int64_t>::max()));
  ASSERT_GT(safe_outputs_total, static_cast<__int128>(std::numeric_limits<int64_t>::max()));
  ASSERT_EQ(safe_inputs_total - safe_outputs_total, static_cast<__int128>(1));

  const test::StubHeaderAncestryView ancestry;
  EXPECT_EQ(ValidateBlockSubsidy({block, ancestry, unspent, kHeight, 0}),
            Error::Spending_CoinbaseAmountExceedsBlockReward);
}

}  // namespace
}  // namespace hornet::consensus::rules