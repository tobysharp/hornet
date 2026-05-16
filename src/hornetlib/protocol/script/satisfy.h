#pragma once

#include <cstdint>
#include <expected>

#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/expected.h"

namespace hornet::protocol::script {

struct PrevoutData {
  Script pubkey_script;
  int64_t amount;
  int funding_height;
  bool is_coinbase;
};

struct SpendData {
  TransactionConstView tx;
  int input_index;
  PrevoutData prevout;

  [[nodiscard]] Script SignatureScript() const { return tx.SignatureScript(input_index); }
  [[nodiscard]] bool IsCoinbase() const { return prevout.is_coinbase; }
};

enum class Feature : uint32_t { 
  // Spend path features
  P2SH,                 // BIP16
  Witness,              // BIP141
  Taproot,              // 
  
  // Execution semantics features
  StrictDER,            // BIP66: Strict DER signatures
  CheckLockTimeVerify,  // BIP65
  CheckSequenceVerify,  // BIP112
  NullDummy             // BIP147
};

class FeatureFlags {
 public:
  constexpr FeatureFlags() = default;
  constexpr FeatureFlags(Feature feature) : flags_(Mask(feature)) {}
  constexpr FeatureFlags(std::initializer_list<Feature> features) : flags_(0) {
    for (Feature feature : features) flags_ |= Mask(feature);
  }
  [[nodiscard]] constexpr bool Has(Feature feature) const { return (flags_ & Mask(feature)) != 0; }
  constexpr FeatureFlags& Add(Feature feature) {
    flags_ |= Mask(feature);
    return *this;
  }

 private:
  static constexpr uint8_t Mask(Feature feature) {
    return static_cast<uint8_t>(1u << static_cast<uint8_t>(feature));
  }

  uint32_t flags_ = 0;
};

using SatisfyResult = util::Expected<bool, lang::Error>;

SatisfyResult SatisfiesLockingScript(const SpendData& spend, FeatureFlags features = {});

}  // namespace hornet::protocol::script
