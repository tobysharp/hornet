#include "hornetlib/consensus/merkle.h"
#include "hornetlib/consensus/rules/validate_spending.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/data/header_timechain.h"
#include "hornetlib/data/utxo/database.h"
#include "hornetlib/data/utxo/joiner.h"
#include "hornetlib/model/header_context.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/transaction.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "testutil/blockchain.h"
#include "testutil/temp_folder.h"

#include "hornetlib/consensus/validate_chain_harness.h"

#include <gtest/gtest.h>

namespace hornet {
namespace {

void FixMerkleRoot(protocol::Block& block) {
  auto header = block.Header();
  header.SetMerkleRoot(consensus::ComputeMerkleRoot(block).hash);
  block.SetHeader(header);
}

class SequenceLocksHarness {
 public:
  using SequenceInput = std::pair<int, uint32_t>;

  protocol::Block MakeCandidateBlock(std::initializer_list<SequenceInput> inputs,
                                     int version = 2) const {
    protocol::Block block;
    const auto prev = kChain[kChain.Length() - 1];
    block.AddTransaction(MakeCoinbase(kChain.Length()));
    block.AddTransaction(MakeSpendTx(inputs, version));

    protocol::BlockHeader header;
    header.SetPreviousBlockHash(prev->Header().ComputeHash());
    header.SetTimestamp(prev->Header().GetTimestamp() + 600);
    header.SetCompactTarget(prev->Header().GetCompactTarget());
    block.SetHeader(header);
    FixMerkleRoot(block);
    return block;
  }

