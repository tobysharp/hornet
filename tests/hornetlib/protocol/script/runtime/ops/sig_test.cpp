#include <array>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "hornetlib/protocol/transaction.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/processor.h"
#include "hornetlib/protocol/script/writer.h"

namespace hornet::protocol::script::runtime::ops {
namespace {

using lang::Op;

Transaction MakeLegacyCheckSigSpendTx() {
  Transaction tx;
  tx.SetVersion(1);
  tx.ResizeInputs(1);
  tx.ResizeOutputs(1);
  tx.Input(0).previous_output = {{}, 0};
  tx.Input(0).sequence = 0xffffffffu;
  tx.Output(0).value = 1000;
  tx.SetPkScript(0, Writer{}.PushInt(1).Release());
  tx.SetLockTime(0);
  return tx;
}

TEST(SigOpsTest, CheckSigAcceptsCompressedSecp256k1PublicKeyOnLegacySpend) {
  const std::vector<uint8_t> signature_blob = {
      0x30, 0x44, 0x02, 0x20, 0x4c, 0xec, 0xbb, 0xfb, 0x4b, 0xdb, 0x36, 0x02,
      0xd9, 0x72, 0xd6, 0x9e, 0xa7, 0xc9, 0x06, 0x03, 0xc4, 0x22, 0x68, 0x4b,
      0xc6, 0xa1, 0x74, 0x76, 0xc0, 0xae, 0x73, 0x4e, 0x69, 0x8a, 0xc0, 0x48,
      0x02, 0x20, 0x09, 0x30, 0xb6, 0x97, 0x88, 0x79, 0xf0, 0x07, 0x19, 0x9d,
      0x8e, 0x7d, 0x51, 0xf7, 0x56, 0xde, 0xcc, 0x7e, 0x69, 0x25, 0x90, 0xed,
      0xd8, 0x6b, 0xd5, 0xe6, 0xe3, 0x2a, 0xed, 0x0d, 0x27, 0xcd, 0x01,
  };
  const std::array<uint8_t, 33> compressed_pubkey = {
      0x02,
      0x79, 0xbe, 0x66, 0x7e, 0xf9, 0xdc, 0xbb, 0xac,
      0x55, 0xa0, 0x62, 0x95, 0xce, 0x87, 0x0b, 0x07,
      0x02, 0x9b, 0xfc, 0xdb, 0x2d, 0xce, 0x28, 0xd9,
      0x59, 0xf2, 0x81, 0x5b, 0x16, 0xf8, 0x17, 0x98,
  };

  const auto unlocking_script = Writer{}.PushData(signature_blob).PushData(compressed_pubkey).Release();
  const auto locking_script = Writer{}.Then(Op::CheckSig).Release();

  Transaction tx = MakeLegacyCheckSigSpendTx();
  tx.SetSignatureScript(0, unlocking_script);

  script::SpendContext spend{tx, 0, script::SpendPath::LegacyDirect};
  script::Processor processor{Policy{.require_minimal = false, .require_strict_der_signatures = false},
                              //0,
                              std::make_optional(spend)};

  ASSERT_TRUE(*processor.Run(unlocking_script));
  const auto lock_result = processor.Run(locking_script);
  ASSERT_TRUE(lock_result);
  EXPECT_TRUE(*lock_result);
}

}  // namespace
}  // namespace hornet::protocol::script::runtime::ops