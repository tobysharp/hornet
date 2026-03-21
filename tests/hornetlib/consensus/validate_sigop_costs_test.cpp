#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/consensus/rules/scripts/sigops.h"
#include "hornetlib/consensus/rules/scripts/sigops_detail.h"
#include "hornetlib/consensus/rules/scripts/spend_patterns.h"
#include "hornetlib/consensus/rules/validate_spending.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/writer.h"
#include "hornetlib/protocol/transaction.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace hornet::consensus::rules::scripts {
namespace {

using protocol::Block;
using protocol::Hash;
using protocol::Transaction;
using protocol::script::Writer;
using protocol::script::lang::ConstantToOp;
using protocol::script::lang::Op;

std::vector<uint8_t> MakeScript(std::initializer_list<Op> opcodes) {
  Writer writer;
  for (const Op opcode : opcodes) writer.Then(opcode);
  return writer.Release();
}

std::vector<uint8_t> MakeMultisigScript(int key_count, Op opcode = Op::CheckMultiSig) {
  return Writer{}.PushInt(key_count).Then(opcode).Release();
}

std::vector<uint8_t> MakeWitnessProgram(int version, std::span<const uint8_t> program) {
  return Writer{}.PushInt(version).PushData(program).Release();
}

std::vector<uint8_t> MakeP2SHScript(std::span<const uint8_t> script_hash) {
  return Writer{}.Then(Op::Hash160).PushData(script_hash).Then(Op::Equal).Release();
}

std::vector<uint8_t> RepeatOp(Op opcode, int count) {
  return std::vector<uint8_t>(count, +opcode);
}

Transaction MakeTransaction(int input_count, int output_count, bool witness = false) {
  Transaction tx;
  tx.SetVersion(2);
  tx.ResizeInputs(input_count);
  tx.ResizeOutputs(output_count);
  if (witness) tx.ResizeWitnesses(input_count);
  for (int i = 0; i < input_count; ++i) {
    tx.Input(i).previous_output = {Hash{static_cast<uint8_t>(i + 1)}, static_cast<uint32_t>(i)};
    tx.Input(i).sequence = 0xffffffff;
  }
  for (int i = 0; i < output_count; ++i) tx.Output(i).value = 1'000;
  tx.SetLockTime(0);
  return tx;
}

SpendRecord MakeSpend(std::span<const uint8_t> pubkey_script, int input_index = 0,
                      bool coinbase = false) {
  return {.funding_height = 200,
          .funding_flags = coinbase ? 1u : 0u,
          .amount = 1'000,
          .pubkey_script = pubkey_script,
          .spend_input_index = input_index};
}

class StubHeaderAncestryView : public HeaderAncestryView {
 public:
  int Length() const override { return 1; }
  const Hash& HashAt(int) const override { return hash_; }
  uint32_t TimestampAt(int) const override { return 0; }
  std::vector<uint32_t> LastNTimestamps(int, int) const override { return {0}; }
 private:
  Hash hash_{};
};

class StubUnspentOutputsView : public UnspentOutputsView {
 public:
  struct Entry {
    Transaction tx;
    std::vector<SpendRecord> spends;
  };

  void Add(Transaction tx, std::vector<SpendRecord> spends) {
    entries_.push_back({std::move(tx), std::move(spends)});
  }
  int EnumeratedCount() const { return enumerated_count_; }
  Result QueryPrevoutsUnspent(const Block&) const override { return {}; }
  Result QueryOutPointsUnique(const Block&) const override { return {}; }

 protected:
  Result EnumerateTransactions(const Block&, const Callback cb, const void* user) const override {
    enumerated_count_ = 0;
    for (const auto& entry : entries_) {
      ++enumerated_count_;
      if (const Result result = cb(entry.tx, entry.spends, user); !result) return result;
    }
    return {};
  }

