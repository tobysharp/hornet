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
#include "hornetlib/consensus/types.h"
#include "hornetlib/model/header_context.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/compact_target.h"
#include "hornetlib/protocol/hash.h"

namespace hornet::consensus {
namespace rules {

using ValidateHeaderResult = ErrorStack<HeaderError>;

struct HeaderValidationContext {
  const protocol::BlockHeader& header;
  const model::HeaderContext& parent;
  const HeaderAncestryView& view;
  int height;
};

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
[[nodiscard]] inline ValidateHeaderResult ValidatePreviousHash(
    const HeaderValidationContext& context) {
  if (context.parent.hash != context.header.GetPreviousBlockHash())
    return HeaderError::ParentNotFound;
  return {};
}

// A header MUST satisfy the chain's target proof-of-work.
[[nodiscard]] inline ValidateHeaderResult ValidateProofOfWork(
    const HeaderValidationContext& context) {
  const auto hash = context.header.ComputeHash();
  const auto target = context.header.GetCompactTarget().Expand();
  if (!(hash <= target)) return HeaderError::InvalidProofOfWork;
  return {};
}

// A header timestamp MUST be strictly greater than the median of its 11 ancestors' timestamps.
[[nodiscard]] inline ValidateHeaderResult ValidateMedianTimePast(
    const HeaderValidationContext& context) {
  if (context.header.GetTimestamp() <= context.view.MedianTimePast())
    return HeaderError::BadTimestamp;
  return {};
}

// A header timestamp MUST be less than or equal to network-adjusted time plus 2 hours.
[[nodiscard]] inline ValidateHeaderResult ValidateTimestampCurrent(
    const HeaderValidationContext& context) {
  constexpr int kTimestampTolerance = 2 * 60 * 60;
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  if (std::chrono::seconds{context.header.GetTimestamp()} >
      now + std::chrono::seconds{kTimestampTolerance})
    return HeaderError::BadTimestamp;
  return {};
}

// A header version number MUST meet deployment requirements depending on activated BIPs.
[[nodiscard]] inline ValidateHeaderResult ValidateVersion(
    const HeaderValidationContext& context) {
  if (!detail::IsVersionValidAtHeight(context.header.GetVersion(), context.height))
    return HeaderError::BadVersion;
  return {};
}

}  // namespace rules
}  // namespace hornet::consensus
