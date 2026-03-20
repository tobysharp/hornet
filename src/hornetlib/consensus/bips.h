// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <limits>

#include "hornetlib/util/assert.h"

namespace hornet::consensus {

// clang-format off

// Bitcoin Improvement Proposals by name.
enum class BIP {
   HeightInCoinbase    =  34,  // BIP34:  Block v2, embeds height in coinbase, March 2013.
   CheckLockTimeVerify =  65,  // BIP65:  CHECKLOCKTIMEVERIFY (absolute locktime opcode), December 2015.
   StrictDERSignatures =  66,  // BIP66:  Strict DER signature encoding, July 2015.
   SequenceLocks       =  68,  // BIP68:  Relative locktime via nSequence, July 2016.
   CheckSequenceVerify = 112,  // BIP112: OP_CHECKSEQUENCEVERIFY opcode, July 2016.
   LockTimeMedianPast  = 113,  // BIP113: Locktime uses Median Time Past (MTP), July 2016.
   SegWit              = 141,  // BIP141: Segregated Witness (SegWit), August 2017.
};

// Bitcoin Improvement Proposals by number.
inline constexpr BIP BIP34  = BIP::HeightInCoinbase;
inline constexpr BIP BIP65  = BIP::CheckLockTimeVerify;
inline constexpr BIP BIP66  = BIP::StrictDERSignatures;
inline constexpr BIP BIP68  = BIP::SequenceLocks;
inline constexpr BIP BIP112 = BIP::CheckSequenceVerify;
inline constexpr BIP BIP113 = BIP::LockTimeMedianPast;
inline constexpr BIP BIP141 = BIP::SegWit;

inline constexpr int GetSoftForkActivationHeight(BIP bip) {
  switch (bip) {
    case BIP::HeightInCoinbase     : return 227'931;
    case BIP::CheckLockTimeVerify  : return 388'381;
    case BIP::StrictDERSignatures  : return 363'725;
    case BIP::SequenceLocks        : // CSV deployment: BIPs 68, 112, 113.
    case BIP::CheckSequenceVerify  :
    case BIP::LockTimeMedianPast   : return 419'328;
    case BIP::SegWit               : return 481'824;  
  }
  Assert(false);
  return std::numeric_limits<int>::max();
}

// Returns true if the specified BIP is active at the given block height.
inline constexpr bool IsBIPActiveAtHeight(BIP bip, int height) {
  return height >= GetSoftForkActivationHeight(bip);
}

// clang-format on

}  // namespace hornet::consensus