 private:
  std::vector<Entry> entries_;
  mutable int enumerated_count_ = 0;
};

TEST(ValidateSigOpCostsTest, WitnessProgramParseRejectsMalformedScripts) {
  const std::array<uint8_t, 20> program20 = {1};
  const auto too_short = MakeScript({Op::CheckSig});
  const auto too_long = MakeWitnessProgram(0, std::vector<uint8_t>(41, 0x33));
  const auto bad_version = Writer{}.Then(Op::CheckSig).PushData(program20).Release();
  const auto negative_version = MakeWitnessProgram(-1, program20);
  const std::vector<uint8_t> bad_push = {+Op::PushConst0, +Op::PushData1, 0x14,
                                         0,              0,              0,    0,
                                         0,              0,              0,    0,
                                         0,              0,              0,    0,
                                         0,              0,              0,    0,
                                         0,              0,              0,    0,
                                         0};
  const std::vector<uint8_t> size_mismatch = {+Op::PushConst0, 0x14,
                                              0,              0,    0, 0, 0, 0, 0,
                                              0,              0,    0, 0, 0, 0, 0,
                                              0,              0,    0, 0, 0};

  EXPECT_FALSE(WitnessProgram::Parse(too_short).has_value());
  EXPECT_FALSE(WitnessProgram::Parse(too_long).has_value());
  EXPECT_FALSE(WitnessProgram::Parse(bad_version).has_value());
  EXPECT_FALSE(WitnessProgram::Parse(negative_version).has_value());
  EXPECT_FALSE(WitnessProgram::Parse(bad_push).has_value());
  EXPECT_FALSE(WitnessProgram::Parse(size_mismatch).has_value());
}

TEST(ValidateSigOpCostsTest, WitnessProgramParseAcceptsValidPrograms) {
  const std::array<uint8_t, 20> key_hash = {1, 2, 3};
  const auto script = MakeWitnessProgram(1, key_hash);

  const auto parsed = WitnessProgram::Parse(script);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->version, 1);
  EXPECT_TRUE(std::equal(parsed->program.begin(), parsed->program.end(), key_hash.begin(),
                         key_hash.end()));
}

TEST(ValidateSigOpCostsTest, PayToScriptHashRequiresExactTemplate) {
  const std::array<uint8_t, 20> hash20 = {9, 8, 7};
  const auto p2sh = MakeP2SHScript(hash20);
  auto wrong_opcode = p2sh;
  wrong_opcode.front() = +Op::Equal;
  std::vector<uint8_t> short_script(p2sh.begin(), p2sh.end() - 1);

  EXPECT_TRUE(IsPayToScriptHash(p2sh));
  EXPECT_FALSE(IsPayToScriptHash(wrong_opcode));
  EXPECT_FALSE(IsPayToScriptHash(short_script));
}

TEST(ValidateSigOpCostsTest, ExtractRedeemScriptRequiresPushOnlyScriptSig) {
  const std::array<uint8_t, 1> first = {0x01};
  const auto redeem = MakeScript({Op::CheckSig, Op::CheckSigVerify});
  const auto push_only = Writer{}.PushData(first).PushData(redeem).Release();
  const auto non_push = MakeScript({Op::CheckSig});

  EXPECT_FALSE(ExtractRedeemScript({}).has_value());
  EXPECT_FALSE(ExtractRedeemScript(non_push).has_value());

  const auto extracted = ExtractRedeemScript(push_only);
  ASSERT_TRUE(extracted.has_value());
  EXPECT_TRUE(std::equal(extracted->begin(), extracted->end(), redeem.begin(), redeem.end()));
}

