#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/decode.h"
#include "hornetlib/protocol/script/runtime/engine.h"

namespace hornet::protocol::script::runtime {

using lang::Bytes;
using lang::Error;
using lang::Op;

namespace {

enum class LockTimeType { BlockHeight, BlockTime };

LockTimeType ClassifyLockTime(int64_t lock_time) {
  constexpr int64_t kLocktimeMinimumTimestamp = 500'000'000;
  return lock_time < kLocktimeMinimumTimestamp ? LockTimeType::BlockHeight : LockTimeType::BlockTime;
}

}

// Op::CheckLockTimeVerify = 0xb1
static void OnCheckLockTimeVerify(const Context& context) {
  if (!context.IsCheckLockTimeVerify()) return;  // Treat as Op::Nop2

  const int64_t lock_time = Decode<int64_t, 5>(context.Stack().Top(), context.RequiresMinimal());
  if (lock_time < 0) Throw(Error::LockTimeInvalid);

  const auto& tx = context.Spend().tx;
  if (ClassifyLockTime(lock_time) != ClassifyLockTime(tx.LockTime()) || lock_time > tx.LockTime() ||
      tx.Input(context.Spend().input_index).sequence == 0xffffffff)
    Throw(Error::LockTimeUnsatisfied);
}

void RegisterLockHandlers(Dispatcher& table) {
  table[Op::CheckLockTimeVerify] = &OnCheckLockTimeVerify;
}

}  // namespace hornet::protocol::script::runtime