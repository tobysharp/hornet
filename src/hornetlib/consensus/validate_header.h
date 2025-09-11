// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <array>

#include "hornetlib/consensus/bips.h"
#include "hornetlib/consensus/difficulty_adjustment.h"
#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/model/header_context.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/compact_target.h"
#include "hornetlib/protocol/hash.h"

namespace hornet::consensus {

namespace constants {
inline constexpr int kTimestampTolerance = 2 * 60 * 60;
}  // namespace constants

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

using HeaderResult = std::variant<model::HeaderContext, HeaderError>;

[[nodiscard]] inline HeaderResult ValidateDownloadedHeader(const model::HeaderContext& parent,
                                                           const protocol::BlockHeader& header,
                                                           const HeaderAncestryView& view) {
  const int height = parent.height + 1;

  // Verify previous hash
  if (parent.hash != header.GetPreviousBlockHash()) return HeaderError::ParentNotFound;

  // Verify PoW target is valid and is achieved by the header's hash.
  const auto hash = header.ComputeHash();
  const auto target = header.GetCompactTarget().Expand();
  if (!(hash <= target)) return HeaderError::InvalidProofOfWork;

  // Verify PoW target obeys the difficulty adjustment rules.
  if (header.GetCompactTarget() != AdjustCompactTarget(height, parent.data, view))
    return HeaderError::BadDifficultyTransition;

  // Verify median of recent timestamps.
  if (header.GetTimestamp() <= view.MedianTimePast()) return HeaderError::BadTimestamp;

  // Verify that the timestamp isn't too far in the future.
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  if (std::chrono::seconds{header.GetTimestamp()} >
      now + std::chrono::seconds{constants::kTimestampTolerance})
    return HeaderError::BadTimestamp;

  // Verify that the version number is allowed at this height.
  if (!detail::IsVersionValidAtHeight(header.GetVersion(), height)) return HeaderError::BadVersion;

  return parent.Extend(header, hash);
}

}  // namespace hornet::consensus
