#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/decode.h"
#include "hornetlib/protocol/script/runtime/engine.h"

namespace hornet::protocol::script::runtime {

using lang::Bytes;
using lang::Error;
using lang::Op;

namespace {

constexpr int64_t kSequenceLockTimeDisableFlag = 1u << 31;
constexpr int64_t kSequenceLockTimeMask = 0x0000'ffff;
constexpr int64_t kSequenceLockTimeTypeFlag = 1u << 22;
constexpr int64_t kLocktimeMinimumTimestamp = 500'000'000;

enum class LockType { BlockHeight, BlockTime };

LockType ClassifyLockTime(int64_t lock_time) {
  return lock_time < kLocktimeMinimumTimestamp ? LockType::BlockHeight : LockType::BlockTime;
}

int64_t MaskSequence(int64_t sequence) {
  return sequence & (kSequenceLockTimeTypeFlag | kSequenceLockTimeMask);
}

LockType ClassifySequence(int64_t sequence) {
  return MaskSequence(sequence) < kSequenceLockTimeTypeFlag ? LockType::BlockHeight : LockType::BlockTime;
}

// Op::CheckLockTimeVerify = 0xb1
void OnCheckLockTimeVerify(const Context& context) {
  if (!context.IsCheckLockTimeVerify()) return;  // Treat as Op::Nop2

  const int64_t lock_time = context.DecodeTop<int64_t, 5>();
  if (lock_time < 0) Throw(Error::LockTimeInvalid);

  const int64_t tx_lock_time = context.Spend().tx.LockTime();
  if (ClassifyLockTime(lock_time) != ClassifyLockTime(tx_lock_time) || lock_time > tx_lock_time ||
      context.Spend().Input().sequence == 0xffffffff)
    Throw(Error::LockTimeUnsatisfied);
}

// Op::CheckSequenceVerify = 0xb2
void OnCheckSequenceVerify(const Context& context) {
  if (!context.IsCheckSequenceVerify()) return;  // Treat as Op::Nop3

  const int64_t sequence = context.DecodeTop<int64_t, 5>();
  if (sequence < 0) Throw(Error::LockTimeInvalid);
  if ((sequence & kSequenceLockTimeDisableFlag) != 0) return;  // Treat as nop

  const int64_t tx_sequence = context.Spend().Input().sequence;
  if (context.Spend().tx.Version() < 2 || (tx_sequence & kSequenceLockTimeDisableFlag) != 0 ||
      ClassifySequence(sequence) != ClassifySequence(tx_sequence) || MaskSequence(sequence) > MaskSequence(tx_sequence))
    Throw(Error::LockTimeUnsatisfied);
}

}  // namespace

void RegisterLockHandlers(Dispatcher& table) {
  table[Op::CheckLockTimeVerify] = &OnCheckLockTimeVerify;
  table[Op::CheckSequenceVerify] = &OnCheckSequenceVerify;
}

}  // namespace hornet::protocol::script::runtime