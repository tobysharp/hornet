// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include <algorithm>
#include <array>
#include <span>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "hornetlib/consensus/rules/validate_spending.h"
#include "hornetlib/crypto/curve.h"
#include "hornetlib/crypto/signature.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/runtime/signing.h"
#include "hornetlib/protocol/script/spend.h"
#include "hornetlib/protocol/script/writer.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/assert.h"
#include "hornetlib/util/hex.h"

namespace hornet::consensus::rules {
namespace {

using secp256k1 = crypto::ecdsa::secp256k1;
using protocol::script::SpendContext;
using protocol::script::SpendPath;
using protocol::script::Writer;
using protocol::script::lang::Op;

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

std::vector<uint8_t> EncodeDerSignature(const secp256k1::Signature& signature, uint8_t sighash_type = 0x01) {
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

std::vector<uint8_t> MakeP2PKLockingScript(std::span<const uint8_t> pubkey) {
  return Writer{}.PushData(pubkey).Then(Op::CheckSig).Release();
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

struct SignedDigest {
  secp256k1::Signature signature;
  std::vector<uint8_t> encoded_signature;
  protocol::Hash digest;
};

SignedDigest MakeSignature(const SpendContext& spend, std::span<const uint8_t> locking_script) {

  static constexpr std::array<uint8_t, 1> kSigHashAll = {0x01};
  const auto digest = protocol::script::runtime::BuildSpendDigest(spend, kSigHashAll, locking_script);

  const secp256k1::Wide private_key{1};
  const secp256k1::Wide nonce{1};
  const secp256k1::Point nonce_point = nonce * secp256k1::G;
  const secp256k1::Mod_n r{nonce_point.NormalizedX().x.Modulo(crypto::ecdsa::constants::n)};
  const secp256k1::Mod_n z{secp256k1::Wide::FromBigEndianBytes(digest)};
  const secp256k1::Mod_n d{private_key};
  const secp256k1::Mod_n s = (z + r * d) / secp256k1::Mod_n{nonce};

  const secp256k1::Signature signature{r.x, s.x};
  return {signature, EncodeDerSignature(signature, kSigHashAll.front()), digest};
}

SpendRecord MakeSpendRecord(std::span<const uint8_t> pubkey_script) {
  return {.funding_height = 1,
          .funding_flags = 0,
          .amount = 50'000'000,
          .pubkey_script = pubkey_script,
          .spend_input_index = 0};
}

struct BenchSpend {
  protocol::Transaction tx;
  std::vector<uint8_t> locking_script;
  SpendRecord spend;
  int height = 2;

  [[nodiscard]] InputSpendContext Context() const { return {tx, spend, height}; }
};

const secp256k1::Point& PublicKeyPoint(const secp256k1::PublicKey& public_key) {
  return static_cast<const secp256k1::Point&>(public_key);
}

secp256k1::PublicKey ParseCompressedPublicKey() {
  const auto public_key = secp256k1::PublicKeyFromSEC1(kCompressedPubkey);
  hornet::Assert(public_key.has_value());
  return *public_key;
}

struct VerifySignatureBenchCase {
  secp256k1::PublicKey public_key;
  secp256k1::Signature signature;
  protocol::Hash digest;
};

struct VerifySignatureSEC1BenchCase {
  std::span<const uint8_t> pubkey_bytes;
  secp256k1::Signature signature;
  protocol::Hash digest;
};

struct CheckSigBenchCase {
  protocol::Transaction tx;
  std::vector<uint8_t> locking_script;
  std::vector<uint8_t> encoded_signature;
};

BenchSpend MakeP2PKBenchSpend() {
  BenchSpend bench_spend{MakeLegacySpendTx(), {}, {}, 2};
  bench_spend.locking_script = MakeP2PKLockingScript(kCompressedPubkey);

  const SpendContext spend_context{bench_spend.tx, 0, SpendPath::LegacyDirect};
  const auto signed_digest = MakeSignature(spend_context, bench_spend.locking_script);
  bench_spend.tx.SetSignatureScript(0, Writer{}.PushData(signed_digest.encoded_signature).Release());
  bench_spend.spend = MakeSpendRecord(bench_spend.locking_script);
  return bench_spend;
}

BenchSpend MakeP2PKHBenchSpend() {
  BenchSpend bench_spend{MakeLegacySpendTx(), {}, {}, 2};
  bench_spend.locking_script = MakeP2PKHLockingScript(kCompressedPubkeyHash);

  const SpendContext spend_context{bench_spend.tx, 0, SpendPath::LegacyDirect};
  const auto signed_digest = MakeSignature(spend_context, bench_spend.locking_script);
  bench_spend.tx.SetSignatureScript(0,
                                    Writer{}.PushData(signed_digest.encoded_signature).PushData(kCompressedPubkey).Release());
  bench_spend.spend = MakeSpendRecord(bench_spend.locking_script);
  return bench_spend;
}

VerifySignatureBenchCase MakeVerifySignatureBenchCase() {
  const auto locking_script = MakeP2PKHLockingScript(kCompressedPubkeyHash);
  const protocol::Transaction tx = MakeLegacySpendTx();
  const SpendContext spend_context{tx, 0, SpendPath::LegacyDirect};
  const auto signed_digest = MakeSignature(spend_context, locking_script);
  return {ParseCompressedPublicKey(), signed_digest.signature, signed_digest.digest};
}

VerifySignatureSEC1BenchCase MakeVerifySignatureSEC1BenchCase() {
  const auto locking_script = MakeP2PKHLockingScript(kCompressedPubkeyHash);
  const protocol::Transaction tx = MakeLegacySpendTx();
  const SpendContext spend_context{tx, 0, SpendPath::LegacyDirect};
  const auto signed_digest = MakeSignature(spend_context, locking_script);
  return {kCompressedPubkey, signed_digest.signature, signed_digest.digest};
}

CheckSigBenchCase MakeCheckSigBenchCase() {
  CheckSigBenchCase bench_case{MakeLegacySpendTx(), {}, {}};
  bench_case.locking_script = MakeP2PKHLockingScript(kCompressedPubkeyHash);
  const SpendContext spend_context{bench_case.tx, 0, SpendPath::LegacyDirect};
  const auto signed_digest = MakeSignature(spend_context, bench_case.locking_script);
  bench_case.encoded_signature = signed_digest.encoded_signature;
  return bench_case;
}

static void BM_ValidateScripts_P2PK(benchmark::State& state) {
  const BenchSpend bench_spend = MakeP2PKBenchSpend();
  if (!ValidateScripts(bench_spend.Context())) {
    state.SkipWithError("failed to build valid P2PK spend");
    return;
  }

  for (auto _ : state) {
    benchmark::DoNotOptimize(static_cast<bool>(ValidateScripts(bench_spend.Context())));
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel("full legacy P2PK validation");
}

static void BM_ValidateScripts_P2PKH(benchmark::State& state) {
  const BenchSpend bench_spend = MakeP2PKHBenchSpend();
  if (!ValidateScripts(bench_spend.Context())) {
    state.SkipWithError("failed to build valid P2PKH spend");
    return;
  }

  for (auto _ : state) {
    benchmark::DoNotOptimize(static_cast<bool>(ValidateScripts(bench_spend.Context())));
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel("full legacy P2PKH validation");
}

static void BM_VerifySignature_Secp256k1(benchmark::State& state) {
  const VerifySignatureBenchCase bench_case = MakeVerifySignatureBenchCase();
  if (!secp256k1::VerifySignature(bench_case.public_key, bench_case.signature, bench_case.digest)) {
    state.SkipWithError("failed to build valid secp256k1 verification case");
    return;
  }

  for (auto _ : state) {
    benchmark::DoNotOptimize(secp256k1::VerifySignature(bench_case.public_key, bench_case.signature, bench_case.digest));
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel("secp256k1 signature verification only");
}

static void BM_VerifySignature_FromSEC1_Secp256k1(benchmark::State& state) {
  const VerifySignatureSEC1BenchCase bench_case = MakeVerifySignatureSEC1BenchCase();
  const auto public_key = secp256k1::PublicKeyFromSEC1(bench_case.pubkey_bytes);
  if (!public_key || !secp256k1::VerifySignature(*public_key, bench_case.signature, bench_case.digest)) {
    state.SkipWithError("failed to build valid secp256k1 SEC1 verification case");
    return;
  }

  for (auto _ : state) {
    const auto loop_public_key = secp256k1::PublicKeyFromSEC1(bench_case.pubkey_bytes);
    benchmark::DoNotOptimize(
        secp256k1::VerifySignature(*loop_public_key, bench_case.signature, bench_case.digest));
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel("secp256k1 SEC1 decode plus signature verification");
}

static void BM_BuildSpendDigest_P2PKH(benchmark::State& state) {
  const CheckSigBenchCase bench_case = MakeCheckSigBenchCase();
  const SpendContext spend_context{bench_case.tx, 0, SpendPath::LegacyDirect};
  const auto digest = protocol::script::runtime::BuildSpendDigest(
      spend_context, bench_case.encoded_signature, bench_case.locking_script);
  auto digest_sink = digest.front();
  benchmark::DoNotOptimize(std::move(digest_sink));

  for (auto _ : state) {
    const auto loop_digest = protocol::script::runtime::BuildSpendDigest(
        spend_context, bench_case.encoded_signature, bench_case.locking_script);
    auto loop_digest_sink = loop_digest.front();
    benchmark::DoNotOptimize(std::move(loop_digest_sink));
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel("legacy P2PKH spend digest only");
}

static void BM_ParseSignatureDER_Lax(benchmark::State& state) {
  const CheckSigBenchCase bench_case = MakeCheckSigBenchCase();
  const auto signature = crypto::ecdsa::ParseSignatureDER<secp256k1::Wide>(
      std::span<const uint8_t>{bench_case.encoded_signature}.first(bench_case.encoded_signature.size() - 1),
      crypto::ecdsa::DERParseType::Lax);
  if (!signature) {
    state.SkipWithError("failed to build valid DER signature input");
    return;
  }
  auto signature_sink = signature->first.Words()[0];
  benchmark::DoNotOptimize(std::move(signature_sink));

  for (auto _ : state) {
    const auto loop_signature = crypto::ecdsa::ParseSignatureDER<secp256k1::Wide>(
        std::span<const uint8_t>{bench_case.encoded_signature}.first(bench_case.encoded_signature.size() - 1),
        crypto::ecdsa::DERParseType::Lax);
    auto loop_signature_sink = loop_signature->first.Words()[0];
    benchmark::DoNotOptimize(std::move(loop_signature_sink));
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel("lax DER signature parse only");
}

static void BM_PublicKeyFromSEC1_Compressed(benchmark::State& state) {
  const auto pubkey = secp256k1::PublicKeyFromSEC1(kCompressedPubkey);
  if (!pubkey) {
    state.SkipWithError("failed to build valid SEC1 pubkey input");
    return;
  }
  auto pubkey_sink = PublicKeyPoint(*pubkey).NormalizedX().x.Words()[0];
  benchmark::DoNotOptimize(std::move(pubkey_sink));

  for (auto _ : state) {
    const auto loop_pubkey = secp256k1::PublicKeyFromSEC1(kCompressedPubkey);
    auto loop_pubkey_sink = PublicKeyPoint(*loop_pubkey).NormalizedX().x.Words()[0];
    benchmark::DoNotOptimize(std::move(loop_pubkey_sink));
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel("compressed SEC1 pubkey decode only");
}

static void BM_CheckSigSetup_P2PKH(benchmark::State& state) {
  const CheckSigBenchCase bench_case = MakeCheckSigBenchCase();
  const SpendContext spend_context{bench_case.tx, 0, SpendPath::LegacyDirect};
  const auto pubkey = secp256k1::PublicKeyFromSEC1(kCompressedPubkey);
  const auto signature = crypto::ecdsa::ParseSignatureDER<secp256k1::Wide>(
      std::span<const uint8_t>{bench_case.encoded_signature}.first(bench_case.encoded_signature.size() - 1),
      crypto::ecdsa::DERParseType::Lax);
  const auto digest = protocol::script::runtime::BuildSpendDigest(
      spend_context, bench_case.encoded_signature, bench_case.locking_script);
  if (!pubkey || !signature) {
    state.SkipWithError("failed to build valid CheckSig setup inputs");
    return;
  }
  auto setup_digest_sink = digest.front();
  benchmark::DoNotOptimize(std::move(setup_digest_sink));

  for (auto _ : state) {
    const auto loop_digest = protocol::script::runtime::BuildSpendDigest(
        spend_context, bench_case.encoded_signature, bench_case.locking_script);
    const auto loop_signature = crypto::ecdsa::ParseSignatureDER<secp256k1::Wide>(
        std::span<const uint8_t>{bench_case.encoded_signature}.first(bench_case.encoded_signature.size() - 1),
        crypto::ecdsa::DERParseType::Lax);
    const auto loop_pubkey = secp256k1::PublicKeyFromSEC1(kCompressedPubkey);
    auto loop_digest_sink = loop_digest.front();
    auto loop_signature_sink = loop_signature->first.Words()[0];
    auto loop_pubkey_sink = PublicKeyPoint(*loop_pubkey).NormalizedX().x.Words()[0];
    benchmark::DoNotOptimize(std::move(loop_digest_sink));
    benchmark::DoNotOptimize(std::move(loop_signature_sink));
    benchmark::DoNotOptimize(std::move(loop_pubkey_sink));
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel("P2PKH CheckSig setup without verification");
}

BENCHMARK(BM_ValidateScripts_P2PK);
BENCHMARK(BM_ValidateScripts_P2PKH);
BENCHMARK(BM_VerifySignature_Secp256k1);
BENCHMARK(BM_VerifySignature_FromSEC1_Secp256k1);
BENCHMARK(BM_BuildSpendDigest_P2PKH);
BENCHMARK(BM_ParseSignatureDER_Lax);
BENCHMARK(BM_PublicKeyFromSEC1_Compressed);
BENCHMARK(BM_CheckSigSetup_P2PKH);

}  // namespace
}  // namespace hornet::consensus::rules

BENCHMARK_MAIN();