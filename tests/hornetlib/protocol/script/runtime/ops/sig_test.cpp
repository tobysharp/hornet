#include <algorithm>
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

using secp256k1 = crypto::ecdsa::secp256k1;

struct TestKey {
  secp256k1::Wide private_key;
  std::vector<uint8_t> public_key;
};

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

std::vector<uint8_t> EncodeDerSignature(const crypto::ecdsa::secp256k1::Signature& signature, uint8_t sighash_type = 0x01) {
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

std::vector<uint8_t> EncodeCompressedPublicKey(const secp256k1::Point& point) {
  std::vector<uint8_t> sec1;
  sec1.reserve(33);
  sec1.push_back(crypto::ecdsa::detail::IsEven(point.y.x) ? 0x02 : 0x03);

  const auto x = ToBigEndianBytes(point.x.x);
  sec1.insert(sec1.end(), x.begin(), x.end());
  return sec1;
}

TestKey MakeTestKey(uint32_t scalar) {
  const secp256k1::Wide private_key{scalar};
  return {private_key, EncodeCompressedPublicKey(private_key * secp256k1::G)};
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

std::vector<uint8_t> MakeCheckMultiSigLockingScript(int sig_count, std::span<const std::vector<uint8_t>> pubkeys) {
  Writer writer;
  writer.PushInt(sig_count);
  for (const auto& pubkey : pubkeys) writer.PushData(pubkey);
  writer.PushInt(int(pubkeys.size()));
  return writer.Then(Op::CheckMultiSig).Release();
}

std::vector<uint8_t> MakeCheckMultiSigUnlockingScript(std::span<const std::vector<uint8_t>> signatures,
                                                      std::span<const uint8_t> dummy = {}) {
  Writer writer;
  writer.PushData(dummy);
  for (const auto& signature : signatures) writer.PushData(signature);
  return writer.Release();
}

std::vector<uint8_t> MakeLegacySignature(const script::SpendContext& spend, std::span<const uint8_t> locking_script,
                                         const secp256k1::Wide& private_key, const secp256k1::Wide& nonce,
                                         uint8_t sighash_type = 0x01) {
  const std::array<uint8_t, 1> sighash = {sighash_type};
  const auto digest = BuildSpendDigest(spend, sighash, locking_script);

  const secp256k1::Point nonce_point = nonce * secp256k1::G;
  const secp256k1::Mod_n r{nonce_point.x.x.Modulo(crypto::ecdsa::constants::n)};
  const secp256k1::Mod_n z{secp256k1::Wide::FromBigEndianBytes(digest)};
  const secp256k1::Mod_n d{private_key};
  const secp256k1::Mod_n s = (z + r * d) / secp256k1::Mod_n{nonce};

  return EncodeDerSignature({r.x, s.x}, sighash_type);
}

std::vector<uint8_t> MakeP2PKHSignature(const script::SpendContext& spend, std::span<const uint8_t> locking_script) {
  return MakeLegacySignature(spend, locking_script, secp256k1::Wide{1}, secp256k1::Wide{1});
}

Policy MakeLegacyPolicy(bool strict_der = false, bool null_dummy = true) {
  script::FeatureFlags features;
  if (strict_der) features |= script::Feature::StrictDER;
  if (null_dummy) features |= script::Feature::NullDummy;
  return Policy{.require_minimal = false, .features = features};
}

script::Processor MakeLegacyProcessor(const script::SpendContext& spend, bool strict_der = false, bool null_dummy = true) {
  return script::Processor{MakeLegacyPolicy(strict_der, null_dummy), std::make_optional(spend)};
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
  script::Processor processor{MakeLegacyPolicy(), std::make_optional(spend)};

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

  script::Processor processor{MakeLegacyPolicy(), std::make_optional(spend)};

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

  script::Processor processor{MakeLegacyPolicy(), std::make_optional(spend)};

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

  script::Processor processor{MakeLegacyPolicy(), std::make_optional(spend)};

  ASSERT_TRUE(*processor.Run(unlocking_script));
  const auto lock_result = processor.Run(locking_script);
  ASSERT_TRUE(lock_result);
  EXPECT_FALSE(*lock_result);
}

TEST(SigOpsTest, CheckSigSignsOnlyTheSuffixAfterCodeSeparator) {
  const auto signing_script = Writer{}.Then(Op::CheckSig).Release();
  const auto locking_script = Writer{}.Then(Op::Nop).Then(Op::CodeSeparator).Then(Op::CheckSig).Release();

  Transaction tx = MakeLegacyCheckSigSpendTx();
  script::SpendContext spend{tx, 0, script::SpendPath::LegacyDirect};
  const auto signature = MakeP2PKHSignature(spend, signing_script);
  const auto unlocking_script = Writer{}.PushData(signature).PushData(kCompressedPubkey).Release();
  tx.SetSignatureScript(0, unlocking_script);

  script::Processor processor{MakeLegacyPolicy(), std::make_optional(spend)};

  ASSERT_TRUE(*processor.Run(unlocking_script));
  const auto lock_result = processor.Run(locking_script);
  ASSERT_TRUE(lock_result);
  EXPECT_TRUE(*lock_result);
}

TEST(SigOpsTest, CheckSigStillCommitsToPrefixWithoutCodeSeparator) {
  const auto signing_script = Writer{}.Then(Op::CheckSig).Release();
  const auto locking_script = Writer{}.Then(Op::Nop).Then(Op::CheckSig).Release();

  Transaction tx = MakeLegacyCheckSigSpendTx();
  script::SpendContext spend{tx, 0, script::SpendPath::LegacyDirect};
  const auto signature = MakeP2PKHSignature(spend, signing_script);
  const auto unlocking_script = Writer{}.PushData(signature).PushData(kCompressedPubkey).Release();
  tx.SetSignatureScript(0, unlocking_script);

  script::Processor processor{MakeLegacyPolicy(), std::make_optional(spend)};

  ASSERT_TRUE(*processor.Run(unlocking_script));
  const auto lock_result = processor.Run(locking_script);
  ASSERT_TRUE(lock_result);
  EXPECT_FALSE(*lock_result);
}

TEST(SigOpsTest, CheckSigUsesTheMostRecentCodeSeparator) {
  const auto signing_script = Writer{}.Then(Op::Nop).Then(Op::CodeSeparator).Then(Op::CheckSig).Release();
  const auto locking_script = Writer{}
      .Then(Op::Nop)
      .Then(Op::CodeSeparator)
      .Then(Op::Nop)
      .Then(Op::CodeSeparator)
      .Then(Op::CheckSig)
      .Release();

  Transaction tx = MakeLegacyCheckSigSpendTx();
  script::SpendContext spend{tx, 0, script::SpendPath::LegacyDirect};
  const auto signature = MakeP2PKHSignature(spend, signing_script);
  const auto unlocking_script = Writer{}.PushData(signature).PushData(kCompressedPubkey).Release();
  tx.SetSignatureScript(0, unlocking_script);

  script::Processor processor{MakeLegacyPolicy(), std::make_optional(spend)};

  ASSERT_TRUE(*processor.Run(unlocking_script));
  const auto lock_result = processor.Run(locking_script);
  ASSERT_TRUE(lock_result);
  EXPECT_FALSE(*lock_result);
}

TEST(SigOpsTest, CheckMultiSigAcceptsValidTwoOfTwoSpend) {
  const auto key1 = MakeTestKey(1);
  const auto key2 = MakeTestKey(2);
  const std::array pubkeys = {key1.public_key, key2.public_key};
  const auto locking_script = MakeCheckMultiSigLockingScript(2, pubkeys);

  Transaction tx = MakeLegacyCheckSigSpendTx();
  script::SpendContext spend{tx, 0, script::SpendPath::LegacyDirect};
  const std::array signatures = {
      MakeLegacySignature(spend, locking_script, key1.private_key, secp256k1::Wide{1}),
      MakeLegacySignature(spend, locking_script, key2.private_key, secp256k1::Wide{2}),
  };
  const auto unlocking_script = MakeCheckMultiSigUnlockingScript(signatures);
  tx.SetSignatureScript(0, unlocking_script);

  auto processor = MakeLegacyProcessor(spend);
  ASSERT_TRUE(*processor.Run(unlocking_script));
  const auto lock_result = processor.Run(locking_script);
  ASSERT_TRUE(lock_result);
  EXPECT_TRUE(*lock_result);
}

TEST(SigOpsTest, CheckMultiSigMatchesOrderedSubsequenceOfPubkeys) {
  const auto key1 = MakeTestKey(1);
  const auto key2 = MakeTestKey(2);
  const auto key3 = MakeTestKey(3);
  const std::array pubkeys = {key1.public_key, key2.public_key, key3.public_key};
  const auto locking_script = MakeCheckMultiSigLockingScript(2, pubkeys);

  Transaction tx = MakeLegacyCheckSigSpendTx();
  script::SpendContext spend{tx, 0, script::SpendPath::LegacyDirect};
  const std::array signatures = {
      MakeLegacySignature(spend, locking_script, key1.private_key, secp256k1::Wide{1}),
      MakeLegacySignature(spend, locking_script, key3.private_key, secp256k1::Wide{3}),
  };
  const auto unlocking_script = MakeCheckMultiSigUnlockingScript(signatures);
  tx.SetSignatureScript(0, unlocking_script);

  auto processor = MakeLegacyProcessor(spend);
  ASSERT_TRUE(*processor.Run(unlocking_script));
  const auto lock_result = processor.Run(locking_script);
  ASSERT_TRUE(lock_result);
  EXPECT_TRUE(*lock_result);
}

TEST(SigOpsTest, CheckMultiSigReturnsFalseWhenRemainingKeysCannotSatisfySignatures) {
  const auto key1 = MakeTestKey(1);
  const auto key2 = MakeTestKey(2);
  const auto key3 = MakeTestKey(3);
  const std::array pubkeys = {key1.public_key, key2.public_key, key3.public_key};
  const auto locking_script = MakeCheckMultiSigLockingScript(2, pubkeys);

  Transaction tx = MakeLegacyCheckSigSpendTx();
  script::SpendContext spend{tx, 0, script::SpendPath::LegacyDirect};
  const std::array signatures = {
      MakeLegacySignature(spend, locking_script, key3.private_key, secp256k1::Wide{3}),
      MakeLegacySignature(spend, locking_script, key1.private_key, secp256k1::Wide{1}),
  };
  const auto unlocking_script = MakeCheckMultiSigUnlockingScript(signatures);
  tx.SetSignatureScript(0, unlocking_script);

  auto processor = MakeLegacyProcessor(spend);
  ASSERT_TRUE(*processor.Run(unlocking_script));
  const auto lock_result = processor.Run(locking_script);
  ASSERT_TRUE(lock_result);
  EXPECT_FALSE(*lock_result);
}

TEST(SigOpsTest, CheckMultiSigStripsMatchingSignaturePushesFromScriptCode) {
  const auto key = MakeTestKey(1);
  const std::array pubkeys = {key.public_key};
  const auto base_script = MakeCheckMultiSigLockingScript(1, pubkeys);
  const auto signing_script = Writer{}.Then(Op::Drop).Write(base_script).Release();

  Transaction tx = MakeLegacyCheckSigSpendTx();
  script::SpendContext spend{tx, 0, script::SpendPath::LegacyDirect};
  const auto signature = MakeLegacySignature(spend, signing_script, key.private_key, secp256k1::Wide{1});
  const auto locking_script = Writer{}
                                  .PushData(signature)
                                  .Write(signing_script)
                                  .Release();
  const std::array signatures = {signature};
  const auto unlocking_script = MakeCheckMultiSigUnlockingScript(signatures);
  tx.SetSignatureScript(0, unlocking_script);

  auto processor = MakeLegacyProcessor(spend);
  ASSERT_TRUE(*processor.Run(unlocking_script));
  const auto lock_result = processor.Run(locking_script);
  ASSERT_TRUE(lock_result);
  EXPECT_TRUE(*lock_result);
}

TEST(SigOpsTest, CheckMultiSigReturnsFalseForEmptySignature) {
  const auto key = MakeTestKey(1);
  const std::array pubkeys = {key.public_key};
  const auto locking_script = MakeCheckMultiSigLockingScript(1, pubkeys);
  const std::array signatures = {std::vector<uint8_t>{}};

  Transaction tx = MakeLegacyCheckSigSpendTx();
  script::SpendContext spend{tx, 0, script::SpendPath::LegacyDirect};
  const auto unlocking_script = MakeCheckMultiSigUnlockingScript(signatures);
  tx.SetSignatureScript(0, unlocking_script);

  auto processor = MakeLegacyProcessor(spend);
  ASSERT_TRUE(processor.Run(unlocking_script));
  const auto lock_result = processor.Run(locking_script);
  ASSERT_TRUE(lock_result);
  EXPECT_FALSE(*lock_result);
}

TEST(SigOpsTest, CheckMultiSigReturnsFalseForInvalidPublicKey) {
  const std::array pubkeys = {std::vector<uint8_t>{0x01}};
  const auto locking_script = MakeCheckMultiSigLockingScript(1, pubkeys);

  Transaction tx = MakeLegacyCheckSigSpendTx();
  script::SpendContext spend{tx, 0, script::SpendPath::LegacyDirect};
  const std::array signatures = {MakeLegacySignature(spend, locking_script, secp256k1::Wide{1}, secp256k1::Wide{1})};
  const auto unlocking_script = MakeCheckMultiSigUnlockingScript(signatures);
  tx.SetSignatureScript(0, unlocking_script);

  auto processor = MakeLegacyProcessor(spend);
  ASSERT_TRUE(*processor.Run(unlocking_script));
  const auto lock_result = processor.Run(locking_script);
  ASSERT_TRUE(lock_result);
  EXPECT_FALSE(*lock_result);
}

TEST(SigOpsTest, CheckMultiSigRejectsMalformedDerWhenStrictDEREnabled) {
  const auto key = MakeTestKey(1);
  const std::array pubkeys = {key.public_key};
  const auto locking_script = MakeCheckMultiSigLockingScript(1, pubkeys);
  const std::array signatures = {std::vector<uint8_t>{0x01}};

  Transaction tx = MakeLegacyCheckSigSpendTx();
  script::SpendContext spend{tx, 0, script::SpendPath::LegacyDirect};
  const auto unlocking_script = MakeCheckMultiSigUnlockingScript(signatures);
  tx.SetSignatureScript(0, unlocking_script);

  auto processor = MakeLegacyProcessor(spend, true);
  ASSERT_TRUE(*processor.Run(unlocking_script));
  EXPECT_EQ(processor.Run(locking_script), lang::Error::InvalidDERSignature);
}

TEST(SigOpsTest, CheckMultiSigTreatsMalformedDerAsFalseWhenStrictDERDisabled) {
  const auto key = MakeTestKey(1);
  const std::array pubkeys = {key.public_key};
  const auto locking_script = MakeCheckMultiSigLockingScript(1, pubkeys);
  const std::array signatures = {std::vector<uint8_t>{0x01}};

  Transaction tx = MakeLegacyCheckSigSpendTx();
  script::SpendContext spend{tx, 0, script::SpendPath::LegacyDirect};
  const auto unlocking_script = MakeCheckMultiSigUnlockingScript(signatures);
  tx.SetSignatureScript(0, unlocking_script);

  auto processor = MakeLegacyProcessor(spend, false);
  ASSERT_TRUE(*processor.Run(unlocking_script));
  const auto lock_result = processor.Run(locking_script);
  ASSERT_TRUE(lock_result);
  EXPECT_FALSE(*lock_result);
}

TEST(SigOpsTest, CheckMultiSigRejectsNonNullDummyWhenEnabled) {
  const auto key = MakeTestKey(1);
  const std::array pubkeys = {key.public_key};
  const auto locking_script = MakeCheckMultiSigLockingScript(1, pubkeys);

  Transaction tx = MakeLegacyCheckSigSpendTx();
  script::SpendContext spend{tx, 0, script::SpendPath::LegacyDirect};
  const std::array signatures = {MakeLegacySignature(spend, locking_script, key.private_key, secp256k1::Wide{1})};
  const auto unlocking_script = MakeCheckMultiSigUnlockingScript(signatures, std::array<uint8_t, 1>{0x01});
  tx.SetSignatureScript(0, unlocking_script);

  auto processor = MakeLegacyProcessor(spend, false, true);
  ASSERT_TRUE(*processor.Run(unlocking_script));
  EXPECT_EQ(processor.Run(locking_script), lang::Error::SigNullDummy);
}

TEST(SigOpsTest, CheckMultiSigAllowsNonNullDummyWhenDisabled) {
  const auto key = MakeTestKey(1);
  const std::array pubkeys = {key.public_key};
  const auto locking_script = MakeCheckMultiSigLockingScript(1, pubkeys);

  Transaction tx = MakeLegacyCheckSigSpendTx();
  script::SpendContext spend{tx, 0, script::SpendPath::LegacyDirect};
  const std::array signatures = {MakeLegacySignature(spend, locking_script, key.private_key, secp256k1::Wide{1})};
  const auto unlocking_script = MakeCheckMultiSigUnlockingScript(signatures, std::array<uint8_t, 1>{0x01});
  tx.SetSignatureScript(0, unlocking_script);

  auto processor = MakeLegacyProcessor(spend, false, false);
  ASSERT_TRUE(*processor.Run(unlocking_script));
  const auto lock_result = processor.Run(locking_script);
  ASSERT_TRUE(lock_result);
  EXPECT_TRUE(*lock_result);
}

TEST(SigOpsTest, CheckMultiSigAcceptsZeroOfOneSpend) {
  const auto key = MakeTestKey(1);
  const std::array pubkeys = {key.public_key};
  const auto locking_script = MakeCheckMultiSigLockingScript(0, pubkeys);
  const std::span<const std::vector<uint8_t>> signatures;

  Transaction tx = MakeLegacyCheckSigSpendTx();
  script::SpendContext spend{tx, 0, script::SpendPath::LegacyDirect};
  const auto unlocking_script = MakeCheckMultiSigUnlockingScript(signatures);
  tx.SetSignatureScript(0, unlocking_script);

  auto processor = MakeLegacyProcessor(spend);
  ASSERT_TRUE(processor.Run(unlocking_script));
  const auto lock_result = processor.Run(locking_script);
  ASSERT_TRUE(lock_result);
  EXPECT_TRUE(*lock_result);
}

TEST(SigOpsTest, CheckMultiSigRejectsTooManyPubKeys) {
  const auto script = Writer{}.PushInt(21).Then(Op::CheckMultiSig).Release();

  script::Processor processor{MakeLegacyPolicy()};
  EXPECT_EQ(processor.Run(script), lang::Error::MultiSigKeyCount);
}

TEST(SigOpsTest, CheckMultiSigRejectsMoreSignaturesThanPubKeys) {
  const auto script = Writer{}
                          .PushInt(0)
                          .PushInt(1)
                          .PushInt(1)
                          .PushInt(2)
                          .PushInt(1)
                          .PushInt(1)
                          .Then(Op::CheckMultiSig)
                          .Release();

  script::Processor processor{MakeLegacyPolicy()};
  EXPECT_EQ(processor.Run(script), lang::Error::MultiSigSigCount);
}

}  // namespace
}  // namespace hornet::protocol::script::runtime::ops