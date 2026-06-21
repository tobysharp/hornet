#include <array>
#include <optional>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "hornetlib/crypto/curve.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/processor.h"
#include "hornetlib/protocol/script/runtime/signing.h"
#include "hornetlib/protocol/script/writer.h"
#include "hornetlib/util/hex.h"

namespace hornet::protocol::script::runtime::ops {
namespace {

using lang::Op;

constexpr auto kSignatureBlob =
  "304402204cecbbfb4bdb3602d972d69ea7c90603c422684bc6a17476c0ae734e698ac048"
  "02200930b6978879f007199d8e7d51f756decc7e692590edd86bd5e6e32aed0d27cd01"_bytes;

constexpr auto kCompressedPubkey =
  "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"_bytes;

constexpr auto kCompressedPubkeyHash =
  "751e76e8199196d454941c45d1b3a323f1433bd6"_bytes;

template <typename Wide>
std::array<uint8_t, sizeof(Wide)> ToBigEndianBytes(const Wide& value) {
  std::array<uint8_t, sizeof(Wide)> bytes{};
  for (int i = 0; i < Wide::kWords; ++i) {
    const auto word = value.Words()[i];
    for (int j = 0; j < int(sizeof(typename Wide::Word)); ++j)
      bytes[bytes.size() - 1 - (i * sizeof(typename Wide::Word) + j)] = uint8_t(word >> (j << 3));
  }
  return bytes;
}

template <typename Wide>
std::vector<uint8_t> EncodeDerInteger(const Wide& value) {
  const auto bytes = ToBigEndianBytes(value);
  const auto first_non_zero = std::find_if(bytes.begin(), bytes.end(), [](uint8_t byte) { return byte != 0; });

  std::vector<uint8_t> encoded(first_non_zero, bytes.end());
  if (encoded.empty()) encoded.push_back(0x00);
  if ((encoded.front() & 0x80) != 0) encoded.insert(encoded.begin(), 0x00);
  return encoded;
}

std::vector<uint8_t> EncodeDerSignature(const crypto::ecdsa::Curve::Signature& signature, uint8_t sighash_type = 0x01) {
  const auto r = EncodeDerInteger(signature.first);
  const auto s = EncodeDerInteger(signature.second);

  std::vector<uint8_t> der;
  der.reserve(2 + 2 + r.size() + 2 + s.size() + 1);
  der.push_back(0x30);
  der.push_back(uint8_t(2 + r.size() + 2 + s.size()));
  der.push_back(0x02);
  der.push_back(uint8_t(r.size()));
  der.insert(der.end(), r.begin(), r.end());
  der.push_back(0x02);
  der.push_back(uint8_t(s.size()));
  der.insert(der.end(), s.begin(), s.end());
  der.push_back(sighash_type);
  return der;
}

std::vector<uint8_t> MakeP2PKHLockingScript(std::span<const uint8_t> pubkey_hash) {
  return Writer{}
      .Then(Op::Duplicate)
      .Then(Op::Hash160)
      .PushData(pubkey_hash)
      .Then(Op::EqualVerify)
      .Then(Op::CheckSig)
      .Release();
}

std::vector<uint8_t> MakeP2PKHSignature(const script::SpendContext& spend, std::span<const uint8_t> locking_script) {
  using Curve = crypto::ecdsa::Curve;

  static constexpr std::array<uint8_t, 1> kSigHashAll = {0x01};
  const auto digest = BuildSpendDigest(spend, kSigHashAll, locking_script);

  const Curve::Wide private_key{1};
  const Curve::Wide nonce{1};
  const Curve::Point nonce_point = nonce * Curve::G;
  const Curve::Mod_n r{nonce_point.NormalizedX().x.Modulo(crypto::ecdsa::secp256k1::n)};
  const Curve::Mod_n z{Curve::Wide::FromBigEndianBytes(digest)};
  const Curve::Mod_n d{private_key};
  const Curve::Mod_n s = (z + r * d) / Curve::Mod_n{nonce};

  return EncodeDerSignature({r.x, s.x}, kSigHashAll.front());
}

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
  const auto unlocking_script = Writer{}.PushData(kSignatureBlob).PushData(kCompressedPubkey).Release();
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

TEST(SigOpsTest, P2PKHAcceptsValidCompressedSecp256k1Spend) {
  const auto locking_script = MakeP2PKHLockingScript(kCompressedPubkeyHash);

  Transaction tx = MakeLegacyCheckSigSpendTx();
  script::SpendContext spend{tx, 0, script::SpendPath::LegacyDirect};
  const auto signature = MakeP2PKHSignature(spend, locking_script);
  const auto unlocking_script = Writer{}.PushData(signature).PushData(kCompressedPubkey).Release();
  tx.SetSignatureScript(0, unlocking_script);

  script::Processor processor{Policy{.require_minimal = false, .require_strict_der_signatures = false},
                              std::make_optional(spend)};

  ASSERT_TRUE(*processor.Run(unlocking_script));
  const auto lock_result = processor.Run(locking_script);
  ASSERT_TRUE(lock_result);
  EXPECT_TRUE(*lock_result);
}

TEST(SigOpsTest, P2PKHFailsEqualVerifyWhenPubkeyHashDoesNotMatch) {
  constexpr auto wrong_hash = "0000000000000000000000000000000000000000"_bytes;
  const auto locking_script = MakeP2PKHLockingScript(wrong_hash);

  Transaction tx = MakeLegacyCheckSigSpendTx();
  script::SpendContext spend{tx, 0, script::SpendPath::LegacyDirect};
  const auto signature = MakeP2PKHSignature(spend, locking_script);
  const auto unlocking_script = Writer{}.PushData(signature).PushData(kCompressedPubkey).Release();
  tx.SetSignatureScript(0, unlocking_script);

  script::Processor processor{Policy{.require_minimal = false, .require_strict_der_signatures = false},
                              std::make_optional(spend)};

  ASSERT_TRUE(*processor.Run(unlocking_script));
  EXPECT_EQ(processor.Run(locking_script), lang::Error::OpEqualVerify);
}

TEST(SigOpsTest, P2PKHReturnsFalseWhenSignatureDoesNotValidate) {
  const auto locking_script = MakeP2PKHLockingScript(kCompressedPubkeyHash);

  Transaction tx = MakeLegacyCheckSigSpendTx();
  script::SpendContext spend{tx, 0, script::SpendPath::LegacyDirect};
  auto bad_signature = MakeP2PKHSignature(spend, locking_script);
  bad_signature[10] ^= 0x01;
  const auto unlocking_script = Writer{}.PushData(bad_signature).PushData(kCompressedPubkey).Release();
  tx.SetSignatureScript(0, unlocking_script);

  script::Processor processor{Policy{.require_minimal = false, .require_strict_der_signatures = false},
                              std::make_optional(spend)};

  ASSERT_TRUE(*processor.Run(unlocking_script));
  const auto lock_result = processor.Run(locking_script);
  ASSERT_TRUE(lock_result);
  EXPECT_FALSE(*lock_result);
}

}  // namespace
}  // namespace hornet::protocol::script::runtime::ops