TEST(ValidateSigOpCostsTest, SpendPathClassifyRecognizesAllSupportedForms) {
  const std::array<uint8_t, 20> hash20 = {1, 2, 3};
  const std::array<uint8_t, 32> hash32 = {4, 5, 6};
  const auto flags = CombineFlags({VerifyFlag::P2SH, VerifyFlag::Witness});
  const auto witness_pubkey = MakeWitnessProgram(0, hash20);
  const auto p2sh_pubkey = MakeP2SHScript(hash20);
  const auto redeem = MakeMultisigScript(2);
  const auto redeem_sig = Writer{}.PushData(redeem).Release();
  const auto redeem_witness = MakeWitnessProgram(0, hash32);
  const auto redeem_witness_sig = Writer{}.PushData(redeem_witness).Release();

  const auto native_witness = SpendPath::Classify({witness_pubkey, {}, {}, flags});
  EXPECT_EQ(native_witness.type, SpendPath::Witness);
  ASSERT_TRUE(native_witness.witness.has_value());

  const auto p2sh = SpendPath::Classify({p2sh_pubkey, redeem_sig, {}, flags});
  EXPECT_EQ(p2sh.type, SpendPath::P2SH);
  ASSERT_TRUE(p2sh.redeem.has_value());
  EXPECT_TRUE(std::equal(p2sh.redeem->begin(), p2sh.redeem->end(), redeem.begin(), redeem.end()));

  const auto p2sh_witness = SpendPath::Classify({p2sh_pubkey, redeem_witness_sig, {}, flags});
  EXPECT_EQ(p2sh_witness.type, SpendPath::P2SHWitness);
  ASSERT_TRUE(p2sh_witness.redeem.has_value());
  ASSERT_TRUE(p2sh_witness.witness.has_value());

  const auto legacy = SpendPath::Classify({MakeScript({Op::CheckSig}), {}, {}, flags});
  EXPECT_EQ(legacy.type, SpendPath::Legacy);
}

TEST(ValidateSigOpCostsTest, OpCodeCountCoversChecksigAndMultisigBranches) {
  EXPECT_EQ(sigops::OpCodeCount<false>(Op::CheckSig, Op::PushEmpty), 1);
  EXPECT_EQ(sigops::OpCodeCount<false>(Op::CheckSigVerify, Op::PushEmpty), 1);
  EXPECT_EQ(sigops::OpCodeCount<false>(Op::CheckMultiSig, ConstantToOp(3)), 20);
  EXPECT_EQ(sigops::OpCodeCount<true>(Op::CheckMultiSig, ConstantToOp(3)), 3);
  EXPECT_EQ(sigops::OpCodeCount<true>(Op::CheckMultiSigVerify, Op::CheckSig), 20);
  EXPECT_EQ(sigops::OpCodeCount<true>(Op::Drop, Op::PushEmpty), 0);
}

TEST(ValidateSigOpCostsTest, ScriptCountHandlesEmptyAndAccurateMultisigScripts) {
  const auto multisig = MakeMultisigScript(3);
  const auto checksigs = MakeScript({Op::CheckSig, Op::CheckSigVerify});

  EXPECT_EQ(sigops::ScriptCount(protocol::Script{}), 0);
  EXPECT_EQ(sigops::ScriptCount(multisig), 20);
  EXPECT_EQ(sigops::ScriptCount<true>(multisig), 3);
  EXPECT_EQ(sigops::ScriptCount<true>(checksigs), 2);
}

TEST(ValidateSigOpCostsTest, WitnessProgramCountCoversVersionAndProgramBranches) {
  const std::array<uint8_t, 20> key_hash = {1};
  const std::array<uint8_t, 32> script_hash = {2};
  const std::array<uint8_t, 31> other_program = {3};
  const auto witness_script = MakeMultisigScript(3, Op::CheckMultiSigVerify);
  auto tx = MakeTransaction(1, 1, true);
  tx.ResizeComponents(0, 1);
  tx.SetWitnessScript(0, 0, witness_script);

  EXPECT_EQ(sigops::WitnessProgramCount({1, key_hash}, {}), 0);
  EXPECT_EQ(sigops::WitnessProgramCount({0, key_hash}, {}), 1);
  EXPECT_EQ(sigops::WitnessProgramCount({0, other_program}, {}), 0);
  EXPECT_EQ(sigops::WitnessProgramCount({0, script_hash}, {}), 0);
  EXPECT_EQ(sigops::WitnessProgramCount({0, script_hash}, tx.InputWitness(0)), 3);
}

