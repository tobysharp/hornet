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
static constexpr int kAdjustmentInterval = 2016;                // Blocks per difficulty period
static constexpr uint32_t kTargetDuration = 14 * 24 * 60 * 60;  // Two weeks in seconds
static constexpr protocol::Target kTargetLimit =
    "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"_h256;
}  // namespace constants

class DifficultyAdjustment {
 public:
  struct Parameters {
    int blocks_per_period;
    int64_t target_duration;
    protocol::Target pow_limit;

    static Parameters Mainnet() {
      return {.blocks_per_period = constants::kAdjustmentInterval,
              .target_duration = constants::kTargetDuration,
              .pow_limit = constants::kTargetLimit};
    }
  };

  DifficultyAdjustment() : parameters_(Parameters::Mainnet()) {}
  DifficultyAdjustment(const Parameters& parameters) : parameters_(parameters) {}

  [[nodiscard]] int GetBlocksPerPeriod() const {
    return parameters_.blocks_per_period;
  }

  [[nodiscard]] bool IsTransition(int height) const {
    return (height % parameters_.blocks_per_period) == 0;
  }

  [[nodiscard]] protocol::CompactTarget ComputeCompactTarget(int height,
                                                             protocol::CompactTarget prev_bits,
                                                             int64_t period_start_time,
                                                             int64_t period_end_time) const {
    if (!IsTransition(height)) return prev_bits;

    int64_t period_duration = period_end_time - period_start_time;
    period_duration = std::clamp(period_duration, parameters_.target_duration / 4,
                                 parameters_.target_duration * 4);

    const protocol::Target last_target = prev_bits.Expand();
    const protocol::Target next_target =
        (last_target.Value() * period_duration) / parameters_.target_duration;
    const protocol::Target limit_target = std::min(next_target, parameters_.pow_limit);
    return limit_target;
  }

 private:
  Parameters parameters_;
};

}  // namespace hornet::consensus
