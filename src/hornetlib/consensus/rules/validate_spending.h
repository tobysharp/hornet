#pragma once

#include "hornetlib/consensus/rules/validate_forward.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus {

struct InputSpendingContext {
    const protocol::TransactionConstView tx;
    const int input_index;
    const UnspentDetail& prevout;
    const int height;
};

namespace rules {

[[nodiscard]] inline Result ValidateCoinbaseMaturity(const InputSpendingContext&) {
    return {};  // TODO
}


}  // namespace rules
}  // namespace hornet::consensus
