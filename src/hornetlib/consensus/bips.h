// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <map>

#include "hornetlib/util/assert.h"

namespace hornet::consensus {

// clang-format off

// Bitcoin Improvement Proposals by name.
enum class BIP {
   HeightInCoinbase    =  34,  // BIP34:  Block v2, embeds height in coinbase, March 2013.
   CheckLockTimeVerify =  65,  // BIP65:  CHECKLOCKTIMEVERIFY (absolute locktime opcode), December 2015.
   StrictDERSignatures =  66,  // BIP66:  Strict DER signature encoding, July 2015.
   LockTimeMedianPast  = 113,  // BIP113: Locktime uses Median Time Past (MTP), July 2016.
   SegWit              = 141,  // BIP141: Segregated Witness (SegWit), August 2017.
};

// Bitcoin Improvement Proposals by number.
inline constexpr BIP BIP34  = BIP::HeightInCoinbase;
inline constexpr BIP BIP65  = BIP::CheckLockTimeVerify;
inline constexpr BIP BIP66  = BIP::StrictDERSignatures;
inline constexpr BIP BIP113 = BIP::LockTimeMedianPast;
inline constexpr BIP BIP141 = BIP::SegWit;

// Returns true if the specified BIP is enabled at the given block height.
inline bool IsBIPEnabledAtHeight(BIP bip, int height) {
  static const std::map<BIP, int> kBIPActivationHeights = {
    {BIP::HeightInCoinbase,     227'931},
    {BIP::CheckLockTimeVerify,  388'381},
    {BIP::StrictDERSignatures,  363'725},
    {BIP::LockTimeMedianPast,   419'328},
    {BIP::SegWit,               481'824}
  };
  Assert(kBIPActivationHeights.contains(bip));
  return height >= kBIPActivationHeights.at(bip);  // Throws std::out_of_range if not found.
}

// clang-format on

}  // namespace hornet::consensus
