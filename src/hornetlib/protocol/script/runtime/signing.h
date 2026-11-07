#pragma once

#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"

namespace hornet::protocol::script::runtime {

Hash BuildSpendDigest(
  const SpendContext& spend,
  const lang::Bytes sig_arg,
  const lang::Bytes code);

}  // namespace hornet::protocol::script::runtime