TEST(ValidateSigOpCostsTest, SpendPathCostCoversLegacyP2SHWitnessAndP2SHWitness) {
  const std::array<uint8_t, 32> script_hash = {7};
  const auto redeem = MakeMultisigScript(2);
  const auto witness_script = MakeMultisigScript(4, Op::CheckMultiSigVerify);
  auto tx = MakeTransaction(1, 1, true);
  tx.ResizeComponents(0, 1);
  tx.SetWitnessScript(0, 0, witness_script);
  const SpendScripts spend{{}, {}, tx.InputWitness(0), 0};

  EXPECT_EQ(sigops::SpendPathCost(spend, {SpendPath::Legacy}), 0);
  EXPECT_EQ(sigops::SpendPathCost(spend, {SpendPath::P2SH, redeem}), 8);
  EXPECT_EQ(sigops::SpendPathCost(spend, {SpendPath::Witness, {}, WitnessProgram{0, script_hash}}), 4);
  EXPECT_EQ(sigops::SpendPathCost(spend, {SpendPath::P2SHWitness, redeem, WitnessProgram{0, script_hash}}), 4);
}

TEST(ValidateSigOpCostsTest, LegacySigOpCountSumsInputAndOutputScripts) {
  auto tx = MakeTransaction(1, 1);
  const auto sig_script = MakeScript({Op::CheckSigVerify});
  const auto pk_script = Writer{}.PushInt(3).Then(Op::CheckMultiSig).Then(Op::CheckSig).Release();
  tx.SetSignatureScript(0, sig_script);
  tx.SetPkScript(0, pk_script);

  EXPECT_EQ(LegacySigOpCount(tx), 22);
}

TEST(ValidateSigOpCostsTest, SigOpCostReturnsLegacyCostForCoinbaseTransactions) {
  auto tx = MakeTransaction(1, 1);
  tx.Input(0).previous_output = protocol::OutPoint::Null();
  tx.ResizeWitnesses(1);
  tx.ResizeComponents(0, 1);
  tx.SetWitnessScript(0, 0, MakeScript({Op::CheckMultiSig}));
  tx.SetSignatureScript(0, MakeScript({Op::CheckSig}));
  tx.SetPkScript(0, MakeScript({Op::CheckSigVerify}));
  const std::vector<uint8_t> empty_script;
  const std::array spends = {MakeSpend(empty_script, 0, true)};

  EXPECT_EQ(SigOpCost(tx, spends, 0), 8);
}

TEST(ValidateSigOpCostsTest, SigOpCostSkipsP2SHPathsWithoutP2SHFlag) {
  const std::array<uint8_t, 20> hash20 = {1, 2, 3};
  const auto p2sh_pubkey = MakeP2SHScript(hash20);
  const auto redeem = MakeMultisigScript(2);
  auto tx = MakeTransaction(1, 1);
  tx.SetSignatureScript(0, Writer{}.PushData(redeem).Release());

  const std::array spends = {MakeSpend(p2sh_pubkey)};

  EXPECT_EQ(SigOpCost(tx, spends, 0), 0);
}

TEST(ValidateSigOpCostsTest, SigOpCostSkipsWitnessPathsWithoutWitnessFlag) {
  const std::array<uint8_t, 32> hash32 = {4, 5, 6};
  const auto witness_pubkey = MakeWitnessProgram(0, hash32);
  const auto witness_script = MakeMultisigScript(2, Op::CheckMultiSigVerify);
  auto tx = MakeTransaction(1, 1, true);
  tx.ResizeComponents(0, 1);
  tx.SetWitnessScript(0, 0, witness_script);

  const std::array spends = {MakeSpend(witness_pubkey)};

  EXPECT_EQ(SigOpCost(tx, spends, CombineFlags({VerifyFlag::P2SH})), 0);
}

TEST(ValidateSigOpCostsTest, SigOpCostSkipsUnknownWitnessVersions) {
  const std::array<uint8_t, 32> hash32 = {7, 8, 9};
  const auto witness_pubkey = MakeWitnessProgram(1, hash32);
  const auto witness_script = MakeMultisigScript(2, Op::CheckMultiSigVerify);
  auto tx = MakeTransaction(1, 1, true);
  tx.ResizeComponents(0, 1);
  tx.SetWitnessScript(0, 0, witness_script);

  const std::array spends = {MakeSpend(witness_pubkey)};

  EXPECT_EQ(SigOpCost(tx, spends, CombineFlags({VerifyFlag::P2SH, VerifyFlag::Witness})), 0);
}

