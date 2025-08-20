// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <algorithm>
#include <cstdint>

#include "hornetlib/protocol/compact_target.h"
#include "hornetlib/protocol/constants.h"
#include "hornetlib/protocol/target.h"
#include "hornetlib/util/big_uint.h"
#include "hornetlib/util/hex.h"

namespace hornet::consensus {

namespace constants {
inline constexpr int kBlocksPerDifficultyPeriod = 2016;                   // Blocks per difficulty period
inline constexpr int64_t kDifficultyPeriodDuration = 14 * 24 * 60 * 60;   // Two weeks in seconds
inline constexpr protocol::Target kPoWTargetLimit =
    "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"_h256;
}  // namespace constants

[[nodiscard]] inline constexpr bool IsDifficultyTransition(int height) {
  return (height % constants::kBlocksPerDifficultyPeriod) == 0;
}

[[nodiscard]] inline protocol::CompactTarget ComputeCompactTarget(int height,
                                                            protocol::CompactTarget prev_bits,
                                                            int64_t period_start_time,
                                                            int64_t period_end_time) {
  if (!IsDifficultyTransition(height)) return prev_bits;

  int64_t period_duration = period_end_time - period_start_time;
  period_duration = std::clamp(period_duration, constants::kDifficultyPeriodDuration / 4,
                                constants::kDifficultyPeriodDuration * 4);

  const protocol::Target last_target = prev_bits.Expand();
  const protocol::Target next_target =
      (last_target.Value() * period_duration) / constants::kDifficultyPeriodDuration;
  const protocol::Target limit_target = std::min(next_target, constants::kPoWTargetLimit);
  return limit_target;
}

}  // namespace hornet::consensus
