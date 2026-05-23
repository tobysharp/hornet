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
constexpr uint32_t kNonFinalSequence = 0xfffffffeu;
constexpr uint32_t kFinalSequence = 0xffffffffu;

Transaction MakeSpendTx(uint32_t lock_time, uint32_t sequence = kNonFinalSequence) {
  Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.ResizeOutputs(1);
  tx.Input(0).previous_output = {{}, 0};
  tx.Input(0).sequence = sequence;
  tx.Output(0).value = 1000;
  tx.SetPkScript(0, Writer{}.PushInt(1).Release());
  tx.SetLockTime(lock_time);
  return tx;
}

Processor MakeProcessor(const Transaction& tx, bool enable_cltv = true, bool require_minimal = true) {
  FeatureFlags features;
  if (enable_cltv) features |= Feature::CheckLockTimeVerify;
  return Processor{runtime::Policy{.require_minimal = require_minimal, .features = features},
                   std::make_optional(SpendContext{tx, 0, SpendPath::LegacyDirect})};
}

std::vector<uint8_t> MakeCltvScript(int32_t lock_time) {
  return Writer{}.PushInt(lock_time).Then(Op::CheckLockTimeVerify).Release();
}

std::vector<uint8_t> MakeCltvScript(std::span<const uint8_t> encoded_lock_time) {
  return Writer{}.PushData(encoded_lock_time).Then(Op::CheckLockTimeVerify).Release();
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

}  // namespace
}  // namespace hornet::protocol::script::runtime::ops