TEST(ValidateSigOpCostsTest, SigOpCostAccumulatesLegacyAndNonLegacyPaths) {
  const std::array<uint8_t, 20> hash20 = {1, 2, 3};
  const std::array<uint8_t, 32> hash32 = {4, 5, 6};
  const auto flags = CombineFlags({VerifyFlag::P2SH, VerifyFlag::Witness});
  const auto legacy_pubkey = MakeScript({Op::CheckSig});
  const auto p2sh_pubkey = MakeP2SHScript(hash20);
  const auto witness_pubkey = MakeWitnessProgram(0, hash20);
  const auto p2sh_redeem = MakeMultisigScript(2);
  const auto p2sh_sig = Writer{}.PushData(p2sh_redeem).Release();
  const auto p2sh_witness_redeem = MakeWitnessProgram(0, hash32);
  const auto p2sh_witness_sig = Writer{}.PushData(p2sh_witness_redeem).Release();
  const auto p2sh_witness_script = MakeMultisigScript(3, Op::CheckMultiSigVerify);
  auto tx = MakeTransaction(4, 1, true);
  tx.SetPkScript(0, MakeScript({Op::CheckSig}));
  tx.SetSignatureScript(1, p2sh_sig);
  tx.SetSignatureScript(3, p2sh_witness_sig);
  tx.ResizeComponents(3, 1);
  tx.SetWitnessScript(3, 0, p2sh_witness_script);

  const std::array spends = {
    MakeSpend(legacy_pubkey, 0),
      MakeSpend(p2sh_pubkey, 1),
      MakeSpend(witness_pubkey, 2),
      MakeSpend(p2sh_pubkey, 3),
  };

  EXPECT_EQ(SigOpCost(tx, spends, flags), 16);
}

TEST(ValidateSigOpCostsTest, ValidateSigOpCostsAcceptsBlockAtBudget) {
  const std::array<uint8_t, 20> hash20 = {1};
  const auto p2sh_pubkey = MakeP2SHScript(hash20);
  const auto redeem = RepeatOp(Op::CheckSig, 20'000);
  auto tx = MakeTransaction(1, 1);
  tx.SetSignatureScript(0, Writer{}.PushData(redeem).Release());

  Block block;
  StubHeaderAncestryView ancestry;
  StubUnspentOutputsView unspent;
  unspent.Add(std::move(tx), {MakeSpend(p2sh_pubkey)});

  EXPECT_EQ(ValidateSigOpCosts({block, ancestry, unspent, 1, CombineFlags({VerifyFlag::P2SH})}),
            Result{});
  EXPECT_EQ(unspent.EnumeratedCount(), 1);
}

TEST(ValidateSigOpCostsTest, ValidateSigOpCostsRejectsBlockAboveBudget) {
  const std::array<uint8_t, 20> hash20 = {1};
  const auto p2sh_pubkey = MakeP2SHScript(hash20);
  const auto redeem = RepeatOp(Op::CheckSig, 20'000);
  auto exact_budget_tx = MakeTransaction(1, 1);
  exact_budget_tx.SetSignatureScript(0, Writer{}.PushData(redeem).Release());

  auto overflow_tx = MakeTransaction(1, 1);
  const auto legacy_pubkey = MakeScript({Op::CheckSig});
  overflow_tx.SetPkScript(0, MakeScript({Op::CheckSig}));

  auto unreachable_tx = MakeTransaction(1, 1);
  unreachable_tx.SetPkScript(0, MakeScript({Op::CheckSig}));

  Block block;
  StubHeaderAncestryView ancestry;
  StubUnspentOutputsView unspent;
  unspent.Add(std::move(exact_budget_tx), {MakeSpend(p2sh_pubkey)});
  unspent.Add(std::move(overflow_tx), {MakeSpend(legacy_pubkey)});
  unspent.Add(std::move(unreachable_tx), {MakeSpend(legacy_pubkey)});

  EXPECT_EQ(ValidateSigOpCosts({block, ancestry, unspent, 1, CombineFlags({VerifyFlag::P2SH})}),
            Error::Spending_BadSigOpsCost);
  EXPECT_EQ(unspent.EnumeratedCount(), 2);
}

}  // namespace
}  // namespace hornet::consensus::rules::scripts