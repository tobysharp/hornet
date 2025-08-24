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
  inline constexpr int kBlocksForMedianTime = 11;
  inline constexpr int kTimestampTolerance = 2 * 60 * 60;
}  // namespace constants

namespace detail {
inline bool IsVersionValidAtHeight(int version, int height) {
  constexpr std::array<BIP, 4> kVersionExpiryToBIP = {
      static_cast<BIP>(-1),  // v0 invalid from genesis, never queried.
      BIP34, BIP66, BIP65    // v1, v2, v3 retired with BIP34, BIP66, BIP65.
    };
  if (version <= 0 || version >= std::ssize(kVersionExpiryToBIP)) return false;
  return !IsBIPEnabledAtHeight(kVersionExpiryToBIP[version], height);
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
  protocol::CompactTarget expected_bits = parent.data.GetCompactTarget();
  if (IsDifficultyTransition(height)) {
    constexpr int blocks_per_period = constants::kBlocksPerDifficultyPeriod;
    Assert(height - blocks_per_period < view.Length());
    const uint32_t period_start_time =
        view.TimestampAt(height - blocks_per_period);             // block[height - 2016].time
    const uint32_t period_end_time = parent.data.GetTimestamp();  // block[height - 1].time
    expected_bits = ComputeCompactTarget(
        height, parent.data.GetCompactTarget(), period_start_time, period_end_time);
  }
  if (expected_bits != header.GetCompactTarget()) return HeaderError::BadDifficultyTransition;

  // Verify median of recent timestamps.
  const auto recent_times = view.LastNTimestamps(constants::kBlocksForMedianTime);
  const uint32_t median_time = recent_times[recent_times.size() / 2];
  if (header.GetTimestamp() <= median_time) return HeaderError::BadTimestamp;

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
