// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <array>

#include "hornetlib/consensus/bips.h"
#include "hornetlib/consensus/difficulty_adjustment.h"
#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/consensus/rule.h"
#include "hornetlib/consensus/rules/context.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/compact_target.h"
#include "hornetlib/protocol/hash.h"

namespace hornet::consensus::rules {

namespace detail {
inline bool IsVersionValidAtHeight(int32_t version, int height) {
  constexpr std::array<BIP, 4> kVersionExpiryToBIP = {
      BIP34, BIP34,  // v0, v1 retired with BIP34.
      BIP66, BIP65   // v2, v3 retired with BIP66, BIP65.
  };
  if (version >= std::ssize(kVersionExpiryToBIP)) return true;
  const int index = std::max(0, version);
  return !IsBIPEnabledAtHeight(kVersionExpiryToBIP[index], height);
}
}  // namespace detail

// A header MUST reference the hash of its valid parent.
[[nodiscard]] inline Result ValidatePreviousHash(
    const HeaderValidationContext& context) {
  if (context.parent.ComputeHash() != context.header.GetPreviousBlockHash())
    return Error::Header_ParentNotFound;
  return {};
}

// A header's 256-bit hash value MUST NOT exceed the header's proof-of-work target.
[[nodiscard]] inline Result ValidateProofOfWork(
    const HeaderValidationContext& context) {
  const auto hash = context.header.ComputeHash();
  const auto target = context.header.GetCompactTarget().Expand();
  if (!(hash <= target)) return Error::Header_InvalidProofOfWork;
  return {};
}

// A header's proof-of-work target MUST satisfy the difficulty adjustment formula.
[[nodiscard]] inline Result ValidateDifficultyAdjustment(
    const HeaderValidationContext& context) {
  if (context.header.GetCompactTarget() !=
      AdjustCompactTarget(context.height, context.parent, context.view))
    return Error::Header_BadDifficultyTransition;
  return {};
}

// A header timestamp MUST be strictly greater than the median of its 11 ancestors' timestamps.
[[nodiscard]] inline Result ValidateMedianTimePast(
    const HeaderValidationContext& context) {
  if (context.header.GetTimestamp() <= context.view.MedianTimePast())
    return Error::Header_BadTimestamp;
  return {};
}

// A header timestamp MUST be less than or equal to network-adjusted time plus 2 hours.
[[nodiscard]] inline Result ValidateTimestampCurrent(
    const HeaderValidationContext& context) {
  constexpr int kTimestampTolerance = 2 * 60 * 60;
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  if (std::chrono::seconds{context.header.GetTimestamp()} >
      now + std::chrono::seconds{kTimestampTolerance})
    return Error::Header_BadTimestamp;
  return {};
}

// A header version number MUST meet deployment requirements depending on activated BIPs.
[[nodiscard]] inline Result ValidateVersion(const HeaderValidationContext& context) {
  if (!detail::IsVersionValidAtHeight(context.header.GetVersion(), context.height))
    return Error::Header_BadVersion;
  return {};
}

}  // namespace hornet::consensus::rules