  template <typename Callback>
  consensus::Result ValidateCandidateTransactions(const protocol::Block& block,
                                                  Callback&& callback) const {
    data::HeaderTimechain headers;
    test::TempFolder datadir;
    data::utxo::Database db{datadir.Path()};
    model::HeaderContext context;
    data::HeaderTimechain::ConstIterator tip;

    for (int height = 0; height < kChain.Length(); ++height) {
      tip = headers.Add(context = context.Extend(kChain[height]->Header())).it;
      if (height > 0) db.Append(*kChain[height], height);
    }

    const int height = kChain.Length();
    const auto ancestry = headers.GetValidationView(tip);
    auto joiner = std::make_shared<data::utxo::SpendJoiner>(
        db, std::make_shared<const protocol::Block>(block), height);

    while (joiner->IsAdvanceReady()) joiner->Advance();
    if (!joiner->IsJoinReady()) return consensus::Error::Spending_PrevoutNotUnspent;

    return joiner->Join([&](const protocol::TransactionConstView& tx,
                            std::span<const consensus::SpendRecord> spends) {
      return callback(tx, spends, *ancestry, height);
    });
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

  protocol::Transaction MakeSpendTx(std::initializer_list<SequenceInput> inputs,
                                    int version) const {
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

// Duplicating an unspent coinbase transaction early in the chain violates the BIP30 check.
TEST(ValidateSpendingTest, ProcessDuplicateOutPoint) {
  test::ExpectValidationResult([] {
    // A few empty blocks...
    test::Blockchain data;
    for (int height = 1; height < 4; ++height)
      data.Append(data.Sample(1'000, true));

    // ... followed by a block that duplicates an earlier coinbase ...
    data[3]->Transaction(0).CopyFrom(data[1]->Transaction(0));  

    // ... and just patch up the Merkle root.
    FixMerkleRoot(*data[3]);
    return data;
  }, consensus::Error::Spending_DuplicateOutPoint);
}

// Duplicating a fully-spent coinbase transaction does not violate the BIP30 check.
TEST(ValidateSpendingTest, ProcessFullySpentDuplicateOutPoint) {
  test::ExpectValidationResult([] {
    test::Blockchain data;
    
    // We load a pre-mined chain to bypass the 100-block coinbase maturity waiting.
    // The chain is long enough that Block 1's coinbase has matured and been fully spent.
    data.Load(test::GetDataPath("ValidationPipelineTest_ProcessBlocks.bin"));

    // Append block: Duplicate the now fully-spent coinbase of Block 1
    data.Append(data.Sample(1'000, true));
    data.Back()->Transaction(0).CopyFrom(data[1]->Transaction(0));
    FixMerkleRoot(*data.Back());
    return data;
  });
}

TEST(ValidateSpendingTest, ProcessOutputAmountsExceedInputAmounts) {
  test::ExpectValidationResult([] {
    test::Blockchain data;

    // We load a pre-mined chain so coinbase outputs are mature and spendable.
    data.Load(test::GetDataPath("ValidationPipelineTest_ProcessBlocks.bin"));

    // Append a valid spending block, then corrupt one spend so its outputs exceed its inputs.
    data.Append(data.Sample(2, true));
    data.Back()->Transaction(1).Output(0).value += 1;
    FixMerkleRoot(*data.Back());
    return data;
  }, consensus::Error::Spending_OutputAmountsExceedInputAmounts);
}

TEST(ValidateSpendingTest, ProcessBlockJustOverSigOpCostLimit) {
  test::ExpectValidationResult([] {
    using protocol::script::Writer;
    using protocol::script::lang::Op;

    test::Blockchain data;
    data.Load(test::GetDataPath("ValidationPipelineTest_ProcessBlocks.bin"));

    // First, create a normal-looking funding block whose spend transaction pays to a P2SH output.
    auto funding = data.Sample(2, true, 1, 1);
    const uint32_t spent_unspent_index = funding.Transaction(1).Input(0).sequence;
    const std::vector<uint8_t> p2sh_hash(20, 0x42);
    funding.Transaction(1).SetPkScript(
        0, Writer{}.Then(Op::Hash160).PushData(p2sh_hash).Then(Op::Equal).Release());
    data.AppendFixed(std::move(funding));

    const auto funding_tx = data.Back()->Transaction(1);
    const protocol::OutPoint prevout{funding_tx.GetHash(), 0};
    const int64_t amount = funding_tx.Output(0).value;

    // Then spend that P2SH output with a pushed redeem script containing 20,001 CHECKSIG opcodes.
    // That keeps the block under the legacy structural sigop limit, but exceeds the weighted
    // spending-stage sigop budget once the redeem script is counted as P2SH.
    protocol::Transaction overflow_tx;
    overflow_tx.SetVersion(1);
    overflow_tx.ResizeInputs(1);
    overflow_tx.ResizeOutputs(1);
    overflow_tx.Input(0).previous_output = prevout;
    overflow_tx.Input(0).sequence = spent_unspent_index;
    overflow_tx.SetSignatureScript(0, Writer{}.PushData(std::vector<uint8_t>(20'001, +Op::CheckSig)).Release());
    overflow_tx.Output(0).value = amount;
    overflow_tx.SetPkScript(0, Writer{}.PushInt(1).Release());

    auto overflow = data.Sample(1, true);
    overflow.AddTransaction(overflow_tx);
    data.AppendFixed(std::move(overflow));
    return data;
  }, consensus::Error::Spending_BadSigOpsCost);
}

TEST(ValidateSpendingTest, SequenceLocksIgnoreVersion1Transactions) {
  const SequenceLocksHarness harness;
  const protocol::Block block = harness.MakeCandidateBlock({{120, 1u}}, 1);
  EXPECT_EQ(harness.ValidateCandidateTransactions(block, [](const protocol::TransactionConstView& tx,
                                                            std::span<const consensus::SpendRecord> spends,
                                                            const consensus::HeaderAncestryView& ancestry,
                                                            int height) {
              return consensus::rules::ValidateSequenceLocks({tx, spends, ancestry, height, 0});
            }),
            consensus::Result{});
}

TEST(ValidateSpendingTest, SequenceLocksIgnoreDisabledInputs) {
  constexpr uint32_t kDisableMask = 1u << 31;

  const SequenceLocksHarness harness;
  const protocol::Block block = harness.MakeCandidateBlock({{120, kDisableMask | 1u}});
  EXPECT_EQ(harness.ValidateCandidateTransactions(block, [](const protocol::TransactionConstView& tx,
                                                            std::span<const consensus::SpendRecord> spends,
                                                            const consensus::HeaderAncestryView& ancestry,
                                                            int height) {
              return consensus::rules::ValidateSequenceLocks({tx, spends, ancestry, height, 0});
            }),
            consensus::Result{});
}

TEST(ValidateSpendingTest, SequenceLocksRejectNonFinalHeightLock) {
  const SequenceLocksHarness harness;
  const protocol::Block block = harness.MakeCandidateBlock({{120, 21u}});
  EXPECT_EQ(harness.ValidateCandidateTransactions(block, [](const protocol::TransactionConstView& tx,
                                                            std::span<const consensus::SpendRecord> spends,
                                                            const consensus::HeaderAncestryView& ancestry,
                                                            int height) {
              return consensus::rules::ValidateSequenceLocks({tx, spends, ancestry, height, 0});
            }),
            consensus::Error::Spending_NonFinalTransaction);
}

TEST(ValidateSpendingTest, SequenceLocksAcceptFinalHeightLock) {
  const SequenceLocksHarness harness;
  const protocol::Block block = harness.MakeCandidateBlock({{120, 20u}});
  EXPECT_EQ(harness.ValidateCandidateTransactions(block, [](const protocol::TransactionConstView& tx,
                                                            std::span<const consensus::SpendRecord> spends,
                                                            const consensus::HeaderAncestryView& ancestry,
                                                            int height) {
              return consensus::rules::ValidateSequenceLocks({tx, spends, ancestry, height, 0});
            }),
            consensus::Result{});
}

TEST(ValidateSpendingTest, SequenceLocksRejectNonFinalTimeLock) {
  constexpr uint32_t kTimeMask = 1u << 22;

  const SequenceLocksHarness harness;
  const protocol::Block block = harness.MakeCandidateBlock({{120, kTimeMask | 24u}});
  EXPECT_EQ(harness.ValidateCandidateTransactions(block, [](const protocol::TransactionConstView& tx,
                                                            std::span<const consensus::SpendRecord> spends,
                                                            const consensus::HeaderAncestryView& ancestry,
                                                            int height) {
              return consensus::rules::ValidateSequenceLocks({tx, spends, ancestry, height, 0});
            }),
            consensus::Error::Spending_NonFinalTransaction);
}

TEST(ValidateSpendingTest, SequenceLocksAcceptFinalTimeLock) {
  constexpr uint32_t kTimeMask = 1u << 22;

  const SequenceLocksHarness harness;
  const protocol::Block block = harness.MakeCandidateBlock({{120, kTimeMask | 23u}});
  EXPECT_EQ(harness.ValidateCandidateTransactions(block, [](const protocol::TransactionConstView& tx,
                                                            std::span<const consensus::SpendRecord> spends,
                                                            const consensus::HeaderAncestryView& ancestry,
                                                            int height) {
              return consensus::rules::ValidateSequenceLocks({tx, spends, ancestry, height, 0});
            }),
            consensus::Result{});
}

TEST(ValidateSpendingTest, SequenceLocksUseMostRestrictiveInput) {
  constexpr uint32_t kTimeMask = 1u << 22;

  const SequenceLocksHarness harness;
  const protocol::Block block = harness.MakeCandidateBlock({{120, 1u}, {120, kTimeMask | 24u}});
  EXPECT_EQ(harness.ValidateCandidateTransactions(block, [](const protocol::TransactionConstView& tx,
                                                            std::span<const consensus::SpendRecord> spends,
                                                            const consensus::HeaderAncestryView& ancestry,
                                                            int height) {
              return consensus::rules::ValidateSequenceLocks({tx, spends, ancestry, height});
            }),
            consensus::Error::Spending_NonFinalTransaction);
}

TEST(ValidateSpendingTest, SpendingTransactionSkipsSequenceLocksBeforeActivation) {
  const SequenceLocksHarness harness;
  const protocol::Block block = harness.MakeCandidateBlock({{20, 121u}});
  EXPECT_EQ(harness.ValidateCandidateTransactions(block, [](const protocol::TransactionConstView& tx,
                                                            std::span<const consensus::SpendRecord> spends,
                                                            const consensus::HeaderAncestryView& ancestry,
                                                            int height) {
              return consensus::rules::ValidateSpendingTransaction(tx, spends, ancestry, height);
            }),
            consensus::Result{});
}

}  // namespace
}  // namespace hornet
