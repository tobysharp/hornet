#include <algorithm>
#include <array>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/processor.h"
#include "hornetlib/protocol/script/satisfy.h"
#include "hornetlib/protocol/script/spend.h"
#include "hornetlib/protocol/script/writer.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::protocol::script::runtime::ops {
namespace {

using lang::Error;
using lang::Op;

constexpr uint32_t kLockTimeThreshold = 500'000'000;
constexpr uint32_t kSequenceLockTimeDisableFlag = 1u << 31;
constexpr uint32_t kSequenceLockTimeTypeFlag = 1u << 22;
constexpr uint32_t kSequenceLockTimeMask = 0x0000ffffu;
constexpr uint32_t kNonFinalSequence = 0xfffffffeu;
constexpr uint32_t kFinalSequence = 0xffffffffu;

Transaction MakeSpendTx(uint32_t lock_time, uint32_t sequence = kNonFinalSequence, int version = 1) {
  Transaction tx;
  tx.SetVersion(version);
  tx.ResizeInputs(1);
  tx.ResizeOutputs(1);
  tx.Input(0).previous_output = {{}, 0};
  tx.Input(0).sequence = sequence;
  tx.Output(0).value = 1000;
  tx.SetPkScript(0, Writer{}.PushInt(1).Release());
  tx.SetLockTime(lock_time);
  return tx;
}

Processor MakeProcessor(const Transaction& tx,
                        bool enable_cltv = true,
                        bool enable_csv = false,
                        bool require_minimal = true) {
  FeatureFlags features;
  if (enable_cltv) features |= Feature::CheckLockTimeVerify;
  if (enable_csv) features |= Feature::CheckSequenceVerify;
  return Processor{runtime::Policy{.require_minimal = require_minimal, .features = features},
                   std::make_optional(SpendContext{tx, 0, SpendPath::LegacyDirect})};
}

std::vector<uint8_t> MakeCltvScript(int32_t lock_time) {
  return Writer{}.PushInt(lock_time).Then(Op::CheckLockTimeVerify).Release();
}

std::vector<uint8_t> MakeCltvScript(std::span<const uint8_t> encoded_lock_time) {
  return Writer{}.PushData(encoded_lock_time).Then(Op::CheckLockTimeVerify).Release();
}

std::vector<uint8_t> MakeCsvScript(int32_t sequence) {
  return Writer{}.PushInt(sequence).Then(Op::CheckSequenceVerify).Release();
}

std::vector<uint8_t> MakeCsvScript(std::span<const uint8_t> encoded_sequence) {
  return Writer{}.PushData(encoded_sequence).Then(Op::CheckSequenceVerify).Release();
}

TEST(LockOpsTest, CheckLockTimeVerifyActsAsNopWhenDisabled) {
  const auto script = MakeCltvScript(100);

  const Transaction tx = MakeSpendTx(0);
  Processor processor = MakeProcessor(tx, false);

  const auto result = processor.Run(script);
  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  EXPECT_EQ(processor.TryPeekInt(), 100);
}

TEST(LockOpsTest, CheckLockTimeVerifyRequiresOneOperand) {
  const auto script = Writer{}.Then(Op::CheckLockTimeVerify).Release();

  const Transaction tx = MakeSpendTx(0);
  Processor processor = MakeProcessor(tx);

  EXPECT_EQ(processor.Run(script), Error::StackUnderflow);
}

TEST(LockOpsTest, CheckLockTimeVerifyRejectsNonMinimalNumbersWhenRequired) {
  constexpr std::array<uint8_t, 2> kNonMinimalOne = {0x01, 0x00};
  const auto script = MakeCltvScript(kNonMinimalOne);

  const Transaction tx = MakeSpendTx(1);
  Processor processor = MakeProcessor(tx, true, true);

  EXPECT_EQ(processor.Run(script), Error::NonMinimalNumber);
}

TEST(LockOpsTest, CheckLockTimeVerifyRejectsNegativeLockTimes) {
  const auto script = MakeCltvScript(-1);

  const Transaction tx = MakeSpendTx(0);
  Processor processor = MakeProcessor(tx);

  EXPECT_EQ(processor.Run(script), Error::LockTimeInvalid);
}

TEST(LockOpsTest, CheckLockTimeVerifyRejectsMismatchedLockTimeTypes) {
  const auto script = MakeCltvScript(1);

  const Transaction tx = MakeSpendTx(kLockTimeThreshold);
  Processor processor = MakeProcessor(tx);

  EXPECT_EQ(processor.Run(script), Error::LockTimeUnsatisfied);
}

TEST(LockOpsTest, CheckLockTimeVerifyRejectsLockTimesAboveTransactionLockTime) {
  const auto script = MakeCltvScript(101);

  const Transaction tx = MakeSpendTx(100);
  Processor processor = MakeProcessor(tx);

  EXPECT_EQ(processor.Run(script), Error::LockTimeUnsatisfied);
}

TEST(LockOpsTest, CheckLockTimeVerifyRejectsFinalizedInputs) {
  const auto script = MakeCltvScript(100);

  const Transaction tx = MakeSpendTx(100, kFinalSequence);
  Processor processor = MakeProcessor(tx);

  EXPECT_EQ(processor.Run(script), Error::LockTimeUnsatisfied);
}

TEST(LockOpsTest, CheckLockTimeVerifyAcceptsSatisfiedBlockHeightLockTimesWithoutPoppingOperand) {
  const auto script = MakeCltvScript(100);

  const Transaction tx = MakeSpendTx(100);
  Processor processor = MakeProcessor(tx);

  const auto result = processor.Run(script);
  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  EXPECT_EQ(processor.TryPeekInt(), 100);
}

TEST(LockOpsTest, CheckLockTimeVerifyAcceptsSatisfiedBlockTimeLockTimesWithoutPoppingOperand) {
  const auto script = MakeCltvScript(static_cast<int32_t>(kLockTimeThreshold));

  const Transaction tx = MakeSpendTx(kLockTimeThreshold);
  Processor processor = MakeProcessor(tx);

  const auto result = processor.Run(script);
  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  EXPECT_EQ(processor.TryPeekInt(), static_cast<int32_t>(kLockTimeThreshold));
}

TEST(LockOpsTest, CheckSequenceVerifyActsAsNopWhenDisabled) {
  const auto script = MakeCsvScript(100);

  const Transaction tx = MakeSpendTx(0, 100, 2);
  Processor processor = MakeProcessor(tx, true, false);

  const auto result = processor.Run(script);
  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  EXPECT_EQ(processor.TryPeekInt(), 100);
}

TEST(LockOpsTest, CheckSequenceVerifyRequiresOneOperand) {
  const auto script = Writer{}.Then(Op::CheckSequenceVerify).Release();

  const Transaction tx = MakeSpendTx(0, 0, 2);
  Processor processor = MakeProcessor(tx, true, true);

  EXPECT_EQ(processor.Run(script), Error::StackUnderflow);
}

TEST(LockOpsTest, CheckSequenceVerifyRejectsNonMinimalNumbersWhenRequired) {
  constexpr std::array<uint8_t, 2> kNonMinimalOne = {0x01, 0x00};
  const auto script = MakeCsvScript(kNonMinimalOne);

  const Transaction tx = MakeSpendTx(0, 1, 2);
  Processor processor = MakeProcessor(tx, true, true, true);

  EXPECT_EQ(processor.Run(script), Error::NonMinimalNumber);
}

TEST(LockOpsTest, CheckSequenceVerifyRejectsNegativeLockTimes) {
  const auto script = MakeCsvScript(-1);

  const Transaction tx = MakeSpendTx(0, 0, 2);
  Processor processor = MakeProcessor(tx, true, true);

  EXPECT_EQ(processor.Run(script), Error::LockTimeInvalid);
}

TEST(LockOpsTest, CheckSequenceVerifyTreatsDisabledOperandAsNop) {
  constexpr std::array<uint8_t, 5> kDisabledSequence = {0x00, 0x00, 0x00, 0x80, 0x00};
  const auto script = MakeCsvScript(kDisabledSequence);

  const Transaction tx = MakeSpendTx(0, 0, 1);
  Processor processor = MakeProcessor(tx, true, true);

  const auto result = processor.Run(script);
  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  const auto top = processor.TryPeek();
  ASSERT_TRUE(top);
  EXPECT_TRUE(std::ranges::equal(*top, kDisabledSequence));
}

TEST(LockOpsTest, CheckSequenceVerifyRejectsTransactionsBelowVersion2) {
  const auto script = MakeCsvScript(1);

  const Transaction tx = MakeSpendTx(0, 1, 1);
  Processor processor = MakeProcessor(tx, true, true);

  EXPECT_EQ(processor.Run(script), Error::LockTimeUnsatisfied);
}

TEST(LockOpsTest, CheckSequenceVerifyRejectsTransactionsWithDisabledSequenceLocks) {
  const auto script = MakeCsvScript(1);

  const Transaction tx = MakeSpendTx(0, kSequenceLockTimeDisableFlag | 1u, 2);
  Processor processor = MakeProcessor(tx, true, true);

  EXPECT_EQ(processor.Run(script), Error::LockTimeUnsatisfied);
}

TEST(LockOpsTest, CheckSequenceVerifyRejectsMismatchedSequenceTypes) {
  const auto script = MakeCsvScript(1);

  const Transaction tx = MakeSpendTx(0, kSequenceLockTimeTypeFlag | 1u, 2);
  Processor processor = MakeProcessor(tx, true, true);

  EXPECT_EQ(processor.Run(script), Error::LockTimeUnsatisfied);
}

TEST(LockOpsTest, CheckSequenceVerifyRejectsSequenceValuesAboveTransactionSequence) {
  const auto script = MakeCsvScript(101);

  const Transaction tx = MakeSpendTx(0, 100, 2);
  Processor processor = MakeProcessor(tx, true, true);

  EXPECT_EQ(processor.Run(script), Error::LockTimeUnsatisfied);
}

TEST(LockOpsTest, CheckSequenceVerifyIgnoresUnmaskedTransactionBits) {
  const auto script = MakeCsvScript(100);

  const Transaction tx = MakeSpendTx(0, 100 | 0x00100000u, 2);
  Processor processor = MakeProcessor(tx, true, true);

  const auto result = processor.Run(script);
  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  EXPECT_EQ(processor.TryPeekInt(), 100);
}

TEST(LockOpsTest, CheckSequenceVerifyAcceptsSatisfiedBlockHeightLocksWithoutPoppingOperand) {
  const auto script = MakeCsvScript(100);

  const Transaction tx = MakeSpendTx(0, 100, 2);
  Processor processor = MakeProcessor(tx, true, true);

  const auto result = processor.Run(script);
  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  EXPECT_EQ(processor.TryPeekInt(), 100);
}

TEST(LockOpsTest, CheckSequenceVerifyAcceptsSatisfiedBlockTimeLocksWithoutPoppingOperand) {
  const auto script = MakeCsvScript(static_cast<int32_t>(kSequenceLockTimeTypeFlag | 100u));

  const Transaction tx = MakeSpendTx(0, kSequenceLockTimeTypeFlag | 100u, 2);
  Processor processor = MakeProcessor(tx, true, true);

  const auto result = processor.Run(script);
  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  EXPECT_EQ(processor.TryPeekInt(), static_cast<int32_t>(kSequenceLockTimeTypeFlag | 100u));
}

TEST(LockOpsTest, CheckSequenceVerifyAcceptsSatisfiedMaskedMaximumSequence) {
  const auto script = MakeCsvScript(static_cast<int32_t>(kSequenceLockTimeMask));

  const Transaction tx = MakeSpendTx(0, kSequenceLockTimeMask, 2);
  Processor processor = MakeProcessor(tx, true, true);

  const auto result = processor.Run(script);
  ASSERT_TRUE(result);
  EXPECT_TRUE(*result);
  EXPECT_EQ(processor.TryPeekInt(), static_cast<int32_t>(kSequenceLockTimeMask));
}

}  // namespace
}  // namespace hornet::protocol::script::runtime::ops