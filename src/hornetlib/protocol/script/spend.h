#pragma once

#include "hornetlib/protocol/transaction.h"

namespace hornet::protocol::script {

// The type of locking script associated with a transaction output.
enum class LockingTemplate {
  GeneralScript,  // A directly executed legacy locking script, including P2PK and P2PKH forms.
  P2SH,           // Pay to script hash.
  P2WPKH,         // Pay to witness public key hash.
  P2WSH,          // Pay to witness script hash.
  P2TR,           // Pay to Taproot.
  NonSpendable    // A non spendable output, e.g. Op::Return.
};

// The spend path is determined from both an input's unlocking script, and its previous output's locking script.
enum class SpendPath {
  LegacyDirect,  // Including P2PK, P2PKH and general scripts.
  P2SH_Legacy,   // P2SH redeem script executed under legacy rules.
  P2SH_P2WPKH,   // P2SH-P2WPKH
  P2SH_P2WSH,    // P2SH-P2WSH
  P2WPKH,        // P2WPKH
  P2WSH,         // P2WSH
  P2TR_Key,      // Taproot key-path spend.
  P2TR_Script    // Taproot script-path spend (Tapscript).
};

// The validation mode to use when validating a spend, which determines the applicable consensus rules.
enum class SpendValidationMode { Legacy, SegwitV0, TaprootKeyPath, Tapscript };

// Contextual information about the spending transaction and input, used by sig opcodes.
struct SpendContext {
  TransactionConstView tx;                   // The spending transaction.
  int input_index;                           // The index of the spending input.
  SpendPath path = SpendPath::LegacyDirect;  // The spend path of the input.
  // Maybe: amount, locking script, funding height, flags, etc.
  const Input& Input() const { return tx.Input(input_index); }
};

inline constexpr SpendValidationMode GetSpendValidationMode(SpendPath path) {
  switch (path) {
    case SpendPath::LegacyDirect:
    case SpendPath::P2SH_Legacy:
      return SpendValidationMode::Legacy;
    case SpendPath::P2SH_P2WPKH:
    case SpendPath::P2SH_P2WSH:
    case SpendPath::P2WPKH:
    case SpendPath::P2WSH:
      return SpendValidationMode::SegwitV0;
    case SpendPath::P2TR_Key:
      return SpendValidationMode::TaprootKeyPath;
    case SpendPath::P2TR_Script:
      return SpendValidationMode::Tapscript;
  }
}

constexpr decltype(auto) VisitSpendValidationMode(SpendValidationMode mode, auto&& fn) {
  switch (mode) {
    case SpendValidationMode::Legacy:
      return fn.template operator()<SpendValidationMode::Legacy>();
    case SpendValidationMode::SegwitV0:
      return fn.template operator()<SpendValidationMode::SegwitV0>();
    case SpendValidationMode::TaprootKeyPath:
      return fn.template operator()<SpendValidationMode::TaprootKeyPath>();
    case SpendValidationMode::Tapscript:
      return fn.template operator()<SpendValidationMode::Tapscript>();
  }
}


}  // namespace hornet::protocol::script
