// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <array>

#include "hornetlib/data/header_context.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/compact_target.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/consensus/difficulty_adjustment.h"

namespace hornet::consensus {

namespace constants {
  static constexpr int kBIP34Height = 227931;
  static constexpr int kBIP65Height = 388381;
  static constexpr int kBIP66Height = 363725;
  static constexpr int kBlocksForMedianTime = 11;
  static constexpr int kTimestampTolerance = 2 * 60 * 60;
}  // namespace constants

namespace detail {
    inline bool IsVersionRetiredAtHeight(int version, int height) {
    const std::array<int, 4> kVersionExpiryHeights = {
        0,                        // v0: Invalid from genesis.
        constants::kBIP34Height,  // v1: Retired at BIP34 height.
        constants::kBIP66Height,  // v2: Retired at BIP66 height.
        constants::kBIP65Height   // v3: Retired at BIP65 height.
    };
    return version < 0 || (version < std::ssize(kVersionExpiryHeights) &&
                           height >= kVersionExpiryHeights[version]);
  }
}  // namespace detail

using HeaderResult = std::variant<data::HeaderContext, HeaderError>;

[[nodiscard]] inline HeaderResult ValidateDownloadedHeader(const data::HeaderContext& parent,
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
  DifficultyAdjustment difficulty_adjustment;
  protocol::CompactTarget expected_bits = parent.data.GetCompactTarget();
  if (difficulty_adjustment.IsTransition(height)) {
    const int blocks_per_period = difficulty_adjustment.GetBlocksPerPeriod();
    Assert(height - blocks_per_period < view.Length());
    const uint32_t period_start_time =
        view.TimestampAt(height - blocks_per_period);             // block[height - 2016].time
    const uint32_t period_end_time = parent.data.GetTimestamp();  // block[height - 1].time
    expected_bits = difficulty_adjustment.ComputeCompactTarget(
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
  if (detail::IsVersionRetiredAtHeight(header.GetVersion(), height))
    return HeaderError::BadVersion;

  return parent.Extend(header, hash);
}

}  // namespace hornet::consensus
