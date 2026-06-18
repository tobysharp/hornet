#pragma once

#include <cstdint>
#include <vector>

#include "hornetlib/protocol/transaction.h"

namespace hornet::test {

inline protocol::Transaction MakeCoinbaseLikeTransaction(int64_t amount = 50'000'000) {
  protocol::Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.Input(0).previous_output = protocol::OutPoint::Null();
  tx.Input(0).sequence = 0xffffffff;
  tx.SetSignatureScript(0, std::vector<uint8_t>{0x02, 0x01});
  tx.ResizeOutputs(1);
  tx.Output(0).value = amount;
  tx.SetPkScript(0, std::vector<uint8_t>{0x51});
  tx.SetLockTime(0);
  return tx;
}

inline protocol::Transaction MakeSpendTransaction(const protocol::OutPoint& prevout, int64_t amount = 1) {
  protocol::Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.Input(0).previous_output = prevout;
  tx.Input(0).sequence = 0xffffffff;
  tx.SetSignatureScript(0, std::vector<uint8_t>{0x51});
  tx.ResizeOutputs(1);
  tx.Output(0).value = amount;
  tx.SetPkScript(0, std::vector<uint8_t>{0x51});
  tx.SetLockTime(0);
  return tx;
}

}  // namespace hornet::test
