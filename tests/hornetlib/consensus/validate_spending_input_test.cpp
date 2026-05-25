#include "hornetlib/consensus/rules/validate_spending.h"

#include <algorithm>
#include <array>
#include <span>
#include <vector>

#include "hornetlib/crypto/curve.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/protocol/script/runtime/signing.h"
#include "hornetlib/protocol/script/writer.h"
#include "hornetlib/util/hex.h"

#include <gtest/gtest.h>

namespace hornet::consensus::rules {
namespace {

using protocol::script::Writer;
using protocol::script::lang::Op;
using secp256k1 = crypto::ecdsa::secp256k1;

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

std::vector<uint8_t> OverpadDerSignatureR(std::vector<uint8_t> signature) {
  EXPECT_GE(signature.size(), 7u);
  EXPECT_EQ(signature[0], 0x30);
  EXPECT_EQ(signature[2], 0x02);
  ++signature[1];
  ++signature[3];
  signature.insert(signature.begin() + 4, 0x00);
  return signature;
}

protocol::Transaction MakeLegacySpendTx() {
  protocol::Transaction tx;
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

std::vector<uint8_t> MakeP2PKHLockingScript(std::span<const uint8_t> pubkey_hash) {
  return Writer{}
      .Then(Op::Duplicate)
      .Then(Op::Hash160)
      .PushData(pubkey_hash)
      .Then(Op::EqualVerify)
      .Then(Op::CheckSig)
      .Release();
}

std::vector<uint8_t> MakeCheckMultiSigLockingScript(int sig_count, std::span<const uint8_t> pubkey) {
  return Writer{}
      .PushInt(sig_count)
      .PushData(pubkey)
      .PushInt(1)
      .Then(Op::CheckMultiSig)
      .Release();
}

std::vector<uint8_t> MakeCheckMultiSigUnlockingScript(std::span<const uint8_t> signature,
                                                      std::span<const uint8_t> dummy = {}) {
  return Writer{}.PushData(dummy).PushData(signature).Release();
}

std::vector<uint8_t> MakeLegacySignature(const protocol::script::SpendContext& spend,
                                         std::span<const uint8_t> locking_script,
                                         const secp256k1::Wide& private_key = secp256k1::Wide{1},
                                         const secp256k1::Wide& nonce = secp256k1::Wide{1}) {
  static constexpr std::array<uint8_t, 1> kSigHashAll = {0x01};
  const auto digest = protocol::script::runtime::BuildSpendDigest(spend, kSigHashAll, locking_script);

  const secp256k1::Point nonce_point = nonce * secp256k1::G;
  const secp256k1::Mod_n r{nonce_point.x.x.Modulo(crypto::ecdsa::constants::n)};
  const secp256k1::Mod_n z{secp256k1::Wide::FromBigEndianBytes(digest)};
  const secp256k1::Mod_n d{private_key};
  const secp256k1::Mod_n s = (z + r * d) / secp256k1::Mod_n{nonce};

  return EncodeDerSignature({r.x, s.x}, kSigHashAll.front());
}

std::vector<uint8_t> MakeP2PKHSignature(const protocol::script::SpendContext& spend, std::span<const uint8_t> locking_script) {
  return MakeLegacySignature(spend, locking_script);
}

SpendRecord MakeSpendRecord(std::span<const uint8_t> pubkey_script) {
  return {.funding_height = 1,
          .funding_flags = 0,
          .amount = 50'000'000,
          .pubkey_script = pubkey_script,
          .spend_input_index = 0};
}

TEST(ValidateSpendingInputTest, EnforcesCoinbaseMaturityBoundary) {
  protocol::Transaction funding_tx;
  const protocol::TransactionConstView tx = funding_tx;
  SpendRecord spend{
      .funding_height = 1000, .funding_flags = 1, .amount = 50'000'000, .pubkey_script = {}, .spend_input_index = 0};

  EXPECT_EQ(ValidateCoinbaseMaturity(InputSpendContext{tx, spend, 1099}),
            Error::Spending_PrematureSpend);
  EXPECT_TRUE(ValidateCoinbaseMaturity(InputSpendContext{tx, spend, 1100}));
}

TEST(ValidateSpendingInputTest, AcceptsValidP2PKHSpend) {
  const auto locking_script = MakeP2PKHLockingScript(kCompressedPubkeyHash);
  protocol::Transaction tx = MakeLegacySpendTx();

  protocol::script::SpendContext spend_context{tx, 0, protocol::script::SpendPath::LegacyDirect};
  const auto signature = MakeP2PKHSignature(spend_context, locking_script);
  tx.SetSignatureScript(0, Writer{}.PushData(signature).PushData(kCompressedPubkey).Release());

  const SpendRecord spend = MakeSpendRecord(locking_script);
  EXPECT_EQ(ValidateScripts(InputSpendContext{tx, spend, 2}), Result{});
}

TEST(ValidateSpendingInputTest, RejectsP2PKHSpendWhenPubkeyHashDoesNotMatch) {
  constexpr auto wrong_hash = "0000000000000000000000000000000000000000"_bytes;
  const auto locking_script = MakeP2PKHLockingScript(wrong_hash);
  protocol::Transaction tx = MakeLegacySpendTx();

  protocol::script::SpendContext spend_context{tx, 0, protocol::script::SpendPath::LegacyDirect};
  const auto signature = MakeP2PKHSignature(spend_context, locking_script);
  tx.SetSignatureScript(0, Writer{}.PushData(signature).PushData(kCompressedPubkey).Release());

  const SpendRecord spend = MakeSpendRecord(locking_script);
  EXPECT_EQ(ValidateScripts(InputSpendContext{tx, spend, 2}), Error::Spending_ScriptLocked);
}

TEST(ValidateSpendingInputTest, RejectsP2PKHSpendWhenSignatureDoesNotValidate) {
  const auto locking_script = MakeP2PKHLockingScript(kCompressedPubkeyHash);
  protocol::Transaction tx = MakeLegacySpendTx();

  protocol::script::SpendContext spend_context{tx, 0, protocol::script::SpendPath::LegacyDirect};
  auto signature = MakeP2PKHSignature(spend_context, locking_script);
  signature[10] ^= 0x01;
  tx.SetSignatureScript(0, Writer{}.PushData(signature).PushData(kCompressedPubkey).Release());

  const SpendRecord spend = MakeSpendRecord(locking_script);
  EXPECT_EQ(ValidateScripts(InputSpendContext{tx, spend, 2}), Error::Spending_ScriptLocked);
}

TEST(ValidateSpendingInputTest, LegacyLockScriptCannotReadAltStackFromUnlockingScript) {
  const auto locking_script = Writer{}.Then(Op::FromAltStack).Release();
  protocol::Transaction tx = MakeLegacySpendTx();
  tx.SetSignatureScript(0, Writer{}.PushInt(1).Then(Op::ToAltStack).Release());

  const SpendRecord spend = MakeSpendRecord(locking_script);
  EXPECT_EQ(ValidateScripts(InputSpendContext{tx, spend, 2}), Error::Spending_ScriptLocked);
}

TEST(ValidateSpendingInputTest, RejectsDisabledOpcodeInUnexecutedBranch) {
  const std::vector<uint8_t> locking_script = {
      ToByte(Op::PushEmpty),
      ToByte(Op::If),
      0x7e,  // OP_CAT
      ToByte(Op::EndIf),
      ToByte(Op::PushConst1),
  };
  protocol::Transaction tx = MakeLegacySpendTx();

  const SpendRecord spend = MakeSpendRecord(locking_script);
  EXPECT_EQ(ValidateScripts(InputSpendContext{tx, spend, 2}), Error::Spending_ScriptLocked);
}

TEST(ValidateSpendingInputTest, RejectsExecutedReservedOpcode) {
  const std::vector<uint8_t> locking_script = {
      0x50,  // OP_RESERVED
  };
  protocol::Transaction tx = MakeLegacySpendTx();

  const SpendRecord spend = MakeSpendRecord(locking_script);
  EXPECT_EQ(ValidateScripts(InputSpendContext{tx, spend, 2}), Error::Spending_ScriptLocked);
}

TEST(ValidateSpendingInputTest, CheckMultiSigEnforcesStrictDERWhenFeatureEnabled) {
  const auto locking_script = MakeCheckMultiSigLockingScript(1, kCompressedPubkey);
  protocol::Transaction tx = MakeLegacySpendTx();

  protocol::script::SpendContext spend_context{tx, 0, protocol::script::SpendPath::LegacyDirect};
  auto signature = MakeLegacySignature(spend_context, locking_script);
  signature = OverpadDerSignatureR(std::move(signature));
  tx.SetSignatureScript(0, MakeCheckMultiSigUnlockingScript(signature));

  const protocol::script::FeatureFlags strict_der_flags{protocol::script::Feature::StrictDER};
  const SpendRecord spend = MakeSpendRecord(locking_script);
  EXPECT_EQ(ValidateScripts(InputSpendContext{tx, spend, 2}), Result{});
  EXPECT_EQ(ValidateScripts(InputSpendContext{tx, spend, 2, strict_der_flags}), Error::Spending_ScriptLocked);
}

TEST(ValidateSpendingInputTest, CheckMultiSigEnforcesNullDummyWhenFeatureEnabled) {
  const auto locking_script = MakeCheckMultiSigLockingScript(1, kCompressedPubkey);
  protocol::Transaction tx = MakeLegacySpendTx();

  protocol::script::SpendContext spend_context{tx, 0, protocol::script::SpendPath::LegacyDirect};
  const auto signature = MakeLegacySignature(spend_context, locking_script);
  tx.SetSignatureScript(0, MakeCheckMultiSigUnlockingScript(signature, std::array<uint8_t, 1>{0x01}));

  const protocol::script::FeatureFlags null_dummy_flags{protocol::script::Feature::NullDummy};
  const SpendRecord spend = MakeSpendRecord(locking_script);
  EXPECT_EQ(ValidateScripts(InputSpendContext{tx, spend, 2}), Result{});
  EXPECT_EQ(ValidateScripts(InputSpendContext{tx, spend, 2, null_dummy_flags}), Error::Spending_ScriptLocked);
}

}  // namespace
}  // namespace hornet::consensus::rules
